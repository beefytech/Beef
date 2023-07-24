using System.Reflection;
using System.Diagnostics;

namespace System
{
	[AttributeUsage(.Field | .StaticField)]
	struct BitfieldAttribute : Attribute, IOnFieldInit
	{
		public enum BitSpec
		{
			case Auto;
			case AutoAt(int pos);
			case AutoRev;
			case Bits(int bits);
			case BitsRev(int bits);
			case BitsAt(int bits, int pos);
			case BitRange(IndexRange range);
			case ValueRange(Range range);
			case ValueRangeRev(Range range);
			case ValueRangeAt(Range range, int pos);
		}

		public enum ProtectionKind
		{
			Private = 0,
			Public = 1,
			Protected = 2,
		}

		public enum AccessFlags
		{
			Read = 1,
			Write = 2
		}

		protected String mName;
		protected int32 mBitPos = -1;
		protected BitSpec mBitSpec;
		protected int32 mFieldIdx = 0;
		protected Type mBFieldType = null;
		protected ProtectionKind mProtection;
		protected AccessFlags mAccessFlags;

		public this(ProtectionKind protection, BitSpec bitSpec, String name, AccessFlags accessFlags = .Read | .Write)
		{
			mName = name;
			mBitSpec = bitSpec;
			mProtection = protection;
			mAccessFlags = accessFlags;
		}

