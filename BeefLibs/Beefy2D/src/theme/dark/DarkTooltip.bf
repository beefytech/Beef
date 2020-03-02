using System;
using System.Collections.Generic;
using System.Text;
using System.Threading.Tasks;
using Beefy;
using Beefy.events;
using Beefy.gfx;
using Beefy.theme.dark;
using Beefy.widgets;
using System.Diagnostics;
using Beefy.geom;
using Beefy.utils;

namespace Beefy.theme.dark
{
    public class DarkTooltipContainer : Widget
    {
        public DarkTooltip mTooltip;

        public override void Resize(float x, float y, float width, float height)
        {
            base.Resize(x, y, width, height);
            mTooltip.Resize(0, GS!(4), width - GS!(DarkTooltip.cShadowSize), height - GS!(DarkTooltip.cShadowSize) - GS!(4));
        }
    }

    public class DarkTooltip : Widget
    {
        public Event<Action> mCloseEvent ~ _.Dispose();
        public Widget mRelWidget;
        public Font mFont;
        public String mText ~ delete _;
        public bool mAllowResize;
		public bool mHasClosed;
		public Insets mRelWidgetMouseInsets ~ delete _;
		public bool mAllowMouseInsideSelf;
		public bool mAllowMouseOutside;
		public int mAutoCloseDelay;

        public const float cShadowSize = 8;

        public this(String text, Widget relWidget, float x, float y, float minWidth, float minHeight, bool allowResize, bool mouseVisible)
        {
            DarkTooltipContainer container = new DarkTooltipContainer();
            container.mTooltip = this;
            container.AddWidget(this);

            Attach(relWidget);
            mFont = DarkTheme.sDarkTheme.mSmallFont;
            mText = new String(text);
            mAllowResize = allowResize;

            FontMetrics fontMetrics = .();
            float height = mFont.Draw(null, mText, x, y, 0, 0, FontOverflowMode.Overflow, &fontMetrics);
            mWidth = Math.Max(minWidth, fontMetrics.mMaxWidth + GS!(32));
            mHeight = Math.Max(minHeight, height + GS!(16));
            
            float screenX;
            float screenY;
            relWidget.SelfToRootTranslate(x, y, out screenX, out screenY);
            screenX += relWidget.mWidgetWindow.mClientX;
            screenY += relWidget.mWidgetWindow.mClientY;
            //screenX -= 2;
            //screenY += 14;

            BFWindow.Flags windowFlags = BFWindow.Flags.ClientSized | BFWindow.Flags.PopupPosition | BFWindow.Flags.NoActivate | BFWindow.Flags.DestAlpha;                                        
            WidgetWindow widgetWindow = new WidgetWindow(relWidget.mWidgetWindow,
                "Tooltip",
                (int32)(screenX), (int32)(screenY),
                (int32)(mWidth +GS!(cShadowSize)), (int32)(mHeight + GS!(cShadowSize)),
                windowFlags,
                container);
            widgetWindow.SetMinimumSize((int32)widgetWindow.mWindowWidth, (int32)widgetWindow.mWindowHeight);
            if (!mouseVisible)
                widgetWindow.SetMouseVisible(mouseVisible);

            if (allowResize)
                widgetWindow.mOnHitTest.Add(new => HitTest);
            WidgetWindow.sOnMouseDown.Add(new => HandleMouseDown);
            WidgetWindow.sOnMouseWheel.Add(new => HandleMouseWheel);
            WidgetWindow.sOnMenuItemSelected.Add(new => HandleSysMenuItemSelected);
            WidgetWindow.sOnKeyDown.Add(new => HandleKeyDown);
        }

		public ~this()
		{
			Debug.Assert(mHasClosed);
			Detach();
		}

		void Attach(Widget widget)
		{
			if (mRelWidget != null)
				Detach();

			mRelWidget = widget;
			if (mRelWidget != null)
				mRelWidget.mOnRemovedFromParent.Add(new => WidgetRemoved);
		}

		void Detach()
		{
			if (mRelWidget != null)
			{
				mRelWidget.mOnRemovedFromParent.Remove(scope => WidgetRemoved, true);
				mRelWidget = null;
			}
		}

		void WidgetRemoved(Widget widget, Widget prevParent, WidgetWindow widgetWindow)
		{
			Detach();
			Close();
		}

		public void Reinit(String text, Widget relWidget, float x, float y, float minWidth, float minHeight, bool allowResize, bool mouseVisible)
		{
			mRelWidget = relWidget;
			mFont = DarkTheme.sDarkTheme.mSmallFont;
			mText.Set(text);
			mAllowResize = allowResize;

			FontMetrics fontMetrics = .();
			float height = mFont.Draw(null, mText, x, y, 0, 0, FontOverflowMode.Overflow, &fontMetrics);
			mWidth = Math.Max(minWidth, fontMetrics.mMaxWidth + GS!(32));
			mHeight = Math.Max(minHeight, height + GS!(16));

			float screenX;
			float screenY;
			relWidget.SelfToRootTranslate(x, y, out screenX, out screenY);
			screenX += relWidget.mWidgetWindow.mClientX;
			screenY += relWidget.mWidgetWindow.mClientY;

			mWidgetWindow.Resize((int32)(screenX), (int32)(screenY),
                (int32)(mWidth + GS!(cShadowSize)), (int32)(mHeight + GS!(cShadowSize)));
		}

