/*
miniz - public domain deflate/inflate, zlib-subset, ZIP reading/writing/appending, PNG writing
Rich Geldreich <richgel99@gmail.com>, last updated Oct. 13, 2013
Implements RFC 1950: http://www.ietf.org/rfc/rfc1950.txt and RFC 1951: http://www.ietf.org/rfc/rfc1951.txt
*/

/**************************************************************************
*
* Copyright 2013-2014 RAD Game Tools and Valve Software
* Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
* Copyright 2016 Martin Raiber
* All Rights Reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
* THE SOFTWARE.
*
**************************************************************************/

using System;
using System.IO;
using System.Diagnostics;

#define TINFL_USE_64BIT_BITBUF
#define MINIZ_HAS_64BIT_REGISTERS
#define MINIZ_LITTLE_ENDIAN
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES

namespace IDE.Util
{
	class ZipFile
	{
		public class Entry
		{
			ZipFile mZipFile;
			MiniZ.ZipArchiveFileStat mFileStat;
			int32 mFileIdx;

			public bool IsDirectory
			{
				get
				{
					return MiniZ.ZipReaderIsFileADirectory(&mZipFile.[Friend]mFile, mFileIdx);
				}
			}

			public Result<void> ExtractToFile(StringView filePath)
			{
				if (!MiniZ.ZipReaderExtractToFile(&mZipFile.[Friend]mFile, mFileIdx, scope String(filePath, .NullTerminate), .None))
					return .Err;
				return .Ok;
			}

			int GetStrLen(char8* ptr, int max)
			{
				int i = 0;
				for (; i < max; i++)
				{
					if (ptr[i] == 0)
						break;
				}
				return i;
			}

			public int GetCompressedSize()
			{
				return mFileStat.mCompSize;
			}

			public int GetUncompressedSize()
			{
				return mFileStat.mUncompSize;
			}

			public Result<void> GetFileName(String outFileName)
			{
				outFileName.Append(&mFileStat.mFilename, GetStrLen(&mFileStat.mFilename, mFileStat.mFilename.Count));
				return .Ok;
			}

			public Result<void> GetComment(String outComment)
			{
				outComment.Append(&mFileStat.mComment, GetStrLen(&mFileStat.mComment, mFileStat.mComment.Count));
				return .Ok;
			}
		}

		MiniZ.ZipArchive mFile;
		bool mInitialized;

		public ~this()
		{
			if (mInitialized)
				MiniZ.ZipReaderEnd(&mFile);
		}

		public Result<void> Open(StringView fileName)
		{
			Debug.Assert(!mInitialized);
			if (!MiniZ.ZipReaderInitFile(&mFile, scope String(fileName, .NullTerminate), .None))
				return .Err;
			mInitialized = true;
			return .Ok;
		}

		public Result<void> Init(Stream stream)
		{
			Debug.Assert(!mInitialized);
			if (!MiniZ.ZipReaderInitStream(&mFile, stream, .None))
				return .Err;
			mInitialized = true;
			return .Ok;
		}

		public int GetNumFiles()
		{
			return MiniZ.ZipReaderGetNumFiles(&mFile);
		}

		public Result<void> SelectEntry(int idx, Entry outEntryInfo)
		{
			if (!MiniZ.ZipReaderFileStat(&mFile, (.)idx, &outEntryInfo.[Friend]mFileStat))
			{
				outEntryInfo.[Friend]mZipFile = null;
				return .Err;
			}
			outEntryInfo.[Friend]mFileIdx = (.)idx;
			outEntryInfo.[Friend]mZipFile = this;
			return .Ok;
		}
	}

	typealias mz_uint = uint32;
	typealias time_t = uint64;

#if TINFL_USE_64BIT_BITBUF
	typealias tinfl_bit_buf_t = uint64;
#else
	typealias tinfl_bit_buf_t = uint32;
#endif

	class MiniZ
	{
		public function void* AllocFunc(void* opaque, int items, int size);
		public function void FreeFunc(void* opaque, void* address);
		public function void* ReallocFunc(void* opaque, void* address, int items, int size);

		const int ADLER32_INIT = 1;
		const int CRC32_INIT = 0;

		const String VERSION = "9.1.15";
		const int32 VerNum = 0x91F0;
		const int32 VerMajor = 9;
		const int32 VerMinor = 1;
		const int32 VerRevision = 15;
		const int32 VerSubRevision = 0;

		const int32 ZIP_MAX_IO_BUF_SIZE = 64 * 1024;
		const int32 ZIP_MAX_ARCHIVE_FILENAME_SIZE = 260;
		const int32 ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE = 256;


		// Compression strategies.
		public enum CompressionStrategy { DEFAULT_STRATEGY = 0, FILTERED = 1, HUFFMAN_ONLY = 2, RLE = 3, FIXED = 4 };

		// Method
		const int DEFLATED = 8;

		// Flush values. For typical usage you only need NO_FLUSH and FINISH. The other values are for advanced use (refer to the zlib docs).
		public enum Flush
		{
			NO_FLUSH = 0, PARTIAL_FLUSH = 1, SYNC_FLUSH = 2, FULL_FLUSH = 3, FINISH = 4, BLOCK = 5
		};

		// Return status codes. PARAM_ERROR is non-standard.
		public enum ReturnStatus { OK = 0, STREAM_END = 1, NEED_DICT = 2, ERRNO = -1, STREAM_ERROR = -2, DATA_ERROR = -3, MEM_ERROR = -4, BUF_ERROR = -5, VERSION_ERROR = -6, PARAM_ERROR = -10000 };

		// Compression levels: 0-9 are the standard zlib-style levels, 10 is best possible compression (not zlib compatible, and may be very slow), DEFAULT_COMPRESSION=DEFAULT_LEVEL.
		public enum CompressionLevel { NO_COMPRESSION = 0, BEST_SPEED = 1, BEST_COMPRESSION = 9, UBER_COMPRESSION = 10, DEFAULT_LEVEL = 6, DEFAULT_COMPRESSION = -1 };

		const int DEFAULT_WINDOW_BITS = 15;

		public struct InternalState
		{
		}

		// Compression/decompression stream struct.
		public struct ZipStream
		{
			public uint8* mNextIn;     // pointer to next byte to read
			public uint32 mAvailIn;            // number of bytes available at next_in
			public uint32 mTotalIn;                // total number of bytes consumed so far

			public uint8* mNextOut;          // pointer to next byte to write
			public uint32 mAvailOut;           // number of bytes that can be written to next_out
			public uint32 mTotalOut;               // total number of bytes produced so far

			public char8* mMsg;                        // error msg (unused)
			public InternalState* mState;  // internal state, allocated by zalloc/zfree

			public AllocFunc mZAlloc;             // optional heap allocation function (defaults to malloc)
			public FreeFunc mZFree;               // optional heap free function (defaults to free)
			public void* mOpaque;                     // heap alloc function user pointer

			public int mDataType;                    // data_type (unused)
			public uint32 mAdler;                   // adler32 of the source or uncompressed data
			public uint32 mReserved;                // not used
		}

		public struct ZipArchiveFileStat
		{
			public int32 mFileIndex;
			public uint32 mCentralDirOfs;
			public uint16 mVersionMadeBy;
			public uint16 mVersionNeeded;
			public uint16 mBitFlag;
			public uint16 mMethod;

			public time_t mTime;

			public uint32 mCrc32;
			public int64 mCompSize;
			public int64 mUncompSize;
			public uint16 mInternalAttr;
			public uint32 mExternalAttr;
			public int64 mLocalHeaderOfs;
			public int32 mCommentSize;
			public char8[ZIP_MAX_ARCHIVE_FILENAME_SIZE] mFilename;
			public char8[ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE] mComment;
		};

		public function int FileReadFunc(void* pOpaque, int64 file_ofs, void* pBuf, int n);
		public function int FileWriteFunc(void* pOpaque, int64 file_ofs, void* pBuf, int n);

		public enum ZipMode
		{
			Invalid = 0,
			Reading = 1,
			Writing = 2,
			WritingHasBeenFinalized = 3
		};

		public struct ZipArchive
		{
			public int64 m_archive_size;
			public int64 m_central_directory_file_ofs;
			public int32 m_total_files;
			public ZipMode m_zip_mode;

			public int32 m_file_offset_alignment;

			public AllocFunc m_pAlloc;
			public FreeFunc m_pFree;
			public ReallocFunc m_pRealloc;
			public void* m_pAlloc_opaque;

			public FileReadFunc m_pRead;
			public FileWriteFunc m_pWrite;
			public void* m_pIO_opaque;

			public ZipInternalState* m_pState;

		}

		enum ZipFlags
		{
			None = 0,
			CaseSensitive = 0x0100,
			IgnorePath = 0x0200,
			CompressedData = 0x0400,
			DoNotSortCentralDirectory = 0x0800
		}

		[CLink, StdCall]
		static extern void* malloc(int size);
		[CLink, StdCall]
		static extern void free(void* ptr);
		[CLink, StdCall]
		static extern void* realloc(void* ptr, int newSize);

		static void* def_alloc_func(void* opaque, int items, int size)
		{
			return malloc(items * size);
		}
		static void def_free_func(void* opaque, void* address)
		{
			free(address);
		}
		static void* def_realloc_func(void* opaque, void* address, int items, int size)
		{
			return realloc(address, items * size);
		}

		enum TinflFlag
		{
			ParseZlibHeader = 1,
			HasMoreInput = 2,
			UsingNonWrappingOutputBuf = 4,
			ComputeAdler32 = 8
		}

		public const int TINFL_LZ_DICT_SIZE = 32768;

		enum TinflStatus
		{
			BadParam = -3,
			Adler32Mismatch = -2,
			Failed = -1,
			Done = 0,
			NeedsMoreInput = 1,
			HasMoreOutput = 2
		}

		static mixin tinfl_init(var r)
		{
			r.m_state = 0;
		}

		static mixin tinfl_get_adler32(var r)
		{
			r.m_check_adler32
		}

		// Internal/private bits follow.

		const int TINFL_MAX_HUFF_TABLES = 3;
		const int TINFL_MAX_HUFF_SYMBOLS_0 = 288;
		const int TINFL_MAX_HUFF_SYMBOLS_1 = 32;
		const int TINFL_MAX_HUFF_SYMBOLS_2 = 19;
		const int TINFL_FAST_LOOKUP_BITS = 10;
		const int TINFL_FAST_LOOKUP_SIZE = 1 << TINFL_FAST_LOOKUP_BITS;

		struct TinflHuffTable
		{
			public uint8[TINFL_MAX_HUFF_SYMBOLS_0] m_code_size;
			public int16[TINFL_FAST_LOOKUP_SIZE] m_look_up;
			public int16[TINFL_MAX_HUFF_SYMBOLS_0 * 2] m_tree;
		}

#if TINFL_USE_64BIT_BITBUF
		const int TINFL_BITBUF_SIZE = 64;
#else
		  const int TINFL_BITBUF_SIZE = 32;
#endif

		struct TinflDecompressor
		{
			public uint32 m_zhdr0, m_zhdr1, m_z_adler32, m_check_adler32;
			public int32 m_state, m_num_bits, m_final, m_type, m_dist, m_counter, m_num_extra;
			public int32[TINFL_MAX_HUFF_TABLES] m_table_sizes;
			public tinfl_bit_buf_t m_bit_buf;
			public int32 m_dist_from_out_buf_start;
			public TinflHuffTable[TINFL_MAX_HUFF_TABLES] m_tables;
			public uint8[4] m_raw_header;
			public uint8[TINFL_MAX_HUFF_SYMBOLS_0 + TINFL_MAX_HUFF_SYMBOLS_1 + 137] m_len_codes;
		};

		  // Output stream interface. The compressor uses this interface to write compressed data. It'll typically be called TDEFL_OUT_BUF_SIZE at a time.
		function bool tdefl_put_buf_func_ptr(void* pBuf, int len, void* pUser);

		  // ------------------- Low-level Compression API Definitions

		  // Set TDEFL_LESS_MEMORY to 1 to use less memory (compression will be slightly slower, and raw/dynamic blocks will be output more frequently).
  //#define TDEFL_LESS_MEMORY

		  // TDEFL_WRITE_ZLIB_HEADER: If set, the compressor outputs a zlib header before the deflate data, and the Adler-32 of the source data at the end. Otherwise, you'll get raw deflate data.
		  // TDEFL_COMPUTE_ADLER32: Always compute the adler-32 of the input data (even when not writing zlib headers).
		  // TDEFL_GREEDY_PARSING_FLAG: Set to use faster greedy parsing, instead of more efficient lazy parsing.
		  // TDEFL_NONDETERMINISTIC_PARSING_FLAG: Enable to decrease the compressor's initialization time to the minimum, but the output may vary from run to run given the same input (depending on the contents of memory).
		  // TDEFL_RLE_MATCHES: Only look for RLE matches (matches with a distance of 1)
		  // TDEFL_FILTER_MATCHES: Discards matches <= 5 chars if enabled.
		  // TDEFL_FORCE_ALL_STATIC_BLOCKS: Disable usage of optimized Huffman tables.
		  // TDEFL_FORCE_ALL_RAW_BLOCKS: Only use raw (uncompressed) deflate blocks.
		  // The low 12 bits are reserved to control the max # of hash probes per dictionary lookup (see TDEFL_MAX_PROBES_MASK).

		  // tdefl_init() compression flags logically OR'd together (low 12 bits contain the max. number of probes per dictionary search):
		  // TDEFL_DEFAULT_MAX_PROBES: The compressor defaults to 128 dictionary probes per dictionary search. 0=Huffman only, 1=Huffman+LZ (fastest/crap compression), 4095=Huffman+LZ (slowest/best compression).
		enum TDEFLFlags
		{
			TDEFL_HUFFMAN_ONLY = 0, TDEFL_DEFAULT_MAX_PROBES = 128, TDEFL_MAX_PROBES_MASK = 0xFFF,

			TDEFL_WRITE_ZLIB_HEADER = 0x01000,
			TDEFL_COMPUTE_ADLER32 = 0x02000,
			TDEFL_GREEDY_PARSING_FLAG = 0x04000,
			TDEFL_NONDETERMINISTIC_PARSING_FLAG = 0x08000,
			TDEFL_RLE_MATCHES = 0x10000,
			TDEFL_FILTER_MATCHES = 0x20000,
			TDEFL_FORCE_ALL_STATIC_BLOCKS = 0x40000,
			TDEFL_FORCE_ALL_RAW_BLOCKS = 0x80000
		};

		const int32 TDEFL_MAX_HUFF_TABLES = 3;
		const int32 TDEFL_MAX_HUFF_SYMBOLS_0 = 288;
		const int32 TDEFL_MAX_HUFF_SYMBOLS_1 = 32;
		const int32 TDEFL_MAX_HUFF_SYMBOLS_2 = 19;
		const int32 TDEFL_LZ_DICT_SIZE = 32768;
		const int32 TDEFL_LZ_DICT_SIZE_MASK = TDEFL_LZ_DICT_SIZE - 1;
		const int32 TDEFL_MIN_MATCH_LEN = 3;
		const int32 TDEFL_MAX_MATCH_LEN = 258;

		  // TDEFL_OUT_BUF_SIZE MUST be large enough to hold a single entire compressed output block (using static/fixed Huffman codes).
#if TDEFL_LESS_MEMORY
		  enum { TDEFL_LZ_CODE_BUF_SIZE = 24 * 1024, TDEFL_OUT_BUF_SIZE = (TDEFL_LZ_CODE_BUF_SIZE * 13 ) / 10, TDEFL_MAX_HUFF_SYMBOLS = 288, TDEFL_LZ_HASH_BITS = 12, TDEFL_LEVEL1_HASH_SIZE_MASK = 4095, TDEFL_LZ_HASH_SHIFT = (TDEFL_LZ_HASH_BITS + 2) / 3, TDEFL_LZ_HASH_SIZE = 1 << TDEFL_LZ_HASH_BITS };
#else
		const int32 TDEFL_LZ_CODE_BUF_SIZE = 64 * 1024;
		const int32 TDEFL_OUT_BUF_SIZE = (TDEFL_LZ_CODE_BUF_SIZE * 13) / 10;
		const int32 TDEFL_MAX_HUFF_SYMBOLS = 288;
		const int32 TDEFL_LZ_HASH_BITS = 15;
		const int32 TDEFL_LEVEL1_HASH_SIZE_MASK = 4095;
		const int32 TDEFL_LZ_HASH_SHIFT = (TDEFL_LZ_HASH_BITS + 2) / 3;
		const int32 TDEFL_LZ_HASH_SIZE = 1 << TDEFL_LZ_HASH_BITS;
#endif

		  // The low-level tdefl functions below may be used directly if the above helper functions aren't flexible enough. The low-level functions don't make any heap allocations, unlike the above helper functions.
		enum TdeflStatus
		{
			TDEFL_STATUS_BAD_PARAM = -2,
			TDEFL_STATUS_PUT_BUF_FAILED = -1,
			TDEFL_STATUS_OKAY = 0,
			TDEFL_STATUS_DONE = 1,
		}

		  // Must map to NO_FLUSH, SYNC_FLUSH, etc. enums
		enum TdeflFlush
		{
			TDEFL_NO_FLUSH = 0,
			TDEFL_SYNC_FLUSH = 2,
			TDEFL_FULL_FLUSH = 3,
			TDEFL_FINISH = 4
		}

		  // tdefl's compression state structure.
		struct tdefl_compressor
		{
			public tdefl_put_buf_func_ptr m_pPut_buf_func;
			public void* m_pPut_buf_user;
			public TDEFLFlags m_flags;
			public uint32[2] m_max_probes;
			public bool m_greedy_parsing;
			public uint32 m_adler32;
			public int32 m_lookahead_pos, m_lookahead_size, m_dict_size;
			public uint8* m_pLZ_code_buf, m_pLZ_flags, m_pOutput_buf, m_pOutput_buf_end;
			public int32 m_num_flags_left, m_total_lz_bytes, m_lz_code_buf_dict_pos, m_bits_in;
			public uint32 m_bit_buffer;
			public int32 m_saved_match_dist, m_saved_match_len, m_saved_lit, m_output_flush_ofs, m_output_flush_remaining, m_block_index;
			public bool m_finished, m_wants_to_finish;
			public TdeflStatus m_prev_return_status;
			public void* m_pIn_buf;
			public void* m_pOut_buf;
			public int* m_pIn_buf_size, m_pOut_buf_size;
			public TdeflFlush m_flush;
			public uint8* m_pSrc;
			public int32 m_src_buf_left, m_out_buf_ofs;
			public uint8[TDEFL_LZ_DICT_SIZE + TDEFL_MAX_MATCH_LEN - 1] m_dict;
			public uint16[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS] m_huff_count;
			public uint16[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS] m_huff_codes;
			public uint8[TDEFL_MAX_HUFF_TABLES][TDEFL_MAX_HUFF_SYMBOLS] m_huff_code_sizes;
			public uint8[TDEFL_LZ_CODE_BUF_SIZE] m_lz_code_buf;
			public uint16[TDEFL_LZ_DICT_SIZE] m_next;
			public uint16[TDEFL_LZ_HASH_SIZE] m_hash;
			public uint8[TDEFL_OUT_BUF_SIZE] m_output_buf;
		}

		static mixin ReadLE16(var p)
		{
			*((uint16*)(p))
		}

		static mixin ReadLE32(var p)
		{
			*((uint32*)(p))
		}

		static mixin MAX(var a, var b)
		{
			(((a) > (b)) ? (a) : (b))
		}

		// ------------------- zlib-style API's

		static uint32 adler32(uint32 adler, uint8* buf, int bufLen)
		{
			uint8* curPtr = buf;
			int lenLeft = bufLen;
			uint32 i, s1 = (uint32)(adler & 0xffff), s2 = (uint32)(adler >> 16); int block_len = lenLeft % 5552;
			if (curPtr == null) return ADLER32_INIT;
			while (lenLeft > 0)
			{
				for (i = 0; i + 7 < block_len; i += 8,curPtr += 8)
				{
					s1 += curPtr[0]; s2 += s1; s1 += curPtr[1]; s2 += s1; s1 += curPtr[2]; s2 += s1; s1 += curPtr[3]; s2 += s1;
					s1 += curPtr[4]; s2 += s1; s1 += curPtr[5]; s2 += s1; s1 += curPtr[6]; s2 += s1; s1 += curPtr[7]; s2 += s1;
				}
				for (; i < block_len; ++i) { s1 += *curPtr++; s2 += s1; }
				s1 %= 65521U; s2 %= 65521U; lenLeft -= block_len; block_len = 5552;
			}
			return (s2 << 16) + s1;
		}

