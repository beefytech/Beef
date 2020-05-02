using System;
using System.Collections;
using System.Text;
using System.Diagnostics;
using Beefy.gfx;
using Beefy.theme;

namespace Beefy.widgets
{
    public abstract class TabbedView : DockedWidget
    {
        public class TabButton : Widget, IDockable, IDragInterface
        {
            public bool mIsActive;
            public String mLabel ~ delete _;
            public TabbedView mTabbedView;
            public WidgetWindow mNewDraggingWindow;
            public WidgetWindow mSrcDraggingWindow;
            public float mWindowDragRelX;
            public float mWindowDragRelY;
            public float mLastMouseX;
            public float mLastMouseY;
			public bool mOwnsContent = true;
            public Widget mContent;
            public Insets mContentInsets = new Insets() ~ delete _;
            public DragHelper mDragHelper ~ delete _;
            public Event<Action> mCloseClickedEvent ~ _.Dispose();

			delegate void Hey();	
            public float mWantWidth;
			public WidgetWindow mMouseDownWindow;
            
			public String Label
			{
				get
				{
					return mLabel;
				}

				set
				{
					String.NewOrSet!(mLabel, value);
				}
			}

            public this()
            {
                mDragHelper = new DragHelper(this, this);
            }

			public ~this()
			{
				Debug.Assert(mMouseDownWindow == null);

				if ((mContent != null) && (mOwnsContent))
				{
					if (mContent.mParent != null)
						mContent.RemoveSelf();
					delete mContent;
				}
			}

			public override void RehupScale(float oldScale, float newScale)
			{
				float valScale = newScale / oldScale;
				mContentInsets.Scale(valScale);
				//Utils.SnapScale(ref mWidth, valScale);
				mWantWidth *= valScale;
				//Utils.SnapScale(ref mHeight, valScale);
				base.RehupScale(oldScale, newScale);
			}

            public virtual void Activate(bool setFocus = true)
            {
                TabButton button = mTabbedView.GetActiveTab();
                if (button != this)
                {
                    if (button != null)
                        button.Deactivate();
                    mIsActive = true;                    
                    mTabbedView.mNeedResizeTabs = true;

                    mTabbedView.AddWidget(mContent);
                    if ((setFocus) && (mWidgetWindow != null))
                        mContent.SetFocus();
                    ResizeContent();
                }
                else if ((setFocus) && (mWidgetWindow != null))
                    mContent.SetFocus();
            }

            public virtual void ResizeContent()
            {
                mContent.Resize(mContentInsets.mLeft, mTabbedView.mTabHeight + mContentInsets.mTop,
                    Math.Max(mTabbedView.mWidth - mContentInsets.mLeft - mContentInsets.mRight, 0),
                    Math.Max(mTabbedView.mHeight - mTabbedView.mTabHeight - mContentInsets.mTop - mContentInsets.mBottom, 0));
            }
            
            public virtual void Deactivate()
            {
                if (mIsActive)
                {
                    mIsActive = false;
                    mTabbedView.RemoveWidget(mContent);
                }
            }

            public override void MouseDown(float x, float y, int32 btn, int32 btnCount)
            {
				if (btn == 2)
				{
					mCloseClickedEvent();
					return;
				}

				bool wasMouseDown = mMouseDown;
                base.MouseDown(x, y, btn, btnCount);
                Activate();
				if ((mMouseDown) && (!wasMouseDown))
				{
					//Debug.WriteLine("MouseDown {0} {1}", this, mWidgetWindow);
					mMouseDownWindow = mWidgetWindow;
                	mMouseDownWindow.mOnMouseLeftWindow.Add(new => MouseLeftWindow);
				}
            }

            public override void MouseMove(float x, float y)
            {
                base.MouseMove(x, y);

                mLastMouseX = x;
                mLastMouseY = y;
                
                if (mNewDraggingWindow != null)
                {
                    float rootX;
                    float rootY;
                    SelfToRootTranslate(mLastMouseX, mLastMouseY, out rootX, out rootY);
                    
                    mNewDraggingWindow.SetClientPosition(
                        (int32)(mSrcDraggingWindow.mClientX + rootX - mWindowDragRelX), (int32)(mSrcDraggingWindow.mClientY + rootY - mWindowDragRelY));
                }            
            }

            public void DragStart()            
            {                
                mSrcDraggingWindow = mWidgetWindow;

                if ((IsTotalWindowContent()) && (!mWidgetWindow.mIsMainWindow))
                {
                    // Drag around entire window if we are the only content in it
                    SelfToRootTranslate(mDragHelper.mMouseDownX, mDragHelper.mMouseDownY, out mWindowDragRelX, out mWindowDragRelY);
                    mNewDraggingWindow = mWidgetWindow;
                    mNewDraggingWindow.SetAlpha(0.5f, 0, false);
                    mNewDraggingWindow.mOnWindowLostFocus.Add(new => WindowDragLostFocusHandler);
                }                
            }

