namespace System
{
	public enum ConsoleColor
	{
		Black,
		DarkBlue,
		DarkGreen,
		DarkCyan,
		DarkRed,
		DarkMagenta,
		DarkYellow,
		DarkGray,
		Gray,
		Blue,
		Green,
		Cyan,
		Red,
		Magenta,
		Yellow,
		White
	}

	extension ConsoleColor
	{
		public uint8 ToConsoleTextAttribute()
		{
			switch (this)
			{
			case .Black:
				return 0;
			case .DarkBlue:
				return 1;
			case .DarkGreen:
				return 2;
			case .DarkCyan:
				return 3;
			case .DarkRed:
				return 4;
			case .DarkMagenta:
				return 5;
			case .DarkYellow:
				return 6;
			case .DarkGray:
				return 7;
			case .Gray:
				return 8;
			case .Blue:
				return 9;
			case .Green:
				return 10;
			case .Cyan:
				return 11;
			case .Red:
				return 12;
			case .Magenta:
				return 13;
			case .Yellow:
				return 14;
			case .White:
				return 15;
			}
		}

		public uint8 ToAnsi()
		{
			switch (this)
			{
			case .Black:
				return 30;
			case .DarkRed:
				return 31;
			case .DarkGreen:
				return 32;
			case .DarkYellow:
				return 33;
			case .DarkBlue:
				return 34;
			case .DarkMagenta:
				return 35;
			case .DarkCyan:
				return 36;
			case .Gray:
				return 37;
			case .DarkGray:
				return 90;
			case .Red:
				return 91;
			case .Green:
				return 92;
			case .Yellow:
				return 93;
			case .Blue:
				return 94;
			case .Magenta:
				return 95;
			case .Cyan:
				return 96;
			case .White:
				return 97;
			}
		}
	}
}
