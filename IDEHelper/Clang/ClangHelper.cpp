#ifdef IDE_C_SUPPORT

#include "ClangHelper.h"

USING_NS_BF;

ClangUnsavedFiles* Beefy::gClangUnsavedFiles = NULL;

static PerfManager gClangPerfManager;

static String ToString(CXString str)
{
	String result;
	const char* cStr = clang_getCString(str);
	if (cStr != NULL) 
		result = cStr;
	clang_disposeString(str);    
    
	return result;
}

static void ToString(CXString str, String& outStr)
{	
	const char* cStr = clang_getCString(str);
	if (cStr != NULL)
		outStr = cStr;
	else
		outStr.clear();
	clang_disposeString(str);	
}

class ClangClassifier
{
public:
	BfSourceClassifier::CharData* mCharData;
	uint8 mClassifierPassId;

public:
	void ModifyFlags(int startPos, int endPos, uint8 andFlags, uint8 orFlags)
	{		
		for (int i = startPos; i < endPos; i++)
		{
			mCharData[i].mDisplayPassId = mClassifierPassId;
			mCharData[i].mDisplayFlags = (mCharData[i].mDisplayFlags & andFlags) | orFlags;
		}
	}

	void SetElementType(int startPos, int endPos, BfSourceElementType elementType)
	{		
		for (int i = startPos; i < endPos; i++)
		{
			mCharData[i].mDisplayPassId = mClassifierPassId;
			mCharData[i].mDisplayTypeId = (uint8) elementType;
		}
	}
};

static CXFile GetLocation(CXSourceLocation loc, int* idx)
{
	CXFile file;
	uint32 line;
	uint32 column;
	clang_getFileLocation(loc, &file, &line, &column, (uint32*)idx);
	return file;
}

static CXFile GetRange(CXSourceRange range, int* startIdx, int* endIdx)
{
	auto loc = clang_getRangeStart(range);
	CXFile file;
	uint32 line;
	uint32 column;
	clang_getFileLocation(loc, &file, &line, &column, (uint32*) startIdx);
	loc = clang_getRangeEnd(range);
	clang_getFileLocation(loc, &file, &line, &column, (uint32*) endIdx);
	return file;
}

static CXFile GetCursorRange(CXCursor cursor, int* startIdx, int* endIdx)
{
	auto range = clang_getCursorExtent(cursor);
	return GetRange(range, startIdx, endIdx);
}

class ClangFindCall
{
public:
	int mCursorIndex;
	std::vector<int> mParamStarts;	
	CXCursor mCallDef;
	bool mFoundCall;
	int mSelectIdx;
	int mLeftParenIdx;

public:	
	ClangFindCall()
	{
		mFoundCall = false;
		mSelectIdx = 0;
		mLeftParenIdx = -1;
	}

	CXChildVisitResult Visit(CXCursor cursor, CXCursor parent)
	{	
		String name = ToString(clang_getCursorSpelling(cursor));

		int rangeStart;
		int rangeEnd;
		GetCursorRange(cursor, &rangeStart, &rangeEnd);
		if ((mCursorIndex >= rangeStart) && (mCursorIndex < rangeEnd))
		{				
			if (clang_getCursorKind(cursor) == CXCursor_CallExpr)
			{	
				String methodName = ToString(clang_getCursorSpelling(cursor));
				int numArgs = clang_Cursor_getNumArguments(cursor);
				if ((methodName.length() == 0) || (mCursorIndex < rangeStart + (int)methodName.length()) /*|| (numArgs < 0)*/)
				{
					// Cursor is just on method name
					return CXChildVisit_Recurse;
				}
				mLeftParenIdx = rangeStart +  (int)methodName.length();

				mSelectIdx = 0;
				mParamStarts.clear();
				mFoundCall = true;
				
				for (int argIdx = 0; argIdx < numArgs; argIdx++)
				{					
					CXCursor argCursor = clang_Cursor_getArgument(cursor, argIdx);
					String argStr = ToString(clang_getCursorSpelling(argCursor));
					
					int argStartIdx;
					int argEndIdx;					
					GetCursorRange(argCursor, &argStartIdx, &argEndIdx);

					if ((mCursorIndex >= argStartIdx) && (mCursorIndex < argEndIdx))
						mSelectIdx = argIdx;

					if (argIdx == 0)
						mParamStarts.push_back(argStartIdx);
					//if (argIdx == numArgs - 1)
					mParamStarts.push_back(argEndIdx + 1);
				}

				if (numArgs == 0)
				{
					mParamStarts.push_back(rangeStart + (int)methodName.length());
				}

				//
				mCallDef = clang_getCursorReferenced(cursor);				
				if (clang_getCursorKind(mCallDef) == CXCursor_FirstInvalid)
					mFoundCall = false;
			}
		}

		return CXChildVisit_Recurse;
	}

	static CXChildVisitResult VisitProc(CXCursor cursor, CXCursor parent, CXClientData clientData)
	{
		return ((ClangFindCall*) clientData)->Visit(cursor, parent);
	}
};

class ClangFindFiles
{
public:
	std::set<String> mFileSet;
	CXFile mPrevFile;

public:
	ClangFindFiles()
	{		
		mPrevFile = NULL;
        
	}

	void ProcessModule(CXTranslationUnit transUnit, CXModule module)
	{
		int numHeaders = clang_Module_getNumTopLevelHeaders(transUnit, module);
		for (int headerIdx = 0; headerIdx < numHeaders; headerIdx++)
		{
			CXFile headerFile = clang_Module_getTopLevelHeader(transUnit, module, headerIdx);
			String fileName = ToString(clang_getFileName(headerFile));
			if (mFileSet.insert(fileName).second)
			{
				CXModule headerModule = clang_getModuleForFile(transUnit, headerFile);
				ProcessModule(transUnit, headerModule);
			}			
		}
	}

	CXChildVisitResult Visit(CXCursor cursor, CXCursor parent)
	{	
		CXFile includedFile = clang_getIncludedFile(cursor);
		if (includedFile != NULL)
		{
			String fileName = ToString(clang_getFileName(includedFile));
			mFileSet.insert(fileName);
		}

		auto range = clang_getCursorExtent(cursor);
		CXFile file;
		uint32 line;
		uint32 column;		
		uint32 offset;
		clang_getFileLocation(clang_getRangeStart(range), &file, &line, &column, &offset);	
	
		if (!clang_File_isEqual(file, mPrevFile))
		{
			String fileName = ToString(clang_getFileName(file));
			mFileSet.insert(fileName);
		}
	
		return CXChildVisit_Recurse;
	}
	
	static CXChildVisitResult VisitProc(CXCursor cursor, CXCursor parent, CXClientData clientData)
	{
		return ((ClangFindFiles*)clientData)->Visit(cursor, parent);
	}

	void InclusionProc(CXFile includedFile, CXSourceLocation* inclusionStack, unsigned includeLen)
	{
		String fileName = ToString(clang_getFileName(includedFile));

		String outData = "### " + fileName + " : \n";
		for (int i = 0; i < (int)includeLen; i++)
		{			
			for (int j = -5; j < i; j++)
				outData += " ";
			
			CXFile file;
			uint32 line;
			uint32 column;
			uint32 offset;
			clang_getFileLocation(inclusionStack[i], &file, &line, &column, &offset);

			CXString str = clang_getFileName(file);
			outData += ToString(str) + "\n";
		}
		OutputDebugStr(outData);

		mFileSet.insert(fileName);
	}

	static void InclusionProc(CXFile includedFile, CXSourceLocation* inclusionStack, unsigned includeLen, CXClientData clientData)
	{
		return ((ClangFindFiles*)clientData)->InclusionProc(includedFile, inclusionStack, includeLen);
	}
};

class ClangFindNavigationData
{
public:
	CXFile mWantFile;
	bool mAllowEmptyMethods;
	bool mAddLocation;

public:
	ClangFindNavigationData()
	{
		mAddLocation = true;
	}

