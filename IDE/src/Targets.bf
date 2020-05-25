using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;

namespace IDE
{
    public class Targets
    {
        [CallingConvention(.Stdcall), CLink]
        static extern void Targets_Create();

        [CallingConvention(.Stdcall), CLink]
        static extern void Targets_Delete();

        public this()
        {
            Targets_Create();
        }

        public ~this()
        {
            Targets_Delete();
        }
    }
}
