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
	            return (int32)mFieldData.mData;
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
	        void* dataAddr = ((uint8*)Internal.UnsafeCastToPtr(obj));
	        if (mTypeInstance.IsStruct)
	        {
	            Type boxedType = obj.[Friend]RawGetType();
	            bool typeMatched = false;
	            if (boxedType.IsBoxed)
				{
				    if (mTypeInstance == boxedType.UnderlyingType)
				    {
						dataAddr = (void*)((int)dataAddr + boxedType.[Friend]mMemberDataOffset);
						if (boxedType.IsBoxedStructPtr)
							dataAddr = *(void**)dataAddr;
				        typeMatched = true;
				    }
				}
	            if (!typeMatched)
	                return .Err(.InvalidTargetType); // "Invalid target type");
	        }
			dataAddr = (void*)((int)dataAddr + mFieldData.mData);

	        Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);

			if (value == null)
			{
				if ((fieldType.IsValueType) && (!fieldType.IsPointer))
				{
					return .Err(.InvalidValueType);
				}
				else
				{
					*((int*)dataAddr) = 0;
					return .Ok;
				}
			}

			Type rawValueType = value.[Friend]RawGetType();
			void* valueDataAddr = ((uint8*)Internal.UnsafeCastToPtr(value)) + rawValueType.[Friend]mMemberDataOffset;
			if (rawValueType.IsBoxedStructPtr)
				valueDataAddr = *(void**)valueDataAddr;
			
			Type valueType = value.GetType();

			if ((valueType != fieldType) && (valueType.IsTypedPrimitive))
				valueType = valueType.UnderlyingType;

			if (valueType == fieldType)
			{
				if (valueType.IsObject)
					*((void**)dataAddr) = Internal.UnsafeCastToPtr(value);
				else
					Internal.MemCpy(dataAddr, valueDataAddr, fieldType.[Friend]mSize);
			}
			else
			{
				return .Err(.InvalidValueType);
			}

	        return .Ok;
	    }
			
		public Result<void> SetValue(Object obj, Variant value)
		{    
			void* dataAddr = ((uint8*)Internal.UnsafeCastToPtr(obj));
		    if (mTypeInstance.IsStruct)
		    {
		        Type boxedType = obj.[Friend]RawGetType();
		        bool typeMatched = false;
		        if (boxedType.IsBoxed)
		        {
		            if (mTypeInstance == boxedType.UnderlyingType)
		            {
						dataAddr = (void*)((int)dataAddr + boxedType.[Friend]mMemberDataOffset);
						if (boxedType.IsBoxedStructPtr)
							dataAddr = *(void**)dataAddr;
		                typeMatched = true;
		            }
		        }
		        if (!typeMatched)
		            return .Err;//("Invalid target type");
		    }
			dataAddr =  (void*)((int)dataAddr + mFieldData.mData);

		    Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);

			let variantType = value.VariantType;
			if (variantType != fieldType)
			{
				if ((variantType.IsPointer) && (variantType.UnderlyingType == fieldType))
				{
					void* srcPtr = value.Get<void*>();
					Internal.MemCpy(dataAddr, srcPtr, fieldType.Size);
					return .Ok;
				}
				else
					return .Err;//("Invalid type");
			}

			value.CopyValueData(dataAddr);

		    return .Ok;
		}
	    
	    static mixin Decode<T2>(void* data)
	    {
	        *((*(T2**)&data)++)
	    }

		public Result<T> GetCustomAttribute<T>() where T : Attribute
		{
			return mTypeInstance.[Friend]GetCustomAttribute<T>(mFieldData.mCustomAttributesIdx);
		}

	    void* GetDataPtrAndType(Object value, out Type type)
	    {
	        type = value.[Friend]RawGetType();
	        /*if (type.IsStruct)
	            return &value;*/

			if (type.IsBoxedStructPtr)
				return *(void**)(((uint8*)Internal.UnsafeCastToPtr(value)) + type.[Friend]mMemberDataOffset);
	        if (type.IsBoxed)
	            return ((uint8*)Internal.UnsafeCastToPtr(value)) + type.[Friend]mMemberDataOffset;
	        return ((uint8*)Internal.UnsafeCastToPtr(value));
	    }

	    public Result<void> GetValue<TMember>(Object target, out TMember value)
	    {
	        value = default(TMember);

			void* targetDataAddr;
			if (target == null)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					// Unhandled
					return .Err;
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err;

				targetDataAddr = null;
			}
			else
			{
				Type tTarget;
				targetDataAddr = GetDataPtrAndType(target, out tTarget);

				if (!tTarget.IsSubtypeOf(mTypeInstance))
				    return .Err; //"Invalid type");
			}
			
	        Type tMember = typeof(TMember);

	        targetDataAddr = (uint8*)targetDataAddr + (int)mFieldData.mData;
	        
	        Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);

			if (tMember.[Friend]mTypeCode == TypeCode.Object)
			{
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

			void* targetDataAddr;
			if (target == null)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					return Variant.Create(FieldType, &mFieldData.mData);
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err;

				targetDataAddr = null;
			}
			else
			{
				Type tTarget;
				targetDataAddr = GetDataPtrAndType(target, out tTarget);

				if (!tTarget.IsSubtypeOf(mTypeInstance))
				    return .Err; //Invalid type;
			}

			targetDataAddr = (uint8*)targetDataAddr + (int)mFieldData.mData;

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

		public Result<Variant> GetValueReference(Object target)
		{
			Variant value = Variant();

			void* targetDataAddr;
			if (target == null)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					return Variant.Create(FieldType, &mFieldData.mData);
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err;

				targetDataAddr = null;
			}
			else
			{
				Type tTarget;
				targetDataAddr = GetDataPtrAndType(target, out tTarget);

				if (!tTarget.IsSubtypeOf(mTypeInstance))
				    return .Err; //Invalid type;
			}

			targetDataAddr = (uint8*)targetDataAddr + (int)mFieldData.mData;

			Type fieldType = Type.[Friend]GetType(mFieldData.mFieldTypeId);

			TypeCode typeCode = fieldType.[Friend]mTypeCode;
			if (typeCode == TypeCode.Enum)
				typeCode = fieldType.UnderlyingType.[Friend]mTypeCode;

			value = Variant.CreateReference(fieldType, targetDataAddr);

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
