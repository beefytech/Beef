// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

namespace System
{
    using System;
    using System.Diagnostics.Contracts;
	using System.Diagnostics;

	/// This class provides general Math functionality.
    public static class Math
    {
		private const float cSingleRoundLimit = 1e8f;
		private const int32 cMaxSingleRoundingDigits = 6;

        private const double cDoubleRoundLimit = 1e16d;
        private const int32 cMaxDoubleRoundingDigits = 15;
		
		// This table is required for the Round function which can specify the number of digits to round to
        private static double[16] sRoundPower10Double = .(
			1E0, 1E1, 1E2, 1E3, 1E4, 1E5, 1E6, 1E7, 1E8,
        	1E9, 1E10, 1E11, 1E12, 1E13, 1E14, 1E15);

		private static float[7] sRoundPower10Single = .(
			1E0f, 1E1f, 1E2f, 1E3f, 1E4f, 1E5f, 1E6f);
        
        public const double PI_d = 3.14159265358979323846;
        public const double E_d = 2.7182818284590452354;
		public const float PI_f = 3.14159265358979323846f;
		public const float E_f = 2.7182818284590452354f;

		public static extern float Acos(float f);
        public static extern double Acos(double d);
        public static extern float Asin(float f);
		public static extern double Asin(double d);
        public static extern float Atan(float f);
		public static extern double Atan(double d);
        public static extern float Atan2(float y, float x);
		public static extern double Atan2(double y, double x);
        public static extern float Ceiling(float f);
		public static extern double Ceiling(double a);
		/// Returns cosine
        public static extern float Cos(float f);
		public static extern double Cos(double d);
        public static extern float Cosh(float f);
		public static extern double Cosh(double d);
        public static extern float Floor(float f);
		public static extern double Floor(double d);

		private static float InternalRound(float value, int32 digits, MidpointRounding mode)
		{
		    if (Abs(value) < cSingleRoundLimit)
		    {
		        float power10 = sRoundPower10Single[digits];
				float curValue = value;
		        curValue *= power10;
		        if (mode == MidpointRounding.AwayFromZero)
		        {
					double fraction = modff(curValue, out curValue);
		            if (Abs(fraction) >= 0.5d)
		            {
		                curValue += Sign(fraction);
		            }
		        }
		        else
		        {
		        	// On X86 this can be inlined to just a few instructions
		            curValue = Round(curValue);
		        }
		        curValue /= power10;
				return curValue;
		    }
		    return value;
		}

        private static double InternalRound(double value, int32 digits, MidpointRounding mode)
        {
            if (Abs(value) < cDoubleRoundLimit)
            {
                double power10 = sRoundPower10Double[digits];
				double curValue = value;
                curValue *= power10;
                if (mode == MidpointRounding.AwayFromZero)
                {
					double fraction = modf(curValue, out curValue);
                    if (Abs(fraction) >= 0.5d)
                    {
                        curValue += Sign(fraction);
                    }
                }
                else
                {
		        	// On X86 this can be inlined to just a few instructions
                    curValue = Round(curValue);
                }
                curValue /= power10;
				return curValue;
            }
            return value;
        }

		public static extern float Sin(float f);
        public static extern double Sin(double a);
		public static extern float Tan(float f);
        public static extern double Tan(double a);
		public static extern float Sinh(float f);
        public static extern double Sinh(double value);
		public static extern float Tanh(float f);
        public static extern double Tanh(double value);
		public static extern float Round(float f);
        public static extern double Round(double a);

		public static float Round(float value, int32 digits)
		{
		    if ((digits < 0) || (digits > cMaxDoubleRoundingDigits))
		        Runtime.FatalError();
		   //Contract.EndContractBlock();
		    return InternalRound(value, digits, MidpointRounding.ToEven);
		}

        public static double Round(double value, int32 digits)
        {
            if ((digits < 0) || (digits > cMaxDoubleRoundingDigits))
                Runtime.FatalError();
		   //Contract.EndContractBlock();
            return InternalRound(value, digits, MidpointRounding.ToEven);
        }

		public static float Round(float value, MidpointRounding mode)
		{
		    return Round(value, 0, mode);
		}

        public static double Round(double value, MidpointRounding mode)
        {
            return Round(value, 0, mode);
        }

