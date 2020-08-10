using System.Globalization;

namespace System
{
#unwarn
	struct Float : float, IFloating, ISigned, IFormattable, IHashable, IEquatable<float>, IOpComparable, IOpNegatable, IOpAddable, IOpSubtractable, IOpMultiplicable, IOpDividable, ICanBeNaN
    {
		public const float MinValue = (float)-3.40282346638528859e+38;
		public const float Epsilon = (float)1.4e-45;
		public const float MaxValue = (float)3.40282346638528859e+38;
		public const float PositiveInfinity = 1.0f / 0.0f;
		public const float NegativeInfinity = -1.0f / 0.0f;
		public const float NaN = 0.0f / 0.0f;

		// We use this explicit definition to avoid the confusion between 0.0 and -0.0.
		public const float NegativeZero = (float)-0.0;

		public static int operator<=>(Float a, Float b)
		{
			return (float)a <=> (float)b;
		}

		public static Float operator-(Float value)
		{
			return (float)value;
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

		/*public bool IsNegative
		{
			get
			{
				return this < 0;
			}
		}*/

		public int GetHashCode()
		{
			var val = (float)this;
			return *(int32*)(&val);
		}

		public bool IsNegative
		{
			get
			{
				return this < 0;
			}
		}

		public bool IsFinite
		{
			get
			{
				float val = (float)this;
			    return (*(int32*)(&val) & 0x7FFFFFFF) < 0x7F800000;
			}
		}

		public bool IsInfinity
        {
			get
			{
				float val = (float)this;
	            return (*(int32*)(&val) & 0x7FFFFFFF) == 0x7F800000;
			}
        }

		public bool IsPositiveInfinity
        {
			get
			{
				float val = (float)this;
	            return *(int32*)(&val) == 0x7F800000;
			}
        }

		public bool IsNegativeInfinity
        {
			get
			{
				float val = (float)this;
	            return *(int32*)(&val) == (int32)0xFF800000;
			}
        }

        public bool IsNaN
        {
			get
			{
				float val = (float)this;
	            return (*(int32*)(&val) & 0x7FFFFFFF) > 0x7F800000;
			}
        }

		public bool IsSubnormal
		{
			get
			{
				float val = (float)this;
			    var bits = *(int32*)(&val);
			    bits &= 0x7FFFFFFF;
			    return (bits < 0x7F800000) && (bits != 0) && ((bits & 0x7F800000) == 0);
			}
		}

		public bool Equals(float other)
		{
			return (float)this == other;
		}

		public int CompareTo(float value) 
        {
            if ((float)this < value) return -1;
            if ((float)this > value) return 1;
            if ((float)this == value) return 0;

            // At least one of the values is NaN.
            if (IsNaN)
                return (value.IsNaN ? 0 : -1);
            else // f is NaN.
                return 1;
        }

		[CallingConvention(.Stdcall), CLink]
		static extern int32 ftoa(float val, char8* str);

		static extern int32 ToString(float val, char8* str);

		public override void ToString(String strBuffer)
		{
			char8[128] outBuff = ?;
			//ftoa((float)this, &outBuff);
			int len = ToString((float)this, &outBuff);
			strBuffer.Append(&outBuff, len);
		}

		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if (format.IsEmpty)
			{
				ToString(outString);
				return;
			}
			NumberFormatter.NumberToString(format, (float)this, formatProvider, outString);
		}

		public static Result<float> Parse(StringView val)
		{
			bool isNeg = false;
		    double result = 0;
			double decimalMultiplier = 0;
			
			//TODO: Use Number.ParseNumber
			for (int32 i = 0; i < val.Length; i++)
			{
				char8 c = val.Ptr[i];

				if (c == '.')
				{
					if (decimalMultiplier != 0)
						return .Err;
					decimalMultiplier = 0.1;
					continue;
				}

				if (decimalMultiplier != 0)
				{
					if ((c >= '0') && (c <= '9'))
					{
						result += (int32)(c - '0') * decimalMultiplier;
						decimalMultiplier *= 0.1;
					}
					else
						return .Err;

					continue;
				}

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
					return .Err;
			}
			return isNeg ? (float)(-result) : (float)result;
		}
    }
}
