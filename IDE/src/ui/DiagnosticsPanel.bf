using System;
using Beefy.utils;
using System.Diagnostics;
using System.Collections;
using Beefy.gfx;
using Beefy.geom;
using Beefy.theme.dark;
using Beefy.widgets;

namespace IDE.ui
{
	class DiagnosticsPanel : Panel
	{
		class DbgAllocListViewItem : IDEListViewItem
		{
			public int mTypeId;
			public int mAllocCount;
			public int mAllocSize;
		}

		class DbgAllocListView : DarkListView
		{
			public this()
			{
				//mShowLineGrid = true;
			}

			protected override ListViewItem CreateListViewItem()
			{
				return new DbgAllocListViewItem();
			}

			public override void DrawAll(Graphics g)
			{
				base.DrawAll(g);

				/*using (g.PushColor(0x40FF0000))
					g.FillRect(8, 8, mWidth - 16, mHeight - 16);*/
			}
		}

		struct Sample
		{
			public double mValue;
			public double mTime;

			public this(double value, double time)
			{
				mValue = value;
				mTime = time;
			}

			public static Sample operator+(Sample lhs, Sample rhs)
			{
				return .(lhs.mValue + rhs.mValue, lhs.mTime + rhs.mTime);
			}

			public static Sample operator-(Sample lhs, Sample rhs)
			{
				return .(lhs.mValue - rhs.mValue, lhs.mTime - rhs.mTime);
			}

			public static Sample operator*(Sample lhs, float rhs)
			{
				return .(lhs.mValue * rhs, lhs.mTime * rhs);
			}
		}

		class Data
		{
			public const int cMaxSamples = 1000;
			public static float[cMaxSamples + 1] sSampleMixTable;

			static this()
			{
				for (int i < cMaxSamples + 1)
				{
					float pct = (i + 0.1f) / (float)(cMaxSamples - 3);
					pct = Math.Pow(pct, 1.5f);
					sSampleMixTable[i] = pct;
				}
			}

			public List<double> mSamples = new .() ~ delete _;
			double mPrevSample;
			double mNextSample;

			public double mMaxValue;
			List<double> mPrevValues = new List<double>() ~ delete _;

			double mSampleAcc;
			int32 mSampleCount;
			int32 mSampleCombine;

			public this(int sampleCombine)
			{
				mSampleCombine = (.)sampleCombine;
			}

			public void AddSample(double sample)
			{
				double insertSample = sample;

				if (mSampleCombine > 1)
				{
					if (mSamples.IsEmpty)
					{
						mPrevSample = sample;
						mNextSample = sample;
					}

					mSampleAcc += sample;
					mSampleCount++;

					insertSample = Math.Lerp(mPrevSample, mNextSample, mSampleCount / (float)mSampleCombine);

					if (mSampleCount == mSampleCombine)
					{
						mPrevSample = mNextSample;
						mNextSample = mSampleAcc / mSampleCombine;
						mSampleCount = 0;
						mSampleAcc = 0;
					}
				}

				mSamples.Add(insertSample);

				mPrevValues.Add(insertSample);

				mMaxValue = Math.Max(mMaxValue, insertSample);

				while (mSamples.Count > cMaxSamples)
				{
					for (int sampleIdx < mSamples.Count - 1)
					{
						let lhs = mSamples[sampleIdx];
						let rhs = mSamples[sampleIdx + 1];
						float pct = sSampleMixTable[sampleIdx];
						mSamples[sampleIdx] = Math.Lerp(lhs, rhs, pct);
					}
					mSamples.PopBack();
				}
			}

			public void Clear()
			{
				mMaxValue = 0;
				mSamples.Clear();
			}

			public float Get(float sampleIdx)
			{
				let lhs = mSamples[(int)sampleIdx];
				let rhs = mSamples[(int)Math.Min(sampleIdx + 1, mSamples.Count - 1)];
				return (.)Math.Lerp(lhs, rhs, sampleIdx - (int)sampleIdx);
			}
		}

		class DataView : Widget
		{
			public DiagnosticsPanel mPanel;
			public Data mTimes;
			public Data mData;
			public double? mOverDataTime;
			public double? mOverDataValue;
			public String mLabel = new .() ~ delete _;

			public this(StringView label)
			{
				mLabel.Set(label);
			}

			[LinkName(.C)]
			public static extern double log10(double val);

			[LinkName(.C)]
			public static extern double log2(double val);

