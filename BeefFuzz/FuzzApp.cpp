#pragma warning(disable:4996)

#include "FuzzApp.h"
#include <iostream>
#include "BeefySysLib/util/String.h"
#include "BeefySysLib/util/FileEnumerator.h"
#include "BeefySysLib/util/WorkThread.h"
#include "BeefySysLib/platform/PlatformHelper.h"
#include "Compiler/BfSystem.h"

#ifdef BF_PLATFORM_WINDOWS
#include <direct.h>
#endif

BF_IMPORT void BF_CALLTYPE Targets_Create();
BF_IMPORT void BF_CALLTYPE Targets_Delete();

BF_IMPORT void BF_CALLTYPE BfSystem_ReportMemory(void* bfSystem);
BF_EXPORT void BF_CALLTYPE BfCompiler_ProgramDone();

BF_IMPORT void BF_CALLTYPE Debugger_FullReportMemory();

//////////////////////////////////////////////////////////////////////////

BF_IMPORT void BF_CALLTYPE BfCompiler_Delete(void* bfCompiler);
BF_EXPORT void BF_CALLTYPE BfCompiler_SetOptions(void* bfCompiler, void* hotProject, int hotIdx,
	const char* targetTriple, const char* targetCPU, int toolsetType, int simdSetting, int allocStackCount, int maxWorkerThreads,
	Beefy::BfCompilerOptionFlags optionFlags, const char* mallocLinkName, const char* freeLinkName);
BF_IMPORT void BF_CALLTYPE BfCompiler_ClearBuildCache(void* bfCompiler);
BF_IMPORT bool BF_CALLTYPE BfCompiler_Compile(void* bfCompiler, void* bfPassInstance, const char* outputPath);
BF_IMPORT float BF_CALLTYPE BfCompiler_GetCompletionPercentage(void* bfCompiler);
BF_IMPORT const char* BF_CALLTYPE BfCompiler_GetUsedOutputFileNames(void* bfCompiler, void* bfProject, bool flushQueuedHotFiles, bool* hadOutputChanges);

BF_IMPORT void* BF_CALLTYPE BfSystem_CreateParser(void* bfSystem, void* bfProject);
BF_IMPORT void BF_CALLTYPE BfParser_SetSource(void* bfParser, const char* data, int length, const char* fileName);
BF_IMPORT void BF_CALLTYPE BfParser_SetCharIdData(void* bfParser, uint8* data, int length);
BF_IMPORT bool BF_CALLTYPE BfParser_Parse(void* bfParser, void* bfPassInstance, bool compatMode);
BF_IMPORT bool BF_CALLTYPE BfParser_Reduce(void* bfParser, void* bfPassInstance);
BF_IMPORT bool BF_CALLTYPE BfParser_BuildDefs(void* bfParser, void* bfPassInstance, void* resolvePassData, bool fullRefresh);

//////////////////////////////////////////////////////////////////////////

BF_IMPORT void* BF_CALLTYPE BfSystem_Create();
BF_EXPORT void BF_CALLTYPE BfSystem_Lock(void* bfSystem, int priority);
BF_EXPORT void BF_CALLTYPE BfSystem_Unlock(void* bfSystem);
BF_IMPORT void BF_CALLTYPE BfSystem_ReportMemory(void* bfSystem);
BF_IMPORT void BF_CALLTYPE BfSystem_Delete(void* bfSystem);
BF_IMPORT void* BF_CALLTYPE BfSystem_CreatePassInstance(void* bfSystem);
BF_IMPORT void* BF_CALLTYPE BfSystem_CreateCompiler(void* bfSystem, bool isResolveOnly);
BF_IMPORT void* BF_CALLTYPE BfSystem_CreateProject(void* bfSystem, const char* projectName, const char* projectDir);
BF_IMPORT void BF_CALLTYPE BfParser_Delete(void* bfParser);
BF_IMPORT void BF_CALLTYPE BfSystem_AddTypeOptions(void* bfSystem, const char* filter, int32 simdSetting, int32 optimizationLevel, int32 emitDebugInfo, int32 arrayBoundsCheck,
	int32 initLocalVariables, int32 emitDynamicCastCheck, int32 emitObjectAccessCheck, int32 allocStackTraceDepth);

