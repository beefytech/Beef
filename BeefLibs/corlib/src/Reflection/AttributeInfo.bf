namespace System.Reflection
{
	class AttributeInfo
	{
		static mixin Decode<T2>(void* data)
		{
		    *((*(T2**)&data)++)
		}

		public static bool HasCustomAttribute(void* inAttrData, Type attributeType)
		{
			TypeId findTypeId = attributeType.[Friend]mTypeId;

			void* data = inAttrData;
			data++;

			uint8 count = Decode!<uint8>(data);
			for (int32 attrIdx = 0; attrIdx < count; attrIdx++)
			AttrBlock:
			{
			    void* startPtr = data;
			    var len = Decode!<uint16>(data);
			    void* endPtr = (uint8*)startPtr + len;

			    var typeId = Decode!<TypeId>(data);
			    if (typeId == findTypeId)
					return true;
			    
		        data = endPtr;
			}

			return false;
		}

		public static Result<void> GetCustomAttribute(void* inAttrData, Type attributeType, Object targetAttr)
		{
			TypeId findTypeId = attributeType.[Friend]mTypeId;

			void* data = inAttrData;
			data++;

			uint8 count = Decode!<uint8>(data);
			for (int32 attrIdx = 0; attrIdx < count; attrIdx++)
			AttrBlock:
			{
			    void* startPtr = data;
			    var len = Decode!<uint16>(data);
			    void* endPtr = (uint8*)startPtr + len;

			    var typeId = Decode!<TypeId>(data);
			    if (typeId != findTypeId)
			    {
			        data = endPtr;
			        continue;
			    }
			    
			    var methodIdx = Decode!<uint16>(data);
			    
			    Type attrType = Type.[Friend]GetType(typeId);
			    TypeInstance attrTypeInst = attrType as TypeInstance;
				MethodInfo methodInfo = .(attrTypeInst, attrTypeInst.[Friend]mMethodDataPtr + methodIdx);

			    Object[] args = scope Object[methodInfo.[Friend]mMethodData.mParamCount];

				int argIdx = 0;
			    while (data < endPtr)
			    {
			        var attrDataType = Decode!<TypeCode>(data);
					switch (attrDataType)
					{
					case .Int8,
						 .UInt8,
						 .Char8,
						 .Boolean:
						let attrData = Decode!<int8>(data);
						args[argIdx] = scope:AttrBlock box attrData;
					case .Int16,
						.UInt16,
						.Char16:
						let attrData = Decode!<int16>(data);
						args[argIdx] = scope:AttrBlock box attrData;
					case .Int32,
						 .UInt32,
						 .Char32:
						let attrData = Decode!<int32>(data);
						args[argIdx] = scope:AttrBlock box attrData;
					case .Float:
						let attrData = Decode!<float>(data);
						args[argIdx] = scope:AttrBlock box attrData;
					case .Int64,
						.UInt64,
						.Double:
						let attrData = Decode!<int64>(data);
						args[argIdx] = scope:AttrBlock box attrData;
					case (TypeCode)255:
						let stringId = Decode!<int32>(data);
						String str = String.[Friend]sIdStringLiterals[stringId];
						args[argIdx] = str;
					default:
						Runtime.FatalError("Not handled");
					}
					argIdx++;
			    }
				
				methodInfo.Invoke(targetAttr, params args);
			    return .Ok;
			}

			return .Err;
		}
	}
}
