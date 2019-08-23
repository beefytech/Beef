using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using Beefy.events;
using Beefy.gfx;
using Beefy.theme;
using Beefy.theme.dark;
using Beefy.utils;
using Beefy.widgets;
using IDE.Debugger;
using IDE.util;
using System.Diagnostics;

namespace IDE.ui
{
    public class BreakpointListViewItem : DarkListViewItem
    {
        public Breakpoint mBreakpoint ~ _?.Deref();
        public WatchRefreshButton mWatchRefreshButton;

		public void Bind(Breakpoint breakpoint)
		{
			mBreakpoint?.Deref();
			mBreakpoint = breakpoint;
			mBreakpoint?.AddRef();
		}

		public override void Draw(Graphics g)
		{
			base.Draw(g);

			if (mColumnIdx == 0)
			{
				if ((mBreakpoint != null) && (!mBreakpoint.mIsDead))
				{
					using (g.PushTranslate(6, 0))
						mBreakpoint.Draw(g, false);
				}
			}
		}
    }

    public class BreakpointListView : IDEListView
    {
        protected override ListViewItem CreateListViewItem()
        {
            DarkListViewItem anItem = new BreakpointListViewItem();
            return anItem;
        }
    }

    public class BreakpointPanel : Panel
    {
        public BreakpointListView mListView;
        public bool mListDirty = true;       
 		public bool mDeselectOnFocusLost = true;
		public bool mHasPendingSelectionClear = false;
		public String mPendingEvalStr ~ delete _;
		public String mCurEvalExpr ~ delete _;
		public Breakpoint mPendingMemoryBreakpoint ~ { if (_ != null) _.Deref(); };
		public Action<int, int, String> mOnPendingMemoryBreakpoint ~ delete _;

        public this()
        {
            mListView = new BreakpointListView();
			mListView.mShowGridLines = true;
            mListView.InitScrollbars(true, true);
            mListView.mHorzScrollbar.mPageSize = GS!(100);
            mListView.mHorzScrollbar.mContentSize = GS!(500);
            mListView.mVertScrollbar.mPageSize = GS!(100);
            mListView.mVertScrollbar.mContentSize = GS!(500);
            mListView.UpdateScrollbars();
            mListView.mOnMouseDown.Add(new => ListViewMouseDown);
			mListView.mOnMouseClick.Add(new => ListViewClicked);

            AddWidget(mListView);

            ListViewColumn column = mListView.AddColumn(GS!(200), "Name");
            column.mMinWidth = GS!(100);
            column = mListView.AddColumn(GS!(80), "Tags");
            column = mListView.AddColumn(GS!(200), "Condition");
            column = mListView.AddColumn(GS!(200), "Hit Count");
			column = mListView.AddColumn(GS!(200), "Logging");

            IDEApp.sApp.mDebugger.mBreakpointsChangedDelegate.Add(new => BreakpointsChanged);

            //RebuildUI();
    		SetScaleData();
		}

		void SetScaleData()
		{
			mListView.mIconX = GS!(4);
			mListView.mOpenButtonX = GS!(4);
			mListView.mLabelX = GS!(30);
			mListView.mChildIndent = GS!(16);
			mListView.mHiliteOffset = GS!(-2);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			base.RehupScale(oldScale, newScale);
			SetScaleData();
		}

		public override void FocusForKeyboard()
		{
			base.FocusForKeyboard();
			Update();
			SetFocus();
			if ((mListView.GetRoot().FindFocusedItem() == null) && (mListView.GetRoot().GetChildCount() > 0))
			{
				mListView.GetRoot().GetChildAtIndex(0).Focused = true;
			}
		}

        public void BreakpointsChanged()
        {
            mListDirty = true;
        }

		public void MarkStatsDirty()
		{
			// We only need to really refresh the hit count...
			mListDirty = true;
		}

        public override void Serialize(StructuredData data)
        {
            base.Serialize(data);

            data.Add("Type", "BreakpointPanel");
        }

        public override bool Deserialize(StructuredData data)
        {
            return base.Deserialize(data);
        }

