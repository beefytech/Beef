using System;
using System.Collections;

namespace utils
{
	class Compression
	{
		[CallingConvention(.Stdcall), CLink]
		extern static bool Compression_Compress(void* ptr, int size, void** outPtr, int* outSize);

		[CallingConvention(.Stdcall), CLink]
		extern static bool Compression_Decompress(void* ptr, int size, void** outPtr, int* outSize);

		public static Result<void> Compress(Span<uint8> inData, List<uint8> outData)
		{
			void* outPtr = null;
			int outSize = 0;
			if (!Compression_Compress(inData.Ptr, inData.Length, &outPtr, &outSize))
				return .Err;
			outData.AddRange(.((.)outPtr, outSize));
			return .Ok;
		}

		public static Result<void> Compress(Span<uint8> inData, String outData)
		{
			void* outPtr = null;
			int outSize = 0;
			if (!Compression_Compress(inData.Ptr, inData.Length, &outPtr, &outSize))
				return .Err;
			outData.Insert(outData.Length, StringView((.)outPtr, outSize));
			return .Ok;
		}

		public static Result<void> Decompress(Span<uint8> inData, List<uint8> outData)
		{
			void* outPtr = null;
			int outSize = 0;
			if (!Compression_Decompress(inData.Ptr, inData.Length, &outPtr, &outSize))
				return .Err;
			outData.AddRange(.((.)outPtr, outSize));
			return .Ok;
		}

		public static Result<void> Decompress(Span<uint8> inData, String outData)
		{
			void* outPtr = null;
			int outSize = 0;
			if (!Compression_Decompress(inData.Ptr, inData.Length, &outPtr, &outSize))
				return .Err;
			outData.Insert(outData.Length, StringView((.)outPtr, outSize));
			return .Ok;
		}
	}
}
