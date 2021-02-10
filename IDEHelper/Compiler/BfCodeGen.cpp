#pragma warning(push)
#pragma warning(disable:4141)
#pragma warning(disable:4146)
#pragma warning(disable:4291)
#pragma warning(disable:4244)
#pragma warning(disable:4267)
#pragma warning(disable:4624)
#pragma warning(disable:4800)
#pragma warning(disable:4996)

#include "BfCompiler.h"
#include "BfCodeGen.h"
#include "BfIRCodeGen.h"
#include "BfModule.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/BeefPerf.h"

#ifdef BF_PLATFORM_WINDOWS
#include "../Backend/BeIRCodeGen.h"
#include "../Backend/BeCOFFObject.h"
#include "../Backend/BeLibManger.h"
#endif

//#define BF_IGNORE_BACKEND_HASH

//#include "../X86.h"

#ifdef _DEBUG
//#define MAX_THREADS 3
//#define MAX_THREADS 1
//#define MAX_THREADS 6
//#define BF_USE_CODEGEN_RELEASE_THUNK
#else
//#define MAX_THREADS 6
//#define MAX_THREADS 8
//#define MAX_THREADS 1
#endif

//#define CODEGEN_DISABLE_CACHE
//#define CODEGEN_DISABLE_CACHE_BEEFBACKEND

// Emergency debugging only! Slow!
//#define DBG_FORCE_SYNCHRONIZED

// This is used for the Release DLL thunk and the build.dat file
#define BF_CODEGEN_VERSION 14

#undef DEBUG

#pragma warning(disable:4267)

#include "BeefySysLib/util/AllocDebug.h"

#pragma warning(pop)

USING_NS_BF;
using namespace llvm;


String BfCodeGenDirectoryData::GetDataFileName()
{
	return mDirectoryName + "/build.dat";
}

void BfCodeGenDirectoryData::Read()
{
	FileStream fileStream;
	String fileName = GetDataFileName();
	if (!fileStream.Open(fileName, "rb"))
		return;

	int fileId = fileStream.ReadInt32();
	if (fileId != 0xBEEF0100)
		return;

	int version = fileStream.ReadInt32();
	if (version != BF_CODEGEN_VERSION)
		return;

	Val128 backendHash;
	fileStream.ReadT(backendHash);
#ifndef BF_IGNORE_BACKEND_HASH
	if (mCodeGen->mBackendHash != backendHash)
		return;
#endif

	int numFiles = fileStream.ReadInt32();
	for (int fileIdx = 0; fileIdx < numFiles; fileIdx++)
	{
		String fileName = fileStream.ReadAscii32SizedString();
		BfCodeGenFileData fileData;		
		fileStream.Read(&fileData.mIRHash, sizeof(Val128));
		fileStream.Read(&fileData.mIROrderedHash, sizeof(Val128));
		fileStream.Read(&fileData.mLastWasObjectWrite, sizeof(bool));
		mFileMap[fileName] = fileData;
	}

	int numValues = fileStream.ReadInt32();
	for (int valIdx = 0; valIdx < numValues; valIdx++)
	{
		String key = fileStream.ReadAscii32SizedString();
		String value = fileStream.ReadAscii32SizedString();
		mBuildSettings[key] = value;
	}

	mFileTime = GetFileTimeWrite(fileName);
}

void BfCodeGenDirectoryData::Write()
{
	if (mFileFailed)
	{
		// We could be smarter about write failures, but we just nuke eveything here
		//  to ensure that the next rebuild attempts to generate the files again
		mFileMap.Clear();
		mFileFailed = false;
	}

	FileStream fileStream;
	String fileName = GetDataFileName();
	if (!fileStream.Open(fileName, "wb"))
	{
		String directory = GetFileDir(fileName);
		if (!DirectoryExists(directory))
		{
			// Someone else (or the user) cleared this directory
			DoBfLog(2, "BfCodeGen cleared cache because '%s' was removed\n", directory.c_str());
			mFileMap.Clear();
			return;
		}

		mError = "Failed to write to " + fileName;
		return;
	}

	fileStream.Write((int)0xBEEF0100);
	fileStream.Write(BF_CODEGEN_VERSION);
	fileStream.WriteT(mCodeGen->mBackendHash);

	fileStream.Write((int)mFileMap.size());
	for (auto& pair : mFileMap)
	{
		fileStream.Write(pair.mKey);
		fileStream.Write(&pair.mValue.mIRHash, sizeof(Val128));
		fileStream.Write(&pair.mValue.mIROrderedHash, sizeof(Val128));
		fileStream.Write(&pair.mValue.mLastWasObjectWrite, sizeof(bool));
	}

	fileStream.Write((int)mBuildSettings.size());
	for (auto& pair : mBuildSettings)
	{
		fileStream.Write(pair.mKey);
		fileStream.Write(pair.mValue);
	}

	fileStream.Close();
	mFileTime = GetFileTimeWrite(fileName);
}

