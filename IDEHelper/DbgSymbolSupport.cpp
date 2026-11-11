#include "DbgTargetHost.h"
#include "DebugTarget.h"
#include "DebugManager.h"
#include "Compiler/BfDemangler.h"
#include "BeefySysLib/util/BeefPerf.h"

#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF_DBG;

bool DbgTargetHost::IsInRunState()
{
	return (mRunState == RunState_Running) || (mRunState == RunState_Running_ToTempBreakpoint);
}

bool DbgTargetHost::IsReturnInstruction(addr_target address, DbgModuleMemoryCache* memoryCache)
{
	DbgInstInfo inst;
	return (DecodeInstInfo(address, memoryCache, NULL, &inst)) && (inst.mIsReturn);
}

static int gFindLineDataAt = 0;

DbgLineData* DbgTargetHost::FindLineDataAtAddress(addr_target address, DbgSubprogram** outSubProgram, DbgSrcFile** outSrcFile, int* outLineIdx, DbgOnDemandKind onDemandKind)
{
	gFindLineDataAt++;

	BP_ZONE("WinDebugger::FindLineDataAtAddress");

	auto dwSubprogram = mDebugTarget->FindSubProgram((addr_target)address, onDemandKind);
	if (dwSubprogram == NULL)
		return NULL;

	DbgModule::FixupLineDataForSubprogram(dwSubprogram);
	auto lineData = dwSubprogram->FindClosestLine(address, outSubProgram, outSrcFile, outLineIdx);

	return lineData;
}

DbgLineData* DbgTargetHost::FindLineDataInSubprogram(addr_target address, DbgSubprogram* dwSubprogram)
{
	auto dwCompileUnit = dwSubprogram->mCompileUnit;

	DbgModule::FixupLineDataForSubprogram(dwSubprogram);
	auto lineData = dwSubprogram->FindClosestLine(address);
	return lineData;
}

bool DbgTargetHost::IsPaused()
{
	return (mRunState == RunState_Paused) || (mRunState == RunState_Breakpoint) || (mRunState == RunState_Exception) || (mRunState == RunState_DebugEval_Done);
}

String DbgTargetHost::GetAutoLocals(int stackFrameIdx, bool showRegs)
{
	BP_ZONE("WinDebugger::GetAutoExpressions");

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (IsInRunState())
		return "";

	if (!IsPaused())
		return "";

	if (mCallStack.size() == 0)
		UpdateCallStack();
	String result;

	Array<String> localList;

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);
	UpdateCallStackMethod(actualStackFrameIdx);

	if (actualStackFrameIdx >= mCallStack.size())
		return "";
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	DbgSubprogram* dwSubprogram = wdStackFrame->mSubProgram;
	if (dwSubprogram == NULL)
		return "";

	auto langage = dwSubprogram->GetLanguage();

	DbgLineData* dwLineData = FindLineDataInSubprogram(wdStackFrame->GetSourcePC(), dwSubprogram);
	if (dwLineData == NULL)
		return "";

	dwSubprogram->PopulateSubprogram();
	mDebugTarget->GetAutoLocalsInBlock(localList, dwSubprogram, &dwSubprogram->mBlock, wdStackFrame, dwLineData);

	String lastLocal;
	for (auto local : localList)
	{
		if (langage == DbgLanguage_C)
		{
			if ((local == "this") && (strncmp(dwSubprogram->mName, "<lambda_", 8) == 0))
			{
				// Use explicit "$this" so we can see the actual capture
				result += "$this\n";
				continue;
			}
		}

		bool wasAlias = false;
		for (int i = 0; i < (int)local.length() - 1; i++)
		{
			if ((local[i] == '$') && (local[i + 1] == 'a'))
			{
				// Alias
				wasAlias = true;
				String localName = local.Substring(0, i) + "\n";
				if (localName != lastLocal)
				{
					result += localName;
					lastLocal = localName;
				}
				break;
			}
		}
		if (!wasAlias)
			result += local + "\n";
	}

	if (showRegs)
	{
		result += "$FLAGS\n";

		UpdateRegisterUsage(stackFrameIdx);
		for (int regIdx = 0; regIdx < (int)wdStackFrame->mRegForms.size(); regIdx++)
		{
			if (wdStackFrame->mRegForms[regIdx] != RegForm_Invalid)
				result += "$" + String(CPURegisters::GetRegisterName(regIdx)) + "\n";
		}
	}

	return result;
}

