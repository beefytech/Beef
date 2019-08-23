#include "PolySpline.h"

USING_NS_BF;
  
PolySpline2D::PolySpline2D()
{
	mCoefs = NULL;	
}

PolySpline2D::~PolySpline2D()
{
	delete mCoefs;	
}

void PolySpline2D::AddPt(float x, float y)
{
	delete mCoefs;
	mCoefs = NULL;
	
	

	mInputPoints.push_back(Point2D(x, y));
}

int PolySpline2D::GetLength()
{
	return (int) mInputPoints.size();
}

void PolySpline2D::Calculate()
{
	int n = (int) mInputPoints.size();
	float* mat = new float[n*n];
	mCoefs = new float[n];

	for (int j=0; j<n; j++)
		mat[j*n] = mInputPoints[j].mY;
	for (int i=1; i<n; i++)
	{	    
	    for (int j=0; j<n-i; j++)
		    mat[i+j*n]=(mat[(i-1)+j*n]-mat[(i-1)+(j+1)*n])/(mInputPoints[j].mX-mInputPoints[j+i].mX);	    
	}

	for (int i=0; i<n; i++)
		mCoefs[i] = mat[i];

	delete mat;	
}

float PolySpline2D::Evaluate(float x)
{
	if (mCoefs == NULL)
		Calculate();

	float result = mCoefs[0];
	int n = (int) mInputPoints.size();
	for (int i = 1; i < n; i++)
	{	    
		float add = mCoefs[i];
	    for (int j = 0; j < i; j++)
			add *= (x - mInputPoints[j].mX);
		result += add;
	}

	return result;
}

