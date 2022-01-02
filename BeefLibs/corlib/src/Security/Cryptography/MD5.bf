using System.IO;

namespace System.Security.Cryptography
{
	struct HashEncode
	{
		// Only 63 chars - skip zero
		const char8[?] cHash64bToChar = .( 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
		'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
		'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
		'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '_' );
		public static void HashEncode64(uint64 val, String outStr)
		{
			var val;
			if ((int64)val < 0)
			{
				uint64 flippedNum = (uint64)-(int64)val;
				// Only flip if the encoded result would actually be shorter
				if (flippedNum <= 0x00FFFFFFFFFFFFFFL)
				{
					val = flippedNum;
					outStr.Append('_');
				}
			}

			for (int i = 0; i < 10; i++)
			{
				int charIdx = (int)((val >> (i * 6)) & 0x3F) - 1;
				if (charIdx != -1)
					outStr.Append(cHash64bToChar[charIdx]);
			}
		}
	}

	struct MD5Hash
	{
		public uint8[16] mHash;

		public static Result<MD5Hash> Parse(StringView str)
		{
			if (str.Length != 32)
				return .Err;

			MD5Hash hash = ?;

			Result<uint8> ParseChar(char8 c)
			{
				if ((c >= '0') && (c <= '9'))
					return (uint8)(c - '0');
				if ((c >= 'A') && (c <= 'F'))
					return (uint8)(c - 'A' + 10);
				if ((c >= 'a') && (c <= 'f'))
					return (uint8)(c - 'a' + 10);
				return .Err;
			}

			for (int i < 16)
			{
				hash.mHash[i] =
                    (Try!(ParseChar(str[i * 2 + 0])) << 4) |
					(Try!(ParseChar(str[i * 2 + 1])));
			}

			return hash;
		}

		public bool IsZero
		{
			get
			{
				for (int i < 16)
					if (mHash[i] != 0)
						return false;
				return true;
			}
		}

		public override void ToString(String strBuffer)
		{
			for (let val in mHash)
			{
				val.ToString(strBuffer, "X2", null);
			}
		}

		public void Encode(String outStr)
		{
#unwarn
			HashEncode.HashEncode64(((uint64*)&mHash)[0], outStr);
#unwarn
			HashEncode.HashEncode64(((uint64*)&mHash)[1], outStr);
		}
	}

	class MD5
	{
		uint32 lo, hi;
		uint32 a, b, c, d;
		uint8[64] buffer;
		uint32[16] block;

		public this()
		{
			a = 0x67452301;
			b = 0xefcdab89;
			c = 0x98badcfe;
			d = 0x10325476;

			lo = 0;
			hi = 0;
		}

