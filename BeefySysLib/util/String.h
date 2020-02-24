#pragma once

#include "BFPlatform.h"
#include "Array.h"
#include "SizedArray.h"

NS_BF_BEGIN;

template <const int TBufSize>
class StringT;

typedef StringT<16> String;

struct StringSplitEnumerator;

class StringView
{
public:
	const char* mPtr;
	int mLength;

public:
	StringView()
	{
		this->mPtr = NULL;
		this->mLength = 0;
	}

	StringView(const StringView& sv)
	{
		this->mPtr = sv.mPtr;
		this->mLength = sv.mLength;
	}
	
	StringView(const StringImpl& str);
	StringView(const StringImpl& str, int offset);
	StringView(const StringImpl& str, int offset, int length);
	StringView(char c);

	StringView(const char* ptr)
	{
		this->mPtr = ptr;
		this->mLength = (int)strlen(ptr);
	}

	StringView(const char* ptr, int length)
	{
		this->mPtr = ptr;
		this->mLength = length;
	}
	
	const char& operator[](intptr idx) const
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mLength);
		return this->mPtr[idx];
	}

	StringView& operator=(const StringImpl& str);

	StringView& operator=(const StringView& str)
	{
		this->mPtr = str.mPtr;
		this->mLength = str.mLength;
		return *this;
	}

	StringView& operator=(const char* str)
	{
		this->mPtr = str;
		this->mLength = (int)strlen(str);
		return *this;
	}

	bool operator==(const StringImpl& strB) const;

	bool operator!=(const StringImpl& strB) const;

	bool operator==(const StringView& strB) const
	{
		if (this->mLength != strB.mLength)
			return false;
		return strncmp(this->mPtr, strB.mPtr, this->mLength) == 0;
	}

	bool operator!=(const StringView& strB) const
	{
		if (this->mLength != strB.mLength)
			return true;
		return strncmp(this->mPtr, strB.mPtr, this->mLength) != 0;
	}

	bool operator==(const char* strB) const
	{
		if (strncmp(mPtr, strB, mLength) != 0)
			return false;
		return strB[mLength] == 0;
	}

	bool operator!=(const char* strB) const
	{
		if (strncmp(mPtr, strB, mLength) != 0)
			return true;
		return strB[mLength] != 0;
	}

	bool IsEmpty() const
	{
		return mLength == 0;
	}

	intptr length() const
	{
		return mLength;
	}

	intptr IndexOf(const StringView& subStr, bool ignoreCase = false) const;
	intptr IndexOf(const StringView& subStr, int32 startIdx) const;
	intptr IndexOf(const StringView& subStr, int64 startIdx) const;
	intptr IndexOf(char c, int32 startIdx = 0) const;
	intptr IndexOf(char c, int64 startIdx) const;
	intptr LastIndexOf(char c) const;
	intptr LastIndexOf(char c, int startCheck) const;
	intptr LastIndexOf(char c, int64 startCheck) const;
	String ToString() const;
	void ToString(StringImpl& str) const;
	void RemoveFromStart(intptr length)
	{
		BF_ASSERT((uintptr)length <= (uintptr)mLength);
		mPtr += length;
		mLength -= (int)length;
	}

	void RemoveToEnd(int startIdx)
	{
		RemoveFromEnd(mLength - startIdx);
	}

	void RemoveFromEnd(int length)
	{
		mLength -= length;
	}

	bool Contains(char c) const
	{
		return IndexOf(c) != -1;
	}

	bool Contains(const StringView& str) const
	{
		return IndexOf(str) != -1;
	}	

	StringSplitEnumerator Split(char c);	
};

struct StringSplitEnumerator
{
public:
	typedef std::random_access_iterator_tag iterator_category;
	typedef char value_type;
	typedef intptr difference_type;

	typedef char* pointer;
	typedef char& reference;

public:
	char mSplitChar0;
	SizedArray<char, 8> mSplitChars;
	const char* mPtr;
	int32 mStrLen;
	int32 mCurCount;
	int32 mMaxCount;
	int32 mPos;
	int32 mMatchPos;
	bool mRemoveEntryEntries;

