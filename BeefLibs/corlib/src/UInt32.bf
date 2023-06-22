using System.Globalization;

namespace System
{
#unwarn
	struct UInt32 : uint32, IInteger, IUnsigned, IHashable, IFormattable, IIsNaN
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case Overflow;
			case InvalidChar(uint32 partialResult);
		}

		public const uint32 MaxValue = 0xFFFFFFFFL;
		public const uint32 MinValue = 0;

		public static int operator<=>(Self a, Self b)
		{
			return (SelfBase)a <=> (SelfBase)b;
		}

		public static Self operator+(Self lhs, Self rhs)
		{
			return (SelfBase)lhs + (SelfBase)rhs;
		}

		public static Self operator-(Self lhs, Self rhs)
		{
			return (SelfBase)lhs - (SelfBase)rhs;
		}

		public static Self operator*(Self lhs, Self rhs)
		{
			return (SelfBase)lhs * (SelfBase)rhs;
		}

		public static Self operator/(Self lhs, Self rhs)
		{
			return (SelfBase)lhs / (SelfBase)rhs;
		}

		public int GetHashCode()
		{
			return (int)this;
		}

		bool IIsNaN.IsNaN
		{
			[SkipCall]
			get
			{
				return false;
			}
		}

		public override void ToString(String strBuffer)
		{
			// Dumb, make better.
			char8[] strChars = scope:: char8[16];
			int32 char8Idx = 14;
			uint32 valLeft = (uint32)this;
			while (valLeft > 0)
			{
				strChars[char8Idx] = (char8)('0' + (valLeft % 10));
				valLeft /= 10;
				char8Idx--;
			}
			if (char8Idx == 14)
				strChars[char8Idx--] = '0';
			char8* char8Ptr = &strChars[char8Idx + 1];
			strBuffer.Append(char8Ptr);
		}

		void ToString(String strBuffer, int minNumerals)
		{
			// Dumb, make better.
			char8[] strChars = scope:: char8[16];
			int32 char8Idx = 14;
			uint32 valLeft = (uint32)this;
			int minNumeralsLeft = minNumerals;
			while ((valLeft > 0) || (minNumeralsLeft > 0))
			{
				strChars[char8Idx] = (char8)('0' + (valLeft % 10));
				valLeft /= 10;
				char8Idx--;
				minNumeralsLeft--;
			}
			if (char8Idx == 14)
				strChars[char8Idx--] = '0';
			char8* char8Ptr = &strChars[char8Idx + 1];
			strBuffer.Append(char8Ptr);
		}

		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if(format == null || format.IsEmpty)
			{
				ToString(outString);
			}
			else
			{
				NumberFormatter.NumberToString(format, (uint32)this, formatProvider, outString);
			}
		}

		public static Result<uint32, ParseError> Parse(StringView val, NumberStyles style = .Number, CultureInfo cultureInfo = null)
		{
			if (val.IsEmpty)
				return .Err(.NoValue);

			uint32 result = 0;
			uint32 prevResult = 0;

			uint32 radix = style.HasFlag(.Hex) ? 0x10 : 10;

			for (int32 i = 0; i < val.Length; i++)
			{
				char8 c = val[i];

				if ((c >= '0') && (c <= '9'))
				{
					result &*= radix;
					result &+= (uint32)(c - '0');
				}
				else if ((c >= 'a') && (c <= 'f'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar(result));
					result &*= radix;
					result &+= (uint32)(c - 'a' + 10);
				}
				else if ((c >= 'A') && (c <= 'F'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar(result));
					result &*= radix;
					result &+= (uint32)(c - 'A' + 10);
				}
				else if ((c == 'X') || (c == 'x'))
				{
					if ((!style.HasFlag(.AllowHexSpecifier)) || (i == 0) || (result != 0))
						return .Err(.InvalidChar(result));
					radix = 0x10;
				}
				else if (c == '\'')
				{
					// Ignore
				}
				else if ((c == '+') && (i == 0))
				{
					// Ignore
				}
				else
					return .Err(.InvalidChar(result));

				if (result < prevResult)
					return .Err(.Overflow);
				prevResult = result;
			}

			return result;
		}
	}
}