		const uint32[16] s_crc32 = uint32[16](0, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
			0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c);
		static uint32 crc32(uint32 crc, uint8* buf, int buf_len)
		{
			uint8* ptr = buf;
			int lenLeft = buf_len;
			uint32 crcu32 = (uint32)crc;
			if (ptr == null) return CRC32_INIT;
			crcu32 = ~crcu32; while (lenLeft-- > 0) { uint8 b = *ptr++; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b & 0xF)]; crcu32 = (crcu32 >> 4) ^ s_crc32[(crcu32 & 0xF) ^ (b >> 4)]; }
			return ~crcu32;
		}

		static String Version()
		{
			return VERSION;
		}

		static ReturnStatus DeflateInit(ZipStream* pStream, CompressionLevel level)
		{
			return DeflateInit(pStream, level, DEFLATED, DEFAULT_WINDOW_BITS, 9, .DEFAULT_STRATEGY);
		}

		static ReturnStatus DeflateInit(ZipStream* pStream, CompressionLevel level, int method, int window_bits, int mem_level, CompressionStrategy strategy)
		{
			tdefl_compressor* pComp;
			TDEFLFlags comp_flags = TDEFLFlags.TDEFL_COMPUTE_ADLER32 | tdefl_create_comp_flags_from_zip_params(level, window_bits, strategy);

			if (pStream == null) return .STREAM_ERROR;
			if ((method != DEFLATED) || ((mem_level < 1) || (mem_level > 9)) || ((window_bits != DEFAULT_WINDOW_BITS) && (-window_bits != DEFAULT_WINDOW_BITS))) return .PARAM_ERROR;

			pStream.mDataType = 0;
			pStream.mAdler = ADLER32_INIT;
			pStream.mMsg = null;
			pStream.mReserved = 0;
			pStream.mTotalIn = 0;
			pStream.mTotalOut = 0;
			if (pStream.mZAlloc == null) pStream.mZAlloc = => def_alloc_func;
			if (pStream.mZFree == null) pStream.mZFree = => def_free_func;

			pComp = (tdefl_compressor*)pStream.mZAlloc(pStream.mOpaque, 1, sizeof(tdefl_compressor));
			if (pComp == null)
				return .MEM_ERROR;

			pStream.mState = (InternalState*)pComp;

			if (tdefl_init(pComp, default, null, comp_flags) != .TDEFL_STATUS_OKAY)
			{
				deflateEnd(pStream);
				return .PARAM_ERROR;
			}

			return .OK;
		}

		static ReturnStatus deflateReset(ZipStream* pStream)
		{
			if ((pStream == null) || (pStream.mState == null) || (pStream.mZAlloc == null) || (pStream.mZFree == null)) return .STREAM_ERROR;
			pStream.mTotalIn = pStream.mTotalOut = 0;
			tdefl_init((tdefl_compressor*)pStream.mState, null, null, ((tdefl_compressor*)pStream.mState).m_flags);
			return .OK;
		}

		static ReturnStatus deflate(ZipStream* pStream, Flush flushIn)
		{
			Flush flush = flushIn;
			int in_bytes, out_bytes;
			uint32 orig_total_in, orig_total_out;
			ReturnStatus status = .OK;

			if ((pStream == null) || (pStream.mState == null) || (flush < default) || (flush > .FINISH) || (pStream.mNextOut == null)) return .STREAM_ERROR;
			if (pStream.mAvailOut == 0) return .BUF_ERROR;

			if (flush == .PARTIAL_FLUSH) flush = .SYNC_FLUSH;

			if (((tdefl_compressor*)pStream.mState).m_prev_return_status == .TDEFL_STATUS_DONE)
				return (flush == .FINISH) ? .STREAM_END : .BUF_ERROR;

			orig_total_in = pStream.mTotalIn; orig_total_out = pStream.mTotalOut;
			for (;;)
			{
				TdeflStatus defl_status;
				in_bytes = pStream.mAvailIn; out_bytes = pStream.mAvailOut;

				defl_status = tdefl_compress((tdefl_compressor*)pStream.mState, pStream.mNextIn, &in_bytes, pStream.mNextOut, &out_bytes, (TdeflFlush)flush);
				pStream.mNextIn += (uint32)in_bytes; pStream.mAvailIn -= (uint32)in_bytes;
				pStream.mTotalIn += (uint32)in_bytes; pStream.mAdler = tdefl_get_adler32((tdefl_compressor*)pStream.mState);

				pStream.mNextOut += (uint32)out_bytes; pStream.mAvailOut -= (uint32)out_bytes;
				pStream.mTotalOut += (uint32)out_bytes;

				if (defl_status < default)
				{
					status = .STREAM_ERROR;
					break;
				}
				else if (defl_status == .TDEFL_STATUS_DONE)
				{
					status = .STREAM_END;
					break;
				}
				else if (pStream.mAvailOut == 0)
					break;
				else if ((pStream.mAvailIn == 0) && (flush != .FINISH))
				{
					if ((flush != 0) || (pStream.mTotalIn != orig_total_in) || (pStream.mTotalOut != orig_total_out))
						break;
					return .BUF_ERROR; // Can't make forward progress without some input.
				}
			}
			return status;
		}

		static ReturnStatus deflateEnd(ZipStream* pStream)
		{
			if (pStream == null) return .STREAM_ERROR;
			if (pStream.mState != null)
			{
				pStream.mZFree(pStream.mOpaque, pStream.mState);
				pStream.mState = null;
			}
			return .OK;
		}

		static uint32 deflateBound(ZipStream* pStream, uint32 source_len)
		{
		  // This is really over conservative. (And lame, but it's actually pretty tricky to compute a true upper bound given the way tdefl's blocking works.)
			return Math.Max(128 + (source_len * 110) / 100, 128 + source_len + ((source_len / (31 * 1024)) + 1) * 5);
		}

		public static ReturnStatus Compress(uint8* pDest, ref int pDest_len, uint8* pSource, int source_len, CompressionLevel level)
		{
			ReturnStatus status;
			ZipStream stream = ?;
			Internal.MemSet(&stream, 0, sizeof(ZipStream));

			// In case uint32 is 64-bits (argh I hate longs).
			if ((source_len | pDest_len) > 0xFFFFFFFFU) return .PARAM_ERROR;

			stream.mNextIn = pSource;
			stream.mAvailIn = (uint32)source_len;
			stream.mNextOut = pDest;
			stream.mAvailOut = (uint32)pDest_len;

			status = DeflateInit(&stream, level);
			if (status != .OK) return status;

			status = deflate(&stream, .FINISH);
			if (status != .STREAM_END)
			{
				deflateEnd(&stream);
				return (status == .OK) ? .BUF_ERROR : status;
			}

			pDest_len = stream.mTotalOut;
			return deflateEnd(&stream);
		}

		public static ReturnStatus Compress(uint8* pDest, ref int pDest_len, uint8* pSource, int source_len)
		{
			return Compress(pDest, ref pDest_len, pSource, source_len, .DEFAULT_COMPRESSION);
		}

		public static uint32 CompressBound(int source_len)
		{
			return deflateBound(null, (uint32)source_len);
		}

		struct inflate_state
		{
			public TinflDecompressor m_decomp;
			public uint32 m_dict_ofs, m_dict_avail;
			public bool m_first_call, m_has_flushed;
			public int m_window_bits;
			public uint8[TINFL_LZ_DICT_SIZE] m_dict;
			public TinflStatus m_last_status;
		}

		static ReturnStatus inflateInit2(ZipStream* pStream, int window_bits)
		{
			inflate_state* pDecomp;
			if (pStream == null) return .STREAM_ERROR;
			if ((window_bits != DEFAULT_WINDOW_BITS) && (-window_bits != DEFAULT_WINDOW_BITS)) return .PARAM_ERROR;

			pStream.mDataType = 0;
			pStream.mAdler = 0;
			pStream.mMsg = null;
			pStream.mTotalIn = 0;
			pStream.mTotalOut = 0;
			pStream.mReserved = 0;
			if (pStream.mZAlloc == null) pStream.mZAlloc = => def_alloc_func;
			if (pStream.mZFree == null) pStream.mZFree = => def_free_func;

			pDecomp = (inflate_state*)pStream.mZAlloc(pStream.mOpaque, 1, sizeof(inflate_state));
			if (pDecomp == null) return .MEM_ERROR;

			pStream.mState = (InternalState*)pDecomp;

			tinfl_init!(&pDecomp.m_decomp);
			pDecomp.m_dict_ofs = 0;
			pDecomp.m_dict_avail = 0;
			pDecomp.m_last_status = .NeedsMoreInput;
			pDecomp.m_first_call = true;
			pDecomp.m_has_flushed = false;
			pDecomp.m_window_bits = window_bits;

			return .OK;
		}

		static ReturnStatus inflateInit(ZipStream* pStream)
		{
			return inflateInit2(pStream, DEFAULT_WINDOW_BITS);
		}

		static ReturnStatus inflate(ZipStream* pStream, Flush flushIn)
		{
			Flush flush = flushIn;
			inflate_state* pState;
			uint32 n;
			bool first_call;
			TinflFlag decomp_flags = .ComputeAdler32;
			int in_bytes, out_bytes, orig_avail_in;
			TinflStatus status = ?;

			if ((pStream == null) || (pStream.mState == null)) return .STREAM_ERROR;
			if (flush == .PARTIAL_FLUSH) flush = .SYNC_FLUSH;
			if ((flush != 0) && (flush != .SYNC_FLUSH) && (flush != .FINISH)) return .STREAM_ERROR;

			pState = (inflate_state*)pStream.mState;
			if (pState.m_window_bits > 0) decomp_flags |= .ParseZlibHeader;
			orig_avail_in = pStream.mAvailIn;

			first_call = pState.m_first_call; pState.m_first_call = false;
			if (pState.m_last_status < default) return .DATA_ERROR;

			if ((pState.m_has_flushed) && (flush != .FINISH)) return .STREAM_ERROR;
			pState.m_has_flushed |= (flush == .FINISH);

			if ((flush == .FINISH) && (first_call))
			{
			  // FINISH on the first call implies that the input and output buffers are large enough to hold the entire compressed/decompressed file.
				decomp_flags |= .UsingNonWrappingOutputBuf;
				in_bytes = pStream.mAvailIn; out_bytes = pStream.mAvailOut;
				status = tinfl_decompress(&pState.m_decomp, pStream.mNextIn, &in_bytes, pStream.mNextOut, pStream.mNextOut, &out_bytes, decomp_flags);
				pState.m_last_status = status;
				pStream.mNextIn += (uint32)in_bytes; pStream.mAvailIn -= (uint32)in_bytes; pStream.mTotalIn += (uint32)in_bytes;
				pStream.mAdler = tinfl_get_adler32!(&pState.m_decomp);
				pStream.mNextOut += (uint)out_bytes; pStream.mAvailOut -= (uint32)out_bytes; pStream.mTotalOut += (uint32)out_bytes;

				if (status < default)
					return .DATA_ERROR;
				else if (status != .Done)
				{
					pState.m_last_status = .Failed;
					return .BUF_ERROR;
				}
				return .STREAM_END;
			}
			// flush != FINISH then we must assume there's more input.
			if (flush != .FINISH) decomp_flags |= .HasMoreInput;

			if (pState.m_dict_avail != 0)
			{
				n = Math.Min(pState.m_dict_avail, pStream.mAvailOut);
				Internal.MemCpy(pStream.mNextOut, &pState.m_dict + pState.m_dict_ofs, n);
				pStream.mNextOut += n; pStream.mAvailOut -= n; pStream.mTotalOut += n;
				pState.m_dict_avail -= n; pState.m_dict_ofs = (pState.m_dict_ofs + n) & (TINFL_LZ_DICT_SIZE - 1);
				return ((pState.m_last_status == .Done) && (pState.m_dict_avail == 0)) ? .STREAM_END : .OK;
			}

			for (;;)
			{
				in_bytes = pStream.mAvailIn;
				out_bytes = TINFL_LZ_DICT_SIZE - pState.m_dict_ofs;

				status = tinfl_decompress(&pState.m_decomp, pStream.mNextIn, &in_bytes, &pState.m_dict, &pState.m_dict + pState.m_dict_ofs, &out_bytes, decomp_flags);
				pState.m_last_status = status;

				pStream.mNextIn += (uint32)in_bytes; pStream.mAvailIn -= (uint32)in_bytes;
				pStream.mTotalIn += (uint32)in_bytes; pStream.mAdler = tinfl_get_adler32!(&pState.m_decomp);

				pState.m_dict_avail = (uint32)out_bytes;

				n = Math.Min(pState.m_dict_avail, pStream.mAvailOut);
				Internal.MemCpy(pStream.mNextOut, &pState.m_dict + pState.m_dict_ofs, n);
				pStream.mNextOut += n; pStream.mAvailOut -= n; pStream.mTotalOut += n;
				pState.m_dict_avail -= n; pState.m_dict_ofs = (pState.m_dict_ofs + n) & (TINFL_LZ_DICT_SIZE - 1);

				if (status < default)
					return .DATA_ERROR; // Stream is corrupted (there could be some uncompressed data left in the output dictionary - oh well).
				else if ((status == .NeedsMoreInput) && (orig_avail_in == 0))
					return .BUF_ERROR; // Signal caller that we can't make forward progress without supplying more input or by setting flush to FINISH.
				else if (flush == .FINISH)
				{
				   // The output buffer MUST be large to hold the remaining uncompressed data when flush==FINISH.
					if (status == .Done)
						return (pState.m_dict_avail != 0) ? .BUF_ERROR : .STREAM_END;
				   // status here must be TINFL_STATUS_HAS_MORE_OUTPUT, which means there's at least 1 more byte on the way. If there's no more room left in the output buffer then something is wrong.
					else if (pStream.mAvailOut == 0)
						return .BUF_ERROR;
				}
				else if ((status == .Done) || (pStream.mAvailIn == 0) || (pStream.mAvailOut == 0) || (pState.m_dict_avail != 0))
					break;
			}

			return ((status == .Done) && (pState.m_dict_avail == 0)) ? .STREAM_END : .OK;
		}

		static ReturnStatus inflateEnd(ZipStream* pStream)
		{
			if (pStream == null)
				return .STREAM_ERROR;
			if (pStream.mState != null)
			{
				pStream.mZFree(pStream.mOpaque, pStream.mState);
				pStream.mState = null;
			}
			return .OK;
		}

		public static ReturnStatus Uncompress(uint8* pDest, ref int pDest_len, uint8* pSource, int source_len)
		{
			ZipStream stream = default;
			ReturnStatus status;
			Internal.MemSet(&stream, 0, sizeof(ZipStream));

			// In case uint32 is 64-bits (argh I hate longs).
			if ((source_len | pDest_len) > 0xFFFFFFFFU) return .PARAM_ERROR;

			stream.mNextIn = pSource;
			stream.mAvailIn = (uint32)source_len;
			stream.mNextOut = pDest;
			stream.mAvailOut = (uint32)pDest_len;

			status = inflateInit(&stream);
			if (status != .OK)
				return status;

			status = inflate(&stream, .FINISH);
			if (status != .STREAM_END)
			{
				inflateEnd(&stream);
				return ((status == .BUF_ERROR) && (stream.mAvailIn == 0)) ? .DATA_ERROR : status;
			}
			pDest_len = stream.mTotalOut;

			return inflateEnd(&stream);
		}

		static char8* error(ReturnStatus err)
		{
			switch (err)
			{
			case .OK: return "";
			case .STREAM_END: return "Stream end";
			case .NEED_DICT: return "need dictionary";
			case .ERRNO: return "file error";
			case .STREAM_ERROR: return "stream error";
			case .DATA_ERROR: return "data error";
			case .MEM_ERROR: return "out of memory";
			case .BUF_ERROR: return "buf error";
			case .VERSION_ERROR: return "version error";
			case .PARAM_ERROR: return "parameter error";
			default: return null;
			}
		}

		//TINFL_CR_RETURN(state_index, result) do { status = result; r.m_state = state_index; break common_exit; case state_index:; } MACRO_END

		const int32[31] s_length_base = int32[31](3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0);
		const int32[31] s_length_extra = int32[31](0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0, 0);
		const int32[32] s_dist_base = int32[32](1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0, 0);
		const int32[32] s_dist_extra = int32[32](0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 0, 0);
		const uint8[19] s_length_dezigzag = uint8[19](16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15);
		const int32[3] s_min_table_sizes = int32[3](257, 1, 4);

		static void Test()
		{
			int status = 0;

			mixin Test()
			{
				status = 12;
			}


			status = 9;
		}

		static TinflStatus tinfl_decompress(TinflDecompressor* r, uint8* pIn_buf_next, int* pIn_buf_size, uint8* pOut_buf_start, uint8* pOut_buf_next, int* pOut_buf_size, TinflFlag decomp_flags)
		{
			TinflStatus status = .Failed; int32 num_bits, dist, counter, num_extra; tinfl_bit_buf_t bit_buf;
			uint8* pIn_buf_cur = pIn_buf_next, pIn_buf_end = pIn_buf_next + *pIn_buf_size;
			uint8* pOut_buf_cur = pOut_buf_next, pOut_buf_end = pOut_buf_next + *pOut_buf_size;
			int32 out_buf_size_mask = (int32)(decomp_flags.HasFlag(.UsingNonWrappingOutputBuf) ? (int32) - 1 : ((pOut_buf_next - pOut_buf_start) + *pOut_buf_size) - 1), dist_from_out_buf_start;

			// Ensure the output buffer's size is a power of 2, unless the output buffer is large enough to hold the entire output file (in which case it doesn't matter).
			if ((((out_buf_size_mask + 1) & out_buf_size_mask) != 0) || (pOut_buf_next < pOut_buf_start)) { *pIn_buf_size = *pOut_buf_size = 0; return .BadParam; }

			num_bits = r.m_num_bits; bit_buf = r.m_bit_buf; dist = r.m_dist; counter = r.m_counter; num_extra = r.m_num_extra; dist_from_out_buf_start = r.m_dist_from_out_buf_start;

			mixin GetByte(var c)
			{
				if (pIn_buf_cur >= pIn_buf_end)
				{
					if (decomp_flags.HasFlag(.HasMoreInput))
					{
						DoResult!(TinflStatus.NeedsMoreInput);
					}
					else
						c = 0;
				}
				else
					c = *pIn_buf_cur++;
			}

			mixin NeedBits(var n)
			{
				repeat
				{
					uint32 c = ?;
					GetByte!(c);
					bit_buf |= (((tinfl_bit_buf_t)c) << num_bits); num_bits += 8;
				}
				while (num_bits < (int32)(n));
			}

			mixin GetBits<T>(T b, var n) where T : var
			{
				if (num_bits < (int32)(n))
				{
					NeedBits!(n);
				}
				b = (T)(bit_buf & ((1 << (int32)(n)) - 1));
				bit_buf >>= (n); num_bits -= (n);
			}

			mixin SkipBits(var n)
			{
				if (num_bits < (int32)(n))
				{
					NeedBits!(n);
				}
				bit_buf >>= (n); num_bits -= (n);
			}

			mixin HuffDecode(var sym, var pHuff)
			{
				int32 temp; int32 code_len, c = ?;
				if (num_bits < 15)
				{
					if ((pIn_buf_end - pIn_buf_cur) < 2)
					{
						repeat
						{
							temp = (int32)pHuff.m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)];
							if (temp >= 0)
							{
								code_len = temp >> 9;
								if ((code_len != 0) && (num_bits >= code_len))
									break;
							}
							else if (num_bits > TINFL_FAST_LOOKUP_BITS)
							{
								code_len = TINFL_FAST_LOOKUP_BITS;
								repeat
								{
									temp = (uint16)pHuff.m_tree[~temp + (int32)((bit_buf >> code_len++) & 1)];
								}
								while ((temp < 0) && (num_bits >= (code_len + 1)));
								if (temp >= 0)
									break;
							}
							GetByte!(c);
							bit_buf |= (((tinfl_bit_buf_t)c) << num_bits); num_bits += 8;
						}
						while (num_bits < 15);
					}
					else
					{
						bit_buf |= (((tinfl_bit_buf_t)pIn_buf_cur[0]) << num_bits) | (((tinfl_bit_buf_t)pIn_buf_cur[1]) << (num_bits + 8)); pIn_buf_cur += 2; num_bits += 16;
					}
				}
				if ((temp = pHuff.m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
				{
					code_len = temp >> 9;
					temp &= 511;
				}
				else
				{
					code_len = TINFL_FAST_LOOKUP_BITS;
					repeat
					{
						temp = pHuff.m_tree[~temp + (int32)((bit_buf >> code_len++) & 1)];
					}
					while (temp < 0);
				}
				sym = temp; bit_buf >>= code_len; num_bits -= code_len;
			}

			mixin DoResult(TinflStatus outResult)
			{
				status = outResult;
				break OuterLoop;
			}

			OuterLoop:while (true)
			{
				//TINFL_CR_BEGIN
				StateSwitch:switch (r.m_state)
				{
				case 0:
					bit_buf = num_bits = dist = counter = num_extra = r.m_zhdr0 = r.m_zhdr1 = 0; r.m_z_adler32 = r.m_check_adler32 = 1;
					if (decomp_flags.HasFlag(.ParseZlibHeader))
					{
						GetByte!(r.m_zhdr0);
						r.m_state = 1;
					}
					else
						r.m_state = 3;
				case 1:
					GetByte!(r.m_zhdr1);
					r.m_state = 2;
				case 2:
					counter = (((r.m_zhdr0 * 256 + r.m_zhdr1) % 31 != 0) || ((r.m_zhdr1 & 32) != 0) || ((r.m_zhdr0 & 15) != 8)) ? 1 : 0;
					if (!(decomp_flags.HasFlag(.UsingNonWrappingOutputBuf))) counter |= (((1U << (8U + (uint32)(r.m_zhdr0 >> 4))) > 32768U) || ((out_buf_size_mask + 1) < (int)(1U << (8U + (r.m_zhdr0 >> 4))))) ? 1 : 0;
					if (counter != 0) { DoResult!(TinflStatus.Failed); }
					r.m_state = 3;
				case 3: // do:
					GetBits!(r.m_final, 3);
					r.m_type = r.m_final >> 1;
					if (r.m_type == 0)
						r.m_state = 5;
					else if (r.m_type == 3)
						r.m_state = 9;
					else
						r.m_state = 10;

				case 5: // if (r.m_type == 0)
					SkipBits!(num_bits & 7);
					counter = 0;
					r.m_state = 6;
				case 6: // header loop
					if (num_bits > 0)
						GetBits!(r.m_raw_header[counter], 8);
					else
						GetByte!(r.m_raw_header[counter]);
					counter++;
					if (counter < 4)
						break;

					if ((counter = (r.m_raw_header[0] | ((int32)r.m_raw_header[1] << 8))) != (int32)(0xFFFF ^ (r.m_raw_header[2] | ((int32)r.m_raw_header[3] << 8))))
					{
						DoResult!(TinflStatus.Failed);
					}
					r.m_state = 7;
				case 7:
					while ((counter > 0) && (num_bits > 0))
					{
						if (pOut_buf_cur >= pOut_buf_end)
							DoResult!(TinflStatus.HasMoreOutput);
						GetBits!(dist, 8);
						*pOut_buf_cur++ = (uint8)dist;
						counter--;
					}
					while (counter > 0)
					{
						int n;
						if (pOut_buf_cur >= pOut_buf_end)
							DoResult!(TinflStatus.HasMoreOutput);
						while (pIn_buf_cur >= pIn_buf_end)
						{
							if (decomp_flags.HasFlag(.HasMoreInput))
							{
								DoResult!(TinflStatus.NeedsMoreInput);
							}
							else
							{
								DoResult!(TinflStatus.Failed);
							}
						}
						n = Math.Min(Math.Min((int)(pOut_buf_end - pOut_buf_cur), (int)(pIn_buf_end - pIn_buf_cur)), counter);
						Internal.MemCpy(pOut_buf_cur, pIn_buf_cur, n); pIn_buf_cur += n; pOut_buf_cur += n; counter -= (int32)n;
					}
					//jump to end of for(;;)
					r.m_state = 30;

				case 9: // if (r.m_type == 3)
					DoResult!(TinflStatus.Failed);

				case 10: // else , when (r.m_type != 0) && (r.m_type != 3)
					if (r.m_type == 1)
					{
						uint8* p = &r.m_tables[0].m_code_size; uint32 i;
						r.m_table_sizes[0] = 288; r.m_table_sizes[1] = 32; Internal.MemSet(&r.m_tables[1].m_code_size, 5, 32);
						for (i = 0; i <= 143; ++i) *p++ = 8; for (; i <= 255; ++i) *p++ = 9; for (; i <= 279; ++i) *p++ = 7; for (; i <= 287; ++i) *p++ = 8;

						// jump to for ( ; (int)r.m_type >= 0; r.m_type--)
						r.m_state = 13;
					}
					else
					{
						counter = 0;
						r.m_state = 11;
					}
				case 11:
					if (counter == 2)
						GetBits!(r.m_table_sizes[counter], 4);
					else
						GetBits!(r.m_table_sizes[counter], 5);
					r.m_table_sizes[counter] += (int32)s_min_table_sizes[counter];
					if (++counter < 3)
						break;
					r.m_tables[2].m_code_size = default;
					counter = 0;
					r.m_state = 12;
				case 12:
					uint32 s = ?;
					GetBits!(s, 3);
					r.m_tables[2].m_code_size[s_length_dezigzag[counter]] = (uint8)s;
					if (++counter < r.m_table_sizes[2])
						break;
					r.m_table_sizes[2] = 19;
					r.m_state = 13; // for ( ; (int)r.m_type >= 0; r.m_type--)

				case 13: // for ( ; (int)r.m_type >= 0; r.m_type--)
					while ((int32)r.m_type >= 0)
					{
						int32 tree_next, tree_cur;
						TinflHuffTable* pTable;
						int32 i, j, used_syms, total, sym_index;
						int32[17] next_code;
						int32[16] total_syms;
						pTable = &r.m_tables[r.m_type];
						total_syms = default; pTable.m_look_up = default; pTable.m_tree = default;
						for (i = 0; i < r.m_table_sizes[r.m_type]; ++i) total_syms[pTable.m_code_size[i]]++;
						used_syms = 0; total = 0; next_code[0] = next_code[1] = 0;
						for (i = 1; i <= 15; ++i) { used_syms += total_syms[i]; next_code[i + 1] = (total = ((total + total_syms[i]) << 1)); }
						if ((65536 != total) && (used_syms > 1))
						{
							DoResult!(TinflStatus.Failed);
						}
						for (tree_next = -1,sym_index = 0; sym_index < r.m_table_sizes[r.m_type]; ++sym_index)
						{
							int32 rev_code = 0, l, cur_code, code_size = pTable.m_code_size[sym_index]; if (code_size == 0) continue;
							cur_code = next_code[code_size]++; for (l = code_size; l > 0; l--,cur_code >>= 1) rev_code = (rev_code << 1) | (cur_code & 1);
							if (code_size <= TINFL_FAST_LOOKUP_BITS) { int16 k = (int16)((code_size << 9) | sym_index); while (rev_code < TINFL_FAST_LOOKUP_SIZE) { pTable.m_look_up[rev_code] = k; rev_code += (1 << code_size); } continue; }
							if (0 == (tree_cur = pTable.m_look_up[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)])) { pTable.m_look_up[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)] = (int16)tree_next; tree_cur = tree_next; tree_next -= 2; }
							rev_code >>= (TINFL_FAST_LOOKUP_BITS - 1);
							for (j = code_size; j > (TINFL_FAST_LOOKUP_BITS + 1); j--)
							{
								tree_cur -= ((rev_code >>= 1) & 1);
								if (pTable.m_tree[-tree_cur - 1] == 0) { pTable.m_tree[-tree_cur - 1] = (int16)tree_next; tree_cur = tree_next; tree_next -= 2; } else tree_cur = pTable.m_tree[-tree_cur - 1];
							}
							tree_cur -= ((rev_code >>= 1) & 1); pTable.m_tree[-tree_cur - 1] = (int16)sym_index;
						}
						if (r.m_type == 2)
						{
							dist = 0;
							counter = 0;
							r.m_state = 14;
							break StateSwitch;
						}
						else
						{
							r.m_type--;
						}
					}
					//jump to for(;;)
					r.m_state = 22;
				case 14:
					if (dist < 16)
					{
						HuffDecode!(dist, &r.m_tables[2]);
						if (dist < 16)
						{
							r.m_len_codes[counter++] = (uint8)dist;
						}
						else if ((dist == 16) && (counter == 0))
						{
							DoResult!(TinflStatus.Failed);
						}
					}

					if (dist >= 16)
					{
						int32 s = ?;
						switch (dist - 16)
						{
						case 0:
							GetBits!(s, 2);
							s += 3;
						case 1:
							GetBits!(s, 3);
							s += 3;
						case 2:
							GetBits!(s, 7);
							s += 11;
						}

						Internal.MemSet(&r.m_len_codes[counter], (dist == 16) ? r.m_len_codes[counter - 1] : 0, s); counter += s;
					}

					dist = 0;
					if (counter < r.m_table_sizes[0] + r.m_table_sizes[1])
					{
						break;
					}

					if ((r.m_table_sizes[0] + r.m_table_sizes[1]) != counter)
					{
						DoResult!(TinflStatus.Failed);
					}
					Internal.MemCpy(&r.m_tables[0].m_code_size, &r.m_len_codes, r.m_table_sizes[0]);
					Internal.MemCpy(&r.m_tables[1].m_code_size, &r.m_len_codes[r.m_table_sizes[0]], r.m_table_sizes[1]);
					r.m_type--;
					r.m_state = 13; // Back to "r->m_type" for loop

				case 22: // for ( ; ; ) (both inner and outer start)
					if (((pIn_buf_end - pIn_buf_cur) < 4) || ((pOut_buf_end - pOut_buf_cur) < 2))
					{
						HuffDecode!(counter, &r.m_tables[0]);
						if (counter >= 256)
						{
							// jump to end of inner for(;;)
							r.m_state = 24;
							break;
						}

						r.m_state = 23;
						break;
					}
					else
					{
						int32 sym2; int32 code_len;
#if TINFL_USE_64BIT_BITBUF
						if (num_bits < 30) { bit_buf |= (((tinfl_bit_buf_t)ReadLE32!(pIn_buf_cur)) << num_bits); pIn_buf_cur += 4; num_bits += 32; }
#else
						if (num_bits < 15) { bit_buf |= (((tinfl_bit_buf_t)ReadLE16!(pIn_buf_cur)) << num_bits); pIn_buf_cur += 2; num_bits += 16; }
#endif
						if ((sym2 = (int32)r.m_tables[0].m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
							code_len = sym2 >> 9;
						else
						{
							code_len = TINFL_FAST_LOOKUP_BITS; repeat { sym2 = r.m_tables[0].m_tree[~sym2 + (int32)((bit_buf >> code_len++) & 1)]; } while (sym2 < 0);
						}
						counter = sym2; bit_buf >>= code_len; num_bits -= code_len;
						if ((counter & 256) != 0)
						{
							// jump to end of inner for(;;)
							r.m_state = 24;
							break;
						}

#if !TINFL_USE_64BIT_BITBUF
						if (num_bits < 15) { bit_buf |= (((tinfl_bit_buf_t)ReadLE16!(pIn_buf_cur)) << num_bits); pIn_buf_cur += 2; num_bits += 16; }
#endif
						if ((sym2 = (int16)r.m_tables[0].m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
							code_len = sym2 >> 9;
						else
						{
							code_len = TINFL_FAST_LOOKUP_BITS; repeat { sym2 = r.m_tables[0].m_tree[~sym2 + (int32)((bit_buf >> code_len++) & 1)]; } while (sym2 < 0);
						}
						bit_buf >>= code_len; num_bits -= code_len;

						pOut_buf_cur[0] = (uint8)counter;
						if ((sym2 & 256) != 0)
						{
							pOut_buf_cur++;
							counter = sym2;
							// jump to end of inner for(;;)
							r.m_state = 24;
							break;
						}
						pOut_buf_cur[1] = (uint8)sym2;
						pOut_buf_cur += 2;
						// Repeat inner loop
					}

				case 23:
					if (pOut_buf_cur >= pOut_buf_end)
						DoResult!(TinflStatus.HasMoreOutput);
					*pOut_buf_cur++ = (uint8)counter;
					// jump to end of inner for(;;)

					// Repeat inner loop
					r.m_state = 22;

				case 24: // end of inner for(;;)
					if ((counter &= 511) == 256)
					{
						// jump to end of outer for(;;)
						r.m_state = 30;
						break;
					}

					num_extra = (int32)s_length_extra[counter - 257];
					counter = (int32)s_length_base[counter - 257];
					if (num_extra != 0)
						r.m_state = 25;
					else
						r.m_state = 26;
				case 25:
					int32 extra_bits = ?;
					GetBits!(extra_bits, num_extra);
					counter += extra_bits;
					r.m_state = 26;
				case 26:
					HuffDecode!(dist, &r.m_tables[1]);
					num_extra = (int32)s_dist_extra[dist]; dist = (int32)s_dist_base[dist];
					if (num_extra != 0)
						r.m_state = 27;
					else
						r.m_state = 28;
				case 27:
					int32 extra_bits = ?;
					GetBits!(extra_bits, num_extra);
					dist += extra_bits;
					r.m_state = 28;
				case 28:
					dist_from_out_buf_start = (int32)(pOut_buf_cur - pOut_buf_start);
					if ((dist > dist_from_out_buf_start) && (decomp_flags.HasFlag(.UsingNonWrappingOutputBuf)))
					{
						DoResult!(TinflStatus.Failed);
					}

					uint8* pSrc = pOut_buf_start + ((dist_from_out_buf_start - dist) & out_buf_size_mask);
					if ((MAX!(pOut_buf_cur, pSrc) + counter) > pOut_buf_end)
					{
						r.m_state = 29; // Slow copy
						break;
					}
#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES
					else if ((counter >= 9) && (counter <= dist))
					{
						uint8* pSrc_end = pSrc + (counter & ~7);
						repeat
						{
							((uint32*)pOut_buf_cur)[0] = ((uint32*)pSrc)[0];
							((uint32*)pOut_buf_cur)[1] = ((uint32*)pSrc)[1];
							pOut_buf_cur += 8;
						} while ((pSrc += 8) < pSrc_end);
						if ((counter &= 7) < 3)
						{
							if (counter != 0)
							{
								pOut_buf_cur[0] = pSrc[0];
								if (counter > 1)
									pOut_buf_cur[1] = pSrc[1];
								pOut_buf_cur += counter;
							}

							  // jump to start of outer for(;;)
							r.m_state = 22;
							break;
						}
					}
#endif
					repeat
					{
						pOut_buf_cur[0] = pSrc[0];
						pOut_buf_cur[1] = pSrc[1];
						pOut_buf_cur[2] = pSrc[2];
						pOut_buf_cur += 3; pSrc += 3;
					} while ((int32)(counter -= 3) > 2);
					if ((int32)counter > 0)
					{
						pOut_buf_cur[0] = pSrc[0];
						if ((int32)counter > 1)
							pOut_buf_cur[1] = pSrc[1];
						pOut_buf_cur += counter;
					}

					// jump to start of outer for(;;)
					r.m_state = 22;
				case 29:
					if (counter <= 0)
					{
						// jump to start of outer for(;;)
						r.m_state = 22;
						break;
					}

					if (pOut_buf_cur >= pOut_buf_end)
						DoResult!(TinflStatus.HasMoreOutput);

					*pOut_buf_cur++ = pOut_buf_start[(dist_from_out_buf_start++ - dist) & out_buf_size_mask];
					counter--;

				case 30: // End of outer for(;;)
					if ((r.m_final & 1) == 0)
					{
						// Main decode loop
						r.m_state = 3;
						break;
					}

					r.m_state = 31;
				case 31:
					if (decomp_flags.HasFlag(.ParseZlibHeader))
					{
						SkipBits!(num_bits & 7);
						r.m_state = 32;
						counter = 0;
						break;
					}
					else
					{
						r.m_state = 33; // TINFL_FLAG_PARSE_ZLIB_HEADER not set
					}
				case 32:
					uint32 s = ?;
					if (num_bits != 0)
						GetBits!(s, 8);
					else
						GetByte!(s);
					r.m_z_adler32 = (r.m_z_adler32 << 8) | s;
					if (++counter < 4)
						break;
					r.m_state = 33;
				case 33: // Done
					DoResult!(TinflStatus.Done);
				}
			}

			// Save state
			r.m_num_bits = num_bits; r.m_bit_buf = bit_buf; r.m_dist = dist; r.m_counter = counter; r.m_num_extra = num_extra; r.m_dist_from_out_buf_start = dist_from_out_buf_start;
			*pIn_buf_size = pIn_buf_cur - pIn_buf_next; *pOut_buf_size = pOut_buf_cur - pOut_buf_next;
			if ((decomp_flags.HasFlag(.ParseZlibHeader | .ComputeAdler32)) && (status >= default))
			{
				uint8* ptr = pOut_buf_next; int buf_len = *pOut_buf_size;
				int32 i;
				uint32 s1 = r.m_check_adler32 & 0xffff, s2 = r.m_check_adler32 >> 16;
				int block_len = buf_len % 5552;
				while (buf_len > 0)
				{
					for (i = 0; i + 7 < block_len; i += 8,ptr += 8)
					{
						s1 += ptr[0]; s2 += s1; s1 += ptr[1]; s2 += s1; s1 += ptr[2]; s2 += s1; s1 += ptr[3]; s2 += s1;
						s1 += ptr[4]; s2 += s1; s1 += ptr[5]; s2 += s1; s1 += ptr[6]; s2 += s1; s1 += ptr[7]; s2 += s1;
					}
					for (; i < block_len; ++i) { s1 += *ptr++; s2 += s1; }
					s1 %= 65521U; s2 %= 65521U; buf_len -= block_len; block_len = 5552;
				}
				r.m_check_adler32 = (s2 << 16) + s1; if ((status == .Done) && (decomp_flags.HasFlag(.ParseZlibHeader)) && (r.m_check_adler32 != r.m_z_adler32)) status = .Adler32Mismatch;
			}
			return status;
		}

		// Higher level helper functions.
		static void* tinfl_decompress_mem_to_heap(void* pSrc_buf, int src_buf_len, int* pOut_len, TinflFlag flags)
		{
			TinflDecompressor decomp; void* pBuf = null, pNew_buf; int src_buf_ofs = 0, out_buf_capacity = 0;
			*pOut_len = 0;
			tinfl_init!(&decomp);
			for (;;)
			{
				int src_buf_size = src_buf_len - src_buf_ofs, dst_buf_size = out_buf_capacity - *pOut_len, new_out_buf_capacity;
				TinflStatus status = tinfl_decompress(&decomp, (uint8*)pSrc_buf + src_buf_ofs, &src_buf_size, (uint8*)pBuf, (pBuf != null) ? (uint8*)pBuf + *pOut_len : null, &dst_buf_size,
					(flags & ~.HasMoreInput) | .UsingNonWrappingOutputBuf);
				if ((status < default) || (status == .NeedsMoreInput))
				{
					free(pBuf); *pOut_len = 0; return null;
				}
				src_buf_ofs += src_buf_size;
				*pOut_len += dst_buf_size;
				if (status == .Done) break;
				new_out_buf_capacity = out_buf_capacity * 2; if (new_out_buf_capacity < 128) new_out_buf_capacity = 128;
				pNew_buf = realloc(pBuf, new_out_buf_capacity);
				if (pNew_buf == null)
				{
					free(pBuf); *pOut_len = 0; return null;
				}
				pBuf = pNew_buf; out_buf_capacity = new_out_buf_capacity;
			}
			return pBuf;
		}

		static int tinfl_decompress_mem_to_mem(void* pOut_buf, int out_buf_len, void* pSrc_buf, int src_buf_len, TinflFlag flags)
		{
			int outBufLen = out_buf_len;
			int srcBufLen = src_buf_len;
			TinflDecompressor decomp; TinflStatus status; tinfl_init!(&decomp);
			status = tinfl_decompress(&decomp, (uint8*)pSrc_buf, &srcBufLen, (uint8*)pOut_buf, (uint8*)pOut_buf, &outBufLen, (flags & ~.HasMoreInput) | .UsingNonWrappingOutputBuf);
			return (status != .Done) ? -1 : out_buf_len;
		}

		function int tinfl_put_buf_func_ptr(void* pBuf, int len, void* pUser);
		static TinflStatus tinfl_decompress_mem_to_callback(void* pIn_buf, int* pIn_buf_size, tinfl_put_buf_func_ptr pPut_buf_func, void* pPut_buf_user, TinflFlag flags)
		{
			TinflStatus result = .Failed;
			TinflDecompressor decomp;
			uint8* pDict = (uint8*)malloc(TINFL_LZ_DICT_SIZE); int in_buf_ofs = 0, dict_ofs = 0;
			if (pDict == null)
				return .Failed;
			tinfl_init!(&decomp);
			for (;;)
			{
				int in_buf_size = *pIn_buf_size - in_buf_ofs, dst_buf_size = TINFL_LZ_DICT_SIZE - dict_ofs;
				TinflStatus status = tinfl_decompress(&decomp, (uint8*)pIn_buf + in_buf_ofs, &in_buf_size, pDict, pDict + dict_ofs, &dst_buf_size,
					(flags & ~(.HasMoreInput | .UsingNonWrappingOutputBuf)));
				in_buf_ofs += in_buf_size;
				if ((dst_buf_size != 0) && (pPut_buf_func(pDict + dict_ofs, (int)dst_buf_size, pPut_buf_user)) == 0)
					break;
				if (status != .HasMoreOutput)
				{
					result = (status == .Done) ? .Done : .Failed;
					break;
				}
				dict_ofs = (dict_ofs + dst_buf_size) & (TINFL_LZ_DICT_SIZE - 1);
			}
			free(pDict);
			*pIn_buf_size = in_buf_ofs;
			return result;
		}

		// ------------------- Low-level Compression (independent from all decompression API's)
		const uint16[256] s_tdefl_len_sym = uint16[256](
			257, 258, 259, 260, 261, 262, 263, 264, 265, 265, 266, 266, 267, 267, 268, 268, 269, 269, 269, 269, 270, 270, 270, 270, 271, 271, 271, 271, 272, 272, 272, 272,
			273, 273, 273, 273, 273, 273, 273, 273, 274, 274, 274, 274, 274, 274, 274, 274, 275, 275, 275, 275, 275, 275, 275, 275, 276, 276, 276, 276, 276, 276, 276, 276,
			277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278,
			279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280,
			281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281,
			282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282,
			283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283,
			284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 285);

		const uint8[256] s_tdefl_len_extra = uint8[256](
			0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
			4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
			5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
			5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0);

		static uint8[512] s_tdefl_small_dist_sym = uint8[512](
			0, 1, 2, 3, 4, 4, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11,
			11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13,
			13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
			14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14,
			14, 14, 14, 14, 14, 14, 14, 14, 14, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
			15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
			16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
			16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
			16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
			17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
			17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17,
			17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17, 17);

		static uint8[512] s_tdefl_small_dist_extra = uint8[512](
			0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 5, 5,
			5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
			6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
			7, 7, 7, 7, 7, 7, 7, 7);

		static uint8[128] s_tdefl_large_dist_sym = uint8[128](
			0, 0, 18, 19, 20, 20, 21, 21, 22, 22, 22, 22, 23, 23, 23, 23, 24, 24, 24, 24, 24, 24, 24, 24, 25, 25, 25, 25, 25, 25, 25, 25, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26, 26,
			26, 26, 26, 26, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 27, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28,
			28, 28, 28, 28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29);

		static uint8[128] s_tdefl_large_dist_extra = uint8[128](
			0, 0, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 10, 10, 10, 10, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
			12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
			13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13);


		// Radix sorts tdefl_sym_freq[] array by 16-bit key m_key. Returns ptr to sorted values.
		struct tdefl_sym_freq { public uint16 m_key, m_sym_index; };
		static tdefl_sym_freq* tdefl_radix_sort_syms(uint32 num_syms, tdefl_sym_freq* pSyms0, tdefl_sym_freq* pSyms1)
		{
			uint32 total_passes = 2, pass_shift, pass, i;
			uint32[256 * 2] hist; tdefl_sym_freq* pCur_syms = pSyms0, pNew_syms = pSyms1; hist = default;
			for (i = 0; i < num_syms; i++) { uint32 freq = pSyms0[i].m_key; hist[freq & 0xFF]++; hist[256 + ((freq >> 8) & 0xFF)]++; }
			while ((total_passes > 1) && (num_syms == hist[(total_passes - 1) * 256])) total_passes--;
			for (pass_shift = 0,pass = 0; pass < total_passes; pass++,pass_shift += 8)
			{
				uint32* pHist = &hist[pass << 8];
				uint32[256] offsets = ?;
				uint32 cur_ofs = 0;
				for (i = 0; i < 256; i++) { offsets[i] = cur_ofs; cur_ofs += pHist[i]; }
				for (i = 0; i < num_syms; i++) pNew_syms[offsets[(pCur_syms[i].m_key >> pass_shift) & 0xFF]++] = pCur_syms[i];
				{ tdefl_sym_freq* t = pCur_syms; pCur_syms = pNew_syms; pNew_syms = t; }
			}
			return pCur_syms;
		}

		// tdefl_calculate_minimum_redundancy() originally written by: Alistair Moffat, alistair@cs.mu.oz.au, Jyrki Katajainen, jyrki@diku.dk, November 1996.
		static void tdefl_calculate_minimum_redundancy(tdefl_sym_freq* A, int n)
		{
			int root, leaf, next, avbl, used, dpth;
			if (n == 0) return; else if (n == 1) { A[0].m_key = 1; return; }
			A[0].m_key += A[1].m_key; root = 0; leaf = 2;
			for (next = 1; next < n - 1; next++)
			{
				if (leaf >= n || A[root].m_key < A[leaf].m_key) { A[next].m_key = A[root].m_key; A[root++].m_key = (uint16)next; } else A[next].m_key = A[leaf++].m_key;
				if (leaf >= n || (root < next && A[root].m_key < A[leaf].m_key)) { A[next].m_key = (uint16)(A[next].m_key + A[root].m_key); A[root++].m_key = (uint16)next; } else A[next].m_key = (uint16)(A[next].m_key + A[leaf++].m_key);
			}
			A[n - 2].m_key = 0; for (next = n - 3; next >= 0; next--) A[next].m_key = A[A[next].m_key].m_key + 1;
			avbl = 1; used = dpth = 0; root = n - 2; next = n - 1;
			while (avbl > 0)
			{
				while (root >= 0 && (int)A[root].m_key == dpth) { used++; root--; }
				while (avbl > used) { A[next--].m_key = (uint16)(dpth); avbl--; }
				avbl = 2 * used; dpth++; used = 0;
			}
		}

		// Limits canonical Huffman code table's max code size.
		const int TDEFL_MAX_SUPPORTED_HUFF_CODESIZE = 32;
		static void tdefl_huffman_enforce_max_code_size(int32* pNum_codes, int code_list_len, int max_code_size)
		{
			int i; uint32 total = 0; if (code_list_len <= 1) return;
			for (i = max_code_size + 1; i <= TDEFL_MAX_SUPPORTED_HUFF_CODESIZE; i++) pNum_codes[max_code_size] += pNum_codes[i];
			for (i = max_code_size; i > 0; i--) total += (((uint32)pNum_codes[i]) << (max_code_size - i));
			while (total != (1UL << max_code_size))
			{
				pNum_codes[max_code_size]--;
				for (i = max_code_size - 1; i > 0; i--) if (pNum_codes[i] != 0) { pNum_codes[i]--; pNum_codes[i + 1] += 2; break; }
				total--;
			}
		}

		static void tdefl_optimize_huffman_table(tdefl_compressor* d, int table_num, int table_len, int code_size_limit, bool static_table)
		{
			int i, j, l;
			int32[1 + TDEFL_MAX_SUPPORTED_HUFF_CODESIZE] num_codes;
			uint32[TDEFL_MAX_SUPPORTED_HUFF_CODESIZE + 1] next_code;
			num_codes = default;
			if (static_table)
			{
				for (i = 0; i < table_len; i++) num_codes[d.m_huff_code_sizes[table_num][i]]++;
			}
			else
			{
				tdefl_sym_freq[TDEFL_MAX_HUFF_SYMBOLS] syms0, syms1;
				tdefl_sym_freq* pSyms;
				uint32 num_used_syms = 0;
				uint16* pSym_count = &d.m_huff_count[table_num][0];
				for (i = 0; i < table_len; i++) if (pSym_count[i] != 0) { syms0[num_used_syms].m_key = (uint16)pSym_count[i]; syms0[num_used_syms++].m_sym_index = (uint16)i; }

				pSyms = tdefl_radix_sort_syms(num_used_syms, &syms0, &syms1); tdefl_calculate_minimum_redundancy(pSyms, num_used_syms);

				for (i = 0; i < num_used_syms; i++) num_codes[pSyms[i].m_key]++;

				tdefl_huffman_enforce_max_code_size(&num_codes, num_used_syms, code_size_limit);

				d.m_huff_code_sizes[table_num] = default; d.m_huff_codes[table_num] = default;
				for (i = 1,j = num_used_syms; i <= code_size_limit; i++)
					for (l = num_codes[i]; l > 0; l--) d.m_huff_code_sizes[table_num][pSyms[--j].m_sym_index] = (uint8)(i);
			}

			next_code[1] = 0;
			for (j = 0,i = 2; i <= code_size_limit; i++)
			{
				j = ((j + num_codes[i - 1]) << 1);
				next_code[i] = (uint32)j;
			}

			for (i = 0; i < table_len; i++)
			{
				uint32 rev_code = 0, code, code_size; if ((code_size = d.m_huff_code_sizes[table_num][i]) == 0) continue;
				code = next_code[code_size]++; for (l = code_size; l > 0; l--,code >>= 1) rev_code = (rev_code << 1) | (code & 1);
				d.m_huff_codes[table_num][i] = (uint16)rev_code;
			}
		}

		static mixin TDEFL_PUT_BITS(var b, var l, var d)
		{
			uint32 bits = (uint32)b; int32 len = (int32)l; Debug.Assert(bits <= ((1U << len) - 1U));
			d.m_bit_buffer |= (bits << d.m_bits_in); d.m_bits_in += len;
			while (d.m_bits_in >= 8)
			{
				if (d.m_pOutput_buf < d.m_pOutput_buf_end)
					*d.m_pOutput_buf++ = (uint8)(d.m_bit_buffer);
				d.m_bit_buffer >>= 8;
				d.m_bits_in -= 8;
			}
		}

		const uint8[19] s_tdefl_packed_code_size_syms_swizzle = uint8[19](16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15);

		static void tdefl_start_dynamic_block(tdefl_compressor* d)
		{
			uint32 num_lit_codes, num_dist_codes, num_bit_lengths; uint32 i, total_code_sizes_to_pack, num_packed_code_sizes, rle_z_count, rle_repeat_count, packed_code_sizes_index;
			uint8[TDEFL_MAX_HUFF_SYMBOLS_0 + TDEFL_MAX_HUFF_SYMBOLS_1] code_sizes_to_pack = ?;
			uint8[TDEFL_MAX_HUFF_SYMBOLS_0 + TDEFL_MAX_HUFF_SYMBOLS_1] packed_code_sizes = ?;
			uint8 prev_code_size = 0xFF;

			d.m_huff_count[0][256] = 1;

			tdefl_optimize_huffman_table(d, 0, TDEFL_MAX_HUFF_SYMBOLS_0, 15, false);
			tdefl_optimize_huffman_table(d, 1, TDEFL_MAX_HUFF_SYMBOLS_1, 15, false);

			for (num_lit_codes = 286; num_lit_codes > 257; num_lit_codes--) if (d.m_huff_code_sizes[0][num_lit_codes - 1] != 0) break;
			for (num_dist_codes = 30; num_dist_codes > 1; num_dist_codes--) if (d.m_huff_code_sizes[1][num_dist_codes - 1] != 0) break;

			Internal.MemCpy(&code_sizes_to_pack, &d.m_huff_code_sizes[0][0], num_lit_codes);
			Internal.MemCpy(&code_sizes_to_pack[num_lit_codes], &d.m_huff_code_sizes[1][0], num_dist_codes);
			total_code_sizes_to_pack = num_lit_codes + num_dist_codes; num_packed_code_sizes = 0; rle_z_count = 0; rle_repeat_count = 0;

			//memset(&d.m_huff_count[2][0], 0, sizeof(d.m_huff_count[2][0]) * TDEFL_MAX_HUFF_SYMBOLS_2);
			Internal.MemSet(&d.m_huff_count[2][0], 0, sizeof(uint16) * TDEFL_MAX_HUFF_SYMBOLS_2);

			mixin TDEFL_RLE_PREV_CODE_SIZE()
			{
				if (rle_repeat_count != 0)
				{
					if (rle_repeat_count < 3)
					{
						d.m_huff_count[2][prev_code_size] = (uint16)(d.m_huff_count[2][prev_code_size] + rle_repeat_count);
						while (rle_repeat_count-- != 0) packed_code_sizes[num_packed_code_sizes++] = prev_code_size;
					} else
					{
						d.m_huff_count[2][16] = (uint16)(d.m_huff_count[2][16] + 1); packed_code_sizes[num_packed_code_sizes++] = 16; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_repeat_count - 3);
					} rle_repeat_count = 0;
				}
			}

			mixin TDEFL_RLE_ZERO_CODE_SIZE()
			{
				if (rle_z_count != 0)
				{
					if (rle_z_count < 3)
					{
						d.m_huff_count[2][0] = (uint16)(d.m_huff_count[2][0] + rle_z_count); while (rle_z_count-- != 0) packed_code_sizes[num_packed_code_sizes++] = 0;
					}
					else if (rle_z_count <= 10)
					{
						d.m_huff_count[2][17] = (uint16)(d.m_huff_count[2][17] + 1); packed_code_sizes[num_packed_code_sizes++] = 17; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_z_count - 3);
					}
					else
					{
						d.m_huff_count[2][18] = (uint16)(d.m_huff_count[2][18] + 1); packed_code_sizes[num_packed_code_sizes++] = 18; packed_code_sizes[num_packed_code_sizes++] = (uint8)(rle_z_count - 11);
					}
					rle_z_count = 0;
				}
			}

			for (i = 0; i < total_code_sizes_to_pack; i++)
			{
				uint8 code_size = code_sizes_to_pack[i];
				if (code_size == 0)
				{
					TDEFL_RLE_PREV_CODE_SIZE!();
					if (++rle_z_count == 138) { TDEFL_RLE_ZERO_CODE_SIZE!(); }
				}
				else
				{
					TDEFL_RLE_ZERO_CODE_SIZE!();
					if (code_size != prev_code_size)
					{
						TDEFL_RLE_PREV_CODE_SIZE!();
						d.m_huff_count[2][code_size] = (uint16)(d.m_huff_count[2][code_size] + 1); packed_code_sizes[num_packed_code_sizes++] = code_size;
					}
					else if (++rle_repeat_count == 6)
					{
						TDEFL_RLE_PREV_CODE_SIZE!();
					}
				}
				prev_code_size = code_size;
			}
			if (rle_repeat_count != 0) { TDEFL_RLE_PREV_CODE_SIZE!(); } else { TDEFL_RLE_ZERO_CODE_SIZE!(); }

			tdefl_optimize_huffman_table(d, 2, TDEFL_MAX_HUFF_SYMBOLS_2, 7, false);

			TDEFL_PUT_BITS!(2, 2, d);

			TDEFL_PUT_BITS!(num_lit_codes - 257, 5, d);
			TDEFL_PUT_BITS!(num_dist_codes - 1, 5, d);

			for (num_bit_lengths = 18; num_bit_lengths >= 0; num_bit_lengths--) if (d.m_huff_code_sizes[2][s_tdefl_packed_code_size_syms_swizzle[num_bit_lengths]] != 0) break;
			num_bit_lengths = Math.Max(4, (num_bit_lengths + 1)); TDEFL_PUT_BITS!(num_bit_lengths - 4, 4, d);
			for (i = 0; (int)i < num_bit_lengths; i++) TDEFL_PUT_BITS!(d.m_huff_code_sizes[2][s_tdefl_packed_code_size_syms_swizzle[i]], 3, d);

			for (packed_code_sizes_index = 0; packed_code_sizes_index < num_packed_code_sizes;)
			{
				uint32 code = packed_code_sizes[packed_code_sizes_index++]; Debug.Assert(code < TDEFL_MAX_HUFF_SYMBOLS_2);
				TDEFL_PUT_BITS!(d.m_huff_codes[2][code], d.m_huff_code_sizes[2][code], d);
				if (code >= 16)
				{
					switch (code - 16)
					{
					case 0:
						TDEFL_PUT_BITS!(packed_code_sizes[packed_code_sizes_index++], 2, d);
					case 1:
						TDEFL_PUT_BITS!(packed_code_sizes[packed_code_sizes_index++], 3, d);
					case 2:
						TDEFL_PUT_BITS!(packed_code_sizes[packed_code_sizes_index++], 7, d);
					}
				}
			}
		}

		static void tdefl_start_static_block(tdefl_compressor* d)
		{
			uint32 i;
			uint8* p = &d.m_huff_code_sizes[0][0];

			for (i = 0; i <= 143; ++i) *p++ = 8;
			for (; i <= 255; ++i) *p++ = 9;
			for (; i <= 279; ++i) *p++ = 7;
			for (; i <= 287; ++i) *p++ = 8;

			Internal.MemSet(&d.m_huff_code_sizes[1], 5, 32);

			tdefl_optimize_huffman_table(d, 0, 288, 15, true);
			tdefl_optimize_huffman_table(d, 1, 32, 15, true);

			TDEFL_PUT_BITS!(1, 2, d);
		}

		const uint32[17] bitmasks = uint32[17](0x0000, 0x0001, 0x0003, 0x0007, 0x000F, 0x001F, 0x003F, 0x007F, 0x00FF, 0x01FF, 0x03FF, 0x07FF, 0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF);

		#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN && MINIZ_HAS_64BIT_REGISTERS
		static bool tdefl_compress_lz_codes(tdefl_compressor* d)
		{
			uint32 flags;
			uint8* pLZ_codes;
			uint8* pOutput_buf = d.m_pOutput_buf;
			uint8* pLZ_code_buf_end = d.m_pLZ_code_buf;
			uint64 bit_buffer = d.m_bit_buffer;
			int32 bits_in = d.m_bits_in;

			mixin TDEFL_PUT_BITS_FAST(var b, var l)
			{
				bit_buffer |= (((uint64)(b)) << bits_in); bits_in += (l);
			}

			flags = 1;
			for (pLZ_codes = &d.m_lz_code_buf; pLZ_codes < pLZ_code_buf_end; flags >>= 1)
			{
				if (flags == 1)
					flags = (uint32)(*pLZ_codes++ | 0x100);

				if ((flags & 1) != 0)
				{
					int32 s0, s1, n0, n1, sym, num_extra_bits;
					uint32 match_len = pLZ_codes[0], match_dist = *(uint16*)(pLZ_codes + 1); pLZ_codes += 3;

					Debug.Assert(d.m_huff_code_sizes[0][s_tdefl_len_sym[match_len]] != 0);
					TDEFL_PUT_BITS_FAST!(d.m_huff_codes[0][s_tdefl_len_sym[match_len]], d.m_huff_code_sizes[0][s_tdefl_len_sym[match_len]]);
					TDEFL_PUT_BITS_FAST!(match_len & bitmasks[s_tdefl_len_extra[match_len]], s_tdefl_len_extra[match_len]);

					// This sequence coaxes MSVC into using cmov's vs. jmp's.
					s0 = s_tdefl_small_dist_sym[match_dist & 511];
					n0 = s_tdefl_small_dist_extra[match_dist & 511];
					s1 = s_tdefl_large_dist_sym[match_dist >> 8];
					n1 = s_tdefl_large_dist_extra[match_dist >> 8];
					sym = (match_dist < 512) ? s0 : s1;
					num_extra_bits = (match_dist < 512) ? n0 : n1;

					Debug.Assert(d.m_huff_code_sizes[1][sym] != 0);
					TDEFL_PUT_BITS_FAST!(d.m_huff_codes[1][sym], d.m_huff_code_sizes[1][sym]);
					TDEFL_PUT_BITS_FAST!(match_dist & bitmasks[num_extra_bits], num_extra_bits);
				}
				else
				{
					uint32 lit = *pLZ_codes++;
					Debug.Assert(d.m_huff_code_sizes[0][lit] != 0);
					TDEFL_PUT_BITS_FAST!(d.m_huff_codes[0][lit], d.m_huff_code_sizes[0][lit]);

					if (((flags & 2) == 0) && (pLZ_codes < pLZ_code_buf_end))
					{
						flags >>= 1;
						lit = *pLZ_codes++;
						Debug.Assert(d.m_huff_code_sizes[0][lit] != 0);
						TDEFL_PUT_BITS_FAST!(d.m_huff_codes[0][lit], d.m_huff_code_sizes[0][lit]);

						if (((flags & 2) == 0) && (pLZ_codes < pLZ_code_buf_end))
						{
							flags >>= 1;
							lit = *pLZ_codes++;
							Debug.Assert(d.m_huff_code_sizes[0][lit] != 0);
							TDEFL_PUT_BITS_FAST!(d.m_huff_codes[0][lit], d.m_huff_code_sizes[0][lit]);
						}
					}
				}

				if (pOutput_buf >= d.m_pOutput_buf_end)
					return false;

				*(uint64*)pOutput_buf = bit_buffer;
				pOutput_buf += (bits_in >> 3);
				bit_buffer >>= (bits_in & ~7);
				bits_in &= 7;
			}



			d.m_pOutput_buf = pOutput_buf;
			d.m_bits_in = 0;
			d.m_bit_buffer = 0;

			while (bits_in != 0)
			{
				int32 n = Math.Min(bits_in, 16);
				TDEFL_PUT_BITS!((uint)bit_buffer & bitmasks[n], n, d);
				bit_buffer >>= n;
				bits_in -= n;
			}

			TDEFL_PUT_BITS!(d.m_huff_codes[0][256], d.m_huff_code_sizes[0][256], d);

			return (d.m_pOutput_buf < d.m_pOutput_buf_end);
		}
