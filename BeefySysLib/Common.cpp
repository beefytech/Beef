#include "Common.h"
#include "util/BumpAllocator.h"
#include "util/UTF8.h"
#include "util/String.h"
//#include "farts.z"
#include <string.h>
#include "platform/PlatformHelper.h"

#ifndef BF_SMALL
#define STB_SPRINTF_DECORATE(name) BF_stbsp_##name
#define STB_SPRINTF_IMPLEMENTATION
#include "third_party/stb/stb_sprintf.h"
#endif

extern "C"
{
#include "third_party/utf8proc/utf8proc.h"
}


#ifdef BF_PLATFORM_WINDOWS
#include <shellapi.h>
#include <direct.h>
#endif

#pragma warning(disable:4996)
#pragma comment(lib, "winmm.lib")

int gBFArgC;
char** gBFArgV;

#ifdef BUMPALLOC_ETW_TRACING
VSHeapTracker::CHeapTracker* Beefy::BumpAllocator::mVSHeapTracker = NULL;
#pragma comment(lib, "VSCustomNativeHeapEtwProvider.lib")
#endif

USING_NS_BF;

UTF16String Beefy::ToWString(const StringImpl& theString)
{
	UTF16String aString;
	aString.Reserve(theString.length() + 1);
	for (intptr i = 0; i < theString.length(); ++i)
		aString.Add((unsigned char)theString[i]);
	aString.Add(0);
	return aString;
}

// String Beefy::ToString(const UTF16String& theString)
// {
// 	String newString;
// 	newString.Reserve((intptr)theString.length());
// 	for (int i = 0; i < (int)theString.length(); ++i)
// 	{
// 		const unsigned int c = (unsigned int)theString[i];
// 		newString.Append(char(c & 0xFF));
// 	}
// 	return newString;
// }

String Beefy::ToUpper(const StringImpl& theString)
{
	String aString = theString;
	for (int i = 0; i < (int)aString.length(); ++i)
		aString[i] = toupper(aString[i]);
	return aString;
}

void Beefy::MakeUpper(StringImpl& theString)
{
	for (int i = 0; i < (int)theString.length(); ++i)
		theString[i] = toupper(theString[i]);
}

UTF16String Beefy::ToUpper(const UTF16String& theString)
{
	UTF16String aString = theString;
	for (int i = 0; i < (int)aString.length(); ++i)
		aString[i] = toupper(aString[i]);
	return aString;
}

UTF16String Beefy::ToLower(const UTF16String& theString)
{
	UTF16String aString = theString;
	for (int i = 0; i < (int)aString.length(); ++i)
		aString[i] = tolower(aString[i]);
	return aString;
}

String Beefy::ToLower(const StringImpl& theString)
{
	String aString = theString;
	for (int i = 0; i < (int)aString.length(); ++i)
		aString[i] = (char)tolower((int)(uint8)aString[i]);
	return aString;
}

// UTF16String Beefy::Trim(const UTF16String& theString)
// {
// 	int left = 0;
// 	while ((left < (int) theString.length() - 1) && (iswspace(theString[left])))
// 		left++;
//
// 	int right = (int)theString.length() - 1;
// 	while ((right >= left) && (iswspace(theString[right])))
// 		right--;
//
// 	if ((left == 0) && (right == theString.length() - 1))
// 		return theString;
//
// 	return theString.substr(left, right - left + 1);
// }

String Beefy::Trim(const StringImpl& theString)
{
	int left = 0;
	while ((left < (int) theString.length() - 1) && (iswspace(theString[left])))
		left++;

	int right = (int)theString.length() - 1;
	while ((right >= left) && (iswspace(theString[right])))
		right--;

	if ((left == 0) && (right == theString.length() - 1))
		return theString;

	return theString.Substring(left, right - left + 1);
}

bool Beefy::StrReplace(StringImpl& str, const StringImpl& from, const StringImpl& to)
{
	str.Replace(from, to);
	return true;
}

bool Beefy::StrStartsWith(const StringImpl& str, const StringImpl& subStr)
{
	if (subStr.length() < str.length())
		return false;

	return strncmp(str.c_str(), subStr.c_str(), subStr.length()) == 0;
}

bool Beefy::StrEndsWith(const StringImpl& str, const StringImpl& subStr)
{
	if (subStr.length() < str.length())
		return false;

	return strncmp(str.c_str() - (str.length() - subStr.length()), subStr.c_str(), subStr.length()) == 0;
}

