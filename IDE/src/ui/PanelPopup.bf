using Beefy.theme.dark;
using Beefy.widgets;
using Beefy.events;
using Beefy;
using System;
using Beefy.geom;
using Beefy.gfx;

namespace IDE.ui
{
	class PanelPopup : Widget
	{
		public Panel mPanel;
		public BFWindowBase.Flags mWindowFlags = .ClientSized | .NoActivate | .NoMouseActivate | .DestAlpha | .PopupPosition;
		Widget mRelativeWidget;
		bool mReverse;
		float mMinContainerWidth = 32;
		float mMaxContainerWidth = 2048;
		Insets mPopupInsets = new .() ~ delete _;
		bool mHasClosed;
		bool mDeferredClose;

		public virtual void CalcSize()
		{

		}

		public virtual float GetReverseAdjust()
		{
			return GS!(10);
		}

		public override void Draw(Graphics g)
		{
			//g.DrawBox(DarkTheme.sDarkTheme.GetImage(.Window), 0, 0, mWidth, mHeight);

			/*using (g.PushColor(0x80FF0000))
				g.FillRect(0, 0, mWidth, mHeight);*/

			using (g.PushColor(0x80000000))
			    g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.DropShadow), GS!(2), 0 + GS!(2), mWidth - GS!(2), mHeight - GS!(2));

			base.Draw(g);
			using (g.PushColor(0xFFFFFFFF))
			    g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Menu), 0, 0, mWidth - GS!(8), mHeight - GS!(8));
		}

		public override void Update()
		{
			base.Update();
			if (mDeferredClose)
				Close();
		}

		public virtual void Init(Panel panel, Widget relativeWidget, float x, float y, float width, float height)
		{
			mPanel = panel;
			float curY = y;
			WidgetWindow curWidgetWindow = null;

			Resize(0, 0, width, height);

			bool reverse = false;
			bool allowReverse = true;
			if ((allowReverse) && (relativeWidget != null))
			{
				CalcSize();

				float minHeight = mHeight;

				float screenX;
				float screenY;
				relativeWidget.SelfToRootTranslate(0, curY, out screenX, out screenY);
				screenY += relativeWidget.mWidgetWindow.mClientY;

				int wsX;
				int wsY;
				int wsWidth;
				int wsHeight;
				BFApp.sApp.GetWorkspaceRect(out wsX, out wsY, out wsWidth, out wsHeight);
				float spaceLeft = (wsY + wsHeight) - (screenY);
				if (spaceLeft < minHeight)
				{
					//curY = curY + GetReverseAdjust() - height;
					curY = curY - height - GetReverseAdjust();
					reverse = true;
				}
			}

		 	mReverse = reverse;

		    float screenX = x;
		    float screenY = curY;

		    CalcSize();            

		    if (relativeWidget != null)
		        relativeWidget.SelfToRootTranslate(x, curY, out screenX, out screenY);
		    screenX += relativeWidget.mWidgetWindow.mClientX;
		    screenY += relativeWidget.mWidgetWindow.mClientY;

	        //float screenWidth;
	        //float screenHeight;
	        //CalcContainerSize(panel, ref screenX, ref screenY, out screenWidth, out screenHeight);

			BFWindowBase.Flags flags = mWindowFlags;
	        curWidgetWindow = new WidgetWindow((relativeWidget != null) ? relativeWidget.mWidgetWindow : null,
	            "Popup",
	            (int32)(screenX), (int32)(screenY),
	            (int32)width, (int32)height,
	            flags,
	            this);
			AddWidget(mPanel);

	        //Resize(0, 0, mWidth, mHeight);
	        //panel.UpdateScrollbars();
	        mRelativeWidget = relativeWidget;                
		    
		    WidgetWindow.sOnKeyDown.Add(new => HandleKeyDown);
		    WidgetWindow.sOnWindowLostFocus.Add(new => WindowLostFocus);
		    WidgetWindow.sOnMouseDown.Add(new => HandleMouseDown);
		    WidgetWindow.sOnMouseWheel.Add(new => HandleMouseWheel);
		    WidgetWindow.sOnWindowMoved.Add(new => HandleWindowMoved);
		    WidgetWindow.sOnMenuItemSelected.Add(new => HandleSysMenuItemSelected);

		    /*if (mRelativeWidget != null)
		        mRelativeWidget.mOnMouseUp.Add(new => HandleMouseUp);*/

		    curWidgetWindow.SetFocus(this);
		}

		public override void Resize(float x, float y, float width, float height)
		{
			base.Resize(x, y, width, height);
			mPanel.Resize(GS!(1), GS!(0), width - GS!(9), height - GS!(8));
		}

		public void RemoveHandlers()
		{
		    WidgetWindow.sOnMouseDown.Remove(scope => HandleMouseDown, true);
		    WidgetWindow.sOnMouseWheel.Remove(scope => HandleMouseWheel, true);
		    WidgetWindow.sOnWindowLostFocus.Remove(scope => WindowLostFocus, true);
		    WidgetWindow.sOnWindowMoved.Remove(scope => HandleWindowMoved, true);
		    WidgetWindow.sOnMenuItemSelected.Remove(scope => HandleSysMenuItemSelected, true);
		    WidgetWindow.sOnKeyDown.Remove(scope => HandleKeyDown, true);
		}

		public void Close()
		{
			if (mHasClosed)
				return;
			mHasClosed = true;

			RemoveHandlers();

			//Debug.Assert(mWidgetWindow != null);

			if (mWidgetWindow != null)
			{
			    mWidgetWindow.Close();            
			    /*if (mMenu.mOnMenuClosed.HasListeners)
			        mMenu.mOnMenuClosed(mMenu, mItemSelected);*/
			}
		}

		void HandleMouseWheel(MouseEvent theEvent)
		{
		    HandleMouseDown(theEvent);
		}

		void HandleMouseDown(MouseEvent theEvent)
		{            
		    WidgetWindow widgetWindow = (WidgetWindow)theEvent.mSender;            
		    if (!(widgetWindow.mRootWidget is PanelPopup))
				mDeferredClose = true;
		        //Close();
		}

		void HandleSysMenuItemSelected(IMenu sysMenu)
		{
		    Close();
		}

		bool IsMenuWindow(BFWindow window)
		{
		    var newWidgetWindow = window as WidgetWindow;
		    if (newWidgetWindow != null)
		    {
		        if (newWidgetWindow.mRootWidget is PanelPopup)
		            return true;
		    }
		    return false;
		}

		void WindowLostFocus(BFWindow window, BFWindow newFocus)
		{
		    if (IsMenuWindow(newFocus))
		        return;

		    if ((mWidgetWindow != null) && (!mWidgetWindow.mHasFocus))
		        Close();
		}

		void HandleKeyDown(KeyDownEvent evt)
		{
		    if (evt.mKeyCode == KeyCode.Escape)
		    {
		        evt.mHandled = true;
		        Close();
		    }
		}

		void HandleWindowMoved(BFWindow window)
		{
		    if (IsMenuWindow(window))
		        return;
		    Close();
		}
	}
}
