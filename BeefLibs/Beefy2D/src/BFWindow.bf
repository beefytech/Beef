using System;
using System.Collections;
using System.Text;
using System.Diagnostics;
using Beefy.gfx;
using Beefy.sys;
using System.IO;

#if MONOTOUCH
using MonoTouch;
#endif

#if STUDIO_CLIENT
using Beefy.ipc;
#endif

namespace Beefy
{
    public class BFWindowBase
    {
        public enum Flags
        {
            Border = 0x000001,
            ThickFrame = 0x000002,
            Resizable = 0x000004,
            SysMenu = 0x000008,
            Caption = 0x000010,
            Minimize = 0x000020,
            Maximize = 0x000040,
            ClientSized = 0x000080,
            QuitOnClose = 0x000100,
            VSync = 0x000200,
            PopupPosition = 0x000400,
            DestAlpha = 0x000800,
            AlphaMask = 0x001000,
            Child = 0x002000,
            TopMost = 0x004000,
            ToolWindow = 0x008000,
            NoActivate = 0x01'0000,
            NoMouseActivate = 0x02'0000,
            Menu = 0x04'0000,
            Modal = 0x08'0000,
            ScaleContent = 0x10'0000,
            UseParentMenu = 0x20'0000,
			CaptureMediaKeys = 0x40'0000,
			Fullscreen = 0x80'0000,
			FakeFocus = 0x0100'0000,
			ShowMinimized = 0x0200'0000,
			ShowMaximized = 0x0400'0000,
			AllowFullscreen = 0x0800'0000,
			AcceptFiles = 0x1000'0000,
			NoShow = 0x2000'0000,
        };

		[AllowDuplicates]
        public enum HitTestResult
        {
            NotHandled = -3,

            Border = 18,
            Bottom = 15,
            BottomLeft = 16,
            BottomRight = 17,
            Caption = 2,
            Client = 1,
            Close = 20,
            Error = -2,
            GrowBox = 4,
            Help = 21,
            HScroll = 6,
            Left = 10,
            Menu = 5,
            MaxButton = 9,
            MinButton = 8,
            NoWhere = 0,
            Reduce = 8,
            Right = 11,
            Size = 4,
            SysMenu = 3,
            Top = 12,
            TopLeft = 13,
            TopRight = 14,
            Transparent = -1,
            VScroll = 7,
            Zoom = 9            
        }

		public enum ShowKind
		{
			Hide,
			Normal,
			Minimized,
			Maximized,
			Show,
			ShowNormal,
			ShowMinimized,
			ShowMaximized
		}

        public SysMenu mSysMenu ~ delete _;
        public Dictionary<int, SysMenu> mSysMenuMap = new Dictionary<int, SysMenu>() ~ delete _;
        public DrawLayer mDefaultDrawLayer ~ delete _;

        public virtual void Draw(Graphics g)
        {
            
        }

        public virtual void PreDraw(Graphics g)
        {
            g.PushDrawLayer(mDefaultDrawLayer);            
        }

        public virtual void PostDraw(Graphics g)
        {
            g.PopDrawLayer();
        }
    }
#if !STUDIO_CLIENT
    public class BFWindow : BFWindowBase, INativeWindow
    {
		public enum ShowKind
		{
			Hide,
			Normal,
			Minimized,
			Maximized,
			Show,
			ShowNormal,
			ShowMinimized,
			ShowMaximized
		}

        delegate void NativeMovedDelegate(void* window);
        delegate int32 NativeCloseQueryDelegate(void* window);
        delegate void NativeClosedDelegate(void* window);
        delegate void NativeGotFocusDelegate(void* window);
        delegate void NativeLostFocusDelegate(void* window);
        delegate void NativeKeyCharDelegate(void* window, char32 theChar);
		delegate bool NativeKeyDownDelegate(void* window, int32 keyCode, int32 isRepeat);
        delegate void NativeKeyUpDelegate(void* window, int32 keyCode);
        delegate int32 NativeHitTestDelegate(void* window, int32 x, int32 y);
        delegate void NativeMouseMoveDelegate(void* window, int32 x, int32 y);
        delegate void NativeMouseProxyMoveDelegate(void* window, int32 x, int32 y);
        delegate void NativeMouseDownDelegate(void* window, int32 x, int32 y, int32 btn, int32 btnCount);
        delegate void NativeMouseUpDelegate(void* window, int32 x, int32 y, int32 btn);
        delegate void NativeMouseWheelDelegate(void* window, int32 x, int32 y, float deltaX, float deltaY);
        delegate void NativeMouseLeaveDelegate(void* window);
        delegate void NativeMenuItemSelectedDelegate(void* window, void* menu);
		delegate void NativeDragDropFileDelegate(void* window, char8* filePath);

