#include "CDepChecker.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "ClangHelper.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

static CDepChecker* gCDepChecker = NULL;

static PerfManager gCDepPerfManager;
static PerfManager* gPerfManagerPtr = NULL;

CDepFile::~CDepFile()
{
	delete mParser;		
}

void CppParser::HandlePragma(const StringImpl& pragma, BfBlock* block)
{
	
}

void CppParser::HandleDefine(const StringImpl& defineString, BfAstNode* paramNode)
{	
	
}

void CppParser::HandleUndefine(const StringImpl& name)
{
	
}

MaybeBool CppParser::HandleIfDef(const StringImpl& name)
{
	return MaybeBool_None;
}

MaybeBool CppParser::HandleProcessorCondition(BfBlock* paramNode)
{
	return MaybeBool_None;
}

void CppParser::HandleInclude(BfAstNode* paramNode)
{
	IncludeHelper(paramNode, false, false);
}

void CppParser::HandleIncludeNext(BfAstNode* paramNode)
{
	IncludeHelper(paramNode, true, false);
}

bool CppParser::IncludeHelper(BfAstNode* paramNode, bool includeNext, bool scanOnly)
{
	String name;
	bool isLocalInclude = false;

	auto checkNode = paramNode;
	if (auto identifier = BfNodeDynCast<BfIdentifierNode>(checkNode))
	{		
		//EvaluatePreprocessor(identifier, NULL, &checkNode);		
		return false;
	}

	if (auto token = BfNodeDynCast<BfTokenNode>(checkNode))
	{
		//TODO: DO THIS
		/*if (token->mToken == BfToken_LChevron)
		{			
			auto curNode = checkNode->mNext;
			while (curNode != NULL)
			{
				if ((token = BfNodeDynCast<BfTokenNode>(curNode)))
				{
					if (token->mToken == BfToken_RChevron)
						break;
				}

				name += curNode->ToString();
				curNode = curNode->mNext;
			}
		}*/
	}
	else if (auto literalExpr = BfNodeDynCast<BfLiteralExpression>(checkNode))
	{
		if (literalExpr->mValue.mTypeCode == BfTypeCode_CharPtr)
		{
			name = String(literalExpr->GetSourceData()->mSrc + literalExpr->GetSrcStart() + 1, literalExpr->GetSourceData()->mSrc + literalExpr->GetSrcEnd() - 1);
			isLocalInclude = true;
		}
	}	

	if (name.empty())
		return false;

	for (int i = 0; i < (int)name.length(); i++)
		if (name[i] == DIR_SEP_CHAR_ALT)
			name[i] = DIR_SEP_CHAR;

	String curDir = GetFileDir(mFileName);

	bool found = false;
	bool foundCurPath = false;
	for (int i = isLocalInclude ? -1 : 0; i < (int)mCDepChecker->mIncludeDirs.size(); i++)
	{
		String checkPath;
		if (i == -1)		
			checkPath = curDir;		
		else
			checkPath = mCDepChecker->mIncludeDirs[i];		

		if (includeNext)
		{
			if (checkPath == curDir)
			{
				foundCurPath = true;
				continue;
			}
			if (!foundCurPath)
				continue;
		}
		
		checkPath = GetAbsPath(name, checkPath);

#ifdef _WIN32
		checkPath = ToUpper(checkPath);
#endif

		if (mCDepChecker->CachedFileExists(checkPath))
		{	
			if (scanOnly)
				return true;			

			bool isFromCache = false;
			found = true;			
			CDepFile* cDepFile = mCDepChecker->LoadFile(checkPath, mCDepFile, &isFromCache);
			if (cDepFile != NULL)
			{
				for (auto refFileName : cDepFile->mFilesReferenced)
					mCDepFile->mFilesReferenced.insert(refFileName);				
			}
			
			return true;
		}
	}
	//BF_ASSERT(found);
	return false;
}

//////////////////////////////////////////////////////////////////////////

CDepChecker::CDepChecker()
{
	mAbort = false;
	mCPPCount = 0;
	mFindHeaderIdx = -1;
}