            public void DragEnd()
            {
                //mWidgetWindow.mMouseLeftWindowDelegate.Remove(scope => MouseLeftWindow, true);

                if ((mSrcDraggingWindow != null) && (mSrcDraggingWindow.mCaptureWidget != null))
					mSrcDraggingWindow.ReleaseMouseCaptures();

                mTabbedView.mParentDockingFrame.GetRootDockingFrame().HideDragTarget(this, !mDragHelper.mAborted);
                if (mNewDraggingWindow != null)
                {
                    mNewDraggingWindow.mOnWindowLostFocus.Remove(scope => WindowDragLostFocusHandler, true);
                    DockingFrame dockingFrame = (DockingFrame)mNewDraggingWindow.mRootWidget;
                    if (dockingFrame.GetDockedWindowCount() > 0)
                        mNewDraggingWindow.SetAlpha(1.0f, 0, true);
                    mNewDraggingWindow = null;
                }
                mSrcDraggingWindow = null;                
            }            

            public void MouseDrag(float x, float y, float dX, float dY)
            {
               	mTabbedView.mParentDockingFrame.GetRootDockingFrame().ShowDragTarget(this);                
            }

            public override void MouseUp(float x, float y, int32 btn)
            {
				bool wasMouseDown = mMouseDown;                
                base.MouseUp(x, y, btn);       
				if ((wasMouseDown) && (!mMouseDown))
				{
     				mMouseDownWindow.mOnMouseLeftWindow.Remove(scope => MouseLeftWindow, true);
					mMouseDownWindow = null;
				}
            }

			public override void RemovedFromParent(Widget previousParent, WidgetWindow window)
			{
				base.RemovedFromParent(previousParent, window);
				
			}

            public virtual bool IsTotalWindowContent()
            {
                return (mTabbedView.mParentDockingFrame.mParentDockingFrame == null) &&
                    (mTabbedView.mParentDockingFrame.GetDockedWindowCount() == 1) &&
                    (mTabbedView.GetTabCount() == 1);
            }

            void WindowDragLostFocusHandler(BFWindow window, BFWindow newFocus)
            {
                mDragHelper.CancelDrag();                
            }

            public void MouseLeftWindow(BFWindow window)
            {
                if (mDragHelper.mIsDragging)
                {
                    if (mNewDraggingWindow == null)
                    {
                        mDragHelper.SetPreparingForWidgetMove(true);

                        WidgetWindow prevWidgetWindow = mWidgetWindow;
                        mTabbedView.mParentDockingFrame.GetRootDockingFrame().HideDragTarget(this);

                        mWindowDragRelX = mDragHelper.mMouseDownX;
                        mWindowDragRelY = mDragHelper.mMouseDownY;

                        float rootX;
                        float rootY;
                        SelfToRootTranslate(mLastMouseX, mLastMouseY, out rootX, out rootY);

                        DockingFrame subFrame = ThemeFactory.mDefault.CreateDockingFrame();

                        var parentWindow = mWidgetWindow;
                        while (parentWindow.mParent != null)
                            parentWindow = (WidgetWindow)parentWindow.mParent;

                        mNewDraggingWindow = new WidgetWindow(parentWindow, "",
                            (int32)(mSrcDraggingWindow.mClientX + rootX - mDragHelper.mMouseDownX), (int32)(mSrcDraggingWindow.mClientY + rootY - mDragHelper.mMouseDownY),
                            300, 500,
                            BFWindowBase.Flags.Border | BFWindowBase.Flags.ThickFrame | BFWindowBase.Flags.Resizable | BFWindowBase.Flags.SysMenu |
                            BFWindowBase.Flags.Caption | BFWindowBase.Flags.Minimize | BFWindowBase.Flags.ToolWindow | BFWindowBase.Flags.TopMost |
                            BFWindowBase.Flags.UseParentMenu,
                            subFrame);
                        Dock(subFrame, null, DockingFrame.WidgetAlign.Top);
                        //subFrame.AddDockedWidget(fourthTabbedView, null, DockingFrame.WidgetAlign.Left, false);

                        prevWidgetWindow.SetNonExclusiveMouseCapture();
                        mNewDraggingWindow.SetAlpha(0.5f, 0, false);
                        //mNewDraggingWindow.SetMouseCapture();
                        //mNewDraggingWindow.mCaptureWidget = sub
                        mDragHelper.SetPreparingForWidgetMove(false);

                        // We need the previous window to keep tracking the mouse dragging, so we need to restore mCaptureWidget since it would be removed
                        //  when the widget gets removed from the previous parent
                        prevWidgetWindow.mCaptureWidget = this;

                        mNewDraggingWindow.mOnWindowLostFocus.Add(new => WindowDragLostFocusHandler);

                        if (mTabbedView.mSharedData.mOpenNewWindowDelegate.HasListeners)
                            mTabbedView.mSharedData.mOpenNewWindowDelegate(mTabbedView, mNewDraggingWindow);
                    }
                }
            }

