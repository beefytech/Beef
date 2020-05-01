using Beefy.widgets;
using Beefy.gfx;
using Beefy.theme.dark;
using System.Collections;
using System;
using Beefy.geom;
using System.Diagnostics;
using Beefy.utils;
using Beefy;
using Beefy.theme;
using Beefy.events;

namespace BeefPerf
{
	class PerfView : Widget
	{
		public enum ScreenItem
		{
			case None;
			case Entry(BPSelection);
			case TrackHeader(TrackNode);
			case TracksTerminator;
			case TrackTreeArrow(TrackNode);
			case TrackMenu(TrackNode);
			case TrackLine(TrackNodeEntry, int32 line);
			case Event(int32 trackIdx, int64 tick);
		}

		class ZoomAnimState
		{
			public int64 mStartTickOffset;
			public int64 mEndTickOffset;

			public double mStartTickScale;
			public double mEndTickScale;

			public float mStartYOffset;
			public float mEndYOffset;

			public float mPct;
		}

		public class HiliteZone
		{
			public int32 mZoneNameId;
			public String mString ~ delete _;

			public this(int32 zoneNameId, String str)
			{
				mZoneNameId = zoneNameId;
				mString = new String(str);
			}

			public bool Matches(int32 zoneNameId, String str)
			{
				if ((mZoneNameId >= 0) && (zoneNameId >= 0))
					return zoneNameId == mZoneNameId;
				return mString == str;
			}
		}

		class ZoomAction : UndoAction
		{
			public PerfView mPerfView;

			public int64 mPrevTickOffset;
			public double mPrevTickScale;

			public int64 mTickOffset;
			public double mTickScale;

			public override bool Undo()
			{
				mPerfView.PhysZoomTo(mPrevTickOffset, mPrevTickScale);
				return true;
			}

			public override bool Redo()
			{
				mPerfView.PhysZoomTo(mTickOffset, mTickScale);
				return true;
			}
		}

		public abstract class TrackNode
		{
			public const int32 cHeaderHeight = 22;

			public String mName ~ delete _;
			public bool mHasRef = true;
			public TrackNodeGroup mParent;
			public float mRot;
			public float mShowChildPct = 1.0f;
			public bool mIsOpen = true;

			public this()
			{
				Open(true, true);
			}

			public void Open(bool open, bool immediate = false)
			{
			    mIsOpen = open;
			    if (immediate)
			        mRot = mIsOpen ? (Math.PI_f / 2) : 0;
			}

			public void Update()
			{
			    float rotSpeed = (0.07f + (10.0f / (GetChildHeight() + 20.0f)));

			    if ((mIsOpen) && (mRot < Math.PI_f / 2))
			    {
			        mRot = Math.Min(Math.PI_f / 2, mRot + rotSpeed);
			    }
			    else if ((!mIsOpen) && (mRot > 0))
			    {
			        mRot = (float)Math.Max(0, mRot - rotSpeed);
			    }

				if (mIsOpen)            
				    mShowChildPct = Math.Min(Math.Sin(mRot) + 0.001f, 1.0f);            
				else            
				    mShowChildPct = Math.Max(1.0f - Math.Sin(Math.PI_f / 2 - mRot) - 0.001f, 0.0f);
			}


			public abstract float GetChildHeight();

			public float GetHeight()
			{
				return cHeaderHeight + GetChildHeight() * mShowChildPct;
			}

			public float GetFinalHeight()
			{
				if (mIsOpen)
					return cHeaderHeight + GetChildHeight();
				else
					return cHeaderHeight;
			}

			public virtual bool WantsClip()
			{
				return (mShowChildPct != 1.0f);
			}
		}

		public class TrackNodeEntry : TrackNode
		{
			public const int32 cLineHeight = 22;
			public const int32 cPeekHeight = 3; // Just to show the nub of any clipped-off entries

			public int32 mTrackIdx;
			public int32 mChildHeight = 8 * cLineHeight + cPeekHeight;

			public override float GetChildHeight()
			{
				return mChildHeight;
			}

			public override bool WantsClip()
			{
				if (base.WantsClip())
					return true;
				return (mChildHeight % cLineHeight) != 0;
			}
		}

		public class TrackNodeGroup : TrackNode
		{
			public List<TrackNode> mChildren = new List<TrackNode>() ~ DeleteContainerAndItems!(_);
			
			public override float GetChildHeight()
			{
				float childHeight = 0;
				for (var entry in mChildren)
				{
					childHeight += entry.GetHeight();
				}
				return childHeight;
			}
		}

		public Board mBoard;
		public BpSession mSession;
		public int64 mTickOffset;
		public int64 mWantTickOffset;
		public double mTickOffsetFrac;
		public double mTickScale = 0.0000015;
		public float mOfsY;
		public float? mWantOfsY;
		public float mDownX;
		public float mDownY;
		public float mDragSize;
		public int32 mDownTick = -1;
		public bool mGotFirstTick;
		public bool mDidDrag;
		public bool mFollowEnd;
		public int32 mMouseStillTicks;
		public int64 mSelectionFromTick;
		public int64 mSelectionToTick;
		public HiliteZone mMouseoverHiliteZone ~ delete _;
		public HiliteZone mProfileHiliteZone ~ delete _;
		public int32 mThreadDataVersion;
		public TrackNodeGroup mNodeRoot = new TrackNodeGroup() ~ delete _;
		public Scrollbar mHorzScrollbar;
		public Scrollbar mVertScrollbar;

		public ScreenItem mSelection;
		ZoomAnimState mZoomAnimState ~ delete _;
		public TrackNodeEntry mDragNodeEntry;
		public float mDragTrackOfsY;

		UndoManager mUndoManager = new UndoManager() ~ delete _;

		public this(Board board, BpSession session)
		{
			mBoard = board;
			mSession = session;
			mTickOffset = mSession.mFirstTick;
			mClipGfx = true;

			mHorzScrollbar = ThemeFactory.mDefault.CreateScrollbar(Scrollbar.Orientation.Horz);
			mHorzScrollbar.Init();
			mHorzScrollbar.mOnScrollEvent.Add(new => HorzEventHandler);
			mHorzScrollbar.mDoAutoClamp = false;
			AddWidgetAtIndex(0, mHorzScrollbar);

			mVertScrollbar = ThemeFactory.mDefault.CreateScrollbar(Scrollbar.Orientation.Vert);
			mVertScrollbar.Init();
			mVertScrollbar.mOnScrollEvent.Add(new => VertEventHandler);
			AddWidgetAtIndex(0, mVertScrollbar);
		}

		void HorzEventHandler(ScrollEvent theEvent)
		{
			mFollowEnd = false;
		    mTickOffsetFrac = 0;
			mTickOffset = (int64)theEvent.mNewPos + mSession.mFirstTick;
		}

		void VertEventHandler(ScrollEvent theEvent)
		{
			mOfsY = (float)-theEvent.mNewPos;
		}

		void ClearSelection()
		{
			mSelectionFromTick = 0;
			mSelectionToTick = 0;
			gApp.mMainFrame.mStatusBar.mSelTime = 0;
			gApp.SetCursor(.Pointer);
		}

		int32 GetXFromTick(int64 tick)
		{
			double xVal = (double)((tick - mTickOffset - mTickOffsetFrac) * mTickScale);
			return (int32)Math.Clamp(xVal, -100000, 100000);
		}

		int64 GetTickFromX(float x)
		{
			double ticksPerPixel = 1.0 / mTickScale;
			return (int64)(x * ticksPerPixel) + mTickOffset;
		}

		int32 GetHeaderHeight()
		{
			return 24;
		}

		Result<float> GetYFromTrackHelper(TrackNodeGroup group, int trackIdx, int trackLine, float startCurY)
		{
			float curY = startCurY;

			for (var trackNode in group.mChildren)
			{
				var subGroup = trackNode as TrackNodeGroup;
				if (subGroup != null)
				{
					var result = GetYFromTrackHelper(subGroup, trackIdx, trackLine, curY + TrackNodeGroup.cHeaderHeight);
					if (result case .Ok)
						return result;
				}
				else
				{
					var trackNodeEntry = (TrackNodeEntry)trackNode;
					if (trackNodeEntry.mTrackIdx == trackIdx)
					{
						return curY + TrackNodeEntry.cHeaderHeight + (float)(trackLine * TrackNodeEntry.cLineHeight);
					}
				}

				curY += trackNode.GetHeight();
			}

			return .Err;
		}

		Result<float> GetYFromTrack(int trackIdx, int trackLine)
		{
			return GetYFromTrackHelper(mNodeRoot, trackIdx, trackLine, 0);
		}

		void EnsureOpen(int trackIdx)
		{
			TrackNodeEntry foundEntry = null;
			WithNodes(mNodeRoot, scope [&] (node) =>
                {
					if (var entry = node as TrackNodeEntry)
					{
						if (entry.mTrackIdx == trackIdx)
							foundEntry = entry;
					}
                });
			if (foundEntry != null)
			{
				if (!foundEntry.mIsOpen)
					foundEntry.Open(true);
				var checkGroup = foundEntry.mParent;
				while (checkGroup != null)
				{
					if (!checkGroup.mIsOpen)
						checkGroup.Open(true);
					checkGroup = checkGroup.mParent;
				}
			}
		}

		int64 GetTickFromX(float x, int64 useTickOffset, double useTickScale)
		{
			double ticksPerPixel = 1.0 / useTickScale;
			return (int64)(x * ticksPerPixel) + useTickOffset;
		}

		bool IsInSelection(float x)
		{
			if (mSelectionToTick == 0)
				return false;

			let mouseTick = GetTickFromX(x);
			int64 minSelTick = Math.Min(mSelectionFromTick, mSelectionToTick);
			int64 maxSelTick = Math.Max(mSelectionFromTick, mSelectionToTick);
			return ((mouseTick >= minSelTick) && (mouseTick <= maxSelTick));
		}

