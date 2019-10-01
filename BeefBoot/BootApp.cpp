#pragma warning(disable:4996)

//#define BFBUILD_MAIN_THREAD_COMPILE

#include "BootApp.h"
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

enum BfCompilerOptionFlags
{
	BfCompilerOptionFlag_None = 0,
	BfCompilerOptionFlag_EmitDebugInfo = 1,
	BfCompilerOptionFlag_EmitLineInfo = 2,
	BfCompilerOptionFlag_WriteIR = 4,
	BfCompilerOptionFlag_GenerateOBJ = 8,
	BfCompilerOptionFlag_NoFramePointerElim = 0x10,
	BfCompilerOptionFlag_ClearLocalVars = 0x20,
	BfCompilerOptionFlag_ArrayBoundsCheck = 0x40,
	BfCompilerOptionFlag_EmitDynamicCastCheck = 0x80,
	BfCompilerOptionFlag_EnableObjectDebugFlags = 0x100,
	BfCompilerOptionFlag_EmitObjectAccessCheck = 0x200,
	BfCompilerOptionFlag_EnableCustodian = 0x400,
	BfCompilerOptionFlag_EnableRealtimeLeakCheck = 0x800,
	BfCompilerOptionFlag_EnableSideStack = 0x1000,
	BfCompilerOptionFlag_EnableHotSwapping = 0x2000
};

BF_IMPORT void BF_CALLTYPE BfCompiler_Delete(void* bfCompiler);
BF_EXPORT void BF_CALLTYPE BfCompiler_SetOptions(void* bfCompiler, void* hotProject, int hotIdx,
	int machineType, int toolsetType, int simdSetting, int allocStackCount, int maxWorkerThreads,
	BfCompilerOptionFlags optionFlags, const char* mallocLinkName, const char* freeLinkName);
BF_IMPORT void BF_CALLTYPE BfCompiler_ClearBuildCache(void* bfCompiler);
BF_IMPORT bool BF_CALLTYPE BfCompiler_Compile(void* bfCompiler, void* bfPassInstance, const char* outputPath);
BF_IMPORT float BF_CALLTYPE BfCompiler_GetCompletionPercentage(void* bfCompiler);
BF_IMPORT const char* BF_CALLTYPE BfCompiler_GetOutputFileNames(void* bfCompiler, void* bfProject, bool* hadOutputChanges);
BF_IMPORT const char* BF_CALLTYPE BfCompiler_GetUsedOutputFileNames(void* bfCompiler, void* bfProject, bool flushQueuedHotFiles, bool* hadOutputChanges);

BF_IMPORT void* BF_CALLTYPE BfSystem_CreateParser(void* bfSystem, void* bfProject);
BF_IMPORT void BF_CALLTYPE BfParser_SetSource(void* bfParser, const char* data, int length, const char* fileName);
BF_IMPORT void BF_CALLTYPE BfParser_SetCharIdData(void* bfParser, uint8* data, int length);
BF_IMPORT bool BF_CALLTYPE BfParser_Parse(void* bfParser, void* bfPassInstance, bool compatMode);
BF_IMPORT bool BF_CALLTYPE BfParser_Reduce(void* bfParser, void* bfPassInstance);
BF_IMPORT bool BF_CALLTYPE BfParser_BuildDefs(void* bfParser, void* bfPassInstance, void* resolvePassData, bool fullRefresh);

//////////////////////////////////////////////////////////////////////////

BF_IMPORT void* BF_CALLTYPE BfSystem_Create();
BF_IMPORT void BF_CALLTYPE BfSystem_ReportMemory(void* bfSystem);
BF_IMPORT void BF_CALLTYPE BfSystem_Delete(void* bfSystem);
BF_IMPORT void* BF_CALLTYPE BfSystem_CreatePassInstance(void* bfSystem);
BF_IMPORT void* BF_CALLTYPE BfSystem_CreateCompiler(void* bfSystem, bool isResolveOnly);
BF_IMPORT void* BF_CALLTYPE BfSystem_CreateProject(void* bfSystem, const char* projectName);
BF_IMPORT void BF_CALLTYPE BfParser_Delete(void* bfParser);
BF_IMPORT void BF_CALLTYPE BfSystem_AddTypeOptions(void* bfSystem, const char* filter, int32 simdSetting, int32 optimizationLevel, int32 emitDebugInfo, int32 arrayBoundsCheck,
	int32 initLocalVariables, int32 emitDynamicCastCheck, int32 emitObjectAccessCheck, int32 allocStackTraceDepth);

