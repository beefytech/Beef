namespace System
{
#unwarn
	struct UInt : uint, IInteger, IUnsigned, IHashable, IFormattable, IIsNaN
	{
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case Overflow;
			case InvalidChar(uint partialResult);
		}

		public const uint MaxValue = (sizeof(uint) == 8) ? 0xFFFFFFFFFFFFFFFFUL : 0xFFFFFFFFL;
		public const uint MinValue = 0;

	    public bool IsNull()
	    {
	        return this == 0;
		}

		public static int operator<=>(UInt a, UInt b)
		{
			return (int)a <=> (int)b;
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
		    if (sizeof(Self) == sizeof(uint64))
				((uint64)this).ToString(strBuffer);
			else
				((uint32)this).ToString(strBuffer);
		}

		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if (sizeof(Self) == sizeof(uint64))
				((uint64)this).ToString(outString, format, formatProvider);
			else
				((uint32)this).ToString(outString, format, formatProvider);
		}

		public static Result<uint, ParseError> Parse(StringView val)
		{
			if (sizeof(Self) == sizeof(uint64))
			{
				var result = UInt64.Parse(val);
				return *(Result<uint, ParseError>*)&result;
			}
			else
			{
				var result = UInt32.Parse(val);
				return *(Result<uint, ParseError>*)&result;
			}
		}
	}
}
