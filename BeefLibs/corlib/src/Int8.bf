using System.Globalization;

namespace System
{
#unwarn
	struct Int8 : int8, IInteger, ISigned, IHashable, IFormattable, IIsNaN
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case Overflow;
			case InvalidChar(int8 partialResult);
		}

		public const int8 MaxValue = 0x7F;
		public const int8 MinValue = -0x80;

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

		public override void ToString(String outString)
		{
			((int32)this).ToString(outString);
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

		public static Result<int8, ParseError> Parse(StringView val, NumberStyles style = .Number, CultureInfo cultureInfo = null)
		{
			if (val.IsEmpty)
				return .Err(.NoValue);

			bool isNeg = false;
			int8 result = 0;

			int8 radix = style.HasFlag(.Hex) ? 0x10 : 10;

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
					result &+= (int8)(c - '0');
				}
				else if ((c >= 'a') && (c <= 'f'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar(result));
					result &*= radix;
					result &+= (int8)(c - 'a' + 10);
				}
				else if ((c >= 'A') && (c <= 'F'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar(result));
					result &*= radix;
					result &+= (int8)(c - 'A' + 10);
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

				if (isNeg ? (uint8)result > (uint8)MaxValue + 1 : (uint8)result > (uint8)MaxValue)
					return .Err(.Overflow);
			}

			return isNeg ? -result : result;
		}
	}
}
