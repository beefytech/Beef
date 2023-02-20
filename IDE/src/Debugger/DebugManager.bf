using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using Beefy.utils;
using IDE.util;
using IDE.Compiler;

namespace IDE.Debugger
{
	public class DebugManager
	{
		public enum RunState
		{
			NotStarted,
			Running,
			Running_ToTempBreakpoint,
			Paused,
			Breakpoint,
			DebugEval,
			DebugEval_Done,
			HotStep,
			Exception,
			Terminating,
			Terminated,
			SearchingSymSrv,
			HotResolve,
			TargetUnloaded
		}

		public enum IntDisplayType
		{
			Default,
			Decimal,
			Hexadecimal,
			Binary,
			Octal,
			COUNT
		}

		public enum FloatDisplayType
		{
			Default,
			Minimal,
			Full,
			Hexadecimal,
			COUNT
		}

		public enum StringDisplayType
		{
			Default,
			ShowSpecials,
			Hexadecimal,
			COUNT
		}

		public enum MmDisplayType
		{
			Default,
			UInt8,
			Int16,
			Int32,
			Int64,
			Float,
			Double,
			COUNT
		}

		public enum Language
		{
			NotSet = -1,
			Unknown = 0,
			C,
			Beef,
			BeefUnfixed, // Has *'s after class names
		}

		public enum FrameFlags
		{
			Optimized = 1,
			HasPendingDebugInfo = 2,
			CanLoadOldVersion = 4,
			WasHotReplaced = 8,
			HadError = 0x10
		}

		//[Flags]
		public enum EvalExpressionFlags
		{
			None				= 0,
			FullPrecision		= 0x01,
			ValidateOnly		= 0x02,
			DeselectCallStackIdx = 0x04,
			AllowSideEffects 	= 0x08,
			AllowCalls			= 0x10,
			AllowProperties		= 0x20,
			MemoryAddress		= 0x40,
			MemoryWatch			= 0x80,
			Symbol				= 0x100,
			StepIntoCall		= 0x200,
			RawStr				= 0x400,
			AllowStringView		= 0x800
		}

		[Reflect]
		public enum SymSrvFlags
		{
			None = 0,
			Disable = 1,
			TempCache = 2
		}

		public enum AttachFlags
		{
			None = 0,
			ShutdownOnExit = 1
		}

		public enum HotResolveFlags
		{
			None,
			ActiveMethods = 1,
			Allocations = 2
		}

		public List<Breakpoint> mBreakpointList = new List<Breakpoint>();
		public Dictionary<String, StepFilter> mStepFilterList = new Dictionary<String, StepFilter>();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_Create();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_Delete();

		[CallingConvention(.Stdcall),CLink]
		static extern int32 Debugger_GetAddrSize();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_FullReportMemory();

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_OpenMiniDump(char8* filename);

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_OpenFile(char8* launchPath, char8* targetPath, char8* args, char8* workingDir, void* envBlockPtr, int32 envBlockLen, bool hotSwapEnabled);

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_ComptimeAttach(void* bfCompiler);

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_Attach(int32 processId, AttachFlags attachFlags);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_Run();

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_HotLoad(char8* objectFileNames, int32 hotIdx);

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_LoadDebugVisualizers(char8* fileName);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_StopDebugging();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_Terminate();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_Detach();

		[CallingConvention(.Stdcall),CLink]
		static extern RunState Debugger_GetRunState();

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_HasLoadedTargetBinary();

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetCurrentException();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_Continue();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_BreakAll();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_StepInto(bool inAssembly);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_StepIntoSpecific(int addr);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_StepOver(bool inAssembly);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_StepOut(bool inAssembly);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_SetNextStatement(bool inAssembly, char8* fileName, int wantLineNumOrAsmAddr, int32 wantColumn);

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_Update();

		[CallingConvention(.Stdcall),CLink]
		static extern void* Debugger_CreateAddressBreakpoint(int address);

		[CallingConvention(.Stdcall),CLink]
		static extern void* Debugger_CreateStepFilter(char8* filter, bool isGlobal, StepFilterKind stepFilterKind);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_SetDisplayTypes(char8* referenceId, char8* formatStr, IntDisplayType intDisplayType, MmDisplayType mmDisplayType, FloatDisplayType floatDisplayType);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetDisplayTypes(char8* referenceId, out IntDisplayType intDisplayType, out MmDisplayType mmDisplayType, out FloatDisplayType floatDisplayType, out bool foundSpecific);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetDisplayTypeNames();

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_EvaluateContinue();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_EvaluateContinueKeep();