		int32 GetTrackXOfs()
		{
			int32 xOfs = 8;
			//
			return xOfs;
		}

		enum SelectItem
		{
			case Track(Rect rect, int32 zoneNameId, String unformattedName, String formattedName, int64 startTick, int64 endTick, int32 trackIdx, int32 stackDepth);
			case Event(String name, String details, int64 tick, int32 trackIdx);
			case TrackHeader(TrackNode);
		}

		delegate void SelectCallback(SelectItem selectItem);
		
		class DrawContext
		{
			public PerfView mPerfView;

			public float mCursorX;
			public float mCursorY;
			public ScreenItem mSelection;
			public SelectCallback mSelectCallback;

			public int64 mMinTick;
			public int64 mMaxTick;
			public int64 mTargetMinTicks;

			public int64 mDataRead;
			public int32 mSmallCount;
			public bool mDrawEntries = true;
			public int32 mDbgStreamDataCount;

			void DrawRootCmdTarget(Graphics g, ref int numFrames)
			{
				int64 minTick = mPerfView.mTickOffset;
				int64 maxTick = (int64)(minTick + (mPerfView.mWidth / mPerfView.mTickScale) + 1);

				// Adjust for xOfs
				minTick -= (int64)(8 / mPerfView.mTickScale);

				int64 prevFrameTick = 0;

				float xOfs = 0;
				float frameBarAlpha = Math.Clamp(0.6f - ((float)numFrames / (float)mPerfView.mWidth * 7.0f), 0.0f, 0.5f);

				var rootCmdTarget = mPerfView.mSession.mRootCmdTarget;
				for (int streamDataListIdx < rootCmdTarget.mStreamDataList.Count)
				{
					var streamData = rootCmdTarget.mStreamDataList[streamDataListIdx];
					if ((streamData.mSplitTick > 0) && (streamData.mSplitTick < minTick))
						continue; // All data is to the left
					if (streamData.mStartTick > maxTick)
						continue; // All data is to the right

					BPStateContext stateCtx = scope BPStateContext(mPerfView.mSession, streamData);
					
					CmdLoop: while (true)
					{
						switch (stateCtx.GetNextEvent())
						{
						case let .PrevFrameTick(tick):
							prevFrameTick = tick;
						case let .FrameTick(tick):

							float x = mPerfView.GetXFromTick(tick) + xOfs;
							if ((x >= 0) && (x < mPerfView.mWidth))
							{
								numFrames++;
								if (g != null)
								{
									float drawBarAlpha = frameBarAlpha;
									drawBarAlpha *= Math.Clamp(x / 64.0f - 1.0f, 0.0f, 1.0f);

									if (drawBarAlpha > 0)
									{
										using (g.PushColor(Color.Get(0xFFFFFF, drawBarAlpha)))
											g.FillRect(x, 0, 1, mPerfView.mHeight);
									}
								}
							}
							if (x > mPerfView.mWidth)
								break CmdLoop;
							prevFrameTick = tick;
						case .EndOfStream:
							break CmdLoop;
						default:
						}
					}
				}
			}

			bool DrawEvent(Graphics g, int trackIdx, float xOfs, float yOfs, int64 tick, ScreenItem showSelection)
			{
				int32 x = mPerfView.GetXFromTick(tick);
				float drawX = x - 10 + xOfs;
				float drawY = yOfs;
				if (g != null)
				{
					if ((showSelection case .Event(let eventThreadIdx, let eventTick)) && (trackIdx == eventThreadIdx) && (eventTick == tick))
					{
						float scale = 1.25f + (float)Math.Cos(mPerfView.mUpdateCnt * 0.15)*0.08f;
						using (g.PushScale(scale, scale, drawX + 10, drawY - 1 + 10))
							g.Draw(DarkTheme.sDarkTheme.GetImage(.DropShadow), drawX, drawY - 1);
						
					}
					g.Draw(DarkTheme.sDarkTheme.GetImage(.EventInfo), drawX, drawY);
				}

				if ((mCursorX != -1) && (mCursorX >= drawX + 3) && (mCursorX < drawX + 17) &&
				    ((mCursorY == -1) || ((mCursorY >= drawY + 3) && (mCursorY < drawY + 17))))
				{
					return true;
				}
				return false;
			}

