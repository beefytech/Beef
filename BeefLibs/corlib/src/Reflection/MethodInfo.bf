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
		[Union]
		struct Data
		{
			public TypeInstance.MethodData* mMethodData;
			public int64 mComptimeMethodInstance;
		}

		TypeInstance mTypeInstance;
		Data mData;

		public this(TypeInstance typeInstance, TypeInstance.MethodData* methodData)
		{
		    mTypeInstance = typeInstance;
		    mData.mMethodData = methodData;
		}

		public this(TypeInstance typeInstance, int64 comptimeMethodInstance)
		{
			mTypeInstance = typeInstance;
			mData.mMethodData = null;
			mData.mComptimeMethodInstance = comptimeMethodInstance;
		}

		public TypeInstance DeclaringType => mTypeInstance;

		public bool IsInitialized => Compiler.IsComptime ?
			(mData.mComptimeMethodInstance != 0) :
			(mData.mMethodData != null);

		public StringView Name => Compiler.IsComptime ?
			Type.[Friend]Comptime_Method_GetName(mData.mComptimeMethodInstance) :
			mData.mMethodData.[Friend]mName;

		public int ParamCount => Compiler.IsComptime ?
			Type.[Friend]Comptime_Method_GetInfo(mData.mComptimeMethodInstance).mParamCount :
			mData.mMethodData.[Friend]mParamCount;

		public bool IsConstructor => Compiler.IsComptime ?
			(Name == "__BfCtor" || Name == "__BfStaticCtor") :
			(mData.mMethodData.mName === "__BfCtor" || mData.mMethodData.mName === "__BfStaticCtor");

		public bool IsDestructor => Compiler.IsComptime ?
			(Name == "__BfDtor" || Name == "__BfStaticDtor") :
			(mData.mMethodData.mName === "__BfDtor" || mData.mMethodData.mName === "__BfStaticDtor");

		public Type ReturnType => Compiler.IsComptime ?
			Type.[Friend]GetType((.)Type.[Friend]Comptime_Method_GetInfo(mData.mComptimeMethodInstance).mReturnTypeId) :
			Type.[Friend]GetType(mData.mMethodData.mReturnType);
		
		public Type GetParamType(int paramIdx)
		{
			if (Compiler.IsComptime)
			{
				return Type.[Friend]GetType((.)Type.[Friend]Comptime_Method_GetParamInfo(mData.mComptimeMethodInstance, (.)paramIdx).mParamTypeId);
			}
			else
			{
				Debug.Assert((uint)paramIdx < (uint)mData.mMethodData.mParamCount);
				return Type.[Friend]GetType(mData.mMethodData.mParamData[paramIdx].mType);
			}
		}

		public StringView GetParamName(int paramIdx)
		{
			if (Compiler.IsComptime)
			{
				return Type.[Friend]Comptime_Method_GetParamInfo(mData.mComptimeMethodInstance, (.)paramIdx).mName;
			}
			else
			{
				Debug.Assert((uint)paramIdx < (uint)mData.mMethodData.mParamCount);
				return mData.mMethodData.mParamData[paramIdx].mName;
			}
		}

		public Result<T> GetParamCustomAttribute<T>(int paramIdx) where T : Attribute
		{
			if (Compiler.IsComptime)
				return .Err;
			Debug.Assert((uint)paramIdx < (uint)mData.mMethodData.mParamCount);
			return mTypeInstance.[Friend]GetCustomAttribute<T>(mData.mMethodData.mParamData[paramIdx].mCustomAttributesIdx);
		}

		public AttributeInfo.CustomAttributeEnumerator GetParamCustomAttributes(int paramIdx)
		{
			if (Compiler.IsComptime)
				Runtime.NotImplemented();
			Debug.Assert((uint)paramIdx < (uint)mData.mMethodData.mParamCount);
			return mTypeInstance.[Friend]GetCustomAttributes(mData.mMethodData.mParamData[paramIdx].mCustomAttributesIdx);
		}

		public AttributeInfo.CustomAttributeEnumerator<T> GetParamCustomAttributes<T>(int paramIdx) where T : Attribute
		{
			if (Compiler.IsComptime)
				Runtime.NotImplemented();
			Debug.Assert((uint)paramIdx < (uint)mData.mMethodData.mParamCount);
			return mTypeInstance.[Friend]GetCustomAttributes<T>(mData.mMethodData.mParamData[paramIdx].mCustomAttributesIdx);
		}

		public Result<T> GetCustomAttribute<T>() where T : Attribute
		{
			if (Compiler.IsComptime)
			{
				int32 attrIdx = -1;
				Type attrType = null;
				repeat
				{
					attrType = Type.[Friend]Comptime_Method_GetCustomAttributeType(mData.mComptimeMethodInstance, ++attrIdx);
					if (attrType == typeof(T))
					{
						T val = ?;
						if (Type.[Friend]Comptime_Method_GetCustomAttribute(mData.mComptimeMethodInstance, attrIdx, &val))
							return val;
					}
				}
				while (attrType != null);
				return .Err;
			}
			return mTypeInstance.[Friend]GetCustomAttribute<T>(mData.mMethodData.mCustomAttributesIdx);
		}

		public AttributeInfo.CustomAttributeEnumerator GetCustomAttributes()
		{
			return mTypeInstance.[Friend]GetCustomAttributes(mData.mMethodData.mCustomAttributesIdx);
		}

		[Comptime]
		public AttributeInfo.ComptimeMethodCustomAttributeEnumerator GetCustomAttributes()
		{
			return .(mData.mComptimeMethodInstance);
		}

		public AttributeInfo.CustomAttributeEnumerator<T> GetCustomAttributes<T>() where T : Attribute
		{
			return mTypeInstance.[Friend]GetCustomAttributes<T>(mData.mMethodData.mCustomAttributesIdx);
		}

		[Comptime]
		public AttributeInfo.ComptimeMethodCustomAttributeEnumerator<T> GetCustomAttributes<T>() where T : Attribute
		{
			return .(mData.mComptimeMethodInstance);
		}

		public Result<T> GetReturnCustomAttribute<T>() where T : Attribute
		{
			if (Compiler.IsComptime)
				return .Err;
			return mTypeInstance.[Friend]GetCustomAttribute<T>(mData.mMethodData.mReturnCustomAttributesIdx);
		}

		public AttributeInfo.CustomAttributeEnumerator GetReturnCustomAttributes()
		{
			if (Compiler.IsComptime)
				Runtime.NotImplemented();
			return mTypeInstance.[Friend]GetCustomAttributes(mData.mMethodData.mReturnCustomAttributesIdx);
		}

		public AttributeInfo.CustomAttributeEnumerator<T> GetReturnCustomAttributes<T>() where T : Attribute
		{
			if (Compiler.IsComptime)
				Runtime.NotImplemented();
			return mTypeInstance.[Friend]GetCustomAttributes<T>(mData.mMethodData.mReturnCustomAttributesIdx);
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
			if (Compiler.IsComptime)
				return .Err(.InvalidTarget);
			var retType = Type.[Friend]GetType(mData.mMethodData.mReturnType);

			FFIABI abi = .Default;
#if BF_PLATFORM_WINDOWS && BF_32_BIT
			if (mData.mMethodData.mFlags.HasFlag(.ThisCall))
				abi = .ThisCall;
			else if (!mData.mMethodData.mFlags.HasFlag(.Static))
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
			if (mData.mMethodData.mFlags.HasFlag(.Static))
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

						int ifaceMethodIdx = interfaceData.mStartInterfaceTableIdx + mData.mMethodData.mMethodIdx;
						if (ifaceMethodIdx >= thisType.[Friend]mInterfaceMethodCount)
							return .Err(.InvalidTarget);
						funcPtr = *(thisType.[Friend]mInterfaceMethodTable + ifaceMethodIdx);
					}

					ifaceOffset = mTypeInstance.[Friend]mMemberDataOffset;
				}

				bool splatThis = thisType.IsSplattable && !mData.mMethodData.mFlags.HasFlag(.Mutating);
