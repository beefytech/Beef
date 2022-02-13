using System.Diagnostics;
namespace System.Text
{
	public static class UTF32
	{
		public enum EncodeError
		{
			case Overflow(int len);
		}

		public static void Decode(char32* utf32Str, String outStr)
		{
			int utf8Len = GetLengthAsUTF8(utf32Str);
			outStr.Reserve(outStr.Length + utf8Len);

			char32* utf32Ptr = utf32Str;
			
			while (true)
			{
				char32 c32 = *(utf32Ptr++);
				if (c32 == 0)
					break;
				outStr.Append(c32);
			}
		}

		public static void Decode(Span<char32> utf32Str, String outStr)
		{
			int utf8Len = GetLengthAsUTF8(utf32Str);
			outStr.Reserve(outStr.Length + utf8Len);

			char32* utf32Ptr = utf32Str.Ptr;
			char32* utf32End = utf32Str.EndPtr;
			while (utf32Ptr < utf32End)
			{
				char32 c32 = *(utf32Ptr++);
				outStr.Append(c32);
			}
		}

		public static (char32 c, int8 cSize) Decode(char32* buf, int lenLeft = 0)
		{
			char32 c = buf[0];
			return (c, 1);
		}

		public static int GetLengthAsUTF8(char32* utf32Str)
		{
			int utf8len = 0;
			char32* utf32Ptr = utf32Str;
			while (true)
			{
				char32 c32 = *(utf32Ptr++);
				if (c32 == 0)
					return utf8len;
				utf8len += UTF8.GetEncodedLength(c32);
			}
		}

		public static int GetLengthAsUTF8(Span<char32> utf32Str)
		{
			int utf8len = 0;
			char32* c16Ptr = utf32Str.Ptr;
			int lenLeft = utf32Str.Length;
			while (lenLeft > 0)
			{
				let (c, encLen) = Decode(c16Ptr, lenLeft);
				c16Ptr += encLen;
				lenLeft -= encLen;
				utf8len += UTF8.GetEncodedLength(c);
			}
			return utf8len;
		}

		public static bool Equals(char32* utf32Str, String str)
		{
			int strIdx = 0;
			char32* c16Ptr = utf32Str;
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
			return utf8Len;
		}

		public static int GetEncodedLength(char32 c)
		{
			return 1;
		}

		public static int GetEncodedLen(StringView str)
		{
			int len = 0;
			for (var c in str.DecodedChars)
			{
				len++;
			}
			len++; // null terminator
			return len;
		}

		public static int Encode(char32 c, Span<uint8> dest)
		{
			if (dest.Length >= 2)
				*((char32*)dest.Ptr) = (char32)c;
			return 2;
		}

		public static Result<int, EncodeError> Encode(StringView str, char32* oututf32Buf, int bufLen)
		{
			char32* buf = oututf32Buf;
			int bufLeft = bufLen;

			void EncodeChar(char32 c)
			{
				if (buf != null)
					*(buf++) = (char32)c;
				if (--bufLeft == 0)
					buf = null;
			}

			for (var c in str.DecodedChars)
			{

				EncodeChar((char32)c);
			}
			EncodeChar(0);

			int encodedLen = bufLen - bufLeft;
			if (bufLeft < 0)
				return .Err(.Overflow(encodedLen));
			return .Ok(encodedLen);
		}

		public static int CStrLen(char32* str)
		{
			for (int i = 0; true; i++)
				if (str[i] == 0)
					return i;
		}
	}
}