			public void DrawEntry(Graphics g, TrackNodeEntry trackNodeEntry, float trackXOfs, float trackYOfs)
			{
				float childHeight = trackNodeEntry.mChildHeight;
				int32 trackIdx = trackNodeEntry.mTrackIdx;

				bool isFirstDrawn = true;
				int64 minDrawDurationAdd = (int64)(1.0 / mPerfView.mTickScale);
				List<BPEntry> entryStack = scope List<BPEntry>(32);

				BpTrack thread = mPerfView.mSession.mThreads[trackIdx];
				int32 xOfs = (int32)0;

				float trackHeight = 20 + childHeight;
				
				float yOfs = trackYOfs;

				if (!mDrawEntries)
					return;
				if (yOfs + trackHeight < 0)
					return; // Off the top
				if (yOfs > mPerfView.mHeight)
					return; // Off the bottom

				int32 threadStartX = mPerfView.GetXFromTick(thread.mCreatedTick);
				int32 threadEndX;
				if (thread.mRemoveTick != 0)
                	threadEndX = mPerfView.GetXFromTick(thread.mRemoveTick);
				else
					threadEndX = mPerfView.GetXFromTick(mPerfView.mSession.mCurTick);
				if (g != null)
				{
					using (g.PushColor(0x10FFFFFF))
						g.FillRect(threadStartX, yOfs, threadEndX - threadStartX + 1, trackHeight + 1);
				}

				int64 minTick = mMinTick;
				int64 maxTick = mMaxTick;
				int32 maxMouseoverDepth = (int32)(childHeight / TrackNodeEntry.cLineHeight);
				int32 maxTrackDepth = (int32)(childHeight / TrackNodeEntry.cLineHeight + 0.9f);
				var client = mPerfView.mSession;

				// When zoomed out we want to avoid drawing lots of small zones in the same place
				int64 minDrawTickForEvent = 0;
				var minDrawEndTickForDepth = scope int64[maxTrackDepth];
				var lastDrawEndXForDepth = scope int64[maxTrackDepth];
				var showSelection = mPerfView.mSelection;

				uint32 color = 0xA000FF00;

				int32 insideSelectAtDepth = -1;

				Header: if (g != null)
				{
					/*using (g.PushColor(0xFF000000))
					{
						g.FillRect(0 + xOfs, 0 + yOfs, 200, 20);
					}
					*/
					/*String str = trackNodeEntry.mName;
					if (str == null)
					{
						str = scope:Header String();
						str.FormatInto("Thread {0}", thread.mNativeThreadId);
					}
					DrawHeader(g, str, trackXOfs + xOfs, yOfs, false);*/
					//g.DrawString(str, 4 + xOfs, 0 + yOfs, .Left, 200 - 8, .Ellipsis);
				}

				String tempDynStr = scope String();
				
				bool autoLOD = true;

				bool dbgColorDataList = false;

				BpStreamLOD streamLOD = null;
				if (mPerfView.mHasFocus)
				{
					if (mPerfView.mWidgetWindow.IsKeyDown((KeyCode)'0'))
						autoLOD = false;
					else if (mPerfView.mWidgetWindow.IsKeyDown((KeyCode)'1'))
					{
						streamLOD = thread.mStreamLODs[0];
						autoLOD = false;
					}
					else if (mPerfView.mWidgetWindow.IsKeyDown((KeyCode)'2'))
					{
						streamLOD = thread.mStreamLODs[1];
						autoLOD = false;
					}
					else
					{
						//streamLOD = thread.mStreamLODs[1];
						//autoLOD = false;
					}

					if (mPerfView.mWidgetWindow.IsKeyDown((KeyCode)'D'))
						dbgColorDataList = true;
				}

				for (int streamDataListIdx < thread.mStreamDataList.Count)
				{
					bool isLOD = false;
					var streamData = thread.mStreamDataList[streamDataListIdx];
					var fullStreamData = streamData;
					if ((streamData.mSplitTick > 0) && (streamData.mSplitTick < mMinTick))
						continue; // All data is to the left
					if (streamData.mStartTick > mMaxTick)
						continue; // All data is to the right

					if (dbgColorDataList)
					{
						mDbgStreamDataCount++;
						color = (uint32)((mDbgStreamDataCount * 0x12345678) ^ (mDbgStreamDataCount * 0x17654321)) & 0x00FFFFFF | 0xE0000000;
					}

					if (autoLOD)
					{
						double bestErrorPct = Double.MaxValue;
						BpStreamData bestLODStreamData = null;
						bool foundMoreDetailed = false;

						for (int32 lodIdx < (int32)thread.mStreamLODs.Count)
						{
							var checkStreamLOD = thread.mStreamLODs[lodIdx];
							if (streamDataListIdx >= checkStreamLOD.mStreamDataList.Count)
								continue;
							var checkStreamData = checkStreamLOD.mStreamDataList[streamDataListIdx];
							double errorPct = Double.MaxValue;
							if (mTargetMinTicks > checkStreamData.mMinTicks)
							{
								errorPct = (double)mTargetMinTicks / checkStreamData.mMinTicks;
								foundMoreDetailed = true;
							}
							else
							{
								// Having too little information is way worse than too much
								errorPct = ((double)checkStreamData.mMinTicks / mTargetMinTicks) * 8;
							}

							if (errorPct < bestErrorPct)
							{
								bestErrorPct = errorPct;
								bestLODStreamData = checkStreamData;
								isLOD = true;
							}
						}

						if ((!foundMoreDetailed) && (bestErrorPct > 32.0))
						{
							// Use the full version
							bestLODStreamData = null;
						}

						if (bestLODStreamData != null)
							streamData = bestLODStreamData;
					}
					else if ((streamLOD != null) && (streamDataListIdx < streamLOD.mStreamDataList.Count))
					{
						var lodStreamData = streamLOD.mStreamDataList[streamDataListIdx];
						if (lodStreamData != null)
						{
							streamData = lodStreamData;
							isLOD = true;
						}
					}

					entryStack.Clear();

					BPStateContext stateCtx = scope BPStateContext(mPerfView.mSession, streamData);

					String tempStr = scope String(128);
					CmdLoop: while (true)
					{
						switch (stateCtx.GetNextEvent())
						{
						case let .Enter(startTick, strIdx):
							BPEntry entry;
							entry.mStartTick = startTick;
							entry.mZoneNameId = strIdx;
							entry.mParamsReadPos = stateCtx.ReadPos;

							if ((showSelection case .Entry(let bpSelection)) && (entry.mStartTick == bpSelection.mTickStart) && (entryStack.Count == bpSelection.mDepth))
							{
								insideSelectAtDepth = (int32)entryStack.Count;
							}

							if ((startTick > maxTick) && (entryStack.Count == 0) && (!isLOD))
							{
								// Off right side
								// We can only fully terminate if we don't have any other stack entries that may need terminating
								// LOD entries may have SmallEntries pending so we can't terminate early
								break CmdLoop; 
							}
							entryStack.Add(entry);

						case let .Leave(endTick):
							let entry = entryStack.PopBack();
							int32 stackDepth = (int32)entryStack.Count;

							if (stackDepth == insideSelectAtDepth)
								insideSelectAtDepth = -1;

							if (stackDepth >= maxTrackDepth)
								continue; // Too deep to draw

							if (endTick < minTick)
								continue; // Off left side
							if (entry.mStartTick > maxTick)
								continue; // Off right side 

							if ((!isFirstDrawn) && (entry.mStartTick < streamData.mStartTick))
								continue; // Would have already been drawn by a previous streamData

							if (endTick < minDrawEndTickForDepth[stackDepth])
								continue; // Too close to prev depth

							minDrawEndTickForDepth[stackDepth] = endTick + minDrawDurationAdd;
							
							int32 endX = mPerfView.GetXFromTick(endTick) + xOfs + 1;
							int32 startX = mPerfView.GetXFromTick(entry.mStartTick) + xOfs;

							if (startX <= lastDrawEndXForDepth[stackDepth])
							{
								startX = (int32)lastDrawEndXForDepth[stackDepth] + 1; // Leave empty space
								if (startX >= endX)
									continue;
							}
							else if (startX == endX)
								endX++; // Make at least 1 pixel wide
							lastDrawEndXForDepth[stackDepth] = endX;

							float w = endX - startX;
							Rect rect = Rect((int32)startX, 22 + stackDepth*22 + yOfs, w, 20);

							String str;
							int32 paramsSize;
							int32 paramReadPos = entry.mParamsReadPos;
							if (entry.mZoneNameId < 0)
							{
								int32 nameLen = -entry.mZoneNameId;
								str = tempDynStr;
								str.Reference((char8*)stateCtx.mReadStart + paramReadPos - nameLen, nameLen, 0);
								paramsSize = -1;
							}
							else
							{
								let zoneName = client.mZoneNames[entry.mZoneNameId];
								str = zoneName.mName;
								paramsSize = zoneName.mParamsSize;
							}

							String unformattedStr = str;

							Rect strRect = rect;
							if (strRect.mX < 0)
							{
								strRect.mWidth += strRect.mX;
								strRect.mX = 0;
							}
							bool wantsDrawStr = strRect.mWidth > 16;

							bool isMouseover = false;
							if ((stackDepth < maxMouseoverDepth) &&
							    (mCursorX != -1) && (mCursorX >= rect.mX) && (mCursorX < rect.mX + rect.mWidth) &&
							    ((mCursorY == -1) || ((mCursorY >= rect.mY) && (mCursorY < rect.mY + rect.mHeight))))
							{
								wantsDrawStr = true;
								isMouseover = true;
							}

							if (paramsSize != 0)
							{
								int32 prevReadPos = stateCtx.ReadPos;
								stateCtx.ReadPos = paramReadPos;
								defer { stateCtx.ReadPos = prevReadPos; }

								if (paramsSize == -1)
								{
									paramsSize = (int32)stateCtx.ReadSLEB128();
								}

								if (paramsSize > 0)
								{
									String name = str;
									tempStr.Clear();
									str = tempStr;
									for (int idx = 0; idx < name.Length; idx++)
									{
										let c = name[idx];
										if (c == '%')
										{
											let cNext = name[idx + 1];
											idx++;

											if (cNext == '%')
											{
												str.Append('%');
											}
											else if (cNext == 'd')
											{
												int32 val = 0;
												stateCtx.Read(&val, 4);
												val.ToString(str);
											}
											else if (cNext == 'f')
											{
												float val = 0;
												stateCtx.Read(&val, 4);
												val.ToString(str);
											}
											else if (cNext == 's')
											{
												while (true)
												{
													char8 paramC = (char8)stateCtx.Read();
													if (paramC == 0)
														break;
													str.Append(paramC);
												}
											}
										}
										else
											str.Append(c);
									}
								}
							}

							if (isMouseover)
							{
								BPSelection newSel;
								newSel.mThreadIdx = trackIdx;
								newSel.mTickStart = entry.mStartTick;
								newSel.mTickEnd = endTick;
								newSel.mDepth = stackDepth;
								mSelection = .Entry(newSel);

								if (mSelectCallback != null)
								{
									mSelectCallback(.Track(rect, entry.mZoneNameId, unformattedStr, str, entry.mStartTick, endTick, trackIdx, stackDepth));
								}
							}

							if (g != null)
							{
								//using (g.PushColor(0xFF00A000))
								using (g.PushColor(color))
									g.FillRect(rect.mX, rect.mY, rect.mWidth, rect.mHeight);

								//Rect strRect = default(Rect);
								//strRect.SetIntersectionOf(rect, Rect(0, 0, mWidth, mHeight));

								if (wantsDrawStr)
									g.DrawString(str, strRect.mX + 3, strRect.mY, .Left, strRect.mWidth - 4, .Truncate);

								if ((showSelection case .Entry(let bpSelection)) && (entry.mStartTick == bpSelection.mTickStart) && (stackDepth == bpSelection.mDepth))
								{
									for (int32 i = 0; i < 3; i++)
									{
										float alpha = (0.8f - i*0.25f) + (float)Math.Cos(mPerfView.mUpdateCnt * 0.15)*(0.0f + i*0.1f);

										using (g.PushColor(Color.Get(alpha)))
										{
											Rect outlineRect = rect;
											outlineRect.Inflate(-i, -i);
											g.OutlineRect(outlineRect.mX, outlineRect.mY, outlineRect.mWidth, outlineRect.mHeight, 1);
										}
									}
								}

								bool doHilite = false;
								if ((mPerfView.mMouseoverHiliteZone != null) && (mPerfView.mMouseoverHiliteZone.Matches(entry.mZoneNameId, unformattedStr)))
									doHilite = true;

								if ((insideSelectAtDepth != -1) && (mPerfView.mProfileHiliteZone != null) && (mPerfView.mProfileHiliteZone.Matches(entry.mZoneNameId, unformattedStr)))
									doHilite = true;

								if (doHilite)
								{
									float alpha = Math.Min(0.2f + rect.mWidth * 0.1f, 0.8f);
									using (g.PushColor(Color.Get(alpha)))
									{
										Rect outlineRect = rect;
										BPUtils.DrawOutlineHilite(g, outlineRect.mX, outlineRect.mY, outlineRect.mWidth, outlineRect.mHeight);
									}
								}
							}

						case let .LODSmallEntry(startTick, endTick, stackDepth):
							if (g != null)
							{
								if (stackDepth >= maxTrackDepth)
									continue; // Too deep to draw

								if (endTick < minTick)
									continue; // Off left side
								if (startTick > maxTick)
									continue; // Off right side 

								int32 endX = mPerfView.GetXFromTick(endTick) + xOfs + 1;
								int32 startX = mPerfView.GetXFromTick(startTick) + xOfs;

								if (startX <= lastDrawEndXForDepth[stackDepth])
								{
									startX = (int32)lastDrawEndXForDepth[stackDepth] + 1; // Leave empty space
									if (startX >= endX)
										continue;
								}
								else if (startX == endX)
									endX++; // Make at least 1 pixel wide
								// Remove empty bar space at end to avoid double-spacing issue
								if ((startX - endX) % 2 == 0)
									endX--;
								lastDrawEndXForDepth[stackDepth] = endX;

								float w = endX - startX;

								Rect rect = Rect((int32)startX, 22 + stackDepth*22 + yOfs, w, 20);

								bool debugColor = false;
								if (!debugColor)
								{
									using (g.PushColor(color))
									{
										for (int32 x = (int32)rect.mX; x < (int32)(rect.mX + rect.mWidth); x += 2)
										{
											//if (x % 2 == 0)
												g.FillRect(x, rect.mY, 1, rect.mHeight);
										}
									}
								}
								else
								{
									uint32 dbgColor = (uint32)((mSmallCount * 0x12345678) ^ (mSmallCount * 0x17654321)) & 0x00FFFFFF | 0xE0000000;
									using (g.PushColor(dbgColor))
										g.OutlineRect(rect.mX, rect.mY, rect.mWidth, rect.mHeight);
								}

								mSmallCount++;
							}
						case let .Event(tick, name, details):

							if (tick < minDrawTickForEvent)
								continue; // Too close to prev depth
							minDrawTickForEvent = tick + minDrawDurationAdd * 3;

							if (DrawEvent(g, trackIdx, xOfs, yOfs, tick, showSelection))
							{
								if (mSelectCallback != null)
								{
									String nameStr = scope String();
									nameStr.Reference(name);
									String detailsStr = scope String();
									detailsStr.Reference(details);
									mSelection = .Event(trackIdx, tick);
									mSelectCallback(.Event(nameStr, detailsStr, tick, trackIdx));
								}
							}
						case let .LODEvent(tick, paramsOfs):
							if (tick < minDrawTickForEvent)
								continue; // Too close to prev depth
							minDrawTickForEvent = tick + minDrawDurationAdd * 3;

							if (DrawEvent(g, trackIdx, xOfs, yOfs, tick, showSelection))
							{
								if (mSelectCallback != null)
								{
									uint8* readPtr = &fullStreamData.mBuffer[0] + paramsOfs;

									char8* name = (char8*)readPtr;
									int32 nameLen = String.StrLen(name);
									readPtr += nameLen + 1;
									char8* details = (char8*)readPtr;

									String nameStr = scope String();
									nameStr.Reference(name);
									String detailsStr = scope String();
									detailsStr.Reference(details);
									mSelectCallback(.Event(nameStr, detailsStr, tick, trackIdx));
								}
							}
						case .EndOfStream:
							break CmdLoop;
						default:
						}
					}

					mDataRead += stateCtx.ReadPos;

					isFirstDrawn = false;
				}

				/*for (var entry in ref thread.mEntries)
				{
					float startX = GetXFromTick(entry.mTickStart);
					float endX = GetXFromTick(entry.mTickEnd);

					Rect rect = Rect(startX, 22, endX - startX, 22);
					g.FillRect(rect.mX, rect.mY, rect.mWidth, rect.mHeight);
				}*/
			}

