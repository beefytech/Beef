#include <math.h>
#include <stdio.h>
#include "BfObjects.h" 

namespace bf
{
	namespace System
	{
		class Math
		{		
		public:
			BFRT_EXPORT static float Acos(float d);
			BFRT_EXPORT static double Acos(double d);
			BFRT_EXPORT static float Asin(float d);
			BFRT_EXPORT static double Asin(double d);
			BFRT_EXPORT static float Atan(float d);
			BFRT_EXPORT static double Atan(double d);
			BFRT_EXPORT static float Atan2(float y, float x);
			BFRT_EXPORT static double Atan2(double y, double x);
			BFRT_EXPORT static float Ceiling(float a);
			BFRT_EXPORT static double Ceiling(double a);
			BFRT_EXPORT static float Cos(float d);
			BFRT_EXPORT static double Cos(double d);
			BFRT_EXPORT static float Cosh(float d);
			BFRT_EXPORT static double Cosh(double d);
			BFRT_EXPORT static float Floor(float d);
			BFRT_EXPORT static double Floor(double d);
			BFRT_EXPORT static float Sin(float a);
			BFRT_EXPORT static double Sin(double a);
			BFRT_EXPORT static float Tan(float a);
			BFRT_EXPORT static double Tan(double a);
			BFRT_EXPORT static float Sinh(float value);
			BFRT_EXPORT static double Sinh(double value);
			BFRT_EXPORT static float Tanh(float value);
			BFRT_EXPORT static double Tanh(double value);
			BFRT_EXPORT static float Round(float a);
			BFRT_EXPORT static double Round(double a);
			BFRT_EXPORT static float Sqrt(float f);
			BFRT_EXPORT static double Sqrt(double d);
			BFRT_EXPORT static float Cbrt(float f);
			BFRT_EXPORT static double Cbrt(double d);
			BFRT_EXPORT static float Log(float d);
			BFRT_EXPORT static double Log(double d);
			BFRT_EXPORT static float Log10(float d);
			BFRT_EXPORT static double Log10(double d);
			BFRT_EXPORT static float Exp(float d);
			BFRT_EXPORT static double Exp(double d);
			BFRT_EXPORT static float Pow(float x, float y);
			BFRT_EXPORT static double Pow(double x, double y);
			BFRT_EXPORT static float Abs(float value);
			BFRT_EXPORT static double Abs(double value);
		};
	}
}

using namespace bf::System;

float Math::Acos(float d)
{
	return acosf(d);
}

double Math::Acos(double d)
{
	return acos(d);
}

float Math::Asin(float d)
{
	return asinf(d);
}

double Math::Asin(double d)
{
	return asin(d);
}

float Math::Atan(float d)
{
	return atanf(d);
}

double Math::Atan(double d)
{
	return atan(d);
}

float Math::Atan2(float y, float x)
{
	return atan2f(y, x);
}

double Math::Atan2(double y,double x)
{
	return atan2(y, x);
}

float Math::Ceiling(float a)
{
	return ceilf(a);
}

double Math::Ceiling(double a)
{
	return ceil(a);
}

float Math::Cos(float d)
{
	return cosf(d);
}

double Math::Cos(double d)
{
	return cos(d);
}

float Math::Cosh(float d)
{
	return coshf(d);
}

double Math::Cosh(double d)
{
	return cosh(d);
}

float Math::Floor(float d)
{
	return floorf(d);
}

double Math::Floor(double d)
{
	return floor(d);
}

float Math::Sin(float a)
{
	return sinf(a);
}

double Math::Sin(double a)
{
	return sin(a);
}

float Math::Tan(float a)
{
	return tanf(a);
}

double Math::Tan(double a)
{
	return tan(a);
}

float Math::Sinh(float value)
{
	return sinhf(value);
}

double Math::Sinh(double value)
{
	return sinh(value);
}

float Math::Tanh(float value)
{
	return tanhf(value);
}

double Math::Tanh(double value)
{
	return tanh(value);
}

float Math::Round(float a)
{
	return roundf(a);
}

double Math::Round(double a)
{
	return round(a);
}

float Math::Sqrt(float f)
{
	return sqrtf(f);
}

double Math::Sqrt(double d)
{
	return sqrt(d);
}

float Math::Cbrt(float f)
{
	return cbrtf(f);
}

double Math::Cbrt(double d)
{
	return cbrt(d);
}

float Math::Log(float d)
{
	return logf(d);
}

double Math::Log(double d)
{
	return log(d);
}

float Math::Log10(float d)
{
	return log10f(d);
}

double Math::Log10(double d)
{
	return log10(d);
}

float Math::Exp(float d)
{
	return expf(d);
}

double Math::Exp(double d)
{
	return exp(d);
}

float Math::Pow(float x, float y)
{
	return powf(x, y);
}

double Math::Pow(double x, double y)
{
	return pow(x, y);
}

float Math::Abs(float value)
{
	return (float)fabs(value);
}

double Math::Abs(double value)
{
	return fabs(value);
}