	CXChildVisitResult Visit(CXCursor cursor, CXCursor parent)
	{
		String& outString = *gTLStrReturn.Get();

		CXSourceLocation sourceLoc = clang_getCursorLocation(cursor);
		CXFile file;
		uint32 line;
		uint32 column;
		uint32 offset;
		clang_getFileLocation(sourceLoc, &file, &line, &column, &offset);
		if ((mWantFile != NULL) && (file != mWantFile))
			return CXChildVisit_Continue;
		//if (!clang_Location_isFromMainFile(sourceLoc))
		//return CXChildVisit_Recurse;

		auto cursorKind = clang_getCursorKind(cursor);
		if (cursorKind == CXCursor_MacroExpansion)
			return CXChildVisit_Continue;

		String defString;

		if ((cursorKind == CXCursor_FunctionDecl) || (cursorKind == CXCursor_CXXMethod) || (cursorKind == CXCursor_Constructor) ||
			(cursorKind == CXCursor_Destructor) || (cursorKind == CXCursor_ConversionFunction) || (cursorKind == CXCursor_FunctionTemplate))
		{
			if (!mAllowEmptyMethods)
			{
				bool hasBody = false;
				clang_visitChildren(cursor, HasBodyVisitProc, &hasBody);
				if (!hasBody)
					return CXChildVisit_Continue;
			}

			String methodDefString;
			methodDefString += ToString(clang_getCursorSpelling(cursor));

			CXType methodType = clang_getCursorType(cursor);

			methodDefString += "(";
			int numArgs = clang_Cursor_getNumArguments(cursor);
			for (int argIdx = 0; argIdx < numArgs; argIdx++)
			{
				if (argIdx > 0)
					methodDefString += ", ";
				CXCursor argCursor = clang_Cursor_getArgument(cursor, argIdx);
				CXType paramType = clang_getCursorType(argCursor);
				methodDefString += ToString(clang_getTypeSpelling(paramType));

				String argName = ToString(clang_getCursorSpelling(argCursor));
				if (!argName.empty())
				{
					char lastC = methodDefString[methodDefString.length() - 1];
					if ((lastC != '*') && (lastC != ' '))
						methodDefString += " ";
					methodDefString += argName;
				}
			}

			if (clang_isFunctionTypeVariadic(methodType))
			{
				if (numArgs > 0)
					methodDefString += ", ";
				methodDefString += "...";
			}

			methodDefString += ")";

			auto checkParent = cursor;
			while (true)
			{
				checkParent = clang_getCursorSemanticParent(checkParent);

				auto checkParentKind = clang_getCursorKind(checkParent);
				if ((checkParentKind == CXCursor_ClassDecl) || (checkParentKind == CXCursor_StructDecl) ||
					(checkParentKind == CXCursor_Namespace) || (checkParentKind == CXCursor_UnionDecl))
				{
					methodDefString = ToString(clang_getCursorSpelling(checkParent)) + "::" + methodDefString;
				}

				// Don't fully qualify - just get the immediately preceding class name or namespace name
				break;
			}

			defString = methodDefString;
		}

		if (!defString.empty())
		{
			/*auto range = clang_getCursorExtent(cursor);
			CXFile file;
			uint32 line;
			uint32 column;
			uint32 offset;
			clang_getFileLocation(clang_getRangeStart(range), &file, &line, &column, &offset);*/

			if (mAddLocation)
				outString += StrFormat("%s\t%d\t%d\n", defString.c_str(), line - 1, column - 1);
			else
				outString = defString;
			return CXChildVisit_Continue;
		}

		return CXChildVisit_Recurse;
	}

	static CXChildVisitResult HasBodyVisitProc(CXCursor cursor, CXCursor parent, CXClientData clientData)
	{
		bool* hasBodyPtr = (bool*)clientData;
		auto cursorKind = clang_getCursorKind(cursor);
		if (cursorKind == CXCursor_CompoundStmt)
		{
			*hasBodyPtr = true;
			return CXChildVisit_Break;
		}
		return CXChildVisit_Recurse;
		//return ((ClangFindNavigationData*)clientData)->Visit(cursor, parent);
	}

	static CXChildVisitResult VisitProc(CXCursor cursor, CXCursor parent, CXClientData clientData)
	{
		return ((ClangFindNavigationData*)clientData)->Visit(cursor, parent);
	}
};


class ClangBreakCompound
{
public:
	CXCursor mDefCursor;	

public:
	ClangBreakCompound()
	{
		mDefCursor = clang_getNullCursor();
	}

	CXChildVisitResult Visit(CXCursor cursor, CXCursor parent)
	{		
		mDefCursor = cursor;
		return CXChildVisit_Break;
	}

	static CXChildVisitResult HasBodyVisitProc(CXCursor cursor, CXCursor parent, CXClientData clientData)
	{
		bool* hasBodyPtr = (bool*)clientData;
		auto cursorKind = clang_getCursorKind(cursor);
		if (cursorKind == CXCursor_CompoundStmt)
		{
			*hasBodyPtr = true;
			return CXChildVisit_Break;
		}
		return CXChildVisit_Recurse;
		//return ((ClangFindNavigationData*)clientData)->Visit(cursor, parent);
	}

	static CXChildVisitResult VisitProc(CXCursor cursor, CXCursor parent, CXClientData clientData)
	{
		return ((ClangBreakCompound*)clientData)->Visit(cursor, parent);
	}
};

/*typedef std::set<BfAutoComplete::Entry, BfAutoComplete::EntryLess> AutocompleteMap; 

static bool AddEntry(AutocompleteMap& entries, const BfAutoComplete::Entry& entry, const StringImpl& filter)
{
	if (filter.length() != 0)
	{
		if (strnicmp(filter.c_str(), entry.mDisplay, filter.length()) != 0)
			return false;
	}		

	entries.insert(entry);
	return true;
}*/

static bool IsIdentifierChar(char c)
{
	return (((c >= '0') && (c <= '9')) ||
		((c >= 'A') && (c <= 'Z')) ||
		((c >= 'a') && (c <= 'z')) ||
		((c == '_')));
}

struct ThreadProtect
{
	ClangHelper* mClangHelper;

	ThreadProtect(ClangHelper* clangHelper)
	{
		mClangHelper = clangHelper;
		if (clangHelper->mEntryCount == 0)
		{
			mClangHelper->mCurThreadId = GetCurrentThreadId();			
		}
		else
		{
			BF_ASSERT(mClangHelper->mCurThreadId == GetCurrentThreadId());
		}

		BF_ASSERT(mClangHelper->mCurThreadId);

		clangHelper->mEntryCount++;
	}

	~ThreadProtect()
	{
		BF_ASSERT(mClangHelper->mCurThreadId == GetCurrentThreadId());
		mClangHelper->mEntryCount--;
		if (mClangHelper->mEntryCount == 0)
		{
			mClangHelper->mCurThreadId = 0;
		}
	}
};

ClangHelper::ClangHelper()
{
	//TODO: Leave on?
	clang_toggleCrashRecovery(0);

	mIndex = clang_createIndex(1, 1);		
	BfLogClang("%d Clang clang_createIndex %p\n", ::GetCurrentThreadId(), mIndex);
	mIsForResolve = false;

	mCurThreadId = 0;
	mEntryCount = 0;
}

ClangHelper::~ClangHelper()
{
	BfLogClang("%d Clang clang_disposeIndex %p\n", ::GetCurrentThreadId, mIndex);
	clang_disposeIndex(mIndex);
		
	for (auto& mapPair : mTranslationUnits)
	{
		ClangTranslationUnit* transUnit = &mapPair.second;
		delete transUnit->mChars;

		BfLogClang("%d Clang clang_disposeTranslationUnit %p\n", ::GetCurrentThreadId(), transUnit->mTransUnit);
		clang_disposeTranslationUnit(transUnit->mTransUnit);
	}
}

