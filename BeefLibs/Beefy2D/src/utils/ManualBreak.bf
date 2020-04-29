using System;
using System.Collections;
using System.Text;
using System.Diagnostics;

namespace Beefy.utils
{
    public static class ManualBreak
    {
        public static void Break()
        {
			ThrowUnimplemented();
            //Debugger.Break();
        }
    }
}
