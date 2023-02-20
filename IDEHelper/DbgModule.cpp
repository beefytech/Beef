#pragma warning(push)
#pragma warning(disable:4146)
#pragma warning(disable:4996)
#pragma warning(disable:4800)
#pragma warning(disable:4244)

#include "DbgModule.h"

#include "DWARFInfo.h"
#include <windows.h>
#include <stddef.h>
#include <stdio.h>
#include <string>
#include <inttypes.h>
#include <assert.h>
#include <vector>
#include "WinDebugger.h"
#include "DebugManager.h"
#include "DebugTarget.h"
#include "COFFData.h"
#include "Compiler/BfDemangler.h"
#include "BeefySysLib/util/Hash.h"
#include "BeefySysLib/util/BeefPerf.h"
#include "DbgSymSrv.h"
#include "MiniDumpDebugger.h"

#pragma warning(pop)

#pragma warning(disable:4996)

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF_DBG;

void SetBreakpoint(int64_t address);

NS_BF_DBG_BEGIN

#ifdef BF_DBG_32
typedef PEOptionalHeader32 PEOptionalHeader;
typedef PE_NTHeaders32 PE_NTHeaders;
#else
typedef PEOptionalHeader64 PEOptionalHeader;
typedef PE_NTHeaders64 PE_NTHeaders;
#endif

#define GET(T) *((T*)(data += sizeof(T)) - 1)
#define GET_FROM(ptr, T) *((T*)(ptr += sizeof(T)) - 1)

//////////////////////////////////////////////////////////////////////////

DbgCompileUnit::DbgCompileUnit(DbgModule* dbgModule)
{
	mDbgModule = dbgModule;
	mLanguage = DbgLanguage_Unknown;

	mGlobalBlock = mDbgModule->mAlloc.Alloc<DbgBlock>();
	mGlobalType = mDbgModule->mAlloc.Alloc<DbgType>();

	mGlobalType->mTypeCode = DbgType_Root;
	mGlobalType->mPriority = DbgTypePriority_Primary_Explicit;
	mGlobalType->mCompileUnit = this;

	mLowPC = (addr_target)-1;
	mHighPC = 0;
	//mDoPrimaryRemapping = true;
	mNeedsLineDataFixup = true;
	mWasHotReplaced = false;
	mIsMaster = false;
}

//////////////////////////////////////////////////////////////////////////

addr_target DbgLineDataEx::GetAddress()
{
	return mSubprogram->GetLineAddr(*mLineData);
}

DbgSrcFile* DbgLineDataEx::GetSrcFile()
{
	auto inlineRoot = mSubprogram->GetRootInlineParent();
	return inlineRoot->mLineInfo->mContexts[mLineData->mCtxIdx].mSrcFile;
}

addr_target DbgSubprogram::GetLineAddr(const DbgLineData& lineData)
{
	return (addr_target)(lineData.mRelAddress + mCompileUnit->mDbgModule->mImageBase);
}

DbgSubprogram* DbgSubprogram::GetLineInlinee(const DbgLineData& lineData)
{
	auto inlineRoot = GetRootInlineParent();
	return inlineRoot->mLineInfo->mContexts[lineData.mCtxIdx].mInlinee;
}

DbgSrcFile* DbgSubprogram::GetLineSrcFile(const DbgLineData& lineData)
{
	auto inlineRoot = GetRootInlineParent();
	return inlineRoot->mLineInfo->mContexts[lineData.mCtxIdx].mSrcFile;
}

bool DbgSubprogram::HasValidLines()
{
	auto inlineRoot = GetRootInlineParent();
	for (int lineIdx = 0; lineIdx < (int)inlineRoot->mLineInfo->mLines.size(); lineIdx++)
	{
		auto& lineInfo = inlineRoot->mLineInfo->mLines[lineIdx];
		if (lineInfo.mColumn >= 0)
			return true;
	}
	return false;
}

void DbgSubprogram::PopulateSubprogram()
{
	if (mDeferredInternalsSize == 0)
		return;
	mCompileUnit->mDbgModule->PopulateSubprogram(this);
}

//////////////////////////////////////////////////////////////////////////

DbgLineDataBuilder::DbgLineDataBuilder(DbgModule* dbgModule)
{
	mDbgModule = dbgModule;
	mCurSubprogram = NULL;
	mCurRecord = NULL;
}

DbgLineData* DbgLineDataBuilder::Add(DbgCompileUnit* compileUnit, DbgLineData& lineData, DbgSrcFile* srcFile, DbgSubprogram* inlinee)
{
	addr_target address = (addr_target)(lineData.mRelAddress + mDbgModule->mImageBase);
	if ((compileUnit->mLowPC != (addr_target)-1) && ((address < (addr_target)compileUnit->mLowPC) || (address >= (addr_target)compileUnit->mHighPC)))
		return NULL;

	if ((mCurSubprogram == NULL) || (address < mCurSubprogram->mBlock.mLowPC) || (address >= mCurSubprogram->mBlock.mHighPC))
	{
		DbgSubprogramMapEntry* mapEntry = mDbgModule->mDebugTarget->mSubprogramMap.Get(address, DBG_MAX_LOOKBACK);
		if (mapEntry != NULL)
		{
			mCurSubprogram = mapEntry->mEntry;

			if (address > mCurSubprogram->mBlock.mHighPC)
				mCurSubprogram = NULL;

			if (mCurSubprogram != NULL)
			{
				SubprogramRecord** recordPtr = NULL;
				if (mRecords.TryAdd(mCurSubprogram, NULL, &recordPtr))
				{
					// It's not too expensive to over-reserve here, because these are just temporary structures that get copied
					//  exactly sized when we Commit
					mCurRecord = mAlloc.Alloc<SubprogramRecord>();
					*recordPtr = mCurRecord;
					mCurRecord->mContexts.mAlloc = &mAlloc;
					mCurRecord->mContexts.Reserve(16);
					mCurRecord->mLines.mAlloc = &mAlloc;
					mCurRecord->mLines.Reserve(128);
					mCurRecord->mCurContext = -1;
					mCurRecord->mHasInlinees = false;
				}
				else
					mCurRecord = *recordPtr;
			}
			else
				mCurRecord = NULL;
		}
	}

	if (mCurSubprogram == NULL)
		return NULL;

	bool needsNewCtx = false;
	if (mCurRecord->mCurContext == -1)
	{
		needsNewCtx = true;
	}
	else
	{
		auto& curContext = mCurRecord->mContexts[mCurRecord->mCurContext];
		if ((curContext.mInlinee != inlinee) || (curContext.mSrcFile != srcFile))
		{
			needsNewCtx = true;
			for (int ctxIdx = 0; ctxIdx < (int)mCurRecord->mContexts.size(); ctxIdx++)
			{
				auto& ctx = mCurRecord->mContexts[ctxIdx];
				if ((ctx.mInlinee == inlinee) && (ctx.mSrcFile == srcFile))
				{
					needsNewCtx = false;
					mCurRecord->mCurContext = ctxIdx;
					break;
				}
			}
		}
	}

	if (needsNewCtx)
	{
		DbgLineInfoCtx ctx;
		ctx.mInlinee = inlinee;
		ctx.mSrcFile = srcFile;
		if (inlinee != NULL)
			mCurRecord->mHasInlinees = true;
		mCurRecord->mContexts.Add(ctx);
		mCurRecord->mCurContext = (int)mCurRecord->mContexts.size() - 1;
	}

	lineData.mCtxIdx = mCurRecord->mCurContext;

	if ((mCurSubprogram->mPrologueSize > 0) && (mCurRecord->mLines.size() == 1) && (inlinee == NULL))
	{
		auto& firstLine = mCurRecord->mLines[0];
		auto dbgStartAddr = firstLine.mRelAddress + mCurSubprogram->mPrologueSize;
		if (lineData.mRelAddress != dbgStartAddr)
		{
			DbgLineData dbgStartLine = firstLine;
			dbgStartLine.mRelAddress = dbgStartAddr;
			mCurRecord->mLines.Add(dbgStartLine);
		}

		firstLine.mColumn = -2; // Marker for 'in prologue'
	}

	if (inlinee != NULL)
	{
		if (inlinee->mInlineeInfo->mFirstLineData.mRelAddress == 0)
			inlinee->mInlineeInfo->mFirstLineData = lineData;
		inlinee->mInlineeInfo->mLastLineData = lineData;
	}

	mCurRecord->mLines.Add(lineData);
	return &mCurRecord->mLines.back();
}

void DbgLineDataBuilder::Commit()
{
	HashSet<DbgSrcFile*> usedSrcFiles;

	for (auto& recordKV : mRecords)
	{
		auto dbgSubprogram = recordKV.mKey;
		auto record = recordKV.mValue;

		usedSrcFiles.Clear();
		for (auto& ctx : record->mContexts)
		{
			if (usedSrcFiles.Add(ctx.mSrcFile))
			{
				ctx.mSrcFile->mLineDataRefs.Add(dbgSubprogram);
			}
		}

		for (int lineIdx = 0; lineIdx < (int)record->mLines.size() - 1; lineIdx++)
		{
			auto& lineData = record->mLines[lineIdx];
			auto& nextLineData = record->mLines[lineIdx + 1];

			if ((lineData.mContribSize == 0) && (lineData.mCtxIdx == nextLineData.mCtxIdx))
			{
				lineData.mContribSize = (uint32)(nextLineData.mRelAddress - lineData.mRelAddress);
			}

			bool sameInliner = lineData.mCtxIdx == nextLineData.mCtxIdx;
			if (!sameInliner)
			{
				auto ctx = record->mContexts[lineData.mCtxIdx];
				auto nextCtx = record->mContexts[lineData.mCtxIdx];
				sameInliner = ctx.mInlinee == nextCtx.mInlinee;
			}
			if ((sameInliner) && (lineData.mRelAddress + lineData.mContribSize < nextLineData.mRelAddress))
			{
				auto ctx = record->mContexts[lineData.mCtxIdx];
				if (ctx.mInlinee != NULL)
					ctx.mInlinee->mHasLineAddrGaps = true;
			}
		}

		DbgLineData* lastLine = NULL;
		for (int lineIdx = 0; lineIdx < (int)record->mLines.size(); lineIdx++)
		{
			auto& lineData = record->mLines[lineIdx];
			if (lineData.mContribSize == 0)
			{
				auto ctx = record->mContexts[lineData.mCtxIdx];
				if (ctx.mInlinee == NULL)
					lastLine = &lineData;
			}
		}
		if (lastLine != NULL)
			lastLine->mContribSize = (uint32)(dbgSubprogram->mBlock.mHighPC - (mDbgModule->mImageBase + lastLine->mRelAddress));

		BF_ASSERT(dbgSubprogram->mLineInfo == NULL);
		dbgSubprogram->mLineInfo = mDbgModule->mAlloc.Alloc<DbgLineInfo>();
		dbgSubprogram->mLineInfo->mLines.CopyFrom(&record->mLines[0], (int)record->mLines.size(), mDbgModule->mAlloc);

		BfSizedArray<DbgLineInfoCtx> contexts;
		contexts.CopyFrom(&record->mContexts[0], (int)record->mContexts.size(), mDbgModule->mAlloc);
		dbgSubprogram->mLineInfo->mContexts = contexts.mVals;

		dbgSubprogram->mLineInfo->mHasInlinees = record->mHasInlinees;
	}
}

//////////////////////////////////////////////////////////////////////////

static const char* DataGetString(const uint8*& data)
{
	const char* prevVal = (const char*)data;
	while (*data != 0)
		data++;
	data++;
	return prevVal;
}

struct AbstractOriginEntry
{
public:
	int mClassType;
	DbgDebugData* mDestination;
	DbgDebugData* mAbstractOrigin;

private:
	AbstractOriginEntry()
	{
	}

public:
	static AbstractOriginEntry Create(int classType, DbgDebugData* destination, DbgDebugData* abstractOrigin)
	{
		AbstractOriginEntry abstractOriginEntry;
		abstractOriginEntry.mClassType = classType;
		abstractOriginEntry.mDestination = destination;
		abstractOriginEntry.mAbstractOrigin = abstractOrigin;
		return abstractOriginEntry;
	}

	void Replace()
	{
		if (mClassType == DbgSubprogram::ClassType)
		{
			DbgSubprogram* destSubprogram = (DbgSubprogram*)mDestination;
			DbgSubprogram* originSubprogram = (DbgSubprogram*)mAbstractOrigin;
			if (destSubprogram->mName == NULL)
			{
				destSubprogram->mName = originSubprogram->mName;
				destSubprogram->mParentType = originSubprogram->mParentType;
			}

			destSubprogram->mHasThis = originSubprogram->mHasThis;
			if (destSubprogram->mFrameBaseData == NULL)
			{
				destSubprogram->mFrameBaseData = originSubprogram->mFrameBaseData;
				destSubprogram->mFrameBaseLen = originSubprogram->mFrameBaseLen;
			}
			destSubprogram->mReturnType = originSubprogram->mReturnType;

			auto originItr = originSubprogram->mParams.begin();
			for (auto destParam : destSubprogram->mParams)
			{
				DbgVariable* originParam = *originItr;
				if (originParam != NULL)
				{
					if (destParam->mName == NULL)
						destParam->mName = originParam->mName;
					if (destParam->mType == NULL)
						destParam->mType = originParam->mType;
				}
				++originItr;
			}
			//BF_ASSERT(originItr == originSubprogram->mParams.end());
		}
		else if (mClassType == DbgVariable::ClassType)
		{
			DbgVariable* destVariable = (DbgVariable*)mDestination;
			DbgVariable* originVariable = (DbgVariable*)mAbstractOrigin;
			if (destVariable->mName == NULL)
				destVariable->mName = originVariable->mName;
			if (destVariable->mType == NULL)
				destVariable->mType = originVariable->mType;
		}
		else
		{
			BF_FATAL("Unhandled");
		}
	}
};

NS_BF_DBG_END

//////////////////////////////////////////////////////////////////////////

void DbgSubprogram::ToString(StringImpl& str, bool internalName)
{
	if ((mInlineeInfo != NULL) && (mInlineeInfo->mInlineeId != 0))
		mCompileUnit->mDbgModule->FixupInlinee(this);

	PopulateSubprogram();

	if (mCheckedKind == BfCheckedKind_Checked)
		str += "[Checked] ";
	else if (mCheckedKind == BfCheckedKind_Unchecked)
		str += "[Unchecked] ";

	auto language = GetLanguage();
	if (mName == NULL)
	{
		if (mLinkName[0] == '<')
		{
			str += mLinkName;
			return;
		}
		str = BfDemangler::Demangle(StringImpl::MakeRef(mLinkName), language);
		// Strip off the params since we need to generate those ourselves
		int parenPos = (int)str.IndexOf('(');
		if (parenPos != -1)
			str = str.Substring(0, parenPos);
	}
	else if ((mHasQualifiedName) && (!internalName))
	{
		const char* cPtr = mName;
		if (strncmp(cPtr, "_bf::", 5) == 0)
		{
			cPtr += 5;
			for ( ; true; cPtr++)
			{
				char c = *cPtr;
				if (c == 0)
					break;

				if ((c == '_') && (cPtr[-1] == ':'))
				{
					if (strcmp(cPtr, "__BfCtor") == 0)
					{
						str += "this";
						break;
					}
					if (strcmp(cPtr, "__BfStaticCtor") == 0)
					{
						str += "this$static";
						break;
					}
					if (strcmp(cPtr, "__BfCtorClear") == 0)
					{
						str += "this$clear";
						break;
					}
				}

				if ((c == ':') && (cPtr[1] == ':'))
				{
					str.Append('.');
					cPtr++;
				}
				else
					str.Append(c);
			}
		}
		else
			str += mName;
	}
	else
	{
		if (mParentType != NULL)
		{
			mParentType->ToString(str, language, true, internalName);
			if (!str.empty())
			{
				if (language == DbgLanguage_Beef)
					str += ".";
				else
					str += "::";
			}
		}

		const char* name = mName;
		if (mHasQualifiedName)
		{
			const char* cPtr = name;
			for (; true; cPtr++)
			{
				char c = *cPtr;
				if (c == 0)
					break;
				if ((c == ':') && (cPtr[1] == ':'))
				{
					name = cPtr + 2;
				}
			}
		}

		if ((language == DbgLanguage_Beef) && (mParentType != NULL) && (mParentType->mTypeName != NULL) && (strcmp(name, mParentType->mTypeName) == 0))
			str += "this";
		else if ((language == DbgLanguage_Beef) && (name[0] == '~'))
			str += "~this";
		else if (strncmp(name, "_bf::", 5) == 0)
			str += name + 5;
		else
		{
			bool handled = false;
			if ((language == DbgLanguage_Beef) && (name[0] == '_'))
			{
				if (strcmp(name, "__BfCtor") == 0)
				{
					str += "this";
					handled = true;
				}
				else if (strcmp(name, "__BfStaticCtor") == 0)
				{
					str += "this";
					handled = true;
				}
				else if (strcmp(name, "__BfCtorClear") == 0)
				{
					str += "this$clear";
					handled = true;
				}
			}
			if (!handled)
				str += name;
		}
	}

	//if (mTemplateName != NULL)
		//str += mTemplateName;

	if (str.empty())
		str += "`anon";
	if ((str[str.length() - 1] == '!') || (str[0] == '<'))
	{
		if (language == DbgLanguage_Beef)
		{
			// It's a mixin - assert that there's no params
			//BF_ASSERT(mParams.Size() == 0);
		}
		//return str;
	}

	str += "(";

	bool showedParam = false;
	int i = 0;
	for (auto variable : mParams)
	{
		if ((variable->mName != NULL) && (strcmp(variable->mName, "this") == 0))
			continue;
		if (showedParam)
			str += ", ";
		if (variable->mType != NULL)
		{
			auto varType = variable->mType;
			if (varType->mTypeCode == DbgType_Const)
				varType = varType->mTypeParam;
			if (variable->mSigNoPointer)
			{
				BF_ASSERT(varType->IsPointer());
				varType = varType->mTypeParam;
			}
			varType->ToString(str, language, false, internalName);
			if (variable->mName != NULL)
				str += " ";
		}
		if (variable->mName != NULL)
			str += variable->mName;
		showedParam = true;
		i++;
	}
	str += ")";
}

String DbgSubprogram::ToString()
{
	String str;
	ToString(str, false);
	return str;
}

// For inlined subprograms, the "root" inliner means the bottom-most non-inlined function.  This subprogram contains
//  all the line data for it's own non-inlined instructions, PLUS line data for all inlined functions that it calls.
//  The inlined functions has empty mLineInfo structures.
//
// When we pass a non-NULL value into inlinedSubprogram, we are requesting to ONLY return lines that were emitted from
//  that subprogram (inlined or not).
//
// If we call FindClosestLine on an inlined subprogram, we only want results of functions that are inside or inlined by
//  the 'this' subprogram.  Thus, we do a "get any line" call on the root inliner and then filter the results based
//  on whether they are relevant.
DbgLineData* DbgSubprogram::FindClosestLine(addr_target addr, DbgSubprogram** inlinedSubprogram, DbgSrcFile** srcFile, int* outLineIdx)
{
	if (mLineInfo == NULL)
	{
		if (mInlineeInfo == NULL)
			return NULL;

		if ((inlinedSubprogram != NULL) && (*inlinedSubprogram != NULL))
		{
			// Keep explicit inlinee requirement
			return mInlineeInfo->mRootInliner->FindClosestLine(addr, inlinedSubprogram, srcFile, outLineIdx);
		}
		else
		{
			DbgSubprogram* rootInlinedSubprogram = NULL;
			auto result = mInlineeInfo->mRootInliner->FindClosestLine(addr, &rootInlinedSubprogram, srcFile, outLineIdx);
			if (result == NULL)
				return NULL;
			if (rootInlinedSubprogram == NULL) // Do not allow root parent, as we cannot be a parent to the root parent (duh)
				return NULL;

			// We need to check to see if we are a parent of the found line
			auto checkSubprogram = rootInlinedSubprogram;
			while ((checkSubprogram != NULL) && (checkSubprogram->mInlineeInfo != NULL))
			{
				if (checkSubprogram == this)
				{
					if (inlinedSubprogram != NULL)
						*inlinedSubprogram = rootInlinedSubprogram;
					return result;
				}
				checkSubprogram = checkSubprogram->mInlineeInfo->mInlineParent;
			}

			return NULL;
		}
	}

	// Binary search - lineData is sorted
	int first = 0;
	int last = (int)mLineInfo->mLines.mSize - 1;
	int middle = (first + last) / 2;

	int useIdx = -1;

	while (first <= last)
	{
		addr_target midAddr = (addr_target)(mLineInfo->mLines.mVals[middle].mRelAddress + mCompileUnit->mDbgModule->mImageBase);
		if (midAddr < addr)
			first = middle + 1;
		else if (midAddr == addr)
		{
			useIdx = middle;
			break;
		}
		else
			last = middle - 1;

		middle = (first + last) / 2;
	}

	if (useIdx == -1)
		useIdx = last;

	if (last == -1)
		return NULL;

	// If we have lines with the same addr, take the more inner one
	while (true)
	{
		auto lineData = &mLineInfo->mLines.mVals[useIdx];
		if (useIdx + 1 < mLineInfo->mLines.mSize)
		{
			auto peekNext = &mLineInfo->mLines.mVals[useIdx + 1];
			if (lineData->mRelAddress != peekNext->mRelAddress)
				break;
			useIdx++;
		}
		else
		{
			break;
		}
	}

	while (true)
	{
		auto lineData = &mLineInfo->mLines.mVals[useIdx];

		if (addr < lineData->mRelAddress + lineData->mContribSize + mCompileUnit->mDbgModule->mImageBase)
		{
			auto& ctx = mLineInfo->mContexts[lineData->mCtxIdx];
			if (srcFile != NULL)
				*srcFile = ctx.mSrcFile;

			if (inlinedSubprogram != NULL)
			{
				auto subprogram = (ctx.mInlinee != NULL) ? ctx.mInlinee : this;
				if (*inlinedSubprogram != NULL)
				{
					// Strictness check
					if (subprogram == *inlinedSubprogram)
					{
						if (outLineIdx != NULL)
							*outLineIdx = useIdx;
						return lineData;
					}
				}
				else
				{
					*inlinedSubprogram = subprogram;
					if (outLineIdx != NULL)
						*outLineIdx = useIdx;
					return lineData;
				}
			}
			else
			{
				if (outLineIdx != NULL)
					*outLineIdx = useIdx;
				return lineData;
			}
		}

		// Hope we can find an earlier entry whose "contribution" is still valid
		if (--useIdx < 0)
			break;
	}

	return NULL;
}

DbgType* DbgSubprogram::GetParent()
{
	if ((mParentType == NULL) && (mCompileUnit != NULL))
		mCompileUnit->mDbgModule->MapCompileUnitMethods(mCompileUnit);
	return mParentType;
}

DbgType* DbgSubprogram::GetTargetType()
{
	if (!mHasThis)
		return mParentType;
	auto thisType = mParams.mHead->mType;
	if (thisType == NULL)
		return mParentType;
	if (thisType->IsPointer())
		return thisType->mTypeParam;
	return thisType;
}

DbgLanguage DbgSubprogram::GetLanguage()
{
	if (mParentType != NULL)
		return mParentType->GetLanguage();
	if (mCompileUnit->mLanguage != DbgLanguage_Unknown)
		return mCompileUnit->mLanguage;
	return DbgLanguage_C; // Parent type would have been set for Beef, so it must be C
}

bool DbgSubprogram::Equals(DbgSubprogram* checkMethod, bool allowThisMismatch)
{
	if ((mLinkName != NULL) && (checkMethod->mLinkName != NULL))
	{
		return strcmp(mLinkName, checkMethod->mLinkName) == 0;
	}

	if (strcmp(mName, checkMethod->mName) != 0)
		return false;

	if (mHasThis != checkMethod->mHasThis)
		return false;

	int paramIdx = 0;
	auto param = mParams.mHead;
	auto checkParam = checkMethod->mParams.mHead;
	while ((param != NULL) && (checkParam != NULL))
	{
		if ((paramIdx == 0) && (allowThisMismatch))
		{
			// Allow
		}
		else if ((param->mType != checkParam->mType) && (!param->mType->Equals(checkParam->mType)))
			return false;
		param = param->mNext;
		checkParam = checkParam->mNext;
		paramIdx++;
	}

	if ((param != NULL) || (checkParam != NULL))
		return false;

	if (!mReturnType->Equals(checkMethod->mReturnType))
		return false;

	return true;
}

int DbgSubprogram::GetParamCount()
{
	int paramCount = mParams.Size();
	if (mHasThis)
		paramCount--;
	return paramCount;
}

String DbgSubprogram::GetParamName(int paramIdx)
{
	auto param = mParams[paramIdx];
	if (param->mName != NULL)
	{
		String name = "'";
		name += param->mName;
		name += "'";
		return name;
	}
	return StrFormat("%d", paramIdx + 1);
}

bool DbgSubprogram::IsGenericMethod()
{
	if (mName == NULL)
		return false;

	for (const char* cPtr = mName; true; cPtr++)
	{
		char c = *cPtr;
		if (c == '\0')
			break;
		if (c == '<')
			return true;
	}

	return false;
}

bool DbgSubprogram::ThisIsSplat()
{
	if (mBlock.mVariables.mHead == NULL)
		return false;
	return strncmp(mBlock.mVariables.mHead->mName, "$this$", 6) == 0;
}

bool DbgSubprogram::IsLambda()
{
	if (mName == NULL)
		return false;
	return StringView(mName).Contains('$');
}

//////////////////////////////////////////////////////////////////////////

DbgSubprogram::~DbgSubprogram()
{
	BfLogDbg("DbgSubprogram::~DbgSubprogram %p\n", this);
}

////////////////////

bool DbgSrcFile::IsBeef()
{
	int dotPos = (int)mFilePath.LastIndexOf('.');
	if (dotPos == -1)
		return false;
	const char* ext = mFilePath.c_str() + dotPos;
	// The ".cs" is legacy.  Remove that eventually.
	return (stricmp(ext, ".bf") == 0) || (stricmp(ext, ".cs") == 0);
}

DbgSrcFile::~DbgSrcFile()
{
	for (auto replacedLineInfo : mHotReplacedDbgLineInfo)
		delete replacedLineInfo;
}

void DbgSrcFile::RemoveDeferredRefs(DbgModule* debugModule)
{
	for (int deferredIdx = 0; deferredIdx < (int)mDeferredRefs.size(); )
	{
		if (mDeferredRefs[deferredIdx].mDbgModule == debugModule)
		{
			// Fast remove
			mDeferredRefs[deferredIdx] = mDeferredRefs.back();
			mDeferredRefs.pop_back();
		}
		else
			deferredIdx++;
	}
}

void DbgSrcFile::RemoveLines(DbgModule* debugModule)
{
 	if (!mHasLineDataFromMultipleModules)
 	{
 		// Fast-out case
 		mLineDataRefs.Clear();
 		return;
 	}

	for (int idx = 0; idx < (int)mLineDataRefs.size(); idx++)
	{
		auto dbgSubprogram = mLineDataRefs[idx];
		if (dbgSubprogram->mCompileUnit->mDbgModule == debugModule)
		{
			mLineDataRefs.RemoveAtFast(idx);
			idx--;
		}
	}
}

void DbgSrcFile::RemoveLines(DbgModule* debugModule, DbgSubprogram* dbgSubprogram, bool isHotReplaced)
{
	debugModule->mDebugTarget->mPendingSrcFileRehup.Add(this);

	if (isHotReplaced)
	{
		int vecIdx = dbgSubprogram->mCompileUnit->mDbgModule->mHotIdx;
		BF_ASSERT(vecIdx >= 0);
 		while (vecIdx >= (int)mHotReplacedDbgLineInfo.size())
			mHotReplacedDbgLineInfo.push_back(new HotReplacedLineInfo());

		auto hotReplacedLineInfo = mHotReplacedDbgLineInfo[vecIdx];

		HotReplacedLineInfo::Entry entry;
		entry.mSubprogram = dbgSubprogram;
		entry.mLineInfo = dbgSubprogram->mLineInfo;
		hotReplacedLineInfo->mEntries.Add(entry);
	}
}

void DbgSrcFile::RehupLineData()
{
	for (int idx = 0; idx < (int)mLineDataRefs.size(); idx++)
	{
		auto dbgSubprogram = mLineDataRefs[idx];
		if (dbgSubprogram->mHotReplaceKind != DbgSubprogram::HotReplaceKind_None)
		{
			mLineDataRefs.RemoveAtFast(idx);
			idx--;
		}
	}
}

void DbgSrcFile::VerifyPath()
{
	if (mVerifiedPath)
		return;

	if (mLineDataRefs.IsEmpty())
		return;

	if (!::FileExists(mFilePath))
	{
		bool didReplace = false;
		for (auto& kv : gDebugManager->mSourcePathRemap)
		{
			if (mFilePath.StartsWith(kv.mKey, StringImpl::CompareKind_OrdinalIgnoreCase))
			{
				mFilePath.Remove(0, kv.mKey.mLength);
				mFilePath.Insert(0, kv.mValue);
				didReplace = true;
			}
		}

		if (!didReplace)
		{
			HashSet<DbgModule*> checkedModules;
			for (auto& lineDataRef : mLineDataRefs)
			{
				auto dbgModule = lineDataRef->mCompileUnit->mDbgModule;
				if (checkedModules.Add(dbgModule))
				{
					if (dbgModule->mDbgFlavor == DbgFlavor_MS)
					{
						COFF* coff = (COFF*)dbgModule;

						if ((!coff->mOrigPDBPath.IsEmpty()) && (!coff->mOrigPDBPath.Equals(coff->mPDBPath, StringImpl::CompareKind_OrdinalIgnoreCase)))
						{
							String relFilePath = GetRelativePath(mFilePath, coff->mOrigPDBPath);
							String checkActualFilePath = GetAbsPath(relFilePath, coff->mPDBPath);

							if (FileExists(checkActualFilePath))
							{
								mFilePath = checkActualFilePath;
								break;
							}
						}
					}
				}
			}
		}
	}

	mVerifiedPath = true;
}

const String& DbgSrcFile::GetLocalPath()
{
	if (!mVerifiedPath)
		VerifyPath();
	return (!mLocalPath.IsEmpty()) ? mLocalPath : mFilePath;
}

void DbgSrcFile::GetHash(String& outStr)
{
	if (mHashKind == DbgHashKind_MD5)
	{
		for (int i = 0; i < 16; i++)
		{
			outStr += StrFormat("%02X", mHash[i]);
		}
	}
	else if (mHashKind == DbgHashKind_SHA256)
	{
		for (int i = 0; i < 32; i++)
		{
			outStr += StrFormat("%02X", mHash[i]);
		}
	}
}

