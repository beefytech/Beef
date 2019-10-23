#include "String.h"

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

StringView::StringView(const StringImpl& str)
{
	mPtr = str.GetPtr();
	mLength = str.mLength;
}

StringView::StringView(const StringImpl& str, int offset)
{
	mPtr = str.GetPtr() + offset;
	mLength = str.mLength - offset;
}

StringView::StringView(const StringImpl& str, int offset, int length)
{
	mPtr = str.GetPtr() + offset;
	mLength = length;
}

StringView& StringView::operator=(const StringImpl& str)
{
	mPtr = str.GetPtr();
	mLength = str.mLength;
	return *this;
}

bool StringView::operator==(const StringImpl& strB) const
{
	if (this->mLength != strB.mLength)
		return false;
	return strncmp(this->mPtr, strB.GetPtr(), this->mLength) == 0;
}

bool StringView::operator!=(const StringImpl& strB) const
{
	if (this->mLength != strB.mLength)
		return true;
	return strncmp(this->mPtr, strB.GetPtr(), this->mLength) != 0;
}

intptr StringView::IndexOf(const StringView& subStr, bool ignoreCase) const
{
	for (intptr ofs = 0; ofs <= mLength - subStr.mLength; ofs++)
	{
		if (String::Compare(*this, ofs, subStr, 0, subStr.mLength, ignoreCase) == 0)
			return ofs;
	}

	return -1;
}

intptr StringView::IndexOf(const StringView& subStr, int32 startIdx) const
{
	return IndexOf(subStr, (int64)startIdx);
}

intptr StringView::IndexOf(const StringView& subStr, int64 startIdx) const
{
	const char* ptr = mPtr;
	const char* subStrPtr = subStr.mPtr;
	for (intptr ofs = (intptr)startIdx; ofs <= (intptr)(mLength - subStr.mLength); ofs++)
	{
		if (strncmp(ptr + ofs, subStrPtr, subStr.mLength) == 0)
			return ofs;
	}

	return -1;
}

intptr StringView::IndexOf(char c, intptr startIdx) const
{
	auto ptr = mPtr;
	for (intptr i = startIdx; i < mLength; i++)
		if (ptr[i] == c)
			return i;
	return -1;
}

intptr StringView::LastIndexOf(char c) const
{
	auto ptr = mPtr;
	for (intptr i = mLength - 1; i >= 0; i--)
		if (ptr[i] == c)
			return i;
	return -1;
}

intptr StringView::LastIndexOf(char c, intptr startCheck) const
{
	auto ptr = mPtr;
	for (intptr i = startCheck; i >= 0; i--)
		if (ptr[i] == c)
			return i;
	return -1;
}

String StringView::ToString() const
{
	return String(this->mPtr, this->mLength);
}

void StringView::ToString(StringImpl& str) const
{
	str.Append(mPtr, mLength);
}

StringSplitEnumerator StringView::Split(char c)
{
	return StringSplitEnumerator(mPtr, mLength, c, 0x7FFFFFFF, false);
}

//////////////////////////////////////////////////////////////////////////

String Beefy::operator+(const StringImpl& lhs, const StringImpl& rhs)
{
	String str;
	str.Reserve(lhs.mLength + rhs.mLength + 1);
	str.Append(lhs);
	str.Append(rhs);
	return str;
}

String Beefy::operator+(const StringImpl& lhs, const StringView& rhs)
{
	String str;
	str.Reserve(lhs.mLength + rhs.mLength + 1);
	str.Append(lhs);
	str.Append(rhs);
	return str;
}

String Beefy::operator+(const StringImpl& lhs, const char* rhs)
{
	String str;
	int rhsLen = (int)strlen(rhs);
	str.Reserve(lhs.mLength + rhsLen + 1);
	str.Append(lhs);
	str.Append(rhs, rhsLen);
	return str;
}

String Beefy::operator+(const StringImpl& lhs, char rhs)
{
	String str;
	str.Reserve(lhs.mLength + 1 + 1);
	str.Append(lhs);
	str.Append(rhs);
	return str;
}

