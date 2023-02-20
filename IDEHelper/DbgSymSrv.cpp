#include "DbgSymSrv.h"
#include "DbgSymSrv.h"
#include "BeefySysLib/MemStream.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "BeefySysLib/util/CabUtil.h"
#include "NetManager.h"
#include "Compiler/BfUtil.h"
#include "DebugManager.h"
#include <shlobj.h>
#include "COFF.h"

#include "BeefySysLib/util/AllocDebug.h"

//  #define BF_DBG_64
// #include "DebugCommon.h"
//  #include "COFF.h"
//  #undef BF_DBG_64

USING_NS_BF_DBG;

DbgSymRequest::DbgSymRequest()
{
	mFailed = false;
	mSearchingSymSrv = false;
	mCancelling = false;
	mInProcess = false;
	mLastUpdateTick = 0;
	mDbgSymSrv = NULL;
	mWantAge = 0;
	mMayBeOld = false;
	mIsPreCache = false;
}

DbgSymRequest::~DbgSymRequest()
{
}

String DbgSymRequest::GetGuidString()
{
	String str;

	// Seems like weird ordering, but the guid is really supposed to be (uint32, uint16, uint16, uint8[8])
	str += StrFormat("%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X",
		mWantGuid[3], mWantGuid[2], mWantGuid[1], mWantGuid[0],
		mWantGuid[5], mWantGuid[4],
		mWantGuid[7], mWantGuid[6],
		mWantGuid[8], mWantGuid[9], mWantGuid[10], mWantGuid[11], mWantGuid[12], mWantGuid[13], mWantGuid[14], mWantGuid[15]);
	return str;
}

String DbgSymRequest::GetPDBStoreDir()
{
	String checkPath = "/";
	checkPath += GetFileName(mPDBRequested);
	checkPath += "/";
	checkPath += GetGuidString();
	checkPath += StrFormat("%x/", mWantAge);
	return checkPath;
}

void DbgSymRequest::Fail(const StringImpl& error)
{
	mFailed = true;
	if (mError.IsEmpty())
		mError = error;
}

bool DbgSymRequest::CheckPDBData(const StringImpl& pdbPath, uint8 outGuid[16], int32& outAge)
{
	BP_ZONE("DbgSymRequest::CheckPDBData");

	if (!FileExists(pdbPath))
		return false;

	mDbgSymSrv->mDebugger->OutputRawMessage(StrFormat("symsrv Checking '%s'", pdbPath.c_str()));

	COFF coff(NULL);
	coff.mParseKind = COFF::ParseKind_Header;
	bool result = coff.TryLoadPDB(pdbPath, mWantGuid, mWantAge);
	memcpy(outGuid, coff.mPDBGuid, 16);
	outAge = coff.mDebugAge;

	if (!result)
	{
		if (outAge != -1)
		{
			mDbgSymSrv->mDebugger->OutputMessage(StrFormat("ERROR: %s cannot be used due to age mismatch\n", pdbPath.c_str()));
		}
		else
		{
			bool hasGuid = false;
			for (int i = 0; i < 16; i++)
				if (outGuid[i] != 0)
					hasGuid = true;
			if (hasGuid)
			{
				mDbgSymSrv->mDebugger->OutputMessage(StrFormat("ERROR: %s cannot be used due to GUID mismatch\n", pdbPath.c_str()));
			}
			else
			{
				mDbgSymSrv->mDebugger->OutputMessage(StrFormat("ERROR: %s failed to load\n", pdbPath.c_str()));
			}
		}
	}

	return result;
}

bool DbgSymRequest::Get(const StringImpl& url, const StringImpl& destPath, NetResult** chainNetResult, bool ignoreSuccess)
{
	if (mIsPreCache)
	{
		auto netResult = mDbgSymSrv->mDebugger->mDebugManager->mNetManager->QueueGet(url, destPath, true);
		if (chainNetResult != NULL)
		{
			if ((*chainNetResult != NULL) && (netResult != NULL))
				mDbgSymSrv->mDebugger->mDebugManager->mNetManager->SetCancelOnSuccess(*chainNetResult, netResult);
			if (!ignoreSuccess)
				*chainNetResult = netResult;
		}
		return false;
	}

	return mDbgSymSrv->mDebugger->mDebugManager->mNetManager->Get(url, destPath);
}

