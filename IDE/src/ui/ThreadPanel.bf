using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
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
	public class ThreadListViewItem : DarkListViewItem
	{
		public int32 mThreadId;
		public bool mFrozen;

		override public bool Selected
		{
			set
			{
				if (mIsSelected != value)
				{
					var threadListView = (ThreadListView)mListView;
					if (threadListView.mThreadPanel.mCallstackPopup != null)
						threadListView.mThreadPanel.mCallstackPopup.Close();
				}
				base.Selected = value;
			}
		}

		public override void Draw(Graphics g)
		{
			if (mColumnIdx == 2)
			{
				float width = mListView.mParent.mWidth - LabelX;

				//

				g.SetFont(DarkTheme.sDarkTheme.mSmallFont);
				g.DrawString(mLabel, GS!(6), GS!(0), .Left, width - GS!(42), .Ellipsis);

				g.Draw(DarkTheme.sDarkTheme.GetImage(.ComboEnd), width - GS!(36), (mHeight - GS!(24)) / 2);
				return;
			}

			base.Draw(g);

			//var threadPanel = ((ThreadListView)mListView).mThreadPanel;

			if (mColumnIdx == 0)
			{
				if (mFrozen)
					g.Draw(DarkTheme.sDarkTheme.GetImage(.IconError), 16, 0);
			}
		}

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
		}

		public void PopupCallStackPanel()
		{
			var baseItem = (ThreadListViewItem)GetSubItem(0);
			mListView.GetRoot().SelectItemExclusively(baseItem);
			int32 threadId = baseItem.mThreadId;

			var threadListView = (ThreadListView)mListView;

			if (threadListView.mThreadPanel.mThreadCallstackJustClosed == threadId)
				return;

			for (var window in gApp.mWindows)
			{
				if (var widgetWindow = window as WidgetWindow)
				{
					if (var panelPopup = widgetWindow.mRootWidget as PanelPopup)
					{
						if (var callstackPanel = panelPopup.mPanel as CallStackPanel)
						{
							if (callstackPanel.mThreadId == threadId)
							{
								panelPopup.Close();
								return;
							}
						}
					}
				}
			}

			CallStackPanel callStackPanel = new CallStackPanel();
			callStackPanel.mThreadId = threadId;

			float width = mListView.mParent.mWidth - LabelX;

			PanelPopup panelPopup = new PanelPopup();
			threadListView.mThreadPanel.mCallstackPopup = panelPopup;
			panelPopup.mWindowFlags = .ClientSized | .DestAlpha | .Resizable;
			panelPopup.Init(callStackPanel, this, GS!(12), mHeight, width + GS!(24), GS!(192));
			panelPopup.mOnRemovedFromParent.Add(new (widget, prevParent, widgetWindow) =>
				{
					threadListView.mThreadPanel.mCallstackPopup = null;
					threadListView.SetFocus();
				});
			callStackPanel.mListView.mOnKeyDown.Add(new (keyEvent) =>
				{
					if (keyEvent.mKeyCode == .Left)
						panelPopup.Close();
				});
			callStackPanel.mWidgetWindow.SetForeground();
			callStackPanel.mListView.SetFocus();
			callStackPanel.Update();
			var root = callStackPanel.mListView.GetRoot();
			if (root.GetChildCount() > 0)
				root.GetChildAtIndex(0).Focused = true;

			panelPopup.mOnRemovedFromParent.Add(new (widget, prevParent, widgetWindow) =>
				{
					threadListView.mThreadPanel.mThreadCallstackJustClosed = threadId;
				});
		}

		public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
		{
			base.MouseDown(x, y, btn, btnCount);

			if ((x >= mListView.mParent.mWidth - LabelX - GS!(34)) && (mParentItem != null))
			{
				PopupCallStackPanel();
			}
		}

		public override bool WantsTooltip(float mouseX, float mouseY)
		{
			if ((mColumnIdx == 2) && (mouseX >= mListView.mParent.mWidth - LabelX - GS!(34)))
				return false;
			return base.WantsTooltip(mouseX, mouseY);
		}
	}

	public class ThreadListView : DarkListView
	{
		public ThreadPanel mThreadPanel;

		protected override ListViewItem CreateListViewItem()
		{
			return new ThreadListViewItem();
		}

		public override void KeyDown(KeyCode keyCode, bool isRepeat)
		{
			base.KeyDown(keyCode, isRepeat);

			switch (keyCode)
			{
			case .Right:
				let focusedItem = GetRoot().FindFocusedItem();
				if (focusedItem != null)
				{
					let callstackItem = (ThreadListViewItem)focusedItem.GetSubItem(2);
					callstackItem.PopupCallStackPanel();
				}
			case .Apps:
				mThreadPanel.[Friend]ShowRightClickMenu(this);
			default:
			}
		}
	}

    public class ThreadPanel : Panel
    {
        public ThreadListView mListView;
        public bool mThreadsDirty = true;
		public int32 mDisabledTicks;
		public bool mWantsKeyboardFocus;
		public PanelPopup mCallstackPopup;
		public bool mDeselectOnFocusLost = true;
		public int mThreadCallstackJustClosed = -1;

        public this()
        {
            mListView = new ThreadListView();
			mListView.mShowGridLines = true;
			mListView.mThreadPanel = this;
            mListView.InitScrollbars(true, true);
            mListView.mHorzScrollbar.mPageSize = GS!(100);
            mListView.mHorzScrollbar.mContentSize = GS!(500);
            mListView.mVertScrollbar.mPageSize = GS!(100);
            mListView.mVertScrollbar.mContentSize = GS!(500);
			mListView.mAutoFocus = true;
			mListView.mOnLostFocus.Add(new (evt) =>
				{
					if ((mCallstackPopup == null) && (mDeselectOnFocusLost))
						mListView.GetRoot().SelectItemExclusively(null);
				});
            mListView.UpdateScrollbars();

            //mListView.mDragEndHandler += HandleDragEnd;
            //mListView.mDragUpdateHandler += HandleDragUpdate;

            AddWidget(mListView);

            //mListView.mMouseDownHandler += ListViewMouseDown;

            ListViewColumn column = mListView.AddColumn(100, "Id");
            column.mMinWidth = GS!(100);

            column = mListView.AddColumn(GS!(200), "Name");
            column.mMinWidth = GS!(100);
            column = mListView.AddColumn(GS!(200), "Location");
            column.mMinWidth = GS!(100);
			
            //RebuildUI();

			SetScaleData();
        }

		void SetScaleData()
		{
			mListView.mIconX = GS!(0);
			mListView.mOpenButtonX = GS!(4);
			mListView.mLabelX = GS!(32);
			mListView.mChildIndent = GS!(16);
			mListView.mHiliteOffset = GS!(-2);
		}

		public override void RemovedFromParent(Widget previousParent, WidgetWindow window)
		{
			if (mCallstackPopup != null)
				mCallstackPopup.Close();
			base.RemovedFromParent(previousParent, window);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			SetScaleData();
		}

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "ThreadPanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }

		protected override void ShowRightClickMenu(Widget relWidget, float x, float y)
		{
			mDeselectOnFocusLost = false;
			defer:: { mDeselectOnFocusLost = true; }

			if (mListView.GetRoot().FindFirstSelectedItem() == null)
				return;

			Menu menu = new Menu();

			bool hasFrozen = false;
			bool hasThawed = false;
			mListView.GetRoot().WithSelectedItems(scope [&] (item) =>
				{
					var threadListViewItem = (ThreadListViewItem)item;
					if (threadListViewItem.mFrozen)
						hasFrozen = true;
					else
						hasThawed = true;
				});

			Menu item;
			if (hasThawed)
			{
				item = menu.AddItem("Freeze");
				item.mOnMenuItemSelected.Add(new (menu) =>
					{
						if (!gApp.mDebugger.IsPaused())
							return;
						mListView.GetRoot().WithSelectedItems(scope (item) =>
							{
								var threadItem = (ThreadListViewItem)item;
								gApp.mDebugger.FreezeThread(threadItem.mThreadId);
								mThreadsDirty = true;
							});
					});
			}

			item = menu.AddItem("Freeze All Except");
			item.mOnMenuItemSelected.Add(new (menu) =>
				{
					if (!gApp.mDebugger.IsPaused())
						return;
					mListView.GetRoot().WithItems(scope (item) =>
						{
							var threadItem = (ThreadListViewItem)item;
							if (item.Selected)
								gApp.mDebugger.ThawThread(threadItem.mThreadId);
							else
								gApp.mDebugger.FreezeThread(threadItem.mThreadId);
							mThreadsDirty = true;
						});
				});

			if (hasFrozen)
			{
				item = menu.AddItem("Thaw");
				item.mOnMenuItemSelected.Add(new (menu) =>
					{
						if (!gApp.mDebugger.IsPaused())
							return;
						mListView.GetRoot().WithSelectedItems(scope (item) =>
							{
								var threadItem = (ThreadListViewItem)item;
								gApp.mDebugger.ThawThread(threadItem.mThreadId);
								mThreadsDirty = true;
							});
					});
			}

			MenuWidget menuWidget = ThemeFactory.mDefault.CreateMenuWidget(menu);
			menuWidget.Init(mListView.GetRoot(), x, y);
		}

        void ValueClicked(MouseEvent evt)
        {
            DarkListViewItem clickedItem = (DarkListViewItem)evt.mSender;
            DarkListViewItem item = (DarkListViewItem)clickedItem.GetSubItem(0);

			if ((clickedItem.mColumnIdx == 2) && (evt.mX > clickedItem.mWidth - GS!(22)))
			{
				return;
			}

            mListView.SetFocus();

			if (evt.mBtn == 1)
			{
				if (!item.Selected)
					mListView.GetRoot().SelectItem(item, true);
				var widget = (Widget)evt.mSender;
				widget.SelfToOtherTranslate(mListView.GetRoot(), evt.mX, evt.mY, var x, var y);
				ShowRightClickMenu(mListView, x, y);
			}
			else if (evt.mBtn == 0)
			{
				mListView.GetRoot().SelectItem(item, true);
			}

            if ((evt.mBtn == 0) && (evt.mBtnCount > 1))
            {
				if (!gApp.mExecutionPaused)
					return;

                for (int32 childIdx = 0; childIdx < mListView.GetRoot().GetChildCount(); childIdx++)
                {
                    var checkListViewItem = mListView.GetRoot().GetChildAtIndex(childIdx);
                    checkListViewItem.IconImage = null;
                }

#unwarn
                int selectedIdx = mListView.GetRoot().GetIndexOfChild(item);

                int32 threadId = int32.Parse(item.mLabel);
                gApp.mDebugger.SetActiveThread(threadId);                
                gApp.mDebugger.mCallStackDirty = true;
                gApp.mCallStackPanel.MarkCallStackDirty();
                gApp.mCallStackPanel.Update();
                gApp.mWatchPanel.MarkWatchesDirty(true);
                gApp.mAutoWatchPanel.MarkWatchesDirty(true);
                gApp.mMemoryPanel.MarkViewDirty();
                gApp.ShowPCLocation(gApp.mDebugger.mActiveCallStackIdx, false, true);
                
                item.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ArrowRight);
            }
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mListView.Resize(0, 0, width, height);
        }

        public void MarkThreadsDirty()
        {
            mThreadsDirty = true;
        }
        
		public override void FocusForKeyboard()
		{
			mListView.SetFocus();
			mWantsKeyboardFocus = true;
		}

		void UpdateThreads()
		{
			if ((!gApp.mExecutionPaused) || (!mThreadsDirty))
				return;

			HashSet<int> selectedIds = scope .();
			int focusedId = -1;

			mListView.GetRoot().WithItems(scope [&] (item) =>
				{
					var threadListViewItem = (ThreadListViewItem)item;
					if (item.Selected)
						selectedIds.Add(threadListViewItem.mThreadId);
					if (item.Focused)
						focusedId = threadListViewItem.mThreadId;
				});

			mListView.GetRoot().Clear();
			String threadInfo = scope String();
			gApp.mDebugger.GetThreadInfo(threadInfo);

			var threadInfos = scope List<StringView>(threadInfo.Split('\n'));
			if (threadInfos.Count < 2)
			    return;

			int32 currentThreadId = int32.Parse(scope String(threadInfos[0]));

			String label = scope String(256);


			for (int32 threadIdx = 1; threadIdx < threadInfos.Count; threadIdx++)
			{                    
			    var elementData = scope List<StringView>(scope String(threadInfos[threadIdx]).Split('\t'));
			    if (elementData.Count == 1)
			        break;                    

			    var listViewItem = (ThreadListViewItem)mListView.GetRoot().CreateChildItem();
			    listViewItem.mOnMouseDown.Add(new => ValueClicked);
			    listViewItem.Label = scope String(elementData[0]);

			    int32 threadId = int32.Parse(scope String(elementData[0]));
			    if (threadId == currentThreadId)
			        listViewItem.IconImage = DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ArrowRight);

				listViewItem.mThreadId = threadId;
				if (threadId == focusedId)
					listViewItem.Focused = true;
				if (selectedIds.Contains(threadId))
					listViewItem.Selected = true;
			    
			    var subItem = (ThreadListViewItem)listViewItem.CreateSubItem(1);
			    subItem.Label = scope String(elementData[1]);
			    subItem.mOnMouseDown.Add(new => ValueClicked);

			    subItem = (ThreadListViewItem)listViewItem.CreateSubItem(2);
				label.Clear();
				label.Append(elementData[2]);
				IDEUtils.ColorizeCodeString(label, .Callstack);
			    subItem.Label = label;
			    subItem.mOnMouseDown.Add(new => ValueClicked);

				if (elementData.Count > 3)
				{
					listViewItem.mFrozen = elementData[3].Contains("Fr");
				}
			}

			mThreadsDirty = false;
		}

        public override void Update()
        {
            base.Update();

			mThreadCallstackJustClosed = -1;
			UpdateThreads();
			bool disabled = !gApp.mExecutionPaused;
            if (!disabled)
			{
				mDisabledTicks = 0;
			}
			else
            {
				mDisabledTicks++;
				if ((mDisabledTicks > 40) && (!gApp.mDebuggerPerformingTask))
                {
					var root = mListView.GetRoot();
					if (root.GetChildCount() != 0)
					{
				        mListView.GetRoot().Clear();
						MarkDirty();
					}
				}
            }

			if (!mListView.mHasFocus)
				mWantsKeyboardFocus = false;

			if (mWantsKeyboardFocus)
			{	
				for (int32 i = 0; i < mListView.GetRoot().GetChildCount(); i++)
				{
					var listViewItem = (DarkListViewItem)mListView.GetRoot().GetChildAtIndex(i);					

					if (listViewItem.mIconImage != null)
					{
						mListView.GetRoot().SelectItem(listViewItem);
						mListView.EnsureItemVisible(listViewItem, true);
						mWantsKeyboardFocus = false;
						break;
					}					
				}
				/*if ((wantSelectIdx != -1) && (wantSelectIdx < mListView.GetRoot().GetChildCount()))
					mListView.GetRoot().SelectItem(mListView.GetRoot().GetChildAtIndex(wantSelectIdx));*/				
			}
        }

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);
        }

		public override bool HasAffinity(Widget otherPanel)
		{
			return base.HasAffinity(otherPanel) || (otherPanel is CallStackPanel);
		}

		public void SelectThreadId(int threadId)
		{
			UpdateThreads();
			for (int threadIdx < mListView.GetRoot().GetChildCount())
			{
				var lvItem = (ThreadListViewItem)mListView.GetRoot().GetChildAtIndex(threadIdx);
				if (lvItem.mThreadId == threadId)
				{
					mListView.GetRoot().SelectItemExclusively(lvItem);
					mListView.EnsureItemVisible(lvItem, true);
					break;
				}
			}
		}
    }
}
