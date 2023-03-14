using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using Beefy;
using Beefy.widgets;
using Beefy.theme;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.events;
using Beefy.utils;
using IDE.Debugger;


namespace IDE.ui
{	
	class ProfilePanel : Panel
	{
		class ProfileListViewItem : DarkListViewItem
		{
			public int32 mSelfSamples;
			public int32 mChildSamples;

			float ArrowCenterY
			{
				get
				{
					return DarkTheme.sUnitSize * 0.5f + GS!(4) + 1;
				}
			}

			protected override void DrawChildren(Graphics g, int itemStart, int itemEnd)
			{
				base.DrawChildren(g, itemStart, itemEnd);

				if (mParentItem == null)
					return;

				float barWidth = (int)GS!(1.5f);

				let lastItem = mChildItems.Back;
				if (!lastItem.mVisible)
					return;

				let parent = (DarkListView)mListView;
				float absX = (mDepth - 1) * parent.mChildIndent + GS!(12);
				if (absX >= parent.mColumns[0].mWidth)
					return;

				using (g.PushColor(0xFFB0B0B0))
					g.FillRect((int)ArrowCenterY, GS!(14), barWidth, (int)lastItem.mY - (int)GS!(4.8f));
			}

			public override void DrawAll(Graphics g)
			{
				float barWidth = (int)GS!(1.5f);

				var listView = (DarkListView)mListView;
				float xStart = mDepth * listView.mChildIndent - GS!(12);
				if (xStart < listView.mColumns[0].mWidth)
				{
					if ((mVisible) && (mColumnIdx == 0) && (mX > 10))
					{
						using (g.PushColor(0xFFB0B0B0))
						{
							float horzWidth = GS!(13) - 3 - barWidth;
							if (mOpenButton == null)
								horzWidth += GS!(4);
		                    g.FillRect(-mX + (int)(ArrowCenterY + GS!(0.7f)), GS!(9), horzWidth, barWidth);
						}
					}
				}

				base.DrawAll(g);
			}
		}

		class ProfileListView : DarkListView
		{
			public this()
			{
				//mShowLineGrid = true;
			}

			protected override ListViewItem CreateListViewItem()
			{
				return new ProfileListViewItem();
			}

			protected override void SetScaleData()
			{
				base.SetScaleData();

				mIconX = GS!(4);
				mOpenButtonX = GS!(4);
				mLabelX = GS!(23);
				mChildIndent = GS!(16);
				mHiliteOffset = GS!(-2);
			}
		}

		public class SessionComboBox : DarkComboBox
		{
			public bool mIsRecording;

			public override void Draw(Graphics g)
			{
				base.Draw(g);
			}

			public override void Update()
			{
				base.Update();
			}

			public override void DrawAll(Graphics g)
			{
				base.DrawAll(g);

				mLabelX = mIsRecording ? GS!(24) : GS!(8);
				if (mIsRecording)
					g.Draw(DarkTheme.sDarkTheme.GetImage(.RedDot), GS!(4), GS!(0));
			}
		}

        ProfileListView mListView;
		public SessionComboBox mSessionComboBox;
		public DarkComboBox mThreadComboBox;
		DarkButton mStopButton;
        public bool mThreadsDirty = true;
        public int32 mThreadIdx;
		public int32 mDisabledTicks;
		public DbgProfiler mProfiler;
		public List<int32> mThreadList = new List<int32>() ~ delete _;
		public uint32 mTickCreated;
		public int32 mCurIdx;
		public DbgProfiler mUserProfiler;
		public DbgProfiler.Overview mShowOverview ~ delete _;
		public int mOverviewAge;
		public bool mAwaitingLoad;
		public bool mIsSamplingHidden;

		public List<DbgProfiler> mProfilers = new List<DbgProfiler>() ~ DeleteContainerAndItems!(_);

		const String cStartProfilingCmd = " < Start Profiling > ";
		const String cStartProfilingExCmd = " < Profile... > ";

		public bool IsSamplingHidden
		{
			get
			{
				return mIsSamplingHidden;
			}
		}

