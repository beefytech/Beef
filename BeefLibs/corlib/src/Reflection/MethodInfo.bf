using System;
using System.Reflection;
using System.FFI;
using System.Diagnostics;
using System.Collections;

namespace System.Reflection
{
	[CRepr, AlwaysInclude]
	public struct MethodInfo
	{
		TypeInstance mTypeInstance;
		TypeInstance.MethodData* mMethodData;

		public this(TypeInstance typeInstance, TypeInstance.MethodData* methodData)
		{
		    mTypeInstance = typeInstance;
		    mMethodData = methodData;
		}

		public bool IsInitialized => mMethodData != null;
		public StringView Name => mMethodData.[Friend]mName;
		public int ParamCount => mMethodData.[Friend]mParamCount;
		public bool IsConstructor => mMethodData.mName === "__BfCtor" || mMethodData.mName === "__BfStaticCtor";
		public bool IsDestructor => mMethodData.mName === "__BfStaticDtor" || mMethodData.mName === "__BfStaticDtor";
		public Type ReturnType => Type.[Friend]GetType(mMethodData.mReturnType);
		
		public Type GetParamType(int paramIdx)
		{
			Debug.Assert((uint)paramIdx < (uint)mMethodData.mParamCount);
			return Type.[Friend]GetType(mMethodData.mParamData[paramIdx].mType);
		}

		public StringView GetParamName(int paramIdx)
		{
			Debug.Assert((uint)paramIdx < (uint)mMethodData.mParamCount);
			return mMethodData.mParamData[paramIdx].mName;
		}

