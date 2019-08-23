using System;
using System.Collections.Generic;
using System.Text;

namespace Beefy.events
{
    public class ScrollEvent : Event
    {
        public double mOldPos;
        public double mNewPos;
		public bool mIsFromUser;
    }
}