ClangTranslationUnit* ClangHelper::GetTranslationUnit(const StringImpl& fileName, const char* headerPrefix, BfSourceClassifier::CharData* charData, int charLen, const char** args, int argCount, bool forceReparse, bool noTransUnit, bool noReparse)
{	
	ThreadProtect threadProtect(this);

	BfLogClang("%d Clang GetTranslationUnit %s\n", ::GetCurrentThreadId(), fileName.c_str());

	uint32 options;		
	if (mIsForResolve)
	{
		options = CXTranslationUnit_DetailedPreprocessingRecord |
			CXTranslationUnit_IncludeBriefCommentsInCodeCompletion |
			CXTranslationUnit_Incomplete |
			CXTranslationUnit_PrecompiledPreamble |
			CXTranslationUnit_CacheCompletionResults;			
	}
	else
	{
		options = CXTranslationUnit_SkipFunctionBodies |
			CXTranslationUnit_PrecompiledPreamble |
			CXTranslationUnit_Incomplete;
	}		

	//options = clang_defaultEditingTranslationUnitOptions();
	
	std::map<String, ClangTranslationUnit, StrPathCompare> transMap;
	auto itr2 = transMap.find(fileName);

	auto itr = mTranslationUnits.find(fileName);
	if (itr == mTranslationUnits.end())
	{	
		if (fileName[fileName.length() - 1] == 'h')
		{
			BF_ASSERT(headerPrefix != NULL);
		}

		const char* defaultArgs [] = { "-cc1", "-x", "c++", "-std=c++11", "-stdlib=libc++", "-target-cpu", "x86-64", "-target-feature", "+sse2",
            "-triple", "x86_64-pc-windows-gnu", "-D__x86_64__", "-D__x86_64", "-MD"};
		if (args == NULL)
		{
			args = defaultArgs;
			argCount = BF_ARRAY_COUNT(defaultArgs);
		}

		ClangTranslationUnit transUnit;

		bool isHeader = headerPrefix != NULL;			
			
		if (headerPrefix != NULL)
		{				
			transUnit.mHeaderPrefix = _strdup(headerPrefix);
			transUnit.mHeaderPrefixLen = (int)strlen(headerPrefix);

			transUnit.mCharLen = transUnit.mHeaderPrefixLen + 1;
			transUnit.mChars = new char[transUnit.mCharLen + 1];
			transUnit.mChars[transUnit.mHeaderPrefixLen] = 0;
			for (int i = 0; i < transUnit.mHeaderPrefixLen; i++)
				transUnit.mChars[i] = headerPrefix[i];

			charLen = transUnit.mHeaderPrefixLen;
		}

		if (charData != NULL)
		{
			BF_ASSERT(transUnit.mHeaderPrefixLen == 0);
			transUnit.mCharLen = charLen + 256;
			transUnit.mChars = new char[transUnit.mCharLen + 1];
			transUnit.mChars[charLen] = 0;
			for (int i = 0; i < charLen; i++)
				transUnit.mChars[i] = charData[i].mChar;
		}

		auto mapPair = mTranslationUnits.insert(ClangTranslationUnitMap::value_type(fileName, transUnit)).first;
		ClangTranslationUnit* transUnitP = &mapPair->second;					

		if (isHeader)
			transUnitP->mTransFileName = fileName + ".cpp";
		else
			transUnitP->mTransFileName = fileName;
		transUnitP->mRequestedFileName = fileName;

		if (transUnit.mChars != NULL)
		{
			AutoCrit autoCrit(gClangUnsavedFiles->mCritSect);

			CXUnsavedFile unsavedFile;
			unsavedFile.Contents = transUnit.mChars;
			unsavedFile.Length = charLen;
			unsavedFile.Filename = transUnitP->mTransFileName.c_str();
			BF_ASSERT(unsavedFile.Contents[unsavedFile.Length] == 0);
			gClangUnsavedFiles->mUnsavedFiles.push_back(unsavedFile);
		}

		if (!noTransUnit)
		{
			transUnitP->mTransUnit = clang_parseTranslationUnit(mIndex, transUnitP->mTransFileName.c_str(), args, argCount, VecFirstRef(gClangUnsavedFiles->mUnsavedFiles), (int)gClangUnsavedFiles->mUnsavedFiles.size(), options);
			BfLogClang("%d Clang clang_parseTranslationUnit %p\n", ::GetCurrentThreadId(), transUnitP->mTransUnit);
		}

		BfLogClang("%d Clang GetTranslationUnit Done(1) %s\n", ::GetCurrentThreadId(), fileName.c_str());
		return transUnitP;
	}
	else
	{
		ClangTranslationUnit* transUnit = &itr->second;
		if (charData != NULL)
		{
			AutoCrit autoCrit(gClangUnsavedFiles->mCritSect);

			int headerPrefixLen = transUnit->mHeaderPrefixLen;
			int contentCharLen = charLen + headerPrefixLen;

			CXUnsavedFile* unsavedFilePtr = NULL;
			unsigned long* lenPtr = NULL;
			if (contentCharLen > transUnit->mCharLen)
			{						
				char* prevChars = transUnit->mChars;					
				transUnit->mCharLen = contentCharLen + 256;
				transUnit->mChars = new char[transUnit->mCharLen + 1];
				if (headerPrefixLen > 0)
					memcpy(transUnit->mChars, transUnit->mHeaderPrefix, headerPrefixLen);

				if (prevChars == NULL)
				{
					CXUnsavedFile unsavedFile;
					unsavedFile.Contents = transUnit->mChars;
					unsavedFile.Length = contentCharLen;
					//unsavedFile.Filename = itr->first.c_str();
					unsavedFile.Filename = transUnit->mTransFileName.c_str();					
					gClangUnsavedFiles->mUnsavedFiles.push_back(unsavedFile);
					lenPtr = &gClangUnsavedFiles->mUnsavedFiles.back().Length;
					unsavedFilePtr = &gClangUnsavedFiles->mUnsavedFiles.back();
				}
				else
				{
					// Remap the new buffer in the mUnsavedFiles
					bool found = false;
					for (int i = 0; i < (int)gClangUnsavedFiles->mUnsavedFiles.size(); i++)
					{
						if (gClangUnsavedFiles->mUnsavedFiles[i].Contents == prevChars)
						{
							auto& unsavedFile = gClangUnsavedFiles->mUnsavedFiles[i];

							unsavedFile.Contents = transUnit->mChars;
							unsavedFile.Length = contentCharLen;
							unsavedFilePtr = &unsavedFile;							

							lenPtr = &unsavedFile.Length;
							found = true;
						}
					}
					BF_ASSERT(found);
					delete prevChars;
				}					
			}
			else
			{
				// Set the size again
				bool found = false;
				for (int i = 0; i < (int)gClangUnsavedFiles->mUnsavedFiles.size(); i++)
				{
					if (gClangUnsavedFiles->mUnsavedFiles[i].Contents == transUnit->mChars)
					{
						gClangUnsavedFiles->mUnsavedFiles[i].Length = contentCharLen;
						lenPtr = &gClangUnsavedFiles->mUnsavedFiles[i].Length;
						found = true;
					}
				}
				BF_ASSERT(found);
			}
			transUnit->mChars[contentCharLen] = 0;
			for (int i = 0; i < charLen; i++)
			{
				char c = charData[i].mChar;
				if (c == 0)
				{
					*lenPtr = i;
					break;
				}
				transUnit->mChars[headerPrefixLen + i] = c;
			}

			if (unsavedFilePtr != NULL)
			{
				BF_ASSERT(unsavedFilePtr->Contents[unsavedFilePtr->Length] == 0);
			}
		}						

		if (((charData != NULL) || (forceReparse)) && (transUnit->mTransUnit != NULL))
		{
			if (!noReparse)
			{
				BfLogClang("%d Clang clang_reparseTranslationUnit %p\n", GetCurrentThreadId(), transUnit->mTransUnit);
				auto reparseOption = clang_defaultReparseOptions(transUnit->mTransUnit);
				//reparseOption = CXReparse_None
				clang_reparseTranslationUnit(transUnit->mTransUnit, (int)gClangUnsavedFiles->mUnsavedFiles.size(), VecFirstRef(gClangUnsavedFiles->mUnsavedFiles), reparseOption);

				BfLogClang("%d Clang clang_reparseTranslationUnit Done %p\n", GetCurrentThreadId(), transUnit->mTransUnit);
			}
		}
		BfLogClang("%d Clang GetTranslationUnit Done(2) %s\n", ::GetCurrentThreadId(), fileName.c_str());
		return transUnit;
	}
}

