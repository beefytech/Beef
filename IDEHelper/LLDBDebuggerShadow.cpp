#include "LLDBDebugger.h"

#ifdef LLDB_ENABLED

#include "DebugManager.h"
#include "DebugTarget.h"
#include "DbgModule.h"
#include "ElfDbgModule.h"
#include "DWARFInfo.h"
#include "BeefySysLib/util/AllocDebug.h"

USING_NS_BF;
USING_NS_BF_DBG;

void LLDBLog(const char* fmt ...);

static const int SHADOW_MAX_REPORTED_MISMATCHES = 200;

static String ShadowNormalizePath(const StringImpl& path)
{
	String result = path;
	result.Replace('\\', '/');
	while (result.Contains("/./"))
		result.Replace("/./", "/");
	while (result.Contains("//"))
		result.Replace("//", "/");
	return result;
}

// LLDB names functions from their linkage names: no Beef parameter lists, '.' for nested types, "~ClassA" for
// destructors and the "__im" suffix of methods taking a splatted mutable this
static String ShadowFunctionName(const StringImpl& name)
{
	String result = name;
	int parenIdx = (int)result.IndexOf('(');
	if (parenIdx != -1)
		result.RemoveToEnd(parenIdx);
	result.Replace('+', '.');
	if (result.EndsWith("__im"))
		result.RemoveToEnd(result.length() - 4);
	int lastDot = (int)result.LastIndexOf('.');
	if ((lastDot != -1) && (lastDot + 1 < (int)result.length()) && (result[lastDot + 1] == '~'))
	{
		result.RemoveToEnd(lastDot + 1);
		result += "~this";
	}
	return result;
}

// LLDB shows an inlined frame by its DW_AT_name alone, so only the unqualified name and the linkage name are compared
static String ShadowInlineName(const StringImpl& name)
{
	String result = ShadowFunctionName(name);
	int nameStart = (int)result.IndexOf('!') + 1;
	int nameEnd = (int)result.IndexOf('!', nameStart);
	if (nameEnd == -1)
		nameEnd = (int)result.length();
	int lastDot = (int)result.LastIndexOf('.', nameEnd - 1);
	if (lastDot >= nameStart)
		result.Remove(nameStart, lastDot + 1 - nameStart);
	return result;
}

// WinDebugger::CheckBreakpoint's choice of addresses, without hot versions: the requested line, or else the
// nearest following line within 6, bound at the first row of each run of rows on that line
static void ShadowBindBreakpoint(DebugTarget* debugTarget, DbgSrcFile* srcFile, int lineNum, int instrOffset, Array<addr_target>& outAddrs)
{
	for (int pass = 0; pass < 2; pass++)
	{
		bool foundLine = false;
		bool foundInSequence = false;
		DbgSubprogram* lastFoundSubprogram = NULL;
		int bestLineNum = -1;
		int bestLineOffset = 0x7FFFFFFF;

		for (auto subprogram : srcFile->mLineDataRefs)
		{
			DbgLineInfo* lineInfo = subprogram->mLineInfo;
			if (lineInfo == NULL)
				continue;
			DbgModule::FixupLineDataForSubprogram(subprogram);
			for (int lineIdx = 0; lineIdx < lineInfo->mLines.mSize; lineIdx++)
			{
				auto lineData = &lineInfo->mLines[lineIdx];
				if (lineInfo->mContexts[lineData->mCtxIdx].mSrcFile != srcFile)
					continue;
				if ((lineData->mColumn == -1) && (instrOffset == -1))
					continue;

				int lineOffset = lineData->mLine - lineNum;
				if (lineOffset == 0)
				{
					foundLine = true;
					addr_target address = subprogram->GetLineAddr(*lineData);
					DbgSubprogram* foundSubprogram = debugTarget->FindSubProgram(address);
					if ((foundInSequence) && (foundSubprogram != lastFoundSubprogram))
						foundInSequence = false;
					if (!foundInSequence)
					{
						lastFoundSubprogram = foundSubprogram;
						outAddrs.Add(address);
						foundInSequence = true;
					}
				}
				else
					foundInSequence = false;

				if ((lineOffset >= 0) && (lineOffset <= 6) && (lineOffset < bestLineOffset))
				{
					bestLineNum = lineData->mLine;
					bestLineOffset = lineOffset;
				}
			}
		}

		if ((foundLine) || (bestLineNum == -1))
			break;
		lineNum = bestLineNum;
	}
}

