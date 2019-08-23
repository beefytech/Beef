#include "BeefySysLib/Common.h"
#include "Beef/BfCommon.h"

#define HUNSPELL_STATIC
#include "../../extern/hunspell/src/hunspell/hunspell.h"

USING_NS_BF;

class SpellChecker
{
public:
	Hunhandle* mHunHandle;

	~SpellChecker()
	{
		Hunspell_destroy(mHunHandle);
	}
};

BF_EXPORT SpellChecker* BF_CALLTYPE SpellChecker_Create(const char* langPath)
{
	String langPathStr = langPath;
	auto hunHandle = Hunspell_create((langPathStr + ".aff").c_str(), (langPathStr + ".dic").c_str());
	if (hunHandle == 0)
	{
		return NULL;
	}

	SpellChecker* spellChecker = new SpellChecker();	
	spellChecker->mHunHandle = hunHandle;
	
	return spellChecker;
}

BF_EXPORT void BF_CALLTYPE SpellChecker_Delete(SpellChecker* spellChecker)
{
	delete spellChecker;
}

BF_EXPORT bool BF_CALLTYPE SpellChecker_IsWord(SpellChecker* spellChecker, const char* word)
{
	return Hunspell_spell(spellChecker->mHunHandle, word) != 0;
}

BF_EXPORT void BF_CALLTYPE SpellChecker_AddWord(SpellChecker* spellChecker, const char* word)
{
	Hunspell_add(spellChecker->mHunHandle, word);
}

BF_EXPORT const char* BF_CALLTYPE SpellChecker_GetSuggestions(SpellChecker* spellChecker, const char* word)
{
	String& returnString = *gTLStrReturn.Get();
	returnString.clear();
	char** suggestList = NULL;
	int suggestCount = Hunspell_suggest(spellChecker->mHunHandle, &suggestList, word);
	for (int i = 0; i < suggestCount; i++)
	{
		if (!returnString.empty())
			returnString += "\n";
		returnString += suggestList[i];
	}
	Hunspell_free_list(spellChecker->mHunHandle, &suggestList, suggestCount);
	return returnString.c_str();
}