String Beefy::operator+(const char* lhs, const StringImpl& rhs)
{
	String str;
	int lhsLen = (int)strlen(lhs);
	str.Reserve(rhs.mLength + lhsLen + 1);
	str.Append(lhs, lhsLen);
	str.Append(rhs);
	return str;
}

String Beefy::operator+(const char* lhs, const StringView& rhs)
{
	String str;
	int lhsLen = (int)strlen(lhs);
	str.Reserve(rhs.mLength + lhsLen + 1);
	str.Append(lhs, lhsLen);
	str.Append(rhs);
	return str;
}

bool Beefy::operator==(const char* lhs, const StringImpl& rhs)
{
	return rhs == lhs;
}

bool Beefy::operator!=(const char* lhs, const StringImpl& rhs)
{
	return rhs != lhs;
}

// bool Beefy::operator==(const StringView& lhs, const StringImpl& rhs)
// {
// 	if (lhs.mLength != rhs.mLength)
// 		return false;
// 	return strncmp(lhs.mPtr, rhs.GetPtr(), lhs.mLength) == 0;
// }
// 
// bool Beefy::operator!=(const StringView& lhs, const StringImpl& rhs)
// {
// 	if (lhs.mLength != rhs.mLength)
// 		return true;
// 	return strncmp(lhs.mPtr, rhs.GetPtr(), lhs.mLength) != 0;
// }

//////////////////////////////////////////////////////////////////////////

StringImpl::StringImpl(const StringView& str)
{
	Init(str.mPtr, str.mLength);
}

void StringImpl::Reference(const char* str)
{
	Reference(str, strlen(str));
}

void StringImpl::Reference(const char* str, intptr length)
{
	if (IsDynAlloc())
		DeletePtr();
	mPtr = (char*)str;
	mLength = (int_strsize)length;
	mAllocSizeAndFlags = mLength | StrPtrFlag;
}

void StringImpl::Reference(const StringView& strView)
{
	Reference(strView.mPtr, strView.mLength);
}

String StringImpl::CreateReference(const StringView& strView)
{
	String str;
	str.Reference(strView);
	return str;
}

intptr StringImpl::CalcNewSize(intptr minSize)
{
	// Grow factor is 1.5
	intptr bumpSize = GetAllocSize();
	bumpSize += bumpSize / 2;
	return (bumpSize > minSize) ? bumpSize : minSize;
}

void StringImpl::Realloc(intptr newSize, bool copyStr)
{
	BF_ASSERT((uint32)newSize < 0x40000000);
	char* newPtr = AllocPtr(newSize);
	if (copyStr)
		memcpy(newPtr, GetPtr(), mLength + 1);
	if (IsDynAlloc())
		DeletePtr();
	mPtr = newPtr;
	mAllocSizeAndFlags = (uint32)newSize | DynAllocFlag | StrPtrFlag;
}

void StringImpl::Realloc(char* newPtr, intptr newSize)
{
	BF_ASSERT((uint32)newSize < 0x40000000);
	// We purposely don't copy the terminating NULL here, it's assumed the caller will do so
	memcpy(newPtr, GetPtr(), mLength);
	if (IsDynAlloc())
		DeletePtr();
	mPtr = newPtr;
	mAllocSizeAndFlags = (uint32)newSize | DynAllocFlag | StrPtrFlag;
}

void StringImpl::Reserve(intptr newSize)
{
	if (GetAllocSize() < newSize)
		Realloc(newSize, true);
}

bool StringImpl::EqualsHelper(const char * a, const char * b, intptr length)
{
	return strncmp(a, b, length) == 0;
}

