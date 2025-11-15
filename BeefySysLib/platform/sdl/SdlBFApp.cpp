#include "SdlBFApp.h"
#include "BFApp.h"
#include "Common.h"
#include "GLRenderDevice.h"
#include "platform/PlatformHelper.h"
#include "platform/PlatformInterface.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_video.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <dlfcn.h>

USING_NS_BF;

///

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "version.lib")

bool (SDLCALL* bf_SDL_Init)(SDL_InitFlags flags);
void (SDLCALL* bf_SDL_Quit)(void);
void* (SDLCALL* bf_SDL_malloc)(size_t size);
void (SDLCALL* bf_SDL_free)(void* mem);
void (SDLCALL* bf_SDL_memset)(void* dest, int c, size_t len);
void* (SDLCALL* bf_SDL_memcpy)(void *dst, const void *src, size_t len);

SDL_PropertiesID (SDLCALL* bf_SDL_CreateProperties)(void);
bool (SDLCALL* bf_SDL_SetNumberProperty)(SDL_PropertiesID props, const char* name, int64_t value);
bool (SDLCALL* bf_SDL_SetBooleanProperty)(SDL_PropertiesID props, const char* name, bool value);
bool (SDLCALL* bf_SDL_SetStringProperty)(SDL_PropertiesID props, const char* name, const char* value);
bool (SDLCALL* bf_SDL_SetPointerProperty)(SDL_PropertiesID props, const char *name, void *value);  

SDL_Window* (SDLCALL* bf_SDL_CreateWindowWithProperties)(SDL_PropertiesID props); 
SDL_WindowID (SDLCALL* bf_SDL_GetWindowID)(SDL_Window* window);
void (SDLCALL* bf_SDL_DestroyWindow)(SDL_Window* window);
bool (SDLCALL* bf_SDL_GetWindowPosition)(SDL_Window* window,int* x, int* y);
bool (SDLCALL* bf_SDL_SetWindowPosition)(SDL_Window* window, int x, int y);
bool (SDLCALL* bf_SDL_GetWindowSize)(SDL_Window* window, int* w, int* h);
bool (SDLCALL* bf_SDL_SetWindowSize)(SDL_Window* window, int w, int h);

char* (SDLCALL* bf_SDL_GetClipboardText)(void);
bool (SDLCALL* bf_SDL_SetClipboardText)(const char* text);
void* (SDLCALL* bf_SDL_GetClipboardData)(const char *mime_type, size_t *size);
bool (SDLCALL* bf_SDL_SetClipboardData)(SDL_ClipboardDataCallback callback, SDL_ClipboardCleanupCallback cleanup, void *userdata, const char **mime_types, size_t num_mime_types);
bool (SDLCALL* bf_SDL_StartTextInput)(SDL_Window* window);
bool (SDLCALL* bf_SDL_StopTextInput)(SDL_Window* window);

bool (SDLCALL* bf_SDL_PollEvent)(SDL_Event* event);
bool (SDLCALL* bf_SDL_PushEvent)(SDL_Event* event);
void (SDLCALL* bf_SDL_PumpEvents)(void);
int (SDLCALL* bf_SDL_PeepEvents)(SDL_Event *events, int numevents, SDL_EventAction action, Uint32 minType, Uint32 maxType);
bool (SDLCALL* bf_SDL_SetError)(const char *fmt, ...);
const char* (SDLCALL* bf_SDL_GetError)(void);

bool (SDLCALL* bf_SDL_SetHint)(const char *name, const char *value);

SDL_DisplayID (SDLCALL* bf_SDL_GetPrimaryDisplay)(void);
SDL_DisplayID* (SDLCALL* bf_SDL_GetDisplays)(int* count);
bool (SDLCALL* bf_SDL_GetDisplayBounds)(SDL_DisplayID displayID, SDL_Rect* rect);
SDL_DisplayMode* (SDLCALL* bf_SDL_GetDesktopDisplayMode)(SDL_DisplayID displayID);
bool (SDLCALL* bf_SDL_HasRectIntersection)(const SDL_Rect* A, const SDL_Rect* B);

