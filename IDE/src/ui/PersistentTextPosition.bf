using System;
using System.Collections;
using System.Text;
using System.Threading.Tasks;
using Beefy.widgets;

namespace IDE.ui
{
    public class PersistentTextPosition
    {        
        public int32 mIndex;
        public bool mWasDeleted;

        public this(int32 index)
        {
            mIndex = index;
        }

		public ~this()
		{

		}
    }
}
