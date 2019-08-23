#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/util/String.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/FileStream.h"
#include "Debugger.h"
#include "DebugManager.h"

namespace Beefy
{
	class NetResult;
}

NS_BF_DBG_BEGIN

class DbgSymSrv;

class DbgSymRequest
{
public:	
	Beefy::CritSect mCritSect;	
	DbgSymSrv* mDbgSymSrv;
	Beefy::DbgSymSrvOptions mOptions;
	
	Beefy::String mModulePath;
	Beefy::String mPDBRequested;
	uint8 mWantGuid[16];
	int32 mWantAge;	
	DWORD mLastUpdateTick;

	Beefy::String mOutPath;
	Beefy::FileStream mOutFile;

	Beefy::String mCurURL;
	Beefy::String mImagePath;
	Beefy::String mFinalPDBPath;
	Beefy::String mError;
	bool mFailed;
	bool mMayBeOld;
	bool mSearchingSymSrv;
	bool mCancelling;
	bool mInProcess;
	bool mIsPreCache;

public:
	DbgSymRequest();	
	~DbgSymRequest();

	bool CheckPEFile(const Beefy::StringImpl& filePath, uint32 fileTime, int size);

public:
	Beefy::String GetGuidString();
	Beefy::String GetPDBStoreDir();

	void Fail(const Beefy::StringImpl& error);
	bool CheckPDBData(const Beefy::StringImpl& path, uint8 outGuid[16], int32& outAge);

	bool Get(const Beefy::StringImpl& url, const Beefy::StringImpl& destPath, Beefy::NetResult** chainNetResult = NULL, bool ignoreSuccess = false);

	void SearchLocal(); // Only search for in specified path and in module path
	void SearchCache(); // Fast, single directory targeted check
	
	void SearchSymSrv(); // Slow - can access network
	Beefy::String SearchForImage(const Beefy::String& filePath, uint32 fileTime, int size); // Slow - can access network
	void Cancel();
	bool IsDone();
};

class DbgSymSrv
{
public:	
	Beefy::Debugger* mDebugger;

public:
	DbgSymSrv(Beefy::Debugger* debugger);

	void PreCacheImage(const Beefy::String& filePath, uint32 fileTime, int size);

	DbgSymRequest* CreateRequest(const Beefy::StringImpl& modulePath, const Beefy::StringImpl& pdbRequested, uint8 wantGuid[16], int32 wantAge);
	DbgSymRequest* CreateRequest();
	void ReleaseRequest(DbgSymRequest* dbgSymRequest);
	
	void Update();	
};

NS_BF_DBG_END