using System;
using System.Collections;
using System.Diagnostics;
using System.Text;
using Beefy.gfx;
using Beefy.events;
using Beefy.sys;
using Beefy.geom;

namespace Beefy.widgets
{
    public delegate void MouseLeftWindowHandler(BFWindow window);
	public delegate void WindowGotFocusHandler(BFWindow window);
    public delegate void WindowLostFocusHandler(BFWindow window, BFWindow newFocus);
    public delegate bool WindowCloseQueryHandler(BFWindow window);
    public delegate void WindowClosedHandler(BFWindow window);
    public delegate void WindowMovedHandler(BFWindow window);
    public delegate void MouseWheelHandler(MouseEvent mouseEvent);    
    public delegate void KeyDownHandler(KeyDownEvent keyboardEvent);
	//public delegate void CloseTemporaryHandler(WidgetWindow window);
	public delegate void DragDropFileHandler(StringView filePath);
    
    public class WidgetWindow : BFWindow
    {
        public Event<MouseLeftWindowHandler> mOnMouseLeftWindow ~ _.Dispose();
        public Event<WindowGotFocusHandler> mOnWindowGotFocus ~ _.Dispose();
		public Event<WindowLostFocusHandler> mOnWindowLostFocus ~ _.Dispose();
        public Event<MouseEventHandler> mOnMouseDown ~ _.Dispose();
		public Event<MouseEventHandler> mOnMouseUp ~ _.Dispose();
        public Event<WindowCloseQueryHandler> mOnWindowCloseQuery ~ _.Dispose();
        public Event<WindowClosedHandler> mOnWindowClosed ~ _.Dispose();
        public Event<WindowMovedHandler> mOnWindowMoved ~ _.Dispose();
        public Event<MouseWheelHandler> mOnMouseWheel ~ _.Dispose();
        public Event<MenuItemSelectedHandler> mOnMenuItemSelected ~ _.Dispose();
        public Event<KeyDownHandler> mOnWindowKeyDown ~ _.Dispose();
    	public Event<delegate HitTestResult(int32, int32)> mOnHitTest ~ _.Dispose();
		public Event<DragDropFileHandler> mOnDragDropFile ~ _.Dispose();

        public static Event<MouseLeftWindowHandler> sOnMouseLeftWindow ~ _.Dispose();
        public static Event<WindowLostFocusHandler> sOnWindowLostFocus ~ _.Dispose();
        public static Event<MouseEventHandler> sOnMouseDown ~ _.Dispose();
        public static Event<WindowClosedHandler> sOnWindowClosed ~ _.Dispose();
        public static Event<WindowMovedHandler> sOnWindowMoved ~ _.Dispose();
        public static Event<MouseWheelHandler> sOnMouseWheel ~ _.Dispose();
        public static Event<MenuItemSelectedHandler> sOnMenuItemSelected ~ _.Dispose();
        public static Event<KeyDownHandler> sOnKeyDown ~ _.Dispose();
		//public Event<CloseTemporaryHandler> sOnCloseTemporaries ~ _.Dispose();

        public bool mIsMainWindow;

        public MouseFlag mMouseFlags;
		public KeyFlags mMouseDownKeyFlags;
        public Widget mRootWidget;
        public Widget mCaptureWidget;
        public Widget mOverWidget;
        public Widget mFocusWidget;
        public int32 mClientMouseX;
        public int32 mClientMouseY;
        public float mMouseX;
        public float mMouseY;
		public float mMouseDownX;
		public float mMouseDownY;
        public bool mIsMouseMoving;
        public bool mHasMouseInside;
        public bool mHasProxyMouseInside;
        public bool mIsKeyDownHandled;
		public bool mWantsUpdateF;
		public bool mTempWantsUpdateF;

        public int32 mContentClientWidth;
        public int32 mContentClientHeight;

        public static WidgetWindow sMouseInsideWindow;
		public static int32 sLastClosedTick = -1;

        public Matrix mScaleMatrix = Matrix.IdentityMatrix;
        public Matrix mInvScaleMatrix = Matrix.IdentityMatrix;

        public bool[] mIsKeyDown = new bool[(int32)KeyCode.COUNT] ~ delete _;

