using System.Threading;
using System.Collections;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS || BF_DEBUG_ALLOC
#define BF_DBG_RUNTIME
#endif

namespace System
{
	struct RuntimeFeatures
	{
		public bool SSE, SSE2;
		public bool AVX, AVX2, AVX512;
	}

	[StaticInitPriority(101)]
	static class Runtime
	{
		const int32 cVersion = 10;

		[CRepr, AlwaysInclude]
		struct BfDebugMessageData
		{
			enum MessageType : int32
			{
				None = 0,
				Error = 1,
				ProfilerCmd = 2
			};

			MessageType mMessageType;
			int32 mStackWindbackCount;
			int32 mBufferParamLen;
			char8* mBufferParam;
			void* mPCOverride;

			char8* mBufferPtr = null;
			int mStrSize = 0;

			[CLink]
			public static BfDebugMessageData gBfDebugMessageData;

			public static ~this()
			{
				if (gBfDebugMessageData.mBufferPtr != null)
				{
                    Internal.Free(gBfDebugMessageData.mBufferPtr);
					gBfDebugMessageData.mBufferPtr = null;
					gBfDebugMessageData.mStrSize = 0;
				}
			}

			public void SetupError(char8* str, int32 stackWindbackCount = 0) mut
			{
				mMessageType = .Error;
				mStackWindbackCount = stackWindbackCount;
				int size = Internal.CStrLen(str) + 1;
				if (mStrSize < size)
				{
					if (mBufferPtr != null)
						Internal.Free(mBufferPtr);
					mStrSize = size;
                    mBufferPtr = (char8*)Internal.Malloc(mStrSize);
				}
				Internal.MemCpy(mBufferPtr, str, size);
				mBufferParam = mBufferPtr;
				mBufferParamLen = (int32)size - 1;
				mPCOverride = null;
			}

			public void SetupProfilerCmd(char8* str) mut
			{
				mMessageType = .ProfilerCmd;
				mStackWindbackCount = 0;

				int size = Internal.CStrLen(str) + 1;
				if (mStrSize < size)
				{
					if (mBufferPtr != null)
						Internal.Free(mBufferPtr);
					mStrSize = size;
				    mBufferPtr = (char8*)Internal.Malloc(mStrSize);
				}
				Internal.MemCpy(mBufferPtr, str, size);
				mBufferParam = mBufferPtr;
				mBufferParamLen = (int32)size - 1;
				mPCOverride = null;
			}

			public void Fatal() mut
			{
				var str = scope String();
				str.Reference(mBufferPtr, mBufferParamLen, 0);
				Internal.FatalError(str, -1);
			}

			public void Clear() mut
			{
				mMessageType = .None;
				if (mBufferPtr != null)
					mBufferPtr[0] = 0;
				mBufferParamLen = 0;
			}
		}

		struct BfRtCallbacks
		{
			public static BfRtCallbacks sCallbacks = .();

			function void* (int size) mAlloc;
			function void (void* ptr) mFree;
			function void (Object obj) mObject_Delete;
			void* mUnused0;
			function Type (Object obj) mObject_GetType;
			function void (Object obj) mObject_GCMarkMembers;
			function Object (Object obj, int32 typeId) mObject_DynamicCastToTypeId;
			function void (Type type, String str) mType_GetFullName;
			function String () mString_Alloc;
			function char8* (String str) mString_ToCStr;
			function Object () mThread_Alloc;
			function Object () mThread_GetMainThread;
			function void (Object thread) mThread_ThreadProc;
			function void* (Object thread) mThread_GetInternalThread;
			function void (Object thread, void* internalThread) mThread_SetInternalThread;
			function bool (Object thread) mThread_IsAutoDelete;
			function void (Object thread) mThread_AutoDelete;
			function int32 (Object thread) mThread_GetMaxStackSize;
			function void () mThread_Exiting;
			function void () mGC_MarkAllStaticMembers;
			function bool () mGC_CallRootCallbacks;
			function void () mGC_Shutdown;
			function void (char8* str) mSetErrorString;
			function void (char8* str, int32 stackWindbackCount) mDebugMessageData_SetupError;
			function void (char8* str) mDebugMessageData_SetupProfilerCmd;
			function void () mDebugMessageData_Fatal;
			function void () mDebugMessageData_Clear;
			function int32 (char8* kind, char8* arg1, char8* arg2, int arg3) mCheckErrorHandler;

