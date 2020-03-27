using System;
using System.Collections.Generic;

namespace System.Threading {
	public function void InvokeFunction();
	public function void ForFunction(int64 idx);

#if BF_PLATFORM_WINDOWS
	public sealed class ParallelState{
		private static extern void InitializeMeta(void* meta);
		private static extern void BreakInternal(void* meta);
		private static extern void StopInternal(void* meta);
		private static extern bool StoppedInternal(void* meta);
		private static extern bool ShouldStopInternal(void* meta);

		private void* meta;

		public this(){
		    InitializeMeta(meta);
		}

		public bool IsStopped
		{
			get
			{
				return StoppedInternal(meta);
			}
		}

		public bool ShouldExitCurrentIteration
		{
			get
			{
			    return ShouldStopInternal(meta);
			}
		}

		public void Break(){
			BreakInternal(meta);
		}

		public void Stop(){
			StopInternal(meta);
		}
	}

	public sealed class Parallel {

		private static extern void InvokeInternal(void* func1, int count);

		public static void Invoke(InvokeFunction[] funcs)
		{
		    InvokeInternal(funcs.CArray(), funcs.Count);	
		}

		private static extern void ForInternal(int64 from, int64 to, void* func);

		public static void For(int64 from, int64 to, ForFunction func)
		{
			ForInternal(from, to, (void*)func);
		}

		private static extern void ForeachInternal(void* arrOfPointers, int count, int32 elementSize, void* func);

		// TODO: Make this also available for Dictionary
		public static void Foreach<T>(Span<T> arr, function void(T item) func)
		{
			List<void*> lv=scope List<void*>();

			for(ref T i in ref arr){
			    lv.Add(&i);
			}

			ForeachInternal(lv.Ptr, arr.Length, sizeof(T), (void*)func);
		}

	}
#endif
}