		public void ConfigureBreakpoint(Breakpoint breakpoint, String condition, int threadId, String logging, bool breakAfterLogging, int targetHitCount, Breakpoint.HitCountBreakKind hitCountBreakKind)
		{
			/*for (int itemIdx < mListView.GetRoot().GetChildCount())
			{
				var listViewItem = (BreakpointListViewItem)mListView.GetRoot().GetChildAtIndex(itemIdx);

				if (listViewItem.Selected)
				{
					var subItem = listViewItem.GetSubItem(2);
					subItem.Label = condition;
					listViewItem.mBreakpoint.SetCondition(condition);
					listViewItem.mBreakpoint.SetThreadId(threadId);
					listViewItem.mBreakpoint.SetLogging(logging, breakAfterLogging);
					listViewItem.mBreakpoint.SetHitCountTarget(targetHitCount, hitCountBreakKind);
					BreakpointsChanged();
				}
			}
			gApp.MarkDirty();*/

			breakpoint.SetCondition(condition);
			breakpoint.SetThreadId(threadId);
			breakpoint.SetLogging(logging, breakAfterLogging);
			breakpoint.SetHitCountTarget(targetHitCount, hitCountBreakKind);
			BreakpointsChanged();
			gApp.MarkDirty();
		}

		public void ClearHitCounts()
		{
			mListView.GetRoot().WithSelectedItems(scope (item) =>
				{
					var listViewItem = (BreakpointListViewItem)item;
					listViewItem.mBreakpoint.ClearHitCount();
					mListDirty = true;
				});
			gApp.MarkDirty();
		}

		public void SetBreakpointDisabled(bool disabled)
		{
			mListView.GetRoot().WithSelectedItems(scope (item) =>
				{
					var listViewItem = (BreakpointListViewItem)item;
					if (listViewItem.Selected)
					{
						gApp.mDebugger.SetBreakpointDisabled(listViewItem.mBreakpoint, disabled);
						if (!disabled)
						{

						}	
						BreakpointsChanged();
						gApp.MarkDirty();
					}
				});
			gApp.MarkDirty();
		}

		public void DeleteBreakpoint()
		{
			mListView.GetRoot().WithSelectedItems(scope (item) =>
				{
					var listViewItem = (BreakpointListViewItem)item;
					if (listViewItem.Selected)
					{
						gApp.mDebugger.DeleteBreakpoint(listViewItem.mBreakpoint);
						BreakpointsChanged();
					}
				});
			gApp.MarkDirty();
		}

		public void BindToThread()
		{
			int activeThreadId = gApp.mDebugger.GetActiveThread();
			if (activeThreadId <= 0)
				return;

			mListView.GetRoot().WithSelectedItems(scope (item) =>
				{
					var listViewItem = (BreakpointListViewItem)item;
					listViewItem.mBreakpoint.SetThreadId(activeThreadId);
					mListDirty = true;
				});
			gApp.MarkDirty();
		}

		public void ConfigureBreakpoints(Widget relWidget)
		{
			mListView.GetRoot().WithSelectedItems(scope (item) =>
				{
					var listViewItem = (BreakpointListViewItem)item;
					if (listViewItem.Selected)
					{
						mDeselectOnFocusLost = false;
						ConditionDialog dialog = new ConditionDialog();
						dialog.Init(listViewItem.mBreakpoint);
						dialog.PopupWindow(relWidget.mWidgetWindow);
						mDeselectOnFocusLost = true;
					}
				});
		}

		public void AddMemoryBreakpoint(WidgetWindow window)
		{
			NewBreakpointDialog dialog = new .(.Memory);
			dialog.Init();
			dialog.PopupWindow(window);
		}

		public void AddSymbolBreakpoint(WidgetWindow window)
		{
			NewBreakpointDialog dialog = new .(.Symbol);
			dialog.Init();
			dialog.PopupWindow(window);
		}