        public this(BFWindow parent, String title, int x, int y, int width, int height, BFWindow.Flags windowFlags, Widget rootWidget) : 
			//TODO: Do BFWindow init outside constructor.. see where it be hanging
			base(parent, title, x, y, width, height, windowFlags)
        {
            if (mWindowFlags.HasFlag(Flags.ClientSized))
            {
                mContentClientWidth = (int32)width;
                mContentClientHeight = (int32)height;
            }

            if (!mWindowFlags.HasFlag(BFWindowBase.Flags.NoActivate))
                GotFocus();

            SetRootWidget(rootWidget);            

			//Debug.WriteLine("WidgetWindow.this {0} {1} {2}", this, mTitle, mRootWidget);

			//Debug.WriteLine("WidgetWindow this");
        }

		public ~this()
		{
			if (sMouseInsideWindow == this)
				sMouseInsideWindow = null;
			delete mRootWidget;

			//Debug.WriteLine("WidgetWindow ~this");
		}

        public void SetRootWidget(Widget rootWidget)
        {
			if (mRootWidget != null)
				mRootWidget.[Friend]RemovedFromWindow();

            mRootWidget = rootWidget;
			if (mRootWidget != null)
			{
	            mRootWidget.mWidgetWindow = this;
	            mRootWidget.InitChildren();
	            mRootWidget.AddedToParent();
				RehupSize();
			}
        }

        public KeyFlags GetKeyFlags()
        {
            KeyFlags keyFlags = default;
            if (IsKeyDown(KeyCode.Shift))
                keyFlags |= KeyFlags.Shift;
            if ((IsKeyDown(KeyCode.Control)) && (!IsKeyDown(KeyCode.RAlt)))
                keyFlags |= KeyFlags.Ctrl;
            if (IsKeyDown(KeyCode.Menu))
                keyFlags |= KeyFlags.Alt;
            return keyFlags;
        }

        public bool IsKeyDown(KeyCode keyCode)
        {
            return mIsKeyDown[(int32)keyCode];
        }

        public override void Draw(Graphics g)
        {   
         	if (mRootWidget == null)
				 return;
			
            base.Draw(g);			
            mRootWidget.DrawAll(g);            
        }

        public override void Update()
        {
			if (mOverWidget != null)
				Debug.Assert(mHasMouseInside);

			if (mRootWidget == null)
				return;
            base.Update();
            RehupMouse(false);
			mTempWantsUpdateF = false;
            mRootWidget.UpdateAll();
        }

		public override void UpdateF(float updatePct)
		{
			if (mRootWidget == null)
				return;
			base.Update();
			if (mWantsUpdateF || mTempWantsUpdateF)
				mRootWidget.UpdateFAll(updatePct);
		}
        
        public override int32 CloseQuery()
        {
			bool hadFalse = false;
			for (var handler in mOnWindowCloseQuery)
			{
				if (!handler(this))
				{
					hadFalse = true;
					break;
				}
			}
			if (hadFalse)
				return 0;

            return base.CloseQuery();
        }

        public override void Closed()
        {
			sLastClosedTick = BFApp.sApp.mUpdateCnt;

            if (!mHasClosed)
            {
				//Debug.WriteLine("WidgetWindow.Closed {0} {1} {2}", this, mTitle, mRootWidget);

				if (mRootWidget != null)
				{
					mRootWidget.[Friend]RemovedFromWindow();
					mRootWidget.RemovedFromParent(null, this);
				}

				//Debug.WriteLine("Window closing: {0}", this);
                mOnWindowClosed(this);
                sOnWindowClosed(this);
                base.Closed();                        
            }            
        }

        public override void PreDraw(Graphics g)
        {
            base.PreDraw(g);

            g.mMatrix.Set(mScaleMatrix);
			g.mMatrixStack[g.mMatrixStackIdx] = g.mMatrix;
        }

		public override void RehupSize()
		{
			base.RehupSize();

			if (mWindowFlags.HasFlag(Flags.ScaleContent))
			{
			    float scaleX = mClientWidth / (float)mContentClientWidth;
			    float scaleY = mClientHeight / (float)mContentClientHeight;

			    if (scaleX > scaleY)
			    {
			        float scale = scaleY;
			        mScaleMatrix.a = scale;
			        mScaleMatrix.b = 0;
			        mScaleMatrix.c = 0;
			        mScaleMatrix.d = scale;
			        mScaleMatrix.tx = (int32)(mClientWidth - (scale * mContentClientWidth)) / 2;
			        mScaleMatrix.ty = 0;
			    }
			    else
			    {
			        float scale = scaleX;
			        mScaleMatrix.a = scale;
			        mScaleMatrix.b = 0;
			        mScaleMatrix.c = 0;
			        mScaleMatrix.d = scale;
			        mScaleMatrix.tx = 0;
			        mScaleMatrix.ty = (int32)(mClientHeight - (scale * mContentClientHeight)) / 2;
			    }

			    mInvScaleMatrix.Set(mScaleMatrix);
			    mInvScaleMatrix.Invert();
			    mRootWidget.Resize(0, 0, mContentClientWidth, mContentClientHeight);
			}
			else
			{
			    mInvScaleMatrix = Matrix.IdentityMatrix;
			    mScaleMatrix = Matrix.IdentityMatrix;
			    mContentClientWidth = mClientWidth;
			    mContentClientHeight = mClientHeight;
			    mRootWidget.Resize(0, 0, mClientWidth, mClientHeight);
			}
		}

