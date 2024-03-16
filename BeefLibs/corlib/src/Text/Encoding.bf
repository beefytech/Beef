using System.Diagnostics;
using System.Threading;
namespace System.Text
{
	[StaticInitPriority(100)]
	abstract class Encoding
	{
		public enum DecodeError
		{
			case PartialDecode(int decodedBytes, int outChars);
			case FormatError;
		}

		public enum EncodeError
		{
			case PartialEncode(int inChars, int encodedBytes);
		}

		static Encoding sASCII ~ delete _;
		static Encoding sUTF8 ~ delete _;
		static Encoding sUTF8WithBOM ~ delete _;
		static Encoding sUTF16 ~ delete _;
		static Encoding sUTF16WithBOM ~ delete _;

		static T GetEncoding<T>(ref Encoding encoding) where T : Encoding, new, delete
		{
			if (encoding != null)
				return (.)encoding;

			var newEncoding = new T();
			if (Compiler.IsComptime)
			{
				encoding = newEncoding;
				return newEncoding;
			}

			let prevValue = Interlocked.CompareExchange(ref encoding, null, newEncoding);
			if (prevValue != null)
			{
				// This was already set - race condition
				delete newEncoding;
				return (.)prevValue;
			}
			return newEncoding;
		}

		public static ASCIIEncoding ASCII => GetEncoding<ASCIIEncoding>(ref sASCII);
		public static UTF8Encoding UTF8 => GetEncoding<UTF8Encoding>(ref sUTF8);
		public static UTF8EncodingWithBOM UTF8WithBOM => GetEncoding<UTF8EncodingWithBOM>(ref sUTF8WithBOM);
		public static UTF16Encoding UTF16 => GetEncoding<UTF16Encoding>(ref sUTF16);
		public static UTF16EncodingWithBOM UTF16WithBOM => GetEncoding<UTF16EncodingWithBOM>(ref sUTF16WithBOM);

		public abstract int GetCharUnitSize();
		public abstract int GetEncodedLength(char32 c);
		public abstract int Encode(char32 c, Span<uint8> dest);
		public abstract int GetMaxCharCount(int size);

		public virtual int GetEncodedSize(StringView str)
		{
			int len = 0;
			for (char32 c in str.DecodedChars)
				len += GetEncodedLength(c);
			return len * GetCharUnitSize();
		}

		public virtual Result<int, EncodeError> Encode(StringView str, Span<uint8> dest)
		{
			uint8* destPtr = dest.Ptr;
			int sizeLeft = dest.Length;

			for (char32 c in str.DecodedChars)
			{
				int encSize = Encode(c, .(destPtr, sizeLeft));
				if (encSize > sizeLeft)
				{
					return .Err(.PartialEncode(@c.NextIndex, dest.Length - sizeLeft));
				}

				destPtr += encSize;
				sizeLeft -= encSize;
			}

			return dest.Length - sizeLeft;
		}

		/// Returns number of UTF8 characters required to hold the decoded result
		public abstract int GetDecodedUTF8Size(Span<uint8> bytes);

		/// Decodes from bytes to UTF8
		public abstract Result<int, DecodeError> DecodeToUTF8(Span<uint8> inBytes, StringView outChars);

		/// Decodes from bytes to UTF8
		public virtual Result<int, DecodeError> DecodeToUTF8(Span<uint8> inBytes, String outStr)
		{
			int utf8Len = GetDecodedUTF8Size(inBytes);

			int prevSize = outStr.Length;
			switch (DecodeToUTF8(inBytes, StringView(outStr.PrepareBuffer(utf8Len), utf8Len)))
			{
			case .Ok(let val):
				 return .Ok(val);
			case .Err(let err):
				switch (err)
				{
				case .PartialDecode(let decodedBytes, let outChars):
					outStr.[Friend]mLength = (.)(prevSize + outChars);
				case .FormatError:
				}
				return .Err(err);
			}
		}