			void DrawHeader(Graphics g, TrackNode trackNode, String name, float xOfs, float yOfs)
			{
				var font = DarkTheme.sDarkTheme.mSmallFont;
				bool isGroup = (trackNode == null) || (trackNode is TrackNodeGroup);
				float rectWidth = 80;
                if (name != null)
                    rectWidth = Math.Min(font.GetWidth(name) + 56, 300);

				/*using (g.PushColor(isGroup ? 0x80A0A0A0 : 0x80FFFFFF))
					g.FillRect(rectWidth + xOfs, 10 + yOfs, mPerfView.mWidth, 2);*/

				float drawY = yOfs;

				if (mCursorX != -1)
				{
					if (name == null)
					{
						var rect = Rect(xOfs, drawY, rectWidth, 20);
						if (rect.Contains(mCursorX, mCursorY))
						{
							mSelection = .TracksTerminator;
						}
					}
					else
					{
						var rect = Rect(xOfs, drawY, rectWidth, 20);
						if (rect.Contains(mCursorX, mCursorY))
						{
							mSelection = .TrackHeader(trackNode);
							if (mSelectCallback != null)
								mSelectCallback(.TrackHeader(trackNode));
						}

						rect = Rect(xOfs, drawY, 20, 20);
						if (rect.Contains(mCursorX, mCursorY))
						{
							mSelection = .TrackTreeArrow(trackNode);
						}

						rect = Rect(rectWidth - 24 + xOfs, drawY, 24, 20);
						if (rect.Contains(mCursorX, mCursorY))
						{
							mSelection = .TrackMenu(trackNode);
						}
					}
				}

				if (g == null)
					return;

				using (g.PushColor(isGroup ? 0xFFA0A0A0 : 0xFFFFFFFF))
					g.DrawBox(DarkTheme.sDarkTheme.GetImage(.WhiteBox), 0 + xOfs, 0 + drawY, rectWidth, 20);
				if (name != null)
				{
					g.DrawString(name, 20 + xOfs, 0 + drawY, .Left, rectWidth - 20 - 24, .Ellipsis);
					g.Draw(DarkTheme.sDarkTheme.GetImage(.DropMenuButton), rectWidth - 24 + xOfs, 0 + drawY);
				}

				if (trackNode != null)
				{
					Matrix matrix = Matrix.IdentityMatrix;
					matrix.Translate(-10, -10);
					matrix.Rotate(trackNode.mRot);
					matrix.Translate(xOfs + 10, drawY + 10);

					using (g.PushMatrix(matrix))
					    g.Draw(DarkTheme.sDarkTheme.mTreeArrow);
				}

			}

			public void DrawTrackNodes(Graphics g, TrackNodeGroup group, float inXOfs, float inYOfs)
			{
				float xOfs = inXOfs;
				float yOfs = inYOfs;
				
				if (group.mName != null)
				{
					xOfs += 20;
					yOfs += 22;
				}

				for (var node in group.mChildren)
				{
					float nodeHeight = node.GetHeight();
					var trackNodeEntry = node as TrackNodeEntry;
					
					if (node.mShowChildPct > 0)
					{
						if ((node.WantsClip()) && (g != null))
							g.PushClip(0, yOfs, mPerfView.mWidth, nodeHeight - 1);
						if (trackNodeEntry != null)
							DrawEntry(g, trackNodeEntry, xOfs, yOfs);
						else
							DrawTrackNodes(g, (TrackNodeGroup)node, xOfs, yOfs);
						if ((node.WantsClip()) && (g != null))
							g.PopClip();
					}
					yOfs += nodeHeight;
				}

				if (group.mName != null)
				{
					if (g != null)
					{
						/*DrawHeader(g, group.mName, selfXOfs, selfYOfs, true);

						float height = group.GetHeight();

						using (g.PushColor(0xFF303030))
							g.FillRect(1 + selfXOfs, 20 + selfYOfs, 3, height - 32);*/
					}
				}
			}

			public void DrawTrackNodesTop(Graphics g, TrackNodeGroup group, float inXOfs, float inYOfs)
			{
				float xOfs = inXOfs;
				float yOfs = inYOfs;

				float selfXOfs = xOfs;
				float selfYOfs = yOfs;

				if (group.mName != null)
				{
					xOfs += 20;
					yOfs += 22;
				}

				if (group.mShowChildPct > 0)
				{
					for (var node in group.mChildren)
					{
						float nodeHeight = node.GetHeight();
						
						if ((node.WantsClip()) && (g != null))
							g.PushClip(0, yOfs, mPerfView.mWidth, nodeHeight);
						var trackNodeEntry = node as TrackNodeEntry;
						if (trackNodeEntry != null)
						{
							Header:
							{
								var thread = mPerfView.mSession.mThreads[trackNodeEntry.mTrackIdx];
								String str = trackNodeEntry.mName;
								if (str == null)
								{
									str = scope:Header String();
									str.AppendF("Thread {0}", thread.mNativeThreadId);
								}
								
								DrawHeader(g, trackNodeEntry, str, xOfs, yOfs);

								if ((mCursorX != -1) && (!mDrawEntries))
								{
									int32 clientY = (int32)(mCursorY - yOfs - TrackNodeEntry.cHeaderHeight);
									if ((clientY >= 0) && (clientY < nodeHeight))
									{
										int32 stackDepth = clientY / TrackNodeEntry.cLineHeight;
										mSelection = .TrackLine(trackNodeEntry, stackDepth);
									}
								}

								if (g != null)
								{
									if (nodeHeight > 24)
									{
										using (g.PushColor(0x60A0A0A0))
										{
											g.FillRect(1 + xOfs, 20 + yOfs, 3, nodeHeight - 24);
											//g.FillRect(1 + xOfs, nodeHeight - 4 - 3 + yOfs, 8, 3);
										}
									}
								}

								//g.DrawString(str, 4 + xOfs, 0 + yOfs, .Left, 200 - 8, .Ellipsis);
							}
						}
						else
							DrawTrackNodesTop(g, (TrackNodeGroup)node, xOfs, yOfs);
						if ((node.WantsClip()) && (g != null))
							g.PopClip();

						yOfs += nodeHeight;
						//yOfs += trackNodeGroup.GetHeight();
					}
				}

				//yOfs += group.GetHeight();

				if (group.mName != null)
				{
					DrawHeader(g, group, group.mName, selfXOfs, selfYOfs);
					if (g != null)
					{
						float height = group.GetHeight();
						using (g.PushColor(0x60606060))
							g.FillRect(1 + selfXOfs, 20 + selfYOfs, 3, height - 24);
					}
				}
			}