//////////////////////////////////////////////////////////////////////////

BF_IMPORT void BF_CALLTYPE BfProject_SetDisabled(void* bfProject, bool disabled);
BF_IMPORT void BF_CALLTYPE BfProject_SetOptions(void* bfProject, int targetType, const char* startupObject, const char* preprocessorMacros,
	int optLevel, int ltoType, bool mergeFunctions, bool combineLoads, bool vectorizeLoops, bool vectorizeSLP);
BF_IMPORT void BF_CALLTYPE BfProject_ClearDependencies(void* bfProject);
BF_IMPORT void BF_CALLTYPE BfProject_AddDependency(void* bfProject, void* depProject);

//////////////////////////////////////////////////////////////////////////

BF_IMPORT const char* BF_CALLTYPE BfPassInstance_PopOutString(void* bfPassInstance);
BF_IMPORT void BF_CALLTYPE BfPassInstance_Delete(void* bfPassInstance);

//////////////////////////////////////////////////////////////////////////

BF_IMPORT const char* BF_CALLTYPE VSSupport_Find();

//////////////////////////////////////////////////////////////////////////

USING_NS_BF;

BootApp* Beefy::gApp = NULL;
uint32 gConsoleFGColor = 0;
uint32 gConsoleBGColor = 0;

static bool GetConsoleColor(uint32& fgColor, uint32& bgColor)
{
#ifdef _WIN32
	static uint32 consoleColors[16] = { 0xff000000, 0xff000080, 0xff008000, 0xff008080, 0xff800000, 0xff800080, 0xff808000, 0xffc0c0c0,
		0xff808080, 0xff0000ff, 0xff00ff00, 0xff00ffff, 0xffff0000, 0xffff00ff, 0xffffff00, 0xffffffff };

	CONSOLE_SCREEN_BUFFER_INFO screenBuffInfo = { 0 };	
	GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &screenBuffInfo);
	fgColor = consoleColors[screenBuffInfo.wAttributes & 0xF];
	bgColor = consoleColors[(screenBuffInfo.wAttributes >> 4) & 0xF];	
	return true;
#else
	fgColor = 0xFF808080;
	bgColor = 0xFF000000;
	return false;
#endif	
}

static WORD GetColorCode(uint32 color)
{
	WORD code = 0;
#ifdef _WIN32	
	if (((color >> 0) & 0xFF) > 0x40)
		code |= FOREGROUND_BLUE;
	if (((color >> 8) & 0xFF) > 0x40)
		code |= FOREGROUND_GREEN;
	if (((color >> 16) & 0xFF) > 0x40)
		code |= FOREGROUND_RED;
	if ((((color >> 0) & 0xFF) > 0xC0) ||
		(((color >> 8) & 0xFF) > 0xC0) ||
		(((color >> 16) & 0xFF) > 0xC0))
		code |= FOREGROUND_INTENSITY;
#endif
	return code;
}

static bool SetConsoleColor(uint32 fgColor, uint32 bgColor)
{
#ifdef _WIN32
	WORD attr = GetColorCode(fgColor) | (GetColorCode(bgColor) << 4);
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attr);
	SetConsoleTextAttribute(GetStdHandle(STD_ERROR_HANDLE), attr);
	return true;
#else
	return false;
#endif
}


BootApp::BootApp()
{	
	Targets_Create();
	
	mVerbosity = Verbosity_Normal;
	mTargetType = BfTargetType_BeefConsoleApplication;

	//char str[MAX_PATH];
	//GetModuleFileNameA(NULL, str, MAX_PATH);
	//mInstallDir = GetFileDir(str) + "/";

	//getcwd(str, MAX_PATH);
	//mStartupDir = str;
	//mStartupDir += "/";

	//mDoClean = false;
	mHadCmdLine = false;
	mShowedHelp = false;
	mHadErrors = false;

	mSystem = NULL;
	mCompiler = NULL;
	mProject = NULL;	

#ifdef BF_PLATFORM_WINDOWS
	mOptLevel = BfOptLevel_OgPlus;
	mToolset = BfToolsetType_Microsoft;
#else
	mOptLevel = BfOptLevel_O0;
	mToolset = BfToolsetType_GNU;
#endif
	mEmitIR = false;
	
	GetConsoleColor(gConsoleFGColor, gConsoleBGColor);
}