void DbgTargetHost::UpdateCallStackMethod(int stackFrameIdx)
{
	if (mCallStack.empty())
		return;

	int startIdx = std::min(stackFrameIdx, (int)mCallStack.size() - 1);
	while (startIdx >= 0)
	{
		WdStackFrame* wdStackFrame = mCallStack[startIdx];
		if (wdStackFrame->mHasGottenSubProgram)
			break;
		startIdx--;
	}
	startIdx++;

	for (int checkFrameIdx = startIdx; checkFrameIdx <= stackFrameIdx; checkFrameIdx++)
	{
		//BF_ASSERT(checkFrameIdx < mCallStack.size());
		if (checkFrameIdx >= mCallStack.size())
			break;

		WdStackFrame* wdStackFrame = mCallStack[checkFrameIdx];

		wdStackFrame->mHasGottenSubProgram = true;
		addr_target pcAddress = (addr_target)wdStackFrame->GetSourcePC();
		DbgSubprogram* dwSubprogram = mDebugTarget->FindSubProgram(pcAddress, DbgOnDemandKind_LocalOnly);
		wdStackFrame->mHasGottenSubProgram = true;
		wdStackFrame->mSubProgram = dwSubprogram;

		if ((dwSubprogram == NULL) && (IsMiniDumpDebugger()))
		{
			// FindSymbolAt will queue up debug info if necessary...
			String symbolName;
			addr_target offset;
			DbgModule* dbgModule;
			mDebugTarget->FindSymbolAt(pcAddress, &symbolName, &offset, &dbgModule);
		}

		auto prevStackFrame = wdStackFrame;

		// Insert inlines
		int insertIdx = checkFrameIdx + 1;
		while ((dwSubprogram != NULL) && (dwSubprogram->mInlineeInfo != NULL))
		{
			WdStackFrame* inlineStackFrame = new WdStackFrame();
			*inlineStackFrame = *wdStackFrame;
			inlineStackFrame->mInInlineMethod = true;
			wdStackFrame->mInInlineCall = true;
			inlineStackFrame->mSubProgram = dwSubprogram->mInlineeInfo->mInlineParent;
			mCallStack.Insert(insertIdx, inlineStackFrame);
			dwSubprogram = dwSubprogram->mInlineeInfo->mInlineParent;
			insertIdx++;
			checkFrameIdx++;

			prevStackFrame = inlineStackFrame;
		}
	}
}