#ifndef BF_SMALL
String Beefy::SlashString(const StringImpl& str, bool utf8decode, bool utf8encode, bool beefString)
{
	bool prevEndedInSlashedNum = false;
	String outStr;

	bool noNextHex = false;

	bool lastWasVisibleChar = false;
	for (int idx = 0; idx < (int)str.length(); idx++)
	{
		bool endingInSlashedNum = false;
		char c = str[idx];

		if (noNextHex)
		{
			if (((c >= '0') && (c <= '9')) ||
				((c >= 'a') && (c <= 'f')) ||
				((c >= 'A') && (c <= 'F')))
			{
				outStr += "\"\"";
			}
			noNextHex = false;
		}

		bool isVisibleChar = false;

		switch (c)
		{
		case '\0':
			outStr += "\\0";
			endingInSlashedNum = true;
			break;
		case '\a':
			outStr += "\\a";
			break;
		case '\b':
			outStr += "\\b";
			break;
		case '\t':
			outStr += "\\t";
			break;
		case '\n':
			outStr += "\\n";
			break;
		case '\v':
			outStr += "\\v";
			break;
		case '\f':
			outStr += "\\f";
			break;
		case '\r':
			outStr += "\\r";
			break;
		case '\"':
			outStr += "\\\"";
			break;
		case '\\':
			outStr += "\\\\";
			break;
		default:
		{
			if ((c >= '0') && (c <= '9'))
			{
				// Need to break string to allow proper evaluation of slashed string
				if (prevEndedInSlashedNum)
					outStr += "\" \"";
			}

			// May be UTF8 byte.  Make sure it's valid before we add it, otherwise write the bytes in direct "\x" style
			if (((uint8)c >= 0x80) && (utf8decode))
			{
				int seqLen = u8_seqlen((char*)&str[idx]);
				if (seqLen > 1)
				{
					if (idx + seqLen <= str.length())
					{
						uint32 c32 = u8_toucs((char*)&str[idx], (int)str.length() - idx);
						bool wantHex = false;

						// Combining mark without previous visible char?
						if ((((c32 >= L'\u0300') && (c32 <= L'\u036F')) || ((c32 >= L'\u1DC0') && (c32 <= L'\u1DFF'))) &&
							(!lastWasVisibleChar))
							wantHex = true;

						utf8proc_category_t cat = utf8proc_category(c32);
						switch (cat)
						{
						case UTF8PROC_CATEGORY_ZS:
						case UTF8PROC_CATEGORY_ZL:
						case UTF8PROC_CATEGORY_ZP:
						case UTF8PROC_CATEGORY_CC:
						case UTF8PROC_CATEGORY_CF:
						case UTF8PROC_CATEGORY_CO:
							wantHex = true;
							break;
						default:
							break;
						}

						if (wantHex)
						{
							if (beefString)
							{
								char tempStr[32];
								sprintf(tempStr, "\\u{%x}", c32);
								outStr += tempStr;
								idx += seqLen - 1;
								continue;
							}
							else
							{
								char tempStr[32];
								sprintf(tempStr, "\\u%04x", c32);
								outStr += tempStr;
								idx += seqLen - 1;
								noNextHex = true;
								continue;
							}
						}

						bool passed = true;
						for (int checkOfs = 0; checkOfs < seqLen - 1; checkOfs++)
						{
							char checkC = str[idx + checkOfs + 1];
							if ((checkC & 0xC0) != 0x80) // Has trailing signature (0x10xxxxxx)?
								passed = false;
						}

						if (passed)
						{
							outStr += c;
							for (int checkOfs = 0; checkOfs < seqLen - 1; checkOfs++)
							{
								char checkC = str[idx + checkOfs + 1];
								outStr += checkC;
							}
							idx += seqLen - 1;
							continue;
						}
					}
				}
			}

			if (((uint8)c >= 0x80) && (utf8encode))
			{
				outStr += (char)(0xC0 | (((uint8)c & 0xFF) >> 6));
				outStr += (char)(0x80 | ((uint8)c & 0x3F));
				isVisibleChar = true;
				continue;
			}

			if (((uint8)c >= 32) && ((uint8)c < 0x80))
			{
				outStr += c;
				isVisibleChar = true;
			}
			else
			{
				char tempStr[32];
				sprintf(tempStr, "\\x%02x", (uint8)c);
				outStr += tempStr;
				endingInSlashedNum = true;
				if (!beefString)
					noNextHex = true;
			}
		}
		break;
		}

		prevEndedInSlashedNum = endingInSlashedNum;
		lastWasVisibleChar = isVisibleChar;
	}
	return outStr;
}
#endif

