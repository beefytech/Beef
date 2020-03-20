using System.Globalization;

namespace System
{
	struct Int32 : int32, IInteger, ISigned, IHashable, IFormattable, IOpComparable, IIsNaN, IOpNegatable, IOpAddable, IOpSubtractable
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
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
			int minNumerals = -1;

			//TOTAL HACK:
			if (format != null)
			{
				if (format.StartsWith("X"))
				{
					((UInt64)(uint32)this).ToString(outString, format, formatProvider);
					return;
				}

				if ((format.Length > 0) && (format[0] == '0'))
				{
					if (Int32.Parse(format) case .Ok(let wantLen))
					{								
						minNumerals = wantLen;
					}
				}
			}

			ToString(outString, minNumerals);
		}

	    public static Result<int32, ParseError> Parse(StringView val, NumberStyles style)
	    {
			if (val.IsEmpty)
				return .Err(.NoValue);

			bool isNeg = false;
			int32 result = 0;

			int32 radix = style.HasFlag(.AllowHexSpecifier) ? 0x10 : 10;
						
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
					result *= radix;
					result += (int32)(c - '0');
				}
				else if ((c >= 'a') && (c <= 'f'))
				{
					result *= radix;
					result += c - 'a' + 10;
				}
				else if ((c >= 'A') && (c <= 'F'))
				{
					result *= radix;
					result += c - 'A' + 10;
				}
				else if ((c == 'X') || (c == 'x'))
				{
					if (result != 0)
						return .Err(.InvalidChar(result));
					radix = 0x10;
				}
				else if (c == '\'')
				{
					// Ignore
				}
				else
					return .Err(.InvalidChar(result));
			}

			return isNeg ? -result : result;
	    }

		public static Result<int32, ParseError> Parse(StringView val)
		{
			return Parse(val, .Any);
		}
	}
}