void ClangHelper::AddTranslationUnit(const StringImpl& fileName, const char* headerPrefix, const StringImpl& clangArgs, BfSourceClassifier::CharData* charData, int charLen)
{
	// Remove any old temporary version
	//TODO: Put back
	//RemoveTranslationUnit(fileName);

	ThreadProtect threadProtect(this);

	String argsMem = clangArgs;
	argsMem += "\n-c\n-x\nc++\n";

	// Add Clang include path
	char exePath[MAX_PATH];
	GetModuleFileNameA(NULL, exePath, MAX_PATH);
	char* pathPtr = exePath + strlen(exePath) - 1;
	while ((*pathPtr != '\\') && (*pathPtr != '/'))
		pathPtr--;
	*pathPtr = 0;
	argsMem += "-I";
	argsMem += exePath;
	argsMem += "/llvm/lib/clang/3.5.0/include";

	std::vector<const char*> args;
	bool isStrStart = true;
	for (int i = 0; i < (int)argsMem.length(); i++)
	{
		if (argsMem[i] == '\n')
		{
			argsMem[i] = 0;
			isStrStart = true;
		}
		else if (isStrStart)
		{
			args.push_back(argsMem.c_str() + i);
			isStrStart = false;
		}
	}
    
	auto transUnit = GetTranslationUnit(fileName, headerPrefix, charData, charLen, VecFirstRef(args), (int)args.size());
	transUnit->mRefCount++;

	BfLogClang("%d Clang clang_AddTranslationUnit %s %p refCount:%d\n", ::GetCurrentThreadId(), fileName.c_str(), transUnit->mTransUnit, transUnit->mRefCount);
}

typedef std::map<String, ClangTranslationUnit, StrPathCompare> ClangTranslationUnitMap2;

void ClangHelper::RemoveTranslationUnit(const StringImpl& fileName)
{
	//ClangTranslationUnitMap2 map;
	//map.find(fileName);
	ThreadProtect threadProtect(this);

	auto itr = mTranslationUnits.find(fileName);
	if (itr == mTranslationUnits.end())
		return;
	ClangTranslationUnit* transUnit = &itr->second;
	transUnit->mRefCount--;

	BfLogClang("%d Clang RemoveTranslationUnit %p refCount:%d\n", ::GetCurrentThreadId(), transUnit->mTransUnit, transUnit->mRefCount);

	if (transUnit->mRefCount > 0)
		return;

	if (transUnit->mChars != NULL)
	{
		bool found = false;
		for (int i = 0; i < (int)gClangUnsavedFiles->mUnsavedFiles.size(); i++)
		{
			if (gClangUnsavedFiles->mUnsavedFiles[i].Contents == transUnit->mChars)
			{
				found = true;
				gClangUnsavedFiles->mUnsavedFiles.erase(gClangUnsavedFiles->mUnsavedFiles.begin() + i);
				i--;
			}				
		}
		BF_ASSERT(found);
			
		delete transUnit->mChars;
		delete transUnit->mHeaderPrefix;
	}

	if (transUnit->mTransUnit != NULL)
	{
		BfLogClang("%d Clang clang_disposeTranslationUnit %p\n", ::GetCurrentThreadId(), transUnit->mTransUnit);
		clang_disposeTranslationUnit(transUnit->mTransUnit);
	}

	mTranslationUnits.erase(itr);
}

const char* ClangHelper::DetermineFilesReferenced(const StringImpl& fileName)
{
	BfLogClang("%d Clang DetermineFilesReferenced %s\n", ::GetCurrentThreadId(), fileName.c_str());
		
	gClangPerfManager.ZoneStart("Clang DetermineFilesReferenced");

	ClangTranslationUnit* clangTransUnit = GetTranslationUnit(fileName, NULL, 0);
	CXTranslationUnit transUnit = clangTransUnit->mTransUnit;
	if (transUnit == NULL)
		return NULL;

	CXFile file = clang_getFile(transUnit, fileName.c_str());
		
	mReturnString.clear();
	ClangFindFiles clangFindFiles;		
		
	gClangPerfManager.ZoneStart("clang_getInclusions");
	clang_getInclusions(transUnit, ClangFindFiles::InclusionProc, &clangFindFiles);
	gClangPerfManager.ZoneEnd();
		
	for (auto& fileName : clangFindFiles.mFileSet)
	{
		String fixedFileName = fileName;
		for (int i = 0; i < (int)fixedFileName.length(); i++)
		{
			if (fixedFileName[i] == DIR_SEP_CHAR_ALT)
				fixedFileName[i] = DIR_SEP_CHAR;
		}
		mReturnString += fixedFileName + "\n";
	}

	gClangPerfManager.ZoneEnd();

	if (gClangPerfManager.mRecording)
	{
		gClangPerfManager.StopRecording();
		gClangPerfManager.DbgPrint();
	}

	return mReturnString.c_str();
}

void ClangHelper::CheckErrorRange(const StringImpl& errorString, int startIdx, int endIdx, int errorLookupTextIdx)
{
	if (errorLookupTextIdx == -1)
		return;
	if (mReturnString.length() != 0)
		return;
	if ((errorLookupTextIdx >= startIdx) && (errorLookupTextIdx < endIdx))		
		mReturnString += StrFormat("diag\t%d\t%d\t%s\n", startIdx, endIdx, errorString.c_str());		
}

