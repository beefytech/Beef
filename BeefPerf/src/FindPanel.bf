using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using System.Collections.Generic;
using System;
using Beefy.events;
using Beefy;
using System.Threading;
using System.Diagnostics;
using Beefy.utils;

namespace BeefPerf
{
	class FindPanel : Widget
	{
		public class FindListViewItem : DarkVirtualListViewItem
		{
			public override void Draw(Graphics g)
			{
				base.Draw(g);
			}

			public override void DrawAll(Graphics g)
			{
				base.DrawAll(g);
			}

			public override bool Selected
			{
				set
				{
					if (value)
					{
						var findPanel = ((FindListView)mListView).mFindPanel;
						findPanel.ItemSelected(this);
					}
					base.Selected = value;
				}
			}
		}

		public class FindListView : DarkVirtualListView
		{
		    public FindPanel mFindPanel;
			public bool mIconsDirty;

		    public this(FindPanel findPanel)
		    {
		        mFindPanel = findPanel;
		    }

			protected override ListViewItem CreateListViewItem()
			{
				mIconsDirty = true;
				return new FindListViewItem();
			}

			public override void PopulateVirtualItem(DarkVirtualListViewItem listViewItem)
			{
				base.PopulateVirtualItem(listViewItem);
				listViewItem.mOnMouseDown.Add(new => mFindPanel.ValueClicked);

				if (listViewItem.mVirtualIdx >= mFindPanel.mSearchState.mFoundEntries.Count)
				{
					listViewItem.mLabel = new String("--- Results Trimmed ---");
					listViewItem.mTextColor = 0xFF808080;
					var subItem = listViewItem.CreateSubItem(1);
					subItem.mLabel = new String();
					subItem = listViewItem.CreateSubItem(2);
					subItem.mLabel = new String();
					return;
				}

				var client = mFindPanel.PerfView.mSession;
				var foundEntry = mFindPanel.mSearchState.mFoundEntries[listViewItem.mVirtualIdx];

				listViewItem.mLabel = new String(foundEntry.mZoneStr);

				var subItem = listViewItem.CreateSubItem(1);
				subItem.mOnMouseDown.Add(new => mFindPanel.ValueClicked);
				subItem.mLabel = new String();
				BpClient.TimeToStr(client.TicksToUS(foundEntry.mStartTick - client.mFirstTick), subItem.mLabel);

				subItem = listViewItem.CreateSubItem(2);
				subItem.mOnMouseDown.Add(new => mFindPanel.ValueClicked);
				subItem.mLabel = new String();
				if (foundEntry.mEndTick != 0)
					client.ElapsedTicksToStr(foundEntry.mEndTick - foundEntry.mStartTick, subItem.mLabel);

				var track = mFindPanel.PerfView.mSession.mThreads[foundEntry.mTrackIdx];
				subItem = listViewItem.CreateSubItem(3);
				subItem.mOnMouseDown.Add(new => mFindPanel.ValueClicked);
				subItem.mLabel = new String();
				track.GetName(subItem.mLabel);
			}

			public void UpdateItem(FindListViewItem listViewItem)
			{
				if (listViewItem.mVirtualIdx >= mFindPanel.mSearchState.mFoundEntries.Count)
				{
					listViewItem.mLabel.Set("???");
					return;
				}

				var client = mFindPanel.PerfView.mSession;
				var foundEntry = mFindPanel.mSearchState.mFoundEntries[listViewItem.mVirtualIdx];

				listViewItem.mLabel.Set(foundEntry.mZoneStr);

				var subItem = listViewItem.GetSubItem(1);
				subItem.mLabel.Clear();
				BpClient.TimeToStr(client.TicksToUS(foundEntry.mStartTick - client.mFirstTick), subItem.mLabel);

				subItem = listViewItem.GetSubItem(2);
				subItem.mLabel.Clear();
				if (foundEntry.mEndTick == 0)
					subItem.mLabel.Clear();
				else
					client.ElapsedTicksToStr(foundEntry.mEndTick - foundEntry.mStartTick, subItem.mLabel);
			}

			public override void ChangeSort(SortType sortType)
			{
				mSortType = sortType;
				if (mFindPanel.mSearchState == null)
					return;
				if (mFindPanel.mSearchState.mTrimmedResults)
					mFindPanel.mNeedsRehup = true;
				else
					mFindPanel.mNeedsResort = true;
			}

			public override void KeyDown(KeyCode keyCode, bool isRepeat)
			{
				base.KeyDown(keyCode, isRepeat);

				if (keyCode == (KeyCode)'D')
				{
					for (int32 i < (int32)mFindPanel.mSearchState.mFoundEntries.Count)
					{
						//Debug.WriteLine("{0} {1}", i, mFindPanel.mSearchState.mFoundEntries[i].mZoneStr);
					}
				}
			}
		}

