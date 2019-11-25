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
				if (methodInfo.mMethodData.mName == methodName)
				{
					if (matched.mMethodData != null)
						return .Err(.MultipleResults);
				}
			}

			if (matched.mMethodData == null)
				return .Err(.NoResults);
			return .Ok(matched);
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
		        if (fieldData.mName == fieldName)
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
	}
}