const char* ClangHelper::Classify(const StringImpl& fileName, BfSourceClassifier::CharData* charData, int charLen, int cursorIdx, int errorLookupTextIdx, bool ignoreErrors)
{
	ThreadProtect threadProtect(this);

	gClangPerfManager.StartRecording();
		
	//OutputDebugStrF("Classify: %s\n", fileName.c_str());

	BfLogClang("%d Clang Classify %s\n", ::GetCurrentThreadId(), fileName.c_str());
		

	//Sleep(500);
	ClangClassifier clangClassifier;
	clangClassifier.mClassifierPassId = 2; //SourceDisplayId_FullClassify;
	clangClassifier.mCharData = charData;		
	//clangClassifier.SetElementType(0, charLen, BfSourceElementType_Normal);
	clangClassifier.ModifyFlags(0, charLen, 0, 0);				

	gClangPerfManager.ZoneStart("GetTranslationUnit");
	//ClangTranslationUnit* clangTransUnit = GetTranslationUnit(fileName, charData, charLen);

	ClangTranslationUnit* clangTransUnit = NULL;
	/*if (headerFileName != NULL)
	{
		GetTranslationUnit(headerFileName, NULL, charData, charLen, NULL, 0, false, true);
		clangTransUnit = GetTranslationUnit(fileName, NULL, 0, NULL, 0, true);			
	}
	else*/
	{
		clangTransUnit = GetTranslationUnit(fileName, NULL, charData, charLen, NULL, 0, false);
	}
	int adjustedCursorIdx = cursorIdx + clangTransUnit->mHeaderPrefixLen;
	const char* cursorFileName = clangTransUnit->mTransFileName.c_str(); //(headerFileName != NULL) ? headerFileName : fileName.c_str();

	CXTranslationUnit transUnit = clangTransUnit->mTransUnit;
	gClangPerfManager.ZoneEnd();
	if (transUnit == NULL)
		return NULL;

	CXFile file = clang_getFile(transUnit, cursorFileName);

	mReturnString.clear();		

	bool showErrors = false;

	gClangPerfManager.ZoneStart("Diagnostics");
	int numDiagnostics = clang_getNumDiagnostics(transUnit);
	for (int diagIdx = 0; diagIdx < numDiagnostics; diagIdx++)
	{
		if (ignoreErrors)
			break;

		CXDiagnostic diag = clang_getDiagnostic(transUnit, diagIdx);
			
		CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diag);
		auto var = clang_getDiagnosticCategory(diag);
		int flag = 0;
		if ((severity == CXDiagnostic_Error) || (severity == CXDiagnostic_Fatal))
			flag = BfSourceElementFlag_Error;
		else if (severity == CXDiagnostic_Warning)
			flag = BfSourceElementFlag_Warning;					

		if (flag != 0)
		{
			String errorString = ToString(clang_getDiagnosticSpelling(diag));

			bool ignore = false;
			if (clangTransUnit->mHeaderPrefix != NULL)
			{
				// A bit of a hack to filter our invalid warnings
				if ((int)errorString.find("#pragma once") != -1)
				{
					ignore = true;
				}
			}

			if (!ignore)
			{
				int rangeCount = (int)clang_getDiagnosticNumRanges(diag);
				for (int rangeIdx = 0; rangeIdx < rangeCount; rangeIdx++)
				{
					CXSourceRange range = clang_getDiagnosticRange(diag, rangeIdx);

					int startIdx;
					int endIdx;
					CXFile diagFile = GetRange(range, &startIdx, &endIdx);
					startIdx -= clangTransUnit->mHeaderPrefixLen;
					endIdx -= clangTransUnit->mHeaderPrefixLen;

					String diagFileName = ToString(clang_getFileName(diagFile));
					if (showErrors)
					{						
						CXSourceLocation sourceLocation = clang_getLocationForOffset(transUnit, file, startIdx);
						uint32 line;
						uint32 column;
						uint32 offset;
						clang_getFileLocation(sourceLocation, &file, &line, &column, &offset);		

						OutputDebugStrF("%s:%d:%d: %s\n", diagFileName.c_str(), line, column, errorString.c_str());
					}

					if ((startIdx >= 0) && (diagFile == file))
					{
						clangClassifier.ModifyFlags(startIdx, endIdx, 0xFF, flag);
						CheckErrorRange(errorString, startIdx, endIdx, errorLookupTextIdx);
					}
				}

				if (rangeCount == 0)
				{
					CXSourceLocation sourceLocation = clang_getDiagnosticLocation(diag);

					int startIdx;
					CXFile diagFile = GetLocation(sourceLocation, &startIdx);
					startIdx -= clangTransUnit->mHeaderPrefixLen;

					String diagFileName = ToString(clang_getFileName(diagFile));
					if (showErrors)
					{
						OutputDebugStrF("%s: %s\n", diagFileName.c_str(), errorString.c_str());
					}

					if ((startIdx >= 0) && (diagFile == file))
					{
						int endIdx = startIdx + 1;
						while (endIdx < charLen)
						{
							if (!IsIdentifierChar(charData[endIdx].mChar))
								break;
							endIdx++;
						}

						clangClassifier.ModifyFlags(startIdx, endIdx, 0xFF, flag);
						CheckErrorRange(errorString, startIdx, endIdx, errorLookupTextIdx);
					}
				}
			}
		}

		clang_disposeDiagnostic(diag);
	}
	gClangPerfManager.ZoneEnd();
		
	transUnit = transUnit;

	//		
	CXSourceLocation sourceLocation = clang_getLocationForOffset(transUnit, file, adjustedCursorIdx);
	CXCursor cursor = clang_getCursor(transUnit, sourceLocation);
	String displayName = ToString(clang_getCursorDisplayName(cursor));

	CXCursor parentCursor = clang_getCursorSemanticParent(cursor);

	// Try to see if we're bound to a function at the current location
	ClangFindCall clangFindCall;
	clangFindCall.mCursorIndex = adjustedCursorIdx;
	clang_visitChildren(parentCursor, clangFindCall.VisitProc, &clangFindCall);

	///						

	if (clangFindCall.mFoundCall) 
	{
		String methodDefString;
		methodDefString += ToString(clang_getCursorSpelling(clangFindCall.mCallDef));

		CXType methodType = clang_getCursorType(clangFindCall.mCallDef);

		methodDefString += "(\x1";
		int numArgs = clang_Cursor_getNumArguments(clangFindCall.mCallDef);
		for (int argIdx = 0; argIdx < numArgs; argIdx++)
		{
			if (argIdx > 0)
				methodDefString += ",\x1 ";
			CXCursor argCursor = clang_Cursor_getArgument(clangFindCall.mCallDef, argIdx);
			CXType paramType = clang_getCursorType(argCursor);
			methodDefString += ToString(clang_getTypeSpelling(paramType));

			String argName = ToString(clang_getCursorSpelling(argCursor));
			if (!argName.empty())
			{
				char lastC = methodDefString[methodDefString.length() - 1];
				if ((lastC != '*') && (lastC != ' '))
					methodDefString += " ";
				methodDefString += argName;
			}
		}

		if (clang_isFunctionTypeVariadic(methodType))
		{
			if (numArgs > 0)
				methodDefString += ",\x1 ";
			methodDefString += "...";
		}

		methodDefString += "\x1)";
		bool isValidCall = true;

		while (clangFindCall.mLeftParenIdx < charLen)
		{
			char c = charData[clangFindCall.mLeftParenIdx].mChar;
			if (c == '(')
				break;
			clangFindCall.mLeftParenIdx++;
		}

		// If we're to the left of the paren then don't show the invocation
		if (clangFindCall.mLeftParenIdx >= adjustedCursorIdx)
			isValidCall = false;

		if (isValidCall)
		{
			mReturnString += "invoke_cur\t" + methodDefString;
			mReturnString += "\n";
			mReturnString += StrFormat("invokeLeftParen\t%d\n", clangFindCall.mLeftParenIdx);
		}
	}

	CXSourceRangeList* rangeList = clang_getSkippedRanges(transUnit, file);
	for (int rangeIdx = 0; rangeIdx < (int)rangeList->count; rangeIdx++)
	{
		int startOffset;
		int endOffset;
		GetRange(rangeList->ranges[rangeIdx], &startOffset, &endOffset);

		startOffset = std::max(0, startOffset - clangTransUnit->mHeaderPrefixLen);
		endOffset = std::max(0, endOffset - clangTransUnit->mHeaderPrefixLen);

		while (startOffset < endOffset)
		{
			if (clangClassifier.mCharData[startOffset].mChar == '\n')
			{
				startOffset++;
				break;
			}
			startOffset++;
		}

		while (endOffset > startOffset)
		{
			endOffset--;
			if (clangClassifier.mCharData[endOffset].mChar == '\n')
				break;
		}

		clangClassifier.ModifyFlags(startOffset, endOffset, 0xFF, BfSourceElementFlag_Skipped);
		//clangClassifier.SetElementType(startOffset, endOffset, BfSourceElementType_Comment);
	}
	clang_disposeSourceRangeList(rangeList);

	gClangPerfManager.ZoneStart("Tokens");
	CXSourceRange range = clang_getCursorExtent(clang_getTranslationUnitCursor(transUnit));
		
	gClangPerfManager.ZoneEnd();
	
	if (gClangPerfManager.mRecording)
	{
		gClangPerfManager.StopRecording();
		gClangPerfManager.DbgPrint();
	}

	return mReturnString.c_str();
}