#else
		static bool tdefl_compress_lz_codes(tdefl_compressor *d)
		{
		  uint32 flags;
		  uint8 *pLZ_codes;

		  flags = 1;
		  for (pLZ_codes = &d.m_lz_code_buf; pLZ_codes < d.m_pLZ_code_buf; flags >>= 1)
		  {
			if (flags == 1)
			  flags = (uint32)(*pLZ_codes++ | 0x100);
			if ((flags & 1) != 0)
			{
			  uint32 sym;
			  uint32 num_extra_bits = ?;
			  uint32 match_len = pLZ_codes[0], match_dist = (pLZ_codes[1] | (pLZ_codes[2] << 8)); pLZ_codes += 3;

			  Debug.Assert(d.m_huff_code_sizes[0][s_tdefl_len_sym[match_len]] != 0);
			  TDEFL_PUT_BITS!(d.m_huff_codes[0][s_tdefl_len_sym[match_len]], d.m_huff_code_sizes[0][s_tdefl_len_sym[match_len]], d);
			  TDEFL_PUT_BITS!(match_len & bitmasks[s_tdefl_len_extra[match_len]], s_tdefl_len_extra[match_len], d);

			  if (match_dist < 512)
			  {
				sym = s_tdefl_small_dist_sym[match_dist]; num_extra_bits = s_tdefl_small_dist_extra[match_dist];
			  }
			  else
			  {
				sym = s_tdefl_large_dist_sym[match_dist >> 8]; num_extra_bits = s_tdefl_large_dist_extra[match_dist >> 8];
			  }
			  Debug.Assert(d.m_huff_code_sizes[1][sym] != 0);
			  TDEFL_PUT_BITS!(d.m_huff_codes[1][sym], d.m_huff_code_sizes[1][sym], d);
			  TDEFL_PUT_BITS!(match_dist & bitmasks[num_extra_bits], num_extra_bits, d);
			}
			else
			{
			  uint32 lit = *pLZ_codes++;
			  Debug.Assert(d.m_huff_code_sizes[0][lit] != 0);
			  TDEFL_PUT_BITS!(d.m_huff_codes[0][lit], d.m_huff_code_sizes[0][lit], d);
			}
		  }

		  TDEFL_PUT_BITS!(d.m_huff_codes[0][256], d.m_huff_code_sizes[0][256], d);

		  return (d.m_pOutput_buf < d.m_pOutput_buf_end);
		}
