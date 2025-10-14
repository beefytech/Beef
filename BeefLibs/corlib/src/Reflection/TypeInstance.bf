using System;
using System.Reflection;

namespace System
{
	extension Type
	{
		public enum MethodError
		{
			NoResults,
			MultipleResults
		}

		public virtual MethodInfo.Enumerator GetMethods(BindingFlags bindingFlags = cDefaultLookup)
		{
		    return MethodInfo.Enumerator(null, bindingFlags);
		}

		public virtual Result<MethodInfo, MethodError> GetMethod(StringView methodName, BindingFlags bindingFlags = cDefaultLookup)
		{
			if (Compiler.IsComptime)
			{
				MethodInfo matched = default;
				for (let methodInfo in GetMethods(bindingFlags))
				{
					if (Compiler.IsComptime)
					{
						if (methodInfo.Name == methodName)
						{
							if (matched.[Friend]mData.mMethodData != null)
								return .Err(.MultipleResults);
							else
							    matched = methodInfo;
						}
					}
					else
					{
						if (methodInfo.[Friend]mData.mMethodData.[Friend]mName == methodName)
						{
							if (matched.[Friend]mData.mMethodData != null)
								return .Err(.MultipleResults);
							else
							    matched = methodInfo;
						}
					}
				}
	
				if (matched.[Friend]mData.mComptimeMethodInstance == 0)
					return .Err(.NoResults);
				
				return .Ok(matched);
			}
			else
			{
				MethodInfo matched = default;
				for (let methodInfo in GetMethods(bindingFlags))
				{
					if (methodInfo.[Friend]mData.mMethodData.[Friend]mName == methodName)
					{
						if (matched.[Friend]mData.mMethodData != null)
							return .Err(.MultipleResults);
						else
						    matched = methodInfo;
					}
				}

				if (matched.[Friend]mData.mMethodData == null)
					return .Err(.NoResults);

				
				return .Ok(matched);
			}
		}

		public virtual Result<MethodInfo, MethodError> GetMethod(int methodIdx)
		{
			if (Compiler.IsComptime)
			{
				int64 nativeMethod = Comptime_GetMethod((.)TypeId, (.)methodIdx);
				if (nativeMethod == 0)
					return .Err(.NoResults);

				return MethodInfo(this as TypeInstance, nativeMethod);
			}
			return .Err(.NoResults);
		}

		public virtual Result<Object> CreateObject(IRawAllocator allocator)
		{
			return .Err;
		}

		public Result<Object> CreateObject() => CreateObject(null);

		public virtual Result<void*> CreateValue()
		{
			return .Err;
		}

		public virtual Result<void*> CreateValueDefault()
		{
			return .Err;
		}
	}
}

namespace System.Reflection
{
	extension TypeInstance
	{
		public override MethodInfo.Enumerator GetMethods(BindingFlags bindingFlags = cDefaultLookup)
		{
		    return MethodInfo.Enumerator(this, bindingFlags);
		}

		/*[Comptime]
		public override ComptimeMethodInfo.Enumerator GetMethods(BindingFlags bindingFlags = cDefaultLookup)
		{
		    return ComptimeMethodInfo.Enumerator(this, bindingFlags);
		}*/

		public override Result<MethodInfo, MethodError> GetMethod(int methodIdx)
		{
			if ((methodIdx < 0) || (methodIdx >= mMethodDataCount))
				return .Err(.NoResults);
			return MethodInfo(this, &mMethodDataPtr[methodIdx]);
		}