			static void* Alloc(int size)
			{
				return Internal.Malloc(size);
			}

			static void Free(void* ptr)
			{
				Internal.Free(ptr);
			}

			static void Object_Delete(Object obj)
			{
				delete obj;
			}

			static Type Object_GetType(Object obj)
			{
#if BF_DBG_RUNTIME
				return obj.[Friend, DisableObjectAccessChecks]RawGetType();
#else
				return null;
#endif
			}

			static void Object_GCMarkMembers(Object obj)
			{
#if BF_ENABLE_REALTIME_LEAK_CHECK
				obj.[Friend, DisableObjectAccessChecks]GCMarkMembers();
#endif
			}

			static Object Object_DynamicCastToTypeId(Object obj, int32 typeId)
			{
#if BF_DYNAMIC_CAST_CHECK
				return obj.DynamicCastToTypeId(typeId);
#else
				return null;
#endif
			}

			static void Type_GetFullName(Type type, String str)
			{
#if BF_DBG_RUNTIME
				type.GetFullName(str);
#else
				//
#endif
			}

			static String String_Alloc()
			{
				return new String();
			}

			static char8* String_ToCStr(String str)
			{
				return str.CStr();
			}

			static void GC_MarkAllStaticMembers()
			{
#if BF_ENABLE_REALTIME_LEAK_CHECK
				GC.[Friend]MarkAllStaticMembers();
#endif
			}

			static bool GC_CallRootCallbacks()
			{
#if BF_ENABLE_REALTIME_LEAK_CHECK
				return GC.[Friend]CallRootCallbacks();
#else
				return true;
#endif
			}

			static void GC_Shutdown()
			{
#if BF_DBG_RUNTIME
				GC.Shutdown();
#endif
			}

			static void DebugMessageData_SetupError(char8* str, int32 stackWindbackCount)
			{
				BfDebugMessageData.gBfDebugMessageData.SetupError(str, stackWindbackCount);
			}

			static void DebugMessageData_SetupProfilerCmd(char8* str)
			{
				BfDebugMessageData.gBfDebugMessageData.SetupProfilerCmd(str);
			}

			static void DebugMessageData_Fatal()
			{
				BfDebugMessageData.gBfDebugMessageData.Fatal();
			}

			static void DebugMessageData_Clear()
			{
				BfDebugMessageData.gBfDebugMessageData.Clear();
			}
		
			static int32 CheckErrorHandler(char8* kind, char8* arg1, char8* arg2, int arg3)
			{
				Error error = null;
				switch (StringView(kind))
				{
				case "FatalError":
					error = scope:: FatalError() { mError = new .(arg1) };
				case "LoadSharedLibrary":
					error = scope:: LoadSharedLibraryError() { mPath = new .(arg1) };
				case "GetSharedProcAddress":
					error = scope:: GetSharedProcAddressError() { mPath = new .(arg1), mProcName = new .(arg2) };	
				}
				if (error == null)
					return 0;
				return (int32)Runtime.CheckErrorHandlers(error);
			}

			public void Init() mut
			{
				mAlloc = => Alloc;
				mFree = => Free;
				mObject_Delete = => Object_Delete;
				mObject_GetType = => Object_GetType;
				mObject_GCMarkMembers = => Object_GCMarkMembers;
			    mObject_DynamicCastToTypeId = => Object_DynamicCastToTypeId;
				mType_GetFullName = => Type_GetFullName;
				mString_Alloc = => String_Alloc;
				mString_ToCStr = => String_ToCStr;
				mGC_MarkAllStaticMembers = => GC_MarkAllStaticMembers;
				mGC_CallRootCallbacks = => GC_CallRootCallbacks;
				mGC_Shutdown = => GC_Shutdown;
				mSetErrorString = => SetErrorString;
				mDebugMessageData_SetupError = => DebugMessageData_SetupError;
				mDebugMessageData_SetupProfilerCmd = => DebugMessageData_SetupProfilerCmd;
				mDebugMessageData_Fatal = => DebugMessageData_Fatal;
				mDebugMessageData_Clear = => DebugMessageData_Clear;
				mCheckErrorHandler = => CheckErrorHandler;
			}
		};