        public this()
        {
			mSessionComboBox = new SessionComboBox();
			mSessionComboBox.mPopulateMenuAction.Add(new => PopulateSessionList);
			AddWidget(mSessionComboBox);
			mSessionComboBox.Label = cStartProfilingCmd;
			mTabWidgets.Add(mSessionComboBox);

			mStopButton = new DarkButton();
			mStopButton.Label = "Stop";
			AddWidget(mStopButton);
			mStopButton.mOnMouseClick.Add(new (evt) =>
				{
					if (mProfiler != null)
						mProfiler.Stop();
				});

			mThreadComboBox = new DarkComboBox();            
			mThreadComboBox.mPopulateMenuAction.Add(new => PopulateThreadList);
			AddWidget(mThreadComboBox);
			mTabWidgets.Add(mThreadComboBox);

            mListView = new ProfileListView();
            mListView.InitScrollbars(true, true);
            mListView.mHorzScrollbar.mPageSize = GS!(100);
            mListView.mHorzScrollbar.mContentSize = GS!(500);
            mListView.mVertScrollbar.mPageSize = GS!(100);
            mListView.mVertScrollbar.mContentSize = GS!(500);
            mListView.UpdateScrollbars();
			mListView.mOnMouseDown.Add(new (mouseArgs) =>
				{
					SetFocus();
				});
			mListView.mOnItemMouseDown.Add(new => ValueMouseDown);
			mTabWidgets.Add(mListView);

            //mListView.mDragEndHandler += HandleDragEnd;
            //mListView.mDragUpdateHandler += HandleDragUpdate;

            AddWidget(mListView);

            //mListView.mMouseDownHandler += ListViewMouseDown;

            ListViewColumn column = mListView.AddColumn(300, "Function");
            column.mMinWidth = 100;

            column = mListView.AddColumn(80, "Total CPU %");
            column.mMinWidth = 60;
            column = mListView.AddColumn(80, "Self CPU %");
            column.mMinWidth = 60;
			column = mListView.AddColumn(90, "Total CPU (ms)");
			column.mMinWidth = 60;
			column = mListView.AddColumn(90, "Self CPU (ms)");
			column.mMinWidth = 60;
            //RebuildUI();
        }

		public void Clear()
		{
			mUserProfiler = null;
			mProfiler = null;
			ClearAndDeleteItems(mProfilers);

			mSessionComboBox.Label = cStartProfilingCmd;
			mThreadComboBox.Label = "";
			mListView.GetRoot().Clear();
			mThreadList.Clear();
			DeleteAndNullify!(mShowOverview);
			mAwaitingLoad = false;
		}

		public void StartProfiling(int threadId, String desc, int sampleRate)
		{
			if (mUserProfiler != null)
				return;

			if (!gApp.mDebugger.mIsRunning)
			{
				gApp.RunWithCompiling();

				gApp.QueueProfiling(threadId, desc, sampleRate);
				return;
			}

			mUserProfiler = gApp.mDebugger.StartProfiling(threadId, desc, sampleRate);
			mUserProfiler.mIsManual = true;
			Add(mUserProfiler);
			Show(mUserProfiler);
		}

		public void StartProfiling()
		{
			StartProfiling(0, "", gApp.mSettings.mDebuggerSettings.mProfileSampleRate);
		}

		public void StopProfiling()
		{
			if (mUserProfiler == null)
				return;

			mUserProfiler.Stop();
			mUserProfiler = null;
		}

		void PopulateSessionList(Menu menu)
		{
			var item = menu.AddItem(cStartProfilingCmd);
			item.mOnMenuItemSelected.Add(new (item) =>
				{
					StartProfiling();
				});

			item = menu.AddItem(cStartProfilingExCmd);
			item.mOnMenuItemSelected.Add(new (item) =>
				{
					var profileDialog = new ProfileDialog();
					profileDialog.PopupWindow(mWidgetWindow);
					//StartProfilingEx();
				});

			for (int profilerIdx = mProfilers.Count - 1; profilerIdx >= 0; profilerIdx--)
			{
				var profiler = mProfilers[profilerIdx];
				var label = scope String();
				profiler.GetLabel(label);
				item = menu.AddItem(label);

				var overview = scope DbgProfiler.Overview();
				profiler.GetOverview(overview);
				if (overview.IsSampling)
					item.mIconImage = DarkTheme.sDarkTheme.GetImage(.RedDot);
				item.mOnMenuItemSelected.Add(new (item) =>
					{
						mSessionComboBox.Label = item.mLabel;
						Show(profiler);
					});
			}
		}

		struct ThreadEntry : this(int32 mThreadId, int32 mCPUUsage, StringView mName)
		{
		}