bool StringImpl::EqualsIgnoreCaseHelper(const char * a, const char * b, int length)
{
	const char* curA = a;
	const char* curB = b;
	int curLength = length;

	/*Contract.Requires(strA != null);
	Contract.Requires(strB != null);
	Contract.EndContractBlock();*/
	while (curLength != 0)
	{
		int_strsize char8A = (int_strsize)*curA;
		int_strsize char8B = (int_strsize)*curB;

		//Contract.Assert((char8A | char8B) <= 0x7F, "strings have to be ASCII");

		// uppercase both char8s - notice that we need just one compare per char8
		if ((uint32)(char8A - 'a') <= (uint32)('z' - 'a')) char8A -= 0x20;
		if ((uint32)(char8B - 'a') <= (uint32)('z' - 'a')) char8B -= 0x20;

		//Return the (case-insensitive) difference between them.
		if (char8A != char8B)
			return false;

		// Next char8
		curA++; curB++;
		curLength--;
	}

	return true;
}

int StringImpl::CompareOrdinalIgnoreCaseHelper(const StringImpl & strA, const StringImpl & strB)
{
	/*Contract.Requires(strA != null);
	Contract.Requires(strB != null);
	Contract.EndContractBlock();*/
	int_strsize length = BF_MIN(strA.mLength, strB.mLength);

	const char* a = strA.GetPtr();
	const char* b = strB.GetPtr();

	while (length != 0)
	{
		int_strsize char8A = (int_strsize)*a;
		int_strsize char8B = (int_strsize)*b;

		//Contract.Assert((char8A | char8B) <= 0x7F, "strings have to be ASCII");

		// uppercase both char8s - notice that we need just one compare per char8
		if ((uint32)(char8A - 'a') <= (uint32)('z' - 'a')) char8A -= 0x20;
		if ((uint32)(char8B - 'a') <= (uint32)('z' - 'a')) char8B -= 0x20;

		//Return the (case-insensitive) difference between them.
		if (char8A != char8B)
			return char8A - char8B;

		// Next char8
		a++; b++;
		length--;
	}

	return strA.mLength - strB.mLength;
}

intptr StringImpl::CompareOrdinalIgnoreCaseHelper(const char * strA, intptr lengthA, const char * strB, intptr lengthB)
{
	const char* a = strA;
	const char* b = strB;
	intptr length = BF_MIN(lengthA, lengthB);

	while (length != 0)
	{
		int_strsize char8A = (int_strsize)*a;
		int_strsize char8B = (int_strsize)*b;

		//Contract.Assert((char8A | char8B) <= 0x7F, "strings have to be ASCII");
		// uppercase both char8s - notice that we need just one compare per char8
		if ((uint32)(char8A - 'a') <= (uint32)('z' - 'a')) char8A -= 0x20;
		if ((uint32)(char8B - 'a') <= (uint32)('z' - 'a')) char8B -= 0x20;

		//Return the (case-insensitive) difference between them.
		if (char8A != char8B)
			return char8A - char8B;

		// Next char8
		a++; b++;
		length--;
	}

	return lengthA - lengthB;
}

intptr StringImpl::CompareOrdinalIgnoreCaseHelper(const StringImpl & strA, intptr indexA, intptr lengthA, const StringImpl & strB, intptr indexB, intptr lengthB)
{
	return CompareOrdinalIgnoreCaseHelper(strA.GetPtr() + indexA, lengthA, strB.GetPtr() + indexB, lengthB);
}

intptr StringImpl::CompareOrdinalHelper(const char * strA, intptr lengthA, const char * strB, intptr lengthB)
{
	const char* a = strA;
	const char* b = strB;
	intptr length = BF_MIN(lengthA, lengthB);

	while (length != 0)
	{
		int_strsize char8A = (int_strsize)*a;
		int_strsize char8B = (int_strsize)*b;

		//Return the (case-insensitive) difference between them.
		if (char8A != char8B)
			return char8A - char8B;

		// Next char8
		a++; b++;
		length--;
	}

	return lengthA - lengthB;
}

intptr StringImpl::CompareOrdinalHelper(const StringImpl & strA, intptr indexA, intptr lengthA, const StringImpl & strB, intptr indexB, intptr lengthB)
{
	return CompareOrdinalHelper(strA.GetPtr() + indexA, lengthA, strB.GetPtr() + indexB, lengthB);
}

