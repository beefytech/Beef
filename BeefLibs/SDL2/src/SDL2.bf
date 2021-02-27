/* Derived from SDL2# - C# Wrapper for SDL2
 *
 * Copyright (c) 2013-2016 Ethan Lee.
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * In no event will the authors be held liable for any damages arising from
 * the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 * claim that you wrote the original software. If you use this software in a
 * product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * Ethan "flibitijibibo" Lee <flibitijibibo@flibitijibibo.com>
 *
 */

using System;

namespace SDL2
{
	public static class SDL
	{
		public static uint32 FOURCC(uint8 A, uint8 B, uint8 C, uint8 D)
		{
			return ((uint32)A | ((uint32)B << 8) | ((uint32)C << 16) | ((uint32)D << 24));
		}

		public enum Bool : int32
		{
			case False;
			case True;

			public static operator Bool (bool val)
			{
				return val ? True : False;
			}

			public static operator bool (Bool val)
			{
				return (val == .True) ? true : false;
			}
		}

		[LinkName("SDL_malloc")]
		public static extern void* malloc(int size);

		[LinkName("SDL_free")]
		public static extern void free(void* memblock);

		public struct RWOps;

		[LinkName("SDL_RWFromFile")]
		public static extern RWOps* RWFromFile(char8* file, char8* mode);
		
		/* mem refers to a void*, IntPtr to an SDL_RWops* */
		[LinkName("SDL_RWFromMem")]
		public static extern RWOps* RWFromMem(void* mem, int32 size);

		[LinkName("SDL_SetMainReady")]
		public static extern void SetMainReady();

		public enum InitFlag : uint32
		{
			Timer =				0x00000001,
			Audio =				0x00000010,
			Video =				0x00000020,
			Joystick =			0x00000200,
			Haptic =			0x00001000,
			GameController = 	0x00002000,
			Events =			0x00004000,
			Sensor =			0x00008000,
			NoParachute =		0x00100000,
			Everything = (
				Timer | Audio | Video |
				Events | Joystick | Haptic |
				GameController | Sensor
			)
		}

		[LinkName("SDL_Init")]
		public static extern int Init(InitFlag flags);
		[LinkName("SDL_InitSubSystem")]
		public static extern int InitSubSystem(InitFlag flags);
		[LinkName("SDL_Quit")]
		public static extern void Quit();
		[LinkName("SDL_QuitSubSystem")]
		public static extern void QuitSubSystem(InitFlag flags);
		[LinkName("SDL_WasInit")]
		public static extern uint32 WasInit(InitFlag flags);

		[LinkName("SDL_GetPlatform")]
		public static extern char8* GetPlatform();

		public const String SDL_HINT_FRAMEBUFFER_ACCELERATION =
			"SDL_FRAMEBUFFER_ACCELERATION";
		public const String SDL_HINT_RENDER_DRIVER =
			"SDL_RENDER_DRIVER";
		public const String SDL_HINT_RENDER_OPENGL_SHADERS =
			"SDL_RENDER_OPENGL_SHADERS";
		public const String SDL_HINT_RENDER_DIRECT3D_THREADSAFE =
			"SDL_RENDER_DIRECT3D_THREADSAFE";
		public const String SDL_HINT_RENDER_VSYNC =
			"SDL_RENDER_VSYNC";
		public const String SDL_HINT_VIDEO_X11_XVIDMODE =
			"SDL_VIDEO_X11_XVIDMODE";
		public const String SDL_HINT_VIDEO_X11_XINERAMA =
			"SDL_VIDEO_X11_XINERAMA";
		public const String SDL_HINT_VIDEO_X11_XRANDR =
			"SDL_VIDEO_X11_XRANDR";
		public const String SDL_HINT_GRAB_KEYBOARD =
			"SDL_GRAB_KEYBOARD";
		public const String SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS =
			"SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS";
		public const String SDL_HINT_IDLE_TIMER_DISABLED =
			"SDL_IOS_IDLE_TIMER_DISABLED";
		public const String SDL_HINT_ORIENTATIONS =
			"SDL_IOS_ORIENTATIONS";
		public const String SDL_HINT_XINPUT_ENABLED =
			"SDL_XINPUT_ENABLED";
		public const String SDL_HINT_GAMECONTROLLERCONFIG =
			"SDL_GAMECONTROLLERCONFIG";
		public const String SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS =
			"SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS";
		public const String SDL_HINT_ALLOW_TOPMOST =
			"SDL_ALLOW_TOPMOST";
		public const String SDL_HINT_TIMER_RESOLUTION =
			"SDL_TIMER_RESOLUTION";
		public const String SDL_HINT_RENDER_SCALE_QUALITY =
			"SDL_RENDER_SCALE_QUALITY";

		/* Only available in SDL 2.0.1 or higher */
		public const String SDL_HINT_VIDEO_HIGHDPI_DISABLED =
			"SDL_VIDEO_HIGHDPI_DISABLED";

		/* Only available in SDL 2.0.2 or higher */
		public const String SDL_HINT_CTRL_CLICK_EMULATE_RIGHT_CLICK =
			"SDL_CTRL_CLICK_EMULATE_RIGHT_CLICK";
		public const String SDL_HINT_VIDEO_WIN_D3DCOMPILER =
			"SDL_VIDEO_WIN_D3DCOMPILER";
		public const String SDL_HINT_MOUSE_RELATIVE_MODE_WARP =
			"SDL_MOUSE_RELATIVE_MODE_WARP";
		public const String SDL_HINT_VIDEO_WINDOW_SHARE_PIXEL_FORMAT =
			"SDL_VIDEO_WINDOW_SHARE_PIXEL_FORMAT";
		public const String SDL_HINT_VIDEO_ALLOW_SCREENSAVER =
			"SDL_VIDEO_ALLOW_SCREENSAVER";
		public const String SDL_HINT_ACCELEROMETER_AS_JOYSTICK =
			"SDL_ACCELEROMETER_AS_JOYSTICK";
		public const String SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES =
			"SDL_VIDEO_MAC_FULLSCREEN_SPACES";

		/* Only available in SDL 2.0.3 or higher */
		public const String SDL_HINT_WINRT_PRIVACY_POLICY_URL =
			"SDL_WINRT_PRIVACY_POLICY_URL";
		public const String SDL_HINT_WINRT_PRIVACY_POLICY_LABEL =
			"SDL_WINRT_PRIVACY_POLICY_LABEL";
		public const String SDL_HINT_WINRT_HANDLE_BACK_BUTTON =
			"SDL_WINRT_HANDLE_BACK_BUTTON";

		/* Only available in SDL 2.0.4 or higher */
		public const String SDL_HINT_NO_SIGNAL_HANDLERS =
			"SDL_NO_SIGNAL_HANDLERS";
		public const String SDL_HINT_IME_INTERNAL_EDITING =
			"SDL_IME_INTERNAL_EDITING";
		public const String SDL_HINT_ANDROID_SEPARATE_MOUSE_AND_TOUCH =
			"SDL_ANDROID_SEPARATE_MOUSE_AND_TOUCH";
		public const String SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT =
			"SDL_EMSCRIPTEN_KEYBOARD_ELEMENT";
		public const String SDL_HINT_THREAD_STACK_SIZE =
			"SDL_THREAD_STACK_SIZE";
		public const String SDL_HINT_WINDOW_FRAME_USABLE_WHILE_CURSOR_HIDDEN =
			"SDL_WINDOW_FRAME_USABLE_WHILE_CURSOR_HIDDEN";
		public const String SDL_HINT_WINDOWS_ENABLE_MESSAGELOOP =
			"SDL_WINDOWS_ENABLE_MESSAGELOOP";
		public const String SDL_HINT_WINDOWS_NO_CLOSE_ON_ALT_F4 =
			"SDL_WINDOWS_NO_CLOSE_ON_ALT_F4";
		public const String SDL_HINT_XINPUT_USE_OLD_JOYSTICK_MAPPING =
			"SDL_XINPUT_USE_OLD_JOYSTICK_MAPPING";
		public const String SDL_HINT_MAC_BACKGROUND_APP =
			"SDL_MAC_BACKGROUND_APP";
		public const String SDL_HINT_VIDEO_X11_NET_WM_PING =
			"SDL_VIDEO_X11_NET_WM_PING";
		public const String SDL_HINT_ANDROID_APK_EXPANSION_MAIN_FILE_VERSION =
			"SDL_ANDROID_APK_EXPANSION_MAIN_FILE_VERSION";
		public const String SDL_HINT_ANDROID_APK_EXPANSION_PATCH_FILE_VERSION =
			"SDL_ANDROID_APK_EXPANSION_PATCH_FILE_VERSION";

		/* Only available in 2.0.5 or higher */
		public const String SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH =
			"SDL_MOUSE_FOCUS_CLICKTHROUGH";
		public const String SDL_HINT_BMP_SAVE_LEGACY_FORMAT =
			"SDL_BMP_SAVE_LEGACY_FORMAT";
		public const String SDL_HINT_WINDOWS_DISABLE_THREAD_NAMING =
			"SDL_WINDOWS_DISABLE_THREAD_NAMING";
		public const String SDL_HINT_APPLE_TV_REMOTE_ALLOW_ROTATION =
			"SDL_APPLE_TV_REMOTE_ALLOW_ROTATION";

		/* Only available in 2.0.6 or higher */
		public const String SDL_HINT_AUDIO_RESAMPLING_MODE =
			"SDL_AUDIO_RESAMPLING_MODE";
		public const String SDL_HINT_RENDER_LOGICAL_SIZE_MODE =
			"SDL_RENDER_LOGICAL_SIZE_MODE";
		public const String SDL_HINT_MOUSE_NORMAL_SPEED_SCALE =
			"SDL_MOUSE_NORMAL_SPEED_SCALE";
		public const String SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE =
			"SDL_MOUSE_RELATIVE_SPEED_SCALE";
		public const String SDL_HINT_TOUCH_MOUSE_EVENTS =
			"SDL_TOUCH_MOUSE_EVENTS";
		public const String SDL_HINT_WINDOWS_INTRESOURCE_ICON =
			"SDL_WINDOWS_INTRESOURCE_ICON";
		public const String SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL =
			"SDL_WINDOWS_INTRESOURCE_ICON_SMALL";
		
		/* Only available in 2.0.8 or higher */
		public const String SDL_HINT_IOS_HIDE_HOME_INDICATOR =
			"SDL_IOS_HIDE_HOME_INDICATOR";

		/* Only available in 2.0.9 or higher */
		public const String SDL_HINT_MOUSE_DOUBLE_CLICK_TIME =
			"SDL_MOUSE_DOUBLE_CLICK_TIME";
		public const String SDL_HINT_MOUSE_DOUBLE_CLICK_RADIUS =
			"SDL_MOUSE_DOUBLE_CLICK_RADIUS";
		public const String SDL_HINT_JOYSTICK_HIDAPI =
			"SDL_JOYSTICK_HIDAPI";
		public const String SDL_HINT_JOYSTICK_HIDAPI_PS4 =
			"SDL_JOYSTICK_HIDAPI_PS4";
		public const String SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE =
			"SDL_JOYSTICK_HIDAPI_PS4_RUMBLE";
		public const String SDL_HINT_JOYSTICK_HIDAPI_STEAM =
			"SDL_JOYSTICK_HIDAPI_STEAM";
		public const String SDL_HINT_JOYSTICK_HIDAPI_SWITCH =
			"SDL_JOYSTICK_HIDAPI_SWITCH";
		public const String SDL_HINT_JOYSTICK_HIDAPI_XBOX =
			"SDL_JOYSTICK_HIDAPI_XBOX";
		public const String SDL_HINT_ENABLE_STEAM_CONTROLLERS =
			"SDL_ENABLE_STEAM_CONTROLLERS";
		public const String SDL_HINT_ANDROID_TRAP_BACK_BUTTON =
			"SDL_ANDROID_TRAP_BACK_BUTTON";

		public enum SDL_HintPriority : int32
		{
			Default,
			Normal,
			Override
		}

		[LinkName("SDL_ClearHints")]
		public static extern void ClearHints();
		[LinkName("SDL_GetHint")]
		public static extern char8* GetHint(char8* name);
		[LinkName("SDL_SetHint")]
		public static extern Bool SetHint(char8* name, char8* value);
		[LinkName("SDL_SetHintWithPriority")]
		public static extern Bool SetHintWithPriority( char8* name, char8* value, SDL_HintPriority priority);

		[LinkName("SDL_GetHintBoolean")]
		public static extern Bool GetHintBoolean(char8* name, Bool default_value);

		[LinkName("SDL_ClearError")]
		public static extern void ClearError();

		[LinkName("SDL_GetError")]
		public static extern char8* GetError();

		/* Use string.Format for arglists */
		[LinkName("SDL_SetError")]
		public static extern void SetError(char8* fmtAndArglist);

		/* Begin nameless enum SDL_LOG_CATEGORY */
		public const int32 LOG_CATEGORY_APPLICATION = 0;
		public const int32 LOG_CATEGORY_ERROR = 1;
		public const int32 LOG_CATEGORY_ASSERT = 2;
		public const int32 LOG_CATEGORY_SYSTEM = 3;
		public const int32 LOG_CATEGORY_AUDIO = 4;
		public const int32 LOG_CATEGORY_VIDEO = 5;
		public const int32 LOG_CATEGORY_RENDER = 6;
		public const int32 LOG_CATEGORY_INPUT = 7;
		public const int32 LOG_CATEGORY_TEST = 8;

		/* Reserved for future SDL library use */
		public const int32 LOG_CATEGORY_RESERVED1 = 9;
		public const int32 LOG_CATEGORY_RESERVED2 = 10;
		public const int32 LOG_CATEGORY_RESERVED3 = 11;
		public const int32 LOG_CATEGORY_RESERVED4 = 12;
		public const int32 LOG_CATEGORY_RESERVED5 = 13;
		public const int32 LOG_CATEGORY_RESERVED6 = 14;
		public const int32 LOG_CATEGORY_RESERVED7 = 15;
		public const int32 LOG_CATEGORY_RESERVED8 = 16;
		public const int32 LOG_CATEGORY_RESERVED9 = 17;
		public const int32 LOG_CATEGORY_RESERVED10 = 18;

		/* Beyond this point is reserved for application use, e.g.
			enum {
				LOG_CATEGORY_AWESOME1 = SDL_LOG_CATEGORY_CUSTOM,
				LOG_CATEGORY_AWESOME2,
				LOG_CATEGORY_AWESOME3,
				...
			};
		*/
		public const int32 LOG_CATEGORY_CUSTOM = 19;
		/* End nameless enum SDL_LOG_CATEGORY */

		public enum LogPriority : int32
		{
			SDL_LOG_PRIORITY_VERBOSE = 1,
			SDL_LOG_PRIORITY_DEBUG,
			SDL_LOG_PRIORITY_INFO,
			SDL_LOG_PRIORITY_WARN,
			SDL_LOG_PRIORITY_ERROR,
			SDL_LOG_PRIORITY_CRITICAL,
			SDL_NUM_LOG_PRIORITIES
		}

		/* userdata refers to a void*, message to a const char* */
		public function void LogOutputFunction(
			void* userdata,
			int32 category,
			LogPriority priority,
			char8* message
		);

		/* Use string.Format for arglists */
		[LinkName("SDL_Log")]
		public static extern void Log(char8* fmtAndArglist);

		/* Use string.Format for arglists */
		[LinkName("SDL_LogVerbose")]
		public static extern void LogVerbose(int category, char8* fmtAndArglist);

		/* Use string.Format for arglists */
		[LinkName("SDL_LogDebug")]
		public static extern void LogDebug(int32 category, char8* fmtAndArglist);

		/* Use string.Format for arglists */
		[LinkName("SDL_LogInfo")]
		
		public static extern void LogInfo(int32 category, char8* fmtAndArglist);

		/* Use string.Format for arglists */
		[LinkName("SDL_LogWarn")]
		public static extern void LogWarn(int32 category, char8* fmtAndArglist);

		/* Use string.Format for arglists */
		[LinkName("SDL_LogError")]
		public static extern void LogError(int32 category, char8* fmtAndArglist);

		/* Use string.Format for arglists */
		[LinkName("SDL_LogCritical")]
		public static extern void LogCritical(int32 category, char8* fmtAndArglist);

		/* Use string.Format for arglists */
		[LinkName("SDL_LogMessage")]
		public static extern void LogMessage(int32 category, LogPriority priority, char8* fmtAndArglist);

		/* Use string.Format for arglists */
		[LinkName("SDL_LogMessageV")]
		public static extern void LogMessageV(int32 category, LogPriority priority, char8* fmtAndArglist);

		[LinkName("SDL_LogGetPriority")]
		public static extern LogPriority SDL_LogGetPriority(int32 category);

		[LinkName("SDL_LogSetPriority")]
		public static extern void LogSetPriority(int32 category, LogPriority priority);

		[LinkName("SDL_LogSetAllPriority")]
		public static extern void LogSetAllPriority(LogPriority priority);

		[LinkName("SDL_LogSetAllPriority")]
		public static extern void LogResetPriorities();

		[LinkName("SDL_LogGetOutputFunction")]
		public static extern void LogGetOutputFunction(out LogOutputFunction callback, out void* userdata);

		[LinkName("SDL_LogSetOutputFunction")]
		public static extern void LogSetOutputFunction(LogOutputFunction callback, void* userdata);
		
		public enum MessageBoxFlags : uint32
		{
			Error =		0x00000010,
			Warning =	0x00000020,
			Information =	0x00000040
		}

		public enum MessageBoxButtonFlags : uint32
		{
			ReturnkeyDefault = 0x00000001,
			EscapekeyDefault = 0x00000002
		}

		[CRepr]
		public struct MessageBoxButtonData
		{
			public MessageBoxButtonFlags flags;
			public int32 buttonid;
			public char8* text; /* The UTF-8 button text */
		}

		[CRepr]
		public struct MessageBoxColor
		{
			public uint8 r, g, b;
		}

		public enum SDL_MessageBoxColorType : uint32
		{
			Background,
			Text,
			ButtonBorder,
			Button_background,
			Button_selected,
			COUNT
		}

		[CRepr]
		public struct MessageBoxColorScheme
		{
			public MessageBoxColor[(int)SDL_MessageBoxColorType.COUNT] colors;
		}

		public struct Window;

		[CRepr]
		public struct MessageBoxData
		{
			public MessageBoxFlags flags;
			public Window* window;				/* Parent window, can be NULL */
			public char8* title;				/* UTF-8 title */
			public char8* message;				/* UTF-8 message text */
			public int numbuttons;
			public MessageBoxButtonData* buttons;
			public MessageBoxColorScheme* colorScheme;	/* Can be NULL to use system settings */
		}


		[LinkName("SDL_ShowMessageBox")]
		public static extern int32 ShowMessageBox(ref MessageBoxData messageboxdata, out int32 buttonid);

		[LinkName("SDL_ShowSimpleMessageBox")]
		public static extern int SimpleMessageBox(
			MessageBoxFlags flags,
			char8* title,
			char8* message,
			Window* window
		);

		/* Similar to the headers, this is the version we're expecting to be
		 * running with. You will likely want to check this somewhere in your
		 * program!
		 */
		public const int MAJOR_VERSION =	2;
		public const int MINOR_VERSION =	0;
		public const int PATCHLEVEL =	9;

		public static readonly int COMPILEDVERSION = VERSIONNUM(
			MAJOR_VERSION,
			MINOR_VERSION,
			PATCHLEVEL
		);

		[CRepr]
		public struct Version
		{
			public uint8 major;
			public uint8 minor;
			public uint8 patch;
		}

		public static void VERSION(out Version x)
		{
			x.major = MAJOR_VERSION;
			x.minor = MINOR_VERSION;
			x.patch = PATCHLEVEL;
		}

		public static int32 VERSIONNUM(int32 X, int32 Y, int32 Z)
		{
			return (X * 1000) + (Y * 100) + Z;
		}

		public static Bool VERSION_ATLEAST(int32 X, int32 Y, int32 Z)
		{
			return (COMPILEDVERSION >= VERSIONNUM(X, Y, Z));
		}

		[LinkName("SDL_GetVersion")]
		public static extern void GetVersion(out Version ver);

		[LinkName("SDL_GetRevision")]
		public static extern char8* GetRevision();

		[LinkName("SDL_GetRevisionNumber")]
		public static extern int GetRevisionNumber();

		public enum SDL_GLAttr : uint32
		{
			GL_RED_SIZE,
			GL_GREEN_SIZE,
			GL_BLUE_SIZE,
			GL_ALPHA_SIZE,
			GL_BUFFER_SIZE,
			GL_DOUBLEBUFFER,
			GL_DEPTH_SIZE,
			GL_STENCIL_SIZE,
			GL_ACCUM_RED_SIZE,
			GL_ACCUM_GREEN_SIZE,
			GL_ACCUM_BLUE_SIZE,
			GL_ACCUM_ALPHA_SIZE,
			GL_STEREO,
			GL_MULTISAMPLEBUFFERS,
			GL_MULTISAMPLESAMPLES,
			GL_ACCELERATED_VISUAL,
			GL_RETAINED_BACKING,
			GL_CONTEXT_MAJOR_VERSION,
			GL_CONTEXT_MINOR_VERSION,
			GL_CONTEXT_EGL,
			GL_CONTEXT_FLAGS,
			GL_CONTEXT_PROFILE_MASK,
			GL_SHARE_WITH_CURRENT_CONTEXT,
			GL_FRAMEBUFFER_SRGB_CAPABLE,
			GL_CONTEXT_RELEASE_BEHAVIOR,
			GL_CONTEXT_RESET_NOTIFICATION,	/* Only available in 2.0.6 */
			GL_CONTEXT_NO_ERROR,		/* Only available in 2.0.6 */
		}

