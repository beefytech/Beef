#include "CubicFuncSpline.h"

USING_NS_BF;
  
CubicFuncSpline::CubicFuncSpline()
{
	lagpoly = NULL;
	intpoly = NULL;
	slopes = NULL;
}

CubicFuncSpline::~CubicFuncSpline()
{
	delete lagpoly;
	delete intpoly;
	delete slopes;
}

void CubicFuncSpline::AddPt(float x, float y)
{
	delete lagpoly;
	delete intpoly;
	delete slopes;
	lagpoly = NULL;
	intpoly = NULL;
	slopes = NULL;

	mInputPoints.push_back(Point2D(x, y));
}

int CubicFuncSpline::GetLength()
{
	return (int) mInputPoints.size();
}

void CubicFuncSpline::Lagrange()
{
	int nPts = (int) mInputPoints.size();
	int i, j, jj, k;
	for( j = 0; j < nPts; j++ )
		intpoly[j] = 0.0;
	for( i = 0; i < nPts; i++ ) 
	{
		lagpoly[i+0*nPts] = 1.0;
		float fac = mInputPoints[i].mY;
		j = 0;
		for( k = 0; k < nPts; k++ ) 
		{
			if( k == i )
				continue;
			lagpoly[i+(j+1)*nPts] = lagpoly[i+j*nPts];
			for( jj = j; jj > 0; jj-- )
				lagpoly[i+jj*nPts] = lagpoly[i+(jj-1)*nPts] - lagpoly[i+jj*nPts]*mInputPoints[k].mX;
			lagpoly[i+0*nPts] *= -mInputPoints[k].mX;
			j++;
			fac /= ( mInputPoints[i].mX - mInputPoints[k].mX );
		}
		for( j = 0; j < nPts; j++ )
			lagpoly[i+j*nPts] *= fac;
		for( j = 0; j < nPts; j++ )
			intpoly[j] += lagpoly[i+j*nPts];
	}
}

void CubicFuncSpline::ComputeSplineSlopes()
{	
	int n = (int) mInputPoints.size() - 1;	
	int i;
	float *h = new float[n];
	float *hinv = new float[n];
	float *g = new float[n];
	float *a = new float[n+1];
	float *b = new float[n+1];
	float fac;

	for( i = 0; i < n; i++ ) 
	{
		h[i] = mInputPoints[i+1].mX - mInputPoints[i].mX;
		hinv[i] = 1.0f / h[i];
		g[i] = 3 * ( mInputPoints[i+1].mY-mInputPoints[i].mY ) * hinv[i] * hinv[i];
	}
	a[0] = 2 * hinv[0];
	b[0] = g[0];
	for( i = 1; i <= n; i++ ) 
	{
		fac = hinv[i-1]/a[i-1];
		a[i] = (2-fac) * hinv[i-1];
		b[i] = g[i-1] - fac * b[i-1];
		if( i < n ) 
		{
			a[i] += 2 * hinv[i];
			b[i] += g[i];
		}
	}
	slopes[n] = b[n] / a[n];
	for( i = n-1; i >= 0; i-- )
		slopes[i] = ( b[i] - hinv[i] * slopes[i+1] ) / a[i];
	delete [] h;
	delete [] hinv;
	delete [] g;
	delete [] a;
	delete [] b;
}

void CubicFuncSpline::Calculate()
{
	int n = (int) mInputPoints.size() - 1;	
	slopes = new float[n+1];
	intpoly = new float[n+1];
	lagpoly = new float[(n+1)*(n+1)];

	Lagrange();
	ComputeSplineSlopes();
}

float CubicFuncSpline::Evaluate(float x)
{
	if (lagpoly == NULL)
		Calculate();
	
	int idx = (int) 0;
	while ((idx < (int) mInputPoints.size()) && (x > mInputPoints[idx].mX))
		idx++;
		
	if ((idx == mInputPoints.size()) || (idx == 0))
	{
		// Past end - extrapolate
		if (idx == mInputPoints.size())			
			idx--;
		float s1 = slopes[idx];
		return mInputPoints[idx].mY + (x - mInputPoints[idx].mX) * s1;
	}

	float x0 = mInputPoints[idx-1].mX;
	float x1 = mInputPoints[idx].mX;
	float y0 = mInputPoints[idx-1].mY;
	float y1 = mInputPoints[idx].mY;
	float s0 = slopes[idx-1];	
	float s1 = slopes[idx];

	float h = x1 - x0;
	float t = (x-x0)/h;
	float u = 1 - t;

	return u * u * ( y0 * ( 2 * t + 1 ) + s0 * h * t )
        + t * t * ( y1 * ( 3 - 2 * t ) - s1 * h * u );
}

