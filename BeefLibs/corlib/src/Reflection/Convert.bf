namespace System.Reflection
{
	static class Convert
	{
		static (Type type, void* ptr) GetTypeAndPointer(Object obj)
		{
			var objType = obj.[Friend]RawGetType();
			void* dataPtr = (uint8*)Internal.UnsafeCastToPtr(obj) + objType.[Friend]mMemberDataOffset;
			if (objType.IsBoxed)
				objType = objType.UnderlyingType;
			if (objType.IsTypedPrimitive)
				objType = objType.UnderlyingType;
			return (objType, dataPtr);
		}

		public static Result<int64> ToInt64(Object obj)
		{
			var (objType, dataPtr) = GetTypeAndPointer(obj);
			switch (objType.[Friend]mTypeCode)
			{
			case .Int8: return *(int8*)dataPtr;
			case .Int16: return *(int16*)dataPtr;
			case .Int32: return *(int32*)dataPtr;
			case .Int64: return *(int64*)dataPtr;
			case .UInt8, .Char8: return (int64)*(uint8*)dataPtr;
			case .UInt16, .Char16: return *(uint16*)dataPtr;
			case .UInt32, .Char32: return *(uint32*)dataPtr;
			case .UInt64: return (int64)*(uint64*)dataPtr;
			case .Int: return (int64)*(int*)dataPtr;
			case .UInt: return (int64)*(uint*)dataPtr;
			default: return .Err;
			}
		}

		public static Result<int64> ToInt64(Variant variant)
		{
			var variant;
			var dataPtr = variant.DataPtr;
			switch (variant.VariantType.[Friend]mTypeCode)
			{
			case .Int8: return *(int8*)dataPtr;
			case .Int16: return *(int16*)dataPtr;
			case .Int32: return *(int32*)dataPtr;
			case .Int64: return *(int64*)dataPtr;
			case .UInt8, .Char8: return *(uint8*)dataPtr;
			case .UInt16, .Char16: return *(uint16*)dataPtr;
			case .UInt32, .Char32: return *(uint32*)dataPtr;
			case .UInt64: return (int64)*(uint64*)dataPtr;
			case .Int: return (int64)*(int*)dataPtr;
			case .UInt: return (int64)*(uint*)dataPtr;
			default: return .Err;
			}
		}

		public static bool IntCanFit(int64 val, Type type)
		{
			switch (type.[Friend]mTypeCode)
			{
			case .Int8: return (val >= -0x80) && (val <= 0x7F);
			case .Int16: return (val >= -0x8000) && (val <= 0x7FFF);
			case .Int32: return (val >= -0x80000000) && (val <= 0x7FFF'FFFF);
			case .Int64: return true;
			case .UInt8, .Char8: return (val >= 0) && (val <= 0xFF);
			case .UInt16, .Char16: return (val >= 0) && (val <= 0xFFFF);
			case .UInt32, .Char32: return (val >= 0) && (val <= 0xFFFFFFFF);
			case .UInt64: return (val >= 0);
#if BF_64_BIT
			case .Int: return true;
			case .UInt: return (val >= 0);
#else
			case .Int: return (val >= -0x80000000) && (val <= 0x7FFF'FFFF);
			case .UInt: return (val >= 0) && (val <= 0xFFFFFFFF);
#endif
			default: return false;
			}
		}

		public static Result<Variant> ConvertTo(Variant variant, Type type)
		{
			var variant;

			if (variant.VariantType == type)
			{
				return Variant.CreateFromVariant(variant);
			}

			var varType = variant.VariantType;
			void* dataPtr = variant.DataPtr;

			if (varType.IsPrimitive)
			{
				if (varType.IsInteger)
				{
					int64 intVal = ToInt64(variant);
					switch (type.[Friend]mTypeCode)
					{
					case .Boolean:
						bool val = intVal != 0;
						return Variant.Create(type, &val);
					case .Float:
						float val = (.)intVal;
						return Variant.Create(type, &val);
					case .Double:
						double val = (.)intVal;
						return Variant.Create(type, &val);
					default:
					}

					if (IntCanFit(intVal, type.IsTypedPrimitive ? type.UnderlyingType : type))
					{
						return Variant.Create(type, &intVal);
					}
				}
				else if (varType.IsFloatingPoint)
				{
					if ((type.[Friend]mTypeCode == .Double) &&
						(varType.[Friend]mTypeCode == .Float))
					{
						double val = (.)*(float*)dataPtr;
						return Variant.Create(type, &val);
					}
				}
			}

			return .Err;
		}

		public static Result<Variant> ConvertTo(Object obj, Type type)
		{
			if (obj.GetType() == type)
			{
				return Variant.Create(obj, false);
			}

			var (objType, dataPtr) = GetTypeAndPointer(obj);

			if (objType == type)
			{
				return Variant.Create(type, dataPtr);
			}

			if (objType.IsPrimitive)
			{
				if (objType.IsInteger)
				{
					int64 intVal = ToInt64(obj);
					switch (type.[Friend]mTypeCode)
					{
					case .Boolean:
						bool val = intVal != 0;
						return Variant.Create(type, &val);
					case .Float:
						float val = (.)intVal;
						return Variant.Create(type, &val);
					case .Double:
						double val = (.)intVal;
						return Variant.Create(type, &val);
					default:
					}

					if (IntCanFit(intVal, type.IsTypedPrimitive ? type.UnderlyingType : type))
					{
						return Variant.Create(type, &intVal);
					}
				}
				else if (objType.IsFloatingPoint)
				{
					if ((type.[Friend]mTypeCode == .Double) &&
						(objType.[Friend]mTypeCode == .Float))
					{
						double val = (.)*(float*)dataPtr;
						return Variant.Create(type, &val);
					}
				}
			}

			if (var unspecializedType = type as SpecializedGenericType)
			{
				if (unspecializedType.UnspecializedType == typeof(Nullable<>))
				{
					switch (ConvertTo(obj, unspecializedType.GetGenericArg(0)))
					{
					case .Ok(var ref val):
						Variant.Alloc(type, var nullableVariant);
						Internal.MemCpy(nullableVariant.DataPtr, val.DataPtr, val.VariantType.Size);
						*((bool*)nullableVariant.DataPtr + val.VariantType.Size) = true;
						return nullableVariant;
					case .Err:
						return .Err;
					}
				}
			}

			

			return .Err;
		}
	}
}