void CDepChecker::SetCArgs(const char* cArgs)
{
	int strStart = 0;
	for (int i = 0; cArgs[i] != (char)0; i++)
	{
		if (cArgs[i] == '\n')
		{
			String arg = String(cArgs + strStart, cArgs + i);
			if (strncmp(arg.c_str(), "-I", 2) == 0)
			{
				String includeDir = arg.Substring(2);
				includeDir = EnsureEndsInSlash(includeDir);
#ifdef _WIN32				
				includeDir = ToUpper(includeDir);
#endif
				mIncludeDirs.push_back(includeDir);
			}
			else if (strncmp(arg.c_str(), "-D", 2) == 0)
			{

			}
			strStart = i + 1;
		}
	}
}

/*String CDepChecker::DetermineFilesReferenced(const StringImpl& fileName)
{
	LoadFile(fileName);
	return "";
}*/

CDepFile* CDepChecker::LoadFile(const StringImpl& inFileName, CDepFile* fileFrom, bool* isFromCache, const char* contentOverride)
{	
	*isFromCache = false;

	if (mAbort)
		return NULL;
	
	AutoPerf autoPerf("LoadFile", gPerfManagerPtr);

#ifdef _WIN32
	String fileName = ToUpper(inFileName);
#else
	String fileName = inFileName;
#endif

	if (mFindHeaderFileName.length() > 0)
	{
		if (fileFrom != NULL)
		{
			if ((mFindHeaderIdx == -1) && (fileName == mFindHeaderFileName))
				mFindHeaderIdx = fileFrom->mParser->mLineStart;
			return NULL;
		}
	}


	//Val128 val128 = cDepPreprocState->mBaseHash;
	//HASH128_MIXIN_STR(val128, fileName);

	
	CDepFile* dataCDep = NULL;

	auto depFileItr = mDepFileMap.find(fileName);
	if (depFileItr != mDepFileMap.end())
	{		
		auto checkCDepFile = depFileItr->second;
		depFileItr++;

		dataCDep = checkCDepFile;

		if (checkCDepFile->mProcessing)
		{
			BF_ASSERT(fileFrom != NULL);
			// Propagate include files down the line once we finish processing cDepFile
			checkCDepFile->mDeferredDepSet.insert(fileFrom);
			for (auto childDep : fileFrom->mDeferredDepSet)
				checkCDepFile->mDeferredDepSet.insert(childDep);
		}
		
		*isFromCache = true;
		return checkCDepFile;
	}
	
	CDepFile* cDepFile = new CDepFile();	
	cDepFile->mFilePath = fileName;
	cDepFile->mFilesReferenced.insert(fileName);
	cDepFile->mProcessing = true;
		
	bool fileAlreadyVisited = mFilesVisited.find(fileName) != mFilesVisited.end();
	if (!fileAlreadyVisited)
		mFilesVisited.insert(fileName);		

	mDepFileMap.insert(CDepFileMap::value_type(fileName, cDepFile));

	BfPassInstance bfPassInstance(NULL);

	CppParser* newParser = new CppParser();		
	cDepFile->mParser = newParser;

	//int64 fileTime = GetFileTimeWrite(fileName);
	//cDepFile->mFileTime = fileTime;

	int fileSize = 0;	
		
	newParser->mAstAllocManager = &mAstAllocManager;
	newParser->mFileName = fileName;

	newParser->mFileAlreadyVisited = fileAlreadyVisited;
	newParser->mAborted = false;	
	newParser->mCDepChecker = this;
	newParser->mCDepFile = cDepFile;

	if (dataCDep != NULL)
	{
		//OutputDebugStrF("CDep Reffing file: %s\n", fileName.c_str());
		newParser->RefSource(dataCDep->mFileData.mData, dataCDep->mFileData.mLength);
		cDepFile->mFileData = dataCDep->mFileData;
	}
	else
	{
		AutoPerf autoPerf("LoadFile LoadTextData", gPerfManagerPtr);

		CDepFileData fileData;		
		fileData.mLength = 0;
		
#ifdef IDE_C_SUPPORT
		if ((contentOverride == NULL) && (gClangUnsavedFiles != NULL))
		{
			AutoCrit autoCrit(gClangUnsavedFiles->mCritSect);
			
			for (auto& unsavedFile : gClangUnsavedFiles->mUnsavedFiles)
			{				
				if (_stricmp(unsavedFile.Filename, fileName.c_str()) == 0)
				{
					fileData.mData = new char[unsavedFile.Length + 1];
					fileData.mData[unsavedFile.Length] = 0;
					memcpy(fileData.mData, unsavedFile.Contents, unsavedFile.Length);
					fileData.mLength = unsavedFile.Length;
					break;
				}
			}
		}
#endif

		if (contentOverride != NULL)
		{
			newParser->RefSource(contentOverride, (int)strlen(contentOverride));
		}
		else
		{
			if (fileData.mData == NULL)
			{
				fileData.mData = Beefy::LoadTextData(fileName, &fileData.mLength);
				//BF_ASSERT(fileData.mData != NULL);
			}
			if (fileData.mData != NULL)
				newParser->RefSource(fileData.mData, fileData.mLength);
		}
		//OutputDebugStrF("CDep Loading file: %s\n", fileName.c_str());
		
		
		cDepFile->mFileData = fileData;		

		if (fileData.mData != NULL)
			mDepFileData.push_back(fileData);
	}

	int maxIncludeDepth = 256;
#ifdef _DEBUG
	maxIncludeDepth = 64;
#endif

	if ((int)gCDepChecker->mIncludeStack.size() >= maxIncludeDepth) // Include stack blown?
		mAbort = true;

	gCDepChecker->mIncludeStack.push_back(cDepFile);
	{
		AutoPerf autoPerf("LoadFile Parse", gPerfManagerPtr);
		if (newParser->mSrc != NULL)
			newParser->Parse(&bfPassInstance);
		newParser->Close();
	}
	gCDepChecker->mIncludeStack.pop_back();

	//bool wasAborted = newParser->mAborted;
	bool wasAborted = false;
	//cDepPreprocState->mParsers.push_back(newParser);

	cDepFile->mProcessing = false;

	// We need to remove this because it wasn't really processed and won't have mFilesReferenced and such filled in
	if (wasAborted)
	{
		delete cDepFile;
		//auto itr = mDepFileMap.find(val128);
		mDepFileMap.erase(depFileItr);
		return NULL;
	}
	else
	{		
		for (auto deferredFile : cDepFile->mDeferredDepSet)
		{
			for (auto refFile : cDepFile->mFilesReferenced)
				deferredFile->mFilesReferenced.insert(refFile);
		}
	}

	return cDepFile;
}

