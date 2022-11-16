#include "Common.h"
#include "PlatformApp.h"
#include "gfx/RenderDevice.h"
#include "gfx/Texture.h"
#include "gfx/Shader.h"
#include "gfx/DrawLayer.h"
#include "gfx/RenderCmd.h"
#include "gfx/FTFont.h"
#include "img/BFIData.h"
#include "util/Vector.h"
#include "util/PerfTimer.h"
#include "util/TLSingleton.h"

#include "util/AllocDebug.h"

#ifdef BF_PLATFORM_WINDOWS
#include "platform/sdl/SdlBFApp.h"
#endif

//#include "third_party/freetype/include/ft2build.h"
//#include FT_FREETYPE_H
//#include "img/PNGData.h"

#define UTF16DECODE_PTR(strPtr) ((strPtr) == NULL ? NULL : UTF16Decode(strPtr).c_str())

USING_NS_BF;

#pragma warning(disable:4996)

static TLSingleton<String> gBeefySys_TLStrReturn;
int gPixelsDrawn = 0;

#ifdef BF_PLATFORM_WINDOWS
static int gLastReqId = 0;
static int BfAllocHook(int nAllocType, void *pvData,
	size_t nSize, int nBlockUse, long lRequest,
	const unsigned char * szFileName, int nLine)
{
	if (gLastReqId == lRequest)
		return TRUE;

	gLastReqId = lRequest;
	if (szFileName == NULL)
		return TRUE;

	/*char str[1024];
	sprintf(str, "Alloc: %d File: %s Line: %d\n", lRequest, szFileName, nLine);
	OutputDebugStringA(str);*/
	return TRUE;
}

HINSTANCE gDLLInstance = NULL;
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	//::MessageBoxA(NULL, "C", "D", MB_OK);

#ifdef BF_VC
	//_CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF  );
	//_CrtSetBreakAlloc(1437);
	//_CrtSetAllocHook(BfAllocHook);