void LLDBDebugger::ShadowInit()
{
	const char* shadowEnv = getenv("BEEF_DBG_SHADOW");
	mShadowEnabled = (shadowEnv != NULL) && (atoi(shadowEnv) != 0);
	mShadowExtraCount = 0;
	const char* reportEnv = getenv("BEEF_DBG_SHADOW_REPORT");
	if (reportEnv != NULL)
		mShadowReportPath = reportEnv;
}

// DBGSHADOW lines read "DBGSHADOW <query>: lldb=<result> beef=<result>"; source lines in them are 0-based, the IDE's convention
void LLDBDebugger::ShadowCompare(const char* query, const StringImpl& lldbResult, const StringImpl& beefResult)
{
	mShadowQueryCounts[query]++;
	if (lldbResult == beefResult)
		return;
	mShadowMismatchCounts[query]++;
	ShadowReportLine(StrFormat("DBGSHADOW %s: lldb=%s beef=%s", query, lldbResult.c_str(), beefResult.c_str()));
}

void LLDBDebugger::ShadowReportLine(const StringImpl& line)
{
	LLDBLog("%s\n", line.c_str());
	if (mShadowMismatchLines.size() < SHADOW_MAX_REPORTED_MISMATCHES)
		mShadowMismatchLines.Add(line);
}

String LLDBDebugger::HostAddressSourceLocation(intptr address)
{
	DbgSubprogram* subprogram = NULL;
	DbgSrcFile* srcFile = NULL;
	DbgLineData* lineData = FindLineDataAtAddress((addr_target)address, &subprogram, &srcFile);
	if ((lineData == NULL) || (srcFile == NULL))
		return String();
	return StrFormat("%s:%d", ShadowNormalizePath(srcFile->GetLocalPath()).c_str(), lineData->mLine);
}