		void PopulateThreadList(Menu menu)
		{
			if (mProfiler == null)
				return;

			if (mProfiler.IsSampling)
				return;

			var subItem = menu.AddItem("All Threads");
			subItem.mOnMenuItemSelected.Add(new (item) => { Show(0, .()); });

			var threadListStr = scope String();
			mProfiler.GetThreadList(threadListStr);

			List<ThreadEntry> entries = scope .();
			for (var entry in threadListStr.Split('\n'))
			{
				if (entry.Length == 0)
					continue;

				var dataItr = entry.Split('\t');

				ThreadEntry threadEntry = default;
				threadEntry.mThreadId = int32.Parse(dataItr.GetNext());
				threadEntry.mCPUUsage = int32.Parse(dataItr.GetNext());
				threadEntry.mName = dataItr.GetNext();
				entries.Add(threadEntry);
			}

			entries.Sort(scope (lhs, rhs) =>
				{
					int cmp = rhs.mCPUUsage <=> lhs.mCPUUsage;
					if (cmp == 0)
						cmp = lhs.mThreadId <=> rhs.mThreadId;
					return cmp;
				});

			for (var entry in entries)
			{
				String threadStr = null;
				var str = scope String();
				str.AppendF("{0}", entry.mThreadId);
				str.AppendF($" ({entry.mCPUUsage}%)");
				if (!entry.mName.IsEmpty)
				{
					threadStr = new String(entry.mName);
					str.AppendF($" - {entry.mName}");
				}

				subItem = menu.AddItem(str);
				subItem.mOnMenuItemSelected.Add(new (item) => { Show(entry.mThreadId, threadStr); } ~ delete threadStr);
			}
		}

		void OpenHot(ListViewItem lvi, int32 minSamples)
		{
			lvi.WithItems(scope (childItem) =>
				{
					var profileListViewItem = childItem as ProfileListViewItem;
					if ((profileListViewItem.mSelfSamples + profileListViewItem.mChildSamples >= minSamples) && (profileListViewItem.IsParent))
					{
						profileListViewItem.Open(true, true);
						OpenHot(childItem, minSamples);
					}
				});
		}

		public void Show(int32 threadId, StringView threadName)
		{
			mTickCreated = Utils.GetTickCount();

			if (threadId == 0)
				mThreadComboBox.Label = "All Threads";
			else
			{
				var threadStr = scope String();
				threadStr.AppendF("{0}", threadId);
				if (!threadName.IsEmpty)
					threadStr.AppendF(" - {0}", threadName);
				mThreadComboBox.Label = threadStr;
			}

			mThreadList.Clear();

			var str = scope String();
			mProfiler.GetCallTree(threadId, str, false);
			
			List<DarkListViewItem> itemStack = scope List<DarkListViewItem>();
			DarkListViewItem curItem = (DarkListViewItem)mListView.GetRoot();

			mListView.GetRoot().Clear();

			var overview = scope DbgProfiler.Overview();
			mProfiler.GetOverview(overview);

			int32 totalSamples = 0;
			float sampleTimeMult = 1000.0f / overview.mSamplesPerSecond; // 1ms per sample

			for (var lineView in str.Split('\n'))
			{
				var line = scope String(lineView);
				if (line.Length == 0)
					continue;
				if (line == "-")
				{
					curItem = itemStack.PopBack();

					curItem.mChildItems.Sort(scope (baseA, baseB) => 
			        	{
							var a = (ProfileListViewItem)baseA;
							var b = (ProfileListViewItem)baseB;
							return (b.mChildSamples + b.mSelfSamples) - (a.mChildSamples + a.mSelfSamples);
			            });

					continue;
				}
				
				var dataList = scope List<StringView>(line.Split('\t'));
				
				ProfileListViewItem newItem = (ProfileListViewItem)curItem.CreateChildItem();
				newItem.mLabel = new String(dataList[0]);				
				//newItem.mOnMouseDown.Add(new => ValueClicked);

				var selfStr = scope String(dataList[1]);
				var childStr = scope String(dataList[2]);

				newItem.mSelfSamples = int32.Parse(selfStr);
				newItem.mChildSamples = int32.Parse(childStr);

				if (totalSamples == 0)
					totalSamples = newItem.mChildSamples;

				var subItem = newItem.CreateSubItem(1);
				var tempStr = scope String();
				if (totalSamples == 0)
					tempStr.Append("0.00%");
				else
					tempStr.AppendF("{0:0.00}%", (newItem.mSelfSamples + newItem.mChildSamples) * 100.0f / totalSamples);
				subItem.Label = tempStr;
				//subItem.mOnMouseDown.Add(new => ValueClicked);

				subItem = newItem.CreateSubItem(2);
				tempStr.Clear();
				if (totalSamples == 0)
					tempStr.Append("0.00%");
				else
					tempStr.AppendF("{0:0.00}%", newItem.mSelfSamples * 100.0f / totalSamples);
				subItem.Label = tempStr;
				//subItem.mOnMouseDown.Add(new => ValueClicked);

				subItem = newItem.CreateSubItem(3);
				tempStr.Clear();
				tempStr.AppendF("{0}ms", (int32)((newItem.mSelfSamples + newItem.mChildSamples) * sampleTimeMult + 0.5f));
				subItem.Label = tempStr;
				//subItem.mOnMouseDown.Add(new => ValueClicked);

				subItem = newItem.CreateSubItem(4);
				tempStr.Clear();
				tempStr.AppendF("{0}ms", (int32)(newItem.mSelfSamples * sampleTimeMult + 0.5f));
				subItem.Label = tempStr;
				//subItem.mOnMouseDown.Add(new => ValueClicked);

				itemStack.Add(curItem);
				curItem = newItem;
			}

			OpenHot(mListView.GetRoot(), (.)(totalSamples * 0.05f));
		}

