#include "String.h"

USING_NS_BF;

//////////////////////////////////////////////////////////////////////////

static const uint8 sStringCharTab[256] = { 
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x8D, 0x8E, 0x8F,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9B, 0x9C, 0x9D, 0x9E, 0x9F,
	0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xAB, 0xAC, 0xAD, 0xAE, 0xAF,
	0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xBB, 0xBC, 0xBD, 0xBE, 0xBF,
	0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xCB, 0xCC, 0xCD, 0xCE, 0xCF,
	0xD0, 0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xDB, 0xDC, 0xDD, 0xDE, 0xDF,
	0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF,
	0xF0, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFB, 0xFC, 0xFD, 0xFE, 0xFF};

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

StringView::StringView(char c)
{
	mPtr = (const char*)&sStringCharTab[(uint8)c];
	mLength = 1;
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

bool StringView::StartsWith(const StringView& b, StringView::CompareKind comparisonType) const
{
	if (this->mLength < b.mLength)
		return false;
	if (comparisonType == StringView::CompareKind_OrdinalIgnoreCase)
		return String::EqualsIgnoreCaseHelper(mPtr, b.mPtr, b.mLength);
	return String::EqualsHelper(mPtr, b.mPtr, b.mLength);
}

bool StringView::EndsWith(const StringView& b, StringView::CompareKind comparisonType) const
{
	if (this->mLength < b.mLength)
		return false;
	if (comparisonType == StringView::CompareKind_OrdinalIgnoreCase)
		return String::EqualsIgnoreCaseHelper(this->mPtr + this->mLength - b.mLength, b.mPtr, b.mLength);
	return String::EqualsHelper(this->mPtr + this->mLength - b.mLength, b.mPtr, b.mLength);
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

intptr StringView::IndexOf(const StringView& subStr, int startIdx) const
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

intptr StringView::IndexOf(char c, int startIdx) const
{
	auto ptr = mPtr;
	for (intptr i = startIdx; i < mLength; i++)
		if (ptr[i] == c)
			return i;
	return -1;
}

intptr StringView::IndexOf(char c, int64 startIdx) const
{
	auto ptr = mPtr;
	for (int64 i = startIdx; i < mLength; i++)
		if (ptr[i] == c)
			return (intptr)i;
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

intptr StringView::LastIndexOf(char c, int startCheck) const
{
	auto ptr = mPtr;
	for (intptr i = startCheck; i >= 0; i--)
		if (ptr[i] == c)
			return i;
	return -1;
}

intptr StringView::LastIndexOf(char c, int64 startCheck) const
{
	auto ptr = mPtr;
	for (intptr i = (intptr)startCheck; i >= 0; i--)
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
	return StringSplitEnumerator(mPtr, (int)mLength, c, 0x7FFFFFFF, false);
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

void StringImpl::Reference(const StringImpl& str)
{
	Reference(str.GetPtr(), str.mLength);
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

bool StringImpl::EqualsIgnoreCaseHelper(const char * a, const char * b, intptr length)
{
	const char* curA = a;
	const char* curB = b;
	intptr curLength = length;

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
	Array<intptr> replaceEntries;

	intptr moveOffset = replace.mLength - find.mLength;

	for (intptr startIdx = 0; startIdx < mLength - find.mLength; startIdx++)
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

void StringImpl::Replace(char find, char replace)
{
	auto ptr = GetMutablePtr();
	for (int i = 0; i < mLength; i++)
	{
		if (ptr[i] == find)
			ptr[i] = replace;
	}
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

	intptr inIdx = 0;
	intptr outIdx = 0;

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
	mLength = (int_strsize)outIdx;
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

intptr StringImpl::IndexOf(char c, int startIdx) const
{
	auto ptr = GetPtr();
	for (intptr i = startIdx; i < mLength; i++)
		if (ptr[i] == c)
			return i;
	return -1;
}

intptr StringImpl::IndexOf(char c, int64 startIdx) const
{
	auto ptr = GetPtr();
	for (int64 i = startIdx; i < mLength; i++)
		if (ptr[i] == c)
			return (intptr)i;
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
