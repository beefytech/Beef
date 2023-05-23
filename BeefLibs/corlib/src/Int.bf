using System;

namespace System
{
#unwarn
	struct Int : int, IInteger, IHashable, IFormattable, IIsNaN
    {
		public enum ParseError
		{
			case Ok;
			case NoValue;
			case Overflow;
			case InvalidChar(int partialResult);
		}

		public const int MaxValue = (sizeof(int) == 8) ? 0x7FFFFFFFFFFFFFFFL : 0x7FFFFFFF;
		public const int MinValue = (sizeof(int) == 8) ? -0x8000000000000000L : -0x80000000;

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

		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{								   
			if (sizeof(int) == sizeof(int64))
			{
				((int64)this).ToString(outString, format, formatProvider);
			}
			else
			{
				((int32)this).ToString(outString, format, formatProvider);
			}
		}

		public override void ToString(String outString)
		{
			if (sizeof(int) == sizeof(int64))
			{
				((int64)this).ToString(outString);
			}
			else
			{
				((int32)this).ToString(outString);
			}
		}

		public static Result<int, ParseError> Parse(StringView val)
		{
			if (sizeof(Self) == sizeof(int64))
			{
				var result = Int64.Parse(val);
				return *(Result<int, ParseError>*)&result;
			}
			else
			{
				var result = Int32.Parse(val);
				return *(Result<int, ParseError>*)&result;
			}
		}
	}
}