void DbgSymRequest::SearchLocal()
{
	if ((gDebugManager->mSymSrvOptions.mFlags & BfSymSrvFlag_Disable) != 0)
	{
		mFailed = true;
		return;
	}

	uint8 outGuid[16];
	int32 outAge;
	if (mPDBRequested.IndexOf('\\') != -1) // Do we have an absolute path at all? System dlls won't.
	{
		// Check actual path
		if (CheckPDBData(mPDBRequested, outGuid, outAge))
		{
			mFinalPDBPath = mPDBRequested;
			return;
		}

		// Check in same dir as module
		String checkPath = ::GetFileDir(mModulePath);
		checkPath += "\\";
		checkPath += GetFileName(mPDBRequested);
		if (!FileNameEquals(checkPath, mPDBRequested))
		{
			if (CheckPDBData(checkPath, outGuid, outAge))
			{
				mFinalPDBPath = checkPath;
				return;
			}
		}
	}
	else
	{
		String checkPath = ::GetFileDir(mModulePath);
		checkPath += "\\";
		checkPath += mPDBRequested;

		if (CheckPDBData(checkPath, outGuid, outAge))
		{
			mFinalPDBPath = checkPath;
			return;
		}
	}

	mMayBeOld = true;
}

void DbgSymRequest::SearchCache()
{
	if ((gDebugManager->mSymSrvOptions.mFlags & BfSymSrvFlag_Disable) != 0)
	{
		mFailed = true;
		return;
	}

	if (mOptions.mCacheDir.IsEmpty())
	{
		mFailed = true;
		return;
	}

	uint8 outGuid[16];
	int32 outAge;

	String cacheDir = mOptions.mCacheDir;
	cacheDir += GetPDBStoreDir();

	String cacheFilePath = cacheDir + "/" + GetFileName(mPDBRequested);
	if (CheckPDBData(cacheFilePath, outGuid, outAge))
	{
		mFinalPDBPath = cacheFilePath;
		return;
	}
}

void DbgSymRequest::SearchSymSrv()
{
	if ((gDebugManager->mSymSrvOptions.mFlags & BfSymSrvFlag_Disable) != 0)
	{
		mFailed = true;
		return;
	}

	if (mOptions.mCacheDir.IsEmpty())
	{
		mFailed = true;
		return;
	}

	/*if (mPDBRequested.Contains("IDEHelper"))
		Sleep(3000);*/

	SetAndRestoreValue<bool> prevSearchingSymSrv(mSearchingSymSrv, true);

	NetResult* chainNetResult = NULL;
	uint8 outGuid[16];
	int32 outAge;

	String cacheDir = mOptions.mCacheDir;
	cacheDir += GetPDBStoreDir();

	//TODO: Remove!
// 	for (int i = 0; i < 8; i++)
// 	{
// 		mDbgSymSrv->mDebugger->OutputMessage(StrFormat("Sleep %d\n", i));
// 		if (mCancelling)
// 			break;
// 		Sleep(1000);
// 	}

	String cacheFilePath = cacheDir + "/" + GetFileName(mPDBRequested);

	for (auto& symServAddr : mOptions.mSymbolServers)
	{
		if (mCancelling)
			break;

		int colonPos = symServAddr.IndexOf(':');
		if (colonPos > 1)
		{
			// HTTP
			bool done = false;

			//TODO: Check for 'index2.txt' for two-tiered structure

			String checkPath = symServAddr;
			checkPath += GetPDBStoreDir();
			checkPath += GetFileName(mPDBRequested);
			if (Get(checkPath, cacheFilePath, &chainNetResult))
				done = true;

			if ((!done) && (!mCancelling))
			{
				// Compressed version
				checkPath[checkPath.length() - 1] = '_';

				String compCacheFilePath = cacheFilePath;
				compCacheFilePath[compCacheFilePath.length() - 1] = '_';
				if (Get(checkPath, compCacheFilePath, &chainNetResult))
				{
					CabFile cabFile;
					cabFile.Load(compCacheFilePath);
					mDbgSymSrv->mDebugger->OutputRawMessage(StrFormat("symsrv Decompressing '%s'", compCacheFilePath.c_str()));
					if (!cabFile.DecompressAll(cacheDir))
					{
						Fail(cabFile.mError);
						return;
					}
					done = true;
				}
			}

			if ((!done) && (!mCancelling))
			{
				String checkPath = symServAddr;
				checkPath += GetPDBStoreDir();
				checkPath += "/file.dir";

				String fileDirFilePath = symServAddr;
				fileDirFilePath += GetPDBStoreDir();
				fileDirFilePath += "/file.dir";

				if (Get(checkPath, fileDirFilePath, &chainNetResult, true))
				{
					// What do I do with this?
				}
			}
		}
		else if (!mIsPreCache)
		{
			// Uncompressed version
			String checkPath = symServAddr;
			checkPath += GetPDBStoreDir();
			checkPath += GetFileName(mPDBRequested);
			if (CheckPDBData(checkPath, outGuid, outAge))
			{
				mFinalPDBPath = checkPath;
				return;
			}

			// Compressed version
			checkPath[checkPath.length() - 1] = '_';
			if (FileExists(checkPath))
			{
				CabFile cabFile;
				cabFile.Load(checkPath);
				mDbgSymSrv->mDebugger->OutputRawMessage(StrFormat("symsrv Decompressing '%s'", checkPath.c_str()));
				if (!cabFile.DecompressAll(cacheDir))
				{
					Fail(cabFile.mError);
					return;
				}
			}
		}

		if (mIsPreCache)
			continue;

		// Re-check cache
		if (CheckPDBData(cacheFilePath, outGuid, outAge))
		{
			mFinalPDBPath = cacheFilePath;
			return;
		}
	}

	mFailed = true;
}

