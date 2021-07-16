using System;
using System.Collections;

namespace System.Threading
{
	public delegate void InvokableFunction();

	struct PInvokeWrapper
	{
		public InvokableFunction[] delegates;

		public static void Call(Self* sf, int idx)
		{
			sf.delegates[idx]();
		}
	}

	struct PForWrapper
	{
		public delegate void(int64 item) mDelegate;

		public static void Call(Self* sf, int64 idx)
		{
			sf.mDelegate(idx);
		}
	}

	struct StatedPForWrapper
	{
		public delegate void(int64 item, ref ParallelState ps) mDelegate;
		public ParallelState ps;

		public static void Call(Self* sf, int64 idx)
		{
			sf.mDelegate(idx, ref sf.ps);
		}
	}

	struct PForeachWrapper<T>
	{
		public delegate void(ref T item) mDelegate;
		public T*[] ptrs;

		public static void Call(Self* sf, int idx)
		{
			sf.mDelegate(ref *(sf.ptrs[idx]));
		}
	}

	struct StatedPForeachWrapper<T>
	{
		public delegate void(ref T item, ref ParallelState ps) mDelegate;
		public T*[] ptrs;
		public ParallelState ps;

		public static void Call(Self* sf, int idx)
		{
			sf.mDelegate(ref *(sf.ptrs[idx]), ref sf.ps);
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

	public sealed class Parallel
	{
		private static extern void InvokeInternal(void* wrapper, void* func, int count);

		public static void Invoke(InvokableFunction[] funcs)
		{
			PInvokeWrapper wDlg;
			wDlg.delegates = funcs;
			function void(PInvokeWrapper*, int) fn = =>PInvokeWrapper.Call; 
			InvokeInternal(&wDlg, (void*)fn, funcs.Count);
		}

		private static extern void ForInternal(int64 from, int64 to, void* wrapper, void* func);
		private static extern void ForInternal(int64 from, int64 to, void* meta, void* wrapper, void* func);

		// From is inclusive, to is exclusive
		public static void For(int64 from, int64 to, delegate void(int64 item) func)
		{
			PForWrapper wDlg;
			wDlg.mDelegate = func;
			function void(PForWrapper*, int64) fn = => PForWrapper.Call;
			ForInternal(from, to, &wDlg, (void*)fn);
		}

		// From is inclusive, to is exclusive
		public static void For(int64 from, int64 to, delegate void(int64 item, ref ParallelState ps) func)
		{
			StatedPForWrapper wDlg;
			wDlg.mDelegate = func;
			wDlg.ps = new ParallelState();
			function void(StatedPForWrapper*, int64) fn = => StatedPForWrapper.Call;

			ForInternal(from, to, wDlg.ps.meta, &wDlg, (void*)fn);
		}

		// TODO: Make this also available for Dictionary
		public static void Foreach<T>(Span<T> arr, delegate void(ref T item) func)
		{
			PForeachWrapper<T> wDlg;
			wDlg.mDelegate = func;
			wDlg.ptrs = new T*[arr.Length];
			function void(PForeachWrapper<T>* wr, int idx) fn = => PForeachWrapper<T>.Call;

			int idx = 0;
			for (ref T i in ref arr)
			{
				wDlg.ptrs[idx] = &i;
				idx += 1;
			}

			ForInternal(0, arr.Length, &wDlg, (void*)fn);
		}

		public static void Foreach<T>(Span<T> arr, delegate void(ref T item) func)
		{
			PForeachWrapper<T> wDlg;
			wDlg.mDelegate = func;
			wDlg.ptrs = new T*[arr.Length];
			function void(PForeachWrapper<T>* wr, int idx) fn = => PForeachWrapper<T>.Call;

			int idx = 0;
			for (ref T i in ref arr)
			{
				wDlg.ptrs[idx] = &i;
				idx += 1;
			}

			ForInternal(0, arr.Length, &wDlg, (void*)fn);
		}

		public static void Foreach<T>(Span<T> arr, delegate void(ref T item, ref ParallelState ps) func)
		{
			StatedPForeachWrapper<T> wDlg;
			wDlg.mDelegate = func;
			wDlg.ps = new ParallelState();
			wDlg.ptrs = new T*[arr.Length];
			function void(StatedPForeachWrapper<T>* wr, int idx) fn = => StatedPForeachWrapper<T>.Call;

			int idx = 0;
			for (ref T i in ref arr)
			{
				wDlg.ptrs[idx] = &i;
				idx += 1;
			}

			ForInternal(0, arr.Length, wDlg.ps.meta, &wDlg, (void*)fn);
		}
	}
}