void LLDBDebugger::ShadowCheckFrames()
{
	if ((mDebugTarget == NULL) || (mCallStack.IsEmpty()))
		return;

	int concreteIdx = -1;
	for (int beefIdx = 0; beefIdx < (int)mCallStack.size(); beefIdx++)
	{
		if (mCallStack[beefIdx]->mInInlineMethod)
			continue;
		concreteIdx++;
		if (concreteIdx >= (int)mHostFrameGroupStart.size())
			break;

		int groupStart = mHostFrameGroupStart[concreteIdx];
		int lldbIdx = groupStart;
		while ((lldbIdx < (int)mLLDBFrames.size()) && (mLLDBFrames[lldbIdx].IsInlined()))
			lldbIdx++;
		if (lldbIdx >= (int)mLLDBFrames.size())
			break;

		intptr addr = 0;
		String file;
		int32 hotIdx = 0;
		int32 defLineStart = 0;
		int32 defLineEnd = 0;
		int32 line = 0;
		int32 column = 0;
		int32 language = 0;
		int32 stackSize = 0;
		int8 flags = 0;

		String lldbName = LLDBDebugger::GetStackFrameInfo(lldbIdx, &addr, &file, &hotIdx, &defLineStart, &defLineEnd, &line, &column, &language, &stackSize, &flags);
		LLDBDebugger::GetStackFrameInfo(groupStart, &addr, &file, &hotIdx, &defLineStart, &defLineEnd, &line, &column, &language, &stackSize, &flags);
		String lldbLine = StrFormat("%s:%d", ShadowNormalizePath(file).c_str(), line);

		String beefName = DbgTargetHost::GetStackFrameInfo(beefIdx, &addr, &file, &hotIdx, &defLineStart, &defLineEnd, &line, &column, &language, &stackSize, &flags);
		String beefLine = StrFormat("%s:%d", ShadowNormalizePath(file).c_str(), line);

		int beefGroupEnd = beefIdx + 1;
		while ((beefGroupEnd < (int)mCallStack.size()) && (mCallStack[beefGroupEnd]->mInInlineMethod))
			beefGroupEnd++;
		int beefDepth = beefGroupEnd - beefIdx - 1;
		int lldbDepth = lldbIdx - groupStart;
		if (beefDepth > 0)
			beefName = DbgTargetHost::GetStackFrameInfo(beefGroupEnd - 1, &addr, &file, &hotIdx, &defLineStart, &defLineEnd, &line, &column, &language, &stackSize, &flags);

		WdStackFrame* wdStackFrame = mCallStack[beefIdx];
		addr_target sourcePC = wdStackFrame->GetSourcePC();
		if (wdStackFrame->mSubProgram != NULL)
		{
			ShadowCompare("frame_name", ShadowFunctionName(lldbName), ShadowFunctionName(beefName));
			ShadowCompare("frame_line", lldbLine, beefLine);
			ShadowCompare("inline_depth", StrFormat("%d", lldbDepth), StrFormat("%d", beefDepth));
			if (lldbDepth == beefDepth)
			{
				for (int depthIdx = 0; depthIdx <= lldbDepth; depthIdx++)
				{
					String lldbInlineName = LLDBDebugger::GetStackFrameInfo(groupStart + depthIdx, &addr, &file, &hotIdx, &defLineStart, &defLineEnd, &line, &column, &language, &stackSize, &flags);
					String lldbInlineLine = StrFormat("%s:%d", ShadowNormalizePath(file).c_str(), line);
					String beefInlineName = DbgTargetHost::GetStackFrameInfo(beefIdx + depthIdx, &addr, &file, &hotIdx, &defLineStart, &defLineEnd, &line, &column, &language, &stackSize, &flags);
					String beefInlineLine = StrFormat("%s:%d", ShadowNormalizePath(file).c_str(), line);
					if (depthIdx < lldbDepth)
						ShadowCompare("inline_name", ShadowInlineName(lldbInlineName), ShadowInlineName(beefInlineName));
					if (depthIdx > 0)
						ShadowCompare("inline_line", lldbInlineLine, beefInlineLine);
				}
			}
			String lldbAddrLine = LLDBDebugger::GetAddressSourceLocation((intptr)sourcePC);
			int columnColon = (int)lldbAddrLine.LastIndexOf(':');
			if (columnColon > 0)
				lldbAddrLine.RemoveToEnd(columnColon);
			int lineColon = (int)lldbAddrLine.LastIndexOf(':');
			if (lineColon > 0)
			{
				int lldbLineNum = atoi(lldbAddrLine.c_str() + lineColon + 1);
				lldbAddrLine.RemoveToEnd(lineColon);
				lldbAddrLine += StrFormat(":%d", lldbLineNum - 1);
			}
			ShadowCompare("addr_line", ShadowNormalizePath(lldbAddrLine), HostAddressSourceLocation((intptr)sourcePC));

			int autosDepth = (lldbDepth == beefDepth) ? lldbDepth : 0;
			for (int depthIdx = 0; depthIdx <= autosDepth; depthIdx++)
			{
				String lldbAutos = String("\n") + LLDBDebugger::GetAutoLocals(groupStart + depthIdx, false);
				String beefAutos = DbgTargetHost::GetAutoLocals(beefIdx + depthIdx, false);
				String missing;
				for (auto name : beefAutos.Split('\n'))
				{
					if (name.IsEmpty())
						continue;
					String key = "\n";
					key += name;
					key += "\n";
					if (!lldbAutos.Contains(key))
					{
						missing += name;
						missing += " ";
					}
				}
				DbgSubprogram* frameSubprogram = mCallStack[beefIdx + depthIdx]->mSubProgram;
				bool isInlineFrame = (frameSubprogram != NULL) && (frameSubprogram->mInlineeInfo != NULL);
				// An empty list passes the subset rule trivially, so inline frames with autos are counted on their own
				if ((isInlineFrame) && (!beefAutos.IsEmpty()))
					mShadowQueryCounts["autos_inline_answered"]++;
				ShadowCompare(isInlineFrame ? "autos_subset_inline" : "autos_subset", "", missing);
			}

			if ((beefIdx == 0) && (beefDepth == 0))
				ShadowCheckLineCalls((addr_target)wdStackFrame->mRegisters.GetPC());
		}

		// Modules with only .dynsym are left out: LLDB also reads MiniDebugInfo locals and symbol versions there
		DbgModule* symbolModule = mDebugTarget->FindDbgModuleForAddress(sourcePC);
		if ((symbolModule != NULL) && (((ElfDbgModule*)symbolModule)->FindSection(".symtab") != NULL))
		{
			String lldbSymbol = LLDBDebugger::GetAddressSymbolName((intptr)sourcePC, false);
			String beefSymbol;
			addr_target offset = 0;
			if (!mDebugTarget->FindSymbolAt(sourcePC, &beefSymbol, &offset))
				beefSymbol = "!Invalid symbol";
			ShadowCompare("addr_symbol", lldbSymbol, beefSymbol);
		}
	}

	ShadowCheckTLS();
}