SDL_GLContext (SDLCALL* bf_SDL_GL_CreateContext)(SDL_Window* window);
bool (SDLCALL* bf_SDL_GL_MakeCurrent)(SDL_Window* window, SDL_GLContext context);
bool (SDLCALL* bf_SDL_GL_SetAttribute)(SDL_GLAttr attr, int value);
void* (SDLCALL* bf_SDL_GL_GetProcAddress)(const char* proc);
bool (SDLCALL* bf_SDL_GL_SwapWindow)(SDL_Window* window);

struct AdjustedMonRect
{
	int mMonCount;
	int mX;
	int mY;
	int mWidth;
	int mHeight;
};

static int bfMouseBtnOf[4] = {NULL, 0, 2, 1}; // Translate SDL mouse buttons to what Beef expects.

static const char* mimeTypes[] = 
{ 
	"text/plain;charset=utf-8",
	"text/vnd.beeflang.bf-text",
	"text/vnd.beeflang.file-list"
};

static HMODULE gSDLModule;

static HMODULE GetSDLModule(const StringImpl& installDir)
{
	if (gSDLModule == NULL)
	{
#if defined (BF_PLATFORM_WINDOWS)
		String loadPath = installDir + "SDL3.dll";
		gSDLModule = ::LoadLibraryA(loadPath.c_str());
#elif defined (BF_PLATFORM_LINUX)
		String loadPath = "libSDL3.so";
		gSDLModule = dlopen(loadPath.c_str(), RTLD_LAZY);
#endif
		if (gSDLModule == NULL)
		{
#ifdef BF_PLATFORM_WINDOWS
			::MessageBoxA(NULL, "Failed to load SDL3.dll", "FATAL ERROR", MB_OK | MB_ICONERROR);
			::ExitProcess(1);
#endif
			BF_FATAL("Failed to load libSDL3.so");
		}
	}
	return gSDLModule;
}

template <typename T>
static void BFGetSDLProc(T& proc, const char* name, const StringImpl& installDir)
{
#if defined (BF_PLATFORM_WINDOWS)
	proc = (T)::GetProcAddress(GetSDLModule(installDir), name);
#elif defined (BF_PLATFORM_LINUX)
	proc = (T)dlsym(GetSDLModule(installDir), name);
#endif
}

#define BF_GET_SDLPROC(name) BFGetSDLProc(bf_##name, #name, mInstallDir)

SdlBFWindow::SdlBFWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags)
{
	SDL_PropertiesID props = bf_SDL_CreateProperties();

	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_OPENGL_BOOLEAN, true);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, (windowFlags & BFWINDOW_RESIZABLE) > 0);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, (windowFlags & BFWINDOW_FULLSCREEN) > 0);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, (windowFlags & BFWINDOW_BORDER) == 0);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_TOOLTIP_BOOLEAN, (windowFlags & BFWINDOW_TOOLTIP) > 0);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_TRANSPARENT_BOOLEAN, (windowFlags & BFWINDOW_DEST_ALPHA) > 0);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_MENU_BOOLEAN, (windowFlags & BFWINDOW_FAKEFOCUS) > 0);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FOCUSABLE_BOOLEAN, (windowFlags & BFWINDOW_FAKEFOCUS) == 0);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_MODAL_BOOLEAN, (windowFlags & BFWINDOW_MODAL) > 0);
	bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, (windowFlags & BFWINDOW_TOPMOST) > 0);

	if (parent != NULL) 
		bf_SDL_SetPointerProperty(props, SDL_PROP_WINDOW_CREATE_PARENT_POINTER, ((SdlBFWindow*)parent)->mSDLWindow);

	if (windowFlags)
#ifdef BF_PLATFORM_FULLSCREEN
		bf_SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
#endif

	bf_SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, x);
	bf_SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, y);
	bf_SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
	bf_SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
	bf_SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, title.c_str());

	mSDLWindow = bf_SDL_CreateWindowWithProperties(props);

//	printf("Created %i : %s\n", bf_SDL_GetWindowID(mSDLWindow), title.c_str());

	bf_SDL_StartTextInput(mSDLWindow);