            public override void Update()
            {
                base.Update();
                if (mNewDraggingWindow != null)
                {
                    if (!mNewDraggingWindow.mHasFocus)
                    {
                        //int a = 0;
                    }                    
                }
            }

            public override void MouseLeave()
            {
                base.MouseLeave();
                
            }

            public bool CanDock(DockingFrame frame, DockedWidget refWidget, DockingFrame.WidgetAlign align)
            {
                return (align != DockingFrame.WidgetAlign.Inside) || (refWidget is TabbedView);
            }

            public virtual void Dock(DockingFrame frame, DockedWidget refWidget, DockingFrame.WidgetAlign align)
            {
                if ((refWidget != null) && (refWidget.mWidgetWindow != mWidgetWindow) && (mWidgetWindow != null))
                    mWidgetWindow.SetForeground();

                if (mTabbedView.GetTabCount() == 1)
                {                    
                    mTabbedView.Dock(frame, refWidget, align);
                    return;
                }

                TabbedView prevTabbedView = mTabbedView;

                frame.StartContentInterpolate();

                if (align == DockingFrame.WidgetAlign.Inside)
                {
                    TabbedView tabbedView = (TabbedView)refWidget;

                    if (tabbedView != mTabbedView)
                    {
                        mTabbedView.RemoveTab(this, false);
                        tabbedView.AddTab(this, tabbedView.GetInsertPositionFromCursor());
                        Activate();
                    }
                }
                else
                {
                    // Create new tabbed view to put this in
                    TabbedView tabbedView = ThemeFactory.mDefault.CreateTabbedView(mTabbedView.mSharedData);
					//tabbedView.mSharedData = mTabbedView.mSharedData.Ref();					
                    //tabbedView.mSharedData.mOpenNewWindowDelegate = mTabbedView.mSharedData.mOpenNewWindowDelegate;
                    tabbedView.SetRequestedSize(mTabbedView.mWidth, mTabbedView.mHeight);
                    mTabbedView.RemoveTab(this, false);
                    tabbedView.AddTab(this);

                    float rootX;
                    float rootY;
                    prevTabbedView.SelfToRootTranslate(mX, mY, out rootX, out rootY);

                    tabbedView.StartInterpolate(rootX, rootY, mWidth, mHeight);
                    tabbedView.Dock(frame, refWidget, align);                    
                }

                if (prevTabbedView.GetTabCount() == 0)
                {                    
                    prevTabbedView.mParentDockingFrame.RemoveDockedWidget(prevTabbedView);
                }
            }

            public virtual void DrawDockPreview(Graphics g)
            {
                using (g.PushTranslate(-mX - mDragHelper.mMouseDownX, -mY - mDragHelper.mMouseDownY))
                    mTabbedView.DrawDockPreview(g);
            }
        }

		public class SharedData
		{
			int32 mRefCount = 1;

			public Event<OpenNewWindowDelegate> mOpenNewWindowDelegate ~ _.Dispose();
			public Event<Action<TabbedView>> mTabbedViewClosed ~ _.Dispose();

			public SharedData Ref()
			{
				mRefCount++;
				return this;
			}

			public void Deref()
			{
				if (--mRefCount == 0)
					delete this;
			}
		}

        public delegate void OpenNewWindowDelegate(TabbedView tabbedView, WidgetWindow newWindow);

        public Action<Menu> mPopulateMenuEvent;
        public float mTabHeight;
        public float mTabAreaWidth;
        public bool mNeedResizeTabs;        
		public SharedData mSharedData ~ _.Deref();

        public List<TabButton> mTabs = new List<TabButton>() ~ delete _;

		public this(SharedData sharedData)
		{
			if (sharedData != null)
				mSharedData = sharedData.Ref();
			else
				mSharedData = new SharedData();
		}

		public ~this()
		{
			for (var tab in mTabs)
				Widget.RemoveAndDelete(tab);
		}

		public void Closed()
		{
			mSharedData.mTabbedViewClosed(this);
		}

		public override void RehupScale(float oldScale, float newScale)
		{
			float valScale = newScale / oldScale;
			Utils.RoundScale(ref mTabHeight, valScale);
			Utils.RoundScale(ref mTabAreaWidth, valScale);
			base.RehupScale(oldScale, newScale);
			mNeedResizeTabs = true;
		}