static bool ShadowSameFile(lldb::SBFileSpec lhs, lldb::SBFileSpec rhs)
{
	const char* lhsName = lhs.GetFilename();
	const char* rhsName = rhs.GetFilename();
	const char* lhsDir = lhs.GetDirectory();
	const char* rhsDir = rhs.GetDirectory();
	return (lhsName != NULL) && (rhsName != NULL) && (strcmp(lhsName, rhsName) == 0) &&
		(strcmp((lhsDir != NULL) ? lhsDir : "", (rhsDir != NULL) ? rhsDir : "") == 0);
}

// LLDB's direct-call targets come from the E8 rel32 bytes, independent of the operand parsing in DecodeInstInfo
void LLDBDebugger::ShadowCheckLineCalls(addr_target pc)
{
	Array<String> beefSites;
	String beefCalls = DbgTargetHost::FindLineCallAddresses((intptr)pc);
	for (auto entry : beefCalls.Split('\n'))
	{
		String site;
		site += entry;
		if (site.StartsWith('-'))
			site.Remove(0, 1);
		int tabIdx = (int)site.IndexOf('\t');
		if (tabIdx != -1)
			site.RemoveToEnd(tabIdx);
		if (site.IsEmpty())
			continue;
		addr_target siteAddr = (addr_target)strtoull(site.c_str(), NULL, 16);
		// Inlined methods are listed at their start, which is not a call instruction
		DbgInstInfo instInfo;
		if ((!mDebugTarget->DecodeInstInfo(siteAddr, NULL, &instInfo)) || (!instInfo.mIsCall))
			continue;
		beefSites.Add(StrFormat("%llx:%llx", (unsigned long long)siteAddr, (unsigned long long)instInfo.mTarget));
	}

	Array<String> lldbSites;
	lldb::SBAddress pcAddr;
	pcAddr.SetLoadAddress((lldb::addr_t)pc, mLLDBTarget);
	lldb::SBLineEntry pcLine = pcAddr.GetLineEntry();
	lldb::SBFunction function = pcAddr.GetFunction();
	if ((!pcLine.IsValid()) || (!function.IsValid()))
		return;
	lldb::SBInstructionList instructions = function.GetInstructions(mLLDBTarget);
	for (uint32 instIdx = 0; instIdx < instructions.GetSize(); instIdx++)
	{
		lldb::SBInstruction instruction = instructions.GetInstructionAtIndex(instIdx);
		lldb::SBAddress instAddr = instruction.GetAddress();
		lldb::SBLineEntry lineEntry = instAddr.GetLineEntry();
		if ((!lineEntry.IsValid()) || (lineEntry.GetLine() != pcLine.GetLine()) || (!ShadowSameFile(lineEntry.GetFileSpec(), pcLine.GetFileSpec())))
			continue;
		if (instAddr.GetBlock().GetContainingInlinedBlock().IsValid())
			continue;
		lldb::InstructionControlFlowKind controlFlowKind = instruction.GetControlFlowKind(mLLDBTarget);
		if ((controlFlowKind != lldb::eInstructionControlFlowKindCall) && (controlFlowKind != lldb::eInstructionControlFlowKindFarCall))
			continue;

		uint64 siteAddr = instAddr.GetLoadAddress(mLLDBTarget);
		uint64 target = 0;
		lldb::SBData data = instruction.GetData(mLLDBTarget);
		uint8 bytes[5] = { 0 };
		lldb::SBError error;
		if ((data.GetByteSize() == 5) && (data.ReadRawData(error, 0, bytes, 5) == 5) && (bytes[0] == 0xE8))
		{
			int32 rel = 0;
			memcpy(&rel, &bytes[1], 4);
			target = siteAddr + 5 + (int64)rel;
		}
		lldbSites.Add(StrFormat("%llx:%llx", (unsigned long long)siteAddr, (unsigned long long)target));
	}

	auto _Format = [](Array<String>& sites)
	{
		sites.Sort([](const String& lhs, const String& rhs) { return strcmp(lhs.c_str(), rhs.c_str()) < 0; });
		String result;
		for (auto& site : sites)
			result += site + " ";
		return result;
	};
	if (!lldbSites.IsEmpty())
		mShadowQueryCounts["line_calls_nonempty"]++;
	String location = StrFormat("%s:%d ", (pcLine.GetFileSpec().GetFilename() != NULL) ? pcLine.GetFileSpec().GetFilename() : "?", pcLine.GetLine() - 1);
	ShadowCompare("line_calls", location + _Format(lldbSites), location + _Format(beefSites));
}