void ClangHelper::DecodeCompletionString(CXCompletionString completionString, String& displayText, String& replaceText, String& chunkText)
{
	auto num = clang_getNumCompletionChunks(completionString);

	//char buf[256];

	bool hadSymbol = false;
	for (int chunkIdx = 0; chunkIdx < (int) num; chunkIdx++)
	{
		auto chunkKind = clang_getCompletionChunkKind(completionString, chunkIdx);
		ToString(clang_getCompletionChunkText(completionString, chunkIdx), chunkText);

		switch (chunkKind)
		{
		case CXCompletionChunk_LeftParen:
			hadSymbol = true;
			replaceText += "(\x1";
			break;
		case CXCompletionChunk_RightParen:
			hadSymbol = true;
			replaceText += "\x1)";
			break;
		case CXCompletionChunk_Comma:
			replaceText += ",\x1 ";
			break;
		case CXCompletionChunk_LeftBracket:
		case CXCompletionChunk_RightBracket:
		case CXCompletionChunk_LeftBrace:
		case CXCompletionChunk_RightBrace:
		case CXCompletionChunk_LeftAngle:
		case CXCompletionChunk_RightAngle:
		case CXCompletionChunk_CurrentParameter:
		case CXCompletionChunk_Colon:
		case CXCompletionChunk_HorizontalSpace:
		case CXCompletionChunk_VerticalSpace:
			hadSymbol = true;
			replaceText += chunkText;
			break;
		case CXCompletionChunk_TypedText:
			if (!hadSymbol)
			{
				displayText += chunkText;					
			}
			replaceText += chunkText;
			break;
		case CXCompletionChunk_Placeholder:
			for (int i = 0; i < (int)chunkText.size(); i++)
				if (chunkText[i] == ',')
					chunkText.insert(chunkText.begin() + i + 1, '\x1');
			replaceText += chunkText;
			break;
		case CXCompletionChunk_ResultType:
		case CXCompletionChunk_Text:
		case CXCompletionChunk_Informative:
		case CXCompletionChunk_Equal:
			/*if (description.length() > 0)
				description += " ";
			description += chunkText;*/
			break;
		case CXCompletionChunk_Optional:
			{
				auto subCompletionString = clang_getCompletionChunkCompletionString(completionString, chunkIdx);					
				DecodeCompletionString(subCompletionString, displayText, replaceText, chunkText);
			}
			break;
		case CXCompletionChunk_SemiColon:
			break;
		}

	}
}
	
