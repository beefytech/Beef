#pragma once

#include "DebugCommon.h"
#include "BeefySysLib/util/PerfTimer.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/Dictionary.h"
#include "Debugger.h"
#include <queue>

NS_BF_BEGIN

class Debugger;
class DebugVisualizers;
class DbgMiniDump;
class NetManager;

class DbgSymSrvOptions
{
public:
	String mCacheDir;
	String mSourceServerCacheDir;
	Array<String> mSymbolServers;
	BfSymSrvFlags mFlags;

	DbgSymSrvOptions()
	{
		mFlags = BfSymSrvFlag_None;
	}
};

enum BfStepFilterKind
{
	BfStepFilterKind_Default = 0,
	BfStepFilterKind_Filtered = 1,
	BfStepFilterKind_NotFiltered = 2
};

enum BfOutputFilterFlags : uint16
	{
	BfOutputFilterFlags_None = 0x0,
	BfOutputFilterFlags_ModuleLoadMessages = 0x1,
	BfOutputFilterFlags_ModuleUnloadMessages = 0x2,
	BfOutputFilterFlags_ProcessExitMessages = 0x4,
	BfOutputFilterFlags_ThreadCreateMessages = 0x8,
	BfOutputFilterFlags_ThreadExitMessages = 0x10,
	BfOutputFilterFlags_SymbolLoadMessages = 0x20,
	BfOutputFilterFlags_ProgramOutput = 0x40 // Can't trap these easily, they're in BeefSysLib
};

class StepFilter
{
public:
	BfStepFilterKind mFilterKind;

public:
	StepFilter()
	{
		// Set global / local
		mFilterKind = BfStepFilterKind_Filtered;
	}

	bool IsFiltered(bool defaultValue)
	{
		switch (mFilterKind)
		{
		case BfStepFilterKind_Default:
			return defaultValue;
		case BfStepFilterKind_Filtered:
			return true;
		default:
			return false;
		}
	}
};

class DebugManager
{
public:
	Debugger* mDebugger32;
	Debugger* mDebugger64;

	CritSect mCritSect;
	Dictionary<String, StepFilter> mStepFilters;
	int mStepFilterVersion;
	std::deque<String> mOutMessages;

	DebugVisualizers* mDebugVisualizers;
	DwDisplayInfo mDefaultDisplayInfo;
	Dictionary<String, DwDisplayInfo> mDisplayInfos;
	Dictionary<String, String> mSourcePathRemap;
	bool mStepOverExternalFiles;

	NetManager* mNetManager;
	DbgSymSrvOptions mSymSrvOptions;

	BfOutputFilterFlags mOutputFilterFlags;

public:
	DebugManager();
	~DebugManager();

	void OutputMessage(const StringImpl& msg);
	void OutputRawMessage(const StringImpl& msg);
	void SetSourceServerCacheDir();

	void SetOutputFilterFlags(BfOutputFilterFlags outputFilterFlags);
	BfOutputFilterFlags GetOutputFilterFlags();
};

extern DebugManager* gDebugManager;

extern Debugger* gDebugger;
extern PerfManager* gDbgPerfManager;

Debugger* CreateDebugger32(DebugManager* debugManager, DbgMiniDump* miniDump);
Debugger* CreateDebugger64(DebugManager* debugManager, DbgMiniDump* miniDump);

NS_BF_END