		struct TrackLoc : IHashable
		{
			public int64 mTick;
			public int32 mDepth;

			public this(int64 tick, int32 depth)
			{
				mTick = tick;
				mDepth = depth;
			}

			public int GetHashCode()
			{
				return (int)mTick;
			}
		}

		class FindEntry
		{
			public int64 mStartTick;
			public int64 mEndTick;
			public int32 mDepth;
			public String mZoneStr ~ delete _;
			public int32 mTrackIdx;
			public bool mTimeDirty;
			public bool mInDictionary;
		}

		class TrackState
		{
			public int64 mLastEntryEndTick;
			public int64 mPrevLastEntryEndTick;
			public int64 mPrevEndTick;
			public Dictionary<TrackLoc, FindEntry> mFindPositions = new Dictionary<TrackLoc, FindEntry>() ~ delete _;
		}

		class SearchState
		{
			public int32 mCurThreadIdx;
			public int32 mCurThreadStreamIdx = -1;

			public TextSearcher mTextSearch = new TextSearcher() ~ delete _;
			public TextSearcher mTrackSearch = new TextSearcher() ~ delete _;
			public int64 mStartTick;
			public int64 mEndTick;
			public int32 mMinDepth;
			public bool mIncludeZones;
			public bool mIncludeEvents;
			
			public List<FindEntry> mFoundEntries = new List<FindEntry>() ~ DeleteContainerAndItems!(_);
			public List<FindEntry> mSortedEntries ~ delete _;
			public List<FindEntry> mPendingDeleteEntries = new List<FindEntry>() ~ DeleteContainerAndItems!(_);
			public FindEntry mHighestEntry;

			public List<TrackState> mTrackStates = new List<TrackState>() ~ DeleteContainerAndItems!(_);
			public bool mTrimmedResults;
		}

		FindListView mListView;

		bool mSelectionDirty;

		public DarkEditWidget mEntryEdit;
		public DarkComboBox mTrackEdit;
		public DarkComboBox mTimeFromEdit;
		public DarkComboBox mTimeToEdit;
		public DarkCheckBox mFormatCheckbox;
		public DarkCheckBox mZonesCheckbox;
		public DarkCheckBox mEventsCheckbox;
		SearchState mSearchState ~ delete _;

		public bool mNeedsRestartSearch;
		public bool mNeedsRehup;
		public bool mNeedsResort;
		int32 mMaxFoundEntries = 100000;
		bool mSorting;
		WaitEvent mSortDoneHandle = new WaitEvent() ~ delete _;
		bool mSearchIncomplete;

		const int cColumnName = 0;
		const int cColumnStart = 1;
		const int cColumnLength = 2;
		const int cColumnTrack = 2;

		int mDrawWaitCount = 0; // We draw when this is > 0
		bool mFailed;

		public this()
		{
			mEntryEdit = new DarkEditWidget();
			AddWidget(mEntryEdit);
			mEntryEdit.mOnKeyDown.Add(new => KeyDownHandler);
			mEntryEdit.mOnContentChanged.Add(new => FindContentChanged);

			mTrackEdit = new DarkComboBox();
			mTrackEdit.MakeEditable();
			AddWidget(mTrackEdit);
			mTrackEdit.mEditWidget.mOnContentChanged.Add(new => FindContentChanged);
			mTrackEdit.mPopulateMenuAction.Add(new => PopulateTrackList);

			mTimeFromEdit = new DarkComboBox();
			mTimeFromEdit.MakeEditable();
			AddWidget(mTimeFromEdit);
			mTimeFromEdit.mEditWidget.mOnContentChanged.Add(new => FindContentChanged);
			mTimeFromEdit.mPopulateMenuAction.Add(new => PopulateTimeFrom);

			mTimeToEdit = new DarkComboBox();
			mTimeToEdit.MakeEditable();
			AddWidget(mTimeToEdit);
			mTimeToEdit.mEditWidget.mOnContentChanged.Add(new => FindContentChanged);
			mTimeToEdit.mPopulateMenuAction.Add(new => PopulateTimeTo);

			mFormatCheckbox = new DarkCheckBox();
			AddWidget(mFormatCheckbox);
			mFormatCheckbox.Checked = true;
			mFormatCheckbox.Label = "Format Strings";
			mFormatCheckbox.mOnMouseUp.Add(new (evt) => { FindOptionsChanged(); });

			mZonesCheckbox = new DarkCheckBox();
			AddWidget(mZonesCheckbox);
			mZonesCheckbox.Checked = true;
			mZonesCheckbox.Label = "Zones";
			mZonesCheckbox.mOnMouseUp.Add(new (evt) => { FindOptionsChanged(); });

			mEventsCheckbox = new DarkCheckBox();
			mEventsCheckbox.Checked = true;
			mEventsCheckbox.Label = "Events";
			AddWidget(mEventsCheckbox);
			mEventsCheckbox.mOnMouseUp.Add(new (evt) => { FindOptionsChanged(); });

			mListView = new FindListView(this);
			mListView.mOnLostFocus.Add(new (evt) => mListView.GetRoot().SelectItemExclusively(null));
			mListView.mSortType.mColumn = 2;
			mListView.mSortType.mReverse = true;
			AddWidget(mListView);
	
			mListView.AddColumn(200, "Name");
			mListView.AddColumn(150, "Start");
			mListView.AddColumn(150, "Length");
			mListView.AddColumn(150, "Track");
			mListView.InitScrollbars(false, true);
		}