        public void* mNativeWindow;
		public bool mNativeWindowClosed;

		static int32 sId;
		protected int32 mId = ++sId;

		public String mName ~ delete _;
        public String mTitle ~ delete _;
        public int32 mX;
        public int32 mY;
        public int32 mWindowWidth;
        public int32 mWindowHeight;
		public int32 mNormX;
		public int32 mNormY;
		public int32 mNormWidth;
		public int32 mNormHeight;
		public bool mVisible = true;
		public ShowKind mShowKind;
        public int32 mClientX;
        public int32 mClientY;
        public int32 mClientWidth;
        public int32 mClientHeight;
        public float mAlpha = 1.0f;
        public Flags mWindowFlags;
        private bool mMouseVisible;
        public bool mHasFocus = false;        
        public bool mHasClosed;
		public bool mIsDirty = true;
        public BFWindow mParent;
        public List<BFWindow> mChildWindows = new List<BFWindow>() ~ delete _;

        static protected Dictionary<int, BFWindow> sWindowDictionary = new Dictionary<int, BFWindow>() ~ delete _;

        static NativeMovedDelegate sNativeMovedDelegate ~ delete _;
        static NativeCloseQueryDelegate sNativeCloseQueryDelegate ~ delete _;
        static NativeClosedDelegate sNativeClosedDelegate ~ delete _;
        static NativeGotFocusDelegate sNativeGotFocusDelegate ~ delete _;
        static NativeLostFocusDelegate sNativeLostFocusDelegate ~ delete _;
        static NativeKeyCharDelegate sNativeKeyCharDelegate ~ delete _;
		static NativeKeyDownDelegate sNativeKeyDownDelegate ~ delete _;
        static NativeKeyUpDelegate sNativeKeyUpDelegate ~ delete _;
        static NativeHitTestDelegate sNativeHitTestDelegate ~ delete _;
        static NativeMouseMoveDelegate sNativeMouseMoveDelegate ~ delete _;
        static NativeMouseProxyMoveDelegate sNativeMouseProxyMoveDelegate ~ delete _;
        static NativeMouseDownDelegate sNativeMouseDownDelegate ~ delete _;
        static NativeMouseUpDelegate sNativeMouseUpDelegate ~ delete _;
        static NativeMouseWheelDelegate sNativeMouseWheelDelegate ~ delete _;
        static NativeMouseLeaveDelegate sNativeMouseLeaveDelegate ~ delete _;
        static NativeMenuItemSelectedDelegate sNativeMenuItemSelectedDelegate ~ delete _;
        static NativeDragDropFileDelegate sNativeDragDropFileDelegate ~ delete _;

        [CallingConvention(.Stdcall), CLink]
        static extern void* BFApp_CreateWindow(void* parent, char8* title, int32 x, int32 y, int32 width, int32 height, int32 windowFlags);

		[CallingConvention(.Stdcall), CLink]
		static extern void* BFWindow_GetNativeUnderlying(void* window);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_SetCallbacks(void* window, void* movedDelegate, void* closeQueryDelegate, void* closedDelegate, 
            void* gotFocusDelegate, void* lostFocusDelegate,
			void* keyCharDelegate, void* keyDownDelegate, void* keyUpDelegate, void* hitTestDelegate,
            void* mouseMoveDelegate, void* mouseProxyMoveDelegate, void* mouseDownDelegate, void* mouseUpDelegate, void* mouseWheelDelegate, void* mouseLeaveDelegate,
            void* menuItemSelectedDelegate, void* dragDropFileDelegate);

