using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;

namespace IDE
{
    public class Targets
    {
        [StdCall, CLink]
        static extern void Targets_Create();

        [StdCall, CLink]
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
