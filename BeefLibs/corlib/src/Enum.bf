using System.Reflection;
using System.Collections;

namespace System
{
	struct Enum
	{
		[NoShow(true)]
		public static int GetCount(Type type)
		{
			int count = 0;
			for (var field in type.GetFields())
			{
				if (field.IsEnumCase)
					count++;
			}
			return count;
		}

		[NoShow(true)]
		[Comptime(ConstEval=true)]
		public static int GetCount<T>() where T : Enum
		{
			return GetCount(typeof(T));
		}

		[NoShow(true)]
		public static int64 GetMinValue(Type type)
		{
			int64? minValue = null;
			for (var data in GetValues(type))
			{
				if (minValue == null)
					minValue = data;
				else
					minValue = Math.Min(minValue.Value, data);
			}
			return minValue.ValueOrDefault;
		}

		[NoShow(true)]
		[Comptime(ConstEval=true)]
		public static var GetMinValue<T>() where T : Enum
		{
			Compiler.SetReturnType(typeof(T));
			return GetMinValue(typeof(T));
		}

		[NoShow(true)]
		public static int64 GetMaxValue(Type type)
		{
			int64? maxValue = null;
			for (var data in GetValues(type))
			{
				if (maxValue == null)
					maxValue = data;
				else
					maxValue = Math.Max(maxValue.Value, data);
			}
			return maxValue ?? -1;
		}

		[NoShow(true)]
		[Comptime(ConstEval=true)]
		public static var GetMaxValue<T>() where T : Enum
		{
			Compiler.SetReturnType(typeof(T));
			return GetMaxValue(typeof(T));
		}

		[NoShow(true)]
		public static void EnumToString(Type type, String strBuffer, int64 iVal)
		{
			for (var (name, data) in GetEnumerator(type))
			{
				if (data == iVal)
				{
					strBuffer.Append(name);
					return;
				}
			}
			iVal.ToString(strBuffer);
		}

		[NoShow(true)]
		public static Result<int64> Parse(Type type, StringView str, bool ignoreCase = false)
		{
			for (var (name, data) in GetEnumerator(type))
			{
				if (str.Equals(name, ignoreCase))
					return .Ok(data);
				if (int64.Parse(str) case .Ok(let val) && val == data)
					return .Ok(data);
			}

			return .Err;
		}

		[NoShow(true)]
		public static Result<T> Parse<T>(StringView str, bool ignoreCase = false) where T : enum
		{
			[IgnoreErrors(true)]
			{
				return EnumParser<T>.Parse(str, ignoreCase);
			}
#unwarn
			return .Err;
		}

