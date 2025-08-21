using System.Globalization;

namespace System
{
#unwarn
	struct UInt8 : uint8, IInteger, IUnsigned, IHashable, IFormattable, IIsNaN, IParseable<uint8, ParseError>, IParseable<uint8>, IMinMaxValue<uint8>
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case Overflow;
			case InvalidChar(uint8 partialResult);
		}

	    public const uint8 MaxValue = 0xFF;
	    public const uint8 MinValue = 0;

		public static uint8 IMinMaxValue<uint8>.MinValue => MinValue;
		public static uint8 IMinMaxValue<uint8>.MaxValue => MaxValue;

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

		public static Result<uint8, ParseError> Parse(StringView val, NumberStyles style = .Number, CultureInfo cultureInfo = null)
		{
			if (val.IsEmpty)
				return .Err(.NoValue);

			bool digitsFound = false;
			uint16 result = 0;

			uint8 radix = style.HasFlag(.Hex) ? 0x10 : 10;

			for (int32 i = 0; i < val.Length; i++)
			{
				char8 c = val[i];
				if ((c >= '0') && (c <= '9'))
				{
					result &*= radix;
					result &+= (uint8)(c - '0');
					digitsFound = true;
				}
				else if ((c >= 'a') && (c <= 'f'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar((.)result));
					result &*= radix;
					result &+= (uint8)(c - 'a' + 10);
					digitsFound = true;
				}
				else if ((c >= 'A') && (c <= 'F'))
				{
					if (radix != 0x10)
						return .Err(.InvalidChar((.)result));
					result &*= radix;
					result &+= (uint8)(c - 'A' + 10);
					digitsFound = true;
				}
				else if ((c == 'X') || (c == 'x'))
				{
					if ((!style.HasFlag(.AllowHexSpecifier)) || (i == 0) || (result != 0))
						return .Err(.InvalidChar((.)result));
					radix = 0x10;
					digitsFound = false;
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
					return .Err(.InvalidChar((.)result));

				if (result > MaxValue)
					return .Err(.Overflow);
			}

			if (!digitsFound)
				return .Err(.NoValue);

			return (.)result;
		}

		public static Result<uint8, ParseError> IParseable<uint8, ParseError>.Parse(StringView val)
		{
			return Parse(val);
		}

		public static Result<uint8> IParseable<uint8>.Parse(StringView val)
		{
			var res = Parse(val);
			if(res case .Err)
				return .Err;
			else
				return .Ok(res.Value);
		}
	}
}