		private static extern void Init(int32 version, int32 flags, BfRtCallbacks* callbacks);
		private static extern void AddCrashInfoFunc(void* func);
		private static extern void Dbg_Init(int32 version, int32 flags, BfRtCallbacks* callbacks);
		private static extern void SetErrorString(char8* error);
		private static extern void* Dbg_GetCrashInfoFunc();
		public static extern void SetCrashReportKind(RtCrashReportKind crashReportKind);

		public enum RtCrashReportKind : int32
		{
			Default,
			GUI,
			Console,
			PrintOnly,
			None
		}

		enum RtFlags : int32
		{
			ObjectHasDebugFlags = 1,
			LeakCheck = 2,
			SilentCrash = 4,
			DebugAlloc = 8,
			NoThreadExitWait = 0x10
		}

		public enum ErrorHandlerResult
		{
			ContinueFailure,
			Ignore,
		}

		public class Error
		{

		}

		public class FatalError : Error
		{
			public String mError ~ delete _;
		}

		public class LoadSharedLibraryError : Error
		{
			public String mPath ~ delete _;
		}

		public class GetSharedProcAddressError : Error
		{
			public String mPath ~ delete _;
			public String mProcName ~ delete _;
		}

		public class AssertError : Error
		{
			public enum Kind
			{
				Debug,
				Runtime,
				Test
			}

			public Kind mKind;
			public String mError ~ delete _;
			public String mFilePath ~ delete _;
			public int mLineNum;

			public this(Kind kind, String error, String filePath, int lineNum)
			{
				mKind = kind;
				mError = new .(error);
				mFilePath = new .(filePath);
				mLineNum = lineNum;
			}
		}

		public enum ErrorStage
		{
			PreFail,
			Fail
		}

		public delegate ErrorHandlerResult ErrorHandler(ErrorStage stage, Error error);

		static RtFlags sExtraFlags;
		static AllocWrapper<Monitor> sMonitor ~ _.Dispose();
		static List<ErrorHandler> sErrorHandlers ~ DeleteContainerAndItems!(_);
		static bool sInsideErrorHandler;

		static bool sQueriedFeatures = false;
		static RuntimeFeatures sFeatures;

		public static this()
		{
			BfRtCallbacks.sCallbacks.Init();

			RtFlags flags = sExtraFlags;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			flags |= .ObjectHasDebugFlags;
#endif
#if BF_ENABLE_REALTIME_LEAK_CHECK
			flags |= .LeakCheck;
#endif
#if BF_DEBUG_ALLOC
			flags |= .DebugAlloc;
#endif
			Init(cVersion, (int32)flags, &BfRtCallbacks.sCallbacks);
#if BF_DBG_RUNTIME
			Dbg_Init(cVersion, (int32)flags, &BfRtCallbacks.sCallbacks);
#endif
			Thread.[Friend]Init();
		}

		[NoReturn]
		public static void FatalError(String msg = "Fatal error encountered", String filePath = Compiler.CallerFilePath, int line = Compiler.CallerLineNum)
		{
			String failStr = scope .()..AppendF("{} at line {} in {}", msg, line, filePath);
			Internal.FatalError(failStr, 1);
		}

		[NoReturn]
		public static void NotImplemented(String filePath = Compiler.CallerFilePath, int line = Compiler.CallerLineNum)
		{
			String failStr = scope .()..AppendF("Not Implemented at line {} in {}", line, filePath);
			Internal.FatalError(failStr, 1);
		}

		public static void Assert(bool condition, String error = Compiler.CallerExpression[0], String filePath = Compiler.CallerFilePath, int line = Compiler.CallerLineNum) 
		{
			if (!condition)
			{
				if (Runtime.CheckErrorHandlers(scope Runtime.AssertError(.Runtime, error, filePath, line)) == .Ignore)
					return;
				String failStr = scope .()..AppendF("Assert failed: {} at line {} in {}", error, line, filePath);
				Internal.FatalError(failStr, 1);
			}
		}

		public static void AddErrorHandler(ErrorHandler handler)
		{
			if (Compiler.IsComptime)
				return;

			using (sMonitor.Val.Enter())
			{
				if (sErrorHandlers == null)
					sErrorHandlers = new .();
				sErrorHandlers.Add(handler);
			}
		}