void DbgTargetHost::GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	DbgSubprogram* subProgram = NULL;
	DbgLineData* callingLineData = FindLineDataAtAddress((addr_target)addr, &subProgram);

	if (inlineCallAddr != 0)
	{
		auto inlinedSubProgram = mDebugTarget->FindSubProgram(inlineCallAddr);
		if (inlinedSubProgram != 0)
		{
			DbgModule::FixupLineDataForSubprogram(inlinedSubProgram->mInlineeInfo->mRootInliner);
			DbgSubprogram* parentSubprogram = inlinedSubProgram->mInlineeInfo->mInlineParent; // Require it be in the inline parent
			auto foundLine = parentSubprogram->FindClosestLine(inlinedSubProgram->mBlock.mLowPC, &parentSubprogram);
			if (foundLine != NULL)
			{
				auto srcFile = parentSubprogram->GetLineSrcFile(*foundLine);
				*outFile = srcFile->GetLocalPath();
				*outLine = foundLine->mLine;
			}

			*outHotIdx = inlinedSubProgram->mCompileUnit->mDbgModule->mHotIdx;
			*outColumn = -1;

			DbgSubprogram* callingSubProgram = NULL;
			DbgLineData* callingLineData = FindLineDataAtAddress(inlinedSubProgram->mBlock.mLowPC - 1, &callingSubProgram);
			if ((callingLineData != NULL) && (callingSubProgram == subProgram))
			{
				auto callingSrcFile = callingSubProgram->GetLineSrcFile(*callingLineData);
				auto srcFile = callingSrcFile;
				*outFile = srcFile->GetLocalPath();

				if (*outLine == callingLineData->mLine)
					*outColumn = callingLineData->mColumn;
			}
			return;
		}
	}

	if (subProgram != NULL)
	{
		if ((subProgram->mInlineeInfo != NULL) && ((addr_target)addr >= subProgram->mBlock.mHighPC))
			callingLineData = &subProgram->mInlineeInfo->mLastLineData;

		*outHotIdx = subProgram->mCompileUnit->mDbgModule->mHotIdx;
		*outFile = subProgram->GetLineSrcFile(*callingLineData)->GetLocalPath();
		*outLine = callingLineData->mLine;
		*outColumn = callingLineData->mColumn;

		DbgModule::FixupLineDataForSubprogram(subProgram);
		DbgLineData* dwStartLineData = NULL;
		DbgLineData* dwEndLineData = NULL;
		if (subProgram->mLineInfo != NULL)
		{
			if (subProgram->mLineInfo->mLines.size() > 0)
			{
				dwStartLineData = &subProgram->mLineInfo->mLines[0];
				dwEndLineData = &subProgram->mLineInfo->mLines[subProgram->mLineInfo->mLines.mSize - 1];
			}
		}
		else
		{
			if (subProgram->mInlineeInfo != NULL)
			{
				dwStartLineData = &subProgram->mInlineeInfo->mFirstLineData;
				dwEndLineData = &subProgram->mInlineeInfo->mLastLineData;
			}
		}

		if (dwEndLineData != NULL)
		{
			*outDefLineStart = dwStartLineData->mLine;
			*outDefLineEnd = dwEndLineData->mLine;
		}
	}
}