	StringSplitEnumerator(const char* ptr, int strLength, const char* splitCharsPtr, int splitCharCount, int count, bool removeEmptyEntries)
	{
		mPtr = ptr;
		mStrLen = strLength;
		// 		if (splitChars.Count > 0)
		// 			mSplitChar0 = splitChars[0];
		// 		else
		// 			mSplitChar0 = '\0';
		mSplitChar0 = splitCharsPtr[0];
		if (splitCharCount > 1)
		{
			mSplitChars.Insert(0, splitCharsPtr + 1, splitCharCount - 1);
		}

		mCurCount = 0;
		mMaxCount = (int32)count;
		mPos = 0;
		mMatchPos = -1;

		MoveNext();
		//mSplitOptions = splitOptions;
	}

	StringSplitEnumerator(const char* ptr, int strLength, char splitChar, int count, bool removeEmptyEntries)
	{
		mPtr = ptr;
		mStrLen = strLength;
		mSplitChar0 = splitChar;
		mCurCount = 0;
		mMaxCount = (int32)count;
		mPos = 0;
		mMatchPos = -1;
		//mSplitOptions = splitOptions;
		MoveNext();
	}


	bool MoveNext()
	{
		if (mCurCount >= mMaxCount)
			return false;

		mPos = mMatchPos + 1;

		mCurCount++;
		if (mCurCount == mMaxCount)
		{
			mMatchPos = mStrLen;
			if (mPos > mMatchPos)
				return false;
			if ((mMatchPos == mPos) && (mRemoveEntryEntries))
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
				char c = mPtr[mMatchPos];
				if (c == mSplitChar0)
				{
					foundMatch = true;
				}
				else if (mSplitChars.mSize > 0)
				{
					for (int i = 1; i < mSplitChars.mSize; i++)
						if (c == mSplitChars[i])
							foundMatch = true;
				}
			}

			if (foundMatch)
			{
				if ((mMatchPos > mPos + 1) || (!mRemoveEntryEntries))
					return true;
				mPos = mMatchPos + 1;
				if (mPos >= mStrLen)
					return false;
			}
		}
	}

	StringSplitEnumerator& operator++()
	{
		MoveNext();
		return *this;
	}

	StringSplitEnumerator operator++(int)
	{
		auto prevVal = *this;
		MoveNext();
		return prevVal;
	}

	bool operator!=(const StringSplitEnumerator& itr) const
	{
		return
			(itr.mPtr != mPtr) ||
			(itr.mPos != mPos) ||
			(itr.mMatchPos != mMatchPos);
	}

	bool operator==(const StringSplitEnumerator& itr) const
	{
		return
			(itr.mPtr == mPtr) &&
			(itr.mPos == mPos) &&
			(itr.mMatchPos == mMatchPos);
	}

	StringView operator*()
	{
		return StringView(mPtr + mPos, mMatchPos - mPos);
	}

	StringView operator->()
	{
		return StringView(mPtr + mPos, mMatchPos - mPos);
	}

	bool operator<(const StringSplitEnumerator& val2)
	{
		return mPtr < val2.mPtr;
	}

	StringSplitEnumerator begin()
	{
		return *this;
	}

	StringSplitEnumerator end()
	{
		StringSplitEnumerator endVal = *this;
		endVal.mPos = endVal.mStrLen + 1;
		endVal.mMatchPos = endVal.mStrLen;		
		return endVal;
	}
};

class StringImpl
{
public:
	enum CompareKind
	{
		CompareKind_CurrentCulture = 0,
		CompareKind_CurrentCultureIgnoreCase = 1,
		CompareKind_InvariantCulture = 2,
		CompareKind_InvariantCultureIgnoreCase = 3,
		CompareKind_Ordinal = 4,
		CompareKind_OrdinalIgnoreCase = 5,
	};

public:
	typedef int int_strsize;

	const static uint32 SizeFlags = 0x3FFFFFFF;
	const static uint32 DynAllocFlag = 0x80000000;
	const static uint32 StrPtrFlag = 0x40000000;
	const static uint32 AttrFlags = 0xC0000000;