#ifndef BF_PLATFORM_OPENGL_ES2
	bf_SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	bf_SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	bf_SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

	if(((SdlBFApp*)gBFApp)->mGLContext == NULL)
	{
		if (!(((SdlBFApp*)gBFApp)->mGLContext = bf_SDL_GL_CreateContext(mSDLWindow)))
		{
			String str = StrFormat(
	#ifdef BF_PLATFORM_OPENGL_ES2
				"Unable to create SDL OpenGLES context: %s"
	#else
				"Unable to create SDL OpenGL context: %s"
	#endif
				, bf_SDL_GetError());


			BF_FATAL(str.c_str());
			bf_SDL_Quit();
			exit(2);
		}
	}

#ifndef BF_PLATFORM_OPENGL_ES2
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

#ifndef BF_PLATFORM_OPENGL_ES2
	//glEnableClientState(GL_INDEX_ARRAY);
#endif

	mIsMouseInside = false;
	mHasPositionInit = false;
	mRenderWindow = new GLRenderWindow((GLRenderDevice*)gBFApp->mRenderDevice, mSDLWindow);
	mRenderWindow->mWindow = this;
	gBFApp->mRenderDevice->AddRenderWindow(mRenderWindow);

	mParent = parent;
	if (parent != NULL)
		parent->mChildren.push_back(this);
}

SdlBFWindow::~SdlBFWindow()
{
	if (mSDLWindow != NULL)
		Destroy();
}

void SdlBFWindow::Destroy()
{
//	printf("Destroy %i\n", bf_SDL_GetWindowID(this->mSDLWindow));

	SdlBFApp* app = (SdlBFApp*)gBFApp;
	app->mSdlWindowMap.Remove(bf_SDL_GetWindowID(mSDLWindow));

	bf_SDL_StopTextInput(mSDLWindow);
	bf_SDL_DestroyWindow(mSDLWindow);
	mSDLWindow = NULL;
}

bool SdlBFWindow::TryClose()
{
//	printf("TryClose %i\n", bf_SDL_GetWindowID(this->mSDLWindow));

	mLostFocusFunc(this);
	if(this->mParent != NULL)
	{
		mGotFocusFunc(this->mParent);
	}

	gBFApp->RemoveWindow(this);
	return mSDLWindow == NULL;
}

static int SDLConvertKeyCode(SDL_Keycode scanCode)
{
	if ((scanCode >= SDLK_A) && (scanCode <= SDLK_Z))
		return (scanCode - SDLK_A) + 'A';
	if ((scanCode >= SDLK_1) && (scanCode <= SDLK_9))
		return (scanCode - SDLK_1) + '1';

	switch (scanCode)
	{
	case SDLK_9: return '0';
    case SDLK_CANCEL: return 0x03;
    case SDLK_BACKSPACE: return 0x08;
    case SDLK_TAB: return 0x09;
    case SDLK_CLEAR: return 0x0C;
    case SDLK_RETURN: return 0x0D;
    case SDLK_LSHIFT: return 0x10;
	case SDLK_RSHIFT: return 0x10;
    case SDLK_LCTRL: return 0x11;
	case SDLK_RCTRL: return 0x11;
    case SDLK_MENU: return 0x12;
    case SDLK_PAUSE: return 0x13;
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG1): return 0x15;
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG2): return 0x15;
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG3): return 0x17;
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG4): return 0x18;
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG5): return 0x19;
    case SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LANG6): return 0x19;
    case SDLK_ESCAPE: return 0x1B;
    case SDLK_SPACE: return 0x20;
    case SDLK_PAGEUP: return 0x21;
    case SDLK_PAGEDOWN: return 0x22;
    case SDLK_END: return 0x23;
    case SDLK_HOME: return 0x24;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    case SDLK_SELECT: return 0x29;
    case SDLK_PRINTSCREEN: return 0x2A;
    case SDLK_EXECUTE: return 0x2B;
    case SDLK_INSERT: return 0x2D;
    case SDLK_DELETE: return 0x2E;
    case SDLK_HELP: return 0x2F;
    case SDLK_LGUI: return 0x5B;
    case SDLK_RGUI: return 0x5C;
	case SDLK_KP_0: return 0x60;
	case SDLK_KP_1: return 0x61;
    case SDLK_KP_2: return 0x62;
    case SDLK_KP_3: return 0x63;
    case SDLK_KP_4: return 0x64;
    case SDLK_KP_5: return 0x65;
    case SDLK_KP_6: return 0x66;
    case SDLK_KP_7: return 0x67;
    case SDLK_KP_8: return 0x68;
    case SDLK_KP_9: return 0x69;
    case SDLK_KP_MULTIPLY: return 0x6A;
    case SDLK_KP_PLUS: return 0x6B;
    case SDLK_SEPARATOR: return 0x6C;
    case SDLK_KP_MINUS: return 0x6D;
    case SDLK_KP_PERIOD: return 0x6E;
    case SDLK_KP_DIVIDE: return 0x6F;
    case SDLK_F1: return 0x70;
    case SDLK_F2: return 0x71;
    case SDLK_F3: return 0x72;
    case SDLK_F4: return 0x73;
    case SDLK_F5: return 0x74;
    case SDLK_F6: return 0x75;
    case SDLK_F7: return 0x76;
    case SDLK_F8: return 0x77;
    case SDLK_F9: return 0x78;
    case SDLK_F10: return 0x79;
    case SDLK_F11: return 0x7A;
    case SDLK_F12: return 0x7B;
    case SDLK_NUMLOCKCLEAR: return 0x90;
    case SDLK_SCROLLLOCK: return 0x91;
    case SDLK_GRAVE: return 0xC0;
    //case SDLK_COMMAND: return 0xF0;
	}
	return 0;
}