			public void DrawEntries(Graphics g)
			{
				g?.SetFont(DarkTheme.sDarkTheme.mSmallFont);

				mMinTick = mPerfView.mTickOffset;
				mMaxTick = (int64)(mMinTick + (mPerfView.mWidth / mPerfView.mTickScale) + 1);

				if (mCursorX != -1)
				{
					mMinTick = mPerfView.GetTickFromX(mCursorX);
					mMaxTick = mMinTick;
				}

				// Adjust for xOfs
				mMinTick -= (int64)(8 / mPerfView.mTickScale);
				//maxTick -= (int64)(20 / mTickScale);

				// For testing clipping
				//minTick += (int64)(10 / mTickScale);
				//maxTick -= (int64)(20 / mTickScale);

				//int64 minDuration = (int64)(0.3 / mTickScale);

				
				mTargetMinTicks = (int64)(1.0 / mPerfView.mTickScale);

				if (g != null)
				{
					//int32 xOfs = 8;

					int numFrames = 0;
					DrawRootCmdTarget(null, ref numFrames);
					DrawRootCmdTarget(g, ref numFrames);

					if (mPerfView.mSelectionFromTick != 0)
					{
						int32 fromX = mPerfView.GetXFromTick(mPerfView.mSelectionFromTick);
						int32 toX = mPerfView.GetXFromTick(mPerfView.mSelectionToTick);
						using (g.PushColor(0x80404080))
							g.FillRect(fromX, 0, toX - fromX, mPerfView.mHeight);
					}
				}

				// Draw track nodes
				{
					if (g != null)
						g.PushClip(0, 0, mPerfView.mWidth - 18, mPerfView.mHeight - 18);

					float yOfsStart;
					if (mPerfView.mOfsY > 0)
						yOfsStart = (int32)(0.5f * (float)Math.Pow(mPerfView.mOfsY, 0.8));
					else
						yOfsStart = (int32)mPerfView.mOfsY;
					yOfsStart += 16;
					float yOfs = yOfsStart;
					DrawTrackNodes(g, mPerfView.mNodeRoot, 4, yOfs);
					yOfs = yOfsStart;
					if (g != null)
					{
						/*uint32 colorLeft = 0xFF202020;
						uint32 colorRight = 0x00202020;
						g.FillRectGradient(0, 0, 60, mPerfView.mHeight, colorLeft, colorLeft, colorLeft, colorLeft);
						g.FillRectGradient(60, 0, 16, mPerfView.mHeight, colorLeft, colorRight, colorLeft, colorRight);*/
					}
					DrawTrackNodesTop(g, mPerfView.mNodeRoot, 4, yOfs);
					DrawHeader(g, null, null, 4, mPerfView.mNodeRoot.GetHeight() - 22 + yOfs);
					/*if (g != null)
					{
						using (g.PushColor(0x60A0A0A0))
							g.FillRect(4, mPerfView.mNodeRoot.GetHeight() - 22 + yOfsStart, 64, 3);
					}*/

					if (g != null)
						g.PopClip();
				}

				/*if (mUpdateCnt % 120 == 0)
					Debug.WriteLine("DataRead: {0}", dataRead);*/

				if ((g != null) && (mPerfView.mSession.mFirstTick != 0))
				{
					using (g.PushColor(0xFF202020))
						g.FillRect(0, 0, mPerfView.mWidth, 15);

					double usPerTick = mPerfView.mSession.GetTicksToUSScale();

					double pixelsPerUS = 1 / usPerTick * mPerfView.mTickScale;

					int64 usPerSeg = 1;
					int segSubTickMod = 1;

					// Calculate usPerSeg
					do
					{
						double pixelsPerUSLeft = pixelsPerUS;
						while (true)
						{
							if (pixelsPerUSLeft > 100.00)
							{
								segSubTickMod = 1;
								break;
							}
							else if (pixelsPerUSLeft > 50.00)
							{
								segSubTickMod = 2;
								break;
							}
							else if (pixelsPerUSLeft > 20.00)
							{
								segSubTickMod = 5;
								break;
							}
							else if (pixelsPerUSLeft > 10.0)
							{
								segSubTickMod = 10;
								break;
							}
							
							pixelsPerUSLeft *= 10;
							usPerSeg *= 10;
						}
					}
					  
					//int64 dispScale = 1000; //ms

					double ticksPerSeg = usPerSeg / usPerTick;

					int64 relTick = mPerfView.mTickOffset - mPerfView.mSession.mFirstTick;

					double leftSegF = (relTick / ticksPerSeg) + (mPerfView.mTickOffsetFrac / ticksPerSeg);
					int64 leftSeg = (int64)leftSegF;
					leftSeg -= segSubTickMod;

					double leftSegFrac = leftSegF - leftSeg;

					double pixelsPerSeg = (usPerSeg / usPerTick * mPerfView.mTickScale);
					int64 maxDispSegs = (int64)(mPerfView.mWidth / pixelsPerSeg) + 2 + segSubTickMod;

					for (int64 segOfs = 0; segOfs < maxDispSegs; segOfs++)
					{
						float xOfs = (int32)(0 + (segOfs - leftSegFrac) * pixelsPerSeg);
						int64 segIdx = leftSeg + segOfs;

						if ((segSubTickMod > 0) && (segIdx % segSubTickMod != 0))
						{
							g.FillRect(xOfs, 13, 1, 2);
							continue;
						}


						g.FillRect(xOfs, 0, 1, 15);

						String str = scope String(64);

						int64 timeUS = segIdx * usPerSeg;

						//0:00:03.123456 {with zeroes trimmed}

						str.AppendF("{0}:{1:02}:", (int32)((timeUS / 60 / 60 / 1000000)), (int32)((timeUS / 60 / 1000000) % 60));
						using (g.PushColor(0x80FFFFFF))
							g.DrawString(str, xOfs + 4, -2);

						xOfs += g.mFont.GetWidth(str);

						str.Clear();
						str.AppendF("{0:02}.{1:06}", (int32)((timeUS / 1000000) % 60), (int32)((timeUS % 1000000)));

						int maxTrim = 0;
						if (pixelsPerUS < 0.012)
							maxTrim = 5;
						else if (pixelsPerUS < 0.12)
							maxTrim = 3;

						for (int i = 0; i < maxTrim; i++)
						{
							if (str.EndsWith("0"))
								str.Remove(str.Length - 1);
						}

						g.DrawString(str, xOfs + 4, -2);
					}
				}
			}
		}
		
		ScreenItem DrawEntries(Graphics g, float cursorX, float cursorY, SelectCallback selectCallback = null)
		{
			DrawContext dc = scope DrawContext();
			dc.mPerfView = this;
			dc.mCursorX = cursorX;
			dc.mCursorY = cursorY;
			dc.mSelectCallback = selectCallback;
			dc.DrawEntries(g);
			return dc.mSelection;
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);
			DrawEntries(g, -1, -1);
		}

		ScreenItem GetSelection(float x, float y)
		{
			return DrawEntries(null, x, y);
		}

		ScreenItem GetScreenItemAt(float x, float y, bool allowEntries)
		{
			DrawContext dc = scope DrawContext();
			dc.mPerfView = this;
			dc.mCursorX = x;
			dc.mCursorY = y;
			dc.mDrawEntries = allowEntries;
			dc.DrawEntries(null);
			return dc.mSelection;
		}

		void WithNodes(TrackNode node, Action<TrackNode> act)
		{
			act(node);
			var group = node as TrackNodeGroup;
			if (group == null)
				return;
			for (var child in group.mChildren)
				WithNodes(child, act);
		}

		delegate void TrackNodePositionDelegate(TrackNode node, float xOfs, float yOfs, float childHeight);
		void WithNodesPositions(TrackNodeGroup group, TrackNodePositionDelegate dlg, ref float xOfs, ref float yOfs)
		{
			/*if (group.mName != null)
			{
				float xOfs = 8;
				using (g.PushColor(0xFF000000))
				{
					g.FillRect(0 + xOfs, 0 + yOfs, 200, 20);
				}

				g.DrawString(group.mName, 4 + xOfs, 0 + yOfs, .Left, 200 - 8, .Ellipsis);
				yOfs += 22;
			}

			for (var node in group.mChildren)
			{
				var trackNodeEntry = node as TrackNodeEntry;
				if (trackNodeEntry != null)
				{
					float trackHeight = 10 * 20;
					DrawEntry(g, trackNodeEntry.mTrackIdx, yOfs, trackHeight);
					yOfs += trackHeight;
					continue;
				}

				var trackNodeGroup = (TrackNodeGroup)node;
				DrawTrackNodes(g, trackNodeGroup, ref yOfs);
			}*/
		}

		void WithNodesPositions(TrackNode node, TrackNodePositionDelegate dlg)
		{
			//float xOfs = 8;
			//float yOfs = 16;
			//WithNodesPositions()
		}

		void ParseName(TrackNodeGroup trackNodeGroup, String name, int trackIdx)
		{
			trackNodeGroup.mHasRef = true;

			int slashIdx = -1;
            if (name != null)
                slashIdx = name.IndexOf('/');
			if (slashIdx == -1)
			{
				for (int idx < trackNodeGroup.mChildren.Count)
				{
					var trackNodeEntry = trackNodeGroup.mChildren[idx] as TrackNodeEntry;
					if (trackNodeEntry != null)
					{
						if (trackNodeEntry.mTrackIdx == trackIdx)
						{
							trackNodeEntry.mHasRef = true;
							return;
						}
					}
				}

				var trackNodeEntry = new TrackNodeEntry();
				if (name != null)
					trackNodeEntry.mName = new String(name);
				trackNodeEntry.mTrackIdx = (int32)trackIdx;
				trackNodeEntry.mParent = trackNodeGroup;
				trackNodeGroup.mChildren.Add(trackNodeEntry);
			}
			else
			{
				String groupName = scope String(name, 0, slashIdx);
				String subName = scope String(name, slashIdx + 1);
				
				for (int idx < trackNodeGroup.mChildren.Count)
				{
					var subTrackNodeGroup = trackNodeGroup.mChildren[idx] as TrackNodeGroup;
					if (subTrackNodeGroup != null)
					{
						if (String.Equals(subTrackNodeGroup.mName, groupName, .OrdinalIgnoreCase))
						{
							ParseName(subTrackNodeGroup, subName, trackIdx);
							return;
						}
					}
				}

				var subTrackNodeGroup = new TrackNodeGroup();
				subTrackNodeGroup.mName = new String(groupName);
				subTrackNodeGroup.mParent = trackNodeGroup;
				trackNodeGroup.mChildren.Add(subTrackNodeGroup);
				ParseName(subTrackNodeGroup, subName, trackIdx);
			}
		}

		void UpdateThreads()
		{
			mThreadDataVersion = mSession.mThreadDataVersion;
			WithNodes(mNodeRoot, scope (node) => { node.mHasRef = false; });

			for (var thread in mSession.mThreads)
			{
				ParseName(mNodeRoot, thread.mName, @thread.Index);
			}

			WithNodes(mNodeRoot, scope (node) =>
				{
					if (!node.mHasRef)
					{
						node.mParent.mChildren.Remove(node);
						delete node;
						return;
					}

					var nodeGroup = node as TrackNodeGroup;
					if (nodeGroup != null)
					{
						nodeGroup.mChildren.Sort(scope [&] (lhs, rhs) =>
                            {
								String lhsName = lhs.mName;
								String rhsName = rhs.mName;

								// Put named tracks first
								if (lhsName == null)
								{
									if (rhsName != null)
										return 1;
									var lhsEntry = (TrackNodeEntry)lhs;
									var rhsEntry = (TrackNodeEntry)rhs;
									return mSession.mThreads[lhsEntry.mTrackIdx].mNativeThreadId -
										mSession.mThreads[rhsEntry.mTrackIdx].mNativeThreadId;
								}
								if (rhsName == null)
									return -1;

								return String.Compare(lhs.mName, rhs.mName, true);
                            });
					}
				});
		}

