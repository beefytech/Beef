using System.Diagnostics;

namespace System.Threading
{
	static class Interlocked
	{
		public enum AtomicOrdering : uint8
		{
			Unordered,
			Relaxed,
			Acquire,
			Release,
			AcqRel,
			SeqCst,
			ORDERMASK = 7,

			Flag_Volatile = 8,
			Flag_ReturnModified = 0x10 // Generally atomic instructions return original value, this overrides that
		}

		public enum RMWAtomicOrdering : uint8
		{
			Relaxed = (.)AtomicOrdering.Relaxed,
			Acquire = (.)AtomicOrdering.Acquire,
			Release = (.)AtomicOrdering.Release,
			AcqRel = (.)AtomicOrdering.AcqRel,
			SeqCst = (.)AtomicOrdering.SeqCst,
			Flag_Volatile = (.)AtomicOrdering.Flag_Volatile,
		}

		public enum LoadAtomicOrdering : uint8
		{
			Relaxed = (.)AtomicOrdering.Relaxed,
			Acquire = (.)AtomicOrdering.Acquire,
			SeqCst = (.)AtomicOrdering.SeqCst,
			Flag_Volatile = (.)AtomicOrdering.Flag_Volatile,
		}

		public enum StoreAtomicOrdering : uint8
		{
			Relaxed = (.)AtomicOrdering.Relaxed,
			Release = (.)AtomicOrdering.Release,
			SeqCst = (.)AtomicOrdering.SeqCst,
			Flag_Volatile = (.)AtomicOrdering.Flag_Volatile,
		}

		public enum FenceAtomicOrdering : uint8
		{
			Acquire = (.)AtomicOrdering.Acquire,
			Release = (.)AtomicOrdering.Release,
			AcqRel = (.)AtomicOrdering.AcqRel,
			SeqCst = (.)AtomicOrdering.SeqCst
		}

		[Intrinsic("atomic_fence")]
		static extern void AtomicFence(AtomicOrdering ordering);
		[Inline]
		public static void Fence<TAtomicOrdering>(TAtomicOrdering ordering = .SeqCst) where TAtomicOrdering : const FenceAtomicOrdering
		{
			AtomicFence((AtomicOrdering)ordering);
		}

		[Intrinsic("atomic_fence")]
		public static extern void CompilerBarrier();

