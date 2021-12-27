using System.Reflection;

namespace System
{
	struct Enum
	{
		public static void EnumToString(Type type, String strBuffer, int64 iVal)
		{
			for (var field in type.GetFields())
			{
				if (field.[Friend]mFieldData.[Friend]mData == iVal)
				{
					strBuffer.Append(field.Name);
					return;
				}
			}

			((int32)iVal).ToString(strBuffer);
		}

		public static Result<T> Parse<T>(StringView str, bool ignoreCase = false) where T : enum
		{
			var typeInst = (TypeInstance)typeof(T);
			for (var field in typeInst.GetFields())
			{
				if (str.Equals(field.[Friend]mFieldData.mName, ignoreCase))
					return .Ok(*((T*)(&field.[Friend]mFieldData.mData)));
			}

			return .Err;
		}

		public static bool IsDefined<T>(T value)
			where T : enum
		{
			var typeInst = (TypeInstance)typeof(T);
			for (var field in typeInst.GetFields())
			{
				if (field.[Friend]mFieldData.[Friend]mData == (.)value)
					return true;
			}

			return false;
		}

		public static readonly EnumValuesEnumerator<TEnum> GetValues<TEnum>()
			where TEnum : enum
		{
		    return .();
		}
		
		public static readonly EnumNamesEnumerator<TEnum> GetNames<TEnum>()
			where TEnum : enum
		{
		    return .();
		}

		private struct EnumFieldsEnumeratorWrapper<TEnum>
			where TEnum : enum
		{
			FieldInfo.Enumerator mEnumerator;

			public this()
			{
				mEnumerator = typeof(TEnum).GetFields();
			}

			public int Index
			{
				get
				{
					return mEnumerator.Index;
				}				
			}

			public int Count
			{
				get
				{
					return mEnumerator.mTypeInstance.[Friend]mFieldDataCount;
				}				
			}

			public FieldInfo Current
			{
				get
				{
					return (.)mEnumerator.Current;
				}
			}

			public bool MoveNext() mut
			{
				return mEnumerator.MoveNext();
			}

			public void Dispose()
			{
			}

			public Result<FieldInfo> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public struct EnumValuesEnumerator<TEnum> : EnumFieldsEnumeratorWrapper<TEnum>, IEnumerator<TEnum>
			where TEnum : enum
		{
			public new TEnum Current
			{
				get
				{
					return (.)base.Current.[Friend]mFieldData.[Friend]mData;
				}
			}

			public new Result<TEnum> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		public struct EnumNamesEnumerator<TEnum> : EnumFieldsEnumeratorWrapper<TEnum>, IEnumerator<StringView>
			where TEnum : enum
		{
			public new StringView Current
			{
				get
				{
					return (.)base.Current.[Friend]mFieldData.[Friend]mName;
				}
			}

			public new Result<StringView> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}
}