#endif // MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN && MINIZ_HAS_64BIT_REGISTERS

		static bool tdefl_compress_block(tdefl_compressor* d, bool static_block)
		{
			if (static_block)
				tdefl_start_static_block(d);
			else
				tdefl_start_dynamic_block(d);
			return tdefl_compress_lz_codes(d);
		}

		static int32 tdefl_flush_block(tdefl_compressor* d, TdeflFlush flush)
		{
			uint32 saved_bit_buf;
			int32 saved_bits_in;
			uint8* pSaved_output_buf;
			bool comp_block_succeeded = false;
			int32 n = ?;
			bool use_raw_block = (d.m_flags.HasFlag(.TDEFL_FORCE_ALL_RAW_BLOCKS)) && ((d.m_lookahead_pos - d.m_lz_code_buf_dict_pos) <= d.m_dict_size);
			uint8* pOutput_buf_start = ((d.m_pPut_buf_func == null) && ((*d.m_pOut_buf_size - d.m_out_buf_ofs) >= TDEFL_OUT_BUF_SIZE)) ? ((uint8*)d.m_pOut_buf + d.m_out_buf_ofs) : &d.m_output_buf;

			d.m_pOutput_buf = pOutput_buf_start;
			d.m_pOutput_buf_end = d.m_pOutput_buf + TDEFL_OUT_BUF_SIZE - 16;

			Debug.Assert(d.m_output_flush_remaining == 0);
			d.m_output_flush_ofs = 0;
			d.m_output_flush_remaining = 0;

			*d.m_pLZ_flags = (uint8)(*d.m_pLZ_flags >> d.m_num_flags_left);
			d.m_pLZ_code_buf -= (d.m_num_flags_left == 8) ? 1 : 0;

			if ((d.m_flags.HasFlag(.TDEFL_WRITE_ZLIB_HEADER)) && (d.m_block_index == 0))
			{
				TDEFL_PUT_BITS!(0x78, 8, d); TDEFL_PUT_BITS!(0x01, 8, d);
			}

			TDEFL_PUT_BITS!((flush == .TDEFL_FINISH) ? 1 : 0, 1, d);

			pSaved_output_buf = d.m_pOutput_buf; saved_bit_buf = d.m_bit_buffer; saved_bits_in = d.m_bits_in;

			if (!use_raw_block)
				comp_block_succeeded = tdefl_compress_block(d, (d.m_flags.HasFlag(.TDEFL_FORCE_ALL_STATIC_BLOCKS)) || (d.m_total_lz_bytes < 48));

			// If the block gets expanded, forget the current contents of the output buffer and send a raw block instead.
			if (((use_raw_block) || ((d.m_total_lz_bytes != 0) && ((d.m_pOutput_buf - pSaved_output_buf + 1U) >= d.m_total_lz_bytes))) &&
				((d.m_lookahead_pos - d.m_lz_code_buf_dict_pos) <= d.m_dict_size))
			{
				int32 i; d.m_pOutput_buf = pSaved_output_buf; d.m_bit_buffer = saved_bit_buf; d.m_bits_in = saved_bits_in;
				TDEFL_PUT_BITS!(0, 2, d);
				if (d.m_bits_in != 0) { TDEFL_PUT_BITS!(0, 8 - d.m_bits_in, d); }
				for (i = 2; i != 0; --i,d.m_total_lz_bytes ^= 0xFFFF)
				{
					TDEFL_PUT_BITS!(d.m_total_lz_bytes & 0xFFFF, 16, d);
				}
				for (i = 0; i < d.m_total_lz_bytes; ++i)
				{
					TDEFL_PUT_BITS!(d.m_dict[(d.m_lz_code_buf_dict_pos + i) & TDEFL_LZ_DICT_SIZE_MASK], 8, d);
				}
			}
			// Check for the extremely unlikely (if not impossible) case of the compressed block not fitting into the output buffer when using dynamic codes.
			else if (!comp_block_succeeded)
			{
				d.m_pOutput_buf = pSaved_output_buf; d.m_bit_buffer = saved_bit_buf; d.m_bits_in = saved_bits_in;
				tdefl_compress_block(d, true);
			}

			if (flush != 0)
			{
				if (flush == .TDEFL_FINISH)
				{
					if (d.m_bits_in != 0) { TDEFL_PUT_BITS!(0, 8 - d.m_bits_in, d); }
					if (d.m_flags.HasFlag(.TDEFL_WRITE_ZLIB_HEADER)) { uint32 i, a = d.m_adler32; for (i = 0; i < 4; i++) { TDEFL_PUT_BITS!((a >> 24) & 0xFF, 8, d); a <<= 8; } }
				}
				else
				{
					uint32 i, z = 0; TDEFL_PUT_BITS!(0, 3, d); if (d.m_bits_in != 0) { TDEFL_PUT_BITS!(0, 8 - d.m_bits_in, d); } for (i = 2; i != 0; --i,z ^= 0xFFFF) { TDEFL_PUT_BITS!(z & 0xFFFF, 16, d); }
				}
			}

			Debug.Assert(d.m_pOutput_buf < d.m_pOutput_buf_end);

			Internal.MemSet(&d.m_huff_count[0][0], 0, sizeof(uint16) * TDEFL_MAX_HUFF_SYMBOLS_0);
			Internal.MemSet(&d.m_huff_count[1][0], 0, sizeof(uint16) * TDEFL_MAX_HUFF_SYMBOLS_1);

			d.m_pLZ_code_buf = &d.m_lz_code_buf + 1; d.m_pLZ_flags = &d.m_lz_code_buf; d.m_num_flags_left = 8; d.m_lz_code_buf_dict_pos += d.m_total_lz_bytes; d.m_total_lz_bytes = 0; d.m_block_index++;

			if ((n = (int32)(d.m_pOutput_buf - pOutput_buf_start)) != 0)
			{
				if (d.m_pPut_buf_func != 0)
				{
					*d.m_pIn_buf_size = d.m_pSrc - (uint8*)d.m_pIn_buf;
					if (!d.m_pPut_buf_func(&d.m_output_buf, n, d.m_pPut_buf_user))
						return (int)(d.m_prev_return_status = .TDEFL_STATUS_PUT_BUF_FAILED);
				}
				else if (pOutput_buf_start == (void*)&d.m_output_buf)
				{
					int32 bytes_to_copy = (int32)Math.Min((int32)n, (int32)(*d.m_pOut_buf_size - d.m_out_buf_ofs));
					Internal.MemCpy((uint8*)d.m_pOut_buf + d.m_out_buf_ofs, &d.m_output_buf, bytes_to_copy);
					d.m_out_buf_ofs += bytes_to_copy;
					if ((n -= bytes_to_copy) != 0)
					{
						d.m_output_flush_ofs = (int32)bytes_to_copy;
						d.m_output_flush_remaining = (int32)n;
					}
				}
				else
				{
					d.m_out_buf_ofs += n;
				}
			}

			return d.m_output_flush_remaining;
		}

		#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES
		static mixin TDEFL_READ_UNALIGNED_WORD(var p) { *(uint16*)(p) }

		static void tdefl_find_match(tdefl_compressor* d, int32 lookahead_pos, int32 max_dist, int32 max_match_len, int32* pMatch_dist, int32* pMatch_len)
		{
			int32 dist = ?, pos = lookahead_pos & TDEFL_LZ_DICT_SIZE_MASK, match_len = *pMatch_len, probe_pos = pos, next_probe_pos, probe_len;
			uint32 num_probes_left = d.m_max_probes[(match_len >= 32) ? 1 : 0];
			uint16* s = (uint16*)(&d.m_dict[pos]), p, q;
			uint16 c01 = TDEFL_READ_UNALIGNED_WORD!(&d.m_dict[pos + match_len - 1]), s01 = TDEFL_READ_UNALIGNED_WORD!(s);
			Debug.Assert(max_match_len <= TDEFL_MAX_MATCH_LEN); if (max_match_len <= match_len) return;
			for (;;)
			{
				for (;;)
				{
					if (--num_probes_left == 0) return;
					mixin TDEFL_PROBE()
					{
						next_probe_pos = d.m_next[probe_pos];
						if ((next_probe_pos == 0) || ((dist = (uint16)(lookahead_pos - next_probe_pos)) > max_dist)) return;
						probe_pos = next_probe_pos & TDEFL_LZ_DICT_SIZE_MASK;
						if (TDEFL_READ_UNALIGNED_WORD!(&d.m_dict[probe_pos + match_len - 1]) == c01) break;
					}

					TDEFL_PROBE!(); TDEFL_PROBE!(); TDEFL_PROBE!();
				}
				if (dist == 0) break; q = (uint16*)(&d.m_dict[probe_pos]); if (TDEFL_READ_UNALIGNED_WORD!(q) != s01) continue; p = s; probe_len = 32;
				repeat { } while ((TDEFL_READ_UNALIGNED_WORD!(++p) == TDEFL_READ_UNALIGNED_WORD!(++q)) && (TDEFL_READ_UNALIGNED_WORD!(++p) == TDEFL_READ_UNALIGNED_WORD!(++q)) &&
					(TDEFL_READ_UNALIGNED_WORD!(++p) == TDEFL_READ_UNALIGNED_WORD!(++q)) && (TDEFL_READ_UNALIGNED_WORD!(++p) == TDEFL_READ_UNALIGNED_WORD!(++q)) && (--probe_len > 0));
				if (probe_len == 0)
				{
					*pMatch_dist = dist; *pMatch_len = Math.Min(max_match_len, TDEFL_MAX_MATCH_LEN); break;
				}
				else if ((probe_len = ((int32)(p - s) * 2) + (int32)((*(uint8*)p == *(uint8*)q) ? 1 : 0)) > match_len)
				{
					*pMatch_dist = dist; if ((*pMatch_len = match_len = Math.Min(max_match_len, probe_len)) == max_match_len) break;
					c01 = TDEFL_READ_UNALIGNED_WORD!(&d.m_dict[pos + match_len - 1]);
				}
			}
		}
#else
		static void tdefl_find_match(tdefl_compressor* d, uint32 lookahead_pos, uint32 max_dist, uint32 max_match_len, uint32* pMatch_dist, uint32* pMatch_len)
		{
		  uint32 dist = ?, pos = lookahead_pos & TDEFL_LZ_DICT_SIZE_MASK, match_len = *pMatch_len, probe_pos = pos, next_probe_pos, probe_len;
		  uint32 num_probes_left = d.m_max_probes[(match_len >= 32) ? 1 : 0];
		  uint8* s = &d.m_dict[pos];
		  uint8* p, q;
		  uint8 c0 = d.m_dict[pos + match_len], c1 = d.m_dict[pos + match_len - 1];
		  Debug.Assert(max_match_len <= TDEFL_MAX_MATCH_LEN); if (max_match_len <= match_len) return;
		  for ( ; ; )
		  {
			for ( ; ; )
			{
			  if (--num_probes_left == 0) return;
			  mixin TDEFL_PROBE()
				{
					next_probe_pos = d.m_next[probe_pos];
					if ((next_probe_pos == 0) || ((dist = (uint16)(lookahead_pos - next_probe_pos)) > max_dist)) return;
					probe_pos = next_probe_pos & TDEFL_LZ_DICT_SIZE_MASK;
					if ((d.m_dict[probe_pos + match_len] == c0) && (d.m_dict[probe_pos + match_len - 1] == c1)) break;
				}
			  TDEFL_PROBE!(); TDEFL_PROBE!(); TDEFL_PROBE!();
			}
			if (dist == 0) break;
			p = s; q = &d.m_dict[probe_pos];
			for (probe_len = 0; probe_len < max_match_len; probe_len++)
				if (*p++ != *q++)
					break;
			if (probe_len > match_len)
			{
			  *pMatch_dist = dist; if ((*pMatch_len = match_len = probe_len) == max_match_len) return;
			  c0 = d.m_dict[pos + match_len]; c1 = d.m_dict[pos + match_len - 1];
			}
		  }
		}
#endif // #if MINIZ_USE_UNALIGNED_LOADS_AND_STORES

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN
		static bool tdefl_compress_fast(tdefl_compressor* d)
		{
		  // Faster, minimally featured LZRW1-style match+parse loop with better register utilization. Intended for applications where raw throughput is valued more highly than ratio.
			int32 lookahead_pos = (int32)d.m_lookahead_pos, lookahead_size = (int32)d.m_lookahead_size, dict_size = (int32)d.m_dict_size, total_lz_bytes = (int32)d.m_total_lz_bytes, num_flags_left = (int32)d.m_num_flags_left;
			uint8* pLZ_code_buf = d.m_pLZ_code_buf, pLZ_flags = d.m_pLZ_flags;
			int32 cur_pos = lookahead_pos & TDEFL_LZ_DICT_SIZE_MASK;

			while ((d.m_src_buf_left != 0) || ((d.m_flush != 0) && (lookahead_size != 0)))
			{
				int32 TDEFL_COMP_FAST_LOOKAHEAD_SIZE = 4096;
				int32 dst_pos = (lookahead_pos + lookahead_size) & TDEFL_LZ_DICT_SIZE_MASK;
				int32 num_bytes_to_process = (int32)Math.Min(d.m_src_buf_left, TDEFL_COMP_FAST_LOOKAHEAD_SIZE - lookahead_size);
				d.m_src_buf_left -= num_bytes_to_process;
				lookahead_size += num_bytes_to_process;

				while (num_bytes_to_process != 0)
				{
					int32 n = Math.Min(TDEFL_LZ_DICT_SIZE - dst_pos, num_bytes_to_process);
					Internal.MemCpy(&d.m_dict[dst_pos], d.m_pSrc, n);
					if (dst_pos < (TDEFL_MAX_MATCH_LEN - 1))
						Internal.MemCpy(&d.m_dict[TDEFL_LZ_DICT_SIZE + dst_pos], d.m_pSrc, Math.Min(n, (TDEFL_MAX_MATCH_LEN - 1) - dst_pos));
					d.m_pSrc += n;
					dst_pos = (dst_pos + n) & TDEFL_LZ_DICT_SIZE_MASK;
					num_bytes_to_process -= n;
				}

				dict_size = Math.Min(TDEFL_LZ_DICT_SIZE - lookahead_size, dict_size);
				if ((d.m_flush == 0) && (lookahead_size < TDEFL_COMP_FAST_LOOKAHEAD_SIZE)) break;

				while (lookahead_size >= 4)
				{
					int32 cur_match_dist, cur_match_len = 1;
					uint8* pCur_dict = &d.m_dict[cur_pos];
					uint32 first_trigram = (*(uint32*)pCur_dict) & 0xFFFFFF;
					uint32 hash = (first_trigram ^ (first_trigram >> (24 - (TDEFL_LZ_HASH_BITS - 8)))) & TDEFL_LEVEL1_HASH_SIZE_MASK;
					int32 probe_pos = d.m_hash[hash];
					d.m_hash[hash] = (uint16)lookahead_pos;

					if (((cur_match_dist = (uint16)(lookahead_pos - probe_pos)) <= dict_size) && ((*(uint32*)(&d.m_dict[(probe_pos &= TDEFL_LZ_DICT_SIZE_MASK)]) & 0xFFFFFF) == first_trigram))
					{
						uint16* p = (uint16*)pCur_dict;
						uint16* q = (uint16*)(&d.m_dict[probe_pos]);
						uint32 probe_len = 32;
						repeat { } while ((TDEFL_READ_UNALIGNED_WORD!(++p) == TDEFL_READ_UNALIGNED_WORD!(++q)) && (TDEFL_READ_UNALIGNED_WORD!(++p) == TDEFL_READ_UNALIGNED_WORD!(++q)) &&
							(TDEFL_READ_UNALIGNED_WORD!(++p) == TDEFL_READ_UNALIGNED_WORD!(++q)) && (TDEFL_READ_UNALIGNED_WORD!(++p) == TDEFL_READ_UNALIGNED_WORD!(++q)) && (--probe_len > 0));
						cur_match_len = ((int32)(p - (uint16*)pCur_dict) * 2) + (int32)((*(uint8*)p == *(uint8*)q) ? 1 : 0);
						if (probe_len != 0)
							cur_match_len = (cur_match_dist != 0) ? TDEFL_MAX_MATCH_LEN : 0;

						if ((cur_match_len < TDEFL_MIN_MATCH_LEN) || ((cur_match_len == TDEFL_MIN_MATCH_LEN) && (cur_match_dist >= 8U * 1024U)))
						{
							cur_match_len = 1;
							*pLZ_code_buf++ = (uint8)first_trigram;
							*pLZ_flags = (uint8)(*pLZ_flags >> 1);
							d.m_huff_count[0][(uint8)first_trigram]++;
						}
						else
						{
							uint32 s0, s1;
							cur_match_len = Math.Min(cur_match_len, lookahead_size);

							Debug.Assert((cur_match_len >= TDEFL_MIN_MATCH_LEN) && (cur_match_dist >= 1) && (cur_match_dist <= TDEFL_LZ_DICT_SIZE));

							cur_match_dist--;

							pLZ_code_buf[0] = (uint8)(cur_match_len - TDEFL_MIN_MATCH_LEN);
							*(uint16*)(&pLZ_code_buf[1]) = (uint16)cur_match_dist;
							pLZ_code_buf += 3;
							*pLZ_flags = (uint8)((*pLZ_flags >> 1) | 0x80);

							s0 = s_tdefl_small_dist_sym[cur_match_dist & 511];
							s1 = s_tdefl_large_dist_sym[cur_match_dist >> 8];
							d.m_huff_count[1][(cur_match_dist < 512) ? s0 : s1]++;

							d.m_huff_count[0][s_tdefl_len_sym[cur_match_len - TDEFL_MIN_MATCH_LEN]]++;
						}
					}
					else
					{
						*pLZ_code_buf++ = (uint8)first_trigram;
						*pLZ_flags = (uint8)(*pLZ_flags >> 1);
						d.m_huff_count[0][(uint8)first_trigram]++;
					}

					if (--num_flags_left == 0) { num_flags_left = 8; pLZ_flags = pLZ_code_buf++; }

					total_lz_bytes += cur_match_len;
					lookahead_pos += cur_match_len;
					dict_size = Math.Min(dict_size + cur_match_len, TDEFL_LZ_DICT_SIZE);
					cur_pos = (cur_pos + cur_match_len) & TDEFL_LZ_DICT_SIZE_MASK;
					Debug.Assert(lookahead_size >= cur_match_len);
					lookahead_size -= cur_match_len;

					if (pLZ_code_buf > &d.m_lz_code_buf[TDEFL_LZ_CODE_BUF_SIZE - 8])
					{
						int32 n;
						d.m_lookahead_pos = lookahead_pos; d.m_lookahead_size = lookahead_size; d.m_dict_size = dict_size;
						d.m_total_lz_bytes = total_lz_bytes; d.m_pLZ_code_buf = pLZ_code_buf; d.m_pLZ_flags = pLZ_flags; d.m_num_flags_left = num_flags_left;
						if ((n = tdefl_flush_block(d, default)) != 0)
							return (n < 0) ? false : true;
						total_lz_bytes = d.m_total_lz_bytes; pLZ_code_buf = d.m_pLZ_code_buf; pLZ_flags = d.m_pLZ_flags; num_flags_left = d.m_num_flags_left;
					}
				}

				while (lookahead_size != 0)
				{
					uint8 lit = d.m_dict[cur_pos];

					total_lz_bytes++;
					*pLZ_code_buf++ = lit;
					*pLZ_flags = (uint8)(*pLZ_flags >> 1);
					if (--num_flags_left == 0) { num_flags_left = 8; pLZ_flags = pLZ_code_buf++; }

					d.m_huff_count[0][lit]++;

					lookahead_pos++;
					dict_size = Math.Min(dict_size + 1, TDEFL_LZ_DICT_SIZE);
					cur_pos = (cur_pos + 1) & TDEFL_LZ_DICT_SIZE_MASK;
					lookahead_size--;

					if (pLZ_code_buf > &d.m_lz_code_buf[TDEFL_LZ_CODE_BUF_SIZE - 8])
					{
						int n;
						d.m_lookahead_pos = lookahead_pos; d.m_lookahead_size = lookahead_size; d.m_dict_size = dict_size;
						d.m_total_lz_bytes = total_lz_bytes; d.m_pLZ_code_buf = pLZ_code_buf; d.m_pLZ_flags = pLZ_flags; d.m_num_flags_left = num_flags_left;
						if ((n = tdefl_flush_block(d, default)) != 0)
							return (n < 0) ? false : true;
						total_lz_bytes = d.m_total_lz_bytes; pLZ_code_buf = d.m_pLZ_code_buf; pLZ_flags = d.m_pLZ_flags; num_flags_left = d.m_num_flags_left;
					}
				}
			}

			d.m_lookahead_pos = lookahead_pos; d.m_lookahead_size = lookahead_size; d.m_dict_size = dict_size;
			d.m_total_lz_bytes = total_lz_bytes; d.m_pLZ_code_buf = pLZ_code_buf; d.m_pLZ_flags = pLZ_flags; d.m_num_flags_left = num_flags_left;
			return true;
		}
