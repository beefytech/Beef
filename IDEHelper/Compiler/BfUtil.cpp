#include "BfUtil.h"

USING_NS_BF;

void* Beefy::DecodeLocalDataPtr(const char*& strRef)
{
	void* val = (void*)stouln(strRef, sizeof(intptr) * 2);
	strRef += sizeof(intptr) * 2;
	return val;
}

String Beefy::EncodeDataPtr(void* addr, bool doPrefix)
{
	if (doPrefix)
	{
		return StrFormat("0x%p", addr);
	}
	else
		return StrFormat("%p", addr);
}

String Beefy::EncodeDataPtr(uint32 addr, bool doPrefix)
{
	if (doPrefix)
		return StrFormat("0x%08X", addr);
	else
		return StrFormat("%08X", addr);
}

String Beefy::EncodeDataPtr(uint64 addr, bool doPrefix)
{
	if (doPrefix)
		return StrFormat("0x%@", addr);
	else
		return StrFormat("%p", addr);
}

void* Beefy::ZeroedAlloc(int size)
{
	//uint8* data = new uint8[size];
	uint8* data = (uint8*)malloc(size);
	BF_ASSERT(((intptr)data & 7) == 0);
	memset(data, 0, size);
	return data;
}

uint64 stouln(const char* str, int len)
{
	uint64 val = 0;
	for (int i = 0; i < len; i++)
	{
		char c = str[i];
		val *= 0x10;
		if ((c >= '0') && (c <= '9'))
			val += c - '0';
		else if ((c >= 'A') && (c <= 'F'))
			val += (c - 'A') + 0xA;
		else if ((c >= 'a') && (c <= 'f'))
			val += (c - 'a') + 0xa;
	}
	return val;
}

bool Beefy::BfCheckWildcard(const StringImpl& wildcard, const StringImpl& checkStr)
{
	bool matched = true;
	const char* filterPtr = wildcard.c_str();
	const char* namePtr = checkStr.c_str();

	char prevFilterC = 0;
	while (true)
	{
		char filterC;
		while (true)
		{
			filterC = *(filterPtr++);
			if (filterC != ' ')
				break;
		}

		char nameC;
		while (true)
		{
			nameC = *(namePtr++);
			if (nameC != ' ')
				break;
		}

		if ((filterC == 0) || (nameC == 0))
		{
			matched = (filterC == 0) && (nameC == 0);
			break;
		}

		bool doWildcard = false;

		if (nameC != filterC)
		{
			if (filterC == '*')
				doWildcard = true;
			else if (((filterC == ',') || (filterC == '>')) &&
				((prevFilterC == '<') || (prevFilterC == ',')))
			{
				doWildcard = true;
				filterPtr--;
			}

			if (!doWildcard)
			{
				matched = false;
				break;
			}
		}

		if (doWildcard)
		{
			int openDepth = 0;

			const char* startNamePtr = namePtr;

			while (true)
			{
				nameC = *(namePtr++);
				if (nameC == 0)
				{
					namePtr--;
					if (openDepth != 0)
						matched = false;
					break;
				}
				if ((nameC == '>') && (openDepth == 0))
				{
					namePtr--;
					break;
				}

				if (nameC == '<')
					openDepth++;
				else if (nameC == '>')
					openDepth--;
				else if ((nameC == ',') && (openDepth == 0))
				{
					namePtr--;
					break;
				}
			}

			if (!matched)
				break;
		}

		prevFilterC = filterC;		
	}

	return matched;
}