			void ValToString(double val, String str)
			{
				let kSize = 1024;

				let maxValue = mData.mMaxValue;
				if (maxValue < 1000)
				{
					str.AppendF("{:0.00}", val);
				}
				else if (maxValue < 10000)
				{
					str.AppendF("{:0.0}", val);
				}
				else if (maxValue < 1'000'000)
				{
					str.AppendF("{}", (int)val);
				}
				else if (maxValue < 1000 * kSize)
				{
					str.AppendF("{:0.000}K", val / kSize);
				}
				else if (maxValue < 1000 * (kSize * kSize))
				{
					str.AppendF("{:0.000}M", val / (kSize * kSize));
				}
				else
				{
					str.AppendF("{:0.000}G", val / (kSize * kSize * kSize));
				}
			}

			public void DrawData(Graphics g, Data data, Data timeData, float xOfs, float yOfs, float width, float height)
			{
				

				/*for (double maxValue = 0.001; maxValue < 2; maxValue += 0.001)
				{
					double segLog10 = log10(maxValue);
					if (segLog10 > 0)
						segLog10 = (int)(segLog10);
					double segSize = Math.Pow(10, segLog10);
					double numSegs = maxValue / segSize;
					if (numSegs < 2)
						segSize /= 4;
					else if (numSegs < 4)
						segSize /= 2;
					numSegs = maxValue / segSize;

					Debug.WriteLine("{} {:.00} {:.00}", maxValue, segSize, numSegs);
				}*/

				/*while (true)
				{
					double numSegs = maxValue / segSize;
					if (numSegs >= 3)
						break;
				}*/

				

				using (g.PushTranslate(xOfs, yOfs))
				{
					using (g.PushColor(0xA0000000))
					{
						g.FillRect(0, 0, width, height);
					}

					using (g.PushColor(0x40FFFFFF))
					{
						g.OutlineRect(-1, -1, width + 2, height + 2);
					}

					if (data.mSamples.IsEmpty)
						return;

					if (data.mMaxValue > 0)
					{
						using (g.PushClip(0, 0, width, height))
						{
							using (g.PushColor(0xFF00FF00))
							{
								float prevY = 0;

								for (int x < (int)width)
								{
									float sampleIdx = x * (data.mSamples.Count - 1) / width;
									float value = data.Get(sampleIdx);

									float y = (.)(height * (1.0 - (value / data.mMaxValue)) + 0.5);

									if (x == 0)
										g.FillRect(x, y, 1, 1);
									else if (y > prevY)
										g.FillRect(x, prevY, 1, y - prevY + 1);
									else
										g.FillRect(x, y, 1, prevY - y + 1);

									prevY = y;
								}
							}
						}

						float labelX = 0;
						float labelWidth = 0;

						g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
						int[?] times = .(5, 15, 60, 5*60, 15*60);
						int findIdx = 0;
						int findTime = times[0];
						double prevTime = mPanel.mCurTimeSecs;

						bool wantOverTime = mOverDataTime != null;

						bool drawLabels = width > GS!(240);
						for (int idx = data.mSamples.Count - 1; idx >= 0; idx--)
						{
							double checkTime = timeData.mSamples[idx];

							if ((wantOverTime) && (checkTime <= mOverDataTime))
							{
								wantOverTime = false;

								//float x = idx + (float)((findTime - checkTime) / (prevTime - checkTime));

								float overIdx = idx; // + (float)((mOverDataTime - checkTime) / (prevTime - checkTime));

								float x = (int)((overIdx / (float)(data.mSamples.Count - 1)) * width);

								double value = mOverDataValue.Value;
								g.SetFont(DarkTheme.sDarkTheme.mSmallBoldFont);

								int deltaSecs = (int)(mPanel.mCurTimeSecs - mOverDataTime.Value);

								String drawStr = scope .();
								ValToString(value, drawStr);
								drawStr.AppendF(" at {}:{:00}", deltaSecs / 60, deltaSecs % 60);

								labelWidth = g.mFont.GetWidth(drawStr);

								labelX = Math.Clamp(x, labelWidth / 2, width - labelWidth / 2);

								g.DrawString(drawStr, labelX, -GS!(19), .Centered);

								using (g.PushColor(0x70FFFFFF))
									g.FillRect(x, 0, 1, height);
							}

							double timeDif = mPanel.mCurTimeSecs - checkTime;
							if (timeDif >= findTime)
							{
								double prevTimeDif = mPanel.mCurTimeSecs - prevTime;
								float addIdx = idx + (float)((timeDif - findTime) / (timeDif - prevTimeDif));
								
								float x = (int)((addIdx / (data.mSamples.Count - 1)) * width);

								using (g.PushColor(0xA0FFFFFF))
									g.FillRect(x, 0, 1, height);

								if (drawLabels)
								{
									uint32 color = 0xFFFFFFFF;
									if (Math.Abs(x - labelX) < labelWidth / 2 + GS!(20))
										color = 0x60FFFFFF;
									using (g.PushColor(color))
										g.DrawString(scope String()..AppendF("{}:{:00}", findTime / 60, findTime % 60), x + GS!(2), -GS!(20), .Centered);
								}
								
								if (++findIdx >= times.Count)
									break;
								findTime = times[findIdx];
							}

							prevTime = checkTime;
						}
					}

					// Draw segments

					double maxValue = mData.mMaxValue;
					double segLog10 = log10(maxValue);
					if (segLog10 > 0)
						segLog10 = (int)(segLog10);
					double segSize = Math.Pow(10, segLog10);

					if (maxValue >= 1'000'000)
					{
						double segLog2 = log2(maxValue);
						segLog2 = (int)(segLog2);
						segSize = Math.Pow(2, segLog2);
					}

					double numSegs = maxValue / segSize;


					if (numSegs < 2)
						segSize /= 4;
					else if (numSegs < 4)
						segSize /= 2;
					
					/*int kSize = 1024;
					if (segSize >= kSize * kSize * kSize)
						segSize = (int)(segSize / (kSize * kSize * kSize)) * (kSize * kSize * kSize);
					else if (segSize >= kSize * kSize)
						segSize = (int)(segSize / (kSize * kSize)) * (kSize * kSize);
					else if (segSize >= kSize)
						segSize = (int)(segSize / (kSize)) * (kSize);*/

					numSegs = (int)(maxValue / segSize + 0.95f);

					g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
					for (int segIdx = 1; segIdx < (int)numSegs; segIdx++)
					{
						bool drawLabels = width > GS!(120);
						double segValue = segIdx * segSize;

						float drawY = (int)((1 - segValue / maxValue) * (height - 1));
						using (g.PushColor(0x40FFFFFF))
							g.FillRect(0, drawY, width, 1);
						if (drawLabels)
						{
							var labelStr = scope String();
							ValToString(segValue, labelStr);
							g.DrawString(labelStr, -xOfs, drawY - GS!(10), .Right, xOfs - GS!(4));
						}
					}
				}
			}