		public Result<T> GetCustomAttribute<T>() where T : Attribute
		{
			return mTypeInstance.[Friend]GetCustomAttribute<T>(mMethodData.mCustomAttributesIdx);
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

		public Result<Variant, CallError> Invoke(Variant target, params Span<Variant> args)
		{
			var retType = Type.[Friend]GetType(mMethodData.mReturnType);

			FFIABI abi = .Default;
#if BF_PLATFORM_WINDOWS && BF_32_BIT
			if (mMethodData.mFlags.HasFlag(.ThisCall))
				abi = .ThisCall;
			else if (!mMethodData.mFlags.HasFlag(.Static))
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
				for (int fieldIdx < type.[Friend]mFieldDataCount)
				{
					let fieldData = ref type.[Friend]mFieldDataPtr[fieldIdx];
					let fieldType = Type.[Friend]GetType(fieldData.mFieldTypeId);
					if (fieldData.mFlags.HasFlag(.Static))
					{
						if (isEnum)
							break; // Already got payload and discriminator
						continue;
					}
					if (fieldType.[Friend]mSize == 0)
						continue;

					if (fieldType.IsStruct)
					{
						SplatArg((TypeInstance)fieldType, (uint8*)ptr + (int)fieldData.mData);
					}
					else
					{
						ffiParamList.Add(FFIType.Get(fieldType, null, null));
						ffiArgList.Add((uint8*)ptr + (int)fieldData.mData);
					}
				}
			}

			mixin AddArg(int argIdx, var arg, void* argPtr, Type paramType, bool splat)
			{
				var argType = arg.VariantType;
				void* dataPtr = arg.DataPtr;
				bool isPtrToPtr = false;
				bool isValid = true;

				bool added =  false;

				if (var refParamType = paramType as RefType)
				{
					if (argType.IsPointer)
					{
						Type elemType = argType.UnderlyingType;
						if (elemType != refParamType.UnderlyingType)
							isValid = false;

						ffiParamList.Add(&FFIType.Pointer);
						ffiArgList.Add(dataPtr);
						added = true;
					}
					else
					{
						if ((Type)argType != refParamType.UnderlyingType)
							isValid = false;

						ffiParamList.Add(&FFIType.Pointer);
						int* stackDataPtr = scope:mixin int();
						*stackDataPtr = (int)dataPtr;
						ffiArgList.Add(stackDataPtr);
						added = true;
					}
				}
				else if (paramType.IsValueType)
				{
					if (argType.IsPointer)
					{
						if (!paramType.IsPointer)
						{
							isPtrToPtr = true;
							argType = argType.UnderlyingType;
						}
						/*else
						{
							dataPtr = *(void**)dataPtr;
						}*/
					}

					if (!argType.IsSubtypeOf(paramType))
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

				if (added)
				{
					// Already handled
				}
				else if (paramType.IsStruct)
				{
					TypeInstance paramTypeInst = (TypeInstance)paramType;

					if (paramType.Size == 0)
					{
						// Do nothing
					}
					else if (splat)
					{
						if (isPtrToPtr)
							dataPtr = *((void**)dataPtr);

						if (paramTypeInst.[Friend]mFieldDataCount > 0)
						{
							SplatArg(paramTypeInst, dataPtr);
						}
						else
						{
							let splatData = (TypeInstance.FieldSplatData*)paramTypeInst.[Friend]mFieldDataPtr;
							for (int splatIdx < 3)
							{
								let splatTypeId = splatData.mSplatTypes[splatIdx];
								if (splatTypeId == 0)
									break;

								let splatType = Type.[Friend]GetType(splatTypeId);
								ffiParamList.Add(GetFFIType!:mixin(splatType));
								ffiArgList.Add((uint8*)dataPtr + splatData.mSplatOffsets[splatIdx]);
							}

						}
					}
					else
					{
						if (!isPtrToPtr)
						{
							int* stackDataPtr = scope:mixin int();
							*stackDataPtr = (int)dataPtr;
							ffiArgList.Add(stackDataPtr);
						}
						else
							ffiArgList.Add(dataPtr);
						// Pass by ref
						ffiParamList.Add(&FFIType.Pointer);
					}
				}
				else if (paramType.IsValueType)
				{
					ffiParamList.Add(GetFFIType!:mixin(paramType));
					ffiArgList.Add(dataPtr);
				}
				else
				{
					ffiParamList.Add(&FFIType.Pointer);
					ffiArgList.Add(argPtr);
				}
			}

			void* funcPtr = null;
			int ifaceOffset = -1;
			if (mMethodData.mFlags.HasFlag(.Static))
			{
				if (target.HasValue)
					return .Err(.TargetNotExpected);
			}
			else
			{
				if (!target.HasValue)
					return .Err(.TargetExpected);
				var thisType = mTypeInstance;
				if (mTypeInstance.IsInterface)
				{
					if (target.IsObject)
					{
						var targetObject = target.Get<Object>();
						thisType = targetObject.GetType() as TypeInstance;
						if (thisType == null)
							return .Err(.InvalidTarget);
					}
					else
					{
						TypeInstance.InterfaceData* interfaceData = null;
						var variantType = target.VariantType;
						if (variantType.IsPointer)
							thisType = variantType.UnderlyingType as TypeInstance;
						else
							thisType = variantType as TypeInstance;
						var checkType = thisType;
						CheckLoop: while (checkType != null)
						{
							for (int ifaceIdx < checkType.[Friend]mInterfaceCount)
							{
								if (checkType.[Friend]mInterfaceDataPtr[ifaceIdx].mInterfaceType == mTypeInstance.TypeId)
								{
									interfaceData = &checkType.[Friend]mInterfaceDataPtr[ifaceIdx];
									break CheckLoop;
								}
							}

							checkType = checkType.BaseType;
						}

						if (interfaceData == null)
							return .Err(.InvalidTarget);

						int ifaceMethodIdx = interfaceData.mStartInterfaceTableIdx + mMethodData.mMethodIdx;
						if (ifaceMethodIdx >= thisType.[Friend]mInterfaceMethodCount)
							return .Err(.InvalidTarget);
						funcPtr = *(thisType.[Friend]mInterfaceMethodTable + ifaceMethodIdx);
					}

					ifaceOffset = mTypeInstance.[Friend]mMemberDataOffset;
				}

				bool splatThis = thisType.IsSplattable && !mMethodData.mFlags.HasFlag(.Mutating);
#if BF_PLATFORM_WINDOWS && BF_32_BIT
				if ((mTypeInstance.IsInterface) && (splatThis))
					abi = .MS_CDecl;
#endif
				AddArg!::(-1, ref target, &target.[Friend]mData, thisType, splatThis);
			}

			if (args.Length != mMethodData.mParamCount)
				return .Err(.ParamCountMismatch);

			var variantData = Variant.Alloc(retType, var retVal);
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
				let paramData = ref mMethodData.mParamData[@arg.Index];
				let argType = Type.[Friend]GetType(paramData.mType);
				AddArg!::(@arg.Index, ref arg, &arg.[Friend]mData, argType, paramData.mParamFlags.HasFlag(.Splat));
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

			if (funcPtr == null)
			{
				funcPtr = mMethodData.mFuncPtr;
				if (mMethodData.mFlags.HasFlag(.Virtual))
				{
					Object objTarget = target.Get<Object>();
	
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
					void* classVData = (void*)(objTarget.[Friend]mClassVData & ~0xFF);
#else
					void* classVData = objTarget.[Friend]mClassVData;
#endif
					if (ifaceOffset >= 0)
					{
						void* ifaceVirtualTable = *(void**)((uint8*)classVData + ifaceOffset);
						funcPtr = (void*)*(int*)((uint8*)ifaceVirtualTable + mMethodData.mVirtualIdx);
					}
					else if (mMethodData.mVirtualIdx >= 0x100000)
					{
						void* extAddr = (void*)*((int*)classVData + (mMethodData.mVirtualIdx>>20 - 1));
						funcPtr = (void*)*((int*)extAddr + (mMethodData.mVirtualIdx & 0xFFFFF));
					}
					else
					{
						funcPtr = (void*)*(int*)((uint8*)classVData + mMethodData.mVirtualIdx);
					}
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

		public Result<Variant, CallError> Invoke(Object target, params Object[] args)
		{
			var retType = Type.[Friend]GetType(mMethodData.mReturnType);

			FFIABI abi = .Default;
#if BF_PLATFORM_WINDOWS && BF_32_BIT
			if (mMethodData.mFlags.HasFlag(.ThisCall))
				abi = .ThisCall;
			else if (!mMethodData.mFlags.HasFlag(.Static))
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
				for (int fieldIdx < type.[Friend]mFieldDataCount)
				{
					let fieldData = ref type.[Friend]mFieldDataPtr[fieldIdx];
					let fieldType = Type.[Friend]GetType(fieldData.mFieldTypeId);
					if (fieldData.mFlags.HasFlag(.Static))
					{
						if (isEnum)
							break; // Already got payload and discriminator
						continue;
					}
					if (fieldType.[Friend]mSize == 0)
						continue;

					if (fieldType.IsStruct)
					{
						SplatArg((TypeInstance)fieldType, (uint8*)ptr + (int)fieldData.mData);
					}
					else
					{
						ffiParamList.Add(FFIType.Get(fieldType, null, null));
						ffiArgList.Add((uint8*)ptr + (int)fieldData.mData);
					}
				}
			}

			mixin AddArg(int argIdx, Object arg, void* argPtr, Type paramType, bool splat)
			{
				bool unbox = false;
				bool unboxToPtr = false;

				let argType = arg.[Friend]RawGetType();
				void* dataPtr = (uint8*)Internal.UnsafeCastToPtr(arg) + argType.[Friend]mMemberDataOffset;
				bool isValid = true;

				bool added = false;

				if (var refParamType = paramType as RefType)
				{
					if (argType.IsBoxedStructPtr || argType.IsBoxedPrimitivePtr)
					{
						var elemType = argType.BoxedPtrType;
						if (elemType != refParamType.UnderlyingType)
							isValid = false;

						ffiParamList.Add(&FFIType.Pointer);
						ffiArgList.Add(dataPtr);
						added = true;
					}
					else
					{
						var elemType = argType.UnderlyingType;
						if (elemType != refParamType.UnderlyingType)
						{
							if (elemType.IsTypedPrimitive)
								elemType = elemType.UnderlyingType;
							if (elemType != refParamType.UnderlyingType)
								isValid = false;
						}

						ffiParamList.Add(&FFIType.Pointer);
						int* stackDataPtr = scope:mixin int();
						*stackDataPtr = (int)dataPtr;
						ffiArgList.Add(stackDataPtr);
						added = true;
					}
				}
				else if (paramType.IsValueType)
				{
					bool handled = true;

					if (!argType.IsBoxed)
						return .Err(.InvalidArgument((.)argIdx));

					Type underlyingType = argType.UnderlyingType;
					if ((paramType.IsPrimitive) && (underlyingType.IsTypedPrimitive)) // Boxed primitive?
						underlyingType = underlyingType.UnderlyingType;

					if (argType.IsBoxedStructPtr || argType.IsBoxedPrimitivePtr)
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

				if (added)
				{
					// Already handled
				}
				else if (paramType.IsStruct)
				{
					TypeInstance paramTypeInst = (TypeInstance)paramType;

					if (paramType.Size == 0)
					{
						// Do nothing
					}
					else if (splat)
					{
						if (paramTypeInst.[Friend]mFieldDataCount > 0)
						{
							SplatArg(paramTypeInst, dataPtr);
						}
						else
						{
							let splatData = (TypeInstance.FieldSplatData*)paramTypeInst.[Friend]mFieldDataPtr;
							for (int splatIdx < 3)
							{
								let splatTypeId = splatData.mSplatTypes[splatIdx];
								if (splatTypeId == 0)
									break;

								let splatType = Type.[Friend]GetType(splatTypeId);
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
						int* stackDataPtr = scope:mixin int();
						*stackDataPtr = (int)dataPtr;
						ffiArgList.Add(stackDataPtr);
					}
					else
						ffiArgList.Add(dataPtr);
				}
			}

			void* funcPtr = mMethodData.mFuncPtr;
			int virtualOffset = 0;
			int ifaceOffset = -1;
			if (mMethodData.mFlags.HasFlag(.Static))
			{
				if (target != null)
					return .Err(.TargetNotExpected);
			}
			else
			{
				if (target == null)
					return .Err(.TargetExpected);

				var thisType = mTypeInstance;
				if (mTypeInstance.IsInterface)
				{
					thisType = target.[Friend]RawGetType() as TypeInstance;
					if (thisType == null)
						return .Err(.InvalidTarget);

					ifaceOffset = mTypeInstance.[Friend]mMemberDataOffset;

					/*TypeInstance.InterfaceData* interfaceData = null;
					var checkType = thisType;
					CheckLoop: while (checkType != null)
					{
						for (int ifaceIdx < checkType.[Friend]mInterfaceCount)
						{
							if (checkType.[Friend]mInterfaceDataPtr[ifaceIdx].mInterfaceType == mTypeInstance.TypeId)
							{
								interfaceData = &checkType.[Friend]mInterfaceDataPtr[ifaceIdx];
								break CheckLoop;
							}
						}

						checkType = checkType.BaseType;
					}

					if (interfaceData == null)
						return .Err(.InvalidTarget);
					virtualOffset = interfaceData.mStartVirtualIdx * sizeof(int);*/
				}

				bool splatThis = thisType.IsSplattable && !mMethodData.mFlags.HasFlag(.Mutating);
				AddArg!::(-1, target, &target, thisType, splatThis);
			}

			if (args.Count != mMethodData.mParamCount)
				return .Err(.ParamCountMismatch);

			var variantData = Variant.Alloc(retType, var retVal);
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
				let argType = Type.[Friend]GetType(paramData.mType);
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

			if (mMethodData.mFlags.HasFlag(.Virtual))
			{
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
				void* classVData = (void*)(target.[Friend]mClassVData & ~0xFF);
#else
				void* classVData = target.[Friend]mClassVData;
#endif
				if (ifaceOffset >= 0)
				{
					void* ifaceVirtualTable = *(void**)((uint8*)classVData + ifaceOffset);
					funcPtr = (void*)*(int*)((uint8*)ifaceVirtualTable + mMethodData.mVirtualIdx + virtualOffset);
				}
				else if (mMethodData.mVirtualIdx >= 0x100000)
				{
					void* extAddr = (void*)*((int*)classVData + (mMethodData.mVirtualIdx>>20 - 1));
					funcPtr = (void*)*((int*)extAddr + (mMethodData.mVirtualIdx & 0xFFFFF) + virtualOffset);
				}
				else
				{
					funcPtr = (void*)*(int*)((uint8*)classVData + mMethodData.mVirtualIdx + virtualOffset);
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

		public override void ToString(String strBuffer)
		{
			let retType = Type.[Friend]GetType(mMethodData.mReturnType);
			retType.ToString(strBuffer);
			strBuffer.Append(' ');
			strBuffer.Append(mMethodData.mName);
			strBuffer.Append('(');
			for (int paramIdx < mMethodData.mParamCount)
			{
				if (paramIdx > 0)
					strBuffer.Append(", ");
				let paramData = mMethodData.mParamData[paramIdx];
				let paramType = Type.[Friend]GetType(paramData.mType);
				paramType.ToString(strBuffer);
				strBuffer.Append(' ');
				strBuffer.Append(paramData.mName);
			}
			strBuffer.Append(')');
		}

		public struct Enumerator : IEnumerator<MethodInfo>
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
					if (mIdx == mTypeInstance.[Friend]mMethodDataCount)
						return false;
					var methodData = &mTypeInstance.[Friend]mMethodDataPtr[mIdx];
					bool matches = (mBindingFlags.HasFlag(BindingFlags.Static) && (methodData.mFlags.HasFlag(.Static)));
					matches |= (mBindingFlags.HasFlag(BindingFlags.Instance) && (!methodData.mFlags.HasFlag(.Static)));
					if (matches)
						break;
				}
		        return true;
		    }

		    public MethodInfo Current
		    {
		        get
		        {
					var methodData = &mTypeInstance.[Friend]mMethodDataPtr[mIdx];
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
