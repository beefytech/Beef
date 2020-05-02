using System;
using System.Collections;
using System.Text;
using System.Reflection;

namespace Beefy.widgets
{
	[AllowDuplicates]
    public enum KeyCode
    {
        LButton = 0x01,
        RButton = 0x02,
        Break = 0x03,
        MButton = 0x04,
        Backspace = 0x08,
        Tab = 0x09,
        Clear = 0x0C,
        Return = 0x0D,
        Shift = 0x10,
        Control = 0x11,
        Alt = 0x12,
        Menu = 0x12,
		Pause = 0x13,
        Capital = 0x14,
        Kana = 0x15,
        Hangul = 0x15,
        Junja = 0x17,
        Final = 0x18,
        Hanja = 0x19,
        Kanji = 0x19,
        Escape = 0x1B,
        Convert = 0x1C,
        NonConvert = 0x1D,
        Accept = 0x1E,
        ModeChange = 0x1F,
        Space = 0x20,
        PageUp = 0x21,
        PageDown = 0x22,
        End = 0x23,
        Home = 0x24,
        Left = 0x25,
        Up = 0x26,
        Right = 0x27,
        Down = 0x28,
        Select = 0x29,
        Print = 0x2A,
        Execute = 0x2B,
        Snapshot = 0x2C,
        Insert = 0x2D,
        Delete = 0x2E,
        Help = 0x2F,
        LWin = 0x5B,
        RWin = 0x5C,
        Apps = 0x5D,
        Numpad0 = 0x60,
        Numpad1 = 0x61,
        Numpad2 = 0x62,
        Numpad3 = 0x63,
        Numpad4 = 0x64,
        Numpad5 = 0x65,
        Numpad6 = 0x66,
        Numpad7 = 0x67,
        Numpad8 = 0x68,
        Numpad9 = 0x69,
        Multiply = 0x6A,
        Add = 0x6B,
        Separator = 0x6C,
        Subtract = 0x6D,
        Decimal = 0x6E,
        Divide = 0x6F,
        F1 = 0x70,
        F2 = 0x71,
        F3 = 0x72,
        F4 = 0x73,
        F5 = 0x74,
        F6 = 0x75,
        F7 = 0x76,
        F8 = 0x77,
        F9 = 0x78,
        F10 = 0x79,
        F11 = 0x7A,
        F12 = 0x7B,
        Numlock = 0x90,
        Scroll = 0x91,
		Semicolon = 0xBA,
		Equals = 0xBB,
		Comma = 0xBC,
		Minus = 0xBD,
		Period = 0xBE,
		Slash = 0xBF,
		Grave = 0xC0,
		LBracket = 0xDB,
		Backslash = 0xDC,
		RBracket = 0xDD,
		Apostrophe = 0xDE,
		Backtick = 0xDF,
        Tilde = 0xC0,
        Command = 0xF0,
        COUNT = 0xFF,

		Media_NextTrack = 0xB0,
		Media_PreviousTrack = 0xB1,
		Media_PlayPause = 0xB3,
		//Z = 0xB4
    }

	extension KeyCode
	{
		public static Result<KeyCode> Parse(StringView str)
		{
			if (str.Length == 1)
			{
				char8 c = str[0];
				if ((c >= 'A') && (c <= 'Z'))
					return (KeyCode)c;
				if ((c >= '0') && (c <= '9'))
					return (KeyCode)c;
				if ((c >= 'a') && (c <= 'a'))
					return (KeyCode)(c.ToUpper);
			}

			return Enum.Parse<KeyCode>(str, true);
		}

		public override void ToString(String strBuffer)
		{
			if (((this >= (KeyCode)'A') && (this <= (KeyCode)'Z')) ||
				((this >= (KeyCode)'0') && (this <= (KeyCode)'9')))
			{
				((char8)this).ToString(strBuffer);
				return;
			}

			base.ToString(strBuffer);
		}
	}
}
///