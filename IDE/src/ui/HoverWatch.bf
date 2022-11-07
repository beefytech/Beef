using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using Beefy;
using Beefy.events;
using Beefy.widgets;
using Beefy.theme.dark;
using Beefy.gfx;
using Beefy.geom;
using Beefy.theme;
using IDE.Debugger;

namespace IDE.ui
{
    public class HoverWatch : Widget, IWatchOwner
    {
        public class PendingWatch
        {
            public String mName ~ delete _;
            public String mEvalStr ~ delete _;
            //public WatchSeriesInfo mWatchSeriesInfo ~ delete _;
			public WatchSeriesInfo mWatchSeriesInfo ~ { if (_ != null) _.ReleaseRef(); };

			public ~this()
			{

			}
        }

        public class HoverListViewItem : WatchListViewItem
        {
			public const uint cActionColor = 0xFFB0B0FF;
			public const uint cActionOverColor = 0xFFE0E0FF;

            public List<PendingWatch> mPendingWatches = new List<PendingWatch>() ~ DeleteContainerAndItems!(_);
            public HoverListView mChildrenListView;

			static int32 sHLVItemId = -1;
			int32 mHLVItemId = ++sHLVItemId;

            public this(IWatchOwner watchOwner, HoverListView listView) : base(watchOwner, listView)
            {
            }

			public ~this()
			{
			}

			public override WatchListViewItem GetWatchListViewItemParent()
			{
				let hoverWatchListView = (HoverListView)mListView;
				return hoverWatchListView.mParentHoverListViewItem;
			}

            public override bool WantsTooltip(float mouseX, float mouseY)
            {
                var hoverWatch = (HoverWatch)mWidgetWindow.mRootWidget;
                if (hoverWatch.mEditWidget != null)
                    return false;

                if (mColumnIdx == 0)
                {
                    var hoverListView = (HoverListView)mListView;
                    if ((mWatchEntry.mResultType.HasFlag(WatchResultType.TypeClass)) || (mWatchEntry.mResultType.HasFlag(WatchResultType.TypeValueType)))
                        return false;
                    return mouseX < LabelX + hoverListView.mFont.GetWidth(mLabel) + GS!(2);
                }
                return base.WantsTooltip(mouseX, mouseY);
            }

            public override void ShowTooltip(float mouseX, float mouseY)
            {
                if (mColumnIdx == 0)
                {
                    if (mWatchEntry.mResultTypeStr != null)
                        DarkTooltipManager.ShowTooltip(mWatchEntry.mResultTypeStr, this, LabelX + GS!(8), mSelfHeight + mBottomPadding);
                    return;
                }

                base.ShowTooltip(mouseX, mouseY);
            }

			public override void DrawAll(Graphics g)
			{
				base.DrawAll(g);
			}

			public override void Update()
			{
				if (mTextAction != null)
				{
					if (mMouseOver)
						mTextColor = cActionOverColor;
					else
						mTextColor = cActionColor;
				}
				base.Update();
			}

			public override void MouseEnter()
			{
				base.MouseEnter();
				if (mTextAction != null)
					gApp.SetCursor(.Hand);
			}

			public override void MouseLeave()
			{
				base.MouseLeave();
				if (mTextAction != null)
					gApp.SetCursor(.Pointer);
			}

			public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
			{
				var hoverWatch = (HoverWatch)mWidgetWindow.mRootWidget;
				var prevActionCount = hoverWatch.mActionIdx;

				base.MouseDown(x, y, btn, btnCount);

				if (prevActionCount == hoverWatch.mActionIdx)
				{
					if ((mTextAction != null) && (btn == 0))
						gApp.PerformAction(mTextAction);
				}
			}
        }

        public class HoverListView : WatchListView
        {
            public HoverWatch mHoverWatch;
            public bool mSizeDirty;
			public bool mWantsHorzResize;
			public bool mIsReversed;
			public Point? mRequestPos;
			public HoverListViewItem mParentHoverListViewItem;

            public this(HoverWatch hoverWatch) : base(hoverWatch)
            {
                mEndInEllipsis = true;
				mInsets = new Insets();
				SetScaleData();
            }

			public ~this()
			{
				Debug.Assert(mParent == null);
				//Debug.WriteLine("ListView Deleted: {0}", this);
			}

			protected override void SetScaleData()
			{
				base.SetScaleData();
				if (mInsets != null)
					mInsets.mRight = GS!(8);
			}

            protected override ListViewItem CreateListViewItem()
            {
                if (mWatchOwner == null) // For root node, mWatchOwner not set yet
                    return new DarkListViewItem();

                HoverListViewItem anItem = new HoverListViewItem(mWatchOwner, this);
                return anItem;
            }

            public override void Draw(Graphics g)
            {

                base.Draw(g);
            }

			public override void DrawAll(Graphics g)
			{
				using (g.PushColor(0x80000000))
				    g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.DropShadow), 0, 0, mWidth + GS!(8), mHeight + GS!(8));

