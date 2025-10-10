using System;
using System.Diagnostics.Contracts;
using System.Security;
using System.Diagnostics;

namespace System
{
    public static class BitConverter
    {
#if BIGENDIAN
        public static readonly bool IsLittleEndian /* = false */;
#else
        public static readonly bool IsLittleEndian = true;
#endif

		public static TTo Convert<TFrom, TTo>(TFrom from)
		{
			Debug.Assert(sizeof(TFrom) == sizeof(TTo));
			var from;
			return *(TTo*)(&from);
		}


		public static uint16 ByteSwap(uint16 val) =>
			(((val & 0x00FF) << 8) |
			((val & 0xFF00) >> 8));
		public static int16 ByteSwap(int16 val) =>
			(.)(((val & 0x00FF) << 8) |
			((val & 0xFF00) >> 8));

		public static uint32 ByteSwap(uint32 val) =>
			((val & 0x000000FF) << 24) | ((val & 0x0000FF00) <<  8) |
			((val & 0x00FF0000) >>  8) | ((val & 0xFF000000) >> 24);
		public static int32 ByteSwap(int32 val) =>
			(.)((val & 0x000000FF) << 24) | (.)((val & 0x0000FF00) <<  8) |
			(.)((val & 0x00FF0000) >>  8) | (.)((val & 0xFF000000) >> 24);

		public static uint64 ByteSwap(uint64 val) =>
			((val & 0x00000000000000FF) << 56) |
			((val & 0x000000000000FF00) << 40) |
			((val & 0x0000000000FF0000) << 24) |
			((val & 0x00000000FF000000) << 8)  |
			((val & 0x000000FF00000000) >> 8)  |
			((val & 0x0000FF0000000000) >> 24) |
			((val & 0x00FF000000000000) >> 40) |
			((val & 0xFF00000000000000) >> 56);
		public static int64 ByteSwap(int64 val) =>
			((val & 0x00000000000000FF) << 56) |
			((val & 0x000000000000FF00) << 40) |
			((val & 0x0000000000FF0000) << 24) |
			((val & 0x00000000FF000000) << 8)  |
			((val & 0x000000FF00000000) >> 8)  |
			((val & 0x0000FF0000000000) >> 24) |
			((val & 0x00FF000000000000) >> 40) |
			((val & (int64)0xFF00000000000000) >> 56);

#if BIGENDIAN
		public static uint16 FromBigEndian(uint16 val) => val;
		public static int16 FromBigEndian(int16 val) => val;

		public static uint32 FromBigEndian(uint32 val) => val;
		public static int32 FromBigEndian(int32 val) => val;

		public static uint64 FromBigEndian(uint64 val) => val;
		public static int64 FromBigEndian(int64 val) => val;

		public static uint16 FromLittleEndian(uint16 val) => ByteSwap(val);
		public static int16 FromLittleEndian(int16 val) => ByteSwap(val);

		public static uint32 FromLittleEndian(uint32 val) => ByteSwap(val);
		public static int32 FromLittleEndian(int32 val) => ByteSwap(val);

		public static uint64 FromLittleEndian(uint64 val) => ByteSwap(val);
		public static int64 FromLittleEndian(int64 val) => ByteSwap(val);
#else
		public static uint16 FromLittleEndian(uint16 val) => val;
		public static int16 FromLittleEndian(int16 val) => val;

		public static uint32 FromLittleEndian(uint32 val) => val;
		public static int32 FromLittleEndian(int32 val) => val;

		public static uint64 FromLittleEndian(uint64 val) => val;
		public static int64 FromLittleEndian(int64 val) => val;

		public static uint16 FromBigEndian(uint16 val) => ByteSwap(val);
		public static int16 FromBigEndian(int16 val) => ByteSwap(val);

		public static uint32 FromBigEndian(uint32 val) => ByteSwap(val);
		public static int32 FromBigEndian(int32 val) => ByteSwap(val);
			
		public static uint64 FromBigEndian(uint64 val) => ByteSwap(val);
		public static int64 FromBigEndian(int64 val) => ByteSwap(val);
#endif

		public static Result<uint8> FromHex(char8 c)
		{
			if ((c >= '0') && (c <= '9'))
				return (.)(c - '0');
			if ((c >= 'a') && (c <= 'f'))
				return (.)(c - 'a') + 0xA;
			if ((c >= 'A') && (c <= 'F'))
				return (.)(c - 'A') + 0xA;
			return .Err;
		}

		public static uint32 FromFourCC(String str)
		{
			Runtime.Assert(str.Length == 4);
			return ((uint32)str[0] << 24) | ((uint32)str[1] << 16) | ((uint32)str[2] << 8) | ((uint32)str[3]);
		}
		public static uint32 FromFourCCLE(String str) => FromFourCC(str);
		public static uint32 FromFourCCBE(String str) => ByteSwap(FromFourCC(str));
    }
}
