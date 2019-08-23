#if BF_ENABLE_REALTIME_LEAK_CHECK || BF_DEBUG_ALLOC
#define BF_DBG_RUNTIME
#endif

namespace System
{
	[StaticInitPriority(100)]
	class Runtime
	{
		const int32 cVersion = 8;

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
			function void (Object obj, String str) mObject_ToString;
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
			function void () mGC_MarkAllStaticMembers;
			function bool () mGC_CallRootCallbacks;
			function void () mGC_Shutdown;
			function void (char8* str) mSetErrorString;
			function void (char8* str, int32 stackWindbackCount) mDebugMessageData_SetupError;
			function void (char8* str) mDebugMessageData_SetupProfilerCmd;
			function void () mDebugMessageData_Fatal;
			function void () mDebugMessageData_Clear;

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

			static void Object_ToString(Object obj, String str)
			{
#if BF_DBG_RUNTIME
				obj.ToString(str);
#endif
			}

			static Type Object_GetType(Object obj)
			{
#if BF_DBG_RUNTIME
				return obj.RawGetType();
#else
				return null;
#endif
			}

			static void Object_GCMarkMembers(Object obj)
			{
#if BF_ENABLE_REALTIME_LEAK_CHECK
				obj.[Friend, SkipAccessCheck]GCMarkMembers();
#endif
			}

			static Object Object_DynamicCastToTypeId(Object obj, int32 typeId)
			{
#if BF_DYNAMIC_CAST_CHECK
				return obj.[Friend]DynamicCastToTypeId(typeId);
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
		

			public void Init() mut
			{
				mAlloc = => Alloc;
				mFree = => Free;
				mObject_Delete = => Object_Delete;
				mObject_ToString = => Object_ToString;
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
			DebugAlloc = 8
		}

		public static this()
		{
			BfRtCallbacks.sCallbacks.Init();

			RtFlags flags = default;
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
		}

		[NoReturn]
		public static void FatalError(String msg = "Fatal error encountered")
		{
			Internal.FatalError(msg, 1);
		}

		[NoReturn]
		public static void NotImplemented()
		{
			Internal.FatalError("Not Implemented", 1);
		}

		public static void Assert(bool condition) 
		{
			if (!condition)
				Internal.FatalError("Assert failed", 1);
		}

		public static void Assert(bool condition, String error) 
		{
			if (!condition)
				Internal.FatalError("Assert failed", 1);
		}
	}
}
