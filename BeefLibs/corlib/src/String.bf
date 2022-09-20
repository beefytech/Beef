// This file contains portions of code released by Microsoft under the MIT license as part
// of an open-sourcing initiative in 2014 of the C# core libraries.
// The original source was submitted to https://github.com/Microsoft/referencesource

using System.Diagnostics.Contracts;
using System.Collections;
using System.Diagnostics;
using System.Text;
using System.Threading;
using System.Interop;
using System;

namespace System
{
	// String size type
#if BF_LARGE_STRINGS
	typealias int_strsize = int64;
	typealias uint_strsize = uint64;
#else
	typealias int_strsize = int32;
	typealias uint_strsize = uint32;
#endif

	enum UnicodeNormalizationOptions
	{
		Stable = (1<<1),
		Compat = (1<<2),
		Compose = (1<<3),
		Decompose = (1<<4),
		CaseFold = (1<<10),
		CharBound = (1<<11),
		Lump = (1<<12),

		NFD = Stable | Decompose,
		NFC = Stable | Compose,
		NFKD = Stable | Decompose | Compat,
		NFKC = Stable | Compose | Compat,
	}

	[Ordered]
	class String : IHashable, IFormattable, IPrintable
	{
		public enum CreateFlags
		{
			None = 0,
			NullTerminate = 1
		}

		int_strsize mLength;
		uint_strsize mAllocSizeAndFlags;
		char8* mPtrOrBuffer = null;

		extern const String* sStringLiterals;
		extern const String* sIdStringLiterals;
		static String* sPrevInternLinkPtr; // For detecting changes to sStringLiterals for hot loads
		static Monitor sMonitor = new Monitor() ~ delete _;
		static HashSet<String> sInterns = new .() ~ delete _;
		static List<String> sOwnedInterns = new .() ~ DeleteContainerAndItems!(_);
		public const String Empty = "";

#if BF_LARGE_STRINGS
		const uint64 cSizeFlags = 0x3FFFFFFF'FFFFFFFF;
		const uint64 cDynAllocFlag = 0x80000000'00000000;
		const uint64 cStrPtrFlag = 0x40000000'00000000;
#else
		const uint32 cSizeFlags = 0x3FFFFFFF;
		const uint32 cDynAllocFlag = 0x80000000;
		const uint32 cStrPtrFlag = 0x40000000;
#endif

		[AllowAppend]
		public this(int count) // 0
		{
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = 0;
		}

		[AllowAppend]
		public this()
		{
		    let bufferSize = 16 - sizeof(char8*);
#unwarn
		    char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
		    mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
		    mLength = 0;
		}

		[AllowAppend]
		public this(String str)
		{
			let count = str.mLength;
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			Internal.MemCpy(Ptr, str.Ptr, count);
			mLength = count;
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
		}

		[AllowAppend]
		public this(String str, int offset)
		{
			Debug.Assert((uint)offset <= (uint)str.Length);
			let count = str.mLength - offset;
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			let srcPtr = str.Ptr;
			for (int_strsize i = 0; i < count; i++)
			    ptr[i] = srcPtr[i + offset];
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = (int_strsize)count;
		}

		[AllowAppend]
		public this(String str, int offset, int count)
		{
			Debug.Assert((uint)offset <= (uint)str.Length);
			Debug.Assert(offset + count <= str.Length);

			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			let srcPtr = str.Ptr;
			for (int i = 0; i < count; i++)
			    ptr[i] = srcPtr[i + offset];
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = (int_strsize)count;
		}

		[AllowAppend]
		public this(char8 c, int count)
		{
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			for (int_strsize i = 0; i < count; i++)
			    ptr[i] = c;
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = (int_strsize)count;
		}

		[AllowAppend]
		public this(char8* char8Ptr)
		{
			let count = Internal.CStrLen(char8Ptr);
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			for (int_strsize i = 0; i < count; i++)
			    ptr[i] = char8Ptr[i];
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = count;
		}

		[AllowAppend]
		public this(char8* char8Ptr, int count)
		{
		    int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
		    char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
		    for (int i = 0; i < count; i++)
		        ptr[i] = char8Ptr[i];
		    mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
		    mLength = (int_strsize)count;
		}

		[AllowAppend]
		public this(char16* char16Ptr)
		{
			let count = UTF16.GetLengthAsUTF8(char16Ptr);
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = 0;
			UTF16.Decode(char16Ptr, this);
		}

		[AllowAppend]
		public this(Span<char16> chars)
		{
			let count = UTF16.GetLengthAsUTF8(chars);
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = 0;
			UTF16.Decode(chars, this);
		}

		[AllowAppend]
		public this(StringView strView)
		{			
			let count = strView.Length;
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			Internal.MemCpy(ptr, strView.Ptr, strView.Length);
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = (int_strsize)strView.Length;
		}

		[AllowAppend]
		public this(StringView strView, CreateFlags flags)
		{			
			let count = strView.Length + (flags.HasFlag(.NullTerminate) ? 1 : 0);
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			Internal.MemCpy(ptr, strView.Ptr, strView.Length);
			if (flags.HasFlag(.NullTerminate))
				ptr[strView.Length] = 0;
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int32)sizeof(char8*);
			mLength = (int32)strView.Length;
		}

		[AllowAppend]
		public this(StringView strView, int offset)
		{
			Debug.Assert((uint)offset <= (uint)strView.Length);
			
			let count = strView.Length - offset;
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			let srcPtr = strView.Ptr;
			for (int i = 0; i < count; i++)
			    ptr[i] = srcPtr[i + offset];
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = (int_strsize)count;
		}

		[AllowAppend]
		public this(StringView strView, int offset, int count)
		{
			Debug.Assert((uint)offset <= (uint)strView.Length);
			Debug.Assert(offset + count <= strView.Length);

			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			let srcPtr = strView.Ptr;
			for (int i = 0; i < count; i++)
			    ptr[i] = srcPtr[i + offset];
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = (int_strsize)count;
		}

		[AllowAppend]
		public this(char8[] chars, int offset, int count)
		{
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			for (int i = 0; i < count; i++)
			    ptr[i] = chars[i + offset];
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
			mLength = (int_strsize)count;
		}

		static int StrLengths(String[] strs)
		{
			int count = 0;
			for (var str in strs)
				count += str.Length;
			return count;
		}

		[AllowAppend]
		public this(params String[] strs)
		{
			int count = StrLengths(strs);
			int bufferSize = (count == 0) ? 0 : (count - 1) & ~(sizeof(char8*) - 1);
#unwarn
			char8* addlPtr = append char8[bufferSize]*(?);
			Init(bufferSize);
			let ptr = Ptr;
			int curIdx = 0;
			for (var str in strs)
			{
				Internal.MemCpy(ptr + curIdx, str.Ptr, str.mLength);
				curIdx += str.Length;
			}
			
			mLength = (int_strsize)count;
			mAllocSizeAndFlags = (uint_strsize)bufferSize + (int_strsize)sizeof(char8*);
		}

#if !VALGRIND
		[SkipCall]
#endif
		void Init(int appendSize)
		{
			Internal.MemSet(Ptr, 0, appendSize + (int_strsize)sizeof(char8*));
		}

		public ~this()
		{
			if (IsDynAlloc)
			    delete:this mPtrOrBuffer;
		}

		void FakeMethod ()
		{

		}

		protected virtual void* Alloc(int size, int align)
		{
			return new char8[size]* (?);
		}

		protected virtual void Free(void* ptr)
		{
			delete ptr;
		}

		public int Length
		{
			[Inline]
			get
			{
				return mLength;
			}

			set
			{
				Debug.Assert((uint)value <= (uint)mLength);
				mLength = (int_strsize)value;
			}
		}

		public int AllocSize
		{
			[Inline]
			get
			{
				return (int_strsize)(mAllocSizeAndFlags & cSizeFlags);
			}
		}

		public bool IsDynAlloc
		{
			[Inline]
			get
			{
				return (mAllocSizeAndFlags & cDynAllocFlag) != 0;
			}
		}

		public bool HasExternalPtr
		{
			get
			{
				return (mAllocSizeAndFlags & cStrPtrFlag) != 0;
			}
		}

		public char8* Ptr
		{
			//[Optimize]
			get
			{
				return ((mAllocSizeAndFlags & cStrPtrFlag) != 0) ? mPtrOrBuffer : (char8*)&mPtrOrBuffer;
			}
		}

		public bool IsWhiteSpace
		{
			get
			{
				let ptr = Ptr;
				for (int i = 0; i < mLength; i++)
					if (!ptr[i].IsWhiteSpace)
						return false;
				return true;
			}
		}

		public bool IsEmpty
		{
			get
			{
				return mLength == 0;
			}
		}

		static int GetHashCode(char8* ptr, int length)
		{
			int charsLeft = length;
			int hash = 0;
			char8* curPtr = ptr;
			let intSize = sizeof(int);
			while (charsLeft >= intSize)
			{
				hash = (hash ^ *((int*)curPtr)) &+ (hash &* 16777619);
				charsLeft -= intSize;
				curPtr += intSize;
			}

			while (charsLeft > 1)
			{
				hash = ((hash ^ (int)*curPtr) << 5) &- hash;
				charsLeft--;
				curPtr++;
			}

			return hash;
		}

		public int GetHashCode()
		{
			return GetHashCode(Ptr, mLength);
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append(this);
		}
		
		[Obsolete("Replaced with Quote", false)]
		public static void QuoteString(char8* ptr, int length, String outString) => Quote(ptr, length, outString);

		public static void Quote(char8* ptr, int length, String outString)
		{
			outString.Append('"');
			Escape(ptr, length, outString);
			outString.Append('"');
		}

		public void Quote(String outString)
		{
			Quote(Ptr, Length, outString);
		}

		public static void Escape(char8* ptr, int length, String outString)
		{
			for (int i < length)
			{
				char8 c = ptr[i];
				switch (c)
				{
				case '\'': outString.Append(@"\'");
			    case '\"': outString.Append("\\\"");
			    case '\\': outString.Append(@"\\");
			    case '\0': outString.Append(@"\0");
			    case '\a': outString.Append(@"\a");
			    case '\b': outString.Append(@"\b");
			    case '\f': outString.Append(@"\f");
			    case '\n': outString.Append(@"\n");
			    case '\r': outString.Append(@"\r");
			    case '\t': outString.Append(@"\t");
			    case '\v': outString.Append(@"\v");
			    default: 
			    	if (c < (char8)32)
			    	{
			    		outString.Append(@"\x");
			    		outString.Append(sHexUpperChars[((int)c>>4) & 0xF]);
			    		outString.Append(sHexUpperChars[(int)c & 0xF]);
			    		break;
			    	}
			    	outString.Append(c);
				}
			}
		}

		public void Escape(String outString)
		{
			Escape(Ptr, Length, outString);
		}

		[Obsolete("Replaced with Unquote", false)]
		public static Result<void> UnQuoteString(char8* ptr, int length, String outString) => Unquote(ptr, length, outString);

		public static Result<void> Unquote(char8* ptr, int length, String outString)
		{
			if (length < 2)
				return .Err;

			// Literal string?
			if ((ptr[0] == '@') && (ptr[1] == '"') && (ptr[length - 1] == '\"'))
			{
				outString.Append(ptr + 2, length - 3);
				return .Ok;
			}

			if ((*ptr != '\"') && (ptr[length - 1] != '\"'))
				return .Err;

			return Unescape(ptr + 1, length - 2, outString);
		}

		public Result<void> Unquote(String outString)
		{
			return Unquote(Ptr, Length, outString);
		}

		public static Result<void> Unescape(char8* ptr, int length, String outString)
		{
			var ptr;
			char8* endPtr = ptr + length;

			while (ptr < endPtr)
			{
				char8 c = *(ptr++);
				if (c == '\\')
				{
					if (ptr == endPtr)
						return .Err;

					char8 nextC = *(ptr++);
					switch (nextC)
					{
					case '\'': outString.Append("'");
					case '\"': outString.Append("\"");
					case '\\': outString.Append("\\");
					case '0': outString.Append("\0");
					case 'a': outString.Append("\a");
					case 'b': outString.Append("\b");
					case 'f': outString.Append("\f");
					case 'n': outString.Append("\n");
					case 'r': outString.Append("\r");
					case 't': outString.Append("\t");
					case 'v': outString.Append("\v");
					case 'x':
						uint8 num = 0;
						for (let i < 2)
						{
						if (ptr == endPtr)
							return .Err;
						let hexC = *(ptr++);

						if ((hexC >= '0') && (hexC <= '9'))
							num = num*0x10 + (uint8)(hexC - '0');
						else if ((hexC >= 'A') && (hexC <= 'F'))
							num = num*0x10 + (uint8)(hexC - 'A') + 10;
						else if ((hexC >= 'a') && (hexC <= 'f'))
							num = num*0x10 + (uint8)(hexC - 'a') + 10;
						else return .Err;
						}

						outString.Append((char8)num);
					default:
						return .Err;
					}
					continue;
				}

				outString.Append(c);
			}

			return .Ok;
		}

		public Result<void> Unescape(String outString)
		{
			return Unescape(Ptr, Length, outString);
		}