void CDepChecker::ClearCache()
{
	mCPPCount = 0;
	for (auto pair : mDepFileMap)
	{
		delete pair.second;
	}
	mDepFileMap.clear();

	for (auto& depFileData : mDepFileData)
	{
		delete depFileData.mData;
	}
	mDepFileData.clear();

	mFileExistsCache.clear();
}

bool CDepChecker::CachedFileExists(const StringImpl& filePath)
{
	AutoPerf autoPerf("CachedFileExists", gPerfManagerPtr);

	auto itr = mFileExistsCache.find(filePath);
	if (itr != mFileExistsCache.end())
		return itr->second;
	auto fileExists = FileExists(filePath);
	mFileExistsCache[filePath] = fileExists;
	return fileExists;
}

//////////////////////////////////////////////////////////////////////////

/// Get the value the ATOMIC_*_LOCK_FREE macro should have for a type with
/// the specified properties.
static int GetLockFreeValue(int typeWidth, int typeAlign, int inlineWidth) 
{
	// Fully-aligned, power-of-2 sizes no larger than the inline
	// width will be inlined as lock-free operations.
	if (typeWidth == typeAlign && (typeWidth & (typeWidth - 1)) == 0 &&
		typeWidth <= inlineWidth)
		return 2; // "always lock free"
					// We cannot be certain what operations the lib calls might be
					// able to implement as lock-free on future processors.
	return 1; // "sometimes lock free"
}