		[CallingConvention(.Stdcall), CLink]
		static extern void BFWindow_SetTitle(void* window, char8* title);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_SetMinimumSize(void* window, int32 minWidth, int32 minHeight, bool clientSized);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_GetPosition(void* window, out int32 x, out int32 y, out int32 width, out int32 height, out int32 clientX, out int32 clientY, out int32 clientWidth, out int32 clientHeight);

		[CallingConvention(.Stdcall), CLink]
		static extern void BFWindow_GetPlacement(void* window, out int32 normX, out int32 normY, out int32 normWidth, out int32 normHeight, out int32 showKind);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_Resize(void* window, int32 x, int32 y, int32 width, int32 height, int showKind);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_Close(void* window, int32 force);

		[CallingConvention(.Stdcall), CLink]
		static extern void BFWindow_Show(void* window, ShowKind showKind);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_SetForeground(void* window);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_LostFocus(void* window, void* newFocus);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_SetNonExclusiveMouseCapture(void* window);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_SetClientPosition(void* window, int32 x, int32 y);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_SetAlpha(void* window, float alpha, uint32 destAlphaSrcMask, int32 mouseVisible);

        [CallingConvention(.Stdcall), CLink]
        static extern void BFWindow_SetMouseVisible(void* window, bool mouseVisible);

		[CallingConvention(.Stdcall), CLink]
		static extern void BFWindow_CaptureMouse(void* window);

		[CallingConvention(.Stdcall), CLink]
		static extern bool BFWindow_IsMouseCaptured(void* window);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BFWindow_AddMenuItem(void* window, void* parent, int32 insertIdx, char8* text, char8* hotKey, void* bitmap, int32 enabled, int32 checkState, int32 radioCheck);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BFWindow_ModifyMenuItem(void* window, void* item, char8* text, char8* hotKey, void* bitmap, int32 enabled, int32 checkState, int32 radioCheck);

        [CallingConvention(.Stdcall), CLink]
        static extern void* BFWindow_DeleteMenuItem(void* window, void* item);

		[CallingConvention(.Stdcall), CLink]
		static extern int BFWindow_GetDPI(void* window);

        public static BFWindow GetBFWindow(void* window)
        {
            return sWindowDictionary[(int)window];
        }

