#include "BFApp.h"
#include "BFWindow.h"
#include "gfx/RenderDevice.h"
#include "FileStream.h"
#include "util/BSpline.h"
#include "util/PerfTimer.h"
#include "sound/WwiseSound.h"

#include "util/AllocDebug.h"

#pragma warning(disable:4996)

USING_NS_BF;

BFApp* Beefy::gBFApp = NULL;

BFApp::BFApp()
{
	mTitle = "Beefy Application";
	mRefreshRate = 60;	
	mLastProcessTick = BFTickCount();
	mPhysFrameTimeAcc = 0;
	mDrawEnabled = true;
	
	mUpdateFunc = NULL;
	mUpdateFFunc = NULL;
	mDrawFunc = NULL;
	
	gBFApp = this;
	mSysDialogCnt = 0;
	mCursor = CURSOR_POINTER;
	mInProcess = false;
	mUpdateCnt = 0;
	mNumPhysUpdates = 0;
    mVSynched = true;
	mMaxUpdatesPerDraw = 60; // 8?
    
    mUpdateSampleCount = 0;
    mUpdateSampleTimes = 0;

    if (gPerfManager == NULL)
        gPerfManager = new PerfManager();

	mRunning = false;
	mRenderDevice = NULL;
	mVSynched = false;
	mVSyncActive = false;
	mForceNextDraw = false;

	mUpdateCnt = 0;
	mUpdateCntF = 0;
	mClientUpdateCntF = 0;
}

BFApp::~BFApp()
{
	gBFApp = NULL;	
	delete gPerfManager;
	for (auto window : mPendingWindowDeleteList)
		delete window;	
}

void BFApp::Init()
{
}

void BFApp::Run()
{
}

void BFApp::Shutdown()
{
	mRunning = false;
}

void BFApp::SetCursor(int cursor)
{
	mCursor = cursor;
	PhysSetCursor();
}

void BFApp::Update(bool batchStart)
{
    //Beefy::DebugTimeGuard suspendTimeGuard(30, "BFApp::Update");
#ifdef BF_WWISE_ENABLED
	WWiseUpdate();
#endif
	
	mUpdateCnt++;
	gPerfManager->NextFrame();
	gPerfManager->ZoneStart("BFApp::Update");
	mUpdateFunc(batchStart);
	gPerfManager->ZoneEnd();

	for (auto window : mPendingWindowDeleteList)
		delete window;
	mPendingWindowDeleteList.clear();
}

void BFApp::UpdateF(float updatePct)
{
	mUpdateFFunc(updatePct);
}

void BFApp::Draw()
{    
	gPerfManager->ZoneStart("BFApp::Draw");
	mDrawFunc(mForceNextDraw);	
	mForceNextDraw = false;
	gPerfManager->ZoneEnd();
}

//#define PERIODIC_PERF_TIMING