        public override void Moved()
        {
            base.Moved();

            mOnWindowMoved(this);
            sOnWindowMoved(this);
        }

        public void SetFocus(Widget widget)
        {
            /*if (widget != null)
                Debug.WriteLine("SetFocus: {0} {1}", widget.GetType(), widget.mWidgetWindow.mTitle);*/

            if (mFocusWidget == widget)
                return;

            Widget oldFocusWidget = mFocusWidget;
            mFocusWidget = null;
            if (oldFocusWidget != null)
			{
				Debug.Assert(oldFocusWidget.mWidgetWindow == this);
				if (oldFocusWidget.mHasFocus)
                	oldFocusWidget.LostFocus();
				else
					Debug.Assert(!mHasFocus);
			}            
			if (mFocusWidget != null)
				return; // Already got a new focus after LostFocus callbacks
            mFocusWidget = widget;
            if ((mFocusWidget != null) && (mHasFocus))
                mFocusWidget.GotFocus();
        }
        
        public override void GotFocus()
        {
			if (mHasFocus)
				return;
			mIsDirty = true;

			//Debug.WriteLine("GotFocus {0}", mIsMainWindow);

			for (var window in sWindowDictionary.Values)
			{
			    // Don't let more than one window think it has the focus
			    var widgetWindow = window as WidgetWindow;
			    if ((widgetWindow != null) && (widgetWindow != this) && (widgetWindow.mHasFocus))
				{
			        widgetWindow.LostFocus(this);
					break;
				}
			}

            //Debug.WriteLine("GotFocus {0} {1}", mTitle, mIsMainWindow);
            base.GotFocus();                        
            
            if (mFocusWidget != null)
			{
				//Debug.Assert(!mFocusWidget.mHasFocus);
				if (!mFocusWidget.mHasFocus)
	                mFocusWidget.GotFocus();
			}

			mOnWindowGotFocus(this);
        }

        public override void LostFocus(BFWindow newFocus)
        {
			//Debug.WriteLine("LostFocus {0}", mIsMainWindow);

			if (!mHasFocus)
				return;


			mIsDirty = true;
            for (int32 i = 0; i < (int32)mIsKeyDown.Count; i++)
                if (mIsKeyDown[i])
					KeyUp(i);

            // We could have a mousedown in one window while another window has focus still���
            //  So this check isn't valid.
            /*for (int btnIdx = 0; btnIdx < 3; btnIdx++)
            {
                if (((int)mMouseFlags & (1 << btnIdx)) != 0)
                {
                    MouseUp(mClientMouseX, mClientMouseY, btnIdx);
                }
            }*/

            base.LostFocus(newFocus);            

            if (mCaptureWidget != null)
                mCaptureWidget = null;
            if (mFocusWidget != null)
			{
				Debug.Assert(mFocusWidget.mWidgetWindow == this);
				var prevFocus = mFocusWidget;				
                prevFocus.LostFocus();
			}

            mOnWindowLostFocus(this, newFocus);
            sOnWindowLostFocus(this, newFocus);

            //Debug.Assert(!mHasFocus);
        }

        public override void KeyChar(char32 c)
        {
			//Debug.WriteLine($"KeyChar {c}");

			var fakeFocusWindow = GetFakeFocusWindow();
			if (fakeFocusWindow != null)
			{
				fakeFocusWindow.KeyChar(c);
				return;
			}

			mIsDirty = true;

            if ((mHasFocus) && (mFocusWidget != null) && (!mIsKeyDownHandled))
            {
				let keyEvent = scope KeyCharEvent();
				keyEvent.mChar = c;
				keyEvent.mSender = mFocusWidget;
				mFocusWidget.KeyChar(keyEvent);
			}
        }

