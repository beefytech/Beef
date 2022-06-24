using System.Reflection;
using System.Collections;

namespace System
{
	struct Enum
	{
		[NoShow(true)]
		[Comptime(ConstEval=true)]
		public static int GetCount<T>() where T : Enum
		{
			int count = 0;
			for (var field in typeof(T).GetFields())
			{
				if (field.IsEnumCase)
					count++;
			}
			return count;
		}

		[NoShow(true)]
		[Comptime(ConstEval=true)]
		public static var GetMinValue<T>() where T : Enum
		{
			Compiler.SetReturnType(typeof(T));

			int? minValue = null;
			for (var field in typeof(T).GetFields())
			{
				if (field.IsEnumCase)
				{
					if (minValue == null)
						minValue = field.[Friend]mFieldData.mData;
					else
						minValue = Math.Min(minValue.Value, field.[Friend]mFieldData.mData);
				}
			}
			return minValue.ValueOrDefault;
		}

		[NoShow(true)]
		[Comptime(ConstEval=true)]
		public static var GetMaxValue<T>() where T : Enum
		{
			Compiler.SetReturnType(typeof(T));

			int? maxValue = null;
			for (var field in typeof(T).GetFields())
			{
				if (field.IsEnumCase)
				{
					if (maxValue == null)
						maxValue = field.[Friend]mFieldData.mData;
					else
						maxValue = Math.Max(maxValue.Value, field.[Friend]mFieldData.mData);
				}
			}
			if (maxValue == null)
				return -1;
			return maxValue.ValueOrDefault;
		}

		[NoShow(true)]
		public static void EnumToString(Type type, String strBuffer, int64 iVal)
		{
			for (var field in type.GetFields())
			{
				if (field.[Friend]mFieldData.mFlags.HasFlag(.EnumCase) &&
					*(int64*)&field.[Friend]mFieldData.[Friend]mData == iVal)
				{
					strBuffer.Append(field.Name);
					return;
				}
			}
			iVal.ToString(strBuffer);
		}

		[NoShow(true)]
		public static Result<T> Parse<T>(StringView str, bool ignoreCase = false) where T : enum
		{
			for (var (name, data) in GetEnumerator<T>())
			{
				if (str.Equals(name, ignoreCase))
					return .Ok(data);
				if (int64.Parse(str) case .Ok(let val) && val == (.)data)
					return .Ok(data);
			}

			return .Err;
		}

		[NoShow(true)]
		public static bool IsDefined<T>(T value) where T : Enum
			where T : enum
		{
			for (var data in GetValues<T>())
			{
				if (data == (.)value)
					return true;
			}

			return false;
		}

		[NoShow(true)]
		public static EnumEnumerator<TEnum> GetEnumerator<TEnum>()
			where TEnum : enum
		{
		    return .();
		}

		[NoShow(true)]
		public static EnumValuesEnumerator<TEnum> GetValues<TEnum>()
			where TEnum : enum
		{
		    return .();
		}

		[NoShow(true)]
		public static EnumNamesEnumerator<TEnum> GetNames<TEnum>()
			where TEnum : enum
		{
		    return .();
		}

		[NoShow(true)]
		private struct EnumFieldsEnumerator<TEnum>
			where TEnum : enum
		{
			TypeInstance mTypeInstance;
			int32 mIdx;

			public this()
			{
			    mTypeInstance = typeof(TEnum) as TypeInstance;
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

				TypeInstance.FieldData* fieldData = null;

				repeat
				{
					mIdx++;
					if (mIdx == mTypeInstance.[Friend]mFieldDataCount)
						return false;
					fieldData = &mTypeInstance.[Friend]mFieldDataPtr[mIdx];
				}
				while (!fieldData.mFlags.HasFlag(.EnumCase));

			    return true;
			}

			public FieldInfo Current
			{
			    get
			    {
					var fieldData = &mTypeInstance.[Friend]mFieldDataPtr[mIdx];
			        return FieldInfo(mTypeInstance, fieldData);
			    }
			}

			public int32 Index
			{
				get
				{
					return mIdx;
				}
			}

			public Result<FieldInfo> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		[NoShow(true)]
		public struct EnumEnumerator<TEnum> : EnumFieldsEnumerator<TEnum>, IEnumerator<(StringView name, TEnum value)>
			where TEnum : enum
		{
			public new (StringView name, TEnum value) Current
			{
				get
				{
					return ((.)base.Current.[Friend]mFieldData.[Friend]mName, (.)*(int64*)&base.Current.[Friend]mFieldData.[Friend]mData);
				}
			}

			public new Result<(StringView name, TEnum value)> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		[NoShow(true)]
		public struct EnumValuesEnumerator<TEnum> : EnumFieldsEnumerator<TEnum>, IEnumerator<TEnum>
			where TEnum : enum
		{
			public new TEnum Current
			{
				get
				{
					return (.)*(int64*)&base.Current.[Friend]mFieldData.[Friend]mData;
				}
			}

			public new Result<TEnum> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		[NoShow(true)]
		public struct EnumNamesEnumerator<TEnum> : EnumFieldsEnumerator<TEnum>, IEnumerator<StringView>
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
