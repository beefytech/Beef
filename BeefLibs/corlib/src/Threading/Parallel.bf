using System;
using System.Collections.Generic;

namespace System.Threading {
	public delegate void InvokableFunction();

#if BF_PLATFORM_WINDOWS
	// The 'V' in the name means that the item is passed by value
	struct VDelegateWrapper<T>
	{
		public delegate void(T item) mDelegate;

		public static void Call(Self* sf, void* item)
		{
		    	T itm=*((T*)item);
			sf.mDelegate(itm);
		}
	}

	struct VStatedDelegateWrapper<T>
	{
		public delegate void(T item, ref ParallelState ps) mDelegate;

		public static void Call(Self* sf, void* item, void* pState)
		{
		    	ParallelState state=*((ParallelState*)pState);
			T itm=*((T*)item);
			sf.mDelegate(itm, ref state);
		}
	}

	struct DelegateWrapper<T>
	{
		public delegate void(ref T item) mDelegate;

		public static void Call(Self* sf, void* item)
		{
		   	 T itm=*((T*)item);
			 sf.mDelegate(ref itm);
		}
	}

	struct StatedDelegateWrapper<T>
	{
		public delegate void(ref T item, ref ParallelState ps) mDelegate;

		public static void Call(Self* sf, void* item, void* pState)
		{
			ParallelState state=*((ParallelState*)pState);
			T itm=*((T*)item);
			sf.mDelegate(ref itm, ref state);
		}
	}

	public sealed class ParallelState
	{
		private static extern void InitializeMeta(void* meta);
		private static extern void BreakInternal(void* meta);
		private static extern void StopInternal(void* meta);
		private static extern bool StoppedInternal(void* meta);
		private static extern bool ShouldStopInternal(void* meta);

		public void* meta;

		public this()
		{
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

		public void Break()
		{
			BreakInternal(meta);
		}

		public void Stop()
		{
			StopInternal(meta);
		}
	}

	public sealed class Parallel {

		private static extern void InvokeInternal(void* func1, int count);

		public static void Invoke(InvokableFunction[] funcs)
		{
		    InvokeInternal(*(funcs.CArray()), funcs.Count);	
		}

		private static extern void ForInternal(int64 from, int64 to, void* wrapper, void* func);
		private static extern void ForInternal(int64 from, int64 to, void* pState, void* meta, void* wrapper, void* func);

		public static void For(int64 from, int64 to, delegate void(int64 item) func)
		{
			VDelegateWrapper<int64> wDlg;
			wDlg.mDelegate=func;
			function void(VDelegateWrapper<int64>* wr, void* item) fn= =>VDelegateWrapper<int64>.Call;
			ForInternal(from, to, &wDlg, (void*)fn);
		}

		public static void For(int64 from, int64 to, delegate void(int64 item, ref ParallelState ps) func)
		{
			VStatedDelegateWrapper<int64> wDlg;
			wDlg.mDelegate=func;
			function void(VStatedDelegateWrapper<int64>* wr, void* item, void* pState) fn= =>VStatedDelegateWrapper<int64>.Call;

			ParallelState parState=scope ParallelState();

			ForInternal(from, to, &parState, parState.meta, &wDlg, (void*)fn);
		}

		private static extern void ForeachInternal(void* arrOfPointers, int count, void* wrapper, void* func);
		private static extern void ForeachInternal(void* arrOfPointers, int count, void* pState, void* meta, void* wrapper, void* func);

		// TODO: Make this also available for Dictionary
		public static void Foreach<T>(Span<T> arr, delegate void(ref T item) func)
		{
			(void*)[] lv=new (void*)[arr.Length];
			
			int idx=0;
			for(ref T i in ref arr){
			    lv[idx]=&i;
			    idx+=1;
			}

			DelegateWrapper<T> wDlg;
			wDlg.mDelegate=func;
			function void(DelegateWrapper<T>* wr, void* item) fn= =>DelegateWrapper<T>.Call;

			ForeachInternal(*(lv.CArray()), arr.Length, &wDlg, (void*)fn);
			delete lv;
		}

		public static void Foreach<T>(Span<T> arr, delegate void(ref T item, ref ParallelState ps) func)
		{
			(void*)[] lv=new (void*)[arr.Length];
			
			int idx=0;
			for(ref T i in ref arr){
			    lv[idx]=&i;
			    idx+=1;
			}

			StatedDelegateWrapper<T> wDlg;
			wDlg.mDelegate=func;
			function void(StatedDelegateWrapper<T>* wr, void* item, void* pState) fn= =>StatedDelegateWrapper<T>.Call;

			ParallelState parState=scope ParallelState();

			ForeachInternal(*(lv.CArray()), arr.Length, &parState, parState.meta, &wDlg, (void*)fn);
		}
	}
#endif
}