		[CallingConvention(.Stdcall),CLink]
		static extern StringView Debugger_Evaluate(char8* expr, int32 callStackIdx, int32 cursorPos, int32 language, EvalExpressionFlags expressionFlags);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_EvaluateToAddress(char8* expr, int32 callStackIdx, int32 cursorPos);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_EvaluateAtAddress(char8* expr, int addr, int32 cursorPos);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetAutoExpressions(int32 callStackIdx, uint64 memoryRangeStart, uint64 memoryRangeLen);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetAutoLocals(int32 callStackIdx, bool showRegs);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_CompactChildExpression(char8* expr, char8* parentExpr, int32 callStackIdx);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetCollectionContinuation(char8* continuationData, int32 callStackIdx, int32 count);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_ForegroundTarget();

		[CallingConvention(.Stdcall),CLink]
		static extern void CallStack_Update();

		[CallingConvention(.Stdcall),CLink]
		static extern void CallStack_Rehup();

		[CallingConvention(.Stdcall),CLink]
		static extern int32 CallStack_GetCount();

		[CallingConvention(.Stdcall),CLink]
		static extern int32 CallStack_GetRequestedStackFrameIdx();

		[CallingConvention(.Stdcall),CLink]
		static extern int32 CallStack_GetBreakStackFrameIdx();

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetCodeAddrInfo(int addr, int inlineCallAddr, out int32 hotIdx, out int32 defLineStart, out int32 defLineEnd, out int32 line, out int32 column);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_GetStackAllocInfo(int addr, out int threadId, int32* outStackIdx);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* CallStack_GetStackFrameInfo(int32 stackFrameIdx, out int addr, out char8* outFile, out int32 hotIdx, out int32 defLineStart, out int32 defLineEnd, out int32 outLine, out int32 outColumn, out int32 outLanguage, out int32 outStackSize, out FrameFlags flags);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* CallStack_GetStackFrameId(int32 stackFrameIdx);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Callstack_GetStackFrameOldFileInfo(int32 stackFrameIdx);

		[CallingConvention(.Stdcall),CLink]
		static extern int32 CallStack_GetJmpState(int32 stackFrameIdx);

		[CallingConvention(.Stdcall),CLink]
		static extern int Debugger_GetStackFrameCalleeAddr(int32 stackFrameIdx);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* CallStack_GetStackMethodOwner(int32 stackFrameIdx, out int32 language);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetProcessInfo();

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetThreadInfo();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_SetActiveThread(int threadId);

		[CallingConvention(.Stdcall),CLink]
		static extern int Debugger_GetActiveThread();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_FreezeThread(int threadId);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_ThawThread(int threadId);

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_IsActiveThreadWaiting();

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_PopMessage();

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_HasMessages();

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_FindCodeAddresses(char8* file, int32 line, int32 column, bool allowAutoResolve);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetAddressSourceLocation(int addr);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetAddressSymbolName(int addr, bool demangle);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_FindLineCallAddresses(int addr);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_DisassembleAt(int addr);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_ReadMemory(int addr, int size, uint8* data);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_WriteMemory(int addr, int size, uint8* data);

		[CallingConvention(.Stdcall),CLink]
		static extern void* Debugger_StartProfiling(int threadId, char8* desc, int32 sampleRate);

		[CallingConvention(.Stdcall),CLink]
		static extern void* Debugger_PopProfiler();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_SetSymSrvOptions(char8* symCacheDir, char8* symSrvStr, int32 flags);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_SetSourcePathRemap(char8* remapStr);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_CancelSymSrv();

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetModulesInfo();

		[CallingConvention(.Stdcall),CLink]
		static extern bool Debugger_HasPendingDebugLoads();

		[CallingConvention(.Stdcall),CLink]
		static extern int32 Debugger_LoadImageForModuleWith(char8* moduleName, char8* imageFileName);

		[CallingConvention(.Stdcall),CLink]
		static extern int32 Debugger_LoadDebugInfoForModule(char8* moduleName);

		[CallingConvention(.Stdcall),CLink]
		static extern int32 Debugger_LoadDebugInfoForModuleWith(char8* moduleName, char8* debugFileName);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_SetStepOverExternalFiles(bool stepOverExternalFiles);

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_InitiateHotResolve(int32 flags);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetHotResolveData(uint8* outTypeData, int32* outTypeDataSize);

		[CallingConvention(.Stdcall),CLink]
		static extern char8* Debugger_GetDbgAllocInfo();

		[CallingConvention(.Stdcall),CLink]
		static extern void Debugger_SetAliasPath(char8* origPath, char8* localPath);

		[CallingConvention(.Stdcall),CLink]
		static extern int Debugger_GetDbgAllocHeapSize();