		uint8* Body(Span<uint8> data)
		{
			// The basic MD5 functions.

			uint8* ptr = data.Ptr;
			int size = data.Length;

			// F and G are optimized compared to their RFC 1321 definitions for
			// architectures that lack an AND-NOT instruction, just like in Colin Plumb's
			// implementation.

			mixin F(var x, var y, var z) { ((z) ^ ((x) & ((y) ^ (z)))) }
			mixin G(var x, var y, var z) { ((y) ^ ((z) & ((x) ^ (y)))) }
			mixin H(var x, var y, var z) { ((x) ^ (y) ^ (z)) }
			mixin I(var x, var y, var z) { ((y) ^ ((x) | ~(z))) }

			// The MD5 transformation for all four rounds.
			mixin STEP_F(var a, var b, var c, var d, var x, var t, var s)
			{
				a += F!(b, c, d) + x + t;
				a = ((a << s) | ((a & 0xffffffff) >> (32 - s)));
				a += b;
			}
			mixin STEP_G(var a, var b, var c, var d, var x, var t, var s)
			{
				a += G!(b, c, d) + x + t;
				a = ((a << s) | ((a & 0xffffffff) >> (32 - s)));
				a += b;
			}
			mixin STEP_H(var a, var b, var c, var d, var x, var t, var s)
			{
				a += H!(b, c, d) + x + t;
				a = ((a << s) | ((a & 0xffffffff) >> (32 - s)));
				a += b;
			}
			mixin STEP_I(var a, var b, var c, var d, var x, var t, var s)
			{
				a += I!(b, c, d) + x + t;
				a = ((a << s) | ((a & 0xffffffff) >> (32 - s)));
				a += b;
			}

			// SET reads 4 input bytes in little-endian byte order and stores them
			// in a properly aligned word in host byte order.
			mixin SET(var n)
			{
                block[n] =
					(uint32)ptr[n * 4] |
                    ((uint32)ptr[n * 4 + 1] << 8) |
					((uint32)ptr[n * 4 + 2] << 16) |
					((uint32)ptr[n * 4 + 3] << 24)
			}

			mixin GET(var n) { block[n] }

			a = this.a;
			b = this.b;
			c = this.c;
			d = this.d;

			repeat
            {
				let saved_a = a;
				let saved_b = b;
				let saved_c = c;
				let saved_d = d;
				
				// Round 1
				STEP_F!(a, b, c, d, SET!(0), 0xd76aa478, 7);
				STEP_F!(d, a, b, c, SET!(1), 0xe8c7b756, 12);
				STEP_F!(c, d, a, b, SET!(2), 0x242070db, 17);
				STEP_F!(b, c, d, a, SET!(3), 0xc1bdceee, 22);
				STEP_F!(a, b, c, d, SET!(4), 0xf57c0faf, 7);
				STEP_F!(d, a, b, c, SET!(5), 0x4787c62a, 12);
				STEP_F!(c, d, a, b, SET!(6), 0xa8304613, 17);
				STEP_F!(b, c, d, a, SET!(7), 0xfd469501, 22);
				STEP_F!(a, b, c, d, SET!(8), 0x698098d8, 7);
				STEP_F!(d, a, b, c, SET!(9), 0x8b44f7af, 12);
				STEP_F!(c, d, a, b, SET!(10), 0xffff5bb1, 17);
				STEP_F!(b, c, d, a, SET!(11), 0x895cd7be, 22);
				STEP_F!(a, b, c, d, SET!(12), 0x6b901122, 7);
				STEP_F!(d, a, b, c, SET!(13), 0xfd987193, 12);
				STEP_F!(c, d, a, b, SET!(14), 0xa679438e, 17);
				STEP_F!(b, c, d, a, SET!(15), 0x49b40821, 22);
				
				// Round 2
				STEP_G!(a, b, c, d, GET!(1), 0xf61e2562, 5);
				STEP_G!(d, a, b, c, GET!(6), 0xc040b340, 9);
				STEP_G!(c, d, a, b, GET!(11), 0x265e5a51, 14);
				STEP_G!(b, c, d, a, GET!(0), 0xe9b6c7aa, 20);
				STEP_G!(a, b, c, d, GET!(5), 0xd62f105d, 5);
				STEP_G!(d, a, b, c, GET!(10), 0x02441453, 9);
				STEP_G!(c, d, a, b, GET!(15), 0xd8a1e681, 14);
				STEP_G!(b, c, d, a, GET!(4), 0xe7d3fbc8, 20);
				STEP_G!(a, b, c, d, GET!(9), 0x21e1cde6, 5);
				STEP_G!(d, a, b, c, GET!(14), 0xc33707d6, 9);
				STEP_G!(c, d, a, b, GET!(3), 0xf4d50d87, 14);
				STEP_G!(b, c, d, a, GET!(8), 0x455a14ed, 20);
				STEP_G!(a, b, c, d, GET!(13), 0xa9e3e905, 5);
				STEP_G!(d, a, b, c, GET!(2), 0xfcefa3f8, 9);
				STEP_G!(c, d, a, b, GET!(7), 0x676f02d9, 14);
				STEP_G!(b, c, d, a, GET!(12), 0x8d2a4c8a, 20);
				
				// Round 3
				STEP_H!( a, b, c, d, GET!(5), 0xfffa3942, 4);
				STEP_H!( d, a, b, c, GET!(8), 0x8771f681, 11);
				STEP_H!( c, d, a, b, GET!(11), 0x6d9d6122, 16);
				STEP_H!( b, c, d, a, GET!(14), 0xfde5380c, 23);
				STEP_H!( a, b, c, d, GET!(1), 0xa4beea44, 4);
				STEP_H!( d, a, b, c, GET!(4), 0x4bdecfa9, 11);
				STEP_H!( c, d, a, b, GET!(7), 0xf6bb4b60, 16);
				STEP_H!( b, c, d, a, GET!(10), 0xbebfbc70, 23);
				STEP_H!( a, b, c, d, GET!(13), 0x289b7ec6, 4);
				STEP_H!( d, a, b, c, GET!(0), 0xeaa127fa, 11);
				STEP_H!( c, d, a, b, GET!(3), 0xd4ef3085, 16);
				STEP_H!( b, c, d, a, GET!(6), 0x04881d05, 23);
				STEP_H!( a, b, c, d, GET!(9), 0xd9d4d039, 4);
				STEP_H!( d, a, b, c, GET!(12), 0xe6db99e5, 11);
				STEP_H!( c, d, a, b, GET!(15), 0x1fa27cf8, 16);
				STEP_H!( b, c, d, a, GET!(2), 0xc4ac5665, 23);
				
				// Round 4
				STEP_I!(a, b, c, d, GET!(0), 0xf4292244, 6);
				STEP_I!(d, a, b, c, GET!(7), 0x432aff97, 10);
				STEP_I!(c, d, a, b, GET!(14), 0xab9423a7, 15);
				STEP_I!(b, c, d, a, GET!(5), 0xfc93a039, 21);
				STEP_I!(a, b, c, d, GET!(12), 0x655b59c3, 6);
				STEP_I!(d, a, b, c, GET!(3), 0x8f0ccc92, 10);
				STEP_I!(c, d, a, b, GET!(10), 0xffeff47d, 15);
				STEP_I!(b, c, d, a, GET!(1), 0x85845dd1, 21);
				STEP_I!(a, b, c, d, GET!(8), 0x6fa87e4f, 6);
				STEP_I!(d, a, b, c, GET!(15), 0xfe2ce6e0, 10);
				STEP_I!(c, d, a, b, GET!(6), 0xa3014314, 15);
				STEP_I!(b, c, d, a, GET!(13), 0x4e0811a1, 21);
				STEP_I!(a, b, c, d, GET!(4), 0xf7537e82, 6);
				STEP_I!(d, a, b, c, GET!(11), 0xbd3af235, 10);
				STEP_I!(c, d, a, b, GET!(2), 0x2ad7d2bb, 15);
				STEP_I!(b, c, d, a, GET!(9), 0xeb86d391, 21);
				
				a += saved_a;
				b += saved_b;
				c += saved_c;
				d += saved_d;
				
				ptr += 64;
			}
            while ((size -= 64) > 0);

			this.a = a;
			this.b = b;
			this.c = c;
			this.d = d;

			return ptr;
		}