void StringImpl::Append(const char* appendPtr)
{
	Append(appendPtr, (int)strlen(appendPtr));
}

void StringImpl::Append(const char* appendPtr, intptr length)
{
	intptr newCurrentIndex = mLength + length;
	char* ptr;
	if (newCurrentIndex >= GetAllocSize())
	{
		// This handles appending to ourselves, we invalidate 'ptr' after calling Realloc
		intptr newSize = CalcNewSize(newCurrentIndex + 1);
		char* newPtr = AllocPtr(newSize);
		memcpy(newPtr + mLength, appendPtr, length);
		Realloc(newPtr, newSize);
		ptr = newPtr;
	}
	else
	{
		ptr = GetMutablePtr();
		memcpy(ptr + mLength, appendPtr, length);
	}
	mLength = (int_strsize)newCurrentIndex;
	ptr[mLength] = 0;
}

void StringImpl::Append(const StringView& value)
{
	//Contract.Ensures(Contract.Result<String>() != null);
	Append(value.mPtr, value.mLength);
}

void StringImpl::Append(const StringImpl& value)
{
	//Contract.Ensures(Contract.Result<String>() != null);
	Append(value.GetPtr(), value.mLength);
}

void StringImpl::Append(const StringImpl& str, const StringImpl& str2)
{
	Append(str.GetPtr(), str.mLength);
	Append(str2.GetPtr(), str2.mLength);
}

void StringImpl::Append(const StringImpl& str, const StringImpl& str2, const StringImpl& str3)
{
	Append(str.GetPtr(), str.mLength);
	Append(str2.GetPtr(), str2.mLength);
	Append(str3.GetPtr(), str3.mLength);
}

void StringImpl::Append(char c, int count)
{
	if (count == 0)
		return;

	if (mLength + count >= GetAllocSize())
		Realloc(CalcNewSize(mLength + count + 1));
	auto ptr = GetMutablePtr();
	for (int_strsize i = 0; i < count; i++)
		ptr[mLength++] = c;
	ptr[mLength] = 0;
	BF_ASSERT(mLength < GetAllocSize());
}

String StringImpl::Substring(intptr startIdx) const
{
	BF_ASSERT((uintptr)startIdx <= (uintptr)mLength);
	return String(GetPtr() + startIdx, mLength - startIdx);
}

String StringImpl::Substring(intptr startIdx, intptr length) const
{
	BF_ASSERT((startIdx >= 0) && (length >= 0) && (startIdx + length <= mLength));
	return String(GetPtr() + startIdx, length);
}

void StringImpl::Remove(intptr startIdx, intptr length)
{
	BF_ASSERT((startIdx >= 0) && (length >= 0) && (startIdx + length <= mLength));
	intptr moveCount = mLength - startIdx - length;
	auto ptr = GetMutablePtr();
	if (moveCount > 0)
		memmove(ptr + startIdx, ptr + startIdx + length, mLength - startIdx - length);
	mLength -= (int_strsize)length;
	ptr[mLength] = 0;
}

void StringImpl::Remove(intptr char8Idx)
{
	Remove(char8Idx, 1);
}

void StringImpl::RemoveToEnd(intptr startIdx)
{
	Remove(startIdx, mLength - startIdx);
}

void StringImpl::RemoveFromEnd(intptr length)
{
	Remove(mLength - length, length);
}

void StringImpl::Insert(intptr idx, const char* str, intptr length)
{
	BF_ASSERT(idx >= 0);
	
	int_strsize newLength = mLength + (int_strsize)length;
	if (newLength >= GetAllocSize())
	{
		intptr newSize = max((int_strsize)GetAllocSize() * 2, newLength + 1);
		Realloc(newSize);
	}

	auto moveChars = mLength - idx;
	auto ptr = GetMutablePtr();
	if (moveChars > 0)
		memmove(ptr + idx + length, ptr + idx, moveChars);
	memcpy(ptr + idx, str, length);
	mLength = newLength;
	ptr[mLength] = 0;
}

