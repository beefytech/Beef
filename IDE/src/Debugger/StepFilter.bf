using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;

namespace IDE.Debugger
{
	enum StepFilterKind : int32
	{
		Default,
		Filtered,
		NotFiltered
	}

    public class StepFilter
    {
        public String mFilter ~ delete _;
        public void* mNativeStepFilter;
        public bool mIsGlobal;
		public StepFilterKind mKind;

        [StdCall, CLink]
        static extern void StepFilter_Delete(char8* filter);

        public ~this()
        {
            StepFilter_Delete(mFilter);
        }
    }
}