		public void Update(Span<uint8> span)
		{
			uint32 saved_lo;
			uint32 used, free;
			uint8* ptr = span.Ptr;
			uint32 size = (uint32)span.Length;
			
			saved_lo = lo;
			if ((lo = (saved_lo + size) & 0x1fffffff) < saved_lo)
				hi++;
			hi += size >> 29;
			
			used = saved_lo & 0x3f;
			
			if (used != 0)
            {
				free = 64 - used;
				if (size < free)
	            {
					Internal.MemCpy(&buffer[used], ptr, (.)size);
					return;
				}
				
				Internal.MemCpy(&buffer[used], ptr, (.)free);
				ptr = ptr + free;
				size -= free;
				Body(Span<uint8>(&buffer[0], 64));
			}
			
			if (size >= 64)
            {
				ptr = Body(Span<uint8>(ptr, (.)(size & ~(uint32)0x3f)));
				size &= 0x3f;
			}
			
			Internal.MemCpy(&buffer[0], ptr, (.)size);
		}

		public MD5Hash Finish()
		{
			uint32 used, free;

			used = lo & 0x3f;

			buffer[used++] = 0x80;

			free = 64 - used;

			if (free < 8)
            {
				if (free > 0)
					Internal.MemSet(&buffer[used], 0, (.)free);
				Body(Span<uint8>(&buffer[0], 64));
				used = 0;
				free = 64;
			}

			Internal.MemSet(&buffer[used], 0, (.)free - 8);

			lo <<= 3;
			LittleEndian.Write(&buffer[56], lo);
			LittleEndian.Write(&buffer[60], hi);

			Body(Span<uint8>(&buffer[0], 64));

			MD5Hash hash = ?;
			LittleEndian.Write(&hash.mHash[0], a);
			LittleEndian.Write(&hash.mHash[4], b);
			LittleEndian.Write(&hash.mHash[8], c);
			LittleEndian.Write(&hash.mHash[12], d);
			return hash;
		}

		public static MD5Hash Hash(Span<uint8> data)
		{
			let md5 = scope MD5();
			md5.Update(data);
			return md5.Finish();
		}
		
		public static Result<MD5Hash> Hash(Stream stream)
		{
			let md5 = scope MD5();

			loop: while (true)
			{
				uint8[4096] buffer;
				switch (stream.TryRead(.(&buffer, 4096)))
				{
				case .Ok(let bytes):
					if (bytes == 0)
						break loop;
					md5.Update(.(&buffer, bytes));
				case .Err(let err):
					return .Err(err);
				}
			}

			return md5.Finish();
		}
	}
}