		public ~this()
		{
			FinishSorting();
		}

		PerfView PerfView
		{
			get
			{
				return gApp.mBoard.mPerfView;
			}
		}

		void PopulateTrackList(Menu menu)
		{
			var perfView = PerfView;
			List<String> threadNames = scope List<String>();
			
			for (var thread in perfView.mSession.mThreads)
			{
				var threadName = new String();
				thread.GetName(threadName);
				threadNames.Add(threadName);
			}

			threadNames.Sort(scope (lhs, rhs) => String.Compare(lhs, rhs, true));
			for (var threadName in threadNames)
			{
#unwarn
				var menuItem = menu.AddItem(threadName);
				menuItem.mOnMenuItemSelected.Add(new (item) =>
                    {
						var str = scope String();
						str.Append('=');
						str.Append(((Menu)item).mLabel);
						if (str.Contains(' '))
						{
							str.Insert(1, '\"');
							str.Append('"');
						}
                        mTrackEdit.mEditWidget.SetText(str);
                    });
			}
			ClearAndDeleteItems(threadNames);
		}

		void SetVisibleLeft()
		{
			var perfView = PerfView;
			var str = scope String();
			perfView.mSession.TicksToStr(perfView.mTickOffset, str);
			mTimeFromEdit.mEditWidget.SetText(str);
		}

		void SetVisibleRight()
		{
			var perfView = PerfView;
			var str = scope String();
			perfView.mSession.TicksToStr(perfView.mTickOffset + (int64)(perfView.mWidth / perfView.mTickScale), str);
			mTimeToEdit.mEditWidget.SetText(str);
		}

		void SetEndRight()
		{
			//var perfView = PerfView;
			//var str = scope String();
			//perfView.mClient.ElapsedTicksToStr(perfView.mClient.mCurTick - perfView.mClient.mFirstTick, str);
			//mTimeToEdit.mEditWidget.SetText(str);
			mTimeToEdit.mEditWidget.SetText("");
		}

		void PopulateTimeFrom(Menu menu)
		{
			var menuItem = menu.AddItem("Start");
			menuItem.mOnMenuItemSelected.Add(new (item) => { mTimeFromEdit.mEditWidget.SetText("0s"); });
			menuItem = menu.AddItem("Entire Range");
			menuItem.mOnMenuItemSelected.Add(new (item) =>
                {
                    mTimeFromEdit.mEditWidget.SetText("0s");
					SetEndRight();
                });
			menuItem = menu.AddItem("Visible Left");
			menuItem.mOnMenuItemSelected.Add(new (item) =>
				{
					SetVisibleLeft();
				});
			menuItem = menu.AddItem("Visible Range");
			menuItem.mOnMenuItemSelected.Add(new (item) =>
				{
					SetVisibleLeft();
					SetVisibleRight();
				});
		}

		void PopulateTimeTo(Menu menu)
		{
			var menuItem = menu.AddItem("End");
			menuItem.mOnMenuItemSelected.Add(new (item) =>
                {
                    SetEndRight();
                });
			menuItem = menu.AddItem("Visible Right");
			menuItem.mOnMenuItemSelected.Add(new (item) =>
				{
					SetVisibleRight();
				});
		}

		void KeyDownHandler(KeyDownEvent evt)
		{
		    //if (evt.mKeycode == KeyCode.Escape)
		        //Close();

		    if (evt.mKeyCode == KeyCode.Return)
		    {
		        //StartSearch();
		    }
		}

		void FindContentChanged(EditEvent editEvent)
		{
			mNeedsRestartSearch = true;
		}

		void FindOptionsChanged()
		{
			mNeedsRestartSearch = true;
		}