		public enum SDL_GLProfile : uint32
		{
			GL_CONTEXT_PROFILE_CORE				= 0x0001,
			GL_CONTEXT_PROFILE_COMPATIBILITY	= 0x0002,
			GL_CONTEXT_PROFILE_ES				= 0x0004
		}

		public enum SDL_GLContextFlags : uint32
		{
			GL_CONTEXT_DEBUG_FLAG				= 0x0001,
			GL_CONTEXT_FORWARD_COMPATIBLE_FLAG	= 0x0002,
			GL_CONTEXT_ROBUST_ACCESS_FLAG		= 0x0004,
			GL_CONTEXT_RESET_ISOLATION_FLAG		= 0x0008
		}

		public enum WindowEventID : uint8
		{
			None,
			Shown,
			Hidden,
			Exposed,
			Moved,
			Resized,
			SizeChanged,
			Minimized,
			Maximized,
			Restored,
			Enter,
			Leave,
			FocusGained,
			Focus_lost,
			Close,
			/* Available in 2.0.5 or higher */
			TAKE_FOCUS,
			HIT_TEST
		}

		public enum DisplayEventID : uint8
		{
			NONE,
			ORIENTATION
		}

		public enum DisplayOrientation : uint32
		{
			Unknown,
			Landscape,
			LandscapeFlipped,
			Portrait,
			PortraitFlipped
		}

		public enum WindowFlags : uint32
		{
			None = 0,
			Fullscreen =		0x00000001,
			OpenGL =		0x00000002,
			Shown =		0x00000004,
			Hidden =		0x00000008,
			Borderless =		0x00000010,
			Resizable =		0x00000020,
			Minimized =		0x00000040,
			Maximized =		0x00000080,
			InputGrabbed =	0x00000100,
			InputFocus =	0x00000200,
			MouseFocus =	0x00000400,
			FullscreenDesktop =
						(Fullscreen | 0x00001000),
			Foreign =		0x00000800,
			AllowHighDPI =	0x00002000,	/* Only available in 2.0.1 */
			MouseCapture =	0x00004000,	/* Only available in 2.0.4 */
			AlwaysOnTop =	0x00008000,	/* Only available in 2.0.5 */
			SkipTaskbar =	0x00010000,	/* Only available in 2.0.5 */
			Utility =		0x00020000,	/* Only available in 2.0.5 */
			Tooltip =		0x00040000,	/* Only available in 2.0.5 */
			PopupMenu =		0x00080000,	/* Only available in 2.0.5 */
			Vulkan =		0x10000000,	/* Only available in 2.0.6 */
		}

		/* Only available in 2.0.4 */
		public enum HitTestResult : uint32
		{
			Normal,		    /* Region is normal. No special properties. */
			Draggable,		/* Region can drag entire window. */
			ResizeTopLeft,
			ResizeTop,
			ResizeTopRight,
			ResizeRight,
			ResizeBottomRight,
			ResizeBottom,
			ResizeBottomLeft,
			ResizeLeft
		}

		struct WindowPos : int32
		{
			public const WindowPos Undefined = 0x1FFF0000;
			public const WindowPos Centered = 0x2FFF0000;
		}
		
		public static int32 WindowPosUndefinedDisplay(int32 x)
		{
			return ((.)WindowPos.Undefined | x);
		}

		public static bool WindowPosIsUndefined(int32 x)
		{
			return (x & 0xFFFF0000) == (.) WindowPos.Undefined;
		}

		public static int WindowPosCenteredDisplay(int32 x)
		{
			return ((.) WindowPos.Centered | x);
		}

		public static bool WindowPosIsCentered(int32 x)
		{
			return (x & 0xFFFF0000) == (.) WindowPos.Centered;
		}

		[CRepr]
		public struct SDL_DisplayMode
		{
			public uint32 format;
			public int32 w;
			public int32 h;
			public int32 refresh_rate;
			public void* driverdata; // void*
		}

		public function HitTestResult SDL_HitTest(Window win, Point* area, void* data);

		public struct Renderer;

		[LinkName("SDL_CreateWindow")]
		public static extern Window* CreateWindow(
			char8* title,
			WindowPos x,
			WindowPos y,
			int32 w,
			int32 h,
			WindowFlags flags
		);

		[LinkName("SDL_CreateWindowAndRenderer"), NoShow, Obsolete("WindowPos has been deprecated in favor of int32.", false)]
		public static extern int CreateWindowAndRenderer(
			WindowPos width,
			WindowPos height,
			WindowFlags window_flags,
			out Window* window,
			out Renderer* renderer
		);

		[LinkName("SDL_CreateWindowAndRenderer")]
		public static extern int CreateWindowAndRenderer(
			int32 width,
			int32 height,
			WindowFlags window_flags,
			out Window* window,
			out Renderer* renderer
		);

		/* data refers to some native window type, IntPtr to an SDL_Window* */
		[LinkName("SDL_CreateWindowFrom")]
		public static extern Window* SDL_CreateWindowFrom(void* data);

		
		[LinkName("SDL_DestroyWindow")]
		public static extern void DestroyWindow(Window* window);

		[LinkName("SDL_DisableScreenSaver")]
		public static extern void DisableScreenSaver();

		[LinkName("SDL_EnableScreenSaver")]
		public static extern void EnableScreenSaver();

		/* IntPtr refers to an SDL_DisplayMode. Just use closest. */
		[LinkName("SDL_GetClosestDisplayMode")]
		public static extern SDL_DisplayMode* SDL_GetClosestDisplayMode(
			int displayIndex,
			ref SDL_DisplayMode mode,
			out SDL_DisplayMode closest
		);


		[LinkName("SDL_GetCurrentDisplayMode")]
		public static extern int SDL_GetCurrentDisplayMode(
			int displayIndex,
			out SDL_DisplayMode mode
		);

		[LinkName("SDL_GetCurrentVideoDriver")]
		public static extern char8* GetCurrentVideoDriver();

		[LinkName("SDL_GetDesktopDisplayMode")]
		public static extern int32 GetDesktopDisplayMode(int32 displayIndex, out SDL_DisplayMode mode);

		[LinkName("SDL_GetDisplayName")]
		public static extern char8* GetDisplayName(int index);

		[LinkName("SDL_GetDisplayBounds")]
		public static extern int32 GetDisplayBounds(int32 displayIndex, out Rect rect);

		/* This function is only available in 2.0.4 or higher */
		[LinkName("SDL_GetDisplayDPI")]
		public static extern int32 GetDisplayDPI(int32 displayIndex, out float ddpi, out float hdpi, out float vdpi);

		/* This function is only available in 2.0.9 or higher */
		[LinkName("SDL_GetDisplayOrientation")]
		public static extern DisplayOrientation SDL_GetDisplayOrientation(int32 displayIndex);

		[LinkName("SDL_GetDisplayMode")]
		public static extern int32 GetDisplayMode(int32 displayIndex, int32 modeIndex, out SDL_DisplayMode mode);

		/* Available in 2.0.5 or higher */
		[LinkName("SDL_GetDisplayUsableBounds")]
		public static extern int SDL_GetDisplayUsableBounds(int displayIndex, out Rect rect);

		[LinkName("SDL_GetNumDisplayModes")]
		public static extern int32 GetNumDisplayModes(int32 displayIndex);

		[LinkName("SDL_GetNumVideoDisplays")]
		public static extern int SDL_GetNumVideoDisplays();

		[LinkName("SDL_GetNumVideoDrivers")]
		public static extern int SDL_GetNumVideoDrivers();

		[LinkName("SDL_GetVideoDriver")]
		public static extern char8* GetVideoDriver(int32 index);

		[LinkName("SDL_GetWindowBrightness")]
		public static extern float SDL_GetWindowBrightness(Window* window);

		/* Available in 2.0.5 or higher */
		[LinkName("SDL_SetWindowOpacity")]
		public static extern int32 SetWindowOpacity(Window* window, float opacity);

		/* Available in 2.0.5 or higher */
		[LinkName("SDL_GetWindowOpacity")]
		public static extern int32 GetWindowOpacity(Window* window, out float out_opacity);

		/* Available in 2.0.5 or higher */
		[LinkName("SDL_SetWindowModalFor")]
		public static extern int SDL_SetWindowModalFor(Window* modal_window, Window* parent_window);

		
		/* Available in 2.0.5 or higher */
		[LinkName("SDL_SetWindowInputFocus")]
		public static extern int SDL_SetWindowInputFocus(Window* window);

		/* window refers to an SDL_Window*, IntPtr to a void* */
		[LinkName("SDL_GetWindowData")]
		public static extern void* SDL_GetWindowData(Window* window, char8* name);

		
		[LinkName("SDL_GetWindowDisplayIndex")]
		public static extern int SDL_GetWindowDisplayIndex(Window* window);
		
		[LinkName("SDL_GetWindowDisplayMode")]
		public static extern int GetWindowDisplayMode(Window* window, out SDL_DisplayMode mode);

		[LinkName("SDL_GetWindowFlags")]
		public static extern uint32 GetWindowFlags(Window* window);

		[LinkName("SDL_GetWindowFromID")]
		public static extern Window* GetWindowFromID(uint32 id);
		
		[LinkName("SDL_GetWindowGammaRamp")]
		public static extern int32 GetWindowGammaRamp(Window* window, uint16* red, uint16* green, uint16* blue);
		
		[LinkName("SDL_GetWindowGrab")]
		public static extern Bool GetWindowGrab(Window* window);
		
		[LinkName("SDL_GetWindowID")]
		public static extern uint32 GetWindowID(Window* window);

		[LinkName("SDL_GetWindowPixelFormat")]
		public static extern uint32 GetWindowPixelFormat(Window* window);
		
		[LinkName("SDL_GetWindowMaximumSize")]
		public static extern void GetWindowMaximumSize(Window* window, out int32 max_w, out int32 max_h);

		[LinkName("SDL_GetWindowMinimumSize")]
		public static extern void GetWindowMinimumSize(Window* window, out int32 min_w, out int32 min_h);

		[LinkName("SDL_GetWindowPosition")]
		public static extern void GetWindowPosition(Window* window, out int32 x, out int32 y);
		
		[LinkName("SDL_GetWindowSize")]
		public static extern void GetWindowSize(Window* window, out int32 w, out int32 h);

		[LinkName("SDL_GetWindowSurface")]
		public static extern Surface* GetWindowSurface(Window* window);
		
		[LinkName("SDL_GetWindowTitle")]
		public static extern char8* GetWindowTitle(Window* window);

		public struct Texture;

		
		[LinkName("SDL_GL_BindTexture")]
		public static extern int32 SDL_GL_BindTexture(Texture* texture, out float texw, out float texh);

		public struct SDL_GLContext : int
		{
		}

		/* IntPtr and window refer to an SDL_GLContext and SDL_Window* */
		[LinkName("SDL_GL_CreateContext")]
		public static extern SDL_GLContext GL_CreateContext(Window* window);

		/* context refers to an SDL_GLContext */
		[LinkName("SDL_GL_DeleteContext")]
		public static extern void GL_DeleteContext(SDL_GLContext context);

		[LinkName("SDL_GL_GetProcAddress")]
		public static extern void* SDL_GL_GetProcAddress(char8* proc);

		[LinkName("SDL_GL_LoadLibrary")]
		public static extern int32 GL_LoadLibrary(char8* path);

		[LinkName("SDL_GL_UnloadLibrary")]
		public static extern void GL_UnloadLibrary();

		[LinkName("SDL_GL_ExtensionSupported")]
		public static extern Bool GL_ExtensionSupported(char8* name);

		/* Only available in SDL 2.0.2 or higher */
		[LinkName("SDL_GL_ResetAttributes")]
		public static extern void GL_ResetAttributes();

		[LinkName("SDL_GL_GetAttribute")]
		public static extern int GL_GetAttribute(SDL_GLAttr attr, out int32 value);

		[LinkName("SDL_GL_GetSwapInterval")]
		public static extern int GL_GetSwapInterval();

		/* window and context refer to an SDL_Window* and SDL_GLContext */
		[LinkName("SDL_GL_MakeCurrent")]
		public static extern int SDL_GL_MakeCurrent(Window* window, SDL_GLContext context);

		/* IntPtr refers to an SDL_Window* */
		[LinkName("SDL_GL_GetCurrentWindow")]
		public static extern Window* GL_GetCurrentWindow();

		/* IntPtr refers to an SDL_Context */
		[LinkName("SDL_GL_GetCurrentContext")]
		public static extern SDL_GLContext GL_GetCurrentContext();

		/* window refers to an SDL_Window*, This function is only available in SDL 2.0.1 */
		[LinkName("SDL_GL_GetDrawableSize")]
		public static extern void GL_GetDrawableSize(Window* window, out int32 w, out int32 h);

		[LinkName("SDL_GL_SetAttribute")]
		public static extern int32 GL_SetAttribute(SDL_GLAttr attr, int32 value);
		public static int32 GL_SetAttribute(SDL_GLAttr attr, SDL_GLProfile profile)
		{
			return GL_SetAttribute(attr, (int32)profile);
		}

		[LinkName("SDL_GL_SetSwapInterval")]
		public static extern int32 GL_SetSwapInterval(int32 interval);
		
		[LinkName("SDL_GL_SwapWindow")]
		public static extern void GL_SwapWindow(Window* window);

		
		[LinkName("SDL_GL_UnbindTexture")]
		public static extern int32 GL_UnbindTexture(Texture* texture);
		
		[LinkName("SDL_HideWindow")]
		public static extern void HideWindow(Window* window);

		[LinkName("SDL_IsScreenSaverEnabled")]
		public static extern Bool IsScreenSaverEnabled();

		[LinkName("SDL_MaximizeWindow")]
		public static extern void MaximizeWindow(Window* window);

		[LinkName("SDL_MinimizeWindow")]
		public static extern void MinimizeWindow(Window* window);

		[LinkName("SDL_RaiseWindow")]
		public static extern void RaiseWindow(Window* window);
		
		[LinkName("SDL_RestoreWindow")]
		public static extern void RestoreWindow(Window* window);
		
		[LinkName("SDL_SetWindowBrightness")]
		public static extern int32 SetWindowBrightness(Window* window, float brightness);

		[LinkName("SDL_SetWindowData")]
		public static extern void* SDL_SetWindowData(Window* window, char8* name, void* userdata);

		[LinkName("SDL_SetWindowDisplayMode")]
		public static extern int32 SetWindowDisplayMode(Window* window, ref SDL_DisplayMode mode);
		
		[LinkName("SDL_SetWindowFullscreen")]
		public static extern int32 SetWindowFullscreen(Window* window, uint32 flags);
		
		[LinkName("SDL_SetWindowGammaRamp")]
		public static extern int32 SetWindowGammaRamp(Window* window, uint16* red, uint16* green, uint16* blue);
		
		[LinkName("SDL_SetWindowGrab")]
		public static extern void SetWindowGrab(Window* window, Bool grabbed);

		[LinkName("SDL_SetWindowIcon")]
		public static extern void SetWindowIcon(Window* window, Surface* icon);
		
		[LinkName("SDL_SetWindowMaximumSize")]
		public static extern void SetWindowMaximumSize(Window* window, int32 max_w, int32 max_h);

		[LinkName("SDL_SetWindowMinimumSize")]
		public static extern void SetWindowMinimumSize(Window* window, int32 min_w, int32 min_h);
		
		[LinkName("SDL_SetWindowPosition")]
		public static extern void SetWindowPosition(Window* window, int32 x, int32 y);
	
		[LinkName("SDL_SetWindowSize")]
		public static extern void SetWindowSize(Window* window, int32 w, int32 h);
		
		[LinkName("SDL_SetWindowBordered")]
		public static extern void SetWindowBordered(Window* window, Bool bordered);
		
		[LinkName("SDL_GetWindowBordersSize")]
		public static extern int32 GetWindowBordersSize(Window* window, out int32 top, out int32 left, out int32 bottom, out int32 right);
		
		/* Available in 2.0.5 or higher */
		[LinkName("SDL_SetWindowResizable")]
		public static extern void SetWindowResizable(Window* window, Bool resizable);

		[LinkName("SDL_SetWindowTitle")]
		public static extern void SetWindowTitle(Window* window, char8* title);
		
		[LinkName("SDL_ShowWindow")]
		public static extern void ShowWindow(Window* window);
		
		[LinkName("SDL_UpdateWindowSurface")]
		public static extern int UpdateWindowSurface(Window* window);
		
		[LinkName("SDL_UpdateWindowSurfaceRects")]
		public static extern int UpdateWindowSurfaceRects(Window* window, Rect* rects, int numrects);

		[LinkName("SDL_VideoInit")]
		public static extern int32 VideoInit(char8* driver_name);

		[LinkName("SDL_VideoQuit")]
		public static extern void VideoQuit();

		/* window refers to an SDL_Window*, callback_data to a void* */
		/* Only available in 2.0.4 */
		[LinkName("SDL_SetWindowHitTest")]
		public static extern int SetWindowHitTest(Window* window, SDL_HitTest callback, void* callback_data);

		/* Only available in 2.0.4 */
		[LinkName("SDL_GetGrabbedWindow")]
		public static extern Window* GetGrabbedWindow();

		public enum BlendMode : uint32
		{
			None =	0x00000000,
			Blend =	0x00000001,
			Add =	0x00000002,
			Mod =	0x00000004,
			Invalid = 0x7FFFFFFF
		}

		public enum BlendOperation : uint32
		{
			Add		= 0x1,
			Subtract = 0x2,
			RevSubtract	= 0x3,
			Minimum	= 0x4,
			Maximum	= 0x5
		}

		public enum BlendFactor : uint32
		{
			Zero				= 0x1,
			One					= 0x2,
			Src_Color			= 0x3,
			OneMinusSrcColor	= 0x4,
			SrcAlpha	 		= 0x5,
			OneMinusSrcAlpha	= 0x6,
			DstColor	 		= 0x7,
			OneMinusDstColor	= 0x8,
			DstAlpha	 		= 0x9,
			OneMinusDstAlpha	= 0xA
		}

		/* Only available in 2.0.6 */
		[LinkName("SDL_ComposeCustomBlendMode")]
		public static extern BlendMode ComposeCustomBlendMode(
			BlendFactor srcColorFactor,
			BlendFactor dstColorFactor,
			BlendOperation colorOperation,
			BlendFactor srcAlphaFactor,
			BlendFactor dstAlphaFactor,
			BlendOperation alphaOperation
		);

		[LinkName("SDL_Vulkan_LoadLibrary")]
		public static extern int32 Vulkan_LoadLibrary(char8* path);

		/* Only available in 2.0.6 */
		[LinkName("SDL_Vulkan_GetVkGetInstanceProcAddr")]
		public static extern void* Vulkan_GetVkGetInstanceProcAddr();

		/* Only available in 2.0.6 */
		[LinkName("SDL_Vulkan_UnloadLibrary")]
		public static extern void Vulkan_UnloadLibrary();

		/* window refers to an SDL_Window*, pNames to a const char**.
		 * Only available in 2.0.6.
		 */
		[LinkName("SDL_Vulkan_GetInstanceExtensions")]
		public static extern Bool Vulkan_GetInstanceExtensions(Window* window, out uint32 pCount, char8** pNames);

		public struct VkInstance : int
		{
		}

		public struct VkSurfaceKHR : int
		{
		}

		/* window refers to an SDL_Window.
		 * instance refers to a VkInstance.
		 * surface refers to a VkSurfaceKHR.
		 * Only available in 2.0.6.
		 */
		[LinkName("SDL_Vulkan_CreateSurface")]
		public static extern Bool Vulkan_CreateSurface(Window* window, VkInstance instance, out VkSurfaceKHR surface);

		/* window refers to an SDL_Window*.
		 * Only available in 2.0.6.
		 */
		[LinkName("SDL_Vulkan_GetDrawableSize")]
		public static extern void Vulkan_GetDrawableSize(Window* window, out int32 w, out int32 h);

		public enum RendererFlags : uint32
		{
			Software =		0x00000001,
			Accelerated =	0x00000002,
			PresentVSync =	0x00000004,
			TargetTexture =	0x00000008
		}

		public enum RendererFlip : uint32
		{
			None =		0x00000000,
			Horizontal = 0x00000001,
			Vertical =	0x00000002
		}

		public enum TextureAccess : uint32
		{
			Static,
			Streaming,
			Target
		}

		public enum TextureModulate
		{
			None     =		0x00000000,
			Horizontal =	0x00000001,
			Vertical =		0x00000002
		}