		[CallingConvention(.Stdcall), CLink]
		static extern char8* Debugger_GetEmitSource(char8* fileName);

		public String mRunningPath ~ delete _;
		public bool mIsRunning;
		public bool mIsRunningCompiled;
		public bool mIsRunningWithHotSwap;
		public bool mIsComptimeDebug;
		//public RunState mLastUpdatedRunState;
		public bool mCallStackDirty;
		public int32 mActiveCallStackIdx;
		public Event<Action> mBreakpointsChangedDelegate ~ _.Dispose();
		public Breakpoint mRunToCursorBreakpoint;
		public int32 mDebugIdx;

		public bool IsRunning
		{
			get
			{
				return mIsRunning;
			}
		}

		public bool IsRunningUncompiled
		{
			get
			{
				return mIsRunning && !mIsRunningCompiled;
			}
		}

		public this()
		{
			Debugger_Create();
		}

		public ~this()
		{
			for (var breakpoint in mBreakpointList)
				breakpoint.Deref();
			delete mBreakpointList;
			for (var filter in mStepFilterList.Values)
				delete filter;
			delete mStepFilterList;
			Debugger_Delete();
		}

		public void Reset()
		{
			for (var breakpoint in mBreakpointList)
				breakpoint.Deref();
			mBreakpointList.Clear();
			for (var filter in mStepFilterList.Values)
				delete filter;
			mStepFilterList.Clear();
		}

		public void LoadDebugVisualizers(String fileName)
		{
			scope AutoBeefPerf("LoadDebugVisualizers");
			Debugger_LoadDebugVisualizers(fileName);
		}

		public void FullReportMemory()
		{
			Debugger_FullReportMemory();
		}

		public bool OpenFile(String launchPath, String targetPath, String args, String workingDir, Span<char8> envBlock, bool isCompiled, bool hotSwapEnabled)
		{
			DeleteAndNullify!(mRunningPath);
			mRunningPath = new String(launchPath);

			mIsComptimeDebug = false;
			mIsRunningCompiled = isCompiled;
			mIsRunningWithHotSwap = hotSwapEnabled;
			return Debugger_OpenFile(launchPath, targetPath, args, workingDir, envBlock.Ptr, (int32)envBlock.Length, hotSwapEnabled);
		}

		public bool ComptimeAttach(BfCompiler compiler)
		{
			mIsComptimeDebug = true;
			mIsRunningCompiled = false;
			mIsRunningWithHotSwap = false;
			mIsRunning = true;
			return Debugger_ComptimeAttach(compiler.mNativeBfCompiler);
		}

		public void SetSymSrvOptions(String symCacheDir, String symSrvStr, SymSrvFlags symSrvFlags)
		{
			Debugger_SetSymSrvOptions(symCacheDir, symSrvStr, (int32)symSrvFlags);
		}

		public void SetSourcePathRemap(String remapStr)
		{
			Debugger_SetSourcePathRemap(remapStr);
		}

		public bool OpenMiniDump(String file)
		{
			mIsComptimeDebug = false;
			mIsRunningCompiled = false;
			mIsRunningWithHotSwap = false;
			return Debugger_OpenMiniDump(file);
		}

		public void Run()
		{
			Debugger_Run();
		}

		public void HotLoad(String[] objectFileNames, int hotIdx)
		{
			

			String filenamesStr = scope String();
			filenamesStr.Join("\n", params objectFileNames);
			Debugger_HotLoad(filenamesStr, (int32)hotIdx);

			// The hot load will bind breakpoints to any new methods, but the old versions
			//  need remapped text positions
			for (var breakpoint in mBreakpointList)
				breakpoint.CheckBreakpointHotBinding();
		}

		public void StopDebugging()
		{
			Debugger_StopDebugging();
		}

		public void Terminate()
		{
			Debugger_Terminate();
		}

		public void Detach()
		{
			Debugger_Detach();

			for (var breakpoint in mBreakpointList)
			{
				// Unbind thread id - it won't match next runthrough
				if (breakpoint.mThreadId != -1)
					breakpoint.mThreadId = 0;
			}

			mIsComptimeDebug = false;
			mIsRunning = false;
		}

		public RunState GetRunState()
		{
			return Debugger_GetRunState();
		}

		public bool HasLoadedTargetBinary()
		{
			return Debugger_HasLoadedTargetBinary();
		}

		public bool HasPendingDebugLoads()
		{
			return Debugger_HasPendingDebugLoads();
		}

		public bool PopMessage(String msg)
		{
			char8* nativeStr = Debugger_PopMessage();
			if (nativeStr == null)
				return false;
			msg.Append(nativeStr);
			return true;
		}

		public bool HasMessages()
		{
			return Debugger_HasMessages();
		}

