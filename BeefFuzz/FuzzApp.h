#pragma once

#include "BeefFuzz.h"
#include "BeefySysLib/FileStream.h"
#include "BeefySysLib/util/CritSect.h"
#include "BeefySysLib/util/String.h"
#include "BeefySysLib/util/Array.h"
#include "Compiler/BfSystem.h"

NS_BF_BEGIN

class FuzzApp
{
public:
	BfTargetType mTargetType;
	String mTargetTriple;
	BfOptLevel mOptLevel;
	BfToolsetType mToolset;
	String mBuildDir;
	String mWorkingDir;
	String mDefines;
	String mStartupObject;
	String mTargetPath;
	String mLinkParams;

	void* mSystem;
	void* mCompiler;		
	void* mProject;	
	void* mPassInstance;

	bool mIsCERun;
	void* mCELibProject;
	String mCEDest;

public:
	bool CopyFile(const StringImpl& srcPath, const StringImpl& destPath);

	bool QueueFile(const char* data, size_t len);
	bool QueuePath(const StringImpl& path);

public:
	FuzzApp();
	~FuzzApp();

	void SetTargetType(BfTargetType value) { mTargetType = value; }
	void SetTargetTriple(const String& value) { mTargetTriple = value; }
	void SetOptLevel(BfOptLevel value) { mOptLevel = value; }
	void SetToolset(BfToolsetType value) { mToolset = value; }
	void SetBuildDir(const String& value) { mBuildDir = value; }
	void SetWorkingDir(const String& value) { mWorkingDir = value; }
	void AddDefine(const String& value)	{ mDefines += mDefines.IsEmpty() ? value : "\n" + value; }
	void SetStartupObject(const String& value) { mStartupObject = value; }
	void SetTargetPath(const String& value) { mTargetPath = value; }
	void SetLinkParams(const String& value) { mLinkParams = value; }
	void SetCEDest(const String& value) { mCEDest = value; }

	bool Init();

	void PrepareCompiler();
	bool Compile();
	void ReleaseCompiler();
};

extern FuzzApp* gApp;

NS_BF_END
