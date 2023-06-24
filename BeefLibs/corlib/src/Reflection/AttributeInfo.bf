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
					case .NullPtr:
						args[argIdx] = null;
						break;
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
					case (TypeCode)typeof(TypeCode).MaxValue + 9: //BfConstType_TypeOf
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
				else
					return .Err;
			    return .Ok;
			}

			return .Err;
		}

		public struct CustomAttributeEnumerator : IEnumerator<Variant>, IDisposable
		{
			void* mData;
			int32 mAttrIdx;
			uint8 mCount;
			Variant mTargetAttr;

			public this(void* inAttrData)
			{
				mData = inAttrData;
				mData++;
				mAttrIdx = 0;
				mCount = mData != null ? AttributeInfo.Decode!<uint8>(mData) : 0;
				mTargetAttr = default;
			}

			public Variant Current
			{
				get
				{
					return mTargetAttr;
				}
			}

			public bool MoveNext() mut
			{
				if (mAttrIdx >= mCount || mData == null)
					return false;

				void* startPtr = mData;
				var len = AttributeInfo.Decode!<uint16>(mData);
				void* endPtr = (uint8*)startPtr + len;

				var typeId = AttributeInfo.Decode!<TypeId>(mData);

				var methodIdx = AttributeInfo.Decode!<uint16>(mData);

				Type attrType = Type.[Friend]GetType(typeId);
				TypeInstance attrTypeInst = attrType as TypeInstance;
				MethodInfo methodInfo = .(attrTypeInst, attrTypeInst.[Friend]mMethodDataPtr + methodIdx);

				Variant[] args = scope Variant[methodInfo.[Friend]mData.mMethodData.mParamCount];

				int argIdx = 0;
				while (mData < endPtr)
				{
				    var attrDataType = AttributeInfo.Decode!<TypeCode>(mData);
					switch (attrDataType)
					{
					case .Int8,
						 .UInt8,
						 .Char8,
						 .Boolean:
						let attrData = AttributeInfo.Decode!<int8>(mData);
						args[argIdx] = Variant.Create(attrData);
					case .Int16,
						.UInt16,
						.Char16:
						let attrData = AttributeInfo.Decode!<int16>(mData);
						args[argIdx] = Variant.Create(attrData);
					case .Int32,
						 .UInt32,
						 .Char32:
						let attrData = AttributeInfo.Decode!<int32>(mData);
						args[argIdx] = Variant.Create(attrData);
					case .Float:
						let attrData = AttributeInfo.Decode!<float>(mData);
						args[argIdx] = Variant.Create(attrData);
					case .Int64,
						.UInt64,
						.Double:
						let attrData = AttributeInfo.Decode!<int64>(mData);
						args[argIdx] = Variant.Create(attrData);
					case (TypeCode)typeof(TypeCode).MaxValue + 9: //BfConstType_TypeOf
						let argTypeId = AttributeInfo.Decode!<int32>(mData);
						args[argIdx] = Variant.Create(Type.[Friend]GetType((.)argTypeId));
					case (TypeCode)255:
						let stringId = AttributeInfo.Decode!<int32>(mData);
						String str = String.[Friend]sIdStringLiterals[stringId];
						args[argIdx] = Variant.Create(str);
					default:
						Runtime.FatalError("Not handled");
					}
					argIdx++;
				}

				mTargetAttr.Dispose();
				Variant.AllocOwned(attrType, out mTargetAttr);

				if (methodInfo.Invoke(mTargetAttr, params args) case .Ok(var val))
					val.Dispose();
				
				for (var variant in ref args)
					variant.Dispose();

				mAttrIdx++;
				return true;
			}

			public void Dispose() mut
			{
				mTargetAttr.Dispose();
			}

			public Result<Variant> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public struct ComptimeTypeCustomAttributeEnumerator : IEnumerator<Variant>, IDisposable
		{
			int32 mTypeId;
			int32 mAttrIdx;
			Variant mTargetAttr;

			public this(int32 typeId)
			{
				mTypeId = typeId;
				mAttrIdx = -1;
				mTargetAttr = default;
			}

			public Variant Current
			{
				get
				{
					return mTargetAttr;
				}
			}

			public bool MoveNext() mut
			{
				let attrType = Type.[Friend]Comptime_Type_GetCustomAttributeType(mTypeId, ++mAttrIdx);
				if (attrType != null)
				{
					mTargetAttr.Dispose();
					void* data = Variant.Alloc(attrType, out mTargetAttr);

					if (Type.[Friend]Comptime_Type_GetCustomAttribute(mTypeId, mAttrIdx, data))
						return true;
				}
				return false;
			}

			public void Dispose() mut
			{
				mTargetAttr.Dispose();
			}

			public Result<Variant> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public struct ComptimeFieldCustomAttributeEnumerator : IEnumerator<Variant>, IDisposable
		{
			int32 mTypeId;
			int32 mFieldIdx;
			int32 mAttrIdx;
			Variant mTargetAttr;

			public this(int32 typeId, int32 fieldIdx)
			{
				mTypeId = typeId;
				mFieldIdx = fieldIdx;
				mAttrIdx = -1;
				mTargetAttr = default;
			}

			public Variant Current
			{
				get
				{
					return mTargetAttr;
				}
			}

			public bool MoveNext() mut
			{
				let attrType = Type.[Friend]Comptime_Field_GetCustomAttributeType(mTypeId, mFieldIdx, ++mAttrIdx);
				if (attrType != null)
				{
					mTargetAttr.Dispose();
					void* data = Variant.Alloc(attrType, out mTargetAttr);

					if (Type.[Friend]Comptime_Field_GetCustomAttribute(mTypeId, mFieldIdx, mAttrIdx, data))
						return true;
				}
				return false;
			}

			public void Dispose() mut
			{
				mTargetAttr.Dispose();
			}

			public Result<Variant> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public struct ComptimeMethodCustomAttributeEnumerator : IEnumerator<Variant>, IDisposable
		{
			int64 mMethodHandle;
			int32 mAttrIdx;
			Variant mTargetAttr;

			public this(int64 methodHandle)
			{
				mMethodHandle = methodHandle;
				mAttrIdx = -1;
				mTargetAttr = default;
			}

			public Variant Current
			{
				get
				{
					return mTargetAttr;
				}
			}

			public bool MoveNext() mut
			{
				let attrType = Type.[Friend]Comptime_Method_GetCustomAttributeType(mMethodHandle, ++mAttrIdx);
				if (attrType != null)
				{
					mTargetAttr.Dispose();
					void* data = Variant.Alloc(attrType, out mTargetAttr);

					if (Type.[Friend]Comptime_Method_GetCustomAttribute(mMethodHandle, mAttrIdx, data))
						return true;
				}
				return false;
			}

			public void Dispose() mut
			{
				mTargetAttr.Dispose();
			}

			public Result<Variant> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public struct CustomAttributeEnumerator<T> : IEnumerator<T>
		{
			void* mData;
			int32 mAttrIdx;
			uint8 mCount;
			T mTargetAttr;

			public this(void* inAttrData)
			{
				mData = inAttrData;
				mData++;
				mAttrIdx = 0;
				mCount = mData != null ? AttributeInfo.Decode!<uint8>(mData) : 0;
				mTargetAttr = ?;
			}

			public T Current
			{
				get
				{
					return mTargetAttr;
				}
			}

			public bool MoveNext() mut
			{
				if (mAttrIdx >= mCount || mData == null)
					return false;

				void* endPtr = null;
				TypeId typeId = 0;

				while (true)
				{
					void* startPtr = mData;
					var len = AttributeInfo.Decode!<uint16>(mData);
					endPtr = (uint8*)startPtr + len;

					typeId = AttributeInfo.Decode!<TypeId>(mData);
					if (typeId != typeof(T).TypeId)
					{
						mAttrIdx++;
						if (mAttrIdx >= mCount)
							return false;
					    mData = endPtr;
					    continue;
					}

					break;
				}

				var methodIdx = AttributeInfo.Decode!<uint16>(mData);

				Type attrType = Type.[Friend]GetType(typeId);
				TypeInstance attrTypeInst = attrType as TypeInstance;
				MethodInfo methodInfo = .(attrTypeInst, attrTypeInst.[Friend]mMethodDataPtr + methodIdx);

				Object[] args = scope Object[methodInfo.[Friend]mData.mMethodData.mParamCount];

				int argIdx = 0;
				while (mData < endPtr)
				{
				    var attrDataType = AttributeInfo.Decode!<TypeCode>(mData);
					switch (attrDataType)
					{
					case .Int8,
						 .UInt8,
						 .Char8,
						 .Boolean:
						let attrData = AttributeInfo.Decode!<int8>(mData);
						args[argIdx] = scope:: box attrData;
					case .Int16,
						.UInt16,
						.Char16:
						let attrData = AttributeInfo.Decode!<int16>(mData);
						args[argIdx] = scope:: box attrData;
					case .Int32,
						 .UInt32,
						 .Char32:
						let attrData = AttributeInfo.Decode!<int32>(mData);
						args[argIdx] = scope:: box attrData;
					case .Float:
						let attrData = AttributeInfo.Decode!<float>(mData);
						args[argIdx] = scope:: box attrData;
					case .Int64,
						.UInt64,
						.Double:
						let attrData = AttributeInfo.Decode!<int64>(mData);
						args[argIdx] = scope:: box attrData;
					case (TypeCode)typeof(TypeCode).MaxValue + 9: //BfConstType_TypeOf
						let argTypeId = AttributeInfo.Decode!<int32>(mData);
						args[argIdx] = Type.[Friend]GetType((.)argTypeId);
					case (TypeCode)255:
						let stringId = AttributeInfo.Decode!<int32>(mData);
						String str = String.[Friend]sIdStringLiterals[stringId];
						args[argIdx] = str;
					default:
						Runtime.FatalError("Not handled");
					}
					argIdx++;
				}

				if (methodInfo.Invoke(&mTargetAttr, params args) case .Ok(var val))
					val.Dispose();

				mAttrIdx++;
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

		public struct ComptimeTypeCustomAttributeEnumerator<T> : IEnumerator<T>
		{
			int32 mTypeId;
			int32 mAttrIdx;
			T mTargetAttr;

			public this(int32 typeId)
			{
				mTypeId = typeId;
				mAttrIdx = -1;
				mTargetAttr = ?;
			}

			public T Current
			{
				get
				{
					return mTargetAttr;
				}
			}

			public bool MoveNext() mut
			{
				Type attrType = null;
				repeat
				{
					attrType = Type.[Friend]Comptime_Type_GetCustomAttributeType(mTypeId, ++mAttrIdx);
					if (attrType == typeof(T))
					{
						if (Type.[Friend]Comptime_Type_GetCustomAttribute(mTypeId, mAttrIdx, &mTargetAttr))
							return true;
					}
				} while (attrType != null);
				return false;
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

		public struct ComptimeFieldCustomAttributeEnumerator<T> : IEnumerator<T>
		{
			int32 mTypeId;
			int32 mFieldIdx;
			int32 mAttrIdx;
			T mTargetAttr;

			public this(int32 typeId, int32 fieldIdx)
			{
				mTypeId = typeId;
				mFieldIdx = fieldIdx;
				mAttrIdx = -1;
				mTargetAttr = ?;
			}

			public T Current
			{
				get
				{
					return mTargetAttr;
				}
			}

			public bool MoveNext() mut
			{
				Type attrType = null;
				repeat
				{
					attrType = Type.[Friend]Comptime_Field_GetCustomAttributeType(mTypeId, mFieldIdx, ++mAttrIdx);
					if (attrType == typeof(T))
					{
						if (Type.[Friend]Comptime_Field_GetCustomAttribute(mTypeId, mFieldIdx, mAttrIdx, &mTargetAttr))
							return true;
					}
				} while (attrType != null);
				return false;
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

		public struct ComptimeMethodCustomAttributeEnumerator<T> : IEnumerator<T>
		{
			int64 mMethodHandle;
			int32 mAttrIdx;
			T mTargetAttr;

			public this(int64 methodHandle)
			{
				mMethodHandle = methodHandle;
				mAttrIdx = -1;
				mTargetAttr = ?;
			}

			public T Current
			{
				get
				{
					return mTargetAttr;
				}
			}

			public bool MoveNext() mut
			{
				Type attrType = null;
				repeat
				{
					attrType = Type.[Friend]Comptime_Method_GetCustomAttributeType(mMethodHandle, ++mAttrIdx);
					if (attrType == typeof(T))
					{
						if (Type.[Friend]Comptime_Method_GetCustomAttribute(mMethodHandle, mAttrIdx, &mTargetAttr))
							return true;
					}
				} while (attrType != null);
				return false;
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