		public void Continue()
		{
			Debugger_Continue();
		}

		public bool IsPaused(bool allowDebugEvalDone = false)
		{
			RunState runState = GetRunState();
			return (runState == .Paused) || (runState == .Breakpoint) || (runState == .Exception) || 
				((runState == .DebugEval_Done) && (allowDebugEvalDone));
		}

		public void GetCurrentException(String exStr)
		{
			exStr.Append(Debugger_GetCurrentException());
		}

		public void BreakAll()
		{
			Debugger_BreakAll();
		}

		public void StepInto(bool inAssembly)
		{
			Debugger_StepInto(inAssembly);
		}

		public void StepIntoSpecific(int addr)
		{
			Debugger_StepIntoSpecific(addr);
		}

		public void StepOver(bool inAssembly)
		{
			Debugger_StepOver(inAssembly);
		}

		public void StepOut(bool inAssembly)
		{
			Debugger_StepOut(inAssembly);
		}

		public void SetNextStatement(bool inAssembly, String fileName, int wantLineNumOrAsmAddr, int wantColumn)
		{
			Debugger_SetNextStatement(inAssembly, fileName, wantLineNumOrAsmAddr, (int32)wantColumn);
		}

		public bool Update()
		{
			return Debugger_Update();
		}

		public Breakpoint CreateBreakpoint(int address)
		{
			void* nativeBreakpoint = Debugger_CreateAddressBreakpoint(address);
			if (nativeBreakpoint == null)
				return null;

			Breakpoint breakpoint = new Breakpoint();
			breakpoint.mNativeBreakpoint = nativeBreakpoint;
			breakpoint.mAddressRequested = true;
			mBreakpointList.Add(breakpoint);

			if (mBreakpointsChangedDelegate.HasListeners)
				mBreakpointsChangedDelegate();

			return breakpoint;
		}

		public Breakpoint CreateBreakpoint_Create(String fileName, int wantLineNum, int wantColumn, int instrOffset = -1)
		{
			Breakpoint breakpoint = new Breakpoint();
			//breakpoint.mNativeBreakpoint = nativeBreakpoint;
			breakpoint.mFileName = new String(fileName);
			breakpoint.mLineNum = (int32)wantLineNum;
			breakpoint.mColumn = (int32)wantColumn;
			breakpoint.mInstrOffset = (int32)instrOffset;
			mBreakpointList.Add(breakpoint);
			return breakpoint;
		}

		public void CreateBreakpoint_Finish(Breakpoint breakpoint, bool createNow = true, bool bindNow = true)
		{
			if ((mIsRunning) && (createNow))
				breakpoint.CreateNative(bindNow);

			if (mBreakpointsChangedDelegate.HasListeners)
				mBreakpointsChangedDelegate();
		}

		public Breakpoint CreateBreakpoint(String fileName, int wantLineNum, int wantColumn, int instrOffset = -1, bool bindNow = true)
		{
			/*void* nativeBreakpoint = Debugger_CreateBreakpoint(fileName, bindNow ? wantLineNum : -1, wantColumn, instrOffset);
			if (nativeBreakpoint == null)
				return null;*/
			var breakpoint = CreateBreakpoint_Create(fileName, wantLineNum, wantColumn, instrOffset);
			CreateBreakpoint_Finish(breakpoint, true, bindNow);

			return breakpoint;
		}

		public Breakpoint CreateMemoryBreakpoint(String watchExpr, int addr, int byteCount, String addrType)
		{
			void* nativeBreakpoint = null;
			if (addr != (int)0)
			{
				nativeBreakpoint = Breakpoint.[Friend]Debugger_CreateMemoryBreakpoint(addr, (.)byteCount);
				if (nativeBreakpoint == null)
					return null;
			}

			Breakpoint breakpoint = new Breakpoint();
			if (addrType != null)
				String.NewOrSet!(breakpoint.mAddrType, addrType);
			breakpoint.mNativeBreakpoint = nativeBreakpoint;
			breakpoint.mByteCount = (uint8)byteCount;
			breakpoint.mMemoryAddress = addr;
			breakpoint.mIsMemoryBreakpoint = true;
			breakpoint.mMemoryWatchExpression = new String(watchExpr);
			mBreakpointList.Add(breakpoint);

			//breakpoint.Bind();

			if (mBreakpointsChangedDelegate.HasListeners)
				mBreakpointsChangedDelegate();

			return breakpoint;
		}