#endif

    switch (fdwReason)
	{
    case DLL_PROCESS_ATTACH:
		gDLLInstance = hinstDLL;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
#endif

BF_EXPORT void BF_CALLTYPE BFApp_GetDesktopResolution(int& width, int& height)
{
	gBFApp->GetDesktopResolution(width, height);
}

BF_EXPORT void BF_CALLTYPE BFApp_GetWorkspaceRect(int& x, int& y, int& width, int& height)
{
	gBFApp->GetWorkspaceRect(x, y, width, height);
}

BF_EXPORT void BF_CALLTYPE BFApp_GetWorkspaceRectFrom(int fromX, int fromY, int fromWidth, int fromHeight, int& outX, int& outY, int& outWidth, int& outHeight)
{
	gBFApp->GetWorkspaceRectFrom(fromX, fromY, fromWidth, fromHeight, outX, outY, outWidth, outHeight);
}

static bool gForceSDL = false;

BF_EXPORT void BF_CALLTYPE BFApp_SetOptionString(const char* name, const char* value)
{
	if (strcmp(name, "SDL") == 0)
	{
		gForceSDL = strcmp(value, "1") == 0;
	}
}

BF_EXPORT void BF_CALLTYPE BFApp_Create()
{
#ifdef BF_PLATFORM_WINDOWS
	if (gForceSDL)
	{
		new SdlBFApp();
		return;
	}
#endif

	new PlatformBFApp();
}

BF_EXPORT void BF_CALLTYPE BFApp_Delete()
{
	delete gBFApp;
	gBFApp = NULL;
	FTFontManager::ClearCache();

	//OutputDebugStrF("Deleting App\n");
#ifdef BF_VC
	//_CrtDumpMemoryLeaks();
#endif
}

//void FT_Test()
//{
//	FT_Library  library;   /* handle to library     */
//	FT_Face     face;      /* handle to face object */
//
//	auto error = FT_Init_FreeType(&library);
//	error = FT_New_Face(library, "/temp/SourceCodePro-Regular.ttf", 0, &face);
//	if (error == FT_Err_Unknown_File_Format)
//	{
//
//	}
//	else if (error)
//	{
//
//	}
//
//	error = FT_Set_Char_Size(
//		face,    /* handle to face object           */
//		0,       /* char_width in 1/64th of points  */
//		9 * 64,   /* char_height in 1/64th of points */
//		96,     /* horizontal device resolution    */
//		96);   /* vertical device resolution      */
//
//	String str = ".cHasDebugFlags";
//
//	PNGData image;
//	image.CreateNew(256, 256);
//	for (int i = 0; i < 256 * 256; i++)
//		image.mBits[i] = 0xFF000000;
//
//	int curX = 0;
//	int curY = 0;
//
//	for (int i = 0; i < (int)str.length(); i++)
//	{
//		int glyph_index = FT_Get_Char_Index(face, str[i]);
//
//		error = FT_Load_Glyph(
//			face,          /* handle to face object */
//			glyph_index,   /* glyph index           */
//			FT_LOAD_NO_BITMAP);  /* load flags, see below */
//
//		error = FT_Render_Glyph(face->glyph,   /* glyph slot  */
//			FT_RENDER_MODE_NORMAL); /* render mode */
//
//		auto& bitmap = face->glyph->bitmap;
//		for (int y = 0; y < (int)bitmap.rows; y++)
//		{
//			for (int x = 0; x < (int)bitmap.width; x++)
//			{
//				uint8 val = bitmap.buffer[y * bitmap.pitch + x];
//
//				val = (uint8)(pow(val / 255.0f, 0.5556) * 255.0f);
//
//				image.mBits[(y + 12 - bitmap.rows) * image.mWidth + x + curX] = 0xFF000000 |
//					((int32)val) | ((int32)val << 8) | ((int32)val << 16);
//			}
//		}
//
//		curX += bitmap.width + 1;
//
//		//int w = face->glyph->bitmap.;
//
//		//face->glyph->bitmap.buffer
//	}
//	image.WriteToFile("/temp/fnt.png");
//}

BF_EXPORT void BF_CALLTYPE BFApp_Init()
{
	//////////////////////////////////////////////////////////////////////////

	//FT_Test();

	//////////////////////////////////////////////////////////////////////////

	gBFApp->Init();
}

BF_EXPORT void BF_CALLTYPE BFApp_Run()
{
	gBFApp->Run();
}

BF_EXPORT void BF_CALLTYPE BFApp_Shutdown()
{
	gBFApp->Shutdown();
}

BF_EXPORT void BF_CALLTYPE BFApp_SetDrawEnabled(int enabled)
{
	gBFApp->mDrawEnabled = enabled != 0;
}

BF_EXPORT void BF_CALLTYPE BFApp_SetRefreshRate(int rate)
{
	gBFApp->mRefreshRate = (float) rate;
}

BF_EXPORT const char* BF_CALLTYPE BFApp_GetInstallDir()
{
	return gBFApp->mInstallDir.c_str();
}

BF_EXPORT const char* BF_CALLTYPE BFApp_GetDataDir()
{
	return gBFApp->mDataDir.c_str();
}

BF_EXPORT void BF_CALLTYPE BFApp_SetCallbacks(BFApp_UpdateFunc updateFunc, BFApp_UpdateFFunc updateFFunc, BFApp_DrawFunc drawFunc)
{
	gBFApp->mUpdateFunc = updateFunc;
	gBFApp->mUpdateFFunc = updateFFunc;
	gBFApp->mDrawFunc = drawFunc;
	//public delegate void UpdateProc();
}

BF_EXPORT BFWindow* BF_CALLTYPE BFApp_CreateWindow(BFWindow* parent, const char* title, int x, int y, int width, int height, int windowFlags)
{
	return gBFApp->CreateNewWindow(parent, title, x, y, width, height, windowFlags);
}

BF_EXPORT void BF_CALLTYPE BFApp_RemoveWindow(BFWindow* window)
{
	delete window;
}

BF_EXPORT void BF_CALLTYPE BFApp_SetCursor(int cursor)
{
	gBFApp->SetCursor(cursor);
}

BF_EXPORT void* BF_CALLTYPE BFApp_GetClipboardData(const char* format, int* size)
{
	return gBFApp->GetClipboardData(format, size);
}

BF_EXPORT void BF_CALLTYPE BFApp_ReleaseClipboardData(void* ptr)
{
	return gBFApp->ReleaseClipboardData(ptr);
}

BF_EXPORT void BF_CALLTYPE BFApp_SetClipboardData(const char* format, void* ptr, int size, int resetClipboard)
{
	return gBFApp->SetClipboardData(format, ptr, size, resetClipboard != 0);
}

BF_EXPORT void BF_CALLTYPE BFApp_CheckMemory()
{
#ifdef BF_PLATFORM_WINDOWS
	_CrtCheckMemory();
#endif
}

BF_EXPORT void BF_CALLTYPE BFApp_RehupMouse()
{

}

BF_EXPORT const char* BF_CALLTYPE BFApp_EnumerateInputDevices()
{
	String& outString = *gBeefySys_TLStrReturn.Get();
	outString = gBFApp->EnumerateInputDevices();
	return outString.c_str();
}

BF_EXPORT BFInputDevice* BFApp_CreateInputDevice(const char* guid)
{
	return gBFApp->CreateInputDevice(guid);
}

BF_EXPORT BFSoundManager* BF_CALLTYPE BFApp_GetSoundManager()
{
	return gBFApp->GetSoundManager();
}

BF_EXPORT intptr BF_CALLTYPE BFApp_GetCriticalThreadId(int idx)
{
	return gBFApp->GetCriticalThreadId(idx);
}

///

BF_EXPORT void BF_CALLTYPE BFWindow_SetCallbacks(BFWindow* window, BFWindow_MovedFunc movedFunc, BFWindow_CloseQueryFunc closeQueryFunc, BFWindow_ClosedFunc closedFunc,
	BFWindow_GotFocusFunc gotFocusFunc, BFWindow_LostFocusFunc lostFocusFunc,
	BFWindow_KeyCharFunc keyCharFunc, BFWindow_KeyDownFunc keyDownFunc, BFWindow_KeyUpFunc keyUpFunc, BFWindow_HitTestFunc hitTestFunc,
	BFWindow_MouseMove mouseMoveFunc, BFWindow_MouseProxyMove mouseProxyMoveFunc,
	BFWindow_MouseDown mouseDownFunc, BFWindow_MouseUp mouseUpFunc, BFWindow_MouseWheel mouseWheelFunc, BFWindow_MouseLeave mouseLeaveFunc,
	BFWindow_MenuItemSelectedFunc menuItemSelectedFunc, BFWindow_DragDropFileFunc dragDropFileFunc)
{
	window->mMovedFunc = movedFunc;
	window->mCloseQueryFunc = closeQueryFunc;
	window->mClosedFunc = closedFunc;
	window->mGotFocusFunc = gotFocusFunc;
	window->mLostFocusFunc = lostFocusFunc;
	window->mKeyCharFunc = keyCharFunc;
	window->mKeyDownFunc = keyDownFunc;
	window->mKeyUpFunc = keyUpFunc;
	window->mHitTestFunc = hitTestFunc;
	window->mMouseMoveFunc = mouseMoveFunc;
	window->mMouseProxyMoveFunc = mouseProxyMoveFunc;
	window->mMouseDownFunc = mouseDownFunc;
	window->mMouseUpFunc = mouseUpFunc;
	window->mMouseWheelFunc = mouseWheelFunc;
	window->mMouseLeaveFunc = mouseLeaveFunc;
	window->mMenuItemSelectedFunc = menuItemSelectedFunc;
	window->mDragDropFileFunc = dragDropFileFunc;
}

BF_EXPORT void* BFWindow_GetNativeUnderlying(BFWindow* window)
{
	return window->GetUnderlying();
}

BF_EXPORT void BF_CALLTYPE BFWindow_MovedDelegate(BFWindow* window, BFWindow_MovedFunc movedFunc)
{
	window->mMovedFunc = movedFunc;
}

BF_EXPORT void BF_CALLTYPE BFWindow_SetTitle(BFWindow* window, const char* title)
{
	window->SetTitle(title);
}

BF_EXPORT void BF_CALLTYPE BFWindow_SetMinimumSize(BFWindow* window, int minWidth, int minHeight, bool clientSized)
{
	window->SetMinimumSize(minWidth, minHeight, clientSized);
}

BF_EXPORT void BF_CALLTYPE BFWindow_GetPosition(BFWindow* window, int* x, int* y, int* width, int* height, int* clientX, int* clientY, int* clientWidth, int* clientHeight)
{
	window->GetPosition(x, y, width, height, clientX, clientY, clientWidth, clientHeight);
}

BF_EXPORT void BF_CALLTYPE BFWindow_GetPlacement(BFWindow* window, int* normX, int* normY, int* normWidth, int* normHeight, int* showKind)
{
	window->GetPlacement(normX, normY, normWidth, normHeight, showKind);
}

BF_EXPORT void BF_CALLTYPE BFWindow_Resize(BFWindow* window, int x, int y, int width, int height, int showKind)
{
	window->Resize(x, y, width, height, (BFWindow::ShowKind)showKind);
}

BF_EXPORT void BF_CALLTYPE BFWindow_Show(BFWindow* window, BFWindow::ShowKind showKind)
{
	window->Show(showKind);
}

BF_EXPORT void BF_CALLTYPE BFWindow_SetForeground(BFWindow* window)
{
	window->SetForeground();
}

BF_EXPORT void BF_CALLTYPE BFWindow_SetNonExclusiveMouseCapture(BFWindow* window)
{
	window->SetNonExclusiveMouseCapture();
}

BF_EXPORT void BF_CALLTYPE BFWindow_LostFocus(BFWindow* window, BFWindow* newFocus)
{
	window->LostFocus(newFocus);
}

BF_EXPORT void BF_CALLTYPE BFWindow_SetAlpha(BFWindow* window, float alpha, uint32 destAlphaSrcMask, int mouseVisible)
{
	window->SetAlpha(alpha, destAlphaSrcMask, mouseVisible != 0);
}

BF_EXPORT void BF_CALLTYPE BFWindow_CaptureMouse(BFWindow* window)
{
	window->CaptureMouse();
}

BF_EXPORT bool BF_CALLTYPE BFWindow_IsMouseCaptured(BFWindow* window)
{
	return window->IsMouseCaptured();
}

BF_EXPORT void BF_CALLTYPE BFWindow_SetMouseVisible(BFWindow* window, bool mouseVisible)
{
	window->SetMouseVisible(mouseVisible);
}

BF_EXPORT void BF_CALLTYPE BFWindow_SetClientPosition(BFWindow* window, int x, int y)
{
	window->SetClientPosition(x, y);
}

BF_EXPORT BFMenu* BF_CALLTYPE BFWindow_AddMenuItem(BFWindow* window, BFMenu* parent, int insertIdx, const char* text, const char* hotKey, BFSysBitmap* bitmap, int enabled, int checkState, int radioCheck)
{
	return window->AddMenuItem(parent, insertIdx, text, hotKey, bitmap, enabled != 0, checkState, radioCheck != 0);
}

BF_EXPORT void BF_CALLTYPE BFWindow_ModifyMenuItem(BFWindow* window, BFMenu* item, const char* text, const char* hotKey, BFSysBitmap* bitmap, int enabled, int checkState, int radioCheck)
{
	window->ModifyMenuItem(item, text, hotKey, bitmap, enabled != 0, checkState, radioCheck != 0);
}

BF_EXPORT void BF_CALLTYPE BFWindow_DeleteMenuItem(BFWindow* window, BFMenu* item)
{
	window->RemoveMenuItem(item);
	delete item;
}

BF_EXPORT void BF_CALLTYPE BFWindow_Close(BFWindow* window, int force)
{
	if (force != 0)
		gBFApp->RemoveWindow(window);
	else
		window->TryClose();
}

BF_EXPORT int BF_CALLTYPE BFWindow_GetDPI(BFWindow* window)
{
	return window->GetDPI();
}

///

BF_EXPORT TextureSegment* BF_CALLTYPE Gfx_CreateRenderTarget(int width, int height, int destAlpha)
{
	Texture* texture = gBFApp->mRenderDevice->CreateRenderTarget(width, height, destAlpha != 0);

	TextureSegment* aTextureSegment = new TextureSegment();
	aTextureSegment->InitFromTexture(texture);
	return aTextureSegment;
}

BF_EXPORT TextureSegment* BF_CALLTYPE Gfx_CreateDynTexture(int width, int height)
{
	Texture* texture = gBFApp->mRenderDevice->CreateDynTexture(width, height);

	TextureSegment* aTextureSegment = new TextureSegment();
	aTextureSegment->InitFromTexture(texture);
	return aTextureSegment;
}

BF_EXPORT TextureSegment* BF_CALLTYPE Gfx_LoadTexture(const char* fileName, int flags)
{
	Texture* texture = gBFApp->mRenderDevice->LoadTexture(fileName, flags);
	if (texture == NULL)
		return NULL;

	TextureSegment* aTextureSegment = new TextureSegment();
	aTextureSegment->InitFromTexture(texture);
	return aTextureSegment;
}

BF_EXPORT void BF_CALLTYPE Gfx_Texture_SetBits(TextureSegment* textureSegment, int destX, int destY, int destWidth, int destHeight, int srcPitch, uint32* bits)
{
	textureSegment->mTexture->SetBits(destX, destY, destWidth, destHeight, srcPitch, bits);
}

BF_EXPORT void BF_CALLTYPE Gfx_Texture_GetBits(TextureSegment* textureSegment, int srcX, int srcY, int srcWidth, int srcHeight, int destPitch, uint32* bits)
{
	textureSegment->mTexture->GetBits(srcX, srcY, srcWidth, srcHeight, destPitch, bits);
}

BF_EXPORT void BF_CALLTYPE Gfx_Texture_Delete(TextureSegment* textureSegment)
{
	textureSegment->mTexture->Release();
	delete textureSegment;
}

BF_EXPORT int BF_CALLTYPE Gfx_Texture_GetWidth(TextureSegment* textureSegment)
{
	return (int) textureSegment->mScaleX;
}

BF_EXPORT int BF_CALLTYPE Gfx_Texture_GetHeight(TextureSegment* textureSegment)
{
	return (int) textureSegment->mScaleY;
}

BF_EXPORT void BF_CALLTYPE Gfx_ModifyTextureSegment(TextureSegment* destTextureSegment, TextureSegment* srcTextureSegment, int srcX, int srcY, int srcWidth, int srcHeight)
{
	if (destTextureSegment->mTexture != srcTextureSegment->mTexture)
	{
		destTextureSegment->mTexture->Release();
		destTextureSegment->mTexture = srcTextureSegment->mTexture;
		destTextureSegment->mTexture->AddRef();
	}

	Texture* texture = srcTextureSegment->mTexture;
	destTextureSegment->mU1 = (srcX / (float) texture->mWidth) + srcTextureSegment->mU1;
	destTextureSegment->mV1 = (srcY / (float) texture->mHeight) + srcTextureSegment->mV1;
	destTextureSegment->mU2 = ((srcX + srcWidth) / (float) texture->mWidth) + srcTextureSegment->mU1;
	destTextureSegment->mV2 = ((srcY + srcHeight) / (float) texture->mHeight) + srcTextureSegment->mV1;
	destTextureSegment->mScaleX = (float)abs(srcWidth);
	destTextureSegment->mScaleY = (float)abs(srcHeight);
}

BF_EXPORT TextureSegment* BF_CALLTYPE Gfx_CreateTextureSegment(TextureSegment* textureSegment, int srcX, int srcY, int srcWidth, int srcHeight)
{
	Texture* texture = textureSegment->mTexture;
	texture->AddRef();

	TextureSegment* aTextureSegment = new TextureSegment();
	aTextureSegment->mTexture = texture;
	aTextureSegment->mU1 = (srcX / (float) texture->mWidth) + textureSegment->mU1;
	aTextureSegment->mV1 = (srcY / (float) texture->mHeight) + textureSegment->mV1;
	aTextureSegment->mU2 = ((srcX + srcWidth) / (float) texture->mWidth) + textureSegment->mU1;
	aTextureSegment->mV2 = ((srcY + srcHeight) / (float) texture->mHeight) + textureSegment->mV1;
	aTextureSegment->mScaleX = (float)abs(srcWidth);
	aTextureSegment->mScaleY = (float)abs(srcHeight);
	return aTextureSegment;
}

BF_EXPORT void BF_CALLTYPE Gfx_SetDrawSize(TextureSegment* textureSegment, int width, int height)
{
	textureSegment->mScaleX = (float)abs(width);
	textureSegment->mScaleY = (float)abs(height);
}

BF_EXPORT void BF_CALLTYPE Gfx_DrawTextureSegment(TextureSegment* textureSegment, float a, float b, float c, float d, float tx, float ty, float z, uint32 color, int pixelSnapping)
{
	DrawLayer* drawLayer = gBFApp->mRenderDevice->mCurDrawLayer;
	drawLayer->SetTexture(0, textureSegment->mTexture);
	DefaultVertex3D* v = (DefaultVertex3D*)drawLayer->AllocStrip(4);

	if ((pixelSnapping == 1) ||
		((pixelSnapping == 2) && (a == 1.0f) && (b == 0) && (c == 0) && (d == 1.0f)))
	{
		tx = (float) (int) (tx + 100000) - 100000;
		ty = (float) (int) (ty + 100000) - 100000;
	}

	a *= textureSegment->mScaleX;
	b *= textureSegment->mScaleX;
	c *= textureSegment->mScaleY;
	d *= textureSegment->mScaleY;

	v[0].Set(tx, ty, z, textureSegment->mU1, textureSegment->mV1, color);
	v[1].Set(tx + a, ty + b, z, textureSegment->mU2, textureSegment->mV1, color);
	v[2].Set(tx + c, ty + d, z, textureSegment->mU1, textureSegment->mV2, color);
	v[3].Set(tx + (a + c), ty + (b + d), z, textureSegment->mU2, textureSegment->mV2, color);

    gPixelsDrawn += (int)((a + b) * (c + d));
}

static TextureSegment* gCurTextureSegment = NULL;
static DefaultVertex3D* gCurAllocVertices = NULL;

BF_EXPORT void BF_CALLTYPE Gfx_AllocTris(TextureSegment* textureSegment, int vtxCount)
{
	gCurTextureSegment = textureSegment;
	DrawLayer* drawLayer = gBFApp->mRenderDevice->mCurDrawLayer;
	drawLayer->SetTexture(0, textureSegment->mTexture);
	gCurAllocVertices = (DefaultVertex3D*)gBFApp->mRenderDevice->mCurDrawLayer->AllocTris(vtxCount);
}

BF_EXPORT void BF_CALLTYPE Gfx_SetDrawVertex(int idx, float x, float y, float z, float u, float v, uint32 color)
{
	gCurAllocVertices[idx].Set(x, y, z,
		gCurTextureSegment->mU1 + u * (gCurTextureSegment->mU2 - gCurTextureSegment->mU1),
		gCurTextureSegment->mV1 + v * (gCurTextureSegment->mV2 - gCurTextureSegment->mV1), color);
}

BF_EXPORT void BF_CALLTYPE Gfx_CopyDrawVertex(int destIdx, int srcIdx)
{
	gCurAllocVertices[destIdx] = gCurAllocVertices[srcIdx];
}

BF_EXPORT void BF_CALLTYPE Gfx_DrawQuads(TextureSegment* textureSegment, DefaultVertex3D* vertices, int vtxCount)
{
	/*for (int vtxIdx = 0; vtxIdx < vtxCount; vtxIdx += 4)
	{
		Vertex3D* v = gBFApp->mRenderDevice->mCurDrawLayer->AllocStrip(textureSegment->mTexture, drawType != 0, 4);

		v[0] = vertices[vtxIdx];
		v[1] = vertices[vtxIdx + 1];
		v[2] = vertices[vtxIdx + 2];
		v[3] = vertices[vtxIdx + 3];
	}

	return;*/

	DrawLayer* drawLayer = gBFApp->mRenderDevice->mCurDrawLayer;
	drawLayer->SetTexture(0, textureSegment->mTexture);

	DefaultVertex3D* vtxInPtr = vertices;

	int curIdx = 0;
	while (curIdx < vtxCount)
	{
		//int batchSize = std::min(128, vtxCount - curIdx);
		int batchSize = std::min(16*1024, vtxCount - curIdx);

		uint16 idxOfs;
		DefaultVertex3D* vtxPtr;
		uint16* idxPtr;
		gBFApp->mRenderDevice->mCurDrawLayer->AllocIndexed(batchSize, batchSize * 6 / 4, (void**)&vtxPtr, &idxPtr, &idxOfs);

		for (int vtxIdx = 0; vtxIdx < batchSize; vtxIdx += 4)
		{
			*(vtxPtr++) = *(vtxInPtr++);
			*(vtxPtr++) = *(vtxInPtr++);
			*(vtxPtr++) = *(vtxInPtr++);
			*(vtxPtr++) = *(vtxInPtr++);

			*(idxPtr++) = idxOfs;
			*(idxPtr++) = idxOfs + 1;
			*(idxPtr++) = idxOfs + 2;

			*(idxPtr++) = idxOfs + 1;
			*(idxPtr++) = idxOfs + 2;
			*(idxPtr++) = idxOfs + 3;

			/*int curIdxIdx = idxPtr - gBFApp->mRenderDevice->mCurDrawLayer->mDrawBatchList.mTail->mIndices;
			BF_ASSERT(curIdxIdx <= gBFApp->mRenderDevice->mCurDrawLayer->mDrawBatchList.mTail->mAllocatedIndices);
			int curVtxIdx = vtxPtr - gBFApp->mRenderDevice->mCurDrawLayer->mDrawBatchList.mTail->mVertices;
			BF_ASSERT(curVtxIdx <= gBFApp->mRenderDevice->mCurDrawLayer->mDrawBatchList.mTail->mAllocatedVertices);*/

			idxOfs += 4;
		}

		curIdx += batchSize;
	}
}

BF_EXPORT void BF_CALLTYPE Gfx_DrawIndexedVertices(int vertexSize, void* vtxData, int vtxCount, uint16* idxData, int idxCount)
{
	DrawLayer* drawLayer = gBFApp->mRenderDevice->mCurDrawLayer;

	uint16 idxOfs;
	void* drawBatchVtxPtr;
	uint16* drawBatchIdxPtr;
	gBFApp->mRenderDevice->mCurDrawLayer->AllocIndexed(vtxCount, idxCount, (void**)&drawBatchVtxPtr, &drawBatchIdxPtr, &idxOfs);
	BF_ASSERT(gBFApp->mRenderDevice->mCurDrawLayer->mCurDrawBatch->mVtxSize == vertexSize);

	uint16* idxPtr = idxData;
	for (int idxIdx = 0; idxIdx < idxCount; idxIdx++)
		*(drawBatchIdxPtr++) = *(idxPtr++) + idxOfs;

	memcpy(drawBatchVtxPtr, vtxData, vertexSize * vtxCount);
}

BF_EXPORT void BF_CALLTYPE Gfx_DrawIndexedVertices2D(int vertexSize, void* vtxData, int vtxCount, uint16* idxData, int idxCount, float a, float b, float c, float d, float tx, float ty, float z)
{
	DrawLayer* drawLayer = gBFApp->mRenderDevice->mCurDrawLayer;

	uint16 idxOfs;
	void* drawBatchVtxPtr;
	uint16* drawBatchIdxPtr;
	gBFApp->mRenderDevice->mCurDrawLayer->AllocIndexed(vtxCount, idxCount, (void**)&drawBatchVtxPtr, &drawBatchIdxPtr, &idxOfs);
	BF_ASSERT(gBFApp->mRenderDevice->mCurDrawLayer->mCurDrawBatch->mVtxSize == vertexSize);

	uint16* idxPtr = idxData;
	for (int idxIdx = 0; idxIdx < idxCount; idxIdx++)
		*(drawBatchIdxPtr++) = *(idxPtr++) + idxOfs;

	//memcpy(drawBatchIdxPtr, idxData, sizeof(uint16) * idxCount);
	//memcpy(drawBatchVtxPtr, vtxData, vertexSize * vtxCount);

	void* vtxPtr = vtxData;
	for (int vtxIdx = 0; vtxIdx < vtxCount; vtxIdx++)
	{
		Vector3* srcPos = (Vector3*)vtxPtr;
		Vector3* destPos = (Vector3*)drawBatchVtxPtr;

		destPos->mX = srcPos->mX * a + srcPos->mY * c + tx;
		destPos->mY = srcPos->mX * b + srcPos->mY * d + ty;
		destPos->mZ = srcPos->mZ + z;

		memcpy((uint8*)drawBatchVtxPtr + sizeof(Vector3), (uint8*)vtxPtr + sizeof(Vector3), vertexSize - sizeof(Vector3));
		drawBatchVtxPtr = (uint8*)drawBatchVtxPtr + vertexSize;
		vtxPtr = (uint8*)vtxPtr + vertexSize;
	}
}

BF_EXPORT void BF_CALLTYPE Gfx_SetShaderConstantData(int usageIdx, int slotIdx, void* constData, int size)
{
	gBFApp->mRenderDevice->mCurDrawLayer->SetShaderConstantData(usageIdx, slotIdx, constData, size);
}

BF_EXPORT void BF_CALLTYPE Gfx_SetShaderConstantDataTyped(int usageIdx, int slotIdx, void* constData, int size, int* typeData, int typeCount)
{
	gBFApp->mRenderDevice->mCurDrawLayer->SetShaderConstantDataTyped(usageIdx, slotIdx, constData, size, typeData, typeCount);
}

BF_EXPORT void BF_CALLTYPE Gfx_QueueRenderCmd(RenderCmd* renderCmd)
{
	gBFApp->mRenderDevice->mCurDrawLayer->QueueRenderCmd(renderCmd);
}

BF_EXPORT VertexDefinition* BF_CALLTYPE Gfx_CreateVertexDefinition(VertexDefData* elementData, int numElements)
{
	return gBFApp->mRenderDevice->CreateVertexDefinition(elementData, numElements);
}

BF_EXPORT void BF_CALLTYPE Gfx_VertexDefinition_Delete(VertexDefinition* vertexDefinition)
{
	delete vertexDefinition;
}

BF_EXPORT void BF_CALLTYPE Gfx_CreateRenderState(RenderState* srcRenderState)
{
	gBFApp->mRenderDevice->CreateRenderState(srcRenderState);
}

BF_EXPORT void BF_CALLTYPE RenderState_Delete(RenderState* renderState)
{
	delete renderState;
}

BF_EXPORT void BF_CALLTYPE RenderState_SetTexWrap(RenderState* renderState, bool texWrap)
{
	renderState->SetTexWrap(texWrap);
}

BF_EXPORT void BF_CALLTYPE RenderState_SetWireframe(RenderState* renderState, bool wireframe)
{
	renderState->SetWireframe(wireframe);
}

BF_EXPORT void BF_CALLTYPE RenderState_SetClip(RenderState* renderState, float x, float y, float width, float height)
{
	BF_ASSERT((width >= 0) && (height >= 0));
	renderState->mClipRect.mX = x;
	renderState->mClipRect.mY = y;
	renderState->mClipRect.mWidth = width;
	renderState->mClipRect.mHeight = height;
	if (!renderState->mClipped)
		renderState->SetClipped(true);
}

BF_EXPORT void BF_CALLTYPE RenderState_SetShader(RenderState* renderState, Shader* shader)
{
	renderState->SetShader(shader);
}

BF_EXPORT void BF_CALLTYPE RenderState_DisableClip(RenderState* renderState)
{
	renderState->SetClipped(false);
}

BF_EXPORT void BF_CALLTYPE Gfx_SetTexture_TextureSegment(int textureIdx, TextureSegment* textureSegment)
{
	DrawLayer* drawLayer = gBFApp->mRenderDevice->mCurDrawLayer;
	drawLayer->SetTexture(textureIdx, textureSegment->mTexture);
}

BF_EXPORT void BF_CALLTYPE RenderState_SetDepthFunc(RenderState* renderState, int depthFunc)
{
	renderState->SetDepthFunc((DepthFunc)depthFunc);
}

BF_EXPORT void BF_CALLTYPE RenderState_SetDepthWrite(RenderState* renderState, int depthWrite)
{
	renderState->SetWriteDepthBuffer(depthWrite != 0);
}

BF_EXPORT void BF_CALLTYPE RenderState_SetTopology(RenderState* renderState, int topology)
{
	renderState->SetTopology((Topology3D)topology);
}

BF_EXPORT Shader* BF_CALLTYPE Gfx_LoadShader(const char* fileName, VertexDefinition* vertexDefinition)
{
	return gBFApp->mRenderDevice->LoadShader(fileName, vertexDefinition);
}

BF_EXPORT void BF_CALLTYPE Gfx_SetRenderState(RenderState* renderState)
{
	BF_ASSERT(renderState->mShader != NULL);
	gBFApp->mRenderDevice->SetRenderState(renderState);
}

BF_EXPORT void BF_CALLTYPE Gfx_Shader_Delete(Shader* shader)
{
	delete shader;
}

BF_EXPORT ShaderParam* BF_CALLTYPE Gfx_GetShaderParam(Shader* shader, const char* shaderName)
{
	return shader->GetShaderParam(shaderName);
}

BF_EXPORT void BF_CALLTYPE BFInput_Destroy(BFInputDevice* inputDevice)
{
	delete inputDevice;
}

BF_EXPORT const char* BF_CALLTYPE BFInput_GetState(BFInputDevice* inputDevice)
{
	String& outString = *gBeefySys_TLStrReturn.Get();
	outString = inputDevice->GetState();
	return outString.c_str();
}

BF_EXPORT int BF_CALLTYPE BF_TickCount()
{
	return (int) BFTickCount();
}

BF_EXPORT int64 BF_CALLTYPE BF_TickCountMicroFast()
{
	return (int) BFGetTickCountMicroFast();
}

BF_EXPORT void BF_CALLTYPE BF_Test()
{
	BF_ASSERT(false);

	int iArr[] = {2, 3, 4};

	for (int i = 0; i < 10; i++)
		OutputDebugStrF("Hey %d\n", i);

	OutputDebugStrF("Break\n");

	for (int i : iArr)
		OutputDebugStrF("Hey %d\n", i);
}