        public MenuWidget ShowRightClickMenu(Widget relWidget, float x, float y, bool isSpecific)
        {
            //mSelectedParentItem = (DarkListViewItem)GetSelectedParentItem();

			mDeselectOnFocusLost = false;
			defer:: { mDeselectOnFocusLost = true; }

            Menu menu = new Menu();

			Menu menuItem;

			Breakpoint firstSelectedBreakpoint = null;
			bool selectedEnabledBreakpoint = false;
			bool selectedDisabledBreakpoint = false;

			mListView.GetRoot().WithSelectedItems(scope [&] (listViewItem) =>
            	{
					var breakpointListViewItem = (BreakpointListViewItem)listViewItem;
					var breakpoint = breakpointListViewItem.mBreakpoint;
					if (firstSelectedBreakpoint == null)
						firstSelectedBreakpoint = breakpoint;
					selectedEnabledBreakpoint |= !breakpoint.mDisabled;
					selectedDisabledBreakpoint |= breakpoint.mDisabled;
                });

			if (firstSelectedBreakpoint != null)
			{
				menuItem = menu.AddItem("Configure Breakpoint");
				menuItem.mOnMenuItemSelected.Add(new (evt) => 
					{
						ConfigureBreakpoints(relWidget);
					});
				menuItem = menu.AddItem("Delete Breakpoint");
				menuItem.mOnMenuItemSelected.Add(new (evt) => 
					{
						DeleteBreakpoint();
					});
				if (selectedEnabledBreakpoint)
				{
					menuItem = menu.AddItem("Disable Breakpoint");
					menuItem.mOnMenuItemSelected.Add(new (evt) => { SetBreakpointDisabled(true); });
				}
				if (selectedDisabledBreakpoint)
				{
					menuItem = menu.AddItem("Enable Breakpoint");
					menuItem.mOnMenuItemSelected.Add(new (evt) => { SetBreakpointDisabled(false); });
				}
				if (gApp.mDebugger.IsPaused())
				{
					menuItem = menu.AddItem("Bind to Current Thread");
					menuItem.mOnMenuItemSelected.Add(new (evt) => { BindToThread(); });
					gApp.MarkDirty();
				}
			}

			if (!isSpecific)
			{
				if (gApp.mDebugger.IsPaused())
				{
					if (firstSelectedBreakpoint != null)
						menu.AddItem();

		            menuItem = menu.AddItem("Add Memory Breakpoint");
		            menuItem.mOnMenuItemSelected.Add(new (evt) =>
		                {
		                    AddMemoryBreakpoint(relWidget.mWidgetWindow);
		                });
				}

				menuItem = menu.AddItem("Add Symbol Breakpoint");
				menuItem.mOnMenuItemSelected.Add(new (evt) =>
				    {
				        AddSymbolBreakpoint(relWidget.mWidgetWindow);
				    });
			}

			if (menu.mItems.IsEmpty)
			{
				delete menu;
				return null;
			}	
            MenuWidget menuWidget = ThemeFactory.mDefault.CreateMenuWidget(menu);

            menuWidget.Init(relWidget, x, y);

			return menuWidget;
            //menuWidget.mWidgetWindow.mWindowClosedHandler += DeselectFolder;
        }

		protected override void ShowRightClickMenu(Widget relWidget, float x, float y)
		{
			ShowRightClickMenu(mListView.GetRoot(), x, y, false);
		}

        public void ListViewItemMouseDown(MouseEvent theEvent)
        {
            var clickedItem = (BreakpointListViewItem)theEvent.mSender;
            var item = (BreakpointListViewItem)clickedItem.GetSubItem(0);

			if (theEvent.mBtn == 1)
			{
				if (!item.Selected)
					mListView.GetRoot().SelectItem(item, true);
			}
			else
				mListView.GetRoot().SelectItem(item, true);

			SetFocus();			 

			if ((theEvent.mBtn == 0) && (theEvent.mBtnCount == 2))
			{
				var breakpoint = item.mBreakpoint;
				if (breakpoint.mFileName != null)
				{
					gApp.RecordHistoryLocation();
					gApp.ShowSourceFileLocation(breakpoint.mFileName, -1, -1, breakpoint.mLineNum, breakpoint.mColumn, LocatorType.Always, true);
				}
			}
            

            /*if ((theEvent.mBtn == 0) && (theEvent.mBtnCount > 1))
            {
                for (int childIdx = 1; childIdx < mListView.GetRoot().GetChildCount(); childIdx++)
                {
                    var checkListViewItem = mListView.GetRoot().GetChildAtIndex(childIdx);
                    checkListViewItem.IconImage = null;
                }

                int selectedIdx = item.mVirtualIdx;
                IDEApp.sApp.mDebugger.mSelectedCallStackIdx = selectedIdx;
                IDEApp.sApp.ShowPCLocation(selectedIdx, false, true);
                IDEApp.sApp.StackPositionChanged();
            }*/
        }

