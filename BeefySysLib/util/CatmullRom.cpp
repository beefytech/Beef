#include "CatmullRom.h"

USING_NS_BF;

Point2D Beefy::CatmullRomEvaluate(Point2D &p0, Point2D &p1, Point2D &p2, Point2D &p3, float tension, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    
	/*
	 * Formula: s(-ttt + 2tt - t)P1 + s(-ttt + tt)P2 + (2ttt - 3tt + 1)P2 + s(ttt - 2tt + t)P3 + (-2ttt + 3tt)P3 + s(ttt - tt)P4
	 */
    float s = (1 - tension) / 2;
	
    float b1 = s * ((-t3 + (2 * t2)) - t);                      // s(-t3 + 2 t2 - t)P1
    float b2 = s * (-t3 + t2) + (2 * t3 - 3 * t2 + 1);          // s(-t3 + t2)P2 + (2 t3 - 3 t2 + 1)P2
    float b3 = s * (t3 - 2 * t2 + t) + (-2 * t3 + 3 * t2);      // s(t3 - 2 t2 + t)P3 + (-2 t3 + 3 t2)P3
    float b4 = s * (t3 - t2);                                   // s(t3 - t2)P4
    
    float x = (p0.mX*b1 + p1.mX*b2 + p2.mX*b3 + p3.mX*b4);
    float y = (p0.mY*b1 + p1.mY*b2 + p2.mY*b3 + p3.mY*b4);
	
	return Point2D(x,y);
}
