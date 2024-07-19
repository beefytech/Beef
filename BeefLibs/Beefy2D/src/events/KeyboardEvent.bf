using System;
using System.Collections;
using System.Text;
using Beefy.widgets;

namespace Beefy.widgets
{
    public enum KeyFlags
    {
		case None = 0,
	        Alt = 1,
	        Ctrl = 2,
	        Shift = 4,
			CapsLock = 8,
			NumLock = 0x10;

		public KeyFlags HeldKeys => this & ~(CapsLock | NumLock);
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