		public void ItemSelected(FindListViewItem item)
		{
			int32 selectedIdx = item.mVirtualIdx;
			if (selectedIdx >= mSearchState.mFoundEntries.Count)
				return; // "-- Results Trimmed --" entry
			var foundEntry = mSearchState.mFoundEntries[selectedIdx];

			if (foundEntry.mEndTick == 0)
			{
				PerfView.mSelection = .Event(foundEntry.mTrackIdx, foundEntry.mStartTick);
			}
			else
			{
				BPSelection selection;
				selection.mTickStart = foundEntry.mStartTick;
				selection.mTickEnd = foundEntry.mEndTick;
				selection.mDepth = foundEntry.mDepth;
				selection.mThreadIdx = foundEntry.mTrackIdx;

				PerfView.mSelection = .Entry(selection);
				gApp.mProfilePanel.Show(PerfView, selection);
			}
		}

		public void ValueClicked(MouseEvent theEvent)
		{
		    DarkVirtualListViewItem clickedItem = (DarkVirtualListViewItem)theEvent.mSender;
		    DarkVirtualListViewItem item = (DarkVirtualListViewItem)clickedItem.GetSubItem(0);

		    mListView.GetRoot().SelectItemExclusively(item);
		    mListView.SetFocus();

		    if (theEvent.mBtn == 0)
		    {
		        int32 selectedIdx = item.mVirtualIdx;
				var foundEntry = mSearchState.mFoundEntries[selectedIdx];
				if (theEvent.mBtnCount > 1)
				{
					PerfView.EnsureVisible(foundEntry.mTrackIdx, foundEntry.mDepth);
					PerfView.ZoomTo(foundEntry.mStartTick, foundEntry.mEndTick);
				}

				if (foundEntry.mEndTick != 0)
				{
					BPSelection selection;
					selection.mTickStart = foundEntry.mStartTick;
					selection.mTickEnd = foundEntry.mEndTick;
					selection.mDepth = foundEntry.mDepth;
					selection.mThreadIdx = foundEntry.mTrackIdx;
					
					PerfView.mSelection = .Entry(selection);
					gApp.mProfilePanel.Show(PerfView, selection);
				}

		        //IDEApp.sApp.mDebugger.mSelectedCallStackIdx = selectedIdx;
		        //IDEApp.sApp.ShowPCLocation(selectedIdx, false, true);
		        //IDEApp.sApp.StackPositionChanged();                
		    }

		    //UpdateIcons();
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);

			var font = DarkTheme.sDarkTheme.mSmallFont;
			String findLabel = "Find";
			String trackLabel = "Thread";
			float findLabelWidth = font.GetWidth(findLabel) + 27;
			float trackLabelWidth = font.GetWidth(trackLabel) + 12;
			float spacingWidth = 8;
			float availWidth = Math.Max(mWidth - findLabelWidth - spacingWidth - trackLabelWidth, 0);
			
			mEntryEdit.Resize(findLabelWidth, 0, availWidth * 0.65f, 20);
			mTrackEdit.Resize(mEntryEdit.mX + mEntryEdit.mWidth + spacingWidth + trackLabelWidth, 0, availWidth * 0.35f, 20);

			String timeFromLabel = "Time From";
			String timeToLabel = "Time To";
			float timeFromLabelWidth = font.GetWidth(timeFromLabel) + 12;
			float timeToLabelWidth = font.GetWidth(timeToLabel) + 12;

			float timeEditWidth = 140;
			mTimeFromEdit.Resize(timeFromLabelWidth, 20, timeEditWidth, 20);
			mTimeToEdit.Resize(mTimeFromEdit.mX + timeEditWidth + spacingWidth + timeToLabelWidth, 20, timeEditWidth, 20);

			float formatWidth = font.GetWidth(mFormatCheckbox.mLabel) + 28;
			float zonesWidth = font.GetWidth(mZonesCheckbox.mLabel) + 28;
			mFormatCheckbox.Resize(mTimeToEdit.mX + timeEditWidth + 8, 20, 20, 20);
			mZonesCheckbox.Resize(mFormatCheckbox.mX + formatWidth, 20, 20, 20);
			mEventsCheckbox.Resize(mZonesCheckbox.mX + zonesWidth, 20, 20, 20);

			mListView.ResizeClamped(0, 40, width, height - 40);
		}
	
		public override void Draw(Graphics g)
		{
			base.Draw(g);
	
			//g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Bkg), 0, 0, mWidth, mHeight);

			var font = DarkTheme.sDarkTheme.mSmallFont;
			String findLabel = "Find";
			String trackLabel = "Thread";
			float findLabelWidth = font.GetWidth(findLabel);
			float trackLabelWidth = font.GetWidth(trackLabel);

			g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			g.DrawString(findLabel, mEntryEdit.mX - findLabelWidth - 6, 0);
			g.DrawString(trackLabel, mTrackEdit.mX - trackLabelWidth - 6, 0);

			String timeFromLabel = "Time From";
			String timeToLabel = "Time To";
			float timeFromLabelWidth = font.GetWidth(timeFromLabel);
			float timeToLabelWidth = font.GetWidth(timeToLabel);
			g.DrawString(timeFromLabel, mTimeFromEdit.mX - timeFromLabelWidth- 6, 20);
			g.DrawString(timeToLabel, mTimeToEdit.mX - timeToLabelWidth - 6, 20);
		}