		#if MONOTOUCH
		[MonoPInvokeCallback(typeof(NativeMovedDelegate))]
		static void Static_NativeMovedDelegate(void* window) { GetBFWindow(window).Moved(); }
		[MonoPInvokeCallback(typeof(NativeClosedDelegate))]
		static void Static_NativeClosedDelegate(void* window) { GetBFWindow(window).Closed(); }
		[MonoPInvokeCallback(typeof(NativeCloseQueryDelegate))]
		static int Static_NativeCloseQueryDelegate(void* window) { return GetBFWindow(window).CloseQuery(); }
		[MonoPInvokeCallback(typeof(NativeGotFocusDelegate))]
		static void Static_NativeGotFocusDelegate(void* window) { GetBFWindow(window).GotFocus(); }
		[MonoPInvokeCallback(typeof(NativeLostFocusDelegate))]
		static void Static_NativeLostFocusDelegate(void* window) { GetBFWindow(window).LostFocus(); }
		[MonoPInvokeCallback(typeof(NativeKeyCharDelegate))]
		static void Static_NativeKeyCharDelegate(void* window, char8 c) { GetBFWindow(window).KeyChar(c); }
		[MonoPInvokeCallback(typeof(NativeKeyDownDelegate))]
		static bool Static_NativeKeyDownDelegate(void* window, int key, int isRepeat) { return GetBFWindow(window).KeyDown(key, isRepeat); }
		[MonoPInvokeCallback(typeof(NativeKeyUpDelegate))]
		static void Static_NativeKeyUpDelegate(void* window, int key) { GetBFWindow(window).KeyUp(key); }
		[MonoPInvokeCallback(typeof(NativeMouseMoveDelegate))]
		static void Static_NativeMouseMoveDelegate(void* window, int mouseX, int mouseY) { GetBFWindow(window).MouseMove(mouseX, mouseY); }
		[MonoPInvokeCallback(typeof(NativeMouseProxyMoveDelegate))]
		static void Static_NativeMouseProxyMoveDelegate(void* window, int mouseX, int mouseY) { GetBFWindow(window).MouseProxyMove(mouseX, mouseY); }
		[MonoPInvokeCallback(typeof(NativeMouseDownDelegate))]
		static void Static_NativeMouseDownDelegate(void* window, int mouseX, int mouseY, int btnNum, int btnCount) { GetBFWindow(window).MouseDown(mouseX, mouseY, btnNum, btnCount); }
		[MonoPInvokeCallback(typeof(NativeMouseUpDelegate))]
		static void Static_NativeMouseUpDelegate(void* window, int mouseX, int mouseY, int btnNum) { GetBFWindow(window).MouseUp(mouseX, mouseY, btnNum); }
		[MonoPInvokeCallback(typeof(NativeMouseWheelDelegate))]
		static void Static_NativeMouseWheelDelegate(void* window, int mouseX, int mouseY, int delta) { GetBFWindow(window).MouseWheel(mouseX, mouseY, delta); }
		[MonoPInvokeCallback(typeof(NativeMouseLeaveDelegate))]
		static void Static_NativeMouseLeaveDelegate(void* window) { GetBFWindow(window).MouseLeave(); }
		[MonoPInvokeCallback(typeof(NativeMenuItemSelectedDelegate))]
		static void Static_NativeMenuItemSelectedDelegate(void* window, void* item) { GetBFWindow(window).NativeMenuItemSelected(item); }
		#else
		static void Static_NativeMovedDelegate(void* window) { GetBFWindow(window).Moved(); }
		static void Static_NativeClosedDelegate(void* window) { GetBFWindow(window).Closed(); }
		static int32 Static_NativeCloseQueryDelegate(void* window) { return GetBFWindow(window).CloseQuery(); }
		static void Static_NativeGotFocusDelegate(void* window) { GetBFWindow(window).GotFocus(); }
		static void Static_NativeLostFocusDelegate(void* window) { GetBFWindow(window).LostFocus(null); }
		static void Static_NativeKeyCharDelegate(void* window, char32 c) { GetBFWindow(window).KeyChar(c); }
		static bool Static_NativeKeyDownDelegate(void* window, int32 key, int32 isRepeat) { return GetBFWindow(window).KeyDown(key, isRepeat); }
		static void Static_NativeKeyUpDelegate(void* window, int32 key) { GetBFWindow(window).KeyUp(key); }
        static int32 Static_NativeHitTestDelegate(void* window, int32 x, int32 y) { return (int32)GetBFWindow(window).HitTest(x, y); }        
        static void Static_NativeMouseMoveDelegate(void* window, int32 mouseX, int32 mouseY) { GetBFWindow(window).MouseMove(mouseX, mouseY); }
		static void Static_NativeMouseProxyMoveDelegate(void* window, int32 mouseX, int32 mouseY) { GetBFWindow(window).MouseProxyMove(mouseX, mouseY); }
		static void Static_NativeMouseDownDelegate(void* window, int32 mouseX, int32 mouseY, int32 btnNum, int32 btnCount) { GetBFWindow(window).MouseDown(mouseX, mouseY, btnNum, btnCount); }
		static void Static_NativeMouseUpDelegate(void* window, int32 mouseX, int32 mouseY, int32 btnNum) { GetBFWindow(window).MouseUp(mouseX, mouseY, btnNum); }
		static void Static_NativeMouseWheelDelegate(void* window, int32 mouseX, int32 mouseY, float deltaX, float deltaY) { GetBFWindow(window).MouseWheel(mouseX, mouseY, deltaX, deltaY); }
		static void Static_NativeMouseLeaveDelegate(void* window) { GetBFWindow(window).MouseLeave(); }
		static void Static_NativeMenuItemSelectedDelegate(void* window, void* item) { GetBFWindow(window).NativeMenuItemSelected(item); }
		static void Static_NativeDragDropFileDelegate(void* window, char8* filePath) { GetBFWindow(window).DragDropFile(StringView(filePath)); }
		#endif

		public this()
		{
		}

        public this(BFWindow parent, String title, int x, int y, int width, int height, BFWindow.Flags windowFlags)
		{
			Init(parent, title, x, y, width, height, windowFlags);
		}

