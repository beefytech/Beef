#include "BSpline.h"

USING_NS_BF;

static void compute_intervals(int *u, int n, int t)   // figure out the knots
{
  int j;

  for (j=0; j<=n+t; j++)
  {
    if (j<t)
      u[j]=0;
    else
    if ((t<=j) && (j<=n))
      u[j]=j-t+1;
    else
    if (j>n)
      u[j]=n-t+2;  // if n-t=-2 then we're screwed, everything goes to 0
  }
}

BSpline2D::BSpline2D()
{
	mUVals = NULL;
}

BSpline2D::~BSpline2D()
{
	delete mUVals;
}

void BSpline2D::AddPt(float x, float y)
{
	delete mUVals;
	mUVals = NULL;

	Point2D pt;
	pt.mX = x;
	pt.mY = y;
	mInputPoints.push_back(pt);
}

void BSpline2D::Calculate()
{
  int n = (int) mInputPoints.size();
  int t = 1;
  Point2D* control = &mInputPoints[0];
  

  mUVals=new int[n+t+1];
  compute_intervals(mUVals, n, t);

  //increment=(float) (n-t+2)/(num_output-1);  // how much parameter goes up each time
  //interval=0;

  /*for (output_index=0; output_index<num_output-1; output_index++)
  {
    compute_point(u, n, t, interval, control, &calcxyz);
    output[output_index].x = calcxyz.x;
    output[output_index].y = calcxyz.y;
    output[output_index].z = calcxyz.z;
    interval=interval+increment;  // increment our parameter
  }
  output[num_output-1].x=control[n].x;   // put in the last point
  output[num_output-1].y=control[n].y;
  output[num_output-1].z=control[n].z;

  delete u;*/
}

static float blend(int k, int t, int *u, float v)  // calculate the blending value
{
  float value;

  if (t==1)			// base case for the recursion
  {
    if ((u[k]<=v) && (v<u[k+1]))
      value=1;
    else
      value=0;
  }
  else
  {
    if ((u[k+t-1]==u[k]) && (u[k+t]==u[k+1]))  // check for divide by zero
      value = 0;
    else
    if (u[k+t-1]==u[k]) // if a term's denominator is zero,use just the other
      value = (u[k+t] - v) / (u[k+t] - u[k+1]) * blend(k+1, t-1, u, v);
    else
    if (u[k+t]==u[k+1])
      value = (v - u[k]) / (u[k+t-1] - u[k]) * blend(k, t-1, u, v);
    else
      value = (v - u[k]) / (u[k+t-1] - u[k]) * blend(k, t-1, u, v) +
	      (u[k+t] - v) / (u[k+t] - u[k+1]) * blend(k+1, t-1, u, v);
  }
  return value;
}


/*void compute_point(int *u, int n, int t, float v, point *control,
			point *output)*/

void BSpline2D::Evaluate(float pct, float* x, float* y)
{
	if (mUVals == NULL)
		Calculate();

  int k;
  float temp;

  int n = (int) mInputPoints.size();
  int t = (int)mInputPoints.size() - 3; // ????
  t = 1;

  Point2D* control = &mInputPoints[0];

  // initialize the variables that will hold our outputted point  

  float oX = 0;
  float oY = 0;

  for (k=0; k<=n; k++)
  {
    temp = blend(t,t,mUVals,pct);  // same blend is used for each dimension coordinate
    oX = oX + (control[k]).mX * temp;
    oY = oY + (control[k]).mY * temp;    
  }

  *x = oX;
  *y = oY;
}
