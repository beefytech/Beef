using System.Collections.Generic;

namespace System.Reflection
{
	[CRepr, AlwaysInclude]
	public struct FieldInfo
	{
	    internal TypeInstance mTypeInstance;
	    internal TypeInstance.FieldData* mFieldData;

	    public this(TypeInstance typeInstance, TypeInstance.FieldData* fieldData)
	    {
	        mTypeInstance = typeInstance;
	        mFieldData = fieldData;
	    }

	    public int32 MemberOffset
	    {
	        get
	        {
	            return mFieldData.mDataOffset;
	        }
	    }

	    public Type FieldType
	    {
	        get
	        {
	            return Type.GetType(mFieldData.mFieldTypeId);
	        }
	    }

		public StringView Name
		{
			get
			{
				return mFieldData.mName;
			}
		}

	    public Result<void> SetValue(Object obj, Object value)
	    {    
	        int32 dataOffsetAdjust = 0;
	        if (mTypeInstance.IsStruct)
	        {
	            Type boxedType = obj.RawGetType();
	            bool typeMatched = false;
	            if (boxedType.IsBoxed)
	            {
	                if (mTypeInstance == boxedType.UnderlyingType)
	                {
	                    dataOffsetAdjust = boxedType.mMemberDataOffset;
	                    typeMatched = true;
	                }
	            }
	            if (!typeMatched)
	                return .Err; // "Invalid target type");
	        }

	        Type fieldType = Type.GetType(mFieldData.mFieldTypeId);
	        //Type objType = obj.GetType();
	        
	        void* dataAddr = ((uint8*)(void*)obj) + mFieldData.mDataOffset + dataOffsetAdjust;

	        switch (fieldType.mTypeCode)
	        {                             
	        case TypeCode.Int32:
	            if (!value is int32)
	                return .Err; //("Invalid type");
	            *(int32*)(uint8*)dataAddr = (int32)value;
	            break;
	        default:
	            return .Err; //("Invalid type");
	        }
	                  
	        return .Ok;
	    }

		public Result<void> SetValue(Object obj, Variant value)
		{    
		    int32 dataOffsetAdjust = 0;
		    if (mTypeInstance.IsStruct)
		    {
		        Type boxedType = obj.RawGetType();
		        bool typeMatched = false;
		        if (boxedType.IsBoxed)
		        {
		            if (mTypeInstance == boxedType.UnderlyingType)
		            {
		                dataOffsetAdjust = boxedType.mMemberDataOffset;
		                typeMatched = true;
		            }
		        }
		        if (!typeMatched)
		            return .Err;//("Invalid target type");
		    }

		    Type fieldType = Type.GetType(mFieldData.mFieldTypeId);
		    //Type objType = obj.GetType();
		    
		    void* dataAddr = ((uint8*)(void*)obj) + mFieldData.mDataOffset + dataOffsetAdjust;

			if (value.VariantType != fieldType)
				return .Err;//("Invalid type");

			value.CopyValueData(dataAddr);

			//TypeCode typeCode = fieldType.mTypeCode;

			/*if (typeCode == TypeCode.Enum)
				typeCode = fieldType.GetUnderlyingType().mTypeCode;

		    switch (typeCode)
		    {                             
		    case TypeCode.Int32:
		        *(int32*)dataAddr = value.Get<int32>();
		        break;
			case TypeCode.Boolean:
				*(bool*)dataAddr = value.Get<bool>();
				break;
			case TypeCode.Object:
				*(Object*)dataAddr = value.Get<Object>();
				break;
		    default:
		        return .Err;//("Invalid type");
		    }*/
		              
		    return .Ok;
		}
	    
	    static mixin Decode<T2>(void* data)
	    {
	        *((*(T2**)&data)++)
	    }

		public Result<T> GetCustomAttribute<T>() where T : Attribute
		{
			return .Err;

			/*if (mFieldData.mCustomAttributesIdx == -1)
			    return .Err;

			void* data = mTypeInstance.mCustomAttrDataPtr[mFieldData.mCustomAttributesIdx];

			T attrInst = ?;
			switch (AttributeInfo.GetCustomAttribute(data, typeof(T), &attrInst))
			{
			case .Ok: return .Ok(attrInst);
			default:
				return .Err;
			}*/
		}

	    void* GetDataPtrAndType(Object value, out Type type)
	    {
	        type = value.RawGetType();
	        /*if (type.IsStruct)
	            return &value;*/

			if (type.IsStruct)
			{
				NOP!();
			}	

	        if (type.IsBoxed)
	            return ((uint8*)(void*)value) + type.mMemberDataOffset;
	        return ((uint8*)(void*)value);
	    }

	    public Result<void> GetValue<TMember>(Object target, out TMember value)
	    {
	        value = default(TMember);

	        Type tTarget;
	        void* targetDataAddr = GetDataPtrAndType(target, out tTarget);
			//Type tTarget = target.RawGetType();
			//void* targetDataAddr = (void*)&target;
	        
	        Type tMember = typeof(TMember);

	        targetDataAddr = (uint8*)targetDataAddr + mFieldData.mDataOffset;
	        
	        Type fieldType = Type.GetType(mFieldData.mFieldTypeId);

			if (tMember.mTypeCode == TypeCode.Object)
			{
				if (!tTarget.IsSubtypeOf(mTypeInstance))
					Runtime.FatalError();
				value = *(TMember*)targetDataAddr;
			}
	        else if (tMember.mTypeCode == TypeCode.Int32)
	        {
	            if (fieldType.mTypeCode == TypeCode.Int32)
	            {
	                if (tMember.mTypeCode != TypeCode.Int32)
	                    Runtime.FatalError("Expected int");
	                *(int32*)&value = *(int32*)targetDataAddr;
	            }
	            else
	            {
	                return .Err;//("Invalid type");
	            }
	        }
	        else
	        {
	            return .Err;//("Invalid type");
	        }
	        
	        return .Ok;
	    }