//////////////////////////////////////////////////////////////////////////

DbgType::DbgType()
{
	mTypeIdx = -1;
	mIsDeclaration = false;
	mParent = NULL;
	mTypeName = NULL;
	mTypeCode = DbgType_Null;
	mSize = 0;
	mPtrType = NULL;
	mTypeParam = NULL;
	mBlockParam = NULL;
	mNext = NULL;
	mPriority = DbgTypePriority_Normal;
}

DbgType::~DbgType()
{
	BfLogDbg("DbgType::~DWType %p\n", this);
}

DbgType* DbgType::ResolveTypeDef()
{
	if (mTypeCode == DbgType_TypeDef)
		return mTypeParam->ResolveTypeDef();
	return this;
}

bool DbgType::Equals(DbgType* dbgType)
{
	if (dbgType == NULL)
		return false;
	if (mTypeCode != dbgType->mTypeCode)
	{
		if ((mTypeCode == DbgType_Enum) || (dbgType->mTypeCode == DbgType_Enum))
		{
			// These may change mTypeCode, so redo the check afterward
			GetPrimaryType();
			dbgType->GetPrimaryType();
		}

		if (mTypeCode != dbgType->mTypeCode)
			return false;
	}
	if ((mName == NULL) != (dbgType->mName == NULL))
		return false;
	if (mName != NULL)
    {
		if (dbgType->mFixedName)
			FixName();
		else if (mFixedName)
			dbgType->FixName();
        if (strcmp(mName, dbgType->mName) != 0)
			return false;
	}
	if ((mTypeParam != NULL) && (!mTypeParam->Equals(dbgType->mTypeParam)))
		return false;

	// Did mName already include the parent name?
	if (mCompileUnit->mDbgModule->mDbgFlavor == DbgFlavor_MS)
		return true;

	if ((mParent != NULL) != (dbgType->mParent != NULL))
		return false;
	if (mParent != NULL)
		return mParent->Equals(dbgType->mParent);
	return true;
}

bool DbgType::IsStruct()
{
	return mTypeCode == DbgType_Struct;
}

bool DbgType::IsPrimitiveType()
{
	return (mTypeCode >= DbgType_i8) && (mTypeCode <= DbgType_Bool);
}

bool DbgType::IsNull()
{
	return mTypeCode == DbgType_Null;
}

bool DbgType::IsVoid()
{
	return (mTypeCode == DbgType_Void);
}

bool DbgType::IsValuelessType()
{
	return ((mTypeCode == DbgType_Struct) && (GetByteCount() == 0)) || (mTypeCode == DbgType_Void);
}

bool DbgType::IsValueType()
{
	return (mTypeCode <= DbgType_DefinitionEnd);
}

bool DbgType::IsTypedPrimitive()
{
	PopulateType();

	if (mTypeCode != DbgType_Struct)
		return false;

	if (mTypeParam != NULL)
		return true;

	auto baseType = GetBaseType();
	if (baseType == NULL)
		return false;

	if (!baseType->IsTypedPrimitive())
		return false;

	mTypeParam = baseType->mTypeParam;
	return true;
}

bool DbgType::IsBoolean()
{
	return mTypeCode == DbgType_Bool;
}

bool DbgType::IsInteger()
{
	return (mTypeCode >= DbgType_i8) && (mTypeCode <= DbgType_u64);
}

bool DbgType::IsIntegral()
{
	return ((mTypeCode >= DbgType_i8) && (mTypeCode <= DbgType_u64)) ||
		((mTypeCode >= DbgType_SChar) && (mTypeCode <= DbgType_UChar32));
}

bool DbgType::IsChar()
{
	return (mTypeCode >= DbgType_SChar) && (mTypeCode <= DbgType_UChar32);
}

bool DbgType::IsChar(DbgLanguage language)
{
	if (language == DbgLanguage_Beef)
		return (mTypeCode >= DbgType_UChar) && (mTypeCode <= DbgType_UChar32);
	return (mTypeCode >= DbgType_SChar) && (mTypeCode <= DbgType_SChar32);
}

bool DbgType::IsFloat()
{
	return (mTypeCode == DbgType_Single) || (mTypeCode == DbgType_Double);
}

// "Struct" in this sense means that we do NOT have a pointer to this value, but it may or may not be a Beef Struct
bool DbgType::IsCompositeType()
{
	if (((mTypeCode == DbgType_TypeDef) || (mTypeCode == DbgType_Const)) && (mTypeParam != NULL))
		return mTypeParam->IsCompositeType();
	return ((mTypeCode == DbgType_Struct) || (mTypeCode == DbgType_Class) || (mTypeCode == DbgType_SizedArray));
}

bool DbgType::WantsRefThis()
{
	return (GetLanguage() == DbgLanguage_Beef) && (!IsBfObject());
}

bool DbgType::IsBfObjectPtr()
{
	if ((mTypeCode == DbgType_Ptr) && (mTypeParam != NULL))
		return mTypeParam->IsBfObject();
	return false;
}

DbgExtType DbgType::CalcExtType()
{
	auto language = GetLanguage();
	if ((!mFixedName) && (language == DbgLanguage_Beef))
	{
		FixName();
	}

	auto primaryType = GetPrimaryType();
	if (this != primaryType)
	{
		return primaryType->CalcExtType();
	}

	if (mCompileUnit == NULL)
		return DbgExtType_Normal;

	if (language != DbgLanguage_Beef)
		return DbgExtType_Normal;

	if ((mTypeCode != DbgType_Struct) && (mTypeCode != DbgType_Class))
		return DbgExtType_Normal;

	PopulateType();
	if (mExtType != DbgExtType_Unknown)
		return mExtType;

	auto baseType = GetBaseType();
	if (baseType == NULL)
	{
		if (mParent == NULL)
			return DbgExtType_Normal;
		if (mParent->mTypeCode != DbgType_Namespace)
			return DbgExtType_Normal;
		if (mParent->mParent != NULL)
			return DbgExtType_Normal;
		if (strcmp(mParent->mTypeName, "System") != 0)
			return DbgExtType_Normal;
		if (strcmp(mTypeName, "Object") != 0)
			return DbgExtType_Normal;
		return DbgExtType_BfObject;
	}
	else
	{
		if (strcmp(baseType->mTypeName, "Enum") == 0)
		{
			for (auto member : mMemberList)
			{
				if ((member->mName != NULL) && (strcmp(member->mName, "__bftag") == 0))
					return DbgExtType_BfPayloadEnum;
			}
			return DbgExtType_Normal;
		}
		else if (strcmp(baseType->mTypeName, "ValueType") == 0)
		{
			for (auto member : mMemberList)
			{
				if ((member->mName != NULL) && (strcmp(member->mName, "$bfunion") == 0))
					return DbgExtType_BfUnion;
			}
		}
	}

	auto baseExtType = baseType->CalcExtType();
	if ((baseExtType == DbgExtType_BfObject) && (GetByteCount() == 0))
		baseExtType = DbgExtType_Interface;
	return baseExtType;
}

DbgLanguage DbgType::GetLanguage()
{
	return mLanguage;
}

void DbgType::FixName()
{
	if (mFixedName)
		return;

	int depthCount = 0;

	auto dbgModule = mCompileUnit->mDbgModule;
	if ((dbgModule->mDbgFlavor == DbgFlavor_MS) && (mName != NULL) && (strlen(mName) > 0))
	{
		bool modified = false;

		if (!dbgModule->DbgIsStrMutable(mName))
			mName = dbgModule->DbgDupString(mName);

		const char* typeNamePtr = mTypeName;
		char* nameP = (char*)mName;

		// Fix the name
		char* inPtr = nameP;
		char* outPtr = nameP;

		while (true)
		{
			char c = *(inPtr++);
			if ((c == '<') || (c == '('))
				depthCount++;
			else if ((c == '>') || (c == ')'))
				depthCount--;

			if ((c == ':') && (inPtr[0] == ':'))
			{
				if (mLanguage == DbgLanguage_Beef)
				{
					modified = true;
					inPtr++;
					*(outPtr++) = '.';
					if (depthCount == 0)
						typeNamePtr = outPtr;
				}
				else if (depthCount == 0)
					mTypeName = inPtr + 1;
			}
			else if (modified)
				*(outPtr++) = c;
			else
				outPtr++;
			if (c == 0)
				break;
		}

		if ((modified) && (mName != mTypeName) && (typeNamePtr != NULL))
		{
			mTypeName = typeNamePtr;
		}
	}

	mFixedName = true;
}

bool DbgType::IsBfObject()
{
	if (mExtType == DbgExtType_Unknown)
		mExtType = CalcExtType();
	return (mExtType == DbgExtType_BfObject) || (mExtType == DbgExtType_Interface);
}

bool DbgType::IsBfPayloadEnum()
{
	if (mExtType == DbgExtType_Unknown)
		mExtType = CalcExtType();
	return mExtType == DbgExtType_BfPayloadEnum;
}

bool DbgType::IsBfUnion()
{
	if (mExtType == DbgExtType_Unknown)
		mExtType = CalcExtType();
	return mExtType == DbgExtType_BfUnion;
}

bool DbgType::IsBfEnum()
{
	if (mTypeCode != DbgType_Struct)
		return false;

	auto baseType = GetBaseType();
	if (baseType == NULL)
	{
		if (mParent == NULL)
			return false;
		if (mParent->mTypeCode != DbgType_Namespace)
			return false;
		if (mParent->mParent != NULL)
			return false;
		if (strcmp(mParent->mTypeName, "System") != 0)
			return false;
		return strcmp(mTypeName, "Enum") == 0;
	}

	return baseType->IsBfEnum();
}

bool DbgType::IsBfTuple()
{
	if (mTypeCode != DbgType_Struct)
		return false;
	if (GetLanguage() != DbgLanguage_Beef)
		return false;
	if (mName == NULL)
		return false;
	return mName[0] == '(';
}

bool DbgType::HasCPPVTable()
{
	if ((mTypeCode != DbgType_Struct) && (mTypeCode != DbgType_Class))
		return false;

	/*if (!mMemberList.IsEmpty())
	{
		//TODO: We commented this out at some point- why did we do that?
		if ((mMemberList.mHead->mName != NULL) && (strncmp(mMemberList.mHead->mName, "_vptr$", 6) == 0))
			return true;
	}*/
	if (mHasVTable)
		return true;

	if (GetLanguage() == DbgLanguage_Beef)
		return false;

	for (auto checkBaseType : mBaseTypes)
	{
		if (checkBaseType->mBaseType->HasCPPVTable())
			return true;
	}
	return false;
}

bool DbgType::IsBaseBfObject()
{
	auto baseType = GetBaseType();
	return (baseType == NULL) && (IsBfObject());
}

bool DbgType::IsInterface()
{
	if (mExtType == DbgExtType_Unknown)
		mExtType = CalcExtType();
	return mExtType == DbgExtType_Interface;
}

bool DbgType::IsNamespace()
{
	return mTypeCode == DbgType_Namespace;
}

bool DbgType::IsEnum()
{
	return (mTypeCode == DbgType_Enum);
}

bool DbgType::IsRoot()
{
	return (mTypeCode == DbgType_Root);
}

bool DbgType::IsRef()
{
	return
		(mTypeCode == DbgType_Ref) ||
		(mTypeCode == DbgType_RValueReference);
}

bool DbgType::IsSigned()
{
	return
		(mTypeCode == DbgType_i8) ||
		(mTypeCode == DbgType_i16) ||
		(mTypeCode == DbgType_i32) ||
		(mTypeCode == DbgType_i64);
}

bool DbgType::IsConst()
{
	if ((mTypeCode == DbgType_Ptr) || (mTypeCode == DbgType_Ref))
	{
		if (mTypeParam != NULL)
        	return mTypeParam->IsConst();
	}
	return mTypeCode == DbgType_Const;
}

bool DbgType::IsPointer(bool includeBfObjectPointer)
{
	if (mTypeCode != DbgType_Ptr)
		return false;
	if ((!includeBfObjectPointer) && (mTypeParam != NULL) && (mTypeParam->IsBfObject()))
		return false;
	return true;
}

bool DbgType::HasPointer(bool includeBfObjectPointer)
{
	if (((mTypeCode == DbgType_Const) || (mTypeCode == DbgType_Ref)) && (mTypeParam != NULL))
		return mTypeParam->IsPointer(includeBfObjectPointer);
	return IsPointer(includeBfObjectPointer);
}

bool DbgType::IsPointerOrRef(bool includeBfObjectPointer)
{
	if ((mTypeCode != DbgType_Ptr) && (mTypeCode != DbgType_Ref) && (mTypeCode != DbgType_RValueReference))
		return false;
	if ((!includeBfObjectPointer) && (mTypeParam != NULL) && (mTypeParam->IsBfObject()))
		return false;
	return true;
}

bool DbgType::IsSizedArray()
{
	return (mTypeCode == DbgType_SizedArray);
}

bool DbgType::IsAnonymous()
{
	return (mTypeName == NULL) || (mTypeName[0] == '<');
}

bool DbgType::IsGlobalsContainer()
{
	return (mTypeName != NULL) && (mTypeName[0] == 'G') && (mTypeName[1] == '$');
}

DbgType* DbgType::GetUnderlyingType()
{
	return mTypeParam;
}

void DbgType::PopulateType()
{
	if (mIsIncomplete)
	{
		mCompileUnit->mDbgModule->PopulateType(this);
		mIsIncomplete = false;
	}
}

DbgModule* DbgType::GetDbgModule()
{
	if (mCompileUnit == NULL)
		return NULL;
	return mCompileUnit->mDbgModule;
}

DbgType* DbgType::GetPrimaryType()
{
	if (mPrimaryType != NULL)
		return mPrimaryType;

	mPrimaryType = this;
	if (mPriority <= DbgTypePriority_Normal)
	{
		if ((mCompileUnit != NULL) &&
			((mCompileUnit->mLanguage == DbgLanguage_Beef)|| (mLanguage == DbgLanguage_Beef) ||
			(mTypeCode == DbgType_Namespace) || (mIsDeclaration)))
		{
			mPrimaryType = mCompileUnit->mDbgModule->GetPrimaryType(this);
			mPrimaryType->PopulateType();
			mTypeCode = mPrimaryType->mTypeCode;
			mTypeParam = mPrimaryType->mTypeParam;
		}
	}

	return mPrimaryType;
}

DbgType* DbgType::GetBaseType()
{
	auto primaryType = GetPrimaryType();
	if (primaryType != this)
		return primaryType->GetBaseType();

	PopulateType();
	if (mBaseTypes.mHead == NULL)
		return NULL;
	if (GetLanguage() != DbgLanguage_Beef)
		return NULL;
	auto baseType = mBaseTypes.mHead->mBaseType;
	BF_ASSERT(!baseType->IsInterface());

	if ((baseType == NULL) || (baseType->mPriority > DbgTypePriority_Normal))
		return baseType;
	baseType = mCompileUnit->mDbgModule->GetPrimaryType(baseType);
	mBaseTypes.mHead->mBaseType = baseType;

	if (baseType->mIsDeclaration)
	{
		// That's no good, try to fix it up
		if (baseType->GetLanguage() == DbgLanguage_Beef)
		{
			if (baseType->GetBaseType() == NULL)
			{
				if (baseType->ToString() == "System.Function")
				{
					DbgBaseTypeEntry* baseTypeEntry = mCompileUnit->mDbgModule->mAlloc.Alloc<DbgBaseTypeEntry>();
					baseTypeEntry->mBaseType = mCompileUnit->mDbgModule->GetPrimitiveType(DbgType_IntPtr_Alias, DbgLanguage_Beef);
					baseType->mBaseTypes.PushBack(baseTypeEntry);
				}
			}
		}
	}

	return baseType;
}

DbgType* DbgType::GetRootBaseType()
{
	auto baseType = GetBaseType();
	if (baseType != NULL)
		return baseType->GetRootBaseType();
	return this;
}

DbgType* DbgType::RemoveModifiers(bool* hadRef)
{
	DbgType* dbgType = this;
	while (dbgType != NULL)
	{
		bool curHadRef = (dbgType->mTypeCode == DbgType_Ref) || (dbgType->mTypeCode == DbgType_RValueReference);
		if ((curHadRef) && (hadRef != NULL))
			*hadRef = true;

		if ((dbgType->mTypeCode == DbgType_Const) || (dbgType->mTypeCode == DbgType_TypeDef) || (dbgType->mTypeCode == DbgType_Volatile) || (dbgType->mTypeCode == DbgType_Bitfield) ||
			(dbgType->mTypeCode == DbgType_Unaligned) || (curHadRef))
		{
			if (dbgType->mTypeParam == NULL)
				break;
			dbgType = dbgType->mTypeParam;
		}
		else
			break;
	}
	return dbgType;
}

String DbgType::ToStringRaw(DbgLanguage language)
{
	if (mTypeIdx != -1)
		return StrFormat("_T_%d_%d", mCompileUnit->mDbgModule->GetLinkedModule()->mId, mTypeIdx);
	return ToString(language);
}

void DbgType::ToString(StringImpl& str, DbgLanguage language, bool allowDirectBfObject, bool internalName)
{
	if (language == DbgLanguage_Unknown)
		language = GetLanguage();

	if (language == DbgLanguage_Beef)
	{
		switch (mTypeCode)
		{
		case DbgType_UChar:
			str += "char8";
			return;
		case DbgType_UChar16:
			str += "char16";
			return;
		case DbgType_UChar32:
			str += "char32";
			return;
		case DbgType_i8:
			str += "int8";
			return;
		case DbgType_u8:
			str += "uint8";
			return;
		case DbgType_i16:
			str += "int16";
			return;
		case DbgType_u16:
			str += "uint16";
			return;
		case DbgType_i32:
			str += "int32";
			return;
		case DbgType_u32:
			str += "uint32";
			return;
		case DbgType_i64:
			str += "int64";
			return;
		case DbgType_u64:
			str += "uint64";
			return;
		}
	}
	else
	{
		switch (mTypeCode)
		{
		case DbgType_SChar:
			str += "char";
			return;
		case DbgType_SChar16:
			str += "wchar_t";
			return;
		case DbgType_SChar32:
			str += "int32_t";
			return;
		case DbgType_UChar:
			str += "uint8_t";
			return;
		case DbgType_UChar16:
			str += "uint16_t";
			return;
		case DbgType_UChar32:
			str += "uint32_t";
			return;
		case DbgType_i8:
			str += "char";
			return;
		case DbgType_u8:
			str += "uint8_t";
			return;
		case DbgType_i16:
			str += "short";
			return;
		case DbgType_u16:
			str += "uint16_t";
			return;
		case DbgType_i32:
			str += "int";
			return;
		case DbgType_u32:
			str += "uint32_t";
			return;
		case DbgType_i64:
			str += "int64_t";
			return;
		case DbgType_u64:
			str += "uint64_t";
			return;
		}
	}

	if (mTypeCode == DbgType_Namespace)
		internalName = false;

	auto parent = mParent;
	if ((parent == NULL) && (internalName))
	{
		auto primaryType = GetPrimaryType();
		parent = primaryType->mParent;
	}

	if (mTypeName != NULL)
	{
		if ((!allowDirectBfObject) && (IsBfObject()))
		{
			// Only use the '#' for testing
			//return ToString(true) + "#";
			ToString(str, DbgLanguage_Unknown, true, internalName);
			return;
		}

		if (IsGlobalsContainer())
		{
			if (mParent != NULL)
			{
				mParent->ToString(str, language, false, internalName);
				return;
			}
			return;
		}

		//String combName;
		/*if (mTemplateParams != NULL)
		{
			combName = nameP;
			combName += mTemplateParams;
			nameP = combName.c_str();
		}*/

		if ((!mFixedName) /*&& (language == DbgLanguage_Beef)*/)
		{
			FixName();
		}
		char* nameP = (char*)mTypeName;

		if (parent == NULL)
		{
			if (strncmp(nameP, "Box<", 4) == 0)
			{
				str += String(nameP + 4, nameP + strlen(nameP) - 1);
				str += "^";
				return;
			}

			// For declarations, may also include namespaces
			str += mName;
			return;
		}

		if (GetLanguage() == DbgLanguage_Beef)
		{
			parent->ToString(str, language, allowDirectBfObject, internalName);
			if ((internalName) && (parent->mTypeCode != DbgType_Namespace))
				str += "+";
			else
				str += ".";
			str += nameP;
		}
		else
		{
			parent->ToString(str, language, allowDirectBfObject, internalName);
			if ((internalName) && (parent->mTypeCode != DbgType_Namespace))
				str += "+";
			else
				str += "::";
			str += nameP;
		}
		return;
	}

	switch (mTypeCode)
	{
	case DbgType_Struct:
	{
		if ((mTypeName == NULL) && (parent != NULL))
		{
			parent->ToString(str, language, allowDirectBfObject, internalName);
			return;
		}
		str += "@struct";
		return;
	}
	case DbgType_Class:
	{
		str += "@class";
		return;
	}
	case DbgType_TypeDef:
	{
		str += "@typedef";
		return;
	}
	case DbgType_Const:
	{
		if (language == DbgLanguage_Beef)
		{
			str += "readonly";
			if (mTypeParam != NULL)
			{
				str += " ";
				mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
			}
			return;
		}

		str += "const";
		if (mTypeParam != NULL)
		{
			str += " ";
			mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		}
		return;
	}
	case DbgType_Volatile:
	{
		str += "volatile";
		if (mTypeParam != NULL)
		{
			str += " ";
			mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		}
		return;
	}
	case DbgType_Unaligned:
	{
		str += "unaligned";
		if (mTypeParam != NULL)
		{
			str += " ";
			mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		}
	}
	case DbgType_Restrict:
	{
		str += "restrict";
		if (mTypeParam != NULL)
		{
			str += " ";
			mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		}
	}
	case DbgType_Ptr:
	{
		if (mTypeParam == NULL)
		{
			str += "void*";
			return;
		}

		if (mTypeParam->IsBfObject())
		{
			mTypeParam->ToString(str, DbgLanguage_Unknown, true, internalName);
			return;
		}

		// Don't put a "*" on the end of a function type, it's implicit
		if (mTypeParam->mTypeCode == DbgType_Subroutine)
		{
			mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
			return;
		}

		mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		str += "*";
		return;
	}
	case DbgType_Ref:
	{
		if (language == DbgLanguage_Beef)
		{
			str += "ref";
			if (mTypeParam != NULL)
			{
				str += " ";
				mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
			}
			return;
		}
		if (mTypeParam == NULL)
		{
			str += "&";
			return;
		}
		mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		str += "&";
		return;
	}
	case DbgType_RValueReference:
	{
		if (language == DbgLanguage_Beef)
		{
			// Ignore this - this is used for passing structs when we're not using the 'byval' attribute
			mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
			return;
		}

		if (mTypeParam == NULL)
		{
			str += "&&";
			return;
		}
		mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		str += "&&";
		return;
	}
	case DbgType_Unspecified:
		str += mTypeName;
		return;
	case DbgType_SizedArray:
	{
		StringT<128> name;
		auto checkType = this;
		while (checkType->mTypeCode == DbgType_SizedArray)
		{
			intptr innerSize = checkType->mTypeParam->GetStride();
			intptr arrSize = 0;
			if (innerSize > 0)
			{
				arrSize = checkType->GetStride() / innerSize;
			}
			name += StrFormat("[%lld]", arrSize);
			checkType = checkType->mTypeParam;
		}
		checkType->ToString(str, language, allowDirectBfObject, internalName);
		str += name;
		return;
	}
	case DbgType_Union:
	{
		str += "union";
		if (mTypeParam != NULL)
		{
			str += " ";
			mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		}
		return;
	}
	case DbgType_Single:
		str += "float";
		return;
	case DbgType_Double:
		str += "double";
		return;
	case DbgType_Null:
		str += "void";
		return;
	case DbgType_Subroutine:
	{
		mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		str += " (";
		int paramIdx = 0;
		for (auto param : mBlockParam->mVariables)
		{
			if (paramIdx > 0)
				str += ", ";
			param->mType->ToString(str, language, allowDirectBfObject, internalName);
			paramIdx++;
		}
		str += ")";
		return;
	}
	case DbgType_VTable:
		str += "@vtable";
		return;
	case DbgType_Enum:
		str += "@enum";
		return;
	case DbgType_Namespace:
	{
		// Anonymous
		str += "`anon`";
		return;
	}
	case DbgType_PtrToMember:
		str += "@ptrToMember";
		return;
	case DbgType_Bitfield:
	{
		auto dbgBitfieldType = (DbgBitfieldType*)this;
		mTypeParam->ToString(str, language, allowDirectBfObject, internalName);
		str += StrFormat("{%d:%d}", dbgBitfieldType->mPosition, dbgBitfieldType->mLength);
		return;
	}
	default:
		break;
	}

	BF_FATAL("Unhandled type");
	str += "???";
}

String DbgType::ToString(DbgLanguage language, bool allowDirectBfObject)
{
	String str;
	ToString(str, language, allowDirectBfObject, false);
	return str;
}

intptr DbgType::GetByteCount()
{
	if (!mSizeCalculated)
	{
		PopulateType();

		if ((mSize == 0) && (GetLanguage() == DbgLanguage_Beef))
			CalcExtType();

		if ((mTypeCode == DbgType_Struct) || (mTypeCode == DbgType_Class) || (mTypeCode == DbgType_Union))
		{
			if (mPriority <= DbgTypePriority_Normal)
			{
				auto primaryType = GetPrimaryType();
				if (primaryType != this)
				{
					mSize = primaryType->GetByteCount();
					mAlign = primaryType->mAlign;
				}
			}
		}
		else if ((mTypeCode == DbgType_Ref) || (mTypeCode == DbgType_Ptr) || (mTypeCode == DbgType_PtrToMember))
		{
#ifdef BF_DBG_32
			mSize = 4;
#else
			mSize = 8;
#endif
		}
		else if (mTypeCode == DbgType_SizedArray)
		{
			auto language = GetLanguage();
			if (language == DbgLanguage_Beef)
			{
				if (mTypeParam->mAlign == 0)
				{
					NOP;
				}
				auto primaryType = mTypeParam->GetPrimaryType();
				if (primaryType->mAlign == 0)
				{
					NOP;
				}
				else
				{
					intptr elemCount = BF_ALIGN(mSize, primaryType->mAlign) / primaryType->GetStride();
					if (elemCount > 0)
					{
						mSize = ((elemCount - 1) * primaryType->GetStride()) + primaryType->GetByteCount();
					}
				}
				mAlign = primaryType->mAlign;
			}
		}
		else if (mTypeParam != NULL) // typedef, const, volatile, restrict, etc
			mSize = mTypeParam->GetByteCount();
		mSizeCalculated = true;
	}
	return mSize;
}

intptr DbgType::GetStride()
{
	return BF_ALIGN(GetByteCount(), GetAlign());
}

int DbgType::GetAlign()
{
	if (mAlign == 0)
	{
		auto primaryType = GetPrimaryType();
		if (primaryType != this)
			return primaryType->GetAlign();

		if (IsCompositeType())
		{
			PopulateType();
		}
	}

	if (mAlign != 0)
		return mAlign;
	return 1;
}

void DbgType::EnsureMethodsMapped()
{
	for (auto methodNameEntry : mMethodNameList)
	{
		if (methodNameEntry->mCompileUnitId != -1)
		{
			mCompileUnit->mDbgModule->MapCompileUnitMethods(methodNameEntry->mCompileUnitId);
			methodNameEntry->mCompileUnitId = -1;
		}
	}
}

#define CREATE_PRIMITIVE_C(typeCode, cTypeName, type) \
	dbgType = mAlloc.Alloc<DbgType>(); \
	dbgType->mCompileUnit = &mDefaultCompileUnit; \
	dbgType->mName = cTypeName; \
	dbgType->mLanguage = DbgLanguage_C;\
	dbgType->mTypeName = cTypeName; \
	dbgType->mTypeCode = typeCode; \
	dbgType->mSize = sizeof(type); \
	dbgType->mAlign = sizeof(type); \
	mCPrimitiveTypes[typeCode] = dbgType; \
	mTypeMap.Insert(dbgType);

#define CREATE_PRIMITIVE(typeCode, cTypeName, bfTypeName, structName, type) \
	dbgType = mAlloc.Alloc<DbgType>(); \
	dbgType->mCompileUnit = &mDefaultCompileUnit; \
	dbgType->mName = cTypeName; \
	dbgType->mLanguage = DbgLanguage_C;\
	dbgType->mTypeName = cTypeName; \
	dbgType->mTypeCode = typeCode; \
	dbgType->mSize = sizeof(type); \
	dbgType->mAlign = sizeof(type); \
	mCPrimitiveTypes[typeCode] = dbgType; \
	mTypeMap.Insert(dbgType); \
	dbgType = mAlloc.Alloc<DbgType>(); \
	dbgType->mCompileUnit = &mDefaultCompileUnit; \
	dbgType->mName = bfTypeName; \
	dbgType->mLanguage = DbgLanguage_Beef;\
	dbgType->mTypeName = bfTypeName; \
	dbgType->mTypeCode = typeCode; \
	dbgType->mSize = sizeof(type); \
	dbgType->mAlign = sizeof(type); \
	mBfPrimitiveTypes[typeCode] = dbgType; \
	mPrimitiveStructNames[typeCode] = structName; \
	mTypeMap.Insert(dbgType);