bool DbgSymRequest::CheckPEFile(const StringImpl& filePath, uint32 fileTime, int size)
{
	FileStream fs;

	if (!fs.Open(filePath, "rb"))
		return false;

	PEHeader hdr;
	fs.ReadT(hdr);

	PE_NTHeaders64 ntHdr64;
	fs.SetPos(hdr.e_lfanew);
	fs.Read(&ntHdr64, sizeof(PE_NTHeaders64));
	if (ntHdr64.mFileHeader.mMachine == PE_MACHINE_X64)
	{
		if ((ntHdr64.mFileHeader.mTimeDateStamp != fileTime) || (ntHdr64.mOptionalHeader.mSizeOfImage != size))
			return false;
	}
	else
	{
		PE_NTHeaders32 ntHdr32;
		fs.SetPos(hdr.e_lfanew);
		fs.Read(&ntHdr32, sizeof(PE_NTHeaders32));
		if ((ntHdr32.mFileHeader.mTimeDateStamp != fileTime) || (ntHdr32.mOptionalHeader.mSizeOfImage != size))
			return false;
	}

	return true;
}

String DbgSymRequest::SearchForImage(const String& filePath, uint32 fileTime, int size)
{
	if ((gDebugManager->mSymSrvOptions.mFlags & BfSymSrvFlag_Disable) != 0)
		return "";

	if (FileExists(filePath))
	{
		if (CheckPEFile(filePath, fileTime, size))
			return filePath;
	}

	/*if (filePath.Contains("IDEHelper"))
		Sleep(3000);*/

	if (mOptions.mCacheDir.IsEmpty())
	{
		mFailed = true;
		return "";
	}

	NetResult* chainNetResult = NULL;

	String fileName = GetFileName(filePath);
	String imageStoreDir = "/";
	imageStoreDir += fileName;
	imageStoreDir += "/";
	imageStoreDir += StrFormat("%08X%x/", fileTime, size);

	SetAndRestoreValue<bool> prevSearchingSymSrv(mSearchingSymSrv, true);

	String cacheDir = mOptions.mCacheDir;
	cacheDir += imageStoreDir;

	/*for (int i = 0; i < 8; i++)
	{
		mDbgSymSrv->mDebugger->OutputMessage(StrFormat("Image Sleep %d\n", i));
		if (mCancelling)
			break;
		Sleep(1000);
	}*/

	String cacheFilePath = cacheDir + "/" + GetFileName(filePath);

	// Check cache
	if (CheckPEFile(cacheFilePath, fileTime, size))
	{
		return cacheFilePath;
	}

	for (auto& symServAddr : mOptions.mSymbolServers)
	{
		if (mCancelling)
			break;

		int colonPos = symServAddr.IndexOf(':');
		if (colonPos > 1)
		{
			// HTTP
			bool done = false;

			//TODO: Check for 'index2.txt' for two-tiered structure

			String checkPath = symServAddr;
			checkPath += imageStoreDir;
			checkPath += fileName;
			if (Get(checkPath, cacheFilePath, &chainNetResult))
				done = true;

			if ((!done) && (!mCancelling))
			{
				// Compressed version
				checkPath[checkPath.length() - 1] = '_';

				String compCacheFilePath = cacheFilePath;
				compCacheFilePath[compCacheFilePath.length() - 1] = '_';
				if (Get(checkPath, compCacheFilePath, &chainNetResult))
				{
					mDbgSymSrv->mDebugger->mDebugManager->mOutMessages.push_back(StrFormat("symsrv Decompressing '%s'", checkPath.c_str()));

					CabFile cabFile;
					cabFile.Load(compCacheFilePath);
					if (!cabFile.DecompressAll(cacheDir))
					{
						Fail(cabFile.mError);
						return "";
					}
					done = true;
				}
			}

			if ((!done) && (!mCancelling))
			{
				String checkPath = symServAddr;
				checkPath += imageStoreDir;
				checkPath += "/file.dir";

				String fileDirFilePath = symServAddr;
				fileDirFilePath += imageStoreDir;
				fileDirFilePath += "/file.dir";

				if (Get(checkPath, fileDirFilePath, &chainNetResult, true))
				{
					// What do I do with this?
				}
			}
		}
		else if (!mIsPreCache)
		{
			// Uncompressed version
			String checkPath = symServAddr;
			checkPath += imageStoreDir;
			checkPath += fileName;
			if (CheckPEFile(checkPath, fileTime, size))
			{
				return checkPath;
			}

			// Compressed version
			checkPath[checkPath.length() - 1] = '_';
			if (FileExists(checkPath))
			{
				CabFile cabFile;
				cabFile.Load(checkPath);
				if (!cabFile.DecompressAll(cacheDir))
				{
					Fail(cabFile.mError);
					return "";
				}
			}
		}

		if (mIsPreCache)
			continue;

		// Re-check cache
		if (CheckPEFile(cacheFilePath, fileTime, size))
		{
			return cacheFilePath;
		}
	}

	if (!mIsPreCache)
	{
		mDbgSymSrv->mDebugger->mHadImageFindError = true;
		mDbgSymSrv->mDebugger->OutputMessage(StrFormat("ERROR: Unable to locate image '%s' (%08X%x). If this file is located on a symbol server, configure the symbol server location in File\\Preferences\\Settings under Debugger\\Symbol File Locations.\n",
			GetFileName(filePath).c_str(), fileTime, size));
	}

	mFailed = true;
	return "";
}