		public static float Round(float value, int32 digits, MidpointRounding mode)
		{
		    if ((digits < 0) || (digits > cMaxDoubleRoundingDigits))
		        Runtime.FatalError();
		    if (mode < MidpointRounding.ToEven || mode > MidpointRounding.AwayFromZero)
		    {
		        Runtime.FatalError();
		    }
		  //Contract.EndContractBlock();
		    return InternalRound(value, digits, mode);
		}

        public static double Round(double value, int32 digits, MidpointRounding mode)
        {
            if ((digits < 0) || (digits > cMaxDoubleRoundingDigits))
                Runtime.FatalError();
            if (mode < MidpointRounding.ToEven || mode > MidpointRounding.AwayFromZero)
            {
                Runtime.FatalError();
            }
		  //Contract.EndContractBlock();
            return InternalRound(value, digits, mode);
        }

		[CLink]
		private static extern double modf(double x, out double intpart);
		[CLink]
		private static extern float modff(float x, out float intpart);

		public static float Truncate(float f)
		{
			float intPart;
			modff(f, out intPart);
			return intPart;
		}

        public static double Truncate(double d)
        {
			double intPart;
			modf(d, out intPart);
			return intPart;
        }
            
        public static extern float Sqrt(float f);
        public static extern double Sqrt(double d);
		public static extern float Log(float f);
        public static extern double Log(double d);
		public static extern float Log10(float f);
        public static extern double Log10(double d);
		public static extern float Exp(float f);
        public static extern double Exp(double d);
		public static extern float Pow(float x, float y);
        public static extern double Pow(double x, double y);

		public static float IEEERemainder(float x, float y)
		{
		    if (x.IsNaN)
			{
			    return x; // IEEE 754-2008: NaN payload must be preserved
			}

			if (y.IsNaN)
			{
			    return y; // IEEE 754-2008: NaN payload must be preserved
			}

			var regularMod = x % y;

			if (regularMod.IsNaN)
			{
				return float.NaN;
			}

			if ((regularMod == 0) && x.IsNegative)
			{
			    return Float.NegativeZero;
			}

			var alternativeResult = (regularMod - (Abs(y) * Sign(x)));

			if (Abs(alternativeResult) == Abs(regularMod))
			{
			    var divisionResult = x / y;
			    var roundedResult = Round(divisionResult);

			    if (Abs(roundedResult) > Abs(divisionResult))
			    {
			        return alternativeResult;
			    }
			    else
			    {
			        return regularMod;
			    }
			}

			if (Math.Abs(alternativeResult) < Math.Abs(regularMod))
			{
			    return alternativeResult;
			}
			else
			{
			    return regularMod;
			}
		}

        public static double IEEERemainder(double x, double y)
        {
            if (x.IsNaN)
            {
                return x; // IEEE 754-2008: NaN payload must be preserved
            }
            if (y.IsNaN)
            {
                return y; // IEEE 754-2008: NaN payload must be preserved
            }
            
            double regularMod = x % y;
            if (regularMod.IsNaN)
            {
                return Double.NaN;
            }
            if (regularMod == 0)
            {
                if (x.IsNegative)
                {
                    return Double.[Friend]NegativeZero;
                }
            }
            double alternativeResult;
            alternativeResult = regularMod - (Math.Abs(y) * Math.Sign(x));
            if (Math.Abs(alternativeResult) == Math.Abs(regularMod))
            {
                double divisionResult = x / y;
                double roundedResult = Math.Round(divisionResult);
                if (Math.Abs(roundedResult) > Math.Abs(divisionResult))
                {
                    return alternativeResult;
                }
                else
                {
                    return regularMod;
                }
            }
            if (Math.Abs(alternativeResult) < Math.Abs(regularMod))
            {
                return alternativeResult;
            }
            else
            {
                return regularMod;
            }
        }

  		[Intrinsic("abs")]
		public static extern float Abs(float value);
		[Intrinsic("abs")]
		public static extern double Abs(double value);
		[Inline]
		public static T Abs<T>(T value) where T : IOpComparable, IOpNegatable
        {
            if (value < default)
                return -value;
            else
				return value;
        }

		
        //extern public static float Abs(float value);
		// This is special code to handle NaN (We need to make sure NaN's aren't 
		// negated).  In CSharp, the else clause here should always be taken if 
		// value is NaN, since the normal case is taken if and only if value < 0.
		// To illustrate this completely, a compiler has translated this into:
		// "load value; load 0; bge; ret -value ; ret value".  
		// The bge command branches for comparisons with the unordered NaN.  So 
		// it runs the else case, which returns +value instead of negating it. 
		//  return (value < 0) ? -value : value;
        