UTF16String Beefy::UTF8Decode(const StringImpl& theString)
{
	UTF16String strOut;

	int strLen = 0;
	char* cPtr = (char*)theString.c_str();
	int lenLeft = (int)theString.length();
	while (lenLeft > 0)
	{
		int seqLen = 0;
		uint32 c32 = u8_toucs(cPtr, lenLeft, &seqLen);
		if ((c32 >= 0x10000) && (sizeof(wchar_t) == 2))
			strLen += 2;
		else
			strLen += 1;
		cPtr += seqLen;
		lenLeft -= seqLen;
	}

	strOut.ResizeRaw(strLen + 1);
	strOut[strLen] = 0;

	cPtr = (char*)theString.c_str();
	uint16* wcPtr = strOut.mVals;
	int wcLenLeft = (int)strOut.length() + 1;
	lenLeft = (int)theString.length();
	while (lenLeft > 0)
	{
		int seqLen = 0;
		uint32 c32 = u8_toucs(cPtr, lenLeft, &seqLen);
		if ((c32 >= 0x10000) && (sizeof(wchar_t) == 2))
		{
			*(wcPtr++) = (wchar_t)(((c32 - 0x10000) >> 10) + 0xD800);
			*(wcPtr++) = (wchar_t)(((c32 - 0x10000) & 0x3FF) + 0xDC00);
			wcLenLeft -= 2;
		}
		else
		{
			*(wcPtr++) = (wchar_t)c32;
			wcLenLeft -= 1;
		}
		cPtr += seqLen;
		lenLeft -= seqLen;
	}

	return strOut;
}

String Beefy::UTF8Encode(const uint16* theString, int length)
{
	if (length == 0)
		return String();

	String strOut;
	int utf8Len = 0;
	uint16 utf16hi = 0;
	for (int i = 0; i < length; i++)
	{
		uint16 c = theString[i];
		uint32 c32 = c;
		if ((c >= 0xD800) && (c < 0xDC00))
		{
			utf16hi = (uint16)c;
			continue;
		}
		else if ((c >= 0xDC00) && (c < 0xE000))
		{
			uint16 utf16lo = c;
			c32 = 0x10000 + ((uint32)(utf16hi - 0xD800) << 10) | (uint32)(utf16lo - 0xDC00);
		}

		utf8Len += u8_seqlen(c32);
	}
	strOut.Append('?', utf8Len);
	char* cPtr = (char*)&strOut[0];
	int lenLeft = (int)strOut.length() + 1;
	for (int i = 0; i < length; i++)
	{
		uint16 c = theString[i];
		uint32 c32 = c;
		if ((c >= 0xD800) && (c < 0xDC00))
		{
			utf16hi = (uint16)c;
			continue;
		}
		else if ((c >= 0xDC00) && (c < 0xE000))
		{
			uint16 utf16lo = c;
			c32 = 0x10000 + ((uint32)(utf16hi - 0xD800) << 10) | (uint32)(utf16lo - 0xDC00);
		}

		int len = u8_toutf8(cPtr, lenLeft, c32);
		cPtr += len;
		lenLeft -= len;
	}

	return strOut;
}

String Beefy::UTF8Encode(const UTF16String& theString)
{
	return UTF8Encode((uint16*)theString.c_str(), (int)theString.length());
}

UTF16String Beefy::UTF16Decode(const uint16* theString)
{
	if (sizeof(wchar_t) == 2)
	{
		UTF16String str;
		str.Set((wchar_t*)theString);
		return str;
	}
	else
	{
		UTF16String str;

		int len = 0;
		while (theString[len] != 0)
			len++;

		str.ResizeRaw(len + 1);
		str[len] = 0;
		for (int pos = 0; pos < len; pos++)
			str[pos] = (wchar_t)theString[pos];
		return str;
	}
}

static const char gHexChar[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };
String Beefy::FileNameToURI(const StringImpl& fileName)
{
#ifdef _WIN32
	String out = "file:///";
#else
	String out = "file://";
#endif
	for (int i = 0; i < (int)fileName.length(); i++)
	{
		char c = out[i];

		bool isValid =
			((c >= '@' && c <= 'Z') ||
			 (c >= 'a' && c <= 'z') ||
			 (c >= '&' && c < 0x3b) ||
			 (c == '!') || (c == '$') || (c == '_') || (c == '=') || (c == '~'));

		if ((((unsigned char)c) >= 0x80) || (!isValid))
		{
			out += '%';
			out += gHexChar[((unsigned char)(c)) >> 4];
			out += gHexChar[((unsigned char)(c)) & 0xf];
		}
		else if (c == '\\')
			out += '/';
		else
			out += c;
	}
	return out;
}

static const uint8 cCharTo64b[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 62, 0, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0, 0, 0, 0, 0,
	0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 0, 0, 0, 0, 63,
	0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
int64 Beefy::DecodeULEB32(const char*& p)
{
	uint64 val = 0;
	unsigned shift = 0;
	uint8 charVal;
	do
	{
		charVal = cCharTo64b[(uint8)*(p++)];
		val += uint64(charVal & 0x1f) << shift;
		shift += 5;
	} while ((charVal & 0x20) != 0);
	return val;
}

static const char c64bToChar[] = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o', 'p',
	'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 'A', 'B', 'C', 'D', 'E', 'F',
	'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V',
	'W', 'X', 'Y', 'Z', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '.', '_'};
void Beefy::EncodeULEB32(uint64 value, StringImpl& buffer)
{
	do
	{
		uint8 byteVal = value & 0x1f;
		value >>= 5;
		if (value != 0)
			byteVal |= 0x20; // Mark this byte to show that more bytes will follow
		buffer.Append(c64bToChar[byteVal]);
	}
	while (value != 0);
}