		public ~this()
		{
			bool worked = sWindowDictionary.Remove((int)mNativeWindow);
			Debug.Assert(worked);
		}

		void Init(BFWindow parent, String title, int x, int y, int width, int height, BFWindow.Flags windowFlags)
        {
            mTitle = new String(title);
            mParent = parent;
            if (mParent != null)
                mParent.mChildWindows.Add(this);

			var useFlags = windowFlags;
			/*if (useFlags.HasFlag(.FakeFocus))
			{
				useFlags |= .NoActivate;
				useFlags |= .NoMouseActivate;
			}*/

			if (windowFlags.HasFlag(.NoShow))
				mVisible = false;

            mNativeWindow = BFApp_CreateWindow((parent != null) ? (parent.mNativeWindow) : null, title, (int32)x, (int32)y, (int32)width, (int32)height, (int32)useFlags);
            sWindowDictionary[(int)mNativeWindow] = this;

            if (sNativeMovedDelegate == null)
            {
				sNativeMovedDelegate = new => Static_NativeMovedDelegate;
				sNativeClosedDelegate = new => Static_NativeClosedDelegate;
				sNativeCloseQueryDelegate = new => Static_NativeCloseQueryDelegate;
				sNativeGotFocusDelegate = new => Static_NativeGotFocusDelegate;
				sNativeLostFocusDelegate = new => Static_NativeLostFocusDelegate;
				sNativeKeyCharDelegate = new => Static_NativeKeyCharDelegate;
				sNativeKeyDownDelegate = new => Static_NativeKeyDownDelegate;
				sNativeKeyUpDelegate = new => Static_NativeKeyUpDelegate;
                sNativeHitTestDelegate = new => Static_NativeHitTestDelegate;
				sNativeMouseMoveDelegate = new => Static_NativeMouseMoveDelegate;
				sNativeMouseProxyMoveDelegate = new => Static_NativeMouseProxyMoveDelegate;
				sNativeMouseDownDelegate = new => Static_NativeMouseDownDelegate;
				sNativeMouseUpDelegate = new => Static_NativeMouseUpDelegate;
				sNativeMouseWheelDelegate = new => Static_NativeMouseWheelDelegate;
				sNativeMouseLeaveDelegate = new => Static_NativeMouseLeaveDelegate;
				sNativeMenuItemSelectedDelegate = new => Static_NativeMenuItemSelectedDelegate;
				sNativeDragDropFileDelegate = new => Static_NativeDragDropFileDelegate;
            }

            BFWindow_SetCallbacks(mNativeWindow, sNativeMovedDelegate.GetFuncPtr(), sNativeCloseQueryDelegate.GetFuncPtr(), sNativeClosedDelegate.GetFuncPtr(), sNativeGotFocusDelegate.GetFuncPtr(), sNativeLostFocusDelegate.GetFuncPtr(),
                sNativeKeyCharDelegate.GetFuncPtr(), sNativeKeyDownDelegate.GetFuncPtr(), sNativeKeyUpDelegate.GetFuncPtr(), sNativeHitTestDelegate.GetFuncPtr(),
                sNativeMouseMoveDelegate.GetFuncPtr(), sNativeMouseProxyMoveDelegate.GetFuncPtr(), sNativeMouseDownDelegate.GetFuncPtr(), sNativeMouseUpDelegate.GetFuncPtr(), sNativeMouseWheelDelegate.GetFuncPtr(), sNativeMouseLeaveDelegate.GetFuncPtr(),
                sNativeMenuItemSelectedDelegate.GetFuncPtr(), sNativeDragDropFileDelegate.GetFuncPtr());            
            BFApp.sApp.mWindows.Add(this);

            mDefaultDrawLayer = new DrawLayer(this);

            if (windowFlags.HasFlag(Flags.Menu))
            {
                mSysMenu = new SysMenu();
                mSysMenu.mWindow = this;                
            }
            mWindowFlags = windowFlags;

            if ((parent != null) && (!mWindowFlags.HasFlag(Flags.NoActivate)))
                parent.LostFocus(this);

            if ((parent != null) && (mWindowFlags.HasFlag(Flags.Modal)))
                parent.PreModalChild();

			if ((mWindowFlags.HasFlag(.FakeFocus)) && (!mWindowFlags.HasFlag(.NoActivate)))
				GotFocus();

			BFApp.sApp.RehupMouse();
        }