        void ListViewItemClicked(MouseEvent theEvent)
        {
            var anItem = (BreakpointListViewItem)theEvent.mSender;
            if (anItem.mColumnIdx != 0)
                anItem = (BreakpointListViewItem)anItem.GetSubItem(0);

            if (theEvent.mBtn == 1)
            {
                float aX, aY;
                theEvent.GetRootCoords(out aX, out aY);                
                ShowRightClickMenu(mWidgetWindow.mRootWidget, aX, aY, false);
            }
            else
            {
                //if (anItem.IsParent)
                //anItem.Selected = false;
            }
        }

        void ListViewMouseDown(MouseEvent theEvent)
        {
			mListView.GetRoot().WithSelectedItems(scope (item) => { item.Selected = false; });
        }

		void ListViewClicked(MouseEvent theEvent)
		{
			if (theEvent.mBtn == 1)
			{
			    float aX, aY;
			    theEvent.GetRootCoords(out aX, out aY);
			    ShowRightClickMenu(mWidgetWindow.mRootWidget, aX, aY, false);
			}
		}

		public enum MemoryBreakpointResult
		{
			Failure,
			Success,
			Pending
		}

		void PendingExprDone(String result)
		{
			int addr = 0;
			int32 byteCount = 0;
			String addrType = scope .();

			if (result != null)
			{
				if (HandleMemoryBreakpointResult(mPendingEvalStr, result, out addr, out byteCount, addrType))
				{
				}
			}

			mOnPendingMemoryBreakpoint(addr, byteCount, addrType);
			DeleteAndNullify!(mOnPendingMemoryBreakpoint);
			DeleteAndNullify!(mPendingEvalStr);
		}

		void ExistingPendingExprDone(int addr, int byteCount, String addrType)
		{
			if (!mPendingMemoryBreakpoint.mIsDead)
			{
				if (addr != 0)
					SetMemoryBreakpoint(mPendingMemoryBreakpoint, addr, byteCount, addrType);
			}
			mPendingMemoryBreakpoint.Deref();
			mPendingMemoryBreakpoint = null;
		}

		public void CancelPending()
		{
			DeleteAndNullify!(mPendingEvalStr);
			DeleteAndNullify!(mOnPendingMemoryBreakpoint);
			if (mPendingMemoryBreakpoint != null)
			{
				mPendingMemoryBreakpoint.Deref();
				mPendingMemoryBreakpoint = null;
			}
			DeleteAndNullify!(gApp.mPendingDebugExprHandler);
		}

		public bool HandleMemoryBreakpointResult(String evalStr, String val, out int addr, out int32 byteCount, String addrType)
		{
			if (val.StartsWith("!", StringComparison.Ordinal))
			{
				String errorString = scope String();
				DebugManager.GetFailString(val, evalStr, errorString);
				IDEApp.sApp.Fail(errorString);
			    return false;
			}

			var vals = scope List<StringView>(val.Split('\n'));
			addr = (int)int64.Parse(scope String(vals[0]), System.Globalization.NumberStyles.HexNumber);
			byteCount = int32.Parse(scope String(vals[1]));
			addrType.Append(vals[2]);
			return true;
		}

        public MemoryBreakpointResult TryCreateMemoryBreakpoint(String evalStr, out int addr, out int32 byteCount, String addrType, Action<int, int, String> pendingHandler)
        {
            addr = 0;
            byteCount = 0;
            String val = scope String();
            //gApp.mDebugger.Evaluate(evalStr, val, -1);
			let result = gApp.DebugEvaluate(null, evalStr, val, -1, .NotSet, .MemoryWatch | .AllowSideEffects | .AllowCalls, new => PendingExprDone);
			if (result == .Pending)
			{
				Debug.Assert(mOnPendingMemoryBreakpoint == null);
				mOnPendingMemoryBreakpoint = pendingHandler;
				mPendingEvalStr = new String(evalStr);
				return .Pending;
			}

			delete pendingHandler;

            return HandleMemoryBreakpointResult(evalStr, val, out addr, out byteCount, addrType) ? .Success : .Failure;
        }

		void DoCreateMemoryBreakpoint(int addr, int byteCount, String addrType)
		{
			if (addr != 0)
			{
				gApp.RefreshWatches();
				var breakpoint = gApp.mDebugger.CreateMemoryBreakpoint(mCurEvalExpr, addr, byteCount, addrType);
				if (!breakpoint.IsBound())
				{
					ShowMemoryBreakpointError();
					gApp.mDebugger.DeleteBreakpoint(breakpoint);
				}
				else
					breakpoint.mDeleteOnUnbind = true;
			}

			DeleteAndNullify!(mCurEvalExpr);
		}

