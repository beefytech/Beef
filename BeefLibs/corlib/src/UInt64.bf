using System.Globalization;

namespace System
{
	struct UInt64 : uint64, IInteger, IUnsigned, IHashable, IOpComparable, IIsNaN, IFormattable, IOpAddable, IOpSubtractable, IOpMultiplicable, IOpDividable
	{
		public const uint64 MaxValue = 0xFFFFFFFFFFFFFFFFUL;
		public const uint64 MinValue = 0;

		public enum ParseError
		{
			case Ok;
			case NoValue;
			case InvalidChar(uint64 partialResult);
		}

		public static int operator<=>(UInt64 a, UInt64 b)
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

		static String sHexUpperChars = "0123456789ABCDEF";
		static String sHexLowerChars = "0123456789abcdef";
		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if (format != null)
			{
				if (format == "P")
				{
					String hexChars = (format == "p") ? sHexLowerChars : sHexUpperChars;

					const int bufLen = 18;
					char8* strChars = scope:: char8[bufLen]* (?);
					int32 curLen = 0;
					uint64 valLeft = (uint64)this;
					while (valLeft > 0)
					{
						if (curLen == 8)
							strChars[bufLen - curLen++ - 1] = '\'';
					    strChars[bufLen - curLen++ - 1] = hexChars[(int)(valLeft & 0xF)];
					    valLeft >>= 4;
					}

					while (curLen < 10)
					{
						if (curLen == 8)
							strChars[bufLen - curLen++ - 1] = '\'';
						strChars[bufLen - curLen++ - 1] = '0';
					}

					char8* char8Ptr = &strChars[bufLen - curLen];
					outString.Append(char8Ptr, curLen);

					return;
				}

				if (format.StartsWith("X", StringComparison.OrdinalIgnoreCase))
				{
					String hexChars = (format == "x") ? sHexLowerChars : sHexUpperChars;

					const int bufLen = 18;
					char8* strChars = scope:: char8[bufLen]* (?);
					int32 curLen = 0;
					uint64 valLeft = (uint64)this;
					while (valLeft > 0)
					{
					    strChars[bufLen - curLen - 1] = hexChars[(int)(valLeft & 0xF)];
					    valLeft >>= 4;
					    curLen++;
					}

					int32 minChars = 1;
					if (format.Length > 0)
					{
						if (Int32.Parse(StringView(format, 1)) case .Ok(out minChars))
						   minChars = Math.Max(1, minChars);
					}

					while (curLen < minChars)
					{
						strChars[bufLen - curLen - 1] = '0';
						curLen++;
					}

					char8* char8Ptr = &strChars[bufLen - curLen];
					outString.Append(char8Ptr, curLen);

					return;
				}
			}

			ToString(outString);
		}

		public override void ToString(String strBuffer)
		{
		    // Dumb, make better.
		    char8[] strChars = scope:: char8[22];
		    int32 char8Idx = 20;
		    uint64 valLeft = (uint64)this;
		    while (valLeft > 0)
		    {
		        strChars[char8Idx] = (char8)('0' + (valLeft % 10));
		        valLeft /= 10;
		        char8Idx--;
			}
		    if (char8Idx == 20)
		        strChars[char8Idx--] = '0';
		    char8* char8Ptr = &strChars[char8Idx + 1];
		    strBuffer.Append(char8Ptr);
		}

		public static Result<uint64, ParseError> Parse(StringView val, NumberStyles numberStyles = .Number, CultureInfo cultureInfo = null)
		{
			if (val.Length == 0)
				return .Err(.NoValue);

			uint64 result = 0;
			if (numberStyles.HasFlag(.AllowHexSpecifier))
			{
				int numDigits = 0;

				for (int32 i = 0; i < val.Length; i++)
				{
					char8 c = val.Ptr[i];

					if (c == '\'')
						continue;

					if ((c == 'X') || (c == 'x'))
					{
						if ((numDigits == 1) && (result == 0))
							continue;
					}

					numDigits++;
					if ((c >= '0') && (c <= '9'))
						result = result*0x10 + (uint64)(c - '0');
					else if ((c >= 'A') && (c <= 'F'))
						result = result*0x10 + (uint64)(c - 'A') + 10;
					else if ((c >= 'a') && (c <= 'f'))
						result = result*0x10 + (uint64)(c - 'a') + 10;
					else
						return .Err(.InvalidChar(result));
				}

				return .Ok(result);
			}

			//TODO: Use Number.ParseNumber
			for (int32 i = 0; i < val.Length; i++)
			{
				char8 c = val.Ptr[i];

				if ((c >= '0') && (c <= '9'))
				{
					result *= 10;
					result += (uint64)(c - '0');
				}
				else
					return .Err(.InvalidChar(result));
			}
			return .Ok(result);
		}
	}
}