		public void Show(PerfView perfView)
		{
			mSelectionDirty = true;
		}

		void GetData()
		{
			mListView.GetRoot().Clear();
	
		}

		bool SearchIn(int32 trackIdx, int32 streamDataIdx)
		{
			var client = PerfView.mSession;
			BpTrack track = client.mThreads[trackIdx];
            BpStreamData streamData = track.mStreamDataList[streamDataIdx];
			var trackState = mSearchState.mTrackStates[trackIdx];
            var curEntryEndTick = ref trackState.mLastEntryEndTick;

			bool isRecording = false;
			//bool isFirstDrawn = true;

			if (mSearchState.mStartTick == 0)
				isRecording = true;

			if ((mSearchState.mStartTick != 0) && (streamData.mSplitTick > 0) && (streamData.mSplitTick < mSearchState.mStartTick))
				return false; // All data is to the left
			//if ((mSearchState.mEndTick != 0) && (streamData.mSplitTick < mSearchState.mStartTick))
				//return false; // All data is to the right

			if ((mSearchState.mEndTick != 0) && (streamData.mStartTick > mSearchState.mEndTick))
				return true;

			BPStateContext stateCtx = scope BPStateContext(client, streamData);

			List<BPEntry> entryStack = scope List<BPEntry>();
			int32 stackDepth = 0;

			String tempDynStr = scope String();
			String tempStr = scope String();

			FindEntry tempFindEntry = new FindEntry();
			defer
            {
				tempFindEntry.mZoneStr = null;
                delete tempFindEntry;
            }

			CmdLoop: while (true)
			{
				switch (stateCtx.GetNextEvent())
				{
				case let .Enter(startTick, strIdx):
#unwarn
					int stackPos = stackDepth++;
					//if ((!isFirstDrawn) && (startTick < streamData.mStartTick))
						//continue; // Would have already been handled by a previous streamData

					if (!mSearchState.mIncludeZones)
						continue;

					if ((mSearchState.mStartTick != 0) && (startTick >= mSearchState.mStartTick))
					{
						isRecording = true;
					}

					if (isRecording)
					{
						BPEntry entry;
						entry.mStartTick = startTick;
						entry.mZoneNameId = strIdx;
						//stateCtx.MoveToParamData();
						entry.mParamsReadPos = stateCtx.ReadPos;
						entryStack.Add(entry);
					}
				case let .Leave(endTick):
					stackDepth--;
					if (isRecording)
					{
						if (entryStack.Count == 0)
						{
							continue;
						}
#unwarn
						let entry = entryStack.PopBack();
#unwarn
						if (endTick < curEntryEndTick)
							continue;

						if ((mSearchState.mEndTick != 0) && (entry.mStartTick > mSearchState.mEndTick))
							continue;

						bool isOld = ((entry.mStartTick <= streamData.mStartTick) && (stackDepth < stateCtx.mSplitCarryoverCount));
						if (!stateCtx.mTimeInferred)
						{
							if (endTick <= curEntryEndTick)
								continue;

							if ((streamData.mSplitTick == 0) || (endTick <= streamData.mSplitTick)) // Don't include entries that span past the end of this stream
								curEntryEndTick = endTick;

							if (isOld)
								continue;
						}

#unwarn
						int32 stackPos = stackDepth;

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

						if (paramsSize != 0)
						{
							tempStr.Clear();
							stateCtx.FormatStr(paramReadPos, paramsSize, str, tempStr);
							str = tempStr;
						}

						FindEntry findEntry = tempFindEntry;
						findEntry.mStartTick = entry.mStartTick;
						findEntry.mEndTick = endTick;
						findEntry.mZoneStr = str;
						findEntry.mTrackIdx = trackIdx;
						findEntry.mDepth = stackDepth;

						bool isOverflow = mSearchState.mFoundEntries.Count >= mMaxFoundEntries;

						if (isOverflow)
						{
							// Can we be sure this entry wouldn't be included in the list after sorting and trimming?
							if (EntryCompare(findEntry, mSearchState.mHighestEntry) >= 0)
							{
								continue;
							}
						}
						
						if (mSearchState.mTextSearch.Matches(str))
						{
							FindEntry* findEntryPtr = null;

							if ((endTick >= client.mCurTick) || // If it extends off the back
								(isOld) ||
                                (entry.mStartTick <= trackState.mPrevLastEntryEndTick)) // Was already started on the previous update
							{
								TrackLoc* trackLoc;
								if (!trackState.mFindPositions.TryAdd(TrackLoc(entry.mStartTick, stackDepth), out trackLoc, out findEntryPtr))
								{
									findEntry = *findEntryPtr;
									findEntry.mEndTick = endTick;
									findEntry.mTimeDirty = true;
									continue;
								}

								if (isOld)
									continue;
							}

							findEntry.mZoneStr = new String(findEntry.mZoneStr);
							mSearchState.mFoundEntries.Add(findEntry);

							if (!isOverflow)
							{
								if ((mSearchState.mHighestEntry == null) || (EntryCompare(findEntry, mSearchState.mHighestEntry) >= 0))
								{
									// We have a new "highest entry"
									mSearchState.mHighestEntry = findEntry;
								}
							}

							// Create a pending entry...
							tempFindEntry = new FindEntry();

							if (findEntryPtr != null)
							{
								findEntry.mInDictionary = true;
							 	*findEntryPtr = findEntry;
							}
						}
					}
				case let .Event(tick, name, details):
					if (tick <= curEntryEndTick)
						continue;

					if (!mSearchState.mIncludeEvents)
						continue;

					if ((mSearchState.mStartTick != 0) && (tick < mSearchState.mStartTick))
						continue;
					if ((mSearchState.mEndTick != 0) && (tick > mSearchState.mEndTick))
						continue;

					curEntryEndTick = tick;

					var nameStr = scope String();
					nameStr.Reference(name);
					var detailsStr = scope String();
					detailsStr.Reference(details);

					if ((mSearchState.mTextSearch.Matches(nameStr)) || (mSearchState.mTextSearch.Matches(detailsStr)))
					{
						let maxDetailChars = 128;
						let strLen = nameStr.Length + " : ".Length + Math.Min(detailsStr.Length, maxDetailChars + 3);
						int numDetailChars = Math.Min(detailsStr.Length, maxDetailChars);
						var foundStr = new String(strLen);
						foundStr.Append(nameStr, " : ");

						for (int i < numDetailChars)
						{
							char8 c = details[i];
							if ((int)c < 32)
								foundStr.Append(' ');
							else
								foundStr.Append(c);
						}

						if (detailsStr.Length > maxDetailChars)
						{
							foundStr.Append("...");
						}

						var findEntry = new FindEntry();
						findEntry.mZoneStr = foundStr;
						findEntry.mStartTick = tick;
						findEntry.mTrackIdx = trackIdx;
						findEntry.mDepth = -1;
						mSearchState.mFoundEntries.Add(findEntry);
					}

				case .EndOfStream:
					break CmdLoop;
				default:
				}
			}

			//isFirstDrawn = false;
			return true;
		}



