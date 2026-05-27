#include "GDBDebugger.h"
#include "DebugManager.h"

#include "BeefySysLib/platform/PlatformInterface.h"
#include "BeefySysLib/BFApp.h"
#include "DebugVisualizers.h"

#ifdef __linux__
#include <limits.h>
#include <unistd.h>
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include <cstdlib>
#include <cstring>
#include <cstdio>

USING_NS_BF;

static CritSect gGDBLogCritSect;

static void GDBLog(const char* fmt ...)
{
	//return;

	va_list argList;
	va_start(argList, fmt);
	String result = vformat(fmt, argList);
	va_end(argList);

	AutoCrit autoCrit(gGDBLogCritSect);
	OutputDebugStr("GDB: ");
	OutputDebugStr(result);
}

//----------------------------------------------------------------------------
// GDBMIValue
//----------------------------------------------------------------------------

GDBMIValue::GDBMIValue()
{
	mKind = Kind::String;
}

GDBMIValue::~GDBMIValue()
{
	for (auto child : mChildren)
		delete child;
}

GDBMIValue* GDBMIValue::Get(const char* key) const
{
	for (int i = 0; i < (int)mKeys.size(); i++)
	{
		if (mKeys[i] == key)
			return mChildren[i];
	}
	return NULL;
}

String GDBMIValue::GetStr(const char* key) const
{
	GDBMIValue* v = Get(key);
	if ((v != NULL) && (v->mKind == Kind::String))
		return v->mString;
	return String();
}

String* GDBMIValue::GetStrPtr(const char* key) const
{
	GDBMIValue* v = Get(key);
	if ((v != NULL) && (v->mKind == Kind::String))
		return &v->mString;
	return NULL;
}

