using System;
using System.Reflection;
using System.FFI;
using System.Diagnostics;
using System.Collections.Generic;

namespace System.Reflection
{
	[CRepr, AlwaysInclude]
	public struct MethodInfo
	{
		internal TypeInstance mTypeInstance;
		internal TypeInstance.MethodData* mMethodData;

		public StringView Name
		{
			get
			{
				return mMethodData.mName;
			}
		}

		public int ParamCount
		{
			get
			{
				return mMethodData.mParamCount;
			}
		}

		public bool IsConstructor
		{
			get
			{
				// String pooling allows us to do identity comparison
				return (Object)mMethodData.mName == "__BfCtor" || ((Object)mMethodData.mName == "__BfStaticCtor");
			}
		}

		public bool IsDestructor
		{
			get
			{
				// String pooling allows us to do identity comparison
				return (Object)mMethodData.mName == "__BfStaticDtor" || (Object)mMethodData.mName == "__BfStaticDtor";
			}
		}

		public Type GetParamType(int paramIdx)
		{
			Debug.Assert((uint)paramIdx < (uint)mMethodData.mParamCount);
			return Type.GetType(mMethodData.mParamData[paramIdx].mType);
		}

		public StringView GetParamName(int paramIdx)
		{
			Debug.Assert((uint)paramIdx < (uint)mMethodData.mParamCount);
			return mMethodData.mParamData[paramIdx].mName;
		}

		public this(TypeInstance typeInstance, TypeInstance.MethodData* methodData)
		{
		    mTypeInstance = typeInstance;
		    mMethodData = methodData;
		}

		public enum CallError
		{
			case None;
			case TargetExpected;
			case TargetNotExpected;
			case InvalidTarget;
			case InvalidArgument(int32 paramIdx);
			case ParamCountMismatch;
			case FFIError;
		}

