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
		public override Result<FieldInfo> GetField(String fieldName)
		{
		    for (int32 i = 0; i < mFieldDataCount; i++)
		    {
		        FieldData* fieldData = &mFieldDataPtr[i];
		        if (fieldData.[Friend]mName == fieldName)
		            return FieldInfo(this, fieldData);
		    }
		    return .Err;
		}

		public override FieldInfo.Enumerator GetFields(BindingFlags bindingFlags = cDefaultLookup)
		{
		    return FieldInfo.Enumerator(this, bindingFlags);
		}

		public override MethodInfo.Enumerator GetMethods(BindingFlags bindingFlags = cDefaultLookup)
		{
		    return MethodInfo.Enumerator(this, bindingFlags);
		}

		public override Result<Object> CreateObject()
		{
			if (mTypeClassVData == null)
				return .Err;

			MethodInfo methodInfo = default;
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
			Object obj = Internal.Dbg_ObjectAlloc(mTypeClassVData, mInstSize, mInstAlign, 1);

			if (methodInfo.Invoke(obj) case .Err)
			{
				delete obj;
				return .Err;
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

			void* data = new uint8[mInstSize]*;

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