#ifdef _WIN32
extern HINSTANCE gDLLInstance;
#endif

SdlBFApp::SdlBFApp()
{
	mRunning = false;
	mRenderDevice = NULL;
	mSdlClipboardData = new SdlClipboardData();

	Beefy::String exePath;
	BfpGetStrHelper(exePath, [](char* outStr, int* inOutStrSize, BfpResult* result)
		{
			BfpSystem_GetExecutablePath(outStr, inOutStrSize, (BfpSystemResult*)result);
		});

	mInstallDir = GetFileDir(exePath) + "/";

	int lastSlash = std::max((int)mInstallDir.LastIndexOf('\\'), (int)mInstallDir.LastIndexOf('/'));
	if (lastSlash != -1)
		mInstallDir = mInstallDir.Substring(0, lastSlash);

    //TODO: We're not properly using DataDir vs InstallDir
#if (!defined BFSYSLIB_DYNAMIC) && (defined BF_RESOURCES_REL_DIR)
    mInstallDir += "/" + Beefy::UTF8Decode(BF_RESOURCES_REL_DIR);
#endif

    mInstallDir += "/";

	if (bf_SDL_Init == NULL)
	{
		BF_GET_SDLPROC(SDL_Init);
		BF_GET_SDLPROC(SDL_Quit);
		BF_GET_SDLPROC(SDL_malloc);
		BF_GET_SDLPROC(SDL_free);
		BF_GET_SDLPROC(SDL_memset);
		BF_GET_SDLPROC(SDL_memcpy);

		BF_GET_SDLPROC(SDL_CreateProperties);
		BF_GET_SDLPROC(SDL_SetNumberProperty);
		BF_GET_SDLPROC(SDL_SetBooleanProperty);
		BF_GET_SDLPROC(SDL_SetStringProperty);
		BF_GET_SDLPROC(SDL_SetPointerProperty);

		BF_GET_SDLPROC(SDL_CreateWindowWithProperties);
		BF_GET_SDLPROC(SDL_GetWindowID);
		BF_GET_SDLPROC(SDL_DestroyWindow);
		BF_GET_SDLPROC(SDL_GetWindowPosition);
		BF_GET_SDLPROC(SDL_SetWindowPosition);
		BF_GET_SDLPROC(SDL_GetWindowSize);
		BF_GET_SDLPROC(SDL_SetWindowSize);

		BF_GET_SDLPROC(SDL_GetClipboardText);
		BF_GET_SDLPROC(SDL_SetClipboardText);
		BF_GET_SDLPROC(SDL_GetClipboardData);
		BF_GET_SDLPROC(SDL_SetClipboardData);
		BF_GET_SDLPROC(SDL_StartTextInput);
		BF_GET_SDLPROC(SDL_StopTextInput);

		BF_GET_SDLPROC(SDL_PollEvent);
		BF_GET_SDLPROC(SDL_PushEvent);
		BF_GET_SDLPROC(SDL_PumpEvents);
		BF_GET_SDLPROC(SDL_PeepEvents);
		BF_GET_SDLPROC(SDL_SetError);
		BF_GET_SDLPROC(SDL_GetError);

		BF_GET_SDLPROC(SDL_SetHint);

		BF_GET_SDLPROC(SDL_GetPrimaryDisplay);
		BF_GET_SDLPROC(SDL_GetDisplays);
		BF_GET_SDLPROC(SDL_GetDisplayBounds);
		BF_GET_SDLPROC(SDL_GetDesktopDisplayMode);
		BF_GET_SDLPROC(SDL_HasRectIntersection);

		BF_GET_SDLPROC(SDL_GL_CreateContext);
		BF_GET_SDLPROC(SDL_GL_MakeCurrent);
		BF_GET_SDLPROC(SDL_GL_SetAttribute);
		BF_GET_SDLPROC(SDL_GL_GetProcAddress);
		BF_GET_SDLPROC(SDL_GL_SwapWindow);
	}

	mDataDir = mInstallDir;

	if (bf_SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD) < 0)
		BF_FATAL(StrFormat("Unable to initialize SDL: %s", bf_SDL_GetError()).c_str());
}