		public override void Update()
		{
			base.Update();

			if (mThreadDataVersion != mSession.mThreadDataVersion)
			{
				UpdateThreads();
				mSession.mThreadDataVersion = mThreadDataVersion;
			}

			WithNodes(mNodeRoot, scope (node) =>
                {
					node.Update();
                });

			if (!mMouseOver)
				mMouseStillTicks = 0;

			if ((!mGotFirstTick) && (mSession.mFirstTick != 0))
			{
				mTickOffset = mSession.mFirstTick;
				mGotFirstTick = true;
			}

			if (mMouseFlags == 0)
			{
				if (mOfsY > 0)
					mOfsY = Math.Max(mOfsY * 0.75f - 10, 0);
			}

			if ((mFollowEnd) && (mGotFirstTick))
			{
				// Kinda ugly way to do this...
				//mTickOffset = Int64.MaxValue;
				//ClampView();

				int64 maxOffset = GetMaxOffset();
				//mWantTickOffset += ;

				int64 ticksPerUpdate = (int64)((gApp.mTimePerFrame * 1000000) / mSession.GetTicksToUSScale());
				mWantTickOffset += ticksPerUpdate;

				int64 endDiffTicks = maxOffset - mWantTickOffset;
				if (endDiffTicks < 0)
					mWantTickOffset = maxOffset;
				else if (endDiffTicks > ticksPerUpdate)
					mWantTickOffset = maxOffset - ticksPerUpdate;

				/*if (mUpdateCnt % 60 == 0)
				{
					int64 endDiffTicks = mWantTickOffset - GetMaxOffset();
					int32 endDeltaMS = (int32)(endDiffTicks * mClient.GetTicksToUSScale() / 1000);
					Debug.WriteLine("End Diff: {0}", endDeltaMS);
				}*/
			}

			if (mWantTickOffset != 0)
			{
				//int64 linearAdd = (int64)(10000 / mClient.GetTicksToUSScale());

				int64 linearAdd = (int64)(3 / mTickScale);

				int64 delta = mWantTickOffset - mTickOffset;
				int64 deltaAdd = delta / 5;
				deltaAdd += Math.Sign(delta) * linearAdd;

				if (Math.Abs(deltaAdd) >= Math.Abs(delta))
				{
					mTickOffset = mWantTickOffset;
					if (!mFollowEnd)
						mWantTickOffset = 0;
				}
				else
				{
					mTickOffset += deltaAdd;
				}
				ClampView();
			}

			if (mWantOfsY.HasValue)
			{
				float delta = mWantOfsY.Value - mOfsY;
				float deltaAdd = delta / 5;
				deltaAdd += Math.Sign(delta) * 1;

				if (Math.Abs(deltaAdd) >= Math.Abs(delta))
				{
					mOfsY = mWantOfsY.Value;
					mWantOfsY = null;
				}
				else
				{
					mOfsY += deltaAdd;
				}
				ClampView();
			}

			if (mZoomAnimState != null)
			{
				mZoomAnimState.mPct += 0.05f;
				if (mZoomAnimState.mPct >= 1.0)
				{
					mTickOffset = mZoomAnimState.mEndTickOffset;
					mTickScale = mZoomAnimState.mEndTickScale;
					delete mZoomAnimState;
					mZoomAnimState = null;
				}
				else
				{
					float pct = Utils.EaseInAndOut(mZoomAnimState.mPct);

					int64 destCenterTick = mZoomAnimState.mEndTickOffset + (int64)((mWidth / 2) / mZoomAnimState.mEndTickScale);

					double destCTStartX = (destCenterTick - mZoomAnimState.mStartTickOffset) * mZoomAnimState.mStartTickScale;
					double destCTEndX = (destCenterTick - mZoomAnimState.mEndTickOffset) * mZoomAnimState.mEndTickScale;
					double destCTX = Math.Lerp(destCTStartX, destCTEndX, pct);

					double scale = Math.Lerp(mZoomAnimState.mStartTickScale, mZoomAnimState.mEndTickScale, pct);
					mTickScale = scale;
					mTickOffset = destCenterTick - (int64)(destCTX / scale);
				}
				ClampView();
			}

			if (gApp.mIsUpdateBatchStart)
			{
				(float mouseX, float mouseY) = GetMouseCoords();

				bool showedTooltip = false;

				bool isMoving = (mWantTickOffset != 0) && (mWantTickOffset != mTickOffset);

				DeleteAndNullify!(mMouseoverHiliteZone);

				//mMouseStillTicks++;
				if ((mMouseOver) && (!isMoving) && (mMouseFlags == 0))
				{
					if (mSelectionFromTick != 0)
					{
						if (IsInSelection(mouseX))
						{
							int64 minSelTick = Math.Min(mSelectionFromTick, mSelectionToTick);
							int64 maxSelTick = Math.Max(mSelectionFromTick, mSelectionToTick);

							var str = scope String();
							BpClient.TimeToStr(mSession.TicksToUS(minSelTick - mSession.mFirstTick), str);
							str.Append("\n");
							BpClient.TimeToStr(mSession.TicksToUS(maxSelTick - mSession.mFirstTick), str);
							str.Append("\n");
							BpClient.ElapsedTimeToStr(mSession.TicksToUS(maxSelTick - minSelTick), str);
							//str.Append("\nClick to Zoom");

							DarkTooltipManager.ShowTooltip(str, this, mouseX + 16, mouseY + 12);
							showedTooltip = true;
						}
					}

					if (!showedTooltip)
					{
						String tooltipStr = scope String();

						DrawEntries(null, mouseX, mouseY, scope [&] (selectItem) =>
							{
								tooltipStr.Clear();

								if (selectItem case let .Track(rect, zoneNameId, unformattedName, formattedName, tickStart, tickEnd, trackIdx, stackDepth))
								{
									mMouseoverHiliteZone = new HiliteZone(zoneNameId, unformattedName);

									tooltipStr.Append(formattedName);
									tooltipStr.Append("\n");
									//str.FormatInto("Ticks: {0}", tickEnd - tickStart);
									mSession.ElapsedTicksToStr(tickEnd - tickStart, tooltipStr);
								}
								else if (selectItem case let .Event(name, details, tick, trackIdx))
								{
									tooltipStr.Append(name);
									if (!details.IsEmpty)
									{
                                        tooltipStr.Append("\n");
										tooltipStr.Append(details);
									}
								}

								/*if (DarkTooltipManager.sTooltip != null)
								{
									showedTooltip = true;
									DarkTooltipManager.sTooltip.Reinit(str, this, mouseX + 16, mouseY + 12, 0, 0, false, false);
									return;
								}*/

								/*if (str.IsEmpty())
									return;*/

								

								//Debug.WriteLine("ShowTooltip {0} {1}", mouseX + 16, mouseY + 12);
							});

						if (!tooltipStr.IsEmpty)
						{
							DarkTooltipManager.ShowTooltip(tooltipStr, this, mouseX + 16, mouseY + 12);
							showedTooltip = true;
						}
					}
				}

				if (!showedTooltip)
					DarkTooltipManager.CloseTooltip();
			}

			UpdateScrollbars();
		}

		void ProfileSelection(int32 trackIdx)
		{
			if (mSelectionFromTick == 0)
				return;

			BPSelection selection;
			selection.mTickStart = Math.Min(mSelectionFromTick, mSelectionToTick);
			selection.mTickEnd = Math.Max(mSelectionFromTick, mSelectionToTick);
			selection.mDepth = -1;
			selection.mThreadIdx = trackIdx;

			gApp.mProfilePanel.Show(this, selection);
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
			
			var screenItem = GetScreenItemAt(x, y, false);
			switch (screenItem)
			{
			case .TrackTreeArrow(let trackNode):
				trackNode.Open(!trackNode.mIsOpen, false);
				return;
			case .TrackMenu(let trackNode):
				var trackEntry = trackNode as TrackNodeEntry;

				Menu menu = new Menu();
#unwarn
				var menuItem = menu.AddItem("Set Track Color ...");
				//menuItem.mMenuItemSelectedHandler.Add(new (evt) => IDEApp.sApp.ShowDisassemblyAtCursor());
				if ((trackEntry != null) && (mSelectionFromTick != 0))
				{
                    menuItem = menu.AddItem("Profile Selection");
					menuItem.mOnMenuItemSelected.Add(new (evt) => ProfileSelection(trackEntry.mTrackIdx));
				}
				MenuWidget menuWidget = DarkTheme.sDarkTheme.CreateMenuWidget(menu);
				menuWidget.Init(this, x, y);
				return;
			default:
			}

			mDownX = x;
			mDownY = y;
			mDidDrag = false;
			mDownTick = mUpdateCnt;
			mDragSize = 0;

			mFollowEnd = false;
			mWantTickOffset = 0;

			if (mWidgetWindow.IsKeyDown(.Shift))
			{
				mSelectionFromTick = GetTickFromX(x);
				mSelectionToTick = mSelectionFromTick;
			}
			else
			{
				if (IsInSelection(x))
				{
					int64 minSelTick = Math.Min(mSelectionFromTick, mSelectionToTick);
					int64 maxSelTick = Math.Max(mSelectionFromTick, mSelectionToTick);
					ZoomTo(minSelTick, maxSelTick);
				}
				
				ClearSelection();
			}

			if ((btn == 0) && (btnCount == 2))
			{
				switch (mSelection)
				{
				case .Entry(let bpSelection):
					ZoomTo(bpSelection.mTickStart, bpSelection.mTickEnd);
				default:
				}
			}

			SetFocus();
		}

