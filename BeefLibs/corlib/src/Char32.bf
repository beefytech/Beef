namespace System
{
	struct Char32 : char32, IHashable
	{
	    public int GetHashCode()
		{
			return (int)this;
		}

		public extern char32 ToLower
		{
			get;
		}

		public extern char32 ToUpper
		{
			get;
		}

		public extern bool IsLower
		{
			get;
		}

		public extern bool IsUpper
		{
			get;
		}

		extern bool IsWhiteSpace_EX
		{
			get;
		}

		public bool IsWhiteSpace
		{
			get
			{
				if (this <= (char32)0xFF)
				{
					// U+0009 = <control> HORIZONTAL TAB
					// U+000a = <control> LINE FEED
					// U+000b = <control> VERTICAL TAB
					// U+000c = <contorl> FORM FEED
					// U+000d = <control> CARRIAGE RETURN
					// U+0085 = <control> NEXT LINE
					// U+00a0 = NO-BREAK SPACE
					return ((this == ' ') || (this >= '\x09' && this <= '\x0d') || this == '\xa0' || this == '\x85');
				}
				else
					return IsWhiteSpace_EX;
			}
		}

		public extern bool IsLetterOrDigit
		{
			get;
		}

		public extern bool IsLetter
		{
			get;
		}

		public extern bool IsNumber
		{
			get;
		}

		public bool IsControl
		{
			get
			{
				return (((char32)this >= (char32)0) && ((char32)this <= (char32)0x1F)) || (((char32)this >= (char32)0x7F) && ((char32)this <= (char32)0x9F));
			}
		}

		public bool IsCombiningMark
		{
			get
			{
				let c = (char32)this;
				return ((c >= '\u{0300}') && (c <= '\u{036F}')) || ((c >= '\u{1DC0}') && (c <= '\u{1DFF}'));
			}
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append((char32)this);
		}
	}
}
