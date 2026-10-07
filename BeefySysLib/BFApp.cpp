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
	mLastWakeMicros = BFGetTickCountMicro();
	mPhysFrameTimeAcc = 0;
	mPhysFrameTimeErr = 0;
	mDrawEnabled = true;
	
	mUpdateFunc = NULL;
	mUpdateFFunc = NULL;
	mDrawFunc = NULL;
	mIdleUpdateFunc = NULL;
	
	gBFApp = this;
	mSysDialogCnt = 0;
	mCursor = CURSOR_POINTER;
	mInProcess = false;
	mProcessCount = 0;
	mUpdateCnt = 0;
	mSlowStartItr = 0;
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
	mExternalPacingActive = false;
	mFramePacer = NULL;
	mFramePacerRefreshRate = 0;
	mWindowFramePacer = NULL;
	mFramePacerWaits = 0;
	mFramePacerSignaled = 0;
	mFramePacerWaitMicros = 0;
	mVirtualFocus = false;
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

bool BFApp::IdleUpdate()
{
	return (mRunning) && (mIdleUpdateFunc != NULL) && (mIdleUpdateFunc());
}

// Waits up to timeoutMS for waitFunc, calling IdleUpdate between 1ms slices for as long as it asks to be
template <typename T>
static bool WaitWithIdle(BFApp* app, int timeoutMS, T waitFunc)
{
	uint32 startTick = BFTickCount();
	while (app->IdleUpdate())
	{
		int remaining = timeoutMS - (int)(BFTickCount() - startTick);
		if (remaining <= 0)
			return false;
		if (waitFunc(BF_MIN(remaining, 1)))
			return true;
	}
	return waitFunc(BF_MAX(timeoutMS - (int)(BFTickCount() - startTick), 0));
}

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

	if ((mFramePacer != NULL) && (mFramePacerRefreshRate > 0))
		physRefreshRate = mFramePacerRefreshRate;
	if (physRefreshRate <= 0)
		physRefreshRate = 60.0f;

	float ticksPerFrame = 1;
	float physTicksPerFrame = 1000.0f / physRefreshRate;

	if (mInProcess)
		return; // No reentry
	mInProcess = true;
	mProcessCount++;

	uint32 tickNow = BFTickCount();
	const int vSyncTestingPeriod = 250;
		
	bool didVBlankWait = false;
	bool externalSignaled = false;
	// The frame goes out in step with a display, so it is on screen for a whole number of refreshes
	bool presentPaced = false;

	if ((!mUnthrottledRendering) && (mFramePacer != NULL))
	{
		presentPaced = true;
		uint64 waitStart = BFGetTickCountMicroFast();
		externalSignaled = WaitWithIdle(this, (int)(physTicksPerFrame * 4 + 1), [&](int timeoutMS) { return (mFramePacer != NULL) && (mFramePacer->WaitForFrame(timeoutMS)); });
		mFramePacerWaits++;
		if (externalSignaled)
			mFramePacerSignaled++;
		mFramePacerWaitMicros += (int64)(BFGetTickCountMicroFast() - waitStart);
	}
	else if ((!mUnthrottledRendering) && (mExternalPacingActive))
	{
		presentPaced = true;
		// Timeout keeps us alive at correct game speed (wall-clock catchup) if the pacer stalls
		externalSignaled = WaitWithIdle(this, (int)(physTicksPerFrame * 4 + 1), [&](int timeoutMS) { return WaitForExternalPacing(timeoutMS); });
	}
	else if ((!mUnthrottledRendering) && (mWindowFramePacer != NULL))
	{
		presentPaced = true;
		// A slot can free up early, so the wake isn't vblank-aligned; the present still flips on one. The timeout only
		// has to outlast a slow GPU frame.
		externalSignaled = WaitWithIdle(this, 100, [&](int timeoutMS) { return (mWindowFramePacer != NULL) && (mWindowFramePacer->WaitForFrame(timeoutMS)); });
	}
	else if ((!mUnthrottledRendering) && (mVSyncActive))
	{
		// Have a time limit in the cases we miss the vblank
		if (WaitWithIdle(this, (int)(physTicksPerFrame + 1), [&](int timeoutMS) { return mVSyncEvent.WaitFor(timeoutMS); }))
			didVBlankWait = true;
		presentPaced = didVBlankWait;
	}
	uint64 wakeMicros = BFGetTickCountMicro();

	// Input that arrived during the wait belongs to this frame, not the next one. A window move/size or a menu can
	// start a modal loop in here, whose WM_TIMER frames must be able to run; if any did, they took this frame's place.
	int processCount = mProcessCount;
	mInProcess = false;
	PumpMessages();
	if (mProcessCount != processCount)
		return;
	mInProcess = true;

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
        		
	// Measured wake to wake, so a frame is given its own time rather than the previous frame's.
	mPhysFrameTimeErr += (float)((wakeMicros - mLastWakeMicros) / 1000.0);
	mLastWakeMicros = wakeMicros;
	// A paced frame is on screen for a whole number of refreshes, which keeps motion even, but that number isn't
	// always one. What rounding leaves behind is carried into the next pass: dropping it slows the update rate
	// whenever a frame takes longer than a refresh. A frame that goes out shows for at least one refresh, so an
	// early wake must not round down to nothing.
	float timeAdvance = mPhysFrameTimeErr;
	if (presentPaced)
	{
		float refreshes = floorf(mPhysFrameTimeErr / physTicksPerFrame + 0.5f);
		if ((refreshes < 1) && ((didVBlankWait) || (externalSignaled)))
			refreshes = 1;
		timeAdvance = refreshes * physTicksPerFrame;
	}
	mPhysFrameTimeErr -= timeAdvance;
	// In step with the display, its clock is the one to follow: let tick rounding and clock skew fade out
	if ((presentPaced) && (timeAdvance == physTicksPerFrame))
		mPhysFrameTimeErr *= 0.98f;

    /*if (updates > 2)
        OutputDebugStrF("Updates: %d  TickDelta: %d\n", updates, tickNow - mLastProcessTick);*/	
    
	// Compensate for "slow start" by limiting the number of catchup-updates we can do when starting the app, or
	// again after BFApp_ResetSlowStart
	int maxUpdates = BF_MIN(mSlowStartItr + 1, mMaxUpdatesPerDraw);

	if ((presentPaced) || (mUnthrottledRendering))
		mUpdateCntF += timeAdvance / ticksPerFrame;
	else
	{
		// Unpaced, the loop polls about every millisecond and draws only when time was handed over, so these
		// refresh-sized steps are its frame rate
		mPhysFrameTimeAcc = BF_MAX(mPhysFrameTimeAcc, 0.001f) + timeAdvance;
		while (mPhysFrameTimeAcc >= physTicksPerFrame)
		{
			mPhysFrameTimeAcc -= physTicksPerFrame;
			mUpdateCntF += physTicksPerFrame / ticksPerFrame;
		}
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

	while ((mRunning) && ((int)mClientUpdateCntF < (int)mUpdateCntF))
	{
		Update(didUpdateCnt == 0);
		didUpdateCnt++;		
		mClientUpdateCntF = (int)mClientUpdateCntF + 1.000001;
		if (didUpdateCnt >= maxUpdates)
			break;
	}

	// At nearly matching rates a paced frame is a whole tick; unthrottled frames split ticks at any rate
	if ((mRunning) && (mRefreshRate != 0) &&
		((mUnthrottledRendering) || (fabs(physRefreshRate - mRefreshRate) / (float)mRefreshRate > 0.1f)))
	{
		float updateFAmt = (float)(mUpdateCntF - mClientUpdateCntF);
		// An unthrottled frame can be a small fraction of a tick; this floor only screens out rounding noise there
		float minUpdateFAmt = mUnthrottledRendering ? 0.001f : 0.05f;
		if ((updateFAmt > minUpdateFAmt) && (updateFAmt < 1.0f) && (didUpdateCnt < maxUpdates))
		{
			UpdateF(updateFAmt);
			didUpdateCnt++;
			mClientUpdateCntF = mUpdateCntF;
		}
	}

	if (didUpdateCnt > 0)
		mSlowStartItr++;

	if ((mRunning) && (didUpdateCnt == 0) && (!externalSignaled) && (!mUnthrottledRendering))
	{
		IdleUpdate();
		BfpThread_Sleep(1);
	}

	// A signaled wake always draws so the pacer gets exactly one frame per signal
	if ((mRunning) &&
		((didUpdateCnt != 0) || (mForceNextDraw) || (externalSignaled) || (mUnthrottledRendering)))
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
