using System.Collections;
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

			    Object[] args = scope Object[methodInfo.[Friend]mData.mMethodData.mParamCount];

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
					case (TypeCode)typeof(TypeCode).MaxValue + 8: //BfConstType_TypeOf
						let argTypeId = Decode!<int32>(data);
						args[argIdx] = Type.[Friend]GetType((.)argTypeId);
					case (TypeCode)255:
						let stringId = Decode!<int32>(data);
						String str = String.[Friend]sIdStringLiterals[stringId];
						args[argIdx] = str;
					default:
						Runtime.FatalError("Not handled");
					}
					argIdx++;
			    }
				
				if (methodInfo.Invoke(targetAttr, params args) case .Ok(var val))
					val.Dispose();
			    return .Ok;
			}

			return .Err;
		}

		public struct CustomAttributeEnumerator : IEnumerator<Object>, IDisposable
		{
			void* data;
			int32 attrIdx;
			uint8 count;
			Object targetAttr;

			public this(void* inAttrData)
			{
				data = inAttrData;
				data++;
				attrIdx = 0;
				count = data != null ? AttributeInfo.Decode!<uint8>(data) : 0;
				targetAttr = null;
			}

			public Object Current
			{
				get
				{
					return targetAttr;
				}
			}

			public bool MoveNext() mut
			{
				if (attrIdx >= count || data == null)
					return false;

				void* startPtr = data;
				var len = AttributeInfo.Decode!<uint16>(data);
				void* endPtr = (uint8*)startPtr + len;

				var typeId = AttributeInfo.Decode!<TypeId>(data);

				var methodIdx = AttributeInfo.Decode!<uint16>(data);

				Type attrType = Type.[Friend]GetType(typeId);
				TypeInstance attrTypeInst = attrType as TypeInstance;
				MethodInfo methodInfo = .(attrTypeInst, attrTypeInst.[Friend]mMethodDataPtr + methodIdx);

				Object[] args = scope Object[methodInfo.[Friend]mData.mMethodData.mParamCount];

				int argIdx = 0;
				while (data < endPtr)
				{
				    var attrDataType = AttributeInfo.Decode!<TypeCode>(data);
					switch (attrDataType)
					{
					case .Int8,
						 .UInt8,
						 .Char8,
						 .Boolean:
						let attrData = AttributeInfo.Decode!<int8>(data);
						args[argIdx] = scope:: box attrData;
					case .Int16,
						.UInt16,
						.Char16:
						let attrData = AttributeInfo.Decode!<int16>(data);
						args[argIdx] = scope:: box attrData;
					case .Int32,
						 .UInt32,
						 .Char32:
						let attrData = AttributeInfo.Decode!<int32>(data);
						args[argIdx] = scope:: box attrData;
					case .Float:
						let attrData = AttributeInfo.Decode!<float>(data);
						args[argIdx] = scope:: box attrData;
					case .Int64,
						.UInt64,
						.Double:
						let attrData = AttributeInfo.Decode!<int64>(data);
						args[argIdx] = scope:: box attrData;
					case (TypeCode)typeof(TypeCode).MaxValue + 8: //BfConstType_TypeOf
						let argTypeId = AttributeInfo.Decode!<int32>(data);
						args[argIdx] = Type.[Friend]GetType((.)argTypeId);
					case (TypeCode)255:
						let stringId = AttributeInfo.Decode!<int32>(data);
						String str = String.[Friend]sIdStringLiterals[stringId];
						args[argIdx] = str;
					default:
						Runtime.FatalError("Not handled");
					}
					argIdx++;
				}

				Type boxedAttrType = attrType.BoxedType;

				delete targetAttr;
				targetAttr = boxedAttrType.CreateObject().Get();

				if (methodInfo.Invoke((uint8*)Internal.UnsafeCastToPtr(targetAttr) + boxedAttrType.[Friend]mMemberDataOffset, params args) case .Ok(var val))
					val.Dispose();

				attrIdx++;
				return true;
			}

			public void Dispose()
			{
				delete targetAttr;
			}

			public Result<Object> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public struct CustomAttributeEnumerator<T> : IEnumerator<T>
		{
			void* data;
			int32 attrIdx;
			uint8 count;
			T targetAttr;

			public this(void* inAttrData)
			{
				data = inAttrData;
				data++;
				attrIdx = 0;
				count = data != null ? AttributeInfo.Decode!<uint8>(data) : 0;
				targetAttr = ?;
			}

			public T Current
			{
				get
				{
					return targetAttr;
				}
			}

			public bool MoveNext() mut
			{
				if (attrIdx >= count || data == null)
					return false;

				void* endPtr = null;
				TypeId typeId = 0;

				while (true)
				{
					void* startPtr = data;
					var len = AttributeInfo.Decode!<uint16>(data);
					endPtr = (uint8*)startPtr + len;

					typeId = AttributeInfo.Decode!<TypeId>(data);
					if (typeId != typeof(T).TypeId)
					{
						attrIdx++;
						if (attrIdx >= count)
							return false;
					    data = endPtr;
					    continue;
					}

					break;
				}

				var methodIdx = AttributeInfo.Decode!<uint16>(data);

				Type attrType = Type.[Friend]GetType(typeId);
				TypeInstance attrTypeInst = attrType as TypeInstance;
				MethodInfo methodInfo = .(attrTypeInst, attrTypeInst.[Friend]mMethodDataPtr + methodIdx);

				Object[] args = scope Object[methodInfo.[Friend]mData.mMethodData.mParamCount];

				int argIdx = 0;
				while (data < endPtr)
				{
				    var attrDataType = AttributeInfo.Decode!<TypeCode>(data);
					switch (attrDataType)
					{
					case .Int8,
						 .UInt8,
						 .Char8,
						 .Boolean:
						let attrData = AttributeInfo.Decode!<int8>(data);
						args[argIdx] = scope:: box attrData;
					case .Int16,
						.UInt16,
						.Char16:
						let attrData = AttributeInfo.Decode!<int16>(data);
						args[argIdx] = scope:: box attrData;
					case .Int32,
						 .UInt32,
						 .Char32:
						let attrData = AttributeInfo.Decode!<int32>(data);
						args[argIdx] = scope:: box attrData;
					case .Float:
						let attrData = AttributeInfo.Decode!<float>(data);
						args[argIdx] = scope:: box attrData;
					case .Int64,
						.UInt64,
						.Double:
						let attrData = AttributeInfo.Decode!<int64>(data);
						args[argIdx] = scope:: box attrData;
					case (TypeCode)typeof(TypeCode).MaxValue + 8: //BfConstType_TypeOf
						let argTypeId = AttributeInfo.Decode!<int32>(data);
						args[argIdx] = Type.[Friend]GetType((.)argTypeId);
					case (TypeCode)255:
						let stringId = AttributeInfo.Decode!<int32>(data);
						String str = String.[Friend]sIdStringLiterals[stringId];
						args[argIdx] = str;
					default:
						Runtime.FatalError("Not handled");
					}
					argIdx++;
				}

				if (methodInfo.Invoke(&targetAttr, params args) case .Ok(var val))
					val.Dispose();

				attrIdx++;
				return true;
			}

			public void Dispose()
			{
			}

			public Result<T> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}
}