#ifndef BF_SMALL
void Beefy::ExactMinimalFloatToStr(float f, char* str)
{
	sprintf(str, "%1.9g", f);

	char* cPtrLast = str;
	while (true)
	{
		char c = cPtrLast[1];
		if ((c == 0) || (c == 'e'))
			break;
		cPtrLast++;
	}
	char* cStrEnd = cPtrLast + 1;
	int baseLen = (int)(cPtrLast - str) + 1;
	if (baseLen < 9)
		return; // Isn't "full" precision

	char removeChar;
	char lastChar = *cPtrLast;
	char secondToLast = cPtrLast[-1];
	if (secondToLast == '9')
	{
		removeChar = '9';
	}
	else if (secondToLast == '0')
	{
		removeChar = '0';
	}
	else
	{
		return; // Not a 'transitional' representation
	}

	do
	{
		*cPtrLast = 0;
		cPtrLast--;
	} while (*cPtrLast == removeChar);
	strcpy(cPtrLast + 1, cStrEnd);

	if (removeChar == '9')
		(*cPtrLast)++;

	// Verify that our pretty representation is equivalent
	float checkF = 0;
	sscanf(str, "%f", &checkF);
	if (f == checkF)
		return;

	// Rollback
	int endLen = (int)strlen(cPtrLast + 1);
	memmove(cStrEnd, cPtrLast + 1, endLen);

	if (removeChar == '9')
		(*cPtrLast)--;

	cPtrLast++;
	while (cPtrLast < cStrEnd - 1)
	{
		*cPtrLast = removeChar;
		cPtrLast++;
	}
	*cPtrLast = lastChar;
}

void Beefy::ExactMinimalDoubleToStr(double d, char* str)
{
	sprintf(str, "%1.17g", d);

	char* cPtrLast = str;
	while (true)
	{
		char c = cPtrLast[1];
		if ((c == 0) || (c == 'e'))
			break;
		cPtrLast++;
	}
	char* cStrEnd = cPtrLast + 1;
	int baseLen = (int)(cPtrLast - str) + 1;
	if (baseLen < 18)
		return; // Isn't "full" precision

	char removeChar;
	char lastChar = *cPtrLast;
	if ((lastChar == '9') || (lastChar == '8'))
	{
		removeChar = '9';
	}
	else if ((lastChar == '1') || (lastChar == '2'))
	{
		removeChar = '0';
	}
	else
	{
		return; // Not a 'transitional' representation
	}

	do
	{
		*cPtrLast = 0;
		cPtrLast--;
	}
	while (*cPtrLast == removeChar);
	strcpy(cPtrLast + 1, cStrEnd);

	if (removeChar == '9')
		(*cPtrLast)++;

	// Verify that our pretty representation is equivalent
	double checkD = 0;
	sscanf(str, "%lf", &checkD);
	if (d == checkD)
		return;

	// Rollback
	int endLen = (int)strlen(cPtrLast + 1);
	memmove(cStrEnd, cPtrLast + 1, endLen);

	if (removeChar == '9')
		(*cPtrLast)--;

	cPtrLast++;
	while (cPtrLast < cStrEnd - 1)
	{
		*cPtrLast = removeChar;
		cPtrLast++;
	}
	*cPtrLast = lastChar;
}
#endif

static char* StbspCallback(char *buf, void *user, int len)
{
    ((String*)user)->Append(buf, len);
    return buf;
}

//extern "C" int _vsnprintf(char* const _Buffer, size_t const _BufferCount, char const* const _Format, va_list _ArgList, int zz);

