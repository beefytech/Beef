using System.Diagnostics;
namespace System.Text
{
	public static class UTF16
	{
		public enum EncodeError
		{
			case Overflow(int len);
		}

		public static void Decode(char16* utf16Str, String outStr)
		{
			int utf8Len = GetLengthAsUTF8(utf16Str);
			outStr.Reserve(outStr.Length + utf8Len);

			char16* utf16Ptr = utf16Str;
			char16 utf16hi = 0;
			while (true)
			{
				char16 c = *(utf16Ptr++);
				char32 c32 = c;
				if (c32 == 0)
					break;
				if ((c >= '\u{D800}') && (c < '\u{DC00}'))
				{
					utf16hi = (char16)c;
					continue;
				}
				else if ((c >= '\u{DC00}') && (c < '\u{E000}'))
				{
					char16 utf16lo = c;
					c32 = (char32)(0x10000 | ((uint32)(utf16hi - 0xD800) << 10) | (uint32)(utf16lo - 0xDC00));
				}

				outStr.Append(c32);
			}
		}

		public static void Decode(Span<char16> utf16Str, String outStr)
		{
			int utf8Len = GetLengthAsUTF8(utf16Str);
			outStr.Reserve(outStr.Length + utf8Len);

			char16* utf16Ptr = utf16Str.Ptr;
			char16* utf16End = utf16Str.EndPtr;
			char16 utf16hi = 0;
			while (utf16Ptr < utf16End)
			{
				char16 c = *(utf16Ptr++);
				char32 c32 = c;
				if ((c >= '\u{D800}') && (c < '\u{DC00}'))
				{
					utf16hi = (char16)c;
					continue;
				}
				else if ((c >= '\u{DC00}') && (c < '\u{E000}'))
				{
					char16 utf16lo = c;
					c32 = (char32)(0x10000 | ((uint32)(utf16hi - 0xD800) << 10) | (uint32)(utf16lo - 0xDC00));
				}

				outStr.Append(c32);
			}
		}

		public static (char32 c, int8 cSize) Decode(char16* buf, int lenLeft = 0)
		{
			char16 c = buf[0];
			if ((c >='\u{D800}') && (c < '\u{DC00}'))
			{
				if (lenLeft == 1)
				{
					// This is considered a soft error
					return ((char32)c, 2);
				}

				char16 utf16lo = buf[1];
				if (utf16lo == 0)
				{
#if BF_UTF_PEDANTIC
					// No trailing char
					Debug.Assert(utf16lo != 0);
#endif
                    return ((char32)c, 1);
				}
				char32 c32 = (char32)(0x10000 | ((uint32)(c - 0xD800) << 10) | (uint32)(utf16lo - 0xDC00));
				return (c32, 2);
			}
#if BF_UTF_PEDANTIC
			Debug.Assert((c <= '\u{D7FF}') || (c >= '\u{E000}'));
#endif
			return (c, 1);
		}

		public static int GetLengthAsUTF8(char16* utf16Str)
		{
			int utf8len = 0;
			char16* c16Ptr = utf16Str;
			while (true)
			{
				let (c, encLen) = Decode(c16Ptr, 0);
				if (c == 0)
					return utf8len;
				c16Ptr += encLen;
				utf8len += UTF8.GetEncodedLength(c);
			}
		}

		public static int GetLengthAsUTF8(Span<char16> utf16Str)
		{
			int utf8len = 0;
			char16* c16Ptr = utf16Str.Ptr;
			int lenLeft = utf16Str.Length;
			while (lenLeft > 0)
			{
				let (c, encLen) = Decode(c16Ptr, lenLeft);
				c16Ptr += encLen;
				lenLeft -= encLen;
				utf8len += UTF8.GetEncodedLength(c);
			}
			return utf8len;
		}

		public static bool Equals(char16* utf16Str, String str)
		{
			int strIdx = 0;
			char16* c16Ptr = utf16Str;
			while (true)
			{
				let (cA, encLenA) = Decode(c16Ptr);
				if (strIdx == str.Length)
					return cA == 0;
				let (cB, encLenB) = str.GetChar32(strIdx);
				if (cA != cB)
					return false;
				c16Ptr += encLenA;
				strIdx += encLenB;
			}
		}

		public static int GetMaxEncodedLen(int utf8Len)
		{
			//  Consider all incoming chars are < \u80, each incoming char8 equals one outgoing char16 (utfLen * 1)
			//  For chars from \u80 to \u7FF, then two incoming char8 equals one outgoing char16 (utfLen * 0.5)
			//  For chars from \u800 to \u7FFF, then three incoming char8 equals one or two char16s (utfLen * 0.33) to (utfLen * 0.67)
			//  For chars from \u1000 to \u10FFFF, then four incoming char8 equals two outgoing char16s (utfLen * 0.5)
			return utf8Len;
		}

		public static int GetEncodedLength(char32 c)
		{
			if (c <= '\u{FFFF}')
				return 1;
			return 2;
		}

		public static int GetEncodedLen(StringView str)
		{
			int len = 0;
			for (var c in str.DecodedChars)
			{
				if (c <= '\u{FFFF}')
				{
#if BF_UTF_PEDANTIC
					// Illegal UTF16 char?
					Debug.Assert((c <= '\u{D7FF}') || (c >= '\u{E000}'));
#endif
					len++;
				}
				else
					len += 2;
			}
			len++; // null terminator
			return len;
		}

		public static int Encode(char32 c, Span<uint8> dest)
		{
			if (c <= '\u{FFFF}')
			{
				if (dest.Length >= 2)
					*((char16*)dest.Ptr) = (char16)c;
				return 2;
			}
			else
			{
				if (dest.Length >= 4)
				{
                    *((char16*)dest.Ptr) = (char16)((int32)c >> 10) + 0xD800;
					*((char16*)dest.Ptr + 1) = (char16)(((int32)c & 0x3FF) + 0xDC00);
				}
				return 4;
			}
		}

		public static Result<int, EncodeError> Encode(StringView str, char16* outUTF16Buf, int bufLen)
		{
			char16* buf = outUTF16Buf;
			int bufLeft = bufLen;

			void EncodeChar(char16 c)
			{
				if (buf != null)
					*(buf++) = (char16)c;
				if (--bufLeft == 0)
					buf = null;
			}

			for (var c in str.DecodedChars)
			{
				if (c <= '\u{FFFF}')
				{
#if BF_UTF_PEDANTIC
					// Illegal UTF16 char?
					Debug.Assert((c <= '\u{D7FF}') || (c >= '\u{E000}'));
#endif
					EncodeChar((char16)c);
				}
				else
				{
					int32 valLeft = (int32)c;
					EncodeChar((char16)(valLeft >> 10) + 0xD800);
					EncodeChar((char16)(valLeft & 0x3FF) + 0xDC00);
				}
			}
			EncodeChar(0);

			int encodedLen = bufLen - bufLeft;
			if (bufLeft < 0)
				return .Err(.Overflow(encodedLen));
			return .Ok(encodedLen);
		}

		public static int CStrLen(char16* str)
		{
			for (int i = 0; true; i++)
				if (str[i] == 0)
					return i;
		}
	}
}