///

CubicUnitFuncSpline::CubicUnitFuncSpline()
{
	lagpoly = NULL;
	intpoly = NULL;
	slopes = NULL;
}

CubicUnitFuncSpline::~CubicUnitFuncSpline()
{
	delete lagpoly;
	delete intpoly;
	delete slopes;
}

void CubicUnitFuncSpline::AddPt(float y)
{
	delete lagpoly;
	delete intpoly;
	delete slopes;
	lagpoly = NULL;
	intpoly = NULL;
	slopes = NULL;

	mInputPoints.push_back(y);
}

int CubicUnitFuncSpline::GetLength()
{
	return (int) mInputPoints.size();
}

void CubicUnitFuncSpline::Lagrange()
{
	int nPts = (int) mInputPoints.size();
	int i, j, jj, k;
	for( j = 0; j < nPts; j++ )
		intpoly[j] = 0.0;
	for( i = 0; i < nPts; i++ ) 
	{
		lagpoly[i+0*nPts] = 1.0;
		float fac = mInputPoints[i];
		j = 0;
		for( k = 0; k < nPts; k++ ) 
		{
			if( k == i )
				continue;
			lagpoly[i+(j+1)*nPts] = lagpoly[i+j*nPts];
			for( jj = j; jj > 0; jj-- )
				lagpoly[i+jj*nPts] = lagpoly[i+(jj-1)*nPts] - lagpoly[i+jj*nPts]*k;
			lagpoly[i+0*nPts] *= -k;
			j++;
			fac /= ( i - k);
		}
		for( j = 0; j < nPts; j++ )
			lagpoly[i+j*nPts] *= fac;
		for( j = 0; j < nPts; j++ )
			intpoly[j] += lagpoly[i+j*nPts];
	}
}

void CubicUnitFuncSpline::ComputeSplineSlopes()
{	
	int n = (int) mInputPoints.size() - 1;	
	int i;
	float *h = new float[n];
	float *hinv = new float[n];
	float *g = new float[n];
	float *a = new float[n+1];
	float *b = new float[n+1];
	float fac;

	for( i = 0; i < n; i++ ) 
	{
		h[i] = 1;
		hinv[i] = 1.0f / h[i];
		g[i] = 3 * ( mInputPoints[i+1] - mInputPoints[i] ) * hinv[i] * hinv[i];
	}
	a[0] = 2 * hinv[0];
	b[0] = g[0];

	for( i = 1; i <= n; i++ ) 
	{
		fac = hinv[i-1]/a[i-1];
		a[i] = (2-fac) * hinv[i-1];
		b[i] = g[i-1] - fac * b[i-1];
		if( i < n ) 
		{
			a[i] += 2 * hinv[i];
			b[i] += g[i];
		}
	}
	slopes[n] = b[n] / a[n];
	
	for( i = n-1; i >= 0; i-- )
		slopes[i] = ( b[i] - hinv[i] * slopes[i+1] ) / a[i];
	delete [] h;
	delete [] hinv;
	delete [] g;
	delete [] a;
	delete [] b;
}

void CubicUnitFuncSpline::Calculate()
{
	int n = (int) mInputPoints.size() - 1;	
	slopes = new float[n+1];
	intpoly = new float[n+1];
	lagpoly = new float[(n+1)*(n+1)];

	Lagrange();
	ComputeSplineSlopes();
}

float CubicUnitFuncSpline::Evaluate(float x)
{
	if (lagpoly == NULL)
		Calculate();
	
	int idx = (int) x;
		
	float x0 = (float)idx;
	float x1 = (float)idx + 1;
	float y0 = mInputPoints[idx];
	float y1 = mInputPoints[idx+1];
	float s0 = slopes[idx];
	float s1 = slopes[idx+1];

	float h = x1 - x0;
	float t = (x-x0)/h;
	float u = 1 - t;

	return u * u * ( y0 * ( 2 * t + 1 ) + s0 * h * t )
        + t * t * ( y1 * ( 3 - 2 * t ) - s1 * h * u );
}

