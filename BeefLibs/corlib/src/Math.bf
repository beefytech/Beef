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

		private static float sMachineEpsilonFloat = GetMachineEpsilonFloat();

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

		public static bool WithinEpsilon(float a, float b)
		{
			return Math.Abs(a - b) < sMachineEpsilonFloat;
		}

		/// <summary>
		/// Find the current machine's Epsilon for the float data type.
		/// (That is, the largest float, e,  where e == 0.0f is true.)
		/// </summary>
		private static float GetMachineEpsilonFloat()
		{
			float machineEpsilon = 1.0f;
			float comparison;

			/* Keep halving the working value of machineEpsilon until we get a number that
			 * when added to 1.0f will still evaluate as equal to 1.0f.
			 */
			repeat
			{
				machineEpsilon *= 0.5f;
				comparison = 1.0f + machineEpsilon;
			}
			while (comparison > 1.0f);

			return machineEpsilon;
		}

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

		public static float RadiansToDegrees(float rad) => rad * (180.f / PI_f);
		public static double RadiansToDegrees(double rad) => rad * (180.0 / PI_d);
		public static float DegreesToRadians(float deg) => deg * (PI_f / 180.f);
		public static double DegreesToRadians(double deg) => deg * (PI_d / 180.0);

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

#if BF_PLATFORM_WINDOWS && BF_64_BIT
		[CLink]
		private static extern float modff(float x, out float intpart);
#else
		private static float modff(float x, out float intpart)
		{
			var f = modf(x, var i);
			intpart = (.)i;
			return (.)f;
		}
#endif

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
		public static extern float Cbrt(float f);
        public static extern double Cbrt(double d);
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
		public static T Abs<T>(T value) where bool : operator T < T where T : operator -T
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
		
		public static T Clamp<T>(T val, T min, T max) where int : operator T<=>T
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

        public static T Min<T>(T val1, T val2) where bool : operator T < T where T : IIsNaN
        {
            if (val1 < val2)
                return val1;
            
            if (val1.IsNaN)
                return val1;
            
            return val2;
        }

		public static T Min<T>(T val1, T val2) where bool : operator T < T
		{
		    if (val1 < val2)
		        return val1;
		    return val2;
		}

		public static T Max<T>(T val1, T val2) where bool : operator T > T where T : IIsNaN
		{
		    if (val1 > val2)
			    return val1;

			if (val1.IsNaN)
			    return val1;

			return val2;
		}

		public static T Max<T>(T val1, T val2) where bool : operator T > T
		{
		    if (val1 > val2)
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
		  
		public static int Sign<T>(T value) where int : operator T <=> T
		{
		    if (value < default)
		        return -1;
		    else if (value > default)
		        return 1;
		    else 
		        return 0;
		}

		public static int Sign<T>(T value) where int : operator T <=> T where T : ICanBeNaN
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

		/// Interpolates between two values using a cubic equation.
		/// @param name Source value.
		/// @param name Source value.
		/// @param name Weighting value.
		/// @returns Interpolated value.
		public static float SmoothStep(float value1, float value2, float amount)
		{
			/* It is expected that 0 < amount < 1.
			 * If amount < 0, return value1.
			 * If amount > 1, return value2.
			 */
			float result = Clamp(amount, 0f, 1f);
			result = Hermite(value1, 0f, value2, 0f, result);

			return result;
		}

		/// Performs a Hermite spline interpolation.
		/// @param value1 Source position.
		/// @param tangent1 Source tangent.
		/// @param value2 Source position.
		/// @param tangent2 Source tangent.
		/// @param amount Weighting factor.
		/// @returns The result of the Hermite spline interpolation.
		public static float Hermite(
			float value1,
			float tangent1,
			float value2,
			float tangent2,
			float amount
		) {
			/* All transformed to double not to lose precision
			 * Otherwise, for high numbers of param:amount the result is NaN instead
			 * of Infinity.
			 */
			double v1 = value1, v2 = value2, t1 = tangent1, t2 = tangent2, s = amount;
			double result;
			double sCubed = s * s * s;
			double sSquared = s * s;

			if (WithinEpsilon(amount, 0f))
			{
				result = value1;
			}
			else if (WithinEpsilon(amount, 1f))
			{
				result = value2;
			}
			else
			{
				result = (
					((2 * v1 - 2 * v2 + t2 + t1) * sCubed) +
					((3 * v2 - 3 * v1 - 2 * t1 - t2) * sSquared) +
					(t1 * s) +
					v1
				);
			}

			return (float) result;
		}

		/// Returns the Cartesian coordinate for one axis of a point that is defined by a
		/// given triangle and two normalized barycentric (areal) coordinates.
		/// <param name="value1">
		/// The coordinate on one axis of vertex 1 of the defining triangle.
		/// </param>
		/// <param name="value2">
		/// The coordinate on the same axis of vertex 2 of the defining triangle.
		/// </param>
		/// <param name="value3">
		/// The coordinate on the same axis of vertex 3 of the defining triangle.
		/// </param>
		/// <param name="amount1">
		/// The normalized barycentric (areal) coordinate b2, equal to the weighting factor
		/// for vertex 2, the coordinate of which is specified in value2.
		/// </param>
		/// @param amount2
		/// The normalized barycentric (areal) coordinate b3, equal to the weighting factor
		/// for vertex 3, the coordinate of which is specified in value3.
		/// </param>
		/// @returns Cartesian coordinate of the specified point with respect to the axis being used.
		public static float Barycentric(
			float value1,
			float value2,
			float value3,
			float amount1,
			float amount2
		) {
			return value1 + (value2 - value1) * amount1 + (value3 - value1) * amount2;
		}

		/// Performs a Catmull-Rom interpolation using the specified positions.
		/// @param value1 The first position in the interpolation.
		/// @param value2">The second position in the interpolation.
		/// @param value3">The third position in the interpolation.
		/// @param value4">The fourth position in the interpolation.
		/// @param name="amount">Weighting factor.
		/// @returns A position that is the result of the Catmull-Rom interpolation.
		public static float CatmullRom(
			float value1,
			float value2,
			float value3,
			float value4,
			float amount
		) {
			/* Using formula from http://www.mvps.org/directx/articles/catmull/
			 * Internally using doubles not to lose precision.
			 */
			double amountSquared = amount * amount;
			double amountCubed = amountSquared * amount;
			return (float) (
				0.5 *
				(
					((2.0 * value2 + (value3 - value1) * amount) +
					((2.0 * value1 - 5.0 * value2 + 4.0 * value3 - value4) * amountSquared) +
					(3.0 * value2 - value1 - 3.0 * value3 + value4) * amountCubed)
				)
			);
		}
    }
}
