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
	mFrameTimeAcc = 0;
	mDrawEnabled = true;
	
	mUpdateFunc = NULL;
	mDrawFunc = NULL;
	
	gBFApp = this;
	mSysDialogCnt = 0;
	mCursor = CURSOR_POINTER;
	mInProcess = false;
	mUpdateCnt = 0;
    mVSynched = true;
	mMaxUpdatesPerDraw = 60; // 8?
    
    mUpdateSampleCount = 0;
    mUpdateSampleTimes = 0;

    if (gPerfManager == NULL)
        gPerfManager = new PerfManager();

	mRunning = false;
	mRenderDevice = NULL;
	mVSynched = false;
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

void BFApp::Draw()
{    
	gPerfManager->ZoneStart("BFApp::Draw");
	mDrawFunc();	
	gPerfManager->ZoneEnd();
}

//#define PERIODIC_PERF_TIMING

void BFApp::Process()
{
    //Beefy::DebugTimeGuard suspendTimeGuard(30, "BFApp::Process");
    
	if (mInProcess)
		return; // No reentry
	mInProcess = true;

	int updates;
	
	uint32 tickNow = BFTickCount();
	const int vSyncTestingPeriod = 250;

	if (mRefreshRate != 0)
	{
		float ticksPerFrame = 1000.0f / mRefreshRate;
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
				if (!mVSyncFailed)
					mVSynched = true;
			
			mUpdateSampleCount = 0;
			mUpdateSampleTimes = 0;
		}
        
		mFrameTimeAcc += tickNow - mLastProcessTick;			
        
		bool vSynched = mVSynched;

		if (vSynched)
		{
			// For the startup, try not to go hyper during those first samplings
			if (mUpdateSampleTimes <= vSyncTestingPeriod)
			{
				if (ticksSinceLastProcess < ticksPerFrame / 1.5)
					vSynched = false;
			}
		}

        if (vSynched)
        {
       		updates = std::max(1, (int)(mFrameTimeAcc / ticksPerFrame + 0.5f));
            mFrameTimeAcc = std::max(0.0f, mFrameTimeAcc - ticksPerFrame * updates);
        }
        else
        {
            updates = std::max(0, (int)(mFrameTimeAcc / ticksPerFrame));
            mFrameTimeAcc = mFrameTimeAcc - ticksPerFrame * updates;
        }
        
		if (updates > mRefreshRate)
		{
			// If more than 1 second of updates is queued, just re-sync
			updates = 1;
			mFrameTimeAcc = 0;
		}

		updates = std::min(updates, mMaxUpdatesPerDraw);
        
        /*if (updates > 2)
            OutputDebugStrF("Updates: %d  TickDelta: %d\n", updates, tickNow - mLastProcessTick);*/
	}
	else
		updates = 1; // RefreshRate of 0 means to update as fast as possible
    
    if (updates == 0)
    {
        // Yield
        BfpThread_Sleep(1);
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

	for (int updateNum = 0; updateNum < updates; updateNum++)
		Update(updateNum == 0);
	
	if ((mRunning) && (updates > 0))
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