//////////////////////////////////////////////////////////////////////////

BF_IMPORT void BF_CALLTYPE BfProject_SetDisabled(void* bfProject, bool disabled);
BF_IMPORT void BF_CALLTYPE BfProject_SetOptions(void* bfProject, int targetType, const char* startupObject, const char* preprocessorMacros,
	int optLevel, int ltoType, int relocType, int picLevel, int32 flags);
BF_IMPORT void BF_CALLTYPE BfProject_ClearDependencies(void* bfProject);
BF_IMPORT void BF_CALLTYPE BfProject_AddDependency(void* bfProject, void* depProject);

//////////////////////////////////////////////////////////////////////////

BF_IMPORT const char* BF_CALLTYPE BfPassInstance_PopOutString(void* bfPassInstance);
BF_IMPORT void BF_CALLTYPE BfPassInstance_Delete(void* bfPassInstance);

//////////////////////////////////////////////////////////////////////////

BF_IMPORT const char* BF_CALLTYPE VSSupport_Find();

//////////////////////////////////////////////////////////////////////////

USING_NS_BF;

FuzzApp* Beefy::gApp = NULL;


FuzzApp::FuzzApp()
{	
	Targets_Create();

	mTargetType = BfTargetType_BeefConsoleApplication;
	
	mSystem = NULL;
	mCompiler = NULL;
	mProject = NULL;	
	mCELibProject = NULL;
	mIsCERun = false;
	mStartupObject = "Program";

#ifdef BF_PLATFORM_WINDOWS
	mOptLevel = BfOptLevel_OgPlus;
	mToolset = BfToolsetType_Microsoft;
#else
	mOptLevel = BfOptLevel_O0;
	mToolset = BfToolsetType_GNU;
#endif

#ifdef BF_PLATFORM_WINDOWS
	mTargetTriple = "x86_64-pc-windows-msvc";
#elif defined BF_PLATFORM_MACOS
	mTargetTriple = "x86_64-apple-macosx10.8.0";
#else
	mTargetTriple = "x86_64-unknown-linux-gnu";
#endif
}

FuzzApp::~FuzzApp()
{
	Targets_Delete();
}

bool FuzzApp::Init()
{    
	char* cwdPtr = getcwd(NULL, 0);
	mWorkingDir = cwdPtr;
	free(cwdPtr);

	if (mTargetPath.IsEmpty())
		return false;

	return true;
}

bool FuzzApp::QueueFile(const char* data, size_t len)
{
	bool worked = true;
	void* bfParser = BfSystem_CreateParser(mSystem, (mCELibProject != NULL) ? mCELibProject : mProject);
	BfParser_SetSource(bfParser, data, len, "Fuzz.bf");
	//bfParser.SetCharIdData(charIdData);
	worked &= BfParser_Parse(bfParser, mPassInstance, false);
	worked &= BfParser_Reduce(bfParser, mPassInstance);
	worked &= BfParser_BuildDefs(bfParser, mPassInstance, NULL, false);
	return worked;
}

bool FuzzApp::QueuePath(const StringImpl& path)
{
	if (DirectoryExists(path))
	{
		for (auto& fileEntry : FileEnumerator(path, FileEnumerator::Flags_Files))
		{
			String filePath = fileEntry.GetFilePath();

			String fileName;
			fileName = GetFileName(filePath);

			String ext;
			ext = GetFileExtension(filePath);

			if ((ext.Equals(".bf", StringImpl::CompareKind_OrdinalIgnoreCase)) ||
				(ext.Equals(".cs", StringImpl::CompareKind_OrdinalIgnoreCase)))
			{
				int len;
				const char* data = LoadTextData(filePath, &len);
				if (data != NULL)
				{
					bool success = QueueFile(data, len);
					delete[] data;

					if (!success)
						return false;
				}
			}
		}

		for (auto& fileEntry : FileEnumerator(path, FileEnumerator::Flags_Directories))
		{
			String childPath = fileEntry.GetFilePath();
			String dirName;
			dirName = GetFileName(childPath);

			if (dirName == "build")
				continue;

			if (!QueuePath(childPath))
				return false;
		}

		return true;
	}

	return false;
}

