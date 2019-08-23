#include "BeefySysLib/Common.h"
#include <stdio.h>
#include <iostream>
#include <vector>

#pragma warning(disable:4996)

USING_NS_BF;

#define BF_IMPORT extern "C" __declspec(dllimport)

class BlContext;

bool gFailed = false;

BF_IMPORT BlContext* BF_CALLTYPE BlContext_Create();
BF_IMPORT void BF_CALLTYPE BlContext_Delete(BlContext* blContext);
BF_IMPORT void BF_CALLTYPE BlContext_Init(BlContext* blContext, bool is64Bit, bool isDebug);
BF_IMPORT void BF_CALLTYPE BlContext_AddSearchPath(BlContext* blContext, const char* directory);
BF_IMPORT void BF_CALLTYPE BlContext_AddFile(BlContext* blContext, const char* fileName);
BF_IMPORT bool BF_CALLTYPE BlContext_Link(BlContext* blContext);
BF_IMPORT bool BF_CALLTYPE BlContext_SetOutName(BlContext* blContext, const char* outName);
BF_IMPORT bool BF_CALLTYPE BlContext_SetEntryPoint(BlContext* blContext, const char* outName);
BF_IMPORT bool BF_CALLTYPE BlContext_SetImpLib(BlContext* blContext, const char* impLibName);
BF_IMPORT void BF_CALLTYPE BlContext_SetDebug(BlContext* blContext, int debugMode);
BF_IMPORT void BF_CALLTYPE BlContext_SetPDBName(BlContext* blContext, const char* pdbPath);
BF_IMPORT void BF_CALLTYPE BlContext_SetIsDLL(BlContext* blContext);
BF_IMPORT void BF_CALLTYPE BlContext_SetVerbose(BlContext* blContext, bool enabled);
BF_IMPORT void BF_CALLTYPE BlContext_SetNoDefaultLib(BlContext* blContext, bool enabled);
BF_IMPORT void BF_CALLTYPE BlContext_AddNoDefaultLib(BlContext* blContext, const char* name);
BF_IMPORT void BF_CALLTYPE BlContext_AddDefaultLib(BlContext* blContext, const char* name);
BF_IMPORT void BF_CALLTYPE BlContext_SetImageBase(BlContext* blContext, int64 imageBase);
BF_IMPORT void BF_CALLTYPE BlContext_SetFixedBase(BlContext* blContext, bool enabled);
BF_IMPORT void BF_CALLTYPE BlContext_SetDynamicBase(BlContext* blContext, bool enabled);
BF_IMPORT void BF_CALLTYPE BlContext_SetHighEntropyVA(BlContext* blContext, bool enabled);
BF_IMPORT void BF_CALLTYPE BlContext_ManifestUAC(BlContext* blContext, const char* manifestUAC);
BF_IMPORT void BF_CALLTYPE BlContext_SetSubsystem(BlContext* blContext, int subsystem);
BF_IMPORT void BF_CALLTYPE BlContext_SetStack(BlContext* blContext, int reserve, int commit);
BF_IMPORT void BF_CALLTYPE BlContext_SetHeap(BlContext* blContext, int reserve, int commit);

void Fail(const std::string& name)
{
	gFailed = true;
	std::cerr << "ERROR: " << name.c_str() << std::endl;
}

static bool CheckBoolean(const std::string& arg)
{
	if (stricmp(arg.c_str(), "YES") == 0)
		return true;
	if (stricmp(arg.c_str(), "NO") == 0)
		return false;
	Fail(StrFormat("Invalid boolean argument: %s", arg.c_str()));
	return false;
}

BlContext* blContext;
std::vector<std::string> fileNames;
std::string defFileName;
std::string outPath;