void BfCodeGenDirectoryData::Verify()
{	
	if (!mError.empty())
		return;

	String fileName = GetDataFileName();
	int64 fileTimeWrite = GetFileTimeWrite(fileName);
	if ((fileTimeWrite == mFileTime) || (mFileTime == 0))
	{
		mVerified = true;
	}
	else
	{
		mError = "Build directory corrupted, perform clean";		
	}	
}

void BfCodeGenDirectoryData::Clear()
{	
	mFileMap.Clear();
	mBuildSettings.Clear();
}

bool BfCodeGenDirectoryData::CheckCache(const StringImpl& fileName, Val128 hash, Val128* outOrderedHash, bool disallowObjectWrite)
{
	if (!mVerified)
		Verify();

	BfCodeGenFileData* fileData = NULL;
	
	if (!mFileMap.TryAdd(fileName, NULL, &fileData))
	{	
		if ((fileData->mLastWasObjectWrite) && (disallowObjectWrite))
			return false;
		if (outOrderedHash != NULL)
			*outOrderedHash = fileData->mIROrderedHash;
		if (fileData->mIRHash == hash)								
			return true;		
		fileData->mIRHash = hash;		
		return false;
	}

	fileData->mLastWasObjectWrite = false;
	fileData->mIRHash = hash;
	return false;
}

void BfCodeGenDirectoryData::SetHash(const StringImpl& fileName, Val128 hash, Val128 orderedHash, bool isObjectWrite)
{
	if (!mVerified)
		Verify();

	BfCodeGenFileData* fileData = NULL;

	mFileMap.TryAdd(fileName, NULL, &fileData);	
	fileData->mIRHash = hash;
	fileData->mIROrderedHash = orderedHash;	
	fileData->mLastWasObjectWrite = isObjectWrite;
}

void BfCodeGenDirectoryData::ClearHash(const StringImpl& fileName)
{
	mFileMap.Remove(fileName);
}


void BfCodeGenDirectoryData::FileFailed()
{
	mFileFailed = true;
}

String BfCodeGenDirectoryData::GetValue(const StringImpl& key)
{
	String* valuePtr = NULL;
	if (mBuildSettings.TryGetValue(key, &valuePtr))
	{
		return *valuePtr;
	}
	return String();
}

void BfCodeGenDirectoryData::SetValue(const StringImpl& key, const StringImpl& value)
{	
	mBuildSettings[key] = value;
}

//////////////////////////////////////////////////////////////////////////

void BfCodeGenRequest::DbgSaveData()
{
	/*FILE* fp = fopen("c:\\temp\\dbgOut.bc", "wb");
	if (fp == NULL)
	{
		Beefy::OutputDebugStrF("Failed to write\n");
	}
	fwrite(mData.begin(), mData.size(), 1, fp);
	fclose(fp);*/
}

void DbgSaveData(BfCodeGenRequest* genRequest)
{
	genRequest->DbgSaveData();
}

//////////////////////////////////////////////////////////////////////////

BfCodeGenThread::BfCodeGenThread()
{	
	mShuttingDown = false;
	mRunning = false;		
	mThreadIdx = 0;
	mCodeGen = NULL;
}

BfCodeGenThread::~BfCodeGenThread()
{
	Shutdown();		
}