DbgModule::DbgModule(DebugTarget* debugTarget) : mDefaultCompileUnit(this)
{
	mMemReporter = NULL;

	mLoadState = DbgModuleLoadState_NotLoaded;
	mMappedImageFile = NULL;
	mEntryPoint = 0;
	mFailMsgPtr = NULL;
	mFailed = false;
	for (int i = 0; i < DbgType_COUNT; i++)
	{
		mBfPrimitiveTypes[i] = NULL;
		mCPrimitiveTypes[i] = NULL;
		mPrimitiveStructNames[i] = NULL;
	}

	DbgType* dbgType;

	mDefaultCompileUnit.mLanguage = DbgLanguage_Beef;
	mDefaultCompileUnit.mDbgModule = this;

	if (debugTarget != NULL)
	{
		// These are 'alias' definitions for C, but get overwritten by their official
		// stdint.h versions (ie: int8_t)
		CREATE_PRIMITIVE_C(DbgType_i8, "int8", int8);
		CREATE_PRIMITIVE_C(DbgType_i16, "int16", int16);
		CREATE_PRIMITIVE_C(DbgType_i32, "int32", int32);
		CREATE_PRIMITIVE_C(DbgType_i64, "int64", int64);
		CREATE_PRIMITIVE_C(DbgType_i8, "uint8", uint8);
		CREATE_PRIMITIVE_C(DbgType_i16, "uint16", uint16);
		CREATE_PRIMITIVE_C(DbgType_i32, "uint32", uint32);
		CREATE_PRIMITIVE_C(DbgType_i64, "uint64", uint64);

		CREATE_PRIMITIVE(DbgType_Void, "void", "void", "void", void*);
		dbgType->mSize = 0;
		dbgType->mAlign = 0;
		CREATE_PRIMITIVE(DbgType_Null, "null", "null", "null", void*);

		CREATE_PRIMITIVE(DbgType_IntPtr_Alias, "intptr_t", "int", "System.Int", intptr_target);
		CREATE_PRIMITIVE(DbgType_UIntPtr_Alias, "uintptr_t", "uint", "System.UInt", addr_target);

		CREATE_PRIMITIVE(DbgType_SChar, "char", "char", "System.Char", char);
		CREATE_PRIMITIVE(DbgType_SChar16, "wchar_t", "wchar", "System.Char16", wchar_t);
		CREATE_PRIMITIVE(DbgType_i8, "int8_t", "int8", "System.SByte", int8);
		CREATE_PRIMITIVE(DbgType_i16, "short", "int16", "System.Int16", int16);
		CREATE_PRIMITIVE(DbgType_i32, "int", "int32", "System.Int32", int32);
		CREATE_PRIMITIVE(DbgType_i64, "int64_t", "int64", "System.Int64", int64);

		CREATE_PRIMITIVE(DbgType_u8, "uint8_t", "uint8", "System.UInt8", uint8);
		CREATE_PRIMITIVE(DbgType_u16, "uint16_t", "uint16", "System.UInt16", uint16);
		CREATE_PRIMITIVE(DbgType_u32, "uint32_t", "uint32", "System.UInt32", uint32);
		CREATE_PRIMITIVE(DbgType_u64, "uint64_t", "uint64", "System.UInt64", uint64);

		CREATE_PRIMITIVE(DbgType_Single, "float", "float", "System.Single", float);
		CREATE_PRIMITIVE(DbgType_Double, "double", "double", "System.Double", double);

		CREATE_PRIMITIVE(DbgType_UChar, "char8", "char8", "System.Char", char);
		CREATE_PRIMITIVE(DbgType_UChar16, "char16", "char16", "System.Char16", short);
		CREATE_PRIMITIVE(DbgType_UChar32, "char32", "char32", "System.Char32", int);
		CREATE_PRIMITIVE(DbgType_Bool, "bool", "bool", "System.Boolean", bool);

		CREATE_PRIMITIVE(DbgType_Subroutine, "@Func", "@Func", "@Func", bool);
		CREATE_PRIMITIVE(DbgType_RawText, "@RawText", "@RawText", "@RawText", bool);

		CREATE_PRIMITIVE(DbgType_RegGroup, "@RegGroup", "@RegGroup", "@RegGroup", void*);

		CREATE_PRIMITIVE_C(DbgType_i16, "int16_t", int16_t);
		CREATE_PRIMITIVE_C(DbgType_i32, "int32_t", int32_t);
		CREATE_PRIMITIVE_C(DbgType_i64, "__int64", int64);
		CREATE_PRIMITIVE_C(DbgType_u64, "unsigned __int64", uint64);

		CREATE_PRIMITIVE_C(DbgType_u8, "unsigned char", uint8);
		CREATE_PRIMITIVE_C(DbgType_u16, "unsigned short", uint16);
		CREATE_PRIMITIVE_C(DbgType_u32, "unsigned int", uint32);
		CREATE_PRIMITIVE_C(DbgType_u32, "unsigned int32_t", uint32_t);
		CREATE_PRIMITIVE_C(DbgType_u32, "unsigned long", uint32);
		CREATE_PRIMITIVE_C(DbgType_u64, "unsigned int64_t", uint64);
	}

	mIsDwarf64 = false;
	mDebugTarget = debugTarget;
	if (debugTarget != NULL)
		mDebugger = debugTarget->mDebugger;
	else
		mDebugger = NULL;
	mDebugLineData = NULL;
	mDebugInfoData = NULL;
	mDebugPubNames = NULL;
	mDebugFrameAddress = 0;
	mDebugFrameData = NULL;
	mDebugLocationData = NULL;
	mDebugRangesData = NULL;
	mDebugAbbrevData = NULL;
	mDebugStrData = NULL;
	mDebugAbbrevPtrData = NULL;
	mEHFrameData = NULL;
	mEHFrameAddress = 0;
	mStringTable = NULL;
	mSymbolData = NULL;
	mCheckedBfObject = false;
	mBfObjectHasFlags = false;
	mModuleKind = DbgModuleKind_Module;
	mStartTypeIdx = 0;
	mEndTypeIdx = 0;
	mHotIdx = 0;
	mId = 0;
	mStartSubprogramIdx = 0;
	mEndSubprogramIdx = 0;
	mCodeAddress = NULL;
	mMayBeOld = false;
	mTimeStamp = 0;
	mExpectedFileSize = 0;
	mBfTypeType = NULL;
	mBfTypesInfoAddr = 0;

	mImageBase = 0;
	mPreferredImageBase = 0;
	mImageSize = 0;
	mOrigImageData = NULL;
	mDeleting = false;

	mAllocSizeData = 0;

	mParsedSymbolData = false;
	mParsedTypeData = false;
	mParsedGlobalsData = false;
	mPopulatedStaticVariables = false;
	mParsedFrameDescriptors = false;

	mTLSAddr = 0;
	mTLSSize = 0;
	mTLSExtraAddr = 0;
	mTLSExtraSize = 0;
	mTLSIndexAddr = 0;
	mDbgFlavor = DbgFlavor_Unknown;
	mMasterCompileUnit = NULL;
}

DbgModule::~DbgModule()
{
	delete mMemReporter;

	for (auto dwSrcFile : mEmptySrcFiles)
		delete dwSrcFile;
	for (auto dwCompileUnit : mCompileUnits)
		delete dwCompileUnit;

	delete mSymbolData;
	delete mStringTable;
	delete mDebugLineData;
	delete mDebugInfoData;
	delete mDebugPubNames;
	delete mDebugFrameData;
	delete mDebugLocationData;
	delete mDebugRangesData;
	delete mDebugAbbrevData;
	delete mDebugAbbrevPtrData;
	delete mDebugStrData;
	for (auto entry : mExceptionDirectory)
		delete entry.mData;
	delete mEHFrameData;

	delete mOrigImageData;

	if ((IsObjectFile()) && (mImageBase != 0))
	{
		mDebugger->ReleaseHotTargetMemory((addr_target)mImageBase, (int)mImageSize);
	}

	for (auto data : mOwnedSectionData)
		delete data;
}

DbgSubprogram* DbgModule::FindSubprogram(DbgType* dbgType, const char * methodName)
{
	dbgType = dbgType->GetPrimaryType();
	dbgType->PopulateType();

	if (dbgType->mNeedsGlobalsPopulated)
		PopulateTypeGlobals(dbgType);

	for (auto methodNameEntry : dbgType->mMethodNameList)
	{
		if ((methodNameEntry->mCompileUnitId != -1) && (strcmp(methodNameEntry->mName, methodName) == 0))
		{
			// If we hot-replaced this type then we replaced and parsed all the methods too
			if (!dbgType->mCompileUnit->mDbgModule->IsObjectFile())
				dbgType->mCompileUnit->mDbgModule->MapCompileUnitMethods(methodNameEntry->mCompileUnitId);
			methodNameEntry->mCompileUnitId = -1;
		}
	}

	DbgSubprogram* result = NULL;
	for (auto method : dbgType->mMethodList)
	{
		if (strcmp(method->mName, methodName) == 0)
		{
			method->PopulateSubprogram();
			if ((result == NULL) || (method->mBlock.mLowPC != 0))
				result = method;
		}
	}

	return result;
}

void DbgModule::Fail(const StringImpl& error)
{
	if (mFailMsgPtr != NULL)
	{
		if (mFailMsgPtr->IsEmpty())
			*mFailMsgPtr = error;
	}

	String errorStr = "error ";
	if (!mFilePath.IsEmpty())
	{
		errorStr += "Error in ";
		errorStr += mFilePath;
		errorStr += ": ";
	}
	errorStr += error;
	errorStr += "\n";

	mDebugger->OutputRawMessage(errorStr);
	mFailed = true;
}

void DbgModule::SoftFail(const StringImpl& error)
{
	if (mFailMsgPtr != NULL)
	{
		if (mFailMsgPtr->IsEmpty())
			*mFailMsgPtr = error;
	}

	String errorStr = "errorsoft ";
	if (!mFilePath.IsEmpty())
	{
		errorStr += "Error in ";
		errorStr += mFilePath;
		errorStr += ": ";
	}
	errorStr += error;
	errorStr += "\n";

	mDebugger->OutputRawMessage(errorStr);
	mFailed = true;
}

void DbgModule::HardFail(const StringImpl& error)
{
	if (mFailMsgPtr != NULL)
	{
		if (mFailMsgPtr->IsEmpty())
			*mFailMsgPtr = error;
	}

	String errorStr;
	if (!mFilePath.IsEmpty())
	{
		errorStr += "Error in ";
		errorStr += mFilePath;
		errorStr += ": ";
	}
	errorStr += error;
	errorStr += "\n";

	BF_FATAL(errorStr.c_str());
}

char* DbgModule::DbgDupString(const char* str, const char* allocName)
{
	int strLen = (int)strlen(str);
	if (strLen == 0)
		return NULL;
	char* dupStr = (char*)mAlloc.AllocBytes(strLen + 1, (allocName != NULL) ? allocName : "DbgDupString");
	memcpy(dupStr, str, strLen);
	return dupStr;
}

DbgModule* DbgModule::GetLinkedModule()
{
	if (IsObjectFile())
		return mDebugTarget->mTargetBinary;
	return this;
}

addr_target DbgModule::GetTargetImageBase()
{
	if (IsObjectFile())
		return (addr_target)mDebugTarget->mTargetBinary->mImageBase;
	return (addr_target)mImageBase;
}

void DbgModule::ParseGlobalsData()
{
	mParsedGlobalsData = true;
}

void DbgModule::ParseSymbolData()
{
	mParsedSymbolData = true;
}

void DbgModule::ParseTypeData()
{
	mParsedTypeData = true;
}

DbgCompileUnit* DbgModule::ParseCompileUnit(int compileUnitId)
{
	return NULL;
}

void DbgModule::MapCompileUnitMethods(DbgCompileUnit * compileUnit)
{
}

void DbgModule::MapCompileUnitMethods(int compileUnitId)
{
}

void DbgModule::PopulateType(DbgType* dbgType)
{
}

void DbgModule::PopulateTypeGlobals(DbgType* dbgType)
{
}

void DbgModule::PopulateStaticVariableMap()
{
	if (mPopulatedStaticVariables)
		return;

	for (auto staticVariable : mStaticVariables)
	{
		mStaticVariableMap[staticVariable->GetMappedName()] = staticVariable;
	}

	mPopulatedStaticVariables = true;
}

void DbgModule::ProcessDebugInfo()
{
}

addr_target DbgModule::RemapAddr(addr_target addr)
{
	if ((addr != 0) && (mPreferredImageBase != 0) && (mImageBase != 0))
		return addr + (intptr_target)(mImageBase - mPreferredImageBase);
	return addr;
}

void DbgModule::ParseAbbrevData(const uint8* data)
{
	while (true)
	{
		int abbrevIdx = (int)DecodeULEB128(data);

		mDebugAbbrevPtrData[abbrevIdx] = data;

		if (abbrevIdx == 0)
			break;
		int entryTag = (int)DecodeULEB128(data);
		bool hasChildren = GET(char) == DW_CHILDREN_yes;
		while (true)
		{
			int attrName = (int)DecodeULEB128(data);
			int form = (int)DecodeULEB128(data);
			if ((attrName == 0) && (form == 0))
				break;
		}
	}
}

void DbgModule::ParseExceptionData()
{
	if (mExceptionDirectory.IsEmpty())
		return;

	BP_ZONE("DbgModule::ParseExceptionData");

	for (auto entry : mExceptionDirectory)
	{
		const uint8* data = entry.mData;
		const uint8* dataEnd = data + entry.mSize;

		static int entryCount = 0;

		addr_target imageBase = GetTargetImageBase();

		while (data < dataEnd)
		{
			addr_target beginAddress = GET(uint32);
			addr_target endAddress = GET(uint32);
			uint32 unwindData = GET(uint32);

			//TODO: Apparently unwindData can refer to another runtime entry in the .pdata if the LSB is set to 1?

			beginAddress += (addr_target)imageBase;
			endAddress += (addr_target)imageBase;

			int exSize = (int)(endAddress - beginAddress);
			for (int exOffset = 0; true; exOffset += DBG_MAX_LOOKBACK)
			{
				int curSize = exSize - exOffset;
				if (curSize <= 0)
					break;

				BP_ALLOC_T(DbgExceptionDirectoryEntry);
				DbgExceptionDirectoryEntry* exceptionDirectoryEntry = mAlloc.Alloc<DbgExceptionDirectoryEntry>();

				exceptionDirectoryEntry->mAddress = beginAddress + exOffset;
				exceptionDirectoryEntry->mOrigAddressOffset = exOffset;
				exceptionDirectoryEntry->mAddressLength = curSize;
				exceptionDirectoryEntry->mExceptionPos = (int)unwindData;
				exceptionDirectoryEntry->mDbgModule = this;
				mDebugTarget->mExceptionDirectoryMap.Insert(exceptionDirectoryEntry);

				entryCount++;
			}
		}
	}
}

static int gIdx = 0;

template <typename T> static bool IsTypeSigned() { return false; }
template <> bool IsTypeSigned<int8>() { return true; }
template <> bool IsTypeSigned<int16>() { return true; }
template <> bool IsTypeSigned<int32>() { return true; }
template <> bool IsTypeSigned<int64>() { return true; }

#pragma warning(push)
#pragma warning(disable:4302)
#pragma warning(disable:4311)
#pragma warning(disable:4312)
#pragma warning(disable:4800)
#pragma warning(disable:4800)
template <typename T>
T DbgModule::ReadValue(const uint8*& data, int form, int refOffset, const uint8** extraData, const uint8* startData)
{
	gIdx++;

	switch (form)
	{
	case DW_FORM_strp:
		{
			int strOffset = GET(int);
			BF_ASSERT(mDebugStrData != NULL);
			const char* str = (const char*)mDebugStrData + strOffset;
			return (T)(intptr)str;
		}
		break;
	case DW_FORM_data1:
		{
			if (IsTypeSigned<T>())
				return (T)(intptr)GET(int8);
			else
				return (T)(uintptr)GET(uint8);
		}
		break;
	case DW_FORM_data2:
		{
		if (IsTypeSigned<T>())
			return (T)(intptr)GET(int16);
		else
			return (T)(uintptr)GET(uint16);
		}
		break;
	case DW_FORM_data4:
		{
		if (IsTypeSigned<T>())
			return (T)(intptr)GET(int32);
		else
			return (T)(uintptr)GET(uint32);
		}
		break;
	case DW_FORM_data8:
		{
			if (IsTypeSigned<T>())
				return (T)GET(int64);
			else
				return (T)GET(uint64);
		}
		break;
	case DW_FORM_ref1:
		{
			return (T)(intptr)GET(int8) + refOffset;
		}
		break;
	case DW_FORM_ref2:
		{
			return (T)(intptr)GET(int16) + refOffset;
		}
		break;
	case DW_FORM_ref4:
		{
			return (T)(intptr)GET(int32) + refOffset;
		}
		break;
	case DW_FORM_sec_offset:
		{
			intptr_target offset;
			if (mIsDwarf64)
				offset = (intptr_target)GET(int64);
			else
				offset = GET(int32);
			if (extraData != NULL)
			{
				*extraData = mDebugLocationData + offset;
				return 0;
			}
			return (T)offset;
		}
		break;
	case DW_FORM_addr:
		{
			return (T)GET(addr_target);
		}
		break;
	case DW_FORM_exprloc:
		{
			int64_t exprLen = DecodeULEB128(data);
			const uint8* endData = data + exprLen;

			if (extraData != NULL)
				*extraData = data;

			data = endData;
			return (T)exprLen;
		}
		break;
	case DW_FORM_flag_present:
		{
			//
			return (T)1;
		}
		break;
	case DW_FORM_flag:
		{
			//
			return (T)(intptr)GET(char);
		}
		break;
	case DW_FORM_sdata:
		return (T)DecodeSLEB128(data);
	case DW_FORM_udata:
		return (T)DecodeULEB128(data);
	case DW_FORM_string:
		{
			const char* str = (const char*)data;
			while (true)
			{
				uint8 val = *data;
				data++;
				if (val == 0)
					return (T)(intptr)str;
			}
		}
	case DW_FORM_block:
		{
			int blockLen = (int)DecodeULEB128(data);
			const uint8* retVal = data;
			data += blockLen;
			return (T)(intptr)retVal;
		}
	case DW_FORM_block1:
		{
			int blockLen = (int)*((uint8*)data);
			data += sizeof(uint8);
			const uint8* retVal = data;
			data += blockLen;
			return (T)(intptr)retVal;
		}
	default:
		assert("Not covered!" == 0);
		break;
	}

	return (T)0;
}
#pragma warning(pop)

static int gAbbrevNum = 0;

DbgType* DbgModule::GetOrCreateType(int typeIdx, DbgDataMap& dataMap)
{
	if (typeIdx == 0)
		return NULL;
	DbgModule* linkedModule = GetLinkedModule();
	DbgType* dbgType = dataMap.Get<DbgType*>(typeIdx);
	if (dbgType != NULL)
		return dbgType;
	dbgType = mAlloc.Alloc<DbgType>();
	dbgType->mTypeIdx = (int)linkedModule->mTypes.size();
	linkedModule->mTypes.push_back(dbgType);
	dataMap.Set(typeIdx, dbgType);
	return dbgType;
}

typedef std::pair<DbgClassType, void*> DataPair;
typedef llvm::SmallVector<DataPair, 16> DataStack;

template <typename T>
T DbgModule::GetOrCreate(int idx, DbgDataMap& dataMap)
{
	if (idx == 0)
		return NULL;
	T val = dataMap.Get<T>(idx);
	if (val != NULL)
		return val;
	val = mAlloc.Alloc<typename RemoveTypePointer<T>::type >();
	dataMap.Set(idx, val);
	return val;
}

template <typename T>
static T GetStackTop(DataStack* dataStack)
{
	auto dataPair = dataStack->back();
	if (dataPair.first == RemoveTypePointer<T>::type::ClassType)
		return (T)dataPair.second;
	return NULL;
}

template <>
DbgBlock* GetStackTop<DbgBlock*>(DataStack* dataStack)
{
	auto dataPair = dataStack->back();
	if (dataPair.first == DbgBlock::ClassType)
		return (DbgBlock*)dataPair.second;
	if (dataPair.first == DbgSubprogram::ClassType)
		return &((DbgSubprogram*)dataPair.second)->mBlock;
	if (dataPair.first == DbgType::ClassType)
		return ((DbgType*)dataPair.second)->mBlockParam;
	return NULL;
}

template <typename T>
static bool StackHasType(DataStack* dataStack)
{
	for (auto itr : *dataStack)
		if (itr.first == RemoveTypePointer<T>::type::ClassType)
			return true;
	return false;
}

template <typename T>
static T GetStackLast(DataStack* dataStack)
{
	for (int i = (int)dataStack->size() - 1; i >= 0; i--)
	{
		if ((*dataStack)[i].first == RemoveTypePointer<T>::type::ClassType)
			return (T)(*dataStack)[i].second;
	}
	return NULL;
}

template <typename T>
static DataPair MakeDataPair(T* data)
{
	return DataPair(T::ClassType, data);
}

void DbgModule::FixupInnerTypes(int startingTypeIdx)
{
	BP_ZONE("DbgModule_FixupInnerTypes");

	for (int typeIdx = startingTypeIdx; typeIdx < (int)mTypes.size(); typeIdx++)
	{
		DbgType* dbgType = mTypes[typeIdx];

		if ((dbgType->mPriority == DbgTypePriority_Primary_Implicit) && (dbgType->mParent != NULL) && (dbgType->mParent->mTypeCode != DbgType_Namespace) &&
			(dbgType->mParent->mPriority <= DbgTypePriority_Primary_Implicit))
		{
			auto primaryParent = dbgType->mParent->GetPrimaryType();
			dbgType->mParent->mSubTypeList.Clear();

			dbgType->mParent = primaryParent;
			primaryParent->mSubTypeList.PushBack(dbgType);
		}
	}
}

void DbgModule::MapTypes(int startingTypeIdx)
{
	BP_ZONE("DbgModule_MapTypes");

	bool needsInnerFixups = false;
	for (int typeIdx = startingTypeIdx; typeIdx < (int)mTypes.size(); typeIdx++)
	{
		DbgType* dbgType = mTypes[typeIdx];
		BF_ASSERT(dbgType->mTypeCode != DbgType_Null);

		if ((dbgType->mTypeCode == DbgType_Namespace) && (dbgType->mPriority < DbgTypePriority_Primary_Implicit))
			continue;

		//TODO: Always valid?
		if (dbgType->mIsDeclaration)
			continue;

		// We were avoiding adding '<' names before, but that made it impossible to look up auto-named primary types ,
		//  like in-place unions like '<unnamed-type-u>'
		if ((dbgType->mTypeName == NULL) || (dbgType->mName == NULL) /*|| (dbgType->mTypeName[0] == '<')*/)
			continue;

		if (dbgType->mTypeCode > DbgType_DefinitionEnd)
		{
			// Only add "definition types"
			continue;
		}

		if (dbgType->mTypeCode == DbgType_Namespace)
		{
			bool isQualifiedNamespace = false;
			for (const char* cPtr = dbgType->mTypeName; *cPtr != '\0'; cPtr++)
				if (*cPtr == '.')
					isQualifiedNamespace = true;
			if (isQualifiedNamespace)
				continue; // Don't add fully qualified namespaces (they come from the 'using' implementation)*
		}

		if (dbgType->mHasStaticMembers)
		{
			for (auto member : dbgType->mMemberList)
				if ((member->mIsStatic) && (member->mLocationData != NULL))
					dbgType->mDefinedMembersSize++;
		}

		if ((dbgType->mTypeName != NULL) && (strcmp(dbgType->mTypeName, "@") == 0))
		{
			// Globals type.
			continue;
		}

		auto prevTypeEntry = FindType(dbgType->mName, dbgType->mLanguage);

		// Only replace previous instance if its a declaration
		if (prevTypeEntry != NULL)
		{
			auto prevType = prevTypeEntry->mValue;

			if (dbgType->mCompileUnit->mDbgModule != prevType->mCompileUnit->mDbgModule)
			{
				// Don't replace original types with hot types -- those need to be inserted in the the hot alternates list
				BF_ASSERT(dbgType->mCompileUnit->mDbgModule->IsObjectFile());
				prevType->mHotNewType = dbgType;
				continue;
			}

			// Never override explicit primaries
			if (prevType->mPriority == DbgTypePriority_Primary_Explicit)
				continue;

			if (dbgType->mTypeCode == DbgType_TypeDef)
			{
				// Typedef can never override anything
				continue;
			}

			if (prevType->mTypeCode == DbgType_TypeDef)
			{
				if (dbgType->mTypeCode != DbgType_TypeDef)
				{
					// Allow this to override
					prevTypeEntry->mValue = dbgType;
				}
				continue;
			}

			// Don't replace a ptr to an BfObject with a BfObject
			if ((prevType->mTypeCode == DbgType_Ptr) && (dbgType->mTypeCode == DbgType_Struct))
				continue;

			if ((prevType->mTypeCode == DbgType_Struct) && (dbgType->mTypeCode == DbgType_Ptr))
			{
				// Allow this to override
				prevTypeEntry->mValue = dbgType;
				continue;
			}

			if (prevType->mTypeCode == DbgType_Namespace)
			{
				if (dbgType->mTypeCode != DbgType_Namespace)
				{
					// Old type was namespace but new isn't? Replace old type.
					while (!prevType->mSubTypeList.IsEmpty())
					{
						DbgType* subType = prevType->mSubTypeList.PopFront();
						subType->mParent = dbgType;
						dbgType->mSubTypeList.PushBack(subType);
					}
					prevType->mPriority = DbgTypePriority_Normal;
					if (dbgType->mPriority < DbgTypePriority_Primary_Implicit)
						dbgType->mPriority = DbgTypePriority_Primary_Implicit;
					prevTypeEntry->mValue = dbgType;
					continue;
				}

				// We definitely didn't want to do this for MS.  For DWARF?
				//prevType->mAlternates.PushFront(dbgType, &mAlloc);
				continue;
			}
			else
			{
				// New type is namespace but old wasn't? Ignore new type.
				if (dbgType->mTypeCode == DbgType_Namespace)
					continue;

				if (dbgType->mIsDeclaration)
					continue;

				if (!prevType->mIsDeclaration)
				{
					if ((prevType->mCompileUnit == NULL) || (dbgType->mLanguage < prevType->mLanguage))
					{
						// We always want 'Beef' types to supersede 'C' types, but don't override the built-in primitive types
						continue;
					}

					if (prevType->mDefinedMembersSize > 0)
					{
						if (dbgType->mDefinedMembersSize > 0)
						{
							// We create an 'alternates' list for all types that define at least one static field
							if (prevType->mHasStaticMembers)
								prevType->mAlternates.PushFront(dbgType, &mAlloc);
						}
						continue;
					}

// 					if (prevType->mDefinedMembersSize > dbgType->mDefinedMembersSize)
// 					{
// 						continue;
// 					}

					if (prevType->mMethodsWithParamsCount > dbgType->mMethodsWithParamsCount)
					{
						// This handles a special case where methods without line data like <Enum>.HasFlags doesn't show containing
						//  params in cases where it gets inlined
						continue;
					}

					// Types with method lists are preferred
					if ((!prevType->mMethodList.IsEmpty()) && (dbgType->mMethodList.IsEmpty()))
						continue;
					if ((prevType->mTypeCode == DbgType_Ptr) && (prevType->mTypeParam != NULL) && (!prevType->mTypeParam->mMethodList.IsEmpty()))
						continue;
				}

				// Replace type
				if (!prevType->mSubTypeList.IsEmpty())
					needsInnerFixups = true;
				prevType->mPriority = DbgTypePriority_Normal;
				if (dbgType->mPriority == DbgTypePriority_Normal)
					dbgType->mPriority = DbgTypePriority_Primary_Implicit;
				prevTypeEntry->mValue = dbgType;
				continue;
			}
		}

		if ((dbgType->mParent != NULL) && (dbgType->mParent->mTypeCode != DbgType_Namespace) && (dbgType->mParent->mPriority <= DbgTypePriority_Primary_Implicit))
			needsInnerFixups = true;

		if (dbgType->mPriority == DbgTypePriority_Normal)
			dbgType->mPriority = DbgTypePriority_Primary_Implicit;
		mTypeMap.Insert(dbgType);
	}

	if (needsInnerFixups)
		FixupInnerTypes(startingTypeIdx);
}

void DbgModule::CreateNamespaces()
{
	BP_ZONE("DbgModule::CreateNamespaces");

	int startLength = (int)mTypes.size();

	for (int typeIdx = 0; typeIdx < startLength; typeIdx++)
	{
		DbgType* dbgType = mTypes[typeIdx];

		if (dbgType->mName == NULL)
			continue;

		if ((dbgType->mTypeCode == DbgType_Namespace) && (dbgType->mTagIdx != 0))
		{
			auto namespaceTypeEntry = FindType(dbgType->mName, dbgType->GetLanguage());
			DbgType* namespaceType;
			if (namespaceTypeEntry == NULL)
			{
				namespaceType = mAlloc.Alloc<DbgType>();
				namespaceType->mTypeCode = DbgType_Namespace;
				namespaceType->mLanguage = dbgType->mLanguage;
				namespaceType->mCompileUnit = dbgType->mCompileUnit;
				namespaceType->mTypeIdx = (int)mTypes.size();
				namespaceType->mPriority = DbgTypePriority_Primary_Explicit;
				namespaceType->mName = dbgType->mName;
				namespaceType->mTypeName = dbgType->mTypeName;

				if (dbgType->mParent != NULL)
				{
					namespaceType->mParent = dbgType->mParent->GetPrimaryType();
					namespaceType->mParent->mSubTypeList.PushBack(namespaceType);
				}
				else
				{
					namespaceType->mCompileUnit->mGlobalType->mSubTypeList.PushBack(namespaceType);
				}

				mTypes.push_back(namespaceType);
				mTypeMap.Insert(namespaceType);
			}
			else
				namespaceType = namespaceTypeEntry->mValue;

			while (!dbgType->mMemberList.IsEmpty())
			{
				DbgVariable* curVar = dbgType->mMemberList.PopFront();
				namespaceType->mMemberList.PushBack(curVar);
			}

			DbgType* prevType = NULL;
			DbgType* curType = dbgType->mSubTypeList.mHead;
			while (curType != NULL)
			{
				DbgType* nextType = curType->mNext;

				if (curType->mPriority >= DbgTypePriority_Primary_Implicit)
				{
					dbgType->mSubTypeList.Remove(curType, prevType);
					namespaceType->mSubTypeList.PushBack(curType);
				}

				prevType = curType;
				curType = nextType;
			}

			continue;
		}
	}

	// If we didn't have a parent type for a namespace (ie: if System.Collections wasn't linked to System) then we wait
	//  until the end and move those from the global list to the parent list
	for (int typeIdx = startLength; typeIdx < (int)mTypes.size(); typeIdx++)
	{
		DbgType* dbgType = mTypes[typeIdx];
		if (dbgType->mParent != NULL)
			continue;

		char* typeName = (char*)dbgType->mTypeName;
		int lastDotIdx = -1;
		for (int i = 0; true; i++)
		{
			char c = typeName[i];
			if (c == 0)
				break;
			if (c == '.')
				lastDotIdx = i;
		}
		if (lastDotIdx == -1)
			continue;

		typeName[lastDotIdx] = 0;
		dbgType->mTypeName = typeName + lastDotIdx + 1;
		auto parentEntry = FindType(typeName, dbgType->GetLanguage());
		typeName[lastDotIdx] = '.';

		if (parentEntry == NULL)
			continue;
		auto parentType = parentEntry->mValue;
		dbgType->mCompileUnit->mGlobalType->mSubTypeList.Remove(dbgType);
		dbgType->mParent = parentType;
		parentType->mSubTypeList.PushBack(dbgType);
	}
}

void DbgModule::FindTemplateStr(const char*& name, int& templateNameIdx)
{
	if (templateNameIdx == 0)
	{
		for (int i = 0; name[i] != 0; i++)
		{
			if (name[i] == '<')
			{
				templateNameIdx = i;
				return;
			}
		}
		templateNameIdx = -1;
	}
}