		public void SetTitle(String title)
		{
			mTitle.Set(title);
			BFWindow_SetTitle(mNativeWindow, mTitle);
		}

		public int Handle
		{
			get
			{
				return (int)BFWindow_GetNativeUnderlying(mNativeWindow);
			}
		}

#if BF_PLATFORM_WINDOWS
		public Windows.HWnd HWND
		{
			get
			{
				return (.)(int)BFWindow_GetNativeUnderlying(mNativeWindow);
			}
		}
#endif

        public void PreModalChild()
        {
            //MouseLeave();
        }

        public virtual void Dispose()
        {            
            Close(true);            
        }

        public virtual int32 CloseQuery()
        {
            return 1;
        }

        public virtual void Close(bool force = false)
        {
			// This doesn't play properly with CloseQuery.  We may do a force close in CloseQuery, but that fails
			//  if we do this following logic:
			/*if (mNativeWindowClosed)
				return;
			mNativeWindowClosed = true;*/

			while (mChildWindows.Count > 0)	
				mChildWindows[mChildWindows.Count - 1].Close(force);

			//for (var childWindow in mChildWindows)
				//childWindow.Close(force);
				//mChildWindows[mChildWindows.Count - 1].Close(force);

            if (mNativeWindow != null)
            {
                BFWindow_Close(mNativeWindow, force ? 1 : 0);
            }
			else
			{
				Closed();
			}
        }

        public virtual void Resize(int x, int y, int width, int height, ShowKind showKind = .Normal)
        {
            Debug.Assert(mNativeWindow != null);
            BFWindow_Resize(mNativeWindow, (int32)x, (int32)y, (int32)width, (int32)height, (int32)showKind);
        }

        public void SetForeground()
        {
            BFWindow_SetForeground(mNativeWindow);
            GotFocus();
        }

		public void Show(ShowKind showKind)
		{
			mShowKind = showKind;
			BFWindow_Show(mNativeWindow, showKind);
		}

        public void SetNonExclusiveMouseCapture()
        {
            // Does checking of mouse coords against all window even when this window has mouse capture,
            //  helps some dragging scenarios.  Gets turned off automatically on mouse up.
            BFWindow_SetNonExclusiveMouseCapture(mNativeWindow);            
        }

		public bool HasParent(BFWindow widgetWindow)
		{
			var checkParent = mParent;
			while (checkParent != null)
			{
				if (checkParent == widgetWindow)
					return true;
				checkParent = checkParent.mParent;
			}
			return false;
		}

        public virtual void Closed()
        {
			if (mHasClosed)
				return;
			mHasClosed = true;

            bool hadFocus = mHasFocus;

            BFApp.sApp.mWindows.Remove(this);

            if (mWindowFlags.HasFlag(Flags.QuitOnClose))
                BFApp.sApp.Stop();
            
            if (mParent != null)
            {
                mParent.mChildWindows.Remove(this);
                if ((hadFocus) && (mWindowFlags.HasFlag(Flags.Modal)))
                    mParent.GotFocus();
            }

			DeleteAndNullify!(mDefaultDrawLayer);
			BFApp.sApp.DeferDelete(this);
        }

        public void SetMinimumSize(int32 minWidth, int32 minHeight, bool clientSized = false)
        {
            BFWindow_SetMinimumSize(mNativeWindow, minWidth, minHeight, clientSized);
        }

		public virtual void RehupSize()
		{
			BFWindow_GetPosition(mNativeWindow, out mX, out mY, out mWindowWidth, out mWindowHeight, out mClientX, out mClientY, out mClientWidth, out mClientHeight);

			int32 showKind = 0;
			BFWindow_GetPlacement(mNativeWindow, out mNormX, out mNormY, out mNormWidth, out mNormHeight, out showKind);
			mShowKind = (.)showKind;

			mIsDirty = true;
		}

        public virtual void Moved()
        {            
            RehupSize();
        }

