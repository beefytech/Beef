using System.Collections;

namespace System.Reflection
{
	[CRepr, AlwaysInclude]
	public struct FieldInfo
	{
		public enum Error
		{
			InvalidTargetType,
			InvalidValueType
		}

	    TypeInstance mTypeInstance;
	    TypeInstance.FieldData* mFieldData;

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
	            return Type.[Friend]GetType(mFieldData.mFieldTypeId);
	        }
	    }

		public StringView Name
		{
			get
			{
				return mFieldData.mName;
			}
		}

	    public Result<void, Error> SetValue(Object obj, Object value)
	    {    
	        int32 dataOffsetAdjust = 0;
	        if (mTypeInstance.IsStruct)
	        {
	            Type boxedType = obj.[Friend]RawGetType();
	            bool typeMatched = false;
	            if (boxedType.IsBoxed)
	            {
	                if (mTypeInstance == boxedType.UnderlyingType)
	                {
	                    dataOffsetAdjust = boxedType.[Friend]mMemberDataOffset;
	                    typeMatched = true;
	                }
	            }
	            if (!typeMatched)
	                return .Err(.InvalidTargetType); // "Invalid target type");
	        }

	        Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);
	        void* fieldDataAddr = ((uint8*)(void*)obj) + mFieldData.mDataOffset + dataOffsetAdjust;

			Type rawValueType = value.[Friend]RawGetType();
			void* valueDataAddr = ((uint8*)(void*)value) + rawValueType.[Friend]mMemberDataOffset;
			
			Type valueType = value.GetType();

			if ((valueType != fieldType) && (valueType.IsTypedPrimitive))
				valueType = valueType.UnderlyingType;

			if (valueType == fieldType)
			{
				Internal.MemCpy(fieldDataAddr, valueDataAddr, fieldType.[Friend]mSize);
			}
			else
			{
				return .Err(.InvalidValueType);
			}

	        /*switch (fieldType.mTypeCode)
	        {
            case .Boolean:
				if (!value is bool)
				return .Err(.InvalidValueType);
				*(bool*)(uint8*)dataAddr = (.)value;
				break;        
	        case .Int32:
	            if (!value is int32)
	                return .Err(.InvalidValueType);
	            *(int32*)(uint8*)dataAddr = (.)value;
	            break;
	        default:
	            return .Err(.InvalidValueType);
	        }*/
	                  
	        return .Ok;
	    }
			
		public Result<void> SetValue(Object obj, Variant value)
		{    
		    int32 dataOffsetAdjust = 0;
		    if (mTypeInstance.IsStruct)
		    {
		        Type boxedType = obj.[Friend]RawGetType();
		        bool typeMatched = false;
		        if (boxedType.IsBoxed)
		        {
		            if (mTypeInstance == boxedType.UnderlyingType)
		            {
		                dataOffsetAdjust = boxedType.[Friend]mMemberDataOffset;
		                typeMatched = true;
		            }
		        }
		        if (!typeMatched)
		            return .Err;//("Invalid target type");
		    }

		    Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);
		    
		    void* dataAddr = ((uint8*)(void*)obj) + mFieldData.mDataOffset + dataOffsetAdjust;

			if (value.VariantType != fieldType)
				return .Err;//("Invalid type");

			value.CopyValueData(dataAddr);

		    return .Ok;
		}
	    
	    static mixin Decode<T2>(void* data)
	    {
	        *((*(T2**)&data)++)
	    }

		public Result<T> GetCustomAttribute<T>() where T : Attribute
		{
			/*if (mFieldData.mCustomAttributesIdx == -1)
			    return .Err;

			void* data = mTypeInstance.[Friend]mCustomAttrDataPtr[mFieldData.mCustomAttributesIdx];

			T attrInst = ?;
			switch (AttributeInfo.GetCustomAttribute(data, typeof(T), &attrInst))
			{
			case .Ok: return .Ok(attrInst);
			default:
				return .Err;
			}*/
			return .Err;
		}

	    void* GetDataPtrAndType(Object value, out Type type)
	    {
	        type = value.[Friend]RawGetType();
	        /*if (type.IsStruct)
	            return &value;*/

	        if (type.IsBoxed)
	            return ((uint8*)(void*)value) + type.[Friend]mMemberDataOffset;
	        return ((uint8*)(void*)value);
	    }

	    public Result<void> GetValue<TMember>(Object target, out TMember value)
	    {
	        value = default(TMember);

	        Type tTarget;
	        void* targetDataAddr = GetDataPtrAndType(target, out tTarget);
			
	        Type tMember = typeof(TMember);

	        targetDataAddr = (uint8*)targetDataAddr + mFieldData.mDataOffset;
	        
	        Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);

			if (tMember.[Friend]mTypeCode == TypeCode.Object)
			{
				if (!tTarget.IsSubtypeOf(mTypeInstance))
					Runtime.FatalError();
				value = *(TMember*)targetDataAddr;
			}
			else if (fieldType.[Friend]mTypeCode == tMember.[Friend]mTypeCode)
			{
				Internal.MemCpy(&value, targetDataAddr, tMember.Size);
			}
	        else
	        {
	            return .Err;
	        }
	        
	        return .Ok;
	    }

		public Result<Variant> GetValue(Object target)
		{
			Variant value = Variant();

			Type tTarget;
			void* targetDataAddr = GetDataPtrAndType(target, out tTarget);

			if (!tTarget.IsSubtypeOf(mTypeInstance))
			    Runtime.FatalError("Invalid type");   

			targetDataAddr = (uint8*)targetDataAddr + mFieldData.mDataOffset;

			Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);

			TypeCode typeCode = fieldType.[Friend]mTypeCode;
			if (typeCode == TypeCode.Enum)
				typeCode = fieldType.UnderlyingType.[Friend]mTypeCode;

		    if (typeCode == TypeCode.Object)
		    {
				value.[Friend]mStructType = 0;
		        value.[Friend]mData = *(int*)targetDataAddr;
		    }
			else
			{
				value = Variant.Create(fieldType, targetDataAddr);
			}

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

			Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);
			value.[Friend]mStructType = (int)(void*)fieldType;

			TypeCode typeCode = fieldType.[Friend]mTypeCode;
			if (typeCode == TypeCode.Enum)
				typeCode = fieldType.UnderlyingType.[Friend]mTypeCode;
			
		    if (typeCode == TypeCode.Int32)
		    {
		        *(int32*)&value.[Friend]mData = *(int32*)targetDataAddr;
		    }
			else if (typeCode == TypeCode.Object)
		    {
				value.[Friend]mStructType = 0;
		        value.[Friend]mData = (int)targetDataAddr;
		    }
		    else
		    {
		        return .Err;
		    }

			return value;
		}

	    public struct Enumerator : IEnumerator<FieldInfo>
	    {
			BindingFlags mBindingFlags;
	        TypeInstance mTypeInstance;
	        int32 mIdx;

	        public this(TypeInstance typeInst, BindingFlags bindingFlags)
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
					if (mIdx == mTypeInstance.[Friend]mFieldDataCount)
						return false;
					var fieldData = &mTypeInstance.[Friend]mFieldDataPtr[mIdx];
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
					var fieldData = &mTypeInstance.[Friend]mFieldDataPtr[mIdx];
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