BootApp::~BootApp()
{
	Targets_Delete();
}

void BootApp::OutputLine(const String& text, OutputPri outputPri)
{
	if (mLogFile.IsOpen())
	{
		mLogFile.WriteSNZ(text);
		mLogFile.WriteSNZ("\n");
	}

	if (outputPri == OutputPri_Error)
		mHadErrors = true;

	switch (outputPri)
	{
	case OutputPri_Low:
		if (mVerbosity < Verbosity_Detailed)
			return;
		break;
	case OutputPri_Normal:
		if (mVerbosity < Verbosity_Normal)
			return;
		break;
	case OutputPri_High:
	case OutputPri_Warning:
	case OutputPri_Error:
		if (mVerbosity < Verbosity_Minimal)
			return;
		break;
	}

	if (outputPri == OutputPri_Warning)
	{
		SetConsoleColor(0xFFFFFF00, gConsoleBGColor);
		std::cerr << text.c_str() << std::endl;
		SetConsoleColor(gConsoleFGColor, gConsoleBGColor);
	}
	else if (outputPri == OutputPri_Error)
	{
		SetConsoleColor(0xFFFF0000, gConsoleBGColor);
		std::cerr << text.c_str() << std::endl;
		SetConsoleColor(gConsoleFGColor, gConsoleBGColor);
	}
	else
		std::cout << text.c_str() << std::endl;
}

void BootApp::Fail(const String& error)
{
	if (mLogFile.IsOpen())
		mLogFile.WriteSNZ("FAIL: " + error + "\n");
	std::cerr << "FAIL: " << error.c_str() << std::endl;
	mHadErrors = true;
}

bool BootApp::HandleCmdLine(const String &cmd, const String& param)
{
	mHadCmdLine = true;

	bool wantedParam = false;

	if ((cmd == "--help") || (cmd == "-h") || (cmd == "/?"))
	{
		mShowedHelp = true;
		std::cout << "BeefBoot - Beef bootstrapping tool" << std::endl;
		
		return false;
	}
	else if (cmd == "--src")
	{
		mRequestedSrc.Add(param);
		wantedParam = true;
	}	
	else if (cmd == "--verbosity")
	{
		if (param == "quiet")
			mVerbosity = Verbosity_Quiet;
		else if (param == "minimal")
			mVerbosity = Verbosity_Minimal;
		else if (param == "normal")
			mVerbosity = Verbosity_Normal;
		else if (param == "detailed")
			mVerbosity = Verbosity_Detailed;
		else if (param == "diagnostic")
			mVerbosity = Verbosity_Diagnostic;
		else
		{
			Fail(StrFormat("Invalid verbosity level: '%s'", param.c_str()));
			return false;
		}
		wantedParam = true;
	}
	else if (cmd == "--define")
	{
		if (!mDefines.IsEmpty())
			mDefines += "\n";
		mDefines += param;
		wantedParam = true;
	}
	else if (cmd == "--startup")
	{
		mStartupObject = param;
		wantedParam = true;
	}
	else if (cmd == "--out")
	{
		mTargetPath = param;
		wantedParam = true;
	}
	else if (cmd == "--linkparams")
	{
		mLinkParams = param;
		wantedParam = true;
	}
	else if (cmd == "-Og+")
	{
		mOptLevel = BfOptLevel_OgPlus;
	}
	else if (cmd == "-O0")
	{
		mOptLevel = BfOptLevel_O0;
	}
	else if (cmd == "-O1")
	{
		mOptLevel = BfOptLevel_O1;
	}
	else if (cmd == "-O2")
	{
		mOptLevel = BfOptLevel_O2;
	}
	else if (cmd == "-O3")
	{
		mOptLevel = BfOptLevel_O3;
	}
	else if (cmd == "-gnu")
	{
		mToolset = BfToolsetType_GNU;
	}
	else if (cmd == "-emitir")
	{
		mEmitIR = true;
	}
	else
	{
		Fail("Unknown option: " + cmd);
		return false;
	}

	if ((wantedParam) && (param.empty()))
	{
		Fail(StrFormat("Parameter expected for '%s'", cmd.c_str()));
		return false;
	}
	else if ((!wantedParam) && (!param.empty()))
	{
		Fail(StrFormat("No parameter expected for '%s'", cmd.c_str()));
		return false;
	}

	return true;
}

