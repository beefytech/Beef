// ==++==
// 
//   Copyright (c) Microsoft Corporation.  All rights reserved.
// 
// ==--==
/*============================================================
**
** Class:  Math
**
**
** Purpose: Some floating-point math operations
**
** 
===========================================================*/
namespace System
{
    //This class contains only static members and doesn't require serialization.
    using System;
    using System.Runtime;
    using System.Diagnostics.Contracts;
	using System.Diagnostics;
    
    public static class Math
    {
        private static double doubleRoundLimit = 1e16d;
        
        private const int32 maxRoundingDigits = 15;
		
        public const double PI = 3.14159265358979323846;
        public const double E = 2.7182818284590452354;

		private static double[16] sRoundPower10Double = .(
			1E0, 1E1, 1E2, 1E3, 1E4, 1E5, 1E6, 1E7, 1E8,
			1E9, 1E10, 1E11, 1E12, 1E13, 1E14, 1E15);

        public static extern double Acos(double d);
        public static extern double Asin(double d);
        public static extern double Atan(double d);
        public static extern double Atan2(double y, double x);
        public static extern double Ceiling(double a);
        public static extern double Cos(double d);
        public static extern double Cosh(double value);
        public static extern double Floor(double d);
        
        public static extern double Sin(double a);
        public static extern double Tan(double a);
        public static extern double Sinh(double value);
        public static extern double Tanh(double value);
        public static extern double Round(double a);
        
		[CLink]
		private static extern double modf(double x, out double intpart);
        
        public static double Truncate(double d)
        {
			double intPart;
			modf(d, out intPart);
			return intPart;
        }
            
        public static extern float Sqrt(float f);
        public static extern double Sqrt(double d);
        public static extern double Log(double d);
        public static extern double Log10(double d);
        public static extern double Exp(double d);
        public static extern double Pow(double x, double y);
            
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
                    return Double.NegativeZero;
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
		  
		[Inline]
		public static T Abs<T>(T value) where T : IOpComparable, IOpNegatable
        {
            if (value < default)
                return (T)-value;
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
		
		/*=====================================Log======================================
		**
		==============================================================================*/
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
