using System;
using System.Collections;
using System.Text;
using Beefy.widgets;

namespace Beefy.widgets
{
    public enum KeyFlags
    {
		None = 0,
        Alt = 1,
        Ctrl = 2,
        Shift = 4
    }
}

namespace Beefy.events
{
    /*[Flags]
    public enum KeyFlags
    {
        Alt = 1,
        Ctrl = 2,
        Shift = 4
    }*/

    public class KeyDownEvent : Event
    {
        public KeyFlags mKeyFlags;
        public KeyCode mKeyCode;
		public bool mIsRepeat;
    }

	public class KeyCharEvent : Event
	{
		public char32 mChar;
	}
}