		public static Encoding DetectEncoding(Span<uint8> data, out int bomSize)
		{
			bomSize = 0;
			if (data.Length < 2)
				return ASCII;

			if ((data[0] == 0xFE) && (data[1] == 0xFF))
			{
				// Big endian UTF16
				//bomSize = 2;
				return ASCII;
			}
			else if ((data[0] == 0xFF) && (data[1] == 0xFE))
			{
				// Little endian UTF16
				bomSize = 2;
				return UTF16WithBOM;
			}

			if (data.Length < 3)
				return ASCII;

			if ((data[0] == 0xEF) && (data[1] == 0xBB) && (data[2] == 0xBF))
			{
				// Big endian unicode
				bomSize = 3;
				return UTF8WithBOM;
			}

			return ASCII;
		}
	}

	class ASCIIEncoding : Encoding
	{
		public override int GetMaxCharCount(int size)
		{
			return size;
		}

		public override int GetCharUnitSize()
		{
			return 1;
		}

		public override int GetEncodedLength(char32 c)
		{
			return 1;
		}

		public override int Encode(char32 c, Span<uint8> dest)
		{
			dest[0] = (uint8)c;
			return 1;
		}

		public override Result<int, EncodeError> Encode(StringView str, Span<uint8> dest)
		{
			// Strings are by definition UTF8 so we can just memcpy
			//  Technically this gives us different results than individually encoding char32s
			//  but truncation will always be wrong for chars over 0x7F whereas UTF8 encoding will
			//  sometimes be right. We are really just opting for the fastest method at the time.

			if (dest.Length < str.Length)
			{
				Internal.MemCpy(dest.Ptr, str.Ptr, dest.Length);
				return .Err(.PartialEncode(dest.Length, dest.Length));
			}

			Internal.MemCpy(dest.Ptr, str.Ptr, str.Length);
			return str.Length;
		}

		public override int GetDecodedUTF8Size(Span<uint8> bytes)
		{
			return bytes.Length;
		}

		public override Result<int, DecodeError> DecodeToUTF8(Span<uint8> inBytes, StringView outChars)
		{
			if (outChars.Length < inBytes.Length)
			{
				Internal.MemCpy(outChars.Ptr, inBytes.Ptr, outChars.Length);
				return .Err(.PartialDecode(outChars.Length, outChars.Length));
			}
			Internal.MemCpy(outChars.Ptr, inBytes.Ptr, inBytes.Length);
			return .Ok(inBytes.Length);
		}
	}

	class UTF8Encoding : Encoding
	{
		public override int GetMaxCharCount(int size)
		{
			return size;
		}

		public override int GetCharUnitSize()
		{
			return 1;
		}

		public override int GetEncodedLength(char32 c)
		{
			return Text.UTF8.GetEncodedLength(c);
		}

		public override int Encode(char32 c, Span<uint8> dest)
		{
			return Text.UTF8.Encode(c, .((char8*)dest.Ptr, dest.Length));
		}

		public override Result<int, EncodeError> Encode(StringView str, Span<uint8> dest)
		{
			// Strings are by definition UTF8 so we can just memcpy.
			if (dest.Length < str.Length)
			{
				Internal.MemCpy(dest.Ptr, str.Ptr, dest.Length);
				return .Err(.PartialEncode(dest.Length, dest.Length));
			}

			Internal.MemCpy(dest.Ptr, str.Ptr, str.Length);
			return str.Length;
		}

		public override int GetDecodedUTF8Size(Span<uint8> bytes)
		{
			return bytes.Length;
		}

		public override Result<int, DecodeError> DecodeToUTF8(Span<uint8> inBytes, StringView outChars)
		{
			if (outChars.Length < inBytes.Length)
			{
				Internal.MemCpy(outChars.Ptr, inBytes.Ptr, outChars.Length);
				return .Err(.PartialDecode(outChars.Length, outChars.Length));
			}
			Internal.MemCpy(outChars.Ptr, inBytes.Ptr, inBytes.Length);
			return .Ok(inBytes.Length);
		}
	}

	class UTF8EncodingWithBOM : UTF8Encoding
	{
		public override int GetEncodedSize(StringView str)
		{
			return 3 + base.GetEncodedSize(str);
		}