			Rect GetDataRect()
			{
				return .(GS!(80), GS!(20), mWidth - GS!(80) - 2, mHeight - GS!(20) - 2);
			}

			public override void MouseLeave()
			{
				base.MouseLeave();
				mOverDataTime = null;
			}

			public override void MouseMove(float x, float y)
			{
				base.MouseMove(x, y);

				let dataRect = GetDataRect();
				if ((x >= dataRect.mX) && (x < dataRect.Right) && (!mData.mSamples.IsEmpty))
				{
					float overDataIdx = (x - dataRect.mX) / (float)dataRect.mWidth * mData.mSamples.Count;
					mOverDataTime = mTimes.Get(overDataIdx);
					mOverDataValue = mData.Get(overDataIdx);
				}
				else
					mOverDataTime = null;
			}

			public override void Draw(Graphics g)
			{
				base.Draw(g);

				let dataRect = GetDataRect();

				using (g.PushColor(0x20FFFFFF))
					g.FillRect(0, 0, mWidth, mHeight);

				DrawData(g, mData, mTimes, dataRect.mX, dataRect.mY, dataRect.mWidth, dataRect.mHeight);

				g.SetFont(DarkTheme.sDarkTheme.mSmallBoldFont);
				g.DrawString(mLabel, -GS!(0), -GS!(0), .Centered, dataRect.mX);
			}
		}

#region fields
		ScrollableWidget mScrollableWidget;
		Widget mContentWidget;

		Stopwatch mSampleStopwatch = new .() ~ delete _;
		int mSampleCPUTime;
		double mCurTimeSecs;

		const int cMaxSamples = 1024;
		Data mRunningTime = new .(0) ~ delete _;
		Data mUserTime = new .(6) ~ delete _;
		Data mWorkingMem = new .(0) ~ delete _;
		Data mVirtualMem = new .(0) ~ delete _;
		Data mDbgAllocMem = new .(0) ~ delete _;

		List<DataView> mDataViews = new List<DataView>() ~ delete _;
		DbgAllocListView mDbgAllocListView;
		DarkButton mRefreshButton;

