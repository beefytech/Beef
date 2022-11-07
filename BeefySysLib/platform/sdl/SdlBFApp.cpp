#include "SdlBFApp.h"
#include "GLRenderDevice.h"
#include "platform/PlatformHelper.h"
#include <SDL2/SDL.h>

USING_NS_BF;

///

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "version.lib")


SdlBFWindow::SdlBFWindow(BFWindow* parent, const StringImpl& title, int x, int y, int width, int height, int windowFlags)
{
	int sdlWindowFlags = 0;
	if (windowFlags & BFWINDOW_RESIZABLE)
		sdlWindowFlags |= SDL_WINDOW_RESIZABLE;
	sdlWindowFlags |= SDL_WINDOW_OPENGL;

#ifdef BF_PLATFORM_FULLSCREEN
    sdlWindowFlags |= SDL_WINDOW_FULLSCREEN;
#endif

	mSDLWindow = SDL_CreateWindow(title.c_str(), x, y, width, height, sdlWindowFlags);

#ifndef BF_PLATFORM_OPENGL_ES2
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif

	if (!SDL_GL_CreateContext(mSDLWindow))
	{
		BF_FATAL(StrFormat(
#ifdef BF_PLATFORM_OPENGL_ES2
			"Unable to create OpenGLES context: %s"
#else
			"Unable to create OpenGL context: %s"
#endif
			, SDL_GetError()).c_str());
		SDL_Quit();
		exit(2);
	}

	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);

	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

#ifndef BF_PLATFORM_OPENGL_ES2
	//glEnableClientState(GL_INDEX_ARRAY);
#endif

	mIsMouseInside = false;
	mRenderWindow = new GLRenderWindow((GLRenderDevice*)gBFApp->mRenderDevice, mSDLWindow);
	mRenderWindow->mWindow = this;
	gBFApp->mRenderDevice->AddRenderWindow(mRenderWindow);

	if (parent != NULL)
		parent->mChildren.push_back(this);
}

SdlBFWindow::~SdlBFWindow()
{
	if (mSDLWindow != NULL)
		TryClose();
}

bool SdlBFWindow::TryClose()
{
	SdlBFApp* app = (SdlBFApp*)gBFApp;
	app->mSdlWindowMap.Remove(SDL_GetWindowID(mSDLWindow));

	SDL_DestroyWindow(mSDLWindow);
	mSDLWindow = NULL;
	return true;
}

static int SDLConvertScanCode(int scanCode)
{
	if ((scanCode >= SDL_SCANCODE_A) && (scanCode <= SDL_SCANCODE_Z))
		return (scanCode - SDL_SCANCODE_A) + 'A';
	if ((scanCode >= SDL_SCANCODE_0) && (scanCode <= SDL_SCANCODE_9))
		return (scanCode - SDL_SCANCODE_0) + '0';

	switch (scanCode)
	{
    case SDL_SCANCODE_CANCEL: return 0x03;
    case SDL_SCANCODE_AC_BACK: return 0x08;
    case SDL_SCANCODE_TAB: return 0x09;
    case SDL_SCANCODE_CLEAR: return 0x0C;
    case SDL_SCANCODE_RETURN: return 0x0D;
    case SDL_SCANCODE_LSHIFT: return 0x10;
	case SDL_SCANCODE_RSHIFT: return 0x10;
    case SDL_SCANCODE_LCTRL: return 0x11;
	case SDL_SCANCODE_RCTRL: return 0x11;
    case SDL_SCANCODE_MENU: return 0x12;
    case SDL_SCANCODE_PAUSE: return 0x13;
    case SDL_SCANCODE_LANG1: return 0x15;
    case SDL_SCANCODE_LANG2: return 0x15;
    case SDL_SCANCODE_LANG3: return 0x17;
    case SDL_SCANCODE_LANG4: return 0x18;
    case SDL_SCANCODE_LANG5: return 0x19;
    case SDL_SCANCODE_LANG6: return 0x19;
    case SDL_SCANCODE_ESCAPE: return 0x1B;
    case SDL_SCANCODE_SPACE: return 0x20;
    case SDL_SCANCODE_PAGEUP: return 0x21;
    case SDL_SCANCODE_PAGEDOWN: return 0x22;
    case SDL_SCANCODE_END: return 0x23;
    case SDL_SCANCODE_HOME: return 0x24;
    case SDL_SCANCODE_LEFT: return 0x25;
    case SDL_SCANCODE_UP: return 0x26;
    case SDL_SCANCODE_RIGHT: return 0x27;
    case SDL_SCANCODE_DOWN: return 0x28;
    case SDL_SCANCODE_SELECT: return 0x29;
    case SDL_SCANCODE_PRINTSCREEN: return 0x2A;
    case SDL_SCANCODE_EXECUTE: return 0x2B;
    case SDL_SCANCODE_INSERT: return 0x2D;
    case SDL_SCANCODE_DELETE: return 0x2E;
    case SDL_SCANCODE_HELP: return 0x2F;
    case SDL_SCANCODE_LGUI: return 0x5B;
    case SDL_SCANCODE_RGUI: return 0x5C;
	case SDL_SCANCODE_KP_0: return 0x60;
	case SDL_SCANCODE_KP_1: return 0x61;
    case SDL_SCANCODE_KP_2: return 0x62;
    case SDL_SCANCODE_KP_3: return 0x63;
    case SDL_SCANCODE_KP_4: return 0x64;
    case SDL_SCANCODE_KP_5: return 0x65;
    case SDL_SCANCODE_KP_6: return 0x66;
    case SDL_SCANCODE_KP_7: return 0x67;
    case SDL_SCANCODE_KP_8: return 0x68;
    case SDL_SCANCODE_KP_9: return 0x69;
    case SDL_SCANCODE_KP_MULTIPLY: return 0x6A;
    case SDL_SCANCODE_KP_PLUS: return 0x6B;
    case SDL_SCANCODE_SEPARATOR: return 0x6C;
    case SDL_SCANCODE_KP_MINUS: return 0x6D;
    case SDL_SCANCODE_KP_PERIOD: return 0x6E;
    case SDL_SCANCODE_KP_DIVIDE: return 0x6F;
    case SDL_SCANCODE_F1: return 0x70;
    case SDL_SCANCODE_F2: return 0x71;
    case SDL_SCANCODE_F3: return 0x72;
    case SDL_SCANCODE_F4: return 0x73;
    case SDL_SCANCODE_F5: return 0x74;
    case SDL_SCANCODE_F6: return 0x75;
    case SDL_SCANCODE_F7: return 0x76;
    case SDL_SCANCODE_F8: return 0x77;
    case SDL_SCANCODE_F9: return 0x78;
    case SDL_SCANCODE_F10: return 0x79;
    case SDL_SCANCODE_F11: return 0x7A;
    case SDL_SCANCODE_F12: return 0x7B;
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x90;
    case SDL_SCANCODE_SCROLLLOCK: return 0x91;
    case SDL_SCANCODE_GRAVE: return 0xC0;
    //case SDL_SCANCODE_COMMAND: return 0xF0;
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

	mDataDir = mInstallDir;

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) < 0)
		BF_FATAL(StrFormat("Unable to initialize SDL: %s", SDL_GetError()).c_str());
}