		public override Result<int, EncodeError> Encode(StringView str, Span<uint8> dest)
		{
			uint8* destPtr = dest.Ptr;
			if (dest.Length < 3)
			{
				return .Err(.PartialEncode(0, 0));
			}

			if (dest.Length >= 3)
			{
				*(destPtr++) = 0xEF;
				*(destPtr++) = 0xBB;
				*(destPtr++) = 0xBF;
			}

			switch (base.Encode(str, .(dest.Ptr, dest.Length - 3)))
			{
			case .Ok(let encSize):
				return .Ok(3 + encSize);
			case .Err(let err):
				switch (err)
				{
				case .PartialEncode(let inChars, let encodedBytes):
					return .Err(.PartialEncode(inChars, 3 + encodedBytes));
				}
			}
		}
	}

	class UTF16Encoding : Encoding
	{
		public override int GetMaxCharCount(int size)
		{
			return size / 2;
		}

		public override int GetCharUnitSize()
		{
			return 2;
		}

		public override int GetEncodedLength(char32 c)
		{
			return Text.UTF16.GetEncodedLength(c);
		}

		public override int Encode(char32 c, Span<uint8> dest)
		{
			return Text.UTF16.Encode(c, dest);
		}

		public override int GetDecodedUTF8Size(Span<uint8> bytes)
		{
			return Text.UTF16.GetLengthAsUTF8(Span<char16>((.)bytes.Ptr, bytes.Length / 2));
		}

		public override Result<int, DecodeError> DecodeToUTF8(Span<uint8> inBytes, StringView outChars)
		{
			char16* cPtr = (char16*)inBytes.Ptr;
			int bytesLeft = inBytes.Length;
			char8* outPtr = outChars.Ptr;
			int outLeft = outChars.Length;

			while (bytesLeft >= 2)
			{
				int charsLeft = bytesLeft / 2;
				let (c, len) = Text.UTF16.Decode(cPtr, charsLeft);
				if ((len == 2) && (charsLeft == 1))
				{
					// Failed to decode
					break;
				}
				cPtr += len;

				// Simple case
				if (c < '\x80')
				{
					*outPtr = (.)c;
					outPtr++;
					outLeft--;
					bytesLeft -= len * 2;
					continue;
				}

				int cOutLen = Text.UTF8.Encode(c, .(outPtr, outLeft));
				if (cOutLen > outLeft)
					break;

				outPtr += cOutLen;
				outLeft -= cOutLen;
				bytesLeft -= len * 2;
			}

			if (bytesLeft == 0)
				return .Ok(outChars.Length - outLeft);

			Debug.Assert(outLeft >= 0);
			return .Err(.PartialDecode(inBytes.Length - bytesLeft, outChars.Length - outLeft));
		}
	}

	class UTF16EncodingWithBOM : UTF16Encoding
	{
		public override int GetEncodedSize(StringView str)
		{
			return 2 + base.GetEncodedSize(str);
		}

		public override Result<int, EncodeError> Encode(StringView str, Span<uint8> dest)
		{
			uint8* destPtr = dest.Ptr;
			if (dest.Length >= 2)
			{
				*(destPtr++) = 0xFF;
				*(destPtr++) = 0xFE;
			}

			switch (base.Encode(str, .(dest.Ptr, dest.Length - 2)))
			{
			case .Ok(let encSize):
				return .Ok(2 + encSize);
			case .Err(let err):
				switch (err)
				{
				case .PartialEncode(let inChars, let encodedBytes):
					return .Err(.PartialEncode(inChars, 3 + encodedBytes));
				}
			}
		}
	}

	class EncodedString
	{
		uint8* mData ~ delete _;
		int32 mSize;

		public uint8* Ptr
		{
			get
            {
                return mData;
			}
		}

		public int Size
		{
			get
			{
				return mSize;
			}
		}

		public this(StringView str, Encoding encoding)
		{
			mSize = (int32)encoding.GetEncodedSize(str);
			mData = new uint8[mSize]*;
			encoding.Encode(str, .(mData, mSize));
		}
	}
}