		void StartSearch()
		{
			//Debug.WriteLine("StartSearch");
			mListView.GetRoot().Clear();

			var error = scope String();

			Clear();

			var perfView = PerfView;
			if (perfView == null)
				return;

			bool hadError = true;
			do
			{
				var client = perfView.mSession;

				mSearchState = new SearchState();

				var str = scope String();
				mEntryEdit.GetText(str);
				if (mSearchState.mTextSearch.Init(str, error) case .Err)
					break;

				str.Clear();
				mTrackEdit.mEditWidget.GetText(str);
				if (mSearchState.mTrackSearch.Init(str, error) case .Err)
					break;

				str.Clear();
				mTimeFromEdit.mEditWidget.GetText(str);
				if (!str.IsWhiteSpace)
				{
					if (client.ParseTime(str) case .Ok(let ticks))
						mSearchState.mStartTick = ticks;
					else
					{
						error.Set("Invalid 'Time From' value");
						break;
					}
				}

				str.Clear();
				mTimeToEdit.mEditWidget.GetText(str);
				if (!str.IsWhiteSpace)
				{
					if (client.ParseTime(str) case .Ok(let ticks))
						mSearchState.mEndTick = ticks;
					else
					{
						error.Set("Invalid 'Time To' value");
						break;
					}
				}

				mSearchState.mIncludeZones = mZonesCheckbox.Checked;
				mSearchState.mIncludeEvents = mEventsCheckbox.Checked;

				hadError = false;
			}

			if (hadError)
			{
				if (error.IsEmpty)
					error.Append("Error");

				var listViewItem = (DarkListViewItem)mListView.GetRoot().CreateChildItem();
				listViewItem.Label = error;
				listViewItem.mTextColor = 0xFFFF8080;

				mFailed = true;
			}
			
			SearchIteration();
		}

