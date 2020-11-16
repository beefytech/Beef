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
			MethodInfo matched = default;
			for (let methodInfo in GetMethods(bindingFlags))
			{
				if (methodInfo.[Friend]mMethodData.[Friend]mName == methodName)
				{
					if (matched.[Friend]mMethodData != null)
						return .Err(.MultipleResults);
					else
					        matched = methodInfo;
				}
			}

			if (matched.[Friend]mMethodData == null)
				return .Err(.NoResults);
			return .Ok(matched);
		}

		public virtual Result<Object> CreateObject()
		{
			return .Err;
		}

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

		public override Result<Object> CreateObject()
		{
			if (mTypeClassVData == null)
				return .Err;

			MethodInfo methodInfo = default;
			if (!IsBoxed)
			{
				for (int methodId < mMethodDataCount)
				{
					let methodData = &mMethodDataPtr[methodId];
					if ((!methodData.mFlags.HasFlag(.Constructor)) || (methodData.mFlags.HasFlag(.Static)))
						continue;
					if (methodData.mParamCount != 0)
						continue;
					
					methodInfo = .(this, methodData);
					break;
				}

				if (!methodInfo.IsInitialized)
					return .Err;
			}
			Object obj;

			let objType = typeof(Object) as TypeInstance;

#if BF_ENABLE_OBJECT_DEBUG_FLAGS
			obj = Internal.Dbg_ObjectAlloc(mTypeClassVData, mInstSize, mInstAlign, 1);
#else
			void* mem = new [Align(16)] uint8[mInstSize]* (?);
			obj = Internal.UnsafeCastToObject(mem);
			obj.[Friend]mClassVData = (.)(void*)mTypeClassVData;
#endif
			Internal.MemSet((uint8*)Internal.UnsafeCastToPtr(obj) + objType.mInstSize, 0, mInstSize - objType.mInstSize);
			if (methodInfo.IsInitialized)
			{
				if (methodInfo.Invoke(obj) case .Err)
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