		public Result<Variant, CallError> Invoke(Object target, params Object[] args)
		{
			var retType = Type.GetType(mMethodData.mReturnType);

			FFIABI abi = .Default;
#if BF_PLATFORM_WINDOWS
			if (mMethodData.mFlags.HasFlag(.StdCall))
				abi = .StdCall;
#endif

			List<FFIType*> ffiParamList = scope .(16);
			List<void*> ffiArgList = scope .(16);
			List<Variant> tempVariants = scope .(4);

			var target;

			mixin GetFFIType(Type type)
			{
				int wantSize = 0;
				FFIType* ffiType = FFIType.Get(type, null, &wantSize);
				if ((ffiType == null) && (wantSize != 0))
				{
					void* allocBytes = scope:mixin uint8[wantSize]*;
					ffiType = FFIType.Get(type, allocBytes, &wantSize);
				}	

				ffiType
			}

			void SplatArg(TypeInstance type, void* ptr)
			{
				if (type.BaseType != null)
					SplatArg(type.BaseType, ptr);

				bool isEnum = type.IsEnum;
				for (int fieldIdx < type.mFieldDataCount)
				{
					let fieldData = ref type.mFieldDataPtr[fieldIdx];
					let fieldType = Type.GetType(fieldData.mFieldTypeId);
					if (fieldData.mFlags.HasFlag(.Static))
					{
						if (isEnum)
							break; // Already got payload and discriminator
						continue;
					}
					if (fieldType.mSize == 0)
						continue;

					if (fieldType.IsStruct)
					{
						SplatArg((TypeInstance)fieldType, (uint8*)ptr + fieldData.mDataOffset);
					}
					else
					{
						ffiParamList.Add(FFIType.Get(fieldType, null, null));
						ffiArgList.Add((uint8*)ptr + fieldData.mDataOffset);
					}
				}
			}

			mixin AddArg(int argIdx, Object arg, void* argPtr, Type paramType, bool splat)
			{
				bool unbox = false;
				bool unboxToPtr = false;

				let argType = arg.RawGetType();
				void* dataPtr = (uint8*)Internal.UnsafeCastToPtr(arg) + argType.mMemberDataOffset;
				bool isValid = true;

				if (paramType.IsValueType)
				{
					bool handled = true;

					if (!argType.IsBoxed)
						return .Err(.InvalidArgument((.)argIdx));

					Type underlyingType = argType.UnderlyingType;
					if ((paramType.IsPrimitive) && (underlyingType.IsTypedPrimitive)) // Boxed primitive?
						underlyingType = underlyingType.UnderlyingType;

					if (argType.IsBoxedStructPtr)
					{
						dataPtr = *(void**)dataPtr;
						handled = true;
					}
					
					if (!handled)
					{
						if (!underlyingType.IsSubtypeOf(paramType))
						{
							if (Convert.ConvertTo(arg, paramType) case .Ok(var variant))
							{
								tempVariants.Add(variant);
								dataPtr = variant.GetValueData();
							}
							else
								isValid = false;
						}
					}
				}
				else
				{
					if (!argType.IsSubtypeOf(paramType))
						isValid = false;
				}

				if (!isValid)
				{
					if (argIdx == -1)
						return .Err(.InvalidTarget);
					else
						return .Err(.InvalidArgument((.)argIdx));
				}

				if (paramType.IsStruct)
				{
					TypeInstance paramTypeInst = (TypeInstance)paramType;

					if (paramType.Size == 0)
					{
						// Do nothing
					}
					else if (splat)
					{
						if (paramTypeInst.mFieldDataCount > 0)
						{
							SplatArg(paramTypeInst, dataPtr);
						}
						else
						{
							let splatData = (TypeInstance.FieldSplatData*)paramTypeInst.mFieldDataPtr;
							for (int splatIdx < 3)
							{
								let splatTypeId = splatData.mSplatTypes[splatIdx];
								if (splatTypeId == 0)
									break;

								let splatType = Type.GetType(splatTypeId);
								ffiParamList.Add(GetFFIType!:mixin(splatType));
								ffiArgList.Add((uint8*)dataPtr + splatData.mSplatOffsets[splatIdx]);
							}

						}
					}
					else
					{
						// Pass by ref
						ffiParamList.Add(&FFIType.Pointer);
						unboxToPtr = true;
						unbox = true;
					}
				}
				else if (paramType.IsValueType)
				{
					ffiParamList.Add(GetFFIType!:mixin(paramType));
					unbox = true;
				}
				else
				{
					ffiParamList.Add(&FFIType.Pointer);
					ffiArgList.Add(argPtr);
				}

				if (unbox)
				{
					if (unboxToPtr)
					{
						int* stackDataPtr = scope:mixin int;
						*stackDataPtr = (int)dataPtr;
						ffiArgList.Add(stackDataPtr);
					}
					else
						ffiArgList.Add(dataPtr);
				}
			}

			if (mMethodData.mFlags.HasFlag(.Static))
			{
				if (target != null)
					return .Err(.TargetNotExpected);
			}
			else
			{
				if (target == null)
					return .Err(.TargetExpected);

				bool splatThis = mTypeInstance.IsSplattable && !mMethodData.mFlags.HasFlag(.Mutating);
				AddArg!::(-1, target, &target, mTypeInstance, splatThis);
			}

			if (args.Count != mMethodData.mParamCount)
				return .Err(.ParamCountMismatch);

			Variant retVal;
			void* variantData = Variant.Alloc(retType, out retVal);
			void* retData = variantData;

			// Struct return? Manually add it as an arg after 'this'.  Revisit this - this is architecture-dependent.
			int unusedRetVal;
			FFIType* ffiRetType = null;
			if (retType.IsStruct)
			{
				ffiRetType = &FFIType.Void;
				ffiParamList.Add(&FFIType.Pointer);
				ffiArgList.Add(&variantData);
				retData = &unusedRetVal;
			}
			else
				ffiRetType = GetFFIType!::(retType);

			for (var arg in ref args)
			{
				let paramData = ref mMethodData.mParamData[@arg];
				let argType = Type.GetType(paramData.mType);
				AddArg!::(@arg, arg, &arg, argType, paramData.mParamFlags.HasFlag(.Splat));
			}

			FFICaller caller = .();
			if (ffiParamList.Count > 0)
			{
				if (caller.Prep(abi, (.)ffiParamList.Count, ffiRetType, &ffiParamList[0]) case .Err)
					return .Err(.FFIError);
			}
			else
			{
				if (caller.Prep(abi, 0, ffiRetType, null) case .Err)
					return .Err(.FFIError);
			}

			void* funcPtr = mMethodData.mFuncPtr;
			if (mMethodData.mFlags.HasFlag(.Virtual))
			{
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
				void* classVData = (void*)(target.[Friend]mClassVData & ~0xFF);
#else
				void* classVData = target.[Friend]mClassVData;
#endif
				if (mMethodData.mVirtualIdx >= 0x100000)
				{
					void* extAddr = (void*)*((int*)classVData + (mMethodData.mVirtualIdx>>20 - 1));
					funcPtr = (void*)*((int*)extAddr + (mMethodData.mVirtualIdx & 0xFFFFF));
				}
				else
				{
					funcPtr = (void*)*(int*)((uint8*)classVData + mMethodData.mVirtualIdx);
				}
			}

			if (ffiArgList.Count > 0)
				caller.Call(funcPtr, retData, &ffiArgList[0]);
			else
				caller.Call(funcPtr, retData, null);

			for (var variant in ref tempVariants)
				variant.Dispose();

			return retVal;
		}

		internal struct Enumerator : IEnumerator<MethodInfo>
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
					if (mIdx == mTypeInstance.mMethodDataCount)
						return false;
#unwarn
					var methodData = &mTypeInstance.mMethodDataPtr[mIdx];
					/*bool matches = (mBindingFlags.HasFlag(BindingFlags.Static) && (methodData.mFlags.HasFlag(FieldFlags.Static)));
					matches |= (mBindingFlags.HasFlag(BindingFlags.Instance) && (!methodData.mFlags.HasFlag(FieldFlags.Static)));*/
					bool matches = true;
					if (matches)
						break;
				}
		        return true;
		    }

		    public MethodInfo Current
		    {
		        get
		        {
					var methodData = &mTypeInstance.mMethodDataPtr[mIdx];
		            return MethodInfo(mTypeInstance, methodData);
		        }
		    }

			public Result<MethodInfo> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}
}