#ifdef BF_SMALL
String Beefy::vformat(const char* fmt, va_list argPtr)
{
	// We draw the line at a 1MB string.
	const int maxSize = 1000000;

	//va_list checkArgPtr = argPtr;
	//va_start(checkArgPtr, fmt);
	int argIdx = 0;
	char* newFmt = NULL;
	char tempBuff[2048];
	char* tempBuffPtr = tempBuff;
	for (int i = 0; fmt[i] != 0; i++)
	{
		if (fmt[i] == '%')
		{

			if (fmt[i + 1] == '%')
			{
				i++;
				continue;
			}

#ifdef BF32
			bool isLongAddr = false;
#else
			bool isLongAddr = true;
#endif
			if ((fmt[i + 1] == 'l') && (fmt[i + 2] == '@'))
			{
				isLongAddr = true;
				i++;
			}

			if (fmt[i + 1] == '@')
			{
				if (newFmt == NULL)
				{
					newFmt = (char*)malloc(strlen(fmt) + 1);
					strcpy(newFmt, fmt);
					//newFmt = strdup(fmt);
				}
				newFmt[i + 1] = 's';

				const char*& argValPtr = *(const char**)((intptr*)argPtr + argIdx);

				if (isLongAddr)
				{
					int64 iVal = *(int64*)((intptr*)argPtr + argIdx);
#ifdef BF32
					argIdx++; // Takes two spots
#endif
					argValPtr = tempBuffPtr;

					int leftVal = (int)(iVal >> 32);
					if (leftVal != 0)
					{
						sprintf(tempBuffPtr, "%x", leftVal);
						tempBuffPtr += strlen(tempBuffPtr);
					}
					tempBuffPtr[0] = '\'';
					tempBuffPtr++;
					sprintf(tempBuffPtr, "%08x", (int)iVal);
					tempBuffPtr += strlen(tempBuffPtr) + 1;
				}
				else
				{
					int32 iVal = (int32)(intptr)argValPtr;
					argValPtr = tempBuffPtr;
					sprintf(tempBuffPtr, "%08x", iVal);
					tempBuffPtr += strlen(tempBuffPtr) + 1;
				}

				if (newFmt[i] == 'l')
					newFmt[i] = '+';
			}
			else
			{
				//const char*& argValPtr = va_arg(checkArgPtr, const char*);
			}

			argIdx++;
		}
	}
	if (newFmt != NULL)
	{
		Beefy::String retVal = vformat(newFmt, argPtr);
		free(newFmt);
		return retVal;
	}

	// If the string is less than 161 characters,
	// allocate it on the stack because this saves
	// the malloc/free time.
	const int bufSize = 161;
	char stackBuffer[bufSize];

	int attemptedSize = bufSize - 1;

	int numChars = 0;

#ifdef _WIN32
	numChars = _vsnprintf(stackBuffer, attemptedSize, fmt, argPtr);
#else
	numChars = vsnprintf(stackBuffer, attemptedSize, fmt, argPtr);
#endif

	if ((numChars >= 0) && (numChars <= attemptedSize))
	{
		// Needed for case of 160-character printf thing
		stackBuffer[numChars] = '\0';

		// Got it on the first try.
		return String(stackBuffer);
	}

	// Now use the heap.
	char* heapBuffer = NULL;

	while (((numChars == -1) || (numChars > attemptedSize)) &&
		(attemptedSize < maxSize))
	{
		// Try a bigger size
		attemptedSize *= 2;
		heapBuffer = (char*)realloc(heapBuffer, (attemptedSize + 1));

#ifdef _WIN32
		numChars = _vsnprintf(heapBuffer, attemptedSize, fmt, argPtr);
#else
		numChars = vsnprintf(heapBuffer, attemptedSize, fmt, argPtr);
#endif

	}

	if (numChars == -1)
	{
		free(heapBuffer);
		return "";
	}

	heapBuffer[numChars] = 0;

	Beefy::String aResult = Beefy::String(heapBuffer);

	free(heapBuffer);
	return aResult;
}
#else
String Beefy::vformat(const char* fmt, va_list argPtr)
{
    String str;
    char buf[STB_SPRINTF_MIN];
    BF_stbsp_vsprintfcb(StbspCallback, (void*)&str, buf, fmt, argPtr);
    return str;
}
#endif

String Beefy::StrFormat(const char* fmt ...)
{
    va_list argList;
    va_start(argList, fmt);
	String aResult = vformat(fmt, argList);
    va_end(argList);

    return aResult;
}

String Beefy::IntPtrDynAddrFormat(intptr addr)
{
	if ((addr & 0xFFFFFFFF00000000LL) == 0)
		return StrFormat("%08X", addr);
	return StrFormat("%p", addr);
}

void Beefy::OutputDebugStr(const StringImpl& theString)
{
	const int maxLen = 0xFFFE;
	if (theString.length() > maxLen)
	{
		OutputDebugStr(theString.Substring(0, maxLen));
		OutputDebugStr(theString.Substring(maxLen));
		return;
	}
    BfpOutput_DebugString(theString.c_str());
}

void Beefy::OutputDebugStrF(const char* fmt ...)
{
	va_list argList;
	va_start(argList, fmt);
	String aResult = vformat(fmt, argList);
	va_end(argList);

    OutputDebugStr(aResult);
}

uint8* Beefy::LoadBinaryData(const StringImpl& path, int* size)
{
	FILE* fP = fopen(path.c_str(), "rb");
	if (fP == NULL)
		return NULL;

	fseek(fP, 0, SEEK_END);
	int aSize = (int32)ftell(fP);
	fseek(fP, 0, SEEK_SET);

	uint8* data = new uint8[aSize];
	int readSize = (int)fread(data, 1, aSize, fP);
	(void)readSize;
	fclose(fP);
	if (size)
		*size = aSize;
	return data;
}

