using System;
using System.Collections.Generic;

namespace System
{
	// Collection size type
#if BF_LARGE_COLLECTIONS
	typealias int_cosize = int64;
#else
	typealias int_cosize = int32;
#endif

	[AlwaysInclude]
	static class CompilerSettings
	{
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
	    public const bool cHasDebugFlags = true;
#else
	    public const bool cHasDebugFlags = false;
#endif
	    public const bool cHasVDataExtender = true;
	    public const int32 cVDataIntefaceSlotCount = 16;

#if BF_LARGE_STRINGS
		public const bool cHasLargeStrings = true;
#else
		public const bool cHasLargeStrings = false;
#endif

#if BF_LARGE_COLLECTIONS
		public const bool cHasLargeCollections = true;
#else
		public const bool cHasLargeCollections = false;
#endif

		static this()
		{
			// This ensures this gets included in vdata
		}
	}

#if BF_ENABLE_OBJECT_DEBUG_FLAGS
	[AlwaysInclude]
#endif
	struct CallStackAddr : int
	{

	}

	interface IDisposable
	{
	    void Dispose() mut;
	}

	struct DeferredCall
	{
		public int64 mMethodId;
	    public DeferredCall* mNext;
	    
		public void Cancel() mut
		{
			mMethodId = 0;
		}
	}
}

static
{
	public static mixin NOP()
	{
	}

	public static mixin ToStackString(var obj)
	{
		var str = scope:: String();
		obj.ToString(str);
		str
	}

	public static mixin StringAppend(var str, var str1, var str2)
	{
		str.Clear();
		str.Append(str1);
		str.Append(str2);
		str
	}

	public static mixin StringAppend(var str, var str1, var str2, var str3)
	{
		str.Clear();
		str.Append(str1);
		str.Append(str2);
		str.Append(str3);
		str
	}

	/*static mixin StackStringFormat(String format, params Object[] args)
	{
		var str = stack String();
		str.AppendF(format, args);
	}*/

	public static mixin StackStringFormat(String format, var arg1)
	{
		var str = scope:: String();
		str.AppendF(format, arg1);
		str
	}

	public static mixin StackStringFormat(String format, var arg1, var arg2)
	{
		var str = scope:: String();
		str.AppendF(format, arg1, arg2);
		str
	}

	public static mixin StackStringFormat(String format, var arg1, var arg2, var arg3)
	{
		var str = scope:: String();
		str.AppendF(format, arg1, arg2, arg3);
		str
	}

	public static mixin Try(var result)
	{
		if (result case .Err(var err))
			return .Err((.)err);
		result.Get()
	}

	public static mixin TrySilent(var result)
	{
		if (result case .Err)
			return default;
		result.Get()
	}

	public static mixin Swap(var a, var b)
	{
		let swap = a;
		a = b;
		b = swap;
	}

	public static mixin DeleteContainerAndItems(var container)
	{
		if (container != null)
		{
			for (var value in container)
				delete value;
			delete container;
		}
	}

	public static mixin DeleteAndClearItems(var container)
	{
		for (var value in container)
			delete value;
		container.Clear();
	}

	public static void ClearAndDeleteItems<T>(List<T> container) where T : var
	{
		for (var value in container)
			delete value;
		container.Clear();
	}

	public static mixin DeleteDictionyAndKeys(var container)
	{
		if (container != null)
		{
			for (var value in container)
			{
				delete value.key;
			}
			delete container;
		}
	}

	public static mixin DeleteDictionyAndKeysAndItems(var container)
	{
		if (container != null)
		{
			for (var value in container)
			{
				delete value.key;
				delete value.value;
			}
			delete container;
		}
	}

	public static mixin DeleteAndNullify(var val)
	{
		delete val;
		val = null;
	}

	[NoReturn]
	public static void ThrowUnimplemented()
	{
		Runtime.FatalError("Unimplemented");
	}

	public static mixin ScopedAlloc(int size, int align)
	{
		void* data;
		if (size <= 128)
		{
			data = scope:mixin [Align(align)] uint8[size]* { ? };
		}
		else
		{
			data = new [Align(align)] uint8[size]* { ? };
			defer:mixin delete data;
		}
		data
	}
}