#endif // MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN

		static void tdefl_record_literal(tdefl_compressor* d, uint8 lit)
		{
			d.m_total_lz_bytes++;
			*d.m_pLZ_code_buf++ = lit;
			*d.m_pLZ_flags = (uint8)(*d.m_pLZ_flags >> 1);
			if (--d.m_num_flags_left == 0) { d.m_num_flags_left = 8; d.m_pLZ_flags = d.m_pLZ_code_buf++; }
			d.m_huff_count[0][lit]++;
		}

		static void tdefl_record_match(tdefl_compressor* d, int32 match_len, int32 match_distIn)
		{
			int32 s0, s1;
			int32 match_dist = match_distIn;

			Debug.Assert((match_len >= TDEFL_MIN_MATCH_LEN) && (match_dist >= 1) && (match_dist <= TDEFL_LZ_DICT_SIZE));

			d.m_total_lz_bytes += match_len;

			d.m_pLZ_code_buf[0] = (uint8)(match_len - TDEFL_MIN_MATCH_LEN);

			match_dist -= 1;
			d.m_pLZ_code_buf[1] = (uint8)(match_dist & 0xFF);
			d.m_pLZ_code_buf[2] = (uint8)(match_dist >> 8); d.m_pLZ_code_buf += 3;

			*d.m_pLZ_flags = (uint8)((*d.m_pLZ_flags >> 1) | 0x80); if (--d.m_num_flags_left == 0) { d.m_num_flags_left = 8; d.m_pLZ_flags = d.m_pLZ_code_buf++; }

			s0 = s_tdefl_small_dist_sym[match_dist & 511]; s1 = s_tdefl_large_dist_sym[(match_dist >> 8) & 127];
			d.m_huff_count[1][(match_dist < 512) ? s0 : s1]++;

			if (match_len >= TDEFL_MIN_MATCH_LEN) d.m_huff_count[0][s_tdefl_len_sym[match_len - TDEFL_MIN_MATCH_LEN]]++;
		}

		static bool tdefl_compress_normal(tdefl_compressor* d)
		{
			uint8* pSrc = d.m_pSrc; int32 src_buf_left = d.m_src_buf_left;
			TdeflFlush flush = d.m_flush;

			while ((src_buf_left != 0) || ((flush != 0) && (d.m_lookahead_size != 0)))
			{
				int32 len_to_move, cur_match_dist, cur_match_len, cur_pos;
				// Update dictionary and hash chains. Keeps the lookahead size equal to TDEFL_MAX_MATCH_LEN.
				if ((d.m_lookahead_size + d.m_dict_size) >= (TDEFL_MIN_MATCH_LEN - 1))
				{
					int32 dst_pos = (d.m_lookahead_pos + d.m_lookahead_size) & TDEFL_LZ_DICT_SIZE_MASK, ins_pos = d.m_lookahead_pos + d.m_lookahead_size - 2;
					uint32 hash = ((uint32)d.m_dict[ins_pos & TDEFL_LZ_DICT_SIZE_MASK] << TDEFL_LZ_HASH_SHIFT) ^ d.m_dict[(ins_pos + 1) & TDEFL_LZ_DICT_SIZE_MASK];
					int32 num_bytes_to_process = (int32)Math.Min(src_buf_left, TDEFL_MAX_MATCH_LEN - d.m_lookahead_size);
					uint8* pSrc_end = pSrc + num_bytes_to_process;
					src_buf_left -= num_bytes_to_process;
					d.m_lookahead_size += num_bytes_to_process;
					while (pSrc != pSrc_end)
					{
						uint8 c = *pSrc++; d.m_dict[dst_pos] = c; if (dst_pos < (TDEFL_MAX_MATCH_LEN - 1)) d.m_dict[TDEFL_LZ_DICT_SIZE + dst_pos] = c;
						hash = ((hash << TDEFL_LZ_HASH_SHIFT) ^ c) & (TDEFL_LZ_HASH_SIZE - 1);
						d.m_next[ins_pos & TDEFL_LZ_DICT_SIZE_MASK] = d.m_hash[hash]; d.m_hash[hash] = (uint16)(ins_pos);
						dst_pos = (dst_pos + 1) & TDEFL_LZ_DICT_SIZE_MASK; ins_pos++;
					}
				}
				else
				{
					while ((src_buf_left != 0) && (d.m_lookahead_size < TDEFL_MAX_MATCH_LEN))
					{
						uint8 c = *pSrc++;
						int32 dst_pos = (d.m_lookahead_pos + d.m_lookahead_size) & TDEFL_LZ_DICT_SIZE_MASK;
						src_buf_left--;
						d.m_dict[dst_pos] = c;
						if (dst_pos < (TDEFL_MAX_MATCH_LEN - 1))
							d.m_dict[TDEFL_LZ_DICT_SIZE + dst_pos] = c;
						if ((++d.m_lookahead_size + d.m_dict_size) >= TDEFL_MIN_MATCH_LEN)
						{
							int32 ins_pos = d.m_lookahead_pos + (d.m_lookahead_size - 1) - 2;
							uint32 hash = (uint32)(((uint32)d.m_dict[ins_pos & TDEFL_LZ_DICT_SIZE_MASK] << (TDEFL_LZ_HASH_SHIFT * 2)) ^ (d.m_dict[(ins_pos + 1) & TDEFL_LZ_DICT_SIZE_MASK] << TDEFL_LZ_HASH_SHIFT) ^ c) & (TDEFL_LZ_HASH_SIZE - 1);
							d.m_next[ins_pos & TDEFL_LZ_DICT_SIZE_MASK] = d.m_hash[hash]; d.m_hash[hash] = (uint16)(ins_pos);
						}
					}
				}
				d.m_dict_size = (int32)Math.Min(TDEFL_LZ_DICT_SIZE - d.m_lookahead_size, d.m_dict_size);
				if ((flush == 0) && (d.m_lookahead_size < TDEFL_MAX_MATCH_LEN))
					break;

				// Simple lazy/greedy parsing state machine.
				len_to_move = 1; cur_match_dist = 0; cur_match_len = (d.m_saved_match_len != 0) ? d.m_saved_match_len : (TDEFL_MIN_MATCH_LEN - 1); cur_pos = d.m_lookahead_pos & TDEFL_LZ_DICT_SIZE_MASK;
				if (d.m_flags.HasFlag(.TDEFL_RLE_MATCHES | .TDEFL_FORCE_ALL_RAW_BLOCKS))
				{
					if ((d.m_dict_size != 0) && (!(d.m_flags.HasFlag(.TDEFL_FORCE_ALL_RAW_BLOCKS))))
					{
						uint8 c = d.m_dict[(cur_pos - 1) & TDEFL_LZ_DICT_SIZE_MASK];
						cur_match_len = 0; while (cur_match_len < d.m_lookahead_size) { if (d.m_dict[cur_pos + cur_match_len] != c) break; cur_match_len++; }
						if (cur_match_len < TDEFL_MIN_MATCH_LEN) cur_match_len = 0; else cur_match_dist = 1;
					}
				}
				else
				{
					tdefl_find_match(d, d.m_lookahead_pos, d.m_dict_size, d.m_lookahead_size, &cur_match_dist, &cur_match_len);
				}
				if (((cur_match_len == TDEFL_MIN_MATCH_LEN) && (cur_match_dist >= 8U * 1024U)) || (cur_pos == cur_match_dist) || ((d.m_flags.HasFlag(.TDEFL_FILTER_MATCHES)) && (cur_match_len <= 5)))
				{
					cur_match_dist = cur_match_len = 0;
				}
				if (d.m_saved_match_len != 0)
				{
					if (cur_match_len > d.m_saved_match_len)
					{
						tdefl_record_literal(d, (uint8)d.m_saved_lit);
						if (cur_match_len >= 128)
						{
							tdefl_record_match(d, cur_match_len, cur_match_dist);
							d.m_saved_match_len = 0; len_to_move = cur_match_len;
						}
						else
						{
							d.m_saved_lit = d.m_dict[cur_pos]; d.m_saved_match_dist = cur_match_dist; d.m_saved_match_len = cur_match_len;
						}
					}
					else
					{
						tdefl_record_match(d, d.m_saved_match_len, d.m_saved_match_dist);
						len_to_move = d.m_saved_match_len - 1; d.m_saved_match_len = 0;
					}
				}
				else if (cur_match_dist == 0)
					tdefl_record_literal(d, d.m_dict[Math.Min(cur_pos, TDEFL_LZ_DICT_SIZE + TDEFL_MAX_MATCH_LEN - 1 - 1)]);
				else if ((d.m_greedy_parsing) || (d.m_flags.HasFlag(.TDEFL_RLE_MATCHES)) || (cur_match_len >= 128))
				{
					tdefl_record_match(d, cur_match_len, cur_match_dist);
					len_to_move = cur_match_len;
				}
				else
				{
					d.m_saved_lit = d.m_dict[Math.Min(cur_pos, TDEFL_LZ_DICT_SIZE + TDEFL_MAX_MATCH_LEN - 1)]; d.m_saved_match_dist = cur_match_dist; d.m_saved_match_len = cur_match_len;
				}
				// Move the lookahead forward by len_to_move bytes.
				d.m_lookahead_pos += len_to_move;
				Debug.Assert(d.m_lookahead_size >= len_to_move);
				d.m_lookahead_size -= len_to_move;
				d.m_dict_size = Math.Min(d.m_dict_size + len_to_move, TDEFL_LZ_DICT_SIZE);
				// Check if it's time to flush the current LZ codes to the internal output buffer.
				if ((d.m_pLZ_code_buf > &d.m_lz_code_buf[TDEFL_LZ_CODE_BUF_SIZE - 8]) ||
					((d.m_total_lz_bytes > 31 * 1024) && (((((int32)(d.m_pLZ_code_buf - (uint8*)&d.m_lz_code_buf) * 115) >> 7) >= d.m_total_lz_bytes) || (d.m_flags.HasFlag(.TDEFL_FORCE_ALL_RAW_BLOCKS)))))
				{
					int32 n;
					d.m_pSrc = pSrc; d.m_src_buf_left = src_buf_left;
					if ((n = tdefl_flush_block(d, default)) != 0)
						return (n < 0) ? false : true;
				}
			}

			d.m_pSrc = pSrc; d.m_src_buf_left = src_buf_left;
			return true;
		}

		static TdeflStatus tdefl_flush_output_buffer(tdefl_compressor* d)
		{
			if (d.m_pIn_buf_size != null)
			{
				*d.m_pIn_buf_size = (int32)(d.m_pSrc - (uint8*)d.m_pIn_buf);
			}

			if (d.m_pOut_buf_size != null)
			{
				int32 n = (int32)Math.Min(*d.m_pOut_buf_size - d.m_out_buf_ofs, d.m_output_flush_remaining);
				Internal.MemCpy((uint8*)d.m_pOut_buf + d.m_out_buf_ofs, &d.m_output_buf + d.m_output_flush_ofs, n);
				d.m_output_flush_ofs += (int32)n;
				d.m_output_flush_remaining -= (int32)n;
				d.m_out_buf_ofs += n;

				*d.m_pOut_buf_size = d.m_out_buf_ofs;
			}

			return (d.m_finished && (d.m_output_flush_remaining == 0)) ? .TDEFL_STATUS_DONE : .TDEFL_STATUS_OKAY;
		}

		static TdeflStatus tdefl_compress(tdefl_compressor* d, void* pIn_buf, int* pIn_buf_size, void* pOut_buf, int* pOut_buf_size, TdeflFlush flush)
		{
			if (d == null)
			{
				if (pIn_buf_size != null) *pIn_buf_size = 0;
				if (pOut_buf_size != null) *pOut_buf_size = 0;
				return .TDEFL_STATUS_BAD_PARAM;
			}

			d.m_pIn_buf = pIn_buf; d.m_pIn_buf_size = pIn_buf_size;
			d.m_pOut_buf = pOut_buf; d.m_pOut_buf_size = pOut_buf_size;
			d.m_pSrc = (uint8*)(pIn_buf); d.m_src_buf_left = (pIn_buf_size != null) ? (int32) * pIn_buf_size : 0;
			d.m_out_buf_ofs = 0;
			d.m_flush = flush;

			if (((d.m_pPut_buf_func != null) == ((pOut_buf != null) || (pOut_buf_size != null))) || (d.m_prev_return_status != .TDEFL_STATUS_OKAY) ||
				(d.m_wants_to_finish && (flush != .TDEFL_FINISH)) || ((pIn_buf_size != null) && (*pIn_buf_size != 0) && (pIn_buf == null)) || ((pOut_buf_size != null) && (*pOut_buf_size != 0) && (pOut_buf == null)))
			{
				if (pIn_buf_size != null) *pIn_buf_size = 0;
				if (pOut_buf_size != null) *pOut_buf_size = 0;
				return (d.m_prev_return_status = .TDEFL_STATUS_BAD_PARAM);
			}
			d.m_wants_to_finish |= (flush == .TDEFL_FINISH);

			if ((d.m_output_flush_remaining != 0) || (d.m_finished))
				return (d.m_prev_return_status = tdefl_flush_output_buffer(d));

#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN
			if (((int32)(d.m_flags & .TDEFL_MAX_PROBES_MASK) == 1) &&
				(d.m_flags.HasFlag(.TDEFL_GREEDY_PARSING_FLAG)) &&
				((d.m_flags & (.TDEFL_FILTER_MATCHES | .TDEFL_FORCE_ALL_RAW_BLOCKS | .TDEFL_RLE_MATCHES)) == 0))
			{
				if (!tdefl_compress_fast(d))
					return d.m_prev_return_status;
			}
			else
#endif // #if MINIZ_USE_UNALIGNED_LOADS_AND_STORES && MINIZ_LITTLE_ENDIAN
			{
				if (!tdefl_compress_normal(d))
					return d.m_prev_return_status;
			}

			if ((d.m_flags.HasFlag(.TDEFL_WRITE_ZLIB_HEADER | .TDEFL_COMPUTE_ADLER32)) && (pIn_buf != null))
				d.m_adler32 = (uint32)adler32(d.m_adler32, (uint8*)pIn_buf, d.m_pSrc - (uint8*)pIn_buf);

			if ((flush != 0) && (d.m_lookahead_size == 0) && (d.m_src_buf_left == 0) && (d.m_output_flush_remaining == 0))
			{
				if (tdefl_flush_block(d, flush) < 0)
					return d.m_prev_return_status;
				d.m_finished = (flush == .TDEFL_FINISH);
				if (flush == .TDEFL_FULL_FLUSH) { d.m_hash = default; d.m_next = default; d.m_dict_size = 0; }
			}

			return (d.m_prev_return_status = tdefl_flush_output_buffer(d));
		}

		static TdeflStatus tdefl_compress_buffer(tdefl_compressor* d, void* pIn_buf, int in_buf_size, TdeflFlush flush)
		{
			int inBufSize = in_buf_size;
			Debug.Assert(d.m_pPut_buf_func != null);
			return tdefl_compress(d, pIn_buf, &inBufSize, null, null, flush);
		}

		static TdeflStatus tdefl_init(tdefl_compressor* d, tdefl_put_buf_func_ptr pPut_buf_func, void* pPut_buf_user, TDEFLFlags flags)
		{
			d.m_pPut_buf_func = pPut_buf_func; d.m_pPut_buf_user = pPut_buf_user;
			d.m_flags = flags; d.m_max_probes[0] = 1 + (((uint32)flags & 0xFFF) + 2) / 3; d.m_greedy_parsing = (flags.HasFlag(.TDEFL_GREEDY_PARSING_FLAG));
			d.m_max_probes[1] = 1 + ((((uint32)flags & 0xFFF) >> 2) + 2) / 3;
			if (!(flags.HasFlag(.TDEFL_NONDETERMINISTIC_PARSING_FLAG))) d.m_hash = default;
			d.m_lookahead_pos = d.m_lookahead_size = d.m_dict_size = d.m_total_lz_bytes = d.m_lz_code_buf_dict_pos = d.m_bits_in = 0;
			d.m_output_flush_ofs = d.m_output_flush_remaining = d.m_block_index = d.m_bit_buffer = 0;
			d.m_wants_to_finish = d.m_finished = false;
			d.m_pLZ_code_buf = &d.m_lz_code_buf[1];
			d.m_pLZ_flags = &d.m_lz_code_buf; d.m_num_flags_left = 8;
			d.m_pOutput_buf = &d.m_output_buf; d.m_pOutput_buf_end = &d.m_output_buf; d.m_prev_return_status = .TDEFL_STATUS_OKAY;
			d.m_saved_match_dist = d.m_saved_match_len = d.m_saved_lit = 0; d.m_adler32 = 1;
			d.m_pIn_buf = null; d.m_pOut_buf = null;
			d.m_pIn_buf_size = null; d.m_pOut_buf_size = null;
			d.m_flush = .TDEFL_NO_FLUSH; d.m_pSrc = null; d.m_src_buf_left = 0; d.m_out_buf_ofs = 0;
			Internal.MemSet(&d.m_huff_count[0][0], 0, sizeof(uint16) * TDEFL_MAX_HUFF_SYMBOLS_0);
			Internal.MemSet(&d.m_huff_count[1][0], 0, sizeof(uint16) * TDEFL_MAX_HUFF_SYMBOLS_1);
			return .TDEFL_STATUS_OKAY;
		}

		static TdeflStatus tdefl_get_prev_return_status(tdefl_compressor* d)
		{
			return d.m_prev_return_status;
		}

		static uint32 tdefl_get_adler32(tdefl_compressor* d)
		{
			return d.m_adler32;
		}

		static bool tdefl_compress_mem_to_output(void* pBuf, int buf_len, tdefl_put_buf_func_ptr pPut_buf_func, void* pPut_buf_user, TDEFLFlags flags)
		{
			tdefl_compressor* pComp; bool succeeded; if (((buf_len != 0) && (pBuf == null)) || (pPut_buf_func == null)) return false;
			pComp = (tdefl_compressor*)malloc(sizeof(tdefl_compressor)); if (pComp == null) return false;
			succeeded = (tdefl_init(pComp, pPut_buf_func, pPut_buf_user, flags) == .TDEFL_STATUS_OKAY);
			succeeded = succeeded && (tdefl_compress_buffer(pComp, pBuf, buf_len, .TDEFL_FINISH) == .TDEFL_STATUS_DONE);
			free(pComp); return succeeded;
		}

		struct tdefl_output_buffer
		{
			public int m_size, m_capacity;
			public uint8* m_pBuf;
			public bool m_expandable;
		}

		static bool tdefl_output_buffer_putter(void* pBuf, int len, void* pUser)
		{
			tdefl_output_buffer* p = (tdefl_output_buffer*)pUser;
			int new_size = p.m_size + len;
			if (new_size > p.m_capacity)
			{
				int new_capacity = p.m_capacity; uint8* pNew_buf; if (!p.m_expandable) return false;
				repeat { new_capacity = Math.Max(128, new_capacity << 1); } while (new_size > new_capacity);
				pNew_buf = (uint8*)realloc(p.m_pBuf, new_capacity); if (pNew_buf == null) return false;
				p.m_pBuf = pNew_buf; p.m_capacity = new_capacity;
			}
			Internal.MemCpy((uint8*)p.m_pBuf + p.m_size, pBuf, len); p.m_size = new_size;
			return true;
		}

		static void* tdefl_compress_mem_to_heap(void* pSrc_buf, int src_buf_len, int* pOut_len, TDEFLFlags flags)
		{
			tdefl_output_buffer out_buf; out_buf = default;
			if (pOut_len == null) return null; else *pOut_len = 0;
			out_buf.m_expandable = true;
			if (!tdefl_compress_mem_to_output(pSrc_buf, src_buf_len, => tdefl_output_buffer_putter, &out_buf, flags)) return null;
			*pOut_len = out_buf.m_size; return out_buf.m_pBuf;
		}

		static int tdefl_compress_mem_to_mem(void* pOut_buf, int out_buf_len, void* pSrc_buf, int src_buf_len, TDEFLFlags flags)
		{
			tdefl_output_buffer out_buf; out_buf = default;
			if (pOut_buf == null) return 0;
			out_buf.m_pBuf = (uint8*)pOut_buf; out_buf.m_capacity = out_buf_len;
			if (!tdefl_compress_mem_to_output(pSrc_buf, src_buf_len, => tdefl_output_buffer_putter, &out_buf, flags)) return 0;
			return out_buf.m_size;
		}

		//#ifndef MINIZ_NO_ZLIB_APIS
		const uint32[11] s_tdefl_num_probes = uint32[11](0, 1, 6, 32, 16, 32, 128, 256, 512, 768, 1500);

		// level may actually range from [0,10] (10 is a "hidden" max level, where we want a bit more compression and it's fine if throughput to fall off a cliff on some files).
		static TDEFLFlags tdefl_create_comp_flags_from_zip_params(CompressionLevel level, int window_bits, CompressionStrategy strategy)
		{
			TDEFLFlags comp_flags = (TDEFLFlags)(s_tdefl_num_probes[(level >= default) ? Math.Min(10, (int)level) : (int)CompressionLevel.DEFAULT_LEVEL] | (((int)level <= 3) ? (int)TDEFLFlags.TDEFL_GREEDY_PARSING_FLAG : 0));
			if (window_bits > 0) comp_flags |= .TDEFL_WRITE_ZLIB_HEADER;

			if (level == 0) comp_flags |= .TDEFL_FORCE_ALL_RAW_BLOCKS;
			else if (strategy == .FILTERED) comp_flags |= .TDEFL_FILTER_MATCHES;
			else if (strategy == .HUFFMAN_ONLY) comp_flags &= ~.TDEFL_MAX_PROBES_MASK;
			else if (strategy == .FIXED) comp_flags |= .TDEFL_FORCE_ALL_STATIC_BLOCKS;
			else if (strategy == .RLE) comp_flags |= .TDEFL_RLE_MATCHES;

			return comp_flags;
		}

		// Simple PNG writer function by Alex Evans, 2011. Released into the public domain: https://gist.github.com/908299, more context at
		// http://altdevblogaday.org/2011/04/06/a-smaller-jpg-encoder/.
		// This is actually a modification of Alex's original code so PNG files generated by this function pass pngcheck.
		const uint32[11] s_tdefl_png_num_probes = uint32[11](0, 1, 6, 32, 16, 32, 128, 256, 512, 768, 1500);
		static void* tdefl_write_image_to_png_file_in_memory_ex(void* pImage, int w, int h, int num_chans, int* pLen_out, uint32 level, bool flip)
		{
		  // Using a local copy of this array here in case MINIZ_NO_ZLIB_APIS was defined.

			tdefl_compressor* pComp = (tdefl_compressor*)malloc(sizeof(tdefl_compressor)); tdefl_output_buffer out_buf; int i, bpl = w * num_chans, y, z; uint32 c; *pLen_out = 0;
			if (pComp == null) return null;
			out_buf = default; out_buf.m_expandable = true; out_buf.m_capacity = 57 + Math.Max(64, (1 + bpl) * h); if (null == (out_buf.m_pBuf = (uint8*)malloc(out_buf.m_capacity))) { free(pComp); return null; }
		  // write dummy header
			for (z = 41; z != 0; --z) tdefl_output_buffer_putter(&z, 1, &out_buf);
		  // compress image data
			tdefl_init(pComp, => tdefl_output_buffer_putter, &out_buf, (TDEFLFlags)s_tdefl_png_num_probes[Math.Min(10, level)] | .TDEFL_WRITE_ZLIB_HEADER);
			for (y = 0; y < h; ++y) { tdefl_compress_buffer(pComp, &z, 1, .TDEFL_NO_FLUSH); tdefl_compress_buffer(pComp, (uint8*)pImage + (flip ? (h - 1 - y) : y) * bpl, bpl, .TDEFL_NO_FLUSH); }
			if (tdefl_compress_buffer(pComp, null, 0, .TDEFL_FINISH) != .TDEFL_STATUS_DONE) { free(pComp); free(out_buf.m_pBuf); return null; }
		  // write real header
			*pLen_out = out_buf.m_size - 41;
			{
				var chans = uint8[5](0x00, 0x00, 0x04, 0x02, 0x06);
				var pnghdr = uint8[41](0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
					0, 0, (uint8)(w >> 8), (uint8)w, 0, 0, (uint8)(h >> 8), (uint8)h, 8, chans[num_chans], 0, 0, 0, 0, 0, 0, 0,
					(uint8)(*pLen_out >> 24), (uint8)(*pLen_out >> 16), (uint8)(*pLen_out >> 8), (uint8) * pLen_out, 0x49, 0x44, 0x41, 0x54);
				c = (uint32)crc32(CRC32_INIT, &pnghdr + 12, 17);
				for (i = 0; i < 4; ++i,c <<= 8) ((uint8*)(&pnghdr + 29))[i] = (uint8)(c >> 24);
				Internal.MemCpy(out_buf.m_pBuf, &pnghdr, 41);
			}
			// write footer (IDAT CRC-32, followed by IEND chunk)
			if (!tdefl_output_buffer_putter((char8*)"\0\0\0\0\0\0\0\0\x49\x45\x4e\x44\xae\x42\x60\x82", 16, &out_buf)) { *pLen_out = 0; free(pComp); free(out_buf.m_pBuf); return null; }
			c = (uint32)crc32(CRC32_INIT, out_buf.m_pBuf + 41 - 4, *pLen_out + 4); for (i = 0; i < 4; ++i,c <<= 8) (out_buf.m_pBuf + out_buf.m_size - 16)[i] = (uint8)(c >> 24);
			// compute final size of file, grab compressed data buffer and return
			*pLen_out += 57; free(pComp); return out_buf.m_pBuf;
		}

		static void* tdefl_write_image_to_png_file_in_memory(void* pImage, int w, int h, int num_chans, int* pLen_out)
		{
		  // Level 6 corresponds to TDEFL_DEFAULT_MAX_PROBES or DEFAULT_LEVEL (but we can't depend on DEFAULT_LEVEL being available in case the zlib API's where #defined out)
			return tdefl_write_image_to_png_file_in_memory_ex(pImage, w, h, num_chans, pLen_out, 6, false);
		}


		// ZIP archive identifiers and record sizes
		const int ZIP_END_OF_CENTRAL_DIR_HEADER_SIG = 0x06054b50;
		const int ZIP_CENTRAL_DIR_HEADER_SIG = 0x02014b50;
		const int ZIP_LOCAL_DIR_HEADER_SIG = 0x04034b50;
		const int ZIP_LOCAL_DIR_HEADER_SIZE = 30;
		const int ZIP_CENTRAL_DIR_HEADER_SIZE = 46;
		const int ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE = 22;
		// Central directory header record offsets
		const int ZIP_CDH_SIG_OFS = 0;
		const int ZIP_CDH_VERSION_MADE_BY_OFS = 4;
		const int ZIP_CDH_VERSION_NEEDED_OFS = 6;
		const int ZIP_CDH_BIT_FLAG_OFS = 8;
		const int ZIP_CDH_METHOD_OFS = 10;
		const int ZIP_CDH_FILE_TIME_OFS = 12;
		const int ZIP_CDH_FILE_DATE_OFS = 14;
		const int ZIP_CDH_CRC32_OFS = 16;
		const int ZIP_CDH_COMPRESSED_SIZE_OFS = 20;
		const int ZIP_CDH_DECOMPRESSED_SIZE_OFS = 24;
		const int ZIP_CDH_FILENAME_LEN_OFS = 28;
		const int ZIP_CDH_EXTRA_LEN_OFS = 30;
		const int ZIP_CDH_COMMENT_LEN_OFS = 32;
		const int ZIP_CDH_DISK_START_OFS = 34;
		const int ZIP_CDH_INTERNAL_ATTR_OFS = 36;
		const int ZIP_CDH_EXTERNAL_ATTR_OFS = 38;
		const int ZIP_CDH_LOCAL_HEADER_OFS = 42;
		// Local directory header offsets
		const int ZIP_LDH_SIG_OFS = 0;
		const int ZIP_LDH_VERSION_NEEDED_OFS = 4;
		const int ZIP_LDH_BIT_FLAG_OFS = 6;
		const int ZIP_LDH_METHOD_OFS = 8;
		const int ZIP_LDH_FILE_TIME_OFS = 10;
		const int ZIP_LDH_FILE_DATE_OFS = 12;
		const int ZIP_LDH_CRC32_OFS = 14;
		const int ZIP_LDH_COMPRESSED_SIZE_OFS = 18;
		const int ZIP_LDH_DECOMPRESSED_SIZE_OFS = 22;
		const int ZIP_LDH_FILENAME_LEN_OFS = 26;
		const int ZIP_LDH_EXTRA_LEN_OFS = 28;
		// End of central directory offsets
		const int ZIP_ECDH_SIG_OFS = 0;
		const int ZIP_ECDH_NUM_THIS_DISK_OFS = 4;
		const int ZIP_ECDH_NUM_DISK_CDIR_OFS = 6;
		const int ZIP_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS = 8;
		const int ZIP_ECDH_CDIR_TOTAL_ENTRIES_OFS = 10;
		const int ZIP_ECDH_CDIR_SIZE_OFS = 12;
		const int ZIP_ECDH_CDIR_OFS_OFS = 16;
		const int ZIP_ECDH_COMMENT_SIZE_OFS = 20;

		public struct ZipArray
		{
			public void* m_p;
			public int m_size, m_capacity;
			public uint32 m_element_size;
		}

		public struct FILE;

		const int32 EOF = -1;
		const int32 SEEK_CUR = 1;
		const int32 SEEK_END = 2;
		const int32 SEEK_SET = 0;

		[StdCall, CLink]
		static extern FILE* fopen(char8* fileName, char8* mode);

		[StdCall, CLink]
		static extern FILE* freopen(char8* fileName, char8* mode, FILE* stream);

		[StdCall, CLink]
		static extern int32 fclose(FILE* stream);

		[StdCall, CLink]
		static extern int32 fflush(FILE* stream);

		//[StdCall, CLink]
		//static extern int64 ftell64(FILE* stream);
		static int64 ftell64(FILE* stream)
		{
			return _ftelli64(stream);
		}

		[StdCall, CLink]
		static extern int64 _ftelli64(FILE* stream);

		//[StdCall, CLink]
		//static extern int64 fseek64(FILE* stream, int64 offset, int32 origin);
		static int32 fseek64(FILE* stream, int64 offset, int32 origin)
		{
			return _fseeki64(stream, offset, origin);
		}

		[StdCall, CLink]
		static extern int32 _fseeki64(FILE* stream, int64 offset, int32 origin);

		[StdCall, CLink]
		static extern int fread(void* buf, int elementSize, int elementCount, FILE* stream);

		[StdCall, CLink]
		static extern int fwrite(void* buf, int elementSize, int elementCount, FILE* stream);

		public struct ZipInternalState
		{
			public ZipArray m_central_dir;
			public ZipArray m_central_dir_offsets;
			public ZipArray m_sorted_central_dir_offsets;
			public FILE* m_pFile;
			public Stream mStream;
			public void* m_pMem;
			public int m_mem_size;
			public int m_mem_capacity;
		}

		static mixin ZIP_ARRAY_SET_ELEMENT_SIZE(var array_ptr, var element_size)
		{
			array_ptr.m_element_size = element_size;
		}

		static mixin ZIP_ARRAY_ELEMENT<T>(var array_ptr, int index)
		{
			((T*)(array_ptr.m_p))[index]
		}

		static void zip_array_clear(ZipArchive* pZip, ZipArray* pArray)
		{
			pZip.m_pFree(pZip.m_pAlloc_opaque, pArray.m_p);
			Internal.MemSet(pArray, 0, sizeof(ZipArray));
		}

		static bool zip_array_ensure_capacity(ZipArchive* pZip, ZipArray* pArray, int min_new_capacity, bool growing)
		{
			void* pNew_p; int new_capacity = min_new_capacity; Debug.Assert(pArray.m_element_size != 0); if (pArray.m_capacity >= min_new_capacity) return true;
			if (growing) { new_capacity = Math.Max(1, pArray.m_capacity); while (new_capacity < min_new_capacity) new_capacity *= 2; }
			if (null == (pNew_p = pZip.m_pRealloc(pZip.m_pAlloc_opaque, pArray.m_p, pArray.m_element_size, new_capacity))) return false;
			pArray.m_p = pNew_p; pArray.m_capacity = new_capacity;
			return true;
		}

		static bool zip_array_reserve(ZipArchive* pZip, ZipArray* pArray, int new_capacity, bool growing)
		{
			if (new_capacity > pArray.m_capacity) { if (!zip_array_ensure_capacity(pZip, pArray, new_capacity, growing)) return false; }
			return true;
		}

		static bool zip_array_resize(ZipArchive* pZip, ZipArray* pArray, int new_size, bool growing)
		{
			if (new_size > pArray.m_capacity) { if (!zip_array_ensure_capacity(pZip, pArray, new_size, growing)) return false; }
			pArray.m_size = new_size;
			return true;
		}

		static bool zip_array_ensure_room(ZipArchive* pZip, ZipArray* pArray, int n)
		{
			return zip_array_reserve(pZip, pArray, pArray.m_size + n, true);
		}

		static bool zip_array_push_back(ZipArchive* pZip, ZipArray* pArray, void* pElements, int n)
		{
			int orig_size = pArray.m_size; if (!zip_array_resize(pZip, pArray, orig_size + n, true)) return false;
			Internal.MemCpy((uint8*)pArray.m_p + orig_size * pArray.m_element_size, pElements, n * pArray.m_element_size);
			return true;
		}

		[CRepr]
		struct tm
		{
			public int32 tm_sec;   // seconds after the minute - [0, 60] including leap second
			public int32 tm_min;   // minutes after the hour - [0, 59]
			public int32 tm_hour;  // hours since midnight - [0, 23]
			public int32 tm_mday;  // day of the month - [1, 31]
			public int32 tm_mon;   // months since January - [0, 11]
			public int32 tm_year;  // years since 1900
			public int32 tm_wday;  // days since Sunday - [0, 6]
			public int32 tm_yday;  // days since January 1 - [0, 365]
			public int32 tm_isdst; // daylight savings time flag
		};

		[CLink, StdCall]
		static extern time_t time(out time_t time);

		[CLink, StdCall]
		static extern time_t mktime(tm* time);

		[CLink, StdCall]
		static extern tm* localtime(time_t* time);

		static time_t zip_dos_to_time_t(int32 dos_time, int32 dos_date)
		{
			tm tm;
			tm = default; tm.tm_isdst = -1;
			tm.tm_year = ((dos_date >> 9) & 127) + 1980 - 1900; tm.tm_mon = ((dos_date >> 5) & 15) - 1; tm.tm_mday = dos_date & 31;
			tm.tm_hour = (dos_time >> 11) & 31; tm.tm_min = (dos_time >> 5) & 63; tm.tm_sec = (dos_time << 1) & 62;
			return mktime(&tm);
		}

		static void zip_time_to_dos_time(time_t time, uint16* pDOS_time, uint16* pDOS_date)
		{
/*#ifdef _MSC_VER
		  struct tm tm_struct;
		  struct tm *tm = &tm_struct;
		  errno_t err = localtime_s(tm, &time);
		  if (err)
		  {
			*pDOS_date = 0; *pDOS_time = 0;
			return;
		  }
#else*/
			time_t localTime = time;
			tm* tm = localtime(&localTime);

			*pDOS_time = (uint16)(((tm.tm_hour) << 11) + ((tm.tm_min) << 5) + ((tm.tm_sec) >> 1));
			*pDOS_date = (uint16)(((tm.tm_year + 1900 - 1980) << 9) + ((tm.tm_mon + 1) << 5) + tm.tm_mday);
		}


		//#ifndef MINIZ_NO_STDIO
		static bool zip_get_file_modified_time(char8* pFilename, uint16* pDOS_time, uint16* pDOS_date)
		{
			FILE_STAT_STRUCT file_stat = default;
			// On Linux with x86 glibc, this call will fail on large files (>= 0x80000000 bytes) unless you compiled with _LARGEFILE64_SOURCE. Argh.
			if (_fstat64i32(pFilename, &file_stat) != 0)
				return false;
			zip_time_to_dos_time(file_stat.st_mtime, pDOS_time, pDOS_date);
			return true;
		}

		struct utimbuf
		{
			public time_t actime;      // access time
			public time_t modtime;     // modification time
		};

		[CLink, StdCall]
		static extern int32 _utime64(char8* fileName, utimbuf* t);

		static bool zip_set_file_times(char8* pFilename, time_t access_time, time_t modified_time)
		{
			utimbuf t; t.actime = access_time; t.modtime = modified_time;
			return _utime64(pFilename, &t) == 0;
		}

		static bool zip_reader_init_internal(ZipArchive* pZip, ZipFlags flags)
		{
			if ((pZip == null) || (pZip.m_pState != null) || (pZip.m_zip_mode != .Invalid))
				return false;

			if (pZip.m_pAlloc == null) pZip.m_pAlloc = => def_alloc_func;
			if (pZip.m_pFree == null) pZip.m_pFree = => def_free_func;
			if (pZip.m_pRealloc == null) pZip.m_pRealloc = => def_realloc_func;

			pZip.m_zip_mode = .Reading;
			pZip.m_archive_size = 0;
			pZip.m_central_directory_file_ofs = 0;
			pZip.m_total_files = 0;

			if (null == (pZip.m_pState = (ZipInternalState*)pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, sizeof(ZipInternalState))))
				return false;
			*pZip.m_pState = default;
			ZIP_ARRAY_SET_ELEMENT_SIZE!(pZip.m_pState.m_central_dir, sizeof(uint8));
			ZIP_ARRAY_SET_ELEMENT_SIZE!(pZip.m_pState.m_central_dir_offsets, sizeof(uint32));
			ZIP_ARRAY_SET_ELEMENT_SIZE!(pZip.m_pState.m_sorted_central_dir_offsets, sizeof(uint32));
			return true;
		}

		static bool zip_reader_filename_less(ZipArray* pCentral_dir_array, ZipArray* pCentral_dir_offsets, uint32 l_index, uint32 r_index)
		{
			uint8* pL = &ZIP_ARRAY_ELEMENT!<uint8>(pCentral_dir_array, ZIP_ARRAY_ELEMENT!<uint32>(pCentral_dir_offsets, l_index));
			uint8* pE;
			uint8* pR = &ZIP_ARRAY_ELEMENT!<uint8>(pCentral_dir_array, ZIP_ARRAY_ELEMENT!<uint32>(pCentral_dir_offsets, r_index));
			uint32 l_len = ReadLE16!(pL + ZIP_CDH_FILENAME_LEN_OFS), r_len = ReadLE16!(pR + ZIP_CDH_FILENAME_LEN_OFS);
			char8 l = 0, r = 0;
			pL += ZIP_CENTRAL_DIR_HEADER_SIZE; pR += ZIP_CENTRAL_DIR_HEADER_SIZE;
			pE = pL + Math.Min(l_len, r_len);
			while (pL < pE)
			{
				if ((l = ((char8) * pL).ToLower) != (r = ((char8) * pR).ToLower))
					break;
				pL++; pR++;
			}
			return (pL == pE) ? (l_len < r_len) : (l < r);
		}

		// Heap sort of lowercased filenames, used to help accelerate plain central directory searches by zip_reader_locate_file(). (Could also use qsort(), but it could allocate memory.)
		static void zip_reader_sort_central_dir_offsets_by_filename(ZipArchive* pZip)
		{
			ZipInternalState* pState = pZip.m_pState;
			ZipArray* pCentral_dir_offsets = &pState.m_central_dir_offsets;
			ZipArray* pCentral_dir = &pState.m_central_dir;
			uint32* pIndices = &ZIP_ARRAY_ELEMENT!<uint32>(&pState.m_sorted_central_dir_offsets, 0);
			int size = (int)pZip.m_total_files;
			int start = (size - 2) >> 1, end;
			while (start >= 0)
			{
				int child, root = start;
				for (;;)
				{
					if ((child = (root << 1) + 1) >= size)
						break;
					child += (((child + 1) < size) && (zip_reader_filename_less(pCentral_dir, pCentral_dir_offsets, pIndices[child], pIndices[child + 1]))) ? 1 : 0;
					if (!zip_reader_filename_less(pCentral_dir, pCentral_dir_offsets, pIndices[root], pIndices[child]))
						break;
					Swap!(pIndices[root], pIndices[child]); root = child;
				}
				start--;
			}

			end = size - 1;
			while (end > 0)
			{
				int child, root = 0;
				Swap!(pIndices[end], pIndices[0]);
				for (;;)
				{
					if ((child = (root << 1) + 1) >= end)
						break;
					child += (((child + 1) < end) && zip_reader_filename_less(pCentral_dir, pCentral_dir_offsets, pIndices[child], pIndices[child + 1])) ? 1 : 0;
					if (!zip_reader_filename_less(pCentral_dir, pCentral_dir_offsets, pIndices[root], pIndices[child]))
						break;
					Swap!(pIndices[root], pIndices[child]); root = child;
				}
				end--;
			}
		}

		static bool zip_reader_read_central_dir(ZipArchive* pZip, ZipFlags flags)
		{
			int32 cdir_size, num_this_disk, cdir_disk_index;
			int64 cdir_ofs;
			int64 cur_file_ofs;
			uint8* p;
			uint32[4096 / sizeof(uint32)] buf_u32; uint8* pBuf = (uint8*)&buf_u32;
			bool sort_central_dir = !flags.HasFlag(.DoNotSortCentralDirectory);
			// Basic sanity checks - reject files which are too small, and check the first 4 bytes of the file to make sure a local header is there.
			if (pZip.m_archive_size < ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE)
				return false;
			// Find the end of central directory record by scanning the file from the end towards the beginning.
			cur_file_ofs = Math.Max((int64)pZip.m_archive_size - (int64)sizeof(decltype(buf_u32)), 0);
			for (;;)
			{
				int i, n = (int)Math.Min(sizeof(decltype(buf_u32)), (int)pZip.m_archive_size - cur_file_ofs);
				if (pZip.m_pRead(pZip.m_pIO_opaque, (int64)cur_file_ofs, pBuf, n) != (uint32)n)
					return false;
				for (i = n - 4; i >= 0; --i)
					if (ReadLE32!(pBuf + i) == ZIP_END_OF_CENTRAL_DIR_HEADER_SIG)
						break;
				if (i >= 0)
				{
					cur_file_ofs += i;
					break;
				}
				if ((cur_file_ofs != 0) || ((pZip.m_archive_size - cur_file_ofs) >= (0xFFFF + ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE)))
					return false;
				cur_file_ofs = Math.Max(cur_file_ofs - (sizeof(decltype(buf_u32)) - 3), 0);
			}
			// Read and verify the end of central directory record.
			if (pZip.m_pRead(pZip.m_pIO_opaque, cur_file_ofs, pBuf, ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE) != ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE)
				return false;
			if ((ReadLE32!(pBuf + ZIP_ECDH_SIG_OFS) != ZIP_END_OF_CENTRAL_DIR_HEADER_SIG) ||
				((pZip.m_total_files = ReadLE16!(pBuf + ZIP_ECDH_CDIR_TOTAL_ENTRIES_OFS)) != ReadLE16!(pBuf + ZIP_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS)))
				return false;

			num_this_disk = ReadLE16!(pBuf + ZIP_ECDH_NUM_THIS_DISK_OFS);
			cdir_disk_index = ReadLE16!(pBuf + ZIP_ECDH_NUM_DISK_CDIR_OFS);
			if (((num_this_disk | cdir_disk_index) != 0) && ((num_this_disk != 1) || (cdir_disk_index != 1)))
				return false;

			if ((cdir_size = (int32)ReadLE32!(pBuf + ZIP_ECDH_CDIR_SIZE_OFS)) < pZip.m_total_files * ZIP_CENTRAL_DIR_HEADER_SIZE)
				return false;

			cdir_ofs = ReadLE32!(pBuf + ZIP_ECDH_CDIR_OFS_OFS);
			if ((cdir_ofs + (int64)cdir_size) > pZip.m_archive_size)
				return false;

			pZip.m_central_directory_file_ofs = cdir_ofs;

			if (pZip.m_total_files != 0)
			{
				int32 i, n;

			   // Read the entire central directory into a heap block, and allocate another heap block to hold the unsorted central dir file record offsets, and another to hold the sorted indices.
				if ((!zip_array_resize(pZip, &pZip.m_pState.m_central_dir, cdir_size, false)) ||
					(!zip_array_resize(pZip, &pZip.m_pState.m_central_dir_offsets, pZip.m_total_files, false)))
					return false;

				if (sort_central_dir)
				{
					if (!zip_array_resize(pZip, &pZip.m_pState.m_sorted_central_dir_offsets, pZip.m_total_files, false))
						return false;
				}

				if (pZip.m_pRead(pZip.m_pIO_opaque, cdir_ofs, pZip.m_pState.m_central_dir.m_p, cdir_size) != cdir_size)
					return false;

				// Now create an index into the central directory file records, do some basic sanity checking on each record, and check for zip64 entries (which are not yet supported).
				p = (uint8*)pZip.m_pState.m_central_dir.m_p;
				for (n = cdir_size,i = 0; i < pZip.m_total_files; ++i)
				{
					int32 total_header_size, comp_size, decomp_size, disk_index;
					if ((n < ZIP_CENTRAL_DIR_HEADER_SIZE) || (ReadLE32!(p) != ZIP_CENTRAL_DIR_HEADER_SIG))
						return false;
					ZIP_ARRAY_ELEMENT!<uint32>(&pZip.m_pState.m_central_dir_offsets, i) = (uint32)(p - (uint8*)pZip.m_pState.m_central_dir.m_p);
					if (sort_central_dir)
						ZIP_ARRAY_ELEMENT!<uint32>(&pZip.m_pState.m_sorted_central_dir_offsets, i) = (uint32)i;
					comp_size = (int32)ReadLE32!(p + ZIP_CDH_COMPRESSED_SIZE_OFS);
					decomp_size = (int32)ReadLE32!(p + ZIP_CDH_DECOMPRESSED_SIZE_OFS);
					if (((ReadLE32!(p + ZIP_CDH_METHOD_OFS) == 0) && (decomp_size != comp_size)) || ((decomp_size != 0) && (comp_size == 0)) || (decomp_size == 0xFFFFFFFF) || (comp_size == 0xFFFFFFFF))
						return false;
					disk_index = ReadLE16!(p + ZIP_CDH_DISK_START_OFS);
					if ((disk_index != num_this_disk) && (disk_index != 1))
						return false;
					if (((int64)ReadLE32!(p + ZIP_CDH_LOCAL_HEADER_OFS) + ZIP_LOCAL_DIR_HEADER_SIZE + comp_size) > pZip.m_archive_size)
						return false;
					if ((total_header_size = (int32)ZIP_CENTRAL_DIR_HEADER_SIZE + ReadLE16!(p + ZIP_CDH_FILENAME_LEN_OFS) + ReadLE16!(p + ZIP_CDH_EXTRA_LEN_OFS) + ReadLE16!(p + ZIP_CDH_COMMENT_LEN_OFS)) > n)
						return false;
					n -= total_header_size; p += total_header_size;
				}
			}

			if (sort_central_dir)
				zip_reader_sort_central_dir_offsets_by_filename(pZip);

			return true;
		}

		static bool zip_reader_init(ZipArchive* pZip, int64 size, ZipFlags flags)
		{
			if ((pZip == null) || (pZip.m_pRead == null))
				return false;
			if (!zip_reader_init_internal(pZip, flags))
				return false;
			pZip.m_archive_size = size;
			if (!zip_reader_read_central_dir(pZip, flags))
			{
				ZipReaderEnd(pZip);
				return false;
			}
			return true;
		}

		static int zip_mem_read_func(void* pOpaque, int64 file_ofs, void* pBuf, int n)
		{
			ZipArchive* pZip = (ZipArchive*)pOpaque;
			int s = (file_ofs >= pZip.m_archive_size) ? 0 : (int)Math.Min(pZip.m_archive_size - file_ofs, n);
			Internal.MemCpy(pBuf, (uint8*)pZip.m_pState.m_pMem + file_ofs, s);
			return s;
		}

		static bool zip_reader_init_mem(ZipArchive* pZip, void* pMem, int size, ZipFlags flags)
		{
			if (!zip_reader_init_internal(pZip, flags))
				return false;
			pZip.m_archive_size = size;
			pZip.m_pRead = => zip_mem_read_func;
			pZip.m_pIO_opaque = pZip;

			pZip.m_pState.m_pMem = (void*)pMem;
			pZip.m_pState.m_mem_size = size;
			if (!zip_reader_read_central_dir(pZip, flags))
			{
				ZipReaderEnd(pZip);
				return false;
			}
			return true;
		}


		static int ZipFileReadFunc(void* pOpaque, int64 file_ofs, void* pBuf, int n)
		{
			ZipArchive* pZip = (ZipArchive*)pOpaque;
			int64 cur_ofs = ftell64(pZip.m_pState.m_pFile);
			if (((int64)file_ofs < 0) || (((cur_ofs != (int64)file_ofs)) && (fseek64(pZip.m_pState.m_pFile, (int64)file_ofs, SEEK_SET) != 0)))
				return 0;
			return fread(pBuf, 1, n, pZip.m_pState.m_pFile);
		}

		public static bool ZipReaderInitFile(ZipArchive* pZip, char8* pFilename, ZipFlags flags)
		{
			int64 file_size;
			FILE* pFile = fopen(pFilename, "rb");
			if (pFile == null)
				return false;
			if (fseek64(pFile, 0, SEEK_END) != 0)
			{
				fclose(pFile);
				return false;
			}
			file_size = ftell64(pFile);
			if (!zip_reader_init_internal(pZip, flags))
			{
				fclose(pFile);
				return false;
			}
			pZip.m_pRead = => ZipFileReadFunc;
			pZip.m_pIO_opaque = pZip;
			pZip.m_pState.m_pFile = pFile;
			pZip.m_archive_size = file_size;
			if (!zip_reader_read_central_dir(pZip, flags))
			{
				ZipReaderEnd(pZip);
				return false;
			}
			return true;
		}

		static int ZipFileStreamReadFunc(void* pOpaque, int64 file_ofs, void* pBuf, int n)
		{
			ZipArchive* pZip = (ZipArchive*)pOpaque;
			int64 cur_ofs = pZip.m_pState.mStream.Position;
			if (((int64)file_ofs < 0) || (((cur_ofs != (int64)file_ofs)) && (pZip.m_pState.mStream.Seek((int64)file_ofs) case .Err)))
				return 0;
			switch (pZip.m_pState.mStream.TryRead(.((uint8*)pBuf, n)))
			{
			case .Ok(let len):
				return len;
			case .Err:
				return 0;
			}
		}

		public static bool ZipReaderInitStream(ZipArchive* pZip, Stream stream, ZipFlags flags)
		{
			int64 file_size = stream.Length;
			if (!zip_reader_init_internal(pZip, flags))
			{
				return false;
			}
			pZip.m_pRead = => ZipFileStreamReadFunc;
			pZip.m_pIO_opaque = pZip;
			pZip.m_pState.mStream = stream;
			pZip.m_archive_size = file_size;
			if (!zip_reader_read_central_dir(pZip, flags))
			{
				ZipReaderEnd(pZip);
				return false;
			}
			return true;
		}

		public static int ZipReaderGetNumFiles(ZipArchive* pZip)
		{
			return (pZip != null) ? pZip.m_total_files : 0;
		}

		static uint8* ZipReaderGetCdh(ZipArchive* pZip, int32 file_index)
		{
			if ((pZip == null) || (pZip.m_pState == null) || (file_index >= pZip.m_total_files) || (pZip.m_zip_mode != .Reading))
				return null;
			return &ZIP_ARRAY_ELEMENT!<uint8>(&pZip.m_pState.m_central_dir, ZIP_ARRAY_ELEMENT!<uint32>(&pZip.m_pState.m_central_dir_offsets, file_index));
		}

		static bool zip_reader_is_file_encrypted(ZipArchive* pZip, int32 file_index)
		{
			uint32 m_bit_flag;
			uint8* p = ZipReaderGetCdh(pZip, file_index);
			if (p == null)
				return false;
			m_bit_flag = ReadLE16!(p + ZIP_CDH_BIT_FLAG_OFS);
			return (m_bit_flag & 1) != 0;
		}

		public static bool ZipReaderIsFileADirectory(ZipArchive* pZip, int32 file_index)
		{
			uint32 filename_len, external_attr;
			uint8* p = ZipReaderGetCdh(pZip, file_index);
			if (p == null)
				return false;

			// First see if the filename ends with a '/' character.
			filename_len = ReadLE16!(p + ZIP_CDH_FILENAME_LEN_OFS);
			if (filename_len != 0)
			{
				if (*(p + ZIP_CENTRAL_DIR_HEADER_SIZE + filename_len - 1) == '/')
					return true;
			}

			// Bugfix: This code was also checking if the internal attribute was non-zero, which wasn't correct.
			// Most/all zip writers (hopefully) set DOS file/directory attributes in the low 16-bits, so check for the DOS directory flag and ignore the source OS ID in the created by field.
			// FIXME: Remove this check? Is it necessary - we already check the filename.
			external_attr = ReadLE32!(p + ZIP_CDH_EXTERNAL_ATTR_OFS);
			if ((external_attr & 0x10) != 0)
				return true;

			return false;
		}

		public static bool ZipReaderFileStat(ZipArchive* pZip, int32 file_index, ZipArchiveFileStat* pStat)
		{
			int32 n;
			uint8* p = ZipReaderGetCdh(pZip, file_index);
			if ((p == null) || (pStat == null))
				return false;

			// Unpack the central directory record.
			pStat.mFileIndex = file_index;
			pStat.mCentralDirOfs = ZIP_ARRAY_ELEMENT!<uint32>(&pZip.m_pState.m_central_dir_offsets, file_index);
			pStat.mVersionMadeBy = ReadLE16!(p + ZIP_CDH_VERSION_MADE_BY_OFS);
			pStat.mVersionNeeded = ReadLE16!(p + ZIP_CDH_VERSION_NEEDED_OFS);
			pStat.mBitFlag = ReadLE16!(p + ZIP_CDH_BIT_FLAG_OFS);
			pStat.mMethod = ReadLE16!(p + ZIP_CDH_METHOD_OFS);
		  //#ifndef MINIZ_NO_TIME
			pStat.mTime = zip_dos_to_time_t(ReadLE16!(p + ZIP_CDH_FILE_TIME_OFS), ReadLE16!(p + ZIP_CDH_FILE_DATE_OFS));
		  //#endif
			pStat.mCrc32 = ReadLE32!(p + ZIP_CDH_CRC32_OFS);
			pStat.mCompSize = ReadLE32!(p + ZIP_CDH_COMPRESSED_SIZE_OFS);
			pStat.mUncompSize = ReadLE32!(p + ZIP_CDH_DECOMPRESSED_SIZE_OFS);
			pStat.mInternalAttr = ReadLE16!(p + ZIP_CDH_INTERNAL_ATTR_OFS);
			pStat.mExternalAttr = ReadLE32!(p + ZIP_CDH_EXTERNAL_ATTR_OFS);
			pStat.mLocalHeaderOfs = ReadLE32!(p + ZIP_CDH_LOCAL_HEADER_OFS);

			// Copy as much of the filename and comment as possible.
			n = ReadLE16!(p + ZIP_CDH_FILENAME_LEN_OFS); n = Math.Min(n, ZIP_MAX_ARCHIVE_FILENAME_SIZE - 1);
			Internal.MemCpy(&pStat.mFilename, p + ZIP_CENTRAL_DIR_HEADER_SIZE, n); pStat.mFilename[n] = '\0';

			n = ReadLE16!(p + ZIP_CDH_COMMENT_LEN_OFS); n = Math.Min(n, ZIP_MAX_ARCHIVE_FILE_COMMENT_SIZE - 1);
			pStat.mCommentSize = (int32)n;
			Internal.MemCpy(&pStat.mComment, p + ZIP_CENTRAL_DIR_HEADER_SIZE + ReadLE16!(p + ZIP_CDH_FILENAME_LEN_OFS) + ReadLE16!(p + ZIP_CDH_EXTRA_LEN_OFS), n); pStat.mComment[n] = '\0';

			return true;
		}

		static int32 zip_reader_get_filename(ZipArchive* pZip, int32 file_index, char8* pFilename, int32 filename_buf_size)
		{
			int32 n;
			uint8* p = ZipReaderGetCdh(pZip, file_index);
			if (p == null) { if (filename_buf_size != 0) pFilename[0] = '\0'; return 0; }
			n = ReadLE16!(p + ZIP_CDH_FILENAME_LEN_OFS);
			if (filename_buf_size != 0)
			{
				n = Math.Min(n, filename_buf_size - 1);
				Internal.MemCpy(pFilename, p + ZIP_CENTRAL_DIR_HEADER_SIZE, n);
				pFilename[n] = '\0';
			}
			return n + 1;
		}

		static bool zip_reader_string_equal(char8* pA, char8* pB, int32 len, ZipFlags flags)
		{
			int32 i;
			if (flags.HasFlag(.CaseSensitive))
				return 0 == Internal.MemCmp(pA, pB, len);
			for (i = 0; i < len; ++i)
				if (pA[i].ToLower != pB[i].ToLower)
					return false;
			return true;
		}

		static int zip_reader_filename_compare(ZipArray* pCentral_dir_array, ZipArray* pCentral_dir_offsets, int32 l_index, char8* pR_in, int32 r_len)
		{
			char8* pR = pR_in;

			uint8* pL = &ZIP_ARRAY_ELEMENT!<uint8>(pCentral_dir_array, ZIP_ARRAY_ELEMENT!<uint32>(pCentral_dir_offsets, l_index));
			uint8* pE;
			int32 l_len = ReadLE16!(pL + ZIP_CDH_FILENAME_LEN_OFS);
			char8 l = 0, r = 0;
			pL += ZIP_CENTRAL_DIR_HEADER_SIZE;
			pE = pL + Math.Min(l_len, r_len);
			while (pL < pE)
			{
				if ((l = ((char8) * pL).ToLower) != (r = ((char8) * pR).ToLower))
					break;
				pL++;
				pR++;
			}
			return (pL == pE) ? (int)(l_len - r_len) : (l - r);
		}

		static int32 zip_reader_locate_file_binary_search(ZipArchive* pZip, char8* pFilename)
		{
			ZipInternalState* pState = pZip.m_pState;
			ZipArray* pCentral_dir_offsets = &pState.m_central_dir_offsets;
			ZipArray* pCentral_dir = &pState.m_central_dir;
			uint32* pIndices = &ZIP_ARRAY_ELEMENT!<uint32>(&pState.m_sorted_central_dir_offsets, 0);
			int32 size = pZip.m_total_files;
			int32 filename_len = (int32)Internal.CStrLen(pFilename);
			int32 l = 0, h = size - 1;
			while (l <= h)
			{
				int32 m = (l + h) >> 1, file_index = (int32)pIndices[m];
				int comp = zip_reader_filename_compare(pCentral_dir, pCentral_dir_offsets, file_index, pFilename, filename_len);
				if (comp == 0)
					return file_index;
				else if (comp < 0)
					l = m + 1;
				else
					h = m - 1;
			}
			return -1;
		}

		static int32 zip_reader_locate_file(ZipArchive* pZip, char8* pName, char8* pComment, ZipFlags flags)
		{
			int32 file_index; int32 name_len, comment_len;
			if ((pZip == null) || (pZip.m_pState == null) || (pName == null) || (pZip.m_zip_mode != .Reading))
				return -1;
			if (((!flags.HasFlag(.IgnorePath) && !flags.HasFlag(.CaseSensitive))) && (pComment == null) && (pZip.m_pState.m_sorted_central_dir_offsets.m_size != 0))
				return zip_reader_locate_file_binary_search(pZip, pName);
			name_len = Internal.CStrLen(pName); if (name_len > 0xFFFF) return -1;
			comment_len = (pComment != null) ? Internal.CStrLen(pComment) : 0; if (comment_len > 0xFFFF) return -1;
			for (file_index = 0; file_index < pZip.m_total_files; file_index++)
			{
				uint8* pHeader = &ZIP_ARRAY_ELEMENT!<uint8>(&pZip.m_pState.m_central_dir, ZIP_ARRAY_ELEMENT!<uint32>(&pZip.m_pState.m_central_dir_offsets, file_index));
				int32 filename_len = ReadLE16!(pHeader + ZIP_CDH_FILENAME_LEN_OFS);
				char8* pFilename = (char8*)pHeader + ZIP_CENTRAL_DIR_HEADER_SIZE;
				if (filename_len < name_len)
					continue;
				if (comment_len != 0)
				{
					int32 file_extra_len = ReadLE16!(pHeader + ZIP_CDH_EXTRA_LEN_OFS), file_comment_len = ReadLE16!(pHeader + ZIP_CDH_COMMENT_LEN_OFS);
					char8* pFile_comment = pFilename + filename_len + file_extra_len;
					if ((file_comment_len != comment_len) || (!zip_reader_string_equal(pComment, pFile_comment, file_comment_len, flags)))
						continue;
				}
				if ((flags.HasFlag(.IgnorePath)) && (filename_len != 0))
				{
					int32 ofs = filename_len - 1;
					repeat
					{
						if ((pFilename[ofs] == '/') || (pFilename[ofs] == '\\') || (pFilename[ofs] == ':'))
							break;
					} while (--ofs >= 0);
					ofs++;
					pFilename += ofs; filename_len -= ofs;
				}
				if ((filename_len == name_len) && (zip_reader_string_equal(pName, pFilename, filename_len, flags)))
					return file_index;
			}
			return -1;
		}

		static bool zip_reader_extract_to_mem_no_alloc(ZipArchive* pZip, int32 file_index, void* pBuf, int buf_size, ZipFlags flags, void* pUser_read_buf, int user_read_buf_size)
		{
			TinflStatus status = .Done;
			int64 needed_size, cur_file_ofs, comp_remaining, out_buf_ofs = 0, read_buf_size, read_buf_ofs = 0, read_buf_avail;
			ZipArchiveFileStat file_stat = ?;
			void* pRead_buf;
			uint32[(ZIP_LOCAL_DIR_HEADER_SIZE + sizeof(uint32) - 1) / sizeof(uint32)] local_header_u32;
			uint8* pLocal_header = (uint8*)&local_header_u32;
			TinflDecompressor inflator;

			if ((buf_size != 0) && (pBuf == null))
				return false;

			if (!ZipReaderFileStat(pZip, file_index, &file_stat))
				return false;

			// Empty file, or a directory (but not always a directory - I've seen odd zips with directories that have compressed data which inflates to 0 bytes)
			if (file_stat.mCompSize == 0)
				return true;

			// Entry is a subdirectory (I've seen old zips with dir entries which have compressed deflate data which inflates to 0 bytes, but these entries claim to uncompress to 512 bytes in the headers).
			// I'm torn how to handle this case - should it fail instead?
			if (ZipReaderIsFileADirectory(pZip, file_index))
				return true;

			// Encryption and patch files are not supported.
			if ((file_stat.mBitFlag & (1 | 32)) != 0)
				return false;

			// This function only supports stored and deflate.
			if ((!flags.HasFlag(.CompressedData)) && (file_stat.mMethod != 0) && (file_stat.mMethod != DEFLATED))
				return false;

			// Ensure supplied output buffer is large enough.
			needed_size = (int64)((flags.HasFlag(.CompressedData)) ? file_stat.mCompSize : file_stat.mUncompSize);
			if (buf_size < needed_size)
				return false;

			// Read and parse the local directory entry.
			cur_file_ofs = (int64)file_stat.mLocalHeaderOfs;
			if (pZip.m_pRead(pZip.m_pIO_opaque, cur_file_ofs, pLocal_header, ZIP_LOCAL_DIR_HEADER_SIZE) != ZIP_LOCAL_DIR_HEADER_SIZE)
				return false;
			if (ReadLE32!(pLocal_header) != ZIP_LOCAL_DIR_HEADER_SIG)
				return false;

			cur_file_ofs += ZIP_LOCAL_DIR_HEADER_SIZE + ReadLE16!(pLocal_header + ZIP_LDH_FILENAME_LEN_OFS) + ReadLE16!(pLocal_header + ZIP_LDH_EXTRA_LEN_OFS);
			if ((cur_file_ofs + (int64)file_stat.mCompSize) > pZip.m_archive_size)
				return false;

			if ((flags.HasFlag(.CompressedData)) || (file_stat.mMethod == 0))
			{
			  // The file is stored or the caller has requested the compressed data.
				if (pZip.m_pRead(pZip.m_pIO_opaque, cur_file_ofs, pBuf, (int)needed_size) != needed_size)
					return false;
				return (flags.HasFlag(.CompressedData)) || (crc32(CRC32_INIT, (uint8*)pBuf, (int)file_stat.mUncompSize) == file_stat.mCrc32);
			}

			// Decompress the file either directly from memory or from a file input buffer.
			tinfl_init!(&inflator);

			if (pZip.m_pState.m_pMem != null)
			{
			  // Read directly from the archive in memory.
				pRead_buf = (uint8*)pZip.m_pState.m_pMem + cur_file_ofs;
				read_buf_size = read_buf_avail = file_stat.mCompSize;
				comp_remaining = 0;
			}
			else if (pUser_read_buf != null)
			{
			  // Use a user provided read buffer.
				if (user_read_buf_size == 0)
					return false;
				pRead_buf = (uint8*)pUser_read_buf;
				read_buf_size = user_read_buf_size;
				read_buf_avail = 0;
				comp_remaining = file_stat.mCompSize;
			}
			else
			{
			  // Temporarily allocate a read buffer.
				read_buf_size = Math.Min(file_stat.mCompSize, ZIP_MAX_IO_BUF_SIZE);

			  //if (((sizeof(int) == sizeof(uint32))) && (read_buf_size > 0x7FFFFFFF))
				if (read_buf_size > 0x7FFFFFFF)
					return false;
				if (null == (pRead_buf = pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, (int)read_buf_size)))
					return false;
				read_buf_avail = 0;
				comp_remaining = file_stat.mCompSize;
			}

			repeat
			{
				int in_buf_size, out_buf_size = (int)(file_stat.mUncompSize - out_buf_ofs);
				if ((read_buf_avail == 0) && (pZip.m_pState.m_pMem == null))
				{
					read_buf_avail = Math.Min(read_buf_size, comp_remaining);
					if (pZip.m_pRead(pZip.m_pIO_opaque, cur_file_ofs, pRead_buf, (int)read_buf_avail) != read_buf_avail)
					{
						status = .Failed;
						break;
					}
					cur_file_ofs += read_buf_avail;
					comp_remaining -= read_buf_avail;
					read_buf_ofs = 0;
				}
				in_buf_size = (int)read_buf_avail;
				status = tinfl_decompress(&inflator, (uint8*)pRead_buf + read_buf_ofs, &in_buf_size, (uint8*)pBuf, (uint8*)pBuf + out_buf_ofs, &out_buf_size, .UsingNonWrappingOutputBuf | ((comp_remaining != default) ? .HasMoreInput : default));
				read_buf_avail -= in_buf_size;
				read_buf_ofs += in_buf_size;
				out_buf_ofs += out_buf_size;
			} while (status == .NeedsMoreInput);

			if (status == .Done)
			{
			  // Make sure the entire file was decompressed, and check its CRC.
				if ((out_buf_ofs != file_stat.mUncompSize) || (crc32(CRC32_INIT, (uint8*)pBuf, (int)file_stat.mUncompSize) != file_stat.mCrc32))
					status = .Failed;
			}

			if ((pZip.m_pState.m_pMem == null) && (pUser_read_buf == null))
				pZip.m_pFree(pZip.m_pAlloc_opaque, pRead_buf);

			return status == .Done;
		}

		public static bool ZipReaderExtractFileToMemNoAlloc(ZipArchive* pZip, char8* pFilename, void* pBuf, int buf_size, ZipFlags flags, void* pUser_read_buf, int user_read_buf_size)
		{
			int32 file_index = zip_reader_locate_file(pZip, pFilename, null, flags);
			if (file_index < 0)
				return false;
			return zip_reader_extract_to_mem_no_alloc(pZip, file_index, pBuf, buf_size, flags, pUser_read_buf, user_read_buf_size);
		}

		public static bool ZipReaderExtractToMem(ZipArchive* pZip, int32 file_index, void* pBuf, int buf_size, ZipFlags flags)
		{
			return zip_reader_extract_to_mem_no_alloc(pZip, file_index, pBuf, buf_size, flags, null, 0);
		}

		public static bool ZipReaderExtractFileToMem(ZipArchive* pZip, char8* pFilename, void* pBuf, int buf_size, ZipFlags flags)
		{
			return ZipReaderExtractFileToMemNoAlloc(pZip, pFilename, pBuf, buf_size, flags, null, 0);
		}

		public static void* ZipReaderExtractToHeap(ZipArchive* pZip, int32 file_index, int* pSize, ZipFlags flags)
		{
			uint64 comp_size, uncomp_size, alloc_size;
			uint8* p = ZipReaderGetCdh(pZip, file_index);
			void* pBuf;

			if (pSize == null)
				*pSize = 0;
			if (p == null)
				return null;

			comp_size = ReadLE32!(p + ZIP_CDH_COMPRESSED_SIZE_OFS);
			uncomp_size = ReadLE32!(p + ZIP_CDH_DECOMPRESSED_SIZE_OFS);

			alloc_size = (flags.HasFlag(.CompressedData)) ? comp_size : uncomp_size;
			if (alloc_size > 0x7FFFFFFF)
				return null;
			if (null == (pBuf = pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, (int)alloc_size)))
				return null;

			if (!ZipReaderExtractToMem(pZip, file_index, pBuf, (int)alloc_size, flags))
			{
				pZip.m_pFree(pZip.m_pAlloc_opaque, pBuf);
				return null;
			}

			if (pSize != null) *pSize = (int)alloc_size;
			return pBuf;
		}

		public static void* ZipReaderExtractFileToHeap(ZipArchive* pZip, char8* pFilename, int* pSize, ZipFlags flags)
		{
			int32 file_index = zip_reader_locate_file(pZip, pFilename, null, flags);
			if (file_index < 0)
			{
				if (pSize != null) *pSize = 0;
				return null;
			}
			return ZipReaderExtractToHeap(pZip, file_index, pSize, flags);
		}

		static bool zip_reader_extract_to_callback(ZipArchive* pZip, int32 file_index, FileWriteFunc pCallback, void* pOpaque, ZipFlags flags)
		{
			TinflStatus status = .Done; uint32 file_crc32 = CRC32_INIT;
			int64 read_buf_size, read_buf_ofs = 0, read_buf_avail, comp_remaining, out_buf_ofs = 0, cur_file_ofs;
			ZipArchiveFileStat file_stat = ?;
			void* pRead_buf = null; void* pWrite_buf = null;
			uint32[(ZIP_LOCAL_DIR_HEADER_SIZE + sizeof(uint32) - 1) / sizeof(uint32)] local_header_u32; uint8* pLocal_header = (uint8*)&local_header_u32;

			if (!ZipReaderFileStat(pZip, file_index, &file_stat))
				return false;

			// Empty file, or a directory (but not always a directory - I've seen odd zips with directories that have compressed data which inflates to 0 bytes)
			if (file_stat.mCompSize == 0)
				return true;

			// Entry is a subdirectory (I've seen old zips with dir entries which have compressed deflate data which inflates to 0 bytes, but these entries claim to uncompress to 512 bytes in the headers).
			// I'm torn how to handle this case - should it fail instead?
			if (ZipReaderIsFileADirectory(pZip, file_index))
				return true;

			// Encryption and patch files are not supported.
			if ((file_stat.mBitFlag & (1 | 32)) != 0)
				return false;

			// This function only supports stored and deflate.
			if ((!(flags.HasFlag(.CompressedData))) && (file_stat.mMethod != 0) && (file_stat.mMethod != DEFLATED))
				return false;

			// Read and parse the local directory entry.
			cur_file_ofs = file_stat.mLocalHeaderOfs;
			if (pZip.m_pRead(pZip.m_pIO_opaque, cur_file_ofs, pLocal_header, ZIP_LOCAL_DIR_HEADER_SIZE) != ZIP_LOCAL_DIR_HEADER_SIZE)
				return false;
			if (ReadLE32!(pLocal_header) != ZIP_LOCAL_DIR_HEADER_SIG)
				return false;

			cur_file_ofs += ZIP_LOCAL_DIR_HEADER_SIZE + ReadLE16!(pLocal_header + ZIP_LDH_FILENAME_LEN_OFS) + ReadLE16!(pLocal_header + ZIP_LDH_EXTRA_LEN_OFS);
			if ((cur_file_ofs + file_stat.mCompSize) > pZip.m_archive_size)
				return false;

			// Decompress the file either directly from memory or from a file input buffer.
			if (pZip.m_pState.m_pMem != null)
			{
				pRead_buf = (uint8*)pZip.m_pState.m_pMem + cur_file_ofs;
				read_buf_size = read_buf_avail = file_stat.mCompSize;
				comp_remaining = 0;
			}
			else
			{
				read_buf_size = Math.Min(file_stat.mCompSize, ZIP_MAX_IO_BUF_SIZE);
				if (null == (pRead_buf = pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, (int)read_buf_size)))
					return false;
				read_buf_avail = 0;
				comp_remaining = file_stat.mCompSize;
			}

			if ((flags.HasFlag(.CompressedData)) || (file_stat.mMethod == 0))
			{
			  // The file is stored or the caller has requested the compressed data.
				if (pZip.m_pState.m_pMem != null)
				{
					if (file_stat.mCompSize > 0xFFFFFFFF)
						return false;
					if (pCallback(pOpaque, out_buf_ofs, pRead_buf, (int)file_stat.mCompSize) != file_stat.mCompSize)
						status = .Failed;
					else if (!(flags.HasFlag(.CompressedData)))
						file_crc32 = (uint32)crc32(file_crc32, (uint8*)pRead_buf, (int)file_stat.mCompSize);
					cur_file_ofs += file_stat.mCompSize;
					out_buf_ofs += file_stat.mCompSize;
					comp_remaining = 0;
				}
				else
				{
					while (comp_remaining != 0)
					{
						read_buf_avail = Math.Min(read_buf_size, comp_remaining);
						if (pZip.m_pRead(pZip.m_pIO_opaque, cur_file_ofs, pRead_buf, (int)read_buf_avail) != read_buf_avail)
						{
							status = .Failed;
							break;
						}

						if (!(flags.HasFlag(.CompressedData)))
							file_crc32 = (uint32)crc32(file_crc32, (uint8*)pRead_buf, (int)read_buf_avail);

						if (pCallback(pOpaque, out_buf_ofs, pRead_buf, (int)read_buf_avail) != read_buf_avail)
						{
							status = .Failed;
							break;
						}
						cur_file_ofs += read_buf_avail;
						out_buf_ofs += read_buf_avail;
						comp_remaining -= read_buf_avail;
					}
				}
			}
			else
			{
				TinflDecompressor inflator;
				tinfl_init!(&inflator);

				if (null == (pWrite_buf = pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, TINFL_LZ_DICT_SIZE)))
					status = .Failed;
				else
				{
					repeat
					{
						uint8* pWrite_buf_cur = (uint8*)pWrite_buf + (out_buf_ofs & (TINFL_LZ_DICT_SIZE - 1));
						int in_buf_size, out_buf_size = TINFL_LZ_DICT_SIZE - (out_buf_ofs & (TINFL_LZ_DICT_SIZE - 1));
						if ((read_buf_avail == 0) && (pZip.m_pState.m_pMem == null))
						{
							read_buf_avail = Math.Min(read_buf_size, comp_remaining);
							if (pZip.m_pRead(pZip.m_pIO_opaque, cur_file_ofs, pRead_buf, (int)read_buf_avail) != read_buf_avail)
							{
								status = .Failed;
								break;
							}
							cur_file_ofs += read_buf_avail;
							comp_remaining -= read_buf_avail;
							read_buf_ofs = 0;
						}

						in_buf_size = (int)read_buf_avail;
						status = tinfl_decompress(&inflator, (uint8*)pRead_buf + read_buf_ofs, &in_buf_size, (uint8*)pWrite_buf, pWrite_buf_cur, &out_buf_size, (comp_remaining != default) ? .HasMoreInput : default);
						read_buf_avail -= in_buf_size;
						read_buf_ofs += in_buf_size;

						if (out_buf_size != 0)
						{
							if (pCallback(pOpaque, out_buf_ofs, pWrite_buf_cur, out_buf_size) != out_buf_size)
							{
								status = .Failed;
								break;
							}
							file_crc32 = (uint32)crc32(file_crc32, pWrite_buf_cur, out_buf_size);
							if ((out_buf_ofs += out_buf_size) > file_stat.mUncompSize)
							{
								status = .Failed;
								break;
							}
						}
					} while ((status == .NeedsMoreInput) || (status == .HasMoreOutput));
				}
			}

			if ((status == .Done) && (!(flags.HasFlag(.CompressedData))))
			{
			  // Make sure the entire file was decompressed, and check its CRC.
				if ((out_buf_ofs != file_stat.mUncompSize) || (file_crc32 != file_stat.mCrc32))
					status = .Failed;
			}

			if (pZip.m_pState.m_pMem == null)
				pZip.m_pFree(pZip.m_pAlloc_opaque, pRead_buf);
			if (pWrite_buf != null)
				pZip.m_pFree(pZip.m_pAlloc_opaque, pWrite_buf);

			return status == .Done;
		}

		static bool zip_reader_extract_file_to_callback(ZipArchive* pZip, char8* pFilename, FileWriteFunc pCallback, void* pOpaque, ZipFlags flags)
		{
			int32 file_index = zip_reader_locate_file(pZip, pFilename, null, flags);
			if (file_index < 0)
				return false;
			return zip_reader_extract_to_callback(pZip, file_index, pCallback, pOpaque, flags);
		}

		//#ifndef MINIZ_NO_STDIO
		static int zip_file_write_callback(void* pOpaque, int64 ofs, void* pBuf, int n)
		{
			return fwrite(pBuf, 1, n, (FILE*)pOpaque);
		}

		public static bool ZipReaderExtractToFile(ZipArchive* pZip, int32 file_index, char8* pDst_filename, ZipFlags flags)
		{
			bool status;
			ZipArchiveFileStat file_stat = ?;
			FILE* pFile;
			if (!ZipReaderFileStat(pZip, file_index, &file_stat))
				return false;
			pFile = fopen(pDst_filename, "wb");
			if (pFile == null)
				return false;
			status = zip_reader_extract_to_callback(pZip, file_index, => zip_file_write_callback, pFile, flags);
			if (fclose(pFile) == EOF)
				return false;

			if (status)
				zip_set_file_times(pDst_filename, file_stat.mTime, file_stat.mTime);

			return status;
		}

		public static bool ZipReaderEnd(ZipArchive* pZip)
		{
			if ((pZip == null) || (pZip.m_pState == null) || (pZip.m_pAlloc == null) || (pZip.m_pFree == null) || (pZip.m_zip_mode != .Reading))
				return false;

			if (pZip.m_pState != null)
			{
				ZipInternalState* pState = pZip.m_pState; pZip.m_pState = null;
				zip_array_clear(pZip, &pState.m_central_dir);
				zip_array_clear(pZip, &pState.m_central_dir_offsets);
				zip_array_clear(pZip, &pState.m_sorted_central_dir_offsets);

				if (pState.m_pFile != null)
				{
					fclose(pState.m_pFile);
					pState.m_pFile = null;
				}

				pZip.m_pFree(pZip.m_pAlloc_opaque, pState);
			}
			pZip.m_zip_mode = .Invalid;

			return true;
		}

		static bool zip_reader_extract_file_to_file(ZipArchive* pZip, char8* pArchive_filename, char8* pDst_filename, ZipFlags flags)
		{
			int32 file_index = zip_reader_locate_file(pZip, pArchive_filename, null, flags);
			if (file_index < 0)
				return false;
			return ZipReaderExtractToFile(pZip, file_index, pDst_filename, flags);
		}

		static void write_le16(uint8* p, uint16 v) { p[0] = (uint8)v; p[1] = (uint8)(v >> 8); }
		static void write_le32(uint8* p, uint32 v) { p[0] = (uint8)v; p[1] = (uint8)(v >> 8); p[2] = (uint8)(v >> 16); p[3] = (uint8)(v >> 24); }

		static mixin WRITE_LE16(var p, var v) { write_le16((uint8*)(p), (uint16)(v)); }
		static mixin WRITE_LE32(var p, var v) { write_le32((uint8*)(p), (uint32)(v)); }

		static bool zip_writer_init(ZipArchive* pZip, int64 existing_size)
		{
			if ((pZip == null) || (pZip.m_pState != null) || (pZip.m_pWrite == null) || (pZip.m_zip_mode != .Invalid))
				return false;

			if (pZip.m_file_offset_alignment != 0)
			{
			  // Ensure user specified file offset alignment is a power of 2.
				if ((pZip.m_file_offset_alignment & (pZip.m_file_offset_alignment - 1)) != 0)
					return false;
			}

			if (pZip.m_pAlloc == null) pZip.m_pAlloc = => def_alloc_func;
			if (pZip.m_pFree == null) pZip.m_pFree = => def_free_func;
			if (pZip.m_pRealloc == null) pZip.m_pRealloc = => def_realloc_func;

			pZip.m_zip_mode = .Writing;
			pZip.m_archive_size = existing_size;
			pZip.m_central_directory_file_ofs = 0;
			pZip.m_total_files = 0;

			if (null == (pZip.m_pState = (ZipInternalState*)pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, sizeof(ZipInternalState))))
				return false;
			Internal.MemSet(pZip.m_pState, 0, sizeof(ZipInternalState));
			ZIP_ARRAY_SET_ELEMENT_SIZE!(&pZip.m_pState.m_central_dir, sizeof(uint8));
			ZIP_ARRAY_SET_ELEMENT_SIZE!(&pZip.m_pState.m_central_dir_offsets, sizeof(uint32));
			ZIP_ARRAY_SET_ELEMENT_SIZE!(&pZip.m_pState.m_sorted_central_dir_offsets, sizeof(uint32));
			return true;
		}

		static int zip_heap_write_func(void* pOpaque, int64 file_ofs, void* pBuf, int n)
		{
			ZipArchive* pZip = (ZipArchive*)pOpaque;
			ZipInternalState* pState = pZip.m_pState;
			int64 new_size = Math.Max(file_ofs + n, pState.m_mem_size);
			if ((n == 0) || (new_size > 0x7FFFFFFF))
				return 0;
			if (new_size > pState.m_mem_capacity)
			{
				void* pNew_block;
				int new_capacity = Math.Max(64, pState.m_mem_capacity); while (new_capacity < new_size) new_capacity *= 2;
				if (null == (pNew_block = pZip.m_pRealloc(pZip.m_pAlloc_opaque, pState.m_pMem, 1, new_capacity)))
					return 0;
				pState.m_pMem = pNew_block; pState.m_mem_capacity = new_capacity;
			}
			Internal.MemCpy((uint8*)pState.m_pMem + file_ofs, pBuf, n);
			pState.m_mem_size = (int)new_size;
			return n;
		}

		static bool zip_writer_init_heap(ZipArchive* pZip, int into_reserve_at_beginning, int initial_allocation_size)
		{
			int useInitialAllocationSize = initial_allocation_size;
			pZip.m_pWrite = => zip_heap_write_func;
			pZip.m_pIO_opaque = pZip;
			if (!zip_writer_init(pZip, into_reserve_at_beginning))
				return false;
			if (0 != (useInitialAllocationSize = Math.Max(useInitialAllocationSize, into_reserve_at_beginning)))
			{
				if (null == (pZip.m_pState.m_pMem = pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, useInitialAllocationSize)))
				{
					zip_writer_end(pZip);
					return false;
				}
				pZip.m_pState.m_mem_capacity = useInitialAllocationSize;
			}
			return true;
		}

		static int zip_file_write_func(void* pOpaque, int64 file_ofs, void* pBuf, int n)
		{
			ZipArchive* pZip = (ZipArchive*)pOpaque;
			int64 cur_ofs = ftell64(pZip.m_pState.m_pFile);
			if (((int64)file_ofs < 0) || (((cur_ofs != (int64)file_ofs)) && (fseek64(pZip.m_pState.m_pFile, (int64)file_ofs, SEEK_SET) != 0)))
				return 0;
			return fwrite(pBuf, 1, n, pZip.m_pState.m_pFile);
		}

		public static bool ZipWriterInitFile(ZipArchive* pZip, char8* pFilename, int64 into_reserve_at_beginning)
		{
			FILE* pFile;
			pZip.m_pWrite = => zip_file_write_func;
			pZip.m_pIO_opaque = pZip;
			if (!zip_writer_init(pZip, into_reserve_at_beginning))
				return false;
			if (null == (pFile = fopen(pFilename, "wb")))
			{
				zip_writer_end(pZip);
				return false;
			}
			pZip.m_pState.m_pFile = pFile;
			if (into_reserve_at_beginning != 0)
			{
				int64 reserveLeft = into_reserve_at_beginning;
				int64 cur_ofs = 0; char8[4096] buf; buf = default;
				repeat
				{
					int64 n = (int)Math.Min(sizeof(decltype(buf)), reserveLeft);
					if (pZip.m_pWrite(pZip.m_pIO_opaque, cur_ofs, &buf, n) != n)
					{
						zip_writer_end(pZip);
						return false;
					}
					cur_ofs += n; reserveLeft -= n;
				} while (reserveLeft != 0);
			}
			return true;
		}

		bool zip_writer_init_from_reader(ZipArchive* pZip, char8* pFilename)
		{
			ZipInternalState* pState;
			if ((pZip == null) || (pZip.m_pState == null) || (pZip.m_zip_mode != .Reading))
				return false;
			// No sense in trying to write to an archive that's already at the support max size
			if ((pZip.m_total_files == 0xFFFF) || ((pZip.m_archive_size + ZIP_CENTRAL_DIR_HEADER_SIZE + ZIP_LOCAL_DIR_HEADER_SIZE) > 0xFFFFFFFF))
				return false;

			pState = pZip.m_pState;

			if (pState.m_pFile != null)
			{
			  // Archive is being read from stdio - try to reopen as writable.
				if (pZip.m_pIO_opaque != pZip)
					return false;
				if (pFilename == null)
					return false;
				pZip.m_pWrite = => zip_file_write_func;
				if (null == (pState.m_pFile = freopen(pFilename, "r+b", pState.m_pFile)))
				{
				  // The zip_archive is now in a bogus state because pState.m_pFile is null, so just close it.
					ZipReaderEnd(pZip);
					return false;
				}
			}
			else if (pState.m_pMem != null)
			{
			  // Archive lives in a memory block. Assume it's from the heap that we can resize using the realloc callback.
				if (pZip.m_pIO_opaque != pZip)
					return false;
				pState.m_mem_capacity = pState.m_mem_size;
				pZip.m_pWrite = => zip_heap_write_func;
			}
			// Archive is being read via a user provided read function - make sure the user has specified a write function too.
			else if (pZip.m_pWrite == null)
				return false;

			// Start writing new files at the archive's current central directory location.
			pZip.m_archive_size = pZip.m_central_directory_file_ofs;
			pZip.m_zip_mode = .Writing;
			pZip.m_central_directory_file_ofs = 0;

			return true;
		}

		bool zip_writer_add_mem(ZipArchive* pZip, char8* pArchive_name, void* pBuf, int buf_size, ZipFlags level_and_flags)
		{
			return ZipWriterAddMemEx(pZip, pArchive_name, pBuf, buf_size, null, 0, level_and_flags, 0, 0);
		}

		struct zip_writer_add_state
		{
			public ZipArchive* m_pZip;
			public int64 m_cur_archive_file_ofs;
			public int64 m_comp_size;
		}

		static bool zip_writer_add_put_buf_callback(void* pBuf, int len, void* pUser)
		{
			zip_writer_add_state* pState = (zip_writer_add_state*)pUser;
			if ((int)pState.m_pZip.m_pWrite(pState.m_pZip.m_pIO_opaque, pState.m_cur_archive_file_ofs, pBuf, len) != len)
				return false;
			pState.m_cur_archive_file_ofs += len;
			pState.m_comp_size += len;
			return true;
		}

		static bool zip_writer_create_local_dir_header(ZipArchive* pZip, uint8* pDst, uint16 filename_size, uint16 extra_size, int64 uncomp_size, int64 comp_size, uint32 uncomp_crc32, uint16 method, uint16 bit_flags, uint16 dos_time, uint16 dos_date)
		{
			Internal.MemSet(pDst, 0, ZIP_LOCAL_DIR_HEADER_SIZE);
			WRITE_LE32!(pDst + ZIP_LDH_SIG_OFS, ZIP_LOCAL_DIR_HEADER_SIG);
			WRITE_LE16!(pDst + ZIP_LDH_VERSION_NEEDED_OFS, (method != 0) ? 20 : 0);
			WRITE_LE16!(pDst + ZIP_LDH_BIT_FLAG_OFS, bit_flags);
			WRITE_LE16!(pDst + ZIP_LDH_METHOD_OFS, method);
			WRITE_LE16!(pDst + ZIP_LDH_FILE_TIME_OFS, dos_time);
			WRITE_LE16!(pDst + ZIP_LDH_FILE_DATE_OFS, dos_date);
			WRITE_LE32!(pDst + ZIP_LDH_CRC32_OFS, uncomp_crc32);
			WRITE_LE32!(pDst + ZIP_LDH_COMPRESSED_SIZE_OFS, comp_size);
			WRITE_LE32!(pDst + ZIP_LDH_DECOMPRESSED_SIZE_OFS, uncomp_size);
			WRITE_LE16!(pDst + ZIP_LDH_FILENAME_LEN_OFS, filename_size);
			WRITE_LE16!(pDst + ZIP_LDH_EXTRA_LEN_OFS, extra_size);
			return true;
		}

		static bool zip_writer_create_central_dir_header(ZipArchive* pZip, uint8* pDst, uint16 filename_size, uint16 extra_size, uint16 comment_size, int64 uncomp_size, int64 comp_size, uint32 uncomp_crc32, uint16 method, uint16 bit_flags, uint16 dos_time, uint16 dos_date, int64 local_header_ofs, uint32 ext_attributes)
		{
			Internal.MemSet(pDst, 0, ZIP_CENTRAL_DIR_HEADER_SIZE);
			WRITE_LE32!(pDst + ZIP_CDH_SIG_OFS, ZIP_CENTRAL_DIR_HEADER_SIG);
			WRITE_LE16!(pDst + ZIP_CDH_VERSION_NEEDED_OFS, (method != 0) ? 20 : 0);
			WRITE_LE16!(pDst + ZIP_CDH_BIT_FLAG_OFS, bit_flags);
			WRITE_LE16!(pDst + ZIP_CDH_METHOD_OFS, method);
			WRITE_LE16!(pDst + ZIP_CDH_FILE_TIME_OFS, dos_time);
			WRITE_LE16!(pDst + ZIP_CDH_FILE_DATE_OFS, dos_date);
			WRITE_LE32!(pDst + ZIP_CDH_CRC32_OFS, uncomp_crc32);
			WRITE_LE32!(pDst + ZIP_CDH_COMPRESSED_SIZE_OFS, comp_size);
			WRITE_LE32!(pDst + ZIP_CDH_DECOMPRESSED_SIZE_OFS, uncomp_size);
			WRITE_LE16!(pDst + ZIP_CDH_FILENAME_LEN_OFS, filename_size);
			WRITE_LE16!(pDst + ZIP_CDH_EXTRA_LEN_OFS, extra_size);
			WRITE_LE16!(pDst + ZIP_CDH_COMMENT_LEN_OFS, comment_size);
			WRITE_LE32!(pDst + ZIP_CDH_EXTERNAL_ATTR_OFS, ext_attributes);
			WRITE_LE32!(pDst + ZIP_CDH_LOCAL_HEADER_OFS, local_header_ofs);
			return true;
		}

		static bool zip_writer_add_to_central_dir(ZipArchive* pZip, char8* pFilename, uint16 filename_size, void* pExtra, uint16 extra_size, void* pComment, uint16 comment_size, int64 uncomp_size, int64 comp_size, uint32 uncomp_crc32, uint16 method, uint16 bit_flags, uint16 dos_time, uint16 dos_date, int64 local_header_ofs, uint32 ext_attributes)
		{
			ZipInternalState* pState = pZip.m_pState;
			uint32 central_dir_ofs = (uint32)pState.m_central_dir.m_size;
			int orig_central_dir_size = pState.m_central_dir.m_size;
			uint8[ZIP_CENTRAL_DIR_HEADER_SIZE] central_dir_header;

			// No zip64 support yet
			if ((local_header_ofs > 0xFFFFFFFF) || (((uint64)pState.m_central_dir.m_size + ZIP_CENTRAL_DIR_HEADER_SIZE + filename_size + extra_size + comment_size) > 0xFFFFFFFF))
				return false;

			if (!zip_writer_create_central_dir_header(pZip, &central_dir_header, filename_size, extra_size, comment_size, uncomp_size, comp_size, uncomp_crc32, method, bit_flags, dos_time, dos_date, local_header_ofs, ext_attributes))
				return false;

			if ((!zip_array_push_back(pZip, &pState.m_central_dir, &central_dir_header, ZIP_CENTRAL_DIR_HEADER_SIZE)) ||
				(!zip_array_push_back(pZip, &pState.m_central_dir, pFilename, filename_size)) ||
				(!zip_array_push_back(pZip, &pState.m_central_dir, pExtra, extra_size)) ||
				(!zip_array_push_back(pZip, &pState.m_central_dir, pComment, comment_size)) ||
				(!zip_array_push_back(pZip, &pState.m_central_dir_offsets, &central_dir_ofs, 1)))
			{
			  // Try to push the central directory array back into its original state.
				zip_array_resize(pZip, &pState.m_central_dir, orig_central_dir_size, false);
				return false;
			}

			return true;
		}

		static bool zip_writer_validate_archive_name(char8* archiveName)
		{
			char8* pArchive_name = archiveName;

		  // Basic ZIP archive filename validity checks: Valid filenames cannot start with a forward slash, cannot contain a drive letter, and cannot use DOS-style backward slashes.
			if (*pArchive_name == '/')
				return false;
			while (*pArchive_name != 0)
			{
				if ((*pArchive_name == '\\') || (*pArchive_name == ':'))
					return false;
				pArchive_name++;
			}
			return true;
		}

		static int32 zip_writer_compute_padding_needed_for_file_alignment(ZipArchive* pZip)
		{
			int32 n;
			if (pZip.m_file_offset_alignment == 0)
				return 0;
			n = (int32)((int32)pZip.m_archive_size & (pZip.m_file_offset_alignment - 1));
			return ((int32)pZip.m_file_offset_alignment - n) & (pZip.m_file_offset_alignment - 1);
		}

		static bool ZipWriterWriteZeros(ZipArchive* pZip, int64 fileOfs, int32 count)
		{
			int64 cur_file_ofs = fileOfs;
			int32 padLeft = count;

			char8[4096] buf;
			Internal.MemSet(&buf, 0, Math.Min(sizeof(decltype(buf)), padLeft));
			while (padLeft != 0)
			{
				int32 s = Math.Min(sizeof(decltype(buf)), padLeft);
				if (pZip.m_pWrite(pZip.m_pIO_opaque, cur_file_ofs, &buf, s) != s)
					return false;
				cur_file_ofs += s; padLeft -= s;
			}
			return true;
		}

		public static bool ZipWriterAddMemEx(ZipArchive* pZip, char8* pArchive_name, void* pBuf, int buf_size, void* pComment, uint16 comment_size, ZipFlags level_and_flags_in, int64 uncomp_size_in, uint32 uncomp_crc32_in)
		{
			int64 uncomp_size = uncomp_size_in;
			uint32 uncomp_crc32 = uncomp_crc32_in;
			ZipFlags level_and_flags = level_and_flags_in;
			uint16 method = 0, dos_time = 0, dos_date = 0;
			CompressionLevel level;
			uint32 ext_attributes = 0;
			int32 num_alignment_padding_bytes;
			int64 local_dir_header_ofs = pZip.m_archive_size, cur_archive_file_ofs = pZip.m_archive_size, comp_size = 0;
			int archive_name_size;
			uint8[ZIP_LOCAL_DIR_HEADER_SIZE] local_dir_header;
			tdefl_compressor* pComp = null;
			bool store_data_uncompressed;
			ZipInternalState* pState;

			if ((int32)level_and_flags < 0)
				level_and_flags = (ZipFlags)CompressionLevel.DEFAULT_LEVEL;
			level = (CompressionLevel)((int32)level_and_flags & 0xF);
			store_data_uncompressed = ((level == 0) || (level_and_flags.HasFlag(.CompressedData)));

			if ((pZip == null) || (pZip.m_pState != null) || (pZip.m_zip_mode != .Writing) || ((buf_size != 0) && (pBuf == null)) ||
				(pArchive_name == null) || ((comment_size != 0) && (pComment == null)) || (pZip.m_total_files == 0xFFFF) || (level > .UBER_COMPRESSION))
				return false;

			pState = pZip.m_pState;

			if ((!(level_and_flags.HasFlag(.CompressedData))) && (uncomp_size != 0))
				return false;
		  // No zip64 support yet
			if ((buf_size > 0xFFFFFFFF) || (uncomp_size > 0xFFFFFFFF))
				return false;
			if (!zip_writer_validate_archive_name(pArchive_name))
				return false;
			{
				time_t cur_time; time(out cur_time);
				zip_time_to_dos_time(cur_time, &dos_time, &dos_date);
			}

			archive_name_size = Internal.CStrLen(pArchive_name);
			if (archive_name_size > 0xFFFF)
				return false;

			num_alignment_padding_bytes = zip_writer_compute_padding_needed_for_file_alignment(pZip);

			// no zip64 support yet
			if ((pZip.m_total_files == 0xFFFF) || ((pZip.m_archive_size + num_alignment_padding_bytes + ZIP_LOCAL_DIR_HEADER_SIZE + ZIP_CENTRAL_DIR_HEADER_SIZE + comment_size + archive_name_size) > 0xFFFFFFFF))
				return false;

			if ((archive_name_size != 0) && (pArchive_name[archive_name_size - 1] == '/'))
			{
			  // Set DOS Subdirectory attribute bit.
				ext_attributes |= 0x10;
			  // Subdirectories cannot contain data.
				if ((buf_size != 0) || (uncomp_size != 0))
					return false;
			}

			// Try to do any allocations before writing to the archive, so if an allocation fails the file remains unmodified. (A good idea if we're doing an in-place modification.)
			if ((!zip_array_ensure_room(pZip, &pState.m_central_dir, ZIP_CENTRAL_DIR_HEADER_SIZE + archive_name_size + comment_size)) || (!zip_array_ensure_room(pZip, &pState.m_central_dir_offsets, 1)))
				return false;

			if ((!store_data_uncompressed) && (buf_size != 0))
			{
				if (null == (pComp = (tdefl_compressor*)pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, sizeof(tdefl_compressor))))
					return false;
			}

			if (!ZipWriterWriteZeros(pZip, cur_archive_file_ofs, num_alignment_padding_bytes + sizeof(decltype(local_dir_header))))
			{
				pZip.m_pFree(pZip.m_pAlloc_opaque, pComp);
				return false;
			}
			local_dir_header_ofs += num_alignment_padding_bytes;
			if (pZip.m_file_offset_alignment != 0) { Debug.Assert((local_dir_header_ofs & (pZip.m_file_offset_alignment - 1)) == 0); }
			cur_archive_file_ofs += num_alignment_padding_bytes + sizeof(decltype(local_dir_header));

			local_dir_header = default;
			if (pZip.m_pWrite(pZip.m_pIO_opaque, cur_archive_file_ofs, pArchive_name, archive_name_size) != archive_name_size)
			{
				pZip.m_pFree(pZip.m_pAlloc_opaque, pComp);
				return false;
			}
			cur_archive_file_ofs += archive_name_size;

			if (!(level_and_flags.HasFlag(.CompressedData)))
			{
				uncomp_crc32 = (uint32)crc32(CRC32_INIT, (uint8*)pBuf, buf_size);
				uncomp_size = buf_size;
				if (uncomp_size <= 3)
				{
					level = default;
					store_data_uncompressed = true;
				}
			}

			if (store_data_uncompressed)
			{
				if (pZip.m_pWrite(pZip.m_pIO_opaque, cur_archive_file_ofs, pBuf, buf_size) != buf_size)
				{
					pZip.m_pFree(pZip.m_pAlloc_opaque, pComp);
					return false;
				}

				cur_archive_file_ofs += buf_size;
				comp_size = buf_size;

				if (level_and_flags.HasFlag(.CompressedData))
					method = DEFLATED;
			}
			else if (buf_size != 0)
			{
				zip_writer_add_state state;

				state.m_pZip = pZip;
				state.m_cur_archive_file_ofs = cur_archive_file_ofs;
				state.m_comp_size = 0;

				if ((tdefl_init(pComp, => zip_writer_add_put_buf_callback, &state, tdefl_create_comp_flags_from_zip_params(level, -15, .DEFAULT_STRATEGY)) != .TDEFL_STATUS_OKAY) ||
					(tdefl_compress_buffer(pComp, pBuf, buf_size, .TDEFL_FINISH) != .TDEFL_STATUS_DONE))
				{
					pZip.m_pFree(pZip.m_pAlloc_opaque, pComp);
					return false;
				}

				comp_size = state.m_comp_size;
				cur_archive_file_ofs = state.m_cur_archive_file_ofs;

				method = DEFLATED;
			}

			pZip.m_pFree(pZip.m_pAlloc_opaque, pComp);
			pComp = null;

			// no zip64 support yet
			if ((comp_size > 0xFFFFFFFF) || (cur_archive_file_ofs > 0xFFFFFFFF))
				return false;

			if (!zip_writer_create_local_dir_header(pZip, &local_dir_header, (uint16)archive_name_size, 0, uncomp_size, comp_size, uncomp_crc32, method, 0, dos_time, dos_date))
				return false;

			if (pZip.m_pWrite(pZip.m_pIO_opaque, local_dir_header_ofs, &local_dir_header, sizeof(decltype(local_dir_header))) != sizeof(decltype(local_dir_header)))
				return false;

			if (!zip_writer_add_to_central_dir(pZip, pArchive_name, (uint16)archive_name_size, null, 0, pComment, comment_size, uncomp_size, comp_size, uncomp_crc32, method, 0, dos_time, dos_date, local_dir_header_ofs, ext_attributes))
				return false;

			pZip.m_total_files++;
			pZip.m_archive_size = cur_archive_file_ofs;

			return true;
		}

		public static bool ZipWriterAddFile(ZipArchive* pZip, char8* pArchive_name, char8* pSrc_filename, void* pComment, uint16 comment_size, ZipFlags level_and_flags_in)
		{
			ZipFlags level_and_flags = level_and_flags_in;
			uint32 uncomp_crc32 = CRC32_INIT;
			int32 num_alignment_padding_bytes;
			CompressionLevel level;
			uint16 method = 0, dos_time = 0, dos_date = 0, ext_attributes = 0;
			int64 local_dir_header_ofs = pZip.m_archive_size, cur_archive_file_ofs = pZip.m_archive_size, uncomp_size = 0, comp_size = 0;
			int archive_name_size;
			uint8[ZIP_LOCAL_DIR_HEADER_SIZE] local_dir_header;
			FILE* pSrc_file = null;

			if ((int32)level_and_flags < 0)
				level_and_flags = (ZipFlags)CompressionLevel.DEFAULT_LEVEL;
			level = (CompressionLevel)((int32)level_and_flags & 0xF);

			if ((pZip == null) || (pZip.m_pState == null) || (pZip.m_zip_mode != .Writing) || (pArchive_name == null) || ((comment_size != 0) && (pComment == null)) || (level > .UBER_COMPRESSION))
				return false;
			if (level_and_flags.HasFlag(.CompressedData))
				return false;
			if (!zip_writer_validate_archive_name(pArchive_name))
				return false;

			archive_name_size = Internal.CStrLen(pArchive_name);
			if (archive_name_size > 0xFFFF)
				return false;

			num_alignment_padding_bytes = zip_writer_compute_padding_needed_for_file_alignment(pZip);

			// no zip64 support yet
			if ((pZip.m_total_files == 0xFFFF) || ((pZip.m_archive_size + num_alignment_padding_bytes + ZIP_LOCAL_DIR_HEADER_SIZE + ZIP_CENTRAL_DIR_HEADER_SIZE + comment_size + archive_name_size) > 0xFFFFFFFF))
				return false;

			if (!zip_get_file_modified_time(pSrc_filename, &dos_time, &dos_date))
				return false;

			pSrc_file = fopen(pSrc_filename, "rb");
			if (pSrc_file == null)
				return false;
			fseek64(pSrc_file, 0, SEEK_END);
			uncomp_size = ftell64(pSrc_file);
			fseek64(pSrc_file, 0, SEEK_SET);

			if (uncomp_size > 0xFFFFFFFF)
			{
			  // No zip64 support yet
				fclose(pSrc_file);
				return false;
			}
			if (uncomp_size <= 3)
				level = default;

			if (!ZipWriterWriteZeros(pZip, cur_archive_file_ofs, num_alignment_padding_bytes + sizeof(decltype(local_dir_header))))
			{
				fclose(pSrc_file);
				return false;
			}
			local_dir_header_ofs += num_alignment_padding_bytes;
			if (pZip.m_file_offset_alignment != 0) { Debug.Assert((local_dir_header_ofs & (pZip.m_file_offset_alignment - 1)) == 0); }
			cur_archive_file_ofs += num_alignment_padding_bytes + sizeof(decltype(local_dir_header));

			local_dir_header = default;
			if (pZip.m_pWrite(pZip.m_pIO_opaque, cur_archive_file_ofs, pArchive_name, archive_name_size) != archive_name_size)
			{
				fclose(pSrc_file);
				return false;
			}
			cur_archive_file_ofs += archive_name_size;

			if (uncomp_size != 0)
			{
				int64 uncomp_remaining = uncomp_size;
				void* pRead_buf = pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, ZIP_MAX_IO_BUF_SIZE);
				if (pRead_buf == null)
				{
					fclose(pSrc_file);
					return false;
				}

				if (level == 0)
				{
					while (uncomp_remaining != 0)
					{
						int32 n = (int32)Math.Min(ZIP_MAX_IO_BUF_SIZE, uncomp_remaining);
						if ((fread(pRead_buf, 1, n, pSrc_file) != n) || (pZip.m_pWrite(pZip.m_pIO_opaque, cur_archive_file_ofs, pRead_buf, n) != n))
						{
							pZip.m_pFree(pZip.m_pAlloc_opaque, pRead_buf);
							fclose(pSrc_file);
							return false;
						}
						uncomp_crc32 = (uint32)crc32(uncomp_crc32, (uint8*)pRead_buf, n);
						uncomp_remaining -= n;
						cur_archive_file_ofs += n;
					}
					comp_size = uncomp_size;
				}
				else
				{
					bool result = false;
					zip_writer_add_state state;
					tdefl_compressor* pComp = (tdefl_compressor*)pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, sizeof(tdefl_compressor));
					if (pComp == null)
					{
						pZip.m_pFree(pZip.m_pAlloc_opaque, pRead_buf);
						fclose(pSrc_file);
						return false;
					}

					state.m_pZip = pZip;
					state.m_cur_archive_file_ofs = cur_archive_file_ofs;
					state.m_comp_size = 0;

					if (tdefl_init(pComp, => zip_writer_add_put_buf_callback, &state, tdefl_create_comp_flags_from_zip_params(level, -15, .DEFAULT_STRATEGY)) != .TDEFL_STATUS_OKAY)
					{
						pZip.m_pFree(pZip.m_pAlloc_opaque, pComp);
						pZip.m_pFree(pZip.m_pAlloc_opaque, pRead_buf);
						fclose(pSrc_file);
						return false;
					}

					for (;;)
					{
						int in_buf_size = (uint32)Math.Min(uncomp_remaining, ZIP_MAX_IO_BUF_SIZE);
						TdeflStatus status;

						if (fread(pRead_buf, 1, in_buf_size, pSrc_file) != in_buf_size)
							break;

						uncomp_crc32 = (uint32)crc32(uncomp_crc32, (uint8*)pRead_buf, in_buf_size);
						uncomp_remaining -= in_buf_size;

						status = tdefl_compress_buffer(pComp, pRead_buf, in_buf_size, (uncomp_remaining != 0) ? .TDEFL_NO_FLUSH : .TDEFL_FINISH);
						if (status == .TDEFL_STATUS_DONE)
						{
							result = true;
							break;
						}
						else if (status != .TDEFL_STATUS_OKAY)
							break;
					}

					pZip.m_pFree(pZip.m_pAlloc_opaque, pComp);

					if (!result)
					{
						pZip.m_pFree(pZip.m_pAlloc_opaque, pRead_buf);
						fclose(pSrc_file);
						return false;
					}

					comp_size = state.m_comp_size;
					cur_archive_file_ofs = state.m_cur_archive_file_ofs;

					method = DEFLATED;
				}

				pZip.m_pFree(pZip.m_pAlloc_opaque, pRead_buf);
			}

			fclose(pSrc_file); pSrc_file = null;

			// no zip64 support yet
			if ((comp_size > 0xFFFFFFFF) || (cur_archive_file_ofs > 0xFFFFFFFF))
				return false;

			if (!zip_writer_create_local_dir_header(pZip, &local_dir_header, (uint16)archive_name_size, 0, uncomp_size, comp_size, uncomp_crc32, method, 0, dos_time, dos_date))
				return false;

			if (pZip.m_pWrite(pZip.m_pIO_opaque, local_dir_header_ofs, &local_dir_header, sizeof(decltype(local_dir_header))) != sizeof(decltype(local_dir_header)))
				return false;

			if (!zip_writer_add_to_central_dir(pZip, pArchive_name, (uint16)archive_name_size, null, 0, pComment, comment_size, uncomp_size, comp_size, uncomp_crc32, method, 0, dos_time, dos_date, local_dir_header_ofs, ext_attributes))
				return false;

			pZip.m_total_files++;
			pZip.m_archive_size = cur_archive_file_ofs;

			return true;
		}

		public static bool ZipWriterAddFromZipReader(ZipArchive* pZip, ZipArchive* pSource_zip, int32 file_index)
		{
			int32 n;
			uint32 bit_flags;
			int32 num_alignment_padding_bytes;
			int64 comp_bytes_remaining, local_dir_header_ofs;
			int64 cur_src_file_ofs, cur_dst_file_ofs;
			uint32[(ZIP_LOCAL_DIR_HEADER_SIZE + sizeof(uint32) - 1) / sizeof(uint32)] local_header_u32; uint8* pLocal_header = (uint8*)&local_header_u32;
			uint8[ZIP_CENTRAL_DIR_HEADER_SIZE] central_header = ?;
			int orig_central_dir_size;
			ZipInternalState* pState;
			void* pBuf; uint8* pSrc_central_header;

			if ((pZip == null) || (pZip.m_pState == null) || (pZip.m_zip_mode != .Writing))
				return false;
			if (null == (pSrc_central_header = ZipReaderGetCdh(pSource_zip, file_index)))
				return false;
			pState = pZip.m_pState;

			num_alignment_padding_bytes = zip_writer_compute_padding_needed_for_file_alignment(pZip);

			// no zip64 support yet
			if ((pZip.m_total_files == 0xFFFF) || ((pZip.m_archive_size + num_alignment_padding_bytes + ZIP_LOCAL_DIR_HEADER_SIZE + ZIP_CENTRAL_DIR_HEADER_SIZE) > 0xFFFFFFFF))
				return false;

			cur_src_file_ofs = ReadLE32!(pSrc_central_header + ZIP_CDH_LOCAL_HEADER_OFS);
			cur_dst_file_ofs = pZip.m_archive_size;

			if (pSource_zip.m_pRead(pSource_zip.m_pIO_opaque, cur_src_file_ofs, pLocal_header, ZIP_LOCAL_DIR_HEADER_SIZE) != ZIP_LOCAL_DIR_HEADER_SIZE)
				return false;
			if (ReadLE32!(pLocal_header) != ZIP_LOCAL_DIR_HEADER_SIG)
				return false;
			cur_src_file_ofs += ZIP_LOCAL_DIR_HEADER_SIZE;

			if (!ZipWriterWriteZeros(pZip, cur_dst_file_ofs, num_alignment_padding_bytes))
				return false;
			cur_dst_file_ofs += num_alignment_padding_bytes;
			local_dir_header_ofs = cur_dst_file_ofs;
			if (pZip.m_file_offset_alignment != 0) { Debug.Assert((local_dir_header_ofs & (pZip.m_file_offset_alignment - 1)) == 0); }

			if (pZip.m_pWrite(pZip.m_pIO_opaque, cur_dst_file_ofs, pLocal_header, ZIP_LOCAL_DIR_HEADER_SIZE) != ZIP_LOCAL_DIR_HEADER_SIZE)
				return false;
			cur_dst_file_ofs += ZIP_LOCAL_DIR_HEADER_SIZE;

			n = ReadLE16!(pLocal_header + ZIP_LDH_FILENAME_LEN_OFS) + ReadLE16!(pLocal_header + ZIP_LDH_EXTRA_LEN_OFS);
			comp_bytes_remaining = n + (int32)ReadLE32!(pSrc_central_header + ZIP_CDH_COMPRESSED_SIZE_OFS);

			if (null == (pBuf = pZip.m_pAlloc(pZip.m_pAlloc_opaque, 1, (int)Math.Max(sizeof(uint32) * 4, Math.Min(ZIP_MAX_IO_BUF_SIZE, comp_bytes_remaining)))))
				return false;

			while (comp_bytes_remaining != 0)
			{
				n = (int32)Math.Min(ZIP_MAX_IO_BUF_SIZE, comp_bytes_remaining);
				if (pSource_zip.m_pRead(pSource_zip.m_pIO_opaque, cur_src_file_ofs, pBuf, n) != n)
				{
					pZip.m_pFree(pZip.m_pAlloc_opaque, pBuf);
					return false;
				}
				cur_src_file_ofs += n;

				if (pZip.m_pWrite(pZip.m_pIO_opaque, cur_dst_file_ofs, pBuf, n) != n)
				{
					pZip.m_pFree(pZip.m_pAlloc_opaque, pBuf);
					return false;
				}
				cur_dst_file_ofs += n;

				comp_bytes_remaining -= n;
			}

			bit_flags = ReadLE16!(pLocal_header + ZIP_LDH_BIT_FLAG_OFS);
			if ((bit_flags & 8) != 0)
			{
			  // Copy data descriptor
				if (pSource_zip.m_pRead(pSource_zip.m_pIO_opaque, cur_src_file_ofs, pBuf, sizeof(uint32) * 4) != sizeof(uint32) * 4)
				{
					pZip.m_pFree(pZip.m_pAlloc_opaque, pBuf);
					return false;
				}

				n = (int32)sizeof(uint32) * ((ReadLE32!(pBuf) == 0x08074b50) ? 4 : 3);
				if (pZip.m_pWrite(pZip.m_pIO_opaque, cur_dst_file_ofs, pBuf, n) != n)
				{
					pZip.m_pFree(pZip.m_pAlloc_opaque, pBuf);
					return false;
				}

				cur_src_file_ofs += n;
				cur_dst_file_ofs += n;
			}
			pZip.m_pFree(pZip.m_pAlloc_opaque, pBuf);

			// no zip64 support yet
			if (cur_dst_file_ofs > 0xFFFFFFFF)
				return false;

			orig_central_dir_size = pState.m_central_dir.m_size;

			Internal.MemCpy(&central_header, pSrc_central_header, ZIP_CENTRAL_DIR_HEADER_SIZE);
			WRITE_LE32!(&central_header[ZIP_CDH_LOCAL_HEADER_OFS], local_dir_header_ofs);
			if (!zip_array_push_back(pZip, &pState.m_central_dir, &central_header, ZIP_CENTRAL_DIR_HEADER_SIZE))
				return false;

			n = (int32)ReadLE16!(pSrc_central_header + ZIP_CDH_FILENAME_LEN_OFS) + (int32)ReadLE16!(pSrc_central_header + ZIP_CDH_EXTRA_LEN_OFS) + (int32)ReadLE16!(pSrc_central_header + ZIP_CDH_COMMENT_LEN_OFS);
			if (!zip_array_push_back(pZip, &pState.m_central_dir, pSrc_central_header + ZIP_CENTRAL_DIR_HEADER_SIZE, n))
			{
				zip_array_resize(pZip, &pState.m_central_dir, orig_central_dir_size, false);
				return false;
			}

			if (pState.m_central_dir.m_size > 0xFFFFFFFF)
				return false;
			n = (int32)orig_central_dir_size;
			if (!zip_array_push_back(pZip, &pState.m_central_dir_offsets, &n, 1))
			{
				zip_array_resize(pZip, &pState.m_central_dir, orig_central_dir_size, false);
				return false;
			}

			pZip.m_total_files++;
			pZip.m_archive_size = cur_dst_file_ofs;

			return true;
		}

		static bool zip_writer_finalize_archive(ZipArchive* pZip)
		{
			ZipInternalState* pState;
			int64 central_dir_ofs, central_dir_size;
			uint8[ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE] hdr;

			if ((pZip == null) || (pZip.m_pState == null) || (pZip.m_zip_mode != .Writing))
				return false;

			pState = pZip.m_pState;

			// no zip64 support yet
			if ((pZip.m_total_files > 0xFFFF) || ((pZip.m_archive_size + pState.m_central_dir.m_size + ZIP_END_OF_CENTRAL_DIR_HEADER_SIZE) > 0xFFFFFFFF))
				return false;

			central_dir_ofs = 0;
			central_dir_size = 0;
			if (pZip.m_total_files != 0)
			{
			  // Write central directory
				central_dir_ofs = pZip.m_archive_size;
				central_dir_size = pState.m_central_dir.m_size;
				pZip.m_central_directory_file_ofs = central_dir_ofs;
				if (pZip.m_pWrite(pZip.m_pIO_opaque, central_dir_ofs, pState.m_central_dir.m_p, (int)central_dir_size) != central_dir_size)
					return false;
				pZip.m_archive_size += central_dir_size;
			}

			// Write end of central directory record
			hdr = default;
			WRITE_LE32!(&hdr[0] + ZIP_ECDH_SIG_OFS, ZIP_END_OF_CENTRAL_DIR_HEADER_SIG);
			WRITE_LE16!(&hdr[0] + ZIP_ECDH_CDIR_NUM_ENTRIES_ON_DISK_OFS, pZip.m_total_files);
			WRITE_LE16!(&hdr[0] + ZIP_ECDH_CDIR_TOTAL_ENTRIES_OFS, pZip.m_total_files);
			WRITE_LE32!(&hdr[0] + ZIP_ECDH_CDIR_SIZE_OFS, central_dir_size);
			WRITE_LE32!(&hdr[0] + ZIP_ECDH_CDIR_OFS_OFS, central_dir_ofs);

			if (pZip.m_pWrite(pZip.m_pIO_opaque, pZip.m_archive_size, &hdr, sizeof(decltype(hdr))) != sizeof(decltype(hdr)))
				return false;

			if ((pState.m_pFile != null) && (fflush(pState.m_pFile) == EOF))
				return false;

			pZip.m_archive_size += sizeof(decltype(hdr));

			pZip.m_zip_mode = .WritingHasBeenFinalized;
			return true;
		}

		static bool ZipWriterFinalizeHeapArchive(ZipArchive* pZip, void** pBuf, int* pSize)
		{
			if ((pZip == null) || (pZip.m_pState == null) || (pBuf == null) || (pSize == null))
				return false;
			if (pZip.m_pWrite != => zip_heap_write_func)
				return false;
			if (!zip_writer_finalize_archive(pZip))
				return false;

			*pBuf = pZip.m_pState.m_pMem;
			*pSize = pZip.m_pState.m_mem_size;
			pZip.m_pState.m_pMem = null;
			pZip.m_pState.m_mem_size = pZip.m_pState.m_mem_capacity = 0;
			return true;
		}

		static bool zip_writer_end(ZipArchive* pZip)
		{
			ZipInternalState* pState;
			bool status = true;
			if ((pZip == null) || (pZip.m_pState == null) || (pZip.m_pAlloc == null) || (pZip.m_pFree == null) ||
				((pZip.m_zip_mode != .Writing) && (pZip.m_zip_mode != .WritingHasBeenFinalized)))
				return false;

			pState = pZip.m_pState;
			pZip.m_pState = null;
			zip_array_clear(pZip, &pState.m_central_dir);
			zip_array_clear(pZip, &pState.m_central_dir_offsets);
			zip_array_clear(pZip, &pState.m_sorted_central_dir_offsets);


			if (pState.m_pFile != null)
			{
				fclose(pState.m_pFile);
				pState.m_pFile = null;
			}

			if ((pZip.m_pWrite == => zip_heap_write_func) && (pState.m_pMem != null))
			{
				pZip.m_pFree(pZip.m_pAlloc_opaque, pState.m_pMem);
				pState.m_pMem = null;
			}

			pZip.m_pFree(pZip.m_pAlloc_opaque, pState);
			pZip.m_zip_mode = .Invalid;
			return status;
		}

		[CRepr]
		struct FILE_STAT_STRUCT
		{
			public uint32 st_dev;
			public uint16 st_ino;
			public uint16 st_mode;
			public int16 st_nlink;
			public int16 st_uid;
			public int16 st_gid;
			public uint32 st_rdev;
			public int64 st_size;
			public time_t st_atime;
			public time_t st_mtime;
			public time_t st_ctime;
		};

		[CLink, StdCall]
		static extern int32 _fstat64i32(char8* fileName, FILE_STAT_STRUCT* stat);

		bool zip_add_mem_to_archive_file_in_place(char8* pZip_filename, char8* pArchive_name, void* pBuf, int buf_size, void* pComment, uint16 comment_size, ZipFlags level_and_flags_in)
		{
			ZipFlags level_and_flags = level_and_flags_in;
			bool status, created_new_archive = false;
			ZipArchive zip_archive;
			FILE_STAT_STRUCT file_stat;
			zip_archive = default;
			if ((int32)level_and_flags < 0)
				level_and_flags = (ZipFlags)CompressionLevel.DEFAULT_LEVEL;
			let level = (CompressionLevel)((int32)level_and_flags & 0xF);
			if ((pZip_filename == null) || (pArchive_name == null) || ((buf_size != 0) && (pBuf == null)) || ((comment_size != 0) && (pComment == null)) || (level > .UBER_COMPRESSION))
				return false;
			if (!zip_writer_validate_archive_name(pArchive_name))
				return false;
			if (_fstat64i32(pZip_filename, &file_stat) != 0)
			{
			  // Create a new archive.
				if (!ZipWriterInitFile(&zip_archive, pZip_filename, 0))
					return false;
				created_new_archive = true;
			}
			else
			{
			  // Append to an existing archive.
				if (!ZipReaderInitFile(&zip_archive, pZip_filename, level_and_flags | .DoNotSortCentralDirectory))
					return false;
				if (!zip_writer_init_from_reader(&zip_archive, pZip_filename))
				{
					ZipReaderEnd(&zip_archive);
					return false;
				}
			}
			status = ZipWriterAddMemEx(&zip_archive, pArchive_name, pBuf, buf_size, pComment, comment_size, level_and_flags, 0, 0);
			// Always finalize, even if adding failed for some reason, so we have a valid central directory. (This may not always succeed, but we can try.)
			if (!zip_writer_finalize_archive(&zip_archive))
				status = false;
			if (!zip_writer_end(&zip_archive))
				status = false;
			if ((!status) && (created_new_archive))
			{
			  // It's a new archive and something went wrong, so just delete it.
				File.Delete(scope String(pZip_filename)).IgnoreError();
			}
			return status;
		}

		static void* zip_extract_archive_file_to_heap(char8* pZip_filename, char8* pArchive_name, int* pSize, ZipFlags flags)
		{
			int32 file_index;
			ZipArchive zip_archive;
			void* p = null;

			if (pSize != null)
				*pSize = 0;

			if ((pZip_filename == null) || (pArchive_name == null))
				return null;

			zip_archive = default;
			if (!ZipReaderInitFile(&zip_archive, pZip_filename, flags | .DoNotSortCentralDirectory))
				return null;

			if ((file_index = zip_reader_locate_file(&zip_archive, pArchive_name, null, flags)) >= 0)
				p = ZipReaderExtractToHeap(&zip_archive, file_index, pSize, flags);

			ZipReaderEnd(&zip_archive);
			return p;
		}
	}
}