		BFWindow GetFakeFocusWindow()
		{
			/*for (int windowIdx = BFApp.sApp.mWindows.Count - 1; windowIdx >= 0; windowIdx--)
			{
				var checkWindow = BFApp.sApp.mWindows[windowIdx];
				if (checkWindow == this)
					return null;
				if ((checkWindow.mWindowFlags.HasFlag(.FakeFocus)) && (checkWindow.mHasFocus))
					return checkWindow;
			}*/
			return null;
		}

        public override bool KeyDown(int32 keyCode, int32 isRepeat)
        {
			var fakeFocusWindow = GetFakeFocusWindow();
			if (fakeFocusWindow != null)
				return fakeFocusWindow.KeyDown(keyCode, isRepeat);

			mIsDirty = true;

            mIsKeyDownHandled = false;
			if (keyCode < mIsKeyDown.Count)
            	mIsKeyDown[keyCode] = true;

			KeyDownEvent e = scope KeyDownEvent();
			e.mSender = this;
			e.mKeyFlags = GetKeyFlags();
			e.mKeyCode = (KeyCode)keyCode;
			e.mIsRepeat = isRepeat != 0;

            if ((sOnKeyDown.HasListeners) || (mOnWindowKeyDown.HasListeners))
            {
                if (sOnKeyDown.HasListeners)
                    sOnKeyDown(e);
                if (mOnWindowKeyDown.HasListeners)
                    mOnWindowKeyDown(e);
            }

            if ((mFocusWidget != null) && (!e.mHandled))
            {
				e.mSender = mFocusWidget;
				mFocusWidget.KeyDown(e);
			}
			if (e.mHandled)
				mIsKeyDownHandled = true;

            return e.mHandled;
        }

        public override void KeyUp(int32 keyCode)
        {
			mIsDirty = true;

			if (keyCode < mIsKeyDown.Count)
            	mIsKeyDown[keyCode] = false;

            if (mFocusWidget != null)
                mFocusWidget.KeyUp((KeyCode)keyCode);

			var fakeFocusWindow = GetFakeFocusWindow();
			if (fakeFocusWindow != null)
				fakeFocusWindow.KeyUp(keyCode);
        }

        public override HitTestResult HitTest(int32 x, int32 y)
        {
            if (mOnHitTest.HasListeners)
            {
                var result = mOnHitTest(x, y);
                if (result != HitTestResult.NotHandled)
				{
					if ((result == .Transparent) || (result == .Caption))
					{
						if (mHasMouseInside)
							MouseLeave();
					}

                    return result;
				}
            }

            if (mWindowFlags.HasFlag(Flags.Resizable))
            {
                if ((x - mX > mWindowWidth - 24) && (y - mY > mWindowHeight - 24))
                    return HitTestResult.BottomRight;
            }

            return HitTestResult.NotHandled;
        }

        public void RehupMouse(bool force)
        {
            if (!mHasMouseInside)
                return;
            
            Widget aWidget = mRootWidget.FindWidgetByCoords(mMouseX, mMouseY);
            if (mCaptureWidget != null)
            {
                bool didSomething = false;

                if (mCaptureWidget == aWidget)
                {
                    if (!mCaptureWidget.mMouseOver)
                    {
                        mCaptureWidget.MouseEnter();
                        didSomething = true;
                    }
                    mOverWidget = aWidget;
                }
                else
                {
                    if (mCaptureWidget.mMouseOver)
                    {
                        mCaptureWidget.MouseLeave();
                        didSomething = true;
                    }
                    mOverWidget = null;
                }

                if ((didSomething) || (force))
                {
                    float childX;
                    float childY;
                    mCaptureWidget.RootToSelfTranslate(mMouseX, mMouseY, out childX, out childY);
                    mCaptureWidget.MouseMove(childX, childY);
                }
            }
            else
            {
                if (mOverWidget != aWidget)
                {
                    if (mOverWidget != null)
                    {                        
                        mOverWidget.MouseLeave();
						if (!IsMouseCaptured())
						{
							// Clear out mousedowns if we don't have capture anymore
							for (int32 btn < 4)
							{
								let checkFlag = (MouseFlag)(1 << btn);
								if (mMouseFlags.HasFlag(checkFlag))
								{
									mMouseFlags &= ~checkFlag;
								    float childX;
								    float childY;
								    mOverWidget.RootToSelfTranslate(mMouseX, mMouseY, out childX, out childY);
								    mOverWidget.MouseUp(childX, childY, btn);
									
								}
							}
						}
                    }

                    mOverWidget = aWidget;
                    if (aWidget != null)
					{
	                    aWidget.MouseEnter();
					}

					//Debug.WriteLine("Set OverWidget {0} in {1}", mOverWidget, this);
                }
                else if (!force)
                    return;                     

                if (aWidget != null)
                {
                    float childX;
                    float childY;
                    aWidget.RootToSelfTranslate(mMouseX, mMouseY, out childX, out childY);
                    aWidget.MouseMove(childX, childY);
                }
            }      
        }

