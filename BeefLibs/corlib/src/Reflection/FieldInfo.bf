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

		public TypeInstance DeclaringType => mTypeInstance;
	    public int32 MemberOffset => (int32)mFieldData.mData;
	    public Type FieldType => Type.[Friend]GetType(mFieldData.mFieldTypeId);
		public bool IsConst => mFieldData.mFlags.HasFlag(.Const);
		public bool IsStatic => mFieldData.mFlags.HasFlag(.Static);
		public bool IsInstanceField => !mFieldData.mFlags.HasFlag(.Static) && !mFieldData.mFlags.HasFlag(.Const);
		public StringView Name => mFieldData.mName;

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
			
		public Result<void, Error> SetValue(Object obj, Variant value)
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
		            return .Err(.InvalidTargetType);
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
					return .Err(.InvalidValueType);
			}

			value.CopyValueData(dataAddr);

		    return .Ok;
		}

		public Result<void, Error> SetValue(Variant target, Object value)
		{    
		   	var target;
			var targetType = target.VariantType;
			void* dataAddr = target.DataPtr;
			if (targetType != mTypeInstance)
			{
				if ((!targetType.IsPointer) || (targetType.UnderlyingType.IsSubtypeOf(mTypeInstance)))
					return .Err(.InvalidTargetType); // Invalid target type
				dataAddr = target.Get<void*>();
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

		public Result<void, Error> SetValue(Variant target, Variant value)
		{
			var target;
			var targetType = target.VariantType;
			void* dataAddr = target.DataPtr;
			if (targetType != mTypeInstance)
			{
				if ((!targetType.IsPointer) || (targetType.UnderlyingType.IsSubtypeOf(mTypeInstance)))
					return .Err(.InvalidTargetType);
				dataAddr = target.Get<void*>();
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
					return .Err(.InvalidValueType);
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
			if (type.IsBoxedStructPtr)
				return *(void**)(((uint8*)Internal.UnsafeCastToPtr(value)) + type.[Friend]mMemberDataOffset);
	        if (type.IsBoxed)
	            return ((uint8*)Internal.UnsafeCastToPtr(value)) + type.[Friend]mMemberDataOffset;
	        return ((uint8*)Internal.UnsafeCastToPtr(value));
	    }

	    public Result<void, Error> GetValue<TMember>(Object target, out TMember value)
	    {
	        value = default(TMember);

			void* targetDataAddr;
			if (target == null)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					// Unhandled
					return .Err(.InvalidTargetType);
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err(.InvalidTargetType);

				targetDataAddr = null;
			}
			else
			{
				Type tTarget;
				targetDataAddr = GetDataPtrAndType(target, out tTarget);

				if (!tTarget.IsSubtypeOf(mTypeInstance))
				    return .Err(.InvalidTargetType); //"Invalid type");
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
	            return .Err(.InvalidValueType);
	        }
	        
	        return .Ok;
	    }

		Result<Variant> GetValue(void* startTargetDataAddr, Type tTarget)
		{
			Variant value = Variant();

			void* targetDataAddr = startTargetDataAddr;
			if (targetDataAddr == null)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					return Variant.Create(FieldType, &mFieldData.mData);
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err;
			}
			else
			{
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

		public Result<Variant> GetValueReference(void* startTargetDataAddr, Type tTarget)
		{
			Variant value = Variant();

			void* targetDataAddr = startTargetDataAddr;
			if (targetDataAddr == null)
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

		public Result<Variant> GetValue(Object target)
		{
			void* targetDataAddr;
			if (target == null)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					return Variant.Create(FieldType, &mFieldData.mData);
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err;

				return GetValue(null, null);
			}
			else
			{
				Type tTarget;
				targetDataAddr = GetDataPtrAndType(target, out tTarget);
				return GetValue(targetDataAddr, tTarget);
			}
		}

		public Result<Variant> GetValue(Variant target)
		{
			if (!target.HasValue)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					return Variant.Create(FieldType, &mFieldData.mData);
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err;

				return GetValue(null, null);
			}
			else
			{
				var target;
				return GetValue(target.DataPtr, target.VariantType);
			}
		}

		public Result<Variant> GetValueReference(Object target)
		{
			void* targetDataAddr;
			if (target == null)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					return Variant.Create(FieldType, &mFieldData.mData);
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err;

				return GetValueReference(null, null);
			}
			else
			{
				Type tTarget;
				targetDataAddr = GetDataPtrAndType(target, out tTarget);
				return GetValueReference(targetDataAddr, tTarget);
			}
		}

		public Result<Variant> GetValueReference(Variant target)
		{
			if (!target.HasValue)
			{
				if (mFieldData.mFlags.HasFlag(FieldFlags.Const))
				{
					return Variant.Create(FieldType, &mFieldData.mData);
				}

				if (!mFieldData.mFlags.HasFlag(FieldFlags.Static))
					return .Err;

				return GetValueReference(null, null);
			}
			else
			{
				var target;
				return GetValueReference(target.DataPtr, target.VariantType);
			}
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
					{
						if (mBindingFlags.HasFlag(.DeclaredOnly))
							return false;
						if (mTypeInstance.[Friend]mBaseType == 0)
							return false;
						mTypeInstance = Type.[Friend]GetType(mTypeInstance.[Friend]mBaseType) as TypeInstance;
						mIdx = -1;
						continue;
					}
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

			public int32 Index
			{
				get
				{
					return mIdx;
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