		int EntryCompare(FindEntry lhs, FindEntry rhs)
		{
			int64 result = 0;
			switch (mListView.mSortType.mColumn)
			{
            case 0:
				result = String.Compare(lhs.mZoneStr, rhs.mZoneStr, true);
				if (result == 0)
					result = lhs.mStartTick - rhs.mStartTick;
			case 1:
				result = lhs.mStartTick - rhs.mStartTick;
			case 2:
				int64 lhsLen = lhs.mEndTick - lhs.mStartTick;
				int64 rhsLen = rhs.mEndTick - rhs.mStartTick;
				result = lhsLen - rhsLen;
			case 3:
				var lhsTrack = PerfView.mSession.mThreads[lhs.mTrackIdx];
				var rhsTrack = PerfView.mSession.mThreads[rhs.mTrackIdx];
				result = BpTrack.Compare(lhsTrack, rhsTrack);
				if (result == 0)
					result = lhs.mStartTick - rhs.mStartTick;
			}
			if (mListView.mSortType.mReverse)
				result = -result;
			return (int)result;
		}

		void SortProc()
		{
			//Debug.WriteLine("SortProc start");

			mSearchState.mSortedEntries = new List<FindEntry>();
			mSearchState.mFoundEntries.CopyTo(mSearchState.mSortedEntries);

			mSearchState.mSortedEntries.Sort(scope => EntryCompare);

			mSortDoneHandle.Set();

			//Debug.WriteLine("SortProc end");
		}

		bool SearchIteration()
		{
			if (mFailed)
				return false;
			mSearchIncomplete = false;
			if (mSorting)
				mSearchIncomplete = true;

			if (mSearchState.mTextSearch.IsEmpty)
				return true;

			var stopwatch = scope Stopwatch();
			stopwatch.Start();

			bool refreshItems = false;
			if (mSearchState.mSortedEntries != null)
			{
				Debug.Assert(mSearchState.mSortedEntries.Count == mSearchState.mFoundEntries.Count);
				delete mSearchState.mFoundEntries;
				mSearchState.mFoundEntries = mSearchState.mSortedEntries;
				mSearchState.mSortedEntries = null;
				mSearchState.mHighestEntry = null;

				while (mSearchState.mFoundEntries.Count > mMaxFoundEntries)
				{
					var entry = mSearchState.mFoundEntries.PopBack();
					if (entry.mInDictionary)
						mSearchState.mPendingDeleteEntries.Add(entry);
					else
						delete entry;
					mSearchState.mTrimmedResults = true;
				}
				if (mSearchState.mFoundEntries.Count > 0)
					mSearchState.mHighestEntry = mSearchState.mFoundEntries.Back;
				refreshItems = true;
			}

			int32 prevFindCount = (int32)mSearchState.mFoundEntries.Count;

			var client = PerfView.mSession;
			if (mSearchState.mCurThreadIdx >= client.mThreads.Count)
			{
				mSearchState.mCurThreadIdx = 0;
				mSearchState.mCurThreadStreamIdx = -1;
			}
			bool didEarlyBreak = false;
			TrackLoop: while (mSearchState.mCurThreadIdx < client.mThreads.Count)
			{
				int32 threadIdx = mSearchState.mCurThreadIdx;
				while (threadIdx >= mSearchState.mTrackStates.Count)
					mSearchState.mTrackStates.Add(new TrackState());

				var track = client.mThreads[threadIdx];
				var trackState = mSearchState.mTrackStates[threadIdx];

				//int32 streamDataIdx = track.mStreamDataList.Count - 1;
				if (mSearchState.mCurThreadStreamIdx == -1)
				{
					trackState.mPrevLastEntryEndTick = trackState.mLastEntryEndTick;
                    mSearchState.mCurThreadStreamIdx = (int32)track.mStreamDataList.Count - 1;
					while (mSearchState.mCurThreadStreamIdx > 0)
					{
						mSearchState.mCurThreadStreamIdx--;
						var streamData = track.mStreamDataList[mSearchState.mCurThreadStreamIdx];
						if ((streamData.mEndTick < trackState.mLastEntryEndTick) ||
							(streamData.mEndTick == trackState.mPrevEndTick))
					  	{
							mSearchState.mCurThreadStreamIdx++;
							break;
						}
					}

					//Test
					//mSearchState.mCurThreadStreamIdx = 0;
				}

				bool includeTrack = true;
				if (!mSearchState.mTrackSearch.IsEmpty)
				{
					var trackName = scope String(128);
					track.GetName(trackName);
					includeTrack = mSearchState.mTrackSearch.Matches(trackName);
				}

				if (includeTrack)
				{
					while (mSearchState.mCurThreadStreamIdx < track.mStreamDataList.Count)
					{
						SearchIn(threadIdx, mSearchState.mCurThreadStreamIdx);
						BpStreamData streamData = track.mStreamDataList[mSearchState.mCurThreadStreamIdx];
						trackState.mPrevEndTick = streamData.mEndTick;
						mSearchState.mCurThreadStreamIdx++;

						if ((mSearchState.mFoundEntries.Count >= mMaxFoundEntries * 2) ||
							(stopwatch.ElapsedMilliseconds > (int)(gApp.mTimePerFrame * 1000)))
						{
							didEarlyBreak = true;
							mSearchIncomplete = true;
							break TrackLoop;
						}
					}
				}

				if (!didEarlyBreak)
				{
					mSearchState.mCurThreadIdx++;
					mSearchState.mCurThreadStreamIdx = -1;
				}
			}

			int width;
			int height;
			gApp.GetDesktopResolution(out width, out height);

			//TODO: We really want to set this to the maximum number of possibly visible lines
			//int32 scrollLinePos = (int32)((height - mListView.mScrollContent.mY) / mListView.mFont.GetLineSpacing());
			int numNewEntries = mSearchState.mFoundEntries.Count - prevFindCount;

			if ((numNewEntries != 0) || (mNeedsResort))
			{
				//mSortThread = new Thread(new => SortProc);
				//mSortThread.Start(false);
				mSorting = true;
				ThreadPool.QueueUserWorkItem(new => SortProc);

				mNeedsResort = false;
				mSearchIncomplete = true;
			}

			// Add entries
			if (numNewEntries != 0)
			{
				if (mListView.GetRoot().GetChildCount() == 0)
				{
					var listViewItem = (DarkVirtualListViewItem)mListView.GetRoot().CreateChildItem();
					listViewItem.mVirtualHeadItem = listViewItem;
					mListView.PopulateVirtualItem(listViewItem);
				}

				var listViewItem = (DarkVirtualListViewItem)mListView.GetRoot().GetChildAtIndex(0);
				listViewItem.mVirtualCount = Math.Min((int32)mSearchState.mFoundEntries.Count, mMaxFoundEntries);
				if (mSearchState.mTrimmedResults)
					listViewItem.mVirtualCount++; // Show "trimmed results" message at bottom
			}

			for (int itemIdx < mListView.GetRoot().GetChildCount())
			{
				var listViewItem = (FindListViewItem)mListView.GetRoot().GetChildAtIndex((int32)itemIdx);
				if (listViewItem.mVirtualIdx >= mSearchState.mFoundEntries.Count)
					continue;
				var foundEntry = mSearchState.mFoundEntries[listViewItem.mVirtualIdx];
				if ((foundEntry.mTimeDirty) || (refreshItems))
				{
					mListView.UpdateItem(listViewItem);
					if (foundEntry.mTimeDirty)
					{
						foundEntry.mTimeDirty = false;
						if (mListView.mSortType.mColumn == cColumnLength)
							mNeedsResort = true;
					}
					
				}
			}

			return true;
		}