SdlBFApp::~SdlBFApp()
{
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
                if (!SDL_PollEvent(&sdlEvent))
                    break;
            }

            //Beefy::DebugTimeGuard suspendTimeGuard(30, "BFApp::Run2");

			switch (sdlEvent.type)
			{
			case SDL_QUIT:
				//gBFApp->RemoveWindow(sdlEvent.window);
				Shutdown();
				break;
			case SDL_MOUSEBUTTONUP:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.button.windowID);
					sdlBFWindow->mMouseUpFunc(sdlBFWindow, sdlEvent.button.x, sdlEvent.button.y, sdlEvent.button.button);
				}
				break;
			case SDL_MOUSEBUTTONDOWN:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.button.windowID);
					sdlBFWindow->mMouseDownFunc(sdlBFWindow, sdlEvent.button.x, sdlEvent.button.y, sdlEvent.button.button, 1);
				}
				break;
			case SDL_MOUSEMOTION:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.button.windowID);
					sdlBFWindow->mMouseMoveFunc(sdlBFWindow, sdlEvent.button.x, sdlEvent.button.y);
				}
				break;
			case SDL_KEYDOWN:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.key.windowID);
					sdlBFWindow->mKeyDownFunc(sdlBFWindow, SDLConvertScanCode(sdlEvent.key.keysym.scancode), sdlEvent.key.repeat);
					sdlBFWindow->mKeyCharFunc(sdlBFWindow, sdlEvent.key.keysym.sym);
				}
				break;
			case SDL_KEYUP:
				{
					SdlBFWindow* sdlBFWindow = GetSdlWindowFromId(sdlEvent.key.windowID);
					sdlBFWindow->mKeyUpFunc(sdlBFWindow, SDLConvertScanCode(sdlEvent.key.keysym.scancode));
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
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
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
	mSdlWindowMap[SDL_GetWindowID(aWindow->mSDLWindow)] = aWindow;
	mWindowList.push_back(aWindow);
	return aWindow;
}

void SdlBFWindow::GetPosition(int* x, int* y, int* width, int* height, int* clientX, int* clientY, int* clientWidth, int* clientHeight)
{
	SDL_GetWindowPosition(mSDLWindow, x, y);
	SDL_GetWindowSize(mSDLWindow, width, height);
	*clientWidth = *width;
	*clientHeight = *height;
}

void SdlBFApp::PhysSetCursor()
{

}

void SdlBFWindow::SetClientPosition(int x, int y)
{
	SDL_SetWindowPosition(mSDLWindow, x, y);

	if (mMovedFunc != NULL)
		mMovedFunc(this);
}

void SdlBFWindow::SetAlpha(float alpha, uint32 destAlphaSrcMask, bool isMouseVisible)
{
	// Not supported
}

uint32 SdlBFApp::GetClipboardFormat(const StringImpl& format)
{
	return /*CF_TEXT*/1;
}

void* SdlBFApp::GetClipboardData(const StringImpl& format, int* size)
{
	return SDL_GetClipboardText();
}

void SdlBFApp::ReleaseClipboardData(void* ptr)
{
	SDL_free(ptr);
}

void SdlBFApp::SetClipboardData(const StringImpl& format, const void* ptr, int size, bool resetClipboard)
{
	SDL_SetClipboardText((const char*)ptr);
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
	width = 1024;
	height = 768;
}

void SdlBFApp::GetWorkspaceRect(int& x, int& y, int& width, int& height)
{
	x = 0;
	y = 0;
	width = 1024;
	height = 768;
}