const char* ClangHelper::Autocomplete(const StringImpl& fileName, BfSourceClassifier::CharData* charData, int charLen, int cursorIdx)
{
	gClangPerfManager.StartRecording();
		
	//Sleep(500);

	BfLogClang("%d Clang Autocomplete %s\n", ::GetCurrentThreadId(), fileName.c_str());

	static int gACCount = 0;
	gACCount++;

	gClangPerfManager.ZoneStart("GetTranslationUnit");
	ClangTranslationUnit* clangTransUnit = NULL;
	/*if (headerFileName != NULL)
	{
		GetTranslationUnit(headerFileName, NULL, charData, charLen, NULL, 0, false, true);
		clangTransUnit = GetTranslationUnit(fileName, NULL, NULL, 0, NULL, 0, true);
	}
	else*/
	{
		bool noReparse = false;
		clangTransUnit = GetTranslationUnit(fileName, NULL, charData, charLen, NULL, 0, false, false, noReparse);
	}
	//const char* cursorFileName = (headerFileName != NULL) ? headerFileName : fileName.c_str();
	const char* cursorFileName = clangTransUnit->mTransFileName.c_str();
		
	CXTranslationUnit transUnit = clangTransUnit->mTransUnit;
	gClangPerfManager.ZoneEnd();
	if (transUnit == NULL)
		return NULL;				

	gClangPerfManager.ZoneStart("Autocomplete");

	String namespaceFind;
	String filter;
	int startIdx = std::max(0, cursorIdx - 1);
	bool isInvocation = false;
	int parenPos = -1;	
	int lastCharIdx = -1;
	while (startIdx > 0)
	{			
		char c = charData[startIdx].mChar;
		if (charData[startIdx].mDisplayTypeId == BfSourceElementType_Comment)
		{
			startIdx--;
			continue;;
		}

		if ((c == '(') && (filter.length() == 0))
		{
			isInvocation = true;
			mInvokeString.clear();
			startIdx--;
			cursorIdx--;
			parenPos = startIdx;

			while (startIdx > 0)
			{
				if (!isspace(charData[startIdx].mChar))
					break;
				startIdx--;
			}

			continue;
		}
			
		if (IsIdentifierChar(c))
		{
			filter.insert(filter.begin(), c);
			lastCharIdx = startIdx;
		}
		else if (isspace((uint8)c))
		{
			if (filter.length() == 0)
				return NULL;
		}
		else
		{
			if ((c == ':') && (startIdx > 0) && (charData[startIdx - 1].mChar == ':'))
			{
				int prevStartIdx = startIdx;

				startIdx -= 2;
				while (startIdx > 0)
				{
					char c = charData[startIdx].mChar;
					if (IsIdentifierChar(c))
					{
						namespaceFind.insert(namespaceFind.begin(), c);
					}
					else
					{
						break;
					}
					startIdx--;
				}

				startIdx = prevStartIdx;
			}
			
			if (lastCharIdx != -1)
				startIdx = lastCharIdx;
			else
				startIdx++;
			break;
		}
		startIdx--;
	}		

	int autocompletePos = startIdx;		
	if ((autocompletePos == 0) || (charData[autocompletePos + 1].mChar == 0))		
		return NULL;

	char prevChar = charData[autocompletePos - 1].mChar;
	//if ((prevChar == '>') || (prevChar == '.') || (prevChar)
	//((isspace((uint8)charData[autocompletePos - 1].mChar)) && (isspace((uint8)charData[autocompletePos].mChar))))

	int adjustedAutocompletePos = autocompletePos + clangTransUnit->mHeaderPrefixLen;
		
	CXFile file = clang_getFile(transUnit, cursorFileName);
	CXSourceLocation sourceLocation = clang_getLocationForOffset(transUnit, file, adjustedAutocompletePos);
	uint32 line;
	uint32 column;
	uint32 offset;

	clang_getFileLocation(sourceLocation, &file, &line, &column, &offset);		

	if (line == 0)
		return NULL;

	gClangPerfManager.ZoneStart("clang_codeCompleteAt");
	CXCodeCompleteResults* completeResults = clang_codeCompleteAt(transUnit, cursorFileName, line, column, VecFirstRef(gClangUnsavedFiles->mUnsavedFiles), (int)gClangUnsavedFiles->mUnsavedFiles.size(),
		CXCodeComplete_IncludeMacros | CXCodeComplete_IncludeCodePatterns | CXCodeComplete_IncludeBriefComments);
	gClangPerfManager.ZoneEnd();
		
	auto contexts = clang_codeCompleteGetContexts(completeResults);

	//String usr = ToString(clang_codeCompleteGetContainerUSR(completeResults));
	uint32 isIncomplete;
	CXCursorKind cursorKind = clang_codeCompleteGetContainerKind(completeResults, &isIncomplete);
	bool isMember = cursorKind != CXCursor_InvalidCode;

	if (namespaceFind.length() > 0)
		isMember = true;

	if (!isInvocation)
	{
		if (isMember)
		{
			filter.clear();
		}
		else if (filter.length() > 0)
		{
			// Just filter on the first character
			filter = filter.substr(0, 1);
		}
	}

	/*char strChars[16] = {0};
	memcpy(strChars, mUnsavedFiles[0].Contents + autocompletePos - 15, 15);
	OutputDebugStrF("Autocomplete: '%s' TextBefore:'%s' Idx:%d ResultCount:%d Container:%d\n", filter.c_str(), strChars, autocompletePos, completeResults->NumResults, cursorKind);*/

	if (isInvocation)
	{
		bool hasMethod = false;
		for (int resultIdx = 0; resultIdx < (int) completeResults->NumResults; resultIdx++)
		{
			CXCompletionResult completeResult = completeResults->Results[resultIdx];			
			auto resultKind = completeResult.CursorKind;
			if ((resultKind == CXCursor_CXXMethod) || (resultKind == CXCursor_FunctionDecl) || (resultKind == CXCursor_Constructor) || (resultKind == CXCursor_Destructor))
			{
				hasMethod = true;
				break;
			}
		}

		// This happens when we have a "new TypeName(" - we only get type completions, so we need to autocomplete on the paren
		if (!hasMethod)
		{
			clang_disposeCodeCompleteResults(completeResults);
			autocompletePos = parenPos;
			sourceLocation = clang_getLocationForOffset(transUnit, file, adjustedAutocompletePos);
			clang_getFileLocation(sourceLocation, &file, &line, &column, &offset);
			completeResults = clang_codeCompleteAt(transUnit, cursorFileName, line, column, VecFirstRef(gClangUnsavedFiles->mUnsavedFiles), (int)gClangUnsavedFiles->mUnsavedFiles.size(),
				CXCodeComplete_IncludeMacros | CXCodeComplete_IncludeCodePatterns | CXCodeComplete_IncludeBriefComments);
		}
	}
		
	mReturnString.clear();
	if (completeResults == NULL)
		return NULL;
				
		
	//AutocompleteMap entries;
	AutoCompleteBase autoComplete;
		
	if (isMember)
		mReturnString += "isMember\t1\n";

	String displayText;
	String replaceText;
	String description;
	String chunkText;

	for (int resultIdx = 0; resultIdx < (int) completeResults->NumResults; resultIdx++)
	{
		AutoPerf autoPerf("HandleResult", &gClangPerfManager);

		displayText.clear();
		replaceText.clear();
		description.clear();

		if ((!isMember) && (filter.empty()))
			break; // Don't allow listing of ALL globals

		CXCompletionResult completeResult = completeResults->Results[resultIdx];
		auto resultKind = completeResult.CursorKind;

		/*if (namespaceFind.length() != 0)
		{
			if (resultKind != CXCursor_Namespace)
				continue;
			
			DecodeCompletionString(completeResult.CompletionString, displayText, replaceText, chunkText);
			if (displayText != namespaceFind)
				continue;

				
		}*/

		//auto priority = clang_getCompletionPriority(completeResult.CompletionString);			
		DecodeCompletionString(completeResult.CompletionString, displayText, replaceText, chunkText);

		if (namespaceFind.length() > 0)
			OutputDebugStrF("%s\n", displayText.c_str());

		if ((resultKind == CXCursor_CXXMethod) || (resultKind == CXCursor_FunctionDecl) || (resultKind == CXCursor_FunctionTemplate) ||
			((resultKind == CXCursor_Constructor) && (isInvocation)))
		{
			bool isValid = true;
			for (int i = 0; i < (int)displayText.size(); i++)
				if (!IsIdentifierChar(displayText[i]))
					isValid = false;

			if (isValid)
			{
				if (!isInvocation)
					autoComplete.AddEntry(BfAutoComplete::Entry("method", displayText), filter);

				if ((isInvocation) && (displayText == filter))
				{						
					mInvokeString += "invoke\t" + replaceText + "\n";
				}
			}
		}	
						
		// Note: primitives are of type CXCursor_NotImplemented
		if ((resultKind == CXCursor_StructDecl) || (resultKind == CXCursor_UnionDecl) || (resultKind == CXCursor_ClassDecl) || 
			(resultKind == CXCursor_ClassTemplate) ||
			(resultKind == CXCursor_EnumDecl) || (resultKind == CXCursor_TypedefDecl) || (resultKind == CXCursor_TypeAliasDecl) ||
			(resultKind == CXCursor_UnexposedDecl) || (resultKind == CXCursor_NotImplemented))
		{
			if (!isInvocation)
			{				
				autoComplete.AddEntry(BfAutoComplete::Entry("type", displayText), filter);
			}				
		}

		if (!isInvocation)
		{
			if (resultKind == CXCursor_VarDecl)
			{
				autoComplete.AddEntry(BfAutoComplete::Entry("local", displayText), filter);
			}
			else if (resultKind == CXCursor_ParmDecl)
			{
				autoComplete.AddEntry(BfAutoComplete::Entry("param", displayText), filter);
			}
			else if (resultKind == CXCursor_FieldDecl)
			{
				autoComplete.AddEntry(BfAutoComplete::Entry("field", displayText), filter);
			}
			else if (resultKind == CXCursor_Namespace)
			{
				autoComplete.AddEntry(BfAutoComplete::Entry("namespace", displayText), filter);
			}							
		}
	}

	for (auto& entry : autoComplete.mEntries)
	{
		String entryString = String(entry.mEntryType) + "\t" + String(entry.mDisplay) + "\n";
		mReturnString.append(entryString);
	}				

	if ((isInvocation) && (!mInvokeString.empty()))
	{
		mInvokeString += StrFormat("invokeLeftParen\t%d\n", cursorIdx);
	}

	mReturnString += mInvokeString;
	mInvokeString.clear();

	clang_disposeCodeCompleteResults(completeResults);		

	gClangPerfManager.ZoneEnd();

	if (gClangPerfManager.mRecording)
	{
		gClangPerfManager.StopRecording();
		gClangPerfManager.DbgPrint();
	}								

	return mReturnString.c_str();
}

//////////////////////////////////////////////////////////////////////////

BF_EXPORT ClangHelper* BF_CALLTYPE ClangHelper_Create(bool isForResolve)
{
	if (gClangUnsavedFiles == NULL)
		gClangUnsavedFiles = new ClangUnsavedFiles();

	ClangHelper* clangHelper = new ClangHelper();
	clangHelper->mIsForResolve = isForResolve;
	return clangHelper;
}

BF_EXPORT void BF_CALLTYPE ClangHelper_Delete(ClangHelper* clangHelper)
{	
	delete clangHelper;

	delete gClangUnsavedFiles;
	gClangUnsavedFiles = NULL;
}

BF_EXPORT void BF_CALLTYPE ClangHelper_AddTranslationUnit(ClangHelper* clangHelper, const char* fileName, const char* headerPrefix, const char* clangArgs, BfSourceClassifier::CharData* charData, int charLen)
{
	if (clangHelper == NULL)
		return;
	if (!clangHelper->mIsForResolve)
	{		
		OutputDebugStrF("AddTranslationUnit %s\n", fileName);
		//gClangPerfManager.StartRecording();
	}

	gClangPerfManager.ZoneStart("Clang AddTranslationUnit");
	clangHelper->AddTranslationUnit(fileName, headerPrefix, clangArgs, charData, charLen);
	gClangPerfManager.ZoneEnd();
}

BF_EXPORT void BF_CALLTYPE ClangHelper_RemoveTranslationUnit(ClangHelper* clangHelper, const char* fileName)
{
	if (clangHelper == NULL)
		return;
	gClangPerfManager.ZoneStart("Clang RemoveTranslationUnit");
	clangHelper->RemoveTranslationUnit(fileName);
	gClangPerfManager.ZoneEnd();	
}

BF_EXPORT const char* BF_CALLTYPE ClangHelper_Classify(ClangHelper* clangHelper, const char* fileName, BfSourceClassifier::CharData* charData, int charLen, int cursorIdx, int errorLookupTextIdx, bool ignoreErrors)
{
	if (clangHelper == NULL)
		return "";
	return clangHelper->Classify(fileName, charData, charLen, cursorIdx, errorLookupTextIdx, ignoreErrors);
}