void StringImpl::Insert(intptr idx, const StringImpl& addString)
{
	BF_ASSERT(idx >= 0);

	int_strsize length = addString.mLength;
	int_strsize newLength = mLength + length;
	if (newLength >= GetAllocSize())
	{
		intptr newSize = max((int_strsize)GetAllocSize() * 2, newLength + 1);
		Realloc(newSize);
	}

	auto moveChars = mLength - idx;
	auto ptr = GetMutablePtr();
	if (moveChars > 0)
		memmove(ptr + idx + length, ptr + idx, moveChars);
	memcpy(ptr + idx, addString.GetPtr(), length);
	mLength = newLength;
	ptr[mLength] = 0;
}

void StringImpl::Insert(intptr idx, char c)
{
	BF_ASSERT(idx >= 0);

	int_strsize newLength = mLength + 1;
	if (newLength >= GetAllocSize())
	{
		int newSize = max((int_strsize)GetAllocSize() * 2, newLength + 1);
		Realloc(newSize);
	}

	auto moveChars = mLength - idx;
	auto ptr = GetMutablePtr();
	if (moveChars > 0)
		memmove(ptr + idx + 1, ptr + idx, moveChars);
	ptr[idx] = c;
	mLength = newLength;
	ptr[mLength] = 0;
}

intptr StringImpl::Compare(const StringImpl & strA, intptr indexA, const StringImpl & strB, intptr indexB, intptr length, bool ignoreCase)
{
	intptr lengthA = length;
	intptr lengthB = length;

	if (strA.GetLength() - indexA < lengthA)
	{
		lengthA = (strA.GetLength() - indexA);
	}

	if (strB.GetLength() - indexB < lengthB)
	{
		lengthB = (strB.GetLength() - indexB);
	}

	if (ignoreCase)
		return CompareOrdinalIgnoreCaseHelper(strA, indexA, lengthA, strB, indexB, lengthB);
	return CompareOrdinalHelper(strA, indexA, lengthA, strB, indexB, lengthB);
}

void StringImpl::ReplaceLargerHelper(const StringView& find, const StringView& replace)
{
	Array<int> replaceEntries;

	int_strsize moveOffset = replace.mLength - find.mLength;

	for (int startIdx = 0; startIdx < mLength - find.mLength; startIdx++)
	{
		if (EqualsHelper(GetPtr() + startIdx, find.mPtr, find.mLength))
		{
			replaceEntries.Add(startIdx);
			startIdx += find.mLength - 1;
		}
	}

	if (replaceEntries.size() == 0)
		return;

	intptr destLength = mLength + moveOffset * replaceEntries.size();
	intptr needSize = destLength + 1;
	if (needSize > GetAllocSize())
		Realloc((int_strsize)needSize);

	auto replacePtr = replace.mPtr;
	auto ptr = GetMutablePtr();

	intptr lastDestStartIdx = destLength;
	for (intptr moveIdx = replaceEntries.size() - 1; moveIdx >= 0; moveIdx--)
	{
		intptr srcStartIdx = replaceEntries[moveIdx];
		intptr srcEndIdx = srcStartIdx + find.mLength;
		intptr destStartIdx = srcStartIdx + moveIdx * moveOffset;
		intptr destEndIdx = destStartIdx + replace.mLength;

		for (intptr i = lastDestStartIdx - destEndIdx - 1; i >= 0; i--)
			ptr[destEndIdx + i] = ptr[srcEndIdx + i];

		for (intptr i = 0; i < replace.mLength; i++)
			ptr[destStartIdx + i] = replacePtr[i];

		lastDestStartIdx = destStartIdx;
	}

	ptr[destLength] = 0;
	mLength = (int_strsize)destLength;
}