char* Beefy::LoadTextData(const StringImpl& path, int* size)
{
	FILE* fP = fopen(path.c_str(), "rb");
	if (fP == NULL)
		return NULL;

	fseek(fP, 0, SEEK_END);
	int fileSize = (int32)ftell(fP);
	int strLen = fileSize;
	fseek(fP, 0, SEEK_SET);

	uint8 charHeader[3] = {0};
	int readSize = (int)fread(charHeader, 1, 3, fP);

	if ((charHeader[0] == 0xFF) && (charHeader[1] == 0xFE))
	{
		//UTF16 LE

		int dataLen = fileSize - 2;
		char* data = new char[dataLen + 2];
		data[0] = (char)charHeader[2];
		data[dataLen] = 0;
		data[dataLen + 1] = 0;
		int readSize = (int)fread(data + 1, 1, dataLen - 1, fP);
		(void)readSize;
		fclose(fP);

		// UTF16
		UTF16String str;
		str.Set((wchar_t*)data);
		delete [] data;

		String utf8Str = UTF8Encode(str);

		char* utf8Data = new char[utf8Str.length() + 1];
		strLen = (int)utf8Str.length();
		if (size != NULL)
			*size = strLen;
		memcpy(utf8Data, utf8Str.c_str(), strLen);
		return utf8Data;
	}
	else if ((charHeader[0] == 0xEF) && (charHeader[1] == 0xBB) && (charHeader[2] == 0xBF))
	{
		strLen = fileSize - 3;
		char* data = new char[strLen + 1];
		data[strLen] = 0;
		if (size != NULL)
			*size = strLen;
		int readSize = (int)fread(data, 1, strLen, fP);
		(void)readSize;
		fclose(fP);
		return data;
	}

	if (size != NULL)
		*size = strLen;
	char* data = new char[strLen + 1];
	data[strLen] = 0;
	for (int i = 0; i < BF_MIN(3, strLen); i++)
		data[i] = charHeader[i];
	if (strLen > 3)
	{
		int readSize = (int)fread(data + 3, 1, strLen - 3, fP);
		(void)readSize;
	}
	fclose(fP);
	return data;
}

bool Beefy::LoadTextData(const StringImpl& path, StringImpl& str)
{
	int size = 0;
	char* data = LoadTextData(path, &size);
	if (data == NULL)
		return false;
	if ((str.mAllocSizeAndFlags & StringImpl::DynAllocFlag) != 0)
		str.Release();
	str.mPtr = data;
	str.mAllocSizeAndFlags = size | StringImpl::DynAllocFlag | StringImpl::StrPtrFlag;
	str.mLength = size;
	return true;
}


#ifdef BF_MINGW
unsigned long long __cdecl _byteswap_uint64(unsigned long long _Int64)
{
#ifdef _WIN64
  unsigned long long retval;
  __asm__ __volatile__ ("bswapq %[retval]" : [retval] "=rm" (retval) : "[retval]" (_Int64));
  return retval;
#else
  union {
    long long int64part;
    struct {
      unsigned long lowpart;
      unsigned long hipart;
    };
  } retval;
  retval.int64part = _Int64;
  __asm__ __volatile__ ("bswapl %[lowpart]\n"
    "bswapl %[hipart]\n"
    : [lowpart] "=rm" (retval.hipart), [hipart] "=rm" (retval.lowpart)  : "[lowpart]" (retval.lowpart), "[hipart]" (retval.hipart));
  return retval.int64part;
#endif
}
#endif

#ifdef _WIN32
int64 Beefy::EndianSwap(int64 val)
{
	return _byteswap_uint64(val);
}
#endif

int32 Beefy::EndianSwap(int val)
{
	return ((val & 0x000000FF) << 24) | ((val & 0x0000FF00) <<  8) |
		((val & 0x00FF0000) >>  8) | ((val & 0xFF000000) >> 24);
}

int16 Beefy::EndianSwap(int16 val)
{
	return ((val & 0x00FF) << 8) | ((val & 0xFF00) >> 8);
}

int32 Beefy::Rand()
{
	return (rand() & 0xFFFF) | ((rand() & 0x7FFF) << 16);
}

int32 Beefy::GetHighestBitSet(int32 n)
{
	int i = 0;
	for (; n; n = (int)((uint32)n >> 1), i++)
		; /* empty */
	return i;
}

String Beefy::GetFileDir(const StringImpl& path)
{
	int slashPos = BF_MAX((int)path.LastIndexOf('\\'), (int)path.LastIndexOf('/'));
	if (slashPos == -1)
		return "";
	return path.Substring(0, slashPos);
}

String Beefy::GetFileName(const StringImpl& path)
{
	int slashPos = BF_MAX((int)path.LastIndexOf('\\'), (int)path.LastIndexOf('/'));
	if (slashPos == -1)
		return path;
	return path.Substring(slashPos + 1);
}

String Beefy::GetFileExtension(const StringImpl& path)
{
	int dotPos = (int)path.LastIndexOf('.');
	if (dotPos == -1)
		return path;
	return path.Substring(dotPos);
}

static String GetDriveStringTo(String path)
{
	if ((path.length() >= 2) && (path[1] == ':'))
		return String(path, 0, 2);
	return "";
}