bool BootApp::Init()
{    
	char* cwdPtr = getcwd(NULL, 0);
	mWorkingDir = cwdPtr;
	free(cwdPtr);

	if (mTargetPath.IsEmpty())
	{
		Fail("'Out' path not specified");
	}

	if (mRequestedSrc.IsEmpty())
	{
		Fail("No source specified");
	}

	return !mHadErrors;
}

void BootApp::QueueFile(const StringImpl& path)
{
	String ext;
	ext = GetFileExtension(path);
    if ((ext.Equals(".bf", StringImpl::CompareKind_OrdinalIgnoreCase)) ||
        (ext.Equals(".cs", StringImpl::CompareKind_OrdinalIgnoreCase)))
	{		
		int len;
		const char* data = LoadTextData(path, &len);
		if (data == NULL)
		{
			Fail(StrFormat("Unable to load file '%s'", path.c_str()));
			return;
		}
		
		bool worked = true;
		void* bfParser = BfSystem_CreateParser(mSystem, mProject);
		BfParser_SetSource(bfParser, data, len, path.c_str());
		//bfParser.SetCharIdData(charIdData);
		worked &= BfParser_Parse(bfParser, mPassInstance, false);
		worked &= BfParser_Reduce(bfParser, mPassInstance);
		worked &= BfParser_BuildDefs(bfParser, mPassInstance, NULL, false);
		
		delete data;
	}
}

void BootApp::QueuePath(const StringImpl& path)
{
	if (DirectoryExists(path))
	{
		for (auto& fileEntry : FileEnumerator(path, FileEnumerator::Flags_Files))
		{
			String filePath = fileEntry.GetFilePath();
			
			String fileName;
			fileName = GetFileName(filePath);

			QueueFile(filePath);			
		}

		for (auto& fileEntry : FileEnumerator(path, FileEnumerator::Flags_Directories))
		{
			String childPath = fileEntry.GetFilePath();
			String dirName;
			dirName = GetFileName(childPath);

			if (dirName == "build")
				continue;

			QueuePath(childPath);			
		}
	}
	else
	{
		QueueFile(path);		
	}
}

static void CompileThread(void* param)
{
	BfpThread_SetName(NULL, "CompileThread", NULL);

	BootApp* app = (BootApp*)param;
	BfCompiler_ClearBuildCache(app->mCompiler);
	
	if (!BfCompiler_Compile(app->mCompiler, app->mPassInstance, app->mBuildDir.c_str()))
		app->mHadErrors = true;
}

void BootApp::DoCompile()
{
#ifdef BFBUILD_MAIN_THREAD_COMPILE
	mOutputDirectory = outputDirectory;
	CompileThread(this);
#else	

	WorkThreadFunc workThread;
	workThread.Start(CompileThread, this);

	int lastProgressTicks = 0;

	bool showProgress = mVerbosity >= Verbosity_Normal;

	int progressSize = 30;
	if (showProgress)
	{
		std::cout << "[";
		for (int i = 0; i < progressSize; i++)
			std::cout << " ";
		std::cout << "]";
		for (int i = 0; i < progressSize + 1; i++)
			std::cout << "\b";
		std::cout.flush();
	}

	while (true)
	{
		bool isDone = workThread.WaitForFinish(100);

		float pct = BfCompiler_GetCompletionPercentage(mCompiler);
		if (isDone)
			pct = 1.0;
		int progressTicks = (int)(pct * progressSize + 0.5f);

		while (progressTicks > lastProgressTicks)
		{
			if (showProgress)
			{
				std::cout << "*";
				std::cout.flush();
			}
			lastProgressTicks++;
		}

		if (isDone)
			break;
	}

	if (showProgress)
		std::cout << std::endl;
#endif
}

struct OutputContext
{
	bool mIsError;
	BfpFile* mFile;
};