void StringImpl::Replace(const StringView& find, const StringView & replace)
{
	if (replace.mLength > find.mLength)
	{
		ReplaceLargerHelper(find, replace);
		return;
	}

	auto ptr = GetMutablePtr();
	auto findPtr = find.mPtr;
	auto replacePtr = replace.mPtr;

	int_strsize inIdx = 0;
	int_strsize outIdx = 0;

	while (inIdx < mLength - find.mLength)
	{
		if (EqualsHelper(ptr + inIdx, findPtr, find.mLength))
		{
			for (int_strsize i = 0; i < replace.mLength; i++)
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

	ptr[outIdx] = 0;
	mLength = outIdx;
}

void StringImpl::TrimEnd()
{
	auto ptr = GetPtr();
	for (intptr i = mLength - 1; i >= 0; i--)
	{
		char c = ptr[i];
		if (!iswspace(c))
		{
			if (i < mLength - 1)
				RemoveToEnd(i + 1);
			return;
		}
	}
	Clear();
}

void StringImpl::TrimStart()
{
	auto ptr = GetPtr();
	for (intptr i = 0; i < mLength; i++)
	{
		char c = ptr[i];
		if (!iswspace(c))
		{
			if (i > 0)
				Remove(0, i);
			return;
		}
	}
	Clear();
}

void StringImpl::Trim()
{
	TrimStart();
	TrimEnd();
}

bool StringImpl::IsWhitespace() const
{
	auto ptr = GetPtr();
	for (intptr i = 0; i < mLength; i++)
		if (!iswspace(ptr[i]))
			return false;
	return true;
}

bool StringImpl::HasMultibyteChars()
{
	auto ptr = GetPtr();
	for (int i = 0; i < (int)mLength; i++)
		if ((uint8)ptr[i] >= (uint8)0x80)
			return true;
	return false;
}

intptr StringImpl::IndexOf(const StringView& subStr, bool ignoreCase) const
{
	for (intptr ofs = 0; ofs <= mLength - subStr.mLength; ofs++)
	{
		if (Compare(*this, ofs, subStr, 0, subStr.mLength, ignoreCase) == 0)
			return ofs;
	}

	return -1;
}

intptr StringImpl::IndexOf(const StringView& subStr, int32 startIdx) const
{
	return IndexOf(subStr, (int64)startIdx);
}

intptr StringImpl::IndexOf(const StringView& subStr, int64 startIdx) const
{
	const char* ptr = GetPtr();
	const char* subStrPtr = subStr.mPtr;
	for (intptr ofs = (intptr)startIdx; ofs <= mLength - subStr.mLength; ofs++)
	{
		if (strncmp(ptr + ofs, subStrPtr, subStr.mLength) == 0)
			return ofs;
	}

	return -1;
}

intptr StringImpl::IndexOf(char c, intptr startIdx) const
{
	auto ptr = GetPtr();
	for (intptr i = startIdx; i < mLength; i++)
		if (ptr[i] == c)
			return i;
	return -1;
}

intptr StringImpl::LastIndexOf(char c) const
{
	auto ptr = GetPtr();
	for (intptr i = mLength - 1; i >= 0; i--)
		if (ptr[i] == c)
			return i;
	return -1;
}

intptr StringImpl::LastIndexOf(char c, intptr startCheck) const
{
	auto ptr = GetPtr();
	for (intptr i = startCheck; i >= 0; i--)
		if (ptr[i] == c)
			return i;
	return -1;
}

//////////////////////////////////////////////////////////////////////////

UTF16String::UTF16String()
{

}

UTF16String::UTF16String(const wchar_t* str)
{
	Set(str);
}

UTF16String::UTF16String(const wchar_t* str, int len)
{
	Set(str, len);
}

void UTF16String::Set(const wchar_t* str, int len)
{
	Clear();
	ResizeRaw(len + 1);
	memcpy(mVals, str, len * 2);
	mVals[len] = 0;	
}

void UTF16String::Set(const wchar_t* str)
{
	return Set(str, (int)wcslen(str));
}

const wchar_t* UTF16String::c_str() const
{
	if (mVals == NULL)
		return L"";
	mVals[mSize - 1] = 0; // Re-terminate in case we modified the string
	return (wchar_t*)mVals;
}

size_t UTF16String::length() const
{
	if (mSize == 0)
		return 0;
	return mSize - 1;
}