void DbgModule::TempRemoveTemplateStr(const char*& name, int& templateNameIdx)
{
	if (templateNameIdx == 0)
		FindTemplateStr(name, templateNameIdx);
	if (templateNameIdx == -1)
		return;

	if (!DbgIsStrMutable(name))
		name = DbgDupString(name);

	((char*)name)[templateNameIdx] = 0;
}

void DbgModule::ReplaceTemplateStr(const char*& name, int& templateNameIdx)
{
	if (templateNameIdx > 0)
		((char*)name)[templateNameIdx] = '<';
}

void DbgModule::MapSubprogram(DbgSubprogram* dbgSubprogram)
{
	if (dbgSubprogram->mBlock.IsEmpty())
		return;

	int progSize = (int)(dbgSubprogram->mBlock.mHighPC - dbgSubprogram->mBlock.mLowPC);
	for (int progOffset = 0; true; progOffset += DBG_MAX_LOOKBACK)
	{
		int curSize = progSize - progOffset;
		if (curSize <= 0)
			break;

		BP_ALLOC_T(DbgSubprogramMapEntry);
		DbgSubprogramMapEntry* subprogramMapEntry = mAlloc.Alloc<DbgSubprogramMapEntry>();
		subprogramMapEntry->mAddress = dbgSubprogram->mBlock.mLowPC + progOffset;
		subprogramMapEntry->mEntry = dbgSubprogram;
		mDebugTarget->mSubprogramMap.Insert(subprogramMapEntry);
	}
}

bool DbgModule::ParseDWARF(const uint8*& dataPtr)
{
	BP_ZONE("ParseDWARF");

	const uint8* data = dataPtr;
	const uint8* startData = mDebugInfoData;
	int dataOfs = (int)(data - mDebugInfoData);

	intptr_target length = GET(int);

	DbgModule* linkedModule = GetLinkedModule();

	if (length == -1)
	{
		mIsDwarf64 = true;
		length = (intptr_target)GET(int64);
	}
	else
		mIsDwarf64 = false;

	if (length == 0)
		return false;
	const uint8* dataEnd = data + length;
	int version = GET(short);
	int abbrevOffset = GET(int);
	char pointerSize = GET(char);

	ParseAbbrevData(mDebugAbbrevData + abbrevOffset);

	DbgCompileUnit* compileUnit = new DbgCompileUnit(this);
	mDbgFlavor = DbgFlavor_GNU;
	compileUnit->mDbgModule = this;
	mCompileUnits.push_back(compileUnit);

	DbgSubprogram* subProgram = NULL;

	//std::map<int, DbgType*> typeMap;
	//std::map<int, DbgSubprogram*> subprogramMap;
	int tagStart = (int)(data - startData);
	int tagEnd = (int)(dataEnd - startData);
	DbgDataMap dataMap(tagStart, tagEnd);
	DataStack dataStack;
	Array<AbstractOriginEntry> abstractOriginReplaceList;

	Array<int> deferredArrayDims;

	int startingTypeIdx = (int)linkedModule->mTypes.size();

	while (data < dataEnd)
	{
		gAbbrevNum++;

		const uint8* tagDataStart = data;
		int tagIdx = (int)(tagDataStart - startData);

		int abbrevIdx = (int)DecodeULEB128(data);
		const uint8* abbrevData = mDebugAbbrevPtrData[abbrevIdx];

		if (abbrevIdx == 0)
		{
			if (deferredArrayDims.size() > 0)
			{
				DbgType* arrType = GetStackTop<DbgType*>(&dataStack);
				BF_ASSERT(arrType->mTypeCode == DbgType_SizedArray);
				arrType->mSize = deferredArrayDims[0]; // Byte count still needs to be multiplied by the underlying type size

				DbgType* rootArrType = arrType;
				for (int dimIdx = 0; dimIdx < (int)deferredArrayDims.size() - 1; dimIdx++)
				{
					int dimSize = deferredArrayDims[dimIdx];

					DbgType* subArrType = mAlloc.Alloc<DbgType>();
					subArrType->mCompileUnit = compileUnit;
					subArrType->mLanguage = compileUnit->mLanguage;
					subArrType->mTypeIdx = (int)linkedModule->mTypes.size();
					linkedModule->mTypes.push_back(subArrType);

					subArrType->mTypeCode = DbgType_SizedArray;
					subArrType->mTypeParam = arrType->mTypeParam;
					subArrType->mSize = deferredArrayDims[dimIdx + 1];

					arrType->mTypeParam = subArrType;
					arrType = subArrType;
				}

				deferredArrayDims.Clear();
			}

			dataStack.pop_back();
			continue;
		}

		int entryTag = (int) DecodeULEB128(abbrevData);
		bool hasChildren = GET_FROM(abbrevData, char) == DW_CHILDREN_yes;

		int64 atLowPC = 0;
		int64 atHighPC = 0;
		int64 atRanges = 0;
		bool hasRanges = false;
		const uint8* atFrameBase = NULL;
		int64_t atFrameBaseLength = 0;
		int64 atLocationLen = 0;
		const uint8* atLocationData = 0;
		const char* atProducer = NULL;
		const char* atName = NULL;
		const char* atCompDir = NULL;
		const char* atLinkageName = NULL;
		int64 atConstValue = 0;
		int atDataMemberLocation = 0;
		const uint8* atDataMemberData = NULL;
		int atDeclFile = 0;
		int atDeclLine = 0;
		int atCallFile = 0;
		int atCallLine = 0;
		int atCount = 0;
		int atType = 0;
		int atImport = 0;
		int atInline = 0;
		int atArtificial = 0;
		int atExternal = 0;
		int atByteSize = -1;
		int atEncoding = 0;
		int atSpecification = 0;
		int atObjectPointer = 0;
		int atBitOffset = 0;
		int atBitSize = 0;
		int atAbstractOrigin = 0;
		const uint8* atVirtualLocData = NULL;
		bool atDeclaration = false;
		bool atVirtual = false;
		bool hadConstValue = false;
		bool hadMemberLocation = false;
		bool isOptimized = false;

		DataPair newDataPair;

		while (true)
		{
			int attrName = (int)DecodeULEB128(abbrevData);
			int form = (int)DecodeULEB128(abbrevData);
			if ((attrName == 0) && (form == 0))
				break;

			switch (attrName)
			{
			case DW_AT_sibling:
				ReadValue<char>(data, form);
				break;
			case DW_AT_location:
				atLocationLen = (int)ReadValue<uint>(data, form, dataOfs, &atLocationData, startData);
				break;
			case DW_AT_name:
				atName = ReadValue<const char*>(data, form);
				break;
			case DW_AT_ordering:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_byte_size:
				atByteSize = ReadValue<int>(data, form);
				break;
			case DW_AT_bit_offset:
				atBitOffset = ReadValue<int>(data, form);
				break;
			case DW_AT_bit_size:
				atBitSize = ReadValue<int>(data, form);
				break;
			case DW_AT_stmt_list:
				ReadValue<int64_t>(data, form);
				break;
			case DW_AT_low_pc:
				atLowPC = RemapAddr((addr_target)ReadValue<int64_t>(data, form));
				break;
			case DW_AT_high_pc:
				atHighPC = ReadValue<int64_t>(data, form);
				break;
			case DW_AT_language:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_discr:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_discr_value:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_visibility:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_import:
				atImport = ReadValue<int>(data, form) + dataOfs;
				break;
			case DW_AT_string_length:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_common_reference:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_comp_dir:
				atCompDir = ReadValue<const char*>(data, form);
				break;
			case DW_AT_const_value:
				atConstValue = ReadValue<int64>(data, form);
				hadConstValue = true;
				break;
			case DW_AT_containing_type:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_default_value:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_inline:
				atInline = ReadValue<int>(data, form);
				break;
			case DW_AT_is_optional:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_lower_bound:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_producer:
				atProducer = ReadValue<const char*>(data, form);
				break;
			case DW_AT_prototyped:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_return_addr:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_start_scope:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_bit_stride:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_upper_bound:
				// Lower bound not supported
				atCount = ReadValue<int>(data, form);
				break;
			case DW_AT_abstract_origin:
				atAbstractOrigin = ReadValue<int>(data, form, dataOfs);
				break;
			case DW_AT_accessibility:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_address_class:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_artificial:
				atArtificial = ReadValue<int>(data, form);
				break;
			case DW_AT_base_types:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_calling_convention:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_count:
				atCount = ReadValue<uint>(data, form);
				break;
			case DW_AT_data_member_location:
				if (form == DW_FORM_exprloc)
				{
					atDataMemberLocation = (int)ReadValue<uint>(data, form, dataOfs, &atDataMemberData);
					hadMemberLocation = true;
				}
				else
				{
					atDataMemberLocation = (int)ReadValue<uint>(data, form);
					hadMemberLocation = true;
				}
				break;
			case DW_AT_decl_column:
				/*TODO:*/ ReadValue<uint32>(data, form);
				break;
			case DW_AT_decl_file:
				atDeclFile = ReadValue<uint32>(data, form);
				break;
			case DW_AT_decl_line:
				atDeclLine = ReadValue<uint32>(data, form);
				break;
			case DW_AT_declaration:
				atDeclaration = ReadValue<bool>(data, form);
				break;
			case DW_AT_discr_list:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_encoding:
				atEncoding = ReadValue<int>(data, form);
				break;
			case DW_AT_external:
				atExternal = ReadValue<int>(data, form);
				break;
			case DW_AT_frame_base:
				atFrameBaseLength = (int64_t)ReadValue<uint64_t>(data, form, dataOfs, &atFrameBase);
				break;
			case DW_AT_friend:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_identifier_case:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_macro_info:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_namelist_item:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_priority:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_segment:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_specification:
				atSpecification = ReadValue<int>(data, form, dataOfs);
				break;
			case DW_AT_static_link:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_type:
				atType = ReadValue<int>(data, form, dataOfs);
				break;
			case DW_AT_use_location:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_variable_parameter:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_virtuality:
				atVirtual = ReadValue<int>(data, form) != 0;
				break;
			case DW_AT_vtable_elem_location:
				ReadValue<uint64_t>(data, form, dataOfs, &atVirtualLocData);
				break;
			case DW_AT_allocated:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_associated:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_data_location:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_byte_stride:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_entry_pc:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_use_UTF8:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_extension:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_ranges:
				atRanges = (int)ReadValue<uint>(data, form);
				hasRanges = true;
				break;
			case DW_AT_trampoline:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_call_column:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_call_file:
				atCallFile = ReadValue<uint32>(data, form);
				break;
			case DW_AT_call_line:
				atCallLine = ReadValue<uint32>(data, form);
				break;
			case DW_AT_description:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_binary_scale:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_decimal_scale:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_small:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_decimal_sign:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_digit_count:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_picture_string:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_mutable:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_threads_scaled:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_explicit:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_object_pointer:
				atObjectPointer = ReadValue<int>(data, form);
				break;
			case DW_AT_endianity:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_elemental:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_pure:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_recursive:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_signature:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_main_subprogram:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_data_bit_offset:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_const_expr:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_enum_class:
				/*TODO:*/ ReadValue<int>(data, form);
				break;
			case DW_AT_linkage_name:
				atLinkageName = ReadValue<const char*>(data, form);
				break;

			//
			case DW_AT_MIPS_linkage_name:
				atLinkageName = ReadValue<const char*>(data, form);
				break;

			case DW_AT_APPLE_optimized:
				isOptimized = ReadValue<bool>(data, form);
				break;

			default:
				ReadValue<int>(data, form);
				break;
			}
		}

		if ((hasRanges) && (atLowPC == 0))
		{
			addr_target* rangeData = (addr_target*)(mDebugRangesData + atRanges);
			while (true)
			{
				addr_target lowPC = *(rangeData++);
				if (lowPC == 0)
					break;
				addr_target highPC = *(rangeData++);

				if (compileUnit->mLowPC != (addr_target)-1)
				{
					// These are sometimes relative to the compile unit and sometimes absolute
					if (highPC + compileUnit->mLowPC <= compileUnit->mHighPC)
					{
						lowPC += compileUnit->mLowPC;
						highPC += compileUnit->mLowPC;
					}
				}

				highPC -= lowPC;

				// Select the largest range.  We have some cases where some hoisting and such will
				//  give us a small inlining aberration much earlier than expected so this ignores that
				if ((int64)highPC > atHighPC)
				{
					atLowPC = lowPC;
					atHighPC = highPC;
				}

				/*if ((atLowPC == 0) || (lowPC < (addr_target)atLowPC))
					atLowPC = lowPC;
				if (highPC > (addr_target)atHighPC)
					atHighPC = highPC;*/
			}
		}

		switch (entryTag)
		{
		case DW_TAG_compile_unit:
			{
				newDataPair = MakeDataPair(compileUnit);
				compileUnit->mName = atName;
				compileUnit->mProducer = atProducer;
				if (atCompDir != NULL)
					compileUnit->mCompileDir = atCompDir;
				if (atLowPC != 0)
				{
					compileUnit->mLowPC = (addr_target)atLowPC;
					compileUnit->mHighPC = (addr_target)(atLowPC + atHighPC);
				}
				if (compileUnit->mProducer.IndexOf("Beef") != -1)
				{
					compileUnit->mLanguage = DbgLanguage_Beef;
				}
				else
				{
					compileUnit->mLanguage = DbgLanguage_C;
				}
				compileUnit->mGlobalType->mLanguage = compileUnit->mLanguage;
			}
			break;
		case DW_TAG_imported_module:
			{
				DbgType* parentType = GetStackTop<DbgType*>(&dataStack);
				DbgType* importType = GetOrCreateType(atImport, dataMap);
				if (parentType != NULL) // Parent type is NULL for Clang DbgModule info
					parentType->mUsingNamespaces.PushFront(importType, &mAlloc);
			}
			break;
		case DW_TAG_inlined_subroutine:
		case DW_TAG_subprogram:
			{
				/*//TODO: This is a test.  See if it breaks anything.
				if ((atExternal != 0) && (atLowPC == 0))
					break;*/

				if (atSpecification == 0)
				{
					subProgram = GetOrCreate<DbgSubprogram*>(tagIdx, dataMap);

					subProgram->mCompileUnit = compileUnit;
					subProgram->mVirtual = atVirtual;
					subProgram->mIsOptimized = isOptimized;

					if (atVirtualLocData != NULL)
					{
						const uint8* opPtr = atVirtualLocData;
						if (*(opPtr++) == DW_OP_constu)
						{
							subProgram->mVTableLoc = (int)DecodeSLEB128(opPtr) * sizeof(addr_target);
						}
					}

					//subProgram->mVTableLoc = atVirtualLoc * sizeof(addr_target);
					//SplitName(atName, subProgram->mName, subProgram->mTemplateName);
					subProgram->mName = atName;

					subProgram->mLinkName = atLinkageName;
					if (atAbstractOrigin != NULL)
					{
						DbgSubprogram* originSubProgram = GetOrCreate<DbgSubprogram*>(atAbstractOrigin, dataMap);
						auto abstractOriginEntry = AbstractOriginEntry::Create(DbgSubprogram::ClassType, subProgram, originSubProgram);
						abstractOriginReplaceList.push_back(abstractOriginEntry);
					}
					subProgram->mParentType = GetStackTop<DbgType*>(&dataStack);
					newDataPair = MakeDataPair(subProgram);

					//if ((atLinkageName != NULL) && (subProgram->mParentType != NULL))
						//subProgram->mParentType->mDefinedMembersCount++;

					mSubprograms.push_back(subProgram);

					if (subProgram->mParentType != NULL)
					{
						subProgram->mParentType->mMethodList.PushBack(subProgram);
					}
					else
					{
						compileUnit->mGlobalType->mMethodList.PushBack(subProgram);
					}
				}
				else
				{
					subProgram = dataMap.Get<DbgSubprogram*>(atSpecification);
					BF_ASSERT(subProgram != NULL);
					// We remove params form the declaration and re-add the real ones here
					subProgram->mParams.Clear();
				}

				newDataPair = MakeDataPair(subProgram);
				DbgBlock* dwBlock = &subProgram->mBlock;

				if (atType != 0)
					subProgram->mReturnType = GetOrCreateType(atType, dataMap);

				if (!atDeclaration)
				{
					dwBlock->mLowPC = (addr_target)atLowPC;
					dwBlock->mHighPC = (addr_target)(atLowPC + atHighPC);

					if (dwBlock->mLowPC != 0)
					{
						compileUnit->mLowPC = std::min(compileUnit->mLowPC, dwBlock->mLowPC);
						compileUnit->mHighPC = std::max(compileUnit->mHighPC, dwBlock->mHighPC);
					}

					if (atObjectPointer != 0)
						subProgram->mHasThis = true;
					subProgram->mFrameBaseLen = (int)atFrameBaseLength;
					subProgram->mFrameBaseData = atFrameBase;

					if (atHighPC > 0)
					{
						MapSubprogram(subProgram);
					}
				}

				if (entryTag == DW_TAG_inlined_subroutine)
				{
					DbgSubprogram* parentSubProgram = GetStackLast<DbgSubprogram*>(&dataStack);
					subProgram->mInlineeInfo = mAlloc.Alloc<DbgInlineeInfo>();
					subProgram->mInlineeInfo->mInlineParent = parentSubProgram;
					subProgram->mInlineeInfo->mRootInliner = parentSubProgram->GetRootInlineParent();
					subProgram->mFrameBaseData = parentSubProgram->mFrameBaseData;
					subProgram->mFrameBaseLen = parentSubProgram->mFrameBaseLen;
				}

				//if (subProgram->mParentType != NULL)
					//subProgram->mParentType->mDefinedMembersCount++;
			}
			break;
		case DW_TAG_lexical_block:
			{
				DbgBlock* prevBlock = GetStackTop<DbgBlock*>(&dataStack);
				DbgBlock* dwBlock = mAlloc.Alloc<DbgBlock>();
				if (hasRanges)
				{
					dwBlock->mLowPC = -1;
					dwBlock->mHighPC = (addr_target)atRanges;
				}
				else
				{
					dwBlock->mLowPC = (addr_target)atLowPC;
					dwBlock->mHighPC = (addr_target)(atLowPC + atHighPC);
				}
				newDataPair = MakeDataPair(dwBlock);
				prevBlock->mSubBlocks.PushBack(dwBlock);
			}
			break;
		case DW_TAG_variable:
			{
				DbgBlock* dwBlock = GetStackTop<DbgBlock*>(&dataStack);

				if (atName && !strncmp(atName, "__asmLines", 10))
				{
					const char* ptr = strchr(atName, '.');
					if (!ptr)
						break;

					int declLine = atDeclLine;
					Array<int> asmLines;
					int curAsmLine = 0;
					int curRunCount = 1; // initial value is starting line, with an assumed run count of 1
					bool parity = true; // starting line is standalone; everything afterwards is in pairs

					while (true)
					{
						++ptr;
						if (!*ptr)
							break;

						String s;
						if (*ptr == '$')
						{
							++ptr;
							const char* dollarPtr = strchr(ptr, '$');
							if (!dollarPtr)
								break;
							s = String(ptr, (int)(dollarPtr - ptr));
							ptr = dollarPtr;
						}
						else
						{
							s += *ptr;
						}

						//int asmLine = atoi(s.c_str());
						//asmLines.push_back(asmLine);
						const char* sPtr = s.c_str();
						int decodedValue = (int)DecodeULEB32(sPtr);
						if (!parity)
						{
							curRunCount = decodedValue;
						}
						else
						{
							for (int iLine=0; iLine<curRunCount; ++iLine)
							{
								curAsmLine += decodedValue;
								asmLines.push_back(curAsmLine);
							}
						}
						parity = !parity;
					}
					BF_ASSERT(!parity);

					if (dwBlock->mAsmDebugLineMap == NULL)
					{
						mAsmDebugLineMaps.resize(mAsmDebugLineMaps.size() + 1);
						dwBlock->mAsmDebugLineMap = &mAsmDebugLineMaps.back();
					}

					auto mapIter = dwBlock->mAsmDebugLineMap->find(declLine);
					if (mapIter != dwBlock->mAsmDebugLineMap->end())
					{
						auto& dstVec = mapIter->second;
						dstVec.Reserve(dstVec.size() + asmLines.size());
						//dstVec.insert(dstVec.end(), asmLines.begin(), asmLines.end());
						if (!asmLines.IsEmpty())
							dstVec.Insert(dstVec.size(), &asmLines[0], asmLines.size());
					}
					else
					{
						(*dwBlock->mAsmDebugLineMap)[declLine] = std::move(asmLines);
					}

					break;
				}

				bool addToGlobalVarMap = false;
				bool isNewVariable = true;
				DbgVariable* dbgVariable = NULL;
				if (atSpecification != 0)
				{
					//dbgVariable = dataMap.Get<DbgVariable*>(atSpecification);
					//BF_ASSERT(dbgVariable != NULL);

					dbgVariable = GetOrCreate<DbgVariable*>(atSpecification, dataMap);
					//dbgVariable = dataMap.Get<DbgVariable*>(atSpecification);
					//BF_ASSERT(dbgVariable != NULL);
				}
				else if (dwBlock != NULL)
				{
					dbgVariable = GetOrCreate<DbgVariable*>(tagIdx, dataMap);
					dwBlock->mVariables.PushBack(dbgVariable);
				}
				else
				{
					DbgType* dbgType = GetStackTop<DbgType*>(&dataStack);
					bool wantGlobal = true;
					if (compileUnit->mLanguage == DbgLanguage_Beef)
					{
						// Don't show certain global variables in Beef -- that includes things like VTable data
						if (atName[0] == '_')
							wantGlobal = false;
					}

					if ((dbgType == NULL) && (wantGlobal))
					{
						/*DbgCompileUnit* topCompileUnit = GetStackTop<DbgCompileUnit*>(&dataStack);
						if (topCompileUnit != NULL)
							dbgType = &topCompileUnit->mGlobalType;*/
						dbgType = linkedModule->mMasterCompileUnit->mGlobalType;

						auto foundEntry = mGlobalVarMap.Find(atName);
						if (foundEntry != NULL)
						{
							isNewVariable = false;
							dbgVariable = foundEntry->mValue;
						}
						else
						{
							addToGlobalVarMap = true;
						}
					}

					if (dbgVariable == NULL)
						dbgVariable = GetOrCreate<DbgVariable*>(tagIdx, dataMap);
					dbgVariable->mIsStatic = true;

					//TODO: dbgType can be NULL. This only (apparently?) happens for DW_TAG_inlined_subroutine, which we don't handle right now...
					if (dbgType != NULL)
					{
						BF_ASSERT(dbgType->IsNamespace() || (dbgType->mTypeCode == DbgType_Root));

						if (isNewVariable)
							dbgType->mMemberList.PushBack(dbgVariable);
					}
				}

				if (dbgVariable != NULL)
				{
					if (atSpecification == 0)
					{
						dbgVariable->mIsParam = false;
						dbgVariable->mName = atName;
						dbgVariable->mConstValue = atConstValue;
						dbgVariable->mType = GetOrCreateType(atType, dataMap);
						dbgVariable->mIsConst = hadConstValue;
						dbgVariable->mIsStatic = !hadMemberLocation;
						dbgVariable->mIsExtern = atExternal != 0;
					}
					if (atLinkageName != NULL)
						dbgVariable->mLinkName = atLinkageName;
					dbgVariable->mLocationLen = (int8)atLocationLen;
					dbgVariable->mLocationData = atLocationData;
					dbgVariable->mCompileUnit = compileUnit;

					/*if (dbgVariable->mIsStatic && !dbgVariable->mIsConst && (dbgVariable->mLocationLen > 0) && (dbgVariable->mIsExtern))
					{
						DbgAddrType addrType = DbgAddrType_Value;
						//
						addr_target valAddr = mDebugTarget->EvaluateLocation(dbgVariable->mCompileUnit->mDbgModule, NULL, dbgVariable->mLocationData, dbgVariable->mLocationLen, NULL, &addrType);
						if ((addrType == DbgAddrType_Target) && (valAddr != 0))
						{
							dbgVariable->mStaticCachedAddr = valAddr;
							if (dbgVariable->mLinkName != NULL)
								mStaticVariables.push_back(dbgVariable);
						}
						else
							dbgVariable->mIsStatic = false;
					}*/
					// We had to remove the above for hot loading, calculate the mStaticCachedAddr later.  Just put into mStaticVariables for now
					mStaticVariables.push_back(dbgVariable);

					if (atAbstractOrigin != NULL)
					{
						DbgVariable* originVariable = GetOrCreate<DbgVariable*>(atAbstractOrigin, dataMap);
						auto abstractOriginEntry = AbstractOriginEntry::Create(DbgVariable::ClassType, dbgVariable, originVariable);
						if (atAbstractOrigin < tagIdx)
							abstractOriginEntry.Replace();
						else
							abstractOriginReplaceList.push_back(abstractOriginEntry);
					}
					else if (dbgVariable->mName == NULL)
						dbgVariable->mName = "_unnamed";

					if (addToGlobalVarMap)
						mGlobalVarMap.Insert(dbgVariable);

					newDataPair = MakeDataPair(dbgVariable);
				}
			}
			break;
		case DW_TAG_formal_parameter:
			{
				DbgSubprogram* dwSubprogram = GetStackTop<DbgSubprogram*>(&dataStack);

				if (dwSubprogram == NULL)
				{
					if ((atName == NULL) && (atAbstractOrigin == 0))
					{
						DbgType* dbgType = GetStackTop<DbgType*>(&dataStack);
						if ((dbgType == NULL) || (dbgType->mTypeCode != DbgType_Subroutine))
							break;

						//TODO: Add params to subroutine type
						break;
					}

					break;
				}

				if ((dwSubprogram->mParams.IsEmpty()) && (dwSubprogram->mParentType != 0))
					dwSubprogram->mParentType->mMethodsWithParamsCount++;

				//DbgVariable* dbgVariable = mAlloc.Alloc<DbgVariable>();
				DbgVariable* dwVariable = GetOrCreate<DbgVariable*>(tagIdx, dataMap);
				dwSubprogram->mParams.PushBack(dwVariable);

				if (atArtificial != 0)
				{
					dwSubprogram->mHasThis = true;
					if (atName == NULL)
						atName = "this";
				}

				dwVariable->mCompileUnit = compileUnit;
				dwVariable->mIsParam = true;
				dwVariable->mName = atName;
				dwVariable->mLocationLen = (int)atLocationLen;
				dwVariable->mLocationData = atLocationData;
				dwVariable->mType = GetOrCreateType(atType, dataMap);

				if (atAbstractOrigin != 0)
				{
				}
			}
			break;
		case DW_TAG_enumerator:
			{
				DbgVariable* member = mAlloc.Alloc<DbgVariable>();
				member->mCompileUnit = compileUnit;
				member->mConstValue = atConstValue;
				member->mName = atName;
				member->mIsStatic = true;
				member->mIsConst = true;

				DbgType* parentType = GetStackTop<DbgType*>(&dataStack);
				parentType->mMemberList.PushBack(member);
				member->mMemberOffset = atDataMemberLocation;
				//member->mType = parentType->mTypeParam;
				member->mType = parentType;

				// Insert into parent's namespace
				auto prevTop = dataStack.back();
				dataStack.pop_back();
				DbgBlock* dwBlock = GetStackTop<DbgBlock*>(&dataStack);
				dataStack.push_back(prevTop);

				if (dwBlock != NULL)
				{
					DbgVariable* dwVariable = mAlloc.Alloc<DbgVariable>();
					dwBlock->mVariables.PushBack(dwVariable);

					if (atSpecification == 0)
					{
						dwVariable->mIsParam = false;
						dwVariable->mName = atName;
						dwVariable->mConstValue = atConstValue;
						dwVariable->mType = parentType->mTypeParam;
						dwVariable->mIsConst = hadConstValue;
						dwVariable->mIsStatic = !hadMemberLocation;
					}
					dwVariable->mLocationLen = (int)atLocationLen;
					dwVariable->mLocationData = atLocationData;
					dwVariable->mCompileUnit = compileUnit;

					BF_ASSERT(dwVariable->mName != 0);

					newDataPair = MakeDataPair(dwVariable);
				}
			}
			break;
		/*case DW_TAG_subrange_type:
			{
				DbgType* parentType = GetStackTop<DbgType*>(&dataStack);
				parentType->mArraySize = atUpperBound;
			}
			break;*/
		case DW_TAG_inheritance:
			{
				DbgType* derivedType = GetStackTop<DbgType*>(&dataStack);
				DbgBaseTypeEntry* baseTypeEntry = mAlloc.Alloc<DbgBaseTypeEntry>();
				baseTypeEntry->mBaseType = GetOrCreateType(atType, dataMap);
				if (atDataMemberData != NULL)
				{
					bool foundVirtOffset = false;
					const uint8* opPtr = atDataMemberData;
					if (*(opPtr++) == DW_OP_dup)
					{
						if (*(opPtr++) == DW_OP_deref)
						{
							if (*(opPtr++) == DW_OP_constu)
							{
								baseTypeEntry->mVTableOffset = (int)DecodeSLEB128(opPtr) / sizeof(int32);
								foundVirtOffset = true;

								if (*(opPtr++) == DW_OP_minus)
									baseTypeEntry->mVTableOffset = -baseTypeEntry->mVTableOffset;
							}
						}
					}

					BF_ASSERT(foundVirtOffset);
				}
				else
					baseTypeEntry->mThisOffset = atDataMemberLocation;
				derivedType->mBaseTypes.PushBack(baseTypeEntry);
			}
			break;
		case DW_TAG_member:
			{
				DbgType* parentType = GetStackTop<DbgType*>(&dataStack);

				if ((atName != NULL) && (strncmp(atName, "_vptr$", 6) == 0))
				{
					parentType->mHasVTable = true;
					break;
				}
				//DbgVariable* member = mAlloc.Alloc<DbgVariable>();
				DbgVariable* member = GetOrCreate<DbgVariable*>(tagIdx, dataMap);
				member->mIsMember = true;
				member->mCompileUnit = compileUnit;
				member->mName = atName;
				member->mType = GetOrCreateType(atType, dataMap);
				member->mConstValue = atConstValue;
				member->mIsConst = hadConstValue;
				member->mIsStatic = !hadMemberLocation;
				member->mBitSize = atBitSize;
				member->mBitOffset = atBitOffset;
				member->mIsExtern = atExternal != 0;

				parentType->mMemberList.PushBack(member);
				member->mMemberOffset = atDataMemberLocation;

				if ((member->mIsStatic) && (!member->mIsConst))
					parentType->mHasStaticMembers = true;

				/*if ((member->mIsStatic) && (!member->mIsConst))
					mStaticVariables.push_back(member);*/

				newDataPair = MakeDataPair(member);
				//dataMap.Set(tagIdx, member);
			}
			break;
		case DW_TAG_subrange_type:
			{
				int typeIdx = (int)(tagDataStart - startData);
				DbgType* parentType = GetStackTop<DbgType*>(&dataStack);

				int arrSize = atCount;
				deferredArrayDims.push_back(arrSize);
			}
			break;

		case DW_TAG_namespace:
		case DW_TAG_const_type:
		case DW_TAG_base_type:
		case DW_TAG_pointer_type:
		case DW_TAG_ptr_to_member_type:
		case DW_TAG_array_type:
		case DW_TAG_reference_type:
		case DW_TAG_rvalue_reference_type:
		case DW_TAG_unspecified_type:
		case DW_TAG_class_type:
		case DW_TAG_enumeration_type:
		case DW_TAG_structure_type:
		case DW_TAG_union_type:
		case DW_TAG_typedef:
		case DW_TAG_volatile_type:
		case DW_TAG_subroutine_type:
		//case DW_TAG_subrange_type:
		case DW_TAG_restrict_type:
			{
				int typeIdx = (int)(tagDataStart - startData);

				DbgType* dbgType = GetOrCreateType(typeIdx, dataMap);
				const char* nameSep = (compileUnit->mLanguage == DbgLanguage_Beef) ? "." : "::";

				if ((atName != NULL) &&
					((entryTag == DW_TAG_structure_type) || (entryTag == DW_TAG_class_type) ||
					 (entryTag == DW_TAG_typedef) || (entryTag == DW_TAG_union_type) || (entryTag == DW_TAG_enumeration_type) ||
					 (entryTag == DW_TAG_namespace)))
				{
					BF_ASSERT(dbgType->mTypeCode == DbgType_Null);
					DbgType* parentType = GetStackTop<DbgType*>(&dataStack);

					if (parentType != NULL)
					{
						dbgType->mParent = parentType;
						dbgType->mParent->mSubTypeList.PushBack(dbgType);

						/*if (dbgType->mParent->mName != NULL)
						{
							if (atName == NULL)
							{
								dbgType->mName = dbgType->mParent->mName; // Extend from name of parent if we're anonymous
							}
							else
							{
								int nameSepLen = strlen(nameSep);
								int parentNameLen = strlen(dbgType->mParent->mName);
								int nameLen = strlen(atName);
								char* name = (char*)mAlloc.AllocBytes(parentNameLen + nameSepLen + nameLen + 1);
								memcpy(name, dbgType->mParent->mName, parentNameLen);
								memcpy(name + parentNameLen, nameSep, nameSepLen);
								memcpy(name + parentNameLen + nameSepLen, atName, nameLen);
								dbgType->mName = name;
							}
						}*/
					}
					else
					{
						// Add to global subtype list but don't set dbgType->mParent
						compileUnit->mGlobalType->mSubTypeList.PushBack(dbgType);
					}
				}

				const char* useName = atName;
				/*if ((useName != NULL) && (strcmp(useName, "@") == 0))
					useName = NULL;*/

				dbgType->mCompileUnit = compileUnit;
				dbgType->mLanguage = compileUnit->mLanguage;
				//SplitName(atName, dbgType->mTypeName, dbgType->mTemplateParams);
				dbgType->mName = useName;
				if (dbgType->mTypeName == NULL)
					dbgType->mTypeName = useName;

				//if (dbgType->mName == NULL)
					//dbgType->mName = atName;
				int parentNameLen = ((dbgType->mParent != NULL) && (dbgType->mParent->mName != NULL)) ? (int)strlen(dbgType->mParent->mName) : 0;
				int typeNameLen = (dbgType->mTypeName != NULL) ? (int)strlen(dbgType->mTypeName) : 0;
				//int templateParamsLen = (dbgType->mTemplateParams != NULL) ? strlen(dbgType->mTemplateParams) : 0;
				if ((parentNameLen != 0) /*&& (templateParamsLen == 0)*/)
				{
					int nameSepLen = (int)strlen(nameSep);

					int nameLen = parentNameLen +  typeNameLen /*+ templateParamsLen*/;
					if ((parentNameLen > 0) && (nameLen > 0))
						nameLen += nameSepLen;

					char* namePtr = (char*)mAlloc.AllocBytes(nameLen + 1, "DWARF");
					dbgType->mName = namePtr;
					if (parentNameLen > 0)
					{
						memcpy(namePtr, dbgType->mParent->mName, parentNameLen);
						namePtr += parentNameLen;
						if (nameLen > 0)
						{
							memcpy(namePtr, nameSep, nameSepLen);
							namePtr += nameSepLen;
						}
					}
					if (nameLen > 0)
					{
						memcpy(namePtr, useName, typeNameLen);
						namePtr += typeNameLen;
					}
					/*if (templateParamsLen > 0)
					{
						memcpy(namePtr, dbgType->mTemplateParams, templateParamsLen);
						namePtr += templateParamsLen;
					}*/
				}

				dbgType->mTypeCode = DbgType_Null;
				dbgType->mIsDeclaration = atDeclaration;

				if (atByteSize != -1)
				{
					dbgType->mSize = atByteSize;
					dbgType->mSizeCalculated = true;
				}

				switch (entryTag)
				{
				case DW_TAG_base_type:
					// Types that may do fallover to int/uints on size mismatch
					switch (atEncoding)
					{
					case DW_ATE_UTF:
						if (atByteSize == 1)
							dbgType->mTypeCode = DbgType_Utf8;
						else if (atByteSize == 2)
							dbgType->mTypeCode = DbgType_Utf16;
						else
							dbgType->mTypeCode = DbgType_Utf32;
						break;
					case DW_ATE_signed_char:
						if (atByteSize == 1)
							dbgType->mTypeCode = DbgType_SChar;
						else if (atByteSize == 2)
							dbgType->mTypeCode = DbgType_SChar16;
						else if (atByteSize == 4)
							dbgType->mTypeCode = DbgType_SChar32;
						else
							atEncoding = DW_ATE_signed;
						break;
					case DW_ATE_unsigned_char:
						if (atByteSize == 1)
							dbgType->mTypeCode = DbgType_UChar;
						else if (atByteSize == 2)
							dbgType->mTypeCode = DbgType_UChar16;
						else if (atByteSize == 4)
							dbgType->mTypeCode = DbgType_UChar32;
							atEncoding = DW_ATE_unsigned;
						break;
					case DW_ATE_boolean:
						if (atByteSize == 1)
							dbgType->mTypeCode = DbgType_Bool;
						else
							atEncoding = DW_ATE_unsigned;
						break;
					}

					if (dbgType->mTypeCode == DbgType_Null)
					{
						switch (atEncoding)
						{
						case DW_ATE_address:
							if (atByteSize == 0)
								dbgType->mTypeCode = DbgType_Void;
							break;
						case DW_ATE_boolean:
							if (atByteSize == 1)
							{
								dbgType->mTypeCode = DbgType_Bool;
								break;
							}
							//Fall through
						case DW_ATE_signed:
							switch (atByteSize)
							{
							case 1:
								dbgType->mTypeCode = DbgType_i8;
								break;
							case 2:
								dbgType->mTypeCode = DbgType_i16;
								break;
							case 4:
								dbgType->mTypeCode = DbgType_i32;
								break;
							case 8:
								dbgType->mTypeCode = DbgType_i64;
								break;
							case 16:
								dbgType->mTypeCode = DbgType_i128;
								break;
							}
							break;
						case DW_ATE_unsigned:
							switch (atByteSize)
							{
							case 1:
								dbgType->mTypeCode = DbgType_u8;
								break;
							case 2:
								if ((atName != NULL) && (strcmp(atName, "wchar_t") == 0))
									dbgType->mTypeCode = DbgType_UChar16;
								else
									dbgType->mTypeCode = DbgType_u16;
								break;
							case 4:
								dbgType->mTypeCode = DbgType_u32;
								break;
							case 8:
								dbgType->mTypeCode = DbgType_u64;
								break;
							case 16:
								dbgType->mTypeCode = DbgType_u128;
								break;
							}
							break;
						case DW_ATE_float:
							if (atByteSize == 4)
								dbgType->mTypeCode = DbgType_Single;
							else if (atByteSize == 8)
								dbgType->mTypeCode = DbgType_Double;
							else if (atByteSize == 12)
								dbgType->mTypeCode = DbgType_Float96;
							else if (atByteSize == 16)
								dbgType->mTypeCode = DbgType_Float128;
							break;
						case DW_ATE_complex_float:
							if (atByteSize == 8)
								dbgType->mTypeCode = DbgType_ComplexFloat;
							else if (atByteSize == 16)
								dbgType->mTypeCode = DbgType_ComplexDouble;
							else if (atByteSize == 24)
								dbgType->mTypeCode = DbgType_ComplexDouble96;
							else if (atByteSize == 32)
								dbgType->mTypeCode = DbgType_ComplexDouble128;
							break;
						default:
							BF_FATAL("Unknown DW_ATE type");
							break;
						}
					}
					break;
				case DW_TAG_enumeration_type: //TODO: Handle these differently
					dbgType->mTypeCode = DbgType_Enum;
					dbgType->mTypeParam = mAlloc.Alloc<DbgType>();
					if (atByteSize == 8)
						dbgType->mTypeParam->mTypeCode = DbgType_i64;
					else if (atByteSize == 4)
						dbgType->mTypeParam->mTypeCode = DbgType_i32;
					else if (atByteSize == 2)
						dbgType->mTypeParam->mTypeCode = DbgType_i16;
					else if (atByteSize == 1)
						dbgType->mTypeParam->mTypeCode = DbgType_i8;
					else
					{
						BF_DBG_FATAL("Invalid enum type");
					}
					break;
				case DW_TAG_namespace:
					dbgType->mTypeCode = DbgType_Namespace;
					break;
				case DW_TAG_const_type:
					dbgType->mTypeCode = DbgType_Const;
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					break;
				case DW_TAG_rvalue_reference_type:
					dbgType->mTypeCode = DbgType_RValueReference;
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					break;
				case DW_TAG_unspecified_type:
					dbgType->mTypeCode = DbgType_Unspecified;
					dbgType->mTypeName = atName;
					break;
				case DW_TAG_reference_type:
					dbgType->mTypeCode = DbgType_Ref;
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					break;
				case DW_TAG_pointer_type:
					dbgType->mTypeCode = DbgType_Ptr;
					dbgType->mSize = sizeof(addr_target);
					dbgType->mSizeCalculated = true;
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					if (dbgType->mTypeParam != NULL)
						dbgType->mTypeParam->mPtrType = dbgType;
					break;
				case DW_TAG_ptr_to_member_type:
					dbgType->mTypeCode = DbgType_PtrToMember;
					dbgType->mSize = sizeof(addr_target);
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					if (dbgType->mTypeParam != NULL)
						dbgType->mTypeParam->mPtrType = dbgType;
					break;
				case DW_TAG_array_type:
					dbgType->mTypeCode = DbgType_SizedArray;
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					break;
				case DW_TAG_structure_type:
					dbgType->mTypeCode = DbgType_Struct;
					break;
				case DW_TAG_class_type:
					dbgType->mTypeCode = DbgType_Class;
					break;
				case DW_TAG_union_type:
					dbgType->mTypeCode = DbgType_Union;
					break;
				case DW_TAG_typedef:
					dbgType->mTypeCode = DbgType_TypeDef;
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					break;
				case DW_TAG_volatile_type:
					dbgType->mTypeCode = DbgType_Volatile;
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					break;
				case DW_TAG_subroutine_type:
					dbgType->mTypeCode = DbgType_Subroutine;
					if (atType != 0) // Return value
						dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					dbgType->mBlockParam = mAlloc.Alloc<DbgBlock>();
					break;
				case DW_TAG_restrict_type:
					dbgType->mTypeCode = DbgType_Restrict;
					dbgType->mTypeParam = GetOrCreateType(atType, dataMap);
					break;
				}

				newDataPair = MakeDataPair(dbgType);
			}
			break;
		}

		if (hasChildren)
			dataStack.push_back(newDataPair);
	}

	for (auto& abstractOriginEntry : abstractOriginReplaceList)
		abstractOriginEntry.Replace();

	GetLinkedModule()->MapTypes(startingTypeIdx);

	dataPtr = dataEnd;
	return true;
}