		public this()
		{
			mSampleStopwatch.Start();
			/*for (int i < 10)
				AddSample(mUserTime, .(i, i));*/

			mScrollableWidget = new ScrollableWidget();
			mScrollableWidget.InitScrollbars(false, true);
			AddWidget(mScrollableWidget);

			mContentWidget = new Widget();
			mScrollableWidget.mScrollContentContainer.AddWidget(mContentWidget);
			mScrollableWidget.mScrollContent = mContentWidget;

			DataView dataView = new DataView("CPU");
			dataView.mPanel = this;
			dataView.mTimes = mRunningTime;
			dataView.mData = mUserTime;
			mContentWidget.AddWidget(dataView);
			mDataViews.Add(dataView);

			dataView = new DataView("VMem");
			dataView.mPanel = this;
			dataView.mTimes = mRunningTime;
			dataView.mData = mVirtualMem;
			mContentWidget.AddWidget(dataView);
			mDataViews.Add(dataView);

			dataView = new DataView("WkMem");
			dataView.mPanel = this;
			dataView.mTimes = mRunningTime;
			dataView.mData = mWorkingMem;
			mContentWidget.AddWidget(dataView);
			mDataViews.Add(dataView);

			dataView = new DataView("DbgAlloc");
			dataView.mPanel = this;
			dataView.mTimes = mRunningTime;
			dataView.mData = mDbgAllocMem;
			mContentWidget.AddWidget(dataView);
			mDataViews.Add(dataView);

			mRefreshButton = new DarkButton();
			mRefreshButton.Label = "Get Debug Alloc Information";
			mRefreshButton.mOnMouseClick.Add(new (mouseArgs) =>
				{
					if (gApp.mDebugger.mIsRunning)
					{
						var info = scope String();
						//let sampler = Profiler.StartSampling().GetValueOrDefault();
						gApp.mDebugger.GetDbgAllocInfo(info);
						/*if (sampler != default)
							sampler.Dispose();*/

						PopulateDbgAllocInfo(info);
					}
				});
			mContentWidget.AddWidget(mRefreshButton);

			mDbgAllocListView = new DbgAllocListView();

			ListViewColumn column = mDbgAllocListView.AddColumn(300, "Type");
			column.mMinWidth = 100;

			column = mDbgAllocListView.AddColumn(80, "Count");
			column.mMinWidth = 60;
			column = mDbgAllocListView.AddColumn(80, "Size");
			column.mMinWidth = 60;

			mDbgAllocListView.mOnItemMouseDown.Add(new => ListViewItemMouseDown);
			mDbgAllocListView.mOnItemMouseClicked.Add(new => ListViewItemMouseClicked);
			mDbgAllocListView.mOnKeyDown.Add(new => ListViewKeyDown_ShowMenu);

			mContentWidget.AddWidget(mDbgAllocListView);

			ResizeComponents();
		}

		void Clear()
		{
			mSampleCPUTime = 0;
			mCurTimeSecs = 0;
			mUserTime.Clear();
			mRunningTime.Clear();
			mVirtualMem.Clear();
			mWorkingMem.Clear();
			mDbgAllocMem.Clear();
			mDbgAllocListView.GetRoot().Clear();
		}

		void ResizeComponents()
		{
			mScrollableWidget.Resize(0, 0, mWidth, mHeight);

			float width = Math.Max(mScrollableWidget.mWidth - GS!(20), 0);

			float dataViewWidth = Math.Max((width - GS!(24)) / 2, 100);
			float dataViewHeight = (int)(dataViewWidth * 0.5f);

			for (let dataView in mDataViews)
			{
				int col = @dataView.Index % 2;
				int row = @dataView.Index / 2;

				
				dataView.Resize(col * (dataViewWidth + GS!(8)) + GS!(8), row * (dataViewHeight + GS!(8)) + GS!(8), dataViewWidth, dataViewHeight);
			}

			float curY = dataViewHeight * 2 + GS!(24);

			mRefreshButton.Resize(GS!(8), curY, GS!(240), GS!(20));
			mDbgAllocListView.Resize(GS!(0), curY + GS!(20), Math.Max(width - GS!(0), 120), mDbgAllocListView.mScrollContent.mHeight + GS!(28));

			mContentWidget.Resize(0, 0, width, curY + mDbgAllocListView.mScrollContent.mHeight + GS!(48));
			mScrollableWidget.UpdateScrollbars();
			mScrollableWidget.UpdateContentPosition();
		}