void DbgSymRequest::Cancel()
{
	mCancelling = true;
	mDbgSymSrv->mDebugger->mDebugManager->mNetManager->CancelCurrent();
}

bool DbgSymRequest::IsDone()
{
	AutoCrit autoCrit(mCritSect);

	if (!mFinalPDBPath.IsEmpty())
		return true;

	if (mFailed)
		return true;

	return false;
}

DbgSymSrv::DbgSymSrv(Debugger* debugger)
{
	mDebugger = debugger;
}

void DbgSymSrv::PreCacheImage(const String& filePath, uint32 fileTime, int size)
{
	auto request = CreateRequest();
	request->mIsPreCache = true;
	request->SearchForImage(filePath, fileTime, size);
	delete request;
}

DbgSymRequest* DbgSymSrv::CreateRequest(const StringImpl& modulePath, const StringImpl& pdbRequested, uint8 wantGuid[16], int32 wantAge)
{
	AutoCrit autoCrit(mDebugger->mDebugManager->mCritSect);

	DbgSymRequest* symRequest = new DbgSymRequest();
	symRequest->mOptions = mDebugger->mDebugManager->mSymSrvOptions;
	symRequest->mDbgSymSrv = this;
	symRequest->mModulePath = modulePath;
	symRequest->mPDBRequested = pdbRequested;
	memcpy(symRequest->mWantGuid, wantGuid, 16);
	symRequest->mWantAge = wantAge;

	return symRequest;
}

DbgSymRequest* DbgSymSrv::CreateRequest()
{
	AutoCrit autoCrit(mDebugger->mDebugManager->mCritSect);

	DbgSymRequest* symRequest = new DbgSymRequest();
	symRequest->mOptions = mDebugger->mDebugManager->mSymSrvOptions;
	symRequest->mDbgSymSrv = this;
	return symRequest;
}

void DbgSymSrv::ReleaseRequest(DbgSymRequest* dbgSymRequest)
{
	delete dbgSymRequest;
}

void DbgSymSrv::Update()
{
}