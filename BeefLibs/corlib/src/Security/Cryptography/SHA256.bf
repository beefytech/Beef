using System.IO;

namespace System.Security.Cryptography
{
	struct SHA256Hash
	{
		public uint8[32] mHash;

		public static Result<SHA256Hash> Parse(StringView str)
		{
			if (str.Length != 64)
				return .Err;

			SHA256Hash hash = ?;

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

			for (int i < 32)
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
				for (int i < 32)
					if (mHash[i] != 0)
						return false;
				return true;
			}
		}
	}

	class SHA256
	{
		int mDataLen;
		int mBitLength;
		uint32[8] mState;
		uint8[64] mData;

		mixin ROTLEFT(var a,var b)
		{
			(((a) << (b)) | ((a) >> (32-(b))))
		}
		mixin ROTRIGHT(var a, var b)
		{
			(((a) >> (b)) | ((a) << (32-(b))))
		}
		
		mixin CH(var x, var y, var z)
		{
			(((x) & (y)) ^ (~(x) & (z)))
		}

		mixin MAJ(var x, var y, var z)
		{
			(((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
		}
		mixin EP0(var x)
		{
			(ROTRIGHT!(x,2) ^ ROTRIGHT!(x,13) ^ ROTRIGHT!(x,22))
		}
		mixin EP1(var x)
		{
			(ROTRIGHT!(x,6) ^ ROTRIGHT!(x,11) ^ ROTRIGHT!(x,25))
		}
		mixin SIG0(var x)
		{
			(ROTRIGHT!(x,7) ^ ROTRIGHT!(x,18) ^ ((x) >> 3))
		}
		mixin SIG1(var x)
		{
			(ROTRIGHT!(x,17) ^ ROTRIGHT!(x,19) ^ ((x) >> 10))
		}

		const uint32[64] k = .(
			0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
			0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
			0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
			0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
			0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
			0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
			0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
			0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
		);

		public this()
		{
			mDataLen = 0;
			mBitLength = 0;
			mState[0] = 0x6a09e667;
			mState[1] = 0xbb67ae85;
			mState[2] = 0x3c6ef372;
			mState[3] = 0xa54ff53a;
			mState[4] = 0x510e527f;
			mState[5] = 0x9b05688c;
			mState[6] = 0x1f83d9ab;
			mState[7] = 0x5be0cd19;
			mData = .(?);
		}

		void Transform() mut
		{
			uint32 a, b, c, d, e, f, g, h, i, j, t1, t2;
			uint32[64] m = ?;

			for (i = 0, j = 0; i < 16; ++i, j += 4)
				m[i] = ((uint32)mData[j] << 24) | ((uint32)mData[j + 1] << 16) | ((uint32)mData[j + 2] << 8) | ((uint32)mData[j + 3]);
			for ( ; i < 64; ++i)
				m[i] = SIG1!(m[i - 2]) + m[i - 7] + SIG0!(m[i - 15]) + m[i - 16];

			a = mState[0];
			b = mState[1];
			c = mState[2];
			d = mState[3];
			e = mState[4];
			f = mState[5];
			g = mState[6];
			h = mState[7];

			for (i = 0; i < 64; ++i)
			{
				t1 = h + EP1!(e) + CH!(e,f,g) + k[i] + m[i];
				t2 = EP0!(a) + MAJ!(a,b,c);
				h = g;
				g = f;
				f = e;
				e = d + t1;
				d = c;
				c = b;
				b = a;
				a = t1 + t2;
			}

			mState[0] += a;
			mState[1] += b;
			mState[2] += c;
			mState[3] += d;
			mState[4] += e;
			mState[5] += f;
			mState[6] += g;
			mState[7] += h;
		}

		public void Update(Span<uint8> data) mut
		{
			for (int i = 0; i < data.Length; ++i)
			{
				mData[mDataLen] = data[i];
				mDataLen++;
				if (mDataLen == 64)
				{
					Transform();
					mBitLength += 512;
					mDataLen = 0;
				}
			}
		}

		public SHA256Hash Finish() mut
		{
			int i = mDataLen;
			
			// Pad whatever data is left in the buffer.
			if (mDataLen < 56)
			{
				mData[i++] = 0x80;
				while (i < 56)
					mData[i++] = 0x00;
			}
			else
			{
				mData[i++] = 0x80;
				while (i < 64)
					mData[i++] = 0x00;
				Transform();
				Internal.MemSet(&mData, 0, 56);
			}

			// Append to the padding the total message's length in bits and transform.
			mBitLength += mDataLen * 8;
			int64 bitLength = mBitLength;
			mData[63] = (.)bitLength;
			mData[62] = (.)(bitLength >> 8);
			mData[61] = (.)(bitLength >> 16);
			mData[60] = (.)(bitLength >> 24);
			mData[59] = (.)(bitLength >> 32);
			mData[58] = (.)(bitLength >> 40);
			mData[57] = (.)(bitLength >> 48);
			mData[56] = (.)(bitLength >> 56);
			Transform();

			SHA256Hash hash = ?;

			// Since this implementation uses little endian byte ordering and SHA uses big endian,
			// reverse all the bytes when copying the final state to the output hash.
			for (i = 0; i < 4; ++i)
			{
				hash.mHash[i]      = (.)(mState[0] >> (24 - i * 8));
				hash.mHash[i + 4]  = (.)(mState[1] >> (24 - i * 8));
				hash.mHash[i + 8]  = (.)(mState[2] >> (24 - i * 8));
				hash.mHash[i + 12] = (.)(mState[3] >> (24 - i * 8));
				hash.mHash[i + 16] = (.)(mState[4] >> (24 - i * 8));
				hash.mHash[i + 20] = (.)(mState[5] >> (24 - i * 8));
				hash.mHash[i + 24] = (.)(mState[6] >> (24 - i * 8));
				hash.mHash[i + 28] = (.)(mState[7] >> (24 - i * 8));
			}

			return hash;
		}

		public static SHA256Hash Hash(Span<uint8> data)
		{
			let sha256 = scope SHA256();
			sha256.Update(data);
			return sha256.Finish();
		}
		
		public static Result<SHA256Hash> Hash(Stream stream)
		{
			let sha256 = scope SHA256();

			loop: while (true)
			{
				uint8[4096] buffer;
				switch (stream.TryRead(.(&buffer, 4096)))
				{
				case .Ok(let bytes):
					if (bytes == 0)
						break loop;
					sha256.Update(.(&buffer, bytes));
				case .Err(let err):
					return .Err(err);
				}
			}

			return sha256.Finish();
		}
	}
}