void DbgModule::ParseDebugFrameData()
{
	BP_ZONE("ParseDebugFrameData");

	const uint8* data = mDebugFrameData;
	if (data == NULL)
		return;

	mParsedFrameDescriptors = true;
	Dictionary<addr_target, DwCommonFrameDescriptor*> commonFrameDescriptorMap;

	while (true)
	{
		addr_target relSectionAddr = (addr_target)(data - mDebugFrameData);

		int length = GET(int);
		if (length == 0)
			break;

		const uint8* dataEnd = data + length;

		int cieID = GET(int);

		if (cieID < 0)
		{
			BP_ALLOC_T(DwCommonFrameDescriptor);
			DwCommonFrameDescriptor* commonFrameDescriptor = mAlloc.Alloc<DwCommonFrameDescriptor>();

			char version = GET(char);
			commonFrameDescriptor->mDbgModule = this;
			commonFrameDescriptor->mAugmentation = DataGetString(data);

			if (version >= 4)
			{
				commonFrameDescriptor->mPointerSize = GET(int8);
				commonFrameDescriptor->mSegmentSize = GET(int8);
			}

			commonFrameDescriptor->mCodeAlignmentFactor = (int)DecodeULEB128(data);
			commonFrameDescriptor->mDataAlignmentFactor = (int)DecodeSLEB128(data);
			commonFrameDescriptor->mReturnAddressColumn = (int)DecodeULEB128(data);
			commonFrameDescriptor->mInstData = data;
			commonFrameDescriptor->mInstLen = (int)(dataEnd - data);

			mDebugTarget->mCommonFrameDescriptors.push_back(commonFrameDescriptor);
			if (version < 3)
				commonFrameDescriptorMap[relSectionAddr] = commonFrameDescriptor;
			else
				commonFrameDescriptorMap[mDebugFrameAddress + relSectionAddr] = commonFrameDescriptor;
		}
		else
		{
			addr_target lowPC = GET(addr_target);
			addr_target highPC = lowPC + GET(addr_target);

			DwCommonFrameDescriptor* commonFrameDescriptor = commonFrameDescriptorMap[(addr_target)cieID];

			BF_ASSERT(commonFrameDescriptor != NULL);

			typedef decltype(mDebugTarget->mDwFrameDescriptorMap) MapType;
			auto resultPair = mDebugTarget->mDwFrameDescriptorMap.insert(MapType::value_type(lowPC, DwFrameDescriptor()));
			auto frameDescriptor = &resultPair.first->second;
			//frameDescriptor->
			frameDescriptor->mLowPC = lowPC;
			frameDescriptor->mHighPC = highPC;
			frameDescriptor->mInstData = data;
			frameDescriptor->mInstLen = (int)(dataEnd - data);
			frameDescriptor->mCommonFrameDescriptor = commonFrameDescriptor;
		}
		data = dataEnd;
	}
}

void DbgModule::ParseEHFrameData()
{
	const uint8* data = mEHFrameData;
	if (data == NULL)
		return;

	Dictionary<addr_target, DwCommonFrameDescriptor*> commonFrameDescriptorMap;

	while (true)
	{
		addr_target sectionAddress = (addr_target)(data - mEHFrameData);

		int length = GET(int);
		if (length == 0)
			break;

		const uint8* dataEnd = data + length;

		int cieID = GET(int);
		if (cieID <= 0)
		{
			BP_ALLOC_T(DwCommonFrameDescriptor);
			DwCommonFrameDescriptor* commonFrameDescriptor = mAlloc.Alloc<DwCommonFrameDescriptor>();

			char version = GET(char);
			const char* augmentation = DataGetString(data);

			commonFrameDescriptor->mDbgModule = this;
			commonFrameDescriptor->mCodeAlignmentFactor = (int)DecodeULEB128(data);
			commonFrameDescriptor->mDataAlignmentFactor = (int)DecodeSLEB128(data);
			commonFrameDescriptor->mReturnAddressColumn = (int)DecodeULEB128(data);
			commonFrameDescriptor->mAugmentation = augmentation;

			if (*augmentation == 'z')
			{
				++augmentation;
				int augLen = (int)DecodeULEB128(data);
				commonFrameDescriptor->mAugmentationLength = augLen;
				const uint8* augEnd = data + augLen;
				while (*augmentation != '\0')
				{
					if (*augmentation == 'R')
						commonFrameDescriptor->mAddressPointerEncoding = (int) GET(uint8);
					else if (*augmentation == 'P')
					{
						int encodingType = GET(uint8);
						BF_ASSERT(encodingType == 0);
						commonFrameDescriptor->mLSDARoutine = GET(addr_target);
					}
					else if (*augmentation == 'L')
						commonFrameDescriptor->mLSDAPointerEncodingFDE = GET(uint8);
					else if (*augmentation == 'S')
					{
						// mIsSignalHandler - on return from stack frame, CFA is before next instruction rather than after it
					}
					else
						BF_FATAL("Unknown CIE augmentation");
					++augmentation;
				}
				data = augEnd;
			}

			commonFrameDescriptor->mInstData = data;
			commonFrameDescriptor->mInstLen = (int)(dataEnd - data);

			mDebugTarget->mCommonFrameDescriptors.push_back(commonFrameDescriptor);
			commonFrameDescriptorMap[sectionAddress] = commonFrameDescriptor;
		}
		else
		{
			int ciePos = (int)(sectionAddress - cieID) + 4;
			DwCommonFrameDescriptor* commonFrameDescriptor = commonFrameDescriptorMap[(addr_target)ciePos];

			addr_target lowPC;
			addr_target highPC;

			if (commonFrameDescriptor->mAddressPointerEncoding == (DW_EH_PE_pcrel | DW_EH_PE_sdata4))
			{
				lowPC = GET(int);
				lowPC += mEHFrameAddress + sectionAddress + 8;
				highPC = lowPC + GET(int);
			}
			else
			{
				lowPC = GET(int);
				highPC = lowPC + GET(int);
			}

			typedef decltype(mDebugTarget->mDwFrameDescriptorMap) MapType;
			auto resultPair = mDebugTarget->mDwFrameDescriptorMap.insert(MapType::value_type(lowPC, DwFrameDescriptor()));
			auto frameDescriptor = &resultPair.first->second;
			frameDescriptor->mLSDARoutine = commonFrameDescriptor->mLSDARoutine;

			const char* augmentation = commonFrameDescriptor->mAugmentation;
			if (*augmentation == 'z')
			{
				int augLen = GET(uint8);
				const uint8* augEnd = data + augLen;
				++augmentation;
				while (*augmentation != '\0')
				{
					if (*augmentation == 'R')
					{
					}
					else if (*augmentation == 'P')
					{
					}
					else if (*augmentation == 'L')
					{
						BF_ASSERT(commonFrameDescriptor->mLSDAPointerEncodingFDE == 0);
						frameDescriptor->mLSDARoutine = GET(addr_target);
					}
					else if (*augmentation == 'S')
					{
					}
					else
						BF_FATAL("Unknown CIE augmentation");
					augmentation++;
				}
				data = augEnd;
			}

			frameDescriptor->mLowPC = lowPC;
			frameDescriptor->mHighPC = highPC;
			frameDescriptor->mInstData = data;
			frameDescriptor->mInstLen = (int)(dataEnd - data);
			frameDescriptor->mCommonFrameDescriptor = commonFrameDescriptor;
		}
		data = dataEnd;
	}
}

void DbgModule::FlushLineData(DbgSubprogram* curSubprogram, std::list<DbgLineData>& queuedLineData)
{
}

DbgSrcFile* DbgModule::AddSrcFile(DbgCompileUnit* compileUnit, const String& srcFilePath)
{
	DbgSrcFile* dwSrcFile = mDebugTarget->AddSrcFile(srcFilePath);
	if (compileUnit != NULL)
	{
		DbgSrcFileReference srcFileRef;
		srcFileRef.mSrcFile = dwSrcFile;
		srcFileRef.mCompileUnit = compileUnit;
		compileUnit->mSrcFileRefs.push_back(srcFileRef);
	}

	return dwSrcFile;
}

bool DbgModule::ParseDebugLineInfo(const uint8*& dataPtr, int compileUnitIdx)
{
	BP_ZONE("ParseDebugLineInfo");

	const uint8* data = dataPtr;
	const int startOffset = (int)(data - mDebugLineData);
	int length = GET(int);
	if (length == 0)
		return false;
	DbgCompileUnit* dwCompileUnit = mCompileUnits[compileUnitIdx];
	const uint8* dataEnd = data + length;
	short version = GET(short);
	int headerLength = GET(int);
	char minimumInstructionLength = GET(char);
	int maximumOperationsPerInstruction = 1;
	char defaultIsStmt = GET(char);
	char lineBase = GET(char);
	char lineRange = GET(char);
	char opcodeBase = GET(char);
	for (int i = 0; i < opcodeBase - 1; i++)
	{
		char standardOpcodeLengths = GET(char);
	}

	Array<const char*> directoryNames;
	while (true)
	{
		const char* name = DataGetString(data);
		if (name[0] == 0)
			break;
		directoryNames.push_back(name);
	}

	DbgSrcFileReference* dwSrcFileRef = NULL;

	HashSet<String> foundPathSet;

	int curFileIdx = 0;

	DbgSubprogram* curSubprogram = NULL;

	#define ADD_LINEDATA(lineData) \
		lineBuilder.Add(dwCompileUnit, lineData, dwSrcFileRef->mSrcFile, NULL);

	while (true)
	{
		const char* path = DataGetString(data);
		if (path[0] == 0)
			break;
		int directoryIdx = (int)DecodeULEB128(data);
		int lastModificationTime = (int)DecodeULEB128(data);
		int fileLength = (int)DecodeULEB128(data);

		String filePath;
		if (directoryIdx > 0)
			filePath = String(directoryNames[directoryIdx - 1]) + "/";
		filePath += path;
		filePath = GetAbsPath(filePath, dwCompileUnit->mCompileDir);
		AddSrcFile(dwCompileUnit, filePath.c_str());
	}

	if (dwCompileUnit->mSrcFileRefs.size() > 0)
		dwSrcFileRef = &dwCompileUnit->mSrcFileRefs.front();

	DbgLineDataBuilder lineBuilder(this);

	bool queuedPostPrologue = false;

	DbgLineDataState dwLineData;
	dwLineData.mLine = 0;
	dwLineData.mRelAddress = 0;
	dwLineData.mOpIndex = 0;
	dwLineData.mBasicBlock = false;
	dwLineData.mDiscriminator = 0;
	dwLineData.mIsStmt = defaultIsStmt != 0;
	dwLineData.mIsa = 0;
	dwLineData.mColumn = -2;

	while (data < dataEnd)
	{
		uint8_t opcode = GET(uint8_t);
		switch (opcode)
		{
		case DW_LNS_extended_op:
			{
				int len = (int)DecodeULEB128(data);
				uint8_t exOpcode = GET(uint8_t);
				switch (exOpcode)
				{
				case DW_LNE_end_sequence:
					{
						ADD_LINEDATA(dwLineData);

						dwSrcFileRef = &dwCompileUnit->mSrcFileRefs[0];
						dwLineData.mLine = 0;
						dwLineData.mRelAddress = 0;
						dwLineData.mOpIndex = 0;
						dwLineData.mBasicBlock = false;
						dwLineData.mDiscriminator = 0;
						dwLineData.mIsStmt = defaultIsStmt != 0;
						dwLineData.mIsa = 0;
						dwLineData.mColumn = -2;
					}
					break;
				case DW_LNE_set_address:
					dwLineData.mRelAddress = (uint32)(RemapAddr(GET(addr_target)) - mImageBase);
					break;
				case DW_LNE_define_file:
					{
						const char* path = DataGetString(data);
						int directoryIdx = (int)DecodeULEB128(data);
						int lastModificationTime = (int)DecodeULEB128(data);
						int fileLength = (int)DecodeULEB128(data);
					}
					break;
				case DW_LNE_set_discriminator:
					dwLineData.mDiscriminator = (int)DecodeULEB128(data);
					break;
				}
			}
			break;
		case DW_LNS_copy:
			ADD_LINEDATA(dwLineData);

			dwLineData.mDiscriminator = 0;
			dwLineData.mBasicBlock = false;
			break;
		case DW_LNS_advance_pc:
			{
				int advance = (int)DecodeULEB128(data);
				dwLineData.mRelAddress += advance;
				// How to advance opCode addr?
			}
			break;
		case DW_LNS_advance_line:
			{
				int advance = (int)DecodeSLEB128(data);
				dwLineData.mLine += advance;
			}
			break;
		case DW_LNS_set_file:
			{
				curFileIdx = (int)DecodeULEB128(data) - 1;
				dwSrcFileRef = &dwCompileUnit->mSrcFileRefs[curFileIdx];
				//dwLineData.mSrcFileRef = dwSrcFileRef;
			}
			break;
		case DW_LNS_set_column:
			{
				dwLineData.mColumn = (int)DecodeULEB128(data) - 1;
			}
			break;
		case DW_LNS_negate_stmt:
			{
				dwLineData.mIsStmt = !dwLineData.mIsStmt;
			}
			break;
		case DW_LNS_set_basic_block:
			{
				dwLineData.mBasicBlock = true;
			}
			break;
		case DW_LNS_const_add_pc:
			{
				int adjustedOpcode = 255 - opcodeBase;
				int opAdvance = adjustedOpcode / lineRange;
				uint32 newAddress = dwLineData.mRelAddress + minimumInstructionLength * ((dwLineData.mOpIndex + opAdvance) / maximumOperationsPerInstruction);
				int newOpIndex = (dwLineData.mOpIndex + opAdvance) % maximumOperationsPerInstruction;

				dwLineData.mRelAddress = newAddress;
				dwLineData.mOpIndex = newOpIndex;
			}
			break;
		case DW_LNS_fixed_advance_pc:
			{
				uint16_t advance = GET(uint16_t);
				dwLineData.mRelAddress += advance;
				dwLineData.mOpIndex = 0;
			}
			break;
		case DW_LNS_set_prologue_end:
			{
				queuedPostPrologue = true;
			}
			break;
		case DW_LNS_set_epilogue_begin:
			{
				dwLineData.mColumn = -2;
			}
			break;
		case DW_LNS_set_isa:
			{
				dwLineData.mIsa = (int)DecodeULEB128(data);
			}
			break;
		default:
			{
				// Special opcode
				int adjustedOpcode = opcode - opcodeBase;
				int opAdvance = adjustedOpcode / lineRange;
				uint32 oldAddress = dwLineData.mRelAddress;
				uint32 newAddress = dwLineData.mRelAddress + minimumInstructionLength * ((dwLineData.mOpIndex + opAdvance) / maximumOperationsPerInstruction);
				int newOpIndex = (dwLineData.mOpIndex + opAdvance) % maximumOperationsPerInstruction;
				int lineIncrement = lineBase + (adjustedOpcode % lineRange);

				dwLineData.mLine += lineIncrement;
				dwLineData.mRelAddress = newAddress;
				dwLineData.mOpIndex = newOpIndex;

				DbgLineData* lastLineData = NULL;

				if ((newAddress == oldAddress) && (queuedPostPrologue) && (curSubprogram != NULL) && (curSubprogram->mBlock.mLowPC == newAddress))
				{
					// Adjust this line later
					ADD_LINEDATA(dwLineData);
				}

				queuedPostPrologue = false;
			}
			break;
		}
	}

	lineBuilder.Commit();

	dataPtr = data;

	return true;
}

addr_target DbgModule::GetHotTargetAddress(DbgHotTargetSection* hotTargetSection)
{
	if ((hotTargetSection->mTargetSectionAddr == NULL) && (hotTargetSection->mDataSize > 0))
	{
		if (hotTargetSection->mNoTargetAlloc)
			return 0;

		BfLogDbg("DbgModule::GetHotTargetAddress %p %p\n", this, hotTargetSection);
		hotTargetSection->mTargetSectionAddr = mDebugger->AllocHotTargetMemory(hotTargetSection->mDataSize, hotTargetSection->mCanExecute, hotTargetSection->mCanWrite, &hotTargetSection->mTargetSectionSize);
		hotTargetSection->mImageOffset = (int)mImageSize;

		if (mImageBase == NULL)
		{
			mImageBase = hotTargetSection->mTargetSectionAddr;
			mOrigImageData->mAddr = mImageBase;
		}
		mImageSize += hotTargetSection->mTargetSectionSize;

		/*if (mExceptionData == hotTargetSection->mData)
			mExceptionDataRVA = (addr_target)(hotTargetSection->mTargetSectionAddr - mImageBase);*/
	}
	return hotTargetSection->mTargetSectionAddr;
}

uint8* DbgModule::GetHotTargetData(addr_target address)
{
	for (int sectNum = 0; sectNum < (int)mHotTargetSections.size(); sectNum++)
	{
		if (mHotTargetSections[sectNum] != NULL)
		{
			DbgHotTargetSection* hotTargetSection = mHotTargetSections[sectNum];
			if ((address >= hotTargetSection->mTargetSectionAddr) && (address < hotTargetSection->mTargetSectionAddr + hotTargetSection->mTargetSectionSize))
			{
				return hotTargetSection->mData + (address - hotTargetSection->mTargetSectionAddr);
			}
		}
	}
	return NULL;
}