SdlBFApp::~SdlBFApp()
{
	delete mSdlClipboardData;
}

SdlBFWindow* SdlBFApp::GetSdlWindowFromId(uint32 id)
{
	SdlBFWindow* window = NULL;
	mSdlWindowMap.TryGetValue(id, &window);
	return window;
}

void SdlBFApp::Init()
{
	mRunning = true;
	mInMsgProc = false;

//	bf_SDL_SetHint(SDL_HINT_EVENT_LOGGING, "1");

	mRenderDevice = new GLRenderDevice();
	mRenderDevice->Init(this);
}

void SdlBFApp::Run()
{
	while (mRunning)
	{
		SDL_Event sdlEvent;
		while (true)
		{
            {
                //Beefy::DebugTimeGuard suspendTimeGuard(30, "BFApp::Run1");
                if (!bf_SDL_PollEvent(&sdlEvent))
                    break;
            }

            //Beefy::DebugTimeGuard suspendTimeGuard(30, "BFApp::Run2");

			switch (sdlEvent.type)
			{
			case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.window.windowID);
					if(sdlBFWindow != NULL)
						if (sdlBFWindow->mCloseQueryFunc(sdlBFWindow) != 0)
							gBFApp->RemoveWindow(sdlBFWindow);
				}
				break;
			case SDL_EVENT_WINDOW_FOCUS_GAINED:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.window.windowID);
					if(sdlBFWindow != NULL)
						sdlBFWindow->mGotFocusFunc(sdlBFWindow);
				}
				break;
			case SDL_EVENT_WINDOW_FOCUS_LOST:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.window.windowID);
					if(sdlBFWindow != NULL)
						sdlBFWindow->mLostFocusFunc(sdlBFWindow);
				}
				break;
			case SDL_EVENT_MOUSE_BUTTON_UP:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.button.windowID);
					if (sdlBFWindow != NULL)
						sdlBFWindow->mMouseUpFunc(sdlBFWindow, sdlEvent.button.x, sdlEvent.button.y, bfMouseBtnOf[sdlEvent.button.button]);
				}
				break;
			case SDL_EVENT_MOUSE_BUTTON_DOWN:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.button.windowID);
					if (sdlBFWindow != NULL)
						sdlBFWindow->mMouseDownFunc(sdlBFWindow, sdlEvent.button.x, sdlEvent.button.y, bfMouseBtnOf[sdlEvent.button.button], sdlEvent.button.clicks);
				}
				break;
			case SDL_EVENT_MOUSE_MOTION:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.button.windowID);
					if (sdlBFWindow != NULL)
						sdlBFWindow->mMouseMoveFunc(sdlBFWindow, sdlEvent.button.x, sdlEvent.button.y);
				}
				break;
			case SDL_EVENT_MOUSE_WHEEL:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.wheel.windowID);
					if(sdlBFWindow != NULL)
						sdlBFWindow->mMouseWheelFunc(sdlBFWindow, sdlEvent.wheel.mouse_x, sdlEvent.wheel.mouse_y, sdlEvent.wheel.x, sdlEvent.wheel.y);
				}
			case SDL_EVENT_KEY_DOWN:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.key.windowID);
					if (sdlBFWindow != NULL)
					{
						sdlBFWindow->mKeyDownFunc(sdlBFWindow, SDLConvertKeyCode(sdlEvent.key.key), sdlEvent.key.repeat);
						switch (sdlEvent.key.key) // These keys are not handled by SDL_TEXTINPUT
						{
							case SDLK_RETURN:
								sdlBFWindow->mKeyCharFunc(sdlBFWindow, '\n');
								break;
							case SDLK_BACKSPACE: 
								sdlBFWindow->mKeyCharFunc(sdlBFWindow, '\b');
								break;
							case SDLK_TAB: 
								sdlBFWindow->mKeyCharFunc(sdlBFWindow, '\t');
								break;
							default:;
						}
					}
				}
				break;
			case SDL_EVENT_TEXT_INPUT:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.text.windowID);
					if (sdlBFWindow != NULL)
					{
						wchar_t wchar;
						mbstowcs(&wchar, sdlEvent.text.text, 1);
						sdlBFWindow->mKeyCharFunc(sdlBFWindow, wchar);
					}
				}
				break;
			case SDL_EVENT_KEY_UP:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.key.windowID);
					if (sdlBFWindow != NULL)
						sdlBFWindow->mKeyUpFunc(sdlBFWindow, SDLConvertKeyCode(sdlEvent.key.key));
				}
				break;
			case SDL_EVENT_WINDOW_MOVED:
//			case SDL_EVENT_WINDOW_RESIZED:
			case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.window.windowID);
					if (sdlBFWindow != NULL)
					{
						if (sdlBFWindow->mHasPositionInit)
						{
							sdlBFWindow->mMovedFunc(sdlBFWindow);
							if (sdlEvent.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) 
							{
								sdlBFWindow->mRenderWindow->Resized();
							}
						}
						else
						{
							sdlBFWindow->mHasPositionInit = true;
						}
					}
				}
				break;
			}
		}

        Process();
	}
}

extern int gPixelsDrawn;
int gFrameCount = 0;
int gBFDrawBatchCount = 0;
void SdlBFApp::Draw()
{
    //Beefy::DebugTimeGuard suspendTimeGuard(30, "SdlBFApp::Draw");

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    gPixelsDrawn = 0;
    gBFDrawBatchCount = 0;

	mRenderDevice->FrameStart();
	BFApp::Draw();
	mRenderDevice->FrameEnd();

    gFrameCount++;
    //if (gFrameCount % 60 == 0)
        //OutputDebugStrF("Pixels: %d  Batches: %d\n", gPixelsDrawn / 1000, gBFDrawBatchCount);
}

BFWindow* SdlBFApp::CreateNewWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags)
{
	SdlBFWindow* aWindow = new SdlBFWindow(parent, title, x, y, width, height, windowFlags);
	mSdlWindowMap[bf_SDL_GetWindowID(aWindow->mSDLWindow)] = aWindow;
	mWindowList.push_back(aWindow);
	return aWindow;
}

void SdlBFWindow::GetPosition(int* x, int* y, int* width, int* height, int* clientX, int* clientY, int* clientWidth, int* clientHeight)
{
	bf_SDL_GetWindowPosition(mSDLWindow, x, y);
	bf_SDL_GetWindowSize(mSDLWindow, width, height);

	if (clientX)
		*clientX = *x;
	if (clientY)
		*clientY = *y;
	if (clientWidth)
		*clientWidth = *width;
	if (clientHeight)
		*clientHeight = *height;
}