        //extern public static double Abs(double value);
		// This is special code to handle NaN (We need to make sure NaN's aren't 
		// negated).  In CSharp, the else clause here should always be taken if 
		// value is NaN, since the normal case is taken if and only if value < 0.
		// To illustrate this completely, a compiler has translated this into:
		// "load value; load 0; bge; ret -value ; ret value".  
		// The bge command branches for comparisons with the unordered NaN.  So 
		// it runs the else case, which returns +value instead of negating it. 
		// return (value < 0) ? -value : value;
		
		public static T Clamp<T>(T val, T min, T max) where T : IOpComparable
		{
		    if (val < min)
		        return min;
		    else if (val > max)
				return max;
		    return val;
		}

		public static float Distance(float dX, float dY)
		{
		    return (float)Math.Sqrt(dX * dX + dY * dY);
		}

		public static float Lerp(float val1, float val2, float pct)
		{
		    return val1 + (val2 - val1) * pct;
		}

		public static double Lerp(double val1, double val2, double pct)
		{
		    return val1 + (val2 - val1) * pct;
		}

		public static T Lerp<T>(T val1, T val2, float pct) where T : operator T + T, operator T - T, operator T * float
		{
		    return val1 + (val2 - val1) * pct;
		}

		public static T Lerp<T>(T val1, T val2, double pct) where T : operator T + T, operator T - T, operator T * double
		{
		    return val1 + (val2 - val1) * pct;
		}

        public static T Min<T>(T val1, T val2) where T : IOpComparable, IIsNaN
        {
            if (val1 < val2)
                return val1;
            
            if (val1.IsNaN)
                return val1;
            
            return val2;
        }

		public static T Max<T>(T val1, T val2) where T : IOpComparable, IIsNaN
		{
		    if (val1 > val2)
			    return val1;

			if (val1.IsNaN)
			    return val1;

			return val2;
		}

		public static float Log(float a, float newBase)
		{
		    if (a.IsNaN)
		    {
		        return a; // IEEE 754-2008: NaN payload must be preserved
		    }
		    if (newBase.IsNaN)
		    {
		        return newBase; // IEEE 754-2008: NaN payload must be preserved
		    }
		    
		    if (newBase == 1)
		        return Float.NaN;
		    if (a != 1 && (newBase == 0 || newBase.IsPositiveInfinity))
		        return Float.NaN;
		    
		    return (Log(a) / Log(newBase));
		}

        public static double Log(double a, double newBase)
        {
            if (a.IsNaN)
            {
                return a; // IEEE 754-2008: NaN payload must be preserved
            }
            if (newBase.IsNaN)
            {
                return newBase; // IEEE 754-2008: NaN payload must be preserved
            }
            
            if (newBase == 1)
                return Double.NaN;
            if (a != 1 && (newBase == 0 || newBase.IsPositiveInfinity))
                return Double.NaN;
            
            return (Log(a) / Log(newBase));
        }
		  
		public static int Sign<T>(T value) where T : IOpComparable
		{
		    if (value < default)
		        return -1;
		    else if (value > default)
		        return 1;
		    else 
		        return 0;
		}

		public static int Sign<T>(T value) where T : IOpComparable, ICanBeNaN
		{
		    if (value < default)
		        return -1;
		    else if (value > default)
		        return 1;
		    else if (value == default)
		        return 0;

			Runtime.FatalError("Cannot be used on NaN");
		}
		
		public static int32 DivRem(int32 a, int32 b, out int32 result)
        {
            result = a % b;
            return a / b;
        }
		
		public static int64 DivRem(int64 a, int64 b, out int64 result)
        {
            result = a % b;
            return a / b;
        }

		public static int32 Align(int32 val, int32 align)
		{
			return ((val) + (align - 1)) & ~(align - 1);
		}

		public static int64 Align(int64 val, int64 align)
		{
			return ((val) + (align - 1)) & ~(align - 1);
		}
    }
}