String Beefy::GetRelativePath(const StringImpl& fullPath, const StringImpl& curDir)
{
	String curPath1 = String(curDir);
	String curPath2 = String(fullPath);

	for (int i = 0; i < (int)curPath1.length(); i++)
		if (curPath1[i] == DIR_SEP_CHAR_ALT)
			curPath1[i] = DIR_SEP_CHAR;

	for (int i = 0; i < (int)curPath2.length(); i++)
		if (curPath2[i] == DIR_SEP_CHAR_ALT)
			curPath2[i] = DIR_SEP_CHAR;

	String driveString1 = GetDriveStringTo(curPath1);
	String driveString2 = GetDriveStringTo(curPath2);

#ifdef _WIN32
	StringImpl::CompareKind compareType = StringImpl::CompareKind_OrdinalIgnoreCase;
#else
	StringImpl::CompareKind compareType = StringImpl::CompareKind_Ordinal;
#endif

	// On separate drives?
	if (!driveString1.Equals(driveString2, compareType))
	{
		return fullPath;
	}

	if (driveString1.mLength > 0)
		curPath1.Remove(0, BF_MIN(driveString1.mLength + 1, curPath1.mLength));
	if (driveString2.mLength > 0)
		curPath2.Remove(0, BF_MIN(driveString2.mLength + 1, curPath2.mLength));

	while ((curPath1.mLength > 0) && (curPath2.mLength > 0))
	{
		int slashPos1 = (int)curPath1.IndexOf(DIR_SEP_CHAR);
		if (slashPos1 == -1)
			slashPos1 = curPath1.mLength;
		int slashPos2 = (int)curPath2.IndexOf(DIR_SEP_CHAR);
		if (slashPos2 == -1)
			slashPos2 = curPath2.mLength;

		String section1;
		section1.Append(StringView(curPath1, 0, slashPos1));
		String section2;
		section2.Append(StringView(curPath2, 0, slashPos2));

		if (!section1.Equals(section2, compareType))
		{
			// a/b/c
			// d/e/f

			while (curPath1.mLength > 0)
			{
				slashPos1 = (int)curPath1.IndexOf(DIR_SEP_CHAR);
				if (slashPos1 == -1)
					slashPos1 = curPath1.mLength;

				if (slashPos1 + 1 >= curPath1.mLength)
					curPath1.Clear();
				else
					curPath1.Remove(0, slashPos1 + 1);
				if (DIR_SEP_CHAR == '\\')
					curPath2.Insert(0, "..\\");
				else
					curPath2.Insert(0, "../");
			}
		}
		else
		{
			if (slashPos1 + 1 >= curPath1.mLength)
				curPath1.Clear();
			else
				curPath1.Remove(0, slashPos1 + 1);

			if (slashPos2 + 2 >= curPath2.mLength)
				curPath1 = "";
			else
				curPath2.Remove(0, slashPos2 + 1);
		}
	}

	return curPath2;
}

String Beefy::GetAbsPath(const StringImpl& relPathIn, const StringImpl& dir)
{
	String relPath = relPathIn;

	String driveString = "";
	String newPath;
	newPath = dir;
	for (int i = 0; i < (int)newPath.length(); i++)
		if (newPath[i] == DIR_SEP_CHAR_ALT)
			newPath[i] = DIR_SEP_CHAR;

	for (int i = 0; i < (int)relPath.length(); i++)
		if (relPath[i] == DIR_SEP_CHAR_ALT)
			relPath[i] = DIR_SEP_CHAR;

	if ((relPath.length() >= 2) && (relPath[1] == ':'))
		return relPath;

	char slashChar = DIR_SEP_CHAR;

	if ((newPath.length() >= 2) && (newPath[1] == ':'))
	{
		driveString = newPath.Substring(0, 2);
		newPath = newPath.Substring(2);
	}

	// Append a trailing slash if necessary
	if ((newPath.length() > 0) && (newPath[newPath.length() - 1] != '\\') && (newPath[newPath.length() - 1] != '/'))
		newPath += slashChar;

	int relIdx = 0;

	for (;;)
	{
		if (newPath.length() == 0)
			break;

		int firstSlash = -1;
		for (int32 i = relIdx; i < (int)relPath.length(); i++)
			if ((relPath[i] == '\\') || (relPath[i] == '/'))
			{
				firstSlash = i;
				break;
			}
		if (firstSlash == -1)
			break;

		String chDir = relPath.Substring(relIdx, firstSlash - relIdx);

		relIdx = firstSlash + 1;

		if (chDir == "..")
		{
			int lastDirStart = (int)newPath.length() - 1;
			while ((lastDirStart > 0) && (newPath[lastDirStart - 1] != '\\') && (newPath[lastDirStart - 1] != '/'))
				lastDirStart--;

			String lastDir = newPath.Substring(lastDirStart, newPath.length() - lastDirStart - 1);
			if (lastDir == "..")
			{
				newPath += "..";
				newPath += DIR_SEP_CHAR;
			}
			else
			{
				newPath.RemoveToEnd(lastDirStart);
			}
		}
		else if (chDir == "")
		{
			newPath = DIR_SEP_CHAR;
			break;
		}
		else if (chDir != ".")
		{
			newPath += chDir + slashChar;
			break;
		}
	}

	//newPath = driveString + newPath + tempRelPath;
	newPath = driveString + newPath;
	newPath += relPath.Substring(relIdx);

	return newPath;
}