		public StepFilter CreateStepFilter(String filter, bool isGlobal, StepFilterKind filterKind)
		{
			if (mStepFilterList.TryGetValue(filter, var value))
				return value;

			Debugger_CreateStepFilter(filter, isGlobal, filterKind);
			var stepFilter = new StepFilter();
			stepFilter.mFilter = new String(filter);
			stepFilter.mKind = filterKind;
			mStepFilterList[stepFilter.mFilter] = stepFilter;
			return stepFilter;
		}

		public void DeleteStepFilter(StepFilter stepFilter)
		{
			mStepFilterList.Remove(stepFilter.mFilter);
			delete stepFilter;
		}

		public Breakpoint CreateSymbolBreakpoint(String symbolName)
		{
			Breakpoint breakpoint = new Breakpoint();
			breakpoint.mSymbol = new String(symbolName);
			breakpoint.mInstrOffset = -1;
			if (mIsRunning)
				breakpoint.CreateNative();
			mBreakpointList.Add(breakpoint);

			if (mBreakpointsChangedDelegate.HasListeners)
				mBreakpointsChangedDelegate();

			return breakpoint;
		}

		public void DeleteBreakpoint(Breakpoint breakpoint)
		{
			if (mRunToCursorBreakpoint == breakpoint)
				mRunToCursorBreakpoint = null;

			if (breakpoint.mIsMemoryBreakpoint)
				gApp.RefreshWatches();

			mBreakpointList.Remove(breakpoint);
			breakpoint.Kill();
			mBreakpointsChangedDelegate();
		}

		public void ClearInvalidBreakpoints()
		{
			for (int32 breakIdx = 0; breakIdx < mBreakpointList.Count; breakIdx++)
			{
				var breakpoint = mBreakpointList[breakIdx];
				if ((breakpoint.mAddressRequested) && (breakpoint.IsBound()))
				{
					BfLog.LogDbg("ClearInvalidBreakpoints deleting breakpoint\n");
					DeleteBreakpoint(breakpoint);
					breakIdx--;
				}

				if (breakpoint.mIsMemoryBreakpoint)
				{
					breakpoint.Disable();
				}
			}
		}

		public enum BreakpointBindKind
		{
			OldRebindNow = 1,
			NewCreateAndBind = 2,
			NewCreateNoBind = 4
		}

		public void RehupBreakpoints(bool rebindNow, bool rebindNew = true)
		{
			for (var breakpoint in mBreakpointList)
			{
				if (!breakpoint.mDisabled)
				{
					if (breakpoint.mNativeBreakpoint == null)
						breakpoint.CreateNative(rebindNew);
					else
						breakpoint.Rehup(rebindNow);
				}
			}
			mBreakpointsChangedDelegate();
		}

		public void SetBreakpointDisabled(Breakpoint breakpoint, bool disabled)
		{
			breakpoint.mDisabled = disabled;
			if (breakpoint.mDisabled)
			{
				breakpoint.Disable();
			}
			else
			{
				if (mIsRunning)
					breakpoint.CreateNative(true, true);
			}
			mBreakpointsChangedDelegate();
		}

		public void DisposeNativeBreakpoints()
		{
			for (int breakpointIdx < mBreakpointList.Count)
			{
				let breakpoint = mBreakpointList[breakpointIdx];
				if (breakpoint.mDeleteOnUnbind)
				{
					mBreakpointList.RemoveAt(breakpointIdx);
					breakpoint.Kill();
					breakpointIdx--;
					mBreakpointsChangedDelegate();
				}
				else
					breakpoint.DisposeNative();
			}
		}

		public void GetCollectionContinuation(String continuationData, int32 count, String outData)
		{
			char8* result = Debugger_GetCollectionContinuation(continuationData, mActiveCallStackIdx, count);
			if (result == null)
				return;
			outData.Append(result);
		}

		public void EvaluateContinue(String outVal)
		{
			char8* result = Debugger_EvaluateContinue();
			if (result == null)
				return;
			outVal.Append(result);
		}

		public void EvaluateContinueKeep()
		{
			Debugger_EvaluateContinueKeep();
		}

		// AllowAssignment, allowCalls
		public void Evaluate(String expr, String outVal, int cursorPos = -1, int language = -1, EvalExpressionFlags expressionFlags = EvalExpressionFlags.None)
		{
			StringView result = Debugger_Evaluate(expr, (expressionFlags.HasFlag(.DeselectCallStackIdx)) ? -1 : mActiveCallStackIdx, (int32)cursorPos, (int32)language, expressionFlags);
			outVal.Append(result);
		}

		public void EvaluateAtAddress(String expr, int addr, String outVal, int cursorPos = -1)
		{
			char8* result = Debugger_EvaluateAtAddress(expr, addr, (int32)cursorPos);
			if (result == null)
				return;
			outVal.Append(result);
		}