        void CheckOtherMouseCaptures()
        {
            if (mCaptureWidget == null)
            {
                /*foreach (WidgetWindow window in sWindowDictionary.Values)
                {
                    if (window.mCaptureWidget != null)
                    {
                        //int a = 0;
                    }
                }*/
            }
        }

        void SetMouseInside()
        {
			if (sMouseInsideWindow == this)
			{
				//Debug.WriteLine("Mouse already inside");
				return;
			}

			//Debug.WriteLine("SetMouseInside 1 {0} prev={1}", this, sMouseInsideWindow);
			var prevMouseInsideWindow = sMouseInsideWindow;
			sMouseInsideWindow = null;
            if (prevMouseInsideWindow != null)
                prevMouseInsideWindow.MouseLeave();
			if (sMouseInsideWindow != null)
			{
				//Debug.WriteLine("SetMouseInside {0} aborted", this);
				return;
			}
            sMouseInsideWindow = this;
            mHasMouseInside = true;

			//Debug.WriteLine("SetMouseInside 2 {0}", this);
        }

        public override void MouseMove(int32 inX, int32 inY)
        {
			//Debug.WriteLine("MouseMove {0} {1},{2}", this, inX, inY);

            mClientMouseX = inX;
            mClientMouseY = inY;

            float x;
            float y;
            TranslateMouseCoords(inX, inY, out x, out y);

            SetMouseInside();            
            CheckOtherMouseCaptures();

            if ((mMouseX == x) && (mMouseY == y))
            {
                RehupMouse(false);
                return;
            }

            mIsMouseMoving = true;
            mMouseX = x;
            mMouseY = y;
            RehupMouse(true);
            mIsMouseMoving = false;
        }

        public override void MouseProxyMove(int32 x, int32 y)
        {
            // This is just from dragging from another so we don't acknowledge the moves directly
            //SetMouseInside();
            mHasProxyMouseInside = true;
            mMouseX = x;
            mMouseY = y;

            CheckOtherMouseCaptures();
        }

        void TranslateMouseCoords(int32 x, int32 y, out float outX, out float outY)
        {
            var pt = mInvScaleMatrix.Multiply(Point(x, y));
            outX = pt.x;
            outY = pt.y;
        }

        public override void MouseDown(int32 inX, int32 inY, int32 btn, int32 btnCount)
        {
			let oldFlags = mMouseFlags;

			if (mMouseFlags == 0)
				mMouseDownKeyFlags = GetKeyFlags();

            mMouseFlags |= (MouseFlag)(1 << btn);
            if ((!mHasFocus) && (mParent == null))
            {
                //INVESTIGATE: This shouldn't be required normally
                //  Fixes some focus issues while debugging?
                GotFocus(); 
            }
            
            float x;
            float y;
            TranslateMouseCoords(inX, inY, out x, out y);

            MouseMove(inX, inY);

			if (oldFlags == default)
			{
				mMouseDownX = mMouseX;
				mMouseDownY = mMouseY;
			}

            if ((mOnMouseDown.HasListeners) || (sOnMouseDown.HasListeners))
            {
                MouseEvent anEvent = scope MouseEvent();
                anEvent.mSender = this;
                anEvent.mX = x;
                anEvent.mY = y;
				anEvent.mBtn = btn;
				anEvent.mBtnCount = btnCount;
                mOnMouseDown(anEvent);
                sOnMouseDown(anEvent);
                if (anEvent.mHandled)
                    return;
            }

			if (btn >= 3) // X button - don't pass on to widgets
				return;

            Widget aWidget = mCaptureWidget ?? mOverWidget;
            if (aWidget != null)
            {
                Debug.Assert(aWidget.mWidgetWindow == this);
                float childX;
                float childY;
                aWidget.RootToSelfTranslate(mMouseX, mMouseY, out childX, out childY);
                aWidget.MouseDown(childX, childY, btn, btnCount);
            }            
        }