#if BF_PLATFORM_WINDOWS && BF_32_BIT
				if ((mTypeInstance.IsInterface) && (splatThis))
					abi = .MS_CDecl;
#endif
				AddArg!::(-1, ref target, &target.[Friend]mData, thisType, splatThis);
			}

			if (args.Length != mData.mMethodData.mParamCount)
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
				let paramData = ref mData.mMethodData.mParamData[@arg.Index];
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
				funcPtr = mData.mMethodData.mFuncPtr;
				if (mData.mMethodData.mFlags.HasFlag(.Virtual))
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
						funcPtr = (void*)*(int*)((uint8*)ifaceVirtualTable + mData.mMethodData.mVirtualIdx);
					}
					else if (mData.mMethodData.mVirtualIdx >= 0x100000)
					{
						void* extAddr = (void*)*((int*)classVData + ((mData.mMethodData.mVirtualIdx>>20) - 1));
						funcPtr = (void*)*((int*)extAddr + (mData.mMethodData.mVirtualIdx & 0xFFFFF));
					}
					else
					{
						funcPtr = (void*)*(int*)((uint8*)classVData + mData.mMethodData.mVirtualIdx);
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
			if (Compiler.IsComptime)
				return .Err(.InvalidTarget);
			var retType = Type.[Friend]GetType(mData.mMethodData.mReturnType);

			FFIABI abi = .Default;
#if BF_PLATFORM_WINDOWS && BF_32_BIT
			if (mData.mMethodData.mFlags.HasFlag(.ThisCall))
				abi = .ThisCall;
			else if (!mData.mMethodData.mFlags.HasFlag(.Static))
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

				void* nullPtr = null;

				Type argType = null;
				void* dataPtr = null;

				bool isValid = true;
				bool added = false;
				bool handled = false;

				if (arg == null)
				{
					isValid = false;

					if ((paramType.IsPointer) || (paramType.IsObject) || (paramType.IsInterface))
					{
						argType = paramType;
						dataPtr = &nullPtr;
						isValid = true;
					}
					else if (var genericType = paramType as SpecializedGenericType)
					{
						if (genericType.UnspecializedType == typeof(Nullable<>))
						{
							argType = paramType;
							dataPtr = ScopedAllocZero!(paramType.Size, 16);
							isValid = true;
							handled = true;
						}
					}
				}
				else
				{
					argType = arg.[Friend]RawGetType();
					dataPtr = (uint8*)Internal.UnsafeCastToPtr(arg) + argType.[Friend]mMemberDataOffset;
				}

				if (!isValid)
				{
					// Not valid
				}
				else if (handled)
				{
					
				}
				else if (var refParamType = paramType as RefType)
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
					handled = true;

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
					else
					{
						isValid = underlyingType == paramType;
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

			void* funcPtr = mData.mMethodData.mFuncPtr;
			int virtualOffset = 0;
			int ifaceOffset = -1;
			if (mData.mMethodData.mFlags.HasFlag(.Static))
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

				bool splatThis = thisType.IsSplattable && !mData.mMethodData.mFlags.HasFlag(.Mutating);
				AddArg!::(-1, target, &target, thisType, splatThis);
			}

			if (args.Count != mData.mMethodData.mParamCount)
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
				let paramData = ref mData.mMethodData.mParamData[@arg];
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

			if (mData.mMethodData.mFlags.HasFlag(.Virtual))
			{
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
				void* classVData = (void*)(target.[Friend]mClassVData & ~0xFF);
#else
				void* classVData = target.[Friend]mClassVData;
#endif
				if (ifaceOffset >= 0)
				{
					void* ifaceVirtualTable = *(void**)((uint8*)classVData + ifaceOffset);
					funcPtr = (void*)*(int*)((uint8*)ifaceVirtualTable + mData.mMethodData.mVirtualIdx + virtualOffset);
				}
				else if (mData.mMethodData.mVirtualIdx >= 0x100000)
				{
					void* extAddr = (void*)*((int*)classVData + ((mData.mMethodData.mVirtualIdx>>20) - 1));
					funcPtr = (void*)*((int*)extAddr + (mData.mMethodData.mVirtualIdx & 0xFFFFF) + virtualOffset);
				}
				else
				{
					funcPtr = (void*)*(int*)((uint8*)classVData + mData.mMethodData.mVirtualIdx + virtualOffset);
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
			if (Compiler.IsComptime)
			{
				String str = Type.[Friend]Comptime_Method_ToString(mData.mComptimeMethodInstance);
				strBuffer.Append(str);
				return;
			}

			let retType = Type.[Friend]GetType(mData.mMethodData.mReturnType);
			retType.ToString(strBuffer);
			strBuffer.Append(' ');
			strBuffer.Append(mData.mMethodData.mName);
			strBuffer.Append('(');
			for (int paramIdx < mData.mMethodData.mParamCount)
			{
				if (paramIdx > 0)
					strBuffer.Append(", ");
				let paramData = mData.mMethodData.mParamData[paramIdx];
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

				if (Compiler.IsComptime)
				{
					for (;;)
					{
						mIdx++;
						int64 nativeMethodHandle = Type.[Friend]Comptime_GetMethod((int32)mTypeInstance.TypeId, mIdx);
						if (nativeMethodHandle == 0)
							return false;
						let info = Type.[Friend]Comptime_Method_GetInfo(nativeMethodHandle);

						bool matches = (mBindingFlags.HasFlag(BindingFlags.Static) && (info.mMethodFlags.HasFlag(.Static)));
						matches |= (mBindingFlags.HasFlag(BindingFlags.Instance) && (!info.mMethodFlags.HasFlag(.Static)));
						matches |= (mBindingFlags.HasFlag(BindingFlags.Public) && (info.mMethodFlags.HasFlag(.Public)));
						matches |= (mBindingFlags.HasFlag(BindingFlags.NonPublic) && (!info.mMethodFlags.HasFlag(.Public)));
						if (matches)
							break;
					}
				}
				else
				{
					for (;;)
					{
						mIdx++;
						if (mIdx == mTypeInstance.[Friend]mMethodDataCount)
						{
							if (mBindingFlags.HasFlag(.DeclaredOnly))
								return false;
							if (mTypeInstance.[Friend]mBaseType == 0)
								return false;
							mTypeInstance = Type.[Friend]GetType(mTypeInstance.[Friend]mBaseType) as TypeInstance;
							mIdx = -1;
							continue;
						}	
						var methodData = &mTypeInstance.[Friend]mMethodDataPtr[mIdx];
						bool matches = (mBindingFlags.HasFlag(BindingFlags.Static) && (methodData.mFlags.HasFlag(.Static)));
						matches |= (mBindingFlags.HasFlag(BindingFlags.Instance) && (!methodData.mFlags.HasFlag(.Static)));
						matches |= (mBindingFlags.HasFlag(BindingFlags.Public) && (methodData.mFlags.HasFlag(.Public)));
						matches |= (mBindingFlags.HasFlag(BindingFlags.NonPublic) && (!methodData.mFlags.HasFlag(.Public)));
						if (matches)
							break;
					}
				}
		        return true;
		    }

		    public MethodInfo Current
		    {
		        get
		        {
					if (Compiler.IsComptime)
					{
						int64 nativeMethodHandle = Type.[Friend]Comptime_GetMethod((int32)mTypeInstance.TypeId, mIdx);
						return MethodInfo(mTypeInstance, nativeMethodHandle);
					}
					else
					{
						var methodData = &mTypeInstance.[Friend]mMethodDataPtr[mIdx];
			            return MethodInfo(mTypeInstance, methodData);
					}
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

	[Obsolete("Use MethodInfo", false)]
	typealias ComptimeMethodInfo = MethodInfo;
}