void BfCodeGenThread::RunLoop()
{
	String threadName = StrFormat("CodeGen/Worker %d", mThreadIdx);
	BpSetThreadName(threadName.c_str());
	BfpThread_SetName(NULL, threadName.c_str(), NULL);
		
	while (!mShuttingDown)
	{				
		BfCodeGenRequest* request = NULL;
		{
			AutoCrit autoCrit(mCodeGen->mPendingRequestCritSect);
			if (!mCodeGen->mPendingRequests.IsEmpty())
			{
				request = mCodeGen->mPendingRequests[0];
				mCodeGen->mPendingRequests.RemoveAt(0);				
			}
		}

		if (request == NULL)
		{			
			mCodeGen->mRequestEvent.WaitFor(20);
			continue;
		}

		auto cacheDir = GetFileDir(request->mOutFileName);
		auto cacheFileName = GetFileName(request->mOutFileName);

		StringT<256> objFileName = request->mOutFileName + BF_OBJ_EXT;

		bool hasCacheMatch = false;
		BfCodeGenDirectoryData* dirCache = NULL;

		Val128 hash;
		Val128 orderedHash;

		DoBfLog(2, "BfCodeGen handle %s\n", request->mOutFileName.c_str());

		bool isLibWrite = (request->mOptions.mOptLevel == BfOptLevel_OgPlus) && (request->mOptions.mWriteToLib) && (!request->mOptions.mIsHotCompile);

#ifndef CODEGEN_DISABLE_CACHE
		{
			AutoCrit autoCrit(mCodeGen->mCacheCritSect);						
			dirCache = mCodeGen->GetDirCache(cacheDir);

			//For testing only!
			/*{
				FileStream fileStr;
				fileStr.Open(request->mOutFileName + ".bfir", "wb");
				fileStr.Write(request->mData.mVals, request->mData.mSize);
			}*/

			HashContext hashCtx;						
			hashCtx.Mixin(request->mOptions.mHash);
			hashCtx.Mixin(request->mData.mVals, request->mData.mSize);			
			hash = hashCtx.Finish128();
						
			hasCacheMatch = dirCache->CheckCache(cacheFileName, hash, &orderedHash, isLibWrite);

#ifdef BF_PLATFORM_WINDOWS
			if ((!hash.IsZero()) && (request->mOptions.mWriteToLib) && (request->mOptions.mOptLevel == BfOptLevel_OgPlus))
			{
				if (!BeLibManager::Get()->AddUsedFileName(objFileName))
				{
					// Oops, this library doesn't actually have this file so we can't use it from the cache. This can happen if we use a type,
					//  delete it, and then add it back (for example). The file info will still be in the cache but the lib won't have it.
					orderedHash = Val128();
					hash = Val128();
					hasCacheMatch = false;
				}
			}			

#endif

		}
#endif

		//TODO: Temporary, to make sure it keeps getting rebuilt

#ifdef CODEGEN_DISABLE_CACHE_BEEFBACKEND
		if (request->mOptions.mOptLevel == BfOptLevel_OgPlus)
			hasCacheMatch = false;
#endif

		String errorMsg;		

		bool doBEProcessing = true; // TODO: Normally 'true' so we do ordered cache check for LLVM too
		if (request->mOptions.mOptLevel == BfOptLevel_OgPlus)
			doBEProcessing = true; // Must do it for this

		BfCodeGenResult result;
		result.mType = BfCodeGenResult_Done;
		result.mErrorMsgBufLen = 0;
		if (hasCacheMatch)
		{
			result.mType = BfCodeGenResult_DoneCached;
		}
		else
		{
#ifdef BF_PLATFORM_WINDOWS			
			BeIRCodeGen* beIRCodeGen = new BeIRCodeGen();
			defer ( delete beIRCodeGen; );

			beIRCodeGen->SetConfigConst(BfIRConfigConst_VirtualMethodOfs, request->mOptions.mVirtualMethodOfs);
			beIRCodeGen->SetConfigConst(BfIRConfigConst_DynSlotOfs, request->mOptions.mDynSlotOfs);

			if (doBEProcessing)
			{				
				BP_ZONE("ProcessBfIRData");			
				beIRCodeGen->Init(request->mData);

				BeHashContext hashCtx;
				hashCtx.Mixin(request->mOptions.mHash);
				beIRCodeGen->Hash(hashCtx);				
				auto newOrderedHash = hashCtx.Finish128();
				DoBfLog(2, "Ordered hash for %s New:%s Old:%s\n", cacheFileName.c_str(), newOrderedHash.ToString().c_str(), orderedHash.ToString().c_str());
				hasCacheMatch = newOrderedHash == orderedHash;
				
				errorMsg = beIRCodeGen->mErrorMsg;
				orderedHash = newOrderedHash;
			}

			if (hasCacheMatch)
			{
				result.mType = BfCodeGenResult_DoneCached;
			}
			
#ifndef CODEGEN_DISABLE_CACHE
			{
				AutoCrit autoCrit(mCodeGen->mCacheCritSect);								
				dirCache->SetHash(cacheFileName, hash, orderedHash, !isLibWrite);
			}
#endif

#endif
			if (request->mOutFileName.Contains("RuntimeThreadInit"))
			{
				NOP;
			}

			if ((hasCacheMatch) || (!errorMsg.IsEmpty()))
			{
				//
			}
			else if (request->mOptions.mOptLevel == BfOptLevel_OgPlus)
			{				
#ifdef BF_PLATFORM_WINDOWS
				BP_ZONE("BfCodeGen::RunLoop.Beef");

				if (request->mOptions.mWriteLLVMIR)
				{
					BP_ZONE("BfCodeGen::RunLoop.Beef.IR");
					std::error_code ec;
					String fileName = request->mOutFileName + ".beir";

					String str = beIRCodeGen->mBeModule->ToString();
					FileStream fs;
					if (!fs.Open(fileName, "w"))
					{
						if (!errorMsg.empty())
							errorMsg += "\n";
						errorMsg += "Failed writing IR '" + fileName + "': " + ec.message();						
					}
					else
						fs.WriteSNZ(str);
				}

				if (!hasCacheMatch)
					beIRCodeGen->Process();
				errorMsg = beIRCodeGen->mErrorMsg;

				DoBfLog(2, "Generating obj %s\n", request->mOutFileName.c_str());

				BeCOFFObject coffObject;
				coffObject.mWriteToLib = request->mOptions.mWriteToLib;				
				if (!coffObject.Generate(beIRCodeGen->mBeModule, objFileName))
					errorMsg = StrFormat("Failed to write object file: %s", objFileName.c_str());				

				if (!beIRCodeGen->mErrorMsg.IsEmpty())
				{
					if (!errorMsg.IsEmpty())
						errorMsg += "\n";
					errorMsg += beIRCodeGen->mErrorMsg;
				}

#else
				errorMsg = "Failed to generate object file";
#endif
			}
			else
			{
				BP_ZONE_F("BfCodeGen::RunLoop.LLVM %s", request->mOutFileName.c_str());

				BfIRCodeGen* llvmIRCodeGen = new BfIRCodeGen();
				llvmIRCodeGen->SetCodeGenOptions(request->mOptions);
				llvmIRCodeGen->SetConfigConst(BfIRConfigConst_VirtualMethodOfs, request->mOptions.mVirtualMethodOfs);
				llvmIRCodeGen->SetConfigConst(BfIRConfigConst_DynSlotOfs, request->mOptions.mDynSlotOfs);				
				llvmIRCodeGen->ProcessBfIRData(request->mData);
								
				errorMsg = llvmIRCodeGen->mErrorMsg;
				llvmIRCodeGen->mErrorMsg.Clear();

				if (errorMsg.IsEmpty())
				{					
					if (request->mOptions.mWriteLLVMIR)
					{
						BP_ZONE("BfCodeGen::RunLoop.LLVM.IR");
						String fileName = request->mOutFileName + ".ll";
						String irError;
						if (!llvmIRCodeGen->WriteIR(fileName, irError))
						{
							if (!errorMsg.empty())
								errorMsg += "\n";
							errorMsg += "Failed writing IR '" + fileName + "': " + irError;
							dirCache->FileFailed();
						}
					}

					if (request->mOptions.mWriteObj)
					{
						BP_ZONE("BfCodeGen::RunLoop.LLVM.OBJ");

						String outFileName;
						if (request->mOptions.mAsmKind != BfAsmKind_None)
							outFileName = request->mOutFileName + ".s";
						else
							outFileName = request->mOutFileName + BF_OBJ_EXT;
						if (!llvmIRCodeGen->WriteObjectFile(outFileName))
						{
							result.mType = BfCodeGenResult_Failed;
							dirCache->FileFailed();
						}
					}					
				}
								
				if (!llvmIRCodeGen->mErrorMsg.IsEmpty())
				{
					if (!errorMsg.IsEmpty())
						errorMsg += "\n";
					errorMsg += llvmIRCodeGen->mErrorMsg;
				}
				
				delete llvmIRCodeGen;
			}
		}

		if (!errorMsg.IsEmpty())
		{
			result.mType = BfCodeGenResult_Failed;
			int errorStrLen = (int)errorMsg.length();
			if ((result.mErrorMsgBufLen + errorStrLen + 1) < BfCodeGenResult::kErrorMsgBufSize)
			{
				memcpy(&result.mErrorMsgBuf[result.mErrorMsgBufLen], errorMsg.c_str(), errorStrLen + 1);
				result.mErrorMsgBufLen += errorStrLen + 1;
			}

			if (dirCache != NULL)
			{
				AutoCrit autoCrit(mCodeGen->mCacheCritSect);
				dirCache->ClearHash(cacheFileName);
			}
		}

		{
			// It's an extern request, so we own this
			bool deleteRequest = false;
			if (request->mExternResultPtr != NULL)
			{					
				*request->mExternResultPtr = result;
				deleteRequest = true;					
			}

			// We need this fence for BF_USE_CODEGEN_RELEASE_THUNK usage- because we can't access the request anymore after setting
			//  request->mIsDone
			BF_FULL_MEMORY_FENCE();

			AutoCrit autoCrit(mCodeGen->mPendingRequestCritSect);
			request->mResult = result;
			mCodeGen->mDoneEvent.Set();				
			if (deleteRequest)
				delete request;
		}				
	}

	mRunning = false;
	mCodeGen->mDoneEvent.Set();	
}