void DbgModule::DoReloc(DbgHotTargetSection* hotTargetSection, COFFRelocation& coffReloc, addr_target resolvedSymbolAddr, PE_SymInfo* symInfo)
{
#ifdef BF_DBG_32
	if (coffReloc.mType == IMAGE_REL_I386_DIR32)
	{
		*(uint32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += resolvedSymbolAddr;
	}
	else if (coffReloc.mType == IMAGE_REL_I386_DIR32NB)
	{
		GetHotTargetAddress(hotTargetSection); // Just to make sure we have mImageBase
		// We were previously using mImageBase instead of mDebugTarget->mTargetBinary->mImageBase.  Was there a reason for that?
		//  It was causing hot-loaded jump tables to have invalid addresses since the need to be relative to __ImageBase
		*(uint32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += (uint32)(resolvedSymbolAddr - GetTargetImageBase());
	}
	else if (coffReloc.mType == IMAGE_REL_I386_REL32)
	{
		addr_target myAddr = GetHotTargetAddress(hotTargetSection) + coffReloc.mVirtualAddress;
		*(uint32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += resolvedSymbolAddr - myAddr - sizeof(int32);
	}
	else if (coffReloc.mType == IMAGE_REL_I386_SECTION)
	{
// 		auto linkedModule = GetLinkedModule();
// 		addr_target mappedAddr = resolvedSymbolAddr & ~0x7FFFFFF;
// 		int* encodingPtr = NULL;
// 		if (linkedModule->mSecRelEncodingMap.TryAdd(mappedAddr, NULL, &encodingPtr))
// 		{
// 			*encodingPtr = (int)linkedModule->mSecRelEncodingVec.size();
// 			linkedModule->mSecRelEncodingVec.push_back(mappedAddr);
// 		}
// 		*(uint16*)(hotTargetSection->mData + coffReloc.mVirtualAddress) = 0x8000 | *encodingPtr;
		*(uint16*)(hotTargetSection->mData + coffReloc.mVirtualAddress) = 0;
	}
	else if (coffReloc.mType == IMAGE_REL_I386_SECREL)
	{
		//*(uint32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += symInfo->mValue;
		*(uint32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += resolvedSymbolAddr;
	}
	else
	{
		BF_ASSERT(0=="Invalid COFF reloc type");
	}
#else

	// CodeView uses SECTION:SECREL locations, and we just want to find a mapping such that
	//  COFF::GetSectionAddr can map it to the 64-bit address.  We do this by encoding the
	//  lower 31 bits in the SECREL (allowing a 31-bit offset at the destination as well)
	//  and then we use a 15-bit key to map the upper bits

	if (coffReloc.mType == IMAGE_REL_AMD64_REL32)
	{
		addr_target myAddr = GetHotTargetAddress(hotTargetSection) + coffReloc.mVirtualAddress;
		intptr_target addrOffset = resolvedSymbolAddr - myAddr - sizeof(int32);

		BF_ASSERT((int64)(int32)addrOffset == addrOffset);

		*(uint32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += (int32)addrOffset;
	}
	else if (coffReloc.mType == IMAGE_REL_AMD64_SECTION)
	{
		/*if (symInfo != NULL)
		{
			*(uint16*)(hotTargetSection->mData + coffReloc.mVirtualAddress) = symInfo->mSectionNum;
		}
		else*/
		{
			auto linkedModule = GetLinkedModule();
			addr_target mappedAddr = resolvedSymbolAddr & ~0x7FFFFFF;
			/*auto pair = linkedModule->mSecRelEncodingMap.insert(std::make_pair(mappedAddr, (int)linkedModule->mSecRelEncodingMap.size()));
			if (pair.second)
				linkedModule->mSecRelEncodingVec.push_back(mappedAddr);*/
			int* encodingPtr = NULL;
			if (linkedModule->mSecRelEncodingMap.TryAdd(mappedAddr, NULL, &encodingPtr))
			{
				*encodingPtr = (int)linkedModule->mSecRelEncodingVec.size();
				linkedModule->mSecRelEncodingVec.push_back(mappedAddr);
			}

			//*(uint16*)(hotTargetSection->mData + coffReloc.mVirtualAddress) = 0x8000 | pair.first->second;
			*(uint16*)(hotTargetSection->mData + coffReloc.mVirtualAddress) = 0x8000 | *encodingPtr;
		}
	}
	else if (coffReloc.mType == IMAGE_REL_AMD64_SECREL)
	{
		auto linkedModule = GetLinkedModule();
		if ((resolvedSymbolAddr >= linkedModule->mTLSAddr) && (resolvedSymbolAddr < linkedModule->mTLSAddr + linkedModule->mTLSSize))
		{
			// Make relative to actual TLS data
			resolvedSymbolAddr -= linkedModule->mTLSAddr;
		}

		*(uint32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += (uint32)(resolvedSymbolAddr & 0x7FFFFFF);
	}
	else if (coffReloc.mType == IMAGE_REL_AMD64_ADDR64)
	{
		*(uint64*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += resolvedSymbolAddr;
	}
	else if (coffReloc.mType == IMAGE_REL_AMD64_ADDR32NB)
	{
		GetHotTargetAddress(hotTargetSection); // Just to make sure we have mImageBase
		// We were previously using mImageBase instead of mDebugTarget->mTargetBinary->mImageBase.  Was there a reason for that?
		//  It was causing hot-loaded jump tables to have invalid addresses since the need to be relative to __ImageBase
		*(uint32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += (uint32)(resolvedSymbolAddr - GetTargetImageBase());
		//*(int32*)(hotTargetSection->mData + coffReloc.mVirtualAddress) += secRelAddr;
	}
	else
	{
		BF_ASSERT(0=="Invalid COFF reloc type");
	}
#endif
}

bool DbgModule::IsHotSwapPreserve(const String& name)
{
	// We have different rules for overwriting symbols in DWARF vs CodeView
	//  Since MS mangling includes return types, we know that a type change of a static
	//  member will mangle to a new name whereas with DWARF we DO want a new
	//  address if the type changes but we can't tell that based on the mangle alone,
	//  thus the reliance on the side table of mStaticVariables.  We still do need
	//  to determine whether the symbol is data (and thus we do preserve) or a method
	//  (in which case we don't)
	if ((mDbgFlavor == DbgFlavor_MS) && (BfDemangler::IsData(name)))
	{
 		if ((!name.StartsWith("?")) && (name.Contains("sBfTypeData"))) // We DO need to replace the fields/methods/etc but not the base sBfTypeData
 			return false;
		if (name.StartsWith("?bf_hs_replace_"))
			return false;
		return true;
	}
	const char* prefix = "bf_hs_preserve@";
	return strncmp(name.c_str(), prefix, strlen(prefix)) == 0;
}

void DbgModule::ParseHotTargetSections(DataStream* stream, addr_target* resolvedSymbolAddrs)
{
	auto mainModule = mDebugTarget->mTargetBinary;
	mainModule->ParseSymbolData();

	String name;
	for (int sectNum = 0; sectNum < (int)mHotTargetSections.size(); sectNum++)
	{
		if (mHotTargetSections[sectNum] != NULL)
		{
			DbgHotTargetSection* hotTargetSection = mHotTargetSections[sectNum];
			stream->SetPos(hotTargetSection->mPointerToRelocations);
			for (int relocIdx = 0; relocIdx < hotTargetSection->mNumberOfRelocations; relocIdx++)
			{
				COFFRelocation coffReloc;
				stream->Read(&coffReloc, sizeof(COFFRelocation));

				PE_SymInfo* symInfo = (PE_SymInfo*)&mSymbolData[coffReloc.mSymbolTableIndex * 18];
				//const char* symName = mSymbolData[coffReloc.mSymbolTableIndex];

				bool isStaticSymbol = symInfo->mStorageClass == COFF_SYM_CLASS_STATIC;

				if (symInfo->mNameOfs[0] != 0)
				{
					if (symInfo->mName[7] != 0)
					{
						// Name is exactly 8 chars, not null terminated yet
						name = String(symInfo->mName, symInfo->mName + 8);
					}
					else
						name = symInfo->mName;
				}
				else
					name = mStringTable + symInfo->mNameOfs[1];

				bool didNameMatch = false;

				addr_target resolvedSymbolAddr = resolvedSymbolAddrs[coffReloc.mSymbolTableIndex];

#ifdef BF_DBG_32
				bool needsSymbolAddr = (coffReloc.mType == IMAGE_REL_I386_DIR32) || (coffReloc.mType == IMAGE_REL_I386_REL32) || (coffReloc.mType == IMAGE_REL_I386_SECREL) || (coffReloc.mType == IMAGE_REL_I386_SECTION);
				if (name[0] == '_')
					name.Remove(0, 1);
#else
				bool needsSymbolAddr = (coffReloc.mType == IMAGE_REL_AMD64_ADDR64) || (coffReloc.mType == IMAGE_REL_AMD64_ADDR32) || (coffReloc.mType == IMAGE_REL_AMD64_ADDR32NB) ||
					((coffReloc.mType >= IMAGE_REL_AMD64_REL32) || (coffReloc.mType <= IMAGE_REL_AMD64_REL32_5));
#endif

				bool isHsPrev = false;
				if (name.StartsWith("bf_hs_prev@"))
				{
					isHsPrev = true;
					name.Remove(0, 11);
				}

				bool deferResolve = false;

				if ((resolvedSymbolAddr == 0) && (needsSymbolAddr))
				{
					bool isHotSwapPreserve = IsHotSwapPreserve(name);
					if ((symInfo->mSectionNum == 0) || (isHotSwapPreserve) || (isHsPrev))
					{
						auto origSymbolEntry = mainModule->mSymbolNameMap.Find(name.c_str());
						if (origSymbolEntry != NULL)
						{
							resolvedSymbolAddr = origSymbolEntry->mValue->mAddress;
						}
						else
						{
							//BF_FATAL("Symbol lookup error");
							deferResolve = true;
						}
					}

					if ((symInfo->mSectionNum != 0) && (resolvedSymbolAddr == NULL))
					{
						DbgHotTargetSection* refHotTargetSection = mHotTargetSections[symInfo->mSectionNum - 1];
						resolvedSymbolAddr = GetHotTargetAddress(refHotTargetSection) + symInfo->mValue;
						//  Using the !hotTargetSection->mNoTargetAlloc check down here caused us to not properly remap reloaded
						//   static members in the debug info.  Even though we parse the debug info before we apply the deferred
						//   resolves, the mLocData points into the original data so we still get it remapped when we use that
						//   mLocData
						if (/*(!hotTargetSection->mNoTargetAlloc) &&*/ ((refHotTargetSection->mData == NULL) || (refHotTargetSection->mNoTargetAlloc)) &&
							(!isStaticSymbol))
							deferResolve = true;
						else
							deferResolve = false;
					}
				}

				if (deferResolve)
				{
					// It's a static field, defer resolution, but don't bother replacing for debug info sections
					DbgDeferredHotResolve* deferredResolve = mDeferredHotResolveList.Alloc();
					deferredResolve->mHotTargetSection = hotTargetSection;
					deferredResolve->mName = name;
					deferredResolve->mNewAddr = resolvedSymbolAddr;
					deferredResolve->mReloc = coffReloc;
					continue;
				}
				else
				{
					resolvedSymbolAddrs[coffReloc.mSymbolTableIndex] = resolvedSymbolAddr;
					DoReloc(hotTargetSection, coffReloc, resolvedSymbolAddr, symInfo);
				}
			}
		}
	}
}

void DbgModule::CommitHotTargetSections()
{
	BfLogDbg("DbgModule::CommitHotTargetSections %p\n", this);

	for (int sectNum = 0; sectNum < (int)mHotTargetSections.size(); sectNum++)
	{
		if (mHotTargetSections[sectNum] != NULL)
		{
			DbgHotTargetSection* hotTargetSection = mHotTargetSections[sectNum];

			addr_target hotAddr = GetHotTargetAddress(hotTargetSection);
			if (hotAddr != 0)
			{
// 				void* imageDestPtr = mOrigImageData->mBlocks[0] + hotTargetSection->mImageOffset;
// 				if (hotTargetSection->mData != NULL)
// 					memcpy(imageDestPtr, hotTargetSection->mData, hotTargetSection->mDataSize);
// 				else
// 					memset(imageDestPtr, 0, hotTargetSection->mDataSize);

				BF_ASSERT(mOrigImageData->mAddr != 0);

				void* imageDestPtr = hotTargetSection->mData;
				bool isTemp = false;
				if (imageDestPtr == NULL)
				{
					imageDestPtr = new uint8[hotTargetSection->mDataSize];
					memset(imageDestPtr, 0, hotTargetSection->mDataSize);
					isTemp = true;
				}

				if (hotTargetSection->mCanExecute)
				{
					bool success = mDebugger->WriteInstructions(hotAddr, imageDestPtr, hotTargetSection->mDataSize);
					BF_ASSERT(success);
				}
				else
				{
					bool success = mDebugger->WriteMemory(hotAddr, imageDestPtr, hotTargetSection->mDataSize);
					BF_ASSERT(success);
				}

				if (isTemp)
					delete imageDestPtr;
			}
		}
	}
}

void DbgModule::HotReplaceType(DbgType* newType)
{
	auto linkedModule = GetLinkedModule();

	newType->PopulateType();
	DbgType* primaryType = linkedModule->GetPrimaryType(newType);
	if (primaryType == newType)
	{
		// There was no previous type
		BF_ASSERT(primaryType->mHotNewType == NULL);
		return;
	}

	if (primaryType->mHotNewType != newType)
	{
		// We have already pulled in the new data from a previous new type
		BF_ASSERT(primaryType->mHotNewType == NULL);
		return;
	}
	primaryType->mHotNewType = NULL;

	primaryType->PopulateType();
	linkedModule->ParseGlobalsData();
	linkedModule->ParseSymbolData();

	if (primaryType->mNeedsGlobalsPopulated)
	{
		// These aren't proper TPI types so we don't have any method declarations until we PopulateTypeGlobals
		linkedModule->PopulateTypeGlobals(primaryType);
	}
	for (auto methodNameEntry : primaryType->mMethodNameList)
	{
		if (methodNameEntry->mCompileUnitId != -1)
		{
			linkedModule->MapCompileUnitMethods(methodNameEntry->mCompileUnitId);
			methodNameEntry->mCompileUnitId = -1;
		}
	}

	// Now actually remove the linedata from the defining module
	HashSet<DbgSrcFile*> checkedFiles;
	for (auto method : primaryType->mMethodList)
	{
		//method->mWasModuleHotReplaced = true;
		method->mHotReplaceKind = DbgSubprogram::HotReplaceKind_Orphaned; // May be temporarily orphaned

		if (method->mLineInfo == NULL)
			continue;

		//FIXME: Hot replacing lines
		DbgSrcFile* lastSrcFile = NULL;
		checkedFiles.Clear();

		int prevCtx = -1;
		auto inlineRoot = method->GetRootInlineParent();
 		for (int lineIdx = 0; lineIdx < method->mLineInfo->mLines.mSize; lineIdx++)
 		{
 			auto& lineData = method->mLineInfo->mLines[lineIdx];
			if (lineData.mCtxIdx != prevCtx)
			{
				auto ctxInfo = inlineRoot->mLineInfo->mContexts[lineData.mCtxIdx];
				auto srcFile = ctxInfo.mSrcFile;
				prevCtx = lineData.mCtxIdx;
				if (srcFile != lastSrcFile)
				{
					if (checkedFiles.Add(srcFile))
					{
						// Remove linedata for old type
						//  These go into a hot-replaced list so we can still bind to them -- that is necessary because
						//   we may still have old versions of this method running (and may forever, if its in a loop on some thread)
						//   since we only patch entry points
						//srcFile->RemoveLines(primaryType->mCompileUnit->mDbgModule, primaryType->mCompileUnit, true);

						//srcFile->RemoveLines(primaryType->mCompileUnit->mDbgModule, method, true);
						srcFile->RemoveLines(method->mCompileUnit->mDbgModule, method, true);
					}

					lastSrcFile = srcFile;
				}
			}
 		}
	}

	//DbgType* primaryType = newType->GetPrimaryType();

	// We need to keep a persistent list of hot replaced methods so we can set hot jumps
	//  in old methods that may still be on the callstack.  These entries get removed when
	//  we unload unused hot files in
	while (!primaryType->mMethodList.IsEmpty())
	{
		auto method = primaryType->mMethodList.PopFront();

		method->PopulateSubprogram();

		primaryType->mHotReplacedMethodList.PushFront(method);
		mHotPrimaryTypes.Add(primaryType);
	}

	Dictionary<StringView, DbgSubprogram*> oldProgramMap;
	for (auto oldMethod : primaryType->mHotReplacedMethodList)
	{
		oldMethod->PopulateSubprogram();
		if (oldMethod->mBlock.IsEmpty())
			continue;
		auto symInfo = mDebugTarget->mSymbolMap.Get(oldMethod->mBlock.mLowPC);
		if (symInfo != NULL)
		{
			oldProgramMap.TryAdd(symInfo->mName, oldMethod);
		}
	}

	bool setHotJumpFailed = false;
	while (!newType->mMethodList.IsEmpty())
	{
		DbgSubprogram* newMethod = newType->mMethodList.PopFront();
		if (!newMethod->mBlock.IsEmpty())
		{
			BfLogDbg("Hot added new method %p %s Address:%p\n", newMethod, newMethod->mName, newMethod->mBlock.mLowPC);

			newMethod->PopulateSubprogram();

			auto symInfo = mDebugTarget->mSymbolMap.Get(newMethod->mBlock.mLowPC);
			if (symInfo != NULL)
			{
				DbgSubprogram* oldMethod = NULL;
				if (oldProgramMap.TryGetValue(symInfo->mName, &oldMethod))
				{
					bool doHotJump = false;

					if (oldMethod->Equals(newMethod))
					{
						doHotJump = true;
					}
					else
					{
						// When mangles match but the actual signatures don't match, that can mean that the call signature was changed
						// and thus it's actually a different method and shouldn't hot jump OR it could be lambda whose captures changed.
						// When the lambda captures change, the user didn't actually enter a different signature so we want to do a hard
						// fail if the old code gets called to avoid confusion of "why aren't my changes working?"

						// If we removed captures then we can still do the hot jump. Otherwise we have to fail...
						doHotJump = false;
						if ((oldMethod->IsLambda()) && (oldMethod->Equals(newMethod, true)) &&
							(oldMethod->mHasThis) && (newMethod->mHasThis))
						{
							auto oldParam = oldMethod->mParams.front();
							auto newParam = newMethod->mParams.front();

							if ((oldParam->mType->IsPointer()) && (newParam->mType->IsPointer()))
							{
								auto oldType = oldParam->mType->mTypeParam->GetPrimaryType();
								oldType->PopulateType();
								auto newType = newParam->mType->mTypeParam->GetPrimaryType();
								newType->PopulateType();
								if ((oldType->IsStruct()) && (newType->IsStruct()))
								{
									bool wasMatch = true;

									auto oldMember = oldType->mMemberList.front();
									auto newMember = newType->mMemberList.front();
									while (newMember != NULL)
									{
										if (oldMember == NULL)
										{
											wasMatch = false;
											break;
										}

										if ((oldMember->mName == NULL) || (newMember->mName == NULL))
										{
											wasMatch = false;
											break;
										}

										if (strcmp(oldMember->mName, newMember->mName) != 0)
										{
											wasMatch = false;
											break;
										}

										if (!oldMember->mType->Equals(newMember->mType))
										{
											wasMatch = false;
											break;
										}

										oldMember = oldMember->mNext;
										newMember = newMember->mNext;
									}

									if (wasMatch)
										doHotJump = true;
								}
							}

							if (!doHotJump)
							{
								mDebugTarget->mDebugger->PhysSetBreakpoint(oldMethod->mBlock.mLowPC);
								oldMethod->mHotReplaceKind = DbgSubprogram::HotReplaceKind_Invalid;
							}
						}
					}

					if (doHotJump)
					{
						if (!setHotJumpFailed)
						{
							if (!mDebugger->SetHotJump(oldMethod, newMethod->mBlock.mLowPC, (int)(newMethod->mBlock.mHighPC - newMethod->mBlock.mLowPC)))
								setHotJumpFailed = true;
						}
						oldMethod->mHotReplaceKind = DbgSubprogram::HotReplaceKind_Replaced;
					}
				}
			}
		}
		newMethod->mParentType = primaryType;
		primaryType->mMethodList.PushBack(newMethod);
	}

	//mDebugTarget->mSymbolMap.Get()

// 	bool setHotJumpFailed = false;
// 	while (!newType->mMethodList.IsEmpty())
// 	{
// 		DbgSubprogram* newMethod = newType->mMethodList.PopFront();
// 		if (!newMethod->mBlock.IsEmpty())
// 		{
// 			newMethod->PopulateSubprogram();
//
// 			bool found = false;
// 			for (auto oldMethod : primaryType->mHotReplacedMethodList)
// 			{
// 				if (oldMethod->mBlock.IsEmpty())
// 					continue;
// 				if (oldMethod->Equals(newMethod))
// 				{
// 					if (!setHotJumpFailed)
// 					{
// 						if (!mDebugger->SetHotJump(oldMethod, newMethod))
// 							setHotJumpFailed = true;
// 						oldMethod->mWasHotReplaced = true;
// 					}
// 				}
// 			}
// 		}
// 		newMethod->mParentType = primaryType;
// 		primaryType->mMethodList.PushBack(newMethod);
// 	}

	primaryType->mCompileUnit->mWasHotReplaced = true;

	primaryType->mNeedsGlobalsPopulated = newType->mNeedsGlobalsPopulated;
	primaryType->mUsingNamespaces = newType->mUsingNamespaces;
	primaryType->mMemberList = newType->mMemberList;
	primaryType->mCompileUnit = newType->mCompileUnit;
}

bool DbgModule::CanRead(DataStream* stream, DebuggerResult* outResult)
{
	PEHeader hdr;
	memset(&hdr, 0, sizeof(hdr));

	PE_NTHeaders ntHdr;
	memset(&ntHdr, 0, sizeof(ntHdr));

	stream->Read(&hdr, sizeof(PEHeader));
	stream->SetPos(hdr.e_lfanew);

	stream->Read(&ntHdr, sizeof(PE_NTHeaders));

	if ((hdr.e_magic != PE_DOS_SIGNATURE) || (ntHdr.mSignature != PE_NT_SIGNATURE))
	{
		*outResult = DebuggerResult_UnknownError;
		return false;
	}

#ifdef BF_DBG_32
	if (ntHdr.mFileHeader.mMachine != PE_MACHINE_X86)
	{
		if (ntHdr.mFileHeader.mMachine == PE_MACHINE_X64)
			*outResult = DebuggerResult_WrongBitSize;
		else
			*outResult = DebuggerResult_UnknownError;
		return false;
	}
#else
	if (ntHdr.mFileHeader.mMachine != PE_MACHINE_X64)
	{
		if (ntHdr.mFileHeader.mMachine == PE_MACHINE_X86)
			*outResult = DebuggerResult_WrongBitSize;
		else
			*outResult = DebuggerResult_UnknownError;
		return false;
	}
#endif

	return true;
}

const char* DbgModule::GetStringTable(DataStream* stream, int stringTablePos)
{
	if (mStringTable == NULL)
	{
		int prevPos = stream->GetPos();
		stream->SetPos(stringTablePos);
		int strTableSize = 0;
		stream->Read(&strTableSize, 4);
		if (strTableSize != 0)
		{
			strTableSize -= 4;

			char* strTableData = new char[strTableSize + 4];
			memcpy(strTableData, &strTableSize, 4);
			stream->Read(strTableData + 4, strTableSize);
			mStringTable = strTableData;
		}

		stream->SetPos(prevPos);
	}
	return mStringTable;
}

bool DbgModule::ReadCOFF(DataStream* stream, DbgModuleKind moduleKind)
{
	BP_ZONE("DbgModule::ReadCOFF");

 	//if (this == mDebugTarget->mTargetBinary)
 		//mMemReporter = new MemReporter();

	BfLogDbg("DbgModule::ReadCOFF %p %s\n", this, mFilePath.c_str());

	if (mMemReporter != NULL)
	{
		mMemReporter->BeginSection(StrFormat("Module: %s", mFilePath.c_str()));
		mMemReporter->Add(mImageSize);
	}
	defer
	(
		if (mMemReporter != NULL)
			mMemReporter->EndSection();
	);

	DbgModule* mainModule = mDebugTarget->mTargetBinary;

	MiniDumpDebugger* miniDumpDebugger = NULL;
	if (mDebugger->IsMiniDumpDebugger())
	{
		miniDumpDebugger = (MiniDumpDebugger*)mDebugger;
	}

	mModuleKind = moduleKind;
	bool isHotSwap = mModuleKind == DbgModuleKind_HotObject;
	bool isObjectFile = mModuleKind != DbgModuleKind_Module;

	auto linkedModule = GetLinkedModule();

	if (isObjectFile)
		linkedModule->PopulateStaticVariableMap();

	mStartTypeIdx = (int)linkedModule->mTypes.size();
	int startSrcFile = (int)mDebugTarget->mSrcFiles.size();
	mStartSubprogramIdx = (int)mSubprograms.size();

	PEHeader hdr;
	memset(&hdr, 0, sizeof(hdr));

	PE_NTHeaders ntHdr;
	memset(&ntHdr, 0, sizeof(ntHdr));

	if (!isObjectFile)
	{
		stream->Read(&hdr, sizeof(PEHeader));
		stream->SetPos(hdr.e_lfanew);

		stream->Read(&ntHdr, sizeof(PE_NTHeaders));

		mPreferredImageBase = ntHdr.mOptionalHeader.mImageBase;
		if (mImageBase == 0)
		{
			BF_ASSERT(this == mainModule);
			mImageBase = mPreferredImageBase;
		}

		if ((hdr.e_magic != PE_DOS_SIGNATURE) || (ntHdr.mSignature != PE_NT_SIGNATURE))
		{
			mLoadState = DbgModuleLoadState_Failed;
			return false;
		}

#ifdef BF_DBG_32
		if (ntHdr.mFileHeader.mMachine != PE_MACHINE_X86)
			return false;
#else
		if (ntHdr.mFileHeader.mMachine != PE_MACHINE_X64)
		{
			mLoadState = DbgModuleLoadState_Failed;
			return false;
		}
#endif

		int pos = hdr.e_lfanew + FIELD_OFFSET(PE_NTHeaders, mOptionalHeader) + ntHdr.mFileHeader.mSizeOfOptionalHeader;
		stream->SetPos(pos);
	}
	else
	{
		stream->Read(&ntHdr.mFileHeader, sizeof(PEFileHeader));

		if (mMemReporter != NULL)
			mMemReporter->Add("PEFileHeader", sizeof(PEFileHeader));

#ifdef BF_DBG_32
		if (ntHdr.mFileHeader.mMachine != PE_MACHINE_X86)
			return false;
#else
		if (ntHdr.mFileHeader.mMachine != PE_MACHINE_X64)
		{
			mLoadState = DbgModuleLoadState_Failed;
			return false;
		}
#endif
	}

	int sectionStartPos = stream->GetPos();
	int sectionDataEndPos = 0;

	if (miniDumpDebugger != NULL)
	{
		// Map header
		miniDumpDebugger->MapMemory((addr_target)mImageBase, (uint8*)mMappedImageFile->mData, 0x1000);
	}

	bool wantStringTable = isObjectFile;

	stream->SetPos(sectionStartPos);
	for (int dirNum = 0; dirNum < (int) ntHdr.mFileHeader.mNumberOfSections; dirNum++)
	{
		PESectionHeader sectHdr;

		char* name = sectHdr.mName;
		stream->Read(&sectHdr, sizeof(PESectionHeader));
		if (sectHdr.mSizeOfRawData > 0)
			sectionDataEndPos = BF_MAX(sectionDataEndPos, (int)(sectHdr.mPointerToRawData + sectHdr.mSizeOfRawData));
		if (sectHdr.mNumberOfRelocations > 0)
			sectionDataEndPos = BF_MAX(sectionDataEndPos, (int)(sectHdr.mPointerToRelocations + sectHdr.mNumberOfRelocations * sizeof(COFFRelocation)));

		if (miniDumpDebugger != NULL)
		{
			miniDumpDebugger->MapMemory((addr_target)(mImageBase + sectHdr.mVirtualAddress), (uint8*)mMappedImageFile->mData + sectHdr.mPointerToRawData, sectHdr.mSizeOfRawData);
		}
	}

	//fseek(fp, sectionDataEndPos + ntHdr.mFileHeader.mNumberOfSymbols * 18, SEEK_SET);
	stream->SetPos(sectionDataEndPos);

	uint8* symbolData = new uint8[ntHdr.mFileHeader.mNumberOfSymbols * 18];
	mAllocSizeData += ntHdr.mFileHeader.mNumberOfSymbols * 18;

	mSymbolData = symbolData;
	stream->Read(symbolData, ntHdr.mFileHeader.mNumberOfSymbols * 18);

	int curPos = stream->GetPos();
	int stringTablePos = curPos;
	if (isObjectFile)
		GetStringTable(stream, stringTablePos);

	int mDebugFrameDataLen = 0;

	stream->SetPos(sectionStartPos);

	PEDataDirectory* exportDataDir = &ntHdr.mOptionalHeader.mDataDirectory[0];

	mHotTargetSections.Resize(ntHdr.mFileHeader.mNumberOfSections);

	Array<PESectionHeader> sectionHeaders;
	sectionHeaders.Resize(ntHdr.mFileHeader.mNumberOfSections);
	mSectionRVAs.Resize(sectionHeaders.size() + 1);

	Array<String> sectionNames;
	sectionNames.Resize(ntHdr.mFileHeader.mNumberOfSections);

	stream->Read(&sectionHeaders[0], sizeof(PESectionHeader) * ntHdr.mFileHeader.mNumberOfSections);

	for (int sectNum = 0; sectNum < ntHdr.mFileHeader.mNumberOfSections; sectNum++)
	{
		mSectionRVAs[sectNum] = sectionHeaders[sectNum].mVirtualAddress;
	}

	int tlsSection = -1;
	for (int sectNum = 0; sectNum < ntHdr.mFileHeader.mNumberOfSections; sectNum++)
	{
		//PEDataDirectory* dataDir = &ntHdr.mOptionalHeader.mDataDirectory[dirNum];

		PESectionHeader& sectHdr = sectionHeaders[sectNum];
		//stream->Read(&sectHdr, sizeof(PESectionHeader));

		const char* name = sectHdr.mName;
		if (name[0] == '/')
		{
			int strIdx = atoi(name + 1);
			name = &GetStringTable(stream, stringTablePos)[strIdx];
		}

		sectionNames[sectNum] = name;

		DbgHotTargetSection* targetSection = NULL;
		if (IsObjectFile())
		{
			targetSection = new DbgHotTargetSection();
			targetSection->mDataSize = sectHdr.mSizeOfRawData;
			targetSection->mPointerToRelocations = sectHdr.mPointerToRelocations;
			targetSection->mNumberOfRelocations = sectHdr.mNumberOfRelocations;
			targetSection->mTargetSectionAddr = 0; // TODO: Allocate!
			targetSection->mCanExecute = (sectHdr.mCharacteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
			targetSection->mCanWrite = (sectHdr.mCharacteristics & IMAGE_SCN_MEM_WRITE) != 0;
			targetSection->mNoTargetAlloc = (sectHdr.mCharacteristics & IMAGE_SCN_MEM_DISCARDABLE) != 0;
			mHotTargetSections[sectNum] = targetSection;
		}

		DbgSection dwSection;
		dwSection.mIsExecutable = (sectHdr.mCharacteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
		dwSection.mAddrStart = sectHdr.mVirtualAddress;
		dwSection.mAddrLength = BF_MAX(sectHdr.mSizeOfRawData, sectHdr.mVirtualSize);
		mSections.push_back(dwSection);

		if (sectHdr.mPointerToRawData == 0)
			continue;

		if (strcmp(name, ".tls") == 0)
			mTLSAddr = (addr_target)(sectHdr.mVirtualAddress + mImageBase);

		if ((IsObjectFile()) && (strcmp(name, ".tls$") == 0))
		{
			tlsSection = sectNum;
			mTLSSize = sectHdr.mSizeOfRawData;
			targetSection->mNoTargetAlloc = true;
		}

		bool isExportDataDir = ((exportDataDir->mVirtualAddress != 0) && (exportDataDir->mVirtualAddress >= sectHdr.mVirtualAddress) && (exportDataDir->mVirtualAddress < sectHdr.mVirtualAddress + sectHdr.mSizeOfRawData));

		if ((!IsObjectFile()) && (!isExportDataDir))
		{
			if (((strcmp(name, ".text")) == 0) ||
				((strcmp(name, ".textbss")) == 0) ||
				((strcmp(name, ".reloc")) == 0)/* ||
				((strcmp(name, ".data")) == 0)*/)
			{
				// Big unneeded sections
				continue;
			}
		}

		stream->SetPos(sectHdr.mPointerToRawData);

		int dataSize = sectHdr.mSizeOfRawData + 8;
		mAllocSizeData += dataSize;

		uint8* data = new uint8[dataSize];
		{
			BP_ZONE("DbgModule::ReadCOFF_ReadSectionData");
			stream->Read(data, sectHdr.mSizeOfRawData);
		}

		BfLogDbg("Read section data %s %p\n", name, data);

		memset(data + sectHdr.mSizeOfRawData, 0, 8);

		if (IsObjectFile())
			targetSection->mData = data;

		addr_target addrOffset = sectHdr.mVirtualAddress;
		if (isExportDataDir)
		{
			BP_ZONE("DbgModule::ReadCOFF_SymbolMap");

			IMAGE_EXPORT_DIRECTORY* imageExportDir = (IMAGE_EXPORT_DIRECTORY*)(data + (exportDataDir->mVirtualAddress - addrOffset));
			for (int funcIdx = 0; funcIdx < (int)imageExportDir->NumberOfNames; funcIdx++)
			{
				//addr_target strAddr = *(addr_target*)(data + (imageExportDir->AddressOfNames - addrOffset) + funcIdx * sizeof(addr_target));

				int32 strAddr = *(int32*)(data + (imageExportDir->AddressOfNames - addrOffset) + funcIdx * sizeof(int32));
				const char* name = (const char*)(data + (strAddr - addrOffset));

#ifdef BF_DBG_32
				if (name[0] == '_')
					name++;
#endif

				int funcOrd = *(uint16*)(data + (imageExportDir->AddressOfNameOrdinals - addrOffset) + funcIdx * sizeof(uint16));
				addr_target funcAddr = *(uint32*)(data + (imageExportDir->AddressOfFunctions - addrOffset) + funcOrd * sizeof(int32));

				int strLen = (int)strlen(name);
				BP_ALLOC("ReadCOFF_SymbolMap", strLen + 1);
				char* allocStr = (char*)mAlloc.AllocBytes(strLen + 1, "ReadCOFF_SymbolMap");
				memcpy(allocStr, name, strLen);

				BP_ALLOC_T(DbgSymbol);
				DbgSymbol* dwSymbol = mAlloc.Alloc<DbgSymbol>();
				dwSymbol->mDbgModule = this;
				dwSymbol->mName = allocStr;
				dwSymbol->mAddress = funcAddr;

				if (strcmp(name, "_tls_index") == 0)
				{
					mTLSIndexAddr = funcAddr;
				}

				//TODO:
				//mDeferredSymbols.PushFront(dwSymbol);

				dwSymbol->mAddress = (addr_target)(dwSymbol->mAddress + mImageBase);
				mDebugTarget->mSymbolMap.Insert(dwSymbol);
				linkedModule->mSymbolNameMap.Insert(dwSymbol);
			}
		}

		if ((IsObjectFile()) && (sectHdr.mNumberOfRelocations > 0))
		{
			//mDebugger->AllocTargetMemory(sectHdr.mSizeOfRawData, true, true);
		}

		if (strcmp(name, ".text") == 0)
		{
			if (!IsObjectFile())
				mCodeAddress = ntHdr.mOptionalHeader.mImageBase + sectHdr.mVirtualAddress;
		}

		//if (strcmp(name, ".rdata") == 0)
		{
			PEDataDirectory& debugDirEntry = ntHdr.mOptionalHeader.mDataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
			if (debugDirEntry.mSize > 0)
			{
				if (mMemReporter != NULL)
					mMemReporter->Add("DataDirectory", debugDirEntry.mSize);

				if ((debugDirEntry.mVirtualAddress >= sectHdr.mVirtualAddress) && (debugDirEntry.mVirtualAddress < sectHdr.mVirtualAddress + sectHdr.mSizeOfRawData))
				{
					int count = debugDirEntry.mSize / sizeof(IMAGE_DEBUG_DIRECTORY);
					for (int dirIdx = 0; dirIdx < count; dirIdx++)
					{
						IMAGE_DEBUG_DIRECTORY* debugDirectory = (IMAGE_DEBUG_DIRECTORY*)(data + debugDirEntry.mVirtualAddress - sectHdr.mVirtualAddress) + dirIdx;

						if (debugDirectory->Type == IMAGE_DEBUG_TYPE_CODEVIEW)
						{
							struct _CodeViewEntry
							{
							public:
								int32 mSig;
								uint8 mGUID[16];
								int32 mAge;
								const char mPDBPath[1];
							};

							if (debugDirectory->AddressOfRawData != 0)
							{
								_CodeViewEntry* codeViewEntry = (_CodeViewEntry*)(data + debugDirectory->AddressOfRawData - sectHdr.mVirtualAddress);
								if (codeViewEntry->mSig == 'SDSR')
								{
									LoadPDB(codeViewEntry->mPDBPath, codeViewEntry->mGUID, codeViewEntry->mAge);
								}
							}
						}
					}
				}

				//stream->SetPos(debugDirEntry.mVirtualAddress);
			}
		}

		//
		{
			PEDataDirectory& tlsDirEntry = ntHdr.mOptionalHeader.mDataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
			if (tlsDirEntry.mSize > 0)
			{
				if ((tlsDirEntry.mVirtualAddress >= sectHdr.mVirtualAddress) && (tlsDirEntry.mVirtualAddress < sectHdr.mVirtualAddress + sectHdr.mSizeOfRawData))
				{
					uint8* relPtr = data + tlsDirEntry.mVirtualAddress - sectHdr.mVirtualAddress;
					uint8* endPtr = relPtr + tlsDirEntry.mSize;

					addr_target tlsDataStart = GET_FROM(relPtr, addr_target) - ntHdr.mOptionalHeader.mImageBase;
					addr_target tlsDataEnd = GET_FROM(relPtr, addr_target) - ntHdr.mOptionalHeader.mImageBase;

					mTLSAddr = (addr_target)(tlsDataStart + mImageBase);
					mTLSSize = (int)(tlsDataEnd - tlsDataStart);
				}
			}
		}

		//
		{
			PEDataDirectory& debugDirEntry = ntHdr.mOptionalHeader.mDataDirectory[IMAGE_DIRECTORY_ENTRY_RESOURCE];
			if (debugDirEntry.mSize > 0)
			{
				if ((debugDirEntry.mVirtualAddress >= sectHdr.mVirtualAddress) && (debugDirEntry.mVirtualAddress < sectHdr.mVirtualAddress + sectHdr.mSizeOfRawData))
				{
					uint8* relPtr = data + debugDirEntry.mVirtualAddress - sectHdr.mVirtualAddress;
					uint8* endPtr = relPtr + debugDirEntry.mSize;

					IMAGE_RESOURCE_DIRECTORY* typeDir = (IMAGE_RESOURCE_DIRECTORY*)(relPtr);

					// Skip named entries
					for (int typeIdx = 0; typeIdx < typeDir->NumberOfIdEntries; typeIdx++)
					{
						IMAGE_RESOURCE_DIRECTORY_ENTRY* typeEntry = (IMAGE_RESOURCE_DIRECTORY_ENTRY*)((uint8*)typeDir + sizeof(IMAGE_RESOURCE_DIRECTORY) +
							(typeDir->NumberOfNamedEntries + typeIdx)*sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY));
						if (typeEntry->Id == 0x10) //VERSION
						{
							IMAGE_RESOURCE_DIRECTORY* idDir = (IMAGE_RESOURCE_DIRECTORY*)(relPtr + (typeEntry->OffsetToData & 0x7FFFFFFF));
							if (idDir->NumberOfIdEntries < 1)
								break;
							IMAGE_RESOURCE_DIRECTORY_ENTRY* idEntry = (IMAGE_RESOURCE_DIRECTORY_ENTRY*)((uint8*)idDir + sizeof(IMAGE_RESOURCE_DIRECTORY) +
								(idDir->NumberOfNamedEntries + 0) * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY));

							IMAGE_RESOURCE_DIRECTORY* langDir = (IMAGE_RESOURCE_DIRECTORY*)(relPtr + (idEntry->OffsetToData & 0x7FFFFFFF));
							if (langDir->NumberOfIdEntries < 1)
								break;
							IMAGE_RESOURCE_DIRECTORY_ENTRY* langEntry = (IMAGE_RESOURCE_DIRECTORY_ENTRY*)((uint8*)langDir + sizeof(IMAGE_RESOURCE_DIRECTORY) +
								(langDir->NumberOfNamedEntries + 0) * sizeof(IMAGE_RESOURCE_DIRECTORY_ENTRY));

							IMAGE_RESOURCE_DATA_ENTRY* dataEntry = (IMAGE_RESOURCE_DATA_ENTRY*)(relPtr + (langEntry->OffsetToData & 0x7FFFFFFF));
							uint8* versionData = data + dataEntry->OffsetToData - sectHdr.mVirtualAddress;
							uint8* vPtr = versionData;

							auto vSize = GET_FROM(vPtr, uint16);
							auto verEnd = vPtr + vSize;
							auto vLength = GET_FROM(vPtr, uint16);
							vPtr += 36; // "VS_VERSION_INFO"

							auto fixedFileInfo = GET_FROM(vPtr, VS_FIXEDFILEINFO);

							auto _GetString = [&]()
							{
								wchar_t* cPtr = (wchar_t*)vPtr;
								int len = (int)wcslen(cPtr);
								vPtr += (len + 1) * 2;

								if (((intptr)vPtr & 3) != 0)
									vPtr += 2;

								UTF16String str16(cPtr, len);
								return UTF8Encode(str16);
							};

							while (vPtr < verEnd)
							{
								auto size = GET_FROM(vPtr, uint16);
								auto childEnd = vPtr + size;
								auto valueLength = GET_FROM(vPtr, uint16);
								auto type = GET_FROM(vPtr, uint16);
								String infoType = _GetString();

								if (infoType == "StringFileInfo")
								{
									while (vPtr < childEnd)
									{
										auto strsSize = GET_FROM(vPtr, uint16);
										auto strsEnd = vPtr + strsSize;
										auto strsLength = GET_FROM(vPtr, uint16);
										auto strsType = GET_FROM(vPtr, uint16);
										String hexNum = _GetString();

										while (vPtr < strsEnd)
										{
											auto strSize = GET_FROM(vPtr, uint16);
											auto strEnd = vPtr + strSize;
											auto strLength = GET_FROM(vPtr, uint16);
											auto strType = GET_FROM(vPtr, uint16);
											String key = _GetString();
											String value = _GetString();
											if (key == "FileVersion")
												mVersion = value;
										}
									}
								}

								vPtr = childEnd;
							}
						}
					}
				}

				//stream->SetPos(debugDirEntry.mVirtualAddress);
			}
		}

		bool usedData = true;

		/*if (isUnwindSection)
		{
			mExceptionData = data;
			mExceptionDataRVA = sectHdr.mVirtualAddress;
		}*/

		if (strcmp(name, ".pdata") == 0)
		{
			DbgSectionData entry;
			entry.mData = data;
			entry.mSize = sectHdr.mSizeOfRawData;
			mExceptionDirectory.Add(entry);
		}

		// Old, unsupported DWARF debug info
		/*
		else if (strcmp(name, ".debug_info") == 0)
		{
			mDebugInfoData = data;
		}
		else if (strcmp(name, ".debug_line") == 0)
		{
			mDebugLineData = data;
		}
		else if (strcmp(name, ".debug_str") == 0)
		{
			mDebugStrData = data;
		}
		else if (strcmp(name, ".debug_frame") == 0)
		{
			mDebugFrameAddress = ntHdr.mOptionalHeader.mImageBase + sectHdr.mVirtualAddress;
			mDebugFrameData = data;
			mDebugFrameDataLen = sectHdr.mSizeOfRawData;
		}
		else if (strcmp(name, ".eh_frame") == 0)
		{
			mEHFrameAddress = ntHdr.mOptionalHeader.mImageBase + sectHdr.mVirtualAddress;
			mEHFrameData = data;
		}
		else if (strcmp(name, ".debug_abbrev") == 0)
		{
			mDebugAbbrevData = data;
			mDebugAbbrevPtrData = new const uint8*[sectHdr.mSizeOfRawData];
		}
		else if (strcmp(name, ".debug_loc") == 0)
		{
			mDebugLocationData = data;
		}
		else if (strcmp(name, ".debug_ranges") == 0)
		{
			mDebugRangesData = data;
		}
		*/

// 		else if (strcmp(name, ".rsrc") == 0)
// 		{
// 			//IMAGE_DIRECTORY_ENTRY_RESOURCE
// 		}
		else if (CheckSection(name, data, sectHdr.mSizeOfRawData))
		{
			// Was used
		}
		else
		{
			/*if (isUnwindSection)
				mOwnsExceptionData = true;
			else*/
				usedData = false;
		}

		if (!usedData)
		{
			if (IsObjectFile())
			{
				mOwnedSectionData.push_back(data);
			}
			else
			{
				mAllocSizeData -= dataSize;
				delete [] data;
			}
		}

		//stream->SetPos(prevPos);
	}

	int needHotTargetMemory = 0;
	if (isObjectFile)
	{
		for (int sectNum = 0; sectNum < ntHdr.mFileHeader.mNumberOfSections; sectNum++)
		{
			auto targetSection = mHotTargetSections[sectNum];
			if (!targetSection->mNoTargetAlloc)
				needHotTargetMemory += (targetSection->mDataSize + (mDebugger->mPageSize - 1)) & ~(mDebugger->mPageSize - 1);
		}
		mDebugger->ReserveHotTargetMemory(needHotTargetMemory);

		// '0' address is temporary
		//mOrigImageData = new DbgModuleMemoryCache(0, NULL, needHotTargetMemory, true);
		mOrigImageData = new DbgModuleMemoryCache(0, needHotTargetMemory);
	}

	int numSections = ntHdr.mFileHeader.mNumberOfSections;
	if (isObjectFile)
	{
		addr_target* resolvedSymbolAddrs = new addr_target[ntHdr.mFileHeader.mNumberOfSymbols];
		memset(resolvedSymbolAddrs, 0, ntHdr.mFileHeader.mNumberOfSymbols * sizeof(addr_target));
		ParseHotTargetSections(stream, resolvedSymbolAddrs);
		delete [] resolvedSymbolAddrs;
	}

	ProcessDebugInfo();

	if (mDebugInfoData != NULL)
	{
		mDbgFlavor = DbgFlavor_GNU;
		mMasterCompileUnit = new DbgCompileUnit(this);
		mMasterCompileUnit->mDbgModule = this;
		mMasterCompileUnit->mIsMaster = true;

		const uint8* data = mDebugInfoData;
		while (ParseDWARF(data)) {}
		CreateNamespaces();

		// Must be added last so module reference indices still map correctly
		mCompileUnits.push_back(mMasterCompileUnit);
	}

	ParseDebugFrameData();
	ParseEHFrameData();

	mEndTypeIdx = (int)linkedModule->mTypes.size();
	mEndSubprogramIdx = (int)mSubprograms.size();

	if (mDebugLineData != NULL)
	{
		const uint8* data = mDebugLineData;
		for (int compileUnitIdx = 0; true; compileUnitIdx++)
			if (!ParseDebugLineInfo(data, compileUnitIdx))
			break;
	}

	{
		BP_ZONE("ReadPE_ReadSymbols");

		//linkedModule->mSymbolNameMap.reserve(linkedModule->mSymbolNameMap.size() + ntHdr.mFileHeader.mNumberOfSymbols * 2);

		bool tlsFailed = false;
		addr_target tlsMappingAddr = 0;

		for (int symNum = 0; symNum < (int)ntHdr.mFileHeader.mNumberOfSymbols; symNum++)
		{
			PE_SymInfo* symInfo = (PE_SymInfo*)&mSymbolData[symNum * 18];

			char* name = symInfo->mName;
			if (symInfo->mNameOfs[0] != 0)
			{
				if (name[7] != 0)
				{
					// Name is exactly 8 chars, not null terminated yet
					name = (char*)mAlloc.AllocBytes(9, "PESymbol");
					memcpy(name, symInfo->mName, 8);
					name[8] = 0;
				}
			}
			else
				name = (char*)GetStringTable(stream, stringTablePos) + symInfo->mNameOfs[1];

			if ((symInfo->mStorageClass == COFF_SYM_CLASS_EXTERNAL) ||
				(symInfo->mStorageClass == COFF_SYM_CLASS_STATIC))
			{
				// 'static' in the C sense.
                //  It means local to the compile unit, so may have multiple copies of the same symbol name.
				bool isStaticSymbol = symInfo->mStorageClass == COFF_SYM_CLASS_STATIC;

				if (symInfo->mSectionNum == 0xFFFF)
					continue;

				if (symInfo->mSectionNum > 0)
				{
					bool isTLS = false;
					addr_target targetAddr = 0;
					if (isObjectFile)
					{
						if (symInfo->mSectionNum - 1 == tlsSection)
						{
							isTLS = true;
						}
						else
						{
							auto hotTargetSection = mHotTargetSections[symInfo->mSectionNum - 1];
							if (hotTargetSection != NULL)
								targetAddr = GetHotTargetAddress(hotTargetSection) + symInfo->mValue;
						}
					}
					else
						targetAddr = mSectionRVAs[symInfo->mSectionNum - 1] + symInfo->mValue;

					if (((targetAddr != 0) || (isTLS)) &&
						(name[0] != '.'))
					{
						const char* symbolName = name;

#ifdef BF_DBG_32
						if (symbolName[0] == '_')
							symbolName++;
#endif

						if (strcmp(symbolName, "_tls_index") == 0)
						{
							mTLSIndexAddr = (addr_target)(targetAddr + mImageBase);
						}

						if ((isStaticSymbol) && (IsHotSwapPreserve(symbolName)))
							isStaticSymbol = false;

						if ((isObjectFile) && (!isStaticSymbol))
						{
							DbgSymbol* dwSymbol = NULL;

							linkedModule->ParseSymbolData() ;

							BP_ALLOC_T(DbgSymbol);
							dwSymbol = mAlloc.Alloc<DbgSymbol>();
							dwSymbol->mDbgModule = this;
							dwSymbol->mName = symbolName;
							dwSymbol->mAddress = targetAddr;

							if (dwSymbol != NULL)
							{
								bool isHotSwapPreserve = IsHotSwapPreserve(dwSymbol->mName);
								bool insertIntoNameMap = true;

								bool oldFound = false;

								auto nameMapEntry = linkedModule->mSymbolNameMap.Find(dwSymbol->mName);
								if (nameMapEntry != NULL)
								{
									oldFound = true;
									if (!isHotSwapPreserve)
									{
										nameMapEntry->mValue = dwSymbol;
									}
									else if (mDbgFlavor == DbgFlavor_MS)
									{
										// Store in our own map - this is needed for storing address of the new vdata
										//  so the new values can be copied in
										mSymbolNameMap.Insert(dwSymbol);
									}
								}
								else
								{
									if (isTLS)
									{
										if (mainModule->mTLSExtraAddr == 0)
										{
											auto extraSym = mainModule->mSymbolNameMap.Find("__BFTLS_EXTRA");
											if (extraSym != NULL)
											{
												mainModule->ParseGlobalsData();
												auto itr = mainModule->mStaticVariableMap.find("__BFTLS_EXTRA");
												if (itr != mainModule->mStaticVariableMap.end())
												{
													auto staticVar = itr->second;
													mainModule->mTLSExtraAddr = extraSym->mValue->mAddress;
													mainModule->mTLSExtraSize = (int)staticVar->mType->GetByteCount();
												}
											}
										}

										if ((mainModule->mTLSExtraAddr != 0) && (tlsMappingAddr == 0))
										{
											// Take a chunk out of __BFTLS_EXTRA
											if (mTLSSize <= mainModule->mTLSExtraSize)
											{
												tlsMappingAddr = mainModule->mTLSExtraAddr;
												mainModule->mTLSExtraAddr += mTLSSize;
												mainModule->mTLSExtraSize -= mTLSSize;
											}
										}

										if (tlsMappingAddr != 0)
										{
											BF_ASSERT(symInfo->mValue < mTLSSize);
											dwSymbol->mAddress = tlsMappingAddr + symInfo->mValue;
										}

										if (dwSymbol->mAddress == 0)
										{
											if (!tlsFailed)
											{
												Fail(StrFormat("Hot swapping failed to allocate TLS address for '%s'. Program restart required.", name));
											}

											dwSymbol->mAddress = (addr_target)0xCDCDCDCD;
											tlsFailed = true;
										}
									}
								}

								if (dwSymbol->mAddress != 0)
								{
									if (!oldFound)
										linkedModule->mSymbolNameMap.Insert(dwSymbol);
									mDebugTarget->mSymbolMap.Insert(dwSymbol);
								}
							}
						}
						else
						{
							//TODO: We don't need to defer symbols anymore... we can just do a Fixup on their addr
							//mDeferredSymbols.PushFront(dwSymbol);
							BP_ALLOC_T(DbgSymbol);
							DbgSymbol* dwSymbol = mAlloc.Alloc<DbgSymbol>();
							dwSymbol->mDbgModule = this;
							dwSymbol->mName = symbolName;
							dwSymbol->mAddress = targetAddr;

							if (!IsObjectFile())
								dwSymbol->mAddress += (addr_target)mImageBase;

							if (IsObjectFile())
								BF_ASSERT((dwSymbol->mAddress >= mImageBase) && (dwSymbol->mAddress < mImageBase + mImageSize));

							mDebugTarget->mSymbolMap.Insert(dwSymbol);
							if (!isStaticSymbol)
								linkedModule->mSymbolNameMap.Insert(dwSymbol);
						}
					}
				}
			}

			if (symInfo->mStorageClass == COFF_SYM_CLASS_FILE)
			{
				const char* fileName = (const char*)&mSymbolData[(symNum + 1) * 18];
			}

			symNum += symInfo->mNumOfAuxSymbols;
		}
	}

	int subProgramSizes = 0;
	for (int subProgramIdx = mStartSubprogramIdx; subProgramIdx < mEndSubprogramIdx; subProgramIdx++)
	{
		auto dwSubprogram = mSubprograms[subProgramIdx];
		subProgramSizes += (int)(dwSubprogram->mBlock.mHighPC - dwSubprogram->mBlock.mLowPC);

		/*for (int i = 0; i < dwSubprogram->mLineDataArray.mSize; i++)
		{
			auto lineData = dwSubprogram->mLineDataArray.mData[i];
			auto srcFile = lineData->mSrcFileRef->mSrcFile;
			srcFile->mLineData.push_back(lineData);
			srcFile->mHadLineData = true;
			if ((srcFile->mFirstLineDataDbgModule == NULL) || (srcFile->mFirstLineDataDbgModule == this))
				srcFile->mFirstLineDataDbgModule = this;
			else
				srcFile->mHasLineDataFromMultipleModules = true;
		}*/
	}

	// Delete srcFiles without line data
	int lineDataCount = 0;
	/*for (int srcFileIdx = startSrcFile; srcFileIdx < (int)mDebugTarget->mSrcFiles.size(); srcFileIdx++)
	{
		if (!mDebugTarget->mSrcFiles[srcFileIdx]->mHadLineData)
		{
			mEmptySrcFiles.push_back(mDebugTarget->mSrcFiles[srcFileIdx]);
			mDebugTarget->mSrcFiles.erase(mDebugTarget->mSrcFiles.begin() + srcFileIdx);
		}
		else
			lineDataCount += (int)mDebugTarget->mSrcFiles[srcFileIdx]->mLineData.size();
	}*/
	auto srcFilesItr = mDebugTarget->mSrcFiles.begin();
	while (srcFilesItr != mDebugTarget->mSrcFiles.end())
	{
		DbgSrcFile* srcFile = srcFilesItr->mValue;
		if ((!srcFile->mHadLineData) && (srcFile->mLocalPath.IsEmpty()))
		{
			mEmptySrcFiles.push_back(srcFile);
			srcFilesItr = mDebugTarget->mSrcFiles.Remove(srcFilesItr);
		}
		else
		{
			++srcFilesItr;
		}
	}

	if (!isObjectFile)
	{
		mImageSize = ntHdr.mOptionalHeader.mSizeOfImage;
		mEntryPoint = ntHdr.mOptionalHeader.mAddressOfEntryPoint;
	}

	/*OutputDebugStrF("%s:\n CompileUnits:%d DebugLines: %d Types: %d (%d in map) SubPrograms: %d (%dk) AllocSize:%dk\n", mFilePath.c_str(), mCompileUnits.size(),
		lineDataCount, mEndTypeIdx - mStartTypeIdx, (int)linkedModule->mTypes.size() - mStartTypeIdx, mEndSubprogramIdx - mStartSubprogramIdx, subProgramSizes / 1024, mAlloc.GetAllocSize() / 1024);*/

	if (isHotSwap)
	{
		// In COFF, we don't necessarily add an actual primary type during MapCompileUnitMethods, so this fixes that
		while (true)
		{
			bool didReplaceType = false;
			for (auto itr = mHotPrimaryTypes.begin(); itr != mHotPrimaryTypes.end(); ++itr)
			{
				auto dbgType = *itr;
				auto primaryType = dbgType->GetPrimaryType();
				if (primaryType != dbgType)
				{
					mHotPrimaryTypes.Remove(itr);
					mHotPrimaryTypes.Add(primaryType);
					didReplaceType = true;
					break;
				}
			}
			if (!didReplaceType)
				break;
		}

		BF_ASSERT(mTypes.size() == 0);
		for (int typeIdx = mStartTypeIdx; typeIdx < (int)linkedModule->mTypes.size(); typeIdx++)
		{
			DbgType* newType = linkedModule->mTypes[typeIdx];
			//if (!newType->mMethodList.IsEmpty())
			if (!newType->mIsDeclaration)
				HotReplaceType(newType);
		}
	}

	if (needHotTargetMemory != 0)
	{
		BF_ASSERT(needHotTargetMemory >= (int)mImageSize);
	}

	//BF_ASSERT(mEndTypeIdx == (int)linkedModule->mTypes.size());
	//BF_ASSERT(mEndSubprogramIdx == (int)mSubprograms.size());

	ParseExceptionData();

	mLoadState = DbgModuleLoadState_Loaded;

	if (mMemReporter != NULL)
	{
		mMemReporter->BeginSection("Sections");

		ParseSymbolData();

		Array<DbgSymbol*> orderedSyms;
		for (auto sym : mSymbolNameMap)
		{
			auto dbgSym = sym->mValue;
			orderedSyms.Add(dbgSym);
		}
		orderedSyms.Sort([](DbgSymbol* lhs, DbgSymbol* rhs) { return lhs->mAddress < rhs->mAddress; });

		for (int sectNum = 0; sectNum < ntHdr.mFileHeader.mNumberOfSections; sectNum++)
		{
			PESectionHeader& sectHdr = sectionHeaders[sectNum];

			mMemReporter->BeginSection(sectionNames[sectNum]);

			DbgSymbol* lastSym = NULL;

			for (auto dbgSym : orderedSyms)
			{
				if (dbgSym->mAddress < mImageBase + sectHdr.mVirtualAddress)
					continue;

				if (dbgSym->mAddress >= mImageBase + sectHdr.mVirtualAddress + sectHdr.mSizeOfRawData)
					break;

				if (lastSym != NULL)
				{
					mMemReporter->Add(lastSym->mName, (int)(dbgSym->mAddress - lastSym->mAddress));
				}
				else
				{
					int startingOffset = (int)(dbgSym->mAddress - (mImageBase + sectHdr.mVirtualAddress + sectHdr.mSizeOfRawData));
					if (startingOffset > 0)
						mMemReporter->Add("<StartData>", startingOffset);
				}
				lastSym = dbgSym;
			}

			if (lastSym != NULL)
				mMemReporter->Add(lastSym->mName, (int)((mImageBase + sectHdr.mVirtualAddress + sectHdr.mSizeOfRawData) - lastSym->mAddress));
			else
			{
				mMemReporter->Add("<Unaccounted>", (int)(sectHdr.mSizeOfRawData));
			}

			mMemReporter->EndSection();
		}
		mMemReporter->EndSection();

		mMemReporter->mShowInKB = false;
		mMemReporter->Report();
	}
	return true;
}

void DbgModule::FinishHotSwap()
{
	BF_ASSERT(IsObjectFile());

	auto linkedModule = GetLinkedModule();
	auto mainModule = mDebugTarget->mTargetBinary;

	HashSet<String> failSet;

	String findName;
	for (auto deferredHotResolve : mDeferredHotResolveList)
	{
		addr_target resolveTargetAddr = deferredHotResolve->mNewAddr;

		findName = deferredHotResolve->mName;
		if (mDbgFlavor == DbgFlavor_MS)
		{
			// ... why do we need to find these variables in the variable map instead of the symbol name map?
		}

		auto itr = mainModule->mStaticVariableMap.find(findName.c_str());
		if (itr != mainModule->mStaticVariableMap.end())
		{
			DbgVariable* variable = itr->second;
			resolveTargetAddr = mDebugTarget->GetStaticAddress(variable);
		}
		else
		{
			auto symbolEntry = mainModule->mSymbolNameMap.Find(findName.c_str());

			if (symbolEntry != NULL)
			{
				resolveTargetAddr = symbolEntry->mValue->mAddress;
			}
			else
			{
				if (deferredHotResolve->mName == "__ImageBase")
				{
					resolveTargetAddr = (addr_target)mainModule->mImageBase;
				}
				else
				{
					resolveTargetAddr = mainModule->LocateSymbol(deferredHotResolve->mName);
					if (resolveTargetAddr == 0)
					{
						failSet.Add(deferredHotResolve->mName);
						continue;
					}
				}
			}
		}
		DoReloc(deferredHotResolve->mHotTargetSection, deferredHotResolve->mReloc, resolveTargetAddr, NULL);
	}
	mDeferredHotResolveList.Clear();

	if (!failSet.IsEmpty())
	{
		bool handled = false;
		if (!mDebugger->mDebugManager->mOutMessages.empty())
		{
			auto& str = mDebugger->mDebugManager->mOutMessages.back();
			if (str.Contains("failed to resolve"))
			{
				for (auto& sym : failSet)
				{
					str += ", ";
					str += sym;
				}
				handled = true;
			}
		}

		if (!handled)
		{
			int symIdx = 0;
			String str;
			if (failSet.size() == 1)
				str = "Hot swapping failed to resolve symbol: ";
			else
				str = "Hot swapping failed to resolve symbols: ";
			for (auto& sym : failSet)
			{
				if (symIdx != 0)
					str += ", ";
				str += sym;
				symIdx++;
			}
			mDebugger->Fail(str);
		}
	}

	CommitHotTargetSections();

	// We need this here because vdata gets loaded first, so we need to wait until we have the addrs for the new methods (from other modules)
	//  before we can finalize the class vdata.
	ProcessHotSwapVariables();

	for (auto hotTargetSection : mHotTargetSections)
		delete hotTargetSection;

	mHotTargetSections.Clear();
	mSymbolNameMap.Clear();
}

addr_target DbgModule::ExecuteOps(DbgSubprogram* dwSubprogram, const uint8* locData, int locDataLen, WdStackFrame* stackFrame, CPURegisters* registers, DbgAddrType* outAddrType, DbgEvalLocFlags flags, addr_target* pushValue)
{
	bool allowReg = (flags & DbgEvalLocFlag_IsParam) == 0;

	const uint8* locDataEnd = locData + locDataLen;
	int regNum = -1;

	addr_target stackFrameData[256];
	int stackIdx = 0;

	if (pushValue != NULL)
		stackFrameData[stackIdx++] = *pushValue;

	while (locData < locDataEnd)
	{
		uint8 opCode = GET_FROM(locData, uint8);
		switch (opCode)
		{
		case DW_OP_piece:
			{
				if (*outAddrType == DbgAddrType_Register)
					*outAddrType = DbgAddrType_Value;
				addr_target val = stackFrameData[--stackIdx];
				int pieceSize = (int)DecodeULEB128(locData);
				if (pieceSize == 4)
					val &= 0xFFFFFFFF;
				else if (pieceSize == 2)
					val &= 0xFFFF;
				else if (pieceSize == 1)
					val &= 0xFF;
				stackFrameData[stackIdx++] = val;
			}
			break;
		case DW_OP_consts:
			{
				int64 val = DecodeSLEB128(locData);
				stackFrameData[stackIdx++] = (addr_target)val;
			}
			break;
		case DW_OP_stack_value:
			{
				*outAddrType = DbgAddrType_Value;
			}
			break;
		case DW_OP_addr_noRemap:
			{
				addr_target addr = GET_FROM(locData, addr_target);
				stackFrameData[stackIdx++] = addr;
				//*outIsAddr = true;
				*outAddrType = DbgAddrType_Target;
			}
			break;

		case DW_OP_addr:
			{
				addr_target addr = GET_FROM(locData, addr_target);
				//if (dwarf != NULL)
					addr = RemapAddr(addr);
				stackFrameData[stackIdx++] = addr;
				//*outIsAddr = true;
				*outAddrType = DbgAddrType_Target;
			}
			break;
		case DW_OP_deref:
			{
				addr_target addr = stackFrameData[--stackIdx];
				addr_target value = mDebugger->ReadMemory<addr_target>(addr);
				stackFrameData[stackIdx++] = value;
			}
			break;
		case DW_OP_fbreg:
			{
				if (registers == NULL)
					return 0;
				BF_ASSERT(dwSubprogram != NULL);
				DbgSubprogram* nonInlinedSubProgram = dwSubprogram->GetRootInlineParent();

				if (nonInlinedSubProgram->mFrameBaseData == NULL)
				{
					*outAddrType = DbgAddrType_Target; //TODO: why?
					return 0;
				}

				BF_ASSERT(nonInlinedSubProgram->mFrameBaseData != NULL);
				intptr loc = EvaluateLocation(nonInlinedSubProgram, nonInlinedSubProgram->mFrameBaseData, nonInlinedSubProgram->mFrameBaseLen, stackFrame, outAddrType, DbgEvalLocFlag_DisallowReg);
				int64 offset = DecodeSLEB128(locData);
				loc += offset;
				//loc = BfDebuggerReadMemory(loc);
				//*outIsAddr = true;
				*outAddrType = DbgAddrType_Target;
				stackFrameData[stackIdx++] = (addr_target)loc;
			}
			break;
		case DW_OP_reg0:
		case DW_OP_reg1:
		case DW_OP_reg2:
		case DW_OP_reg3:
		case DW_OP_reg4:
		case DW_OP_reg5:
		case DW_OP_reg6:
		case DW_OP_reg7:
		case DW_OP_reg8:
		case DW_OP_reg9:
		case DW_OP_reg10:
		case DW_OP_reg11:
		case DW_OP_reg12:
		case DW_OP_reg13:
		case DW_OP_reg14:
		case DW_OP_reg15:
			if (registers == NULL)
				return 0;
			BF_ASSERT((opCode - DW_OP_reg0) < CPURegisters::kNumIntRegs);
			regNum = opCode - DW_OP_reg0;
			stackFrameData[stackIdx++] = registers->mIntRegsArray[regNum];
			*outAddrType = DbgAddrType_Register;
			break;

		case DW_OP_reg21: //XMM0
			BF_FATAL("XMM registers not supported yet");
			break;

		case DW_OP_breg0:
		case DW_OP_breg1:
		case DW_OP_breg2:
		case DW_OP_breg3:
		case DW_OP_breg4:
		case DW_OP_breg5:
		case DW_OP_breg6:
		case DW_OP_breg7:
		case DW_OP_breg8:
		case DW_OP_breg9:
		case DW_OP_breg10:
		case DW_OP_breg11:
		case DW_OP_breg12:
		case DW_OP_breg13:
		case DW_OP_breg14:
		case DW_OP_breg15:
			{
				if (registers == NULL)
					return 0;
				int64 offset = DecodeSLEB128(locData);
				BF_ASSERT((opCode - DW_OP_breg0) < CPURegisters::kNumIntRegs);
				auto loc = registers->mIntRegsArray[opCode - DW_OP_breg0] + offset;
				//loc = BfDebuggerReadMemory(loc);
				//*outIsAddr = true;
				*outAddrType = DbgAddrType_Target;
				stackFrameData[stackIdx++] = (addr_target)loc;
			}
			break;
		case DW_OP_bregx:
			{
				if (registers == NULL)
					return 0;
				int regNum = (int)DecodeULEB128(locData);
				int64 offset = DecodeSLEB128(locData);
				BF_ASSERT(regNum < CPURegisters::kNumIntRegs);
				auto loc = registers->mIntRegsArray[regNum] + offset;
				//loc = BfDebuggerReadMemory(loc);
				//*outIsAddr = true;
				*outAddrType = DbgAddrType_Target;
				stackFrameData[stackIdx++] = (addr_target)loc;
			}
			break;
		case DW_OP_const4u:
			{
				uint32 val = GET_FROM(locData, uint32);
				stackFrameData[stackIdx++] = val;
			}
			break;
		case DW_OP_const8u:
			{
				uint64 val = GET_FROM(locData, uint64);
				stackFrameData[stackIdx++] = (addr_target)val;
			}
			break;
		case DW_OP_GNU_push_tls_address:
			{
				if ((mTLSAddr == 0) || (mTLSIndexAddr == 0))
					return 0;

				int tlsIndex = mDebugger->ReadMemory<int>(mTLSIndexAddr);
				addr_target tlsEntry = mDebugger->GetTLSOffset(tlsIndex);

				intptr_target tlsValueIndex = stackFrameData[--stackIdx];

				stackFrameData[stackIdx++] = (tlsValueIndex - mTLSAddr) + tlsEntry;
				*outAddrType = DbgAddrType_Target;
			}
			break;
		case DW_OP_nop:
			break;
		default:
			BF_FATAL("Unknown DW_OP");
			break;
		}
	}

	if (*outAddrType == DbgAddrType_Register)
	{
		if (allowReg)
			return regNum;
		*outAddrType = DbgAddrType_Value;
	}

	//BF_ASSERT(stackIdx == 1);
	return stackFrameData[--stackIdx];
}

intptr DbgModule::EvaluateLocation(DbgSubprogram* dwSubprogram, const uint8* locData, int locDataLen, WdStackFrame* stackFrame, DbgAddrType* outAddrType, DbgEvalLocFlags flags)
{
	BP_ZONE("DebugTarget::EvaluateLocation");

	auto dbgModule = this;

	if (locDataLen == DbgLocationLenKind_SegPlusOffset)
	{
		BF_ASSERT(dbgModule->mDbgFlavor == DbgFlavor_MS);
		if (dbgModule->mDbgFlavor == DbgFlavor_MS)
		{
			COFF* coff = (COFF*)dbgModule;
			struct SegOfsData
			{
				uint32 mOfs;
				uint16 mSeg;
			};
			SegOfsData* segOfsData = (SegOfsData*)locData;

			*outAddrType = DbgAddrType_Target;
			return coff->GetSectionAddr(segOfsData->mSeg, segOfsData->mOfs);
		}
		else
		{
			*outAddrType = DbgAddrType_Target;
			return 0;
		}
	}

	CPURegisters* registers = NULL;
	if (stackFrame != NULL)
		registers = &stackFrame->mRegisters;

	if (locDataLen < 0)
	{
		if (registers == NULL)
			return 0;
		int64 ipAddr = stackFrame->GetSourcePC();

		const uint8* checkLocData = locData;
		int64 startLoc = (int64)GET_FROM(checkLocData, addr_target);
		int64 endLoc = startLoc + GET_FROM(checkLocData, uint16);

		BF_ASSERT(dwSubprogram != NULL);
		startLoc += dwSubprogram->mCompileUnit->mLowPC;
		endLoc += dwSubprogram->mCompileUnit->mLowPC;

		if ((ipAddr >= startLoc) && (ipAddr < endLoc))
		{
			locDataLen = -locDataLen - sizeof(addr_target) - sizeof(uint16);
			locData = checkLocData;
		}
		else
		{
			*outAddrType = DbgAddrType_OptimizedOut;
			return 0;
		}
	}
	else if (locDataLen == 0)
	{
		if (registers == NULL)
			return 0;
		int64 ipAddr = stackFrame->GetSourcePC();

		const uint8* checkLocData = locData;
		while (true)
		{
			int64 startLoc = (int64)GET_FROM(checkLocData, addr_target);
			int64 endLoc = (int64)GET_FROM(checkLocData, addr_target);

			if ((startLoc == 0) && (endLoc == 0))
			{
				*outAddrType = DbgAddrType_OptimizedOut;
				return 0;
			}

			BF_ASSERT(dwSubprogram != NULL);
			startLoc += dwSubprogram->mCompileUnit->mLowPC;
			endLoc += dwSubprogram->mCompileUnit->mLowPC;

			if ((ipAddr >= startLoc) && (ipAddr < endLoc))
			{
				locDataLen = GET_FROM(checkLocData, int16);
				locData = checkLocData;
				break;
			}
			else
			{
				int len = GET_FROM(checkLocData, int16);;
				checkLocData += len;
			}
		}
	}

	return ExecuteOps(dwSubprogram, locData, locDataLen, stackFrame, registers, outAddrType, flags);
}

void DbgModule::ProcessHotSwapVariables()
{
	BP_ZONE("DbgModule::ProcessHotSwapVariables");

	auto linkedModule = GetLinkedModule();

	for (auto staticVariable : mStaticVariables)
	{
		bool replaceVariable = false;

		const char* findName = staticVariable->GetMappedName();
		auto itr = linkedModule->mStaticVariableMap.find(findName);
		if (itr != linkedModule->mStaticVariableMap.end())
		{
			DbgVariable* oldVariable = itr->second;
			// If the old static field has the same type as the new static field then we keep the same
			//  address, otherwise we use the new (zeroed-out) allocated space

			auto _GetNewAddress = [&]()
			{
				addr_target newAddress = 0;
				if (mDbgFlavor == DbgFlavor_GNU)
				{
					newAddress = mDebugTarget->GetStaticAddress(staticVariable);
				}
				else
				{
					// In CodeView, the newVariable ends up pointing to the old address, so we need to store
					//  the location in our own mSymbolNameMap
					auto entry = mSymbolNameMap.Find(oldVariable->mLinkName);
					if (entry != NULL)
						newAddress = entry->mValue->mAddress;
				}
				return newAddress;
			};

			if (oldVariable->mType->IsSizedArray())
			{
				mDebugTarget->GetCompilerSettings();

				bool doMerge = strstr(oldVariable->mName, "sBfClassVData") != NULL;
				bool keepInPlace = (doMerge) && (strstr(oldVariable->mName, ".vext") == NULL);
				if (doMerge)
				{
					addr_target oldAddress = mDebugTarget->GetStaticAddress(oldVariable);
					addr_target newAddress = _GetNewAddress();
					if (newAddress == 0)
						continue;

					uint8* newData = GetHotTargetData(newAddress);
					int newArraySize = (int)staticVariable->mType->GetByteCount();
					int oldArraySize = (int)oldVariable->mType->GetByteCount();

					int copySize = std::min(newArraySize, oldArraySize);

					BF_ASSERT((oldArraySize & (sizeof(addr_target) - 1)) == 0);

					DbgModule* defModule = oldVariable->mType->mCompileUnit->mDbgModule;
					defModule->EnableWriting(oldAddress);

					uint8* mergedData = new uint8[copySize];
					mDebugger->ReadMemory(oldAddress, copySize, mergedData);

					// The new vtable may have 0's in it when virtual methods are removed. Keep the old virtual addresses in those.
					addr_target* newDataPtr = (addr_target*)newData;
					addr_target* mergedPtr = (addr_target*)mergedData;
					while (mergedPtr < (addr_target*)(mergedData + copySize))
					{
						if (*newDataPtr != 0)
							*mergedPtr = *newDataPtr;
						mergedPtr++;
						newDataPtr++;
					}

					bool success;
					success = mDebugger->WriteMemory(oldAddress, mergedData, copySize);
					BF_ASSERT(success);
					memcpy(newData, mergedData, copySize);
					delete mergedData;
				}
				else if (strstr(oldVariable->mName, "sStringLiterals") != NULL)
				{
					addr_target oldAddress = mDebugTarget->GetStaticAddress(oldVariable);
					addr_target newAddress = NULL;
					if (mDbgFlavor == DbgFlavor_GNU)
					{
						newAddress = mDebugTarget->GetStaticAddress(staticVariable);
					}
					else
					{
						// In CodeView, the newVariable ends up pointing to the old address, so we need to store
						//  the location in our own mSymbolNameMap
						auto entry = mSymbolNameMap.Find(oldVariable->mLinkName);
						if (entry == NULL)
							continue;
						newAddress = entry->mValue->mAddress;
					}

					// Make sure newAddress doesn't have anything linked to it
					addr_target val = 0;
					bool success = mDebugger->ReadMemory((intptr)newAddress, sizeof(addr_target), &val);
					BF_ASSERT(success);
					BF_ASSERT(val == 0);

					// Link the new table to the old extended table
					addr_target prevLinkage = 0;
					success = mDebugger->ReadMemory((intptr)oldAddress, sizeof(addr_target), &prevLinkage);
					BF_ASSERT(success);
					success = mDebugger->WriteMemory((intptr)newAddress, &prevLinkage, sizeof(addr_target));
					BF_ASSERT(success);

					mDebugger->EnableWriting((intptr)oldAddress, sizeof(addr_target));
					success = mDebugger->WriteMemory((intptr)oldAddress, &newAddress, sizeof(addr_target));
					BF_ASSERT(success);

					keepInPlace = true;
				}

				if (keepInPlace)
				{
					// We have to maintain the OLD size because we can't overwrite the original bounds
					staticVariable->mType = oldVariable->mType;
					staticVariable->mLocationLen = oldVariable->mLocationLen;
					staticVariable->mLocationData = oldVariable->mLocationData;
					staticVariable->mCompileUnit = oldVariable->mCompileUnit;
				}
			}
			else if (oldVariable->mType->Equals(staticVariable->mType))
			{
				if (oldVariable->mType->IsStruct())
				{
					if ((strncmp(oldVariable->mName, "?sBfTypeData@", 13) == 0) || (strncmp(oldVariable->mName, "sBfTypeData.", 12) == 0))
					{
						int size = (int)staticVariable->mType->GetByteCount();
						addr_target oldAddress = mDebugTarget->GetStaticAddress(oldVariable);
						addr_target newAddress = _GetNewAddress();
						if (newAddress == 0)
							continue;

						uint8* data = new uint8[size];
						bool success = mDebugger->ReadMemory(newAddress, size, data);
						if (success)
						{
							mDebugger->EnableWriting((intptr)oldAddress, size);
							success = mDebugger->WriteMemory(oldAddress, data, size);
						}
						delete data;

						BF_ASSERT(success);

						staticVariable->mLocationLen = oldVariable->mLocationLen;
						staticVariable->mLocationData = oldVariable->mLocationData;
					}
				}

				//staticVariable->mLocationLen = oldVariable->mLocationLen;
				//staticVariable->mLocationData = oldVariable->mLocationData;
				replaceVariable = false;
			}
			else
			{
				BF_ASSERT(!oldVariable->mType->IsSizedArray());
			}

			if (!replaceVariable)
			{
				auto symbolVal = linkedModule->mSymbolNameMap.Find(staticVariable->GetMappedName());
				if (symbolVal != NULL)
				{
					addr_target oldAddress = mDebugTarget->GetStaticAddress(oldVariable);
					DbgSymbol* oldSymbol = mDebugTarget->mSymbolMap.Get(oldAddress);
					if (oldSymbol != NULL)
						symbolVal->mValue = oldSymbol;
				}
			}
		}
		else // Not found - new variable
			replaceVariable = true;

		if (replaceVariable)
		{
			linkedModule->mStaticVariableMap[staticVariable->GetMappedName()] = staticVariable;
		}
	}
}

int64 DbgModule::GetImageSize()
{
	return mImageSize;
}

/*const uint8* DbgModule::GetOrigImageData(addr_target address)
{
	return mOrigImageData + (address - mImageBase);
}*/

DbgFileExistKind DbgModule::CheckSourceFileExist(const StringImpl& path)
{
	DbgFileExistKind existsKind = DbgFileExistKind_NotFound;

	if (path.StartsWith("$Emit"))
		return DbgFileExistKind_Found;

	if (FileExists(path))
		existsKind = DbgFileExistKind_Found;

	String oldSourceCommand = GetOldSourceCommand(path);
	if (!oldSourceCommand.IsEmpty())
	{
		int crPos = (int)oldSourceCommand.IndexOf('\n');
		if (crPos != -1)
		{
			String targetPath = oldSourceCommand.Substring(0, crPos);
			if (FileExists(targetPath))
				existsKind = DbgFileExistKind_Found;
			else
				existsKind = DbgFileExistKind_HasOldSourceCommand;
		}
	}

	return existsKind;
}

void DbgModule::EnableWriting(addr_target address)
{
	for (int sectionIdx = 0; sectionIdx < (int)mSections.size(); sectionIdx++)
	{
		DbgSection* section = &mSections[sectionIdx];
		if ((address >= mImageBase + section->mAddrStart) && (address < mImageBase + section->mAddrStart + section->mAddrLength))
		{
			if (!section->mWritingEnabled)
			{
				section->mOldProt = mDebugger->EnableWriting(mImageBase + section->mAddrStart, (int32)section->mAddrLength);
				section->mWritingEnabled = true;
			}
		}
	}
}

void DbgModule::RevertWritingEnable()
{
	for (int sectionIdx = 0; sectionIdx < (int)mSections.size(); sectionIdx++)
	{
		DbgSection* section = &mSections[sectionIdx];
		if (section->mWritingEnabled)
		{
			mDebugger->SetProtection(mImageBase + section->mAddrStart, (int32)section->mAddrLength, section->mOldProt);
			section->mWritingEnabled = false;
		}
	}
}

template <typename TRadixMap>
static void RemoveInvalidRange(TRadixMap& radixMap, addr_target startAddr, int addrLength)
{
	radixMap.RemoveRange(startAddr, addrLength);
}

template <typename TMap>
static void RemoveInvalidMapRange(TMap& map, addr_target startAddr, int addrLength)
{
	auto itr = map.lower_bound(startAddr);
	while (itr != map.end())
	{
		auto val = itr->first;
		if (val >= startAddr + addrLength)
			return;
		itr = map.erase(itr);
	}
}

void DbgModule::RemoveTargetData()
{
	BP_ZONE("DbgModule::RemoveTargetData");

	for (auto srcFileRef : mSrcFileDeferredRefs)
		srcFileRef->RemoveDeferredRefs(this);

	HashSet<DbgSrcFile*> visitedFiles;
	for (auto compileUnit : mCompileUnits)
	{
		for (auto& fileRef : compileUnit->mSrcFileRefs)
		{
			if (visitedFiles.Add(fileRef.mSrcFile))
			{
				fileRef.mSrcFile->RemoveLines(this);
			}
		}
	}

	RemoveInvalidRange(mDebugTarget->mSymbolMap, (addr_target)mImageBase, (int32)mImageSize);
	RemoveInvalidRange(mDebugTarget->mSubprogramMap, (addr_target)mImageBase, (int32)mImageSize);
	RemoveInvalidRange(mDebugTarget->mExceptionDirectoryMap, (addr_target)mImageBase, (int32)mImageSize);
	RemoveInvalidRange(mDebugTarget->mContribMap, (addr_target)mImageBase, (int32)mImageSize);
	RemoveInvalidMapRange(mDebugTarget->mDwFrameDescriptorMap, (addr_target)mImageBase, (int32)mImageSize);
	RemoveInvalidMapRange(mDebugTarget->mCOFFFrameDescriptorMap, (addr_target)mImageBase, (int32)mImageSize);

	//mDebugTarget->mDwFrameDescriptorMap.erase()

	// Remove any of our entries from the mHotReplacedMethodList from 'primary modules' that are not going away
	for (auto dbgType : mHotPrimaryTypes)
	{
		DbgSubprogram** nextSrc = &dbgType->mHotReplacedMethodList.mHead;
		while (*nextSrc != NULL)
		{
			auto* subprogram = *nextSrc;
			if (subprogram->mCompileUnit->mDbgModule == this)
				*nextSrc = subprogram->mNext;
			else
				nextSrc = &(*nextSrc)->mNext;;
		}
	}
}

void DbgModule::ReportMemory(MemReporter* memReporter)
{
	//memReporter->Add("BumpAlloc_Used", mAlloc.GetAllocSize());
	//memReporter->Add("BumpAlloc_Unused", mAlloc.GetTotalAllocSize() - mAlloc.GetAllocSize());

	memReporter->AddBumpAlloc("BumpAlloc", mAlloc);
	memReporter->AddVec(mTypes);
	memReporter->AddVec(mSubprograms);
	//memReporter->Add("TypeMap", mTypeMap.mAlloc.GetTotalAllocSize() + sizeof(StrHashMap<DbgType*>));
	memReporter->AddHashSet("TypeMap", mTypeMap.mMap);
	memReporter->Add("SymbolNameMap", mSymbolNameMap.mAlloc.GetTotalAllocSize() + sizeof(StrHashMap<DbgType*>));

	if (mOrigImageData != NULL)
	{
		memReporter->BeginSection("OrigImageData");
		mOrigImageData->ReportMemory(memReporter);
		memReporter->EndSection();
	}
}

DbgType* DbgModule::GetPointerType(DbgType* innerType)
{
	auto linkedModule = GetLinkedModule();

	BF_ASSERT(innerType->GetDbgModule()->GetLinkedModule() == linkedModule);

	if (innerType->mPtrType == NULL)
	{
		BP_ALLOC_T(DbgType);
		auto ptrType = mAlloc.Alloc<DbgType>();
		ptrType->mCompileUnit = innerType->mCompileUnit;
		ptrType->mLanguage = innerType->mLanguage;
		ptrType->mTypeCode = DbgType_Ptr;
		ptrType->mTypeParam = innerType;
		ptrType->mSize = sizeof(addr_target);
		ptrType->mAlign = (int)ptrType->mSize;
		ptrType->mTypeIdx = (int32)linkedModule->mTypes.size();
		linkedModule->mTypes.push_back(ptrType);

		innerType->mPtrType = ptrType;
	}

	return innerType->mPtrType;
}

DbgType* DbgModule::GetConstType(DbgType* innerType)
{
	auto linkedModule = GetLinkedModule();

	BF_ASSERT(innerType->GetDbgModule()->GetLinkedModule() == linkedModule);

	/*auto itr = linkedModule->mConstTypes.find(innerType);
	if (itr != linkedModule->mConstTypes.end())
		return itr->second;*/

	DbgType* constType = NULL;
	if (linkedModule->mConstTypes.TryGetValue(innerType, &constType))
		return constType;

	BP_ALLOC_T(DbgType);
	constType = mAlloc.Alloc<DbgType>();
	constType->mCompileUnit = innerType->mCompileUnit;
	constType->mLanguage = innerType->mLanguage;
	constType->mTypeCode = DbgType_Const;
	constType->mTypeParam = innerType;
	constType->mSize = sizeof(addr_target);
	constType->mTypeIdx = (int32)linkedModule->mTypes.size();
	linkedModule->mTypes.push_back(constType);
	linkedModule->mConstTypes[innerType] = constType;

	return constType;
}

DbgType* DbgModule::GetPrimaryType(DbgType* dbgType)
{
	if (dbgType->mPriority <= DbgTypePriority_Normal)
	{
		if ((dbgType->mLanguage == DbgLanguage_Beef) && (dbgType->mName != NULL))
		{
			auto newTypeEntry = FindType(dbgType->mName, dbgType->mLanguage);
			if (newTypeEntry != NULL)
			{
				DbgType* newType = newTypeEntry->mValue;
				if ((newType->mTypeCode == DbgType_Ptr) && (newType->IsBfObjectPtr()))
					newType = newType->mTypeParam;
				newType->mPriority = DbgTypePriority_Primary_Implicit;
				return newType;
			}
		}
		else if (dbgType->mName != NULL)
		{
			auto newTypeEntry = FindType(dbgType->mName, dbgType->mLanguage);
			if (newTypeEntry != NULL)
			{
				DbgType* newType = newTypeEntry->mValue;
				newType = newType->RemoveModifiers();
				if (newType != dbgType)
					newType = GetPrimaryType(newType);
				newType->mPriority = DbgTypePriority_Primary_Implicit;
				return newType;
			}
		}
	}
	return dbgType;
}

DbgType* DbgModule::GetInnerTypeOrVoid(DbgType* dbgType)
{
	if (dbgType->mTypeParam != NULL)
		return dbgType->mTypeParam;
	return GetPrimitiveType(DbgType_Void, dbgType->mLanguage);
}

DbgType* DbgModule::FindTypeHelper(const String& typeName, DbgType* checkType)
{
	for (auto subType : checkType->mSubTypeList)
	{
		if (strcmp(subType->mTypeName, typeName.c_str()) == 0)
			return subType;
	}

	for (auto baseType : checkType->mBaseTypes)
	{
		auto retType = FindTypeHelper(typeName, baseType->mBaseType);
		if (retType != NULL)
			return retType;
	}
	return NULL;
}

DbgType* DbgModule::FindType(const String& typeName, DbgType* contextType, DbgLanguage language, bool bfObjectPtr)
{
	if ((language == DbgLanguage_Unknown) && (contextType != NULL))
		language = contextType->mLanguage;

	if (typeName.length() > 0)
	{
		if (typeName[typeName.length() - 1] == '*')
		{
			DbgType* dbgType = FindType(typeName.Substring(0, typeName.length() - 1), contextType, language, bfObjectPtr);
			if (dbgType == NULL)
				return NULL;
			return GetPointerType(dbgType);
		}
	}

	auto entry = GetLinkedModule()->mTypeMap.Find(typeName.c_str(), language);
	if (entry != NULL)
	{
		if ((bfObjectPtr) && (entry->mValue->IsBfObject()))
			return GetPointerType(entry->mValue);
		return entry->mValue;
	}

	if (contextType != NULL)
	{
		DbgType* checkType = contextType;
		if (checkType->IsPointer())
			checkType = checkType->mTypeParam;

		return FindTypeHelper(typeName, checkType);
	}
	return NULL;
}

DbgTypeMap::Entry* DbgModule::FindType(const char* typeName, DbgLanguage language)
{
	return GetLinkedModule()->mTypeMap.Find(typeName, language);

	/*auto& typeMap = GetLinkedModule()->mTypeMap;
	auto dbgTypeEntry = typeMap.Find(typeName);
	if (dbgTypeEntry == NULL)
		return NULL;
	if (dbgTypeEntry->mValue->mLanguage == language)
		return dbgTypeEntry;
	while (dbgTypeEntry != NULL)
	{
		DbgType* dbgType = dbgTypeEntry->mValue;
		if ((dbgType->GetLanguage() == language) && (typeMap.StrEqual(dbgType->mName, typeName)))
			return dbgTypeEntry;
		dbgTypeEntry = dbgTypeEntry->mNext;
	}*/
	//return NULL;
}

DbgType* DbgModule::GetPrimitiveType(DbgTypeCode typeCode, DbgLanguage language)
{
	if (language == DbgLanguage_Beef)
		return mBfPrimitiveTypes[(int)typeCode];
	else
		return mCPrimitiveTypes[(int)typeCode];
}

DbgType* DbgModule::GetPrimitiveStructType(DbgTypeCode typeCode)
{
	const char* name = mPrimitiveStructNames[typeCode];
	if (name == NULL)
		return NULL;
	return FindType(name, NULL, DbgLanguage_Beef);
}

DbgType* DbgModule::GetSizedArrayType(DbgType * elementType, int count)
{
	auto linkedModule = GetLinkedModule();
	if ((linkedModule != NULL) && (linkedModule != this))
	{
		return linkedModule->GetSizedArrayType(elementType, count);
	}

	DbgType** sizedArrayTypePtr;
	DbgSizedArrayEntry entry;
	entry.mElementType = elementType;
	entry.mCount = count;
	if (mSizedArrayTypes.TryAdd(entry, NULL, &sizedArrayTypePtr))
	{
		BP_ALLOC_T(DbgType);
		auto sizedArrayType = mAlloc.Alloc<DbgType>();
		sizedArrayType->mCompileUnit = elementType->mCompileUnit;
		sizedArrayType->mLanguage = elementType->mLanguage;
		sizedArrayType->mTypeCode = DbgType_SizedArray;
		sizedArrayType->mTypeParam = elementType;
		sizedArrayType->mSize = count * elementType->GetStride();
		sizedArrayType->mAlign = elementType->GetAlign();
		sizedArrayType->mSizeCalculated = true;
		sizedArrayType->mTypeIdx = (int32)mTypes.size();
		linkedModule->mTypes.push_back(sizedArrayType);
		*sizedArrayTypePtr = sizedArrayType;
	}
	return *sizedArrayTypePtr;
}