		public override void MouseUp(float x, float y, int32 btn)
		{
			base.MouseUp(x, y, btn);

			if (mDragNodeEntry != null)
			{
				mDragNodeEntry.mChildHeight = (int32)(Math.Round(mDragNodeEntry.mChildHeight / (double)TrackNodeEntry.cLineHeight) * TrackNodeEntry.cLineHeight) + TrackNodeEntry.cPeekHeight;
			}

			if (mSelectionToTick != 0)
			{
				//
			}
			else
			{
				if ((!mDidDrag) ||
					((mDragSize <= 3) && (mUpdateCnt - mDownTick < 20)))
				{
					mSelection = GetSelection(x, y);
					if (mSelection case let .Entry(selection))
					{
						gApp.ZoneSelected(this, selection);
						//mSelection = selection;
					}
					else
					{
						//gApp.ZoneSelected(null, BPSelection());
					}
				}
			}

			mDownTick = -1;
		}

		
		public override void MouseMove(float x, float y)
		{
			base.MouseMove(x, y);
			mMouseStillTicks = 0;

			//gApp.CloseTooltip();

			Cursor wantCursor = .Pointer;
			
			double ticksPerPixel = 1.0 / mTickScale;
			gApp.mMainFrame.mStatusBar.mShowTime = mSession.TicksToUS((int64)(GetTickFromX(x) - mSession.mFirstTick));
			gApp.mMainFrame.mStatusBar.mSelTick = GetTickFromX(x);

			if (mMouseFlags.HasFlag(.Left))
			{
				if (mDownTick == -1)
					return;

				if (mDragNodeEntry != null)
				{
					wantCursor = .SizeNS;
					mDragNodeEntry.mChildHeight = (int32)Math.Max(20, y + mDragTrackOfsY);
				}
				else if (mSelectionFromTick != 0)
				{
					mSelectionToTick = GetTickFromX(x);
					gApp.mMainFrame.mStatusBar.mSelTime = mSession.TicksToUS(Math.Abs((int64)(mSelectionToTick - mSelectionFromTick)));
				}
				else
				{
					float dx = (x - mDownX);
					float dy = (y - mDownY);
					mDragSize += Math.Abs(dx) + Math.Abs(dy);

					double tickOfsAdd = (-dx * ticksPerPixel) + mTickOffsetFrac;
					mTickOffset += (int64)tickOfsAdd;
					mTickOffsetFrac = tickOfsAdd - ((int64)tickOfsAdd);

					mOfsY += dy;

					ClampView();

					mDidDrag = true;
					mDownX = x;
					mDownY = y;
				}
			}
			else
			{
				mDragNodeEntry = null;

				TrackNode findBefore = null;

				bool forceLastNode = false;
				var screenItem = GetScreenItemAt(x, y, false);
				if (screenItem case .TracksTerminator)
				{
					findBefore = mNodeRoot;
					forceLastNode = true;
				}

				if (screenItem case .TrackHeader(let trackNodeEntry))
				{
					findBefore = trackNodeEntry;
				}

				//if (screenItem case .TrackHeader(let trackNodeEntry))
				if (findBefore != null)
				{
					TrackNodeEntry prevNodeEntry = null;

					//var findBefore = trackNodeEntry;
					FindPrevNodeEntry: while ((findBefore != null) || (forceLastNode))
					{
						TrackNodeGroup checkParent;
						int checkIdx;
						if (forceLastNode)
						{
							checkParent = mNodeRoot;
							checkIdx = checkParent.mChildren.Count - 1;
							forceLastNode = false;
						}
						else
						{
							checkParent = findBefore.mParent;
							if (checkParent == null)
								break;

							checkIdx = checkParent.mChildren.IndexOf(findBefore) - 1;
						}

						while (true)
						{
							while (checkIdx >= 0)
							{
								var checkNode = checkParent.mChildren[checkIdx];
								if (checkNode.mIsOpen)
									break;
								checkIdx--;
							}

							if (checkIdx < 0)
							{
								findBefore = checkParent;
								break;
							}
							else
							{
								var checkNode = checkParent.mChildren[checkIdx];
								while (true)
								{
									var checkNodeEntry = checkNode as TrackNodeEntry;
									if (checkNodeEntry != null)
									{
										prevNodeEntry = checkNodeEntry;
										break FindPrevNodeEntry;
									}

									var checkNodeGroup = (TrackNodeGroup)checkNode;
									int subCheckIdx = checkNodeGroup.mChildren.Count - 1;
									while (subCheckIdx >= 0)
									{
										checkNode = checkNodeGroup.mChildren[subCheckIdx];
										if (checkNode.mIsOpen)
											break;
										subCheckIdx--;
									}

									if (subCheckIdx == -1)
									{
										checkIdx--;
										break;
										//findBefore = checkParent;
										//break;
									}	
								}
							}
						}
					}

					if (prevNodeEntry != null)
					{
						wantCursor = .SizeNS;
						mDragNodeEntry = prevNodeEntry;
						mDragTrackOfsY = mDragNodeEntry.mChildHeight - y;
					}
				}
				else if (mSelectionFromTick != 0)
				{
					if (IsInSelection(x))
					{
						wantCursor = .Hand;
					}
					else
					{
						wantCursor = .Pointer;
					}
				}
			}

			gApp.SetCursor(wantCursor);
		}

		public override void MouseWheel(float x, float y, int32 delta)
		{
			base.MouseWheel(x, y, delta);

			double ticksPerPixel = 1.0 / mTickScale;
			double mouseTick = (x * ticksPerPixel) + mTickOffset;

			float scaleFactor = (mWidgetWindow.IsKeyDown(.Control)) ? 1.02f : 1.2f;

			if (delta > 0)
				mTickScale *= scaleFactor;
			else if (delta < 0)
				mTickScale /= scaleFactor;

			ClampView(); // Clamp scale first

			ticksPerPixel = 1.0 / mTickScale;
			mTickOffset = (int64)(mouseTick - (x * ticksPerPixel));

			ClampView(); // Then clamp offset
		}

		public override void MouseEnter()
		{
			base.MouseEnter();
		}

		public override void MouseLeave()
		{
			base.MouseLeave();
			gApp.mMainFrame.mStatusBar.mShowTime = 0;
			gApp.SetCursor(.Pointer);
		}

		double MaxVisibleTicks
		{
			get
			{
				return ((mWidth - 32) / mTickScale);
			}
		}

		// Truncates
		int64 GetMaxOffset()
		{
			// Show an extra 10us
			int64 maxTick = mSession.mCurTick + (int64)(10 / mSession.GetTicksToUSScale());
			return (int64)(maxTick - MaxVisibleTicks);
		}

		void UpdateScrollbars()
		{
			if (!mHorzScrollbar.mThumb.mMouseDown)
			{
				int64 maxTick = mSession.mCurTick - mSession.mFirstTick;
				
				mHorzScrollbar.mPageSize = MaxVisibleTicks;
				mHorzScrollbar.mContentSize = maxTick;
				mHorzScrollbar.mContentPos = mTickOffset - mSession.mFirstTick;
				mHorzScrollbar.UpdateData();
			}

			if (!mVertScrollbar.mThumb.mMouseDown)
			{
				mVertScrollbar.mPageSize = mHeight;
				mVertScrollbar.mContentSize = mNodeRoot.GetHeight() + mHeight - mHeight / 8;
				mVertScrollbar.mContentPos = -mOfsY;
				mVertScrollbar.UpdateData();
			}
		}

		void ClampView()
		{
			if (!mGotFirstTick)
				return;

			if (mTickOffset < mSession.mFirstTick)
			{
				mTickOffset = mSession.mFirstTick;
				mTickOffsetFrac = 0;
			}
			else if (mTickOffset == mSession.mFirstTick)
			{
				if (mTickOffsetFrac < 0)
					mTickOffsetFrac = 0;
			}

			double maxVisibleTicks = MaxVisibleTicks;
			// Show an extra 10us
			int64 maxTick = mSession.mCurTick + (int64)(10 / mSession.GetTicksToUSScale());
			int64 showTicks = maxTick - mTickOffset;

			if (showTicks < maxVisibleTicks)
			{
				mTickOffset = maxTick - (int64)maxVisibleTicks;
				mTickOffsetFrac = maxVisibleTicks - (int64)maxVisibleTicks;
			}


			if (mOfsY > 0)
				mOfsY = 0;

			// Allow scrolling up enough that we can expose the lower 25% as empty space
#unwarn
			//(int32 track0YOfs, int32 trackHeight) = GetTrackYPos(0);
			//double minOfsY = -((mClient.mThreads.Count * trackHeight) - /*mHeight * 3 / 4*/ mHeight / 8);
			double minOfsY = -(mNodeRoot.GetHeight() - /*mHeight * 3 / 4*/ mHeight / 8);
			if (minOfsY >= 0)
				mOfsY = 0;
			else if (mOfsY < minOfsY)
			{
				mOfsY = (int32)minOfsY;
			}

			if (mSession.mCurTick != mSession.mFirstTick)
			{
				double minScale = (mWidth - 32) / (double)(mSession.mCurTick - mSession.mFirstTick); // Limit to full view
				double maxScale = 1000 * mSession.GetTicksToUSScale();
				mTickScale = Math.Clamp(mTickScale, minScale, maxScale);
			}
		}

