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

#if BF_ENABLE_REALTIME_LEAK_CHECK || BF_DEBUG_ALLOC
		public extern static void Report();
		public extern static void Shutdown();
		public extern static void SetMaxRawDeferredObjectFreePercentage(int maxPercentage);
#else
		public static void Report() {}
		public static void Shutdown() {}
		public static void SetMaxRawDeferredObjectFreePercentage(int maxPercentage) {}
#endif

#if BF_ENABLE_REALTIME_LEAK_CHECK
		private extern static void Init();
        public extern static void Collect(bool async = true);
		private extern static void StopCollecting();
		private extern static void AddStackMarkableObject(Object obj);
		private extern static void RemoveStackMarkableObject(Object obj);
		[AlwaysInclude]
		private extern static void MarkAllStaticMembers();
		private extern static void FindAllTLSMembers();
		public extern static void DebugDumpLeaks();
        public extern static void Mark(Object obj);
		public extern static void Mark(void* ptr, int size);
		public extern static void SetAutoCollectPeriod(int periodMS); // <= -1 to disable, 0 to constantly run. Defaults to -1
		public extern static void SetCollectFreeThreshold(int freeBytes); // -1 to disable, 0 to trigger collection after every single free. Defaults to 64MB
		public extern static void SetMaxPausePercentage(int maxPausePercentage); // 0 = disabled. Defaults to 20.
#else
		public static void Collect(bool async = true) {}
		private static void MarkAllStaticMembers() {}
		public static void DebugDumpLeaks() {}
		public static void Mark(Object obj) {}
		public static void Mark(void* ptr, int size) {}
		public static void SetAutoCollectPeriod(int periodMS) {}
		public static void SetCollectFreeThreshold(int freeBytes) {}
		public static void SetMaxPausePercentage(int maxPausePercentage) {}
#endif

		static void MarkDerefedObject(Object* obj)
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
			Mark(*obj);
#endif
		}

        public static void Mark<T>(T val) where T : class
        {
#if BF_ENABLE_REALTIME_LEAK_CHECK
            Mark((Object)val);
#endif
		}

        public static void Mark<T>(T val) where T : struct
        {
#if BF_ENABLE_REALTIME_LEAK_CHECK
            val.[Friend]GCMarkMembers();
#endif
		}

        public static void Mark<T>(T val) where T : struct*
        {
            // Memory pointed to by struct*'s will already-scanned stack memory,
            //  or the memory would already be registered with the GC
		}

		public static void Mark<TSizedArray, T, Size>(TSizedArray val) where Size : const int where TSizedArray : SizedArray<T, Size>
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
		    for (var element in val)
			{
				Mark(element);
			}
#endif
		}

		public static void Mark_Enumerate<T>(T val) where T : var
		{
			/*for (var element in val)
			{
				Mark(element);
			}*/
		}


		public static void Mark_Unbound<T>(T val) where T : var
		{
#if BF_ENABLE_REALTIME_LEAK_CHECK
			Mark(val);
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