		void RefreshData()
		{
			mListView.GetRoot().Clear();
		}

		public override void Update()
		{
			base.Update();

			if (mSelectionDirty)
			{
				RefreshData();
				mSelectionDirty = false;
			}

			if (mSorting)
			{
				if (mSortDoneHandle.WaitFor(0))
				{
					mSorting = false;
					mSortDoneHandle.Reset();
				}
			}

			if ((mNeedsRestartSearch) && (!mSorting))
			{
				StartSearch();
				mNeedsRestartSearch = false;
			}

			if ((mNeedsRehup) && (!mSorting))
			{
				if (mSearchState != null)
				{
					mSearchState.mHighestEntry = null;
					ClearAndDeleteItems(mSearchState.mFoundEntries);
					DeleteAndNullify!(mSearchState.mSortedEntries);
					for (var trackState in mSearchState.mTrackStates)
					{
						trackState.mFindPositions.Clear();
						trackState.mLastEntryEndTick = 0;
						trackState.mPrevLastEntryEndTick = 0;
					}
					mListView.GetRoot().Clear();
				}
				mNeedsRehup = false;
			}

			if ((mSearchState != null) && (!mSorting))
			{
				if (gApp.mIsUpdateBatchStart)
					SearchIteration();
			}

			if (mSearchIncomplete)
				mDrawWaitCount = Math.Min(mDrawWaitCount + 1, 8);
			else
				mDrawWaitCount = Math.Max(mDrawWaitCount - 1, 0);
		}

		void ClearMem()
		{

		}

		void FinishSorting()
		{
			if (mSorting)
			{
				mSortDoneHandle.WaitFor();
				mSorting = false;
				mSortDoneHandle.Reset();
			}
		}

		public void Clear()
		{
			mFailed = false;
			//Debug.WriteLine("FinePanel.Clear");
			FinishSorting();

			//Debug.WriteLine("FinePanel.Clear - deleting search state");
			DeleteAndNullify!(mSearchState);
			mListView.GetRoot().Clear();
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);

			if (mDrawWaitCount > 0)
			{
				BPUtils.DrawWait(g, 1, mEntryEdit.mY + 0);
			}
		}
	}
}