BF_EXPORT const char* BF_CALLTYPE ClangHelper_Autocomplete(ClangHelper* clangHelper, const char* fileName, BfSourceClassifier::CharData* charData, int charLen, int cursorIdx)
{
	if (clangHelper == NULL)
		return "";
	return clangHelper->Autocomplete(fileName, charData, charLen, cursorIdx);
}

BF_EXPORT const char* BF_CALLTYPE ClangHelper_DetermineFilesReferenced(ClangHelper* clangHelper, const char* fileName)
{
	if (clangHelper == NULL)
		return "";
	return clangHelper->DetermineFilesReferenced(fileName);
}

BF_EXPORT const char* BF_CALLTYPE ClangHelper_FindDefinition(ClangHelper* clangHelper, const char* fileName, int defPos, int& outDefLine, int& outDefColumn)
{
	if (clangHelper == NULL)
		return "";

	outDefLine = 0;
	outDefColumn = 0;
	
	ClangTranslationUnit* clangTransUnit = clangHelper->GetTranslationUnit(fileName, NULL, 0);
	CXTranslationUnit transUnit = clangTransUnit->mTransUnit;

	const char* cursorFileName = clangTransUnit->mTransFileName.c_str();
	CXFile file = clang_getFile(transUnit, cursorFileName);
	//CXSourceLocation loc = clang_getLocation(transUnit, file, line + 1, column + 1);
	int adjustedPos = defPos + clangTransUnit->mHeaderPrefixLen - 1; 
	CXSourceLocation loc = clang_getLocationForOffset(transUnit, file, adjustedPos);
	
	CXCursor cursor = clang_getCursor(transUnit, loc);	

	if (clang_getCursorKind(cursor) == CXCursor_CompoundStmt)
	{
		CXSourceRange range = clang_getCursorExtent(cursor);
		uint numTokens = 0;
		CXToken* tokens = NULL;
		clang_tokenize(transUnit, range, &tokens, &numTokens);
		for (int tokenIdx = 0; tokenIdx < (int)numTokens; tokenIdx++)
		{

		}

		clang_disposeTokens(transUnit, tokens, numTokens);

		/*auto startLoc = clang_getCursorLocation(cursor);
		CXFile defFile;
		uint32 offset;
		clang_getFileLocation(startLoc, &defFile, (uint32*)&outDefLine, (uint32*)&outDefColumn, &offset);
		outDefLine--;
		outDefColumn--;

		

		ClangBreakCompound clangBreakCompound;
		clang_visitChildren(cursor, ClangBreakCompound::VisitProc, &clangBreakCompound);
		cursor = clangBreakCompound.mDefCursor;*/
	}

	String displayName = ToString(clang_getCursorDisplayName(cursor));

	int numArgs = clang_Cursor_getNumArguments(cursor);
	for (int argIdx = 0; argIdx < numArgs; argIdx++)
	{
		CXCursor argCursor = clang_Cursor_getArgument(cursor, argIdx);		
	}

	CXFile includedFile = clang_getIncludedFile(cursor);
	if (includedFile != NULL)
	{
		outString = ToString(clang_getFileName(includedFile));
		return outString.c_str();
	}
	
	CXCursor refCursor = clang_getCursorReferenced(cursor);
	//if (clang_getCursorKind(refCursor) < CXCursor_FirstInvalid)
	cursor = refCursor;

	CXCursor defCursor = clang_getCursorDefinition(cursor);

	/*auto cursorKind = clang_getCursorKind(cursor);
	if ((cursorKind == CXCursor_FunctionDecl) || (cursorKind == CXCursor_CXXMethod) || (cursorKind == CXCursor_Constructor) ||
		(cursorKind == CXCursor_Destructor) || (cursorKind == CXCursor_ConversionFunction) || (cursorKind == CXCursor_FunctionTemplate))
	{
		//ClangFindNavigationData clangFindNavigationData;
		bool hasBody = false;
		clang_visitChildren(cursor, ClangFindNavigationData::HasBodyVisitProc, &hasBody); 

		if (hasBody)
		{
			defCursor = clang_getCursorLexicalParent(cursor);
		}
	}*/

	if (!clang_Cursor_isNull(defCursor))
		cursor = defCursor;

	CXSourceRange cursorExtent = clang_getCursorExtent(cursor);
	CXSourceLocation startLoc = clang_getRangeStart(cursorExtent);

	CXFile defFile;
	uint32 offset;
	clang_getFileLocation(startLoc, &defFile, (uint32*)&outDefLine, (uint32*)&outDefColumn, &offset);
	outDefLine--;
	outDefColumn--;

	if (defFile == file)
	{
		outString = clangTransUnit->mRequestedFileName;

		// Adjust line num
		for (int i = 0; i < clangTransUnit->mHeaderPrefixLen; i++)
			if (clangTransUnit->mHeaderPrefix[i] == '\n')
				outDefLine--;
	}
	else
		outString = ToString(clang_getFileName(defFile));
	if (outString.length() == 0)
		return NULL;
	return outString.c_str();	
}

BF_EXPORT const char* BF_CALLTYPE ClangHelper_GetNavigationData(ClangHelper* clangHelper, const char* fileName)
{
	if (clangHelper == NULL)
		return "";

	ClangTranslationUnit* clangTransUnit = clangHelper->GetTranslationUnit(fileName, NULL, 0);
	CXTranslationUnit transUnit = clangTransUnit->mTransUnit;

	outString.clear();	

	ClangFindNavigationData clangFindNavigationData;
	clangFindNavigationData.mAllowEmptyMethods = clangTransUnit->mIsHeader;
	clangFindNavigationData.mWantFile = clang_getFile(transUnit, clangTransUnit->mRequestedFileName.c_str());;	
	clang_visitChildren(clang_getTranslationUnitCursor(transUnit), clangFindNavigationData.VisitProc, &clangFindNavigationData);

	return outString.c_str();
}

BF_EXPORT const char* BF_CALLTYPE ClangHelper_GetCurrentLocation(ClangHelper* clangHelper, const char* fileName, int defPos)
{
	if (clangHelper == NULL)
		return "";

	ClangTranslationUnit* clangTransUnit = clangHelper->GetTranslationUnit(fileName, NULL, 0);
	CXTranslationUnit transUnit = clangTransUnit->mTransUnit;

	if (defPos >= clangTransUnit->mCharLen)
		return ""; // Sanity check

	outString.clear();
	
	const char* cursorFileName = clangTransUnit->mTransFileName.c_str();
	CXFile file = clang_getFile(transUnit, cursorFileName);
	//CXSourceLocation loc = clang_getLocation(transUnit, file, line + 1, column + 1);
	int adjustedPos = defPos + clangTransUnit->mHeaderPrefixLen;
	CXSourceLocation loc = clang_getLocationForOffset(transUnit, file, adjustedPos);

	ClangFindNavigationData clangFindNavigationData;
	CXCursor cursor = clang_getCursor(transUnit, loc);
	clangFindNavigationData.mAllowEmptyMethods = false;
	clangFindNavigationData.mWantFile = NULL;//clang_getFile(transUnit, clangTransUnit->mRequestedFileName.c_str());;
	clangFindNavigationData.mAddLocation = false;
	while (!clang_Cursor_isNull(cursor))
	{
		auto cursorKind = clang_getCursorKind(cursor);

		if ((cursorKind == CXCursor_FunctionDecl) || (cursorKind == CXCursor_CXXMethod) || (cursorKind == CXCursor_Constructor) ||
			(cursorKind == CXCursor_Destructor) || (cursorKind == CXCursor_ConversionFunction) || (cursorKind == CXCursor_FunctionTemplate))
		{
			clangFindNavigationData.Visit(cursor, cursor);
			break;
		}

		//cursor = clang_getCursorLexicalParent(cursor);
		cursor = clang_getCursorSemanticParent(cursor);
	}
	
	return outString.c_str();
}

#endif