String Beefy::FixPath(const StringImpl& pathIn)
{
	String path = pathIn;

	for (int i = 0; i < (int)path.length(); i++)
	{
		if (path[i] == DIR_SEP_CHAR_ALT)
			path[i] = DIR_SEP_CHAR;
		if ((i > 0) && (path[i - 1] == '.') && (path[i] == '.'))
		{
			for (int checkIdx = i - 3; checkIdx >= 0; checkIdx--)
			{
				if ((path[checkIdx] == '\\') || (path[checkIdx] == '/'))
				{
					path = path.Substring(0, checkIdx) + path.Substring(i + 1);
					i = checkIdx;
					break;
				}
			}
		}
	}

	return path;
}

String Beefy::FixPathAndCase(const StringImpl& pathIn)
{
	if ((!pathIn.IsEmpty()) && (pathIn[0] == '$'))
		return pathIn;

	String path = FixPath(pathIn);
#ifdef _WIN32
	for (int i = 0; i < (int)path.length(); ++i)
		path[i] = toupper(path[i]);
#endif
	return path;
}

String Beefy::EnsureEndsInSlash(const StringImpl& dir)
{
	if ((dir[dir.length() - 1] != '/') && (dir[dir.length() - 1] != '\\'))
		return dir + "/";
	return dir;
}

String Beefy::RemoveTrailingSlash(const StringImpl& str)
{
	if (str.length() != 0)
	{
		char c = str[str.length() - 1];
		if ((c == '\\') || (c == '/'))
			return str.Substring(0, str.length() - 1);
	}
	return str;
}

bool Beefy::FileNameEquals(const StringImpl& filePathA, const StringImpl& filePathB)
{
#ifdef _WIN32
	if (filePathA.length() != filePathB.length())
		return false;

	const char* aPtr = filePathA.c_str();
	const char* bPtr = filePathB.c_str();
	while (true)
	{
		char a = *(aPtr++);
		char b = *(bPtr++);

		if (a == 0)
			return true;
		if (a == b)
			continue;
		if (a == '/')
			a = '\\';
		if (b == '/')
			b = '\\';
		if (a == b)
			continue;
		if (::toupper(a) == ::toupper(b))
			continue;
		return false;
	}
#else
	return strcmp(filePathA.c_str(), filePathB.c_str()) == 0;
#endif
}

bool Beefy::RecursiveCreateDirectory(const StringImpl& dirName)
{
	int slashPos = -1;
	for (int i = (int)dirName.length() - 1; i >= 0; i--)
	{
		char c = dirName[i];
		if ((c == '\\') || (c == '/'))
		{
			slashPos = i;
			break;
		}
	}

	if (slashPos != -1)
	{
		RecursiveCreateDirectory(dirName.Substring(0, slashPos));
	}

	BfpFileResult result;
    BfpDirectory_Create(dirName.c_str(), &result);
	return result == BfpFileResult_Ok;
}

bool Beefy::RecursiveDeleteDirectory(const StringImpl& dirPath)
{
	String findSpec = dirPath + "/*.*";
	bool failed = false;

	BfpFileResult result;
	BfpFindFileData* findFileData = BfpFindFileData_FindFirstFile(findSpec.c_str(), (BfpFindFileFlags)(BfpFindFileFlag_Directories | BfpFindFileFlag_Files), &result);
	if (result == BfpFileResult_Ok)
	{
		while (true)
		{
			Beefy::String fileName;
			BFP_GETSTR_HELPER(fileName, result, BfpFindFileData_GetFileName(findFileData, __STR, __STRLEN, &result));

			String filePath = dirPath + "/" + fileName;
			if ((BfpFindFileData_GetFileAttributes(findFileData) & BfpFileAttribute_Directory) != 0)
			{
				if (!RecursiveDeleteDirectory(filePath))
					failed = true;
			}
			else
			{
				BfpFile_Delete(filePath.c_str(), &result);
				if (result != BfpFileResult_Ok)
					failed = true;
			}

			if (!BfpFindFileData_FindNextFile(findFileData))
				break;
		}
		BfpFindFileData_Release(findFileData);
	}
	else if (result != BfpFileResult_NoResults)
	{
		return false;
	}

	BfpDirectory_Delete(dirPath.c_str(), &result);
	return (result == BfpFileResult_Ok) && (!failed);
}

void Beefy::BFFatalError(const char* message, const char* file, int line)
{
	BFFatalError(String(message), String(file), line);
}

void MakeUpper(const StringImpl& theString)
{
}