		[Comptime]
		public void OnFieldInit(FieldInfo fieldInfo, Self* prev) mut
		{
			if (mBFieldType == null)
				mBFieldType = fieldInfo.FieldType;
			var buType = mBFieldType;
			if (buType.IsEnum)
				buType = buType.UnderlyingType;

			if (buType.IsFloatingPoint)
				Runtime.FatalError("Invalid bitfield type");

			bool isRev = false;
			int32 bitCount = 0;
			int32 underlyingBitCount = fieldInfo.FieldType.Size * 8;

			int32 GetBitSize(int min, int max)
			{
				var min;
				var max;
				if (min < 0)
					min = ~min;
				if (max < 0)
					max = ~max;
				uint64 value = (uint64)min | (uint64)max;

				int32 bitCount = 1;
				if (buType.IsSigned)
					bitCount++;

				while ((value >>= 1) != 0)
					bitCount++;

				return bitCount;
			}

			bool hasRange = false;
			bool rangeCheckMin = false;
			bool rangeCheckMax = false;
			int minVal = 0;
			int maxVal = 0;

			switch (mBitSpec)
			{
			case .Bits(let bits):
				bitCount = (.)bits;
			case .BitsRev(let bits):
				bitCount = (.)bits;
				isRev = true;
			case .BitsAt(let bits, let pos):
				bitCount = (.)bits;
				mBitPos = (.)pos;
			case .BitRange(let range):
				bitCount = (.)range.GetLength(underlyingBitCount);
				mBitPos = (.)range.Start.Get(underlyingBitCount);
			case .ValueRange(let range):
				minVal = range.Start;
				maxVal = range.End - 1;
				hasRange = true;
				bitCount = GetBitSize(minVal, maxVal);
			case .ValueRangeRev(let range):
				minVal = range.Start;
				maxVal = range.End - 1;
				hasRange = true;
				bitCount = GetBitSize(minVal, maxVal);
				isRev = true;
			case .ValueRangeAt(let range, int pos):
				bitCount = GetBitSize(range.Start, range.End - 1);
				bitCount = (.)pos;
			default:
				Runtime.FatalError("Invalid bitspec");
			}

			if ((prev != null) && (prev.mFieldIdx == fieldInfo.FieldIdx))
			{
				if (mBitPos == -1)
				{
					mBitPos = prev.mBitPos;
					if (isRev)
						mBitPos -= bitCount;
				}
			}

			if (mBitPos == -1)
				mBitPos = isRev ? underlyingBitCount - bitCount : 0;

			int bitSize = fieldInfo.FieldType.Size * 8;
			if ((mBitPos < 0) || (mBitPos + bitCount > bitSize))
				Runtime.FatalError("Bitfield exceeds bounds of underlying value");

			String str = scope .();
			if (mProtection == .Public)
				str.Append("public ");
			else if (mProtection == .Protected)
				str.Append("protected ");
			if (fieldInfo.IsStatic)
				str.Append("static ");
			bool wantsMut = fieldInfo.DeclaringType.IsStruct && !fieldInfo.IsStatic;

			void TypeStr(String str, Type t)
			{
				if (t.IsPrimitive)
					t.GetFullName(str);
				else
					str.AppendF($"comptype({(int)t.TypeId})");
			}

			String uTypeStr = TypeStr(.. scope .(), fieldInfo.FieldType);
			String bTypeStr = TypeStr(.. scope .(), mBFieldType);
			int mask = ((1 << mBitPos) << bitCount) - (1 << mBitPos);

			if ((!hasRange) && (mBFieldType.IsSigned))
			{
				minVal = -(1<<(bitCount-1));
				maxVal = (1<<(bitCount-1))-1;
			}
			else if (!hasRange)
			{
				maxVal = (1<<(bitCount))-1;
			}

			str.AppendF($"{bTypeStr} {mName}\n{{\n");

			if (mAccessFlags.HasFlag(.Read))
			{
				str.Append("\t[Inline]\n\tget => ");
				if (mBFieldType == typeof(bool))
					str.AppendF($"(({fieldInfo.Name} & {mask}) >> {mBitPos}) != 0;\n");
				else if (buType.IsSigned)
					str.AppendF($"(.)((int)((({fieldInfo.Name} & 0x{mask:X16}) >> {mBitPos}) ^ 0x{1 << (bitCount - 1):X}) - 0x{1 << (bitCount - 1):X});\n");
				else
					str.AppendF($"(.)(({fieldInfo.Name} & 0x{mask:X16}) >> {mBitPos});\n");
			}

			if (mBFieldType.IsInteger)
			{
				if (mBFieldType.IsSigned)
				{
					rangeCheckMin = minVal != -(1<<(mBFieldType.Size*8-1));
					rangeCheckMax = maxVal != (1<<(mBFieldType.Size*8-1))-1;
				}
				else
				{
					rangeCheckMin = minVal != 0;
					rangeCheckMax = maxVal != (1<<mBFieldType.Size*8)-1;
				}
			}

			if (mAccessFlags.HasFlag(.Write))
			{
				for (int pass < 2) 
				{
					if (!rangeCheckMin && !rangeCheckMax)
					{
						if (pass == 0)
						{
							str.AppendF($"\t[Inline]\n\tset{(wantsMut ? " mut" : "")}\n\t{{\n");
						}
						else
							break;
					}
					else
					{
						str.AppendF($"\t[Inline, {((pass == 0) ? "Checked" : "Unchecked")}]\n\tset{(wantsMut ? " mut" : "")}\n\t{{\n");
						if ((pass == 0) && (rangeCheckMin) && (rangeCheckMax))
							str.AppendF($"\t\tRuntime.Assert((value >= {minVal}) && (value <= {maxVal}));\n");
						else if ((pass == 0) && (rangeCheckMin))
							str.AppendF($"\t\tRuntime.Assert(value >= {minVal});\n");
						else if ((pass == 0) && (rangeCheckMax))
							str.AppendF($"\t\tRuntime.Assert(value <= {maxVal});\n");
					}

					if (mBFieldType == typeof(bool))
						str.AppendF($"\t\t{fieldInfo.Name} = ({fieldInfo.Name} & ({uTypeStr})~0x{mask:X16}) | (value ? 0x{mask:X16} : 0);\n");
					else
						str.AppendF($"\t\t{fieldInfo.Name} = ({fieldInfo.Name} & ({uTypeStr})~0x{mask:X16}) | ((({uTypeStr})value << {mBitPos}) & ({uTypeStr})0x{mask:X16});\n");

					str.Append("\t}\n");
				}
			}

			str.Append("}\n");

			Compiler.EmitTypeBody(fieldInfo.DeclaringType, str);

			if (!isRev)
				mBitPos += bitCount;
			mFieldIdx = (.)fieldInfo.FieldIdx;
		}
	}

	[AttributeUsage(.Field | .StaticField)]
	struct BitfieldAttribute<TField> : BitfieldAttribute
	{
		int GetBitCount()
		{
			int val = typeof(TField).BitSize;
			return val;
		}

		BitSpec FixSpec(BitSpec spec)
		{
			switch (spec)
			{
			case .Auto:
				return .Bits(typeof(TField).BitSize);
			case .AutoRev():
				return .BitsRev(typeof(TField).BitSize);
			case .AutoAt(let pos):
				return .BitsAt(typeof(TField).BitSize, pos);
			default:
				return spec;
			}
		}

		public this(ProtectionKind protection, String name, AccessFlags accessFlags = .Read | .Write) : base(protection, FixSpec(.Auto), name, accessFlags)
		{
			mBFieldType = typeof(TField);
		}

		public this(ProtectionKind protection, BitSpec bitSpec, String name, AccessFlags accessFlags = .Read | .Write) : base(protection, FixSpec(bitSpec), name, accessFlags)
		{
			mBFieldType = typeof(TField);
		}
	}
}