String DbgTargetHost::GetStackFrameInfo(int stackFrameIdx, intptr* addr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd,
	int* outLine, int* outColumn, int* outLanguage, int* outStackSize, int8* outFlags)
{
	enum FrameFlags
	{
		FrameFlags_Optimized = 1,
		FrameFlags_HasPendingDebugInfo = 2,
		FrameFlags_CanGetOldSource = 4,
		FrameFlags_WasHotReplaced = 8,
		FrameFlags_HadError = 0x10
	};

	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mCallStack.size() == 0)
		UpdateCallStack();

	*addr = 0;
	*outFile = "";
	*outHotIdx = 0;
	*outDefLineStart = -1;
	*outDefLineEnd = -1;
	*outLine = -1;
	*outColumn = 0;
	*outLanguage = 0;
	*outStackSize = 0;
	*outFlags = 0;

	UpdateCallStackMethod(stackFrameIdx);

	if (stackFrameIdx >= mCallStack.size())
	{
		return "";
	}

	int actualStackFrameIdx = BF_MAX(0, stackFrameIdx);

	UpdateCallStackMethod(actualStackFrameIdx);
	WdStackFrame* wdStackFrame = mCallStack[actualStackFrameIdx];

	addr_target pcAddress = (addr_target)wdStackFrame->mRegisters.GetPC();
	if (stackFrameIdx == -1)
		pcAddress = mShowPCOverride;

	*addr = pcAddress;

	if (actualStackFrameIdx < (int)mCallStack.size() - 2)
	{
		WdStackFrame* prevStackFrame = mCallStack[actualStackFrameIdx + 1];
		// Inlined methods have no stack frame
		*outStackSize = prevStackFrame->mRegisters.GetSP() - wdStackFrame->mRegisters.GetSP();
	}

	const auto& _CheckHashSrcFile = [&](String& outStr, DbgModule* dbgModule, DbgSrcFile* srcFile)
	{
		if (srcFile->mHashKind != DbgHashKind_None)
		{
			outStr += "#";
			srcFile->GetHash(outStr);
		}
	};

	auto _SetFlags = [&](DbgSubprogram* dwSubprogram)
	{
		DbgModule* dbgModule = dwSubprogram->mCompileUnit->mDbgModule;
		if (dwSubprogram->mIsOptimized)
			*outFlags |= FrameFlags_Optimized;
		if (dbgModule->HasPendingDebugInfo())
			*outFlags |= FrameFlags_HasPendingDebugInfo;
		if (dbgModule->CanGetOldSource())
			*outFlags |= FrameFlags_CanGetOldSource;
		if ((dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) || (dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Invalid))
			*outFlags |= FrameFlags_WasHotReplaced;
	};

	auto _FixFilePath = [&](DbgModule* dbgModule)
	{
		if (outFile == NULL)
			return;

		if (outFile->StartsWith("$Emit"))
		{
			int dollarPos = outFile->IndexOf('$', 1);
			if (dollarPos == -1)
				return;

			outFile->Insert(dollarPos, StrFormat("%d", dbgModule->mId));
		}
	};

	if (wdStackFrame->mInInlineMethod)
	{
		WdStackFrame* nextStackFrame = mCallStack[actualStackFrameIdx - 1];
		auto subProgram = nextStackFrame->mSubProgram;

		_SetFlags(subProgram);

		DbgModule::FixupLineDataForSubprogram(subProgram->mInlineeInfo->mRootInliner);
		DbgSubprogram* parentSubprogram = subProgram->mInlineeInfo->mInlineParent; // Require it be in the inline parent
		auto foundLine = parentSubprogram->FindClosestLine(subProgram->mBlock.mLowPC, &parentSubprogram);
		if (foundLine != NULL)
		{
			auto srcFile = parentSubprogram->GetLineSrcFile(*foundLine);
			*outFile = srcFile->GetLocalPath();
			_CheckHashSrcFile(*outFile, subProgram->mCompileUnit->mDbgModule, srcFile);
			*outLine = foundLine->mLine;
		}

		*outLanguage = subProgram->GetLanguage();
		*outHotIdx = subProgram->mCompileUnit->mDbgModule->mHotIdx;
		*outColumn = -1;

		DbgSubprogram* callingSubProgram = NULL;
		DbgLineData* callingLineData = FindLineDataAtAddress(nextStackFrame->mSubProgram->mBlock.mLowPC - 1, &callingSubProgram);
		if ((callingLineData != NULL) && (callingSubProgram == wdStackFrame->mSubProgram))
		{
			auto callingSrcFile = callingSubProgram->GetLineSrcFile(*callingLineData);
			*outLanguage = callingSubProgram->mCompileUnit->mLanguage;
			auto srcFile = callingSrcFile;
			*outFile = srcFile->GetLocalPath();

			_CheckHashSrcFile(*outFile, subProgram->mCompileUnit->mDbgModule, srcFile);
			if (*outLine == callingLineData->mLine)
				*outColumn = callingLineData->mColumn;
		}

		String name = wdStackFrame->mSubProgram->ToString();
		DbgModule* dbgModule = wdStackFrame->mSubProgram->mCompileUnit->mDbgModule;
		DbgModule* linkedModule = dbgModule->GetLinkedModule();
		if (!linkedModule->mDisplayName.empty())
			name = linkedModule->mDisplayName + "!" + name;
		_FixFilePath(dbgModule);
		return name;
	}

	DbgSubprogram* dwSubprogram = wdStackFrame->mSubProgram;
	if (dwSubprogram != NULL)
	{
		String demangledName;
		if ((dwSubprogram->mName != NULL) && (strncmp(dwSubprogram->mName, ":Sep@", 5) == 0))
		{
			char* p;
			auto addr = strtoll(dwSubprogram->mName + 5, &p, 16);
			if (addr != 0)
			{
				auto parentSubprogram = mDebugTarget->FindSubProgram(addr);
				if (parentSubprogram != NULL)
					demangledName = parentSubprogram->ToString();
			}
		}
		if (demangledName.IsEmpty())
		{
			dwSubprogram->ToString(demangledName, true);
		}

		DbgSrcFile* dwSrcFile = NULL;
		DbgLineData* dwLineData = NULL;

		DbgModule::FixupLineDataForSubprogram(dwSubprogram);
		addr_target findAddress = wdStackFrame->GetSourcePC();
		DbgSubprogram* specificSubprogram = dwSubprogram;
		dwLineData = dwSubprogram->FindClosestLine(findAddress, &specificSubprogram);

		if ((dwLineData == NULL) && (dwSubprogram->mInlineeInfo != NULL) && (findAddress >= dwSubprogram->mBlock.mHighPC))
			dwLineData = &dwSubprogram->mInlineeInfo->mLastLineData;

		if (dwLineData != NULL)
			dwSrcFile = dwSubprogram->GetLineSrcFile(*dwLineData);

		DbgLineData* dwStartLineData = NULL;
		DbgLineData* dwEndLineData = NULL;
 		if (dwSubprogram->mLineInfo != NULL)
 		{
			if (dwSubprogram->mLineInfo->mLines.size() > 0)
			{
				dwStartLineData = &dwSubprogram->mLineInfo->mLines[0];
				dwEndLineData = &dwSubprogram->mLineInfo->mLines[dwSubprogram->mLineInfo->mLines.mSize - 1];
			}
 		}
		else
		{
			if (dwSubprogram->mInlineeInfo != NULL)
			{
				dwStartLineData = &dwSubprogram->mInlineeInfo->mFirstLineData;
				dwEndLineData = &dwSubprogram->mInlineeInfo->mLastLineData;
			}
		}

		DbgModule* dbgModule = dwSubprogram->mCompileUnit->mDbgModule;
		DbgModule* linkedModule = dbgModule->GetLinkedModule();
		if (!linkedModule->mDisplayName.empty())
			demangledName = linkedModule->mDisplayName + "!" + demangledName;

		if ((dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Replaced) || (dwSubprogram->mHotReplaceKind == DbgSubprogram::HotReplaceKind_Invalid))
			demangledName = "#" + demangledName;

		_SetFlags(dwSubprogram);

		if ((dwLineData != NULL) && (dwSrcFile != NULL))
		{
			*outFile = dwSrcFile->GetLocalPath();
			_CheckHashSrcFile(*outFile, dbgModule, dwSrcFile);

			*outHotIdx = dbgModule->mHotIdx;
			*outLine = dwLineData->mLine;
			*outColumn = dwLineData->mColumn;
			*outLanguage = (int)dwSubprogram->mCompileUnit->mLanguage;

			if (dwEndLineData != NULL)
			{
				*outDefLineStart = dwStartLineData->mLine;
				*outDefLineEnd = dwEndLineData->mLine;
			}

			_FixFilePath(dbgModule);
			return demangledName;
		}
		else
		{
			_FixFilePath(dbgModule);
			return demangledName + StrFormat("+0x%X", pcAddress - dwSubprogram->mBlock.mLowPC);
		}
	}
	else
	{
		String symbolName;
		addr_target offset;
		DbgModule* dbgModule = NULL;
		if (mDebugTarget->FindSymbolAt(pcAddress, &symbolName, &offset, &dbgModule))
		{
			if (dbgModule->HasPendingDebugInfo())
			{
				*outFlags |= FrameFlags_HasPendingDebugInfo;

				if (mPendingDebugInfoLoad.ContainsKey(dbgModule))
				{
					String outName = EncodeDataPtr(pcAddress, true);
					if ((dbgModule != NULL) && (!dbgModule->mDisplayName.empty()))
						outName = dbgModule->mDisplayName + "!<Loading...>" + outName;
					return outName;
				}
			}

			DbgModule* linkedModule = dbgModule->GetLinkedModule();
			String demangledName = BfDemangler::Demangle(symbolName, DbgLanguage_Unknown);
			if (!linkedModule->mDisplayName.empty())
				demangledName = linkedModule->mDisplayName + "!" + demangledName;
			_FixFilePath(dbgModule);
			return demangledName + StrFormat("+0x%X", offset);
		}
	}

	DbgModule* dbgModule = mDebugTarget->FindDbgModuleForAddress(pcAddress);
	DbgModule* linkedModule = NULL;
	if (dbgModule != NULL)
	{
		linkedModule = dbgModule->GetLinkedModule();
		if (dbgModule->HasPendingDebugInfo())
			*outFlags |= FrameFlags_HasPendingDebugInfo;
	}
	String outName = EncodeDataPtr(pcAddress, true);
	if ((linkedModule != NULL) && (!linkedModule->mDisplayName.empty()))
		outName = linkedModule->mDisplayName + "!" + outName;
	_FixFilePath(dbgModule);
	return outName;
}