void SdlBFApp::PhysSetCursor()
{

}

void SdlBFWindow::SetClientPosition(int x, int y)
{
	bf_SDL_SetWindowPosition(mSDLWindow, x, y);

	SDL_Event e;
	bool hasHandled;
	while (bf_SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_WINDOW_MOVED, SDL_EVENT_WINDOW_MOVED) > 0) 
	{
    	if (mMovedFunc != NULL && !hasHandled)
		{
			mRenderWindow->Resized();
			mMovedFunc(this);

			hasHandled = true;
			mHasPositionInit = false;
		}
	}
}

void SdlBFWindow::Resize(int x, int y, int width, int height, ShowKind showKind)
{
	bf_SDL_SetWindowPosition(mSDLWindow, x, y);
	bf_SDL_SetWindowSize(mSDLWindow, width, height);

	SDL_Event e;
	bool hasHandled;
	while (bf_SDL_PeepEvents(&e, 1, SDL_GETEVENT, SDL_EVENT_WINDOW_MOVED, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) > 0) 
	{
    	if (mMovedFunc != NULL && !hasHandled)
		{
			mRenderWindow->Resized();
			mMovedFunc(this);

			hasHandled = true;
			mHasPositionInit = false;
		}
	}
}

void SdlBFWindow::GetPlacement(int* normX, int* normY, int* normWidth, int* normHeight, int* showKind)
{
	bf_SDL_GetWindowPosition(mSDLWindow, normX, normY);
	bf_SDL_GetWindowSize(mSDLWindow, normWidth, normHeight);
}

void SdlBFWindow::SetAlpha(float alpha, uint32 destAlphaSrcMask, bool isMouseVisible)
{
	// Not supported
}

const char* SdlBFApp::GetClipboardFormat(const StringImpl& format)
{
	if (format == "text" || format == "atext")
	{
		return "text/plain;charset=utf-8";
	}
	if (format == "bf_text")
	{
		return "text/vnd.beeflang.bf-text";
	}
	if (format == "code/file-list")
	{
		return "text/vnd.beeflang.file-list";
	}
	return format.c_str();
}

void* SdlBFApp::GetClipboardData(const StringImpl& format, int* size)
{
	size_t outSize;
	void* data = bf_SDL_GetClipboardData(GetClipboardFormat(format), &outSize);
	*size = outSize;
	return data;
}

void SdlBFApp::ReleaseClipboardData(void* ptr)
{
	bf_SDL_free(ptr);
}

const void* SDLClipboardCallback(void* userData, const char* mimeType, size_t* outSize)
{
	SdlClipboardData* clipboard = *(SdlClipboardData**)userData;

	void* data;
	if (clipboard->TryGetValue(StringImpl::MakeRef(mimeType), &data))
	{
		*outSize = strlen((const char*) data);
	}
	return data;
}

void SdlBFApp::SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard)
{
	void* buffer = bf_SDL_malloc(size);
	if (buffer == NULL)
	{
		bf_SDL_SetError("Out of memory for clipboard");
	}
	else 
	{
		StringImpl mime = StringImpl::MakeRef(GetClipboardFormat(format));

		void* previous;
		if (mSdlClipboardData->TryGetValue(mime, &previous))
		{
			bf_SDL_free(previous);
		}
		bf_SDL_memcpy(buffer, ptr, size);
		(*mSdlClipboardData)[mime] = buffer;

		bf_SDL_SetClipboardData(SDLClipboardCallback, NULL, &mSdlClipboardData, mimeTypes, 3);
	}
}

BFMenu* SdlBFWindow::AddMenuItem(BFMenu* parent, int insertIdx, const char* text, const char* hotKey, BFSysBitmap* bitmap, bool enabled, int checkState, bool radioCheck)
{
	return NULL;
}

void SdlBFWindow::RemoveMenuItem(BFMenu* item)
{
}