	int mLength;
	uint32 mAllocSizeAndFlags;
	char* mPtr;

public:
	struct iterator
	{
	public:
		typedef std::random_access_iterator_tag iterator_category;
		typedef char value_type;
		typedef intptr difference_type;

		typedef char* pointer;
		typedef char& reference;

	public:
		char* mPtr;

	public:
		iterator(char* ptr)
		{
			mPtr = ptr;
		}

		iterator& operator++()
		{
			mPtr++;
			return *this;
		}

		iterator operator++(int)
		{
			auto prevVal = *this;
			mPtr++;
			return prevVal;
		}

		bool operator!=(const iterator& itr) const
		{
			return itr.mPtr != mPtr;
		}

		bool operator==(const iterator& itr) const
		{
			return itr.mPtr == mPtr;
		}

		intptr operator-(const iterator& itr) const
		{
			return mPtr - itr.mPtr;
		}

		iterator operator+(intptr offset) const
		{
			iterator itr(mPtr + offset);			
			return itr;
		}

		char& operator*()
		{
			return *mPtr;
		}

		char* operator->()
		{
			return mPtr;
		}

		bool operator<(const iterator& val2)
		{
			return mPtr < val2.mPtr;
		}
	};

	struct const_iterator
	{
	public:
		typedef std::random_access_iterator_tag iterator_category;
		typedef char value_type;
		typedef intptr difference_type;

		typedef const char* pointer;
		typedef const char& reference;

	public:
		const char* mPtr;

	public:
		const_iterator(const char* ptr)
		{
			mPtr = ptr;
		}

		const_iterator& operator++()
		{
			mPtr++;
			return *this;
		}

		const_iterator operator++(int)
		{
			auto prevVal = *this;
			mPtr++;
			return prevVal;
		}

		bool operator!=(const const_iterator& itr) const
		{
			return itr.mPtr != mPtr;
		}

		bool operator==(const const_iterator& itr) const
		{
			return itr.mPtr == mPtr;
		}

		intptr operator-(const iterator& itr) const
		{
			return mPtr - itr.mPtr;
		}

		const_iterator operator+(intptr offset) const
		{
			const_iterator itr(mPtr + offset);
			return itr;
		}

		const char& operator*()
		{
			return *mPtr;
		}

		const char* operator->()
		{
			return mPtr;
		}

		bool operator<(const const_iterator& val2)
		{
			return mPtr < val2.mPtr;
		}
	};

protected:
	void EnsureMutable()
	{
		if ((mAllocSizeAndFlags & AttrFlags) == StrPtrFlag)
		{
			// It's a reference
			char* newPtr = AllocPtr(this->mLength);
			memcpy(newPtr, this->mPtr, this->mLength + 1);
			this->mPtr = newPtr;
			mAllocSizeAndFlags = this->mLength | DynAllocFlag | StrPtrFlag;
		}
	}

	char* AllocPtr(intptr size)
	{
		return (char*)malloc(size);
	}

	void DeletePtr()
	{
		free(this->mPtr);
	}

	intptr CalcNewSize(intptr minSize);	
	void Realloc(intptr newSize, bool copyStr = true);
	void Realloc(char* newPtr, intptr newSize);		
	static bool EqualsHelper(const char* a, const char* b, intptr length);	
	static bool EqualsIgnoreCaseHelper(const char* a, const char* b, int length);	
	static int CompareOrdinalIgnoreCaseHelper(const StringImpl& strA, const StringImpl& strB);
	static intptr CompareOrdinalIgnoreCaseHelper(const char* strA, intptr lengthA, const char* strB, intptr lengthB);
	static intptr CompareOrdinalIgnoreCaseHelper(const StringImpl& strA, intptr indexA, intptr lengthA, const StringImpl& strB, intptr indexB, intptr lengthB);	
	static intptr CompareOrdinalHelper(const char* strA, intptr lengthA, const char* strB, intptr lengthB);
	static intptr CompareOrdinalHelper(const StringImpl& strA, intptr indexA, intptr lengthA, const StringImpl& strB, intptr indexB, intptr lengthB);