		void FindNextZone(int32 findDir)
		{
			mFollowEnd = false;
			(float mouseX, float mouseY) = GetMouseCoords();
			
			/*(int32 yOfs, int32 trackHeight) = GetTrackYPos(0);
			int32 trackIdx = (int32)(mouseY - yOfs) / trackHeight;
			if (trackIdx >= mClient.mThreads.Count)
				return;*/

			int32 stackDepthSelected;
			int32 trackIdx;
			var screenItem = GetScreenItemAt(mouseX, mouseY, false);
			if (screenItem case let .TrackLine(inTrackNodeEntry, inStackDepth))
			{
				trackIdx = inTrackNodeEntry.mTrackIdx;
				stackDepthSelected = inStackDepth;
			}
			else
			{
				return;
			}
			//(yOfs, trackHeight) = GetTrackYPos(trackIdx);

			int64 useTickOffset = (mWantTickOffset != 0) ? mWantTickOffset : mTickOffset;
			int64 mouseTick = GetTickFromX(mouseX, useTickOffset, mTickScale);

			/*int32 deepest = 0;
			DrawEntries(null, mouseX, -1, scope [&] (rect, name, tickStart, tickEnd, foundTrackIdx, stackDepth) =>
				{
					if (trackIdx == foundTrackIdx)
						deepest = Math.Max(deepest, stackDepth);
				});*/

			//int32 trackSelected = (int32)((mouseY - yOfs) - 22) / 20;

			var thread = mSession.mThreads[trackIdx];

			int64 minDist = (int64)(8 / mTickScale);

			int64 foundStartTick = 0;
			FindBlock: do
			{
				if (findDir > 0)
				{
					int64 minWantTick = mouseTick + minDist;
					for (int streamDataListIdx < thread.mStreamDataList.Count)
					{
						var streamData = thread.mStreamDataList[streamDataListIdx];
						if ((streamData.mSplitTick > 0) && (mouseTick > streamData.mSplitTick))
							continue; // All entries are before mouseTick

						int stackDepth = 0;

						BPStateContext stateCtx = scope BPStateContext(mSession, streamData);
						CmdLoop: while (true)
						{
							switch (stateCtx.GetNextEvent())
							{
							case let .Enter(startTick, strIdx):
								if ((startTick > minWantTick) && (stackDepth <= stackDepthSelected))
								{
									bool isOld = ((startTick <= streamData.mStartTick) && (stackDepth < stateCtx.mSplitCarryoverCount));
									if (!isOld)
									{
										foundStartTick = startTick;
										break FindBlock;
									}
								}
								stackDepth++;
							case let .Leave(endTick):
								stackDepth--;
							case .EndOfStream:
								break CmdLoop;
							default:
							}
						}
					}
				}
				else
				{
					int64 maxWantTick = mouseTick - minDist;

					int streamDataListIdx = 0;
					for (int checkStreamDataListIdx < thread.mStreamDataList.Count)
					{
						var streamData = thread.mStreamDataList[checkStreamDataListIdx];
						if ((streamData.mSplitTick > 0) && (mouseTick > streamData.mSplitTick))
							continue; // All entries are before mouseTick
						else
						{
                            streamDataListIdx = checkStreamDataListIdx;
							break;
						}
					}

					while (streamDataListIdx >= 0)
					{
						var streamData = thread.mStreamDataList[streamDataListIdx];
						int stackDepth = 0;

						BPStateContext stateCtx = scope BPStateContext(mSession, streamData);
						CmdLoop: while (true)
						{
							switch (stateCtx.GetNextEvent())
							{
							case let .Enter(startTick, strIdx):
								if ((startTick < maxWantTick) && (stackDepth <= stackDepthSelected))
								{
									// Don't break, find the last one
									foundStartTick = startTick;
								}
								stackDepth++;
							case let .Leave(endTick):
								stackDepth--;
							case .EndOfStream:
								break CmdLoop;
							default:
							}
						}

						if (foundStartTick != 0)
							break;

						streamDataListIdx--;
					}
				}
			}

			if (foundStartTick == 0)
				return;

			mWantTickOffset = foundStartTick - (int64)(mouseX / mTickScale);
		}

		public override void KeyDown(KeyCode keyCode, bool isRepeat)
		{
			base.KeyDown(keyCode, isRepeat);

			bool ctrlDown = mWidgetWindow.IsKeyDown(.Control);
			bool altDown = mWidgetWindow.IsKeyDown(.Alt);
			bool shiftDown = mWidgetWindow.IsKeyDown(.Shift);

			if ((!ctrlDown) && (!altDown) && (!shiftDown))
			{
				switch (keyCode)
				{
				case .Home:
					mWantTickOffset = mSession.mFirstTick;
					mFollowEnd = false;
					//mWantPos = 
				case .End:
					mWantTickOffset = GetMaxOffset();
					mFollowEnd = true;
				case .Left:
					FindNextZone(-1);
				case .Right:
					FindNextZone(1);
				case .Escape:
					ClearSelection();
				case (KeyCode)' ':
					mFollowEnd = false;
				default:
				}
			}
			else if (ctrlDown)
			{
				switch (keyCode)
				{
				case (KeyCode)'Z':
					mUndoManager.Undo();
				case (KeyCode)'Y':
					mUndoManager.Redo();
				case (KeyCode)'X':
					mGotFirstTick = false;
					mSession.ClearData();
					gApp.mFindPanel.Clear();
					gApp.mProfilePanel.Clear();
				default:
				}
			}
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ClampView();

			float baseSize = 18;
			float yPos = Math.Max(0, mHeight - baseSize);
			mHorzScrollbar.Resize(0, yPos,
			    mWidth - baseSize + 2,
			    baseSize);
			mVertScrollbar.Resize(mWidth - baseSize, 20, baseSize,
				mHeight - baseSize + 2 - 20);
		}
		
		(float, float) GetMouseCoords()
		{
			float mouseX;
			float mouseY;
			RootToSelfTranslate(mWidgetWindow.mClientMouseX, mWidgetWindow.mClientMouseY, out mouseX, out mouseY);
			return (mouseX, mouseY);
		}

		public void EnsureVisible(int32 trackIdx, int32 trackLine)
		{
			EnsureOpen(trackIdx);

			float wantY = GetYFromTrack(trackIdx, trackLine);

			float wantYBot = wantY + TrackNode.cHeaderHeight;
			float dispHeight = mHeight - GetHeaderHeight() - 18;
			if (wantY <= -mOfsY)
				mWantOfsY = -wantY + 20;
			else if ((wantYBot > dispHeight - mOfsY))
				mWantOfsY = dispHeight - wantYBot;
		}

		public void ZoomTo(int64 startTick, int64 endTick)
		{
			float usableWidth = mWidth - 20; // Adjust for scrollbar

			if (endTick == 0)
			{
				int64 adjustedStartTick = startTick - (int64)(180 / mTickScale);

				ZoomAction zoomAction = new ZoomAction();
				zoomAction.mPerfView = this;
				zoomAction.mPrevTickScale = mTickScale;
				zoomAction.mPrevTickOffset = mTickOffset;
				zoomAction.mTickOffset = adjustedStartTick;
				zoomAction.mTickScale = mTickScale;
				mUndoManager.Add(zoomAction);

				PhysZoomTo(adjustedStartTick, mTickScale);
				return;
			}

			// The larger this number, the more we show on either side of the zoomed area
			double scale = (double)usableWidth / (endTick - startTick);

			int64 leftAdjust = (int64)(64 / scale);
			int64 rightAdjust = (int64)(32 / scale);
			int64 adjustedStartTick = startTick - leftAdjust;
			int64 adjustedEndTick = endTick + rightAdjust;
			scale = (double)usableWidth / (adjustedEndTick - adjustedStartTick);

			ZoomAction zoomAction = new ZoomAction();
			zoomAction.mPerfView = this;
			zoomAction.mPrevTickScale = mTickScale;
			zoomAction.mPrevTickOffset = mTickOffset;
			zoomAction.mTickOffset = adjustedStartTick;
			zoomAction.mTickScale = scale;
			mUndoManager.Add(zoomAction);

			PhysZoomTo(adjustedStartTick, scale);
		}
		
		public void PhysZoomTo(int64 startTick, double scale)
		{
			delete mZoomAnimState;

			mTickOffsetFrac = 0;
			var zoomAnimState = new ZoomAnimState();
			zoomAnimState.mStartTickOffset = mTickOffset;
			zoomAnimState.mEndTickOffset = startTick;
			zoomAnimState.mStartTickScale = mTickScale;
			zoomAnimState.mEndTickScale = scale;
			mZoomAnimState = zoomAnimState;
		}

		public void SaveEntrySummary(StringView findStr, StringView filePath)
		{
			bool found = false;

			TrackLoop: for (var track in mSession.mThreads)
			{
				int64 findTick = -1;
				int findDepth = 0;

				for (var streamData in track.mStreamDataList)
				{
					BPStateContext stateCtx = scope BPStateContext(mSession, streamData);

					CmdLoop: while (true)
					{
						switch (stateCtx.GetNextEvent())
						{
						case let .Enter(startTick, strIdx):
							if (findDepth > 0)
							{
								findDepth++;
							}

							if (findTick != -1)
								break;
							
							if (strIdx < 0)
							{

							}
							else
							{
								let zoneName = mSession.mZoneNames[strIdx];
								if (zoneName.mName == findStr)
								{
									findTick = startTick;
									findDepth = 1;
								}
							}
						case let .Leave(endTick):
							if (findDepth > 0)
							{
								findDepth--;
								if (findDepth == 0)
								{
									found = true;

									BPSelection sel;
									sel.mThreadIdx = (int32)@track.Index;
									sel.mTickStart = findTick;
									sel.mTickEnd = endTick;
									sel.mDepth = 0;
									gApp.mProfilePanel.[Friend]mFormatCheckbox.Checked = false;
									gApp.mProfilePanel.[Friend]mListView.mSortType.mColumn = 3;
									gApp.mProfilePanel.Show(this, sel);
									while (true)
									{
										gApp.mProfilePanel.Update();
										if ((gApp.mProfilePanel.[Friend]mProfileCtx == null) ||
											(gApp.mProfilePanel.[Friend]mProfileCtx.mDone))
											break;
									}
									var str = scope String();
									gApp.mProfilePanel.[Friend]mListView.GetSummaryString(str);

									if (Utils.WriteTextFile(filePath, str) case .Err)
										gApp.Fail(scope String()..AppendF("Failed to create file '{0}'", filePath));
									break TrackLoop;
								}
							}
						case let .Event(tick, name, details):
						case .EndOfStream:
							break CmdLoop;
						default:
						}
					}
				}
			}

			if (!found)
			{
				gApp.Fail("Summary string not found");
			}
		}
	}
}