		[NoShow(true)]
		public static bool IsDefined(Type type, int64 value)
		{
			for (var data in GetValues(type))
			{
				if (data == value)
					return true;
			}

			return false;
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
		public static EnumEnumerator GetEnumerator(Type type)
		{
		    return .(type);
		}

		[NoShow(true)]
		public static EnumEnumerator<TEnum> GetEnumerator<TEnum>()
			where TEnum : enum
		{
		    return .();
		}

		[NoShow(true)]
		public static EnumValuesEnumerator GetValues(Type type)
		{
		    return .(type);
		}

		[NoShow(true)]
		public static EnumValuesEnumerator<TEnum> GetValues<TEnum>()
			where TEnum : enum
		{
		    return .();
		}

		[NoShow(true)]
		public static EnumNamesEnumerator GetNames(Type type)
		{
		    return .(type);
		}

		[NoShow(true)]
		public static EnumNamesEnumerator<TEnum> GetNames<TEnum>()
			where TEnum : enum
		{
		    return .();
		}

		[NoShow(true)]
		private struct EnumFieldsEnumerator
		{
			TypeInstance mTypeInstance;
			int32 mIdx;

			public this(Type type)
			{
			    mTypeInstance = type as TypeInstance;
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
		public struct EnumEnumerator : EnumFieldsEnumerator, IEnumerator<(StringView name, int64 value)>
		{
			public this(Type type) : base(type)
			{
			}

			public new (StringView name, int64 value) Current
			{
				get
				{
					return ((.)base.Current.[Friend]mFieldData.[Friend]mName, *(int64*)&base.Current.[Friend]mFieldData.[Friend]mData);
				}
			}

			public new Result<(StringView name, int64 value)> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		[NoShow(true)]
		public struct EnumEnumerator<TEnum> : EnumFieldsEnumerator, IEnumerator<(StringView name, TEnum value)>
			where TEnum : enum
		{
			public this() : base(typeof(TEnum))
			{
			}

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
		public struct EnumValuesEnumerator : EnumFieldsEnumerator, IEnumerator<int64>
		{
			public this(Type type) : base(type)
			{
			}

			public new int64 Current
			{
				get
				{
					return *(int64*)&base.Current.[Friend]mFieldData.[Friend]mData;
				}
			}

			public new Result<int64> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}

		[NoShow(true)]
		public struct EnumValuesEnumerator<TEnum> : EnumFieldsEnumerator, IEnumerator<TEnum>
			where TEnum : enum
		{
			public this() : base(typeof(TEnum))
			{
			}

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
		public struct EnumNamesEnumerator : EnumFieldsEnumerator, IEnumerator<StringView>
		{
			public this(Type type) : base(type)
			{
			}

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

		[NoShow(true)]
		public struct EnumNamesEnumerator<TEnum> : EnumFieldsEnumerator, IEnumerator<StringView>
			where TEnum : enum
		{
			public this() : base(typeof(TEnum))
			{
			}

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

	class EnumParser<T>
	{
		[OnCompile(.TypeInit), Comptime]
		public static void OnTypeInit()
		{
			String code = scope .();

			code.Append("public static Result<T> Parse(StringView str, bool ignoreCase = false)\n");
			code.Append("{\n");
			code.Append("\tif (str.IsEmpty)\n");
			code.Append("\t\treturn .Err;\n");

			Type dscrType = typeof(int);
			int dscrOffset = 0;
			for (var fieldInfo in typeof(T).GetFields())
			{
				if (fieldInfo.Name == "$discriminator")
				{
					dscrOffset = fieldInfo.MemberOffset;
					dscrType = fieldInfo.FieldType;
				}
			}

			List<FieldInfo> caseList = scope .();

			bool hasPayload = false;

			for (var fieldInfo in typeof(T).GetFields())
			{
				if (!fieldInfo.IsEnumCase)
					continue;
				if (var fieldTypeInst = fieldInfo.FieldType as TypeInstance)
				{
					caseList.Add(fieldInfo);
					if ((fieldTypeInst.IsTuple) && (fieldTypeInst.FieldCount > 0))
					{
						hasPayload = true;
					}
				}
			}

			if (hasPayload)
				code.Append("\tT result = default;\n");

			if ((!hasPayload) && (caseList.Count > 0))
			{
				Type underlyingType = typeof(T).UnderlyingType;
				if (underlyingType.Size < 4)
					underlyingType = typeof(int);
				code.AppendF($"\tif (((str[0].IsDigit) || (str[0] == '-')) && ({underlyingType}.Parse(str) case let .Ok(iVal)))\n");
				code.Append("\t\treturn .Ok((.)iVal);\n");
			}
			else if (hasPayload)
			{
				code.Append("""
								StringView nameStr;
								StringView argStr = default;
								int parenPos = str.IndexOf('(');
								if (parenPos != -1)
								{
									nameStr = str.Substring(0, parenPos);
									argStr = str.Substring(parenPos + 1);
								}
								else
									nameStr = str;
								int matchIdx = -1;

							""");
			}

			for (int ignoreCasePass < 2)
			{
				if (caseList.IsEmpty)
					break;

				void GetFieldName(FieldInfo fieldInfo, String outName)
				{
					outName.Append(fieldInfo.Name);
					if (ignoreCasePass == 0)
						outName.ToLower();
				}

				if (ignoreCasePass == 0)
					code.Append("\tif (ignoreCase)\n");
				else
					code.Append("\telse\n");
				code.Append("\t{\n");

				int numBuckets = 1;
				Dictionary<int, CompactList<(int hash, int caseIdx)>> buckets = scope .();
				void ClearBucketList()
				{
					for (var list in buckets.Values)
						list.Dispose();
					buckets.Clear();
				}
				defer ClearBucketList();

				for (int bucketPass < caseList.Count * 2)
				{
					ClearBucketList();
					numBuckets = (bucketPass % 2 == 0) ? (caseList.Count + bucketPass / 2) : (caseList.Count - bucketPass / 2 - 1);

					for (int caseIdx < caseList.Count)
					{
						var fieldInfo = caseList[caseIdx];
						var fieldName = GetFieldName(fieldInfo, .. scope .(64));
						int hashCode = fieldName.GetHashCode() & Int.MaxValue;
						if (buckets.TryAdd(hashCode % numBuckets, ?, var entryList))
							*entryList = default;
						entryList.Add((hashCode, caseIdx));
					}

					// Try for at least a 75% occupancy to ensure we get a jump table
					if (buckets.Count >= numBuckets * 0.75)
						break;
				}

				StringView strName;
				if (hasPayload)
					strName = (ignoreCasePass == 0) ? "checkStr" : "nameStr";
				else
					strName = (ignoreCasePass == 0) ? "checkStr" : "str";
				if (ignoreCasePass == 0)
					code.AppendF($"\t\tString checkStr = scope .({(hasPayload ? "nameStr" : "str")})..ToLower();\n");
				code.AppendF($"\t\tint hashCode = {strName}.GetHashCode();\n");
				if (numBuckets > 1)
					code.AppendF($"\t\tswitch ((hashCode & Int.MaxValue) % {numBuckets})\n");
				code.Append("\t\t{\n");
				for (int bucketIdx < numBuckets)
				{
					if (!buckets.TryGet(bucketIdx, ?, var entryList))
						continue;
					if (numBuckets > 1)
						code.AppendF($"\t\tcase {bucketIdx}:\n");
					for (var entry in entryList)
					{
						var fieldInfo = caseList[entry.caseIdx];
						var fieldName = GetFieldName(fieldInfo, .. scope .(64));
						var fieldTypeInst = fieldInfo.FieldType as TypeInstance;
						bool caseHasPayload = (fieldTypeInst.IsTuple) && (fieldTypeInst.FieldCount > 0);
						
						if (hasPayload)
							code.AppendF($"\t\t\tif ((hashCode == {fieldName.GetHashCode()}) && ({strName} == \"{fieldName}\") && ({caseHasPayload ? "!" : ""}argStr.IsEmpty))\n");
						else
							code.AppendF($"\t\t\tif ((hashCode == {fieldName.GetHashCode()}) && ({strName} == \"{fieldName}\"))\n");
						if (caseHasPayload)
							code.AppendF($"\t\t\t\tmatchIdx = {entry.caseIdx};\n");
						else
							code.AppendF($"\t\t\t\treturn .Ok(.{fieldInfo.Name});\n");
					}
				}
				

				code.Append("\t\t}\n");
				code.Append("\t}\n");
			}

			if (hasPayload)
			{
				code.Append("\tswitch (matchIdx)\n");
				code.Append("\t{\n");
				for (var caseIdx < caseList.Count)
				{
					var fieldInfo = caseList[caseIdx];
					var fieldTypeInst = fieldInfo.FieldType as TypeInstance;
					bool caseHasPayload = (fieldTypeInst.IsTuple) && (fieldTypeInst.FieldCount > 0);

					if (caseHasPayload)
					{
						code.AppendF($"\tcase {caseIdx}:\n");
						code.AppendF($"\t\t*({dscrType}*)((uint8*)&result + {dscrOffset}) = {fieldInfo.MemberOffset};\n");
						code.AppendF($"\t\tvar itr = Try!(EnumFields(str.Substring({fieldInfo.Name.Length+1})));\n");
						for (var tupField in fieldTypeInst.GetFields())
							code.AppendF($"\t\tTry!(ParseValue(ref itr, ref *({tupField.FieldType}*)((uint8*)&result + {tupField.MemberOffset})));\n");
						code.Append("\t\treturn result;\n");
					}

				}
				code.Append("\t}\n");
			}

			code.Append("\treturn .Err;\n");
			code.Append("}\n");

			Compiler.EmitTypeBody(typeof(Self), code);
		}

		static Result<StringSplitEnumerator> EnumFields(StringView str)
		{
			var str;
			str.Trim();
			if (!str.EndsWith(')'))
				return .Err;
			str.RemoveFromEnd(1);
			return str.Split(',');
		}

		static Result<void> ParseValue<TValue>(ref StringSplitEnumerator itr, ref TValue value)
		{
			return .Err;
		}

		static Result<void> ParseValue<TValue>(ref StringSplitEnumerator itr, ref TValue value) where TValue : IParseable<TValue>
		{
			var str = Try!(itr.GetNext());
			str.Trim();
			value = Try!(TValue.Parse(str));
			return .Ok;
		}
	}
}