        public virtual void SetClientPosition(float x, float y)
        {
            mClientX = (int32)x;
            mClientY = (int32)y;
            BFWindow_SetClientPosition(mNativeWindow, (int32)x, (int32)y);
        }

        public virtual void SetAlpha(float alpha, uint32 destAlphaSrcMask, bool mouseVisible)
        {
            mAlpha = alpha;
            BFWindow_SetAlpha(mNativeWindow, alpha, destAlphaSrcMask, mouseVisible ? 1 : 0);
        }

        public virtual void SetMouseVisible(bool mouseVisible)
        {
            mMouseVisible = mouseVisible;
            BFWindow_SetMouseVisible(mNativeWindow, mouseVisible);
        }

		public virtual void CaptureMouse()
		{
			BFWindow_CaptureMouse(mNativeWindow);
		}

		public bool IsMouseCaptured()
		{
			return BFWindow_IsMouseCaptured(mNativeWindow);
		}

        public virtual void* AddMenuItem(void* parent, int insertIdx, String text, String hotKey, void* bitmap, bool enabled, int checkState, bool radioCheck)
        {
            return BFWindow_AddMenuItem(mNativeWindow, parent, (int32)insertIdx, text, hotKey, bitmap, enabled ? 1 : 0, (int32)checkState, radioCheck ? 1 : 0);
        }

        public virtual void ModifyMenuItem(void* item, String text, String hotKey, void* bitmap, bool enabled, int checkState, bool radioCheck)
        {
            BFWindow_ModifyMenuItem(mNativeWindow, item, text, hotKey, bitmap, enabled ? 1 : 0, (int32)checkState, radioCheck ? 1 : 0);
        }

        public virtual void DeleteMenuItem(void* menuItem)
        {
            BFWindow_DeleteMenuItem(mNativeWindow, menuItem);
        }

        public virtual void MenuItemSelected(SysMenu sysMenu)
        {
            sysMenu.Selected();
        }

        public virtual void NativeMenuItemSelected(void* menu)
        {
            SysMenu aSysMenu = mSysMenuMap[(int)menu];
            MenuItemSelected(aSysMenu);                      
        }

		public virtual void DragDropFile(StringView filePath)
		{
		}

        public virtual SysBitmap LoadSysBitmap(String path)
        {
            return null;
        }

        public virtual void GotFocus()
        {
            if (mHasFocus)
                return;
            mHasFocus = true;
            //Console.WriteLine("GotFocus {0}", mTitle);
        }

        public virtual void LostFocus(BFWindow newFocus)
        {			
            if (!mHasFocus)
                return;
			mHasFocus = false;
            if (mNativeWindow != null)
                BFWindow_LostFocus(mNativeWindow, (newFocus != null) ? newFocus.mNativeWindow : null);            

			//TODO: REMOVE
            //Debug.WriteLine("LostFocus {0}", mTitle);
        }

		public virtual int GetDPI()
		{
			return BFWindow_GetDPI(mNativeWindow);
		}
        
        public virtual void KeyChar(char32 theChar)
        {
        }

        public virtual bool KeyDown(int32 keyCode, int32 isRepeat)
        {
            return false;
        }

        public virtual void KeyUp(int32 keyCode)
        {
			
        }

        public virtual HitTestResult HitTest(int32 x, int32 y)
        {
            return HitTestResult.NotHandled;
        }

        public virtual void MouseMove(int32 x, int32 y)
        {
        }

        public virtual void MouseProxyMove(int32 x, int32 y)
        {
        }

        public virtual void MouseDown(int32 x, int32 y, int32 btn, int32 btnCount)
        {
        }

        public virtual void MouseUp(int32 x, int32 y, int32 btn)
        {
        }

        public virtual void MouseWheel(int32 x, int32 y, float deltaX, float deltaY)
        {
        }

        public virtual void MouseLeave()
        {
        }

        public override void Draw(Graphics g)
        {
        }

        public virtual void Update()
        {            
        }

		public virtual void UpdateF(float updatePct)
		{

		}
    }
#else
    public class BFWindow : BFWindowBase, IStudioClientWindow
    {        
        //IStudioWidgetWindow mProxy;
        public int mClientWidth;
        public int mClientHeight;
        public int mClientX;
        public int mClientY;
        public float mAlpha = 1.0f;
        public bool mVisible = true;
        public Flags mWindowFlags;
        
