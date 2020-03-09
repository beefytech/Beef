// ==++==
//
//   Copyright (c) Microsoft Corporation.  All rights reserved.
//
// ==--==
/*============================================================
**
** Class:  Double
**
**
** Purpose: A representation of an IEEE double precision
**          floating point number.
**
**
===========================================================*/
namespace System
{
    using System;
///#if GENERICS_WORK
///    using System.Numerics;
///#endif
    using System.Diagnostics.Contracts;
    
    public struct Double : double, IHashable, IOpComparable, IOpNegatable, ICanBeNaN /*: IComparable, IFormattable, IConvertible
        , IComparable<Double>, IEquatable<Double>*/
    {
        //
        // Public Constants
        //
        public const double MinValue = -1.7976931348623157E+308;
        public const double MaxValue = 1.7976931348623157E+308;

        // Note Epsilon should be a double whose hex representation is 0x1
        // on little endian machines.
        public const double Epsilon = 4.9406564584124654E-324;
        public const double NegativeInfinity = (double)(- 1.0 / (double)(0.0));
        public const double PositiveInfinity = (double)1.0 / (double)(0.0);
        public const double NaN = (double)0.0 / (double)0.0;
        
        public static double NegativeZero = BitConverter.Int64BitsToDouble((int64)(0x8000000000000000UL));

		public static int operator<=>(Double lhs, Double rhs)
		{
			return (double)lhs <=> (double)rhs;
		}

		public static Double operator-(Double value)
		{
			return -(double)value;
		}

		int IHashable.GetHashCode()
		{
			double d = (double)this;
			if (d == 0)
			{
			    // Ensure that 0 and -0 have the same hash code
			    return 0;
			}

			if (sizeof(int) == sizeof(double))
			{
				var val = (double)this;
				return *(int*)(&val);
			}
			else
			{
				int64 value = *(int64*)(&d);
				return ((int32)value) ^ ((int32)(value >> 32));
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
		        
		public bool IsNegative
		{
			get
			{
				double val = (double)this;
		        return (*(uint64*)(&val) & 0x8000000000000000UL) == 0x8000000000000000UL;
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

		public static Result<double> Parse(String val)
		{
			return .Ok(strtod(val, null));
		}

        //TODO:
        /*[System.Security.SecuritySafeCritical]  // auto-generated
        public override String ToString() {
            Contract.Ensures(Contract.Result<String>() != null);
            return Number.FormatDouble((double)this, null, NumberFormatInfo.CurrentInfo);
        }

        [System.Security.SecuritySafeCritical]  // auto-generated
        public String ToString(String format) {
            Contract.Ensures(Contract.Result<String>() != null);
            return Number.FormatDouble((double)this, format, NumberFormatInfo.CurrentInfo);
        }
        
        [System.Security.SecuritySafeCritical]  // auto-generated
        public String ToString(IFormatProvider provider) {
            Contract.Ensures(Contract.Result<String>() != null);
            return Number.FormatDouble((double)this, null, NumberFormatInfo.GetInstance(provider));
        }
        
        [System.Security.SecuritySafeCritical]  // auto-generated
        public String ToString(String format, IFormatProvider provider) {
            Contract.Ensures(Contract.Result<String>() != null);
            return Number.FormatDouble((double)this, format, NumberFormatInfo.GetInstance(provider));
        }

        public static double Parse(String s) {
            return Parse(s, NumberStyles.Float| NumberStyles.AllowThousands, NumberFormatInfo.CurrentInfo);
        }

        public static double Parse(String s, NumberStyles style) {
            NumberFormatInfo.ValidateParseStyleFloatingPoint(style);
            return Parse(s, style, NumberFormatInfo.CurrentInfo);
        }

        public static double Parse(String s, IFormatProvider provider) {
            return Parse(s, NumberStyles.Float| NumberStyles.AllowThousands, NumberFormatInfo.GetInstance(provider));
        }

        public static double Parse(String s, NumberStyles style, IFormatProvider provider) {
            NumberFormatInfo.ValidateParseStyleFloatingPoint(style);
            return Parse(s, style, NumberFormatInfo.GetInstance(provider));
        }
        
        // Parses a double from a String in the given style.  If
        // a NumberFormatInfo isn't specified, the current culture's
        // NumberFormatInfo is assumed.
        //
        // This method will not throw an OverflowException, but will return
        // PositiveInfinity or NegativeInfinity for a number that is too
        // large or too small.
        //
        private static double Parse(String s, NumberStyles style, NumberFormatInfo info) {
            return Number.ParseDouble(s, style, info);
        }

        public static bool TryParse(String s, out double result) {
            return TryParse(s, NumberStyles.Float| NumberStyles.AllowThousands, NumberFormatInfo.CurrentInfo, out result);
        }

        public static bool TryParse(String s, NumberStyles style, IFormatProvider provider, out double result) {
            NumberFormatInfo.ValidateParseStyleFloatingPoint(style);
            return TryParse(s, style, NumberFormatInfo.GetInstance(provider), out result);
        }
        
        private static bool TryParse(String s, NumberStyles style, NumberFormatInfo info, out double result) {
            if (s == null) {
                result = 0;
                return false;
            }
            bool success = Number.TryParseDouble(s, style, info, out result);
            if (!success) {
                String sTrim = s.Trim();
                if (sTrim.Equals(info.PositiveInfinitySymbol)) {
                    result = PositiveInfinity;
                } else if (sTrim.Equals(info.NegativeInfinitySymbol)) {
                    result = NegativeInfinity;
                } else if (sTrim.Equals(info.NaNSymbol)) {
                    result = NaN;
                } else
                    return false; // We really failed
            }
            return true;
        }

        //
        // IConvertible implementation
        //

        public TypeCode GetTypeCode() {
            return TypeCode.Double;
        }

        /// <internalonly/>
        bool IConvertible.ToBoolean(IFormatProvider provider) {
            return Convert.ToBoolean((double)this);
        }

        /// <internalonly/>
        char8 IConvertible.ToChar(IFormatProvider provider) {
            Runtime.FatalError();
        }

        /// <internalonly/>
        sbyte IConvertible.ToSByte(IFormatProvider provider) {
            return Convert.ToSByte((double)this);
        }

        /// <internalonly/>
        byte IConvertible.ToByte(IFormatProvider provider) {
            return Convert.ToByte((double)this);
        }

        /// <internalonly/>
        short IConvertible.ToInt16(IFormatProvider provider) {
            return Convert.ToInt16((double)this);
        }

        /// <internalonly/>
        ushort IConvertible.ToUInt16(IFormatProvider provider) {
            return Convert.ToUInt16((double)this);
        }

        /// <internalonly/>
        int IConvertible.ToInt32(IFormatProvider provider) {
            return Convert.ToInt32((double)this);
        }

        /// <internalonly/>
        uint IConvertible.ToUInt32(IFormatProvider provider) {
            return Convert.ToUInt32((double)this);
        }

        /// <internalonly/>
        long IConvertible.ToInt64(IFormatProvider provider) {
            return Convert.ToInt64((double)this);
        }

        /// <internalonly/>
        ulong IConvertible.ToUInt64(IFormatProvider provider) {
            return Convert.ToUInt64((double)this);
        }

        /// <internalonly/>
        float IConvertible.ToSingle(IFormatProvider provider) {
            return Convert.ToSingle((double)this);
        }

        /// <internalonly/>
        double IConvertible.ToDouble(IFormatProvider provider) {
            return (double)this;
        }

        /// <internalonly/>
        Decimal IConvertible.ToDecimal(IFormatProvider provider) {
            return Convert.ToDecimal((double)this);
        }

        /// <internalonly/>
        DateTime IConvertible.ToDateTime(IFormatProvider provider) {
            Runtime.FatalError();
        }

        /// <internalonly/>
        Object IConvertible.ToType(Type type, IFormatProvider provider) {
            return Convert.DefaultToType((IConvertible)this, type, provider);
        }*/
    }
}
