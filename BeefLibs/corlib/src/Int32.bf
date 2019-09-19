namespace System
{
	struct Int32 : int32, IInteger, ISigned, IHashable, IFormattable, IOpComparable, IIsNaN, IOpNegatable, IOpAddable
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

		public static Self operator-(Self value)
		{
			return (SelfBase)value;
		}

		int IHashable.GetHashCode()
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

	    public static Result<int32, ParseError> Parse(StringView val)
	    {
			if (val.Length == 0)
				return .Err(.NoValue);

			bool isNeg = false;
	        int32 result = 0;
			//TODO: Use Number.ParseNumber
			for (int32 i = 0; i < val.Length; i++)
			{
				char8 c = val.Ptr[i];

				if ((i == 0) && (c == '-'))
				{
					isNeg = true;
					continue;
				}

				if ((c >= '0') && (c <= '9'))
				{
					result *= 10;
					result += (int32)(c - '0');
				}
				else
					return .Err(.InvalidChar(isNeg ? -result : result));
			}
			return .Ok(isNeg ? -result : result);
	    }
	}
}