		public void EvaluateToAddress(String expr, String outVal, int cursorPos = -1)
		{
			char8* result = Debugger_EvaluateToAddress(expr, mActiveCallStackIdx, (int32)cursorPos);
			if (result == null)
				return;
			outVal.Append(result);
		}

		public void GetAutoExpressions(uint64 memoryRangeStart, uint64 memoryRangeLen, String outVal)
		{
			char8* result = Debugger_GetAutoExpressions(mActiveCallStackIdx, memoryRangeStart, memoryRangeLen);
			if (result == null)
				return;
			outVal.Append(result);
		}

		public void GetAutoLocals(bool showRegs, String outLocals)
		{
			char8* result = Debugger_GetAutoLocals(mActiveCallStackIdx, showRegs);
			if (result == null)
				return;
			outLocals.Append(result);
		}

		public void CompactChildExpression(String expr, String parentExpr, String outVal)
		{
			char8* result = Debugger_CompactChildExpression(expr, parentExpr, mActiveCallStackIdx);
			if (result == null)
				return;
			outVal.Append(result);
		}

		public void ForegroundTarget()
		{
			Debugger_ForegroundTarget();
		}

		public void UpdateCallStack()
		{
			// Always revert back to top of call stack
			mActiveCallStackIdx = CallStack_GetRequestedStackFrameIdx();

			CallStack_Update();
			mCallStackDirty = false;

			/*int newCallStackIdx = mSelectedCallStackIdx;
			while (newCallStackIdx < CallStack_GetCount() - 1)
			{
				intptr addr;
				String file = scope String();
				String stackFrameInfo = scope String();
				GetStackFrameInfo(newCallStackIdx, out addr, file, stackFrameInfo);
				if (file.Length > 0)
				{					
					mSelectedCallStackIdx = newCallStackIdx;
					break;
				}
				newCallStackIdx++;
			}*/
		}

		public void RehupCallstack()
		{
			CallStack_Rehup();
		}

		public int32 GetBreakStackFrameIdx()
		{
			return CallStack_GetBreakStackFrameIdx();
		}

		public void CheckCallStack()
		{
			if (!IsPaused())
				return;

			if (mCallStackDirty)
			{
				UpdateCallStack();
			}
			else
			{
				// Incremental update
				CallStack_Update();
			}
		}

		public int32 GetCallStackCount()
		{
			return CallStack_GetCount();
		}

		public void GetStackFrameInfo(int32 stackFrameIdx, out int addr, String file, String outStackFrameInfo)
		{
			int hotIdx;
			int defLineStart;
			int defLineEnd;
			int line;
			int column;
			int language;
			int stackSize;
			DebugManager.FrameFlags flags;
			GetStackFrameInfo(stackFrameIdx, outStackFrameInfo, out addr, file, out hotIdx, out defLineStart, out defLineEnd, out line, out column, out language, out stackSize, out flags);
		}

		public int GetStackFrameCalleeAddr(int32 stackFrameIdx)
		{
			return Debugger_GetStackFrameCalleeAddr(stackFrameIdx);
		}

		public void GetCodeAddrInfo(int addr, int inlineCallAddr, String outFile, out int hotIdx, out int defLineStart, out int defLineEnd, out int line, out int column)
		{
			int32 hotIdxOut;
			int32 lineOut;
			int32 columnOut;
			int32 defLineStartOut = -1;
			int32 defLineEndOut = -1;
			char8* locationStr = Debugger_GetCodeAddrInfo(addr, inlineCallAddr, out hotIdxOut, out defLineStartOut, out defLineEndOut, out lineOut, out columnOut);
			hotIdx = hotIdxOut;
			defLineStart = defLineStartOut;
			defLineEnd = defLineEndOut;
			line = lineOut;
			column = columnOut;
			if (locationStr != null)
				outFile.Append(locationStr);
		}

		public void GetStackAllocInfo(int addr, out int threadId, int* outStackIdx)
		{
			int32 stackIdx32 = -1;
			int32* stackIdx32Ptr = null;
			if (outStackIdx != null)
				stackIdx32Ptr = &stackIdx32;
			//Debugger_GetStackAllocInfo(addr, out threadId, (outStackIdx != null) ? &stackIdx32 : null);
			Debugger_GetStackAllocInfo(addr, out threadId, stackIdx32Ptr);
			if (outStackIdx != null)
				*outStackIdx = stackIdx32;
		}