static void OutputThread(void* param)
{
	OutputContext* context = (OutputContext*)param;

	String queuedStr;
	
	while (true)
	{
		char data[1024];
		
		BfpFileResult result;
		int bytesRead = (int)BfpFile_Read(context->mFile, data, 1023, -1, &result);
		if ((result != BfpFileResult_Ok) && (result != BfpFileResult_PartialData))
			return;

		data[bytesRead] = 0;
		if (context->mIsError)
		{
			std::cerr << data;
			std::cerr.flush();
		}
		else
		{
			std::cout << data;
			std::cout.flush();
		}

		if (gApp->mLogFile.IsOpen())
		{
			// This is to ensure that error and output lines are not merged together, though they may interleave
			queuedStr.Append(data, bytesRead);
			while (true)
			{
				int crPos = (int)queuedStr.IndexOf('\n');
				if (crPos == -1)
					break;

				AutoCrit autoCrit(gApp->mLogCritSect);
				if (context->mIsError)
					gApp->mLogFile.WriteSNZ("err> ");
				else
					gApp->mLogFile.WriteSNZ("out> ");

				int endPos = crPos;
				if ((endPos > 0) && (queuedStr[endPos - 1] == '\r'))
					endPos--;
				gApp->mLogFile.Write((void*)queuedStr.c_str(), endPos);
				gApp->mLogFile.WriteSNZ("\n");
				queuedStr.Remove(0, crPos + 1);
			}
		}
	}
}

bool BootApp::QueueRun(const String& fileName, const String& args, const String& workingDir, BfpSpawnFlags extraFlags)
{
	OutputLine(StrFormat("EXECUTING: %s %s", fileName.c_str(), args.c_str()), OutputPri_Low);

    BfpSpawnFlags spawnFlags = (BfpSpawnFlags)(BfpSpawnFlag_NoWindow | BfpSpawnFlag_RedirectStdOutput | BfpSpawnFlag_RedirectStdError | extraFlags);    
    BfpSpawn* spawn = BfpSpawn_Create(fileName.c_str(), args.c_str(), workingDir.c_str(), NULL, spawnFlags, NULL);
    if (spawn == NULL)
    {
        Fail(StrFormat("Failed to execute '%s'", fileName.c_str()));
        return false;
    }
    int exitCode = 0;

	OutputContext outputContext;;
	outputContext.mIsError = false;
	OutputContext errorContext;	
	errorContext.mIsError = false;
	BfpSpawn_GetStdHandles(spawn, NULL, &outputContext.mFile, &errorContext.mFile);

	BfpThread* outputThread = BfpThread_Create(OutputThread, (void*)&outputContext);
	BfpThread* errorThread = BfpThread_Create(OutputThread, (void*)&errorContext);
	
    BfpSpawn_WaitFor(spawn, -1, &exitCode, NULL);	

	if (outputContext.mFile != NULL)
		BfpFile_Close(outputContext.mFile, NULL);
	if (errorContext.mFile != NULL)
		BfpFile_Close(errorContext.mFile, NULL);

	BfpThread_WaitFor(outputThread, -1);
	BfpThread_WaitFor(errorThread, -1);	

	BfpThread_Release(outputThread);
	BfpThread_Release(errorThread);
    BfpSpawn_Release(spawn);

    if (exitCode != 0)
    {
        Fail(StrFormat("Exit code returned: %d", exitCode));
        return false;
    }
    return true;
}