	void Init(const char* charPtr, int_strsize count)
	{
		int_strsize internalSize = (int_strsize)(sizeof(StringImpl) - offsetof(StringImpl, mPtr));
		int_strsize allocSize = count + 1;

		if (allocSize <= internalSize)
		{
			// Fits
			auto ptr = (char*)&this->mPtr;
			memcpy(ptr, charPtr, count);
			ptr[count] = 0;
			mAllocSizeAndFlags = internalSize;
			this->mLength = count;
		}
		else
		{
			// Too big, must alloc
			auto ptr = AllocPtr(allocSize);
			memcpy(ptr, charPtr, count);
			ptr[count] = 0;
			this->mPtr = ptr;
			mAllocSizeAndFlags = allocSize | DynAllocFlag | StrPtrFlag;
			this->mLength = count;
		}
	}

protected:
	// Use "String" or "StringT<>"
	StringImpl()
	{

	}


public:	
	static StringImpl MakeRef(const char* charPtr)
	{
		StringImpl str;
		// This is just a ref - called when we pass a literal to a method (for example)
		str.mPtr = (char*)charPtr;
		str.mLength = (int_strsize)strlen(charPtr);
		str.mAllocSizeAndFlags = str.mLength | StrPtrFlag;
		return str;
	}	

	static StringImpl MakeRef(const StringView& strView)
	{
		StringImpl str;
		// This is just a ref - called when we pass a literal to a method (for example)
		str.mPtr = (char*)strView.mPtr;
		str.mLength = (int_strsize)strView.mLength;
		str.mAllocSizeAndFlags = str.mLength | StrPtrFlag;
		return str;
	}

	StringImpl(const char* charPtr)
	{
		// This is just a ref - called when we pass a literal to a method (for example)
		this->mPtr = (char*)charPtr;
		this->mLength = (int_strsize)strlen(charPtr);
		mAllocSizeAndFlags = this->mLength | StrPtrFlag;
	}

	StringImpl(const char* charPtr, const char* charPtrEnd)
	{
		Init(charPtr, (int_strsize)(charPtrEnd - charPtr));
	}

	StringImpl(const char* charPtr, intptr length)
	{
		Init(charPtr, (int_strsize)length);
	}

	StringImpl(const StringImpl& str)
	{
		Init(str.GetPtr(), str.mLength);
	}

	StringImpl(const StringView& str);

	StringImpl(const StringImpl& str, intptr offset)
	{
		Init(str.GetPtr() + offset, (int_strsize)(str.mLength - offset));
	}

	StringImpl(const StringImpl& str, intptr offset, intptr length)
	{
		Init(str.GetPtr() + offset, (int_strsize)length);
	}

	StringImpl(const std::string& str)
	{
		// This is just a ref - called when we pass a std::string to a method (for example)
		this->mPtr = (char*)str.c_str();
		this->mLength = (int_strsize)str.length();
		mAllocSizeAndFlags = this->mLength | StrPtrFlag;
	}

	StringImpl(StringImpl&& str)
	{
		if ((str.mAllocSizeAndFlags & StrPtrFlag) != 0)
		{
			this->mPtr = str.mPtr;
			mAllocSizeAndFlags = str.mAllocSizeAndFlags;
			this->mLength = str.mLength;

			str.mAllocSizeAndFlags = 0;
		}
		else
		{
			// If there's an internal buffer then we have to copy
			int_strsize count = (int_strsize)str.mLength;
			int_strsize allocSize = count + 1;
			auto ptr = AllocPtr(allocSize);
			memcpy(ptr, str.GetPtr(), count + 1);
			ptr[count] = 0;
			this->mPtr = ptr;
			mAllocSizeAndFlags = allocSize | DynAllocFlag | StrPtrFlag;
			this->mLength = count;
		}
	}
	
	~StringImpl()
	{
		if (IsDynAlloc())
			DeletePtr();
	}