		public void GetStackFrameInfo(int32 stackFrameIdx, String outStackFrameInfo, out int addr, String outFile, out int hotIdx, out int defLineStart, out int defLineEnd,
			out int line, out int column, out int language, out int stackSize, out FrameFlags flags)
		{
			char8* fileStrPtr;

			int32 hotIdxOut;
			int32 defLineStartOut;
			int32 defLineEndOut;
			int32 lineOut;
			int32 columnOut;
			int32 languageOut;
			int32 stackSizeOut;
			char8* locationStr = CallStack_GetStackFrameInfo(stackFrameIdx, out addr, out fileStrPtr, out hotIdxOut, out defLineStartOut, out defLineEndOut, out lineOut, out columnOut, out languageOut, out stackSizeOut, out flags);
			hotIdx = hotIdxOut;
			defLineStart = defLineStartOut;
			defLineEnd = defLineEndOut;
			line = lineOut;
			column = columnOut;
			language = languageOut;
			stackSize = stackSizeOut;

			if (outFile != null)
			{
				outFile.Append(fileStrPtr);
				if ((outFile.StartsWith("$Emit")) && (mIsRunningCompiled))
				{
					int dollarPos = outFile.IndexOf('$', 1);
					outFile.Remove(5, dollarPos - 5);
				}
			}
			if (outStackFrameInfo != null)
				outStackFrameInfo.Append(locationStr);
		}

		public void GetStackFrameOldFileInfo(int32 stackFrameIdx, String outOldInfoInfo)
		{
			char8* oldFileInfo = Callstack_GetStackFrameOldFileInfo(stackFrameIdx);
			outOldInfoInfo.Append(oldFileInfo);
		}

		public int32 GetJmpState(int32 stackFrameIdx)
		{
			return CallStack_GetJmpState(stackFrameIdx);
		}

		public void GetStackMethodOwner(int32 stackFrameIdx, String outStr, out int32 language)
		{
			char8* str = CallStack_GetStackMethodOwner(stackFrameIdx, out language);
			if (str != null)
				outStr.Append(str);
		}

		public void GetProcessInfo(String outProcessInfo)
		{
			if (!mIsRunning)
				return;
			char8* strPtr = Debugger_GetProcessInfo();
			outProcessInfo.Append(strPtr);
		}

		public void GetThreadInfo(String outThreadInfo)
		{
			if (!mIsRunning)
				return;
			char8* strPtr = Debugger_GetThreadInfo();
			outThreadInfo.Append(strPtr);
		}

		public void SetActiveThread(int32 threadId)
		{
			Debugger_SetActiveThread(threadId);
		}

		public int GetActiveThread()
		{
			if (!IsPaused())
				return -1;
			return Debugger_GetActiveThread();
		}

		public void FreezeThread(int32 threadId)
		{
			Debugger_FreezeThread(threadId);
		}

		public void ThawThread(int32 threadId)
		{
			Debugger_ThawThread(threadId);
		}

		public bool IsActiveThreadWaiting()
		{
			return Debugger_IsActiveThreadWaiting();
		}

		public void FindCodeAddresses(String file, int line, int column, bool allowAutoResolve, String outCodeAddresses)
		{
			char8* strPtr = Debugger_FindCodeAddresses(file, (int32)line, (int32)column, allowAutoResolve);
			outCodeAddresses.Append(strPtr);
		}

		public void FindLineCallAddresses(int addr, String outCallAddresses)
		{
			char8* strPtr = Debugger_FindLineCallAddresses(addr);
			outCallAddresses.Append(strPtr);
		}

		public void DisassembleAt(int addr, String outText)
		{
			char8* strPtr = Debugger_DisassembleAt(addr);
			outText.Append(strPtr);
		}

		public void GetAddressSourceLocation(int addr, String outSourceLoc)
		{
			char8* strPtr = Debugger_GetAddressSourceLocation(addr);
			outSourceLoc.Append(strPtr);
		}

		public void GetAddressSymbolName(int addr, bool demangle, String outSourceLoc)
		{
			char8* strPtr = Debugger_GetAddressSymbolName(addr, demangle);
			outSourceLoc.Append(strPtr);
		}

		public void ReadMemory(int addr, int size, uint8[] data)
		{
			Debugger_ReadMemory(addr, size, data.CArray());
		}

		public void WriteMemory(int addr, int size, uint8[] data)
		{
			Debugger_WriteMemory(addr, size, data.CArray());
		}

		public void SetDisplayTypes(String referenceId, String formatStr, IntDisplayType intDisplayType, MmDisplayType mmDisplayType, FloatDisplayType floatDisplayType)
		{
			Debugger_SetDisplayTypes(referenceId, formatStr, intDisplayType, mmDisplayType, floatDisplayType);
		}