		[CRepr]
		public struct RendererInfo
		{
			public char8* name; // const char*
			public uint32 flags;
			public uint32 num_texture_formats;
			public uint32[16] texture_formats;
			public int max_texture_width;
			public int max_texture_height;
		}

		/* IntPtr refers to an SDL_Renderer*, window to an SDL_Window* */
		[LinkName("SDL_CreateRenderer")]
		public static extern Renderer* CreateRenderer(
			Window* window,
			int32 index,
			RendererFlags flags
		);

		/* IntPtr refers to an SDL_Renderer*, surface to an SDL_Surface* */
		[LinkName("SDL_CreateSoftwareRenderer")]
		public static extern Surface* CreateSoftwareRenderer(Renderer* surface);

		/* IntPtr refers to an SDL_Texture*, renderer to an SDL_Renderer* */
		[LinkName("SDL_CreateTexture")]
		public static extern Texture* CreateTexture(Renderer* renderer, uint32 format, int32 access, int32 w, int32 h);

		[LinkName("SDL_CreateTextureFromSurface")]
		public static extern Texture* CreateTextureFromSurface(Renderer* renderer, Surface* surface);

		
		[LinkName("SDL_DestroyRenderer")]
		public static extern void DestroyRenderer(Renderer* renderer);

		
		[LinkName("SDL_DestroyTexture")]
		public static extern void DestroyTexture(Texture* texture);

		[LinkName("SDL_GetNumRenderDrivers")]
		public static extern int32 GetNumRenderDrivers();

		
		[LinkName("SDL_GetRenderDrawBlendMode")]
		public static extern int32 GetRenderDrawBlendMode(Renderer* renderer, out BlendMode blendMode);

		
		[LinkName("SDL_GetRenderDrawColor")]
		public static extern int GetRenderDrawColor(Renderer* renderer, out uint8 r, out uint8 g, out uint8 b, out uint8 a);

		[LinkName("SDL_GetRenderDriverInfo")]
		public static extern int GetRenderDriverInfo(int index, out RendererInfo info);

		[LinkName("SDL_GetRenderer")]
		public static extern Renderer* GetRenderer(Window* window);

		[LinkName("SDL_GetRendererInfo")]
		public static extern int32 GetRendererInfo(Renderer* renderer, out RendererInfo info);

		[LinkName("SDL_GetRendererOutputSize")]
		public static extern int32 GetRendererOutputSize(Renderer* renderer, out int w, out int h);

		[LinkName("SDL_GetTextureAlphaMod")]
		public static extern int32 GetTextureAlphaMod(Texture* texture, out uint8 alpha);

		[LinkName("SDL_GetTextureBlendMode")]
		public static extern int32 GetTextureBlendMode(Texture* texture, out BlendMode blendMode);

		[LinkName("SDL_GetTextureColorMod")]
		public static extern int32 GetTextureColorMod(Texture* texture, out uint8 r, out uint8 g, out uint8 b);

		/* texture refers to an SDL_Texture*, pixels to a void* */
		[LinkName("SDL_LockTexture")]
		public static extern int32 LockTexture(
			Texture* texture,
			Rect* rect,
			out void* pixels,
			out int32 pitch
		);

		
		[LinkName("SDL_QueryTexture")]
		public static extern int QueryTexture(
			Texture* texture,
			out uint32 format,
			out int32 access,
			out int32 w,
			out int32 h
		);

		
		[LinkName("SDL_RenderClear")]
		public static extern int RenderClear(Renderer* renderer);

		[LinkName("SDL_RenderCopy")]
		public static extern int RenderCopy(
			Renderer* renderer,
			Texture* texture,
			Rect* srcrect,
			Rect* dstrect
		);

		[LinkName("SDL_RenderCopyEx")]
		public static extern int RenderCopyEx(
			Renderer* renderer,
			Texture* texture,
			Rect* srcrect,
			Rect* dstrect,
			double angle,
			Point* center,
			RendererFlip flip
		);

		[LinkName("SDL_RenderDrawLine")]
		public static extern int RenderDrawLine(
			Renderer* renderer,
			int32 x1,
			int32 y1,
			int32 x2,
			int32 y2
		);
		
		[LinkName("SDL_RenderDrawLines")]
		public static extern int RenderDrawLines(
			Renderer* renderer,
			Point* points,
			int32 count
		);
	
		[LinkName("SDL_RenderDrawPoint")]
		public static extern int RenderDrawPoint(
			Renderer* renderer,
			int32 x,
			int32 y
		);
		
		[LinkName("SDL_RenderDrawPoints")]
		public static extern int RenderDrawPoints(
			Renderer* renderer,
			Point** points,
			int32 count
		);
	
		[LinkName("SDL_RenderDrawRect")]
		public static extern int RenderDrawRect(
			Renderer* renderer,
			Rect* rect
		);
		
		[LinkName("SDL_RenderDrawRects")]
		public static extern int RenderDrawRects(
			Renderer* renderer,
			Rect** rects,
			int32 count
		);

		[LinkName("SDL_RenderFillRect")]
		public static extern int RenderFillRect(
			Renderer* renderer,
			Rect* rect
		);
		
		[LinkName("SDL_RenderFillRects")]
		public static extern int RenderFillRects(
			Renderer* renderer,
			Rect** rects,
			int32 count
		);
		
		[LinkName("SDL_RenderGetClipRect")]
		public static extern void RenderGetClipRect(
			Renderer* renderer,
			out Rect rect
		);
		
		[LinkName("SDL_RenderGetLogicalSize")]
		public static extern void RenderGetLogicalSize(
			Renderer* renderer,
			out int32 w,
			out int32 h
		);

		
		[LinkName("SDL_RenderGetScale")]
		public static extern void RenderGetScale(
			Renderer* renderer,
			out float scaleX,
			out float scaleY
		);
		
		[LinkName("SDL_RenderGetViewport")]
		public static extern int RenderGetViewport(
			Renderer* renderer,
			out Rect rect
		);
		
		[LinkName("SDL_RenderPresent")]
		public static extern void RenderPresent(Renderer* renderer);
		
		[LinkName("SDL_RenderReadPixels")]
		public static extern int RenderReadPixels(
			Renderer* renderer,
			Rect* rect,
			uint32 format,
			void* pixels,
			int32 pitch
		);
		
		[LinkName("SDL_RenderSetClipRect")]
		public static extern int RenderSetClipRect(
			Renderer* renderer,
			Rect* rect
		);
		
		[LinkName("SDL_RenderSetLogicalSize")]
		public static extern int RenderSetLogicalSize(
			Renderer* renderer,
			int32 w,
			int32 h
		);
		
		[LinkName("SDL_RenderSetScale")]
		public static extern int RenderSetScale(
			Renderer* renderer,
			float scaleX,
			float scaleY
		);

		/* Available in 2.0.5 or higher */
		[LinkName("SDL_RenderSetIntegerScale")]
		public static extern int RenderSetIntegerScale(
			Renderer* renderer,
			Bool enable
		);

		
		[LinkName("SDL_RenderSetViewport")]
		public static extern int RenderSetViewport(
			Renderer* renderer,
			Rect* rect
		);

		
		[LinkName("SDL_SetRenderDrawBlendMode")]
		public static extern int SetRenderDrawBlendMode(
			Renderer* renderer,
			BlendMode blendMode
		);

		
		[LinkName("SDL_SetRenderDrawColor")]
		public static extern int SetRenderDrawColor(
			Renderer* renderer,
			uint8 r,
			uint8 g,
			uint8 b,
			uint8 a
		);

		[LinkName("SDL_SetRenderTarget")]
		public static extern int SetRenderTarget(
			Renderer* renderer,
			Texture* texture
		);
		
		[LinkName("SDL_SetTextureAlphaMod")]
		public static extern int SetTextureAlphaMod(
			Texture* texture,
			uint8 alpha
		);
		
		[LinkName("SDL_SetTextureBlendMode")]
		public static extern int SetTextureBlendMode(
			Texture* texture,
			BlendMode blendMode
		);
		
		[LinkName("SDL_SetTextureColorMod")]
		public static extern int SetTextureColorMod(
			Texture* texture,
			uint8 r,
			uint8 g,
			uint8 b
		);
		
		[LinkName("SDL_UnlockTexture")]
		public static extern void UnlockTexture(Texture* texture);
		
		[LinkName("SDL_UpdateTexture")]
		public static extern int UpdateTexture(
			Texture* texture,
			Rect* rect,
			void* pixels,
			int pitch
		);
		
		[LinkName("SDL_UpdateTexture")]
		public static extern int UpdateTexture(
			Texture* texture,
			Rect* rect,
			void* pixels,
			int32 pitch
		);

		/* Available in 2.0.1 or higher */
		[LinkName("SDL_UpdateYUVTexture")]
		public static extern int UpdateYUVTexture(
			Texture* texture,
			Rect* rect,
			uint8* yPlane,
			int32 yPitch,
			uint8* uPlane,
			int32 uPitch,
			uint8* vPlane,
			int32 vPitch
		);
		
		[LinkName("SDL_RenderTargetSupported")]
		public static extern Bool RenderTargetSupported(
			Renderer* renderer
		);

		[LinkName("SDL_GetRenderTarget")]
		public static extern Texture* GetRenderTarget(Renderer* renderer);
		
		/* Available in 2.0.8 or higher */
		[LinkName("SDL_RenderGetMetalLayer")]
		public static extern void* RenderGetMetalLayer(
			Renderer* renderer
		);
		
		/* Available in 2.0.8 or higher */
		[LinkName("SDL_RenderGetMetalCommandEncoder")]
		public static extern void* RenderGetMetalCommandEncoder(
			Renderer* renderer
		);

		/* Only available in 2.0.4 */
		[LinkName("SDL_RenderIsClipEnabled")]
		public static extern Bool RenderIsClipEnabled(Renderer* renderer);

		public static uint32 DEFINE_PIXELFOURCC(uint8 A, uint8 B, uint8 C, uint8 D)
		{
			return FOURCC(A, B, C, D);
		}

		public static uint32 DEFINE_PIXELFORMAT(
			SDL_PIXELTYPE_ENUM type,
			SDL_PIXELORDER_ENUM order,
			SDL_PACKEDLAYOUT_ENUM layout,
			uint8 bits,
			uint8 bytes
		) {
			return (uint32) (
				(1 << 28) |
				(((uint32) type) << 24) |
				(((uint32) order) << 20) |
				(((uint32) layout) << 16) |
				((uint32)bits << 8) |
				(bytes)
			);
		}

		public static uint8 SDL_PIXELFLAG(uint32 X)
		{
			return (uint8) ((X >> 28) & 0x0F);
		}

		public static uint8 SDL_PIXELTYPE(uint32 X)
		{
			return (uint8) ((X >> 24) & 0x0F);
		}

		public static uint8 SDL_PIXELORDER(uint32 X)
		{
			return (uint8) ((X >> 20) & 0x0F);
		}

		public static uint8 SDL_PIXELLAYOUT(uint32 X)
		{
			return (uint8) ((X >> 16) & 0x0F);
		}

		public static uint8 SDL_BITSPERPIXEL(uint32 X)
		{
			return (uint8) ((X >> 8) & 0xFF);
		}

		public static uint8 SDL_BYTESPERPIXEL(uint32 X)
		{
			if (ISPIXELFORMAT_FOURCC(X))
			{
				if (	(X == PIXELFORMAT_YUY2) ||
						(X == PIXELFORMAT_UYVY) ||
						(X == PIXELFORMAT_YVYU)	)
				{
					return 2;
				}
				return 1;
			}
			return (uint8) (X & 0xFF);
		}

		public static Bool ISPIXELFORMAT_INDEXED(uint32 format)
		{
			if (ISPIXELFORMAT_FOURCC(format))
			{
				return false;
			}
			SDL_PIXELTYPE_ENUM pType =
					(SDL_PIXELTYPE_ENUM) SDL_PIXELTYPE(format);
			return (
				pType == .Index1 ||
				pType == .Index4 ||
				pType == .Index8
			);
		}

		public static Bool ISPIXELFORMAT_ALPHA(uint32 format)
		{
			if (ISPIXELFORMAT_FOURCC(format))
			{
				return false;
			}
			SDL_PIXELORDER_ENUM pOrder =
					(SDL_PIXELORDER_ENUM) SDL_PIXELORDER(format);
			return (
				pOrder == .PackedOrderARGB ||
				pOrder == .PackedOrderRGBA ||
				pOrder == .PackedOrderABGR ||
				pOrder == .PackedOrderBGRA
			);
		}

		public static Bool ISPIXELFORMAT_FOURCC(uint32 format)
		{
			return (format == 0) && (SDL_PIXELFLAG(format) != 1);
		}

		public enum SDL_PIXELTYPE_ENUM : uint32
		{
			Unknown,
			Index1,
			Index4,
			Index8,
			Packed8,
			Packed16,
			Packed32,
			ArrayU8,
			ArrayU16,
			ArrayU32,
			ArrayF16,
			ArrayF32
		}

		[AllowDuplicates]
		public enum SDL_PIXELORDER_ENUM : uint32
		{
			/* BITMAPORDER */
			BitmapOrderNONE,
			BitmapOrder4321,
			BitmapOrder1234,
			/* PACKEDORDER */
			PackedOrderNONE = 0,
			PackedOrderXRGB,
			PackedOrderRGBX,
			PackedOrderARGB,
			PackedOrderRGBA,
			PackedOrderXBGR,
			PackedOrderBGRX,
			PackedOrderABGR,
			PackedOrderBGRA,
			/* ARRAYORDER */
			ArrayOrderNONE = 0,
			ArrayOrderRGB,
			ArrayOrderRGBA,
			ArrayOrderARGB,
			ArrayOrderBGR,
			ArrayOrderBGRA,
			ArrayOrderABGR
		}

		public enum SDL_PACKEDLAYOUT_ENUM : uint32
		{
			LayoutNONE,
			Layout332,
			Layout4444,
			Layout1555,
			Layout5551,
			Layout565,
			Layout8888,
			Layout2101010,
			Layout1010102
		}

