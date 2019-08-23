#include "CubicSpline.h"

USING_NS_BF;
  
/* calculates the natural cubic spline that interpolates
y[0], y[1], ... y[n]
The first segment is returned as
C[0].a + C[0].b*u + C[0].c*u^2 + C[0].d*u^3 0<=u <1
the other segments are in C[1], C[2], ...  C[n-1] */

CubicVal* CubicSpline2D::SolveCubic(std::vector<float> vals)
{
	int n = (int) vals.size() - 1;
	const float* x = &vals[0];

	float* gamma = new float[n+1];
	float* delta = new float[n+1];
	float* D = new float[n+1];
	int i;
	/* We solve the equation
	[2 1       ] [D[0]]   [3(x[1] - x[0])  ]
	|1 4 1     | |D[1]|   |3(x[2] - x[0])  |
	|  1 4 1   | | .  | = |      .         |
	|    ..... | | .  |   |      .         |
	|     1 4 1| | .  |   |3(x[n] - x[n-2])|
	[       1 2] [D[n]]   [3(x[n] - x[n-1])]
       
	by using row operations to convert the matrix to upper triangular
	and then back sustitution.  The D[i] are the derivatives at the knots.
	*/
    
	gamma[0] = 1.0f/2.0f;
	for ( i = 1; i < n; i++)
		gamma[i] = 1/(4-gamma[i-1]);	
	gamma[n] = 1/(2-gamma[n-1]);
    
	delta[0] = 3*(x[1]-x[0])*gamma[0];
	for ( i = 1; i < n; i++) 	
		delta[i] = (3*(x[i+1]-x[i-1])-delta[i-1])*gamma[i];	
	delta[n] = (3*(x[n]-x[n-1])-delta[n-1])*gamma[n];
    
	D[n] = delta[n];
	for ( i = n-1; i >= 0; i--)
		D[i] = delta[i] - gamma[i]*D[i+1];	

	/* now compute the coefficients of the cubics */
	CubicVal* C = new CubicVal[n];
	for ( i = 0; i < n; i++) 
	{
		C[i].Set((float)x[i], D[i], 3*(x[i+1] - x[i]) - 2*D[i] - D[i+1],
		2*(x[i] - x[i+1]) + D[i] + D[i+1]);
	}
	return C;
}

CubicSpline2D::CubicSpline2D()
{
	mXCubicArray = NULL;
	mYCubicArray = NULL;
}

CubicSpline2D::~CubicSpline2D()
{
	delete mXCubicArray;
	delete mYCubicArray;
}

void CubicSpline2D::AddPt(float x, float y)
{
	delete mXCubicArray;
	mXCubicArray = NULL;
	delete mYCubicArray;
	mYCubicArray = NULL;

	mInputPoints.push_back(Point2D(x, y));
}

int CubicSpline2D::GetLength()
{
	return (int) mInputPoints.size();
}

void CubicSpline2D::Calculate()
{
	std::vector<float> xVals;
	std::vector<float> yVals;
	for (int i = 0; i < (int) mInputPoints.size(); i++)
	{
		xVals.push_back(mInputPoints[i].mX);
		yVals.push_back(mInputPoints[i].mY);
	}

	mXCubicArray = SolveCubic(xVals);
	mYCubicArray = SolveCubic(yVals);
}

Point2D CubicSpline2D::Evaluate(float t)
{
	if (mXCubicArray == NULL)
		Calculate();

	int idx = (int) t;
	float frac = t - idx;
	
	if (idx >= (int) mInputPoints.size() - 1)
		return mInputPoints[mInputPoints.size() - 1];

	return Point2D(mXCubicArray[idx].Evaluate(frac), mYCubicArray[idx].Evaluate(frac));
}