namespace System
{
#unwarn
	struct Char8 : char8, IHashable, IOpComparable, IIsNaN
	{
		public static int operator<=>(Char8 a, Char8 b)
		{
			return (char8)a <=> (char8)b;
		}

		bool IIsNaN.IsNaN
		{
			[SkipCall]
			get
			{
				return false;
			}
		}

		public char8 ToLower
		{
			get
			{
				if ((this >= 'A') && (this <= 'Z'))
					return (char8)((this - 'A') + 'a');
				return (char8)this;
			}
		}

		public char8 ToUpper
		{
			get
			{
				if ((this >= 'a') && (this <= 'z'))
					return (char8)((this - 'a') + 'A');
				return (char8)this;
			}
		}

		public bool IsLower
		{
			get
			{
				return ((this >= 'a') && (this <= 'z'));
			}
		}

		public bool IsUpper
		{
			get
			{
				return ((this >= 'A') && (this <= 'Z'));
			}
		}

		public bool IsWhiteSpace
		{
			get
			{
				// U+0009 = HORIZONTAL TAB
				// U+000a = LINE FEED
				// U+000b = VERTICAL TAB
				// U+000c = FORM FEED
				// U+000d = CARRIAGE RETURN
				switch (this)
				{
				case ' ', '\t', '\n', '\r': return true;
				default: return false;
				}
			}
		}

		public bool IsDigit
		{
			get
			{
				return ((this >= '0') && (this <= '9'));
			}
		}

		public bool IsLetterOrDigit
		{
			get
			{
				return (((this >= 'A') && (this <= 'Z')) || ((this >= 'a') && (this <= 'z')) || ((this >= '0') && (this <= '9')));
			}
		}

		public bool IsLetter
		{
			get
			{
				return (((this >= 'A') && (this <= 'Z')) || ((this >= 'a') && (this <= 'z')));
			}
		}

		public bool IsNumber
		{
			get
			{
				return ((this >= '0') && (this <= '9'));
			}
		}

		public bool IsControl
		{
			get
			{
				return ((this >= (char8)0) && (this <= (char8)0x1F)) || ((this >= (char8)0x7F) && (this <= (char8)0x9F));
			}
		}

		public int GetHashCode()
		{
			return (int32)this;
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append((char8)this);
		}
	}
}