void LLDBDebugger::ShadowCheckTLS()
{
	if ((mCallStack.IsEmpty()) || (mDebugTarget->mLaunchBinary == NULL))
		return;
	DbgModule* dbgModule = mDebugTarget->mLaunchBinary;
	WdStackFrame* wdStackFrame = mCallStack[0];

	HashSet<String> seenNames;
	for (auto dbgType : dbgModule->mTypes)
	{
		if ((dbgType->mCompileUnit == NULL) || (dbgType->mCompileUnit->mLanguage != DbgLanguage_Beef))
			continue;
		for (auto member : dbgType->mMemberList)
		{
			if ((!member->mIsStatic) || (member->mName == NULL) || (member->mLocationLen <= 0))
				continue;
			uint8 lastOp = member->mLocationData[member->mLocationLen - 1];
			if ((lastOp != DW_OP_GNU_push_tls_address) && (lastOp != DW_OP_form_tls_address))
				continue;

			String qualifiedName = dbgType->ToString(DbgLanguage_Beef);
			qualifiedName.Replace(".", "::");
			qualifiedName += "::";
			qualifiedName += member->mName;
			if (!seenNames.Add(qualifiedName))
				continue;

			String lldbName;
			lldb::SBValueList candidates = mLLDBTarget.FindGlobalVariables(member->mName, 64);
			for (uint32 candidateIdx = 0; candidateIdx < candidates.GetSize(); candidateIdx++)
			{
				const char* candidateName = candidates.GetValueAtIndex(candidateIdx).GetName();
				if ((candidateName != NULL) && (String(candidateName).EndsWith(qualifiedName)))
				{
					lldbName = candidateName;
					break;
				}
			}
			if (lldbName.IsEmpty())
				continue;

			// A target-level SBValue has no thread, so a thread-local is looked up through the stopped thread's frame
			uint64 lldbAddr = mLLDBFrames[0].GetValueForVariablePath(lldbName.c_str()).GetLoadAddress();

			DbgAddrType addrType = DbgAddrType_NoValue;
			addr_target beefAddr = (addr_target)dbgModule->EvaluateLocation(NULL, member->mLocationData, member->mLocationLen, wdStackFrame, &addrType);
			ShadowCompare("tls_addr", StrFormat("%s %llx", qualifiedName.c_str(), (unsigned long long)lldbAddr),
				StrFormat("%s %llx", qualifiedName.c_str(), (unsigned long long)beefAddr));
		}
	}
}

