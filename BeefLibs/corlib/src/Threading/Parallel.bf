using System;
using System.Collections.Generic;

namespace System.Threading {
	public function void InvokeFunction();
	public function void ForFunctionLong(int64 idx);
	public function void ForFunctionInt(int32 idx);

	public sealed class Parallel {
		static extern void InvokeInternal(void* func1, int count);

		public static void Invoke(InvokeFunction[] funcs)
		{
		    InvokeInternal(funcs.CArray(), funcs.Count);	
		}

		static extern void ForInternal(int64 from, int64 to, void* func);
		static extern void ForInternal(int32 from, int32 to, void* func);

		public static void For(int64 from, int64 to, ForFunctionLong func)
		{
			ForInternal(from, to, (void*)func);
		}

		public static void For(int32 from, int32 to, ForFunctionInt func)
		{
			ForInternal(from, to, (void*)func);
		}

		static extern void ForeachInternal(void* arrOfPointers, int count, void* func);

		// TODO: Make this generic after ICollection is available
		public static void Foreach<T>(List<T> arr, function void(T item) func)
		{
			List<void*> lv=scope List<void*>();

			for(ref T i in ref arr){
			    lv.Add(&i);
			}

			ForeachInternal(lv.Ptr, arr.Count, (void*)func);
		}

		public static void Foreach<T>(T[] arr, function void(T item) func)
		{
			List<void*> lv=scope List<void*>();

			for(ref T i in ref arr){
			    lv.Add(&i);
			}

			ForeachInternal(lv.Ptr, arr.Count, (void*)func);
		}
	}
}
