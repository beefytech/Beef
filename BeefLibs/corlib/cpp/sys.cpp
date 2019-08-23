// This is a test class for the debugger.  This file isn't actually used or required.

#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <math.h>
#include <vector>
#include <map>
#include <string>

namespace Hey
{
	namespace Dude
	{
		namespace Bro
		{
		    struct Vector2
		    {
		        float mX;
		        float mY;
		
		        void Write();
                float get__Length();
			};
		
		    void Vector2::Write()
		    {
                // Debug visualizers work for C++ on
                //  things like std::map and std::string
                std::map<int, std::string> testMap;
                testMap[1] = "One";
                testMap[2] = "Two";

                // Calls back into Length property getter in Beef
		        printf("X:%f Y:%f Length:%f\n", mX, mY, get__Length());
			}
		}
	}
}