#ifdef BF_PLATFORM_WINDOWS
void BootApp::DoLinkMS()
{
	String vsStr = VSSupport_Find();
		
	int toolIdx = (int)vsStr.IndexOf("TOOL64\t");
	int toolCrIdx = (int)vsStr.IndexOf('\n', toolIdx + 1);
	if ((toolIdx == -1) || (toolCrIdx == -1))
	{
		Fail("Failed to detect Visual Studio configuration. Is Visual Studio 2015 or later installed?");
		return;
	}
	
	String linkerPath = vsStr.Substring(toolIdx + 7, toolCrIdx - toolIdx - 7);
	linkerPath.Append("\\link.exe");

	String linkLine;

	String targetPath = mTargetPath;

	bool hadOutputChanges;
	const char* result = BfCompiler_GetUsedOutputFileNames(mCompiler, mProject, true, &hadOutputChanges);
	if (result == NULL)
		return;
	std::string fileNamesStr;
	fileNamesStr += result;
	if (fileNamesStr.length() == 0)
		return;
	int curIdx = -1;
	while (curIdx < (int)fileNamesStr.length())
	{
		int nextBr = (int)fileNamesStr.find('\n', curIdx + 1);
		if (nextBr == -1)
			nextBr = (int)fileNamesStr.length();
		linkLine.Append(fileNamesStr.substr(curIdx + 1, nextBr - curIdx - 1));
		linkLine.Append(" ");
		curIdx = nextBr;
	}

	linkLine.Append("-out:");
	IDEUtils::AppendWithOptionalQuotes(linkLine, targetPath);
	linkLine.Append(" ");

	if (mTargetType == BfTargetType_BeefConsoleApplication)
		linkLine.Append("-subsystem:console ");
	else
		linkLine.Append("-subsystem:windows ");

	linkLine.Append("-defaultlib:libcmtd ");
	linkLine.Append("-nologo ");

	linkLine.Append("-pdb:");
	int lastDotPos = (int)targetPath.LastIndexOf('.');
    if (lastDotPos == -1)
        lastDotPos = (int)targetPath.length();
	auto pdbName = String(targetPath, 0, lastDotPos);
	pdbName.Append(".pdb");
	IDEUtils::AppendWithOptionalQuotes(linkLine, pdbName);
	linkLine.Append(" ");

	linkLine.Append("-debug ");

	int checkIdx = 0;
	while (true)
	{
		int libIdx = (int)vsStr.IndexOf("LIB64\t", checkIdx);
		if (libIdx == -1)
			break;
		int libCrIdx = (int)vsStr.IndexOf('\n', libIdx + 1);
		if (libCrIdx == -1)
			break;

		String libPath = vsStr.Substring(libIdx + 6, libCrIdx - libIdx - 6);
		linkLine.Append("-libpath:\"");
		linkLine.Append(libPath);
		linkLine.Append("\" ");
		checkIdx = libCrIdx + 1;
	}

	linkLine.Append(mLinkParams);	

	BfpSpawnFlags flags = BfpSpawnFlag_None;
	if (true)
	{
		//if (linkLine.HasMultibyteChars())
		if (true)
			flags = (BfpSpawnFlags)(BfpSpawnFlag_UseArgsFile | BfpSpawnFlag_UseArgsFile_Native | BfpSpawnFlag_UseArgsFile_BOM);
		else
			flags = (BfpSpawnFlags)(BfpSpawnFlag_UseArgsFile);
	}

	auto runCmd = QueueRun(linkerPath, linkLine, mWorkingDir, flags);
}
#endif

void BootApp::DoLinkGNU()
{
    String linkerPath = "/usr/bin/c++";

    String linkLine;

    String targetPath = mTargetPath;

    bool hadOutputChanges;
    const char* result = BfCompiler_GetUsedOutputFileNames(mCompiler, mProject, true, &hadOutputChanges);
    if (result == NULL)
        return;
    std::string fileNamesStr;
    fileNamesStr += result;
    if (fileNamesStr.length() == 0)
        return;
    int curIdx = -1;
    while (curIdx < (int)fileNamesStr.length())
    {
        int nextBr = (int)fileNamesStr.find('\n', curIdx + 1);
        if (nextBr == -1)
            nextBr = (int)fileNamesStr.length();
        linkLine.Append(fileNamesStr.substr(curIdx + 1, nextBr - curIdx - 1));
        linkLine.Append(" ");
        curIdx = nextBr;
    }

    linkLine.Append("-o ");
    IDEUtils::AppendWithOptionalQuotes(linkLine, targetPath);
    linkLine.Append(" ");
    linkLine.Append("-g ");

    linkLine.Append("-debug -no-pie ");
    linkLine.Append(mLinkParams);

    auto runCmd = QueueRun(linkerPath, linkLine, mWorkingDir, true ? BfpSpawnFlag_UseArgsFile : BfpSpawnFlag_None);
}

