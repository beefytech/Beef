namespace System
{
	struct Char16 : char16, IHashable
	{
		internal const int UNICODE_PLANE00_END = 0x00ffff;
		// The starting codepoint for Unicode plane 1.  Plane 1 contains 0x010000 ~ 0x01ffff.
		internal const int UNICODE_PLANE01_START = 0x10000;
		// The end codepoint for Unicode plane 16.  This is the maximum code point value allowed for Unicode.
		// Plane 16 contains 0x100000 ~ 0x10ffff.
		internal const int UNICODE_PLANE16_END   = 0x10ffff;

		internal const char16 HIGH_SURROGATE_START = (char16)0xd800;
		internal const char16 LOW_SURROGATE_END    = (char16)0xdfff;
		internal const char16 HIGH_SURROGATE_END   = (char16)0xdbff;
		internal const char16 LOW_SURROGATE_START  = (char16)0xdc00;

		int IHashable.GetHashCode()
		{
			return (int)this;
		}

		public extern char16 ToLower
		{
			get;
		}

		public extern char16 ToUpper
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

		public extern bool IsWhiteSpace
		{
			get;
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
				return (((char16)this >= (char16)0) && ((char16)this <= (char16)0x1F)) || (((char16)this >= (char16)0x7F) && ((char16)this <= (char16)0x9F));
			}
		}

		public bool IsCombiningMark
		{
			get
			{
				let c = (char16)this;
				return ((c >= '\u{0300}') && (c <= '\u{036F}')) || ((c >= '\u{1DC0}') && (c <= '\u{1DFF}'));
			}
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append((char16)this);
		}

		public bool IsSurrogate
		{
			get
			{
			    return (this >= HIGH_SURROGATE_START && this <= LOW_SURROGATE_END);
			}
		}

		public static bool IsSurrogatePair(char16 highSurrogate, char16 lowSurrogate)
		{
		    return ((highSurrogate >= HIGH_SURROGATE_START && highSurrogate <= HIGH_SURROGATE_END) &&
				(lowSurrogate >= LOW_SURROGATE_START && lowSurrogate <= LOW_SURROGATE_END));
		}
	}
}