		public void Add(DbgProfiler profiler)
		{
			profiler.mIdx = mCurIdx++;
			mProfilers.Add(profiler);
		}

		public void UpdateOverview(out bool changed)
		{
			changed = false;
			int prevActualSamples = (mShowOverview?.mTotalActualSamples).GetValueOrDefault();

			DeleteAndNullify!(mShowOverview);

			if (mProfiler == null)
				return;

			mShowOverview = new DbgProfiler.Overview();
			mProfiler.GetOverview(mShowOverview);
			changed = mShowOverview.mTotalActualSamples != prevActualSamples;
			mOverviewAge = 0;
		}

		public void Show(DbgProfiler profiler)
		{
			mThreadList.Clear();

			mProfiler = profiler;

			UpdateOverview(var changed);

			var overview = scope DbgProfiler.Overview();
			profiler.GetOverview(overview);

			var label = scope String();
			profiler.GetLabel(label);
			mSessionComboBox.Label = label;

			if (overview.IsSampling)
			{
				mAwaitingLoad = true;
				mThreadComboBox.Label = "";
				mListView.GetRoot().Clear();
				ResizeComponents();
				return;
			}

			mAwaitingLoad = false;
			var threadListStr = scope String();
			mProfiler.GetThreadList(threadListStr);

			for (var entry in threadListStr.Split('\n'))
			{
				if (entry.Length == 0)
					continue;
				var dataItr = entry.Split('\t');
				int32 threadId = int32.Parse(dataItr.GetNext());
				
				mThreadList.Add(threadId);
			}

			if (mThreadList.Count == 0)
				return;

			int32 threadId = mThreadList[0];
			Show(threadId, .());
			ResizeComponents();
		}

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "ProfilePanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }

        void ValueMouseDown(ListViewItem item, float x, float y, int32 btnNum, int32 btnCount)
        {
#unwarn
            DarkListViewItem baseItem = (DarkListViewItem)item.GetSubItem(0);

			ListViewItemMouseDown(item, x, y, btnNum, btnCount);

            if ((btnNum == 0) && (btnCount > 1))
            {
                /*for (int childIdx = 0; childIdx < mListView.GetRoot().GetChildCount(); childIdx++)
                {
                    var checkListViewItem = mListView.GetRoot().GetChildAtIndex(childIdx);
                    checkListViewItem.IconImage = null;
                }

                int selectedIdx = mListView.GetRoot().GetIndexOfChild(item);

                int threadId = int.Parse(item.mLabel);
                IDEApp.sApp.mDebugger.SetActiveThread(threadId);                
                IDEApp.sApp.mDebugger.mCallStackDirty = true;
                IDEApp.sApp.mCallStackPanel.MarkCallStackDirty();
                IDEApp.sApp.mCallStackPanel.Update();
                IDEApp.sApp.mWatchPanel.MarkWatchesDirty(true);
                IDEApp.sApp.mAutoWatchPanel.MarkWatchesDirty(true);
                IDEApp.sApp.mMemoryPanel.MarkViewDirty();
                IDEApp.sApp.ShowPCLocation(IDEApp.sApp.mDebugger.mSelectedCallStackIdx, false, true);
                
                item.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ArrowRight);*/
            }
        }

