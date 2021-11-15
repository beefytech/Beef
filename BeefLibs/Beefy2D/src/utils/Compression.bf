using System;
using System.Collections;

namespace utils
{
	class Compression
	{
		[CallingConvention(.Stdcall), CLink]
		extern static Span<uint8> Compression_Compress(void* ptr, int size);

		[CallingConvention(.Stdcall), CLink]
		extern static Span<uint8> Compression_Decompress(void* ptr, int size);

		[CallingConvention(.Stdcall), CLink]
		extern static void Compression_Free(void* ptr);

		public static Result<void> Compress(Span<uint8> inData, List<uint8> outData)
		{
			var outSpan = Compression_Compress(inData.Ptr, inData.Length);
			if ((outSpan.Length == 0) && (inData.Length != 0))
				return .Err;
			outData.AddRange(outSpan);
			Compression_Free(outSpan.Ptr);
			return .Ok;
		}

		public static Result<void> Compress(Span<uint8> inData, String outData)
		{
			var outSpan = Compression_Compress(inData.Ptr, inData.Length);
			if ((outSpan.Length == 0) && (inData.Length != 0))
				return .Err;
			outData.Insert(outData.Length, StringView((.)outSpan.Ptr, outSpan.Length));
			Compression_Free(outSpan.Ptr);
			return .Ok;
		}

		public static Result<void> Decompress(Span<uint8> inData, List<uint8> outData)
		{
			var outSpan = Compression_Decompress(inData.Ptr, inData.Length);
			if ((outSpan.Length == 0) && (inData.Length != 0))
				return .Err;
			outData.AddRange(outSpan);
			Compression_Free(outSpan.Ptr);
			return .Ok;
		}

		public static Result<void> Decompress(Span<uint8> inData, String outData)
		{
			var outSpan = Compression_Decompress(inData.Ptr, inData.Length);
			if ((outSpan.Length == 0) && (inData.Length != 0))
				return .Err;
			outData.Insert(outData.Length, StringView((.)outSpan.Ptr, outSpan.Length));
			Compression_Free(outSpan.Ptr);
			return .Ok;
		}
	}
}