String DbgTargetHost::FindCodeAddresses(const StringImpl& fileName, int line, int column, bool allowAutoResolve)
{
	String result;
	if (mDebugTarget == NULL)
		return "";

	DbgSrcFile* srcFile = mDebugTarget->GetSrcFile(fileName);
	if (srcFile == NULL)
		return result;

	bool foundInSequence = false;

	int bestLineOffset = 0x7FFFFFFF;

	for (auto dbgSubprogram : srcFile->mLineDataRefs)
	{
		for (auto& lineData : dbgSubprogram->mLineInfo->mLines)
		{
			auto lineSrcFile = dbgSubprogram->GetLineSrcFile(lineData);
			if (lineSrcFile != srcFile)
				continue;
			int lineOffset = lineData.mLine - line;

			if ((lineOffset >= 0) && (lineOffset <= 12) && (lineOffset <= bestLineOffset))
			{
				if (lineOffset < bestLineOffset)
				{
					bestLineOffset = lineOffset;
					result = "";
				}

				if (!foundInSequence)
				{
					auto addr = dbgSubprogram->GetLineAddr(lineData);
					result += EncodeDataPtr(addr, false) + "\t" + dbgSubprogram->ToString() + "\n";
				}
			}

			// New sequence?
			if (!lineData.IsStackFrameSetup())
				foundInSequence = false;
		}
	}

	return result;
}

