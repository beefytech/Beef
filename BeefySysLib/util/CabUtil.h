#pragma once

#include "../Common.h"
#include "../FileStream.h"

NS_BF_BEGIN


class CabFile
{
public:
	String mSrcFileName;
	String mDestDir;
	String mError;

public:
	void Fail(const StringImpl& err);

public:
	CabFile();

	bool Load(const StringImpl& path);
	bool DecompressAll(const StringImpl& destDir);
};

NS_BF_END

