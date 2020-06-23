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

#if BF_DYNAMIC_CAST_CHECK || BF_ENABLE_REALTIME_LEAK_CHECK
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

        int IHashable.GetHashCode()
        {
            return (int)Internal.UnsafeCastToPtr(this);
        }
        
        public virtual void ToString(String strBuffer)
        {
            //strBuffer.Set(stack string(GetType().mName));
            RawGetType().GetName(strBuffer);
			strBuffer.Append("@");
			((int)Internal.UnsafeCastToPtr(this)).ToString(strBuffer, "X", null);
        }
                
        [SkipCall, NoShow]
    	protected virtual void GCMarkMembers()
        {
            //PrintF("Object.GCMarkMembers %08X\n", this);
		}
    }
}