	char& operator[](intptr idx)
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mLength);
		switch (mAllocSizeAndFlags & AttrFlags)
		{
		case 0:
			return ((char*)&this->mPtr)[idx];
		case StrPtrFlag:
			EnsureMutable();
		default:
			return this->mPtr[idx];
		}
	}

	const char& operator[](intptr idx) const
	{
		BF_ASSERT((uintptr)idx < (uintptr)this->mLength);
		return ((mAllocSizeAndFlags & StrPtrFlag) != 0) ? this->mPtr[idx] : ((char*)&this->mPtr)[idx];
	}

	bool operator==(const StringImpl& strB) const
	{
		if (this->mLength != strB.mLength)
			return false;
		return strncmp(GetPtr(), strB.GetPtr(), this->mLength) == 0;
	}

	bool operator!=(const StringImpl& strB) const
	{
		if (this->mLength != strB.mLength)
			return true;
		return strncmp(GetPtr(), strB.GetPtr(), this->mLength) != 0;
	}

	bool operator==(const StringView& strB) const
	{
		if (this->mLength != strB.mLength)
			return false;
		return strncmp(GetPtr(), strB.mPtr, this->mLength) == 0;
	}

	bool operator!=(const StringView& strB) const
	{
		if (this->mLength != strB.mLength)
			return true;
		return strncmp(GetPtr(), strB.mPtr, this->mLength) != 0;
	}

	bool operator==(const char* strB) const
	{
		return strcmp(GetPtr(), strB) == 0;
	}

	bool operator!=(const char* strB) const
	{
		return strcmp(GetPtr(), strB) != 0;
	}

	bool operator<(const StringImpl& strB) const
	{
		return strcmp(GetPtr(), strB.GetPtr()) < 0;
	}

	bool operator>(const StringImpl& strB) const
	{
		return strcmp(GetPtr(), strB.GetPtr()) > 0;
	}	
	
	StringImpl& operator=(const StringImpl& str)
	{
		if (&str != this)			
		{
			this->mLength = 0;
			Append(str.GetPtr(), str.mLength);
		}
		return *this;
	}

	StringImpl& operator=(StringImpl&& str)
	{
		if ((str.mAllocSizeAndFlags & StrPtrFlag) != 0)
		{
			if (IsDynAlloc())
				DeletePtr();

			this->mPtr = str.mPtr;
			mAllocSizeAndFlags = str.mAllocSizeAndFlags;
			this->mLength = str.mLength;

			str.mAllocSizeAndFlags = 0;
		}
		else
		{
			// If there's an internal buffer then we have to copy
			int_strsize count = (int_strsize)str.mLength;
			int_strsize allocSize = count + 1;			
			if (allocSize > GetAllocSize())
				Realloc(allocSize, false);
			auto ptr = GetMutablePtr();
			memcpy(ptr, str.GetPtr(), count + 1);
			ptr[count] = 0;			
			this->mLength = count;
		}		
		return *this;
	}

	StringImpl& operator=(const std::string& str)
	{
		this->mLength = 0;
		Append(str.c_str(), (intptr)str.length());
		return *this;
	}

	StringImpl& operator=(const char* str)
	{
		this->mLength = 0;
		Append(str, strlen(str));
		return *this;
	}

	StringImpl& operator=(char c)
	{
		this->mLength = 0;
		Append(c);
		return *this;
	}

	StringImpl& operator+=(const StringImpl& str)
	{
		Append(str.GetPtr(), str.mLength);
		return *this;
	}

	StringImpl& operator+=(const StringView& str)
	{
		Append(str.mPtr, str.mLength);
		return *this;
	}

	StringImpl& operator+=(const char* str)
	{
		Append(str, strlen(str));
		return *this;
	}

	StringImpl& operator+=(char c)
	{
		Append(c);
		return *this;
	}

	operator std::string() const
	{
		return std::string(GetPtr(), GetPtr() + this->mLength);
	}

	intptr length() const
	{
		return this->mLength;
	}

	int GetLength() const
	{
		return this->mLength;
	}

	intptr GetAllocSize() const
	{
		return (int_strsize)(mAllocSizeAndFlags & SizeFlags);
	}

	bool IsDynAlloc() const
	{
		return (mAllocSizeAndFlags & DynAllocFlag) != 0;
	}

	const char* GetPtr() const
	{
		return ((mAllocSizeAndFlags & StrPtrFlag) != 0) ? this->mPtr : (char*)&this->mPtr;
	}

	const char* c_str() const
	{
		return ((mAllocSizeAndFlags & StrPtrFlag) != 0) ? this->mPtr : (char*)&this->mPtr;
	}

	char* GetMutablePtr()
	{
		switch (mAllocSizeAndFlags & AttrFlags)
		{
		case 0:
			return ((char*)&this->mPtr);
		case StrPtrFlag:
			EnsureMutable();
		default:
			return this->mPtr;
		}
	}

	void Reference(const char* str);
	void Reference(const char* str, intptr length);
	void Reference(const StringView& strView);
	static String CreateReference(const StringView& strView);
	void Reserve(intptr newSize);

	void Append(const char* appendPtr);
	void Append(const char* appendPtr, intptr length);	
	void Append(const StringView& str);	
	void Append(const StringImpl& str);
	void Append(const StringImpl& str, const StringImpl& str2);
	void Append(const StringImpl& str, const StringImpl& str2, const StringImpl& str3);
	void Append(char c, int count = 1);		

	void Clear()
	{
		this->mLength = 0;
		GetMutablePtr()[0] = 0;
	}

	void clear()
	{
		this->mLength = 0;
		GetMutablePtr()[0] = 0;
	}

	String Substring(intptr startIdx) const;
	String Substring(intptr startIdx, intptr length) const;

	void Remove(intptr startIdx, intptr length);	
	void Remove(intptr char8Idx);	
	void RemoveToEnd(intptr startIdx);		
	void RemoveFromEnd(intptr length);	
	void Insert(intptr idx, const StringImpl& addString);
	void Insert(intptr idx, const char* str, intptr len);
	void Insert(intptr idx, char c);

	intptr CompareTo(const StringImpl& strB, bool ignoreCase = false) const
	{
		if (ignoreCase)
			return CompareOrdinalIgnoreCaseHelper(GetPtr(), GetLength(), strB.GetPtr(), strB.GetLength());
		return CompareOrdinalHelper(GetPtr(), GetLength(), strB.GetPtr(), strB.GetLength());
	}

	static intptr Compare(const StringImpl& strA, const StringImpl& strB, bool ignoreCase)
	{		
		if (ignoreCase)
			return CompareOrdinalIgnoreCaseHelper(strA.GetPtr(), strA.GetLength(), strB.GetPtr(), strB.GetLength());
		return CompareOrdinalHelper(strA.GetPtr(), strA.GetLength(), strB.GetPtr(), strB.GetLength());
	}

	static intptr Compare(const StringImpl& strA, intptr indexA, const StringImpl& strB, intptr indexB, intptr length, bool ignoreCase);
	
	bool Equals(const StringImpl& b, CompareKind comparisonType = CompareKind_Ordinal) const
	{
		return Equals(*this, b, comparisonType);
	}

	static bool Equals(const StringImpl& a, const StringImpl& b, CompareKind comparisonType = CompareKind_Ordinal)
	{
		if (a.mLength != b.mLength)
			return false;
		if (comparisonType == CompareKind_OrdinalIgnoreCase)
			return EqualsIgnoreCaseHelper(a.GetPtr(), b.GetPtr(), a.mLength);
		return EqualsHelper(a.GetPtr(), b.GetPtr(), a.mLength);
	}

	bool StartsWith(const StringView& b, CompareKind comparisonType = CompareKind_Ordinal) const
	{
		if (this->mLength < b.mLength)
			return false;
		if (comparisonType == CompareKind_OrdinalIgnoreCase)
			return EqualsIgnoreCaseHelper(this->GetPtr(), b.mPtr, b.mLength);
		return EqualsHelper(this->GetPtr(), b.mPtr, b.mLength);
	}

	bool EndsWith(const StringView& b, CompareKind comparisonType = CompareKind_Ordinal) const
	{
		if (this->mLength < b.mLength)
			return false;
		if (comparisonType == CompareKind_OrdinalIgnoreCase)
			return EqualsIgnoreCaseHelper(this->GetPtr() + this->mLength - b.mLength, b.mPtr, b.mLength);
		return EqualsHelper(this->GetPtr() + this->mLength - b.mLength, b.mPtr, b.mLength);
	}

	bool StartsWith(char c) const
	{
		if (this->mLength == 0)
			return false;
		return GetPtr()[0] == c;
	}

	bool EndsWith(char c) const
	{
		if (this->mLength == 0)
			return false;		
		return GetPtr()[this->mLength - 1] == c;
	}

	void ReplaceLargerHelper(const StringView& find, const StringView& replace);
	void Replace(const StringView& find, const StringView& replace);
	void TrimEnd();	
	void TrimStart();	
	void Trim();	
	bool IsWhitespace() const;
	bool IsEmpty() const
	{
		return this->mLength == 0;
	}

	bool empty() const
	{
		return this->mLength == 0;
	}

	bool HasMultibyteChars();
	intptr IndexOf(const StringView& subStr, bool ignoreCase = false) const;
	intptr IndexOf(const StringView& subStr, int32 startIdx) const;
	intptr IndexOf(const StringView& subStr, int64 startIdx) const;	
	intptr IndexOf(char c, int32 startIdx = 0) const;	
	intptr IndexOf(char c, int64 startIdx) const;
	intptr LastIndexOf(char c) const;	
	intptr LastIndexOf(char c, intptr startCheck) const;
	
	StringSplitEnumerator Split(char c)
	{
		return StringSplitEnumerator(GetPtr(), mLength, c, 0x7FFFFFFF, false);
	}

	bool Contains(char c) const
	{
		return IndexOf(c) != -1;
	}
	
	bool Contains(const StringView& str) const
	{
		return IndexOf(str) != -1;
	}

	const_iterator begin() const
	{
		return GetPtr();
	}

	const_iterator end() const
	{
		return GetPtr() + this->mLength;
	}

	iterator begin()
	{
		return GetMutablePtr();
	}

	iterator end()
	{
		return GetMutablePtr() + this->mLength;
	}
};

