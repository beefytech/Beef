using System.Globalization;

namespace System
{
#unwarn
	struct UInt16 : uint16, IInteger, IUnsigned, IHashable, IFormattable, IIsNaN
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case Overflow;
			case InvalidChar(uint16 partialResult);
		}

		public const uint16 MaxValue = 0xFFFF;
		public const uint16 MinValue = 0;

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
			((uint32)this).ToString(outString);
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

		public static Result<uint16, ParseError> Parse(StringView val, NumberStyles style = .Number, CultureInfo cultureInfo = null)
		{
			if (val.IsEmpty)
				return .Err(.NoValue);

			uint16 result = 0;
			uint16 prevResult = 0;

			uint16 radix = style.HasFlag(.Hex) ? 0x10 : 10;

			for (int32 i = 0; i < val.Length; i++)
			{
				char8 c = val[i];

				if ((c >= '0') && (c <= '9'))
				{
					result &*= radix;
					result &+= (uint16)(c - '0');
				}
				else if ((c >= 'a') && (c <= 'f'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar(result));
					result &*= radix;
					result &+= (uint16)(c - 'a' + 10);
				}
				else if ((c >= 'A') && (c <= 'F'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar(result));
					result &*= radix;
					result &+= (uint16)(c - 'A' + 10);
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