		public override Result<Object> CreateObject(IRawAllocator allocator)
		{
			if (mTypeClassVData == null)
				return .Err;

			MethodInfo methodInfo = default;
			MethodInfo calcAppendMethodInfo = default;
			if (!IsBoxed)
			{
				for (int methodId < mMethodDataCount)
				{
					let methodData = &mMethodDataPtr[methodId];
					if ((!methodData.mFlags.HasFlag(.Constructor)) || (methodData.mFlags.HasFlag(.Static)))
					{
						if (((Object)methodData.mName == "this$calcAppend") && (methodData.mParamCount == 0))
							calcAppendMethodInfo = .(this, methodData);
						continue;
					}
					if (methodData.mParamCount == 0)
					{
						methodInfo = .(this, methodData);
						break;
					}
					else if ((methodData.mParamCount == 1) && (methodData.mParamData[0].mParamFlags.HasFlag(.AppendIdx)))
						methodInfo = .(this, methodData);
				}

				if (!methodInfo.IsInitialized)
					return .Err;
				if ((methodInfo.[Friend]mData.mMethodData.mParamCount != 0) && (!calcAppendMethodInfo.IsInitialized))
					return .Err;
			}
			Object obj;

			let objType = typeof(Object) as TypeInstance;

			int allocSize = mInstSize;
			bool hasAppendAlloc = (methodInfo.IsInitialized) && (methodInfo.[Friend]mData.mMethodData.mParamCount != 0);

			if (hasAppendAlloc)
			{
				switch (calcAppendMethodInfo.Invoke(null))
				{
				case .Err:
					return .Err;
				case .Ok(let val):
					allocSize += val.Get<int>();
				}
			}

#if BF_ENABLE_REALTIME_LEAK_CHECK
			int32 stackCount = Compiler.Options.AllocStackCount;
			if (mAllocStackCountOverride != 0)
				stackCount = mAllocStackCountOverride;

			if (allocator != null)
			{
				int stackTraceSize = Internal.Dbg_PrepareStackTrace(allocSize, stackCount);
				int totalAllocSize = allocSize + stackTraceSize;
				obj = Internal.UnsafeCastToObject(allocator.Alloc(totalAllocSize, mInstAlign));
				Internal.Dbg_ObjectAllocatedEx(obj, allocSize, (.)(void*)mTypeClassVData, 0);
			}
			else
			{
				obj = Internal.Dbg_ObjectAlloc(mTypeClassVData, allocSize, mInstAlign, stackCount, 0);
			}
#else
			void* mem;
			if (allocator != null)
				mem = allocator.Alloc(allocSize, mInstAlign);
			else
				mem = new [Align(16)] uint8[allocSize]* (?);
			obj = Internal.UnsafeCastToObject(mem);
			*(void**)mem = (void*)mTypeClassVData;
#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			Internal.Dbg_ObjectAllocated(obj, allocSize, (.)(void*)mTypeClassVData);
			if (allocator == null)
				*(int*)mem |= 0x04/*BfObjectFlag_Allocate*/;
#else
			*(void**)mem = (void*)mTypeClassVData;
#endif

#endif
			Internal.MemSet((uint8*)Internal.UnsafeCastToPtr(obj) + objType.mInstSize, 0, mInstSize - objType.mInstSize);
			if (methodInfo.IsInitialized)
			{
				Object[] args = null;
				if (hasAppendAlloc)
					args = scope:: .(scope:: box ((int)Internal.UnsafeCastToPtr(obj) + mInstSize));
				else
					args = scope:: Object[0];
					
				if (methodInfo.Invoke(obj, params args) case .Err)
				{
					delete obj;
					return .Err;
				}
			}

			return obj;
		}

		public override Result<void*> CreateValue()
		{
			if (!IsValueType)
				return .Err;

			MethodInfo methodInfo = default;
			for (int methodId < mMethodDataCount)
			{
				let methodData = &mMethodDataPtr[methodId];
				if (!methodData.mFlags.HasFlag(.Constructor))
					continue;
				if (methodData.mParamCount != 0)
					continue;
				
				methodInfo = .(this, methodData);
				break;
			}

			if (!methodInfo.IsInitialized)
				return .Err;

			void* data = new [Align(16)] uint8[mInstSize]* (?);

			if (methodInfo.Invoke(data) case .Err)
			{
				delete data;
				return .Err;
			}

			return data;
		}

		public override Result<void*> CreateValueDefault()
		{
			if (!IsValueType)
				return .Err;

			void* data = new uint8[mInstSize]*;
			return data;
		}
	}
}
