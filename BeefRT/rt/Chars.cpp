#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/TLSingleton.h"
#include "BfObjects.h"

extern "C"
{
#include "BeefySysLib/third_party/utf8proc/utf8proc.h"
}

namespace bf
{
	namespace System
	{
		struct Char32
		{
		private:
			BFRT_EXPORT static bool get__IsWhiteSpace_EX__im(char32_t c);

		public:
			BFRT_EXPORT static char32_t get__ToLower__im(char32_t c);
			BFRT_EXPORT static char32_t get__ToUpper__im(char32_t c);
			BFRT_EXPORT static bool get__IsLower__im(char32_t c);
			BFRT_EXPORT static bool get__IsUpper__im(char32_t c);
			BFRT_EXPORT static bool get__IsLetterOrDigit__im(char32_t c);
			BFRT_EXPORT static bool get__IsLetter__im(char32_t c);
			BFRT_EXPORT static bool get__IsNumber__im(char32_t c);
		};

		struct Char16
		{
		public:
			BFRT_EXPORT static char16_t get__ToLower__im(char16_t c);
			BFRT_EXPORT static char16_t get__ToUpper__im(char16_t c);
			BFRT_EXPORT static bool get__IsLower__im(char16_t c);
			BFRT_EXPORT static bool get__IsUpper__im(char16_t c);
			BFRT_EXPORT static bool get__IsWhiteSpace__im(char16_t c);
			BFRT_EXPORT static bool get__IsLetterOrDigit__im(char16_t c);
			BFRT_EXPORT static bool get__IsLetter__im(char16_t c);
			BFRT_EXPORT static bool get__IsNumber__im(char16_t c);
		};
	}
}

char32_t bf::System::Char32::get__ToLower__im(char32_t c)
{
	return utf8proc_tolower(c);
}

char32_t bf::System::Char32::get__ToUpper__im(char32_t c)
{
	return utf8proc_toupper(c);
}

bool bf::System::Char32::get__IsLower__im(char32_t c)
{
	return utf8proc_category(c) == UTF8PROC_CATEGORY_LL;
}

bool bf::System::Char32::get__IsUpper__im(char32_t c)
{
	return utf8proc_category(c) == UTF8PROC_CATEGORY_LU;
}

bool bf::System::Char32::get__IsWhiteSpace_EX__im(char32_t c)
{
	auto cat = utf8proc_category(c);
	return (cat == UTF8PROC_CATEGORY_ZS) || (cat == UTF8PROC_CATEGORY_ZL) || (cat == UTF8PROC_CATEGORY_ZP);
}

bool bf::System::Char32::get__IsLetterOrDigit__im(char32_t c)
{
	auto cat = utf8proc_category(c);
	switch (cat)
	{
	case UTF8PROC_CATEGORY_LU:
	case UTF8PROC_CATEGORY_LL:
	case UTF8PROC_CATEGORY_LT:
	case UTF8PROC_CATEGORY_LM:
	case UTF8PROC_CATEGORY_LO:
	case UTF8PROC_CATEGORY_ND:
	case UTF8PROC_CATEGORY_NL:
	case UTF8PROC_CATEGORY_NO: return true;
	default: break;
	}
	return false;
}

bool bf::System::Char32::get__IsLetter__im(char32_t c)
{
	auto cat = utf8proc_category(c);
	switch (cat)
	{
	case UTF8PROC_CATEGORY_LU:
	case UTF8PROC_CATEGORY_LL:
	case UTF8PROC_CATEGORY_LT:
	case UTF8PROC_CATEGORY_LM:
	case UTF8PROC_CATEGORY_LO: return true;
	default: break;
	}
	return false;
}

bool bf::System::Char32::get__IsNumber__im(char32_t c)
{
	auto cat = utf8proc_category(c);
	switch (cat)
	{
	case UTF8PROC_CATEGORY_ND:
	case UTF8PROC_CATEGORY_NL:
	case UTF8PROC_CATEGORY_NO: return true;
	default: break;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

char16_t bf::System::Char16::get__ToLower__im(char16_t c)
{
	return utf8proc_tolower(c);
}

char16_t bf::System::Char16::get__ToUpper__im(char16_t c)
{
	return utf8proc_toupper(c);
}

bool bf::System::Char16::get__IsLower__im(char16_t c)
{
	return utf8proc_category(c) == UTF8PROC_CATEGORY_LL;
}

bool bf::System::Char16::get__IsUpper__im(char16_t c)
{
	return utf8proc_category(c) == UTF8PROC_CATEGORY_LU;
}

bool bf::System::Char16::get__IsWhiteSpace__im(char16_t c)
{
	return utf8proc_category(c) == UTF8PROC_CATEGORY_ZS;
}

bool bf::System::Char16::get__IsLetterOrDigit__im(char16_t c)
{
	auto cat = utf8proc_category(c);
	switch (cat)
	{
	case UTF8PROC_CATEGORY_LU:
	case UTF8PROC_CATEGORY_LL:
	case UTF8PROC_CATEGORY_LT:
	case UTF8PROC_CATEGORY_LM:
	case UTF8PROC_CATEGORY_LO:
	case UTF8PROC_CATEGORY_ND:
	case UTF8PROC_CATEGORY_NL:
	case UTF8PROC_CATEGORY_NO: return true;
	default: break;
	}
	return false;
}

bool bf::System::Char16::get__IsLetter__im(char16_t c)
{
	auto cat = utf8proc_category(c);
	switch (cat)
	{
	case UTF8PROC_CATEGORY_LU:
	case UTF8PROC_CATEGORY_LL:
	case UTF8PROC_CATEGORY_LT:
	case UTF8PROC_CATEGORY_LM:
	case UTF8PROC_CATEGORY_LO: return true;
	default: break;
	}
	return false;
}

bool bf::System::Char16::get__IsNumber__im(char16_t c)
{
	auto cat = utf8proc_category(c);
	switch (cat)
	{
	case UTF8PROC_CATEGORY_ND:
	case UTF8PROC_CATEGORY_NL:
	case UTF8PROC_CATEGORY_NO: return true;
	default: break;
	}
	return false;
}

intptr bf::System::String::UTF8GetAllocSize(char* str, intptr strlen, int32 options)
{
	return utf8proc_decompose_custom((const utf8proc_uint8_t*)str, strlen, NULL, 0, (utf8proc_option_t)options, NULL, NULL);
}

intptr bf::System::String::UTF8Map(char* str, intptr strlen, char* outStr, intptr outSize, int32 options)
{
	intptr result = utf8proc_decompose_custom((const utf8proc_uint8_t*)str, strlen, (utf8proc_int32_t*)outStr, outSize, (utf8proc_option_t)options, NULL, NULL);
	if (result < 0)
		return result;
	result = utf8proc_reencode((utf8proc_int32_t*)outStr, outSize, (utf8proc_option_t)options);
	return result;
}
