namespace System
{
#unwarn
	struct UInt32 : uint32, IInteger, IUnsigned, IHashable, IFormattable, IIsNaN
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case InvalidChar(uint32 partialResult);
		}

		public const uint MaxValue = 0xFFFFFFFFL;
		public const uint MinValue = 0;

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
		    int32 valLeft = (int32)this;
			bool isNeg = false;
			if (valLeft < 0)
			{
				valLeft = -valLeft;
				isNeg = true;
			}
		    while (valLeft > 0)
		    {
		        strChars[char8Idx] = (char8)('0' + (valLeft % 10));
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
			bool isNeg = false;
			int minNumeralsLeft = minNumerals;
			if (valLeft < 0)
			{
				valLeft = -valLeft;
				isNeg = true;
			}
		    while ((valLeft > 0) || (minNumeralsLeft > 0))
		    {
		        strChars[char8Idx] = (char8)('0' + (valLeft % 10));
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
				NumberFormatter.NumberToString(format, (uint32)this, formatProvider, outString);
			}
		}

		public static Result<uint32, ParseError> Parse(StringView val)
		{
			if (val.Length == 0)
				return .Err(.NoValue);

		    uint32 result = 0;
			//TODO: Use Number.ParseNumber
			for (int32 i = 0; i < val.Length; i++)
			{
				char8 c = val.Ptr[i];

				if ((i == 0) && (c == '-'))
				{
					return .Err(.InvalidChar(0));
				}

				if ((c >= '0') && (c <= '9'))
				{
					result *= 10;
					result += (uint32)(c - '0');
				}
				else
					return .Err(.InvalidChar(result));
			}
			return .Ok(result);
		}
	}
}