void BfCodeGenThread::Shutdown()
{
	mShuttingDown = true;
	if (mRunning)
		mCodeGen->mRequestEvent.Set(true);	

	while (mRunning)
	{
		mCodeGen->mDoneEvent.WaitFor(20);
	}
}

static void BFP_CALLTYPE RunLoopThunk(void* codeGenThreadP)
{
	auto codeGenThread = (BfCodeGenThread*)codeGenThreadP;
	BfpThread_SetName(NULL, StrFormat("BfCodeGenThread%d", codeGenThread->mThreadIdx).c_str(), NULL);
	codeGenThread->RunLoop();
}

void BfCodeGenThread::Start()
{
	mRunning = true;	
	//TODO: How much mem do we need? WTF- we have 32MB set before!
	auto mThread = BfpThread_Create(RunLoopThunk, (void*)this, 1024 * 1024, BfpThreadCreateFlag_StackSizeReserve);
	BfpThread_SetPriority(mThread, BfpThreadPriority_Low, NULL);
	BfpThread_Release(mThread);	
}

//////////////////////////////////////////////////////////////////////////

BfCodeGen::BfCodeGen()
{
	mAttemptedReleaseThunkLoad = false;
	mIsUsingReleaseThunk = false;	
	mReleaseModule = NULL;
	mClearCacheFunc = NULL;
	mGetVersionFunc = NULL;
	mKillFunc = NULL;
	mCancelFunc = NULL;
	mFinishFunc = NULL;
	mGenerateObjFunc = NULL;
	
	mRequestIdx = 0;
#ifdef MAX_THREADS
	mMaxThreadCount = MAX_THREADS;	
#else
	mMaxThreadCount = 6;
#endif
	mQueuedCount = 0;
	mCompletionCount = 0;
	mDisableCacheReads = false;

	HashContext hashCtx;
	hashCtx.Mixin(BF_CODEGEN_VERSION);

#ifdef BF_PLATFORM_WINDOWS
	char path[MAX_PATH];
	::GetModuleFileNameA(NULL, path, MAX_PATH);

	if (_strnicmp(path + 2, "\\Beef\\", 6) == 0)
	{
		path[8] = 0;

		char* checkFileNames[] =
		{
			"IDEHelper/Backend/BeCOFFObject.cpp",
			"IDEHelper/Backend/BeCOFFObject.h",
			"IDEHelper/Backend/BeContext.cpp",
			"IDEHelper/Backend/BeContext.h",
			"IDEHelper/Backend/BeDbgModule.h",
			"IDEHelper/Backend/BeIRCodeGen.cpp",
			"IDEHelper/Backend/BeIRCodeGen.h",
			"IDEHelper/Backend/BeLibManager.cpp",
			"IDEHelper/Backend/BeLibManager.h",
			"IDEHelper/Backend/BeMCContext.cpp",
			"IDEHelper/Backend/BeMCContext.h",
			"IDEHelper/Backend/BeModule.cpp",
			"IDEHelper/Backend/BeModule.h",
			"IDEHelper/Compiler/BfCodeGen.cpp",
			"IDEHelper/Compiler/BfCodeGen.h",
			"IDEHelper/Compiler/BfIRBuilder.cpp",
			"IDEHelper/Compiler/BfIRBuilder.h",
			"IDEHelper/Compiler/BfIRCodeGen.cpp",
			"IDEHelper/Compiler/BfIRCodeGen.h",
		};

		for (int i = 0; i < BF_ARRAY_COUNT(checkFileNames); i++)
		{
			String filePath;
			filePath += path;
			filePath += checkFileNames[i];
			auto writeTime = GetFileTimeWrite(filePath);
			hashCtx.Mixin(writeTime);
		}
	}
#endif

	mBackendHash = hashCtx.Finish128();
}

