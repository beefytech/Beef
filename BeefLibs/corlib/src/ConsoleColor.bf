namespace System
{
	public enum ConsoleColor
	{
		case Black;
		case DarkBlue;
		case DarkGreen;
		case DarkCyan;
		case DarkRed;
		case DarkMagenta;
		case DarkYellow;
		case Gray;
		case DarkGray;
		case Blue;
		case Green;
		case Cyan;
		case Red;
		case Magenta;
		case Yellow;
		case White;
	
		public uint8 ConsoleTextAttribute
		{
			get
			{
				return (.)this;
			}

			set mut
			{
				this = (.)value;
			}
		}

		public uint8 AnsiCode
		{
			get
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
}
