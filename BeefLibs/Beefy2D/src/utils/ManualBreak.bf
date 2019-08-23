using System;
using System.Collections.Generic;
using System.Text;
using System.Runtime;
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