void DbgTargetHost::CreateFilterName(String& name, DbgType* type)
{
	CreateFilterName(name, type->mParent);

	switch (type->mTypeCode)
	{
	case DbgType_Namespace:
	case DbgType_Struct:
	case DbgType_Class:
		name += type->mName;
		break;
	}
}

void DbgTargetHost::CreateFilterName(String& name, const char* srcStr, DbgLanguage language)
{
	int chevronDepth = 0;
	const char* cPtr = srcStr;
	for (; true; cPtr++)
	{
		char c = *cPtr;
		if (c == 0)
			break;
		if (c == '>')
			chevronDepth--;
		bool inGeneric = chevronDepth > 0;
		if (c == '<')
			chevronDepth++;
		if (inGeneric) // Bundle all generic instances together
			continue;
		if (c == '[') // Bundle all arrays together
			name.clear();
		if (c == '(')
			return; // Start of params
		if ((c == ':') && (cPtr[1] == ':') && (language == DbgLanguage_Beef))
		{
			name.Append('.');
			cPtr++;
		}
		else
			name.Append(c);
	}
}

void DbgTargetHost::CreateFilterName(String& name, DbgSubprogram* subprogram)
{
	auto language = subprogram->GetLanguage();
	if (subprogram->mName == NULL)
	{
		if (subprogram->mLinkName[0] == '<')
		{
			name += subprogram->mLinkName;
			return;
		}
		name = BfDemangler::Demangle(subprogram->mLinkName, language);
		// Strip off the params since we need to generate those ourselves
		int parenPos = (int)name.IndexOf('(');
		if (parenPos != -1)
			name.RemoveToEnd(parenPos);
		return;
	}
	else if (subprogram->mHasQualifiedName)
	{
		const char* cPtr = subprogram->mName;
		if (strncmp(cPtr, "_bf::", 5) == 0)
		{
			CreateFilterName(name, cPtr + 5, DbgLanguage_Beef);
			name.Replace(".__BfStaticCtor", ".this$static");
			name.Replace(".__BfCtorClear", ".this$clear");
			name.Replace(".__BfCtor", ".this");
		}
		else
			CreateFilterName(name, subprogram->mName, language);

		return;
	}
	else
	{
		if (subprogram->mParentType != NULL)
		{
			String parentName = subprogram->mParentType->ToString();
			CreateFilterName(name, parentName.c_str(), language);
			if (!name.empty())
			{
				if (language == DbgLanguage_Beef)
					name += ".";
				else
					name += "::";
			}
		}

		if ((language == DbgLanguage_Beef) && (subprogram->mParentType != NULL) && (subprogram->mParentType->mTypeName != NULL) &&
			(strcmp(subprogram->mName, subprogram->mParentType->mTypeName) == 0))
			name += "this";
		else if ((language == DbgLanguage_Beef) && (subprogram->mName[0] == '~'))
			name += "~this";
		else if (strncmp(subprogram->mName, "_bf::", 5) == 0)
		{
			CreateFilterName(name, subprogram->mName + 5, DbgLanguage_Beef);
		}
		else
		{
			CreateFilterName(name, subprogram->mName, language);
		}
	}

	if (name.empty())
		name += "`anon";
	if ((name[name.length() - 1] == '!') || (name[0] == '<'))
	{
		if (language == DbgLanguage_Beef)
		{
			// It's a mixin - assert that there's no params
			//BF_ASSERT(subprogram->mParams.Size() == 0);
		}
		return;
	}
}