        public virtual TabButton FindTabForContent(Widget content)
        {
            for (TabButton aTabButton in mTabs)
                if (aTabButton.mContent == content)
                    return aTabButton;
            return null;
        }

        protected virtual TabButton CreateTabButton()
        {
            return new TabButton();
        }

        public virtual int GetTabCount()
        {
            return mTabs.Count;
        }
        
        public virtual void WithTabs(Action<TabbedView.TabButton> func)
        {
            for (var tab in mTabs)
                func(tab);
        }

        public virtual TabButton GetActiveTab()
        {
            TabButton activeTab = null;
            WithTabs(scope [&] (tab) => 
                {
                    if (tab.mIsActive)
                        activeTab = tab;
                });
            return activeTab;
        }

        public virtual TabButton AddTab(String label, float width, Widget content, bool ownsContent)
        {
            TabButton aTabButton = CreateTabButton();
            aTabButton.mTabbedView = this;
			aTabButton.mOwnsContent = ownsContent;
            aTabButton.Label = label;
            aTabButton.mWantWidth = width;
            aTabButton.mHeight = mTabHeight;
            aTabButton.mContent = content;
            AddTab(aTabButton);            
            return aTabButton;
        }

        public virtual int GetInsertPositionFromCursor()
        {
            int bestIdx = mTabs.Count;
            while (bestIdx > 0)
            {
                var checkTab = mTabs[bestIdx - 1];
                float tabCenterX;
                float tabCenterY;
                checkTab.SelfToRootTranslate(checkTab.mWidth / 2, checkTab.mHeight / 2, out tabCenterX, out tabCenterY);
                if (mWidgetWindow.mMouseX > tabCenterX)
                    break;
                bestIdx--;
            }
            return bestIdx;    
        }

        public virtual void AddTab(TabButton tabButton, int insertIdx = 0)
        {
            AddWidget(tabButton);
            mTabs.Insert(insertIdx, tabButton);
            tabButton.mTabbedView = this;
            if (mTabs.Count == 1)
                tabButton.Activate();
            mNeedResizeTabs = true;            
        }

        public virtual void RemoveTab(TabButton tabButton, bool deleteTab = true)
        {
            bool hadFocus = mWidgetWindow.mFocusWidget != null;
            if (tabButton.mIsActive)
                tabButton.Deactivate();
            RemoveWidget(tabButton);
            mTabs.Remove(tabButton);
            if ((GetActiveTab() == null) && (mTabs.Count > 0))
                mTabs[0].Activate((hadFocus) && (mWidgetWindow.mFocusWidget == null));
            mNeedResizeTabs = true;

			if (deleteTab)
				BFApp.sApp.DeferDelete(tabButton);			
        }

        protected virtual void ResizeTabs(bool immediate)
        {
            float curX = 0;
            for (TabButton aTabButton in mTabs)
            {                
                aTabButton.Resize(curX, aTabButton.mY, aTabButton.mWidth, aTabButton.mHeight);
                curX += aTabButton.mWidth;
            }

            mTabAreaWidth = curX;
            mNeedResizeTabs = false;
        }

		public void FinishTabAnim()
		{
			if (mNeedResizeTabs)
			{
				ResizeTabs(true);
			}
		}

        public override void Update()
        {
            base.Update();
            if (mNeedResizeTabs)
                ResizeTabs(false);
        }

        public override void Dock(DockingFrame frame, DockedWidget refWidget, DockingFrame.WidgetAlign align)
        {
            if (this == refWidget)
                return;

            if ((refWidget != null) && (refWidget.mWidgetWindow != mWidgetWindow))
                refWidget.mWidgetWindow.SetForeground();

            if (align == DockingFrame.WidgetAlign.Inside)
            {
                TabbedView tabbedView = (TabbedView)refWidget;
                while (mTabs.Count > 0)
                {
                    TabButton tab = mTabs[0];
                    RemoveTab(tab, false);
                    tabbedView.AddTab(tab, tabbedView.GetInsertPositionFromCursor());
                    tab.Activate();
                }                
                mParentDockingFrame.RemoveDockedWidget(this);

				Closed();
				BFApp.sApp.DeferDelete(this);
            }
            else
            {
                if (frame.mWidgetWindow != mWidgetWindow)
                    mAllowInterpolate = false;

                if (mWidth != 0)
                    SetRequestedSize(mWidth, mHeight);
                frame.StartContentInterpolate();
                if (mParentDockingFrame != null)
                {
                    mParentDockingFrame.StartContentInterpolate();
                    mParentDockingFrame.RemoveDockedWidget(this);
                }                
                frame.AddDockedWidget(this, refWidget, align);
            }                            
        }

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);

            ResizeTabs(true);

            TabButton tab = GetActiveTab();
            if (tab != null)
                tab.ResizeContent();
        }
    }
}
