using System;
using System.Reflection;
using System.Collections.Generic;
using System.Diagnostics;

namespace System
{
    interface IDisposable
    {
        void Dispose() mut;
    }

    interface IPrintable
	{
		void Print(String outString);
	}

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
    }

#if BF_ENABLE_OBJECT_DEBUG_FLAGS
	[AlwaysInclude]
#endif
	struct CallStackAddr : int
	{

	}

    class Object : IHashable
    {
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
        int mClassVData;
        int mDbgAllocInfo;
#else        
        ClassVData* mClassVData;
#endif        
    
        public virtual ~this()
        {
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			mClassVData = ((mClassVData & ~0x08) | 0x80);
#endif			
        }

        int IHashable.GetHashCode()
        {
            return (int)(void*)this;
        }

        public Type GetType()
        {
            Type type;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
            ClassVData* maskedVData = (ClassVData*)(void*)(mClassVData & ~(int)0xFF);
            type = maskedVData.mType;
#else            
            type = mClassVData.mType;
#endif            
            if ((type.[Friend]mTypeFlags & TypeFlags.Boxed) != 0)
            {
                //int32 underlyingType = (int32)((TypeInstance)type).mUnderlyingType;
                type = Type.[Friend]GetType(((TypeInstance)type).[Friend]mUnderlyingType);
            }
            return type;
        }

		[NoShow]
        Type RawGetType()
        {
            Type type;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
            ClassVData* maskedVData = (ClassVData*)(void*)(mClassVData & ~(int)0xFF);
            type = maskedVData.mType;
#else            
            type = mClassVData.mType;
#endif            
            return type;
        }

#if BF_ALLOW_HOT_SWAPPING
		[NoShow]
        public virtual Object DynamicCastToTypeId(int32 typeId)
        {
            if (typeId == (int32)RawGetType().[Friend]mTypeId)
                return this;
            return null;
        }

		[NoShow]
		public virtual Object DynamicCastToInterface(int32 typeId)
		{
		    return null;
		}
#endif
        
        public virtual void ToString(String strBuffer)
        {
            //strBuffer.Set(stack string(GetType().mName));
            RawGetType().GetName(strBuffer);
        }
        
        /*public virtual int GetHashCode()
        {
            return (int)(intptr)(void*)this;
        }*/

        [NoShow, SkipCall]
    	protected virtual void GCMarkMembers()
        {
            //PrintF("Object.GCMarkMembers %08X\n", this);
		}

		static void ToString(Object obj, String strBuffer)
		{
            if (obj == null)
                strBuffer.Append("null");
            else
                obj.ToString(strBuffer);
		}
    }
    
    interface IResult<T>
    {
        T GetBaseResult();
    }
        
    struct ValueType
    {
		public static extern bool Equals<T>(T val1, T val2);
	}

	struct Function : int
	{

	}

	[AlwaysInclude]
	struct Pointer : IHashable
	{
		void* mVal;

		public int GetHashCode()
		{
			return (int)mVal;
		}

		[AlwaysInclude]
		Object GetBoxed()
		{
			return new box this;
		}
	}

	struct Pointer<T> : IHashable
	{
		T* mVal;

		public int GetHashCode()
		{
			return (int)(void*)mVal;
		}
	}

	struct MethodReference<T>
	{
		T mVal;
	}

	struct SizedArray<T, CSize> where CSize : const int
	{
		T[CSize] mVal;

		public int Count
		{
			[Inline]
			get
			{
				return CSize;
			}
		}	

		public explicit static operator T[CSize] (Self val)
		{
			return val.mVal;
		}

		public override void ToString(String strBuffer) mut
		{
			if (typeof(T) == typeof(char8))
			{
				strBuffer.Append((char8*)&mVal, CSize);
				return;
			}

			strBuffer.Append('(');
			for (int i < CSize)
			{
				if (i != 0)
					strBuffer.Append(", ");
				mVal[i].ToString(strBuffer);
			}
			strBuffer.Append(')');
		}
	}

    struct Void : void
    {
    }
    
    struct Boolean : bool
    {        
    }
    
    struct Char8 : char8
    {
  		public bool IsWhiteSpace
		{
			get
			{
				switch (this)
				{
				case ' ', '\t', '\n', '\r', '\xa0', '\x85': return true;
				default: return false;
				}
			}
		}
    }

	struct Char16 : char16
	{
	    
	}

    struct Char32 : char32
    {
        public extern bool IsLower
		{
			get;
		}
    }
    
    struct Int8 : int8
    {        
    }
    
    struct UInt8 : uint8
    {        
    }
    
    struct Int16 : int16, IOpComparable, IIsNaN
    {
		public static int operator<=>(Int16 a, Int16 b)
		{
			return (int16)a <=> (int16)b;
		}

		bool IIsNaN.IsNaN
		{
			[SkipCall]
			get
			{
				return false;
			}
		}
    }
    
    struct UInt16 : uint16
    {        
    }
    
    struct UInt32 : uint32, IHashable, IOpComparable, IIsNaN, IOpNegatable
    {
        public const int32 MaxValue = 0x7FFFFFFF;
        public const int32 MinValue = -0x80000000;

		public static int operator<=>(UInt32 a, UInt32 b)
		{
			return (uint32)a <=> (uint32)b;
		}

		public static UInt32 operator-(UInt32 value)
		{
			if (value != 0)
				Runtime.FatalError("Cannot negate positive unsigned integer");
			return 0;
		}

		public this()
		{
			
		}

		bool IIsNaN.IsNaN
		{
			[SkipCall]
			get
			{
				return false;
			}
		}

		public int GetHashCode()
		{
			return (.)this;
		}
    }
    
    struct Int64 : int64
    {
        public const int64 MaxValue = 0x7FFFFFFFFFFFFFFFL;
        //public const long MinValue = -0x8000000000000000L;
        public const int64 MinValue = -0x7FFFFFFFFFFFFFFFL; //TODO: Should be one lower!

		public override void ToString(String strBuffer)
		{
		    // Dumb, make better.
		    char8[] strChars = scope:: char8[16];
		    int32 charIdx = 14;
		    int64 valLeft = (int64)this;
		    while (valLeft > 0)
		    {
		        strChars[charIdx] = (char8)('0' + (valLeft % 10));
		        valLeft /= 10;
		        charIdx--;
			}
		    if (charIdx == 14)
		        strChars[charIdx--] = '0';
		    char8* charPtr = &strChars[charIdx + 1];		   
		    strBuffer.Append(scope:: String(charPtr));
		}
    }
    
    struct UInt64 : uint64
    {
    }

    struct Float : float
    {
		public bool IsNegative
		{
			get
			{
				return this < 0;
			}
		}

		[StdCall, CLink]
		static extern int32 ftoa(float val, char8* str);

		static extern int32 ToString(float val, char8* str);

		public override void ToString(String strBuffer)
		{
			char8[128] outBuff = ?;
			//ftoa((float)this, &outBuff);
			int len = ToString((float)this, &outBuff);
			strBuffer.Append(&outBuff, len);
		}
    }

    struct Int : int, IOpComparable, IOpAddable, IOpDividable, IIsNaN
    {
		public static int operator<=>(Int a, Int b)
		{
			return (int)a <=> (int)b;
		}

		public static Int operator+(Int a, Int b)
		{
			return (int)a + (int)b;
		}

		public static Self operator/(Self a, Self b)
		{
			return (int)a / (int)b;
		}

		bool IIsNaN.IsNaN
		{
			[SkipCall]
			get
			{
				return false;
			}
		}
	}

    struct UInt : uint, IOpComparable, IIsNaN
    {        
        public static int operator<=>(UInt a, UInt b)
		{
			return (uint)a <=> (uint)b;
		}

		bool IIsNaN.IsNaN
		{
			[SkipCall]
			get
			{
				return false;
			}
		}
	}

    struct Int32 : int32, IHashable, IOpComparable, IIsNaN
    {
        public const int32 MaxValue = 0x7FFFFFFF;
        public const int32 MinValue = -0x80000000;

		public static int operator<=>(Int32 a, Int32 b)
		{
			return (int32)a <=> (int32)b;
		}

		public this()
		{
			this = (Int32)345;
		}

		bool IIsNaN.IsNaN
		{
			[SkipCall]
			get
			{
				return false;
			}
		}

		public int GetHashCode()
		{
			return (int32)this;
		}

        public int32 GetBaseResult()
        {
            return (int32)this + 4;
        }

        public override void ToString(String strBuffer)
        {
            // Dumb, make better.
            char8[] strChars = scope:: char8[16];
            int32 charIdx = 14;
            int32 valLeft = (int32)this;
            while (valLeft > 0)
            {
                strChars[charIdx] = (char8)('0' + (valLeft % 10));
                valLeft /= 10;
                charIdx--;
			}
            if (charIdx == 14)
                strChars[charIdx--] = '0';
            char8* charPtr = &strChars[charIdx + 1];		   
            strBuffer.Append(scope:: String(charPtr));
        }
    }
        
    struct Enum
    {
		public static Result<T> Parse<T>(String str) where T : Enum
		{
			var typeInst = (TypeInstance)typeof(T);
			for (var field in typeInst.GetFields())
			{
				if (str == field.[Friend]mFieldData.mName)
					return .Ok(*((T*)(&field.[Friend]mFieldData.mConstValue)));
			}

			return .Err;
		}
    }

    class Delegate
    {
        void* mFuncPtr;
        void* mTarget;

		public static bool Equals(Delegate a, Delegate b)
		{
			if ((Object)a == (Object)b)
				return true;
			if ((Object)a == null || (Object)b == null)
				return false;
			return (a.mFuncPtr == b.mFuncPtr) && (a.mTarget == b.mTarget);
		}

        public Result<void*> GetFuncPtr()
        {
			if (mTarget != null)
				return .Err;//("Delegate target method must be static");
            return mFuncPtr;
        }

		public void SetFuncPtr(void* ptr, void* target = null)
		{
			mTarget = target;
			mFuncPtr = ptr;
		}

		protected override void GCMarkMembers()
		{
			GC.Mark(Internal.UnsafeCastToObject(mTarget));
		}
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

	delegate void Action();
	delegate void Action<T1>(T1 p1);
	delegate void Action<T1, T2>(T1 p1, T2 p2);
	delegate TResult Func<TResult>();
	delegate TResult Func<T1, TResult>(T1 p1);
	delegate TResult Func<T1, T2, TResult>(T1 p1, T2 p2);
	delegate int32 Comparison<T>(T a, T b);
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

	public static mixin Try(var result)
	{
		if (result case .Err(var err))
			return .Err((.)err);
		result.Get()
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

	public static mixin DeleteContainerItems(var container)
	{
		if (container != null)
		{
			for (var value in container)
				delete value;
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
		Debug.FatalError();
	}

	public static mixin ScopedAlloc(int size, int align)
	{
		void* data;
		if (size <= 128)
		{
			data = scope:mixin uint8[size]* { ? };
		}
		else
		{
			data = new uint8[size]* { ? };
			defer:mixin delete data;
		}
		data
	}
}