String DbgTargetHost::FindLineCallAddresses(intptr inAddress)
{
	String callAddresses;

	addr_target address = (addr_target)inAddress;

	DbgSubprogram* dwSubprogram = NULL;

	DbgLineData* startLineData = FindLineDataAtAddress(address, &dwSubprogram, NULL);

	if (dwSubprogram == NULL)
		return "";

	CPURegisters registers;
	PopulateRegisters(&registers);

	auto inlinerSubprogram = dwSubprogram->GetRootInlineParent();
	DbgModule::FixupLineDataForSubprogram(inlinerSubprogram);

	if (inlinerSubprogram->mLineInfo->mLines.mSize == 0)
		return "";
	auto lineData = &inlinerSubprogram->mLineInfo->mLines[0];

	addr_target addr = dwSubprogram->mBlock.mLowPC;
	addr_target endAddr = dwSubprogram->mBlock.mHighPC;

	DbgSubprogram* checkSubprogram = dwSubprogram;
	DbgLineData* checkLineData = lineData;
	addr_target checkLineAddr = 0;

	int lineIdx = 0;
	while (checkLineData != NULL)
	{
		//auto nextLineData = dwSubprogram->mCompileUnit->mLineDataMap.GetNext(checkLineData);
		++lineIdx;

		DbgLineData* nextLineData = NULL;
		addr_target nextLineAddr;
		if (lineIdx < inlinerSubprogram->mLineInfo->mLines.size())
		{
			nextLineData = &inlinerSubprogram->mLineInfo->mLines[lineIdx];
			nextLineAddr = dwSubprogram->GetLineAddr(*nextLineData);
		}
		else
			nextLineAddr = inlinerSubprogram->mBlock.mHighPC;

		// This stuff doesn't make sense...
		DbgSubprogram* nextSubProgram;
		if (nextLineData != NULL)
		{
			if (nextLineAddr > dwSubprogram->mBlock.mHighPC)
				break;

			endAddr = nextLineAddr;
			nextSubProgram = mDebugTarget->FindSubProgram(endAddr);
			if (nextSubProgram != NULL)
			{
				auto dbgModule = nextSubProgram->mCompileUnit->mDbgModule;
				dbgModule->ParseSymbolData();
			}
		}
		else
		{
			nextSubProgram = dwSubprogram;
			endAddr = dwSubprogram->mBlock.mHighPC;
		}

		auto _HandleSection = [&]()
		{
			while (addr < endAddr)
			{
				DbgInstInfo inst;
				if (!mDebugTarget->DecodeInstInfo(addr, &registers, &inst))
					break;

				if (inst.mIsCall)
				{
					bool addSymbol = true;

					if (addr < (addr_target)inAddress)
						callAddresses += "-";
					callAddresses += EncodeDataPtr(addr, false);

					addr_target targetAddr = inst.mTarget;
					if (targetAddr != 0)
					{
						String outSymbol;
						auto subprogram = mDebugTarget->FindSubProgram(targetAddr);
						if (subprogram != NULL)
						{
							CreateFilterName(outSymbol, subprogram);
							addSymbol = true;
						}
						else
						{
							addr_target offset = 0;
							String fullSymbolName;
							if (mDebugTarget->FindSymbolAt(targetAddr, &outSymbol, &offset))
							{
								if (offset < 0x200)
								{
									//outSymbol = BfDemangler::Demangle(outSymbol, dwSubprogram->GetLanguage());
									if (outSymbol == "___chkstk_ms")
										addSymbol = false;
									else
									{
										String demangledName = BfDemangler::Demangle(outSymbol, DbgLanguage_C);
										outSymbol.clear();
										CreateFilterName(outSymbol, demangledName.c_str(), DbgLanguage_C);
									}
								}
								else
									outSymbol.clear();
							}
						}

						if (addSymbol)
						{
							if (outSymbol.empty())
								callAddresses += "\tFunc@" + EncodeDataPtr(targetAddr, false);
							else
								callAddresses += "\t" + outSymbol;

							String attrs;
							bool isFiltered = false;
							if (subprogram != NULL)
							{
								subprogram->PopulateSubprogram();
								isFiltered = subprogram->mIsStepFilteredDefault;
								if (isFiltered)
									attrs += "d"; // 'd' for default filtered
							}

							StepFilter* stepFilterPtr = NULL;
							if (mDebugManager->mStepFilters.TryGetValue(outSymbol, &stepFilterPtr))
								isFiltered = stepFilterPtr->IsFiltered(isFiltered);

							if (isFiltered)
								attrs += "f";  // 'f' for filter

							if (!attrs.IsEmpty())
								callAddresses += "\t" + attrs;
						}
					}

					if (addSymbol)
						callAddresses += "\n";
				}

				addr += inst.mSize;
			}
		};

		// For inlining - only add calls that are found either directly in our main block (not an inlined block)
		//  But add inlined methods when their parent is our current block
		if ((checkSubprogram == dwSubprogram) && (checkLineData->mLine == startLineData->mLine))
		{
			_HandleSection();
		}
		else if ((checkSubprogram->mInlineeInfo != NULL) && (checkSubprogram->mInlineeInfo->mInlineParent == dwSubprogram))
		{
			if (checkLineAddr == checkSubprogram->mBlock.mLowPC)
			{
				addr_target inlineStartAddr = checkSubprogram->mBlock.mLowPC;

				// Find the calling line
				DbgSubprogram* callingSubprogram = dwSubprogram;
				auto checkLineData = dwSubprogram->FindClosestLine(inlineStartAddr, &callingSubprogram);
				if ((checkLineData != NULL) && (checkLineData->mCtxIdx == startLineData->mCtxIdx) && (checkLineData->mLine == startLineData->mLine))
				{
					if (inlineStartAddr <= (addr_target)inAddress)
						callAddresses += "-";
					callAddresses += EncodeDataPtr(inlineStartAddr, false);
					String outSymbol;
					CreateFilterName(outSymbol, checkSubprogram);
					callAddresses += "\t" + outSymbol;

					bool isFiltered = dwSubprogram->mIsStepFilteredDefault;
					StepFilter* stepFilterPtr;
					if (mDebugManager->mStepFilters.TryGetValue(outSymbol, &stepFilterPtr))
						isFiltered = stepFilterPtr->IsFiltered(isFiltered);

					if (isFiltered)
						callAddresses += "\tf"; // 'f' for filter

					callAddresses += "\n";
				}

// 				if (checkSubprogram->mBlock.mHighPC < endAddr)
// 				{
// 					addr = checkSubprogram->mBlock.mHighPC;
// 					_HandleSection();
// 				}
			}

			// If we have unattributed data after the end of an inlined method, add that
			if ((endAddr > checkSubprogram->mBlock.mHighPC) && (nextSubProgram == dwSubprogram))
			{
				addr = checkSubprogram->mBlock.mHighPC;
				_HandleSection();
			}
		}

		checkLineData = nextLineData;
		checkSubprogram = nextSubProgram;
		checkLineAddr = nextLineAddr;
		addr = endAddr;
	}

	return callAddresses;
}