bool FuzzApp::CopyFile(const StringImpl& srcPath, const StringImpl& destPath)
{
	BfpFileResult result = BfpFileResult_Ok;
	for (int i = 0; i < 20; i++)
	{
		BfpFile_Copy(srcPath.c_str(), destPath.c_str(), BfpFileCopyKind_Always, &result);
		if (result == BfpFileResult_Ok)
			return true;
		BfpThread_Sleep(100);
	}
	return false;
}

void FuzzApp::PrepareCompiler()
{
	mSystem = BfSystem_Create();

	mCompiler = BfSystem_CreateCompiler(mSystem, false);

	String projectName = GetFileName(mTargetPath);
	int dotPos = (int)projectName.IndexOf('.');
	if (dotPos != -1)
		projectName.RemoveToEnd(dotPos);
	if (projectName.IsEmpty())
		projectName.Append("BeefProject");
	
	mProject = BfSystem_CreateProject(mSystem, projectName.c_str(), GetFileDir(mTargetPath).c_str());

	if (mIsCERun)
	{
		mCELibProject = BfSystem_CreateProject(mSystem, "BeefLib", GetFileDir(mTargetPath).c_str());
		BfProject_SetOptions(mCELibProject, BfTargetType_BeefLib, "", mDefines.c_str(), mOptLevel, 0, 0, 0, BfProjectFlags_None);
	}

	String defines = mDefines;
	if (!defines.IsEmpty())
		defines.Append("\n");
	defines.Append("BF_64_BIT");
	defines.Append("\nBF_LITTLE_ENDIAN");
	defines.Append("\n");
	defines.Append(BF_PLATFORM_NAME);

	int ltoType = 0;
	BfProject_SetOptions(mProject, mTargetType, mStartupObject.c_str(), defines.c_str(), BfOptLevel_O0, ltoType, 0, 0, BfProjectFlags_None);

	if (mCELibProject != NULL)
		BfProject_AddDependency(mProject, mCELibProject);

	mPassInstance = BfSystem_CreatePassInstance(mSystem);

	Beefy::String exePath;
	BfpGetStrHelper(exePath, [](char* outStr, int* inOutStrSize, BfpResult* result)
		{
			BfpSystem_GetExecutablePath(outStr, inOutStrSize, (BfpSystemResult*)result);
		});
	mBuildDir = GetFileDir(exePath) + "/build";

	RecursiveCreateDirectory(mBuildDir + "/" + projectName);
	if (mIsCERun)
		RecursiveCreateDirectory(mBuildDir + "/BeefLib");

	BfCompilerOptionFlags optionFlags = (BfCompilerOptionFlags)(BfCompilerOptionFlag_EmitDebugInfo | BfCompilerOptionFlag_EmitLineInfo | BfCompilerOptionFlag_GenerateOBJ | BfCompilerOptionFlag_OmitDebugHelpers);

	//int maxWorkerThreads = BfpSystem_GetNumLogicalCPUs(NULL);
	//if (maxWorkerThreads <= 1)
	//	maxWorkerThreads = 6;

	BfCompiler_SetOptions(mCompiler, NULL, 0, mTargetTriple.c_str(), "", mToolset, BfSIMDSetting_SSE2, 1, 1, optionFlags, "malloc", "free");
}


bool FuzzApp::Compile()
{
	BfCompiler_ClearBuildCache(mCompiler);

	if (!BfCompiler_Compile(mCompiler, mPassInstance, mBuildDir.c_str()))
		return false;

	if (!mCEDest.IsEmpty())
	{
		String ext;
		String srcResult = mBuildDir + "/BeefProject/BeefProject";
		srcResult += BF_OBJ_EXT;
		
		if (!CopyFile(srcResult, mCEDest))
			return false;
	}

	while (true)
	{
		const char* msg = BfPassInstance_PopOutString(mPassInstance);
		if (msg == NULL)
			break;

		if ((strncmp(msg, ":error ", 7) == 0) ||
			(strncmp(msg, "ERROR(", 6) == 0) ||
			(strncmp(msg, "ERROR:", 6) == 0))
		{
			return false;
		}
	}

	return true;
}

void FuzzApp::ReleaseCompiler()
{
	BfPassInstance_Delete(mPassInstance);
	BfCompiler_Delete(mCompiler);

	BfSystem_Delete(mSystem);
}