        public IPCProxy<IStudioHostWindow> mRemoteWindow;
        public IPCEndpoint<IStudioClientWindow> mStudioClientWindow;

        public BFWindow(BFWindow parent, string title, int x, int y, int width, int height, BFWindow.Flags windowFlags)
        {
            mStudioClientWindow = IPCEndpoint<IStudioClientWindow>.Create(this);

            IStudioHost studioInstance = BFApp.sApp.mStudioHost.Proxy;
            IPCObjectId remoteWindowId = studioInstance.CreateWindow(mStudioClientWindow.ObjId, 0, title, x, y, width, height, (int)windowFlags);
            mRemoteWindow = IPCProxy<IStudioHostWindow>.Create(remoteWindowId);

            mDefaultDrawLayer = new DrawLayer(this);

            BFApp.sApp.mWindows.Add(this);

            mClientX = 0;
            mClientY = 0;
            mClientWidth = width;
            mClientHeight = height;

            mWindowFlags = windowFlags;
        }

        public void Dispose()
        {
            Close();
        }

        public virtual int CloseQuery()
        {
            return 1;
        }

        public virtual void Close(bool force = false)
        {
            //if (mNativeWindow != null)
                //BFWindow_Close(mNativeWindow, force ? 1 : 0);
        }

        public virtual void Closed()
        {
            //mNativeWindow = null;
            BFApp.sApp.mWindows.Remove(this);
        }

        public virtual void Moved()
        {
            //BFWindow_GetPosition(mNativeWindow, out mX, out mY, out mWindowWidth, out mWindowHeight, out mClientX, out mClientY, out mClientWidth, out mClientHeight);
        }

        public virtual void SetClientPosition(float x, float y)
        {
            //BFWindow_SetClientPosition(mNativeWindow, (int)x, (int)y);
        }

        public virtual void SetAlpha(float alpha, uint destAlphaSrcMask, bool mouseVisible)
        {
            //mAlpha = alpha;
            //BFWindow_SetAlpha(mNativeWindow, alpha, destAlphaSrcMask, mouseVisible ? 1 : 0);
        }

        public virtual void* AddMenuItem(void* parent, string text, string hotKey, void* bitmap, bool enabled, int checkState, bool radioCheck)
        {
            //return BFWindow_AddMenuItem(mNativeWindow, parent, text, hotKey, bitmap, enabled ? 1 : 0, checkState, radioCheck ? 1 : 0);
            return null;
        }

        public virtual void MenuItemSelected(SysMenu sysMenu)
        {
            sysMenu.Selected();
        }

        public virtual void NativeMenuItemSelected(void* menu)
        {
            //SysMenu aSysMenu = mSysMenuMap[menu];
            //MenuItemSelected(aSysMenu);
        }

        public virtual SysBitmap LoadSysBitmap(string path)
        {
            return null;
        }

        public virtual void GotFocus()
        {
        }

        public virtual void LostFocus()
        {
        }

        public virtual void KeyChar(char32 theChar)
        {
        }        

        public virtual void KeyDown(int keyCode, int isRepeat)
        {
        }

        public virtual void KeyUp(int keyCode)
        {
        }

        public virtual void MouseMove(int x, int y)
        {
        }

        public virtual void MouseProxyMove(int x, int y)
        {
        }

        public virtual void MouseDown(int x, int y, int btn, int btnCount)
        {
        }

        public virtual void MouseUp(int x, int y, int btn)
        {
        }

        public virtual void MouseWheel(int x, int y, int delta)
        {
        }

        public virtual void MouseLeave()
        {
        }
                
        public virtual void Update()
        {
        }

        public override void PreDraw(Graphics g)
        {                        
            base.PreDraw(g);
            mRemoteWindow.Proxy.RemoteDrawingStarted();
        }

        public override void PostDraw(Graphics g)
        {
            base.PostDraw(g);
            mRemoteWindow.Proxy.RemoteDrawingDone();
        }

        public void SetIsVisible(bool visible)
        {
            mVisible = visible;
        }
    }
#endif
}