template <const int TBufSize = 16>
class StringT : public StringImpl
{
protected:
	void Init(const char* charPtr, int_strsize count)
	{
		int_strsize internalSize = (int_strsize)(sizeof(StringT) - offsetof(StringImpl, mPtr));
		int_strsize allocSize = count + 1;

		if (allocSize <= internalSize)
		{
			// Fits
			auto ptr = (char*)&this->mPtr;
			memcpy(ptr, charPtr, count);
			ptr[count] = 0;
			mAllocSizeAndFlags = internalSize;
			this->mLength = count;
		}
		else
		{
			// Too big, must alloc
			auto ptr = AllocPtr(allocSize);
			memcpy(ptr, charPtr, count);
			ptr[count] = 0;
			this->mPtr = ptr;
			mAllocSizeAndFlags = allocSize | DynAllocFlag | StrPtrFlag;
			this->mLength = count;
		}
	}

public:
	char mInternalBuffer[TBufSize - sizeof(intptr)];

	using StringImpl::operator=;

	StringT()
	{
		int_strsize internalSize = (int_strsize)(sizeof(StringT) - offsetof(StringImpl, mPtr));
		this->mPtr = 0;
		mAllocSizeAndFlags = internalSize;
		this->mLength = 0;
	}

	StringT(const char* charPtr)
	{
		Init(charPtr, (int_strsize)strlen(charPtr));
	}