bool HandleParam(const std::string& arg, bool isFirstArg = false)
{
	if ((arg[0] == '-') || (arg[0] == '/'))
	{
		int colonPos = (int)arg.find(':');
		if (colonPos != -1)
		{
			std::string argName = ToUpper(arg.substr(1, colonPos - 1));
			std::string argParam = arg.substr(colonPos + 1);

			if (argName == "OUT")
			{
				outPath = argParam;
				BlContext_SetOutName(blContext, outPath.c_str());
			}
			else if (argName == "ERRORREPORT")
			{

			}
			else if (argName == "MANIFEST")
			{				
			}
			else if (argName == "MANIFESTUAC")
			{
				BlContext_ManifestUAC(blContext, argParam.c_str());
			}
			else if (argName == "UIACCESS")
			{

			}
			else if (argName == "DEBUG")
			{
				if (ToUpper(argParam) == "NONE")
					BlContext_SetDebug(blContext, 0);
				else
					BlContext_SetDebug(blContext, 1);
			}
			else if (argName == "DEBUGTYPE")
			{

			}
			else if (argName == "PDB")
			{
				BlContext_SetPDBName(blContext, argParam.c_str());
			}
			else if (argName == "ENTRY")
			{
				BlContext_SetEntryPoint(blContext, argParam.c_str());
			}
			else if (argName == "SUBSYSTEM")
			{
				std::string subsysName = ToUpper(argParam);
				if (subsysName == "WINDOWS")
					BlContext_SetSubsystem(blContext, IMAGE_SUBSYSTEM_WINDOWS_GUI);
				else if (subsysName == "CONSOLE")
					BlContext_SetSubsystem(blContext, IMAGE_SUBSYSTEM_WINDOWS_CUI);
				else
					Fail("Specified subsystem not supported");
			}
			else if (argName == "TLBID")
			{

			}
			else if (argName == "IMPLIB")
			{
				BlContext_SetImpLib(blContext, argParam.c_str());
			}
			else if (argName == "MACHINE")
			{
				if (argParam != "X64")
				{
					Fail("Only X64 is currently supported");
				}
			}
			else if (argName == "HEAP")
			{
				int commaPos = (int)argParam.find(',');
				if (commaPos > 0)
					BlContext_SetHeap(blContext, strtol(argParam.c_str(), NULL, 0), strtol(argParam.c_str() + commaPos + 1, NULL, 0));
				else
					BlContext_SetHeap(blContext, strtol(argParam.c_str(), NULL, 0), 0);
			}
			else if (argName == "STACK")
			{
				int commaPos = (int)argParam.find(',');
				if (commaPos > 0)
					BlContext_SetStack(blContext, strtol(argParam.c_str(), NULL, 0), strtol(argParam.c_str() + commaPos + 1, NULL, 0));
				else
					BlContext_SetStack(blContext, strtol(argParam.c_str(), NULL, 0), 0);
			}
			else if (argName == "LIBPATH")
			{
				BlContext_AddSearchPath(blContext, argParam.c_str());
			}
			else if (argName == "VERBOSE")
			{
				BlContext_SetVerbose(blContext, CheckBoolean(argParam));
			}
			else if (argName == "NODEFAULTLIB")
			{
				BlContext_AddNoDefaultLib(blContext, argParam.c_str());
			}			
			else if (argName == "BASE")
			{
				int64 imageBase = 0;
				imageBase = strtoll(argParam.c_str(), NULL, 0);
				if (imageBase != 0)
					BlContext_SetImageBase(blContext, imageBase);
			}
			else if (argName == "FIXED")
			{
				BlContext_SetFixedBase(blContext, CheckBoolean(argParam));
			}
			else if (argName == "DYNAMICBASE")
			{
				BlContext_SetDynamicBase(blContext, CheckBoolean(argParam));
			}
			else if (argName == "HIGHENTROPYVA")
			{
				BlContext_SetHighEntropyVA(blContext, CheckBoolean(argParam));
			}
			else if (argName == "DEF")
			{
				defFileName = argParam;
			}
			else if (argName == "INCREMENTAL")
			{

			}
			else if (argName == "DEFAULTLIB")
			{
				BlContext_AddDefaultLib(blContext, argParam.c_str());
			}
			else
			{
				Fail(StrFormat("Invalid argument: %s", arg.c_str()));
				return 1;
			}
		}
		else
		{
			std::string argName = ToUpper(arg.substr(1));
			if (argName == "NODEFAULTLIB")
			{
				BlContext_SetNoDefaultLib(blContext, true);
			}
			if (argName == "VERBOSE")
			{
				BlContext_SetVerbose(blContext, true);
			}
			else if (argName == "NOLOGO")
			{

			}
			else if (argName == "MANIFEST")
			{

			}
			else if (argName == "DEBUG")
			{
				BlContext_SetDebug(blContext, 1);
			}			
			else if (argName == "NXCOMPAT")
			{

			}
			else if (argName == "DLL")
			{
				BlContext_SetIsDLL(blContext);
			}
			else if (argName == "FIXED")
			{
				BlContext_SetFixedBase(blContext, true);
			}
			else if (argName == "DYNAMICBASE")
			{
				BlContext_SetDynamicBase(blContext, true);
			}
			else if (argName == "INCREMENTAL")
			{

			}
			else
			{
				Fail(StrFormat("Invalid argument: %s", arg.c_str()));
				return false;
			}
		}
	}
	else if (arg[0] == '@')
	{
		char* cmdData = LoadTextData(arg.substr(1), NULL);
		if (cmdData == NULL)
		{
			Fail(StrFormat("Failed to load command file: \"%s\"", arg.substr(1).c_str()));
			return false;
		}
		std::string curArg;
		bool inQuote = false;
		bool prevSlash = false;		
		for (char* cPtr = cmdData; true; cPtr++)
		{
			char c = *cPtr;
			if (c == 0)
			{
				if (!curArg.empty())
					HandleParam(curArg);
				break;
			}
			if (c == '"')
			{
				inQuote = !inQuote;
			}
			else if ((isspace(c)) && (!inQuote))
			{
				if (!curArg.empty())
				{
					HandleParam(curArg);
					curArg.clear();
				}
			}
			else
				curArg += c;
		}

		delete cmdData;
	}
	else if (!isFirstArg)
	{
		fileNames.push_back(arg.c_str());
	}

	return true;
}