void LLDBDebugger::ShadowCheckBreakpoint(LLDBBreakpoint* bp)
{
	if ((bp->mFilePath.IsEmpty()) || (!bp->mLLDBBreakpoint.IsValid()) || (!bp->mVersionBreakpoints.IsEmpty()) || (bp->mInstrOffset > 0) || (!mLLDBProcess.IsValid()))
		return;
	HostSyncModules();
	if (mDebugTarget == NULL)
		return;

	Array<uint64> lldbAddrs;
	for (uint32 locIdx = 0; locIdx < bp->mLLDBBreakpoint.GetNumLocations(); locIdx++)
	{
		lldb::SBBreakpointLocation loc = bp->mLLDBBreakpoint.GetLocationAtIndex(locIdx);
		if ((!loc.IsValid()) || (!loc.IsEnabled()))
			continue;
		lldb::addr_t loadAddr = loc.GetLoadAddress();
		if (loadAddr != LLDB_INVALID_ADDRESS)
			lldbAddrs.Add((uint64)loadAddr);
	}

	Array<addr_target> boundAddrs;
	DbgSrcFile* srcFile = mDebugTarget->GetSrcFile(bp->mFilePath);
	if (srcFile != NULL)
		ShadowBindBreakpoint(mDebugTarget, srcFile, bp->mRequestedLineNum, bp->mInstrOffset, boundAddrs);
	Array<uint64> beefAddrs;
	for (auto addr : boundAddrs)
		beefAddrs.Add((uint64)addr);

	auto _Format = [](Array<uint64>& addrs)
	{
		addrs.Sort([](uint64 lhs, uint64 rhs) { return lhs < rhs; });
		String result;
		for (auto addr : addrs)
			result += StrFormat("%llx ", (unsigned long long)addr);
		return result;
	};
	String location = StrFormat("%s:%d ", GetFileName(bp->mFilePath).c_str(), bp->mRequestedLineNum);
	mShadowQueryCounts["breakpoint"]++;

	// Windows binds every run of the line and LLDB one location per function, so only addresses LLDB has and Beef lacks are mismatches
	Array<uint64> missingAddrs;
	Array<uint64> extraAddrs;
	for (auto addr : lldbAddrs)
	{
		if (!beefAddrs.Contains(addr))
			missingAddrs.Add(addr);
	}
	for (auto addr : beefAddrs)
	{
		if (!lldbAddrs.Contains(addr))
			extraAddrs.Add(addr);
	}
	if (!missingAddrs.IsEmpty())
	{
		mShadowMismatchCounts["breakpoint"]++;
		ShadowReportLine(StrFormat("DBGSHADOW breakpoint: lldb=%s beef=%s", (location + _Format(lldbAddrs)).c_str(), (location + _Format(beefAddrs)).c_str()));
	}
	else if (!extraAddrs.IsEmpty())
	{
		mShadowExtraCount++;
		ShadowReportLine(StrFormat("DBGSHADOW breakpoint-extra: lldb=%s beef=%s", (location + _Format(lldbAddrs)).c_str(), (location + _Format(beefAddrs)).c_str()));
	}
}

void LLDBDebugger::ShadowWriteReport()
{
	if (!mShadowEnabled)
		return;

	int queries = 0;
	int mismatches = 0;
	for (auto& kv : mShadowQueryCounts)
		queries += kv.mValue;
	for (auto& kv : mShadowMismatchCounts)
		mismatches += kv.mValue;
	if ((queries == 0) && (mHostUnsupportedCount == 0))
		return;

	String report;
	for (auto& line : mShadowMismatchLines)
		report += line + "\n";
	for (auto& kv : mShadowQueryCounts)
	{
		int queryMismatches = 0;
		mShadowMismatchCounts.TryGetValue(kv.mKey, &queryMismatches);
		report += StrFormat("DBGSHADOW query %s queries=%d mismatches=%d\n", kv.mKey.c_str(), kv.mValue, queryMismatches);
	}
	report += StrFormat("DBGSHADOW queries=%d mismatches=%d extra=%d unsupported=%d\n", queries, mismatches, mShadowExtraCount, mHostUnsupportedCount);
	LLDBLog("%s", report.c_str());

	if (!mShadowReportPath.IsEmpty())
	{
		FILE* fp = fopen(mShadowReportPath.c_str(), "a");
		if (fp != NULL)
		{
			fwrite(report.c_str(), 1, report.length(), fp);
			fclose(fp);
		}
	}

	mShadowQueryCounts.Clear();
	mShadowMismatchCounts.Clear();
	mShadowMismatchLines.Clear();
	mHostUnsupportedCount = 0;
	mShadowExtraCount = 0;
}

#endif