		public static readonly uint32 PIXELFORMAT_UNKNOWN = 0;
		public static readonly uint32 PIXELFORMAT_INDEX1LSB =
			DEFINE_PIXELFORMAT(
				.Index1,
				.BitmapOrder4321,
				.LayoutNONE,
				1, 0
			);
		public static readonly uint32 PIXELFORMAT_INDEX1MSB =
			DEFINE_PIXELFORMAT(
				.Index1,
				.BitmapOrder1234,
				.LayoutNONE,
				1, 0
			);
		public static readonly uint32 PIXELFORMAT_INDEX4LSB =
			DEFINE_PIXELFORMAT(
				.Index4,
				.BitmapOrder4321,
				.LayoutNONE,
				4, 0
			);
		public static readonly uint32 PIXELFORMAT_INDEX4MSB =
			DEFINE_PIXELFORMAT(
				.Index4,
				.BitmapOrder1234,
				.LayoutNONE,
				4, 0
			);
		public static readonly uint32 PIXELFORMAT_INDEX8 =
			DEFINE_PIXELFORMAT(
				.Index8,
				.ArrayOrderNONE,
				.LayoutNONE,
				8, 1
			);
		public static readonly uint32 PIXELFORMAT_RGB332 =
			DEFINE_PIXELFORMAT(
				.Packed8,
				.PackedOrderXRGB,
				.Layout332,
				8, 1
			);
		public static readonly uint32 PIXELFORMAT_RGB444 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderXRGB,
				.Layout4444,
				12, 2
			);
		public static readonly uint32 PIXELFORMAT_RGB555 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderXRGB,
				.Layout1555,
				15, 2
			);
		public static readonly uint32 PIXELFORMAT_BGR555 =
			DEFINE_PIXELFORMAT(
				.Index1,
				.BitmapOrder4321,
				.Layout1555,
				15, 2
			);
		public static readonly uint32 PIXELFORMAT_ARGB4444 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderARGB,
				.Layout4444,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_RGBA4444 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderRGBA,
				.Layout4444,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_ABGR4444 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderABGR,
				.Layout4444,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_BGRA4444 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderBGRA,
				.Layout4444,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_ARGB1555 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderARGB,
				.Layout1555,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_RGBA5551 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderRGBA,
				.Layout5551,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_ABGR1555 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderABGR,
				.Layout1555,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_BGRA5551 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderBGRA,
				.Layout5551,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_RGB565 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderXRGB,
				.Layout565,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_BGR565 =
			DEFINE_PIXELFORMAT(
				.Packed16,
				.PackedOrderXBGR,
				.Layout565,
				16, 2
			);
		public static readonly uint32 PIXELFORMAT_RGB24 =
			DEFINE_PIXELFORMAT(
				.ArrayU8,
				.ArrayOrderRGB,
				.LayoutNONE,
				24, 3
			);
		public static readonly uint32 PIXELFORMAT_BGR24 =
			DEFINE_PIXELFORMAT(
				.ArrayU8,
				.ArrayOrderBGR,
				.LayoutNONE,
				24, 3
			);
		public static readonly uint32 PIXELFORMAT_RGB888 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderXRGB,
				.Layout8888,
				24, 4
			);
		public static readonly uint32 PIXELFORMAT_RGBX8888 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderRGBX,
				.Layout8888,
				24, 4
			);
		public static readonly uint32 PIXELFORMAT_BGR888 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderXBGR,
				.Layout8888,
				24, 4
			);
		public static readonly uint32 PIXELFORMAT_BGRX8888 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderBGRX,
				.Layout8888,
				24, 4
			);
		public static readonly uint32 PIXELFORMAT_ARGB8888 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderARGB,
				.Layout8888,
				32, 4
			);
		public static readonly uint32 PIXELFORMAT_RGBA8888 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderRGBA,
				.Layout8888,
				32, 4
			);
		public static readonly uint32 PIXELFORMAT_ABGR8888 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderABGR,
				.Layout8888,
				32, 4
			);
		public static readonly uint32 PIXELFORMAT_BGRA8888 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderBGRA,
				.Layout8888,
				32, 4
			);
		public static readonly uint32 PIXELFORMAT_ARGB2101010 =
			DEFINE_PIXELFORMAT(
				.Packed32,
				.PackedOrderARGB,
				.Layout2101010,
				32, 4
			);
		public static readonly uint32 PIXELFORMAT_YV12 =
			DEFINE_PIXELFOURCC(
				(uint8) 'Y', (uint8) 'V', (uint8) '1', (uint8) '2'
			);
		public static readonly uint32 PIXELFORMAT_IYUV =
			DEFINE_PIXELFOURCC(
				(uint8) 'I', (uint8) 'Y', (uint8) 'U', (uint8) 'V'
			);
		public static readonly uint32 PIXELFORMAT_YUY2 =
			DEFINE_PIXELFOURCC(
				(uint8) 'Y', (uint8) 'U', (uint8) 'Y', (uint8) '2'
			);
		public static readonly uint32 PIXELFORMAT_UYVY =
			DEFINE_PIXELFOURCC(
				(uint8) 'U', (uint8) 'Y', (uint8) 'V', (uint8) 'Y'
			);
		public static readonly uint32 PIXELFORMAT_YVYU =
			DEFINE_PIXELFOURCC(
				(uint8) 'Y', (uint8) 'V', (uint8) 'Y', (uint8) 'U'
			);

		[CRepr]
		public struct Color
		{
			public uint8 r;
			public uint8 g;
			public uint8 b;
			public uint8 a;

			public this()
			{
				this = default;
			}

			public this(uint8 r, uint8 g, uint8 b, uint8 a)
			{
				this.r = r;
				this.g = g;
				this.b = b;
				this.a = a;
			}
		}

		[CRepr]
		public struct Palette
		{
			public int32 ncolors;
			public void* colors;
			public int32 version;
			public int32 refcount;
		}

		[CRepr]
		public struct PixelFormat
		{
			public uint32 format;
			public Palette* palette; // SDL_Palette*
			public uint8 bitsPerPixel;
			public uint8 bytesPerPixel;
			public uint32 Rmask;
			public uint32 Gmask;
			public uint32 Bmask;
			public uint32 Amask;
			public uint8 rloss;
			public uint8 gloss;
			public uint8 bloss;
			public uint8 Aloss;
			public uint8 rshift;
			public uint8 gshift;
			public uint8 bshift;
			public uint8 Ashift;
			public int32 refcount;
			public PixelFormat* next; // SDL_PixelFormat*
		}

		/* IntPtr refers to an SDL_PixelFormat* */
		[LinkName("SDL_AllocFormat")]
		public static extern PixelFormat* AllocFormat(uint32 pixel_format);

		/* IntPtr refers to an SDL_Palette* */
		[LinkName("SDL_AllocPalette")]
		public static extern Palette* AllocPalette(int ncolors);

		[LinkName("SDL_CalculateGammaRamp")]
		public static extern void CalculateGammaRamp(
			float gamma,
			uint16* ramp
		);

		/* format refers to an SDL_PixelFormat* */
		[LinkName("SDL_FreeFormat")]
		public static extern void FreeFormat(PixelFormat* format);

		/* palette refers to an SDL_Palette* */
		[LinkName("SDL_FreePalette")]
		public static extern void FreePalette(Palette* palette);

		[LinkName("SDL_GetPixelFormatName")]
		public static extern char8* GetPixelFormatName(uint32 format);

		/* format refers to an SDL_PixelFormat* */
		[LinkName("SDL_GetRGB")]
		public static extern void GetRGB(
			uint32 pixel,
			PixelFormat* format,
			out uint8 r,
			out uint8 g,
			out uint8 b
		);

		/* format refers to an SDL_PixelFormat* */
		[LinkName("SDL_GetRGBA")]
		public static extern void GetRGBA(
			uint32 pixel,
			PixelFormat* format,
			out uint8 r,
			out uint8 g,
			out uint8 b,
			out uint8 a
		);

		/* format refers to an SDL_PixelFormat* */
		[LinkName("SDL_MapRGB")]
		public static extern uint32 MapRGB(
			PixelFormat* format,
			uint8 r,
			uint8 g,
			uint8 b
		);

		/* format refers to an SDL_PixelFormat* */
		[LinkName("SDL_MapRGBA")]
		public static extern uint32 MapRGBA(
			PixelFormat* format,
			uint8 r,
			uint8 g,
			uint8 b,
			uint8 a
		);

		[LinkName("SDL_MasksToPixelFormatEnum")]
		public static extern uint32 MasksToPixelFormatEnum(
			int32 bpp,
			uint32 Rmask,
			uint32 Gmask,
			uint32 Bmask,
			uint32 Amask
		);

		[LinkName("SDL_PixelFormatEnumToMasks")]
		public static extern Bool PixelFormatEnumToMasks(
			uint32 format,
			out int32 bpp,
			out uint32 Rmask,
			out uint32 Gmask,
			out uint32 Bmask,
			out uint32 Amask
		);

		/* palette refers to an SDL_Palette* */
		[LinkName("SDL_SetPaletteColors")]
		public static extern int SetPaletteColors(
			Palette* palette,
			Color* colors,
			int32 firstcolor,
			int32 ncolors
		);

		/* format and palette refer to an SDL_PixelFormat* and SDL_Palette* */
		[LinkName("SDL_SetPixelFormatPalette")]
		public static extern int SetPixelFormatPalette(
			PixelFormat* format,
			Palette* palette
		);

		[CRepr]
		public struct Point
		{
			public int32 x;
			public int32 y;

			public this()
			{
				this = default;
			}

			public this(int32 x, int32 y)
			{
				this.x = x;
				this.y = y;
			}
		}

		[CRepr]
		public struct Rect
		{
			public int32 x;
			public int32 y;
			public int32 w;
			public int32 h;

			public this()
			{
				this = default;
			}

			public this(int32 x, int32 y, int32 w, int32 h)
			{
				this.x = x;
				this.y = y;
				this.w = w;
				this.h = h;
			}

			public bool Contains(int32 x, int32 y)
			{
				return (x >= this.x) && (x < this.x + this.w) &&
					(y >= this.y) && (y < this.y + this.h);
			}
		}

		/* Only available in 2.0.4 */
		public static Bool PointInRect(Point* p, Rect* r)
		{
			return (	(p.x >= r.x) &&
					(p.x < (r.x + r.w)) &&
					(p.y >= r.y) &&
					(p.y < (r.y + r.h))	) ?
				Bool.True :
				Bool.False;
		}

		[LinkName("SDL_EnclosePoints")]
		public static extern Bool EnclosePoints(
			Point* points,
			int32 count,
			Rect* clip,
			out Rect result
		);

		[LinkName("SDL_HasIntersection")]
		public static extern Bool HasIntersection(
			Rect* A,
			Rect* B
		);

		[LinkName("SDL_IntersectRect")]
		public static extern Bool IntersectRect(
			Rect* A,
			Rect* B,
			out Rect result
		);

		[LinkName("SDL_IntersectRectAndLine")]
		public static extern Bool IntersectRectAndLine(
			Rect* rect,
			int32* X1,
			int32* Y1,
			int32* X2,
			int32* Y2
		);

		public static Bool RectEmpty(Rect* r)
		{
			return ((r.w <= 0) || (r.h <= 0)) ?
				Bool.True :
				Bool.False;
		}

		public static Bool RectEquals(
			Rect* a,
			Rect* b
		) {
			return (	(a.x == b.x) &&
					(a.y == b.y) &&
					(a.w == b.w) &&
					(a.h == b.h)	) ?
				Bool.True :
				Bool.False;
		}

		[LinkName("SDL_UnionRect")]
		public static extern void UnionRect(
			Rect* A,
			Rect* B,
			out Rect result
		);

		public const uint32 SDL_SWSURFACE =	0x00000000;
		public const uint32 SDL_PREALLOC =	0x00000001;
		public const uint32 SDL_RLEACCEL =	0x00000002;
		public const uint32 SDL_DONTFREE =	0x00000004;

		public struct SDL_BlitMap;

		[CRepr]
		public struct Surface
		{
			public uint32 flags;
			public PixelFormat* format; // SDL_PixelFormat*
			public int32 w;
			public int32 h;
			public int32 pitch;
			public void* pixels; // void*
			public void* userdata; // void*
			public int32 locked;
			public void* lock_data; // void*
			public Rect clip_rect;
			public SDL_BlitMap* map; // SDL_BlitMap*
			public int refcount;
		}

		/* surface refers to an SDL_Surface* */
		/*public static Bool MUSTLOCK(SDL_Surface* surface)
		{
			SDL_Surface sur;
			sur = (SDL_Surface) Marshal.PtrToStructure(
				surface,
				typeof(SDL_Surface)
			);
			return (sur.flags & SDL_RLEACCEL) != 0;
		}*/

		/* src and dst refer to an SDL_Surface* */
		[LinkName("SDL_UpperBlit")]
		public static extern int SDL_BlitSurface(
			Surface* src,
			Rect* srcrect,
			Surface* dst,
			Rect* dstrect
		);

		/* src and dst refer to an SDL_Surface* */
		[LinkName("SDL_UpperBlitScaled")]
		public static extern int SDL_BlitScaled(
			Surface* src,
			Rect* srcrect,
			Surface* dst,
			Rect* dstrect
		);

		/* src and dst are void* pointers */
		[LinkName("SDL_ConvertPixels")]
		public static extern int ConvertPixels(
			int32 width,
			int32 height,
			uint32 src_format,
			void* src,
			int32 src_pitch,
			uint32 dst_format,
			void* dst,
			int32 dst_pitch
		);

		/* IntPtr refers to an SDL_Surface*
		 * src refers to an SDL_Surface*
		 * fmt refers to an SDL_PixelFormat*
		 */
		[LinkName("SDL_ConvertSurface")]
		public static extern Surface* ConvertSurface(
			Surface* src,
			PixelFormat* fmt,
			uint32 flags
		);

		/* IntPtr refers to an SDL_Surface*, src to an SDL_Surface* */
		[LinkName("SDL_ConvertSurfaceFormat")]
		public static extern Surface* ConvertSurfaceFormat(
			Surface* src,
			uint32 pixel_format,
			uint32 flags
		);

		/* IntPtr refers to an SDL_Surface* */
		[LinkName("SDL_CreateRGBSurface")]
		public static extern Surface* CreateRGBSurface(
			uint32 flags,
			int32 width,
			int32 height,
			int32 depth,
			uint32 Rmask,
			uint32 Gmask,
			uint32 Bmask,
			uint32 Amask
		);

		/* IntPtr refers to an SDL_Surface*, pixels to a void* */
		[LinkName("SDL_CreateRGBSurfaceFrom")]
		public static extern Surface* CreateRGBSurfaceFrom(
			void* pixels,
			int32 width,
			int32 height,
			int32 depth,
			int32 pitch,
			uint32 Rmask,
			uint32 Gmask,
			uint32 Bmask,
			uint32 Amask
		);

		/* IntPtr refers to an SDL_Surface* */
		/* Available in 2.0.5 or higher */
		[LinkName("SDL_CreateRGBSurfaceWithFormat")]
		public static extern Surface* CreateRGBSurfaceWithFormat(
			uint32 flags,
			int32 width,
			int32 height,
			int32 depth,
			uint32 format
		);

		/* IntPtr refers to an SDL_Surface*, pixels to a void* */
		/* Available in 2.0.5 or higher */
		[LinkName("SDL_CreateRGBSurfaceWithFormatFrom")]
		public static extern Surface* CreateRGBSurfaceWithFormatFrom(
			void* pixels,
			int32 width,
			int32 height,
			int32 depth,
			int32 pitch,
			uint32 format
		);

		/* dst refers to an SDL_Surface* */
		[LinkName("SDL_FillRect")]
		public static extern int FillRect(
			Surface* dst,
			Rect* rect,
			uint32 color
		);

		/* dst refers to an SDL_Surface* */
		[LinkName("SDL_FillRects")]
		public static extern int FillRects(
			Surface* dst,
			Rect* rects,
			int32 count,
			uint32 color
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_FreeSurface")]
		public static extern void FreeSurface(Surface* surface);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_GetClipRect")]
		public static extern void GetClipRect(
			Surface* surface,
			out Rect rect
		);

		/* surface refers to an SDL_Surface*.
		 * This function is only available in 2.0.9 or higher.
		 */
		[LinkName("SDL_HasColorKey")]
		public static extern Bool HasColorKey(Surface* surface);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_GetColorKey")]
		public static extern int GetColorKey(
			Surface* surface,
			out uint32 key
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_GetSurfaceAlphaMod")]
		public static extern int GetSurfaceAlphaMod(
			Surface* surface,
			out uint8 alpha
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_GetSurfaceBlendMode")]
		public static extern int GetSurfaceBlendMode(
			Surface* surface,
			out BlendMode blendMode
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_GetSurfaceColorMod")]
		public static extern int GetSurfaceColorMod(
			Surface* surface,
			out uint8 r,
			out uint8 g,
			out uint8 b
		);

		[LinkName("SDL_LoadBMP_RW")]
		private static extern Surface* LoadBMP_RW(
			RWOps* src,
			int freesrc
		);

		public static Surface* SDL_LoadBMP(char8* file)
		{
			RWOps* rwops = RWFromFile(file, "rb");
			return LoadBMP_RW(rwops, 1);
		}

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_LockSurface")]
		public static extern int LockSurface(Surface* surface);

		/* src and dst refer to an SDL_Surface* */
		[LinkName("SDL_LowerBlit")]
		public static extern int LowerBlit(
			Surface* src,
			Rect* srcrect,
			Surface* dst,
			Rect* dstrect
		);

		/* src and dst refer to an SDL_Surface* */
		[LinkName("SDL_LowerBlitScaled")]
		public static extern int LowerBlitScaled(
			Surface* src,
			Rect* srcrect,
			Surface* dst,
			Rect* dstrect
		);

		/* These are for SDL_SaveBMP, which is a macro in the SDL headers. */
		/* IntPtr refers to an SDL_Surface* */
		/* THIS IS AN RWops FUNCTION! */
		[LinkName("SDL_SaveBMP_RW")]
		private static extern int SaveBMP_RW(
			Surface* surface,
			RWOps* dst,
			int freedst
		);

		public static int SDL_SaveBMP(Surface* surface, char8* file)
		{
			RWOps* rwops = RWFromFile(file, "wb");
			return SaveBMP_RW(surface, rwops, 1);
		}

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_SetClipRect")]
		public static extern Bool SetClipRect(
			Surface* surface,
			Rect* rect
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_SetColorKey")]
		public static extern int SetColorKey(
			Surface* surface,
			int32 flag,
			uint32 key
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_SetSurfaceAlphaMod")]
		public static extern int SetSurfaceAlphaMod(
			Surface* surface,
			uint8 alpha
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_SetSurfaceBlendMode")]
		public static extern int SetSurfaceBlendMode(
			Surface* surface,
			BlendMode blendMode
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_SetSurfaceColorMod")]
		public static extern int SetSurfaceColorMod(
			Surface* surface,
			uint8 r,
			uint8 g,
			uint8 b
		);

		/* surface refers to an SDL_Surface*, palette to an SDL_Palette* */
		[LinkName("SDL_SetSurfacePalette")]
		public static extern int SetSurfacePalette(
			Surface* surface,
			Palette* palette
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_SetSurfaceRLE")]
		public static extern int SetSurfaceRLE(
			Surface* surface,
			int32 flag
		);

		/* src and dst refer to an SDL_Surface* */
		[LinkName("SDL_SoftStretch")]
		public static extern int SoftStretch(
			Surface* src,
			Rect* srcrect,
			Surface* dst,
			Rect* dstrect
		);

		/* surface refers to an SDL_Surface* */
		[LinkName("SDL_UnlockSurface")]
		public static extern void UnlockSurface(Surface* surface);

		/* src and dst refer to an SDL_Surface* */
		[LinkName("SDL_UpperBlit")]
		public static extern int UpperBlit(
			Surface* src,
			Rect* srcrect,
			Surface* dst,
			Rect* dstrect
		);

		/* src and dst refer to an SDL_Surface* */
		[LinkName("SDL_UpperBlitScaled")]
		public static extern int UpperBlitScaled(
			Surface* src,
			Rect* srcrect,
			Surface* dst,
			Rect* dstrect
		);

		/* surface and IntPtr refer to an SDL_Surface* */
		[LinkName("SDL_DuplicateSurface")]
		public static extern Surface* DuplicateSurface(Surface* surface);

		[LinkName("SDL_HasClipboardText")]
		public static extern Bool HasClipboardText();

		[LinkName("SDL_GetClipboardText")]
		public static extern char8* GetClipboardText();

		[LinkName("SDL_SetClipboardText")]
		public static extern int32 SetClipboardText(char8* text
		);

		/* General keyboard/mouse state definitions. */
		public const uint8 SDL_PRESSED =		1;
		public const uint8 SDL_RELEASED =	0;

		/* Default size is according to SDL2 default. */
		public const int32 TEXTEDITINGEVENT_TEXT_SIZE = 32;
		public const int32 TEXTINPUTEVENT_TEXT_SIZE = 32;

		/* The types of events that can be delivered. */
		public enum EventType : uint32
		{
			FIRSTEVENT =		0,

			/* application events */
			Quit = 			0x100,

			/* ios/android/winrt app events */
			AppTerminating,
			AppLowMemory,
			AppWillEnterBackground,
			AppDidEnterBackground,
			AppWillEnterForeground,
			AppDidEnterForeground,

			/* display events */
			/* only available in sdl 2.0.9 or higher */
			DisplayEvent =		0x150,

			/* window events */
			WindowEvent = 		0x200,
			SysWMEvent,

			/* keyboard events */
			KeyDown = 			0x300,
			KeyUp,
			TextEditing,
			TextInput,
			KeyMapChanged,

			/* mouse events */
			MouseMotion = 		0x400,
			MouseButtonDown,
			MouseButtonUp,
			MouseWheel,

			/* joystick events */
			JoyAxisMotion =		0x600,
			JoyBallMotion,
			JoyHatMotion,
			JoyButtonDown,
			JoyButtonUp,
			JoyDeviceAdded,
			JoyDeviceRemoved,

			/* game controller events */
			ControllerAxismotion = 	0x650,
			ControllerButtondown,
			ControllerButtonup,
			ControllerDeviceadded,
			ControllerDeviceremoved,
			ControllerDeviceremapped,

			/* touch events */
			FingerDown = 		0x700,
			FingerUp,
			FingerMotion,

			/* gesture events */
			DollarGesture =		0x800,
			DollarRecord,
			MultiGesture,

			/* clipboard events */
			ClipboardUpdate =		0x900,

			/* drag and drop events */
			DropFile =			0x1000,
			/* only available in 2.0.4 or higher */
			DropText,
			DropBegin,
			DropComplete,

			/* audio hotplug events */
			/* only available in sdl 2.0.4 or higher */
			AudioDeviceAdded =		0x1100,
			AudioDeviceRemoved,

			/* sensor events */
			/* only available in sdl 2.0.9 or higher */
			SensorUpdate =		0x1200,

			/* render events */
			/* only available in sdl 2.0.2 or higher */
			RenderTargetsReset =	0x2000,
			/* only available in sdl 2.0.4 or higher */
			RenderDeviceReset,

			/* Events USEREVENT through LASTEVENT are for
			 * your use, and should be allocated with
			 * RegisterEvents()
			 */
			USEREVENT =			0x8000,

			/* The last event, used for bouding arrays. */
			LASTEVENT =			0xFFFF
		}

		/* Only available in 2.0.4 or higher */
		public enum MouseWheelDirection : uint32
		{
			Normal,
			Flipped
		}

		/* Fields shared by every event */
		[CRepr]
		public struct GenericEvent
		{
			public EventType type;
			public uint32 timestamp;
		}


		[CRepr]
		public struct DisplayEvent
		{
			public EventType type;
			public uint32 timestamp;
			public uint32 display;
			public DisplayEventID displayEvent; // event, lolC#
			private uint8 padding1;
			private uint8 padding2;
			private uint8 padding3;
			public int32 data1;
		}

		[CRepr]
		public struct WindowEvent
		{
			public EventType type;
			public uint32 timestamp;
			public uint32 windowID;
			public WindowEventID windowEvent; // event, lolC#
			private uint8 padding1;
			private uint8 padding2;
			private uint8 padding3;
			public int32 data1;
			public int32 data2;
		}

		[CRepr]
		public struct KeyboardEvent
		{
			public EventType type;
			public uint32 timestamp;
			public uint32 windowID;
			public uint8 state;
			public uint8 isRepeat; /* non-zero if this is a repeat */
			private uint8 padding2;
			private uint8 padding3;
			public KeySym keysym;
		}

		[CRepr]
		public struct TextEditingEvent
		{
			public EventType type;
			public uint32 timestamp;
			public uint32 windowID;
			public uint8[TEXTEDITINGEVENT_TEXT_SIZE] text;
			public int32 start;
			public int32 length;
		}

		[CRepr]
		public struct TextInputEvent
		{
			public EventType type;
			public uint32 timestamp;
			public uint32 windowID;
			public uint8[TEXTINPUTEVENT_TEXT_SIZE] text;
		}

		[CRepr]
		public struct MouseMotionEvent
		{
			public EventType type;
			public uint32 timestamp;
			public uint32 windowID;
			public uint32 which;
			public uint8 state; /* bitmask of buttons */
			private uint8 padding1;
			private uint8 padding2;
			private uint8 padding3;
			public int32 x;
			public int32 y;
			public int32 xrel;
			public int32 yrel;
		}

		[CRepr]
		public struct MouseButtonEvent
		{
			public EventType type;
			public uint32 timestamp;
			public uint32 windowID;
			public uint32 which;
			public uint8 button; /* button id */
			public uint8 state; /* SDL_PRESSED or SDL_RELEASED */
			public uint8 clicks; /* 1 for single-click, 2 for double-click, etc. */
			private uint8 padding1;
			public int32 x;
			public int32 y;
		}

		[CRepr]
		public struct MouseWheelEvent
		{
			public EventType type;
			public uint32 timestamp;
			public uint32 windowID;
			public uint32 which;
			public int32 x; /* amount scrolled horizontally */
			public int32 y; /* amount scrolled vertically */
			public uint32 direction; /* Set to one of the SDL_MOUSEWHEEL_* defines */
		}


		[CRepr]
		public struct JoyAxisEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which; /* SDL_JoystickID */
			public uint8 axis;
			private uint8 padding1;
			private uint8 padding2;
			private uint8 padding3;
			public Int16 axisValue; /* value, lolC# */
			public uint16 padding4;
		}

		[CRepr]
		public struct JoyBallEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which; /* SDL_JoystickID */
			public uint8 ball;
			private uint8 padding1;
			private uint8 padding2;
			private uint8 padding3;
			public Int16 xrel;
			public Int16 yrel;
		}

		[CRepr]
		public struct JoyHatEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which; /* SDL_JoystickID */
			public uint8 hat; /* index of the hat */
			public uint8 hatValue; /* value, lolC# */
			private uint8 padding1;
			private uint8 padding2;
		}

		[CRepr]
		public struct JoyButtonEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which; /* SDL_JoystickID */
			public uint8 button;
			public uint8 state; /* SDL_PRESSED or SDL_RELEASED */
			private uint8 padding1;
			private uint8 padding2;
		}

		[CRepr]
		public struct JoyDeviceEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which; /* SDL_JoystickID */
		}

		[CRepr]
		public struct ControllerAxisEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which; /* SDL_JoystickID */
			public uint8 axis;
			private uint8 padding1;
			private uint8 padding2;
			private uint8 padding3;
			public int16 axisValue; /* value, lolC# */
			private uint16 padding4;
		}

		[CRepr]
		public struct ControllerButtonEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which; /* SDL_JoystickID */
			public uint8 button;
			public uint8 state;
			private uint8 padding1;
			private uint8 padding2;
		}

		[CRepr]
		public struct ControllerDeviceEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which;	/* joystick id for ADDED,
						 * else instance id
						 */
		}

		[CRepr]
		public struct AudioDeviceEvent
		{
			public uint32 type;
			public uint32 timestamp;
			public uint32 which;
			public uint8 iscapture;
			private uint8 padding1;
			private uint8 padding2;
			private uint8 padding3;
		}

		[CRepr]
		public struct TouchFingerEvent
		{
			public uint32 type;
			public uint32 timestamp;
			public Int64 touchId; // SDL_TouchID
			public Int64 fingerId; // SDL_GestureID
			public float x;
			public float y;
			public float dx;
			public float dy;
			public float pressure;
		}

		[CRepr]
		public struct MultiGestureEvent
		{
			public uint32 type;
			public uint32 timestamp;
			public Int64 touchId; // SDL_TouchID
			public float dTheta;
			public float dDist;
			public float x;
			public float y;
			public uint16 numFingers;
			public uint16 padding;
		}

		[CRepr]
		public struct DollarGestureEvent
		{
			public uint32 type;
			public uint32 timestamp;
			public int64 touchId; // SDL_TouchID
			public int64 gestureId; // SDL_GestureID
			public uint32 numFingers;
			public float error;
			public float x;
			public float y;
		}

		[CRepr]
		public struct DropEvent
		{
			public EventType type;
			public uint32 timestamp;

			/* char* filename, to be freed.
			 * Access the variable EXACTLY ONCE like this:
			 * string s = SDL.UTF8_ToManaged(evt.drop.file, true);
			 */
			public char8* file;
			public uint32 windowID;
		}

		[CRepr]
		public struct SensorEvent
		{
			public EventType type;
			public uint32 timestamp;
			public int32 which;
			public float[6] data;
		}

		[CRepr]
		public struct QuitEvent
		{
			public EventType type;
			public uint32 timestamp;
		}

		[CRepr]
		public struct UserEvent
		{
			public uint32 type;
			public uint32 timestamp;
			public uint32 windowID;
			public int32 code;
			public void* data1; /* user-defined */
			public void* data2; /* user-defined */
		}

		public struct SDL_SysWMmsg;

		[CRepr]
		public struct SysWMEvent
		{
			public EventType type;
			public uint32 timestamp;
			public SDL_SysWMmsg* msg; /* SDL_SysWMmsg*, system-dependent*/
		}

		/* General event structure */
		// C# doesn't do unions, so we do this ugly thing. 
		[CRepr, Union]
		public struct Event
		{
			public EventType type;
			public DisplayEvent display;
			public WindowEvent window;
			public KeyboardEvent key;
			public TextEditingEvent edit;
			public TextInputEvent text;
			public MouseMotionEvent motion;
			public MouseButtonEvent button;
			public MouseWheelEvent wheel;
			public JoyAxisEvent jaxis;
			public JoyBallEvent jball;
			public JoyHatEvent jhat;
			public JoyButtonEvent jbutton;
			public JoyDeviceEvent jdevice;
			public ControllerAxisEvent caxis;
			public ControllerButtonEvent cbutton;
			public ControllerDeviceEvent cdevice;
			public AudioDeviceEvent adevice;
			public SensorEvent sensor;
			public QuitEvent quit;
			public UserEvent user;
			public SysWMEvent syswm;
			public TouchFingerEvent tfinger;
			public MultiGestureEvent mgesture;
			public DollarGestureEvent dgesture;
			public DropEvent drop;
		}

		public function int EventFilter(
			void* userdata, // void*
			Event* sdlevent // SDL_Event* event, lolC#
		);

		/* Pump the event loop, getting events from the input devices*/
		[LinkName("SDL_PumpEvents")]
		public static extern void PumpEvents();

		public enum EventAction
		{
			AddEvent,
			PeekEvent,
			GetEvent
		}

		[LinkName("SDL_PeepEvents")]
		public static extern int PeepEvents(
			Event* events,
			int32 numevents,
			EventAction action,
			EventType minType,
			EventType maxType
		);

		/* Checks to see if certain events are in the event queue */
		[LinkName("SDL_HasEvent")]
		public static extern Bool HasEvent(EventType type);

		[LinkName("SDL_HasEvents")]
		public static extern Bool HasEvents(
			EventType minType,
			EventType maxType
		);

		/* Clears events from the event queue */
		[LinkName("SDL_FlushEvent")]
		public static extern void FlushEvent(EventType type);

		[LinkName("SDL_FlushEvents")]
		public static extern void FlushEvents(
			EventType min,
			EventType max
		);

		[LinkName("SDL_PollEvent")]
		public static extern int32 PollEvent(out Event event);

		[LinkName("SDL_WaitEvent")]
		public static extern int32 WaitEvent(out Event event);
		
		[LinkName("SDL_WaitEventTimeout")]
		public static extern int32 WaitEventTimeout(out Event event, int32 timeout);

		[LinkName("SDL_PushEvent")]
		public static extern int32 PushEvent(ref Event event);

		[LinkName("SDL_SetEventFilter")]
		public static extern void SetEventFilter(
			EventFilter filter,
			void* userdata
		);

		/* userdata refers to a void* */
		[LinkName("SDL_GetEventFilter")]
		public static extern Bool GetEventFilter(
			out EventFilter filter,
			out void* userdata
		);

		/* userdata refers to a void* */
		[LinkName("SDL_AddEventWatch")]
		public static extern void AddEventWatch(
			EventFilter filter,
			void* userdata
		);

		/* userdata refers to a void* */
		[LinkName("SDL_DelEventWatch")]
		public static extern void DelEventWatch(
			EventFilter filter,
			void* userdata
		);

		/* userdata refers to a void* */
		[LinkName("SDL_FilterEvents")]
		public static extern void FilterEvents(
			EventFilter filter,
			void* userdata
		);

		[AllowDuplicates]
		enum EventState : int32
		{
			Query = -1,
			Ignore = 0,
			Disable = 0,
			Enable = 1
		}

		/* This function allows you to enable/disable certain events */
		[LinkName("SDL_EventState")]
		public static extern uint8 EventState(EventType type, EventState state);

		/* Get the state of an event */
		public static uint8 GetEventState(EventType type)
		{
			return EventState(type, .Query);
		}

		/* Allocate a set of user-defined events */
		[LinkName("SDL_RegisterEvents")]
		public static extern uint32 RegisterEvents(int32 numevents);
		
		/* Scancodes based off USB keyboard page (0x07) */
		public enum Scancode : uint32
		{
			UNKNOWN = 0,

			A = 4,
			B = 5,
			C = 6,
			D = 7,
			E = 8,
			F = 9,
			G = 10,
			H = 11,
			I = 12,
			J = 13,
			K = 14,
			L = 15,
			M = 16,
			N = 17,
			O = 18,
			P = 19,
			Q = 20,
			R = 21,
			S = 22,
			T = 23,
			U = 24,
			V = 25,
			W = 26,
			X = 27,
			Y = 28,
			Z = 29,

			Key1 = 30,
			Key2 = 31,
			Key3 = 32,
			Key4 = 33,
			Key5 = 34,
			Key6 = 35,
			Key7 = 36,
			Key8 = 37,
			Key9 = 38,
			Key0 = 39,

			Return = 40,
			Escape = 41,
			BackSpace = 42,
			Tab = 43,
			Space = 44,

			Minus = 45,
			Equals = 46,
			LeftBracket = 47,
			RightBracket = 48,
			BackSlash = 49,
			NonUSHash = 50,
			Semicolon = 51,
			Apostrophe = 52,
			Grave = 53,
			Comma = 54,
			Period = 55,
			Slash = 56,

			CapsLock = 57,

			F1 = 58,
			F2 = 59,
			F3 = 60,
			F4 = 61,
			F5 = 62,
			F6 = 63,
			F7 = 64,
			F8 = 65,
			F9 = 66,
			F10 = 67,
			F11 = 68,
			F12 = 69,

			PrintScreen = 70,
			ScrollLock = 71,
			Pause = 72,
			Insert = 73,
			Home = 74,
			Pageup = 75,
			Delete = 76,
			End = 77,
			PageDown = 78,
			Right = 79,
			Left = 80,
			Down = 81,
			Up = 82,

			NumLockClear = 83,
			KpDivide = 84,
			KpMultiply = 85,
			KpMinus = 86,
			KpPlus = 87,
			KpEnter = 88,
			Kp1 = 89,
			Kp2 = 90,
			Kp3 = 91,
			Kp4 = 92,
			Kp5 = 93,
			Kp6 = 94,
			Kp7 = 95,
			Kp8 = 96,
			Kp9 = 97,
			Kp0 = 98,
			Kpperiod = 99,

			NonUSBackslash = 100,
			Application = 101,
			Power = 102,
			KpEquals = 103,
			F13 = 104,
			F14 = 105,
			F15 = 106,
			F16 = 107,
			F17 = 108,
			F18 = 109,
			F19 = 110,
			F20 = 111,
			F21 = 112,
			F22 = 113,
			F23 = 114,
			F24 = 115,
			Execute = 116,
			Help = 117,
			Menu = 118,
			Select = 119,
			Stop = 120,
			Again = 121,
			Undo = 122,
			Cut = 123,
			Copy = 124,
			Paste = 125,
			Find = 126,
			Mute = 127,
			Volumeup = 128,
			Volumedown = 129,
			/* not sure whether there's a reason to enable these */
			/*	lockingcapslock = 130, */
			/*	lockingnumlock = 131, */
			/*	lockingscrolllock = 132, */
			KpComma = 133,
			KpEqualsAS400 = 134,

			International1 = 135,
			International2 = 136,
			International3 = 137,
			International4 = 138,
			International5 = 139,
			International6 = 140,
			International7 = 141,
			International8 = 142,
			International9 = 143,
			Lang1 = 144,
			Lang2 = 145,
			Lang3 = 146,
			Lang4 = 147,
			Lang5 = 148,
			Lang6 = 149,
			Lang7 = 150,
			Lang8 = 151,
			Lang9 = 152,

			AltErase = 153,
			SysReq = 154,
			Cancel = 155,
			Clear = 156,
			Prior = 157,
			Return2 = 158,
			Separator = 159,
			Out = 160,
			Oper = 161,
			ClearAgain = 162,
			CrSel = 163,
			ExSel = 164,

			Kp00 = 176,
			Kp000 = 177,
			Thousandsseparator = 178,
			Decimalseparator = 179,
			Currencyunit = 180,
			Currencysubunit = 181,
			Kpleftparen = 182,
			Kprightparen = 183,
			Kpleftbrace = 184,
			Kprightbrace = 185,
			Kptab = 186,
			Kpbackspace = 187,
			KPA = 188,
			KPB = 189,
			KPC = 190,
			KPD = 191,
			KPE = 192,
			KPF = 193,
			KpXor = 194,
			KpPower = 195,
			KpPercent = 196,
			KpLess = 197,
			KpGreater = 198,
			KpAmpersand = 199,
			KpdBlAmpersand = 200,
			KpVerticalBar = 201,
			KpDblVerticalBar = 202,
			KpColon = 203,
			KpHash = 204,
			KpSpace = 205,
			KpAt = 206,
			KpExclam = 207,
			KpMemstore = 208,
			KpMemrecall = 209,
			KpMemclear = 210,
			KpMemadd = 211,
			KpMemsubtract = 212,
			KpMemmultiply = 213,
			KpMemdivide = 214,
			KpPlusminus = 215,
			KpClear = 216,
			KpClearentry = 217,
			KpBinary = 218,
			KpOctal = 219,
			KpDecimal = 220,
			KpHexadecimal = 221,

			LCtrl = 224,
			LShift = 225,
			LAlt = 226,
			LGui = 227,
			RCtrl = 228,
			RShift = 229,
			RAlt = 230,
			RGui = 231,

			Mode = 257,

			/* these come from the usb consumer page (0x0c) */
			Audionext = 258,
			Audioprev = 259,
			Audiostop = 260,
			Audioplay = 261,
			Audiomute = 262,
			Mediaselect = 263,
			Www = 264,
			Mail = 265,
			Calculator = 266,
			Computer = 267,
			Acsearch = 268,
			Achome = 269,
			Acback = 270,
			Acforward = 271,
			Acstop = 272,
			Acrefresh = 273,
			Acbookmarks = 274,

			/* these come from other sources, and are mostly mac related */
			Brightnessdown = 275,
			Brightnessup = 276,
			Displayswitch = 277,
			KbdIllumtoggle = 278,
			KbdIllumdown = 279,
			KbdIllumup = 280,
			Eject = 281,
			Sleep = 282,

			App1 = 283,
			App2 = 284,

			/* this is not a key, simply marks the number of scancodes
			 * so that you know how big to make your arrays. */
			NUMSCANCODES = 512
		}

		public const int SCANCODE_MASK = (1 << 30);
		public static Keycode SCANCODE_TO_KEYCODE(Scancode X)
		{
			return (Keycode)((int)X | SCANCODE_MASK);
		}

		/* So, in the C headers, SDL_Keycode is a typedef of Sint32
		 * and all of the names are in an anonymous enum. Yeah...
		 * that's not going to cut it for C#. We'll just put them in an
		 * enum for now? */
		[AllowDuplicates]
		public enum Keycode : uint32
		{
			UNKNOWN = 0,

			RETURN = (.)'\r',
			ESCAPE = 27, // '\033'
			BACKSPACE = (.)'\b',
			TAB = (.)'\t',
			SPACE = (.)' ',
			EXCLAIM = (.)'!',
			QUOTEDBL = (.)'"',
			HASH = (.)'#',
			PERCENT = (.)'%',
			DOLLAR = (.)'$',
			AMPERSAND = (.)'&',
			QUOTE = (.)'\'',
			LEFTPAREN = (.)'(',
			RIGHTPAREN = (.)')',
			ASTERISK = (.)'*',
			PLUS = (.)'+',
			COMMA = (.)',',
			MINUS = (.)'-',
			PERIOD = (.)'.',
			SLASH = (.)'/',
			Num0 = (.)'0',
			Num1 = (.)'1',
			Num2 = (.)'2',
			Num3 = (.)'3',
			Num4 = (.)'4',
			Num5 = (.)'5',
			Num6 = (.)'6',
			Num7 = (.)'7',
			Num8 = (.)'8',
			Num9 = (.)'9',
			Colon = (.)':',
			Semicolon = (.)';',
			Less = (.)'<',
			Equals = (.)'=',
			Greater = (.)'>',
			Question = (.)'?',
			AT = (.)'@',
			/*
			Skip uppercase letters
			*/
			Leftbracket = (.)'[',
			Backslash = (.)'\\',
			Rightbracket = (.)']',
			Caret = (.)'^',
			Underscore = (.)'_',
			Backquote = (.)'`',
			A = (.)'a',
			B = (.)'b',
			C = (.)'c',
			D = (.)'d',
			E = (.)'e',
			F = (.)'f',
			G = (.)'g',
			H = (.)'h',
			I = (.)'i',
			J = (.)'j',
			K = (.)'k',
			L = (.)'l',
			M = (.)'m',
			N = (.)'n',
			O = (.)'o',
			P = (.)'p',
			Q = (.)'q',
			R = (.)'r',
			S = (.)'s',
			T = (.)'t',
			U = (.)'u',
			V = (.)'v',
			W = (.)'w',
			X = (.)'x',
			Y = (.)'y',
			Z = (.)'z',

			CAPSLOCK = (int)Scancode.CapsLock | SCANCODE_MASK,

			F1 = (int)Scancode.F1 | SCANCODE_MASK,
			F2 = (int)Scancode.F2 | SCANCODE_MASK,
			F3 = (int)Scancode.F3 | SCANCODE_MASK,
			F4 = (int)Scancode.F4 | SCANCODE_MASK,
			F5 = (int)Scancode.F5 | SCANCODE_MASK,
			F6 = (int)Scancode.F6 | SCANCODE_MASK,
			F7 = (int)Scancode.F7 | SCANCODE_MASK,
			F8 = (int)Scancode.F8 | SCANCODE_MASK,
			F9 = (int)Scancode.F9 | SCANCODE_MASK,
			F10 = (int)Scancode.F10 | SCANCODE_MASK,
			F11 = (int)Scancode.F11 | SCANCODE_MASK,
			F12 = (int)Scancode.F12 | SCANCODE_MASK,

			PRINTSCREEN = (int)Scancode.PrintScreen | SCANCODE_MASK,
			SCROLLLOCK = (int)Scancode.ScrollLock | SCANCODE_MASK,
			PAUSE = (int)Scancode.Pause | SCANCODE_MASK,
			INSERT = (int)Scancode.Insert | SCANCODE_MASK,
			HOME = (int)Scancode.Home | SCANCODE_MASK,
			PAGEUP = (int)Scancode.Pageup | SCANCODE_MASK,
			DELETE = 127,
			END = (int)Scancode.End | SCANCODE_MASK,
			PAGEDOWN = (int)Scancode.PageDown | SCANCODE_MASK,
			RIGHT = (int)Scancode.Right | SCANCODE_MASK,
			LEFT = (int)Scancode.Left | SCANCODE_MASK,
			DOWN = (int)Scancode.Down | SCANCODE_MASK,
			UP = (int)Scancode.Up | SCANCODE_MASK,

			NUMLOCKCLEAR = (int)Scancode.NumLockClear | SCANCODE_MASK,
			KP_DIVIDE = (int)Scancode.KpDivide | SCANCODE_MASK,
			KPMULTIPLY = (int)Scancode.KpMultiply | SCANCODE_MASK,
			KPMINUS = (int)Scancode.KpMinus | SCANCODE_MASK,
			KPPLUS = (int)Scancode.KpPlus | SCANCODE_MASK,
			KPENTER = (int)Scancode.KpEnter | SCANCODE_MASK,
			KP1 = (int)Scancode.Kp1 | SCANCODE_MASK,
			KP2 = (int)Scancode.Kp2 | SCANCODE_MASK,
			KP3 = (int)Scancode.Kp3 | SCANCODE_MASK,
			KP4 = (int)Scancode.Kp4 | SCANCODE_MASK,
			KP5 = (int)Scancode.Kp5 | SCANCODE_MASK,
			KP6 = (int)Scancode.Kp6 | SCANCODE_MASK,
			KP7 = (int)Scancode.Kp7 | SCANCODE_MASK,
			KP8 = (int)Scancode.Kp8 | SCANCODE_MASK,
			KP9 = (int)Scancode.Kp9 | SCANCODE_MASK,
			KP0 = (int)Scancode.Kp0 | SCANCODE_MASK,
			KPPERIOD = (int)Scancode.Kpperiod | SCANCODE_MASK,

			APPLICATION = (int)Scancode.Application | SCANCODE_MASK,
			POWER = (int)Scancode.Power | SCANCODE_MASK,
			KPEQUALS = (int)Scancode.KpEquals | SCANCODE_MASK,
			F13 = (int)Scancode.F13 | SCANCODE_MASK,
			F14 = (int)Scancode.F14 | SCANCODE_MASK,
			F15 = (int)Scancode.F15 | SCANCODE_MASK,
			F16 = (int)Scancode.F16 | SCANCODE_MASK,
			F17 = (int)Scancode.F17 | SCANCODE_MASK,
			F18 = (int)Scancode.F18 | SCANCODE_MASK,
			F19 = (int)Scancode.F19 | SCANCODE_MASK,
			F20 = (int)Scancode.F20 | SCANCODE_MASK,
			F21 = (int)Scancode.F21 | SCANCODE_MASK,
			F22 = (int)Scancode.F22 | SCANCODE_MASK,
			F23 = (int)Scancode.F23 | SCANCODE_MASK,
			F24 = (int)Scancode.F24 | SCANCODE_MASK,
			EXECUTE = (int)Scancode.Execute | SCANCODE_MASK,
			HELP = (int)Scancode.Help | SCANCODE_MASK,
			MENU = (int)Scancode.Menu | SCANCODE_MASK,
			SELECT = (int)Scancode.Select | SCANCODE_MASK,
			STOP = (int)Scancode.Stop | SCANCODE_MASK,
			AGAIN = (int)Scancode.Again | SCANCODE_MASK,
			UNDO = (int)Scancode.Undo | SCANCODE_MASK,
			CUT = (int)Scancode.Cut | SCANCODE_MASK,
			COPY = (int)Scancode.Copy | SCANCODE_MASK,
			PASTE = (int)Scancode.Paste | SCANCODE_MASK,
			FIND = (int)Scancode.Find | SCANCODE_MASK,
			MUTE = (int)Scancode.Mute | SCANCODE_MASK,
			VOLUMEUP = (int)Scancode.Volumeup | SCANCODE_MASK,
			VOLUMEDOWN = (int)Scancode.Volumedown | SCANCODE_MASK,
			KP_COMMA = (int)Scancode.KpComma | SCANCODE_MASK,
			KP_EQUALSAS400 = (int)Scancode.KpEqualsAS400 | SCANCODE_MASK,

			ALTERASE = (int)Scancode.AltErase | SCANCODE_MASK,
			SYSREQ = (int)Scancode.SysReq | SCANCODE_MASK,
			CANCEL = (int)Scancode.Cancel | SCANCODE_MASK,
			CLEAR = (int)Scancode.Clear | SCANCODE_MASK,
			PRIOR = (int)Scancode.Prior | SCANCODE_MASK,
			RETURN2 = (int)Scancode.Return2 | SCANCODE_MASK,
			SEPARATOR = (int)Scancode.Separator | SCANCODE_MASK,
			OUT = (int)Scancode.Out | SCANCODE_MASK,
			OPER = (int)Scancode.Oper | SCANCODE_MASK,
			CLEARAGAIN = (int)Scancode.ClearAgain | SCANCODE_MASK,
			CRSEL = (int)Scancode.CrSel | SCANCODE_MASK,
			EXSEL = (int)Scancode.ExSel | SCANCODE_MASK,

			KP00 = (int)Scancode.Kp00 | SCANCODE_MASK,
			KP000 = (int)Scancode.Kp00 | SCANCODE_MASK,
			THOUSANDSSEPARATOR = (int)Scancode.Thousandsseparator | SCANCODE_MASK,
			DECIMALSEPARATOR = (int)Scancode.Decimalseparator | SCANCODE_MASK,
			CURRENCYUNIT = (int)Scancode.Currencyunit | SCANCODE_MASK,
			CURRENCYSUBUNIT = (int)Scancode.Currencysubunit | SCANCODE_MASK,
			KPLEFTPAREN = (int)Scancode.Kpleftparen | SCANCODE_MASK,
			KPRIGHTPAREN = (int)Scancode.Kprightparen | SCANCODE_MASK,
			KPLEFTBRACE = (int)Scancode.Kpleftbrace | SCANCODE_MASK,
			KPRIGHTBRACE = (int)Scancode.Kprightbrace | SCANCODE_MASK,
			KPTAB = (int)Scancode.Kptab | SCANCODE_MASK,
			KPBACKSPACE = (int)Scancode.Kpbackspace | SCANCODE_MASK,
			KPA = (int)Scancode.KPA | SCANCODE_MASK,
			KPB = (int)Scancode.KPB | SCANCODE_MASK,
			KPC = (int)Scancode.KPC | SCANCODE_MASK,
			KPD = (int)Scancode.KPD | SCANCODE_MASK,
			KPE = (int)Scancode.KPE | SCANCODE_MASK,
			KPF = (int)Scancode.KPF | SCANCODE_MASK,
			KPXOR = (int)Scancode.KpXor | SCANCODE_MASK,
			KPPOWER = (int)Scancode.KpPower | SCANCODE_MASK,
			KPPERCENT = (int)Scancode.KpPercent | SCANCODE_MASK,
			KPLESS = (int)Scancode.KpLess | SCANCODE_MASK,
			KPGREATER = (int)Scancode.KpGreater | SCANCODE_MASK,
			KPAMPERSAND = (int)Scancode.KpAmpersand | SCANCODE_MASK,
			KPDBLAMPERSAND = (int)Scancode.KpdBlAmpersand | SCANCODE_MASK,
			KPVERTICALBAR = (int)Scancode.KpVerticalBar | SCANCODE_MASK,
			KPDBLVERTICALBAR = (int)Scancode.KpDblVerticalBar | SCANCODE_MASK,
			KPCOLON = (int)Scancode.KpColon | SCANCODE_MASK,
			KPHASH = (int)Scancode.KpHash | SCANCODE_MASK,
			KPSPACE = (int)Scancode.KpSpace | SCANCODE_MASK,
			KPAT = (int)Scancode.KPA | SCANCODE_MASK,
			KPEXCLAM = (int)Scancode.KpExclam | SCANCODE_MASK,
			KPMEMSTORE = (int)Scancode.KpMemstore | SCANCODE_MASK,
			KPMEMRECALL = (int)Scancode.KpMemrecall | SCANCODE_MASK,
			KPMEMCLEAR = (int)Scancode.KpMemclear | SCANCODE_MASK,
			KPMEMADD = (int)Scancode.KpMemadd | SCANCODE_MASK,
			KPMEMSUBTRACT = (int)Scancode.KpMemsubtract | SCANCODE_MASK,
			KPMEMMULTIPLY = (int)Scancode.KpMemmultiply | SCANCODE_MASK,
			KPMEMDIVIDE = (int)Scancode.KpMemdivide | SCANCODE_MASK,
			KPPLUSMINUS = (int)Scancode.KpPlusminus | SCANCODE_MASK,
			KPCLEAR = (int)Scancode.KpClear | SCANCODE_MASK,
			KPCLEARENTRY = (int)Scancode.KpClearentry | SCANCODE_MASK,
			KPBINARY = (int)Scancode.KpBinary | SCANCODE_MASK,
			KPOCTAL = (int)Scancode.KpOctal | SCANCODE_MASK,
			KPDECIMAL = (int)Scancode.KpDecimal | SCANCODE_MASK,
			KPHEXADECIMAL =	 (int)Scancode.KpHexadecimal | SCANCODE_MASK,

			LCTRL = (int)Scancode.LCtrl | SCANCODE_MASK,
			LSHIFT = (int)Scancode.LShift | SCANCODE_MASK,
			LALT = (int)Scancode.LAlt | SCANCODE_MASK,
			LGUI = (int)Scancode.LGui | SCANCODE_MASK,
			RCTRL = (int)Scancode.RCtrl | SCANCODE_MASK,
			RSHIFT = (int)Scancode.RShift | SCANCODE_MASK,
			RALT = (int)Scancode.RAlt | SCANCODE_MASK,
			RGUI = (int)Scancode.RGui | SCANCODE_MASK,

			MODE = (int)Scancode.Mode | SCANCODE_MASK,

			AUDIONEXT = (int)Scancode.Audionext | SCANCODE_MASK,
			AUDIOPREV = (int)Scancode.Audioprev | SCANCODE_MASK,
			AUDIOSTOP = (int)Scancode.Audiostop | SCANCODE_MASK,
			AUDIOPLAY = (int)Scancode.Audioplay | SCANCODE_MASK,
			AUDIOMUTE = (int)Scancode.Audiomute | SCANCODE_MASK,
			MEDIASELECT = (int)Scancode.Mediaselect | SCANCODE_MASK,
			WWW = (int)Scancode.Www | SCANCODE_MASK,
			MAIL = (int)Scancode.Mail | SCANCODE_MASK,
			CALCULATOR = (int)Scancode.Calculator | SCANCODE_MASK,
			COMPUTER = (int)Scancode.Computer | SCANCODE_MASK,
			ACSEARCH = (int)Scancode.Acsearch | SCANCODE_MASK,
			ACHOME = (int)Scancode.Achome | SCANCODE_MASK,
			ACBACK = (int)Scancode.Acback | SCANCODE_MASK,
			ACFORWARD = (int)Scancode.Acforward | SCANCODE_MASK,
			ACSTOP = (int)Scancode.Acstop | SCANCODE_MASK,
			ACREFRESH = (int)Scancode.Acrefresh | SCANCODE_MASK,
			ACBOOKMARKS = (int)Scancode.Acbookmarks | SCANCODE_MASK,

			BRIGHTNESSDOWN =
				(int)Scancode.Brightnessdown | SCANCODE_MASK,
				BRIGHTNESSUP = (int)Scancode.Brightnessup | SCANCODE_MASK,
				DISPLAYSWITCH = (int)Scancode.Displayswitch | SCANCODE_MASK,
				KBDILLUMTOGGLE =
			(int)Scancode.KbdIllumtoggle | SCANCODE_MASK,
				KBDILLUMDOWN = (int)Scancode.KbdIllumdown | SCANCODE_MASK,
				KBDILLUMUP = (int)Scancode.KbdIllumup | SCANCODE_MASK,
				EJECT = (int)Scancode.Eject | SCANCODE_MASK,
				SLEEP = (int)Scancode.Sleep | SCANCODE_MASK
		}

		/* Key modifiers (bitfield) */
		public enum KeyMod : uint16
		{
			None = 0x0000,
			LShift = 0x0001,
			RShift = 0x0002,
			LCtrl = 0x0040,
			RCtrl = 0x0080,
			LAlt = 0x0100,
			RAlt = 0x0200,
			LGui = 0x0400,
			RGui = 0x0800,
			Num = 0x1000,
			Caps = 0x2000,
			Mode = 0x4000,
			RESERVED = 0x8000,

			/* These are defines in the SDL headers */
			CTRL = (LCtrl | RCtrl),
			SHIFT = (LShift | RShift),
			ALT = (LAlt | RAlt),
			GUI = (LGui | RGui)
		}

		[CRepr]
		public struct KeySym
		{
			public Scancode scancode;
			public Keycode sym;
			public KeyMod mod; /* uint16 */
			public uint32 unicode; /* Deprecated */
		}

		/* Get the window which has kbd focus */
		/* Return type is an SDL_Window pointer */
		[LinkName("SDL_GetKeyboardFocus")]
		public static extern Window* GetKeyboardFocus();

		/* Get a snapshot of the keyboard state. */
		/* Return value is a pointer to a UInt8 array */
		/* Numkeys returns the size of the array if non-null */
		[LinkName("SDL_GetKeyboardState")]
		public static extern bool* GetKeyboardState(int32* numkeys);

		/* Get the current key modifier state for the keyboard. */
		[LinkName("SDL_GetModState")]
		public static extern KeyMod GetModState();

		/* Set the current key modifier state */
		[LinkName("SDL_SetModState")]
		public static extern void SetModState(KeyMod modstate);

		/* Get the key code corresponding to the given scancode
		 * with the current keyboard layout.
		 */
		[LinkName("SDL_GetKeyFromScancode")]
		public static extern Keycode GetKeyFromScancode(Scancode scancode);

		/* Get the scancode for the given keycode */
		[LinkName("SDL_GetScancodeFromKey")]
		public static extern Scancode GetScancodeFromKey(Keycode key);

		/* Wrapper for SDL_GetScancodeName */
		[LinkName("SDL_GetScancodeName")]
		public static extern char8* GetScancodeName(Scancode scancode);

		/* Get a scancode from a human-readable name */
		[LinkName("SDL_GetScancodeFromName")]
		public static extern Scancode GetScancodeFromName(char8 name);

		/* Wrapper for SDL_GetKeyName */
		[LinkName("SDL_GetKeyName")]
		public static extern char8* GetKeyName(Keycode key);

		[LinkName("SDL_GetKeyFromName")]
		public static extern Keycode GetKeyFromName(char8* name);

		/* Start accepting Unicode text input events, show keyboard */
		[LinkName("SDL_StartTextInput")]
		public static extern void StartTextInput();

		/* Check if unicode input events are enabled */
		[LinkName("SDL_IsTextInputActive")]
		public static extern Bool IsTextInputActive();

		/* Stop receiving any text input events, hide onscreen kbd */
		[LinkName("SDL_StopTextInput")]
		public static extern void StopTextInput();

		/* Set the rectangle used for text input, hint for IME */
		[LinkName("SDL_SetTextInputRect")]
		public static extern void SetTextInputRect(Rect* rect);

		/* Does the platform support an on-screen keyboard? */
		[LinkName("SDL_HasScreenKeyboardSupport")]
		public static extern Bool HasScreenKeyboardSupport();

		/* Is the on-screen keyboard shown for a given window? */
		/* window is an SDL_Window pointer */
		[LinkName("SDL_IsScreenKeyboardShown")]
		public static extern Bool IsScreenKeyboardShown(Window* window);

		#endregion

		#region SDL_mouse.c

		/* Note: SDL_Cursor is a typedef normally. We'll treat it as
		 * an IntPtr, because C# doesn't do typedefs. Yay!
		 */

		/* System cursor types */
		public enum SDL_SystemCursor : uint32
		{
			SDL_SYSTEM_CURSOR_ARROW,	// Arrow
			SDL_SYSTEM_CURSOR_IBEAM,	// I-beam
			SDL_SYSTEM_CURSOR_WAIT,		// Wait
			SDL_SYSTEM_CURSOR_CROSSHAIR,	// Crosshair
			SDL_SYSTEM_CURSOR_WAITARROW,	// Small wait cursor (or Wait if not available)
			SDL_SYSTEM_CURSOR_SIZENWSE,	// Double arrow pointing northwest and southeast
			SDL_SYSTEM_CURSOR_SIZENESW,	// Double arrow pointing northeast and southwest
			SDL_SYSTEM_CURSOR_SIZEWE,	// Double arrow pointing west and east
			SDL_SYSTEM_CURSOR_SIZENS,	// Double arrow pointing north and south
			SDL_SYSTEM_CURSOR_SIZEALL,	// Four pointed arrow pointing north, south, east, and west
			SDL_SYSTEM_CURSOR_NO,		// Slashed circle or crossbones
			SDL_SYSTEM_CURSOR_HAND,		// Hand
			SDL_NUM_SYSTEM_CURSORS
		}

		/* Get the window which currently has mouse focus */
		/* Return value is an SDL_Window pointer */
		[LinkName("SDL_GetMouseFocus")]
		public static extern Window* GetMouseFocus();

		/* Get the current state of the mouse */
		[LinkName("SDL_GetMouseState")]
		public static extern uint32 GetMouseState(int32* x, int32* y);

		[LinkName("SDL_GetGlobalMouseState")]
		public static extern uint32 GetGlobalMouseState(int32* x, int32* y);

		/* Get the mouse state with relative coords*/
		[LinkName("SDL_GetRelativeMouseState")]
		public static extern uint32 GetRelativeMouseState(int32* x, int32* y);

		/* Set the mouse cursor's position (within a window) */
		/* window is an SDL_Window pointer */
		[LinkName("SDL_WarpMouseInWindow")]
		public static extern void WarpMouseInWindow(Window* window, int32 x, int32 y);

		/* Set the mouse cursor's position in global screen space */
		/* Only available in 2.0.4 */
		[LinkName("SDL_WarpMouseGlobal")]
		public static extern int WarpMouseGlobal(int32 x, int32 y);

		/* Enable/Disable relative mouse mode (grabs mouse, rel coords) */
		[LinkName("SDL_SetRelativeMouseMode")]
		public static extern int SetRelativeMouseMode(Bool enabled);

		/* Capture the mouse, to track input outside an SDL window */
		/* Only available in 2.0.4 */
		[LinkName("SDL_CaptureMouse")]
		public static extern int CaptureMouse(Bool enabled);

		/* Query if the relative mouse mode is enabled */
		[LinkName("SDL_GetRelativeMouseMode")]
		public static extern Bool GetRelativeMouseMode();

		public struct SDL_Cursor;

		/* Create a cursor from bitmap data (amd mask) in MSB format */
		/* data and mask are byte arrays, and w must be a multiple of 8 */
		/* return value is an SDL_Cursor pointer */
		[LinkName("SDL_CreateCursor")]
		public static extern SDL_Cursor* CreateCursor(
			uint8* data,
			uint8* mask,
			int32 w,
			int32 h,
			int32 hot_x,
			int32 hot_y
		);

		/* Create a cursor from an SDL_Surface */
		/* IntPtr refers to an SDL_Cursor*, surface to an SDL_Surface* */
		[LinkName("SDL_CreateColorCursor")]
		public static extern SDL_Cursor* CreateColorCursor(
			Surface* surface,
			int32 hot_x,
			int32 hot_y
		);

		/* Create a cursor from a system cursor id */
		/* return value is an SDL_Cursor pointer */
		[LinkName("SDL_CreateSystemCursor")]
		public static extern SDL_Cursor* CreateSystemCursor(SDL_SystemCursor id);

		/* Set the active cursor */
		/* cursor is an SDL_Cursor pointer */
		[LinkName("SDL_SetCursor")]
		public static extern void SetCursor(SDL_Cursor* cursor);

		/* Return the active cursor */
		/* return value is an SDL_Cursor pointer */
		[LinkName("SDL_GetCursor")]
		public static extern SDL_Cursor* GetCursor();

		/* Frees a cursor created with one of the CreateCursor functions */
		/* cursor in an SDL_Cursor pointer */
		[LinkName("SDL_FreeCursor")]
		public static extern void FreeCursor(SDL_Cursor* cursor);

		/* Toggle whether or not the cursor is shown */
		[LinkName("SDL_ShowCursor")]
		public static extern int32 ShowCursor(int32 toggle);

		public static uint32 BUTTON(uint32 X)
		{
			// If only there were a better way of doing this in C#
			return (uint32) (1 << ((int32) X - 1));
		}

		public const uint32 SDL_BUTTON_LEFT = 1;
		public const uint32 SDL_BUTTON_MIDDLE =	2;
		public const uint32 SDL_BUTTON_RIGHT = 3;
		public const uint32 SDL_BUTTON_X1 =	4;
		public const uint32 SDL_BUTTON_X2 =	5;
		public static readonly uint32 BUTTON_LMASK =	BUTTON(SDL_BUTTON_LEFT);
		public static readonly uint32 BUTTON_MMASK =	BUTTON(SDL_BUTTON_MIDDLE);
		public static readonly uint32 BUTTON_RMASK =	BUTTON(SDL_BUTTON_RIGHT);
		public static readonly uint32 BUTTON_X1MASK =	BUTTON(SDL_BUTTON_X1);
		public static readonly uint32 BUTTON_X2MASK =	BUTTON(SDL_BUTTON_X2);

		public const uint32 TOUCH_MOUSEID = uint32.MaxValue;

		public struct SDL_Finger
		{
			public int64 id; // SDL_FingerID
			public float x;
			public float y;
			public float pressure;
		}

		/**
		 *  \brief Get the number of registered touch devices.
 		 */
		[LinkName("SDL_GetNumTouchDevices")]
		public static extern int32 GetNumTouchDevices();

		/**
		 *  \brief Get the touch ID with the given index, or 0 if the index is invalid.
		 */
		[LinkName("SDL_GetTouchDevice")]
		public static extern int64 GetTouchDevice(int32 index);

		/**
		 *  \brief Get the number of active fingers for a given touch device.
		 */
		[LinkName("SDL_GetNumTouchFingers")]
		public static extern int GetNumTouchFingers(int64 touchID);

		/**
		 *  \brief Get the finger object of the given touch, with the given index.
		 *  Returns pointer to SDL_Finger.
		 */
		[LinkName("SDL_GetTouchFinger")]
		public static extern SDL_Finger* GetTouchFinger(int64 touchID, int32 index);


		public const uint8 SDL_HAT_CENTERED =	0x00;
		public const uint8 SDL_HAT_UP =		0x01;
		public const uint8 SDL_HAT_RIGHT =	0x02;
		public const uint8 SDL_HAT_DOWN =	0x04;
		public const uint8 SDL_HAT_LEFT =	0x08;
		public const uint8 SDL_HAT_RIGHTUP =	SDL_HAT_RIGHT | SDL_HAT_UP;
		public const uint8 SDL_HAT_RIGHTDOWN =	SDL_HAT_RIGHT | SDL_HAT_DOWN;
		public const uint8 SDL_HAT_LEFTUP =	SDL_HAT_LEFT | SDL_HAT_UP;
		public const uint8 SDL_HAT_LEFTDOWN =	SDL_HAT_LEFT | SDL_HAT_DOWN;

		public enum SDL_JoystickPowerLevel : int32
		{
			Unknown = -1,
			Empty,
			Low,
			Medium,
			Full,
			Wired,
			Max
		}

		public enum SDL_JoystickType : int32
		{
			Unknown,
			Gamecontroller,
			Wheel,
			ArcadeStick,
			FlightStick,
			DancePad,
			Guitar,
			DrumKit,
			ArcadePad
		}

		public struct SDL_Joystick;

		/* joystick refers to an SDL_Joystick*.
		 * This function is only available in 2.0.9 or higher.
		 */
		[LinkName("SDL_JoystickRumble")]
		public static extern int JoystickRumble(
			SDL_Joystick* joystick,
			uint16 low_frequency_rumble,
			uint16 high_frequency_rumble,
			uint32 duration_ms
		);

		
		[LinkName("SDL_JoystickClose")]
		public static extern void JoystickClose(SDL_Joystick* joystick);

		[LinkName("SDL_JoystickEventState")]
		public static extern int32 JoystickEventState(int state);

		
		[LinkName("SDL_JoystickGetAxis")]
		public static extern int16 JoystickGetAxis(
			SDL_Joystick* joystick,
			int32 axis
		);

		/* joystick refers to an SDL_Joystick*.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_JoystickGetAxisInitialState")]
		public static extern Bool JoystickGetAxisInitialState(
			SDL_Joystick* joystick,
			int32 axis,
			out uint16 state
		);

		
		[LinkName("SDL_JoystickGetBall")]
		public static extern int JoystickGetBall(
			SDL_Joystick* joystick,
			int32 ball,
			out int32 dx,
			out int32 dy
		);
		
		[LinkName("SDL_JoystickGetButton")]
		public static extern uint8 JoystickGetButton(
			SDL_Joystick* joystick,
			int32 button
		);
		
		[LinkName("SDL_JoystickGetHat")]
		public static extern uint8 JoystickGetHat(
			SDL_Joystick* joystick,
			int32 hat
		);
		
		[LinkName("SDL_JoystickName")]
		public static extern char8* JoystickName(SDL_Joystick* joystick);

		[LinkName("SDL_JoystickNameForIndex")]
		public static extern char8* JoystickNameForIndex(int32 device_index);
		
		[LinkName("SDL_JoystickNumAxes")]
		public static extern int32 JoystickNumAxes(SDL_Joystick* joystick);
		
		[LinkName("SDL_JoystickNumBalls")]
		public static extern int32 JoystickNumBalls(SDL_Joystick* joystick);
		
		[LinkName("SDL_JoystickNumButtons")]
		public static extern int32 JoystickNumButtons(SDL_Joystick* joystick);
		
		[LinkName("SDL_JoystickNumHats")]
		public static extern int32 JoystickNumHats(SDL_Joystick* joystick);
		
		[LinkName("SDL_JoystickOpen")]
		public static extern SDL_Joystick* JoystickOpen(int32 device_index);
		
		[LinkName("SDL_JoystickUpdate")]
		public static extern void JoystickUpdate();
		
		[LinkName("SDL_NumJoysticks")]
		public static extern int32 NumJoysticks();

		[LinkName("SDL_JoystickGetDeviceGUID")]
		public static extern Guid JoystickGetDeviceGUID(
			int32 device_index
		);
		
		[LinkName("SDL_JoystickGetGUID")]
		public static extern Guid JoystickGetGUID(
			SDL_Joystick* joystick
		);

		[LinkName("SDL_JoystickGetGUIDString")]
		public static extern void JoystickGetGUIDString(
			Guid guid,
			uint8* pszGUID,
			int32 cbGUID
		);

		[LinkName("SDL_JoystickGetGUIDFromString")]
		public static extern Guid JoystickGetGUIDFromString(char8* pchGuid);

		/* This function is only available in 2.0.6 or higher. */
		[LinkName("SDL_JoystickGetDeviceVendor")]
		public static extern uint16 JoystickGetDeviceVendor(int32 device_index);

		/* This function is only available in 2.0.6 or higher. */
		[LinkName("SDL_JoystickGetDeviceProduct")]
		public static extern uint16 JoystickGetDeviceProduct(int32 device_index);

		/* This function is only available in 2.0.6 or higher. */
		[LinkName("SDL_JoystickGetDeviceProductVersion")]
		public static extern uint16 JoystickGetDeviceProductVersion(int32 device_index);

		/* This function is only available in 2.0.6 or higher. */
		[LinkName("SDL_JoystickGetDeviceType")]
		public static extern SDL_JoystickType JoystickGetDeviceType(int32 device_index);

		/* int refers to an SDL_JoystickID.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_JoystickGetDeviceInstanceID")]
		public static extern int32 JoystickGetDeviceInstanceID(int32 device_index);

		/* joystick refers to an SDL_Joystick*.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_JoystickGetVendor")]
		public static extern uint16 JoystickGetVendor(SDL_Joystick* joystick);

		/* joystick refers to an SDL_Joystick*.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_JoystickGetProduct")]
		public static extern uint16 JoystickGetProduct(SDL_Joystick* joystick);

		/* joystick refers to an SDL_Joystick*.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_JoystickGetProductVersion")]
		public static extern uint16 JoystickGetProductVersion(SDL_Joystick* joystick);

		/* joystick refers to an SDL_Joystick*.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_JoystickGetType")]
		public static extern SDL_JoystickType JoystickGetType(SDL_Joystick* joystick);

		
		[LinkName("SDL_JoystickGetAttached")]
		public static extern Bool JoystickGetAttached(SDL_Joystick* joystick);

		/* int refers to an SDL_JoystickID, joystick to an SDL_Joystick* */
		[LinkName("SDL_JoystickInstanceID")]
		public static extern int32 JoystickInstanceID(SDL_Joystick* joystick);

		/* joystick refers to an SDL_Joystick*.
		 * This function is only available in 2.0.4 or higher.
		 */
		[LinkName("SDL_JoystickCurrentPowerLevel")]
		public static extern SDL_JoystickPowerLevel JoystickCurrentPowerLevel(
			SDL_Joystick* joystick
		);

		/* int refers to an SDL_JoystickID, IntPtr to an SDL_Joystick*.
		 * This function is only available in 2.0.4 or higher.
		 */
		[LinkName("SDL_JoystickFromInstanceID")]
		public static extern SDL_Joystick* JoystickFromInstanceID(int32 joyid);

		/* Only available in 2.0.7 */
		[LinkName("SDL_LockJoysticks")]
		public static extern void LockJoysticks();

		/* Only available in 2.0.7 */
		[LinkName("SDL_UnlockJoysticks")]
		public static extern void UnlockJoysticks();

		public enum SDL_GameControllerBindType : int32
		{
			None,
			Button,
			Axis,
			Hat
		}

		public enum SDL_GameControllerAxis : int32
		{
			Invalid = -1,
			LeftX,
			LeftY,
			RightX,
			RightY,
			TriggerLeft,
			TriggerRight,
			Max
		}

		public enum SDL_GameControllerButton : int32
		{
			INVALID = -1,
			A,
			B,
			X,
			Y,
			Back,
			Guide,
			Start,
			LeftStick,
			RightStick,
			LeftShoulder,
			RightShoulder,
			DpadUp,
			DpadDown,
			DpadLeft,
			DpadRight,
			Max,
		}

		// FIXME: I'd rather this somehow be private...
		[CRepr]
		public struct GameControllerButtonBind_hat
		{
			public int32 hat;
			public int32 hat_mask;
		}

		// FIXME: I'd rather this somehow be private...
		[Union]
		public struct INTERNAL_GameControllerButtonBind_union
		{
			public int32 button;
			public int32 axis;
			public GameControllerButtonBind_hat hat;
		}

		[CRepr]
		public struct SDL_GameControllerButtonBind
		{
			public SDL_GameControllerBindType bindType;
			public INTERNAL_GameControllerButtonBind_union value;
		}

		/* This exists to deal with C# being stupid about blittable types. */
		[CRepr]
		private struct INTERNAL_SDL_GameControllerButtonBind
		{
			public int32 bindType;
			/* Largest data type in the union is two ints in size */
			public int32 unionVal0;
			public int32 unionVal1;
		}

		[LinkName("SDL_GameControllerAddMapping")]
		public static extern int32 GameControllerAddMapping(
			char8* mappingString
		);

		/* This function is only available in 2.0.6 or higher. */
		[LinkName("SDL_GameControllerNumMappings")]
		public static extern int GameControllerNumMappings();

		/* This function is only available in 2.0.6 or higher. */
		[LinkName("SDL_GameControllerMappingForIndex")]
		public static extern char8* GameControllerMappingForIndex(int32 mapping_index);

		/* THIS IS AN RWops FUNCTION! */
		[LinkName("SDL_GameControllerAddMappingsFromRW")]
		private static extern int32 GameControllerAddMappingsFromRW(
			RWOps* rw,
			int32 freerw
		);
		public static int32 GameControllerAddMappingsFromFile(char8* file)
		{
			RWOps* rwops = RWFromFile(file, "rb");
			return GameControllerAddMappingsFromRW(rwops, 1);
		}

		[LinkName("SDL_GameControllerMappingForGUID")]
		public static extern char8* GameControllerMappingForGUID(Guid guid);

		public struct SDL_GameController;

		/* gamecontroller refers to an SDL_GameController* */
		[LinkName("SDL_GameControllerMapping")]
		public static extern char8* GameControllerMapping(
			SDL_GameController* gamecontroller
		);

		[LinkName("SDL_IsGameController")]
		public static extern Bool IsGameController(int32 joystick_index);

		[LinkName("SDL_GameControllerNameForIndex")]
		public static extern char8* GameControllerNameForIndex(
			int32 joystick_index
		);

		/* Only available in 2.0.9 or higher */
		[LinkName("SDL_GameControllerMappingForDeviceIndex")]
		public static extern char8* GameControllerMappingForDeviceIndex(
			int32 joystick_index
		);

		[LinkName("SDL_GameControllerOpen")]
		public static extern SDL_GameController* GameControllerOpen(int32 joystick_index);

		/* gamecontroller refers to an SDL_GameController* */
		[LinkName("SDL_GameControllerName")]
		public static extern char8* GameControllerName(
			SDL_GameController* gamecontroller
		);

		/* gamecontroller refers to an SDL_GameController*.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_GameControllerGetVendor")]
		public static extern uint16 GameControllerGetVendor(
			SDL_GameController* gamecontroller
		);

		/* gamecontroller refers to an SDL_GameController*.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_GameControllerGetProduct")]
		public static extern uint16 GameControllerGetProduct(
			SDL_GameController* gamecontroller
		);

		/* gamecontroller refers to an SDL_GameController*.
		 * This function is only available in 2.0.6 or higher.
		 */
		[LinkName("SDL_GameControllerGetProductVersion")]
		public static extern uint16 GameControllerGetProductVersion(
			SDL_GameController* gamecontroller
		);

		/* gamecontroller refers to an SDL_GameController* */
		[LinkName("SDL_GameControllerGetAttached")]
		public static extern Bool GameControllerGetAttached(
			SDL_GameController* gamecontroller
		);

		/* IntPtr refers to an SDL_Joystick*
		 * gamecontroller refers to an SDL_GameController*
		 */
		[LinkName("SDL_GameControllerGetJoystick")]
		public static extern SDL_Joystick* GameControllerGetJoystick(
			SDL_GameController* gamecontroller
		);

		[LinkName("SDL_GameControllerEventState")]
		public static extern int GameControllerEventState(int state);

		[LinkName("SDL_GameControllerUpdate")]
		public static extern void GameControllerUpdate();

		[LinkName("SDL_GameControllerGetAxisFromString")]
		public static extern SDL_GameControllerAxis GameControllerGetAxisFromString(
			char8* pchString
		);

		[LinkName("SDL_GameControllerGetStringForAxis")]
		public static extern char8* GameControllerGetStringForAxis(
			SDL_GameControllerAxis axis
		);

		/* gamecontroller refers to an SDL_GameController* */
		[LinkName("SDL_GameControllerGetBindForAxis")]
		public static extern SDL_GameControllerButtonBind GameControllerGetBindForAxis(
			SDL_GameController* gamecontroller,
			SDL_GameControllerAxis axis
		);

		/* gamecontroller refers to an SDL_GameController* */
		[LinkName("SDL_GameControllerGetAxis")]
		public static extern int16 GameControllerGetAxis(
			SDL_GameController* gamecontroller,
			SDL_GameControllerAxis axis
		);

		[LinkName("SDL_GameControllerGetButtonFromString")]
		public static extern SDL_GameControllerButton GameControllerGetButtonFromString(
			char8* pchString
		);

		[LinkName("SDL_GameControllerGetStringForButton")]
		public static extern char8* GameControllerGetStringForButton(
			SDL_GameControllerButton button
		);

		/* gamecontroller refers to an SDL_GameController* */
		[LinkName("SDL_GameControllerGetBindForButton")]
		public static extern SDL_GameControllerButtonBind GameControllerGetBindForButton(
			SDL_GameController* gamecontroller,
			SDL_GameControllerButton button
		);

		/* gamecontroller refers to an SDL_GameController* */
		[LinkName("SDL_GameControllerGetButton")]
		public static extern uint8 GameControllerGetButton(
			SDL_GameController* gamecontroller,
			SDL_GameControllerButton button
		);

		/* gamecontroller refers to an SDL_GameController*.
		 * This function is only available in 2.0.9 or higher.
		 */
		[LinkName("SDL_GameControllerRumble")]
		public static extern int GameControllerRumble(
			SDL_GameController* gamecontroller,
			uint16 low_frequency_rumble,
			uint16 high_frequency_rumble,
			uint32 duration_ms
		);

		/* gamecontroller refers to an SDL_GameController* */
		[LinkName("SDL_GameControllerClose")]
		public static extern void GameControllerClose(
			SDL_GameController* gamecontroller
		);

		/* int refers to an SDL_JoystickID, IntPtr to an SDL_GameController*.
		 * This function is only available in 2.0.4 or higher.
		 */
		[LinkName("SDL_GameControllerFromInstanceID")]
		public static extern SDL_GameController* GameControllerFromInstanceID(int joyid);

		/* SDL_HapticEffect type */
		public const uint16 SDL_HAPTIC_CONSTANT =	(1 << 0);
		public const uint16 SDL_HAPTIC_SINE =		(1 << 1);
		public const uint16 SDL_HAPTIC_LEFTRIGHT =	(1 << 2);
		public const uint16 SDL_HAPTIC_TRIANGLE =	(1 << 3);
		public const uint16 SDL_HAPTIC_SAWTOOTHUP =	(1 << 4);
		public const uint16 SDL_HAPTIC_SAWTOOTHDOWN =	(1 << 5);
		public const uint16 SDL_HAPTIC_SPRING =		(1 << 7);
		public const uint16 SDL_HAPTIC_DAMPER =		(1 << 8);
		public const uint16 SDL_HAPTIC_INERTIA =	(1 << 9);
		public const uint16 SDL_HAPTIC_FRICTION =	(1 << 10);
		public const uint16 SDL_HAPTIC_CUSTOM =		(1 << 11);
		public const uint16 SDL_HAPTIC_GAIN =		(1 << 12);
		public const uint16 SDL_HAPTIC_AUTOCENTER =	(1 << 13);
		public const uint16 SDL_HAPTIC_STATUS =		(1 << 14);
		public const uint16 SDL_HAPTIC_PAUSE =		(1 << 15);

		/* SDL_HapticDirection type */
		public const uint8 SDL_HAPTIC_POLAR =		0;
		public const uint8 SDL_HAPTIC_CARTESIAN =	1;
		public const uint8 SDL_HAPTIC_SPHERICAL =	2;

		/* SDL_HapticRunEffect */
		public const uint32 SDL_HAPTIC_INFINITY = 4294967295U;

		[CRepr]
		public struct SDL_HapticDirection
		{
			public uint8 type;
			public int32[3] dir;
		}

		[CRepr]
		public struct SDL_HapticConstant
		{
			// Header
			public uint16 type;
			public SDL_HapticDirection direction;
			// Replay
			public uint32 length;
			public uint16 delay;
			// Trigger
			public uint16 button;
			public uint16 interval;
			// Constant
			public int16 level;
			// Envelope
			public uint16 attack_length;
			public uint16 attack_level;
			public uint16 fade_length;
			public uint16 fade_level;
		}

		[CRepr]
		public struct SDL_HapticPeriodic
		{
			// Header
			public uint16 type;
			public SDL_HapticDirection direction;
			// Replay
			public uint32 length;
			public uint16 delay;
			// Trigger
			public uint16 button;
			public uint16 interval;
			// Periodic
			public uint16 period;
			public int16 magnitude;
			public int16 offset;
			public uint16 phase;
			// Envelope
			public uint16 attack_length;
			public uint16 attack_level;
			public uint16 fade_length;
			public uint16 fade_level;
		}

		[CRepr]
		public struct SDL_HapticCondition
		{
			// Header
			public uint16 type;
			public SDL_HapticDirection direction;
			// Replay
			public uint32 length;
			public uint16 delay;
			// Trigger
			public uint16 button;
			public uint16 interval;
			// Condition
			public uint16[3] right_sat;
			public uint16[3] left_sat;
			public int16[3] right_coeff;
			public int16[3] left_coeff;
			public uint16[3] deadband;
			public int16[3] center;
		}

		[CRepr]
		public struct SDL_HapticRamp
		{
			// Header
			public uint16 type;
			public SDL_HapticDirection direction;
			// Replay
			public uint32 length;
			public uint16 delay;
			// Trigger
			public uint16 button;
			public uint16 interval;
			// Ramp
			public int16 start;
			public int16 end;
			// Envelope
			public uint16 attack_length;
			public uint16 attack_level;
			public uint16 fade_length;
			public uint16 fade_level;
		}

		[CRepr]
		public struct SDL_HapticLeftRight
		{
			// Header
			public uint16 type;
			// Replay
			public uint32 length;
			// Rumble
			public uint16 large_magnitude;
			public uint16 small_magnitude;
		}

		[CRepr]
		public struct SDL_HapticCustom
		{
			// Header
			public uint16 type;
			public SDL_HapticDirection direction;
			// Replay
			public uint32 length;
			public uint16 delay;
			// Trigger
			public uint16 button;
			public uint16 interval;
			// Custom
			public uint8 channels;
			public uint16 period;
			public uint16 samples;
			public uint16* data; // uint16*
			// Envelope
			public uint16 attack_length;
			public uint16 attack_level;
			public uint16 fade_length;
			public uint16 fade_level;
		}

		[Union]
		public struct SDL_HapticEffect
		{
			public uint16 type;
			public SDL_HapticConstant constant;
			public SDL_HapticPeriodic periodic;
			public SDL_HapticCondition condition;
			public SDL_HapticRamp ramp;
			public SDL_HapticLeftRight leftright;
			public SDL_HapticCustom custom;
		}

		public struct SDL_Haptic;

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticClose")]
		public static extern void HapticClose(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticDestroyEffect")]
		public static extern void HapticDestroyEffect(
			SDL_Haptic* haptic,
			int32 effect
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticEffectSupported")]
		public static extern int HapticEffectSupported(
			SDL_Haptic* haptic,
			ref SDL_HapticEffect effect
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticGetEffectStatus")]
		public static extern int HapticGetEffectStatus(
			SDL_Haptic* haptic,
			int32 effect
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticIndex")]
		public static extern int HapticIndex(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticName")]
		public static extern char8* HapticName(int32 device_index);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticNewEffect")]
		public static extern int32 HapticNewEffect(
			SDL_Haptic* haptic,
			ref SDL_HapticEffect effect
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticNumAxes")]
		public static extern int32 HapticNumAxes(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticNumEffects")]
		public static extern int32 HapticNumEffects(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticNumEffectsPlaying")]
		public static extern int32 HapticNumEffectsPlaying(SDL_Haptic* haptic);

		/* IntPtr refers to an SDL_Haptic* */
		[LinkName("SDL_HapticOpen")]
		public static extern SDL_Haptic* HapticOpen(int32 device_index);

		[LinkName("SDL_HapticOpened")]
		public static extern int32 HapticOpened(int32 device_index);

		/* IntPtr refers to an SDL_Haptic*, joystick to an SDL_Joystick* */
		[LinkName("SDL_HapticOpenFromJoystick")]
		public static extern SDL_Haptic* HapticOpenFromJoystick(
			SDL_Joystick* joystick
		);

		/* IntPtr refers to an SDL_Haptic* */
		[LinkName("SDL_HapticOpenFromMouse")]
		public static extern SDL_Haptic* HapticOpenFromMouse();

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticPause")]
		public static extern int32 HapticPause(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticQuery")]
		public static extern uint32 HapticQuery(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticRumbleInit")]
		public static extern int32 HapticRumbleInit(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticRumblePlay")]
		public static extern int32 HapticRumblePlay(
			SDL_Haptic* haptic,
			float strength,
			uint32 length
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticRumbleStop")]
		public static extern int32 HapticRumbleStop(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticRumbleSupported")]
		public static extern int32 HapticRumbleSupported(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticRunEffect")]
		public static extern int32 HapticRunEffect(
			SDL_Haptic* haptic,
			int32 effect,
			uint32 iterations
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticSetAutocenter")]
		public static extern int32 HapticSetAutocenter(
			SDL_Haptic* haptic,
			int32 autocenter
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticSetGain")]
		public static extern int32 HapticSetGain(
			SDL_Haptic* haptic,
			int32 gain
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticStopAll")]
		public static extern int32 HapticStopAll(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticStopEffect")]
		public static extern int32 HapticStopEffect(
			SDL_Haptic* haptic,
			int32 effect
		);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticUnpause")]
		public static extern int32 HapticUnpause(SDL_Haptic* haptic);

		/* haptic refers to an SDL_Haptic* */
		[LinkName("SDL_HapticUpdateEffect")]
		public static extern int32 HapticUpdateEffect(
			SDL_Haptic* haptic,
			int32 effect,
			ref SDL_HapticEffect data
		);

		
		[LinkName("SDL_JoystickIsHaptic")]
		public static extern int32 JoystickIsHaptic(SDL_Joystick* joystick);

		[LinkName("SDL_MouseIsHaptic")]
		public static extern int32 MouseIsHaptic();

		[LinkName("SDL_NumHaptics")]
		public static extern int32 NumHaptics();

		#endregion

		#region SDL_sensor.h

		/* This region is only available in 2.0.9 or higher. */

		public enum SDL_SensorType : int32
		{
			SDL_SENSOR_INVALID = -1,
			SDL_SENSOR_UNKNOWN,
			SDL_SENSOR_ACCEL,
			SDL_SENSOR_GYRO
		}

		public const float SDL_STANDARD_GRAVITY = 9.80665f;

		[LinkName("SDL_NumSensors")]
		public static extern int32 NumSensors();

		[LinkName("SDL_SensorGetDeviceName")]
		public static extern char8* SensorGetDeviceName(int32 device_index);

		[LinkName("SDL_SensorGetDeviceType")]
		public static extern SDL_SensorType SensorGetDeviceType(int32 device_index);

		[LinkName("SDL_SensorGetDeviceNonPortableType")]
		public static extern int SensorGetDeviceNonPortableType(int32 device_index);

		[LinkName("SDL_SensorGetDeviceInstanceID")]
		public static extern int32 SensorGetDeviceInstanceID(int32 device_index);

		/* IntPtr refers to an SDL_Sensor* */
		[LinkName("SDL_SensorOpen")]
		public static extern SDL_Sensor* SensorOpen(int device_index);

		/* IntPtr refers to an SDL_Sensor* */
		[LinkName("SDL_SensorFromInstanceID")]
		public static extern SDL_Sensor* SensorFromInstanceID(
			int32 instance_id
		);

		public struct SDL_Sensor;

		/* sensor refers to an SDL_Sensor* */
		[LinkName("SDL_SensorGetName")]
		public static extern char8* SensorGetName(SDL_Sensor* sensor);

		/* sensor refers to an SDL_Sensor* */
		[LinkName("SDL_SensorGetType")]
		public static extern SDL_SensorType SensorGetType(SDL_Sensor* sensor);

		/* sensor refers to an SDL_Sensor* */
		[LinkName("SDL_SensorGetNonPortableType")]
		public static extern int SensorGetNonPortableType(SDL_Sensor* sensor);

		/* sensor refers to an SDL_Sensor* */
		[LinkName("SDL_SensorGetInstanceID")]
		public static extern int32 SensorGetInstanceID(SDL_Sensor* sensor);

		/* sensor refers to an SDL_Sensor* */
		[LinkName("SDL_SensorGetData")]
		public static extern int SensorGetData(
			SDL_Sensor* sensor,
			float* data,
			int32 num_values
		);

		/* sensor refers to an SDL_Sensor* */
		[LinkName("SDL_SensorClose")]
		public static extern void SensorClose(SDL_Sensor* sensor);

		[LinkName("SDL_SensorUpdate")]
		public static extern void SensorUpdate();

		public const uint16 SDL_AUDIO_MASK_BITSIZE =	0xFF;
		public const uint16 SDL_AUDIO_MASK_DATATYPE =	(1 << 8);
		public const uint16 SDL_AUDIO_MASK_ENDIAN =	(1 << 12);
		public const uint16 SDL_AUDIO_MASK_SIGNED =	(1 << 15);

		public static uint16 SDL_AUDIO_BITSIZE(uint16 x)
		{
			return (uint16) (x & SDL_AUDIO_MASK_BITSIZE);
		}

		public static Bool AUDIO_ISFLOAT(uint16 x)
		{
			return (x & SDL_AUDIO_MASK_DATATYPE) != 0;
		}

		public static Bool AUDIO_ISBIGENDIAN(uint16 x)
		{
			return (x & SDL_AUDIO_MASK_ENDIAN) != 0;
		}

		public static Bool AUDIO_ISSIGNED(uint16 x)
		{
			return (x & SDL_AUDIO_MASK_SIGNED) != 0;
		}

		public static Bool AUDIO_ISINT(uint16 x)
		{
			return (x & SDL_AUDIO_MASK_DATATYPE) == 0;
		}

		public static Bool AUDIO_ISLITTLEENDIAN(uint16 x)
		{
			return (x & SDL_AUDIO_MASK_ENDIAN) == 0;
		}

		public static Bool AUDIO_ISUNSIGNED(uint16 x)
		{
			return (x & SDL_AUDIO_MASK_SIGNED) == 0;
		}

		public const uint16 AUDIO_U8 =		0x0008;
		public const uint16 AUDIO_S8 =		0x8008;
		public const uint16 AUDIO_U16LSB =	0x0010;
		public const uint16 AUDIO_S16LSB =	0x8010;
		public const uint16 AUDIO_U16MSB =	0x1010;
		public const uint16 AUDIO_S16MSB =	0x9010;
		public const uint16 AUDIO_U16 =		AUDIO_U16LSB;
		public const uint16 AUDIO_S16 =		AUDIO_S16LSB;
		public const uint16 AUDIO_S32LSB =	0x8020;
		public const uint16 AUDIO_S32MSB =	0x9020;
		public const uint16 AUDIO_S32 =		AUDIO_S32LSB;
		public const uint16 AUDIO_F32LSB =	0x8120;
		public const uint16 AUDIO_F32MSB =	0x9120;
		public const uint16 AUDIO_F32 =		AUDIO_F32LSB;

		public static readonly uint16 AUDIO_U16SYS =
			BitConverter.IsLittleEndian ? AUDIO_U16LSB : AUDIO_U16MSB;
		public static readonly uint16 AUDIO_S16SYS =
			BitConverter.IsLittleEndian ? AUDIO_S16LSB : AUDIO_S16MSB;
		public static readonly uint16 AUDIO_S32SYS =
			BitConverter.IsLittleEndian ? AUDIO_S32LSB : AUDIO_S32MSB;
		public static readonly uint16 AUDIO_F32SYS =
			BitConverter.IsLittleEndian ? AUDIO_F32LSB : AUDIO_F32MSB;

		public const uint32 AUDIO_ALLOW_FREQUENCY_CHANGE =	0x00000001;
		public const uint32 AUDIO_ALLOW_FORMAT_CHANGE =	0x00000002;
		public const uint32 AUDIO_ALLOW_CHANNELS_CHANGE =	0x00000004;
		public const uint32 AUDIO_ALLOW_SAMPLES_CHANGE =	0x00000008;
		public const uint32 AUDIO_ALLOW_ANY_CHANGE = (
			AUDIO_ALLOW_FREQUENCY_CHANGE |
			AUDIO_ALLOW_FORMAT_CHANGE |
			AUDIO_ALLOW_CHANNELS_CHANGE |
			AUDIO_ALLOW_SAMPLES_CHANGE
		);

		public const int32 MIX_MAXVOLUME = 128;

		public enum SDL_AudioStatus : int32
		{
			SDL_AUDIO_STOPPED,
			SDL_AUDIO_PLAYING,
			SDL_AUDIO_PAUSED
		}

		[CRepr]
		public struct SDL_AudioSpec
		{
			public int32 freq;
			public uint16 format; // SDL_AudioFormat
			public uint8 channels;
			public uint8 silence;
			public uint16 samples;
			public uint32 size;
			public AudioCallback callback;
			public void* userdata; // void*
		}

		/* userdata refers to a void*, stream to a Uint8 */
		public function void AudioCallback(
			void* userdata,
			uint8* stream,
			int len
		);

		[LinkName("SDL_AudioInit")]
		public static extern int32 AudioInit(char8* driver_name);

		[LinkName("SDL_AudioQuit")]
		public static extern void AudioQuit();

		[LinkName("SDL_CloseAudio")]
		public static extern void CloseAudio();

		/* dev refers to an SDL_AudioDeviceID */
		[LinkName("SDL_CloseAudioDevice")]
		public static extern void CloseAudioDevice(uint32 dev);

		/* audio_buf refers to a malloc()'d buffer from SDL_LoadWAV */
		[LinkName("SDL_FreeWAV")]
		public static extern void FreeWAV(void* audio_buf);

		[LinkName("SDL_GetAudioDeviceName")]
		public static extern char8* GetAudioDeviceName(
			int32 index,
			int32 iscapture
		);

		/* dev refers to an SDL_AudioDeviceID */
		[LinkName("SDL_GetAudioDeviceStatus")]
		public static extern SDL_AudioStatus GetAudioDeviceStatus(
			uint32 dev
		);

		[LinkName("SDL_GetAudioDriver")]
		public static extern char8* GetAudioDriver(int32 index);

		[LinkName("SDL_GetAudioStatus")]
		public static extern SDL_AudioStatus GetAudioStatus();

		[LinkName("SDL_GetCurrentAudioDriver")]
		public static extern char8* GetCurrentAudioDriver();

		[LinkName("SDL_GetNumAudioDevices")]
		public static extern int GetNumAudioDevices(int iscapture);

		[LinkName("SDL_GetNumAudioDrivers")]
		public static extern int GetNumAudioDrivers();

		/* audio_buf will refer to a malloc()'d uint8 buffer */
		/* THIS IS AN RWops FUNCTION! */
		[LinkName("SDL_LoadWAV_RW")]
		private static extern SDL_AudioSpec* LoadWAV_RW(
			RWOps* src,
			int32 freesrc,
			SDL_AudioSpec* spec,
			out uint8* audio_buf,
			out uint32 audio_len
		);
		public static SDL_AudioSpec* LoadWAV(
			char8* file,
			SDL_AudioSpec* spec,
			out uint8* audio_buf,
			out uint32 audio_len
		) {
			RWOps* rwops = RWFromFile(file, "rb");
			SDL_AudioSpec* result_ptr = LoadWAV_RW(
				rwops,
				1,
				spec,
				out audio_buf,
				out audio_len
			);
			return result_ptr;
		}

		[LinkName("SDL_LockAudio")]
		public static extern void LockAudio();

		/* dev refers to an SDL_AudioDeviceID */
		[LinkName("SDL_LockAudioDevice")]
		public static extern void LockAudioDevice(uint32 dev);

		[LinkName("SDL_MixAudio")]
		public static extern void MixAudio(
			uint8* dst,
			uint8* src,
			uint32 len,
			int32 volume
		);

		/* format refers to an SDL_AudioFormat */
		[LinkName("SDL_MixAudioFormat")]
		public static extern void MixAudioFormat(
			uint8* dst,
			uint8* src,
			uint16 format,
			uint32 len,
			int32 volume
		);

		[LinkName("SDL_OpenAudio")]
		public static extern int OpenAudio(
			ref SDL_AudioSpec desired,
			SDL_AudioSpec* obtained
		);

		/* uint32 refers to an SDL_AudioDeviceID */
		[LinkName("SDL_OpenAudioDevice")]
		private static extern AudioDeviceID OpenAudioDevice(
			char8* device,
			int32 iscapture,
			ref SDL_AudioSpec desired,
			out SDL_AudioSpec obtained,
			int32 allowed_changes
		);

		struct AudioDeviceID : uint32
		{

		}

		[LinkName("SDL_PauseAudio")]
		public static extern void PauseAudio(int32 pause_on);

		/* dev refers to an SDL_AudioDeviceID */
		[LinkName("SDL_PauseAudioDevice")]
		public static extern void PauseAudioDevice(
			uint32 dev,
			int32 pause_on
		);

		[LinkName("SDL_UnlockAudio")]
		public static extern void UnlockAudio();

		/* dev refers to an SDL_AudioDeviceID */
		[LinkName("SDL_UnlockAudioDevice")]
		public static extern void UnlockAudioDevice(AudioDeviceID dev);

		/* dev refers to an SDL_AudioDeviceID, data to a void* */
		/* Only available in 2.0.4 */
		[LinkName("SDL_QueueAudio")]
		public static extern int QueueAudio(
			AudioDeviceID dev,
			void* data,
			uint32 len
		);

		/* dev refers to an SDL_AudioDeviceID, data to a void* */
		/* Only available in 2.0.5 */
		[LinkName("SDL_DequeueAudio")]
		public static extern uint32 DequeueAudio(
			AudioDeviceID dev,
			void* data,
			uint32 len
		);

		/* dev refers to an SDL_AudioDeviceID */
		/* Only available in 2.0.4 */
		[LinkName("SDL_GetQueuedAudioSize")]
		public static extern uint32 GetQueuedAudioSize(AudioDeviceID dev);

		/* dev refers to an SDL_AudioDeviceID */
		/* Only available in 2.0.4 */
		[LinkName("SDL_ClearQueuedAudio")]
		public static extern void ClearQueuedAudio(AudioDeviceID dev);

		/* src_format and dst_format refer to SDL_AudioFormats.
		 * IntPtr refers to an SDL_AudioStream*.
		 * Only available in 2.0.7
		 */

		public struct AudioStream;

		[LinkName("SDL_NewAudioStream")]
		public static extern AudioStream* NewAudioStream(
			uint16 src_format,
			uint8 src_channels,
			int32 src_rate,
			uint16 dst_format,
			uint8 dst_channels,
			int32 dst_rate
		);

		/* stream refers to an SDL_AudioStream*, buf to a void*.
		 * Only available in 2.0.7
		 */
		[LinkName("SDL_AudioStreamPut")]
		public static extern int AudioStreamPut(
			AudioStream* stream,
			void* buf,
			int len
		);

		/* stream refers to an SDL_AudioStream*, buf to a void*.
		 * Only available in 2.0.7
		 */
		[LinkName("SDL_AudioStreamGet")]
		public static extern int AudioStreamGet(
			AudioStream* stream,
			void* buf,
			int len
		);

		/* stream refers to an SDL_AudioStream*.
		 * Only available in 2.0.7
		 */
		[LinkName("SDL_AudioStreamAvailable")]
		public static extern int AudioStreamAvailable(AudioStream* stream);

		/* stream refers to an SDL_AudioStream*.
		 * Only available in 2.0.7
		 */
		[LinkName("SDL_AudioStreamClear")]
		public static extern void AudioStreamClear(AudioStream* stream);

		/* stream refers to an SDL_AudioStream*.
		 * Only available in 2.0.7
		 */
		[LinkName("SDL_FreeAudioStream")]
		public static extern void FreeAudioStream(AudioStream* stream);

		/* System timers rely on different OS mechanisms depending on
		 * which operating system SDL2 is compiled against.
		 */

		/* Compare tick values, return true if A has passed B. Introduced in SDL 2.0.1,
		 * but does not require it (it was a macro).
		 */
		public static Bool TICKS_PASSED(uint32 A, uint32 B)
		{
			return ((int32)(B - A) <= 0);
		}

		/* Delays the thread's processing based on the milliseconds parameter */
		[LinkName("SDL_Delay")]
		public static extern void Delay(uint32 ms);

		/* Returns the milliseconds that have passed since SDL was initialized */
		[LinkName("SDL_GetTicks")]
		public static extern uint32 GetTicks();

		/* Get the current value of the high resolution counter */
		[LinkName("SDL_GetPerformanceCounter")]
		public static extern uint64 GetPerformanceCounter();

		/* Get the count per second of the high resolution counter */
		[LinkName("SDL_GetPerformanceFrequency")]
		public static extern uint64 GetPerformanceFrequency();

		/* param refers to a void* */
		public function uint32 TimerCallback(uint32 interval, void* param);

		/* int refers to an SDL_TimerID, param to a void* */
		[LinkName("SDL_AddTimer")]
		public static extern int AddTimer(
			uint32 interval,
			TimerCallback callback,
			void* param
		);

		/* id refers to an SDL_TimerID */
		[LinkName("SDL_RemoveTimer")]
		public static extern Bool RemoveTimer(int32 id);

		/* Windows */

		public function void WindowsMessageHook(
			void* userdata,
			void* hWnd,
			uint32 message,
			uint64 wParam,
			int64 lParam
		);

		[LinkName("SDL_SetWindowsMessageHook")]
		public static extern void SetWindowsMessageHook(
			WindowsMessageHook callback,
			void* userdata
		);

		/* iOS */

		public function void iPhoneAnimationCallback(void* p);

		[LinkName("SDL_iPhoneSetAnimationCallback")]
		public static extern int iPhoneSetAnimationCallback(
			Window* window, /* SDL_Window* */
			int32 interval,
			iPhoneAnimationCallback callback,
			void* callbackParam
		);

		[LinkName("SDL_iPhoneSetEventPump")]
		public static extern void iPhoneSetEventPump(Bool enabled);

		/* Android */

		public const int SDL_ANDROID_EXTERNAL_STORAGE_READ = 0x01;
		public const int SDL_ANDROID_EXTERNAL_STORAGE_WRITE = 0x02;

		public struct JNIEnv;

		/* IntPtr refers to a JNIEnv* */
		[LinkName("SDL_AndroidGetJNIEnv")]
		public static extern JNIEnv* AndroidGetJNIEnv();

		/* IntPtr refers to a jobject */
		[LinkName("SDL_AndroidGetActivity")]
		public static extern JNIEnv* AndroidGetActivity();

		[LinkName("SDL_IsAndroidTV")]
		public static extern Bool IsAndroidTV();

		[LinkName("SDL_IsChromebook")]
		public static extern Bool IsChromebook();

		[LinkName("SDL_IsDeXMode")]
		public static extern Bool IsDeXMode();

		[LinkName("SDL_AndroidBackButton")]
		public static extern void AndroidBackButton();

		[LinkName("SDL_AndroidGetInternalStoragePath")]
		private static extern char8* AndroidGetInternalStoragePath();

		[LinkName("SDL_AndroidGetExternalStorageState")]
		public static extern int AndroidGetExternalStorageState();

		[LinkName("SDL_AndroidGetExternalStoragePath")]
		private static extern char8* AndroidGetExternalStoragePath();

		/* WinRT */

		public enum SDL_WinRT_DeviceFamily
		{
			SDL_WINRT_DEVICEFAMILY_UNKNOWN,
			SDL_WINRT_DEVICEFAMILY_DESKTOP,
			SDL_WINRT_DEVICEFAMILY_MOBILE,
			SDL_WINRT_DEVICEFAMILY_XBOX
		}

		[LinkName("SDL_WinRTGetDeviceFamily")]
		public static extern SDL_WinRT_DeviceFamily WinRTGetDeviceFamily();

		[LinkName("SDL_IsTablet")]
		public static extern Bool IsTablet();

		public enum SDL_SYSWM_TYPE : int32
		{
			SDL_SYSWM_UNKNOWN,
			SDL_SYSWM_WINDOWS,
			SDL_SYSWM_X11,
			SDL_SYSWM_DIRECTFB,
			SDL_SYSWM_COCOA,
			SDL_SYSWM_UIKIT,
			SDL_SYSWM_WAYLAND,
			SDL_SYSWM_MIR,
			SDL_SYSWM_WINRT,
			SDL_SYSWM_ANDROID
		}

		// FIXME: I wish these weren't public...
#if BF_PLATFORM_WINDOWS
		[CRepr]
		public struct INTERNAL_windows_wminfo
		{
			public Windows.HWnd window; // Refers to an HWND
			public Windows.Handle hdc; // Refers to an HDC
			public Windows.HInstance hinstance; // Refers to an HINSTANCE
		}
#endif

		[CRepr]
		public struct INTERNAL_winrt_wminfo
		{
			public void* window; // Refers to an IInspectable*
		}

		[CRepr]
		public struct INTERNAL_x11_wminfo
		{
			public void* display; // Refers to a Display*
			public void* window; // Refers to a Window (XID, use ToInt64!)
		}

		[CRepr]
		public struct INTERNAL_directfb_wminfo
		{
			public void* dfb; // Refers to an IDirectFB*
			public void* window; // Refers to an IDirectFBWindow*
			public void* surface; // Refers to an IDirectFBSurface*
		}

		[CRepr]
		public struct INTERNAL_cocoa_wminfo
		{
			public void* window; // Refers to an NSWindow*
		}

		[CRepr]
		public struct INTERNAL_uikit_wminfo
		{
			public void* window; // Refers to a UIWindow*
			public uint32 framebuffer;
			public uint32 colorbuffer;
			public uint32 resolveFramebuffer;
		}

		[CRepr]
		public struct INTERNAL_wayland_wminfo
		{
			public void* display; // Refers to a wl_display*
			public void* surface; // Refers to a wl_surface*
			public void* shell_surface; // Refers to a wl_shell_surface*
		}

		[CRepr]
		public struct INTERNAL_mir_wminfo
		{
			public void* connection; // Refers to a MirConnection*
			public void* surface; // Refers to a MirSurface*
		}

		[CRepr]
		public struct INTERNAL_android_wminfo
		{
			public void* window; // Refers to an ANativeWindow
			public void* surface; // Refers to an EGLSurface
		}

		[CRepr, Union]
		public struct INTERNAL_SysWMDriverUnion
		{
#if BF_PLATFORM_WINDOWS
			public INTERNAL_windows_wminfo win;
#endif
			public INTERNAL_winrt_wminfo winrt;
			public INTERNAL_x11_wminfo x11;
			public INTERNAL_directfb_wminfo dfb;
			public INTERNAL_cocoa_wminfo cocoa;
			public INTERNAL_uikit_wminfo uikit;
			public INTERNAL_wayland_wminfo wl;
			public INTERNAL_mir_wminfo mir;
			public INTERNAL_android_wminfo android;
			// private int dummy;
		}

		[CRepr]
		public struct SDL_SysWMinfo
		{
			public Version version;
			public SDL_SYSWM_TYPE subsystem;
			public INTERNAL_SysWMDriverUnion info;
		}
		
		[LinkName("SDL_GetWindowWMInfo")]
		public static extern Bool GetWindowWMInfo(
			Window* window,
			ref SDL_SysWMinfo info
		);

		/* Only available in 2.0.1 */
		[LinkName("SDL_GetBasePath")]
		public static extern char8* GetBasePath();

		/* Only available in 2.0.1 */
		[LinkName("SDL_GetPrefPath")]
		public static extern char8* GetPrefPath(char8* org, char8* app);

		public enum SDL_PowerState : int32
		{
			SDL_POWERSTATE_UNKNOWN = 0,
			SDL_POWERSTATE_ON_BATTERY,
			SDL_POWERSTATE_NO_BATTERY,
			SDL_POWERSTATE_CHARGING,
			SDL_POWERSTATE_CHARGED
		}

		[LinkName("SDL_GetPowerInfo")]
		public static extern SDL_PowerState GetPowerInfo(
			out int32 secs,
			out int32 pct
		);

		[LinkName("SDL_GetCPUCount")]
		public static extern int32 GetCPUCount();

		/* Only available in 2.0.1 */
		[LinkName("SDL_GetSystemRAM")]
		public static extern int32 GetSystemRAM();
	}
}