        void HandleKeyDown(KeyDownEvent keyboardEvent)
        {
			mOnKeyDown(keyboardEvent);

            if (keyboardEvent.mHandled)
                return;

            if (keyboardEvent.mKeyCode == KeyCode.Escape)
            {
                Close();
                keyboardEvent.mHandled = true;
            }
        }

        BFWindow.HitTestResult HitTest(int32 x, int32 y)
        {
            int32 relX = x - mWidgetWindow.mX;
            int32 relY = y - mWidgetWindow.mY;

            if ((relX >= mWidgetWindow.mWindowWidth - GS!(18)) && (relY >= mWidgetWindow.mWindowHeight - GS!(18)))
                return BFWindowBase.HitTestResult.BottomRight;
            return BFWindowBase.HitTestResult.Client;
        }

        public override void Draw(Graphics g)
        {
            base.Draw(g);

            using (g.PushColor(0x80000000))
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.DropShadow), 0, 0, mWidth + cShadowSize, mHeight + cShadowSize);

            using (g.PushColor(0xFFFFFFFF))
                g.DrawBox(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.Menu), 0, 0, mWidth, mHeight);

            g.SetFont(mFont);
            g.DrawString(mText, 0, GS!(5), FontAlign.Centered, mWidth);

            if (mAllowResize)
                g.Draw(DarkTheme.sDarkTheme.GetImage(DarkTheme.ImageIdx.ResizeGrabber), mWidth - GS!(22), mHeight - GS!(22));
        }
        
		public override void ParentDeleted()
		{
			Close();
			base.ParentDeleted();
		}

        public void Close()
        {   
			if (mHasClosed)
				return;
			mHasClosed = true;

            if (mWidgetWindow != null)
            {
                mWidgetWindow.Close();
            }
            mCloseEvent();
        }

        void HandleMouseWheel(MouseEvent evt)
        {            
            WidgetWindow widgetWindow = (WidgetWindow)evt.mSender;
            if (widgetWindow == mWidgetWindow)
                return;
			if (widgetWindow.HasParent(mWidgetWindow))
				return;
            Close();
        }

        void HandleMouseDown(MouseEvent evt)
        {
            WidgetWindow widgetWindow = (WidgetWindow)evt.mSender;
            if (widgetWindow == mWidgetWindow)
                return;
			if (widgetWindow.HasParent(mWidgetWindow))
				return;
            //if ((!(widgetWindow.mRootWidget is HoverWatch)) && (!(widgetWindow.mRootWidget is MenuWidget)))
            Close();
        }

        void HandleSysMenuItemSelected(IMenu sysMenu)
        {
            Close();
        }

        public override void Update()
        {
            base.Update();

			if (mAutoCloseDelay > 0)
			{
				mAutoCloseDelay--;
				return;
			}

            if (mWidgetWindow == null)
                return;

			if (mAllowMouseOutside)
				return;

			float rootX;
			float rootY;
			mRelWidget.SelfToRootTranslate(0, 0, out rootX, out rootY);

			Rect checkRect = Rect(rootX, rootY, mRelWidget.mWidth, mRelWidget.mHeight);
			mRelWidgetMouseInsets?.ApplyTo(ref checkRect);
			if ((mRelWidget.mWidgetWindow != null) && (mRelWidget.mWidgetWindow.mHasMouseInside))
			{
				//checkRect.Inflate(8, 8);
				if (checkRect.Contains(mRelWidget.mWidgetWindow.mClientMouseX, mRelWidget.mWidgetWindow.mClientMouseY))
					return;
			}

			if ((mWidgetWindow.mHasMouseInside) && (mAllowMouseInsideSelf))
				return;

			var checkWindow = BFApp.sApp.FocusedWindow;
			if ((checkWindow != null) && (checkWindow.HasParent(mWidgetWindow)))
				return;
			
			Close();
        }

		public void ExpandAllowedRegion()
		{

		}

		protected override void RemovedFromWindow()
		{
			base.RemovedFromWindow();

			if (!mHasClosed)
				Close();

			WidgetWindow.sOnMouseDown.Remove(scope => HandleMouseDown, true);
			WidgetWindow.sOnMouseWheel.Remove(scope => HandleMouseWheel, true);
			WidgetWindow.sOnMenuItemSelected.Remove(scope => HandleSysMenuItemSelected, true);
			WidgetWindow.sOnKeyDown.Remove(scope => HandleKeyDown, true);
		}
    }

	static class DarkTooltipManager
	{
		public static DarkTooltip sTooltip;
		public static Widget sLastMouseWidget;
		public static int32 sMouseStillTicks;
		public static Point sLastAbsMousePos;
		public static Point sLastRelMousePos;
		public static bool sWantsRefireMouseOver;

		public static bool IsTooltipShown(Widget relWidget)
		{
		    return (sTooltip != null) && (sTooltip.mRelWidget == relWidget);
		}

		public static DarkTooltip ShowTooltip(String text, Widget relWidget, float x, float y, float minWidth = 0, float minHeight = 0, bool allowResize = false, bool mouseVisible = false)
		{
			scope AutoBeefPerf("DarkTooltipManager.ShowTooltip");

		    if (sTooltip != null)
		    {
		        if (relWidget == sTooltip.mRelWidget)
				{
					sTooltip.Reinit(text, relWidget, x, y, minWidth, minHeight, allowResize, mouseVisible);
		            return null; // Only return the tooltip when a new one has been allocated
				}

		        sTooltip.Close();
		    }

		    sTooltip = new DarkTooltip(text, relWidget, x, y, minWidth, minHeight, allowResize, mouseVisible);
		    sTooltip.mCloseEvent.Add(new () => {sTooltip = null; });
		    return sTooltip;
		}

		public static void CloseTooltip()
		{
		    if (sTooltip != null)
		        sTooltip.Close();
		}

		public static void UpdateTooltip()
		{
		    if (sTooltip == null)
		        return;
		}

		public static bool CheckMouseover(Widget checkWidget, int wantTicks, out Point mousePoint, bool continuous = false)
		{
		    mousePoint = Point(Int32.MinValue, Int32.MinValue);
		    if (checkWidget != sLastMouseWidget) 
		        return false;

			for (var childWindow in checkWidget.mWidgetWindow.mChildWindows)
			{
				var childWidgetWindow = childWindow as WidgetWindow;
				if (childWidgetWindow == null)
					continue;
				if (childWidgetWindow.mRootWidget is MenuContainer)
					return false;
			}

		    checkWidget.RootToSelfTranslate(sLastRelMousePos.x, sLastRelMousePos.y, out mousePoint.x, out mousePoint.y);
			if ((continuous) && (sMouseStillTicks > wantTicks))
				return true;
			if (sWantsRefireMouseOver)
			{
				sWantsRefireMouseOver = false;
				return true;
			}
		    return sMouseStillTicks == wantTicks;
		}

		static void LastMouseWidgetDeleted(Widget widget)
		{
			if (sLastMouseWidget == widget)
				sLastMouseWidget = null;
		}

		public static void RefireMouseOver()
		{
			sWantsRefireMouseOver = true;
		}

		static void SetLastMouseWidget(Widget newWidget)
		{
			if (sLastMouseWidget != null)
				sLastMouseWidget.mOnDeleted.Remove(scope => LastMouseWidgetDeleted, true);
			sLastMouseWidget = newWidget;
			if (sLastMouseWidget != null)
				sLastMouseWidget.mOnDeleted.Add(new => LastMouseWidgetDeleted);
		}

		public static void UpdateMouseover()
		{
			if (sMouseStillTicks != -1)
		    	sMouseStillTicks++;

		    Widget overWidget = null;
		    int32 numOverWidgets = 0;
		    for (var window in BFApp.sApp.mWindows)
		    {
		        var widgetWindow = window as WidgetWindow;
		        
		        widgetWindow.RehupMouse(false);
		        var windowOverWidget = widgetWindow.mCaptureWidget ?? widgetWindow.mOverWidget;
		        if ((windowOverWidget != null) && (widgetWindow.mAlpha == 1.0f) && (widgetWindow.mCaptureWidget == null))
		        {
		            overWidget = windowOverWidget;
		            numOverWidgets++;
		            if (overWidget != sLastMouseWidget)
		            {
						SetLastMouseWidget(overWidget);                        
		                sMouseStillTicks = -1;
		            }

					float actualX = widgetWindow.mClientX + widgetWindow.mMouseX;
					float actualY = widgetWindow.mClientY + widgetWindow.mMouseY;
		            if ((sLastAbsMousePos.x != actualX) || (sLastAbsMousePos.y != actualY))
		            {
		                sMouseStillTicks = 0;
		                sLastAbsMousePos.x = actualX;
		                sLastAbsMousePos.y = actualY;
		            }
					sLastRelMousePos.x = widgetWindow.mMouseX;
					sLastRelMousePos.y = widgetWindow.mMouseY;
		        }
		    }

		    if (overWidget == null)
		    {     
		   		SetLastMouseWidget(null);
		        sMouseStillTicks = -1;
		    }

		    if (numOverWidgets > 1)
		    {
				//int a = 0;
				Debug.WriteLine("numOverWidgets > 1");
		    }


		    //Debug.Assert(numOverWidgets <= 1);                        
		}
	}
}
