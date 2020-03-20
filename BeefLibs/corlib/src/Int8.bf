namespace System
{
	struct Int8 : int8, IInteger, ISigned, IHashable, IFormattable, IOpComparable, IIsNaN, IOpNegatable, IOpAddable, IOpSubtractable
	{
		public const int32 MaxValue = 0x7F;
		public const int32 MinValue = -0x80;

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

		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if ((format != null) && (format.StartsWith("X")))
			{
				((uint64)this).ToString(outString, format, formatProvider);
				return;
			}
			((int64)this).ToString(outString, format, formatProvider);
		}
	}
}