BfCodeGen::~BfCodeGen()
{
	if (mIsUsingReleaseThunk)
		mKillFunc();

	if (mReleaseModule != NULL)
        BfpDynLib_Release(mReleaseModule);

	ClearOldThreads(true);

	for (auto thread : mThreads)
	{
		thread->mShuttingDown = true;
	}
	mRequestEvent.Set(true);

	for (auto thread : mThreads)
	{
		thread->Shutdown();
		delete thread;
	}

	for (auto request : mRequests)
	{
		delete request;
	}

	for (auto& entry : mDirectoryCache)
		delete entry.mValue;
}

void BfCodeGen::ResetStats()
{
	mQueuedCount = 0;
	mCompletionCount = 0;
}

void BfCodeGen::UpdateStats()
{
	while (mRequests.size() != 0)
	{
		auto request = mRequests[0];
		if (request->mResult.mType == BfCodeGenResult_NotDone)
			return;
		
		RequestComplete(request);
		delete request;
		mRequests.RemoveAt(0);
		return;
	}
}

void BfCodeGen::ClearOldThreads(bool waitForThread)
{
	while (mOldThreads.size() != 0)
	{
		auto thread = mOldThreads[0];
		if (waitForThread)
			thread->Shutdown();
		if (thread->mRunning)
			return;

		delete thread;
		mOldThreads.RemoveAt(0);
	}
}