		static String sHexUpperChars = "0123456789ABCDEF";
		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if (format == "Q")
			{
				Quote(Ptr, mLength, outString);
				return;
			}
			outString.Append(this);
		}

		void IPrintable.Print(String outString)
		{
			String.Quote(Ptr, mLength, outString);
		}

		[AlwaysInclude]
		public char8* CStr()
		{
			EnsureNullTerminator();
			return Ptr;
		}

		public static implicit operator char8*(String str)
		{
		    if (str == null)
				return null;
			str.EnsureNullTerminator();
			return str.Ptr;
		}

		public static implicit operator Span<char8>(String str)
		{
		    if (str == null)
				return .(null, 0);
			return .(str.Ptr, str.Length);
		}

		[Commutable]
		public static bool operator==(String s1, String s2)
		{
			return Equals(s1, s2);
		}

		public static int operator<=>(String s1, String s2)
		{
			return String.Compare(s1, s2, false);
		}

		public void Clear()
		{
			mLength = 0;
		}

		public void Set(String str)
		{
			if ((Object)this == (Object)str)
				return;
			mLength = 0;
            Append(str.Ptr, str.mLength);
		}

		public void Set(StringView str)
		{
			mLength = 0;
			Append(str.Ptr, str.Length);
		}

		public void MoveTo(String str, bool keepRef = false)
		{
			if (IsDynAlloc)
			{
				if (str.IsDynAlloc)
				{
					delete str.mPtrOrBuffer;
				}
	
				str.mPtrOrBuffer = mPtrOrBuffer;
				str.mAllocSizeAndFlags = mAllocSizeAndFlags;
				str.mLength = mLength;

				if (keepRef)
				{
					mAllocSizeAndFlags &= ~cDynAllocFlag;
				}
				else
				{
					mPtrOrBuffer = null;
					mAllocSizeAndFlags = (int_strsize)sizeof(char8*);
					mLength = 0;
				}
			}
			else
			{
				str.Set(this);
				if (!keepRef)
					Clear();
			}
		}

		public void Reference(String str)
		{
			if (IsDynAlloc)
				delete:this mPtrOrBuffer;
			mPtrOrBuffer = str.Ptr;
			mLength = str.mLength;
			mAllocSizeAndFlags = cStrPtrFlag;
		}

		public void Reference(char8* ptr, int length, int allocSize)
		{
			if (IsDynAlloc)
				delete:this mPtrOrBuffer;
			mPtrOrBuffer = ptr;
			mLength = (int_strsize)length;
			mAllocSizeAndFlags = cStrPtrFlag;
		}

		public void Reference(char8* ptr, int length)
		{
			if (IsDynAlloc)
				delete:this mPtrOrBuffer;
			mPtrOrBuffer = ptr;
			mLength = (int_strsize)length;
			mAllocSizeAndFlags = cStrPtrFlag;
		}

		public void Reference(StringView stringView)
		{
			Reference(stringView.Ptr, stringView.Length, stringView.Length);
		}

		public void Reference(char8* ptr)
		{
			if (IsDynAlloc)
				delete:this mPtrOrBuffer;
			mPtrOrBuffer = ptr;
			mLength = StrLen(ptr);
			mAllocSizeAndFlags = cStrPtrFlag;
		}

		// This is a fast way to remove characters are the beginning of a string, but only works for strings
		//  that are not dynamically allocated.  Mostly useful for parsing through Referenced strings quickly.
		public void AdjustPtr(int adjBytes)
		{
			Debug.Assert(!IsDynAlloc);
			Debug.Assert(AllocSize == 0); // Assert is reference
			Debug.Assert((uint)mLength >= (uint)adjBytes);
			mPtrOrBuffer += adjBytes;
			mLength -= (int_strsize)adjBytes;
		}

		int CalcNewSize(int minSize)
		{
			// Grow factor is 1.5
			int bumpSize = AllocSize;
			bumpSize += bumpSize / 2;
			return (bumpSize > minSize) ? bumpSize : minSize;
		}

		[Inline]
		void CalculatedReserve(int newSize)
		{
			if (newSize > AllocSize)
				Realloc(CalcNewSize(newSize));
		}

		void Realloc(int newSize)
		{
			Debug.Assert(AllocSize > 0, "String has been frozen");
			Debug.Assert((uint)newSize <= cSizeFlags);
			char8* newPtr = new:this char8[newSize]* (?);
			Internal.MemCpy(newPtr, Ptr, mLength);
#if VALGRIND
			Internal.MemSet(newPtr + mLength, 0, newSize - mLength);
#endif
			if (IsDynAlloc)
				delete:this mPtrOrBuffer;
			mPtrOrBuffer = newPtr;
			mAllocSizeAndFlags = (uint_strsize)newSize | cDynAllocFlag | cStrPtrFlag;
		}

		public void Reserve(int size)
		{
			if (size > AllocSize)
				Realloc(size);
		}

		void Realloc(char8* newPtr, int newSize)
		{
			Debug.Assert(AllocSize > 0, "String has been frozen");
			Debug.Assert((uint)newSize <= cSizeFlags);
			Internal.MemCpy(newPtr, Ptr, mLength);
			if (IsDynAlloc)
				delete:this mPtrOrBuffer;
			mPtrOrBuffer = newPtr;
			mAllocSizeAndFlags = (uint_strsize)newSize | cDynAllocFlag | cStrPtrFlag;
		}

		public static int_strsize StrLen(char8* str)
		{
			for (int_strsize i = 0; true; i++)
				if (str[i] == (char8)0)
					return i;
		}

		[NoDiscard]
		public StringView Substring(int pos)
		{
			return .(this, pos);
		}

		[NoDiscard]
		public StringView Substring(int pos, int length)
		{
			return .(this, pos, length);
		}

		public void Append(StringView strView)
		{
			Append(strView.Ptr, strView.Length);
		}

		public void Append(Span<char16> utf16Str)
		{
			UTF16.Decode(utf16Str, this);
		}

		public void Append(char16* utf16Str)
		{
			UTF16.Decode(utf16Str, this);
		}

		public void Append(StringView str, int offset)
		{
			Debug.Assert((uint)offset <= (uint)str.[Friend]mLength);
			Append(str.Ptr + offset, str.[Friend]mLength - offset);
		}

		public void Append(StringView str, int offset, int length)
		{
			Debug.Assert((uint)offset + (uint)length <= (uint)str.[Friend]mLength);
			Append(str.Ptr + offset, length);
		}

		public void Append(char8* appendPtr)
		{
			int_strsize length = StrLen(appendPtr);
			int_strsize newCurrentIndex = mLength + length;
			char8* ptr;
			if (newCurrentIndex > AllocSize)
			{
				// This handles appending to ourselves, we invalidate 'ptr' after calling Realloc
				int newSize = CalcNewSize(newCurrentIndex);
				char8* newPtr = new:this char8[newSize]* (?);
#if VALGRIND
				Internal.MemSet(newPtr, 0, newSize);
#endif
				Internal.MemCpy(newPtr + mLength, appendPtr, length);
				Realloc(newPtr, newSize);
				ptr = newPtr;
			}
			else
			{
				ptr = Ptr;
				Internal.MemCpy(ptr + mLength, appendPtr, length);
			}
			mLength = newCurrentIndex;
		}

		public void Append(char8* appendPtr, int length)
		{
			int newCurrentIndex = mLength + length;
			char8* ptr;
			if (newCurrentIndex > AllocSize)
			{
				// This handles appending to ourselves, we invalidate 'ptr' after calling Realloc
				int newSize = CalcNewSize(newCurrentIndex);
				char8* newPtr = new:this char8[newSize]* (?);
#if VALGRIND
				Internal.MemSet(newPtr, 0, newSize);
#endif
				Internal.MemCpy(newPtr + mLength, appendPtr, length);
				Realloc(newPtr, newSize);
				ptr = newPtr;
			}
			else
			{
				ptr = Ptr;
				Internal.MemCpy(ptr + mLength, appendPtr, length);
			}
			mLength = (int_strsize)newCurrentIndex;
		}

		public void Append(char8[] arr, int idx, int length)
		{
			int newCurrentIndex = mLength + length;
			char8* ptr;
			if (newCurrentIndex > AllocSize)
			{
				// This handles appending to ourselves, we invalidate 'ptr' after calling Realloc
				int newSize = CalcNewSize(newCurrentIndex);
				char8* newPtr = new:this char8[newSize]* (?);
#if VALGRIND
				Internal.MemSet(newPtr, 0, newSize);
#endif
				Internal.MemCpy(newPtr + mLength, arr.CArray() + idx, length);
				Realloc(newPtr, newSize);
				ptr = newPtr;
			}
			else
			{
				ptr = Ptr;
				Internal.MemCpy(ptr + mLength, arr.CArray() + idx, length);
			}
			mLength = (int_strsize)newCurrentIndex;
		}

		public char8* PrepareBuffer(int bytes)
		{
			Debug.Assert(bytes >= 0);
			if (bytes <= 0)
				return null;
			int count = bytes;
			CalculatedReserve(mLength + count + 1);
			char8* ptr = Ptr + mLength;
			mLength += (int_strsize)bytes;
			return ptr;
		}

        // Appends a copy of this string at the end of this string builder.
		public void Append(String value)
		{
            //Contract.Ensures(Contract.Result<String>() != null);
			Append(value.Ptr, value.mLength);
		}

		public void Append(String str, int offset)
		{
			Debug.Assert((uint)offset <= (uint)str.mLength);
			Append(str.Ptr + offset, str.mLength - offset);
		}

		public void Append(String str, int offset, int length)
		{
			Debug.Assert((uint)offset <= (uint)str.mLength);
			Debug.Assert(length >= 0);
			Debug.Assert((uint)offset + (uint)length <= (uint)str.mLength);
			Append(str.Ptr + offset, length);
		}

		public void Append(params String[] strings)
		{
			for (var str in strings)
				Append(str);
		}

		public void Append(char8 c)
		{
			CalculatedReserve(mLength + 1);
			let ptr = Ptr;
			ptr[mLength++] = c;
		}

		public void Append(char8 c, int count)
		{
			if (count <= 0)
				return;

			CalculatedReserve(mLength + count);
			let ptr = Ptr;
			for (int_strsize i = 0; i < count; i++)
				ptr[mLength++] = c;
		}

		public void Append(char32 c)
		{
			if (c < (char32)0x80)
		    {
				CalculatedReserve(mLength + 1);
				let ptr = Ptr;
			    ptr[mLength++] = (char8)c;
			}
			else if (c < (char32)0x800)
		    {
				CalculatedReserve(mLength + 2);
				let ptr = Ptr;
			    ptr[mLength++] = (char8)(c>>6) | (char8)0xC0;
			    ptr[mLength++] = (char8)(c & (char8)0x3F) | (char8)0x80;
			}
			else if (c < (char32)0x10000)
		    {
				CalculatedReserve(mLength + 3);
				let ptr = Ptr;
			    ptr[mLength++] = (char8)(c>>12) | (char8)0xE0;
			    ptr[mLength++] = (char8)((c>>6) & (char8)0x3F) | (char8)0x80;
			    ptr[mLength++] = (char8)(c & (char8)0x3F) | (char8)0x80;
			}
			else if (c < (char32)0x110000)
		    {
				CalculatedReserve(mLength + 4);
				let ptr = Ptr;
			    ptr[mLength++] = (char8)((c>>18) | (char8)0xF0);
			    ptr[mLength++] = (char8)((c>>12) & (char8)0x3F) | (char8)0x80;
			    ptr[mLength++] = (char8)((c>>6) & (char8)0x3F) | (char8)0x80;
			    ptr[mLength++] = (char8)(c & (char8)0x3F) | (char8)0x80;
			}
		}

		public void Append(char32 c, int count)
		{
			if (count <= 0)
				return;

			if (count == 1)
			{
				Append(c);
				return;
			}

			int encodedLen = UTF8.GetEncodedLength(c);

			CalculatedReserve(mLength + count * encodedLen);

			let ptr = Ptr;
			for (int_strsize i = 0; i < count; i++)
			{
				if (c < (char32)0x80)
	            {
				    ptr[mLength++] = (char8)c;
				}
				else if (c < (char32)0x800)
	            {
				    ptr[mLength++] = (char8)(c>>6) | (char8)0xC0;
				    ptr[mLength++] = (char8)(c & (char8)0x3F) | (char8)0x80;
				}
				else if (c < (char32)0x10000)
	            {
				    ptr[mLength++] = (char8)(c>>12) | (char8)0xE0;
				    ptr[mLength++] = (char8)((c>>6) & (char8)0x3F) | (char8)0x80;
				    ptr[mLength++] = (char8)(c & (char8)0x3F) | (char8)0x80;
				}
				else if (c < (char32)0x110000)
	            {
				    ptr[mLength++] = (char8)((c>>18) | (char8)0xF0);
				    ptr[mLength++] = (char8)((c>>12) & (char8)0x3F) | (char8)0x80;
				    ptr[mLength++] = (char8)((c>>6) & (char8)0x3F) | (char8)0x80;
				    ptr[mLength++] = (char8)(c & (char8)0x3F) | (char8)0x80;
				}
			}
		}
		
		public void Append(Object object)
		{
			if (object == null)
				return;
			Append(object.ToString(.. scope .(128)));
		}

		public void operator+=(String str)
		{
			Append(str);
		}

		public void operator+=(StringView sv)
		{
			Append(sv);
		}

		public void operator+=(char8 c)
		{
			Append(c);
		}

		public void operator+=(char32 c)
		{
			Append(c);
		}

		public void operator+=(Object obj)
		{
			Append(obj);
		}

		[Error("String addition is not supported. Consider allocating a new string and using Append, Concat, or +=")]
		public static String operator+(String lhs, String rhs)
		{
			return lhs;
		}

		[Error("String addition is not supported. Consider allocating a new string and using Append, Concat, or +=")]
		public static String operator+(String lhs, StringView rhs)
		{
			return lhs;
		}

		[Error("String addition is not supported. Consider allocating a new string and using Append, Concat, or +=")]
		public static String operator+(String lhs, char32 rhs)
		{
			return lhs;
		}

		public void EnsureNullTerminator()
		{
			int allocSize = AllocSize;
			if ((allocSize == mLength) || (Ptr[mLength] != 0))
			{
				CalculatedReserve(mLength + 1);
				Ptr[mLength] = 0;
			}
		}

		public ref char8 this[int index]
		{
			[Checked]
			get
			{
				Debug.Assert((uint)index < (uint)mLength);
				return ref Ptr[index];
			}

			[Unchecked, Inline]
			get
			{
				return ref Ptr[index];
			}

			[Checked]
			set
			{
				Debug.Assert((uint)index < (uint)mLength);
				Ptr[index] = value;
			}

			[Unchecked, Inline]
			set
			{
				Ptr[index] = value;
			}
		}

		public ref char8 this[Index index]
		{
			[Checked]
			get
			{
				int idx;
				switch (index)
				{
				case .FromFront(let offset): idx = offset;
				case .FromEnd(let offset): idx = mLength - offset;
				}
				Debug.Assert((uint)idx < (uint)mLength);
				return ref Ptr[idx];
			}

			[Unchecked, Inline]
			get
			{
				int idx;
				switch (index)
				{
				case .FromFront(let offset): idx = offset;
				case .FromEnd(let offset): idx = mLength - offset;
				}
				return ref Ptr[idx];
			}

			[Checked]
			set
			{
				int idx;
				switch (index)
				{
				case .FromFront(let offset): idx = offset;
				case .FromEnd(let offset): idx = mLength - offset;
				}
				Debug.Assert((uint)idx < (uint)mLength);
				Ptr[idx] = value;
			}

			[Unchecked, Inline]
			set
			{
				int idx;
				switch (index)
				{
				case .FromFront(let offset): idx = offset;
				case .FromEnd(let offset): idx = mLength - offset;
				}
				Ptr[idx] = value;
			}
		}

		public StringView this[IndexRange range]
		{
#if !DEBUG
			[Inline]
#endif
			get
			{
				return StringView(Ptr, Length)[range];
			}
		}

		public void Concat(params Object[] objects)
		{
			// This reserves the correct number of characters if it can
			int totalLen = 0;
			for (var obj in objects)
			{
				if (var str = obj as String)
				{
					totalLen += str.Length;
				}
				else if (obj is char8)
				{
					totalLen++;
				}
				else
				{
					totalLen = 0;
					break;
				}
			}

			if (totalLen > 0)
				Reserve(mLength + totalLen);

			for (var obj in objects)
			{
				obj.ToString(this);
			}
		}

		public static String GetStringOrEmpty(String str)
		{
			return str ?? Empty;
		}

		public static bool IsNullOrEmpty(String str)
		{
			return (str == null) || (str.Length == 0);
		}

		public static bool IsNullOrWhiteSpace(String str)
		{
			if ((str == null) || (str.Length == 0))
				return true;

			let strPtr = str.Ptr;
			for (int_strsize i = 0; i < str.mLength; i++)
			{
				if (!strPtr[i].IsWhiteSpace)
					return false;
			}
			return true;
		}

		Result<void> FormatError()
		{
			return .Err;
		}

		/** Appends formatted text.
		* @param provider The format provider
		* @returns This method can fail if the format string is invalid.
		*/
		public Result<void> AppendF(IFormatProvider provider, StringView format, params Span<Object> args)
		{
			if (format.Ptr == null)
			{
				return .Err;
			}
			//Contract.Ensures(Contract.Result<StringBuilder>() != null);
			//Contract.EndContractBlock();

			int pos = 0;
			int len = format.Length;
			char8 ch = '\x00';

			/*ICustomFormatter cf = null;
			if (provider != null)
			{
				cf = (ICustomFormatter)provider.GetFormat(typeof(ICustomFormatter));
			}*/
			String s = null;
			String fmt = "";
			int autoArgIdx = 0;

			while (true)
			{
				int p = pos;
				int i = pos;
				while (pos < len)
				{
					ch = format[pos];

					pos++;
					if (ch == '}')
					{
						if (pos < len && format[pos] == '}') // Treat as escape character for }}
							pos++;
						else
							return FormatError();
					}

					if (ch == '{')
					{
						if (pos < len && format[pos] == '{') // Treat as escape character for {{
							pos++;
						else
						{
							pos--;
							break;
						}
					}

					Append(ch);
				}

				if (pos == len) break;
				pos++;
				int index = 0;
				if (pos == len || (ch = format[pos]) < '0' || ch > '9')
				{
					if ((pos < len) &&
						((ch == '}') || (ch == ':') || (ch == ',')))
						index = autoArgIdx++;
					else
						return FormatError();
				}
				else
				{
					repeat
					{
						index = index * 10 + ch - '0';
						pos++;
						if (pos == len) return FormatError();
						ch = format[pos];
					}
			        while (ch >= '0' && ch <= '9' && index < 1000000);
				}
				if (index >= args.Length) return FormatError();
				while (pos < len && (ch = format[pos]) == ' ') pos++;
				bool leftJustify = false;
				int width = 0;
				if (ch == ',')
				{
					pos++;
					while (pos < len && format[pos] == ' ') pos++;

					if (pos == len) return FormatError();
					ch = format[pos];
					if (ch == '-')
					{
						leftJustify = true;
						pos++;
						if (pos == len) return FormatError();
						ch = format[pos];
					}
					if (ch < '0' || ch > '9') return FormatError();
					repeat
					{
						width = width * 10 + ch - '0';
						pos++;
						if (pos == len) return FormatError();
						ch = format[pos];
					}
		            while (ch >= '0' && ch <= '9' && width < 1000000);
				}

				while (pos < len && (ch = format[pos]) == ' ') pos++;
				Object arg = args[index];
				if (ch == ':')
				{
					if (fmt == "")
						fmt = scope:: String(64);
					else
						fmt.Clear();

					bool isFormatEx = false;
					pos++;
					p = pos;
					i = pos;
					while (true)
					{
						if (pos == len) return FormatError();
						ch = format[pos];
						pos++;
						if (ch == '{')
						{
							isFormatEx = true;
							if (pos < len && format[pos] == '{')  // Treat as escape character for {{
								pos++;
							else
								return FormatError();
						}
						else if (ch == '}')
						{
							// We only treat '}}' as an escape character if the format had an opening '{'. Otherwise we just close on the first '}'
							if ((isFormatEx) && (pos < len && format[pos] == '}'))  // Treat as escape character for }}
								pos++;
							else
							{
								pos--;
								break;
							}
						}

						if (fmt == null)
						{
							fmt = scope:: String(0x100);
						}
						fmt.Append(ch);
					}
				}
				if (ch != '}') return FormatError();
				pos++;
				if (s == null)
					s = scope:: String(128);
				
				s.Clear();
				IFormattable formattableArg = arg as IFormattable;
				if (formattableArg != null)
					formattableArg.ToString(s, fmt, provider);
				else if (arg != null)
					arg.ToString(s);
				else
					s.Append("null");
				if (fmt != (Object)"")
					fmt.Clear();
				
				if (s == null) s = String.Empty;
				int pad = width - s.Length;
				if (!leftJustify && pad > 0) Append(' ', pad);
				Append(s);
				if (leftJustify && pad > 0) Append(' ', pad);
			}

			return .Ok;
		}

		public Result<void> AppendF(StringView format, params Span<Object> args)
		{
			return AppendF((IFormatProvider)null, format, params args);
		}

		public int IndexOf(StringView subStr, bool ignoreCase = false)
		{
			for (int ofs = 0; ofs <= Length - subStr.Length; ofs++)
			{
				if (Compare(Ptr+ofs, subStr.Length, subStr.Ptr, subStr.Length, ignoreCase) == 0)
					return ofs;
			}

			return -1;
		}

		public int IndexOf(StringView subStr, int startIdx, bool ignoreCase = false)
		{
			for (int ofs = startIdx; ofs <= Length - subStr.Length; ofs++)
			{
				if (Compare(Ptr+ofs, subStr.Length, subStr.Ptr, subStr.Length, ignoreCase) == 0)
					return ofs;
			}

			return -1;
		}

		public int Count(char8 c)
		{
			int count = 0;
			let ptr = Ptr;
			for (int i = 0; i < mLength; i++)
				if (ptr[i] == c)
					count++;
			return count;
		}

		public int IndexOf(char8 c, int startIdx = 0)
		{
			let ptr = Ptr;
			for (int i = startIdx; i < mLength; i++)
				if (ptr[i] == c)
					return i;
			return -1;
		}

		public int LastIndexOf(char8 c)
		{
			let ptr = Ptr;
			for (int i = mLength - 1; i >= 0; i--)
				if (ptr[i] == c)
					return i;
			return -1;
		}

		public int LastIndexOf(char8 c, int startCheck)
		{
			let ptr = Ptr;
			for (int i = startCheck; i >= 0; i--)
				if (ptr[i] == c)
					return i;
			return -1;
		}

		public int IndexOfAny(char8[] targets)
		{
			return IndexOfAny(targets, 0, mLength);
		}

		public int IndexOfAny(char8[] targets, int startIdx)
		{
			return IndexOfAny(targets, startIdx, mLength - startIdx);
		}

		public int IndexOfAny(char8[] targets, int startIdx, int count)
		{
			let ptr = Ptr;
			for (var i = startIdx; i < count; i++)
			{
				let ch = ptr[i];
				for (let tag in targets)
				{
					if (ch == tag)
						return i;
				}
			}

			return -1;
		}

		public bool Contains(StringView str, bool ignoreCase = false)
		{
			return IndexOf(str, ignoreCase) != -1;
		}

		public bool Contains(char8 c)
		{
			let ptr = Ptr;
			for (int_strsize i = 0; i < mLength; i++)
				if (ptr[i] == c)
					return true;
			return false;
		}

		public void Replace(char8 c, char8 newC)
		{
			let ptr = Ptr;
			for (int_strsize i = 0; i < mLength; i++)
				if (ptr[i] == c)
					ptr[i] = newC;
		}

		void CaseConv(bool toUpper)
		{
			let ptr = Ptr;
			int addSize = 0;
			bool sizeChanged = false;

			for (int i = 0; i < mLength; i++)
			{
				let c = toUpper ? ptr[i].ToUpper : ptr[i].ToLower;
				if (c < '\x80')
				{
					ptr[i] = c;
					continue;
				}

				var (c32, decLen) = UTF8.Decode(ptr + i, mLength - i);
				if (c32 == (char32)-1)
					return; // Error

				let uc32 = toUpper ? c32.ToUpper : c32.ToLower;
				int encLen = UTF8.Encode(uc32, .(ptr + i, decLen));
				if (encLen != decLen)
				{
					// Put old char back
					sizeChanged = true;
					addSize += encLen - decLen;
					encLen = UTF8.Encode(c32, .(ptr + i, decLen));
				}

				i += encLen - 1;
			}
			if (!sizeChanged)
				return;

			// Handle 'slow' resize case
			if (sizeChanged)
			{
				int newSize = mLength + addSize + 1;
				char8* newPtr = new:this char8[newSize]*;

				int outIdx = 0;
				for (int i = 0; i < mLength; i++)
				{
					let c = toUpper ? ptr[i].ToUpper : ptr[i].ToLower;
					if (c < '\x80')
					{
						newPtr[outIdx++] = c;
						continue;
					}

					var (c32, decLen) = UTF8.Decode(ptr + i, mLength - i);
					if (c32 == (char32)-1)
						return; // Error

					c32 = toUpper ? c32.ToUpper : c32.ToLower;
					int encLen = UTF8.Encode(c32, .(newPtr + outIdx, newSize - outIdx));
					i += decLen - 1;
					outIdx += encLen;
				}
				newPtr[outIdx] = '\0';

				if (IsDynAlloc)
					delete mPtrOrBuffer;
				mPtrOrBuffer = newPtr;
				mAllocSizeAndFlags = (uint_strsize)newSize | cDynAllocFlag | cStrPtrFlag;
			}
		}

		public void ToUpper()
		{
			CaseConv(true);
		}

		public void ToLower()
		{
			CaseConv(false);
		}

		[CallingConvention(.Cdecl)]
		static extern int UTF8GetAllocSize(char8* str, int strlen, int32 options);
		[CallingConvention(.Cdecl)]
		static extern int UTF8Map(char8* str, int strlen, char8* outStr, int outSize, int32 options);

		public Result<void> Normalize(UnicodeNormalizationOptions unicodeNormalizationOptions = .NFC)
		{
			int allocSize = UTF8GetAllocSize(Ptr, mLength, (int32)unicodeNormalizationOptions);
			if (allocSize < 0)
				return .Err;
			char8* newStr = (char8*)Alloc(allocSize, 1);
			int newLen = UTF8Map(Ptr, mLength, newStr, allocSize, (int32)unicodeNormalizationOptions);

			if (IsDynAlloc)
				delete:this mPtrOrBuffer;
			mPtrOrBuffer = newStr;
			mLength = (int_strsize)newLen;
			mAllocSizeAndFlags = (uint32)(allocSize) | cDynAllocFlag | cStrPtrFlag;
			return .Ok;
		}

		public Result<void> Normalize(String destStr, UnicodeNormalizationOptions unicodeNormalizationOptions = .NFC)
		{
			if (destStr == (Object)this)
				return Normalize(unicodeNormalizationOptions);

			int allocSize = UTF8GetAllocSize(Ptr, mLength, (int32)unicodeNormalizationOptions);
			if (allocSize < 0)
			{
				// Just append unnormalized
				destStr.Append(this);
				return .Err;
			}
			char8* newStr = (char8*)Alloc(allocSize, 1);
			int newLen = UTF8Map(Ptr, mLength, newStr, allocSize, (int32)unicodeNormalizationOptions);

			if (destStr.IsDynAlloc)
				delete:destStr destStr.mPtrOrBuffer;
			destStr.mPtrOrBuffer = newStr;
			destStr.mLength = (int_strsize)newLen;
			destStr.mAllocSizeAndFlags = (uint_strsize)(newLen + 1) | cDynAllocFlag | cStrPtrFlag;
			return .Ok;
		}

		public void Remove(int startIdx, int length)
		{
			Contract.Requires((startIdx >= 0) && (length >= 0) && (startIdx + length <= mLength));
			int moveCount = mLength - startIdx - length;
			let ptr = Ptr;
			if (moveCount > 0)
				Internal.MemMove(ptr + startIdx, ptr + startIdx + length, mLength - startIdx - length);
			mLength -= (int_strsize)length;
		}

		public void Remove(int char8Idx)
		{
			Remove(char8Idx, 1);
		}

		public void RemoveToEnd(int startIdx)
		{
			Remove(startIdx, mLength - startIdx);
		}

		public void RemoveFromEnd(int length)
		{
			Remove(mLength - length, length);
		}

		public void Insert(int idx, StringView addString)
		{
			Contract.Requires(idx >= 0);

			int_strsize length = (int_strsize)addString.Length;
			int_strsize newLength = mLength + length;
			CalculatedReserve(newLength);

			let moveChars = mLength - idx;
			let ptr = Ptr;
			if (moveChars > 0)
				Internal.MemMove(ptr + idx + length, ptr + idx, moveChars);
			Internal.MemCpy(ptr + idx, addString.Ptr, length);
			mLength = newLength;
		}

		public void Insert(int idx, char8 c)
		{
			Contract.Requires(idx >= 0);

			let newLength = mLength + 1;
			CalculatedReserve(newLength);

			let moveChars = mLength - idx;
			let ptr = Ptr;
			if (moveChars > 0)
				Internal.MemMove(ptr + idx + 1, ptr + idx, moveChars);
			ptr[idx] = c;
			mLength = newLength;
		}

		public void Insert(int idx, char8 c, int count)
		{
			Contract.Requires(idx >= 0);

			if (count <= 0)
				return;

			let newLength = mLength + (int_strsize)count;
			CalculatedReserve(newLength);

			let moveChars = mLength - idx;
			let ptr = Ptr;
			if (moveChars > 0)
				Internal.MemMove(ptr + idx + count, ptr + idx, moveChars);
			for (let i < count)
				ptr[idx + i] = c;
			mLength = newLength;
		}

		public void Insert(int idx, char32 c)
		{
			Contract.Requires(idx >= 0);

			let moveChars = mLength - idx;
			if (c < (char32)0x80)
		    {
				CalculatedReserve(mLength + 1);
				let ptr = Ptr;
				if (moveChars > 0)
					Internal.MemMove(ptr + idx + 1, ptr + idx, moveChars);
			    ptr[idx] = (char8)c;
				mLength++;
			}
			else if (c < (char32)0x800)
		    {
				CalculatedReserve(mLength + 2);
				let ptr = Ptr;
				if (moveChars > 0)
					Internal.MemMove(ptr + idx + 2, ptr + idx, moveChars);
			    ptr[idx] = (char8)(c>>6) | (char8)0xC0;
			    ptr[idx + 1] = (char8)(c & (char8)0x3F) | (char8)0x80;
				mLength += 2;
			}
			else if (c < (char32)0x10000)
		    {
				CalculatedReserve(mLength + 3);
				let ptr = Ptr;
				if (moveChars > 0)
					Internal.MemMove(ptr + idx + 3, ptr + idx, moveChars);
			    ptr[idx] = (char8)(c>>12) | (char8)0xE0;
			    ptr[idx + 1] = (char8)((c>>6) & (char8)0x3F) | (char8)0x80;
			    ptr[idx + 2] = (char8)(c & (char8)0x3F) | (char8)0x80;
				mLength += 3;
			}
			else if (c < (char32)0x110000)
		    {
				CalculatedReserve(mLength + 4);
				let ptr = Ptr;
				if (moveChars > 0)
					Internal.MemMove(ptr + idx + 4, ptr + idx, moveChars);
			    ptr[idx] = (char8)((c>>18) | (char8)0xF0);
			    ptr[idx + 1] = (char8)((c>>12) & (char8)0x3F) | (char8)0x80;
			    ptr[idx + 2] = (char8)((c>>6) & (char8)0x3F) | (char8)0x80;
			    ptr[idx + 3] = (char8)(c & (char8)0x3F) | (char8)0x80;
				mLength += 4;
			}
		}

		public void Insert(int idx, char32 c, int count)
		{
			Contract.Requires(idx >= 0);

			if (count <= 0)
				return;

			if (count == 1)
			{
				Insert(idx, c);
				return;
			}

			let encodedLen = UTF8.GetEncodedLength(c);
			let newLength = mLength + (int_strsize)(count * encodedLen);
			CalculatedReserve(newLength);

			let moveChars = mLength - idx;
			let ptr = Ptr;
			if (moveChars > 0)
				Internal.MemMove(ptr + idx + count * encodedLen, ptr + idx, moveChars);
			for (let i < count)
			{
				let ofs = idx + i * encodedLen;
				if (c < (char32)0x80)
	            {
				    ptr[ofs] = (char8)c;
				}
				else if (c < (char32)0x800)
	            {
				    ptr[ofs] = (char8)(c>>6) | (char8)0xC0;
				    ptr[ofs + 1] = (char8)(c & (char8)0x3F) | (char8)0x80;
				}
				else if (c < (char32)0x10000)
	            {
				    ptr[ofs] = (char8)(c>>12) | (char8)0xE0;
				    ptr[ofs + 1] = (char8)((c>>6) & (char8)0x3F) | (char8)0x80;
				    ptr[ofs + 2] = (char8)(c & (char8)0x3F) | (char8)0x80;
				}
				else if (c < (char32)0x110000)
	            {
				    ptr[ofs] = (char8)((c>>18) | (char8)0xF0);
				    ptr[ofs + 1] = (char8)((c>>12) & (char8)0x3F) | (char8)0x80;
				    ptr[ofs + 2] = (char8)((c>>6) & (char8)0x3F) | (char8)0x80;
				    ptr[ofs + 3] = (char8)(c & (char8)0x3F) | (char8)0x80;
				}
			}

			mLength = newLength;
		}

		static bool EqualsHelper(char8* a, char8* b, int length)
		{
			for (int i = 0; i < length; i++)
				if (a[i] != b[i])
					return false;
			return true;
		}

		static bool EqualsIgnoreCaseHelper(char8* a, char8* b, int length)
		{
			char8* curA = a;
			char8* curB = b;
			int curLength = length;

            /*Contract.Requires(strA != null);
            Contract.Requires(strB != null);
            Contract.EndContractBlock();*/
			while (curLength != 0)
			{
				int_strsize charA = (int_strsize)*curA;
				int_strsize charB = (int_strsize)*curB;

                //Contract.Assert((char8A | char8B) <= 0x7F, "strings have to be ASCII");

                // uppercase both chars - notice that we need just one compare per char
				if ((uint32)(charA &- 'a') <= (uint32)('z' - 'a')) charA -= 0x20;
				if ((uint32)(charB &- 'a') <= (uint32)('z' - 'a')) charB -= 0x20;

                //Return the (case-insensitive) difference between them.
				if (charA != charB)
					return false;

                // Next char
				curA++;curB++;
				curLength--;
			}

			return true;
		}

		private static int CompareOrdinalIgnoreCaseHelper(String strA, String strB)
		{
            /*Contract.Requires(strA != null);
            Contract.Requires(strB != null);
            Contract.EndContractBlock();*/
			int_strsize length = Math.Min(strA.mLength, strB.mLength);

			char8* a = strA.Ptr;
			char8* b = strB.Ptr;

			while (length != 0)
			{
				int_strsize charA = (int_strsize)*a;
				int_strsize charB = (int_strsize)*b;

                //Contract.Assert((char8A | char8B) <= 0x7F, "strings have to be ASCII");

                // uppercase both chars - notice that we need just one compare per char
				if ((uint32)(charA &- 'a') <= (uint32)('z' - 'a')) charA -= 0x20;
				if ((uint32)(charB &- 'a') <= (uint32)('z' - 'a')) charB -= 0x20;

                //Return the (case-insensitive) difference between them.
				if (charA != charB)
					return charA - charB;

                // Next char
				a++;b++;
				length--;
			}

			return strA.mLength - strB.mLength;
		}

		private static int CompareOrdinalIgnoreCaseHelper(char8* strA, int lengthA, char8* strB, int lengthB)
		{
			char8* a = strA;
			char8* b = strB;
			int length = Math.Min(lengthA, lengthB);

			while (length != 0)
			{
				int_strsize charA = (int_strsize)*a;
				int_strsize charB = (int_strsize)*b;

			    //Contract.Assert((char8A | char8B) <= 0x7F, "strings have to be ASCII");
			    // uppercase both chars - notice that we need just one compare per char
				if ((uint32)(charA &- 'a') <= (uint32)('z' - 'a')) charA -= 0x20;
				if ((uint32)(charB &- 'a') <= (uint32)('z' - 'a')) charB -= 0x20;

			    //Return the (case-insensitive) difference between them.
				if (charA != charB)
					return charA - charB;

			    // Next char
				a++;b++;
				length--;
			}

			return lengthA - lengthB;
		}

		private static int CompareOrdinalIgnoreCaseHelper(String strA, int indexA, int lengthA, String strB, int indexB, int lengthB)
		{
			return CompareOrdinalIgnoreCaseHelper(strA.Ptr + indexA, lengthA, strB.Ptr + indexB, lengthB);
		}

		private static int CompareOrdinalHelper(char8* strA, int lengthA, char8* strB, int lengthB)
		{
			char8* a = strA;
			char8* b = strB;
			int length = Math.Min(lengthA, lengthB);

			while (length != 0)
			{
				int_strsize char8A = (int_strsize)*a;
				int_strsize char8B = (int_strsize)*b;

			    //Return the (case-insensitive) difference between them.
				if (char8A != char8B)
					return char8A - char8B;

			    // Next char8
				a++;b++;
				length--;
			}

			return lengthA - lengthB;
		}

		public static int Compare(char8* strA, int lengthA, char8* strB, int lengthB, bool ignoreCase)
		{
			if (ignoreCase)
				return CompareOrdinalIgnoreCaseHelper(strA, lengthA, strB, lengthB);
			return CompareOrdinalHelper(strA, lengthA, strB, lengthB);
		}

		private static int CompareOrdinalHelper(String strA, int indexA, int lengthA, String strB, int indexB, int lengthB)
		{
		    return CompareOrdinalHelper(strA.Ptr + indexA, lengthA, strB.Ptr + indexB, lengthB);
		}

		public int CompareTo(String strB, bool ignoreCase = false)
		{
			if (ignoreCase)
				return CompareOrdinalIgnoreCaseHelper(Ptr, Length, strB.Ptr, strB.Length);
			return CompareOrdinalHelper(Ptr, Length, strB.Ptr, strB.Length);
		}

		public static int Compare(String strA, String strB, bool ignoreCase)
		{
			//they can't both be null;
			if (strA == null)
			{
			    return -1;
			}

			if (strB == null)
			{
			    return 1;
			}

			if (ignoreCase)
				return CompareOrdinalIgnoreCaseHelper(strA.Ptr, strA.Length, strB.Ptr, strB.Length);
			return CompareOrdinalHelper(strA.Ptr, strA.Length, strB.Ptr, strB.Length);

			//return Compare(strA, 0, strB, 0, strB.Length, ignoreCase);
		}

		public static int Compare(String strA, int indexA, String strB, int indexB, int length, bool ignoreCase)
        {
		    int lengthA = length;
		    int lengthB = length;

		    if (strA!=null) 
            {
		        if (strA.Length - indexA < lengthA) 
                {
		          lengthA = (strA.Length - indexA);
		        }
		    }

		    if (strB!=null) 
            {
		        if (strB.Length - indexB < lengthB) 
                {
		            lengthB = (strB.Length - indexB);
		        }
		    }

			if (ignoreCase)
				return CompareOrdinalIgnoreCaseHelper(strA, indexA, lengthA, strB, indexB, lengthB);
			return CompareOrdinalHelper(strA, indexA, lengthA, strB, indexB, lengthB);

		    /*if (ignoreCase) {
		        return CultureInfo.CurrentCulture.CompareInfo.Compare(strA, indexA, lengthA, strB, indexB, lengthB, CompareOptions.IgnoreCase);
		    }
		    return CultureInfo.CurrentCulture.CompareInfo.Compare(strA, indexA, lengthA, strB, indexB, lengthB, CompareOptions.None);*/
		}

		public bool Equals(String b, StringComparison comparisonType = StringComparison.Ordinal)
		{
			return String.Equals(this, b, comparisonType);
		}

		public static bool Equals(String a, String b, StringComparison comparisonType = StringComparison.Ordinal)
		{
			if ((Object)a == (Object)b)
				return true;
			if ((Object)a == null || (Object)b == null)
				return false;
			if (a.mLength != b.mLength)
				return false;
			if (comparisonType == StringComparison.OrdinalIgnoreCase)
				return EqualsIgnoreCaseHelper(a.Ptr, b.Ptr, a.mLength);
			return EqualsHelper(a.Ptr, b.Ptr, a.mLength);
		}
		
		public bool Equals(StringView str)
		{
			if (mLength != str.[Friend]mLength)
				return false;
			return EqualsHelper(str.Ptr, Ptr, mLength);
		}

		public bool Equals(StringView str, StringComparison comparisonType = StringComparison.Ordinal)
		{
			if (mLength != str.[Friend]mLength)
				return false;
			if (comparisonType == StringComparison.OrdinalIgnoreCase)
				return EqualsIgnoreCaseHelper(str.Ptr, Ptr, mLength);
			return EqualsHelper(str.Ptr, Ptr, mLength);
		}

		public bool StartsWith(StringView b, StringComparison comparisonType = StringComparison.Ordinal)
		{
			if (mLength < b.[Friend]mLength)
				return false;
			if (comparisonType == StringComparison.OrdinalIgnoreCase)
				return EqualsIgnoreCaseHelper(this.Ptr, b.Ptr, b.Length);
			return EqualsHelper(this.Ptr, b.Ptr, b.[Friend]mLength);
		}

		public bool EndsWith(StringView b, StringComparison comparisonType = StringComparison.Ordinal)
		{
			if (mLength < b.[Friend]mLength)
				return false;
			if (comparisonType == StringComparison.OrdinalIgnoreCase)
				return EqualsIgnoreCaseHelper(Ptr + mLength - b.[Friend]mLength, b.Ptr, b.[Friend]mLength);
			return EqualsHelper(this.Ptr + mLength - b.[Friend]mLength, b.Ptr, b.[Friend]mLength);
		}

		public bool StartsWith(char8 c)
		{
			if (mLength == 0)
				return false;
			return Ptr[0] == c;
		}

		public bool StartsWith(char32 c)
		{
			if (c < '\x80')
				return StartsWith((char8)c);
			if (mLength == 0)
				return false;
			return UTF8.Decode(Ptr, mLength).c == c;
		}

		public bool EndsWith(char8 c)
		{
			if (mLength == 0)
				return false;
			return Ptr[mLength - 1] == c;
		}

		public bool EndsWith(char32 c)
		{
			if (c < '\x80')
				return EndsWith((char8)c);
			if (mLength == 0)
				return false;
			char8* ptr = Ptr;
			int idx = mLength - 1;
			while ((idx > 0) && ((uint8)ptr[idx] & 0xC0 == 0x80))
				idx--;
			return UTF8.Decode(ptr + idx, mLength - idx).c == c;
		}

		public void ReplaceLargerHelper(String find, String replace)
		{
			List<int> replaceEntries = scope List<int>(8192);
			
			int_strsize moveOffset = replace.mLength - find.mLength;

			for (int startIdx = 0; startIdx <= mLength - find.mLength; startIdx++)
			{
				if (EqualsHelper(Ptr + startIdx, find.Ptr, find.mLength))
				{
					replaceEntries.Add(startIdx);
					startIdx += find.mLength - 1;
				}
			}

			if (replaceEntries.Count == 0)
				return;

			int destLength = mLength + moveOffset * replaceEntries.Count;
			int needSize = destLength;
			CalculatedReserve(needSize);

			let replacePtr = replace.Ptr;
			let ptr = Ptr;

			int lastDestStartIdx = destLength;
			for (int moveIdx = replaceEntries.Count - 1; moveIdx >= 0; moveIdx--)
			{
				int srcStartIdx = replaceEntries[moveIdx];
				int srcEndIdx = srcStartIdx + find.mLength;
				int destStartIdx = srcStartIdx + moveIdx * moveOffset;
				int destEndIdx = destStartIdx + replace.mLength;

				for (int i = lastDestStartIdx - destEndIdx - 1; i >= 0; i--)
					ptr[destEndIdx + i] = ptr[srcEndIdx + i];

				for (int i < replace.mLength)
					ptr[destStartIdx + i] = replacePtr[i];

				lastDestStartIdx = destStartIdx;
			}

			mLength = (int_strsize)destLength;
		}

		public void Replace(String find, String replace)
		{
			if (replace.mLength > find.mLength)
			{
				ReplaceLargerHelper(find, replace);
				return;
			}

			let ptr = Ptr;
			let findPtr = find.Ptr;
			let replacePtr = replace.Ptr;

			int_strsize inIdx = 0;
			int_strsize outIdx = 0;

			if (find.Length == 1)
			{
				// Optimized version for single-character replacements
				char8 findC = find[0];
				while (inIdx < mLength)
				{
					if (ptr[inIdx] == findC)
					{
						for (int i = 0; i < replace.mLength; i++)
							ptr[outIdx++] = replacePtr[i];

						inIdx += find.mLength;
					}
					else if (inIdx == outIdx)
					{
						++inIdx;
						++outIdx;
					}
					else // We need to physically move characters once we've found an equal span
					{
						ptr[outIdx++] = ptr[inIdx++];
					}
				}
			}
			else
			{
				while (inIdx <= mLength - find.mLength)
				{
					if (EqualsHelper(ptr + inIdx, findPtr, find.mLength))
					{
						for (int i = 0; i < replace.mLength; i++)
							ptr[outIdx++] = replacePtr[i];

						inIdx += find.mLength;
					}
					else if (inIdx == outIdx)
					{
						++inIdx;
						++outIdx;
					}
					else // We need to physically move characters once we've found an equal span
					{
						ptr[outIdx++] = ptr[inIdx++];
					}
				}
			}

			while (inIdx < mLength)
			{
				if (inIdx == outIdx)
				{
					++inIdx;
					++outIdx;
				}
				else
				{
					ptr[outIdx++] = ptr[inIdx++];
				}
			}

			mLength = outIdx;
		}

		public void TrimEnd()
		{
			let ptr = Ptr;
			for (int i = mLength - 1; i >= 0; i--)
			{
				char8 c = ptr[i];
				if (c >= (char8)0x80)
				{
					var (c32, idx, len) = GetChar32WithBacktrack(i);
					if (!c32.IsWhiteSpace)
					{
						if (i < mLength - 1)
							RemoveToEnd(i + 1);
						return;
					}
					i = idx;
				}
				else if (!c.IsWhiteSpace)
				{
					if (i < mLength - 1)
						RemoveToEnd(i + 1);
					return;
				}
			}
			Clear();
		}

		public void TrimStart()
		{
			let ptr = Ptr;
			for (int i = 0; i < mLength; i++)
			{
				char8 c = ptr[i];
				if (c >= (char8)0x80)
				{
					var (c32, len) = GetChar32(i);
					if (!c32.IsWhiteSpace)
					{
						if (i > 0)
							Remove(0, i);
						return;
					}
					i += len - 1;
				}
				else if (!c.IsWhiteSpace)
				{
					if (i > 0)
						Remove(0, i);
					return;
				}
			}
			Clear();
		}

		public void Trim()
		{
			TrimStart();
			TrimEnd();
		}

		public void TrimEnd(char32 trimChar)
		{
			let ptr = Ptr;
			for (int i = mLength - 1; i >= 0; i--)
			{
				char8 c = ptr[i];
				if (c >= (char8)0x80)
				{
					var (c32, idx, len) = GetChar32WithBacktrack(i);
					if (c32 != trimChar)
					{
						if (i < mLength - 1)
							RemoveToEnd(i + 1);
						return;
					}
					i = idx;
				}
				else if ((char32)c != trimChar)
				{
					if (i < mLength - 1)
						RemoveToEnd(i + 1);
					return;
				}
			}
			Clear();
		}

		public void TrimEnd(char8 trimChar)
		{
			TrimEnd((char32)trimChar);
		}

		public void TrimStart(char32 trimChar)
		{
			let ptr = Ptr;
			for (int i = 0; i < mLength; i++)
			{
				char8 c = ptr[i];
				if (c >= (char8)0x80)
				{
					var (c32, len) = GetChar32(i);
					if (c32 != trimChar)
					{
						if (i > 0)
							Remove(0, i);
						return;
					}
					i += len - 1;
				}
				else if ((char32)c != trimChar)
				{
					if (i > 0)
						Remove(0, i);
					return;
				}
			}
			Clear();
		}

		public void TrimStart(char8 trimChar)
		{
			TrimStart((char32)trimChar);
		}

		public void Trim(char32 trimChar)
		{
			TrimStart(trimChar);
			TrimEnd(trimChar);
		}

		public void Trim(char8 trimChar)
		{
			TrimStart((.)trimChar);
			TrimEnd((.)trimChar);
		}
		
		public void PadLeft(int totalWidth, char8 paddingChar)
		{
			Insert(0, paddingChar, totalWidth - Length);
		}

		public void PadLeft(int totalWidth, char32 paddingChar)
		{
			Insert(0, paddingChar, totalWidth - Length);
		}

		public void PadLeft(int totalWidth)
		{
			Insert(0, ' ', totalWidth - Length);
		}

		public void PadRight(int totalWidth, char8 paddingChar)
		{
			Append(paddingChar, totalWidth - Length);
		}

		public void PadRight(int totalWidth, char32 paddingChar)
		{
			Append(paddingChar, totalWidth - Length);
		}

		public void PadRight(int totalWidth)
		{
			Append(' ', totalWidth - Length);
		}

		public void Join(StringView sep, IEnumerator<String> enumerable)
		{
			bool isFirst = true;
			for (var str in enumerable)
			{
				if (!isFirst)
					Append(sep);
				Append(str);
				isFirst = false;
			}
		}

		public void Join(StringView sep, IEnumerator<StringView> enumerable)
		{
			bool isFirst = true;
			for (var str in enumerable)
			{
				if (!isFirst)
					Append(sep);
				Append(str);
				isFirst = false;
			}
		}

		public void Join(String separator, params String[] values)
		{
			for (int i = 0; i < values.Count; i++)
			{
				if (i > 0)
					Append(separator);
				values[i].ToString(this);
			}
		}

		public StringSplitEnumerator Split(char8 c)
		{
			return StringSplitEnumerator(Ptr, Length, c, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8 separator, int count)
		{
			return StringSplitEnumerator(Ptr, Length, separator, count, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8 separator, StringSplitOptions options)
		{
			return StringSplitEnumerator(Ptr, Length, separator, Int32.MaxValue, options);
		}

		public StringSplitEnumerator Split(char8 separator, int count, StringSplitOptions options)
		{
			return StringSplitEnumerator(Ptr, Length, separator, count, options);
		}

		public StringSplitEnumerator Split(params char8[] separators)
		{
			return StringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8[] separators)
		{
			return StringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8[] separators, int count)
		{
			return StringSplitEnumerator(Ptr, Length, separators, count, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8[] separators, int count, StringSplitOptions options)
		{
			return StringSplitEnumerator(Ptr, Length, separators, count, options);
		}

		public StringSplitEnumerator Split(char8[] separators, StringSplitOptions options)
		{
			return StringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, options);
		}

		public StringStringSplitEnumerator Split(StringView sv)
		{
			return StringStringSplitEnumerator(Ptr, Length, sv, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView separator, int count)
		{
			return StringStringSplitEnumerator(Ptr, Length, separator, count, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView separator, StringSplitOptions options)
		{
			return StringStringSplitEnumerator(Ptr, Length, separator, Int32.MaxValue, options);
		}

		public StringStringSplitEnumerator Split(StringView separator, int count, StringSplitOptions options)
		{
			return StringStringSplitEnumerator(Ptr, Length, separator, count, options);
		}

		public StringStringSplitEnumerator Split(params StringView[] separators)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView[] separators)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView[] separators, int count)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, count, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView[] separators, int count, StringSplitOptions options)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, count, options);
		}

		public StringStringSplitEnumerator Split(StringView[] separators, StringSplitOptions options)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, options);
		}

		public static mixin NewOrSet(var target, var source)
		{
			if (target == null)
				target = new String(source);
			else
				target.Set(source);
		}

		public static mixin DupIfReferenceEqual(var inStr, var outStr)
		{
			if ((Object)inStr == (Object)outStr)
				inStr = scope:: String(outStr);
		}

		public static mixin StackSplit(var target, var splitChar)
		{
			var strings = scope:mixin List<String>();
			for (var strView in target.Split(splitChar))
				strings.Add(scope:mixin String(strView));
			strings
		}

		public mixin Split(var splitChar)
		{
			int count = 0;
			for (this.Split(splitChar))
				count++;
			var stringArr = scope:mixin StringView[count];
			int idx = 0;
			for (var strView in Split(splitChar))
				stringArr[idx++] = strView;
			stringArr
		}
		
		public mixin ToScopedNativeWChar()
		{
			int encodedLen = UTF16.GetEncodedLen(this);
			c_wchar* buf;
			if (encodedLen < 128)
			{
				buf = scope:mixin c_wchar[encodedLen+1]* ( ? );
			}
			else
			{
				buf = new c_wchar[encodedLen+1]* ( ? );
				defer:mixin delete buf;
			}

			if (sizeof(c_wchar) == 2)
				UTF16.Encode(this, (.)buf, encodedLen);
			else
				UTF32.Encode(this, (.)buf, encodedLen);
			buf[encodedLen] = 0;
			buf
		}

		[Comptime(ConstEval=true)]
		public Span<c_wchar> ToConstNativeW()
		{
			if (sizeof(c_wchar) == 2)
			{
				int encodedLen = UTF16.GetEncodedLen(this);
				var buf = new char16[encodedLen + 1]* ( ? );
				UTF16.Encode(this, buf, encodedLen);
				buf[encodedLen] = 0;
				return .((.)buf, encodedLen + 1);
			}
			else
			{
				int encodedLen = UTF32.GetEncodedLen(this);
				var buf = new char32[encodedLen + 1]* ( ? );
				UTF32.Encode(this, buf, encodedLen);
				buf[encodedLen] = 0;
				return .((.)buf, encodedLen + 1);
			}
		}

		[Comptime(ConstEval=true)]
		public static String ConstF(String format, params Span<Object> args)
		{
			return new String()..AppendF(format, params args);
		}

		public static bool Equals(char8* str1, char8* str2)
		{
			for (int i = 0; true; i++)
			{
				char8 c = str1[i];
				char8 c2 = str2[i];
				if (c != c2)
					return false;
				if ((c == (char8)0) || (c2 == (char8)0))
					return ((c == (char8)0) && (c2 == (char8)0));
			}
		}

		public static bool Equals(char8* str1, char8* str2, int length)
		{
			for (int i < length)
			{
				char8 c = str1[i];
				char8 c2 = str2[i];
				if (c != c2)
					return false;
			}
			return true;
		}

		public RawEnumerator RawChars
		{
			[Inline]
			get
			{
				return RawEnumerator(Ptr, 0, mLength);
			}
		}

		public UTF8Enumerator DecodedChars
		{
			[Inline]
			get
			{
				return UTF8Enumerator(Ptr, 0, mLength);
			}
		}

		public UTF8Enumerator DecodedChars(int startIdx)
		{
			return UTF8Enumerator(Ptr, startIdx, mLength);
		}

		public bool HasMultibyteChars()
		{
			char8* ptr = Ptr;
			int len = Length;
			for (int i < len)
				if (ptr[i] >= '\x80')
					return true;
			return false;
		}

		public (char32 c, int8 length) GetChar32(int idx)
		{
			Debug.Assert((uint)idx < (uint)mLength);
			char8* ptr = Ptr;
			char8 c = ptr[idx];
			if (c < '\x80')
				return (c, 1);
			if (((uint8)ptr[idx] & 0xC0) == 0x80)
				return (0, 0); // Invalid UTF8 data
			return UTF8.Decode(ptr + idx, mLength - idx);
		}

		public (char32 c, int idx, int8 length) GetChar32WithBacktrack(int idx)
		{
			Debug.Assert((uint)idx < (uint)mLength);
			char8* ptr = Ptr;
			char8 c = ptr[idx];
			if (c < '\x80')
				return (c, idx, 1);
			var idx;
			while (((uint8)ptr[idx] & 0xC0) == 0x80)
			{
				if (idx == 0) // Invalid UTF8 data
					return (0, 0, 0);
				idx--;
			}
			let (c32, len) = UTF8.Decode(ptr + idx, mLength - idx);
			return (c32, idx, len);
		}

		public (int startIdx, int length) GetCodePointSpan(int idx)
		{
			char8* ptr = Ptr;
			int startIdx = idx;

			// Move to start of char
			while (startIdx >= 0)
			{
				char8 c = ptr[startIdx];
				if (((uint8)c & 0x80) == 0)
					return (startIdx, 1);
				if (((uint8)c & 0xC0) != 0x80)
					break;
				startIdx--;
			}

			return (startIdx, UTF8.GetDecodedLength(ptr + startIdx));
		}

		public (int startIdx, int length) GetGraphemeClusterSpan(int idx)
		{
			char8* ptr = Ptr;
			int startIdx = idx;

			// Move to start of char
			while (startIdx >= 0)
			{
				char8 c = ptr[startIdx];
				if (((uint8)c & 0x80) == 0)
				{
					// This is the simple and fast case - ASCII followed by the string end or more ASCII
					if ((startIdx == mLength - 1) || ((uint8)ptr[startIdx + 1] & 0x80) == 0)
						return (startIdx, 1);
					break;
				}
				if (((uint8)c & 0xC0) != 0x80)
				{
					let (c32, cLen) = UTF8.Decode(ptr + startIdx, mLength - startIdx);
					if (!c32.IsCombiningMark)
						break;
				}
				startIdx--;
			}

			int curIdx = startIdx;
			while (true)
			{
				let (c32, cLen) = UTF8.Decode(ptr + curIdx, mLength - curIdx);
				int nextIdx = curIdx + cLen;
                if ((curIdx != startIdx) && (!c32.IsCombiningMark))
					return (startIdx, curIdx - startIdx);
				if (nextIdx == mLength)
					return (startIdx, nextIdx - startIdx);
				curIdx = nextIdx;
			}
		}

		static void CheckLiterals(String* ptr)
		{
			var ptr;

			String* prevList = *((String**)(ptr++));
			if (prevList != null)
			{
				CheckLiterals(prevList);
			}

			while (true)
			{
				String str = *(ptr++);
				if (str == null)
					break;
				sInterns.Add(str);
			}
		}

		public String Intern()
		{
			using (sMonitor.Enter())
			{
				bool needsLiteralPass = sInterns.Count == 0;
				String* internalLinkPtr = *((String**)(sStringLiterals));
				if (internalLinkPtr != sPrevInternLinkPtr)
				{
					sPrevInternLinkPtr = internalLinkPtr;
					needsLiteralPass = true;
				}
				if (needsLiteralPass)
					CheckLiterals(sStringLiterals);

				String* entryPtr;
				if (sInterns.TryAdd(this, out entryPtr))
				{
					String result = new String(mLength + 1);
					result.Append(this);
					result.EnsureNullTerminator();
					*entryPtr = result;
					sOwnedInterns.Add(result);
					return result;
				}
				return *entryPtr;
			}
		}

		public struct RawEnumerator : IRefEnumerator<char8*>, IEnumerator<char8>
		{
			char8* mPtr;
			int_strsize mIdx;
			int_strsize mLength;

			[Inline]
			public this(char8* ptr, int idx, int length)
			{
				mPtr = ptr;
				mIdx = (int_strsize)idx - 1;
				mLength = (int_strsize)length;
			}

			public char8 Current
			{
				[Inline]
			    get
			    {
			        return mPtr[mIdx];
			    }

				[Inline]
				set
				{
					mPtr[mIdx] = value;
				}
			}

			public ref char8 CurrentRef
			{
				[Inline]
			    get
			    {
			        return ref mPtr[mIdx];
			    }
			}

			public int Index
			{
				[Inline]
				get
				{
					return mIdx;
				}				
			}

			public int Length
			{
				[Inline]
				get
				{
					return mLength;
				}				
			}

			[Inline]
			public Result<char8> GetNext() mut
			{
				++mIdx;
				if (mIdx >= mLength)
					return .Err;
				return mPtr[mIdx];
			}

			[Inline]
			public Result<char8*> GetNextRef() mut
			{
				++mIdx;
				if (mIdx >= mLength)
					return .Err;
				return &mPtr[mIdx];
			}
		}

		public struct UTF8Enumerator : IEnumerator<char32>
		{
			char8* mPtr;
			int_strsize mNextIndex;
			int_strsize mLength;
			char32 mChar;

			public this(char8* ptr, int idx, int length)
			{
				mPtr = ptr;
				mNextIndex = (int_strsize)idx;
				mLength = (int_strsize)length;
				mChar = 0;
			}

			public int NextIndex
			{
				get
				{
					return mNextIndex;
				}
				set mut
				{
					mNextIndex = (int_strsize)value;
				}
			}

			public char32 Current
		    {
		        get
				{
					return mChar;
				}
		    }

			public bool MoveNext() mut
			{
				let strLen = mLength;
				if (mNextIndex >= strLen)
					return false;

				mChar = (char32)mPtr[mNextIndex++];
				if (mChar < (char32)0x80)
					return true;
#if BF_UTF_PEDANTIC
				// If this fails then we are starting on a trailing byte
				Debug.Assert((mChar & (char32)0xC0) != (char32)0x80);
#endif
				int8 trailingBytes = UTF8.sTrailingBytesForUTF8[mChar];
				if (mNextIndex + trailingBytes > strLen)
					return true;

				switch (trailingBytes)
				{
				case 3: mChar <<= 6; mChar += (int32)mPtr[mNextIndex++]; fallthrough;
				case 2: mChar <<= 6; mChar += (int32)mPtr[mNextIndex++]; fallthrough;
				case 1: mChar <<= 6; mChar += (int32)mPtr[mNextIndex++]; fallthrough;
				}

				mChar -= (int32)UTF8.sOffsetsFromUTF8[trailingBytes];
				return true;
			}

			public void Reset() mut
			{
				mNextIndex = -1;
			}

			public void Dispose()
			{

			}

			public Result<char32> GetNext() mut
			{
				if (!MoveNext())
					return .Err;
				return Current;
			}
		}
	}

	public enum StringSplitOptions
	{
		None = 0,
		RemoveEmptyEntries = 1
	}

	public struct StringSplitEnumerator : IEnumerator<StringView>
	{
		StringSplitOptions mSplitOptions;
		char8 mFirstSeparator;
		char8[] mSeparators;
		char8* mPtr;
		int_strsize mStrLen;
		int32 mCurCount;
		int32 mMaxCount;
		int_strsize mPos;
		int_strsize mMatchPos;

		public this(char8* ptr, int strLength, char8[] separators, int count, StringSplitOptions splitOptions)
		{
			mPtr = ptr;
			mStrLen = (int_strsize)strLength;
			if (separators?.Count > 0)
				mFirstSeparator = separators[0];
			else
				mFirstSeparator = '\0';
			mSeparators = separators;
			mCurCount = 0;
			mMaxCount = (int32)count;
			mPos = 0;
			mMatchPos = -1;
			mSplitOptions = splitOptions;
		}

		public this(char8* ptr, int strLength, char8 separator, int count, StringSplitOptions splitOptions)
		{
			mPtr = ptr;
			mStrLen = (int_strsize)strLength;
			mFirstSeparator = separator;
			mSeparators = null;
			mCurCount = 0;
			mMaxCount = (int32)count;
			mPos = 0;
			mMatchPos = -1;
			mSplitOptions = splitOptions;
		}

		public StringView Current
	    {
	        get
			{
				return StringView(mPtr + mPos, mMatchPos - mPos);
			}
	    }

		public int_strsize Pos
		{
			get
			{
				return mPos;
			}
		}

		public int_strsize MatchPos
		{
			get
			{
				return mMatchPos;
			}
		}
		
		public int32 MatchIndex
		{
			get
			{
				return mCurCount - 1;
			}
		}

		public bool HasMore
		{
			get
			{
				return mMatchPos < mStrLen;
			}
		}

		public bool MoveNext() mut
		{
			if (mCurCount >= mMaxCount)
				return false;

			mPos = mMatchPos + 1;

			mCurCount++;
			if (mCurCount == mMaxCount)
			{
				mMatchPos = (int_strsize)mStrLen;
				if (mPos > mMatchPos)
					return false;
				if ((mMatchPos == mPos) && (mSplitOptions.HasFlag(.RemoveEmptyEntries)))
					return false;
				return true;
			}

			int endDiff = mStrLen - mMatchPos;
			if (endDiff == 0)
				return false;
			while (true)
			{
				mMatchPos++;
				endDiff--;
				bool foundMatch = false;
				if (endDiff == 0)
				{
					foundMatch = true;
				}
				else
				{
					char8 c = mPtr[mMatchPos];
					if (c.IsWhiteSpace && mFirstSeparator == '\0' && (mSeparators == null || mSeparators.IsEmpty))
					{
						foundMatch = true;
					}
					else if (c == mFirstSeparator)
					{
						foundMatch = true;
					}
					else if (mSeparators != null)
					{
						for (int i = 1; i < mSeparators.Count; i++)
							if (c == mSeparators[i])
								foundMatch = true;
					}
				}

				if (foundMatch)
				{
					if ((mMatchPos >= mPos + 1) || (!mSplitOptions.HasFlag(StringSplitOptions.RemoveEmptyEntries)))
						return true;
					mPos = mMatchPos + 1;
					if (mPos >= mStrLen)
						return false;
				}
			}
		}

		public void Reset() mut
		{
			mPos = 0;
			mMatchPos = -1;
		}

		public void Dispose()
		{

		}

		public Result<StringView> GetNext() mut
		{
			if (!MoveNext())
				return .Err;
			return Current;
		}
	}

	public struct StringStringSplitEnumerator : IEnumerator<StringView>
	{
		StringSplitOptions mSplitOptions;
		StringView mFirstSeparator;
		StringView[] mSeparators;
		char8* mPtr;
		int_strsize mStrLen;
		int32 mCurCount;
		int32 mMaxCount;
		int_strsize mPos;
		int_strsize mMatchPos;
		int_strsize mMatchLen;

		public this(char8* ptr, int strLength, StringView[] separators, int count, StringSplitOptions splitOptions)
		{
			mPtr = ptr;
			mStrLen = (int_strsize)strLength;
			if (separators?.Count > 0)
				mFirstSeparator = separators[0];
			else
				mFirstSeparator = .();
			mSeparators = separators;
			mCurCount = 0;
			mMaxCount = (int32)count;
			mPos = 0;
			mMatchPos = -1;
			mMatchLen = 1;
			mSplitOptions = splitOptions;
		}

		public this(char8* ptr, int strLength, StringView separator, int count, StringSplitOptions splitOptions)
		{
			mPtr = ptr;
			mStrLen = (int_strsize)strLength;
			mFirstSeparator = separator;
			mSeparators = null;
			mCurCount = 0;
			mMaxCount = (int32)count;
			mPos = 0;
			mMatchPos = -1;
			mMatchLen = 1;
			mSplitOptions = splitOptions;
		}

		public StringView Current
	    {
	        get
			{
				return StringView(mPtr + mPos, mMatchPos - mPos);
			}
	    }

		public int_strsize Pos
		{
			get
			{
				return mPos;
			}
		}

		public int_strsize MatchPos
		{
			get
			{
				return mMatchPos;
			}
		}
		
		public int32 MatchIndex
		{
			get
			{
				return mCurCount - 1;
			}
		}

		public bool HasMore
		{
			get
			{
				return mMatchPos < mStrLen;
			}
		}

		public bool MoveNext() mut
		{
			if (mCurCount >= mMaxCount)
				return false;

			mPos = mMatchPos + mMatchLen;

			mCurCount++;
			if (mCurCount == mMaxCount)
			{
				mMatchPos = (int_strsize)mStrLen;
				if (mPos > mMatchPos)
					return false;
				if ((mMatchPos == mPos) && (mSplitOptions.HasFlag(.RemoveEmptyEntries)))
					return false;
				return true;
			}

			int endDiff = mStrLen - mMatchPos;
			if (endDiff == 0)
				return false;
			while (true)
			{
				mMatchPos++;
				endDiff--;
				bool foundMatch = false;
				if (endDiff == 0)
				{
					foundMatch = true;
				}
				else
				{
					if (mFirstSeparator.IsNull && (mSeparators == null || mSeparators.IsEmpty) && mPtr[mMatchPos].IsWhiteSpace)
					{
						foundMatch = true;
						mMatchLen = 1;
					}
					else if (mFirstSeparator.Length <= mStrLen - mMatchPos && StringView(&mPtr[mMatchPos], mFirstSeparator.Length) == mFirstSeparator)
					{
						foundMatch = true;
						mMatchLen = (int_strsize)mFirstSeparator.Length;
					}
					else if (mSeparators != null)
					{
						for (int i = 1; i < mSeparators.Count; i++)
						{
							if (mSeparators[i].Length <= mStrLen - mMatchPos && StringView(&mPtr[mMatchPos], mSeparators[i].Length) == mSeparators[i])
							{
								foundMatch = true;
								mMatchLen = (int_strsize)mSeparators[i].Length;
							}
						}
					}
				}

				if (foundMatch)
				{
					if ((mMatchPos >= mPos + 1) || (!mSplitOptions.HasFlag(StringSplitOptions.RemoveEmptyEntries)))
						return true;
					mPos = mMatchPos + mMatchLen;
					if (mPos >= mStrLen)
						return false;
				}
				else
				{
					mMatchLen = 1;
				}
			}
		}

		public void Reset() mut
		{
			mPos = 0;
			mMatchPos = -1;
		}

		public void Dispose()
		{

		}

		public Result<StringView> GetNext() mut
		{
			if (!MoveNext())
				return .Err;
			return Current;
		}
	}

	public struct StringView : Span<char8>, IFormattable, IPrintable, IHashable
	{
		public this()
		{
			mPtr = null;
			mLength = 0;
		}

		public this(String string)
		{
			if (string == null)
			{
				this = default;
				return;
			}
			mPtr = string.Ptr;
			mLength = string.Length;
		}

		public this(String string, int offset)
		{
			if (string == null)
			{
				Debug.Assert(offset == 0);
				this = default;
				return;
			}
			Debug.Assert((uint)offset <= (uint)string.Length);
			mPtr = string.Ptr + offset;
			mLength = string.Length - offset;
		}

		public this(String string, int offset, int length)
		{
			if (string == null)
			{
				Debug.Assert(offset == 0 && length == 0);
				this = default;
				return;
			}
			Debug.Assert((uint)offset + (uint)length <= (uint)string.Length);
			mPtr = string.Ptr + offset;
			mLength = length;
		}

		public this(StringView stringView)
		{
			mPtr = stringView.mPtr;
			mLength = stringView.mLength;
		}

		public this(StringView stringView, int offset)
		{
			Debug.Assert((uint)offset <= (uint)stringView.Length);
			mPtr = stringView.mPtr + offset;
			mLength = stringView.mLength - offset;
		}

		public this(StringView stringView, int offset, int length)
		{
			Debug.Assert((uint)offset + (uint)length <= (uint)stringView.Length);
			mPtr = stringView.mPtr + offset;
			mLength = length;
		}

		public this(char8[] arr, int offset, int length)
		{
			if (arr == null)
			{
				Debug.Assert(offset == 0 && length == 0);
				this = default;
				return;
			}
			Debug.Assert((uint)offset + (uint)length <= (uint)arr.Count);
			mPtr = arr.CArray() + offset;
			mLength = length;
		}

		public this(char8* ptr)
		{
			if (ptr == null)
			{
				this = default;
				return;
			}
			mPtr = ptr;
			mLength = String.StrLen(ptr);
		}

		public this(char8* ptr, int length)
		{
			if (ptr == null)
			{
				Debug.Assert(length == 0);
				this = default;
				return;
			}
			mPtr = ptr;
			mLength = length;
		}

		public ref char8 this[int index]
		{
			[Checked]
		    get
			{
				Runtime.Assert((uint)index < (uint)mLength);
				return ref mPtr[index];
			}

			[Unchecked, Inline]
			get
			{
				return ref mPtr[index];
			}
		}

		public ref char8 this[Index index]
		{
			[Checked]
		    get
			{
				int idx;
				switch (index)
				{
				case .FromFront(let offset): idx = offset;
				case .FromEnd(let offset): idx = mLength - offset;
				}
				Runtime.Assert((uint)idx < (uint)mLength);
				return ref mPtr[idx];
			}

			[Unchecked, Inline]
			get
			{
				int idx;
				switch (index)
				{
				case .FromFront(let offset): idx = offset;
				case .FromEnd(let offset): idx = mLength - offset;
				}
				return ref mPtr[idx];
			}
		}

		public StringView this[IndexRange range]
		{
#if !DEBUG
			[Inline]
#endif
			get
			{
				char8* start;
				switch (range.[Friend]mStart)
				{
				case .FromFront(let offset):
					Debug.Assert((uint)offset <= (uint)mLength);
					start = mPtr + offset;
				case .FromEnd(let offset):
					Debug.Assert((uint)offset <= (uint)mLength);
					start = mPtr + mLength - offset;
				}
				char8* end;
				if (range.[Friend]mIsClosed)
				{
					switch (range.[Friend]mEnd)
					{
					case .FromFront(let offset):
						Debug.Assert((uint)offset < (uint)mLength);
						end = mPtr + offset + 1;
					case .FromEnd(let offset):
						Debug.Assert((uint)(offset - 1) <= (uint)mLength);
						end = mPtr + mLength - offset + 1;
					}
				}
				else
				{
					switch (range.[Friend]mEnd)
					{
					case .FromFront(let offset):
						Debug.Assert((uint)offset <= (uint)mLength);
						end = mPtr + offset;
					case .FromEnd(let offset):
						Debug.Assert((uint)offset <= (uint)mLength);
						end = mPtr + mLength - offset;
					}
				}

				return .(start, end - start);
			}
		}

		public String.RawEnumerator RawChars
		{
			get
			{
				return String.RawEnumerator(Ptr, 0, mLength);
			}
		}

		public String.UTF8Enumerator DecodedChars
		{
			get
			{
				return String.UTF8Enumerator(Ptr, 0, mLength);
			}
		}

		public bool IsWhiteSpace
		{
			get
			{
				for (int i = 0; i < mLength; i++)
					if (!mPtr[i].IsWhiteSpace)
						return false;
				return true;
			}
		}

		public int GetHashCode()
		{
			return String.[Friend]GetHashCode(mPtr, mLength);
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append(mPtr, mLength);
		}

		public void ToString(String outString, String format, IFormatProvider formatProvider)
		{
			if (format == "Q")
			{
				String.Quote(mPtr, mLength, outString);
				return;
			}
			outString.Append(mPtr, mLength);
		}

		void IPrintable.Print(String outString)
		{
			String.Quote(mPtr, mLength, outString);
		}

		[Commutable]
		public static bool operator==(StringView val1, StringView val2)
		{
			if (val1.mLength != val2.mLength)
				return false;
			char8* ptr1 = val1.mPtr;
			char8* ptr2 = val2.mPtr;
			if (ptr1 == ptr2)
				return true;
			if ((ptr1 == null) || (ptr2 == null))
				return false;
			return String.[Friend]EqualsHelper(ptr1, ptr2, val1.mLength);
		}

		[Commutable]
		public static bool operator==(StringView val1, String val2)
		{
			if (val1.mLength != val2.Length)
				return false;
			char8* ptr1 = val1.mPtr;
			char8* ptr2 = val2.Ptr;
			if (ptr1 == ptr2)
				return true;
			if ((ptr1 == null) || (ptr2 == null))
				return false;
			return String.[Friend]EqualsHelper(ptr1, ptr2, val1.mLength);
		}

		public static int operator<=>(StringView val1, StringView val2)
		{
			return String.[Friend]CompareOrdinalHelper(val1.mPtr, val1.mLength, val2.mPtr, val2.mLength);
		}

		public static int Compare(StringView val1, StringView val2, bool ignoreCase = false)
		{
			if (ignoreCase)
				return String.[Friend]CompareOrdinalIgnoreCaseHelper(val1.mPtr, val1.mLength, val2.mPtr, val2.mLength);
			else
				return String.[Friend]CompareOrdinalHelper(val1.mPtr, val1.mLength, val2.mPtr, val2.mLength);
		}

		public int CompareTo(StringView strB, bool ignoreCase = false)
		{
			if (ignoreCase)
				return String.[Friend]CompareOrdinalIgnoreCaseHelper(Ptr, Length, strB.Ptr, strB.Length);
			return String.[Friend]CompareOrdinalHelper(Ptr, Length, strB.Ptr, strB.Length);
		}

		public bool Equals(StringView str)
		{
			if (mLength != str.[Friend]mLength)
				return false;
			return String.[Friend]EqualsHelper(str.Ptr, mPtr, mLength);
		}

		public bool Equals(StringView str, bool ignoreCase)
		{
			if (mLength != str.[Friend]mLength)
				return false;
			if (ignoreCase)
				return String.[Friend]EqualsIgnoreCaseHelper(str.Ptr, mPtr, mLength);
			return String.[Friend]EqualsHelper(str.Ptr, mPtr, mLength);
		}

		public int IndexOf(StringView subStr, bool ignoreCase = false)
		{
			/*for (int ofs = 0; ofs <= Length - subStr.Length; ofs++)
			{
				if (String.[Friend]Compare(mPtr + ofs, subStr.mLength, subStr.mPtr, subStr.mLength, ignoreCase) == 0)
					return ofs;
			}

			return -1;*/
			return IndexOf(subStr, 0, ignoreCase);
		}

		public int IndexOf(StringView subStr, int startIdx, bool ignoreCase = false)
		{
			if (subStr.Length == 0)
				return -1;

			if (!ignoreCase)
			{
				char8 firstC = subStr[0];
				for (int ofs = startIdx; ofs <= mLength - subStr.mLength; ofs++)
				{
					if ((mPtr[ofs] == firstC) && (String.Compare(mPtr + ofs + 1, subStr.mLength - 1, subStr.mPtr + 1, subStr.mLength - 1, ignoreCase) == 0))
						return ofs;
				}

			}
			else
			{
				char8 firstC = subStr[0];
				char8 firstC2 = firstC;
				if (firstC.IsLower)
					firstC2 = firstC.ToUpper;
				else
					firstC2 = firstC.ToLower;
				for (int ofs = startIdx; ofs <= mLength - subStr.mLength; ofs++)
				{
					if (((mPtr[ofs] == firstC) || (mPtr[ofs] == firstC2)) &&
						(String.Compare(mPtr + ofs + 1, subStr.mLength - 1, subStr.mPtr + 1, subStr.mLength - 1, ignoreCase) == 0))
						return ofs;
				}
			}

			return -1;
		}

		public int IndexOf(char8 c, int startIdx = 0)
		{
			let ptr = mPtr;
			for (int i = startIdx; i < mLength; i++)
				if (ptr[i] == c)
					return i;
			return -1;
		}

		public int LastIndexOf(char8 c)
		{
			let ptr = mPtr;
			for (int i = mLength - 1; i >= 0; i--)
				if (ptr[i] == c)
					return i;
			return -1;
		}

		public int LastIndexOf(char8 c, int startCheck)
		{
			let ptr = mPtr;
			for (int i = startCheck; i >= 0; i--)
				if (ptr[i] == c)
					return i;
			return -1;
		}

		public int IndexOfAny(char8[] targets)
		{
			return IndexOfAny(targets, 0, mLength);
		}

		public int IndexOfAny(char8[] targets, int startIdx)
		{
			return IndexOfAny(targets, startIdx, mLength - startIdx);
		}

		public int IndexOfAny(char8[] targets, int startIdx, int count)
		{
			let ptr = mPtr;
			for (var i = startIdx; i < count; i++)
			{
				let ch = ptr[i];
				for (let tag in targets)
				{
					if (ch == tag)
						return i;
				}
			}

			return -1;
		}

		public bool Contains(char8 c)
		{
			for (int i = 0; i < mLength; i++)
				if (mPtr[i] == c)
					return true;
			return false;
		}

		public bool Contains(StringView stringView, bool ignoreCase = false)
		{
			return IndexOf(stringView, ignoreCase) != -1;
		}

		public bool StartsWith(StringView b, StringComparison comparisonType = StringComparison.Ordinal)
		{
			if (mLength < b.mLength)
				return false;
			if (comparisonType == StringComparison.OrdinalIgnoreCase)
				return String.[Friend]EqualsIgnoreCaseHelper(this.Ptr, b.Ptr, b.Length);
			return String.[Friend]EqualsHelper(this.Ptr, b.Ptr, b.mLength);
		}

		public bool EndsWith(StringView b, StringComparison comparisonType = StringComparison.Ordinal)
		{
			if (mLength < b.mLength)
				return false;
			if (comparisonType == StringComparison.OrdinalIgnoreCase)
				return String.[Friend]EqualsIgnoreCaseHelper(Ptr + mLength - b.mLength, b.Ptr, b.mLength);
			return String.[Friend]EqualsHelper(this.Ptr + mLength - b.mLength, b.Ptr, b.mLength);
		}

		public void TrimEnd() mut
		{
			let ptr = Ptr;
			for (int i = mLength - 1; i >= 0; i--)
			{
				char8 c = ptr[i];
				if (c >= (char8)0x80)
				{
					var (c32, idx, len) = GetChar32WithBacktrack(i);
					if (!c32.IsWhiteSpace)
					{
						if (i < mLength - 1)
						{
							mLength = i + 1;
						}	
						return;
					}
					i = idx;
				}
				else if (!c.IsWhiteSpace)
				{
					if (i < mLength - 1)
					{
						mLength = i + 1;
					}
					return;
				}
			}
			Clear();
		}

		public void TrimStart() mut
		{
			let ptr = Ptr;
			for (int i = 0; i < mLength; i++)
			{
				char8 c = ptr[i];
				if (c >= (char8)0x80)
				{
					var (c32, len) = GetChar32(i);
					if (!c32.IsWhiteSpace)
					{
						if (i > 0)
						{
							mPtr += i;
							mLength -= i;
						}	
						return;
					}
					i += len - 1;
				}
				else if (!c.IsWhiteSpace)
				{
					if (i > 0)
					{
						mPtr += i;
						mLength -= i;
					}
					return;
				}
			}
			Clear();
		}

		public void Trim() mut
		{
			TrimStart();
			TrimEnd();
		}

		public void TrimEnd(char32 trimChar) mut
		{
			let ptr = Ptr;
			for (int i = mLength - 1; i >= 0; i--)
			{
				char8 c = ptr[i];
				if (c >= (char8)0x80)
				{
					var (c32, idx, len) = GetChar32WithBacktrack(i);
					if (c32 != trimChar)
					{
						if (i < mLength - 1)
						{
							mLength = i + 1;
						}	
						return;
					}
					i = idx;
				}
				else if (c != (char32)trimChar)
				{
					if (i < mLength - 1)
					{
						mLength = i + 1;
					}
					return;
				}
			}
			Clear();
		}

		public void TrimEnd(char8 trimChar) mut
		{
			TrimEnd((char32)trimChar);
		}

		public void TrimStart(char32 trimChar) mut
		{
			let ptr = Ptr;
			for (int i = 0; i < mLength; i++)
			{
				char8 c = ptr[i];
				if (c >= (char8)0x80)
				{
					var (c32, len) = GetChar32(i);
					if (c32 != trimChar)
					{
						if (i > 0)
						{
							mPtr += i;
							mLength -= i;
						}	
						return;
					}
					i += len - 1;
				}
				else if (c != (char32)trimChar)
				{
					if (i > 0)
					{
						mPtr += i;
						mLength -= i;
					}
					return;
				}
			}
			Clear();
		}

		public void TrimStart(char8 trimChar) mut
		{
			TrimStart((char32)trimChar);
		}

		public void Trim(char32 trimChar) mut
		{
			TrimStart(trimChar);
			TrimEnd(trimChar);
		}

		public void Trim(char8 trimChar) mut
		{
			TrimStart((.)trimChar);
			TrimEnd((.)trimChar);
		}

		public bool StartsWith(char8 c)
		{
			if (mLength == 0)
				return false;
			return Ptr[0] == c;
		}

		public bool StartsWith(char32 c)
		{
			if (c < '\x80')
				return StartsWith((char8)c);
			if (mLength == 0)
				return false;
			return UTF8.Decode(Ptr, mLength).c == c;
		}

		public bool EndsWith(char8 c)
		{
			if (mLength == 0)
				return false;
			return Ptr[mLength - 1] == c;
		}

		public bool EndsWith(char32 c)
		{
			if (c < '\x80')
				return EndsWith((char8)c);
			if (mLength == 0)
				return false;
			char8* ptr = Ptr;
			int idx = mLength - 1;
			while ((idx > 0) && ((uint8)ptr[idx] & 0xC0 == 0x80))
				idx--;
			return UTF8.Decode(ptr + idx, mLength - idx).c == c;
		}

		public void QuoteString(String outString)
		{
			String.Quote(Ptr, Length, outString);
		}

		public Result<void> UnQuoteString(String outString)
		{
			return String.Unquote(Ptr, Length, outString);
		}

		public Result<void> Unescape(String outString)
		{
			return String.Unescape(Ptr, Length, outString);
		}

		[NoDiscard]
		public StringView Substring(int pos)
		{
			return .(this, pos);
		}

		[NoDiscard]
		public StringView Substring(int pos, int length)
		{
			return .(this, pos, length);
		}

		public (char32, int) GetChar32(int idx)
		{
			Debug.Assert((uint)idx < (uint)mLength);
			char8* ptr = Ptr;
			char8 c = ptr[idx];
			if (c < '\x80')
				return (c, 1);
			if (((uint8)ptr[idx] & 0xC0) == 0x80)
				return (0, 0); // Invalid UTF8 data
			return UTF8.Decode(ptr + idx, mLength - idx);
		}

		public (char32, int, int) GetChar32WithBacktrack(int idx)
		{
			Debug.Assert((uint)idx < (uint)mLength);
			char8* ptr = Ptr;
			char8 c = ptr[idx];
			if (c < '\x80')
				return (c, idx, 1);
			var idx;
			while (((uint8)ptr[idx] & 0xC0) == 0x80)
			{
				if (idx == 0) // Invalid UTF8 data
					return (0, 0, 0);
				idx--;
			}
			let (c32, len) = UTF8.Decode(ptr + idx, mLength - idx);
			return (c32, idx, len);
		}

		public StringSplitEnumerator Split(char8 c)
		{
			return StringSplitEnumerator(Ptr, Length, c, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8 separator, int count)
		{
			return StringSplitEnumerator(Ptr, Length, separator, count, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8 separator, StringSplitOptions options)
		{
			return StringSplitEnumerator(Ptr, Length, separator, Int32.MaxValue, options);
		}

		public StringSplitEnumerator Split(char8 separator, int count, StringSplitOptions options)
		{
			return StringSplitEnumerator(Ptr, Length, separator, count, options);
		}

		public StringSplitEnumerator Split(params char8[] separators)
		{
			return StringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8[] separators)
		{
			return StringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8[] separators, int count)
		{
			return StringSplitEnumerator(Ptr, Length, separators, count, StringSplitOptions.None);
		}

		public StringSplitEnumerator Split(char8[] separators, int count, StringSplitOptions options)
		{
			return StringSplitEnumerator(Ptr, Length, separators, count, options);
		}

		public StringSplitEnumerator Split(char8[] separators, StringSplitOptions options)
		{
			return StringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, options);
		}

		public StringStringSplitEnumerator Split(StringView c)
		{
			return StringStringSplitEnumerator(Ptr, Length, c, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView separator, int count)
		{
			return StringStringSplitEnumerator(Ptr, Length, separator, count, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView separator, StringSplitOptions options)
		{
			return StringStringSplitEnumerator(Ptr, Length, separator, Int32.MaxValue, options);
		}

		public StringStringSplitEnumerator Split(StringView separator, int count, StringSplitOptions options)
		{
			return StringStringSplitEnumerator(Ptr, Length, separator, count, options);
		}

		public StringStringSplitEnumerator Split(params StringView[] separators)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView[] separators)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView[] separators, int count)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, count, StringSplitOptions.None);
		}

		public StringStringSplitEnumerator Split(StringView[] separators, int count, StringSplitOptions options)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, count, options);
		}

		public StringStringSplitEnumerator Split(StringView[] separators, StringSplitOptions options)
		{
			return StringStringSplitEnumerator(Ptr, Length, separators, Int32.MaxValue, options);
		}

		public String Intern()
		{
			using (String.[Friend]sMonitor.Enter())
			{
				bool needsLiteralPass = String.[Friend]sInterns.Count == 0;
				String* internalLinkPtr = *((String**)(String.[Friend]sStringLiterals));
				if (internalLinkPtr != String.[Friend]sPrevInternLinkPtr)
				{
					String.[Friend]sPrevInternLinkPtr = internalLinkPtr;
					needsLiteralPass = true;
				}
				if (needsLiteralPass)
					String.[Friend]CheckLiterals(String.[Friend]sStringLiterals);

				String* entryPtr;
				if (String.[Friend]sInterns.TryAddAlt(this, out entryPtr))
				{
					String result = new String(mLength + 1);
					result.Append(this);
					result.EnsureNullTerminator();
					*entryPtr = result;
					String.[Friend]sOwnedInterns.Add(result);
					return result;
				}
				return *entryPtr;
			}
		}

		public static operator StringView (String str)
		{
			StringView sv;
			if (str != null)
			{
				sv.mPtr = str.Ptr;
				sv.mLength = str.[Friend]mLength;
			}
			else
			{
				sv = default;
			}
			return sv;
		}

		public mixin ToScopeCStr(int maxInlineChars = 128)
		{
			char8* ptr = null;
			if (this.mPtr != null)
			{
				if (mLength < maxInlineChars)
				{
					ptr = scope:mixin char8[mLength + 1]*;
				}
				else
				{
					ptr = new char8[mLength + 1]*;
					defer:mixin delete ptr;
				}
				Internal.MemCpy(ptr, mPtr, mLength);
				ptr[mLength] = 0;
			}
			ptr
		}

		public mixin ToScopedNativeWChar()
		{
			int encodedLen = UTF16.GetEncodedLen(this);
			c_wchar* buf;
			if (encodedLen < 128)
			{
				buf = scope:mixin c_wchar[encodedLen+1]* ( ? );
			}
			else
			{
				buf = new c_wchar[encodedLen+1]* ( ? );
				defer:mixin delete buf;
			}

			if (sizeof(c_wchar) == 2)
				UTF16.Encode(this, (.)buf, encodedLen);
			else
				UTF32.Encode(this, (.)buf, encodedLen);
			buf[encodedLen] = 0;
			buf
		}
	}

	class StringWithAlloc : String
	{
		IRawAllocator mAlloc;

		[AllowAppend]
		private this()
		{
		}

		[AllowAppend]
		public this(IRawAllocator alloc)
		{
			mAlloc = alloc;
		}

		protected override void* Alloc(int size, int align)
 		{
			 return mAlloc.Alloc(size, align);
		}

		protected override void Free(void* ptr)
 		{
			 mAlloc.Free(ptr);
		}
	}

#if TEST
	extension String
	{
		[Test]
		public static void Test_Intern()
		{
			String str = "TestString";
			str.EnsureNullTerminator();

			String strA = scope String("Test");
			strA.Append("String");
			Runtime.Assert(strA == str);
			Runtime.Assert((Object)strA != str);

			String strB = strA.Intern();
			Runtime.Assert(strB == str);
			Runtime.Assert((Object)strB == str);
		}
	}
#endif
}