		public void CreateMemoryBreakpoint(String evalStr)
		{
			if (mCurEvalExpr != null)
			{
				gApp.Fail("Already creating memory breakpoint");
				return;
			}
			
			int addr = 0;
			int32 byteCount = 0;
			String addrType = scope String();
			switch (gApp.mBreakpointPanel.TryCreateMemoryBreakpoint(evalStr, out addr, out byteCount, addrType, new => DoCreateMemoryBreakpoint))
			{
			case .Pending:
				mCurEvalExpr = new String(evalStr);
				return;
			case .Failure:
				//gApp.Fail("Failed to evaluate to memory address");
				return;
			case .Success:
				mCurEvalExpr = new String(evalStr);
			}
			DoCreateMemoryBreakpoint(addr, byteCount, addrType);
			MarkDirty();
			gApp.RefreshWatches();
		}

        public void ShowMemoryBreakpointError()
        {
            IDEApp.sApp.Fail("The breakpoint cannot be set. The maximum number of data breakpoints have already been set.");
        }

		void SetMemoryBreakpoint(Breakpoint breakpoint, int addr, int byteCount, String addrType)
		{
			gApp.RefreshWatches();
			breakpoint.MoveMemoryBreakpoint((int)addr, byteCount, addrType);
			if (!breakpoint.IsBound())
			    ShowMemoryBreakpointError();
			BreakpointsChanged();
		}

        public void RefreshBreakpoint(BreakpointListViewItem listViewItem)
        {
            //
			if (!IDEApp.sApp.mDebugger.mIsRunning)
				return; // Fail

			var breakpoint = listViewItem.mBreakpoint;

            int addr;
            int32 byteCount;
			String addrType = scope String();
            switch (TryCreateMemoryBreakpoint(listViewItem.mBreakpoint.mMemoryWatchExpression, out addr, out byteCount, addrType, new => ExistingPendingExprDone))
			{
			case .Failure:
				return;
			case .Success:
			case .Pending:
				Debug.Assert(mPendingMemoryBreakpoint == null);
				mPendingMemoryBreakpoint = breakpoint;
				mPendingMemoryBreakpoint.AddRef();
				return;
			}
            //if (newBreakpoint == null)
            //return;

            SetMemoryBreakpoint(breakpoint, addr, byteCount, addrType);
        }

		public void SelectBreakpoints(HashSet<Breakpoint> breakpoints)
		{
			mListView.GetRoot().WithItems(scope (listViewItem) =>
				{
					var breakListViewItem = (BreakpointListViewItem)listViewItem;
					breakListViewItem.Selected = breakpoints.Contains(breakListViewItem.mBreakpoint);
					if (breakListViewItem.Selected)
					{
						if (mWidgetWindow != null)
							mListView.EnsureItemVisible(breakListViewItem, false);
					}
				});
			mHasPendingSelectionClear = true;
		}