bool BootApp::Compile()
{
	DWORD startTick = BFTickCount();

	mSystem = BfSystem_Create();

	mCompiler = BfSystem_CreateCompiler(mSystem, false);

	String projectName = GetFileName(mTargetPath);
	int dotPos = (int)projectName.IndexOf('.');
	if (dotPos != -1)
		projectName.RemoveToEnd(dotPos);

	mProject = BfSystem_CreateProject(mSystem, projectName.c_str());
	
	if (!mDefines.IsEmpty())
		mDefines.Append("\n");
	mDefines.Append("BF_64_BIT");
	mDefines.Append("\nBF_LITTLE_ENDIAN");
	mDefines.Append("\n");
	mDefines.Append(BF_PLATFORM_NAME);

	int ltoType = 0;
    BfProject_SetOptions(mProject, mTargetType, mStartupObject.c_str(), mDefines.c_str(), mOptLevel, ltoType, false, false, false, false);
	
	mPassInstance = BfSystem_CreatePassInstance(mSystem);

	Beefy::String exePath;
	BfpGetStrHelper(exePath, [](char* outStr, int* inOutStrSize, BfpResult* result)
		{
			BfpSystem_GetExecutablePath(outStr, inOutStrSize, (BfpSystemResult*)result);
		});
	mBuildDir = GetFileDir(exePath) + "/build";
	
	RecursiveCreateDirectory(mBuildDir + "/" + projectName);

	BfCompilerOptionFlags optionFlags = (BfCompilerOptionFlags)(BfCompilerOptionFlag_EmitDebugInfo | BfCompilerOptionFlag_EmitLineInfo | BfCompilerOptionFlag_GenerateOBJ);
	if (mEmitIR)
		optionFlags = (BfCompilerOptionFlags)(optionFlags | BfCompilerOptionFlag_WriteIR);


	int maxWorkerThreads = BfpSystem_GetNumLogicalCPUs(NULL);
	if (maxWorkerThreads <= 1)
		maxWorkerThreads = 6;

    BfCompiler_SetOptions(mCompiler, NULL, 0, BfMachineType_x64, mToolset, BfSIMDSetting_SSE2, 1, maxWorkerThreads, optionFlags, "malloc", "free");
	    
	for (auto& srcName : mRequestedSrc)
	{
		String absPath = GetAbsPath(srcName, mWorkingDir);
		QueuePath(absPath);
	}

	if (!mHadErrors)
	{
		DoCompile();
		OutputLine(StrFormat("TIMING: Beef compiling: %0.1fs", (BFTickCount() - startTick) / 1000.0), OutputPri_Normal);
	}

	while (true)
	{
		const char* msg = BfPassInstance_PopOutString(mPassInstance);
		if (msg == NULL)
			break;

		if ((strncmp(msg, ":warn ", 6) == 0))
		{
			OutputLine(msg + 6, OutputPri_Warning);
		}
		else if ((strncmp(msg, ":error ", 7) == 0))
		{
			OutputLine(msg + 7, OutputPri_Error);
		}
		else if ((strncmp(msg, ":med ", 5) == 0))
		{
			OutputLine(msg + 5, OutputPri_Normal);
		}
		else if ((strncmp(msg, ":low ", 5) == 0))
		{
			OutputLine(msg + 5, OutputPri_Low);
		}
		else if ((strncmp(msg, "ERROR(", 6) == 0) || (strncmp(msg, "ERROR:", 6) == 0))
		{
			OutputLine(msg, OutputPri_Error);
		}
		else if ((strncmp(msg, "WARNING(", 8) == 0) || (strncmp(msg, "WARNING:", 8) == 0))
		{
			OutputLine(msg, OutputPri_Warning);
		}
		else
			OutputLine(msg);
	}
		
	if (!mHadErrors)
    {
		if (mVerbosity == Verbosity_Normal)
		{
			std::cout << "Linking " << mTargetPath.c_str() << "...";
			std::cout.flush();
		}

#ifdef BF_PLATFORM_WINDOWS
        DoLinkMS();
#else
        DoLinkGNU();
#endif

		if (mVerbosity == Verbosity_Normal)
			std::cout << std::endl;
    }

	BfPassInstance_Delete(mPassInstance);
	BfCompiler_Delete(mCompiler);

	BfSystem_Delete(mSystem);

	return !mHadErrors;
}