		public Result<Variant> GetValue(Object target)
		{
			Variant value = Variant();

			Type tTarget;
			void* targetDataAddr = GetDataPtrAndType(target, out tTarget);
			//Type tTarget = target.RawGetType();
			//void* targetDataAddr = (void*)&target;

			if (!tTarget.IsSubtypeOf(mTypeInstance))
			    Runtime.FatalError("Invalid type");   

			targetDataAddr = (uint8*)targetDataAddr + mFieldData.mDataOffset;

			Type fieldType = Type.GetType(mFieldData.mFieldTypeId);

			/*if (fieldType.IsNullable)
			{
				var specializedType = (SpecializedGenericType)fieldType;
				var genericArg = specializedType.GetGenericArg(0);

				bool hasValue = *(bool*)((uint8*)targetDataAddr + genericArg.mSize);
				if (!hasValue)
					return .Err;
				fieldType = genericArg;
			}*/

			//value.mStructType = (int)(void*)fieldType;

			TypeCode typeCode = fieldType.mTypeCode;
			if (typeCode == TypeCode.Enum)
				typeCode = fieldType.UnderlyingType.mTypeCode;

		    /*if (typeCode == TypeCode.Int32)
		    {
		        *(int32*)&value.mData = *(int32*)targetDataAddr;
		    }
			else if (typeCode == TypeCode.Boolean)
			{
			    *(bool*)&value.mData = *(bool*)targetDataAddr;
			}
			else */if (typeCode == TypeCode.Object)
		    {
				value.mStructType = 0;
		        value.mData = *(int*)targetDataAddr;
		    }
			else
			{
				value = Variant.Create(fieldType, targetDataAddr);
			}

		    /*else if (fieldType.mSize <= sizeof(int))
			{
				value.mStructType = (int)(void*)fieldType;
				Internal.MemCpy(&value.mData, targetDataAddr, fieldType.mSize);
			}
			else
			{
				value.mStructType = (int)(void*)fieldType;
				void* data = new uint8[fieldType.mSize]*;
				Internal.MemCpy(data, targetDataAddr, fieldType.mSize);
				value.mData = (int)data;
			}*/
		    /*{
		        return .Err;
		    }*/

			return value;
		}

		public Result<Variant> GetValue()
		{
			Variant value = Variant();

			//TODO: Assert static

			if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
			{
				return Variant.Create(FieldType, &mFieldData.mConstValue);
			}

			ThrowUnimplemented();

			//Type tTarget;
#unwarn
			void* targetDataAddr = (void*)(int)mFieldData.mConstValue;

			Type fieldType = Type.GetType(mFieldData.mFieldTypeId);
			value.mStructType = (int)(void*)fieldType;

			TypeCode typeCode = fieldType.mTypeCode;
			if (typeCode == TypeCode.Enum)
				typeCode = fieldType.UnderlyingType.mTypeCode;
			
		    if (typeCode == TypeCode.Int32)
		    {
		        *(int32*)&value.mData = *(int32*)targetDataAddr;
		    }
			else if (typeCode == TypeCode.Object)
		    {
				value.mStructType = 0;
		        value.mData = (int)targetDataAddr;
		    }
		    else
		    {
		        return .Err;
		    }

			return value;
		}

	    internal struct Enumerator : IEnumerator<FieldInfo>
	    {
			BindingFlags mBindingFlags;
	        TypeInstance mTypeInstance;
	        int32 mIdx;

	        internal this(TypeInstance typeInst, BindingFlags bindingFlags)
	        {
	            mTypeInstance = typeInst;
				mBindingFlags = bindingFlags;
	            mIdx = -1;
	        }

	        public void Reset() mut
	        {
	            mIdx = -1;
	        }

	        public void Dispose()
	        {
	        }

	        public bool MoveNext() mut
	        {
				if (mTypeInstance == null)
					return false;

				for (;;)
				{
					mIdx++;
					if (mIdx == mTypeInstance.mFieldDataCount)
						return false;
					var fieldData = &mTypeInstance.mFieldDataPtr[mIdx];
					bool matches = (mBindingFlags.HasFlag(BindingFlags.Static) && (fieldData.mFlags.HasFlag(FieldFlags.Static)));
					matches |= (mBindingFlags.HasFlag(BindingFlags.Instance) && (!fieldData.mFlags.HasFlag(FieldFlags.Static)));
					if (matches)
						break;
				}
	            return true;
	        }

	        public FieldInfo Current
	        {
	            get
	            {
					var fieldData = &mTypeInstance.mFieldDataPtr[mIdx];
	                return FieldInfo(mTypeInstance, fieldData);
	            }
	        }

			public Result<FieldInfo> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
	    }
	}
}