void BfCodeGen::ClearBuildCache()
{
	AutoCrit autoCrit(mCacheCritSect);
	for (auto& dirCachePair : mDirectoryCache)
	{
		auto dirData = dirCachePair.mValue;
		dirData->Clear();		
	}
	// This just disables reading the cache file, but it does not disable creating
	//  the cache structes in memory and writing them out, thus it's valid to leave
	//  this 'true' forever
	mDisableCacheReads = true;

#ifdef BF_USE_CODEGEN_RELEASE_THUNK
	BindReleaseThunks();
	if (mClearCacheFunc != NULL)
		mClearCacheFunc();
#endif
}

void BfCodeGen::DoWriteObjectFile(BfCodeGenRequest* codeGenRequest, const void* ptr, int size, const StringImpl& outFileName, BfCodeGenResult* externResultPtr)
{	
	codeGenRequest->mData.mVals = (uint8*)ptr;
	codeGenRequest->mData.mSize = size;	
	codeGenRequest->mOutFileName = outFileName;
	codeGenRequest->mResult.mType = BfCodeGenResult_NotDone;
	codeGenRequest->mResult.mErrorMsgBufLen = 0;
	codeGenRequest->mExternResultPtr = externResultPtr;

	int threadIdx = mRequestIdx % mMaxThreadCount;
	if (threadIdx >= (int) mThreads.size())
	{
		AutoCrit autoCrit(mThreadsCritSect);
		BfCodeGenThread* thread = new BfCodeGenThread();
		thread->mThreadIdx = threadIdx;
		thread->mCodeGen = this;
		mThreads.push_back(thread);
		thread->Start();
	}

	auto thread = mThreads[threadIdx];

	BP_ZONE("WriteObjectFile_CritSect");
	AutoCrit autoCrit(mPendingRequestCritSect);	
	mPendingRequests.push_back(codeGenRequest);

#ifdef BF_PLATFORM_WINDOWS
	BF_ASSERT(!mRequestEvent.mEvent->mManualReset); // Make sure it's out of the SignalAll state	
#endif
	mRequestEvent.Set();

	mRequestIdx++;
}

void BfCodeGen::SetMaxThreads(int maxThreads)
{
#ifndef MAX_THREADS
	mMaxThreadCount = BF_CLAMP(maxThreads, 2, 64);
#endif
}

void BfCodeGen::BindReleaseThunks()
{
	if (mAttemptedReleaseThunkLoad)
		return;

	mAttemptedReleaseThunkLoad = true;
	if (mReleaseModule == NULL)
	{
#ifdef BF32
        mReleaseModule = BfpDynLib_Load("./IDEHelper32.dll");
#else
        mReleaseModule = BfpDynLib_Load("./IDEHelper64.dll");
#endif
		if (mReleaseModule == NULL)
		{
			BF_FATAL("Unable to locate release DLL. Please rebuild Release configuration.\n");
			return;
		}

#ifdef BF32
        mClearCacheFunc = (GetVersionFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "_BfCodeGen_ClearCache@0");
        mGetVersionFunc = (GetVersionFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "_BfCodeGen_GetVersion@0");
        mKillFunc = (KillFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "_BfCodeGen_Kill@0");
        mCancelFunc = (KillFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "_BfCodeGen_Cancel@0");
        mFinishFunc = (FinishFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "_BfCodeGen_Finish@0");
        mGenerateObjFunc = (GenerateObjFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "_BfCodeGen_GenerateObj@20");
#else
        mClearCacheFunc = (GetVersionFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "BfCodeGen_ClearCache");
        mGetVersionFunc = (GetVersionFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "BfCodeGen_GetVersion");
        mKillFunc = (KillFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "BfCodeGen_Kill");
        mCancelFunc = (KillFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "BfCodeGen_Cancel");
        mFinishFunc = (FinishFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "BfCodeGen_Finish");
        mGenerateObjFunc = (GenerateObjFunc)::BfpDynLib_GetProcAddress(mReleaseModule, "BfCodeGen_GenerateObj");
#endif

		if ((mGetVersionFunc == NULL) || (mKillFunc == NULL) || (mCancelFunc == NULL) || (mFinishFunc == NULL) || (mGenerateObjFunc == NULL))
		{
			BF_FATAL("Invalid signature in IDEHelper release DLL. Please rebuild Release configuration.\n");
			return;
		}

		if (mGetVersionFunc() != BF_CODEGEN_VERSION)
		{
			BF_FATAL("Invalid BF_CODEGEN_VERSION in IDEHelper release DLL. Please rebuild Release configuration.\n");
			return;
		}

		mIsUsingReleaseThunk = true;
	}
}

bool BfCodeGen::ExternWriteObjectFile(BfCodeGenRequest* codeGenRequest)
{
#ifndef BF_USE_CODEGEN_RELEASE_THUNK
	return false;
#endif
	
	BindReleaseThunks();

	if (!mIsUsingReleaseThunk)
		return false;	

	mGenerateObjFunc(codeGenRequest->mData.mVals, codeGenRequest->mData.mSize, codeGenRequest->mOutFileName.c_str(), &codeGenRequest->mResult, codeGenRequest->mOptions);

	return true;
}

