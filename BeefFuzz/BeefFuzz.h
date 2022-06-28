#pragma once

#include "BeefySysLib/Common.h"
#include "BeefySysLib/util/Array.h"
#include "BeefySysLib/util/String.h"
#include <set>
//#include <direct.h>

#pragma warning(disable:4996)

NS_BF_BEGIN

#define APPEND2(VAL1, VAL2) VAL1##VAL2
#define APPEND(VAL1, VAL2) APPEND2(VAL1, VAL2)
#define ENUM_VAL_GENERATE(ENUM_ENTRY) APPEND(ENUM_TYPE, _##ENUM_ENTRY),
#define ENUM_NAME_GENERATE(ENUM_ENTRY) #ENUM_ENTRY,
#define ENUM_CREATE_DO2(EnumName) \
	static const char* EnumName##_Names[] = { ENUM_DECLARE(ENUM_NAME_GENERATE) }; \
	enum EnumName { ENUM_DECLARE(ENUM_VAL_GENERATE) }; \
	static bool EnumParse(const String& name, EnumName& result) \
	{ \
		for (int i = 0; i < sizeof(EnumName##_Names)/sizeof(const char*); i++) \
			if (name == EnumName##_Names[i]) { result = (EnumName)i; return true; } \
		return false; \
	} \
	static const char* EnumToString(EnumName enumVal) \
	{ \
		return EnumName##_Names[(int)enumVal]; \
	}
#define ENUM_CREATE_DO(EnumType) ENUM_CREATE_DO2(EnumType)
#define ENUM_CREATE ENUM_CREATE_DO(ENUM_TYPE)

class IDEUtils
{
public:
	static bool FixFilePath(String& filePath)
	{
		if (filePath.length() == 0)
			return false;

		if (filePath[0] == '<')
			return false;

		if ((filePath.length() > 1) && (filePath[1] == ':'))
			filePath[0] = tolower(filePath[0]);

		bool prevWasSlash = false;
		for (int i = 0; i < filePath.length(); i++)
		{
			//if ((filePath[i] == '/') && (filePath[i - 1])

			if (filePath[i] == DIR_SEP_CHAR_ALT)
				filePath[i] = DIR_SEP_CHAR;

			if (filePath[i] == DIR_SEP_CHAR)
			{
				if ((prevWasSlash) && (i > 1))
				{
					filePath.Remove(i, 1);
					i--;
					continue;
				}

				prevWasSlash = true;
			}
			else
				prevWasSlash = false;

			if ((i >= 4) && (filePath[i - 3] == DIR_SEP_CHAR) && (filePath[i - 2] == '.') && (filePath[i - 1] == '.') && (filePath[i] == DIR_SEP_CHAR))
			{
				int prevSlash = (int)filePath.LastIndexOf(DIR_SEP_CHAR, i - 4);
				if (prevSlash != -1)
				{
					filePath.Remove(prevSlash, i - prevSlash);
					i = prevSlash;
				}
			}
		}
		return true;
	}
	
	static void GetDirWithSlash(String& dirName)
	{
		if (dirName.length() == 0)
			return;
		char lastC = dirName[dirName.length() - 1];
		if ((lastC != '\\') && (lastC != '/'))
			dirName += DIR_SEP_CHAR;
	}

	static FILE* CreateFileWithDir(const String& fileName, const char* options)
	{
		FILE* fp = fopen(fileName.c_str(), options);
		if (fp == NULL)
		{
			String fileDir = GetFileDir(fileName);
			if (!fileDir.empty())
			{
				RecursiveCreateDirectory(fileDir);
				fp = fopen(fileName.c_str(), "w");
			}
		}
		return fp;
	}

	static bool WriteAllText(const String& fileName, const String& data)
	{
		FILE* fp = CreateFileWithDir(fileName, "w");
		if (fp == NULL)
			return false;
		fwrite(data.c_str(), 1, data.length(), fp);
		fclose(fp);
		return true;
	}

	static void GetFileNameWithoutExtension(const String& filePath, String& outFileName)
	{
		outFileName += GetFileName(filePath);
		int dot = (int)outFileName.LastIndexOf('.');
		if (dot != -1)
			outFileName.RemoveToEnd(dot);
	}

	static void GetExtension(const String& filePath, String& ext)
	{
		int idx = (int)filePath.LastIndexOf('.');
		if (idx != -1)
			ext = filePath.Substring(idx);
	}

	static void AppendWithOptionalQuotes(String& targetStr, const String& srcFileName)
	{
		if ((int)srcFileName.IndexOf(' ') == -1)
			targetStr += srcFileName;
		else
			targetStr += "\"" + srcFileName + "\"";
	}
};

class ArgBuilder
{
public:
	String* mTarget;
	bool mDoLongBreak;
	int mLastBreak;
	std::multiset<String> mLinkPaths;

public:
	ArgBuilder(String& target, bool doLongBreak)
	{
		mTarget = &target;
		mDoLongBreak = doLongBreak;
		if (mDoLongBreak)
			mLastBreak = (int)mTarget->LastIndexOf('\n');
		else
			mLastBreak = 0;
	}

	void AddSep()
	{
		if (mDoLongBreak)
		{
			if (mTarget->length() - mLastBreak > 0x1F000)
			{
				mLastBreak = (int)mTarget->length();
				mTarget->Append('\n');
				return;
			}
		}
		mTarget->Append(' ');
	}

	void AddFileName(const String& filePath)
	{
		IDEUtils::AppendWithOptionalQuotes(*mTarget, filePath);
	}
};

NS_BF_END

