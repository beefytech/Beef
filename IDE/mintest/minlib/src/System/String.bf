using System.Diagnostics;
using System.Collections;

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

	[CRepr]
    class String
    {
		enum CreateFlags
		{
			None = 0,
			NullTerminate = 1
		}

        int_strsize mLength;
        uint_strsize mAllocSizeAndFlags;
        char8* mPtr = null;

		extern const String* sStringLiterals;
		public const String Empty = "";

#if BF_LARGE_STRINGS
		const uint64 SizeFlags = 0x3FFFFFFF'FFFFFFFF;
		const uint64 DynAllocFlag = 0x80000000'00000000;
		const uint64 StrPtrFlag = 0x40000000'00000000;
#else
        const uint32 SizeFlags = 0x3FFFFFFF;
        const uint32 DynAllocFlag = 0x80000000;
		const uint32 StrPtrFlag = 0x40000000;
#endif

		public static void CheckLiterals(String* ptr)
		{
			var ptr;
			
			String* prevList = *((String**)(ptr++));
			if (prevList != null)
			{
				CheckLiterals(prevList);
			}

			PrintF("StrList: %p\n", ptr);
			while (true)
			{
				String str = *(ptr++);
				if (str == null)
					break;
				PrintF("Str: %s\n", str.CStr());
			}
		}

		public static void CheckLiterals()
		{
			CheckLiterals(sStringLiterals);
		}

		[AllowAppend]
        public this(int count)
		{
			let alignInt = sizeof(int) - 1;
			let tryBufferSize = ((count - sizeof(char8*)) + alignInt) & ~alignInt;
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
			char8* ptr = append char8[bufferSize]* { ? };
			mAllocSizeAndFlags = (uint32)bufferSize + (int32)sizeof(char8*);
			mLength = 0;
		}

		[AllowAppend]
        public this()
        {
            let bufferSize = 16 - sizeof(char8*);
#unwarn
            char8* ptr = append char8[bufferSize]*;            
            mAllocSizeAndFlags = (uint32)bufferSize + (int32)sizeof(char8*);
            mLength = 0;
		}

		[AllowAppend]
		public this(String str)
		{
			let count = str.mLength;
			let tryBufferSize = count - sizeof(char8*);
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
			char8* addlPtr = append char8[bufferSize]*;			
			Internal.MemCpy(Ptr, str.Ptr, count);
			mLength = count;
			mAllocSizeAndFlags = (uint32)bufferSize + (int_strsize)sizeof(char8*);
		}

		[AllowAppend]
        public this(char8* char8Ptr)
		{
			let count = Internal.CStrLen(char8Ptr);
			let tryBufferSize = count - sizeof(char8*);
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
			char8* addlPtr = append char8[bufferSize]*;            
			let ptr = Ptr;
			for (int_strsize i = 0; i < count; i++)
			    ptr[i] = char8Ptr[i];
			mAllocSizeAndFlags = (uint32)bufferSize + (int_strsize)sizeof(char8*);
			mLength = count;
		}

		[AllowAppend]
		public this(String str, int offset)
		{
			let count = str.mLength - offset;
			let tryBufferSize = count + 1 - sizeof(char8*);
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
			char8* addlPtr = append char8[bufferSize]*;            
			let ptr = Ptr;
			let srcPtr = str.Ptr;
			for (int32 i = 0; i < count; i++)
			    ptr[i] = srcPtr[i + offset];
			ptr[count] = 0;
			mAllocSizeAndFlags = (uint32)bufferSize + (int32)sizeof(char8*);
			mLength = (int32)count;
		}

		[AllowAppend]
		public this(char8[] char8s, int offset, int count)
		{
			let tryBufferSize = count + 1 - sizeof(char8*);
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
			char8* addlPtr = append char8[bufferSize]*;            
			let ptr = Ptr;
			for (int i = 0; i < count; i++)
			    ptr[i] = char8s[i + offset];
			ptr[count] = 0;
			mAllocSizeAndFlags = (uint32)bufferSize + (int32)sizeof(char8*);
			mLength = (int32)count;
		}

		[AllowAppend]
        public this(char8* char8Ptr, int count)
        {
            let tryBufferSize = count + 1 - sizeof(char8*);
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
            char8* addlPtr = append char8[bufferSize]*;            
			let ptr = Ptr;
            for (int32 i = 0; i < count; i++)
                ptr[i] = char8Ptr[i];
            ptr[count] = 0;
            mAllocSizeAndFlags = (uint32)bufferSize + (int32)sizeof(char8*);
            mLength = (int32)count;
        }

		[AllowAppend]
		public this(StringView strView)
		{			
			let tryBufferSize = strView.Length + 1 - sizeof(char8*);
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
			char8* addlPtr = append char8[bufferSize]*;			
			let ptr = Ptr;
			Internal.MemCpy(ptr, strView.Ptr, strView.Length);
			ptr[strView.Length] = 0;
			mAllocSizeAndFlags = (uint32)bufferSize + (int32)sizeof(char8*);
			mLength = (int32)strView.Length;
		}

		[AllowAppend]
		public this(StringView strView, CreateFlags flags)
		{			
			let tryBufferSize = strView.Length + (flags.HasFlag(.NullTerminate) ? 1 : 0) - sizeof(char8*);
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
			char8* addlPtr = append char8[bufferSize]*;			
			let ptr = Ptr;
			Internal.MemCpy(ptr, strView.Ptr, strView.Length);
			if (flags.HasFlag(.NullTerminate))
				ptr[strView.Length] = 0;
			mAllocSizeAndFlags = (uint32)bufferSize + (int32)sizeof(char8*);
			mLength = (int32)strView.Length;
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
			let tryBufferSize = count - sizeof(char8*);
			let bufferSize = (tryBufferSize >= 0) ? tryBufferSize : 0;
#unwarn
			char8* addlPtr = append char8[bufferSize]*;
			let ptr = Ptr;
			int curIdx = 0;
			for (var str in strs)
			{
				Internal.MemCpy(ptr + curIdx, str.Ptr, str.mLength);
				curIdx += str.Length;
			}
			
			mLength = (int_strsize)count;
			mAllocSizeAndFlags = (uint32)bufferSize + (int_strsize)sizeof(char8*);
		}

		protected virtual void* Alloc(int size, int align)
		{
			return new char8[size]*;
		}

		protected virtual void Free(void* addr)
		{
			delete addr;
		}

        public ~this()
        {
            if (IsDynAlloc)
                delete:this mPtr;
		}

        public int Length
        {
			[Inline]
            get
            {
                return mLength;
			}
		}

		private int PrivateLength
		{
		    get
		    {
		        return mLength;
			}
		}

        int32 AllocSize
        {
			[Inline]
            get
            {
                return (int32)(mAllocSizeAndFlags & SizeFlags);
			}
		}

        bool IsDynAlloc
        {
            get
            {
                return (mAllocSizeAndFlags & DynAllocFlag) != 0;
			}
		}

		/// Gets character pointer
		public char8* Ptr
		{
			//[Optimize]
			get
			{
				return ((mAllocSizeAndFlags & StrPtrFlag) != 0) ? mPtr : (char8*)&mPtr;
			}
		}

		[AlwaysInclude]
        public char8* CStr()
        {
            return Ptr;
		}

		public int GetLength()
		{
			return mLength;
		}

		public static implicit operator char8*(String str)
		{
		    if (str == null)
				return null;
			return str.Ptr;
		}

		[Commutable]
		public static bool operator==(String s1, String s2)
		{
			return Equals(s1, s2);
		}

		public static int operator<=>(String s1, String s2)
		{
			Debug.FatalError();
		}

		public static bool Equals(String a, String b)
		{
			if ((Object)a == (Object)b)
				return true;
			if ((Object)a == null || (Object)b == null)
				return false;
			if (a.Length != b.Length)
				return false;
			return EqualsHelper((char8*)a, (char8*)b, a.mLength);
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

		public static bool EqualsHelper(char8* a, char8* b, int length)
		{
			for (int i = 0; i < length; i++)
				if (a[i] != b[i])
					return false;
			return true;
		}

		public bool EndsWith(String b)
		{
			if (mLength < b.mLength)
				return false;
			return EqualsHelper((char8*)this + mLength - b.mLength, (char8*)b, b.mLength);
		}

		public void Reference(char8* ptr)
		{
			if (IsDynAlloc)
				delete:this mPtr;
			mPtr = ptr;
			mLength = Internal.CStrLen(ptr);
			mAllocSizeAndFlags = (uint32)mLength | StrPtrFlag;
		}

		public void EnsureNullTerminator()
		{
			int allocSize = AllocSize;
			if ((allocSize == mLength) || (Ptr[mLength] != 0))
			{
				if (mLength >= allocSize)
					Realloc(CalcNewSize(mLength + 1));
				Ptr[mLength] = 0;
			}
		}

		public void Reference(char8* ptr, int length, int allocSize)
		{
			if (IsDynAlloc)
				delete:this mPtr;
			mPtr = ptr;
			mLength = (int_strsize)length;
			mAllocSizeAndFlags = (uint32)allocSize | StrPtrFlag;
		}

        public void Clear()
        {
            mLength = 0;
		}

        public void Set(String str)
        {
            mLength = 0;
			Realloc(str.mLength);
            Append(str);
		}

		int CalcNewSize(int minSize)
		{
			// Grow factor is 1.5
			int32 bumpSize = AllocSize;
			bumpSize += bumpSize / 2;
			return (bumpSize > minSize) ? bumpSize : minSize;
		}

		public void Reserve(int size)
		{
			if (size > AllocSize)
				Realloc((.)size);
		}

		void Realloc(int newSize)
		{
			Debug.Assert((uint32)newSize < 0x40000000);
			char8* newPtr = new:this char8[newSize]*;
			Internal.MemCpy(newPtr, Ptr, mLength + 1);
			if (IsDynAlloc)
				delete:this mPtr;
			mPtr = newPtr;
			mAllocSizeAndFlags = (uint32)newSize | DynAllocFlag | StrPtrFlag;
		}

		public void Append(char8* appendPtr, int length)
		{
			int32 newCurrentIndex = (int32)(mLength + length);
			if (newCurrentIndex >= AllocSize)
			{
				int32 newAllocSize = AllocSize;
				newAllocSize += newAllocSize / 2;
				int32 newSize = Math.Max(newAllocSize, newCurrentIndex + 1);
				Realloc(newSize);
			}

			let ptr = Ptr;
			Internal.MemCpy(ptr + mLength, appendPtr, length);
			mLength = newCurrentIndex;
			ptr[mLength] = 0;
		}

		private void Append(char8[] arr, int idx, int length)
		{
			Append(&arr.getRef(idx), length);
		}

		public void Append(StringView arr, int idx, int length)
		{
			Append(arr.Ptr + idx, length);
		}

        public void Append(StringView value)
		{
            //Contract.Ensures(Contract.Result<String>() != null);
			Append(value.Ptr, value.Length);
		}

		public void Append(String value)
		{
		    //Contract.Ensures(Contract.Result<String>() != null);
			Append(value.Ptr, value.mLength);
		}

		/// Appends a character to this string.
		public void Append(char8 c)
		{
			Append(c, 1);
		}

		/// Append a few characters to the string.
		/// @brief Append a few characters.
		public void Append(char8 c, int32 count)
		{
			if (count == 0)
				return;

			if (mLength + count >= AllocSize)
				Realloc(CalcNewSize(mLength + count + 1));
			let ptr = Ptr;
			for (int32 i = 0; i < count; i++)
				ptr[mLength++] = c;
			ptr[mLength] = 0;
			Debug.Assert(mLength < AllocSize);
		}

        public String Append(params String[] strings)
        {
            for (var str in strings)
                Append(str);
            return this;
		}

		public void Substring(int startIdx, int length, String outStr)
		{
			outStr.Append(Ptr + startIdx, (int32)length);
		}

		public void Substring(int startIdx, String outStr)
		{
			outStr.Append(Ptr + startIdx, (int32)(mLength - startIdx));
		}

		public void RemoveToEnd(int startIdx)
		{
			Remove(startIdx, mLength - startIdx);
		}

		public void TrimEnd()
		{
			let ptr = Ptr;
			for (int i = mLength - 1; i >= 0; i--)
			{
				char8 c = ptr[i];
				if (!c.IsWhiteSpace)
				{
					if (i < mLength - 1)
						RemoveToEnd(i + 1);
					return;
				}
			}
			Clear();
		}

        public char8 this[int index]
        {
            get         
            {
                if ((index < 0) || (index >= mLength))
					Debug.FatalError("String index out of bounds");
                return Ptr[index];
            }

            set
            {
                if ((index < 0) || (index >= mLength))
					Debug.FatalError("String index out of bounds");
                Ptr[index] = value;
            }
        }

        public static String Concat(params Object[] objects)
        {
            String[] strings = scope:: String[objects.Count];

            int32 totalLen = 0;
            for (int32 i = 0; i < objects.Count; i++)
            {
                var obj = objects[i];
                var str = obj as String;

                if (str == null)
                {
                    //TODO: stack alloc
                    str = new String();
                    obj.ToString(str);
				}
                totalLen += (int32)str.Length;
                strings[i] = str;
            }

            String newStr = new String(totalLen + 1);
            for (int32 i = 0; i < objects.Count; i++)
            {
                newStr.Append(strings[i]);
                if (!(objects[i] is String))
                    delete strings[i];
            }

            return newStr;
		}

		public void Remove(int startIdx, int length)
		{
			Debug.Assert((startIdx >= 0) && (length >= 0) && (startIdx + length <= mLength));
			int moveCount = mLength - startIdx - length;
			let ptr = Ptr;
			if (moveCount > 0)
				Internal.MemMove(ptr + startIdx, ptr + startIdx + length, mLength - startIdx - length);
			mLength -= (int32)length;
			ptr[mLength] = 0;
		}

        public String ConcatInto(params Object[] objects)
        {
            String[] strings = scope:: String[objects.Count];

            int32 totalLen = 0;
            for (int32 i = 0; i < objects.Count; i++)
            {
                var obj = objects[i];
                var str = obj as String;

                if (str == null)
                {
                    //TODO: stack alloc
                    str = new String();
                    obj.ToString(str);
				}
                totalLen += (int32)str.Length;
                strings[i] = str;
            }

            //TODO: Reserve(totalLen + 1);
            for (int32 i = 0; i < objects.Count; i++)
            {
                Append(strings[i]);
                if (!(objects[i] is String))
                    delete strings[i];
            }
            return this;
		}

		public static String Intern(String str)
		{
			return str;
		}

        public static bool IsNullOrEmpty(String str)
        {
            return (str == null) || (str.Length == 0);
		}

		public static mixin NewOrSet(var target, var source)
		{
			if (target == null)
				target = new String(source);
			else
				target.Set(source);
		}

		public mixin ToScopedNativeWChar()
		{
#unwarn
			String str = this;
			null
		}

		public mixin GetLen()
		{
			this.GetLength()
		}

		public Result<void> FormatInto(String format, params Object[] args)
		{
			ConcatInto(args);
			return .Ok;
		}

		struct ReplaceEntry
		{
			public int mIndex;
		}

		public void ReplaceLargerHelper(String find, String replace)
		{
			List<int> replaceEntries = scope List<int>(8192);
			
			int moveOffset = replace.mLength - find.mLength;

			for (int startIdx = 0; startIdx < mLength - find.mLength; startIdx++)
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
			int needSize = destLength + 1;
			if (needSize > AllocSize)
				Realloc((int32)needSize);

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

			ptr[destLength] = 0;
			mLength = (int32)destLength;
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

			int inIdx = 0;
			int outIdx = 0;

			while (inIdx < mLength - find.mLength)
			{
				if (EqualsHelper(ptr + inIdx, findPtr, find.mLength))
				{
					for (int32 i = 0; i < replace.mLength; i++)
						ptr[outIdx++] = replacePtr[i];

					inIdx += find.mLength;
				}
				else if (inIdx == outIdx)
				{
					++inIdx;
					++outIdx;
				}
				else // We need to physically move char8acters once we've found an equal span
				{
					ptr[outIdx++] = ptr[inIdx++];
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

			ptr[outIdx++] = 0;
			mLength = (.)outIdx;
		}

		public StringSplitEnumerator Split(params char8[] separators)
		{
			return StringSplitEnumerator(this, separators, StringSplitOptions.None);
		}

		public static mixin StackSplit(var target, var splitChar)
		{
			var strings = scope:mixin List<String>();
			for (var strView in target.Split(splitChar))
				strings.Add(scope:mixin String(strView));
			strings
		}
	}

    /*public struct StringView2
    {
        public this(String string)
        {
            mString = string;
            mOffset = 0;
            mLength = string.Length;
        }

        public this(String string, int32 offset)
        {
            mString = string;
            mOffset = offset;
            mLength = string.Length - mOffset;
        }

        public this(String string, int32 offset, int32 length)
        {
            mString = string;
            mOffset = offset;
            mLength = length;
        }

        public String mString;
        public int32 mOffset;
        public int32 mLength;

        public override void ToString(String strBuffer)
        {
            strBuffer.Append(mString.CStr() + mOffset, mLength);
        }

        public static bool operator==(StringView val1, StringView val2)
        {
            if (((Object)val1.mString == null) || ((Object)val2.mString == null))
                return (Object)val1.mString == (Object)val2.mString;
            if (val1.mOffset != val2.mOffset)
                return false;
            return String.EqualsHelper((char8*)val1.mString + val1.mOffset, (char8*)val2.mString + val2.mOffset, val1.mLength);
        }

		public static bool operator==(StringView val1, String val2)
		{
			if (val1.mLength != val2.Length)
				return false;
			char8* ptr1 = (char8*)val1.mString + val1.mOffset;
			char8* ptr2 = (char8*)val2;
			if (ptr1 == ptr2)
				return true;
			if ((ptr1 == null) || (ptr2 == null))
				return false;
			return String.EqualsHelper(ptr1, ptr2, val1.mLength);
		}
    }*/

	public enum StringSplitOptions
	{
		None = 0,
		RemoveEmptyEntries = 1
	}

	struct StringSplitEnumerator : IEnumerator<StringView>
	{
		StringSplitOptions mSplitOptions;
		char8[] mSplitChars;
		String mStr;
		int_strsize mPos;
		int_strsize mMatchPos;

		public this(String str, char8[] splitChars, StringSplitOptions splitOptions)
		{
			mStr = str;
			mSplitChars = splitChars;
			mPos = 0;
			mMatchPos = -1;
			mSplitOptions = splitOptions;
		}

		public StringView Current
	    {
	        get
			{
				return StringView(mStr, mPos, mMatchPos - mPos);
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

		public bool MoveNext() mut
		{
			mPos = mMatchPos + 1;

			while (true)
			{
				mMatchPos++;
				bool foundMatch = false;
				int endDiff = mStr.Length - mMatchPos;
				if (endDiff < 0)
					return false;
				if (endDiff == 0)
				{
					foundMatch = true;
				}
				else
				{
					char8 c = mStr[mMatchPos];
					for (char8 splitChar in mSplitChars)
						if (c == splitChar)
							foundMatch = true;
				}

				if (foundMatch)
				{
					if ((mMatchPos > mPos + 1) || (!mSplitOptions.HasFlag(StringSplitOptions.RemoveEmptyEntries)))
						return true;
					mPos = mMatchPos + 1;
				}
			}
		}

		public void Reset() mut
		{
			mPos = 0;
			mMatchPos = -1;
		}

		/// Disposes
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

	public struct StringView : Span<char8>
	{
		public this()
		{
			mPtr = null;
			mLength = 0;
		}

		public this(String string)
		{
			mPtr = string.Ptr;
			mLength = string.Length;
		}

		public this(String string, int offset)
		{
			mPtr = string.Ptr + offset;
			mLength = string.Length - offset;
		}

		public this(String string, int offset, int length)
		{
			mPtr = string.Ptr + offset;
			mLength = length;
		}

		public this(char8[] arr, int offset, int length)
		{
			mPtr = arr.CArray() + offset;
			mLength = length;
		}

		public override void ToString(String strBuffer)
		{
			strBuffer.Append(mPtr, mLength);
		}

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
			return String.EqualsHelper(ptr1, ptr2, val1.mLength);
		}

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
			return String.EqualsHelper(ptr1, ptr2, val1.mLength);
		}

		/*public static int Compare(StringView val1, StringView val2, bool ignoreCase = false)
		{
			if (ignoreCase)
				return String.[Friend]CompareOrdinalIgnoreCaseHelper(val1.mPtr, val1.mLength, val2.mPtr, val2.mLength);
			else
				return String.[Friend]CompareOrdinalHelper(val1.mPtr, val1.mLength, val2.mPtr, val2.mLength);
		}*/

		public static operator StringView (String str)
		{
			return StringView(str);
		}

		public mixin ToScopeCStr(int maxInlineChars = 128)
		{
			char8* ptr = null;
			if (mPtr != null)
			{
				if (mLength < maxInlineChars)
				{
					ptr = scope:mixin char8[mLength + 1]* (?);
				}
				else
				{
					ptr = new char8[mLength + 1]* (?);
					defer:mixin delete ptr;
				}
				Internal.MemCpy(ptr, mPtr, mLength);
				ptr[mLength] = 0;
			}
			ptr
		}

		/*public mixin ToNewCStr()
		{
			char8* ptr = null;
			if (mPtr != null)
			{
				ptr = new:mixin char8[mLength + 1]* (?);
				Internal.MemCpy(ptr, mPtr, mLength);
				ptr[mLength] = 0;
			}
			ptr
		}*/
	}

	static
	{
		public static mixin StackStringFormat(String format, var arg1)
		{
			var str = scope:: String();
			str.FormatInto(format, arg1);
			str
		}

		public static mixin StackStringFormat(String format, var arg1, var arg2)
		{
			var str = scope:: String();
			str.FormatInto(format, arg1, arg2);
			str
		}
	}
}