		void ResizeComponents()
		{
			mThreadComboBox.Resize(GS!(80), GS!(22), GS!(220), GS!(24));
			mListView.Resize(0, GS!(44), mWidth, Math.Max(mHeight - GS!(44), 0));

			if (mSessionComboBox.mIsRecording)
			{
				mStopButton.mVisible = true;
				mStopButton.Resize(GS!(28), GS!(1), GS!(52), GS!(22));
				//mSessionComboBox.Resize(GS!(80) + GS!(42), GS!(1), GS!(220) - GS!(42), GS!(24));
				mSessionComboBox.Resize(GS!(80), GS!(1), GS!(220), GS!(24));
			}
			else
			{
				mStopButton.mVisible = false;
				mSessionComboBox.Resize(GS!(80), GS!(1), GS!(220), GS!(24));
			}
		}

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);

			ResizeComponents();
        }

        public void MarkThreadsDirty()
        {
            mThreadsDirty = true;
        }

        public override void LostFocus()
        {
            base.LostFocus();
            mListView.GetRoot().SelectItemExclusively(null);
        }

        public override void Update()
        {
            base.Update();

			if (mUserProfiler != null)
			{
				if (!mUserProfiler.IsSampling)
					mUserProfiler = null;
			}

			if (mProfiler == null)
			{
				if (!mProfilers.IsEmpty)
				{
					var profiler = mProfilers.Back;
					Show(profiler);
					MarkDirty();
				}
			}

            if (mProfiler != null)
			{
				++mOverviewAge;
				if (mOverviewAge >= 15)
				{
					UpdateOverview(var changed);
					if (changed)
						MarkDirty();
				}

				if (mUpdateCnt % 60 == 0)
					MarkDirty();

				if ((!mProfiler.IsSampling) && (mAwaitingLoad))
					Show(mProfiler);
			}

			if (mShowOverview != null)
				mSessionComboBox.mIsRecording = mShowOverview.IsSampling;
			else
				mSessionComboBox.mIsRecording = false;
			ResizeComponents();
        }

		public void ForcedUpdate()
		{
			bool isSamplingHidden = false;
			if ((mWidgetWindow == null) || (mProfiler == null) || (!mProfiler.IsSampling))
			{
				for (var profile in mProfilers)
					if (profile.IsSampling)
						isSamplingHidden = true;
			}

			if (mIsSamplingHidden != isSamplingHidden)
			{
				mIsSamplingHidden = isSamplingHidden;
				gApp.MarkDirty();
			}
		}

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);
        }

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
			g.DrawString("Session", mSessionComboBox.mX - GS!(2), mSessionComboBox.mY, .Right);
			g.DrawString("Thread", mThreadComboBox.mX - GS!(2), mThreadComboBox.mY, .Right);

			if ((mProfiler != null) && (mShowOverview != null))
			{
				g.DrawString(StackStringFormat!("Rate: {0} Hz", mShowOverview.mSamplesPerSecond), GS!(320), GS!(2));
				int32 seconds = (mShowOverview.mRecordedTicks / 1000);
				g.DrawString(StackStringFormat!("Length: {0}:{1:00}.{2}", seconds / 60, seconds % 60, (mShowOverview.mRecordedTicks % 1000)/100), GS!(320), GS!(22));

				seconds = (mShowOverview.mEndedTicks / 1000);
				if (seconds > 60*60)
					g.DrawString(StackStringFormat!("Age: {0}:{1:00}:{2:00}", seconds / 60 / 60, (seconds / 60) % 60, seconds % 60), GS!(420), GS!(22));
				else
					g.DrawString(StackStringFormat!("Age: {0}:{1:00}", seconds / 60, seconds % 60), GS!(420), GS!(22));

				g.DrawString(StackStringFormat!("Samples: {0}", mShowOverview.mTotalActualSamples), GS!(420), GS!(2));
				g.DrawString(StackStringFormat!("Missed Samples: {0}", mShowOverview.mTotalVirtualSamples - mShowOverview.mTotalActualSamples), GS!(550), GS!(2));
			}

			if (mTickCreated != 0)
			{
				
				/*String text = scope String();
				text.AppendF("{0}s ago", (int32)(Utils.GetTickCount() - mTickCreated) / 1000);
				g.DrawString(text, mWidth - 4, 2, FontAlign.Right);*/
			}
		}

		public override void FocusForKeyboard()
		{
			base.FocusForKeyboard();
			mSessionComboBox.SetFocus();
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);
			SetFocus();
		}
    }
}