		[Intrinsic("atomic_load")]
		static extern uint8 Load(ref uint8 location, AtomicOrdering ordering);
		[Intrinsic("atomic_load")]
		static extern uint16 Load(ref uint16 location, AtomicOrdering ordering);
		[Intrinsic("atomic_load")]
		public static extern uint32 Load(ref uint32 location, AtomicOrdering ordering);
		[Intrinsic("atomic_load")]
		static extern uint64 Load(ref uint64 location, AtomicOrdering ordering);
		[Inline]
		public static T Load<T, TAtomicOrdering>(ref T location, TAtomicOrdering ordering = .SeqCst) where TAtomicOrdering : const LoadAtomicOrdering
		{
			if (sizeof(T) == sizeof(uint8))
			{
				var result = Load(ref *(uint8*)&location, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(uint16))
			{
				var result = Load(ref *(uint16*)&location, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(uint32))
			{
				var result = Load(ref *(uint32*)&location, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(uint64))
			{
				var result = Load(ref *(uint64*)&location, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_store")]
		static extern void Store(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_store")]
		static extern void Store(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_store")]
		static extern void Store(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_store")]
		static extern void Store(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static void Store<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where TAtomicOrdering : const StoreAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(uint8))
				Store(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(uint16))
				Store(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(uint32))
				Store(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(uint64))
				Store(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_xchg")]
		extern static uint8 Exchange(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_xchg")]
		extern static uint16 Exchange(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_xchg")]
		extern static uint32 Exchange(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_xchg")]
		extern static uint64 Exchange(ref uint64 location, uint64 value, AtomicOrdering ordering);
		/// Atomically sets a value and returns the original value.
		public static T Exchange<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Exchange(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Exchange(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
		        var result = Exchange(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Exchange(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_cmpxchg")]
		extern static uint8 CompareExchange(ref uint8 location, uint8 comparand, uint8 value, AtomicOrdering successOrdering);
		[Intrinsic("atomic_cmpxchg")]
		extern static uint16 CompareExchange(ref uint16 location, uint16 comparand, uint16 value, AtomicOrdering successOrdering);
		[Intrinsic("atomic_cmpxchg")]
		extern static uint32 CompareExchange(ref uint32 location, uint32 comparand, uint32 value, AtomicOrdering successOrdering);
		[Intrinsic("atomic_cmpxchg")]
		extern static uint64 CompareExchange(ref uint64 location, uint64 comparand, uint64 value, AtomicOrdering successOrdering);
		/// Compares 'location' to 'comparand' for equality and, if they are equal, replaces 'location' with 'value' and returns the original value in 'location'.
		/// @param location The destination, whose value is compared with comparand and possibly replaced.
		/// @param value The value that replaces the destination value if the comparison results in equality.
		/// @param comparand The value that is compared to the value at location.
		[Inline]
		public static T CompareExchange<T, TAtomicOrdering>(ref T location, T comparand, T value, TAtomicOrdering successOrdering = .SeqCst) where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			var comparand;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = CompareExchange(ref *(uint8*)&location, *(uint8*)&comparand, *(uint8*)&value, (AtomicOrdering)successOrdering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = CompareExchange(ref *(uint16*)&location, *(uint16*)&comparand, *(uint16*)&value, (AtomicOrdering)successOrdering);
				return *(T*)&result;
			}	
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = CompareExchange(ref *(uint32*)&location, *(uint32*)&comparand, *(uint32*)&value, (AtomicOrdering)successOrdering);
				return *(T*)&result;
			}	
			else if (sizeof(T) == sizeof(int64))
			{
			    var result = CompareExchange(ref *(uint64*)&location, *(uint64*)&comparand, *(uint64*)&value, (AtomicOrdering)successOrdering);
				return *(T*)&result;
			}	
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_cmpxchg")]
		extern static uint8 CompareExchange(ref uint8 location, uint8 comparand, uint8 value, AtomicOrdering successOrdering, AtomicOrdering failOrdering);
		[Intrinsic("atomic_cmpxchg")]
		extern static uint16 CompareExchange(ref uint16 location, uint16 comparand, uint16 value, AtomicOrdering successOrdering, AtomicOrdering failOrdering);
		[Intrinsic("atomic_cmpxchg")]
		extern static uint32 CompareExchange(ref uint32 location, uint32 comparand, uint32 value, AtomicOrdering successOrdering, AtomicOrdering failOrdering);
		[Intrinsic("atomic_cmpxchg")]
		extern static uint64 CompareExchange(ref uint64 location, uint64 comparand, uint64 value, AtomicOrdering successOrdering, AtomicOrdering failOrdering);
		/// Compares 'location' to 'comparand' for equality and, if they are equal, replaces 'location' with 'value' and returns the original value in 'location'.
		/// @param location The destination, whose value is compared with comparand and possibly replaced.
		/// @param value The value that replaces the destination value if the comparison results in equality.
		/// @param comparand The value that is compared to the value at location.
		[Inline]
		public static T CompareExchange<T, TSuccessOrdering, TFailOrdering>(ref T location, T comparand, T value, TSuccessOrdering successOrdering, TFailOrdering failOrdering)
			where TSuccessOrdering : const RMWAtomicOrdering
			where TFailOrdering : const LoadAtomicOrdering
		{
			var value;
			var comparand;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = CompareExchange(ref *(uint8*)&location, *(uint8*)&comparand, *(uint8*)&value, (AtomicOrdering)successOrdering, (AtomicOrdering)failOrdering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = CompareExchange(ref *(uint16*)&location, *(uint16*)&comparand, *(uint16*)&value, (AtomicOrdering)successOrdering, (AtomicOrdering)failOrdering);
				return *(T*)&result;
			}	
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = CompareExchange(ref *(uint32*)&location, *(uint32*)&comparand, *(uint32*)&value, (AtomicOrdering)successOrdering, (AtomicOrdering)failOrdering);
				return *(T*)&result;
			}	
			else if (sizeof(T) == sizeof(int64))
			{
			    var result = CompareExchange(ref *(uint64*)&location, *(uint64*)&comparand, *(uint64*)&value, (AtomicOrdering)successOrdering, (AtomicOrdering)failOrdering);
				return *(T*)&result;
			}	
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_cmpstore")]
		extern static bool CompareStore(ref uint8 location, uint8 comparand, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_cmpstore")]
		extern static bool CompareStore(ref uint16 location, uint16 comparand, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_cmpstore")]
		extern static bool CompareStore(ref uint32 location, uint32 comparand, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_cmpstore")]
		extern static bool CompareStore(ref uint64 location, uint64 comparand, uint64 value, AtomicOrdering ordering);
		/// Compares 'location' to 'comparand' for equality and, if they are equal, replaces 'location' with 'value' and returns the original value in 'location'.
		/// @param location The destination, whose value is compared with comparand and possibly replaced.
		/// @param value The value that replaces the destination value if the comparison results in equality.
		/// @param comparand The value that is compared to the value at location.
		[Inline]
		public static bool CompareStore<T, TAtomicOrdering>(ref T location, T comparand, T value, TAtomicOrdering ordering = .SeqCst) where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			var comparand;
			if (sizeof(T) == sizeof(int8))
			    return CompareStore(ref *(uint8*)&location, *(uint8*)&comparand, *(uint8*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(int16))
				return CompareStore(ref *(uint16*)&location, *(uint16*)&comparand, *(uint16*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(int32))
				return CompareStore(ref *(uint32*)&location, *(uint32*)&comparand, *(uint32*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(int64))
				return CompareStore(ref *(uint64*)&location, *(uint64*)&comparand, *(uint64*)&value, (AtomicOrdering)ordering);
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_cmpstore_weak")]
		extern static bool CompareStoreWeak(ref uint8 location, uint8 comparand, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_cmpstore_weak")]
		extern static bool CompareStoreWeak(ref uint16 location, uint16 comparand, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_cmpstore_weak")]
		extern static bool CompareStoreWeak(ref uint32 location, uint32 comparand, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_cmpstore_weak")]
		extern static bool CompareStoreWeak(ref uint64 location, uint64 comparand, uint64 value, AtomicOrdering ordering);
		/// Compares 'location' to 'comparand' for equality and, if they are equal, replaces 'location' with 'value' and returns the original value in 'location'.
		/// @param location The destination, whose value is compared with comparand and possibly replaced.
		/// @param value The value that replaces the destination value if the comparison results in equality.
		/// @param comparand The value that is compared to the value at location.
		[Inline]
		public static bool CompareStoreWeak<T, TAtomicOrdering>(ref T location, T comparand, T value, TAtomicOrdering ordering = .SeqCst) where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			var comparand;
			if (sizeof(T) == sizeof(int8))
			    return CompareStoreWeak(ref *(uint8*)&location, *(uint8*)&comparand, *(uint8*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(int16))
				return CompareStoreWeak(ref *(uint16*)&location, *(uint16*)&comparand, *(uint16*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(int32))
				return CompareStoreWeak(ref *(uint32*)&location, *(uint32*)&comparand, *(uint32*)&value, (AtomicOrdering)ordering);
			else if (sizeof(T) == sizeof(int64))
				return CompareStoreWeak(ref *(uint64*)&location, *(uint64*)&comparand, *(uint64*)&value, (AtomicOrdering)ordering);
			else
				FailOnDataSize();
		}

		//////

		[Intrinsic("atomic_add")]
		extern static float Add(ref float location, float value, AtomicOrdering ordering);
		[Intrinsic("atomic_add")]
		extern static double Add(ref double location, double value, AtomicOrdering ordering);
		[Intrinsic("atomic_add")]
		extern static uint8 Add(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_add")]
		extern static uint16 Add(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_add")]
		extern static uint32 Add(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_add")]
		extern static uint64 Add(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static T Add<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Add(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Add(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
				var result = Add(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Add(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T Add<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IFloating where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(float))
			{
				var result = Add(ref *(float*)&location, *(float*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(double))
			{
				var result = Add(ref *(double*)&location, *(double*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeAdd<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Add(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Add(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Add(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Add(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeAdd<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IFloating where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(float))
			{
				var result = Add(ref *(float*)&location, *(float*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(double))
			{
				var result = Add(ref *(double*)&location, *(double*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T Increment<T, TAtomicOrdering>(ref T location, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Add(ref *(uint8*)&location, 1, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Add(ref *(uint16*)&location, 1, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Add(ref *(uint32*)&location, 1, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Add(ref *(uint64*)&location, 1, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_and")]
		extern static float And(ref float location, float value, AtomicOrdering ordering);
		[Intrinsic("atomic_and")]
		extern static double And(ref double location, double value, AtomicOrdering ordering);
		[Intrinsic("atomic_and")]
		extern static uint8 And(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_and")]
		extern static uint16 And(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_and")]
		extern static uint32 And(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_and")]
		extern static uint64 And(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static T And<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = And(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = And(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
				var result = And(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = And(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeAnd<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = And(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = And(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = And(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = And(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_max")]
		extern static float Max(ref float location, float value, AtomicOrdering ordering);
		[Intrinsic("atomic_max")]
		extern static double Max(ref double location, double value, AtomicOrdering ordering);
		[Intrinsic("atomic_max")]
		extern static uint8 Max(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_max")]
		extern static uint16 Max(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_max")]
		extern static uint32 Max(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_max")]
		extern static uint64 Max(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static T Max<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Max(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Max(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
				var result = Max(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Max(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeMax<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Max(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Max(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Max(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Max(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_min")]
		extern static float Min(ref float location, float value, AtomicOrdering ordering);
		[Intrinsic("atomic_min")]
		extern static double Min(ref double location, double value, AtomicOrdering ordering);
		[Intrinsic("atomic_min")]
		extern static uint8 Min(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_min")]
		extern static uint16 Min(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_min")]
		extern static uint32 Min(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_min")]
		extern static uint64 Min(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static T Min<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Min(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Min(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
				var result = Min(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Min(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeMin<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Min(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Min(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Min(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Min(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_nand")]
		extern static float Nand(ref float location, float value, AtomicOrdering ordering);
		[Intrinsic("atomic_nand")]
		extern static double Nand(ref double location, double value, AtomicOrdering ordering);
		[Intrinsic("atomic_nand")]
		extern static uint8 Nand(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_nand")]
		extern static uint16 Nand(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_nand")]
		extern static uint32 Nand(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_nand")]
		extern static uint64 Nand(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static T Nand<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Nand(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Nand(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
				var result = Nand(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Nand(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeNand<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Nand(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Nand(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Nand(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Nand(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_or")]
		extern static float Or(ref float location, float value, AtomicOrdering ordering);
		[Intrinsic("atomic_or")]
		extern static double Or(ref double location, double value, AtomicOrdering ordering);
		[Intrinsic("atomic_or")]
		extern static uint8 Or(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_or")]
		extern static uint16 Or(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_or")]
		extern static uint32 Or(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_or")]
		extern static uint64 Or(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static T Or<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Or(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Or(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
				var result = Or(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Or(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeOr<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Or(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Or(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Or(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Or(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_sub")]
		extern static float Sub(ref float location, float value, AtomicOrdering ordering);
		[Intrinsic("atomic_sub")]
		extern static double Sub(ref double location, double value, AtomicOrdering ordering);
		[Intrinsic("atomic_sub")]
		extern static uint8 Sub(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_sub")]
		extern static uint16 Sub(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_sub")]
		extern static uint32 Sub(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_sub")]
		extern static uint64 Sub(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static T Sub<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Sub(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Sub(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Sub(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Sub(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T Sub<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IFloating where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(float))
			{
				var result = Sub(ref *(float*)&location, *(float*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(double))
			{
				var result = Sub(ref *(double*)&location, *(double*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeSub<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Sub(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Sub(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Sub(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Sub(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeSub<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IFloating where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(float))
			{
				var result = Sub(ref *(float*)&location, *(float*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(double))
			{
				var result = Sub(ref *(double*)&location, *(double*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T Decrement<T, TAtomicOrdering>(ref T location, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Sub(ref *(uint8*)&location, 1, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Sub(ref *(uint16*)&location, 1, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Sub(ref *(uint32*)&location, 1, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Sub(ref *(uint64*)&location, 1, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[Intrinsic("atomic_xor")]
		extern static float Xor(ref float location, float value, AtomicOrdering ordering);
		[Intrinsic("atomic_xor")]
		extern static double Xor(ref double location, double value, AtomicOrdering ordering);
		[Intrinsic("atomic_xor")]
		extern static uint8 Xor(ref uint8 location, uint8 value, AtomicOrdering ordering);
		[Intrinsic("atomic_xor")]
		extern static uint16 Xor(ref uint16 location, uint16 value, AtomicOrdering ordering);
		[Intrinsic("atomic_xor")]
		extern static uint32 Xor(ref uint32 location, uint32 value, AtomicOrdering ordering);
		[Intrinsic("atomic_xor")]
		extern static uint64 Xor(ref uint64 location, uint64 value, AtomicOrdering ordering);
		[Inline]
		public static T Xor<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Xor(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Xor(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
				var result = Xor(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Xor(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering | .Flag_ReturnModified);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}
		[Inline]
		public static T ExchangeXor<T, TAtomicOrdering>(ref T location, T value, TAtomicOrdering ordering = .SeqCst) where T : IInteger where TAtomicOrdering : const RMWAtomicOrdering
		{
			var value;
			if (sizeof(T) == sizeof(int8))
			{
			    var result = Xor(ref *(uint8*)&location, *(uint8*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int16))
			{
			    var result = Xor(ref *(uint16*)&location, *(uint16*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int32))
			{
			    var result = Xor(ref *(uint32*)&location, *(uint32*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else if (sizeof(T) == sizeof(int64))
			{
				var result = Xor(ref *(uint64*)&location, *(uint64*)&value, (AtomicOrdering)ordering);
				return *(T*)&result;
			}
			else
				FailOnDataSize();
		}

		[NoReturn]
		static void FailOnDataSize()
		{
			Runtime.FatalError("Data size not supported");
		}
	}
}
