using System;
using System.Reflection;
using System.Collections;
using System.Diagnostics;

namespace System
{
    class Object : IHashable
    {
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
        int mClassVData;
        int mDbgAllocInfo;
#else
        ClassVData* mClassVData;
#endif

		[AlwaysInclude]
        public virtual ~this()
        {
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			mClassVData = ((mClassVData & ~0x08) | 0x80);
#endif
        }

#if BF_ENABLE_OBJECT_DEBUG_FLAGS
		[NoShow]
		int32 GetFlags()
		{
			return (int32)mClassVData & 0xFF;
		}

        [DisableObjectAccessChecks, NoShow]
        public bool IsDeleted()
        {
            return (int32)mClassVData & 0x80 != 0;
        }
#else
        [SkipCall]
        public bool IsDeleted()
        {
            return false;
        }
#endif
		extern Type Comptime_GetType();

        public Type GetType()
        {
			if (Compiler.IsComptime)
				return Comptime_GetType();

			ClassVData* classVData;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			classVData = (ClassVData*)(void*)(mClassVData & ~(int)0xFF);
#else
			classVData = mClassVData;
#endif

#if BF_32_BIT
			Type type = Type.[Friend]GetType_(classVData.mType2);
#else
			Type type = Type.[Friend]GetType_((.)(classVData.mType >> 32));
#endif
            return type;
        }

        TypeId GetTypeId()
        {
			ClassVData* classVData;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
            classVData = (ClassVData*)(void*)(mClassVData & ~(int)0xFF);
#else
			classVData = mClassVData;
#endif

#if BF_32_BIT
			return (.)classVData.mType2;
#else
			return (.)(classVData.mType >> 32);
#endif
        }

		[NoShow]
        Type RawGetType()
        {
			if (Compiler.IsComptime)
				return Comptime_GetType();

			ClassVData* classVData;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			classVData = (ClassVData*)(void*)(mClassVData & ~(int)0xFF);
#else
			classVData = mClassVData;
#endif

#if BF_32_BIT
			Type type = Type.[Friend]GetType_(classVData.mType);
#else
			Type type = Type.[Friend]GetType_((.)(classVData.mType & 0xFFFFFFFF));
#endif
			return type;
        }

		TypeId RawGetTypeId()
		{
			ClassVData* classVData;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			classVData = (ClassVData*)(void*)(mClassVData & ~(int)0xFF);
#else
			classVData = mClassVData;
#endif

#if BF_32_BIT
			return (.)classVData.mType;
#else
			return (.)(classVData.mType & 0xFFFFFFFF);
#endif
		}

#if BF_DYNAMIC_CAST_CHECK || BF_ENABLE_REALTIME_LEAK_CHECK
		[NoShow]
		public virtual Object DynamicCastToTypeId(int32 typeId)
		{
		    if (typeId == (.)RawGetTypeId())
		        return this;
		    return null;
		}

		[NoShow]
		public virtual Object DynamicCastToInterface(int32 typeId)
		{
		    return null;
		}
#endif

        int IHashable.GetHashCode()
        {
            return (int)Internal.UnsafeCastToPtr(this);
        }

        public virtual void ToString(String strBuffer)
        {
#if BF_REFLECT_MINIMAL
			strBuffer.AppendF($"Type#{(Int.Simple)GetTypeId()}@0x");
			NumberFormatter.AddrToString((uint)Internal.UnsafeCastToPtr(this), strBuffer);
#else
			let t = RawGetType();
			if (t.IsBoxedStructPtr)
			{
				let ti = (TypeInstance)t;
				let innerPtr = *(void**)((uint8*)Internal.UnsafeCastToPtr(this) + ti.[Friend]mMemberDataOffset);
				strBuffer.Append("(");
				ti.UnderlyingType.GetFullName(strBuffer);
				//strBuffer.AppendF("*)0x{0:A}", (UInt.Simple)(uint)(void*)innerPtr);
				strBuffer.Append("*)0x");
				NumberFormatter.AddrToString((uint)(void*)innerPtr, strBuffer);
				return;
			}
            t.GetFullName(strBuffer);
			strBuffer.Append("@0x");
			NumberFormatter.AddrToString((uint)Internal.UnsafeCastToPtr(this), strBuffer);
#endif
        }

		private static void ToString(Object obj, String strBuffer)
		{
			if (obj == null)
				strBuffer.Append("null");
			else
				obj.ToString(strBuffer);
		}

        [SkipCall, NoShow]
    	protected virtual void GCMarkMembers()
        {
            //PrintF("Object.GCMarkMembers %08X\n", this);
		}
    }
}