	StringT(const char* charPtr, const char* charPtrEnd)
	{
		Init(charPtr, (int_strsize)(charPtrEnd - charPtr));
	}

	StringT(const char* charPtr, intptr length)
	{
		Init(charPtr, (int_strsize)length);
	}

	StringT(const StringT& str)
	{
		Init(str.GetPtr(), str.mLength);
	}

	StringT(StringT&& str)
	{
		if ((str.mAllocSizeAndFlags & StrPtrFlag) != 0)
		{
			this->mPtr = str.mPtr;
			mAllocSizeAndFlags = str.mAllocSizeAndFlags;
			this->mLength = str.mLength;

			str.mAllocSizeAndFlags = 0;
		}
		else
		{
			// If there's an internal buffer then we have to copy
			Init(str.GetPtr(), str.mLength);
		}
	}

	StringT(const StringT& str, intptr offset)
	{
		Init(str.GetPtr() + offset, (int_strsize)(str.mLength - offset));
	}

	StringT(const StringT& str, intptr offset, intptr length)
	{
        BF_ASSERT(offset >= 0);
        BF_ASSERT((uintptr)offset + (uintptr)length <= (uintptr)str.mLength);
		Init(str.GetPtr() + offset, (int_strsize)length);
	}

	StringT(const StringImpl& str)
	{
		Init(str.GetPtr(), str.mLength);
	}