        public override void MouseUp(int32 inX, int32 inY, int32 btn)
        {
			//Debug.WriteLine("MouseUp {0} {1} {2}", mIsMainWindow, this, mMouseFlags);

            if (((int32)mMouseFlags & (1 << btn)) == 0)
                return;

			MouseMove(inX, inY);
            mMouseFlags &= (MouseFlag)(~(1 << btn));

			float x;
			float y;
			TranslateMouseCoords(inX, inY, out x, out y);

			if (mOnMouseUp.HasListeners)
			{
			    MouseEvent anEvent = scope MouseEvent();
			    anEvent.mSender = this;
			    anEvent.mX = x;
			    anEvent.mY = y;
				anEvent.mBtn = btn;
			    mOnMouseUp(anEvent);
			    if (anEvent.mHandled)
			        return;
			}

			if (btn >= 3) // X button - don't pass on to widgets
				return;

            Widget aWidget = mCaptureWidget ?? mOverWidget;
            if (aWidget != null)
            {
                float origX;
                float origY;
                aWidget.RootToSelfTranslate(mMouseDownX, mMouseDownY, out origX, out origY);

				float childX;
				float childY;
				aWidget.RootToSelfTranslate(mMouseX, mMouseY, out childX, out childY);
				aWidget.MouseUp(childX, childY, btn);

				if (aWidget.mMouseOver)
				{
					aWidget.MouseClicked(childX, childY, origX, origY, btn);
				}
            }

			if (mMouseFlags == 0)
				mMouseDownKeyFlags = 0;
        }

		public void ReleaseMouseCaptures()
		{
			if (mCaptureWidget == null)
				return;
			for (int32 btn < 3)
				MouseUp((int32)mMouseX, (int32)mMouseY, btn);
			mCaptureWidget = null;
		}

        public override void MouseWheel(int32 inX, int32 inY, float deltaX, float deltaY)
        {
            float x;
            float y;
            TranslateMouseCoords(inX, inY, out x, out y);

            MouseMove(inX, inY);

            if (sOnMouseWheel.HasListeners)
            {
                MouseEvent anEvent = scope MouseEvent();
                anEvent.mX = x;
                anEvent.mY = y;
				anEvent.mWheelDeltaX = deltaX;
                anEvent.mWheelDeltaY = deltaY;
                anEvent.mSender = this;
                sOnMouseWheel(anEvent);

                if (anEvent.mHandled)
                    return;
            }

            if (mOnMouseWheel.HasListeners)
            {
                MouseEvent anEvent = scope MouseEvent();
                anEvent.mX = x;
                anEvent.mY = y;
                anEvent.mWheelDeltaX = deltaX;
				anEvent.mWheelDeltaY = deltaY;
                anEvent.mSender = this;
                mOnMouseWheel(anEvent);

                if (anEvent.mHandled)
                    return;
            }

            Widget aWidget = mOverWidget;
            if (aWidget != null)
            {
                float childX;
                float childY;
                aWidget.RootToSelfTranslate(mMouseX, mMouseY, out childX, out childY);
                aWidget.MouseWheel(childX, childY, deltaX, deltaY);
            }            
        }

        public override void MouseLeave()
        {
			if (sMouseInsideWindow == this)
			{
				sMouseInsideWindow = null;
			}

            mHasProxyMouseInside = false;
            mHasMouseInside = false;
            if (mOverWidget != null)
            {
                mOverWidget.MouseLeave();
                mOverWidget = null;
            }

			mIsDirty = true;
            mOnMouseLeftWindow(this);
            sOnMouseLeftWindow(this);
        }

        public override void MenuItemSelected(SysMenu sysMenu)
        {
            mOnMenuItemSelected(sysMenu);
            sOnMenuItemSelected(sysMenu);
            base.MenuItemSelected(sysMenu);            
        }

		public override void DragDropFile(StringView filePath)
		{
			mOnDragDropFile(filePath);
			base.DragDropFile(filePath);
		}
		
		public void TransferMouse(WidgetWindow newMouseWindow)
		{
			if (mWindowFlags.HasFlag(.FakeFocus))
				return;

			if (mMouseFlags == default)
				return;

			newMouseWindow.mMouseFlags = mMouseFlags;
			newMouseWindow.mMouseDownKeyFlags = mMouseDownKeyFlags;
			newMouseWindow.CaptureMouse();
			mMouseFlags = default;
		}
		
		public void SetContentSize(int width, int height)
		{
			mContentClientWidth = (.)width;
			mContentClientHeight = (.)height;
			RehupSize();
		}
    }
}
