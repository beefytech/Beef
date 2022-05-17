// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System
{
    using System;
    using System.Globalization;
    using System.Diagnostics.Contracts;
	using System.Diagnostics;

#unwarn
    public struct Double : double, IFloating, ISigned, IFormattable, IHashable, ICanBeNaN
    {
        public const double MinValue = -1.7976931348623157E+308;
        public const double MaxValue = 1.7976931348623157E+308;

        // Note Epsilon should be a double whose hex representation is 0x1
        // on little endian machines.
        public const double Epsilon = 4.9406564584124654E-324;
        public const double NegativeInfinity = (double)(- 1.0 / (double)(0.0));
        public const double PositiveInfinity = (double)1.0 / (double)(0.0);
        public const double NaN = (double)0.0 / (double)0.0;
        public const double NegativeZero = -0.0;

		public static int operator<=>(Double a, Double b)
		{
			return (double)a <=> (double)b;
		}

        public static Double operator-(Double value)
        {
            return (double)value;
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
			double d = (double)this;
			if (d == 0)
			{
			    // Ensure that 0 and -0 have the same hash code
			    return 0;
			}
			return *((int*)&d) ^ ((int32*)&d)[1];
			
		}

		public bool IsNegative
		{
			get
			{
				double val = (double)this;
		        return (*(uint64*)(&val) & 0x8000000000000000UL) == 0x8000000000000000UL;
			}
		}

		public bool IsFinite
		{
			get
			{
				double val = (double)this;
		        return (*(int64*)(&val) & 0x7FFFFFFFFFFFFFFFL) < 0x7FF0000000000000L;
			}
		}

        public bool IsInfinity
        {
			get
			{
				double val = (double)this;
	            return (*(int64*)(&val) & 0x7FFFFFFFFFFFFFFFL) == 0x7FF0000000000000L;
			}
        }
                
        public bool IsPositiveInfinity
        {
			get
			{
	            return (double)this == double.PositiveInfinity;
			}
        }
                
        public bool IsNegativeInfinity
        {
			get
			{
				return (double)this == double.NegativeInfinity;
			}
        }
                
        
        public bool IsNaN
        {
			get
			{
				double val = (double)this;
	            return (*(uint64*)(&val) & 0x7FFFFFFFFFFFFFFFUL) > 0x7FF0000000000000UL;
			}
        }

		public bool IsSubnormal
		{
			get
			{
				double val = (double)this;
			    var bits = *(int64*)(&val);
			    bits &= 0x7FFFFFFFFFFFFFFFUL;
			    return (bits < 0x7F80000000000000UL) && (bits != 0) && ((bits & 0x7F80000000000000UL) == 0);
			}
		}

        // Compares this object to another object, returning an instance of System.Relation.
        // Null is considered less than any instance.
        //
        // If object is not of type Double, this method throws an ArgumentException.
        //
        // Returns a value less than zero if this  object
        //
        public int32 CompareTo(Object value)
        {
            if (value == null)
            {
                return 1;
            }
            if (value is double)
            {
                double d = (double)value;
                if ((double)this < d) return -1;
                if ((double)this > d) return 1;
                if ((double)this == d) return 0;

                // At least one of the values is NaN.
                if (IsNaN)
                    return (d.IsNaN ? 0 : -1);
                else
                    return 1;
            }
            Runtime.FatalError();
        }
        
        public int32 CompareTo(double value)
        {
            if ((double)this < value) return -1;
            if ((double)this > value) return 1;
            if ((double)this == value) return 0;

            // At least one of the values is NaN.
            if (IsNaN)
                return (value.IsNaN) ? 0 : -1;
            else
                return 1;
        }

        public bool Equals(double obj)
        {
            if (obj == (double)this)
            {
                return true;
            }
            return obj.IsNaN && IsNaN;
        }

		[CLink]
		static extern double strtod(char8* str, char8** endPtr);

		public static Result<double> Parse(StringView val)
		{
			var tempStr = scope String(val);
			int count = 0;
			L:for(char8 c in val)
			{
				if(c.IsDigit)
					continue L;
				else if(c == '.')
				{
					count++;
					if(count > 0)
						return .Err;
				}
				//This doesnt handle other writing styles yet (hex etc.)
				return .Err
			}
			return .Ok(strtod(tempStr, null));
		}

		[CallingConvention(.Stdcall), CLink]
		static extern int32 ftoa(float val, char8* str);

		static extern int32 ToString(double val, char8* str);

		public override void ToString(String strBuffer)
		{
			char8[128] outBuff = ?;
			int len = ToString((double)this, &outBuff);
			strBuffer.Append(&outBuff, len);
		}

		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if (format.IsEmpty)
			{
				ToString(outString);
				return;
			}
			NumberFormatter.NumberToString(format, (double)this, formatProvider, outString);
		}
    }
}