void BFApp::Process()
{
    //Beefy::DebugTimeGuard suspendTimeGuard(30, "BFApp::Process");
    
	RenderWindow* headRenderWindow = NULL;

 	float physRefreshRate = 0;
	if ((mRenderDevice != NULL) && (!mRenderDevice->mRenderWindowList.IsEmpty()))
	{
		headRenderWindow = mRenderDevice->mRenderWindowList[0];
		physRefreshRate = headRenderWindow->GetRefreshRate();
	}

	if (physRefreshRate <= 0)
		physRefreshRate = 60.0f;

	float ticksPerFrame = 1;
	float physTicksPerFrame = 1000.0f / physRefreshRate;

	if (mInProcess)
		return; // No reentry
	mInProcess = true;
	
	uint32 tickNow = BFTickCount();
	const int vSyncTestingPeriod = 250;
		
	bool didVBlankWait = false;
		
	if (mVSyncActive)
	{		
		// Have a time limit in the cases we miss the vblank
		if (mVSyncEvent.WaitFor((int)(physTicksPerFrame + 1)))
			didVBlankWait = true;
	}

	if (mRefreshRate > 0)
		ticksPerFrame = 1000.0f / mRefreshRate;
	int ticksSinceLastProcess = tickNow - mLastProcessTick;

    mUpdateSampleCount++;
    mUpdateSampleTimes += ticksSinceLastProcess;
    //TODO: Turn off mVSynched based on error calculations - (?)

	// Two VSync failures in a row means we set mVSyncFailed and permanently disable it
	if (mUpdateSampleTimes >= vSyncTestingPeriod)
	{
		int expectedFrames = (int)(mUpdateSampleTimes / ticksPerFrame);
		if (mUpdateSampleCount > expectedFrames * 1.5)			
		{
			if (!mVSynched)
				mVSyncFailed = true;				
			mVSynched = false;
		}
		else
		{
			if (!mVSyncFailed)
				mVSynched = true;
		}
			
		mUpdateSampleCount = 0;
		mUpdateSampleTimes = 0;
	}
        		
	mPhysFrameTimeAcc += tickNow - mLastProcessTick;
        	
	if (didVBlankWait)
	{
		// Try to keep time synced with vblank
		if (mPhysFrameTimeAcc < physTicksPerFrame * 2)
		{
			float timeAdjust = physTicksPerFrame - mPhysFrameTimeAcc + 0.001f;
			mPhysFrameTimeAcc += timeAdjust;				
		}
	}

    /*if (updates > 2)
        OutputDebugStrF("Updates: %d  TickDelta: %d\n", updates, tickNow - mLastProcessTick);*/	
    
	// Compensate for "slow start" by limiting the number of catchup-updates we can do when starting the app
	int maxUpdates = BF_MIN(mNumPhysUpdates + 1, mMaxUpdatesPerDraw);

	while (mPhysFrameTimeAcc >= physTicksPerFrame)
	{
		mPhysFrameTimeAcc -= physTicksPerFrame;
		mUpdateCntF += physTicksPerFrame / ticksPerFrame;
	}
    	
	static uint32 lastUpdate = BFTickCount();	
		
#ifdef PERIODIC_PERF_TIMING
	bool perfTime = (tickNow - lastUpdate >= 5000) && (updates > 0);	
	if (perfTime)
	{
		updates = 1;		
		lastUpdate = tickNow;
				
		if (perfTime)
			gPerfManager->StartRecording();	
	}
#endif

		
	int didUpdateCnt = 0;
	
	if (mUpdateCntF - mClientUpdateCntF > physRefreshRate / 2)
	{
		// Too large of a difference, just sync
		mClientUpdateCntF = mUpdateCntF - 1;
	}

	while ((int)mClientUpdateCntF < (int)mUpdateCntF)
	{
		Update(didUpdateCnt == 0);
		didUpdateCnt++;		
		mClientUpdateCntF = (int)mClientUpdateCntF + 1.000001;
		if (didUpdateCnt >= maxUpdates)
			break;
	}

	// Only attempt UpdateF updates if our rates aren't nearly the same
	if ((mRefreshRate != 0) && (fabs(physRefreshRate - mRefreshRate) / (float)mRefreshRate > 0.1f))
	{
		float updateFAmt = (float)(mUpdateCntF - mClientUpdateCntF);
		if ((updateFAmt > 0.05f) && (updateFAmt < 1.0f) && (didUpdateCnt < maxUpdates))
		{
			UpdateF(updateFAmt);
			didUpdateCnt++;
			mClientUpdateCntF = mUpdateCntF;
		}
	}

	if (didUpdateCnt > 0)
		mNumPhysUpdates++;

	if ((mRunning) && (didUpdateCnt == 0))
	{
		BfpThread_Sleep(1);				
	}
	
	if ((mRunning) && 
		((didUpdateCnt != 0) || (mForceNextDraw)))
		Draw();

#ifdef PERIODIC_PERF_TIMING
	if (perfTime)
	{
		gPerfManager->StopRecording();	
		gPerfManager->DbgPrint();
	}
#endif

	mLastProcessTick = tickNow;
	mInProcess = false;
}

void BFApp::RemoveWindow(BFWindow* window)
{
	AutoCrit autoCrit(mCritSect);

	auto itr = std::find(mWindowList.begin(), mWindowList.end(), window);
	if (itr == mWindowList.end()) // Allow benign failure (double removal)
		return; 
	mWindowList.erase(itr);

	while (window->mChildren.size() > 0)
		RemoveWindow(window->mChildren.front());

	if (window->mParent != NULL)
	{		
		window->mParent->mChildren.erase(std::find(window->mParent->mChildren.begin(), window->mParent->mChildren.end(), window));

		if (window->mFlags & BFWINDOW_MODAL)
		{	
			bool hasModal = false;

			for (auto childWindow : window->mParent->mChildren)			
			{
				if (childWindow->mFlags & BFWINDOW_MODAL)
					hasModal = true;				
			}

			if (!hasModal)
				window->mParent->ModalsRemoved();
		}	
	}

	window->mClosedFunc(window);
	mRenderDevice->RemoveRenderWindow(window->mRenderWindow);	
	window->Destroy();
	mPendingWindowDeleteList.push_back(window);
}

FileStream* BFApp::OpenBinaryFile(const StringImpl& fileName)
{
	FILE* fP = fopen(fileName.c_str(), "rb");
	if (fP == NULL)
		return NULL;

	FileStream* fileStream = new FileStream();
	fileStream->mFP = fP;
	return fileStream;
}