        public void Populate()
        {            
            var root = mListView.GetRoot();
            var debugger = IDEApp.sApp.mDebugger;
            while (root.GetChildCount() > debugger.mBreakpointList.Count)
            {
                root.RemoveChildItemAt(debugger.mBreakpointList.Count);
            }

            for (int32 breakIdx = 0; breakIdx < debugger.mBreakpointList.Count; breakIdx++)
            {
                var breakpoint = debugger.mBreakpointList[breakIdx];
                if (breakIdx >= root.GetChildCount())
                {
                    var newItem = root.CreateChildItem();
                    newItem.mOnMouseDown.Add(new => ListViewItemMouseDown);
                    newItem.mOnMouseClick.Add(new => ListViewItemClicked);

                    var subItem = newItem.CreateSubItem(1);
					subItem.mOnMouseDown.Add(new => ListViewItemMouseDown);
					subItem.mOnMouseClick.Add(new => ListViewItemClicked);

					subItem = newItem.CreateSubItem(2);
					subItem.mOnMouseDown.Add(new => ListViewItemMouseDown);
					subItem.mOnMouseClick.Add(new => ListViewItemClicked);
					
					subItem = newItem.CreateSubItem(3);
					subItem.mOnMouseDown.Add(new => ListViewItemMouseDown);
					subItem.mOnMouseClick.Add(new => ListViewItemClicked);

					subItem = newItem.CreateSubItem(4);
					subItem.mOnMouseDown.Add(new => ListViewItemMouseDown);
					subItem.mOnMouseClick.Add(new => ListViewItemClicked);
                }
                var listViewItem = (BreakpointListViewItem)root.GetChildAtIndex(breakIdx);
                listViewItem.mTextColor = Color.White;

				listViewItem.mIsBold = breakpoint.IsActiveBreakpoint();

				var locString = scope String();
				breakpoint.ToString_Location(locString);
				listViewItem.Label = locString;
				if (breakpoint.IsBound())
					listViewItem.mTextColor = 0xFFFFFFFF;
				else
					listViewItem.mTextColor = 0x80FFFFFF;

				// Condition
				var subItem = listViewItem.GetSubItem(2);
				if (breakpoint == gApp.mDebugger.mRunToCursorBreakpoint)
					subItem.Label = "Run to cursor";
				else if (breakpoint.mCondition != null)
					subItem.Label = breakpoint.mCondition;
				else
					subItem.Label = "";

				// Hit count
				subItem = listViewItem.GetSubItem(3);
				let hitCountLabel = scope String();
				breakpoint.ToString_HitCount(hitCountLabel);
				subItem.Label = hitCountLabel;

				// Logging
				subItem = listViewItem.GetSubItem(4);
				if (breakpoint.mLogging != null)
					subItem.Label = breakpoint.mLogging;
				else
					subItem.Label = "";

                listViewItem.Bind(breakpoint);

                if (breakpoint.mIsMemoryBreakpoint && (!breakpoint.IsBound()))
                {
					if (listViewItem.mWatchRefreshButton == null)
					{
	                    var watchRefreshButton = new WatchRefreshButton();
	                    watchRefreshButton.Resize(GS!(-16), 0, GS!(20), GS!(20));
	                    watchRefreshButton.mOnMouseDown.Add(new (evt) => RefreshBreakpoint(listViewItem));
	                    var typeSubItem = listViewItem.GetSubItem(1);
	                    typeSubItem.AddWidget(watchRefreshButton);
	                    listViewItem.mWatchRefreshButton = watchRefreshButton;
	                    listViewItem.mTextAreaLengthOffset = -16;
					}
                }
                else if (listViewItem.mWatchRefreshButton != null)
                {
                    listViewItem.mTextAreaLengthOffset = 0;
					defer delete listViewItem.mWatchRefreshButton;
                    listViewItem.mWatchRefreshButton.RemoveSelf();
					listViewItem.mWatchRefreshButton = null;
                }

                /*else if (breakpoint.GetAddress() != 0)
                    listViewItem.Label = String.Format("{0}", Path.GetFileName(breakpoint.mFileName), breakpoint.mLineNum);*/
            }
        }

        public override void Update()
        {
            base.Update();
            if (mListDirty)
            {
                Populate();
                mListDirty = false;
            }

			if ((mHasPendingSelectionClear) && (!mHasFocus) && (!gApp.HasPopupMenus()) && (!gApp.HasModalDialogs()))
			{
				// Don't clear focus if there's a dialog or something else with focus
				mListView.GetRoot().SelectItemExclusively(null);
				mHasPendingSelectionClear = false;
			}

			if (mWidgetWindow == null)
			{
				mListView.mListSizeDirty = true;
			}	
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mListView.Resize(0, 0, width, height);
			mListView.ScrollPositionChanged();
        }

        public override void KeyDown(KeyCode keyCode, bool isRepeat)
        {
            mListView.KeyDown(keyCode, isRepeat);

            base.KeyDown(keyCode, isRepeat);

			switch (keyCode)
            {
			case .Apps:
				ShowRightClickMenu(mListView);
			case KeyCode.Delete:
                mListView.GetRoot().WithSelectedItems(scope (listViewItem) =>
                    {
                        var breakpointListViewItem = (BreakpointListViewItem)listViewItem;
						BfLog.LogDbg("Manually deleting breakpoint\n");
                        IDEApp.sApp.mDebugger.DeleteBreakpoint(breakpointListViewItem.mBreakpoint);
                    });
			default:
            }
        }

        public override void LostFocus()
        {
            base.LostFocus();
			if (mDeselectOnFocusLost)
            	mListView.GetRoot().SelectItemExclusively(null);
        }

		public override void DrawAll(Graphics g)
		{
			base.DrawAll(g);
		}
    }
}
