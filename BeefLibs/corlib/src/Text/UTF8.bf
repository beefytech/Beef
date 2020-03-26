namespace System.Text
{
	class UTF8
	{
		public static int8* sTrailingBytesForUTF8 = new int8[]*
		{
		    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
		    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
		} ~ delete _;

		public static uint32* sOffsetsFromUTF8 = new uint32[]*
		{
		    0x00000000, 0x00003080, 0x000E2080,
		    0x03C82080, 0xFA082080, 0x82082080
		} ~ delete _;

		public static int GetEncodedLength(char32 c)
		{
			if (c <(char32)0x80)
				return 1;			
			else if (c < (char32)0x800)
				return 2;			
			else if (c < (char32)0x10000)
				return 3;			
			else if (c < (char32)0x110000)
				return 4;			
			return 5;
		}

		public static int GetDecodedLength(char8* buf)
		{
			char32 c = *buf;
			return UTF8.sTrailingBytesForUTF8[c] + 1;
		}

		public static int GetDecodedLength(char8 firstChar)
		{
			return UTF8.sTrailingBytesForUTF8[firstChar] + 1;
		}

		public static (char32 c, int8 length) Decode(char8* buf, int bufSize)
		{
			char32 c = *buf;
			int8 trailingBytes = UTF8.sTrailingBytesForUTF8[c];
			if (trailingBytes > bufSize)
				return ((char32)-1, trailingBytes + 1);

			int bufIdx = 1;
			switch (trailingBytes)
			{
			case 3: c <<= 6; c += (int32)buf[bufIdx++]; fallthrough;
			case 2: c <<= 6; c += (int32)buf[bufIdx++]; fallthrough;
			case 1: c <<= 6; c += (int32)buf[bufIdx++]; fallthrough;
			}
			c -= (int32)UTF8.sOffsetsFromUTF8[trailingBytes];
			return (c, trailingBytes + 1);
		}

		public static Result<(char32, int32)> TryDecode(char8* buf, int bufSize)
		{
			char32 c = *buf;
			int8 trailingBytes = UTF8.sTrailingBytesForUTF8[c];
			if (trailingBytes > bufSize)
				return .Ok(((char32)-1, trailingBytes + 1));

			switch (trailingBytes)
			{
			case 1:
				char8 c2 = buf[1];
				if (((uint8)c2 & 0xC0) != 0x80)
					return .Err;
				c <<= 6;
				c += (int32)c2;
			case 2:
				char8 c2 = buf[1];
				if (((uint8)c2 & 0xC0) != 0x80)
					return .Err;
				char8 c3 = buf[2];
				if (((uint8)c3 & 0xC0) != 0x80)
					return .Err;
				c <<= 6;
				c += (int32)c2;
				c <<= 6;
				c += (int32)c3;
			case 3:
				char8 c2 = buf[1];
				if (((uint8)c2 & 0xC0) != 0x80)
					return .Err;
				char8 c3 = buf[2];
				if (((uint8)c3 & 0xC0) != 0x80)
					return .Err;
				char8 c4 = buf[3];
				if (((uint8)c4 & 0xC0) != 0x80)
					return .Err;
				c <<= 6;
				c += (int32)c2;
				c <<= 6;
				c += (int32)c3;
				c <<= 6;
				c += (int32)c4;
			}
			c -= (int32)UTF8.sOffsetsFromUTF8[trailingBytes];
			return .Ok((c, trailingBytes + 1));
		}

		public static int Encode(char32 c, Span<char8> dest)
		{
			char8* destEnd = dest.EndPtr;
			char8* curDest = dest.Ptr;
			int len = 0;
			if (c < (char32)0x80)
            {
				if (curDest >= destEnd)
					return 1;
				len = 1;
				*curDest++ = (char8)c;
			}
			else if (c < (char32)0x800)
            {
				if (curDest >= destEnd - 1)
					return 2;
				len = 2;
				*curDest++ = (.)(((uint32)c >> 6) | 0xC0);
				*curDest++ = (.)(((uint32)c & 0x3F) | 0x80);
			}
			else if (c < (char32)0x10000)
            {
				if (curDest >= destEnd - 2)
					return 3;
				len = 3;
				*curDest++ = (.)(((uint32)c >> 12) | 0xE0);
				*curDest++ = (.)((((uint32)c >> 6) & 0x3F) | 0x80);
				*curDest++ = (.)(((uint32)c & 0x3F) | 0x80);
			}
			else if (c < (char32)0x110000)
            {
				if (curDest >= destEnd - 3)
					return 4;
				len = 4;
				*curDest++ = (.)(((uint32)c >> 18) | 0xF0);
				*curDest++ = (.)((((uint32)c >> 12) & 0x3F) | 0x80);
				*curDest++ = (.)((((uint32)c >> 6) & 0x3F) | 0x80);
				*curDest++ = (.)(((uint32)c & 0x3F) | 0x80);
			}
			return len;
		}

		public static void Encode(char32 c, ref char8* dest)
		{
			if (c < (char32)0x80)
		    {
				*dest++ = (char8)c;
			}
			else if (c < (char32)0x800)
		    {
				*dest++ = (.)(((uint32)c >> 6) | 0xC0);
				*dest++ = (.)(((uint32)c & 0x3F) | 0x80);
			}
			else if (c < (char32)0x10000)
		    {
				
				*dest++ = (.)(((uint32)c >> 12) | 0xE0);
				*dest++ = (.)((((uint32)c >> 6) & 0x3F) | 0x80);
				*dest++ = (.)(((uint32)c & 0x3F) | 0x80);
			}
			else if (c < (char32)0x110000)
		    {
				*dest++ = (.)(((uint32)c >> 18) | 0xF0);
				*dest++ = (.)((((uint32)c >> 12) & 0x3F) | 0x80);
				*dest++ = (.)((((uint32)c >> 6) & 0x3F) | 0x80);
				*dest++ = (.)(((uint32)c & 0x3F) | 0x80);
			}
		}
	}
}
