using System;
using System.Threading;

namespace System
{
	public interface ILeakIdentifiable
	{
		void ToLeakString(String str);
	}

#if BF_ENABLE_REALTIME_LEAK_CHECK || BF_DEBUG_ALLOC
	[AlwaysInclude]
#endif
	public static class GC
	{
		enum RootResult
		{
			Ok
		}

		public delegate Result<void> RootCallback();
#if BF_ENABLE_REALTIME_LEAK_CHECK
		static Event<RootCallback> sRootCallbacks ~ _.Dispose();
        static Thread sThread;
		static Monitor sMonitor = new Monitor() ~ delete _;

		static this()
		{
			FindAllTLSMembers();
			Init();
		}

		static ~this()
		{
			StopCollecting();
		}
#elif BF_DEBUG_ALLOC
		static ~this()
		{
			//Report();
		}
#endif

		public static void AddRootCallback(RootCallback rootDelegate)
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
			using (sMonitor.Enter())
				sRootCallbacks.Add(rootDelegate);
#endif
		}

		public static void RemoveRootCallback(RootCallback rootDelegate)
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
			using (sMonitor.Enter())
				sRootCallbacks.Remove(rootDelegate);
#endif
		}

		static bool CallRootCallbacks()
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
			using (sMonitor.Enter())
			{
				var result = sRootCallbacks();
				if (result case .Err)
					return false;
			}
#endif
			return true;
		}

		[CallingConvention(.Cdecl)]
		extern static void ReportTLSMember(int moduleTLSIndex, void* addr, void* markFunc);
		static void ReportTLSMember(void* addr, void* markFunc)
		{
			ReportTLSMember(Thread.ModuleTLSIndex, addr, markFunc);
		}

        extern static void Run();
        static void ThreadProc(Object param)
        {
            Run();
		}

        public static void Start()
        {
#if BF_ENABLE_REALTIME_LEAK_CHECK
            sThread = new Thread(new => ThreadProc);
            sThread.Start(true);
#endif
		}

#if (BF_ENABLE_REALTIME_LEAK_CHECK || BF_DEBUG_ALLOC) && !BF_RUNTIME_DISABLE
		[CallingConvention(.Cdecl)]
		public extern static void Report();
		[CallingConvention(.Cdecl)]
		public extern static void Shutdown();
		[CallingConvention(.Cdecl)]
		public extern static void SetMaxRawDeferredObjectFreePercentage(int maxPercentage);
#else
		[LinkName("__GC_Report")]
		public static void Report() {}
		[LinkName("__GC_Shutdown")]
		public static void Shutdown() {}
		[LinkName("__GC_SetMaxRawDeferredObjectFreePercentage")]
		public static void SetMaxRawDeferredObjectFreePercentage(int maxPercentage) {}
#endif

#if BF_ENABLE_REALTIME_LEAK_CHECK
		[CallingConvention(.Cdecl)]
		private extern static void Init();
		[CallingConvention(.Cdecl)]
		public extern static void Disable();
		[CallingConvention(.Cdecl)]
        public extern static void Collect(bool async = true);
		[CallingConvention(.Cdecl)]
		private extern static void StopCollecting();
		[CallingConvention(.Cdecl)]
		private extern static void AddStackMarkableObject(Object obj);
		[CallingConvention(.Cdecl)]
		private extern static void RemoveStackMarkableObject(Object obj);
		[CallingConvention(.Cdecl), AlwaysInclude]
		private extern static void MarkAllStaticMembers();
		[CallingConvention(.Cdecl)]
		private extern static void FindAllTLSMembers();
		[CallingConvention(.Cdecl)]
		public extern static void DebugDumpLeaks();
		[CallingConvention(.Cdecl)]
        public extern static void Mark(Object obj);
		[CallingConvention(.Cdecl)]
		public extern static void Mark(void* ptr, int size);
		[CallingConvention(.Cdecl)]
		public extern static void SetAutoCollectPeriod(int periodMS); // <= -1 to disable, 0 to constantly run. Defaults to 2000.
		[CallingConvention(.Cdecl)]
		public extern static void SetCollectFreeThreshold(int freeBytes); // -1 to disable, 0 to trigger collection after every single free. Defaults to 64MB
		[CallingConvention(.Cdecl)]
		public extern static void SetMaxPausePercentage(int maxPausePercentage); // 0 = disabled. Defaults to 20.
		[CallingConvention(.Cdecl)]
		extern static void AddPendingThread(void* internalThread);
		[CallingConvention(.Cdecl)]
		public extern static void ExcludeThreadId(int thereadId);
#else
		[LinkName("__GC_Disable")]
		public static void Disable() {}
		[LinkName("__GC_Collect")]
		public static void Collect(bool async = true) {}
		[LinkName("__GC_MarkAllStaticMembers")]
		private static void MarkAllStaticMembers() {}
		[LinkName("__GC_DebugDumpLeaks")]
		public static void DebugDumpLeaks() {}
		[SkipCall, LinkName("__GC_Mark1")]
		public static void Mark(Object obj) {}
		[LinkName("__GC_Mark2")]
		public static void Mark(void* ptr, int size) {}
		[LinkName("__GC_SetAutoCollectPeriod")]
		public static void SetAutoCollectPeriod(int periodMS) {}
		[LinkName("__GC_SetCollectFreeThreshold")]
		public static void SetCollectFreeThreshold(int freeBytes) {}
		[LinkName("__GC_SetMaxPausePercentage")]
		public static void SetMaxPausePercentage(int maxPausePercentage) {}
		[LinkName("__GC_AddPendingThread")]
		static void AddPendingThread(void* internalThreadInfo) {}
		[LinkName("__GC_ExcludeThreadId")]
		public static void ExcludeThreadId(int thereadId) {}
#endif

		[DisableObjectAccessChecks]
		static void MarkAppendedObject(Object obj)
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			ClassVData* maskedVData = (ClassVData*)(void*)(obj.[Friend]mClassVData & ~(int)0xFF);
			if (maskedVData == null)
				return;
#else
			if (obj.[Friend]mClassVData == null)
				return;
#endif
			obj.[Friend]GCMarkMembers();
#endif
		}

		static void MarkDerefedObject(Object* obj)
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
			Mark(*obj);
#endif
		}

		public static mixin Mark<T>(T val) where T : class
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
		    Mark((Object)val);
#endif
		}

		public static mixin Mark<T>(T val) where T : struct
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
		    val.[Friend, Unbound]GCMarkMembers();
#endif
		}

		public static mixin Mark<T>(T val) where T : struct*
		{
		    // Memory pointed to by struct*'s will already-scanned stack memory,
		    //  or the memory would already be registered with the GC
		}

		public static mixin Mark<T>(T val)
		{
		}

		public static mixin Mark<TSizedArray, T, Size>(TSizedArray val) where Size : const int where TSizedArray : SizedArray<T, Size>
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
		    for (var element in val)
			{
				Mark(element);
			}
#endif
		}

		public static void ToLeakString(Object obj, String strBuffer)
		{
			obj.GetType().GetName(strBuffer);
			/*strBuffer.Append("@");
			((intptr)(void*)obj).ToString(strBuffer, "X", null);

			var leakIdentifier = obj as ILeakIdentifiable;
			if (leakIdentifier != null)
			{
				strBuffer.Append(" : ");
				leakIdentifier.ToLeakString(strBuffer);
			}*/
		}
	}
}