BFSysBitmap* SdlBFApp::LoadSysBitmap(const wchar_t* fileName)
{
	return NULL;
}

void SdlBFWindow::ModalsRemoved()
{
	//::EnableWindow(mHWnd, TRUE);
	//::SetFocus(mHWnd);
}

DrawLayer* SdlBFApp::CreateDrawLayer(BFWindow* window)
{
	GLDrawLayer* drawLayer = new GLDrawLayer();
	if (window != NULL)
	{
		drawLayer->mRenderWindow = window->mRenderWindow;
		window->mRenderWindow->mDrawLayerList.push_back(drawLayer);
	}
	drawLayer->mRenderDevice = mRenderDevice;
	return drawLayer;
}


void SdlBFApp::GetDesktopResolution(int& width, int& height)
{
	SDL_DisplayID display = bf_SDL_GetPrimaryDisplay();
    if (display != 0) 
	{
		SDL_DisplayMode* displayMode = bf_SDL_GetDesktopDisplayMode(display);
		if (displayMode != NULL) {
			width = displayMode->w;
			height = displayMode->h;
		}
	}
}

static bool InflateRectToMonitor(SDL_DisplayID monitor, AdjustedMonRect* inflatedRect)
{
	SDL_Rect bounds;
	if(!bf_SDL_GetDisplayBounds(monitor, &bounds))
		return false;

	inflatedRect->mMonCount++;
	if (inflatedRect->mMonCount == 1)
	{
		inflatedRect->mX = bounds.x;
		inflatedRect->mY = bounds.y;
		inflatedRect->mWidth = bounds.w;
		inflatedRect->mHeight = bounds.h;
	}
	else
	{
		int minLeft = BF_MIN(inflatedRect->mX, bounds.x);
		int minTop = BF_MIN(inflatedRect->mY, bounds.y);
		int maxRight = BF_MAX(inflatedRect->mX + inflatedRect->mWidth, bounds.x + bounds.w);
		int maxBottom = BF_MAX(inflatedRect->mY + inflatedRect->mHeight, bounds.y + bounds.h);

		inflatedRect->mX = minLeft;
		inflatedRect->mY = minTop;
		inflatedRect->mWidth = maxRight - minLeft;
		inflatedRect->mHeight = maxBottom - minTop;
	}

	return true;
}

void SdlBFApp::GetWorkspaceRect(int& x, int& y, int& width, int& height)
{
	AdjustedMonRect inflateRect = { 0 };

	int displayCount;
	SDL_DisplayID* displays = bf_SDL_GetDisplays(&displayCount);
    if (!displays)
        return;

    for (int i = 0; i < displayCount; i++) 
	{
        InflateRectToMonitor(displays[i], &inflateRect);
    }
    bf_SDL_free(displays);
	
	x = inflateRect.mX;
	y = inflateRect.mY;
	width = inflateRect.mWidth;
	height = inflateRect.mHeight;
}

void SdlBFApp::GetWorkspaceRectFrom(int fromX, int fromY, int fromWidth, int fromHeight, int& outX, int& outY, int& outWidth, int& outHeight)
{
	SDL_Rect bounds;
	SDL_Rect clip = {fromX, fromY, fromWidth == 0 ? 1 : fromWidth, fromHeight == 0 ? 1 : fromHeight};
	AdjustedMonRect inflateRect = { 0 };

	int displayCount;
	SDL_DisplayID* displays = bf_SDL_GetDisplays(&displayCount);
    if (displays == NULL)
        return;

    for (int i = 0; i < displayCount; i++) 
	{
		bf_SDL_GetDisplayBounds(displays[i], &bounds);
		if (bf_SDL_HasRectIntersection(&clip, &bounds))
		{
        	InflateRectToMonitor(displays[i], &inflateRect);
		}
    }
    bf_SDL_free(displays);

	if (inflateRect.mMonCount == 0)
	{
		GetWorkspaceRect(outX, outY, outWidth, outHeight);
		return;
	}
	
	outX = inflateRect.mX;
	outY = inflateRect.mY;
	outWidth = inflateRect.mWidth;
	outHeight = inflateRect.mHeight;
}