void BfCodeGen::WriteObjectFile(BfModule* bfModule, const StringImpl& outFileName, const BfCodeGenOptions& options)
{		
	mQueuedCount++;
	
	BfLogSys(bfModule->mSystem, "WriteObjectFile %s\n", outFileName.c_str());

	BfCodeGenRequest* codeGenRequest = new BfCodeGenRequest();	
	mRequests.push_back(codeGenRequest);
	
	{			
		BP_ZONE("WriteObjectFile_GetBufferData");
		bfModule->mBfIRBuilder->GetBufferData(codeGenRequest->mOutBuffer);
	}

	auto rootModule = bfModule;
	while (rootModule->mParentModule != NULL)
		rootModule = rootModule->mParentModule;

	codeGenRequest->mSrcModule = rootModule;
	codeGenRequest->mOutFileName = outFileName;	
	codeGenRequest->mData = codeGenRequest->mOutBuffer;	
	codeGenRequest->mOptions = options;
	if (ExternWriteObjectFile(codeGenRequest))
		return;	

	DoWriteObjectFile(codeGenRequest, (void*)&codeGenRequest->mOutBuffer[0], (int)codeGenRequest->mOutBuffer.size(), codeGenRequest->mOutFileName, NULL);		

#ifdef DBG_FORCE_SYNCHRONIZED
	while (mRequests.size() != 0)
	{
		UpdateStats();
	}

	BfLogSys(bfModule->mSystem, "WriteObjectFile Done\n");
#endif
}

String BfCodeGen::GetBuildValue(const StringImpl& buildDir, const StringImpl& key)
{
	AutoCrit autoCrit(mCacheCritSect);
	BfCodeGenDirectoryData* dirCache = GetDirCache(buildDir);
	return dirCache->GetValue(key);
}

void BfCodeGen::SetBuildValue(const StringImpl& buildDir, const StringImpl & key, const StringImpl & value)
{
	AutoCrit autoCrit(mCacheCritSect);
	BfCodeGenDirectoryData* dirCache = GetDirCache(buildDir);
	dirCache->SetValue(key, value);
}

void BfCodeGen::WriteBuildCache(const StringImpl& buildDir)
{
	AutoCrit autoCrit(mCacheCritSect);
	BfCodeGenDirectoryData* dirCache = GetDirCache(buildDir);
	dirCache->Write();
}

void BfCodeGen::RequestComplete(BfCodeGenRequest* request)
{
	mCompletionCount++;
	if ((request->mResult.mType == BfCodeGenResult_Failed) || (request->mResult.mType == BfCodeGenResult_Aborted))
	{
		BfCodeGenErrorEntry errorEntry;
		errorEntry.mSrcModule = request->mSrcModule;		
		errorEntry.mOutFileName = request->mOutFileName;

		int errorPos = 0;
		while (errorPos < request->mResult.mErrorMsgBufLen)
		{
			char* errorStr = &request->mResult.mErrorMsgBuf[errorPos];
			errorEntry.mErrorMessages.push_back(errorStr);
			errorPos += (int)strlen(errorStr) + 1;
		}
		
		mFailedRequests.push_back(errorEntry);
	}
	else
	{
		BfCodeGenFileEntry entry;
		entry.mFileName = request->mOutFileName;
		entry.mModule = request->mSrcModule;
		entry.mProject = request->mSrcModule->mProject;
		entry.mWasCached = request->mResult.mType == BfCodeGenResult_DoneCached;
		entry.mModuleHotReferenced = false;
		mCodeGenFiles.push_back(entry);
	}
}

void BfCodeGen::ProcessErrors(BfPassInstance* passInstance, bool canceled)
{
	for (auto& errorEntry : mFailedRequests)
	{
		if (!errorEntry.mErrorMessages.IsEmpty())
		{
			for (auto const & errorMsg : errorEntry.mErrorMessages)
				passInstance->Fail(StrFormat("Module '%s' failed during codegen with: %s", errorEntry.mSrcModule->mModuleName.c_str(), errorMsg.c_str()));
		}
		else if (!canceled)
			passInstance->Fail(StrFormat("Failed to create %s", errorEntry.mOutFileName.c_str()));
		// mHadBuildError forces it to build again
		errorEntry.mSrcModule->mHadBuildError = true;
	}

	mFailedRequests.Clear();

	bool showedCacheError = false;
	for (auto& dirEntryPair : mDirectoryCache)
	{
		auto dirEntry = dirEntryPair.mValue;
		if (!dirEntry->mError.empty())
		{
			if (!showedCacheError)
			{
				passInstance->Fail(dirEntry->mError);
				showedCacheError = true;
			}			
			dirEntry->mError.clear();
		}
	}
}