		public static Result<void> RemoveErrorHandler(ErrorHandler handler)
		{
			if (Compiler.IsComptime)
				return .Ok;

			using (sMonitor.Val.Enter())
			{
				if (sErrorHandlers.RemoveStrict(handler))
					return .Ok;
			}
			return .Err;
		}

		public static ErrorHandlerResult CheckErrorHandlers(Error error)
		{
			if (Compiler.IsComptime)
				return .ContinueFailure;

			using (sMonitor.Val.Enter())
			{
				if (sInsideErrorHandler)
					return .ContinueFailure;

				sInsideErrorHandler = true;
				defer { sInsideErrorHandler = false; }

				for (int pass = 0; pass < 2; pass++)
				{
					int idx = (sErrorHandlers?.Count).GetValueOrDefault() - 1;
					while (idx >= 0)
					{
						if (idx < sErrorHandlers.Count)
						{
							var handler = sErrorHandlers[idx];
							var result = handler((pass == 0) ? .PreFail : .Fail, error);
							if (result == .Ignore)
							{
								if (pass == 1)
									Internal.FatalError("Can only ignore error on prefail");
								return .Ignore;
							}
						}
						idx--;
					}
				}
			}
			return .ContinueFailure;
		}

		public static RuntimeFeatures Features
		{
			get
			{
				if (!sQueriedFeatures)
				{
#if BF_MACHINE_X86 || BF_MACHINE_X64
					QueryFeaturesX86();
#else
					sFeatures = .();
					sQueriedFeatures = true;
#endif
				}

				return sFeatures;
			}
		}

#if BF_MACHINE_X86 || BF_MACHINE_X64
		private static void QueryFeaturesX86()
		{
			sFeatures = .();
			sQueriedFeatures = true;

			uint32 _ = 0;

			// 0: Basic information
			uint32 maxBasicLeaf = 0;
			cpuid(0, 0, &maxBasicLeaf, &_, &_, &_);

			if (maxBasicLeaf < 1)
			{
			    // Earlier Intel 486, CPUID not implemented
			    return;
			}

			// 1: Processor Info and Feature Bits
			uint32 procInfoEcx = 0;
			uint32 procInfoEdx = 0;
			cpuid(1, 0, &_, &_, &procInfoEcx, &procInfoEdx);

			sFeatures.SSE = (procInfoEdx & (1 << 25)) != 0;
			sFeatures.SSE2 = (procInfoEdx & (1 << 26)) != 0;

			// 7: Extended Features
			uint32 extendedFeaturesEbx = 0;
			cpuid(7, 0, &_, &extendedFeaturesEbx, &_, &_);

			// `XSAVE` and `AVX` support:
			if ((procInfoEcx & (1 << 26)) != 0)
			{
			    // Here the CPU supports `XSAVE`

			    // Detect `OSXSAVE`, that is, whether the OS is AVX enabled and
			    // supports saving the state of the AVX/AVX2 vector registers on
			    // context-switches
			    if ((procInfoEcx & (1 << 27)) != 0)
				{
			        // The OS must have signaled the CPU that it supports saving and restoring the
			        uint64 xcr0 = xgetbv(0);

			        bool avxSupport = (xcr0 & 6) == 6;
			        bool avx512Support = (xcr0 & 224) == 224;

			        // Only if the OS and the CPU support saving/restoring the AVX registers we enable `xsave` support
			        if (avxSupport)
					{
			            sFeatures.AVX = (procInfoEcx & (1 << 28)) != 0;
			            sFeatures.AVX2 = (extendedFeaturesEbx & (1 << 5)) != 0;

			            // For AVX-512 the OS also needs to support saving/restoring
			            // the extended state, only then we enable AVX-512 support:
			            if (avx512Support)
			                sFeatures.AVX512 = (extendedFeaturesEbx & (1 << 16)) != 0;
			        }
			    }
			}
		}

		[Intrinsic("cpuid")]
		private static extern void cpuid(uint32 leaf, uint32 subleaf, uint32* eax, uint32* ebx, uint32* ecx, uint32* edx);

		[Intrinsic("xgetbv")]
		private static extern uint64 xgetbv(uint32 xcr);
#endif
	}
}
