using System.Globalization;

namespace System
{
#unwarn
	struct Int32 : int32, IInteger, ISigned, IHashable, IFormattable, IIsNaN
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case Overflow;
			case InvalidChar(int32 partialResult);
		}

		public const int32 MaxValue = 0x7FFFFFFF;
		public const int32 MinValue = -0x80000000;

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

		public static Self operator-(Self value)
		{
			return (SelfBase)value;
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
			int32 valLeft = (int32)this;
			bool isNeg = true;
			if (valLeft >= 0)
			{
				valLeft = -valLeft;
				isNeg = false;
			}
			while (valLeft < 0)
			{
				strChars[char8Idx] = (char8)('0' &- (valLeft % 10));
				valLeft /= 10;
				char8Idx--;
			}
			if (char8Idx == 14)
				strChars[char8Idx--] = '0';
			if (isNeg)
				strChars[char8Idx--] = '-';
			char8* char8Ptr = &strChars[char8Idx + 1];
			strBuffer.Append(char8Ptr);
		}

		void ToString(String strBuffer, int minNumerals)
		{
			// Dumb, make better.
			char8[] strChars = scope:: char8[16];
			int32 char8Idx = 14;
			int32 valLeft = (int32)this;
			bool isNeg = true;
			int minNumeralsLeft = minNumerals;
			if (valLeft >= 0)
			{
				valLeft = -valLeft;
				isNeg = false;
			}
			while ((valLeft < 0) || (minNumeralsLeft > 0))
			{
				strChars[char8Idx] = (char8)('0' - (valLeft % 10));
				valLeft /= 10;
				char8Idx--;
				minNumeralsLeft--;
			}
			if (char8Idx == 14)
				strChars[char8Idx--] = '0';
			if (isNeg)
				strChars[char8Idx--] = '-';
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
				NumberFormatter.NumberToString(format, (int32)this, formatProvider, outString);
			}
		}

		public static Result<int32, ParseError> Parse(StringView val, NumberStyles style = .Number, CultureInfo cultureInfo = null)
		{
			if (val.IsEmpty)
				return .Err(.NoValue);

			bool isNeg = false;
			int32 result = 0;

			int32 radix = style.HasFlag(.Hex) ? 0x10 : 10;

			for (int32 i = 0; i < val.Length; i++)
			{
				char8 c = val[i];

				if ((i == 0) && (c == '-'))
				{
					isNeg = true;
					continue;
				}

				if ((c >= '0') && (c <= '9'))
				{
					result &*= radix;
					result &+= (int32)(c - '0');
				}
				else if ((c >= 'a') && (c <= 'f'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar(result));
					result &*= radix;
					result &+= c - 'a' + 10;
				}
				else if ((c >= 'A') && (c <= 'F'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar(result));
					result &*= radix;
					result &+= c - 'A' + 10;
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

				if (isNeg ? (uint32)result > (uint32)MinValue : (uint32)result > (uint32)MaxValue)
					return .Err(.Overflow);
			}

			return isNeg ? -result : result;
		}
	}
}