BfCodeGenDirectoryData * BfCodeGen::GetDirCache(const StringImpl & cacheDir)
{
	BfCodeGenDirectoryData* dirCache = NULL;
	BfCodeGenDirectoryData** dirCachePtr = NULL;
	if (mDirectoryCache.TryAdd(cacheDir, NULL, &dirCachePtr))
	{
		dirCache = new BfCodeGenDirectoryData();
		*dirCachePtr = dirCache;
		dirCache->mCodeGen = this;
		dirCache->mDirectoryName = cacheDir;
		if (!mDisableCacheReads)
			dirCache->Read();
	}
	else
	{
		dirCache = *dirCachePtr;
	}
	return dirCache;
}

void BfCodeGen::Cancel()
{
	for (auto thread : mThreads)
	{
		thread->mShuttingDown = true;		
	}

	mRequestEvent.Set(true);

	if (mIsUsingReleaseThunk)
		mCancelFunc();
}

void BfCodeGen::ClearResults()
{
	mFailedRequests.Clear();
	mCodeGenFiles.Clear();
}

bool BfCodeGen::Finish()
{	
	BP_ZONE("BfCodeGen::Finish");
		
	while (mRequests.size() != 0)
	{
		auto request = mRequests[0];
		if (request->mResult.mType == BfCodeGenResult_NotDone)
		{
			bool hasRunningThreads = false;
			for (auto thread : mThreads)
			{
				if (thread->mRunning)
					hasRunningThreads = true;
			}
			if (!hasRunningThreads)
			{
				mPendingRequests.Clear();
				request->mResult.mType = BfCodeGenResult_Aborted;
				continue;
			}

			mDoneEvent.WaitFor(20);
			continue;
		}

		RequestComplete(request);
		delete request;
		mRequests.RemoveAt(0);
		return false;
	}

	if (mIsUsingReleaseThunk)	
	{
		// Make the thunk release its threads (and more important, its LLVM contexts)
		mFinishFunc();
	}

	// We need to shut down these threads to remove their memory
	for (auto thread : mThreads)
	{
		thread->mShuttingDown = true;		
		mOldThreads.push_back(thread);		
	}
	mThreads.Clear();
	mRequestEvent.Set(true);

	ClearOldThreads(false);
		
// 	for (auto request : mPendingRequests)
// 	{
// 		if (request->mExternResultPtr != NULL)
// 		{
// 			request->mExternResultPtr->mType = BfCodeGenResult_Aborted;
// 			//delete request;
// 		}
// 		//request->mResult.mType = BfCodeGenResult_Aborted;
// 		//delete request;
// 	}	
// 	mPendingRequests.Clear();

	///

	for (auto& cachePair : mDirectoryCache)
	{
		auto cacheDir = cachePair.mValue;
		cacheDir->Write();
		cacheDir->mVerified = false;
	}

	mRequestEvent.Reset();
	mRequestIdx = 0;
	return true;
}

//////////////////////////////////////////////////////////////////////////

static BfCodeGen* gExternCodeGen = NULL;

BF_EXPORT void BF_CALLTYPE Targets_Create();
BF_EXPORT void BF_CALLTYPE Targets_Delete();

static void GetExternCodeGen()
{
	if (gExternCodeGen == NULL)
	{
		gExternCodeGen = new BfCodeGen();
		Targets_Create();
	}
}

BF_EXPORT int BF_CALLTYPE BfCodeGen_GetVersion()
{
	return BF_CODEGEN_VERSION;
}

BF_EXPORT void BF_CALLTYPE BfCodeGen_ClearCache()
{
	GetExternCodeGen();	
	gExternCodeGen->ClearBuildCache();	
}

BF_EXPORT void BF_CALLTYPE BfCodeGen_Finish()
{
	if (gExternCodeGen != NULL)
	{
		gExternCodeGen->Finish();
		gExternCodeGen->ClearResults();
	}
}

BF_EXPORT void BF_CALLTYPE BfCodeGen_Kill()
{
	delete gExternCodeGen;
	gExternCodeGen = NULL;	
	Targets_Delete();
}

BF_EXPORT void BF_CALLTYPE BfCodeGen_Cancel()
{
	if (gExternCodeGen != NULL)
		gExternCodeGen->Cancel();
}

BF_EXPORT void BF_CALLTYPE BfCodeGen_GenerateObj(const void* ptr, int size, const char* outFileName, BfCodeGenResult* resultPtr, const BfCodeGenOptions& options)
{	
	GetExternCodeGen();

	BfCodeGenRequest* codeGenRequest = new BfCodeGenRequest();	
	codeGenRequest->mOptions = options;
	gExternCodeGen->DoWriteObjectFile(codeGenRequest, ptr, size, outFileName, resultPtr);
}