	StringT(const std::string& str)
	{
		Init(str.c_str(), (int_strsize)str.length());
	}

	StringT(StringImpl&& str)
	{
		if ((str.mAllocSizeAndFlags & StrPtrFlag) != 0)
		{
			this->mPtr = str.mPtr;
			mAllocSizeAndFlags = str.mAllocSizeAndFlags;
			this->mLength = str.mLength;

			str.mAllocSizeAndFlags = 0;
		}
		else
		{
			// If there's an internal buffer then we have to copy
			Init(str.GetPtr(), str.mLength);
		}
	}

	StringImpl& operator=(StringT&& str)
	{
		return StringImpl::operator=(std::move(str));
	}

	StringImpl& operator=(const StringT& str)
	{
		return StringImpl::operator=(str);
	}
};

class UTF16String : public Array<uint16>
{
public:
	UTF16String();
	UTF16String(const wchar_t* str);
	UTF16String(const wchar_t* str, int len);

	void Set(const wchar_t* str, int len);
	void Set(const wchar_t* str);
	const wchar_t* c_str() const;
	size_t length() const;	
};

#define BF_SPECIALIZE_STR(size) \
template <> class StringT<size> : public StringImpl \
{ \
public: \
	using StringImpl::StringImpl; \
	using StringImpl::operator=; \
	StringT() { mPtr = NULL; mLength = 0; mAllocSizeAndFlags = 0; } \
	explicit StringT(const char* str) { Init(str, (int_strsize)strlen(str)); } \
	StringT(const std::string& str) { Init(str.c_str(), (int_strsize)str.length()); } \
	StringT(const StringImpl& str) : StringImpl(str) {} \
	StringT(StringImpl&& str) : StringImpl(std::move(str)) {} \
};
BF_SPECIALIZE_STR(0)
BF_SPECIALIZE_STR(1)
BF_SPECIALIZE_STR(2)
BF_SPECIALIZE_STR(3)
#ifdef BF64
BF_SPECIALIZE_STR(4)
BF_SPECIALIZE_STR(5)
BF_SPECIALIZE_STR(6)
#endif
#undef BF_SPECIALIZE_STR

String operator+(const StringImpl& lhs, const StringImpl& rhs);
String operator+(const StringImpl& lhs, const StringView& rhs);
String operator+(const StringImpl& lhs, const char* rhs);
String operator+(const StringImpl& lhs, char rhs);
String operator+(const char* lhs, const StringImpl& rhs);
String operator+(const char* lhs, const StringView& rhs);

bool operator==(const char* lhs, const StringImpl& rhs);
bool operator!=(const char* lhs, const StringImpl& rhs);
// bool operator==(const StringView& lhs, const StringImpl& rhs);
// bool operator!=(const StringView& lhs, const StringImpl& rhs);

NS_BF_END;

namespace std
{
	template<>
	struct hash<Beefy::StringImpl>
	{
		size_t operator()(const Beefy::StringImpl& val) const
		{
			return HashBytes((const uint8*)val.GetPtr(), val.mLength);
		}
	};

	template<>
	struct hash<Beefy::StringView>
	{
		size_t operator()(const Beefy::StringView& val) const
		{
			return HashBytes((const uint8*)val.mPtr, val.mLength);
		}
	};

	template<>
	struct hash<Beefy::String>
	{
		size_t operator()(const Beefy::String& val) const
		{
			return HashBytes((const uint8*)val.GetPtr(), val.mLength);
		}
	};
}

