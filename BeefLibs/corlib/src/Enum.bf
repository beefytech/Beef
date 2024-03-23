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
		struct Entry : this(int hashCode, String name, T value, bool hasMore)
		{
			
		}

		[OnCompile(.TypeInit), Comptime]
		public static void OnTypeInit()
		{
			String entryTableCode = scope .();
			String lookupTableCode = scope .();
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

			bool useHashTable = (!hasPayload) && (caseList.Count > 0);
			if ((!hasPayload) && (caseList.Count > 0))
			{
				Type underlyingType = typeof(T).UnderlyingType;
				if (underlyingType.Size < 4)
					underlyingType = typeof(int);
				code.AppendF($"\tif (((str[0].IsDigit) || (str[0] == '-')) && ({underlyingType}.Parse(str) case let .Ok(iVal)))\n");
				code.Append("\t\treturn .Ok((.)iVal);\n");

				if (useHashTable)
				{
					code.Append("\tint entryIdx = -1;\n");
					code.Append("\tint hashCode;\n");
					code.Append("\tStringView checkStr;\n");
				}
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

			int bucketsTried = 0;

			int tableEntryIdx = 0;

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

				List<int> hashList = scope .();
				hashList.Resize(caseList.Count);
				String fieldName = scope .(64);
				for (int caseIdx < caseList.Count)
				{
					var fieldInfo = caseList[caseIdx];
					fieldName.Clear();
					GetFieldName(fieldInfo, fieldName);
					hashList[caseIdx] = fieldName.GetHashCode();
				}

				if (ignoreCasePass == 0)
					code.Append("\tif (ignoreCase)\n");
				else
					code.Append("\telse\n");
				code.Append("\t{\n");

				bool[] bucketUsed = new bool[caseList.Count] (?);
				defer delete bucketUsed;

				int numBuckets = caseList.Count;
				if (numBuckets < 4)
				{
					// Don't bother with the switch
					numBuckets = 1;
				}

				// Try for at least a 75% occupancy in the non-table case to ensure we get a jump table
				float minBucketOccupancy = useHashTable ? 0.65f : 0.75f;
				while (numBuckets > 2)
				{
					bucketsTried++;
					Internal.MemSet(bucketUsed.Ptr, 0, numBuckets);

					int numBucketsUsed = 0;
					for (int caseIdx < caseList.Count)
					{
						int bucketNum = (hashList[caseIdx] & Int.MaxValue) % numBuckets;
						if (!bucketUsed[bucketNum])
						{
							numBucketsUsed++;
							bucketUsed[bucketNum] = true;
						}
					}

					if (numBucketsUsed >= numBuckets * minBucketOccupancy)
						break;
					numBuckets -= numBuckets / 20 + 1;
				}

				List<int> startFields = scope .();
				startFields.Resize(numBuckets, -1);
				List<int> nextCases = scope .();
				nextCases.Resize(caseList.Count, -1);

				for (int caseIdx < caseList.Count)
				{
					int bucketNum = (hashList[caseIdx] & Int.MaxValue) % numBuckets;
					nextCases[caseIdx] = startFields[bucketNum];
					startFields[bucketNum] = caseIdx;
				}

				StringView strName;
				if (hasPayload)
					strName = (ignoreCasePass == 0) ? "checkStr" : "nameStr";
				else
					strName = (ignoreCasePass == 0) ? "checkStr" : "str";
				
				if (useHashTable)
				{
					int endingIdx = tableEntryIdx + caseList.Count;
					String idxType = "int32";
					if (endingIdx <= Int8.MaxValue)
						idxType = "int8";
					else if (endingIdx <= Int16.MaxValue)
						idxType = "int16";
					lookupTableCode.AppendF($"const {idxType}[{numBuckets}] cLookupTable{ignoreCasePass} = .(");
					if (tableEntryIdx == 0)
						entryTableCode.AppendF($"const Entry[{caseList.Count * 2}] cEntryTable = .(");

					if (ignoreCasePass == 0)
						code.AppendF($"\t\tcheckStr = scope:: String(str)..ToLower();\n");
					else
						code.Append("\t\tcheckStr = str;\n");
					code.Append("\t\thashCode = checkStr.GetHashCode();\n");
					code.AppendF($"\t\tentryIdx = cLookupTable{ignoreCasePass}[(hashCode & Int.MaxValue) % {numBuckets}];\n");
				}
				else
				{
					if (ignoreCasePass == 0)
						code.AppendF($"\t\tString checkStr = scope .({(hasPayload ? "nameStr" : "str")})..ToLower();\n");
					code.AppendF($"\t\tint hashCode = {strName}.GetHashCode();\n");
					if (numBuckets > 1)
						code.AppendF($"\t\tswitch ((hashCode & Int.MaxValue) % {numBuckets})\n");
					code.Append("\t\t{\n");
				}

				for (int bucketIdx < numBuckets)
				{
					if ((!hasPayload) && (bucketIdx > 0))
						lookupTableCode.Append(", ");

					int bucketEntryCount = 0;
					int caseIdx = startFields[bucketIdx];
					while (caseIdx != -1)
					{
						if (useHashTable)
						{
							if (bucketEntryCount == 0)
								tableEntryIdx.ToString(lookupTableCode);
						}
						else if ((bucketEntryCount == 0) && (numBuckets > 1))
							code.AppendF($"\t\tcase {bucketIdx}:\n");

						var fieldInfo = caseList[caseIdx];
						fieldName.Clear();
						GetFieldName(fieldInfo, fieldName);
						var fieldTypeInst = fieldInfo.FieldType as TypeInstance;
						bool caseHasPayload = (fieldTypeInst.IsTuple) && (fieldTypeInst.FieldCount > 0);

						int hashCode = fieldName.GetHashCode();
						if (useHashTable)
						{
							if (tableEntryIdx > 0)
								entryTableCode.Append(",");
							entryTableCode.Append("\n\t.(");
							hashCode.ToString(entryTableCode);
							entryTableCode.Append(", \"");
							entryTableCode.Append(fieldName);
							entryTableCode.Append("\", .");
							entryTableCode.Append(fieldInfo.Name);
							entryTableCode.Append(", ");
							entryTableCode.Append((nextCases[caseIdx] == -1) ? "false" : "true");
							entryTableCode.Append(")");

							tableEntryIdx++;
						}
						else if (hasPayload)
						{
							code.AppendF($"\t\t\tif ((hashCode == {hashCode}) && ({strName} == \"{fieldName}\") && ({caseHasPayload ? "!" : ""}argStr.IsEmpty))\n");
							if (caseHasPayload)
								code.AppendF($"\t\t\t\tmatchIdx = {caseIdx};\n");
							else
								code.AppendF($"\t\t\t\treturn .Ok(.{fieldInfo.Name});\n");
						}
						else
						{
							code.Append("\t\t\tif ((hashCode == ");
							hashCode.ToString(code);
							code.Append(") && (");
							code.Append(strName);
							code.Append(" == \"");
							code.Append(fieldName);
							code.Append("\"))\n");
							code.Append("\t\t\t\treturn .Ok(.");
							code.Append(fieldInfo.Name);
							code.Append(");\n");
						}

						caseIdx = nextCases[caseIdx];
						bucketEntryCount++;
					}

					if ((!hasPayload) && (bucketEntryCount == 0))
					{
						lookupTableCode.Append("-1");
					}
				}

				if (!useHashTable)
					code.Append("\t\t}\n");
				code.Append("\t}\n");

				if (!lookupTableCode.IsEmpty)
				{
					lookupTableCode.Append(");\n\n");
				}
			}

			if (useHashTable)
			{
				code.Append("""
								while (entryIdx >= 0)
								{
									var entry = cEntryTable[entryIdx];
									if ((hashCode == entry.hashCode) && (checkStr == entry.name))
										return .Ok(entry.value);
									if (!entry.hasMore)
										break;
									entryIdx++;
								}

							""");
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

			if (!entryTableCode.IsEmpty)
			{
				entryTableCode.Append(");\n\n");
				Compiler.EmitTypeBody(typeof(Self), entryTableCode);
			}
			if (!lookupTableCode.IsEmpty)
				Compiler.EmitTypeBody(typeof(Self), lookupTableCode);

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