uint64 GDBMIValue::GetHex(const char* key) const
{
	GDBMIValue* v = Get(key);
	if ((v != NULL) && (v->mKind == Kind::String))
	{
		const char* s = v->mString.c_str();
		if ((s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X')))
			return (uint64)strtoull(s + 2, NULL, 16);
		return (uint64)strtoull(s, NULL, 16);
	}
	return 0;
}

int GDBMIValue::GetInt(const char* key) const
{
	GDBMIValue* v = Get(key);
	if ((v != NULL) && (v->mKind == Kind::String))
		return atoi(v->mString.c_str());
	return 0;
}

//----------------------------------------------------------------------------
// GDBMIRecord
//----------------------------------------------------------------------------

GDBMIRecord::GDBMIRecord()
{
	mType = GDBMIRecordType::Prompt;
	mToken = -1;
	mValue = NULL;
}

GDBMIRecord::~GDBMIRecord()
{
	delete mValue;
}

//----------------------------------------------------------------------------
// MI parser
//----------------------------------------------------------------------------

// Parse a GDB/MI C-string (surrounded by double-quotes with C escapes).
// On entry, *p points at the opening '"'.  On exit, *p points past the closing '"'.
String GDBDebugger::ParseCString(const char*& p)
{
	String result;
	if (*p != '"')
		return result;
	++p;
	while ((*p != '\0') && (*p != '"'))
	{
		if (*p == '\\')
		{
			++p;
			switch (*p)
			{
			case 'n':  result += '\n'; break;
			case 't':  result += '\t'; break;
			case 'r':  result += '\r'; break;
			case '"':  result += '"';  break;
			case '\\': result += '\\'; break;
			default:   result += *p;   break;
			}
		}
		else
		{
			result += *p;
		}
		++p;
	}
	if (*p == '"')
		++p;
	return result;
}

// Parse a GDB/MI value: C-string, tuple {}, or list [].
// On entry, *p points at the first character of the value.
// On exit, *p points past the value.
GDBMIValue* GDBDebugger::ParseValue(const char*& p)
{
	GDBMIValue* val = new GDBMIValue();

	if (*p == '"')
	{
		val->mKind = GDBMIValue::Kind::String;
		val->mString = ParseCString(p);
		return val;
	}

	if (*p == '{')
	{
		val->mKind = GDBMIValue::Kind::Tuple;
		++p;
		while ((*p != '\0') && (*p != '}'))
		{
			// key=value
			const char* keyStart = p;
			while ((*p != '\0') && (*p != '=') && (*p != '}') && (*p != ','))
				++p;
			String key(keyStart, (int)(p - keyStart));
			if (*p == '=')
			{
				++p;
				GDBMIValue* child = ParseValue(p);
				val->mKeys.push_back(key);
				val->mChildren.push_back(child);
			}
			if (*p == ',')
				++p;
		}
		if (*p == '}')
			++p;
		return val;
	}

	if (*p == '[')
	{
		val->mKind = GDBMIValue::Kind::List;
		++p;
		while ((*p != '\0') && (*p != ']'))
		{
			// Could be "key=value" or just "value"
			// Peek ahead for '=' before a '"', '{', '['
			const char* peek = p;
			while ((*peek != '\0') && (*peek != '=') && (*peek != '"') &&
			       (*peek != '{') && (*peek != '[') && (*peek != ']') && (*peek != ','))
				++peek;

			if (*peek == '=')
			{
				String key(p, (int)(peek - p));
				p = peek + 1;
				GDBMIValue* child = ParseValue(p);
				val->mKeys.push_back(key);
				val->mChildren.push_back(child);
			}
			else
			{
				GDBMIValue* child = ParseValue(p);
				val->mKeys.push_back(String());
				val->mChildren.push_back(child);
			}

			if (*p == ',')
				++p;
		}
		if (*p == ']')
			++p;
		return val;
	}

	// Bare token (e.g. an unquoted number or keyword) — treat as string
	val->mKind = GDBMIValue::Kind::String;
	const char* start = p;
	while ((*p != '\0') && (*p != ',') && (*p != '}') && (*p != ']') && (*p != '\n'))
		++p;
	val->mString = String(start, (int)(p - start));
	return val;
}

// Parse a single GDB/MI output line into a GDBMIRecord.
// Returns NULL if the line cannot be parsed (e.g. empty).
GDBMIRecord* GDBDebugger::ParseMILine(const char* line)
{
	if ((line == NULL) || (line[0] == '\0'))
		return NULL;

	const char* p = line;

	GDBMIRecord* rec = new GDBMIRecord();

	// Optional numeric token
	if ((*p >= '0') && (*p <= '9'))
	{
		char* end = NULL;
		rec->mToken = (int)strtol(p, &end, 10);
		p = end;
	}

	// Record type discriminator
	char disc = *p;

	if (disc == '^')
	{
		rec->mType = GDBMIRecordType::Result;
		++p;
	}
	else if (disc == '*')
	{
		rec->mType = GDBMIRecordType::ExecAsync;
		++p;
	}
	else if (disc == '+')
	{
		rec->mType = GDBMIRecordType::StatusAsync;
		++p;
	}
	else if (disc == '=')
	{
		rec->mType = GDBMIRecordType::NotifyAsync;
		++p;
	}
	else if (disc == '~')
	{
		rec->mType = GDBMIRecordType::ConsoleStream;
		++p;
		rec->mClass = ParseCString(p);
		return rec;
	}
	else if (disc == '@')
	{
		rec->mType = GDBMIRecordType::TargetStream;
		++p;
		rec->mClass = ParseCString(p);
		return rec;
	}
	else if (disc == '&')
	{
		rec->mType = GDBMIRecordType::LogStream;
		++p;
		rec->mClass = ParseCString(p);
		return rec;
	}
	else if (strncmp(p, "(gdb)", 5) == 0)
	{
		rec->mType = GDBMIRecordType::Prompt;
		return rec;
	}
	else
	{
		// Unrecognized — treat as log stream
		rec->mType = GDBMIRecordType::LogStream;
		rec->mClass = p;
		return rec;
	}

	// Class string (up to first comma or end)
	const char* classStart = p;
	while ((*p != '\0') && (*p != ',') && (*p != '\n'))
		++p;
	rec->mClass = String(classStart, (int)(p - classStart));

	// Optional result fields: ,key=value,...
	if (*p == ',')
	{
		++p;
		rec->mValue = new GDBMIValue();
		rec->mValue->mKind = GDBMIValue::Kind::Tuple;

		while ((*p != '\0') && (*p != '\n'))
		{
			// key=value
			const char* keyStart = p;
			while ((*p != '\0') && (*p != '=') && (*p != '\n'))
				++p;
			String key(keyStart, (int)(p - keyStart));
			if (*p != '=')
				break;
			++p;
			GDBMIValue* child = ParseValue(p);
			rec->mValue->mKeys.push_back(key);
			rec->mValue->mChildren.push_back(child);
			if (*p == ',')
				++p;
		}
	}

	return rec;
}

//----------------------------------------------------------------------------
// GDBDebugger — constructor / destructor
//----------------------------------------------------------------------------

GDBDebugger::GDBDebugger(DebugManager* debugManager)
{
	mDebugManager = debugManager;
	mGDBSpawn = NULL;
	mGDBStdin = NULL;
	mGDBStdout = NULL;
	mGDBStderr = NULL;
	mLaunchThread = NULL;
	mStdoutThread = NULL;
	mNextToken = 1;
	mCallStackDirty = false;
	mActiveBreakpoint = NULL;
	mRequestedStackFrameIdx = 0;
	mBreakStackFrameIdx = 0;
	mEvalStackFrameIdx = 0;
	mProcessId = 0;
	mDidAttach = false;
	mNeedBreakpointRebind = false;
	mAutoStepRemaining = 0;
	mExceptionAddress = 0;
	mExceptionCode = 0;
	mHotSwapEnabled = false;
	mOpenFileFlags = DbgOpenFileFlag_None;
	mLaunchMode = GDBLaunchMode_Local;
	mGDBReady = false;
	mRunning = false;
	mNeedsExecRun = false;	
}

GDBDebugger::~GDBDebugger()
{
	WaitForLaunchThread();
	WaitForStdoutThread();
	for (auto bp : mBreakpoints)
		delete bp;
}

//----------------------------------------------------------------------------
// Output helpers
//----------------------------------------------------------------------------

void GDBDebugger::OutputMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back("msg " + msg);
}

void GDBDebugger::OutputRawMessage(const StringImpl& msg)
{
	if (this == NULL)
		return;
	AutoCrit autoCrit(mDebugManager->mCritSect);
	mDebugManager->mOutMessages.push_back(msg);
}

//----------------------------------------------------------------------------
// MI I/O
//----------------------------------------------------------------------------

void GDBDebugger::SendRaw(const char* line)
{
	if (mGDBStdin == NULL)
		return;
	GDBLog("-> %s\n", line);
	BfpFile_Write(mGDBStdin, line, (intptr)strlen(line), -1, NULL);
	BfpFile_Write(mGDBStdin, "\n", 1, -1, NULL);
}

int GDBDebugger::SendCommand(const char* cmd)
{
	int token = mNextToken++;
	String line = StrFormat("%d%s", token, cmd);
	SendRaw(line.c_str());
	return token;
}

GDBMIRecord* GDBDebugger::WaitForToken(int token, int timeoutMS)
{
	int elapsed = 0;
	const int sleepInterval = 1;

	while ((timeoutMS < 0) || (elapsed < timeoutMS))
	{
		{
			AutoCrit autoCrit(mRecordCritSect);
			for (int i = 0; i < (int)mIncomingRecords.size(); i++)
			{
				GDBMIRecord* rec = mIncomingRecords[i];
				if ((rec->mToken == token) && (rec->mType == GDBMIRecordType::Result))
				{
					mIncomingRecords.erase(mIncomingRecords.begin() + i);
					return rec;
				}
			}
		}

		// Check if GDB process died
		if (mGDBSpawn == NULL)
			return NULL;

		if (mStdoutThread != NULL)
		{
			// Read thread exited
			if (BfpThread_WaitFor(mStdoutThread, 0))
				return NULL;
		}

		BfpThread_Sleep(sleepInterval);
		elapsed += sleepInterval;
	}

	return NULL;
}

GDBMIRecord* GDBDebugger::SendSync(const char* cmd, int timeoutMS)
{
	int token = SendCommand(cmd);
	return WaitForToken(token, timeoutMS);
}

bool GDBDebugger::SendSyncNoResult(const char* cmd, int timeoutMS)
{
	auto rec = SendSync(cmd, timeoutMS);
	if (rec == NULL)
		return false;
	delete rec;
	return true;
}

//----------------------------------------------------------------------------
// Stdout reader thread
//----------------------------------------------------------------------------

void BFP_CALLTYPE GDBDebugger::StdoutThreadProc(void* param)
{
	((GDBDebugger*)param)->ReadStdout();
}

void GDBDebugger::ReadStdout()
{
	char buf[4096];
	String pending;

	while (true)
	{
		BfpFileResult result = BfpFileResult_Ok;
		intptr bytesRead = BfpFile_Read(mGDBStdout, buf, sizeof(buf) - 1, -1, &result);
		if (bytesRead <= 0)
			break;

		buf[bytesRead] = '\0';
		pending += String(buf, (int)bytesRead);

		// Process complete lines
		while (true)
		{
			int nlPos = -1;
			for (int i = 0; i < (int)pending.length(); i++)
			{
				if (pending[i] == '\n')
				{
					nlPos = i;
					break;
				}
			}
			if (nlPos < 0)
				break;

			String line = pending.Substring(0, nlPos);
			// Strip trailing '\r'
			if ((!line.IsEmpty()) && (line[line.length() - 1] == '\r'))
				line = line.Substring(0, (int)line.length() - 1);

			pending = pending.Substring(nlPos + 1);

			GDBLog("<- %s\n", line.c_str());

			if (line.IsEmpty())
				continue;

			GDBMIRecord* rec = ParseMILine(line.c_str());
			if (rec != NULL)
			{
				AutoCrit autoCrit(mRecordCritSect);
				mIncomingRecords.push_back(rec);
			}
		}
	}	
}

//----------------------------------------------------------------------------
// Record processing
//----------------------------------------------------------------------------

void GDBDebugger::ProcessRecord(GDBMIRecord* rec)
{
	if (rec->mType == GDBMIRecordType::ExecAsync)
	{
		if (rec->mClass == "stopped")
		{
			HandleStoppedRecord(rec);
		}
		else if (rec->mClass == "running")
		{
			// Inferior resumed — nothing to do here; mRunState was set by the
			// execution-control method that sent the -exec-* command.
		}
	}
	else if (rec->mType == GDBMIRecordType::ConsoleStream)
	{
		// Forward target output to the IDE output panel
		if (!rec->mClass.IsEmpty())
			OutputMessage(rec->mClass);
	}
	else if (rec->mType == GDBMIRecordType::TargetStream)
	{
		if (!rec->mClass.IsEmpty())
			OutputMessage(rec->mClass);
	}
	else if (rec->mType == GDBMIRecordType::LogStream)
	{		
		if (rec->mClass.StartsWith("Python Exception"))
			OutputRawMessage("errorsoft " + Trim(rec->mClass));
		else
			OutputMessage(rec->mClass + "\n");
	}
}

// True if the signal number is a fatal/crash signal
static bool GDBIsCrashSignal(uint32 signo)
{
	// SIGILL=4, SIGABRT=6, SIGBUS=7(Linux)/10(macOS), SIGFPE=8, SIGSEGV=11
	return ((signo == 4) || (signo == 6) || (signo == 7) ||
	        (signo == 8) || (signo == 10) || (signo == 11));
}

void GDBDebugger::CheckBreakpoints(bool force)
{
	for (auto bp : mBreakpoints)
		DoCheckBreakpoint(bp, force);
}

void GDBDebugger::HandleStoppedRecord(GDBMIRecord* rec)
{
	AutoCrit autoCrit(mDebugManager->mCritSect);

	if (mRunState == RunState_Terminated)
		return;

	String reason;
	if (rec->mValue != NULL)
		reason = rec->mValue->GetStr("reason");

	GDBLog("HandleStoppedRecord reason='%s'\n", reason.c_str());

	// Rebind breakpoints on the first stop after launch (stop-at-entry equivalent)
	if (mNeedBreakpointRebind)
	{
		mNeedBreakpointRebind = false;
		CheckBreakpoints(false);
	}

	ClearCallStack();
	mRequestedStackFrameIdx = 0;
	mBreakStackFrameIdx = 0;
	mEvalStackFrameIdx = 0;

	if ((reason == "exited") || (reason == "exited-normally") || (reason == "exited-signalled"))
	{
		mActiveBreakpoint = NULL;
		mProcessId = 0;
		mRunState = RunState_Terminated;
		return;
	}

	if ((reason == "breakpoint-hit") || (reason == "watchpoint-trigger"))
	{
		// Auto-step sequence: if a step just completed and we have more to do
		if (mAutoStepRemaining > 0)
		{
			// A breakpoint hit cancels the auto-step sequence
			mAutoStepRemaining = 0;
		}

		// Identify which of our breakpoints was hit
		mActiveBreakpoint = NULL;
		if (rec->mValue != NULL)
		{
			int bpNum = rec->mValue->GetInt("bkptno");
			if (bpNum > 0)
			{
				GDBBreakpoint* bp = NULL;
				mBreakpointNumMap.TryGetValue(bpNum, &bp);
				mActiveBreakpoint = bp;

				// Record resolved address from the frame
				if ((bp != NULL) && (bp->mResolvedAddr == 0) && (rec->mValue != NULL))
				{
					GDBMIValue* frame = rec->mValue->Get("frame");
					if (frame != NULL)
					{
						uint64 addr = frame->GetHex("addr");
						if (addr != 0)
						{
							bp->mResolvedAddr = (uintptr)addr;
							mBreakpointAddrMap.ForceAdd(bp->mResolvedAddr, bp);
						}
					}
				}
			}
		}

		mRunState = RunState_Breakpoint;
		return;
	}

	if (reason == "end-stepping-range")
	{
		// A step completed — handle auto-step sequence
		if (mAutoStepRemaining > 0)
		{
			if (mAutoStepRemaining == 2)
			{
				mAutoStepRemaining--;
				mRunState = RunState_Running;
				SendCommand("-exec-step");
				return;
			}
			else  // mAutoStepRemaining == 1
			{
				mAutoStepRemaining = 0;
				mRunState = RunState_Running;
				SendCommand("-exec-next");
				return;
			}
		}

		mActiveBreakpoint = NULL;
		mRunState = RunState_Paused;
		return;
	}

	if ((reason == "signal-received") || (reason == ""))
	{
		// Check if it's a crash signal
		uint32 signo = 0;
		String sigName;
		if (rec->mValue != NULL)
		{
			sigName = rec->mValue->GetStr("signal-name");
			String sigStr = rec->mValue->GetStr("signal-meaning");
			// Map common signal names to numbers
			if (sigName == "SIGSEGV")      signo = 11;
			else if (sigName == "SIGABRT") signo = 6;
			else if (sigName == "SIGFPE")  signo = 8;
			else if (sigName == "SIGILL")  signo = 4;
			else if (sigName == "SIGBUS")  signo = 7;
			else if (sigName == "SIGKILL") signo = 9;
		}

		if (GDBIsCrashSignal(signo))
		{
			mAutoStepRemaining = 0;
			mExceptionCode = signo;
			mExceptionAddress = 0;
			if (rec->mValue != NULL)
			{
				GDBMIValue* frame = rec->mValue->Get("frame");
				if (frame != NULL)
					mExceptionAddress = frame->GetHex("addr");
			}
			if (!sigName.IsEmpty())
				mExceptionDescription = sigName + " in process " + StrFormat("%d", mProcessId);
			else
				mExceptionDescription = StrFormat("Signal %d in process %d", (int)signo, mProcessId);
			mActiveBreakpoint = NULL;
			mRunState = RunState_Exception;
			return;
		}

		mAutoStepRemaining = 0;
		mActiveBreakpoint = NULL;
		mRunState = RunState_Paused;
		return;
	}

	// Any other stop (e.g. "function-finished", "location-reached", etc.)
	mAutoStepRemaining = 0;
	mActiveBreakpoint = NULL;
	mRunState = RunState_Paused;
}

//----------------------------------------------------------------------------
// Launch
//----------------------------------------------------------------------------


void GDBDebugger::OpenFile(const StringImpl& launchPath, const StringImpl& targetPath, const StringImpl& args, const StringImpl& workingDir, const Array<uint8>& envBlock, bool hotSwapEnabled, DbgOpenFileFlags openFileFlags)
{
	GDBLog("OpenFile\n");

	// Parse optional "path@location" syntax.
	// Recognised location tags:
	//   gdb_wsl          — run "wsl gdb"; paths converted to WSL /mnt/ form
	//   gdb_ssh:server   — run "ssh <server> gdb"; path is already a remote path
	//   gdb:host:port    — run GDB locally; connect to gdbserver on <host:port>
	mLaunchMode = GDBLaunchMode_Local;
	mGDBServerHost = "";

	const char* rawPath = launchPath.c_str();
	const char* atSign = strchr(rawPath, '@');
	if (atSign != NULL)
	{
		const char* location = atSign + 1;

		if (strcmp(location, "gdb_wsl") == 0)
		{
			mLaunchMode = GDBLaunchMode_WSL;
		}
		else if (strncmp(location, "gdb_ssh:", 8) == 0)
		{
			mLaunchMode = GDBLaunchMode_SSH;
			mGDBServerHost = location + 8;
		}
		else if (strncmp(location, "gdb:", 4) == 0)
		{
			mLaunchMode = GDBLaunchMode_GDBServer;
			mGDBServerHost = location + 4;
		}
		// else: unknown tag — ignore and treat the whole string as the path

		if (mLaunchMode != GDBLaunchMode_Local)
			mLaunchPath = String(rawPath, (int)(atSign - rawPath));
		else
			mLaunchPath = launchPath;
	}
	else
	{
		mLaunchPath = launchPath;
	}

	mLaunchArgs = args;
	mWorkingDir = workingDir;
	mEnvBlock = envBlock;
	mHotSwapEnabled = hotSwapEnabled;
	mOpenFileFlags = openFileFlags;
}

void GDBDebugger::DoLaunch()
{
	GDBLog("DoLaunch\n");

	// --- Choose GDB executable and arg prefix based on launch mode ---
	//
	// Local:     gdb  --interpreter=mi2 --nx
	// WSL:       wsl  gdb --interpreter=mi2 --nx
	// SSH:       ssh  <server> gdb --interpreter=mi2 --nx
	// GDBServer: gdb  --interpreter=mi2 --nx   (then connect via -target-select)
	//
	// In all cases we redirect stdin/stdout/stderr so we can drive GDB over MI.

	String gdbExe;
	String gdbBaseArgs = "gdb --interpreter=mi2 --nx";

	switch (mLaunchMode)
	{
	case GDBLaunchMode_Local:
	case GDBLaunchMode_GDBServer:
		gdbExe = "gdb";
		gdbBaseArgs = "--interpreter=mi2 --nx";
		break;
	case GDBLaunchMode_WSL:
		// "wsl" is the executable; "gdb ..." is the command-line inside WSL
		gdbExe = "wsl";
		gdbBaseArgs = "gdb --interpreter=mi2 --nx";
		break;
	case GDBLaunchMode_SSH:
		// ssh <server> gdb --interpreter=mi2 --nx
		gdbExe = "ssh";
		gdbBaseArgs = mGDBServerHost + " gdb --interpreter=mi2 --nx";
		break;
	}

	// For WSL mode, convert both the executable path and working directory to
	// WSL /mnt/... paths before passing them to GDB inside WSL.
	String launchPathForGDB = mLaunchPath;
	String workingDirForGDB = mWorkingDir;
	if (mLaunchMode == GDBLaunchMode_WSL)
	{
		launchPathForGDB = ConvertToWSLPath(mLaunchPath);
		if (!mWorkingDir.IsEmpty())
			workingDirForGDB = ConvertToWSLPath(mWorkingDir);
	}

	BfpSpawnResult spawnResult = BfpSpawnResult_Ok;
	BfpSpawn* spawn = BfpSpawn_Create(
		gdbExe.c_str(),
		gdbBaseArgs.c_str(),
		mWorkingDir.IsEmpty() ? NULL : mWorkingDir.c_str(),
		NULL,
		(BfpSpawnFlags)(BfpSpawnFlag_RedirectStdInput | BfpSpawnFlag_RedirectStdOutput |
		                BfpSpawnFlag_RedirectStdError | BfpSpawnFlag_NoWindow),
		&spawnResult);

	if ((spawn == NULL) || (spawnResult != BfpSpawnResult_Ok))
	{
		OutputMessage("GDB: Failed to spawn " + gdbExe + "\n");
		AutoCrit autoCrit(mDebugManager->mCritSect);
		mRunState = RunState_Terminated;
		return;
	}

	{
		AutoCrit autoCrit(mDebugManager->mCritSect);
		mGDBSpawn = spawn;
		mProcessId = BfpSpawn_GetProcessId(spawn);
	}

	// Get stdin/stdout handles
	BfpFile* gdbStdin = NULL;
	BfpFile* gdbStdout = NULL;
	BfpFile* gdbStderr = NULL;
	BfpSpawn_GetStdHandles(spawn, &gdbStdin, &gdbStdout, &gdbStderr);

	{
		AutoCrit autoCrit(mDebugManager->mCritSect);
		mGDBStdin = gdbStdin;
		mGDBStdout = gdbStdout;
		mGDBStderr = gdbStderr;
	}

	// Start the stdout reader thread
	mStdoutThread = BfpThread_Create(StdoutThreadProc, (void*)this, 128 * 1024, BfpThreadCreateFlag_StackSizeReserve);

	// Wait for the initial "(gdb)" prompt (up to 10 seconds)
	{
		int waited = 0;
		bool gotPrompt = false;
		while (waited < 10000)
		{
			{
				AutoCrit autoCrit(mRecordCritSect);
				for (int i = 0; i < (int)mIncomingRecords.size(); i++)
				{
					if (mIncomingRecords[i]->mType == GDBMIRecordType::Prompt)
					{
						gotPrompt = true;
						delete mIncomingRecords[i];
						mIncomingRecords.erase(mIncomingRecords.begin() + i);
						break;
					}
				}
			}
			if (gotPrompt)
				break;
			BfpThread_Sleep(5);
			waited += 5;
		}

		if (!gotPrompt)
		{
			OutputMessage("GDB: Timed out waiting for GDB prompt\n");
			AutoCrit autoCrit(mDebugManager->mCritSect);
			mRunState = RunState_Terminated;
			return;
		}
	}

	// Set the inferior executable (use WSL-converted path for WSL mode)
	{
		String cmd = StrFormat("-file-exec-and-symbols \"%s\"", launchPathForGDB.c_str());
		GDBMIRecord* rec = SendSync(cmd.c_str(), 10000);
		if (rec != NULL)
		{
			if (rec->mClass != "done")
			{
				String errMsg = "GDB: Failed to load executable";
				if ((rec->mValue != NULL) && (!rec->mValue->GetStr("msg").IsEmpty()))
				{
					errMsg += ": ";
					errMsg += rec->mValue->GetStr("msg");
				}
				errMsg += "\n";
				OutputMessage(errMsg);
				delete rec;
				AutoCrit autoCrit(mDebugManager->mCritSect);
				mRunState = RunState_Terminated;
				return;
			}
			delete rec;
		}
	}

	/*{
		String cmd = "-gdb-set mi-async on";
		GDBMIRecord* rec = SendSync(cmd.c_str());
		delete rec;
	}*/

	SendSyncNoResult("-interpreter-exec console \"set pagination off\"");
	SendSyncNoResult("-interpreter-exec console \"set debuginfod enabled on\"");
	SendSyncNoResult("-gdb-set auto-solib-add on");	
	SendSyncNoResult("-enable-pretty-printing");

	// Set working directory if specified (WSL and SSH use the converted/remote path)
	if (!workingDirForGDB.IsEmpty())
	{
		String cmd = StrFormat("-environment-cd \"%s\"", workingDirForGDB.c_str());
		GDBMIRecord* rec = SendSync(cmd.c_str());
		delete rec;
	}

	// Set program arguments (not applicable for gdbserver — the inferior is already
	// started on the remote; arguments were passed to gdbserver when it was launched)
	if ((!mLaunchArgs.IsEmpty()) && (mLaunchMode != GDBLaunchMode_GDBServer))
	{
		String cmd = "-exec-arguments " + mLaunchArgs;
		GDBMIRecord* rec = SendSync(cmd.c_str());
		delete rec;
	}

	// Set environment variables from env block
	if (!mEnvBlock.IsEmpty())
	{
		const uint8* p = &mEnvBlock.front();
		const uint8* end = p + mEnvBlock.size();
		while ((p < end) && (*p != '\0'))
		{
			const uint8* start = p;
			while ((p < end) && (*p != '\0'))
				++p;
			String envEntry((const char*)start, (int)(p - start));
			if (p < end)
				++p;

			// envEntry is "KEY=VALUE"
			int eqPos = -1;
			for (int i = 0; i < (int)envEntry.length(); i++)
			{
				if (envEntry[i] == '=')
				{
					eqPos = i;
					break;
				}
			}
			if (eqPos >= 0)
			{
				//TODO: Put back
// 				String cmd = StrFormat("-gdb-set environment %s", envEntry.c_str());
// 				GDBMIRecord* rec = SendSync(cmd.c_str());
// 				delete rec;
			}
		}
	}

	if (mLaunchMode == GDBLaunchMode_GDBServer)
	{
		// ---- GDBServer mode ----
		// The inferior is already running on the remote machine under gdbserver.
		// We connect to it with -target-select extended-remote; the process will
		// already be stopped at its entry point.  Breakpoint rebind happens when
		// HandleStoppedRecord fires for that first stop.
		String connectCmd = StrFormat("-target-select extended-remote %s", mGDBServerHost.c_str());
		GDBMIRecord* rec = SendSync(connectCmd.c_str(), 15000);
		if (rec != NULL)
		{
			// GDB replies with ^connected on success (not ^done)
			if ((rec->mClass != "connected") && (rec->mClass != "done"))
			{
				String errMsg = "GDB: Failed to connect to gdbserver at " + mGDBServerHost;
				if ((rec->mValue != NULL) && (!rec->mValue->GetStr("msg").IsEmpty()))
				{
					errMsg += ": ";
					errMsg += rec->mValue->GetStr("msg");
				}
				errMsg += "\n";
				OutputMessage(errMsg);
				delete rec;
				AutoCrit autoCrit(mDebugManager->mCritSect);
				mRunState = RunState_Terminated;
				return;
			}
			delete rec;
		}
	}
	else
	{
		// Load pretty printers
		for (auto checkDir : mDebugManager->mDebugVisualizers->mCheckDirectories)
		{
			String gdbDir = checkDir + "gdb/";
			// BfpFindFileData_FindFirstFile takes a glob pattern
			String gdbDirPattern = gdbDir + "*.py";

			BfpFileResult findResult = BfpFileResult_Ok;
			BfpFindFileData* findData = BfpFindFileData_FindFirstFile(gdbDirPattern.c_str(), BfpFindFileFlag_Files, &findResult);
			if (findData != NULL)
			{
				do
				{
					char fileNameBuf[MAX_PATH];
					int fileNameSize = MAX_PATH;
					BfpFindFileData_GetFileName(findData, fileNameBuf, &fileNameSize, NULL);

					String pyFilePath = gdbDir + fileNameBuf;
					String pyFilePathForGDB = pyFilePath;
					if (mLaunchMode == GDBLaunchMode_WSL)
						pyFilePathForGDB = ConvertToWSLPath(pyFilePath);

					// Replace backslashes with forward slashes for GDB's Python interpreter
					for (int i = 0; i < (int)pyFilePathForGDB.length(); i++)
					{
						if (pyFilePathForGDB[i] == '\\')
							pyFilePathForGDB[i] = '/';
					}

					String sourceCmd = StrFormat("-interpreter-exec console \"source %s\"", pyFilePathForGDB.c_str());
					GDBMIRecord* pyRec = SendSync(sourceCmd.c_str(), 5000);
					if (pyRec != NULL)
					{
						if (pyRec->mClass == "error")
						{
							String errMsg = "GDB: Failed to load pretty-printer: " + pyFilePath;
							if ((pyRec->mValue != NULL) && (!pyRec->mValue->GetStr("msg").IsEmpty()))
							{
								errMsg += ": ";
								errMsg += pyRec->mValue->GetStr("msg");
							}
							errMsg += "\n";
							OutputMessage(errMsg);
						}
						delete pyRec;
					}
				} while (BfpFindFileData_FindNextFile(findData));

				BfpFindFileData_Release(findData);
			}
		}

		AutoCrit autoCrit(mDebugManager->mCritSect);
		CheckBreakpoints(true);		
		mNeedsExecRun = true;
	}

	AutoCrit autoCrit(mDebugManager->mCritSect);
	mGDBReady = true;
	mNeedBreakpointRebind = true;
	mRunning = true;

	if (mNeedsExecRun)
		mRunState = RunState_Paused;
	else
		mRunState = RunState_Running;
}

void BFP_CALLTYPE GDBDebugger::LaunchThreadProc(void* param)
{
	((GDBDebugger*)param)->DoLaunch();
}

void GDBDebugger::WaitForLaunchThread()
{
	if (mLaunchThread != NULL)
	{
		BfpThread_WaitFor(mLaunchThread, -1);
		BfpThread_Release(mLaunchThread);
		mLaunchThread = NULL;
	}
}

void GDBDebugger::WaitForStdoutThread()
{
	if (mStdoutThread != NULL)
	{
		BfpThread_WaitFor(mStdoutThread, 2000);
		BfpThread_Release(mStdoutThread);
		mStdoutThread = NULL;
	}
}

bool GDBDebugger::Attach(int processId, BfDbgAttachFlags attachFlags)
{
	mDidAttach = true;
	return false;
}

void GDBDebugger::GetStdHandles(BfpFile** outStdIn, BfpFile** outStdOut, BfpFile** outStdErr)
{
}

void GDBDebugger::Run()
{
	if (!mLaunchPath.IsEmpty())
		mLaunchThread = BfpThread_Create(LaunchThreadProc, (void*)this, 128 * 1024, BfpThreadCreateFlag_StackSizeReserve);
}

//----------------------------------------------------------------------------
// Update — drain and process async records each IDE tick
//----------------------------------------------------------------------------

void GDBDebugger::Update()
{
	if (mGDBSpawn == NULL)
		return;
	if ((mRunState == RunState_NotStarted) || (mRunState == RunState_Terminating) || (mRunState == RunState_Terminated))
		return;

	if (mStdoutThread != NULL)
	{
		if (BfpThread_WaitFor(mStdoutThread, 0))
		{
			OutputRawMessage("error GDB exited unexpectedly");
			mRunState = RunState_Terminated;
		}
	}

	// Drain all queued records (non-blocking, already parsed by reader thread)
	while (true)
	{
		GDBMIRecord* rec = NULL;
		{
			AutoCrit autoCrit(mRecordCritSect);
			for (int i = 0; i < (int)mIncomingRecords.size(); i++)
			{
				GDBMIRecord* r = mIncomingRecords[i];
				// Only process async records here; result records are consumed by WaitForToken
				if ((r->mToken == -1) || (r->mType != GDBMIRecordType::Result))
				{
					rec = r;
					mIncomingRecords.erase(mIncomingRecords.begin() + i);
					break;
				}
			}
		}

		if (rec == NULL)
			break;

		ProcessRecord(rec);
		delete rec;
	}
}

//----------------------------------------------------------------------------
// Execution control
//----------------------------------------------------------------------------

void GDBDebugger::ContinueDebugEvent()
{
	if (mGDBSpawn == NULL)
		return;
	if ((mRunState != RunState_Paused) && (mRunState != RunState_Breakpoint) && (mRunState != RunState_Exception))
		return;

	GDBLog("ContinueDebugEvent\n");

	mAutoStepRemaining = 0;
	ClearCallStack();
	mActiveBreakpoint = NULL;
	mRunState = RunState_Running;

	if (mNeedsExecRun)
	{
		// Launch the inferior
		GDBMIRecord* rec = SendSync("-exec-run");
		if (rec != NULL)
		{
			if ((rec->mClass != "running") && (rec->mClass != "done"))
			{
				String errMsg = "GDB: Failed to start inferior";
				if ((rec->mValue != NULL) && (!rec->mValue->GetStr("msg").IsEmpty()))
				{
					errMsg += ": ";
					errMsg += rec->mValue->GetStr("msg");
				}
				errMsg += "\n";
				OutputMessage(errMsg);
				delete rec;
				AutoCrit autoCrit(mDebugManager->mCritSect);
				mRunState = RunState_Terminated;
				return;
			}
			delete rec;
		}

		mNeedsExecRun = false;
		return;
	}

	SendCommand("-exec-continue");
}

bool GDBDebugger::TryRunContinue()
{
	return ((mRunState == RunState_Paused) || (mRunState == RunState_Breakpoint));
}

void GDBDebugger::BreakAll()
{
	if ((mGDBSpawn != NULL) && (mRunState == RunState_Running))
	{
		// Send interrupt via -exec-interrupt
		SendRaw("-exec-interrupt");
	}
}

void GDBDebugger::ForegroundTarget(int altProcessId)
{
}

//----------------------------------------------------------------------------
// BeefStartProgram detection helper
//----------------------------------------------------------------------------

static bool GDBIsAtBeefStartProgram(const Array<GDBFrame>& callStack)
{
	if (callStack.IsEmpty())
		return false;
	const char* fn = callStack[0].mFunction.c_str();
	return (fn != NULL) && (strstr(fn, "BeefStartProgram") != NULL);
}

void GDBDebugger::StepInto(bool inAssembly)
{
	if (mGDBSpawn == NULL)
		return;

	mAutoStepRemaining = 0;

	if ((!inAssembly) && GDBIsAtBeefStartProgram(mCallStack))
		mAutoStepRemaining = 1;

	ClearCallStack();
	mRunState = RunState_Running;
	if (inAssembly)
		SendCommand("-exec-step-instruction");
	else
		SendCommand("-exec-step");
}

void GDBDebugger::StepIntoSpecific(intptr addr)
{
}

void GDBDebugger::StepOver(bool inAssembly)
{
	if (mGDBSpawn == NULL)
		return;

	mAutoStepRemaining = 0;
	ClearCallStack();
	mRunState = RunState_Running;
	if (inAssembly)
		SendCommand("-exec-next-instruction");
	else
		SendCommand("-exec-next");
}

void GDBDebugger::StepOut(bool inAssembly)
{
	if (mGDBSpawn == NULL)
		return;

	mAutoStepRemaining = 0;
	ClearCallStack();
	mRunState = RunState_Running;
	SendCommand("-exec-finish");
}

void GDBDebugger::SetNextStatement(bool inAssembly, const StringImpl& fileName, int64 lineNumOrAsmAddr, int wantColumn)
{
}

//----------------------------------------------------------------------------
// Breakpoint helpers
//----------------------------------------------------------------------------

// Translate "bf::Namespace::Method" → "Namespace.Method" for Beef frames
String GDBDebugger::FixBeefName(const char* name)
{
	if ((name == NULL) || (name[0] == '\0'))
		return String();

	const char* src = name;

	// Strip "bf::" prefix
	if ((src[0] == 'b') && (src[1] == 'f') && (src[2] == ':') && (src[3] == ':'))
		src += 4;

	// Replace "::" with "."
	String result;
	while (*src != '\0')
	{
		if ((src[0] == ':') && (src[1] == ':'))
		{
			result += '.';
			src += 2;
		}
		else
		{
			result += *src;
			++src;
		}
	}
	return result;
}

GDBMIRecord* GDBDebugger::InsertBreakpointByLocation(const char* file, int lineNum1Based)
{
	String wslPath;
	if (mLaunchMode == GDBLaunchMode_WSL)
	{
		wslPath = ConvertToWSLPath(file);
		file = wslPath.c_str();
	}

	// -break-insert "file:line"
	//String cmd = StrFormat("-break-insert -f \"%s:%d\"", file, lineNum1Based);
	String cmd = StrFormat("-break-insert -f %s:%d", file, lineNum1Based);
	return SendSync(cmd.c_str());
}

GDBMIRecord* GDBDebugger::InsertBreakpointByName(const char* sym, bool mainModuleOnly)
{	
	String cmd = StrFormat("-break-insert \"%s\"", sym);
	return SendSync(cmd.c_str());
}

void GDBDebugger::BindBreakpointFromResult(GDBBreakpoint* bp, GDBMIRecord* rec)
{
	if ((rec == NULL) || (rec->mClass != "done") || (rec->mValue == NULL))
		return;

	GDBMIValue* bkpt = rec->mValue->Get("bkpt");
	if (bkpt == NULL)
		return;

	int bpNum = bkpt->GetInt("number");
	if (bpNum <= 0)
		return;

	bp->mGDBBpNum = bpNum;
	mBreakpointNumMap.ForceAdd(bpNum, bp);

	// Try to get the resolved address
	uint64 addr = bkpt->GetHex("addr");
	if (addr != 0)
	{
		bp->mResolvedAddr = (uintptr)addr;
		mBreakpointAddrMap.ForceAdd(bp->mResolvedAddr, bp);
	}
}

void GDBDebugger::DeleteGDBBreakpoint(GDBBreakpoint* bp)
{
	if (bp->mGDBBpNum < 0)
		return;

	// Remove from ID map
	auto idItr = mBreakpointNumMap.Find(bp->mGDBBpNum);
	if ((idItr != mBreakpointNumMap.end()) && (idItr->mValue == bp))
		mBreakpointNumMap.Remove(idItr);

	// Send -break-delete to GDB
	String cmd = StrFormat("-break-delete %d", bp->mGDBBpNum);
	GDBMIRecord* rec = SendSync(cmd.c_str());
	delete rec;

	bp->mGDBBpNum = -1;
}

//----------------------------------------------------------------------------
// Breakpoints
//----------------------------------------------------------------------------

Breakpoint* GDBDebugger::CreateBreakpoint(const StringImpl& fileName, int lineNum, int wantColumn, int instrOffset)
{
	GDBBreakpoint* bp = new GDBBreakpoint();
	bp->mFilePath = fileName;
	bp->mRequestedLineNum = lineNum;
	bp->mLineNum = lineNum;
	bp->mColumn = wantColumn;
	bp->mInstrOffset = instrOffset;
	mBreakpoints.push_back(bp);

	if (mGDBReady)
	{
		GDBMIRecord* rec = InsertBreakpointByLocation(fileName.c_str(), lineNum + 1);
		BindBreakpointFromResult(bp, rec);
		delete rec;
	}

	return bp;
}

Breakpoint* GDBDebugger::CreateMemoryBreakpoint(intptr addr, int byteCount)
{
	return NULL;
}

Breakpoint* GDBDebugger::CreateSymbolBreakpoint(const StringImpl& symbolName)
{
	GDBLog("CreateSymbolBreakpoint '%s'\n", symbolName.c_str());

	GDBBreakpoint* bp = new GDBBreakpoint();
	bp->mSymbolName = symbolName;
	mBreakpoints.push_back(bp);

	if (mGDBReady)
	{
		const char* sym = symbolName.c_str();
		bool mainModuleOnly = (sym[0] == '-');
		if (mainModuleOnly)
			sym++;

		GDBMIRecord* rec = InsertBreakpointByName(sym, mainModuleOnly);
		BindBreakpointFromResult(bp, rec);
		delete rec;
	}

	return bp;
}

Breakpoint* GDBDebugger::CreateAddressBreakpoint(intptr address)
{
	GDBBreakpoint* bp = new GDBBreakpoint();
	bp->mResolvedAddr = (uintptr)address;
	mBreakpoints.push_back(bp);

	if (mGDBReady)
	{
		String cmd = StrFormat("-break-insert *0x%llX", (uint64)address);
		GDBMIRecord* rec = SendSync(cmd.c_str());
		BindBreakpointFromResult(bp, rec);
		delete rec;
	}

	return bp;
}

void GDBDebugger::DoCheckBreakpoint(Breakpoint* checkBreakpoint, bool force)
{
	GDBBreakpoint* bp = (GDBBreakpoint*)checkBreakpoint;

	if ((!mGDBReady) && (!force))
		return;

	// If not yet bound, create the physical breakpoint
	if (bp->mGDBBpNum < 0)
	{
		GDBMIRecord* rec = NULL;

		if ((!bp->mFilePath.IsEmpty()) && (bp->mRequestedLineNum >= 0))
		{
			rec = InsertBreakpointByLocation(bp->mFilePath.c_str(), bp->mRequestedLineNum + 1);
		}
		else if (!bp->mSymbolName.IsEmpty())
		{
			const char* sym = bp->mSymbolName.c_str();
			bool mainModuleOnly = (sym[0] == '-');
			if (mainModuleOnly)
				sym++;
			rec = InsertBreakpointByName(sym, mainModuleOnly);
		}
		else if (bp->mResolvedAddr != 0)
		{
			String cmd = StrFormat("-break-insert *0x%llX", (uint64)bp->mResolvedAddr);
			rec = SendSync(cmd.c_str());
		}

		if (rec != NULL)
		{
			BindBreakpointFromResult(bp, rec);
			delete rec;
		}
	}
}

void GDBDebugger::CheckBreakpoint(Breakpoint* checkBreakpoint)
{
	DoCheckBreakpoint(checkBreakpoint, false);
}

void GDBDebugger::HotBindBreakpoint(Breakpoint* wdBreakpoint, int lineNum, int hotIdx)
{
}

void GDBDebugger::DeleteBreakpoint(Breakpoint* breakpoint)
{
	GDBBreakpoint* bp = (GDBBreakpoint*)breakpoint;

	if (bp == mActiveBreakpoint)
		mActiveBreakpoint = NULL;

	if (bp->mResolvedAddr != 0)
	{
		auto addrItr = mBreakpointAddrMap.Find(bp->mResolvedAddr);
		if ((addrItr != mBreakpointAddrMap.end()) && (addrItr->mValue == bp))
			mBreakpointAddrMap.Remove(addrItr);
	}

	if (mGDBReady)
		DeleteGDBBreakpoint(bp);

	if (!bp->mIsLinkedSibling)
		mBreakpoints.Remove(bp);

	delete bp;
}

void GDBDebugger::DetachBreakpoint(Breakpoint* breakpoint)
{
	GDBBreakpoint* bp = (GDBBreakpoint*)breakpoint;

	if (mGDBReady && (bp->mGDBBpNum >= 0))
	{
		// Disable rather than delete
		String cmd = StrFormat("-break-disable %d", bp->mGDBBpNum);
		GDBMIRecord* rec = SendSync(cmd.c_str());
		delete rec;
	}

	if (bp->mResolvedAddr != 0)
	{
		auto addrItr = mBreakpointAddrMap.Find(bp->mResolvedAddr);
		if ((addrItr != mBreakpointAddrMap.end()) && (addrItr->mValue == bp))
			mBreakpointAddrMap.Remove(addrItr);
		bp->mResolvedAddr = 0;
	}

	bp->mLineNum = bp->mRequestedLineNum;
}

void GDBDebugger::MoveBreakpoint(Breakpoint* breakpoint, int lineNum, int wantColumn, bool rebindNow)
{
	GDBBreakpoint* bp = (GDBBreakpoint*)breakpoint;

	// Remove old GDB binding
	if ((mGDBReady) && (bp->mGDBBpNum >= 0))
	{
		auto numItr = mBreakpointNumMap.Find(bp->mGDBBpNum);
		if ((numItr != mBreakpointNumMap.end()) && (numItr->mValue == bp))
			mBreakpointNumMap.Remove(numItr);

		if (bp->mResolvedAddr != 0)
		{
			auto addrItr = mBreakpointAddrMap.Find(bp->mResolvedAddr);
			if ((addrItr != mBreakpointAddrMap.end()) && (addrItr->mValue == bp))
				mBreakpointAddrMap.Remove(addrItr);
			bp->mResolvedAddr = 0;
		}

		String cmd = StrFormat("-break-delete %d", bp->mGDBBpNum);
		GDBMIRecord* rec = SendSync(cmd.c_str());
		delete rec;
		bp->mGDBBpNum = -1;
	}

	bp->mLineNum = lineNum;
	bp->mRequestedLineNum = lineNum;
	bp->mColumn = wantColumn;

	if ((rebindNow) && (mGDBReady) && (!bp->mFilePath.IsEmpty()))
	{
		GDBMIRecord* rec = InsertBreakpointByLocation(bp->mFilePath.c_str(), lineNum + 1);
		BindBreakpointFromResult(bp, rec);
		delete rec;
	}
}

void GDBDebugger::MoveMemoryBreakpoint(Breakpoint* wdBreakpoint, intptr addr, int byteCount)
{
}

void GDBDebugger::DisableBreakpoint(Breakpoint* breakpoint)
{
	GDBBreakpoint* bp = (GDBBreakpoint*)breakpoint;
	if ((mGDBReady) && (bp->mGDBBpNum >= 0))
	{
		String cmd = StrFormat("-break-disable %d", bp->mGDBBpNum);
		GDBMIRecord* rec = SendSync(cmd.c_str());
		delete rec;
	}
}

void GDBDebugger::SetBreakpointCondition(Breakpoint* breakpoint, const StringImpl& condition)
{
	GDBBreakpoint* bp = (GDBBreakpoint*)breakpoint;
	if ((mGDBReady) && (bp->mGDBBpNum >= 0))
	{
		String cmd;
		if (condition.IsEmpty())
			cmd = StrFormat("-break-condition %d", bp->mGDBBpNum);
		else
			cmd = StrFormat("-break-condition %d %s", bp->mGDBBpNum, condition.c_str());
		GDBMIRecord* rec = SendSync(cmd.c_str());
		delete rec;
	}
}

void GDBDebugger::SetBreakpointLogging(Breakpoint* wdBreakpoint, const StringImpl& logging, bool breakAfterLogging)
{
}

Breakpoint* GDBDebugger::FindBreakpointAt(intptr address)
{
	GDBBreakpoint* bp = NULL;
	mBreakpointAddrMap.TryGetValue((uintptr)address, &bp);
	return bp;
}

Breakpoint* GDBDebugger::GetActiveBreakpoint()
{
	if ((mActiveBreakpoint != NULL) && (mActiveBreakpoint->mHead != NULL))
		return mActiveBreakpoint->mHead;
	return mActiveBreakpoint;
}

//----------------------------------------------------------------------------
// Call stack
//----------------------------------------------------------------------------

void GDBDebugger::ClearCallStack()
{
	mCallStack.Clear();
	mCallStackDirty = true;
}

void GDBDebugger::UpdateCallStack(bool slowEarlyOut)
{
	if (!mCallStackDirty)
		return;
	if (mGDBSpawn == NULL)
		return;
	if ((mRunState != RunState_Paused) && (mRunState != RunState_Breakpoint) && (mRunState != RunState_Exception))
		return;

	mCallStack.Clear();

	GDBMIRecord* rec = SendSync("-stack-list-frames");
	if ((rec == NULL) || (rec->mClass != "done") || (rec->mValue == NULL))
	{
		delete rec;
		mCallStackDirty = false;
		return;
	}

	GDBMIValue* stack = rec->mValue->Get("stack");
	if (stack != NULL)
	{
		for (int i = 0; i < (int)stack->mChildren.size(); i++)
		{
			GDBMIValue* frameVal = stack->mChildren[i];
			if (frameVal == NULL)
				continue;

			// Might be wrapped in a "frame" key
			GDBMIValue* f = frameVal;
			if ((stack->mKeys[i] == "frame") && (frameVal->mKind == GDBMIValue::Kind::Tuple))
				f = frameVal;
			else if (frameVal->mKind == GDBMIValue::Kind::Tuple)
				f = frameVal;

			GDBFrame frame;
			frame.mAddr = (intptr)f->GetHex("addr");

			String fullname = f->GetStr("fullname");
			String file = f->GetStr("file");
			frame.mFile = file;
			frame.mFullFile = fullname.IsEmpty() ? file : fullname;

			if (mLaunchMode == GDBLaunchMode_WSL)
			{
				frame.mFile = ConvertFromWSLPath(frame.mFile);
				frame.mFullFile = ConvertFromWSLPath(frame.mFullFile);
			}
			
			String lineStr = f->GetStr("line");
			if (!lineStr.IsEmpty())
			{
				int lineNum = atoi(lineStr.c_str());
				if (lineNum > 0)
					frame.mLine = lineNum - 1;  // Convert to 0-based
			}

			String func = f->GetStr("func");
			
			// Check if this looks like a Beef frame (bf:: prefix)
			frame.mIsBeef = (strncmp(func.c_str(), "bf::", 4) == 0) ||
			                StringView(frame.mFullFile).EndsWith(".bf", StringView::CompareKind_OrdinalIgnoreCase);

			if (frame.mIsBeef)
				frame.mFunction = FixBeefName(func.c_str());
			else
				frame.mFunction = func;

			String from = f->GetStr("from");
			if (!from.IsEmpty())
			{
				// "from" is the shared library path — extract filename
				const char* p = from.c_str();
				const char* lastSlash = strrchr(p, '/');
				if (lastSlash == NULL)
					lastSlash = strrchr(p, '\\');
				frame.mModule = (lastSlash != NULL) ? String(lastSlash + 1) : from;
			}

			mCallStack.push_back(frame);
		}
	}

	delete rec;
	mCallStackDirty = false;
}

int GDBDebugger::GetCallStackCount()
{
	return (int)mCallStack.size();
}

int GDBDebugger::GetRequestedStackFrameIdx()
{
	return mRequestedStackFrameIdx;
}

int GDBDebugger::GetBreakStackFrameIdx()
{
	return mBreakStackFrameIdx;
}

void GDBDebugger::UpdateCallStackMethod(int stackFrameIdx)
{
}

void GDBDebugger::UpdateRegisterUsage(int stackFrameIdx)
{
}

//----------------------------------------------------------------------------
// Stack frame info
//----------------------------------------------------------------------------

String GDBDebugger::GetStackFrameInfo(int stackFrameIdx, intptr* addr, String* outFile, int32* outHotIdx, int32* outDefLineStart, int32* outDefLineEnd, int32* outLine, int32* outColumn, int32* outLanguage, int32* outStackSize, int8* outFlags)
{
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

	if (mCallStack.IsEmpty())
		UpdateCallStack();

	if ((stackFrameIdx < 0) || (stackFrameIdx >= (int)mCallStack.size()))
		return String();

	const GDBFrame& frame = mCallStack[stackFrameIdx];

	*addr = frame.mAddr;
	*outLine = frame.mLine;
	*outFile = frame.mFullFile.IsEmpty() ? frame.mFile : frame.mFullFile;

	DbgLanguage language = DbgLanguage_Unknown;
	if (frame.mIsBeef)
		language = DbgLanguage_Beef;
	else if (!frame.mFile.IsEmpty())
	{
		StringView sv(frame.mFile);
		if (sv.EndsWith(".bf", StringView::CompareKind_OrdinalIgnoreCase))
			language = DbgLanguage_Beef;
		else if (sv.EndsWith(".c", StringView::CompareKind_OrdinalIgnoreCase) ||
		         sv.EndsWith(".cpp", StringView::CompareKind_OrdinalIgnoreCase) ||
		         sv.EndsWith(".cc", StringView::CompareKind_OrdinalIgnoreCase))
			language = DbgLanguage_C;
	}
	*outLanguage = language;

	// Function display name: "module!function" or just "function" or raw address
	if (!frame.mFunction.IsEmpty())
	{
		String result;
		if (!frame.mModule.IsEmpty())
		{
			result += frame.mModule;
			result += '!';
		}
		result += frame.mFunction;
		return result;
	}

	return StrFormat("0x%llX", (uint64)frame.mAddr);
}

String GDBDebugger::Callstack_GetStackFrameOldFileInfo(int stackFrameIdx)
{
	return String();
}

int GDBDebugger::GetJmpState(int stackFrameIdx)
{
	return 0;
}

intptr GDBDebugger::GetStackFrameCalleeAddr(int stackFrameIdx)
{
	return 0;
}

String GDBDebugger::GetStackMethodOwner(int stackFrameIdx, int& language)
{
	return String();
}

//----------------------------------------------------------------------------
// Code address queries
//----------------------------------------------------------------------------

void GDBDebugger::GetCodeAddrInfo(intptr addr, intptr inlineCallAddr, String* outFile, int* outHotIdx, int* outDefLineStart, int* outDefLineEnd, int* outLine, int* outColumn)
{
}

void GDBDebugger::GetStackAllocInfo(intptr addr, int* outThreadId, int* outStackIdx)
{
}

String GDBDebugger::FindCodeAddresses(const StringImpl& fileName, int line, int column, bool allowAutoResolve)
{
	return String();
}

String GDBDebugger::GetAddressSourceLocation(intptr address)
{
	return String();
}

String GDBDebugger::GetAddressSymbolName(intptr address, bool demangle)
{
	return String();
}

String GDBDebugger::DisassembleAtRaw(intptr address)
{
	return String();
}

String GDBDebugger::DisassembleAt(intptr address)
{
	return String();
}

String GDBDebugger::FindLineCallAddresses(intptr address)
{
	return String();
}

//----------------------------------------------------------------------------
// Memory access
//----------------------------------------------------------------------------

bool GDBDebugger::ReadMemory(intptr address, uint64 length, void* dest, bool local)
{
	if (mGDBSpawn == NULL)
		return false;

	// Use -data-read-memory-bytes addr length
	String cmd = StrFormat("-data-read-memory-bytes 0x%llX %llu", (uint64)address, length);
	GDBMIRecord* rec = SendSync(cmd.c_str());
	if ((rec == NULL) || (rec->mClass != "done") || (rec->mValue == NULL))
	{
		delete rec;
		return false;
	}

	GDBMIValue* memList = rec->mValue->Get("memory");
	if ((memList == NULL) || memList->mChildren.IsEmpty())
	{
		delete rec;
		return false;
	}

	GDBMIValue* block = memList->mChildren[0];
	if (block == NULL)
	{
		delete rec;
		return false;
	}

	String contents = block->GetStr("contents");
	delete rec;

	if ((int)contents.length() < (int)(length * 2))
		return false;

	uint8* out = (uint8*)dest;
	for (uint64 i = 0; i < length; i++)
	{
		const char* hex = contents.c_str() + i * 2;
		char hi = hex[0];
		char lo = hex[1];
		auto hexDigit = [](char c) -> uint8
		{
			if ((c >= '0') && (c <= '9')) return (uint8)(c - '0');
			if ((c >= 'a') && (c <= 'f')) return (uint8)(c - 'a' + 10);
			if ((c >= 'A') && (c <= 'F')) return (uint8)(c - 'A' + 10);
			return 0;
		};
		out[i] = (uint8)((hexDigit(hi) << 4) | hexDigit(lo));
	}

	return true;
}

bool GDBDebugger::WriteMemory(intptr address, void* src, uint64 length)
{
	if (mGDBSpawn == NULL)
		return false;

	// Build hex string
	const uint8* bytes = (const uint8*)src;
	String hexStr;
	for (uint64 i = 0; i < length; i++)
		hexStr += StrFormat("%02x", (unsigned)bytes[i]);

	String cmd = StrFormat("-data-write-memory-bytes 0x%llX %s", (uint64)address, hexStr.c_str());
	GDBMIRecord* rec = SendSync(cmd.c_str());
	bool ok = (rec != NULL) && (rec->mClass == "done");
	delete rec;
	return ok;
}

DbgMemoryFlags GDBDebugger::GetMemoryFlags(intptr address)
{
	return DbgMemoryFlags_None;
}

//----------------------------------------------------------------------------
// Process / thread info
//----------------------------------------------------------------------------

String GDBDebugger::GetProcessInfo()
{
	if (mGDBSpawn == NULL)
		return String();

	String result;

#ifdef __linux__
	// Virtual / working set from /proc/<pid>/status
	{
		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/status", mProcessId);
		FILE* f = fopen(path, "r");
		if (f != NULL)
		{
			uint64 vmSize = 0, vmRSS = 0;
			char line[256];
			while (fgets(line, sizeof(line), f) != NULL)
			{
				unsigned long val = 0;
				if (sscanf(line, "VmSize: %lu kB", &val) == 1)
					vmSize = (uint64)val * 1024ULL;
				else if (sscanf(line, "VmRSS: %lu kB", &val) == 1)
					vmRSS = (uint64)val * 1024ULL;
			}
			fclose(f);
			result += StrFormat("VirtualMemory\t%llu\n", vmSize);
			result += StrFormat("WorkingMemory\t%llu\n", vmRSS);
		}
	}
	// CPU times from /proc/<pid>/stat
	{
		char path[64];
		snprintf(path, sizeof(path), "/proc/%d/stat", mProcessId);
		FILE* f = fopen(path, "r");
		if (f != NULL)
		{
			int pid;
			char comm[256];
			char state;
			int ppid, pgrp, session, tty, tpgid;
			unsigned long flags, minflt, cminflt, majflt, cmajflt, utime, stime;
			if (fscanf(f, "%d %255s %c %d %d %d %d %d %lu %lu %lu %lu %lu %lu %lu",
				&pid, comm, &state, &ppid, &pgrp, &session, &tty, &tpgid,
				&flags, &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime) == 15)
			{
				long clkTck = sysconf(_SC_CLK_TCK);
				if (clkTck <= 0)
					clkTck = 100;
				uint64 utimeHns = (uint64)utime * 10000000ULL / (uint64)clkTck;
				uint64 stimeHns = (uint64)stime * 10000000ULL / (uint64)clkTck;
				result += StrFormat("UserTime\t%llu\n",   utimeHns);
				result += StrFormat("KernelTime\t%llu\n", stimeHns);
			}
			fclose(f);
		}
	}
#elif defined(__APPLE__)
	{
		struct mach_task_basic_info info;
		mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
		task_t task;
		if (task_for_pid(mach_task_self(), mProcessId, &task) == KERN_SUCCESS)
		{
			if (task_info(task, MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS)
			{
				result += StrFormat("VirtualMemory\t%llu\n",  (uint64)info.virtual_size);
				result += StrFormat("WorkingMemory\t%llu\n",  (uint64)info.resident_size);
				uint64 utimeHns  = (uint64)info.user_time.seconds   * 10000000ULL
				                 + (uint64)info.user_time.microseconds * 10ULL;
				uint64 stimeHns  = (uint64)info.system_time.seconds  * 10000000ULL
				                 + (uint64)info.system_time.microseconds * 10ULL;
				result += StrFormat("UserTime\t%llu\n",   utimeHns);
				result += StrFormat("KernelTime\t%llu\n", stimeHns);
			}
			mach_port_deallocate(mach_task_self(), task);
		}
	}
#endif

	return result;
}

int GDBDebugger::GetProcessId()
{
	return mProcessId;
}

String GDBDebugger::GetThreadInfo()
{
	if (mGDBSpawn == NULL)
		return String();

	// Get list of threads from GDB
	GDBMIRecord* rec = SendSync("-thread-info");
	if ((rec == NULL) || (rec->mClass != "done") || (rec->mValue == NULL))
	{
		delete rec;
		return String();
	}

	int currentThreadId = rec->mValue->GetInt("current-thread-id");

	String result;
	result += StrFormat("%d\n", currentThreadId);

	GDBMIValue* threads = rec->mValue->Get("threads");
	if (threads != NULL)
	{
		for (int i = 0; i < (int)threads->mChildren.size(); i++)
		{
			GDBMIValue* t = threads->mChildren[i];
			if (t == NULL)
				continue;

			int threadId = t->GetInt("id");

			String threadName = t->GetStr("name");
			if (threadName.IsEmpty())
			{
				if (i == 0)
					threadName = "Main Thread";
				else
					threadName = StrFormat("Worker Thread %d", threadId);
			}

			// Location from frame
			String locString;
			GDBMIValue* frame = t->Get("frame");
			if (frame != NULL)
			{
				String from = frame->GetStr("from");
				if (!from.IsEmpty())
				{
					const char* p = from.c_str();
					const char* lastSlash = strrchr(p, '/');
					if (lastSlash == NULL)
						lastSlash = strrchr(p, '\\');
					locString += (lastSlash != NULL) ? String(lastSlash + 1) : from;
					locString += '!';
				}

				String func = frame->GetStr("func");
				if (!func.IsEmpty())
					locString += FixBeefName(func.c_str());
				else
					locString += StrFormat("0x%llX", frame->GetHex("addr"));
			}

			result += StrFormat("%d\t", threadId);
			result += threadName;
			result += '\t';
			result += locString;
			result += '\n';
		}
	}

	delete rec;
	return result;
}

void GDBDebugger::SetActiveThread(int threadId)
{
	if (mGDBSpawn == NULL)
		return;

	String cmd = StrFormat("-thread-select %d", threadId);
	GDBMIRecord* rec = SendSync(cmd.c_str());
	if ((rec != NULL) && (rec->mClass == "done"))
	{
		ClearCallStack();
		mCallStackDirty = true;
	}
	delete rec;
}

int GDBDebugger::GetActiveThread()
{
	if (mGDBSpawn == NULL)
		return 0;

	GDBMIRecord* rec = SendSync("-thread-info");
	if ((rec == NULL) || (rec->mClass != "done") || (rec->mValue == NULL))
	{
		delete rec;
		return 0;
	}

	int threadId = rec->mValue->GetInt("current-thread-id");
	delete rec;
	return threadId;
}

void GDBDebugger::FreezeThread(int threadId)
{
}

void GDBDebugger::ThawThread(int threadId)
{
}

bool GDBDebugger::IsActiveThreadWaiting()
{
	return false;
}

String GDBDebugger::GetCurrentException()
{
	if (mRunState != RunState_Exception)
		return String();

	String result;
	result += StrFormat("0x%llX", mExceptionAddress);
	result += '\n';
	result += StrFormat("%08X", mExceptionCode);
	result += '\n';
	result += mExceptionDescription;

	mRunState = RunState_Paused;

	return result;
}

//----------------------------------------------------------------------------
// Expression evaluation
//----------------------------------------------------------------------------

// Parse format specifiers from an expression (same convention as LLDBDebugger)
static bool GDBTryParseSpecifier(const char* spec, DwIntDisplayType& outIntDisplay, bool& outNoMembers)
{
	if (strcmp(spec, "x") == 0)      { outIntDisplay = DwIntDisplayType_HexadecimalLower; return true; }
	if ((strcmp(spec, "X") == 0) || (strcmp(spec, "Xh") == 0))
	                                   { outIntDisplay = DwIntDisplayType_HexadecimalUpper; return true; }
	if (strcmp(spec, "d") == 0)      { outIntDisplay = DwIntDisplayType_Decimal; return true; }
	if (strcmp(spec, "nm") == 0)     { outNoMembers = true; return true; }
	// Silently accept other known specifiers
	if ((strcmp(spec, "s") == 0) || (strcmp(spec, "s8") == 0) || (strcmp(spec, "s16") == 0) || (strcmp(spec, "s32") == 0))
		return true;
	if ((spec[0] == 'n') && (spec[1] != '\0') && (spec[2] == '\0'))
		return true;
	if (strncmp(spec, "refid=", 6) == 0)    return true;
	if (strncmp(spec, "this=", 5) == 0)     return true;
	if (strncmp(spec, "count=", 6) == 0)    return true;
	if (strncmp(spec, "maxcount=", 9) == 0) return true;
	if (strncmp(spec, "arraysize=", 10) == 0) return true;
	if (strncmp(spec, "assign=", 7) == 0)   return true;
	if (strncmp(spec, "action=", 7) == 0)   return true;
	if (strncmp(spec, "_=", 2) == 0)        return true;
	if (strncmp(spec, "expectedType=", 13) == 0)   return true;
	if (strncmp(spec, "namespaceSearch=", 16) == 0) return true;
	return false;
}

static void GDBParseExprAndFormat(const StringImpl& expr, String& outExpr, DwIntDisplayType& outIntDisplay, bool& outNoMembers)
{
	outExpr = expr;
	outIntDisplay = DwIntDisplayType_Default;
	outNoMembers = false;

	// Strip language prefix @Beef: / @C:
	{
		const char* src = outExpr.c_str();
		if ((src[0] == '@') && (src[1] != '\0'))
		{
			const char* colon = strchr(src + 1, ':');
			if (colon != NULL)
				outExpr = String(colon + 1);
		}
	}

	while (true)
	{
		const char* p = outExpr.c_str();
		int len = (int)strlen(p);
		int commaPos = -1;
		for (int i = len - 1; i >= 0; --i)
		{
			if (p[i] == ',') { commaPos = i; break; }
		}
		if (commaPos < 0)
			break;
		if (!GDBTryParseSpecifier(p + commaPos + 1, outIntDisplay, outNoMembers))
			break;
		outExpr = String(p, commaPos);
	}
}

String GDBDebugger::Evaluate(const StringImpl& expr, int callStackIdx, int cursorPos, int language, DwEvalExpressionFlags expressionFlags)
{
	GDBLog("Evaluate '%s'\n", expr.c_str());

	if (mGDBSpawn == NULL)
		return "!Not running";
	if ((mRunState != RunState_Paused) && (mRunState != RunState_Breakpoint) && (mRunState != RunState_Exception))
		return "!Not paused";

	String evalExpr;
	DwIntDisplayType intDisplay;
	bool noMembers;
	GDBParseExprAndFormat(expr, evalExpr, intDisplay, noMembers);

	if (evalExpr.IsEmpty())
		return "!Empty expression";

	// Select the correct stack frame
	if (callStackIdx != mEvalStackFrameIdx)
	{
		String selectCmd = StrFormat("-stack-select-frame %d", callStackIdx);
		SendSyncNoResult(selectCmd.c_str());
		mEvalStackFrameIdx = callStackIdx;
	}

	if (language == -1)
	{
		if (callStackIdx < mCallStack.Count())
			if (mCallStack[callStackIdx].mIsBeef)
				language = DbgLanguage_Beef;
			else
				language = DbgLanguage_C;
	}

	uint64 ptrAddr = 0;

	// Pass 1 = deref pointer pass
	for (int pass = 0; pass < 2; pass++)
	{
		// Use varobj for richer type/member information
			// Generate a unique varobj name
		String varobjName = StrFormat("bfeval__%d", mNextToken);

		if (pass == 1)
		{
			evalExpr = "*(" + evalExpr + ")";
		}

		String createCmd = StrFormat("-var-create \"%s\" @ \"%s\"",
			varobjName.c_str(), evalExpr.c_str());
		GDBMIRecord* createRec = SendSync(createCmd.c_str());

		if ((createRec == NULL) || (createRec->mClass != "done") || (createRec->mValue == NULL))
		{
			String errMsg = "!";
			if ((createRec != NULL) && (createRec->mValue != NULL))
			{
				String msg = createRec->mValue->GetStr("msg");
				if (!msg.IsEmpty())
					errMsg += msg;
				else
					errMsg += "Evaluation failed";
			}
			else
			{
				errMsg += "Evaluation failed";
			}
			delete createRec;
			return errMsg;
		}

		String typeName = createRec->mValue->GetStr("type");
		String displayVal = createRec->mValue->GetStr("value");
		int numChildren = createRec->mValue->GetInt("numchild");
		bool hasMore = createRec->mValue->GetInt("has_more") > 0;
		delete createRec;

		// Apply int display overrides
		if ((intDisplay != DwIntDisplayType_Default) && (!displayVal.IsEmpty()))
		{
			// Try to reinterpret as integer
			char* end = NULL;
			long long ival = strtoll(displayVal.c_str(), &end, 0);
			if ((end != NULL) && (*end == '\0'))
			{
				if (intDisplay == DwIntDisplayType_HexadecimalLower)
					displayVal = StrFormat("0x%llx", (uint64)ival);
				else if (intDisplay == DwIntDisplayType_HexadecimalUpper)
					displayVal = StrFormat("0x%llX", (uint64)ival);
				// Decimal is already the GDB default
			}
		}

		if (displayVal.IsEmpty())
			displayVal = "{...}";

		// Fix type name for Beef

		if (language == DbgLanguage_Beef)
			typeName = FixBeefName(typeName.c_str());

		// Determine type category
		bool isPointer = (!typeName.IsEmpty() && (typeName[typeName.length() - 1] == '*'));

		if ((isPointer) && (pass == 0))
		{
			const char* p = displayVal.c_str();
			if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X')))
			{
				ptrAddr = (uint64)strtoull(p + 2, NULL, 16);
				if (ptrAddr != 0)
					continue;
			}
		}

		bool isComposite = (((numChildren > 0) || (hasMore)) &&
			(!isPointer));

		String result;
		
		result += displayVal;
		if (pass == 1)
			result += StrFormat(" @ 0x%@", ptrAddr);
		result += '\n';
		result += typeName;
		
		if (pass == 1)
			result += "*";

		if (isPointer)
		{
			result += "\n:type\tpointer";
			// Try to get address
			String getValCmd = StrFormat("-var-evaluate-expression --thread-group i1 \"%s\"", varobjName.c_str());
			// We already have the value; just use displayVal as the address string
			// (GDB returns pointer values as "0xADDR")
			uint64 addr = 0;
			const char* p = displayVal.c_str();
			if ((p[0] == '0') && ((p[1] == 'x') || (p[1] == 'X')))
				addr = (uint64)strtoull(p + 2, NULL, 16);
			if (addr != 0)
			{
				result += StrFormat("\n:pointer\t0x%llX", addr);
				String pointeeType = typeName;
				// Strip trailing '*'
				if ((!pointeeType.IsEmpty()) && (pointeeType[pointeeType.length() - 1] == '*'))
					pointeeType = pointeeType.Substring(0, (int)pointeeType.length() - 1);
				// Trim trailing space
				while ((!pointeeType.IsEmpty()) && (pointeeType[pointeeType.length() - 1] == ' '))
					pointeeType = pointeeType.Substring(0, (int)pointeeType.length() - 1);
				result += StrFormat("\n:pointeeExpr\t(%s)0x%llX", pointeeType.c_str(), addr);
				result += StrFormat("\n:addrValueExpr\t(%s*)0x%llX", pointeeType.c_str(), addr);
			}
		}
		else if (isComposite && !noMembers)
		{
			result += "\n:type\tobject";

			// Recursively expand one level of -var-list-children, transparently
			// descending into GDB protection-level pseudo-children.
			//
			// GDB inserts fake grouping nodes named "public", "protected", "private"
			// (and sometimes "base class" or other labels) with no "type" field to
			// represent C++ access sections.  These must not appear in the member
			// list shown to the user; instead we recurse into them to collect the
			// real typed members they contain.
			//
			// A child is a protection pseudo-node when its "type" string is empty.
			// We use a small fixed-depth work-list to avoid unbounded recursion on
			// pathological varobjs.

			// Work-list: varobj child names to expand.  Starts with the root varobj.
			Array<String> toExpand;
			toExpand.push_back(varobjName);

			result += '\n';

			String collPtrValue;

			const int kMaxDepth = 8;
			for (int depth = 0; (depth < kMaxDepth) && (!toExpand.IsEmpty()); depth++)
			{
				Array<String> nextExpand;

				for (int wi = 0; wi < (int)toExpand.size(); wi++)
				{
					String listCmd = StrFormat("-var-list-children --simple-values \"%s\" 0 1000", toExpand[wi].c_str());
					GDBMIRecord* listRec = SendSync(listCmd.c_str());
					if ((listRec == NULL) || (listRec->mClass != "done") || (listRec->mValue == NULL))
					{
						delete listRec;
						continue;
					}

					GDBMIValue* children = listRec->mValue->Get("children");
					if (children != NULL)
					{
						for (int i = 0; i < (int)children->mChildren.size(); i++)
						{
							GDBMIValue* child = children->mChildren[i];
							if (child == NULL)
								continue;

							String childExp = child->GetStr("exp");
							String childType = child->GetStr("type");
							String childName = child->GetStr("name");  // fully-qualified varobj name
							String* childValuePtr = child->GetStrPtr("value");

							if (childExp.IsEmpty())
								continue;

							if ((childValuePtr == NULL) && (depth == 0) && (wi == 0))
							{
								if (childType == "System::ValueType")
								{
									// Don't list
									continue;
								}

								// This is a base type
								result += '\n';
								result += "[base]";
								result += '\t';
								result += StrFormat("(%s)({0})", childExp.c_str());
							}
							else if (childType.IsEmpty())
							{
								// No type — this is a protection pseudo-node ("public",
								// "protected", "private", or a base-class grouping).
								// Queue it for expansion rather than emitting it.
								if (!childName.IsEmpty())
									nextExpand.push_back(childName);
							}
							else
							{
								if ((!collPtrValue.IsEmpty()) && (childExp.StartsWith('[')) && (childExp.length() > 2) &&
									(isdigit(childExp[1])))
								{
									result += '\n';
									result += childExp;
									result += '\t';									
									result += collPtrValue;
									result += childExp;
									continue;
								}

								// Real typed member — emit it
								result += '\n';
								result += childExp;
								result += '\t';
								if (childExp.StartsWith('['))
									result += StrFormat("({0})%s", childExp.c_str());
								else
									result += StrFormat("({0}).%s", childExp.c_str());

								bool useChildValue = !childType.EndsWith("*");
								
								if ((childExp == "[Ptr]") && (childValuePtr != NULL))
								{
									collPtrValue = "((" + childType + ")" + *childValuePtr + ")";
									useChildValue = true;
								}
																
								if ((childValuePtr != NULL) && (useChildValue))
								{
									result += '\t';
									result += *childValuePtr;
									result += '\t';
									result += childType;
								}								
							}
						}
					}

					delete listRec;
				}

				toExpand = nextExpand;
			}
		}
		else if (!isComposite && !isPointer)
		{
			result += "\n:type\tint";
			result += "\n:canEdit";
			result += "\n:editVal\t";
			result += displayVal;
		}

		// Clean up the varobj
		{
			String deleteCmd = StrFormat("-var-delete \"%s\"", varobjName.c_str());
			GDBMIRecord* delRec = SendSync(deleteCmd.c_str());
			delete delRec;
		}

		// Reached end
		return result;
	}

	return "";
}

String GDBDebugger::EvaluateContinue()
{
	return String();
}

void GDBDebugger::EvaluateContinueKeep()
{
}

String GDBDebugger::EvaluateToAddress(const StringImpl& expr, int callStackIdx, int cursorPos)
{
	return String();
}

String GDBDebugger::EvaluateAtAddress(const StringImpl& expr, intptr atAddr, int cursorPos)
{
	return String();
}

String GDBDebugger::GetCollectionContinuation(const StringImpl& continuationData, int callStackIdx, int count)
{
	return String();
}

String GDBDebugger::GetAutoExpressions(int callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen)
{
	return String();
}

String GDBDebugger::GetAutoLocals(int callStackIdx, bool showRegs)
{
	return String();
}

String GDBDebugger::CompactChildExpression(const StringImpl& expr, const StringImpl& parentExpr, int callStackIdx)
{
	return String();
}

//----------------------------------------------------------------------------
// Module / debug info
//----------------------------------------------------------------------------

String GDBDebugger::GetModulesInfo()
{
	return String();
}

void GDBDebugger::SetAliasPath(const StringImpl& origPath, const StringImpl& localPath)
{
}

void GDBDebugger::CancelSymSrv()
{
}

bool GDBDebugger::HasPendingDebugLoads()
{
	return false;
}

int GDBDebugger::LoadImageForModule(const StringImpl& moduleName, const StringImpl& debugFileName)
{
	return 0;
}

int GDBDebugger::LoadDebugInfoForModule(const StringImpl& moduleName)
{
	return 0;
}

int GDBDebugger::LoadDebugInfoForModule(const StringImpl& moduleName, const StringImpl& debugFileName)
{
	return 0;
}

//----------------------------------------------------------------------------
// Hot-reload stubs
//----------------------------------------------------------------------------

void GDBDebugger::HotLoad(const Array<String>& objectFiles, int hotIdx)
{
}

void GDBDebugger::InitiateHotResolve(DbgHotResolveFlags flags)
{
}

intptr GDBDebugger::GetDbgAllocHeapSize()
{
	return 0;
}

String GDBDebugger::GetDbgAllocInfo()
{
	return String();
}

//----------------------------------------------------------------------------
// Shutdown
//----------------------------------------------------------------------------

void GDBDebugger::StopDebugging()
{
	GDBLog("StopDebugging\n");

	WaitForLaunchThread();

	ClearCallStack();
	mActiveBreakpoint = NULL;

	if (mGDBSpawn != NULL)
	{		
		// Ask GDB to quit gracefully
		if (mGDBStdin != NULL)
			SendSync("-gdb-exit", 2000);

		BfpSpawn_Kill(mGDBSpawn, 0, BfpKillFlag_KillChildren, NULL);
		BfpSpawn_Release(mGDBSpawn);
		mGDBSpawn = NULL;
	}

	if (mGDBStdin != NULL)
	{
		BfpFile_Release(mGDBStdin);
		mGDBStdin = NULL;
	}
	if (mGDBStdout != NULL)
	{
		BfpFile_Close(mGDBStdout, NULL);
		BfpFile_Release(mGDBStdout);
		mGDBStdout = NULL;
	}
	if (mGDBStderr != NULL)
	{
		BfpFile_Release(mGDBStderr);
		mGDBStderr = NULL;
	}

	WaitForStdoutThread();

	// Drain remaining records
	{
		AutoCrit autoCrit(mRecordCritSect);
		for (auto r : mIncomingRecords)
			delete r;
		mIncomingRecords.clear();
	}

	mProcessId = 0;
	mGDBReady = false;
	mRunning = false;
	mRunState = RunState_Terminated;
}

void GDBDebugger::Terminate()
{
	GDBLog("Terminate\n");

	WaitForLaunchThread();

	mRunState = RunState_Terminating;

	ClearCallStack();
	mActiveBreakpoint = NULL;

	if (mGDBSpawn != NULL)
	{
		BfpSpawn_Kill(mGDBSpawn, 0, BfpKillFlag_KillChildren, NULL);
		BfpSpawn_Release(mGDBSpawn);
		mGDBSpawn = NULL;
	}

	if (mGDBStdin != NULL)
	{
		BfpFile_Release(mGDBStdin);
		mGDBStdin = NULL;
	}
	if (mGDBStdout != NULL)
	{
		BfpFile_Close(mGDBStdout, NULL);
		BfpFile_Release(mGDBStdout);
		mGDBStdout = NULL;
	}
	if (mGDBStderr != NULL)
	{
		BfpFile_Release(mGDBStderr);
		mGDBStderr = NULL;
	}

	WaitForStdoutThread();

	{
		AutoCrit autoCrit(mRecordCritSect);
		for (auto r : mIncomingRecords)
			delete r;
		mIncomingRecords.clear();
	}

	mProcessId = 0;
	mGDBReady = false;
	mRunning = false;
	mRunState = RunState_Terminated;
}

void GDBDebugger::Detach()
{
	GDBLog("Detach\n");

	WaitForLaunchThread();

	mCallStack.Clear();
	mCallStackDirty = false;

	if (mGDBSpawn != NULL)
	{
		if (mDidAttach)
		{
			// Detach from the inferior so it keeps running
			GDBMIRecord* rec = SendSync("-target-detach");
			delete rec;
		}
		
		GDBMIRecord* rec = SendSync("-gdb-exit", 2000);
		delete rec;

		BfpSpawn_Kill(mGDBSpawn, 0, BfpKillFlag_KillChildren, NULL);
		BfpSpawn_Release(mGDBSpawn);
		mGDBSpawn = NULL;
	}

	if (mGDBStdin != NULL)
	{
		BfpFile_Release(mGDBStdin);
		mGDBStdin = NULL;
	}
	if (mGDBStdout != NULL)
	{
		BfpFile_Close(mGDBStdout, NULL);
		BfpFile_Release(mGDBStdout);
		mGDBStdout = NULL;
	}
	if (mGDBStderr != NULL)
	{
		BfpFile_Release(mGDBStderr);
		mGDBStderr = NULL;
	}

	WaitForStdoutThread();

	{
		AutoCrit autoCrit(mRecordCritSect);
		for (auto r : mIncomingRecords)
			delete r;
		mIncomingRecords.clear();
	}

	// Reset breakpoints (keep objects, clear GDB-side binding)
	for (auto bp : mBreakpoints)
	{
		bp->mGDBBpNum = -1;
		bp->mResolvedAddr = 0;
	}
	mBreakpointNumMap.Clear();
	mBreakpointAddrMap.Clear();

	mActiveBreakpoint = NULL;
	mProcessId = 0;
	mRequestedStackFrameIdx = 0;
	mBreakStackFrameIdx = 0;
	mHadImageFindError = false;

	mLaunchPath.Clear();
	mLaunchArgs.Clear();
	mWorkingDir.Clear();
	mEnvBlock.Clear();

	mRunState = RunState_NotStarted;
	mDidAttach = false;
	mNeedBreakpointRebind = false;
	mAutoStepRemaining = 0;
	mLaunchMode = GDBLaunchMode_Local;
	mGDBServerHost = "";
	mGDBReady = false;
	mRunning = false;
	mNeedsExecRun = false;	
}

//----------------------------------------------------------------------------
// Misc
//----------------------------------------------------------------------------

int GDBDebugger::GetAddrSize()
{
	return sizeof(addr_target);
}

bool GDBDebugger::CanOpen(const StringImpl& fileName, DebuggerResult* outResult)
{
	return true;
}

Profiler* GDBDebugger::StartProfiling()
{
	return NULL;
}

Profiler* GDBDebugger::PopProfiler()
{
	return NULL;
}

void GDBDebugger::ReportMemory(MemReporter* memReporter)
{
}

bool GDBDebugger::IsOnDemandDebugger()
{
	return false;
}

bool GDBDebugger::GetEmitSource(const StringImpl& filePath, String& outText)
{
	return false;
}

Debugger* Beefy::CreateDebuggerGDB(DebugManager* debugManager)
{
	return new GDBDebugger(debugManager);
}