static void FixFilePath(String& fileName)
{
	for (int i = 0; i < (int)fileName.length(); i++)
		if (fileName[i] == DIR_SEP_CHAR_ALT)
			fileName[i] = DIR_SEP_CHAR;
#ifdef _WIN32
		else
			fileName[i] = (char)::toupper((uint8)fileName[i]);
#endif
}

static int gCheckCount = 0;

BF_EXPORT const char* BF_CALLTYPE CDep_DetermineFilesReferenced(const char* fileNamePtr, const char* cArgs)
{
	String& outString = *gTLStrReturn.Get();

	gCheckCount++;

	if (gCDepChecker == NULL)
		gCDepChecker = new CDepChecker();

	gCDepChecker->mCPPCount++;

	gPerfManagerPtr = &gCDepPerfManager;
	gCDepChecker->SetCArgs(cArgs);

	//gPerfManagerPtr->StartRecording();

	String fileName = fileNamePtr;
	FixFilePath(fileName);

	bool isFromCache;
	CDepFile* depFile = gCDepChecker->LoadFile(fileName, NULL, &isFromCache);
	BF_ASSERT(gCDepChecker->mIncludeStack.size() == 0);

	outString.clear();
	for (auto fileRef : depFile->mFilesReferenced)
	{
		if (!outString.empty())
			outString += "\n";
		outString += fileRef;
	}
	//OutputDebugStrF("%s Files Referenced: %s\n", fileNamePtr, outString.c_str());

	/*if ((_stricmp(fileNamePtr, "C:/Beef/IDE/mintest/cpp/test1.cpp") == 0) || (stricmp(fileNamePtr, "C:/Beef/IDE/mintest/cpp/test2.cpp") == 0))
	{		
		//TODO: Temporary for testing
		BF_ASSERT(depFile->mFilesReferenced.size() > 1);
	}*/
	
	if (gPerfManagerPtr->IsRecording())
	{
		gPerfManagerPtr->StopRecording();
		gPerfManagerPtr->DbgPrint();
	}

	gCDepChecker->mAbort = false;
	gCDepChecker->mIncludeDirs.clear();
	gCDepChecker->mFilesVisited.clear();

	return outString.c_str();
}

BF_EXPORT int BF_CALLTYPE CDep_GetIncludePosition(const char* sourceFileName, const char* sourceContent, const char* headerFileNamePtr, const char* cArgs)
{
	if (gCDepChecker == NULL)
		gCDepChecker = new CDepChecker();

	gPerfManagerPtr = &gCDepPerfManager;	
	gCDepChecker->SetCArgs(cArgs);
	
	String fileName = sourceFileName;
	FixFilePath(fileName);
	String headerFileName = headerFileNamePtr;
	FixFilePath(headerFileName);

	gCDepChecker->mFindHeaderFileName = headerFileName;
	gCDepChecker->mFindHeaderIdx = -1;

	bool isFromCache;
	CDepFile* depFile = gCDepChecker->LoadFile(fileName, NULL, &isFromCache, sourceContent);
	BF_ASSERT(gCDepChecker->mIncludeStack.size() == 0);
	
	gCDepChecker->mAbort = false;
	gCDepChecker->mIncludeDirs.clear();
	gCDepChecker->mFilesVisited.clear();
	gCDepChecker->mFindHeaderFileName.clear();	
	
	gCDepChecker->ClearCache();

	return gCDepChecker->mFindHeaderIdx;
}

void BfFullReportMemory();

BF_EXPORT void BF_CALLTYPE CDep_ClearCache()
{
	if (gCDepChecker != NULL)
		gCDepChecker->ClearCache();
}

BF_EXPORT void BF_CALLTYPE CDep_Shutdown()
{	
	delete gCDepChecker;
	gCDepChecker = NULL;
}

/*BF_EXPORT const char* BF_CALLTYPE CDep_DetermineFilesReferenced(const char* fileNamePtr, const char* cArgs)
{
	for (int i = 0; i < 20; i++)
	{		
		zCDep_DetermineFilesReferenced(fileNamePtr, cArgs);
		CDep_ClearCache();
	}

	return zCDep_DetermineFilesReferenced(fileNamePtr, cArgs);
}
*/