int main(int argc, char* argv[])
{
	int startTick = GetTickCount();	

	blContext = BlContext_Create();

	// C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\bin\x86_amd64\link.exe /ERRORREPORT:PROMPT /OUT:"C:\proj\TestCPP\x64\Debug\TestCPP.exe" /INCREMENTAL /NOLOGO /MANIFEST /MANIFESTUAC:"level='asInvoker' uiAccess='false'" /manifest:embed /DEBUG /PDB:"C:\proj\TestCPP\x64\Debug\TestCPP.pdb" /SUBSYSTEM:CONSOLE /TLBID:1 /DYNAMICBASE /NXCOMPAT /IMPLIB:"C:\proj\TestCPP\x64\Debug\TestCPP.lib" /MACHINE:X64 kernel32.lib user32.lib advapi32.lib shell32.lib x64\Debug\main.obj	

	bool success = true;

	for (int argIdx = 0; argIdx < argc; argIdx++)
	{
		if (!HandleParam(argv[argIdx], argIdx == 0))
			break;
	}

	if (gFailed)
		success = false;

	if (success)
	{
		BlContext_Init(blContext, true, true);		
		for (auto fileName : fileNames)
			BlContext_AddFile(blContext, fileName.c_str());
		// Process DEF first.  Relies on LIFO ordering
		if (!defFileName.empty())
			BlContext_AddFile(blContext, defFileName.c_str());

		success &= BlContext_Link(blContext);
	}
	BlContext_Delete(blContext);

	printf("Total Time: %dms\n", GetTickCount() - startTick);

	if (success)
		return 0;
	return 2;
}
