using System.Globalization;

namespace System
{
	struct Int64 : int64, IInteger, ISigned, IFormattable, IHashable, IOpComparable, IIsNaN, IOpNegatable
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case InvalidChar(int64 partialResult);
		}

	    public const int64 MaxValue = 0x7FFFFFFFFFFFFFFFL;
	    //public const long MinValue = -0x8000000000000000L;
	    public const int64 MinValue = -0x7FFFFFFFFFFFFFFFL; //TODO: Should be one lower!

		public static int operator<=>(Int64 a, Int64 b)
		{
			return (int64)a <=> (int64)b;
		}

		public static Int64 operator-(Int64 value)
		{
			return (int64)value;
		}

		int IHashable.GetHashCode()
		{
			return (int)(int64)this;
		}

		bool IIsNaN.IsNaN
		{
			[SkipCall]
			get
			{
				return false;
			}
		}

		//static char8[] sHexUpperChars = new char8[]{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'} ~ delete _;
		//static char8[] sHexLowerChars = new char8[]{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'} ~ delete _;

		static String sHexUpperChars = "0123456789ABCDEF";
		static String sHexLowerChars = "0123456789abcdef";
		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if ((format != null) && (!format.IsEmpty))
			{
				((UInt64)this).ToString(outString, format, formatProvider);
				return;
			}

			ToString(outString);
		}

		public override void ToString(String strBuffer)
		{
		    // Dumb, make better.
		    char8[] strChars = scope:: char8[22];
			int32 char8Idx = 20;
			int64 valLeft = (int64)this;
			bool isNeg = false;
			int minNumeralsLeft = 0;
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
			if (char8Idx == 20)
			    strChars[char8Idx--] = '0';
			if (isNeg)
				strChars[char8Idx--] = '-';
			char8* char8Ptr = &strChars[char8Idx + 1];
			strBuffer.Append(char8Ptr);
		}

		public static Result<int64, ParseError> Parse(StringView val, NumberStyles style)
		{
			//TODO: Use Number.ParseNumber

			if (val.IsEmpty)
				return .Err(.NoValue);

			bool isNeg = false;
			int64 result = 0;

			int64 radix = style.HasFlag(.AllowHexSpecifier) ? 0x10 : 10;
						
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

		public static Result<int64, ParseError> Parse(StringView val)
		{
			return Parse(val, .Any);
		}
	}
}