		void PopulateDbgAllocInfo(String info)
		{
			//Debug.WriteLine(info);

			mDbgAllocListView.GetRoot().Clear();

			String totalLabel = "Total Debug Allocations";

			int totalSize = 0;
			int totalCount = 0;

			DbgAllocListViewItem AddItem(StringView label, int allocCount, int allocSize)
			{
				var newItem = (DbgAllocListViewItem)mDbgAllocListView.GetRoot().CreateChildItem();
				newItem.mAllocCount = allocCount;
				newItem.mAllocSize = allocSize;
				newItem.Label = label;

				var subItem = newItem.GetOrCreateSubItem(1);
				subItem.Label = scope String()..AppendF("{}", allocCount);

				subItem = newItem.GetOrCreateSubItem(2);
				subItem.Label = scope String()..AppendF("{}k", (allocSize + 1023)/1024);
				return newItem;
			}

			for (let line in info.Split('\n'))
			{
				var elemItr = line.Split('\t');
				switch (elemItr.GetNext().Value)
				{
				case "type":
					int typeId = int.Parse(elemItr.GetNext().Value).Value;
					StringView name = elemItr.GetNext().Value;
					int allocCount = int.Parse(elemItr.GetNext().Value).Value;
					int allocSize = int.Parse(elemItr.GetNext().Value).Value;

					totalCount += allocCount;
					totalSize += allocSize;

					var newItem = AddItem(name, allocCount, allocSize);
					newItem.mTypeId = typeId;
					//newItem.MakeParent();
				}
			}

			AddItem(totalLabel, totalCount, totalSize);

			mDbgAllocListView.GetRoot().mChildItems.Sort(scope (lhs, rhs) =>
				{
					return ((DbgAllocListViewItem)rhs).mAllocSize <=> ((DbgAllocListViewItem)lhs).mAllocSize;
				});
			mDbgAllocListView.GetRoot().ResizeComponents(0);
			mDbgAllocListView.mAutoFocus = true;
			mDbgAllocListView.mOnLostFocus.Add(new (evt) =>
				{
					if (!mShowingRightClickMenu)
						mDbgAllocListView.GetRoot().SelectItemExclusively(null);
				});

			mDbgAllocListView.UpdateListSize();
			ResizeComponents();
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);
			
		}

		public void UpdateStats()
		{
			/*int addIdx = mUpdateCnt;
			AddSample(mUserTime, .(addIdx, addIdx));*/

			scope AutoBeefPerf("DiagnosticsPanel.UpdateStats");

			String procInfo = scope .();
			gApp.mDebugger.GetProcessInfo(procInfo);

			if (gApp.mDebugger.IsPaused())
			{
				mSampleStopwatch.Restart();
				return;
			}

			if (procInfo.IsEmpty)
			{
				Clear();
				return;
			}

			if (gApp.mUpdateCnt % 5 != 0)
			{
				return;
			}

			MarkDirty();
			if (mSampleStopwatch.ElapsedMilliseconds < 10)
				return;

			// 5 Hz sample rate
			/*if (elapsedTime < 100)
				return;*/
			mSampleStopwatch.Restart();

			int runningTime = 0;
			int virtualMem = 0;
			int workingMem = 0;
			int kernelTime = 0;
			int userTime = 0;

			for (let line in procInfo.Split('\n'))
			{
				if (line.IsEmpty)
					break;

				var lineEnum = line.Split('\t');
				let category = lineEnum.GetNext().Value;
				let valueSV = lineEnum.GetNext().Value;
				let value = int64.Parse(valueSV).Value;

				switch (category)
				{
				case "WorkingMemory": workingMem = value;
				case "VirtualMemory": virtualMem = value;
				case "RunningTime": runningTime = value;
				case "KernelTime": kernelTime = value;
				case "UserTime": userTime = value;
				}
			}

			int cpuTime = kernelTime + userTime;

			double newTimeSecs = runningTime / (double)(10*1000*1000);
			double elapsedTime = newTimeSecs - mCurTimeSecs;

			mCurTimeSecs = newTimeSecs;
			
			mRunningTime.AddSample(mCurTimeSecs);
			mUserTime.AddSample((cpuTime - mSampleCPUTime) * 100 / elapsedTime / 10000000.0f);
			mWorkingMem.AddSample(workingMem);
			mVirtualMem.AddSample(virtualMem);
			mDbgAllocMem.AddSample(gApp.mDebugger.GetDbgAllocHeapSize());

			mSampleCPUTime = cpuTime;

			/*int addIdx = mUpdateCnt;
			AddSample(mUserTime, .(addIdx, addIdx));*/
		}

		public override void Update()
		{
			base.Update();
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			ResizeComponents();
		}

		public override void Serialize(StructuredData data)
		{
		    base.Serialize(data);

		    data.Add("Type", "DiagnosticsPanel");
		}
	}
}