		public bool GetDisplayTypes(String referenceId, String formatStr, out IntDisplayType intDisplayType, out MmDisplayType mmDisplayType, out FloatDisplayType floatDisplayType)
		{
			bool foundSpecific = false;
			char8* result = Debugger_GetDisplayTypes(referenceId, out intDisplayType, out mmDisplayType, out floatDisplayType, out foundSpecific);
			formatStr.Append(result);
			return foundSpecific;
		}

		public void GetDisplayTypeNames(String outDisplayTypeNames)
		{
			char8* displayTypes = Debugger_GetDisplayTypeNames();
			outDisplayTypeNames.Append(displayTypes);
		}

		public int32 GetAddrSize()
		{
			return Debugger_GetAddrSize();
		}

		public bool Attach(Process process, AttachFlags attachFlags)
		{
			mIsRunningCompiled = false;
			mIsComptimeDebug = false;
			mIsRunningWithHotSwap = false;
			return Debugger_Attach(process.Id, attachFlags);
		}

		public DbgProfiler StartProfiling(int threadId, String desc, int sampleRate)
		{
			DbgProfiler profiler = new DbgProfiler(Debugger_StartProfiling(threadId, desc, (.)sampleRate));
			return profiler;
		}

		public DbgProfiler PopProfiler()
		{
			DbgProfiler profiler = new DbgProfiler(Debugger_PopProfiler());
			return profiler;
		}

		public void CancelSymSrv()
		{
			Debugger_CancelSymSrv();
		}

		public void GetModulesInfo(String modulesInfo)
		{
			modulesInfo.Append(Debugger_GetModulesInfo());
		}

		public int32 LoadDebugInfoForModule(String moduleName)
		{
			return Debugger_LoadDebugInfoForModule(moduleName);
		}

		public int32 LoadImageForModule(String moduleName, String debugFileName)
		{
			return Debugger_LoadImageForModuleWith(moduleName, debugFileName);
		}

		public int32 LoadDebugInfoForModule(String moduleName, String debugFileName)
		{
			return Debugger_LoadDebugInfoForModuleWith(moduleName, debugFileName);
		}

		public void SetStepOverExternalFiles(bool stepOverExternalFiles)
		{
			Debugger_SetStepOverExternalFiles(stepOverExternalFiles);
		}

		public void InitiateHotResolve(HotResolveFlags flags)
		{
			Debugger_InitiateHotResolve((.)flags);
		}

		public bool GetHotResolveData(List<uint8> outTypeData, String outStackStr)
		{
			int32 outDataSize = 0;
			char8* result = Debugger_GetHotResolveData(null, &outDataSize);
			if (outDataSize == -1)
				return false;

			outTypeData.Clear();
			result = Debugger_GetHotResolveData(outTypeData.GrowUnitialized(outDataSize), &outDataSize);
			outStackStr.Append(result);
			return true;
		}

		public void GetDbgAllocInfo(String outInfo)
		{
			char8* result = Debugger_GetDbgAllocInfo();
			outInfo.Append(result);
		}

		public static void GetFailString(StringView result, StringView expr, String outFailStr)
		{
			Debug.Assert(result[0] == '!');
			StringView errorString = .(result, 1);

			var errorVals = scope List<StringView>(errorString.Split('\t'));
			if (errorVals.Count == 3)
			{
				int32 errorStart = int32.Parse(scope String(errorVals[0]));
				int32 errorEnd = errorStart + int32.Parse(scope String(errorVals[1])).GetValueOrDefault();
				outFailStr.Append(errorVals[2]);

				if ((errorEnd > 0) && (errorStart < expr.Length))
				{
					bool useRef = false;
					errorEnd = Math.Min(errorEnd, (int32)expr.Length);

					StringView refStr = .(expr, errorStart, errorEnd - errorStart);
					for (let c in refStr)
					{
						if (c.IsLetterOrDigit)
						{
							useRef = true;
						}
					}

					if (useRef)
					{
						outFailStr.Append(": ");
						outFailStr.Append(refStr);
					}
				}
			}
			else
			{
				outFailStr.Append(errorString);
			}
		}

		public void SetAliasPath(String origPath, String localPath)
		{
			Debugger_SetAliasPath(origPath, localPath);
		}

		public int GetDbgAllocHeapSize()
		{
			return Debugger_GetDbgAllocHeapSize();
		}

		public bool GetEmitSource(StringView fileName, String outText)
		{
			char8* str = Debugger_GetEmitSource(fileName.ToScopeCStr!());
			if (str == null)
				return false;
			outText.Append(str);
			return true;
		}

		public bool GetStackFrameId(int32 callStackIdx, String outString)
		{
			char8* stackId = CallStack_GetStackFrameId(callStackIdx);
			if (stackId == null)
				return false;
			outString.Append(stackId);
			return true;
		}
	}
}