				using (g.PushColor(0xFFFFFFFF))
				    g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Menu), 0, 0, mWidth, mHeight);

				using (g.PushClip(0, 0, mWidth - GS!(3), mHeight))
					base.DrawAll(g);
			}

            public override void Update()
            {
                base.Update();

                if (mSizeDirty)
                {
                    mSizeDirty = false;
                    mHoverWatch.FinishListView(this, 0, 0, mWantsHorzResize);
					mWantsHorzResize = false;
                }
            }

			public override void Resize(float x, float y, float width, float height)
			{
				base.Resize(x, y, width, height);
			}
        }

        public class HoverEditWidget : ExpressionEditWidget
        {
            HoverWatch mHoverWatch;

            public this(HoverWatch hoverWatch)
            {
                mHoverWatch = hoverWatch;
            }

            public override void UpdateText(char32 keyChar, bool doAutoComplete)
            {
                base.UpdateText(keyChar, doAutoComplete);
                var listView = (HoverListView)mHoverWatch.mEditingItem.mListView;
                mHoverWatch.FinishListView(listView, listView.mX, listView.mY);
                float adjust = (listView.mVertScrollbar != null) ? 0 : 0;
                Resize(mX, mY, listView.mColumns[1].mWidth - adjust, mHeight);
                HorzScrollTo(0);
            }
        }

		class ContentWidget : Widget
		{
			public override void Draw(Graphics g)
			{
				base.Draw(g);

				/*using (g.PushColor(0x40FF0000))
					g.FillRect(-10000, -10000, 20000, 20000);*/
			}
		}

		public String mDisplayString = new String() ~ delete _;
		public String mOrigEvalString = new String() ~ delete _;
        public String mEvalString = new String() ~ delete _;
        public TextPanel mTextPanel;
        public HoverListView mListView;
        public Point mOpenMousePos;
        public bool mCancelingEdit;
        public HoverEditWidget mEditWidget;
        public HoverListViewItem mEditingItem;
        public int32 mEditLostFocusTick;
        public int32 mCloseDelay;
		public int32 mCloseCountdown;
        public List<HoverListView> mListViews = new List<HoverListView>() ~ delete _;
        public Action mCloseHandler;
        public String mLastError ~ delete _;
        public bool mAllowLiterals;
        public bool mAllowSideEffects;
		public bool mDeselectCallStackIdx;
		public Widget mContentWidget;
		public Event<Action> mRehupEvent ~ _.Dispose();
		public DebugManager.Language mLanguage = .NotSet;
        bool mIsTiming;
        float mOrigX;
        float mOrigY;
		float mOrigScreenX;
		float mOrigScreenY;
		float mTaskbarXOffset;
		float mTaskbarYOffset;
        public bool mIsShown;
		bool mCreatedWindow;
		bool mClosed;
		bool mOwnsListView = true;
		int32 mActionIdx;

		static HoverWatch sActiveHoverWatch;
        static int32 sActiveInstanceCount = 0;

        public this()
        {
            //IDEApp.sApp.mBfResolveSystem.StartTiming();
            //IDEApp.sApp.mBfResolveSystem.mIsTiming = false;
            //mIsTiming = true;

			mContentWidget = new ContentWidget();
			AddWidget(mContentWidget);

			//Debug.WriteLine("HoverWatch.this {0}", this);
        }

		public ~this()
		{
			//Debug.WriteLine("HoverWatch.~this {0}", this);
			Clear();
		}

		public bool HasDisplay => mListView != null;

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            // To debug window area
            /*using (g.PushColor(0x20000000))
                g.FillRect(0, 0, mWidth, mHeight);*/
        }

        public override void DrawAll(Graphics g)
        {
            base.DrawAll(g);

            if (mIsTiming)
            {
                IDEApp.sApp.mBfResolveSystem.StopTiming();
                IDEApp.sApp.mBfResolveSystem.DbgPrintTimings();
                mIsTiming = false;
            }
        }

        WatchListView IWatchOwner.GetWatchListView()
        {
            return mListViews[0];
        }

        void IWatchOwner.UpdateWatch(WatchListViewItem watchEntry)
        {
        }

        public void Clear()
        {
            mIsShown = false;

			ListView saveListView = null;
            if (mListView != null)
            {
				if (!mOwnsListView)
					saveListView = mListView;

                CloseChildren(mListView);
                //RemoveWidget(mListView);

				if ((mOwnsListView) && (mListViews.Count == 0))
				{
					delete mListView;
				}
				if (mOwnsListView)
					mListView = null;
            }

            if (mTextPanel == null)
                return;

			sActiveHoverWatch = null;
            sActiveInstanceCount--;
            Debug.Assert(sActiveInstanceCount == 0);

			mTextPanel.mOnRemovedFromParent.Remove(scope => PanelRemovedFromParent, true);

            mTextPanel.mHoverWatch = null;
            mTextPanel = null;

            if (mWidgetWindow != null)
            {
                WidgetWindow.sOnMouseDown.Remove(scope => HandleMouseDown, true);
                WidgetWindow.sOnMouseWheel.Remove(scope => HandleMouseWheel, true);
                WidgetWindow.sOnMenuItemSelected.Remove(scope => HandleSysMenuItemSelected, true);
                WidgetWindow.sOnKeyDown.Remove(scope => HandleKeyDown, true);

                mWidgetWindow.Close();
                mWidgetWindow = null;
            }


			Widget.RemoveAndDelete(mEditWidget);
            mEditWidget = null;

            for (var listView in mListViews)
			{
				if (listView == saveListView)
				{
					listView.RemoveSelf();
				}
				else
				{
					listView.RemoveSelf();
					gApp.DeferDelete(listView);
				}
			}
            mListViews.Clear();
        }

        public void Close()
        {
			//Debug.WriteLine("HoverWatch.Close {0}", this);

			if (mEditWidget != null)
				HandleEditLostFocus(mEditWidget);

			mClosed = true;

			if (mTextPanel != null)
				mTextPanel.mHoverWatch = null;

            if (mCloseHandler != null)
                mCloseHandler();

            Clear();

			if (!mCreatedWindow)
				delete this;
        }

        void HandleMouseWheel(MouseEvent evt)
        {
			if (mListViews.Count > 1)
			{
	            return;
			}

			if (evt.mSender != mWidgetWindow)
			{
				var widgetWindow = evt.mSender as BFWindow;
				while (widgetWindow != null)
				{
					if (widgetWindow == mWidgetWindow)
						return;
					widgetWindow = widgetWindow.mParent;
				}
			}

			EditWidget editWidget = mEditWidget;
			if (var sourceViewPanel = mTextPanel as SourceViewPanel)
				editWidget = sourceViewPanel.mEditWidget;

            Close();
			editWidget?.MouseWheel(0, 0, evt.mWheelDeltaX, evt.mWheelDeltaY);
        }

        void HandleMouseDown(MouseEvent evt)
        {
            WidgetWindow widgetWindow = (WidgetWindow)evt.mSender;
			bool canClose = true;

			var checkWindow = widgetWindow;
			while (checkWindow != null)
			{
				if ((checkWindow.mRootWidget is HoverWatch) || (widgetWindow.mRootWidget is DarkMenuContainer) || (widgetWindow.mRootWidget is DarkTooltipContainer))
					canClose = false;
				checkWindow = checkWindow.mParent as WidgetWindow;
			}

			if (canClose)
                Close();
        }

        void HandleSysMenuItemSelected(IMenu sysMenu)
        {
            Close();
        }

        public override void Update()
        {
            base.Update();

			for (let item in mListView.GetRoot().mChildItems)
			{
				let listViewItem = (HoverListViewItem)item;
				if (listViewItem.mWatchEntry?.mIsPending == true)
				{
					if ((gApp.mDebugger.IsPaused(true)) && (!gApp.mDebugger.HasMessages()))
					{
						let listView = (HoverListView)listViewItem.mListView;
						DoListViewItem(listView, listViewItem, listViewItem.mWatchEntry.mName, listViewItem.mWatchEntry.mEvalStr, true);
						FinishListView(listView, listView.mX, listView.mY, true);
						gApp.RefreshVisibleViews();
					}
				}
			}

            if (mCloseDelay > 1)
               mCloseDelay--;
			if (mCloseCountdown > 0)
			{
				mCloseCountdown--;
				if (mCloseCountdown == 0)
				{
					Close();
					return;
				}
			}

            if (mTextPanel != null)
            {
                if (mTextPanel.mWidgetWindow == null)
                    return;

                for (WidgetWindow childWindow in mTextPanel.mWidgetWindow.mChildWindows)
                {
                    // Focus switched away from a dialog?
                    if ((childWindow.mRootWidget is Dialog) && (!childWindow.mHasFocus))
                    {
                        //TODO: This causes the hoverwatch to close immediately if we have a backgrounded project properties dialog up
                        //  I don't remember what this test was originally supposed to fix.  Remove?
                        //Close();
                        //return;
                    }
                }
            }

            if (mChildWidgets.Count < 2)
                return;

            float y = mWidgetWindow.mMouseY;

            var lastListView = mChildWidgets[mChildWidgets.Count - 1] as HoverListView;
            if (lastListView != null)
            {
                if ((y < lastListView.mY - GS!(20)) && (mWidgetWindow.mCaptureWidget == null))
                {
                    var beforeLastListView = mChildWidgets[mChildWidgets.Count - 2] as HoverListView;
                    if (beforeLastListView != null)
                    {
                        if (mEditWidget != null)
                            mEditWidget.LostFocus();
                        CloseChildren(beforeLastListView);
                    }
                }
            }
        }

        public override void MouseMove(float x, float y)
        {
            base.MouseMove(x, y);


        }

        void CloseChildren(HoverListView hoverListView)
        {
            for (HoverListViewItem childItem in hoverListView.GetRoot().mChildItems)
            {
				var childrenListView = childItem.mChildrenListView;
                if (childrenListView != null)
                {
                    CloseChildren(childrenListView);
                    mListViews.Remove(childrenListView);
                    childrenListView.RemoveSelf();
                    childItem.mChildrenListView = null;

					if (childItem.mOpenButton != null)
                    	childItem.mOpenButton.mIsOpen = false;
					//delete childrenListView;
					gApp.DeferDelete(childrenListView);
                }
            }
        }

        void OpenButtonClicked(HoverListViewItem listViewItem)
        {
            //var listView = (HoverListView)listViewItem.mListView;
            var openButton = listViewItem.mOpenButton;
            bool isOpening = !openButton.mIsOpen;
            CloseChildren((HoverListView)listViewItem.mListView);

            if (isOpening)
            {
                //IDEApp.sApp.mBfResolveSystem.StartTiming();
                //IDEApp.sApp.mBfResolveSystem.mIsTiming = false;
                //mIsTiming = true;

                Debug.Assert(listViewItem.mChildrenListView == null);

                float x;
                float y;
                openButton.SelfToOtherTranslate(mContentWidget, 0, 0, out x, out y);

                var childrenListView = CreateListView();
				childrenListView.mParentHoverListViewItem = listViewItem;
                mContentWidget.AddWidget(childrenListView);
                mListViews.Add(childrenListView);
                listViewItem.mChildrenListView = childrenListView;
                for (var pendingEntry in listViewItem.mPendingWatches)
                {
                    var watchListViewItem = DoListViewItem(listViewItem.mChildrenListView, null, pendingEntry.mName, pendingEntry.mEvalStr, false, listViewItem.mWatchEntry);
					if (watchListViewItem == null)
						continue;

                    if (pendingEntry.mWatchSeriesInfo != null)
                    {
						pendingEntry.mWatchSeriesInfo.AddRef();
                        watchListViewItem.mWatchSeriesInfo = pendingEntry.mWatchSeriesInfo;
						watchListViewItem.mWatchSeriesInfo.mMoreButton = null;
						watchListViewItem.mWatchSeriesInfo.mLessButton = null;

						if (watchListViewItem.mWatchSeriesInfo.mCount != 0)
						{
							double selfHeight = (double)watchListViewItem.mSelfHeight * watchListViewItem.mWatchSeriesInfo.mCount;
							if (watchListViewItem.mWatchSeriesInfo.mCount  == -1)
								selfHeight = watchListViewItem.mSelfHeight;
							else if (watchListViewItem.mWatchSeriesInfo.mShowPages > 0)
								selfHeight = Math.Min(selfHeight, (double)watchListViewItem.mSelfHeight * watchListViewItem.mWatchSeriesInfo.mShowPages * watchListViewItem.mWatchSeriesInfo.mShowPageSize);
							watchListViewItem.mSelfHeight = (float)selfHeight;
						}
                    }
                }
                FinishListView(listViewItem.mChildrenListView, x + GS!(18), y + GS!(18));
				openButton.mIsReversed = childrenListView.mIsReversed;
            }
            ResizeWindow();

            openButton.mIsOpen = isOpening;
        }

        void OpenButtonClicked(MouseEvent mouseEvent)
        {
            var openButton = (DarkTreeOpenButton)mouseEvent.mSender;
            var listViewItem = (HoverListViewItem)openButton.mItem;
            OpenButtonClicked(listViewItem);
            mouseEvent.mHandled = true;
        }

        void RefreshListView(HoverListView listView)
        {
            for (HoverListViewItem listViewItem in listView.GetRoot().mChildItems)
            {
                DoListViewItem(listView, listViewItem, listViewItem.mWatchEntry.mName, listViewItem.mWatchEntry.mEvalStr, false);
				if (listViewItem.mChildrenListView != null)
					RefreshListView(listViewItem.mChildrenListView);
            }
			FinishListView(listView, listView.mX, listView.mY, false);
        }

		public void Refresh()
		{
			mAllowSideEffects = true;
			if (mListView != null)
				RefreshListView(mListView);
			MarkDirty();
			mCloseDelay = 40;
		}

        public void SetupListViewItem(WatchListViewItem listViewItem, String name, String evalStr)
        {
            HoverListView hoverListView = (HoverListView)listViewItem.mListView;
			hoverListView.mSizeDirty = true;
			if (WantsHorzResize(hoverListView))
            	hoverListView.mWantsHorzResize = true;
            DoListViewItem(hoverListView, (HoverListViewItem)listViewItem, name, evalStr, false);
        }

		void RefreshWatch()
		{
			Refresh();
		}

        HoverListViewItem DoListViewItem(HoverListView listView, HoverListViewItem listViewItem, String displayString, String evalString, bool isPending, WatchEntry parentWatchEntry = null)
        {
			if ((displayString.StartsWith(":")) && (displayString.Contains('\n')))
			{
				HoverListViewItem headListView = null;

				int displayIdx = 0;
				for (var displayLine in displayString.Split('\n'))
				{
					if (displayIdx == 0)
					{
						let str = scope String(displayLine);
						headListView = DoListViewItem(listView, listViewItem, str, str, isPending);
					}
					else
					{
						var line = scope String();
						line.Reference(displayLine);

						if (line.StartsWith("@"))
						{
							String cmd = new String("ShowCode ");

							let str = scope String(":");
							int columnIdx = 0;
							for (var column in line.Split('\t'))
							{
								if (columnIdx == 0)
								{
									var fileName = scope String(column.Ptr + 1, column.Length - 1);
									Utils.QuoteString(fileName, cmd);
								}
								else if (columnIdx == 1)
								{
									cmd.Append(' ');
									cmd.Append(column);
								}
								else
								{
									str.Append(column);
								}
								columnIdx++;
							}
							var subListViewItem = DoListViewItem(listView, listViewItem, str, str, isPending);
							subListViewItem.mTextAction = cmd;
							subListViewItem.mTextColor = HoverListViewItem.cActionColor;
						}
						else
						{
							let str = scope String(":");
							str.Append(line);
							DoListViewItem(listView, listViewItem, str, str, isPending);
						}
					}

					displayIdx++;
				}
				return headListView;
			}

            HoverListViewItem valueSubItem = null;
			var useListViewItem = listViewItem;
            if (useListViewItem == null)
                useListViewItem = (HoverListViewItem)listView.GetRoot().CreateChildItem();

            if (useListViewItem.mWatchEntry == null)
            {
                useListViewItem.mWatchEntry = new WatchEntry();
                valueSubItem = (HoverListViewItem)useListViewItem.CreateSubItem(1);
                valueSubItem.mOnMouseDown.Add(new (evt) => { listView.mHoverWatch.ValueMouseDown(evt); });
            }
            else
            {
                valueSubItem = (HoverListViewItem)useListViewItem.GetSubItem(1);
            }

			bool isLiteral = displayString.StartsWith("'") || displayString.StartsWith("\"");

            var watch = useListViewItem.mWatchEntry;
            String.NewOrSet!(watch.mName, displayString);
            String.NewOrSet!(watch.mEvalStr, evalString);

			if (watch.mEvalStr.StartsWith("!raw"))
			{
				for (int i < 4)
					watch.mEvalStr[i] =  ' ';
				watch.mResultType = .RawText;
			}

            useListViewItem.mWatchEntry = watch;
			if (!isLiteral)
            	useListViewItem.Label = displayString;
			else
				useListViewItem.Label = "";
            useListViewItem.mOnMouseDown.Add(new (evt) => { listView.mHoverWatch.ValueMouseDown(evt); });
            useListViewItem.mOpenOnDoubleClick = false;

            bool isStringLiteral = false;
            String val = scope String();
            if (evalString.StartsWith(":", StringComparison.Ordinal))
            {
				var showString = scope String(4096)..Append(evalString, 1);
				bool isShowingDoc = showString.Contains('\x01');
				if (!isShowingDoc)
				{
					int crPos = showString.IndexOf('\n');
					if (crPos != -1)
					{
						val.Append("\n\n");
						val.Append(showString, crPos + 1);
						showString.RemoveToEnd(crPos);
					}
				}
				showString.Replace('\r', '\n');
                useListViewItem.Label = showString;
                isStringLiteral = true;
            }
            else
			{
				if (isPending)
                	IDEApp.sApp.mDebugger.EvaluateContinue(val);
				else
				{
					DebugManager.EvalExpressionFlags flags = default;
					if (mDeselectCallStackIdx)
						flags |= DebugManager.EvalExpressionFlags.DeselectCallStackIdx;
					if (mAllowSideEffects)
						flags |= .AllowSideEffects | .AllowCalls;
					if (gApp.mSettings.mDebuggerSettings.mAutoEvaluateProperties)
						flags |= .AllowProperties;
					if (watch.mResultType == .RawText)
						flags |= .RawStr;

					DebugManager.Language language = mLanguage;
					if (parentWatchEntry != null)
						language = parentWatchEntry.mLanguage;
					gApp.DebugEvaluate(null, watch.mEvalStr, val, -1, language, flags);
				}
				if (val == "!pending")
				{
					watch.mIsPending = true;
					valueSubItem.Label = "";
					return useListViewItem;
				}
				watch.mIsPending = false;
			}

			if (watch.mResultType == .RawText)
			{
				String.NewOrSet!(valueSubItem.mLabel, val);
				return useListViewItem;
			}

            var vals = scope List<StringView>(val.Split('\n'));

			//if (!vals[0].IsEmpty)
            	String.NewOrSet!(valueSubItem.mLabel, vals[0]);
			if ((vals[0] == "!sideeffects") || (vals[0] == "!incomplete"))
			{
				if (useListViewItem.mWatchRefreshButton == null)
				{
					useListViewItem.mWatchRefreshButton = new WatchRefreshButton();
					useListViewItem.mWatchRefreshButton.Resize(GS!(-22), 0, GS!(20), GS!(20));
					useListViewItem.mWatchRefreshButton.mOnMouseDown.Add(new (evt) => RefreshWatch());
					valueSubItem.AddWidget(useListViewItem.mWatchRefreshButton);
					mListView.mListSizeDirty = true;
				}

				valueSubItem.mFailed = false;
				valueSubItem.Label = "";
			}
            else if (valueSubItem.mLabel.StartsWith("!", StringComparison.Ordinal))
            {
                var errorVals = scope List<StringView>(scope String(valueSubItem.mLabel, 1).Split('\t'));
                if (errorVals.Count > 1)
                {
                    String.NewOrSet!(valueSubItem.mLabel, errorVals[2]);
                }
                else
                    String.NewOrSet!(valueSubItem.mLabel, errorVals[0]);
                valueSubItem.mFailed = true;
            }
            else
                valueSubItem.mFailed = false;

            if ((vals.Count > 1) && (!vals[1].IsEmpty))
                String.NewOrSet!(watch.mResultTypeStr, vals[1]);
            else
                DeleteAndNullify!(watch.mResultTypeStr);

            int cmdStringCount = Math.Max(0, vals.Count - 2);
            int memberCount = 0;

            ClearAndDeleteItems(useListViewItem.mPendingWatches);

            watch.mResultType = isStringLiteral ? WatchResultType.None : WatchResultType.Value;
            watch.mIsDeleted = false;
            watch.mIsStackAlloc = false;
            watch.mIsAppendAlloc = false;
            watch.mCanEdit = false;
			watch.mLanguage = .NotSet;
			watch.mMemoryBreakpointAddr = 0;

			DeleteAndNullify!(watch.mEditInitialize);
			DeleteAndNullify!(watch.mAction);
            for (int32 memberIdx = 0; memberIdx < cmdStringCount; memberIdx++)
            {
                var memberVals = scope List<StringView>(scope String(vals[memberIdx + 2]).Split('\t'));
				var memberVals0 = scope String(memberVals[0]);

                if (memberVals0.StartsWith(":"))
                {
                    if (memberVals0 == ":literal")
                    {
                        // Don't show literals
                        if (!mAllowLiterals)
                        {
							// This messes up cases where we view function pointers, whose expanded view includes an debugger-generated literal
							//  What was this supposed to catch?
							//valueSubItem.mFailed = true;
						}
                    }
                    else if (memberVals0 == ":canEdit")
                    {
                        watch.mCanEdit = true;
                        if (memberVals.Count > 1)
							String.NewOrSet!(watch.mEditInitialize, scope String(memberVals[1]));
                    }
                    else if (memberVals0 == ":editVal")
                    {
                        String.NewOrSet!(watch.mEditInitialize, scope String(memberVals[1]));
                    }
					else if (memberVals0 == ":break")
					{
						watch.mMemoryBreakpointAddr = (int)Int64.Parse(memberVals[1], .HexNumber);
					}
                    else if (memberVals0 == ":referenceId")
                    {
                        String.NewOrSet!(watch.mReferenceId, scope String(memberVals[1]));
                    }
                    else if (memberVals0 == ":repeat")
                    {
                        int startIdx = Int.Parse(scope String(memberVals[1]));
                        int count = Int.Parse(scope String(memberVals[2]));
                        int maxShow = Int.Parse(scope String(memberVals[3]));

                        String displayStr = new String(memberVals[4]);
                        String evalStr = new String(memberVals[5]);

                        String addrParam = null;
						String addrParam2 = null;
                        if (memberVals.Count > 6)
                            addrParam = scope:: String(memberVals[6]);
						if (memberVals.Count > 7)
							addrParam2 = scope:: String(memberVals[7]);

                        WatchSeriesInfo watchSeriesInfo = new WatchSeriesInfo();
                        watchSeriesInfo.mStartMemberIdx = (int32)memberCount;
                        watchSeriesInfo.mDisplayTemplate = displayStr;
                        watchSeriesInfo.mEvalTemplate = evalStr;
                        watchSeriesInfo.mStartIdx = startIdx;
                        watchSeriesInfo.mCount = count;
                        watchSeriesInfo.mShowPageSize = (int32)maxShow;
                        watchSeriesInfo.mShowPages = 1;

                        var memberWatch = new PendingWatch();
                        memberWatch.mName = new String();
                        memberWatch.mName.AppendF(displayStr, startIdx);
                        memberWatch.mEvalStr = new String();
                        memberWatch.mEvalStr.AppendF(evalStr, startIdx, addrParam, addrParam2);
                        memberWatch.mWatchSeriesInfo = watchSeriesInfo;
                        useListViewItem.mPendingWatches.Add(memberWatch);
                        memberCount++;
                    }
                    else if (memberVals0 == ":addrs")
                    {
                        PendingWatch pendingWatch = useListViewItem.mPendingWatches[useListViewItem.mPendingWatches.Count - 1];
                        String.NewOrSet!(pendingWatch.mWatchSeriesInfo.mAddrs, memberVals[1]);
                        pendingWatch.mWatchSeriesInfo.mAddrsEntrySize = 1;
                    }
                    else if (memberVals0 == ":addrsEntrySize")
                    {
                        int32 addrsEntrySize = int32.Parse(scope String(memberVals[1]));
                        PendingWatch pendingWatch = useListViewItem.mPendingWatches[useListViewItem.mPendingWatches.Count - 1];
                        pendingWatch.mWatchSeriesInfo.mAddrsEntrySize = addrsEntrySize;
                    }
                    else if (memberVals0 == ":continuation")
                    {
                        PendingWatch pendingWatch = useListViewItem.mPendingWatches[useListViewItem.mPendingWatches.Count - 1];
                        String.NewOrSet!(pendingWatch.mWatchSeriesInfo.mContinuationData, memberVals[1]);
                    }
					else if (memberVals0 == ":action")
					{
						watch.mAction = new String(memberVals[1]);
						useListViewItem.AddActionButton();
					}
					else if (memberVals0 == ":language")
					{
						int32 language;
						if (int32.Parse(memberVals[1]) case .Ok(out language))
						{
							watch.mLanguage = (.)language;
						}
					}
					else if (memberVals0 == ":warn")
					{
						if (watch.mWarnings == null)
							watch.mWarnings = new .();
						watch.mWarnings.Add(new String(memberVals[1]));
					}
                    else
                        watch.ParseCmd(memberVals);
                    continue;
                }

                if (memberVals.Count >= 2)
                {
                    if (useListViewItem.mOpenButton == null)
                    {
                        useListViewItem.MakeParent();
                        useListViewItem.mOpenButton.mOnMouseDown.Add(new (evt) => { listView.mHoverWatch.OpenButtonClicked(evt); });//OpenButtonClicked;
                    }

                    var memberWatch = new PendingWatch();
                    String.NewOrSet!(memberWatch.mName, memberVals[0]);
					String evalStr = scope String();
					evalStr.AppendF(scope String(memberVals[1]), watch.mEvalStr).IgnoreError();
					String.NewOrSet!(memberWatch.mEvalStr, evalStr);

                    useListViewItem.mPendingWatches.Add(memberWatch);
                    memberCount++;
                }

                if (watch.mEvalStr.Length == 0)
                    watch.mResultType = WatchResultType.None;
            }

            /*if ((watch.mReferenceId == null) && (watch.mEvalStr.Length > 0) && (!valueSubItem.mFailed))
            {
                // Allocate a referenceId if we didn't get one (was a temporary expression)
                //  Make it start with a zero so it parses but is easy to identify as not-an-identifier
                watch.mReferenceId = "0" + (++WatchListView.sCurDynReferenceId).ToString();
            }*/

            if ((memberCount > 0) && (useListViewItem.mOpenButton == null))
            {
                useListViewItem.MakeParent();
                useListViewItem.mOpenButton.mOnMouseDown.Add(new (evt) => { listView.mHoverWatch.OpenButtonClicked(evt); });//OpenButtonClicked;
            }

            if ((memberCount == 0) && (useListViewItem.mOpenButton != null))
            {
				Widget.RemoveAndDelete(useListViewItem.mOpenButton);
                useListViewItem.mOpenButton = null;
				delete useListViewItem.mChildItems;
                useListViewItem.mChildItems = null;
            }
            //if (valueSubItem.mFailed)
                //return null;
            return useListViewItem;
        }

        HoverListView CreateListView()
        {
            //var font = DarkTheme.sDarkTheme.mSmallFont;
            var listView = new HoverListView(this);

			//Debug.WriteLine("HoverWatch.CreateListView {0}", listView);

            listView.mHoverWatch = this;
            listView.SetShowHeader(false);
            listView.mShowColumnGrid = true;

#unwarn
            var nameColumn = listView.AddColumn(100, "Name");
#unwarn
            var valueColumn = listView.AddColumn(100, "Value");

            return listView;
        }

		int32 mResizeCount = 0;

		Widget GetParentWidget()
		{
			Widget parentWidget = null;
			var parentEditWidget = mTextPanel.EditWidget;
			if (parentEditWidget != null)
				parentWidget = parentEditWidget.Content;
			else
				parentWidget = mTextPanel.mParent;
			return parentWidget;
		}

        void ResizeWindow()
        {
			if (mClosed)
			{
				//Debug.WriteLine("HoverWatch.ResizeWindow Closed {0}", this);
				return;
			}

			if ((mWidgetWindow != null) && (mWidgetWindow.mHasClosed))
				return;

            /*if ((mWidgetWindow == null) || (mWidgetWindow.mHasClosed))
                return;*/

			float minX = Int32.MaxValue;
			float minY = Int32.MaxValue;
            float maxX = Int32.MinValue;
            float maxY = Int32.MinValue;
            for (var childWidget in mContentWidget.mChildWidgets)
            {
				minX = Math.Min(minX, childWidget.mX);
				minY = Math.Min(minY, childWidget.mY);
                maxX = Math.Max(maxX, childWidget.mX + childWidget.mWidth + GS!(8));
                maxY = Math.Max(maxY, childWidget.mY + childWidget.mHeight + GS!(8));
            }

			mContentWidget.mX = -minX;
			mContentWidget.mY = -minY;
			if (mWidgetWindow == null)
			{
				//Debug.WriteLine("HoverWatch.ResizeWindow new WidgetWindow {0}", this);

				var parentWidget = GetParentWidget();

				BFWindow.Flags windowFlags = BFWindow.Flags.ClientSized /*| BFWindow.Flags.PopupPosition*/ | BFWindow.Flags.NoActivate | BFWindow.Flags.DestAlpha;
#unwarn
				WidgetWindow widgetWindow = new WidgetWindow(parentWidget.mWidgetWindow,
				    "HoverWatch",
				    (int32)(mOrigScreenX + minX), (int32)(mOrigScreenY + minY), (int32)(maxX - minX), (int32)(maxY - minY),
				    windowFlags, this);

				WidgetWindow.sOnMouseDown.Add(new => HandleMouseDown);
				WidgetWindow.sOnMouseWheel.Add(new => HandleMouseWheel);
				WidgetWindow.sOnMenuItemSelected.Add(new => HandleSysMenuItemSelected);
				WidgetWindow.sOnKeyDown.Add(new => HandleKeyDown);

				widgetWindow.mOnWindowClosed.Add(new (window) => Close());

				mTaskbarXOffset = mOrigScreenX - widgetWindow.mNormX;
				mTaskbarYOffset = mOrigScreenY - widgetWindow.mNormY;
			}
            else
			{
				AutoComplete autocomplete = null;
				if (mEditWidget != null)
				{
					var watchEditWidgetContent = (ExpressionEditWidgetContent)mEditWidget.Content;
					autocomplete = watchEditWidgetContent.mAutoComplete;
				}

				if (autocomplete != null)
					autocomplete.SetIgnoreMove(true);
                mWidgetWindow.Resize((int32)(mOrigScreenX - mTaskbarXOffset + minX), (int32)(mOrigScreenY - mTaskbarYOffset + minY), (int32)(maxX - minX), (int32)(maxY - minY));
				if (autocomplete != null)
					autocomplete.SetIgnoreMove(false);
			}
        }

        void FinishListView(HoverListView listView, float x = 0, float y = 0, bool wantsHorzResize = true)
        {
			if (!listView.mRequestPos.HasValue)
			{
				listView.mRequestPos = Point(x, y);
			}

            var font = DarkTheme.sDarkTheme.mSmallFont;
            float nameWidth = 0;
            float valueWidth = 0;

            bool hadMembers = false;

			bool hasLeftIcon = false;
			bool hasRightValues = false;

			bool wantWordWrap = false;

            listView.mLabelX = GS!(40);
            float childHeights = 0;
            for (WatchListViewItem listViewItem in listView.GetRoot().mChildItems)
            {
                childHeights += listViewItem.mSelfHeight + listViewItem.mChildAreaHeight + listViewItem.mBottomPadding;

                if (listViewItem.mChildItems != null)
                    hadMembers = true;

                if (listViewItem.mLabel == null)
                    continue;


				if (listViewItem.mWatchEntry.mEvalStr.StartsWith(':'))
					wantWordWrap = true;

                //nameWidth = Math.Max(nameWidth, font.GetWidth(listViewItem.mLabel));
                //TODO:
                FontMetrics fontMetrics = .();
                float nameHeight = font.Draw(null, listViewItem.mLabel, 0, 0, -1, 0, FontOverflowMode.Clip, &fontMetrics);
				float thisNameWidth = fontMetrics.mMaxWidth;
				if (listViewItem.mWatchRefreshButton != null)
					thisNameWidth += GS!(18);
                nameWidth = Math.Max(nameWidth, thisNameWidth);

                float addHeight = nameHeight - listView.mFont.GetLineSpacing();
                childHeights += addHeight;
                listViewItem.mSelfHeight += addHeight;

                float checkValueWidth = font.GetWidth(listViewItem.GetSubItem(1).mLabel);
                if (listViewItem.GetSubItem(1) == mEditingItem)
                {
					String text = scope String();
					mEditWidget.GetText(text);
                    checkValueWidth = Math.Max(GS!(12), font.GetWidth(text));
                }
				if (listViewItem.mWatchEntry.mAction != null)
					checkValueWidth += GS!(16);
                valueWidth = Math.Max(valueWidth, checkValueWidth);

				if (!listViewItem.mSubItems[1].mLabel.IsEmpty)
					hasRightValues = true;
                if (listViewItem.mWatchEntry.mResultType != WatchResultType.None)
                    hasLeftIcon = true;
            }

            if (!hadMembers)
                listView.mLabelX -= GS!(14);
            if (!hasRightValues)
            {
				if (hasLeftIcon)
                	listView.mLabelX += GS!(4);
				else
					listView.mLabelX -= GS!(4);
                listView.mShowColumnGrid = false;
            }

            listView.mColumns[0].mWidth = nameWidth + listView.mLabelX + GS!(8);
            listView.mColumns[1].mWidth = valueWidth + GS!(10);

            float wantWidth = listView.mColumns[0].mWidth + listView.mColumns[1].mWidth + GS!(8);
            float height = childHeights + GS!(6);

            float maxHeight = font.GetLineSpacing() * 12 + 6.001f;
            if (height > maxHeight)
            {
                if (listView.mVertScrollbar == null)
				{
                    listView.InitScrollbars(false, true);
					listView.mVertScrollbar.mOnScrollEvent.Add(new (dlg) =>
						{
							if (mEditWidget != null)
								HandleEditLostFocus(mEditWidget);
						});
					var thumb = listView.mVertScrollbar.mThumb;
					thumb.mOnDrag.Add(new (deltaX, deltaY) =>
                        {
							// Only allow size to grow between what it is current at and what we want it to be at.
							// For exactly-sized scroll areas this means it wouldn't change at all.
							/*float haveWidth = listView.mWidth;
							float setWidth = listView.mWidth + parentX;
							float wantWidth = listView.mColumns[0].mWidth + listView.mColumns[1].mWidth + GS!(26);
							if (haveWidth < wantWidth)
								setWidth = Math.Max(haveWidth, Math.Min(setWidth, wantWidth));
							else
								setWidth = Math.Min(haveWidth, Math.Max(setWidth, wantWidth));*/

							float setWidth = listView.mWidth;
							float thumbX = thumb.mDraggableHelper.mMouseX;

							if (thumbX < 0)
							{
								float transX;
								float transY;
								thumb.SelfToOtherTranslate(listView, thumbX, 0, out transX, out transY);
								setWidth = transX + thumb.mWidth;
							}
							else if (thumbX > thumb.mWidth)
							{
								float wantWidth = listView.mColumns[0].mWidth + listView.mColumns[1].mWidth + GS!(26);
								float transX;
								float transY;
								thumb.SelfToOtherTranslate(listView, thumbX, 0, out transX, out transY);
								setWidth = Math.Min(transX, wantWidth);
								setWidth = Math.Max(setWidth, listView.mWidth);
							}

							setWidth = Math.Max(setWidth, listView.mColumns[0].mWidth + GS!(64));

							if (setWidth != listView.mWidth)
							{
								listView.Resize(listView.mX, listView.mY, setWidth, listView.mHeight);
								ResizeWindow();
							}
                        });
				}
                height = maxHeight;
                wantWidth += GS!(20);
            }

			int workspaceX;
			int workspaceY;
			int workspaceWidth;
			int workspaceHeight;

			float popupX = listView.mRequestPos.Value.x;
			float popupY = listView.mRequestPos.Value.y;

			// GetWorkspaceRectFrom expects virtual screen coordinates
			int screenX = (.)(popupX + mOrigScreenX);
			int screenY = (.)(popupY + mOrigScreenY);

			BFApp.sApp.GetWorkspaceRectFrom(screenX, screenY, 1, 1,
				out workspaceX, out workspaceY, out workspaceWidth, out workspaceHeight);

			int maxWidth = workspaceWidth - screenX + workspaceX - GS!(8);

			if (mTextPanel != null)
			{
				maxWidth = Math.Min(maxWidth, mTextPanel.mWidgetWindow.mWindowWidth);
			}

			var useWidth = Math.Min(wantWidth, maxWidth);

			if (!listView.mIsReversed)
			{
				if (mOrigScreenY + popupY + height - 0 >= workspaceHeight)
					listView.mIsReversed = true;
			}

			if (listView.mIsReversed)
			{
				popupY -= height + GS!(20);
			}

            listView.UpdateAll();
			if (wantsHorzResize)
			{
				if (mWidgetWindow != null)
					useWidth = Math.Max(useWidth, mWidgetWindow.mMouseX - popupX + GS!(12));
			}
			else
				useWidth = listView.mWidth;

			if ((wantWordWrap) && (useWidth < wantWidth))
			{
				float actualMaxWidth = 0;

				for (WatchListViewItem listViewItem in listView.GetRoot().mChildItems)
				{
				    if (listViewItem.mLabel == null)
				        continue;

					listView.mWordWrap = true;
				    FontMetrics fontMetrics = .();
				    float nameHeight = font.Draw(null, listViewItem.mLabel, 0, 0, -1, useWidth - GS!(32), FontOverflowMode.Wrap, &fontMetrics);
					actualMaxWidth = Math.Max(actualMaxWidth, fontMetrics.mMaxWidth);

					float addHeight = nameHeight - listViewItem.mSelfHeight;
					listViewItem.mSelfHeight = nameHeight;
					height += addHeight;
				}

				useWidth = actualMaxWidth + GS!(32);

				listView.mColumns[0].mWidth = useWidth - GS!(2);
			}

			height += GS!(2);

            listView.Resize(popupX, popupY, useWidth, height);
            ResizeWindow();
        }

		bool WantsHorzResize(HoverListView listView)
		{
			if ((mWidgetWindow != null) && (mWidgetWindow.mCaptureWidget != null) &&
				((mWidgetWindow.mCaptureWidget == listView.mVertScrollbar) || (mWidgetWindow.mCaptureWidget.mParent == listView.mVertScrollbar)))
				return false;
			return true;
		}

        void EditListViewItem(HoverListViewItem listViewItem)
        {
            if (mEditWidget != null)
                return;

            var listView = (HoverListView)listViewItem.mListView;
            if ((listViewItem.mColumnIdx == 0) && (listViewItem.mParentItem.mParentItem != null))
                return;

			//listView.mHorzScrollbar.mOnScrollEvent.Add();

            var headListViewItem = (HoverListViewItem)listViewItem.GetSubItem(0);
            if ((headListViewItem.mWatchEntry != null) && (!headListViewItem.mWatchEntry.mCanEdit))
			{
				if (headListViewItem.mActionButton != null)
				{
					mActionIdx++;
					headListViewItem.mActionButton.mOnMouseDown(null);
				}
                return;
			}

            var valueListViewItem = (HoverListViewItem)listViewItem.GetSubItem(1);

            mCancelingEdit = false;

            CloseChildren(listView);
            HoverEditWidget editWidget = new HoverEditWidget(this);
			//editWidget.mScrollContentInsets.Set(GS!(2), GS!(3), GS!(1), GS!(3));
			//editWidget.Content.mTextInsets.Set(GS!(-3), GS!(2), 0, GS!(2));

            editWidget.Content.mAutoHorzScroll = false;
            mEditWidget = editWidget;
            //editWidget.mWatchListViewItem = (HoverListViewItem)valueListViewItem;
            String editVal = valueListViewItem.mLabel;
            if (headListViewItem.mWatchEntry.mEditInitialize != null)
                editVal = headListViewItem.mWatchEntry.mEditInitialize;
			editWidget.mExpectingType = headListViewItem.mWatchEntry.mResultTypeStr;
            editWidget.SetText(editVal);
			editWidget.Content.ClearUndoData();
            editWidget.Content.SelectAll();
            valueListViewItem.Label = "";

            float x;
            float y;
            valueListViewItem.SelfToOtherTranslate(listView, 0, 0, out x, out y);

            mEditingItem = (HoverListViewItem)valueListViewItem;

            x = valueListViewItem.LabelX - GS!(4);

            float width = valueListViewItem.LabelWidth + GS!(6);

            if (listView.mVertScrollbar != null)
                width = listView.mWidth - x - GS!(20);
            else
                width = listView.mWidth - x - GS!(4);


            //editWidget.Resize(x - GS!(1), y - GS!(2), width, GS!(24));
			editWidget.ResizeAround(x, y, width);

            listView.AddWidget(editWidget);

            editWidget.mOnLostFocus.Add(new => HandleEditLostFocus);
            editWidget.mOnSubmit.Add(new => HandleRenameSubmit);
            editWidget.mOnCancel.Add(new => HandleRenameCancel);
            WidgetWindow.sOnMouseWheel.Add(new => HandleMouseWheel);

            editWidget.SetFocus();
			mActionIdx++;
        }

        void HandleRenameCancel(EditEvent theEvent)
        {
            mCancelingEdit = true;
            HandleEditLostFocus((EditWidget)theEvent.mSender);
        }

        void HandleRenameSubmit(EditEvent theEvent)
        {
            var watchEditWidgetContent = (ExpressionEditWidgetContent)mEditWidget.Content;
            if ((watchEditWidgetContent.mAutoComplete != null) && (watchEditWidgetContent.mAutoComplete.HasSelection()))
                watchEditWidgetContent.mAutoComplete.InsertSelection((char8)0);
            HandleEditLostFocus((EditWidget)theEvent.mSender);
        }

        void HandleEditLostFocus(Widget widget)
        {
            EditWidget editWidget = (EditWidget)widget;
            editWidget.mOnLostFocus.Remove(scope => HandleEditLostFocus, true);
            editWidget.mOnSubmit.Remove(scope => HandleRenameSubmit, true);
            editWidget.mOnCancel.Remove(scope => HandleRenameCancel, true);
            WidgetWindow.sOnMouseWheel.Remove(scope => HandleMouseWheel, true);

            if (!mEditWidget.Content.HasUndoData())
                mCancelingEdit = true;

            String newValue = scope String();
            editWidget.GetText(newValue);
            newValue.Trim();

            HoverListView listView = (HoverListView)mEditingItem.mListView;
            WatchListViewItem listViewItem = (WatchListViewItem)mEditingItem;
            int32 column = listViewItem.mColumnIdx;
            //bool hadError = false;

            if ((column == 1) && (newValue.Length > 0) && (!mCancelingEdit))
            {
                var headListViewItem = (WatchListViewItem)listViewItem.GetSubItem(0);
                String evalStr = scope String(headListViewItem.mWatchEntry.mEvalStr, ", assign=", newValue);
                String val = scope String();
				DebugManager.EvalExpressionFlags flags = default;
				if (mDeselectCallStackIdx)
					flags |= DebugManager.EvalExpressionFlags.DeselectCallStackIdx;
				flags |= .AllowCalls | .AllowSideEffects;
                gApp.DebugEvaluate(null, evalStr, val, -1, headListViewItem.mWatchEntry.mLanguage, flags);
                //IDEApp.sApp.OutputLine(val);

                if (val.StartsWith("!", StringComparison.Ordinal))
                {
                    String errorString = scope String(val, 1);
                    var errorVals = scope List<StringView>(errorString.Split('\t'));
                    if (errorVals.Count == 3)
                    {
                        int errorStart = (int32)int32.Parse(scope String(errorVals[0]));
                        int errorEnd = errorStart + int32.Parse(scope String(errorVals[1])).Get();
						if (errorEnd > evalStr.Length)
							errorEnd = evalStr.Length;
                        errorString.Set(errorVals[2]);
                        if ((errorEnd > 0) && (errorStart < evalStr.Length) && (!errorString.Contains(':')))
                            errorString.Append(": ", scope String(evalStr, errorStart, errorEnd - errorStart));
                    }
                    //hadError = true;
                    IDEApp.sApp.Fail(errorString);
                }

                IDEApp.sApp.MemoryEdited();
            }

			editWidget.RemoveSelf();
			BFApp.sApp.DeferDelete(editWidget);
            mEditWidget = null;
            mEditingItem = null;

            // We need to refresh all values in all list views after an edit
            for (var childWidget in mContentWidget.mChildWidgets)
            {
                var childListView = childWidget as HoverListView;
                RefreshListView(childListView);
            }

            mEditLostFocusTick = mUpdateCnt;
            FinishListView(listView, listView.mX, listView.mY);

			if (mTextPanel != null)
            	mTextPanel.SetFocus();

            /*if (!hadError)
                mSourceViewPanel.mWidgetWindow.SetForeground();*/
        }

        void ShowRightClickMenu(WatchListViewItem listViewItem, float x, float y)
        {
			if (mOrigEvalString.StartsWith(":"))
				return;

			float clickX = x;
			float clickY = DarkTheme.sDarkTheme.mSmallFont.GetLineSpacing() + GS!(1);
			listViewItem.SelfToOtherTranslate(listViewItem.mListView.GetRoot(), clickX, clickY, var aX, var aY);
			WatchPanel.ShowRightClickMenu(null, (WatchListView)listViewItem.mListView, listViewItem, aX, aY);
        }

        void ValueMouseDown(MouseEvent theEvent)
        {
            if (mUpdateCnt == mEditLostFocusTick)
                return;

            HoverListViewItem clickedItem = (HoverListViewItem)theEvent.mSender;
            HoverListViewItem item = (HoverListViewItem)clickedItem.GetSubItem(0);

            if (theEvent.mBtn == 0)
            {
                //PropertyEntry aPropertyEntry = mPropertyEntryMap[item];
                //EditValue(aPropertyEntry);
                EditListViewItem(item);
            }
            else if (theEvent.mBtn == 1)
            {
                float aX, aY;
				clickedItem.SelfToOtherTranslate(clickedItem.mListView.GetRoot(), theEvent.mX, theEvent.mY, out aX, out aY);
                ShowRightClickMenu(item, aX, aY);
            }

            theEvent.mHandled = true;
        }

        public HoverListViewItem Eval(String displayString, String evalString, bool isPending)
        {
			Debug.Assert(evalString != null);
            mOrigEvalString.Set(evalString);
			if (mListView == null)
            	mListView = CreateListView();
			else
				mListView.GetRoot().Clear();
            return DoListViewItem(mListView, null, displayString, evalString, isPending);
        }

        public void SetListView(HoverListView listView)
        {
            mListView = listView;
            CloseChildren(mListView);
			mOwnsListView = false;
        }

		void PanelRemovedFromParent(Widget widget, Widget prevParent, WidgetWindow widgetWindow)
		{
			Close();
		}

        public bool Show(TextPanel textPanel, float x, float y, String displayString, String evalString)
        {
			if ((mIsShown) && (mListView != null) && (evalString.StartsWith(':')))
			{
				var listItem = mListView.GetRoot().GetChildAtIndex(0) as HoverListViewItem;
				bool isLiteral = (listItem.Label == listItem.GetSubItem(1).Label) || (listItem.Label == "");

				StringView useStr = evalString.Substring(1);
				int crPos = useStr.IndexOf('\n');
				if (crPos != -1)
					useStr.RemoveToEnd(crPos);
				crPos = useStr.IndexOf('\r');
				if (crPos != -1)
					useStr.RemoveFromStart(crPos + 1);
				if (isLiteral)
					mDisplayString.Set(useStr);
				else
				{
					mDisplayString.Append('\n');
					mDisplayString.Append(useStr);
				}

				Rehup();
				return true;
			}

            if (mIsShown)
                Clear();

            mIsShown = true;
            if (mTextPanel == null)
            {
				if (sActiveHoverWatch != null)
				{
					sActiveHoverWatch.Close();
				}

                sActiveInstanceCount++;
                Debug.Assert(sActiveInstanceCount == 1);

				sActiveHoverWatch = this;
            }

			if (mTextPanel != null)
			{
				mTextPanel.mOnRemovedFromParent.Add(scope => PanelRemovedFromParent);
			}

            mTextPanel = textPanel;
			mTextPanel.mOnRemovedFromParent.Add(new => PanelRemovedFromParent);
            if (mTextPanel.mHoverWatch != null)
                Debug.Assert(mTextPanel.mHoverWatch == this);
            mTextPanel.mHoverWatch = this;
			if (displayString == null)
				mDisplayString.Clear();
			else
				mDisplayString.Set(displayString);

			if (evalString == null)
				mEvalString.Clear();
			else
            	mEvalString.Set(evalString);
            mOrigX = x;
            mOrigY = y;

			Widget parentWidget = GetParentWidget();

            //var parentWidget = textPanel.EditWidget.Content;

            if (evalString != null)
            {
                if (Eval(displayString, evalString, false).Failed)
				{
					if (mListView.mParent != null)
						mListView.RemoveSelf();
					delete mListView;
					mListView = null;
                    return false;
				}
            }

            float screenX;
            float screenY;
            parentWidget.SelfToRootTranslate(x, y, out screenX, out screenY);
            screenX += parentWidget.mWidgetWindow.mClientX;
            screenY += parentWidget.mWidgetWindow.mClientY;

			screenX -= GS!(2);
			screenY += GS!(14);
			/*if (screenY + screenHeight + 0 >= workspaceHeight)
			{
				screenY -= screenHeight;
			}*/

			mOrigScreenX = screenX;
			mOrigScreenY = screenY;

			mCreatedWindow = true;
			Debug.Assert((mContentWidget.mChildWidgets == null) || (mContentWidget.mChildWidgets.Count == 0));
			mContentWidget.AddWidget(mListView);
			mListViews.Add(mListView);
			mListView.mHoverWatch = this;
			FinishListView(mListView, 0, 0);

			/*float screenWidth = mListView.mWidth + 8;
			float screenHeight = mListView.mHeight + 8;

            BFWindow.Flags windowFlags = BFWindow.Flags.ClientSized /*| BFWindow.Flags.PopupPosition*/ | BFWindow.Flags.NoActivate | BFWindow.Flags.DestAlpha;
#unwarn
            WidgetWindow widgetWindow = new WidgetWindow(parentWidget.mWidgetWindow,
                "HoverWatch",
                (int)screenX, (int)screenY,
                (int)screenWidth, (int)screenHeight,
                windowFlags,
                this);

            WidgetWindow.sMouseDownHandler.Add(new => HandleMouseDown);
            WidgetWindow.sMouseWheelDelegate.Add(new => HandleMouseWheel);
            WidgetWindow.sMenuItemSelectedDelegate.Add(new => HandleSysMenuItemSelected);
            WidgetWindow.sKeyDownDelegate.Add(new => HandleKeyDown);*/

            return true;
        }

        public void Rehup()
        {
			//Debug.WriteLine("HoverWatch.Rehup {0}", this);
            List<float> openYList = scope List<float>();
            for (var watchListView in mListViews)
            {
                float openY = -1;
                for (int32 itemIdx = 0; itemIdx < watchListView.GetRoot().GetChildCount(); itemIdx++)
                {
                    var watchListViewItem = (WatchListViewItem)watchListView.GetRoot().GetChildAtIndex(itemIdx);
                    if ((watchListViewItem.mOpenButton != null) && (watchListViewItem.mOpenButton.mIsOpen))
                    {
                        //int actualIdx = (int)(watchListViewItem.Y / listView.mFont.GetLineSpacing() + 0.5f);
                        //openY = itemIdx;
                        openY = watchListViewItem.mY;
                    }
                }
                if (openY == -1)
                    break;
                openYList.Add(openY);
            }

            var sourceViewPanel = mTextPanel;
			var widgetWindow = mWidgetWindow;
            Close();
			if (widgetWindow != null)
				widgetWindow.mRootWidget = null; // Detach root
			//Debug.WriteLine("Hoverwatch showing");
			mClosed = false;
            Show(sourceViewPanel, mOrigX, mOrigY, mDisplayString, mOrigEvalString);
            sourceViewPanel.mHoverWatch = this;

            for (int32 listIdx = 0; listIdx < openYList.Count; listIdx++)
            {
                float openY = openYList[listIdx];
                if (listIdx >= mListViews.Count)
                    break;
                var watchListView = mListViews[listIdx];
                watchListView.UpdateAll();
                /*if (openY >= watchListView.GetRoot().GetChildCount())
                    break;
                var hoverListViewItem = (HoverListViewItem)watchListView.GetRoot().GetChildAtIndex(openY);
                if (hoverListViewItem.mOpenButton == null)
                    break;*/

                for (int32 itemIdx = 0; itemIdx < watchListView.GetRoot().GetChildCount(); itemIdx++)
                {
                    var hoverListViewItem = (HoverListViewItem)watchListView.GetRoot().GetChildAtIndex(itemIdx);
                    if ((hoverListViewItem.mY == openY) && (hoverListViewItem.mOpenButton != null))
                    {
                        OpenButtonClicked(hoverListViewItem);
                        break;
                    }
                }
            }

			mRehupEvent();
        }

        void HandleKeyDown(KeyDownEvent keyboardEvent)
        {
            if (DarkTooltipManager.sTooltip != null)
                return;

            if (keyboardEvent.mKeyCode == KeyCode.Escape)
            {
                mCancelingEdit = true;
				Close();
                keyboardEvent.mHandled = true;
            }
        }

        public bool HasContentAt(float x, float y)
        {
			float checkX = x - mContentWidget.mX;
			float checkY = y - mContentWidget.mY;

            for (var child in mContentWidget.mChildWidgets)
            {
                Rect rect = child.GetRect();
                rect.mX -= GS!(12);
                rect.mWidth += GS!(12 + 4);
                rect.mHeight += GS!(4);

                if (rect.Contains(checkX, checkY))
                    return true;
            }
            return false;
        }
    }
}
