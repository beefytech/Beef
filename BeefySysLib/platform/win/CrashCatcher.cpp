#include "CrashCatcher.h"
#include "../util/Dictionary.h"
#include <commdlg.h>
#include <time.h>
#include <shellapi.h>

USING_NS_BF;

#pragma comment(lib, "version.lib")
#pragma comment(lib, "comdlg32.lib")

#pragma warning(disable:4091)
#pragma warning(disable:4996)
#include <imagehlp.h>

#ifdef BF64
typedef BOOL(__stdcall * SYMINITIALIZEPROC)(HANDLE, LPSTR, BOOL);
typedef DWORD(__stdcall *SYMSETOPTIONSPROC)(DWORD);
typedef BOOL(__stdcall *SYMCLEANUPPROC)(HANDLE);
typedef LPCSTR(__stdcall *UNDECORATESYMBOLNAMEPROC)(LPCSTR, LPSTR, DWORD, DWORD);
typedef BOOL(__stdcall * STACKWALKPROC)
(DWORD, HANDLE, HANDLE, LPSTACKFRAME, LPVOID,
	PREAD_PROCESS_MEMORY_ROUTINE, PFUNCTION_TABLE_ACCESS_ROUTINE,
	PGET_MODULE_BASE_ROUTINE, PTRANSLATE_ADDRESS_ROUTINE);
typedef LPVOID(__stdcall *SYMFUNCTIONTABLEACCESSPROC)(HANDLE, DWORD64);
typedef DWORD64 (__stdcall *SYMGETMODULEBASEPROC)(HANDLE, DWORD64);
typedef BOOL(__stdcall *SYMGETSYMFROMADDRPROC)(HANDLE, DWORD64, PDWORD64, PIMAGEHLP_SYMBOL64);
typedef BOOL(__stdcall *SYMGETLINEFROMADDR)(HANDLE hProcess, DWORD64 qwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINE64 Line64);
#else
typedef BOOL(__stdcall * SYMINITIALIZEPROC)(HANDLE, LPSTR, BOOL);
typedef DWORD(__stdcall *SYMSETOPTIONSPROC)(DWORD);
typedef BOOL(__stdcall *SYMCLEANUPPROC)(HANDLE);
typedef LPCSTR(__stdcall *UNDECORATESYMBOLNAMEPROC)(LPCSTR, LPSTR, DWORD, DWORD);
typedef BOOL(__stdcall * STACKWALKPROC)
(DWORD, HANDLE, HANDLE, LPSTACKFRAME, LPVOID,
	PREAD_PROCESS_MEMORY_ROUTINE, PFUNCTION_TABLE_ACCESS_ROUTINE,
	PGET_MODULE_BASE_ROUTINE, PTRANSLATE_ADDRESS_ROUTINE);
typedef LPVOID(__stdcall *SYMFUNCTIONTABLEACCESSPROC)(HANDLE, DWORD);
typedef DWORD(__stdcall *SYMGETMODULEBASEPROC)(HANDLE, DWORD);
typedef BOOL(__stdcall *SYMGETSYMFROMADDRPROC)(HANDLE, DWORD, PDWORD, PIMAGEHLP_SYMBOL);
typedef BOOL(__stdcall *SYMGETLINEFROMADDR)(HANDLE hProcess, DWORD qwAddr, PDWORD pdwDisplacement, PIMAGEHLP_LINE Line64);
#endif

static HMODULE gImageHelpLib = NULL;
static SYMINITIALIZEPROC gSymInitialize = NULL;
static SYMSETOPTIONSPROC gSymSetOptions = NULL;
static UNDECORATESYMBOLNAMEPROC gUnDecorateSymbolName = NULL;
static SYMCLEANUPPROC gSymCleanup = NULL;
static STACKWALKPROC gStackWalk = NULL;
static SYMFUNCTIONTABLEACCESSPROC gSymFunctionTableAccess = NULL;
static SYMGETMODULEBASEPROC gSymGetModuleBase = NULL;
static SYMGETSYMFROMADDRPROC gSymGetSymFromAddr = NULL;
static SYMGETLINEFROMADDR gSymGetLineFromAddr = NULL;


static bool CreateMiniDump(EXCEPTION_POINTERS* pep, const StringImpl& filePath);

static bool LoadImageHelp()
{
	gImageHelpLib = LoadLibraryA("IMAGEHLP.DLL");
	if (!gImageHelpLib)
		return false;

	gSymInitialize = (SYMINITIALIZEPROC)GetProcAddress(gImageHelpLib, "SymInitialize");
	if (!gSymInitialize)
		return false;

	gSymSetOptions = (SYMSETOPTIONSPROC)GetProcAddress(gImageHelpLib, "SymSetOptions");
	if (!gSymSetOptions)
		return false;

	gSymCleanup = (SYMCLEANUPPROC)GetProcAddress(gImageHelpLib, "SymCleanup");
	if (!gSymCleanup)
		return false;

	gUnDecorateSymbolName = (UNDECORATESYMBOLNAMEPROC)GetProcAddress(gImageHelpLib, "UnDecorateSymbolName");
	if (!gUnDecorateSymbolName)
		return false;

	gStackWalk = (STACKWALKPROC)GetProcAddress(gImageHelpLib, "StackWalk");
	if (!gStackWalk)
		return false;

	gSymFunctionTableAccess = (SYMFUNCTIONTABLEACCESSPROC)GetProcAddress(gImageHelpLib, "SymFunctionTableAccess");
	if (!gSymFunctionTableAccess)
		return false;

	gSymGetModuleBase = (SYMGETMODULEBASEPROC)GetProcAddress(gImageHelpLib, "SymGetModuleBase");
	if (!gSymGetModuleBase)
		return false;

	gSymGetSymFromAddr = (SYMGETSYMFROMADDRPROC)GetProcAddress(gImageHelpLib, "SymGetSymFromAddr");
	if (!gSymGetSymFromAddr)
		return false;

	gSymGetLineFromAddr = (SYMGETLINEFROMADDR)GetProcAddress(gImageHelpLib, "SymGetLineFromAddr64");
	if (!gSymGetLineFromAddr)
		return false;

	gSymSetOptions(SYMOPT_DEFERRED_LOADS);

	// Get image filename of the main executable
	char filepath[MAX_PATH], *lastdir, *pPath;
	DWORD filepathlen = GetModuleFileNameA(NULL, filepath, sizeof(filepath));

	lastdir = strrchr(filepath, '/');
	if (lastdir == NULL) lastdir = strrchr(filepath, '\\');
	if (lastdir != NULL) lastdir[0] = '\0';

	// Initialize the symbol table routines, supplying a pointer to the path
	pPath = filepath;
	if (filepath[0] == 0) pPath = NULL;

	if (!gSymInitialize(GetCurrentProcess(), pPath, TRUE))
		return false;

	return true;
}

struct
{
	DWORD   dwExceptionCode;
	char    *szMessage;
} gMsgTable[] = {
	{ STATUS_SEGMENT_NOTIFICATION,     "Segment Notification" },
	{ STATUS_BREAKPOINT,               "Breakpoint" },
	{ STATUS_SINGLE_STEP,              "Single step" },
	{ STATUS_WAIT_0,                   "Wait 0" },
	{ STATUS_ABANDONED_WAIT_0,         "Abandoned Wait 0" },
	{ STATUS_USER_APC,                 "User APC" },
	{ STATUS_TIMEOUT,                  "Timeout" },
	{ STATUS_PENDING,                  "Pending" },
	{ STATUS_GUARD_PAGE_VIOLATION,     "Guard Page Violation" },
	{ STATUS_DATATYPE_MISALIGNMENT,    "Data Type Misalignment" },
	{ STATUS_ACCESS_VIOLATION,         "Access Violation" },
	{ STATUS_IN_PAGE_ERROR,            "In Page Error" },
	{ STATUS_NO_MEMORY,                "No Memory" },
	{ STATUS_ILLEGAL_INSTRUCTION,      "Illegal Instruction" },
	{ STATUS_NONCONTINUABLE_EXCEPTION, "Noncontinuable Exception" },
	{ STATUS_INVALID_DISPOSITION,      "Invalid Disposition" },
	{ STATUS_ARRAY_BOUNDS_EXCEEDED,    "Array Bounds Exceeded" },
	{ STATUS_FLOAT_DENORMAL_OPERAND,   "Float Denormal Operand" },
	{ STATUS_FLOAT_DIVIDE_BY_ZERO,     "Divide by Zero" },
	{ STATUS_FLOAT_INEXACT_RESULT,     "Float Inexact Result" },
	{ STATUS_FLOAT_INVALID_OPERATION,  "Float Invalid Operation" },
	{ STATUS_FLOAT_OVERFLOW,           "Float Overflow" },
	{ STATUS_FLOAT_STACK_CHECK,        "Float Stack Check" },
	{ STATUS_FLOAT_UNDERFLOW,          "Float Underflow" },
	{ STATUS_INTEGER_DIVIDE_BY_ZERO,   "Integer Divide by Zero" },
	{ STATUS_INTEGER_OVERFLOW,         "Integer Overflow" },
	{ STATUS_PRIVILEGED_INSTRUCTION,   "Privileged Instruction" },
	{ STATUS_STACK_OVERFLOW,           "Stack Overflow" },
	{ STATUS_CONTROL_C_EXIT,           "Ctrl+C Exit" },
	{ 0xFFFFFFFF,                      "" }
};

static HFONT gDialogFont;
static HFONT gBoldFont;
static String gErrorTitle;
static String gErrorText;
static HWND gDebugButtonWindow = NULL;
static HWND gYesButtonWindow = NULL;
static HWND gNoButtonWindow = NULL;
static bool gExiting = false;

static LRESULT CALLBACK SEHWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_COMMAND:
	{
		HWND hwndCtl = (HWND)lParam;
		if (hwndCtl == gYesButtonWindow)
		{
			WCHAR fileName[MAX_PATH];
			fileName[0] = 0;

			OPENFILENAMEW openFileName = { 0 };
			openFileName.hInstance = ::GetModuleHandle(NULL);
			openFileName.hwndOwner = hWnd;
			openFileName.lStructSize = sizeof(OPENFILENAMEW);
			openFileName.lpstrDefExt = L".dmp";
			openFileName.lpstrFilter = L"Crash Dump (*.dmp)\0*.dmp\0All files (*.*)\0*.*\0\0";
			openFileName.lpstrFile = fileName;
			openFileName.nMaxFile = MAX_PATH;
			openFileName.Flags = OFN_EXPLORER | OFN_ENABLESIZING;
			openFileName.lpstrTitle = L"Save Crash Dump";

			if (::GetSaveFileNameW(&openFileName))
			{
				CreateMiniDump(CrashCatcher::Get()->mExceptionPointers, UTF8Encode(fileName));
			}
		}
		else if (hwndCtl == gNoButtonWindow)
		{
			if (!CrashCatcher::Get()->mRelaunchCmd.IsEmpty())
			{
				SHELLEXECUTEINFOW shellExecuteInfo = { 0 };
				shellExecuteInfo.cbSize = sizeof(SHELLEXECUTEINFOW);
				shellExecuteInfo.nShow = SW_SHOWNORMAL;

				String cmd = CrashCatcher::Get()->mRelaunchCmd;
				String file;

				bool nameQuoted = cmd[0] == '\"';

				int i;
				for (i = (nameQuoted ? 1 : 0); cmd[i] != 0; i++)
				{
					wchar_t c = cmd[i];

					if (((nameQuoted) && (c == '"')) ||
						((!nameQuoted) && (c == ' ')))
					{
						i++;
						break;
					}
					file += cmd[i];
				}

				const char* useParamsPtr = cmd.c_str();
				useParamsPtr += i;
				while (*useParamsPtr == L' ')
					useParamsPtr++;

				auto fileW = UTF8Decode(file);
				shellExecuteInfo.lpFile = fileW.c_str();
				auto paramsW = UTF8Decode(useParamsPtr);
				shellExecuteInfo.lpParameters = paramsW.c_str();

				BOOL success = ::ShellExecuteExW(&shellExecuteInfo);
			}

			CrashCatcher::Get()->mCloseRequested = true;
			gExiting = true;
		}
		else if (hwndCtl == gDebugButtonWindow)
		{
			CrashCatcher::Get()->mDebugError = true;
			gExiting = true;
		}
	}
	break;
	case WM_CLOSE:
		CrashCatcher::Get()->mCloseRequested = true;
		gExiting = true;
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static void ShowErrorDialog(const StringImpl& errorTitle, const StringImpl& errorText, const StringImpl& relaunchCmd)
{
	bool gUseDefaultFonts;

	HINSTANCE gHInstance = ::GetModuleHandle(NULL);

	OSVERSIONINFO aVersionInfo;
	aVersionInfo.dwOSVersionInfoSize = sizeof(aVersionInfo);
	GetVersionEx(&aVersionInfo);

	// Setting fonts on 98 causes weirdo crash things in GDI upon the second crash.
	//  That's no good.
	gUseDefaultFonts = aVersionInfo.dwPlatformId != VER_PLATFORM_WIN32_NT;

	int aHeight = -MulDiv(8, 96, 72);
	gDialogFont = ::CreateFontA(aHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE,
		false, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, "Tahoma");

	aHeight = -MulDiv(10, 96, 72);
	gBoldFont = ::CreateFontA(aHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE,
		false, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, "Tahoma");

	::SetCursor(::LoadCursor(NULL, IDC_ARROW));

	gErrorTitle = errorTitle;
	gErrorText = errorText;

	WNDCLASSW wc;
	wc.style = 0;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hbrBackground = ::GetSysColorBrush(COLOR_BTNFACE);
	wc.hCursor = ::LoadCursor(NULL, IDC_ARROW);
	wc.hIcon = ::LoadIcon(NULL, IDI_ERROR);
	wc.hInstance = gHInstance;
	wc.lpfnWndProc = SEHWindowProc;
	wc.lpszClassName = L"SEHWindow";
	wc.lpszMenuName = NULL;
	RegisterClassW(&wc);

	RECT aRect;
	aRect.left = 0;
	aRect.top = 0;
	aRect.right = 500;
	aRect.bottom = 400;

	DWORD aWindowStyle = WS_CLIPCHILDREN | WS_POPUP | WS_BORDER | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

	RECT windowRect = aRect;
	BOOL worked = AdjustWindowRect(&windowRect, aWindowStyle, FALSE);

	HWND aHWnd = ::CreateWindowExW(0, L"SEHWindow", L"Fatal Error!",
		aWindowStyle,
		64, 64,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		NULL,
		NULL,
		gHInstance,
		0);

	int textHeight = 30;

	HWND aLabelWindow = ::CreateWindowW(L"EDIT",
		L"An unexpected error has occured!",
		WS_VISIBLE | WS_CHILD | ES_MULTILINE | ES_READONLY,
		8, 8,
		aRect.right - 8 - 8,
		textHeight,
		aHWnd,
		NULL,
		gHInstance,
		0);

	int aFontHeight = -MulDiv(9, 96, 72);
	HFONT aBoldArialFont = CreateFontA(aFontHeight, 0, 0, 0, FW_BOLD, 0, 0,
		false, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, "Arial");

	if (!gUseDefaultFonts)
		SendMessage(aLabelWindow, WM_SETFONT, (WPARAM)aBoldArialFont, 0);

	HWND anEditWindow = CreateWindowA("EDIT", errorText.c_str(),
		WS_VISIBLE | WS_CHILD | ES_MULTILINE | WS_BORDER | WS_HSCROLL | WS_VSCROLL | ES_READONLY,
		8, textHeight + 8,
		aRect.right - 8 - 8,
		aRect.bottom - textHeight - 24 - 8 - 8 - 8,
		aHWnd,
		NULL,
		gHInstance,
		0);

	aFontHeight = -MulDiv(8, 96, 72);
	HFONT aCourierNewFont = CreateFontA(aFontHeight, 0, 0, 0, FW_NORMAL, 0, 0,
		false, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
		DEFAULT_PITCH | FF_DONTCARE, "Courier New");
	if (!gUseDefaultFonts)
		SendMessage(anEditWindow, WM_SETFONT, (WPARAM)aCourierNewFont, 0);

	aWindowStyle = WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | BS_PUSHBUTTON;

	//if (mApp == NULL)
		//aWindowStyle |= WS_DISABLED;

//#ifdef _DEBUG
	bool doDebugButton = true;
// #else
// 	bool doDebugButton = false;
// #endif

	//bool canSubmit = mAllowSubmit && !mSubmitHost.empty();
	bool canSubmit = true;
	int aNumButtons = 1 + (doDebugButton ? 1 : 0) + (canSubmit ? 1 : 0);

	int aButtonWidth = (aRect.right - 8 - 8 - (aNumButtons - 1) * 8) / aNumButtons;

	int aCurX = 8;

	if (canSubmit)
	{
		gYesButtonWindow = CreateWindowA("BUTTON", "Save Crash Dump...",
			aWindowStyle,
			aCurX, aRect.bottom - 24 - 8,
			aButtonWidth,
			24,
			aHWnd,
			NULL,
			gHInstance,
			0);
		if (!gUseDefaultFonts)
			SendMessage(gYesButtonWindow, WM_SETFONT, (WPARAM)aBoldArialFont, 0);

		aCurX += aButtonWidth + 8;
	}

	if (doDebugButton)
	{
		gDebugButtonWindow = CreateWindowA("BUTTON", "Debug",
			aWindowStyle,
			aCurX, aRect.bottom - 24 - 8,
			aButtonWidth,
			24,
			aHWnd,
			NULL,
			gHInstance,
			0);
		if (!gUseDefaultFonts)
			SendMessage(gDebugButtonWindow, WM_SETFONT, (WPARAM)aBoldArialFont, 0);

		aCurX += aButtonWidth + 8;
	}

	gNoButtonWindow = CreateWindowA("BUTTON", relaunchCmd.IsEmpty() ? "Close Now" : "Relaunch",
		WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON | BS_PUSHBUTTON,
		aCurX, aRect.bottom - 24 - 8,
		aButtonWidth,
		24,
		aHWnd,
		NULL,
		gHInstance,
		0);

	if (!gUseDefaultFonts)
		SendMessage(gNoButtonWindow, WM_SETFONT, (WPARAM)aBoldArialFont, 0);

	ShowWindow(aHWnd, SW_NORMAL);

	MSG msg;
	while ((GetMessage(&msg, NULL, 0, 0) > 0) && (!gExiting))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	DestroyWindow(aHWnd);

	DeleteObject(gDialogFont);
	DeleteObject(gBoldFont);
	DeleteObject(aBoldArialFont);
	DeleteObject(aCourierNewFont);
}

static bool GetLogicalAddress(void* addr, char* szModule, DWORD len, uintptr& section, uintptr& offset)
{
	MEMORY_BASIC_INFORMATION mbi;

	if (!VirtualQuery(addr, &mbi, sizeof(mbi)))
		return false;

	uintptr hMod = (uintptr)mbi.AllocationBase;

	if (hMod == NULL)
	{
		szModule[0] = 0;
		section = 0;
		offset = 0;
		return false;
	}

	if (!GetModuleFileNameA((HMODULE)hMod, szModule, len))
		return false;

	// Point to the DOS header in memory
	PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)hMod;

	// From the DOS header, find the NT (PE) header
	PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)((uint8*)hMod + pDosHdr->e_lfanew);

	PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION(pNtHdr);

	uintptr rva = (uintptr)addr - hMod; // RVA is offset from module load address

									// Iterate through the section table, looking for the one that encompasses
									// the linear address.
	for (unsigned i = 0; i < pNtHdr->FileHeader.NumberOfSections; i++, pSection++)
	{
		uintptr sectionStart = pSection->VirtualAddress;
		uintptr sectionEnd = sectionStart + BF_MAX(pSection->SizeOfRawData, pSection->Misc.VirtualSize);

		// Is the address in this section???
		if ((rva >= sectionStart) && (rva <= sectionEnd))
		{
			// Yes, address is in the section.  Calculate section and offset,
			// and store in the "section" & "offset" params, which were
			// passed by reference.
			section = i + 1;
			offset = rva - sectionStart;
			return true;
		}
	}

	return false; // Should never get here!
}

static BOOL CALLBACK MyMiniDumpCallback(
	PVOID                            pParam,
	const PMINIDUMP_CALLBACK_INPUT   pInput,
	PMINIDUMP_CALLBACK_OUTPUT        pOutput
)
{
	BOOL bRet = FALSE;


	// Check parameters

	if (pInput == 0)
		return FALSE;

	if (pOutput == 0)
		return FALSE;


	// Process the callbacks

	switch (pInput->CallbackType)
	{
		case IncludeModuleCallback:
		{
			// Include the module into the dump
			bRet = TRUE;
		}
		break;
	case IncludeThreadCallback:
		{
			// Include the thread into the dump
			bRet = TRUE;
		}
		break;
	case ModuleCallback:
		{
			// Does the module have ModuleReferencedByMemory flag set ?

			if (!(pOutput->ModuleWriteFlags & ModuleReferencedByMemory))
			{
				// No, it does not - exclude it

				//wprintf(L"Excluding module: %s \n", pInput->Module.FullPath);

				pOutput->ModuleWriteFlags &= (~ModuleWriteModule);
			}

			bRet = TRUE;
		}
		break;
	case ThreadCallback:
		{
			// Include all thread information into the minidump
			bRet = TRUE;
		}
		break;
	case ThreadExCallback:
		{
			// Include this information
			bRet = TRUE;
		}
		break;
	case MemoryCallback:
		{
			// We do not include any information here -> return FALSE
			bRet = FALSE;
		}
		break;
	case CancelCallback:
		break;
	}

	return bRet;
}


static bool CreateMiniDump(EXCEPTION_POINTERS* pep, const StringImpl& filePath)
{
	// Open the file
	typedef BOOL(*PDUMPFN)(
		HANDLE hProcess,
		DWORD ProcessId,
		HANDLE hFile,
		MINIDUMP_TYPE DumpType,
		PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam,
		PMINIDUMP_USER_STREAM_INFORMATION UserStreamParam,
		PMINIDUMP_CALLBACK_INFORMATION CallbackParam
		);


	HANDLE hFile = CreateFileW(UTF8Decode(filePath).c_str(), GENERIC_READ | GENERIC_WRITE,
		0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	HMODULE h = ::LoadLibrary(L"DbgHelp.dll");
	PDUMPFN pFn = (PDUMPFN)GetProcAddress(h, "MiniDumpWriteDump");
	if (pFn == NULL)
		return false;

	if ((hFile == NULL) || (hFile == INVALID_HANDLE_VALUE))
		return false;

	// Create the minidump

	MINIDUMP_EXCEPTION_INFORMATION mdei;

	mdei.ThreadId = GetCurrentThreadId();
	mdei.ExceptionPointers = pep;
	mdei.ClientPointers = TRUE;

	MINIDUMP_CALLBACK_INFORMATION mci;
	mci.CallbackRoutine = (MINIDUMP_CALLBACK_ROUTINE)MyMiniDumpCallback;
	mci.CallbackParam = 0;

	MINIDUMP_TYPE mdt = (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory);

	CrashCatcher* crashCatcher = CrashCatcher::Get();

	MINIDUMP_USER_STREAM user_info_stream = {
	  0xBEEF00,
	  (ULONG)crashCatcher->mCrashInfo.length(),
	  (void*)crashCatcher->mCrashInfo.c_str()
	};
	MINIDUMP_USER_STREAM_INFORMATION user_stream_info = {
		1,
		&user_info_stream
	};

	BOOL rv = (*pFn)(GetCurrentProcess(), GetCurrentProcessId(),
		hFile, mdt, (pep != 0) ? &mdei : 0, &user_stream_info, &mci);

	// Close the file
	CloseHandle(hFile);
	return true;
}

static String ImageHelpWalk(PCONTEXT theContext, int theSkipCount)
{
	//char aBuffer[2048];
	String aDebugDump;

	STACKFRAME sf;
	memset(&sf, 0, sizeof(sf));

	// Initialize the STACKFRAME structure for the first call.  This is only
	// necessary for Intel CPUs, and isn't mentioned in the documentation.
#ifdef BF64
	sf.AddrPC.Offset = theContext->Rip;
	sf.AddrPC.Mode = AddrModeFlat;
	sf.AddrStack.Offset = theContext->Rsp;
	sf.AddrStack.Mode = AddrModeFlat;
	sf.AddrFrame.Offset = theContext->Rbp;
	sf.AddrFrame.Mode = AddrModeFlat;
#else
	sf.AddrPC.Offset = theContext->Eip;
	sf.AddrPC.Mode = AddrModeFlat;
	sf.AddrStack.Offset = theContext->Esp;
	sf.AddrStack.Mode = AddrModeFlat;
	sf.AddrFrame.Offset = theContext->Ebp;
	sf.AddrFrame.Mode = AddrModeFlat;
#endif

	int aLevelCount = 0;

	CONTEXT ctx = *theContext;

	struct ModuleInfo
	{

	};

	Dictionary<String, ModuleInfo> moduleInfoMap;

	for (;;)
	{
#ifdef BF64
		DWORD machineType = IMAGE_FILE_MACHINE_AMD64;
		PCONTEXT ctxPtr = &ctx;
#else
		DWORD machineType = IMAGE_FILE_MACHINE_I386;
		PCONTEXT ctxPtr = NULL;
#endif

		if (!gStackWalk(machineType, GetCurrentProcess(), GetCurrentThread(),
			&sf, ctxPtr, NULL, gSymFunctionTableAccess, gSymGetModuleBase, 0))
		{
			//DWORD lastErr = GetLastError();
			//sprintf(aBuffer, "StackWalk failed (error %d)\r\n", lastErr);
			//aDebugDump += aBuffer;
			break;
		}

		if ((aLevelCount > 0) && ((sf.AddrFrame.Offset == 0) || (sf.AddrPC.Offset == 0)))
			break;

		if (theSkipCount > 0)
		{
			theSkipCount--;
			continue;
		}

		BYTE symbolBuffer[sizeof(IMAGEHLP_SYMBOL) + 512];
		PIMAGEHLP_SYMBOL pSymbol = (PIMAGEHLP_SYMBOL)symbolBuffer;
		pSymbol->SizeOfStruct = sizeof(symbolBuffer);
		pSymbol->MaxNameLength = 512;

		// Displacement of the input address, relative to the start of the symbol
#ifdef BF64
		DWORD64 symDisplacement = 0;
#else
		DWORD symDisplacement = 0;
#endif

		HANDLE hProcess = GetCurrentProcess();

		char szModule[MAX_PATH];
		szModule[0] = 0;
		uintptr section = 0, offset = 0;

		GetLogicalAddress((PVOID)sf.AddrPC.Offset, szModule, sizeof(szModule), section, offset);

		bool forceFail = false;
		if ((gSymGetSymFromAddr(hProcess, sf.AddrPC.Offset, &symDisplacement, pSymbol)) && (!forceFail))
		{
			char aUDName[256];
			gUnDecorateSymbolName(pSymbol->Name, aUDName, 256,
				UNDNAME_NO_ALLOCATION_MODEL | UNDNAME_NO_ALLOCATION_LANGUAGE |
				UNDNAME_NO_MS_THISTYPE | UNDNAME_NO_ACCESS_SPECIFIERS |
				UNDNAME_NO_THISTYPE | UNDNAME_NO_MEMBER_TYPE |
				UNDNAME_NO_RETURN_UDT_MODEL | UNDNAME_NO_THROW_SIGNATURES |
				UNDNAME_NO_SPECIAL_SYMS);

			String dispName = aUDName;

			if (dispName.StartsWith("_bf::"))
			{
				dispName.Remove(0, 5);
				dispName.Replace("::", ".");
			}

			aDebugDump += StrFormat("%@ %@ %s %hs+%X\r\n",
				sf.AddrFrame.Offset, sf.AddrPC.Offset, GetFileName(szModule).c_str(), dispName.c_str(), symDisplacement);

			DWORD displacement = 0;
#ifdef BF64
			IMAGEHLP_LINE64 lineInfo = { 0 };
			lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
#else
			IMAGEHLP_LINE lineInfo = { 0 };
			lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE);
#endif
			if (gSymGetLineFromAddr(hProcess, sf.AddrPC.Offset, &displacement, &lineInfo))
			{
				aDebugDump += StrFormat("    at %s:%d\r\n", lineInfo.FileName, lineInfo.LineNumber);
			}
		}
		else // No symbol found.  Print out the logical address instead.
		{


// 			ModuleInfo* moduleInfo = NULL;
// 			if (moduleInfoMap.TryAdd(szModule, NULL, &moduleInfo))
// 			{
//
// 			}

			aDebugDump += StrFormat("%@ %@ %04X:%@ %s\r\n", sf.AddrFrame.Offset, sf.AddrPC.Offset, section, offset, GetFileName(szModule).c_str());
		}

		aDebugDump += StrFormat("    Params: %@ %@ %@ %@\r\n", sf.Params[0], sf.Params[1], sf.Params[2], sf.Params[3]);
		aDebugDump += "\r\n";

		aLevelCount++;
	}

	return aDebugDump;
}

static String GetSysInfo()
{
	return "";
}

static String GetVersion(const StringImpl& fileName)
{
	String verStr = "";

	bool bReturn = false;
	DWORD dwReserved = 0;
	DWORD dwBufferSize = GetFileVersionInfoSizeA(fileName.c_str(), &dwReserved);

	struct TRANSARRAY
	{
		WORD wLanguageID;
		WORD wCharacterSet;
	};

	if (dwBufferSize > 0)
	{
		LPVOID pBuffer = (void*)malloc(dwBufferSize);

		if (pBuffer != (void*)NULL)
		{
			UINT        nInfoSize = 0,
			nFixedLength = 0;
			LPSTR       lpVersion = NULL;
			void*       lpFixedPointer;
			TRANSARRAY* lpTransArray;

			GetFileVersionInfoA(fileName.c_str(),
				dwReserved,
				dwBufferSize,
				pBuffer);

			VerQueryValueA(pBuffer,
				"\\VarFileInfo\\Translation",
				&lpFixedPointer,
				&nFixedLength);
			lpTransArray = (TRANSARRAY*)lpFixedPointer;

			String langStr = StrFormat(
				"\\StringFileInfo\\%04x%04x\\FileVersion",
				lpTransArray[0].wLanguageID,
				lpTransArray[0].wCharacterSet);
			VerQueryValueA(pBuffer,
				langStr.c_str(),
				(void**)&lpVersion,
				&nInfoSize);
			if (nInfoSize != 0)
			{
				verStr += "File Version: ";
				verStr += lpVersion;
				verStr += "\r\n";
			}

			langStr = StrFormat(
				"\\StringFileInfo\\%04x%04x\\ProductVersion",
				lpTransArray[0].wLanguageID,
				lpTransArray[0].wCharacterSet);
			VerQueryValueA(pBuffer,
				langStr.c_str(),
				(void**)&lpVersion,
				&nInfoSize);
			if (nInfoSize != 0)
			{
				verStr += "Product Version: ";
				verStr += lpVersion;
				verStr += "\r\n";
			}

			free(pBuffer);
		}
	}

	return verStr;
}

static void DoHandleDebugEvent(LPEXCEPTION_POINTERS lpEP)
{
	auto crashCatcher = CrashCatcher::Get();
	if (crashCatcher->mCrashed)
		return;
	crashCatcher->mCrashed = true;

	HMODULE hMod = GetModuleHandleA(NULL);

	PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)hMod;
	PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)((uint8*)hMod + pDosHdr->e_lfanew);
	bool isCLI = pNtHdr->OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_CUI;
	if (CrashCatcher::Get()->mCrashReportKind == BfpCrashReportKind_GUI)
		isCLI = false;
	else if ((CrashCatcher::Get()->mCrashReportKind == BfpCrashReportKind_Console) || (CrashCatcher::Get()->mCrashReportKind == BfpCrashReportKind_PrintOnly))
		isCLI = true;

	bool hasImageHelp = LoadImageHelp();

	String anErrorTitle;
	String aDebugDump;
	String& crashInfo = CrashCatcher::Get()->mCrashInfo;

	char aBuffer[2048];

	if (isCLI)
		aDebugDump += "**** FATAL APPLICATION ERROR ****\n";

	for (auto func : CrashCatcher::Get()->mCrashInfoFuncs)
		func();

	CHAR path[MAX_PATH];
	GetModuleFileNameA(NULL, path, MAX_PATH);
	crashInfo += "\nExecutable: ";
	crashInfo += path;
	crashInfo += "\r\n";
	crashInfo += GetVersion(path);

	aDebugDump.Append(crashInfo);

	for (int i = 0; i < (int)aDebugDump.length(); i++)
	{
		char c = aDebugDump[i];
		if (c == '\n')
		{
			aDebugDump.Insert(i, '\r');
			i++;
		}
		else if (c == '\t')
		{
			aDebugDump[i] = ' ';
			aDebugDump.Insert(i, ' ');
		}
	}

// 	aDebugDump.Replace("\n", "\r\n");
// 	aDebugDump.Replace("\t", "  ");

	if (!aDebugDump.IsEmpty())
	{
		if (!aDebugDump.EndsWith("\n"))
			aDebugDump += "\r\n";

		aDebugDump += "\r\n";
	}

	WCHAR exeFilePathW[MAX_PATH];
	exeFilePathW[0] = 0;
	::GetModuleFileNameW(hMod, exeFilePathW, MAX_PATH);
	String exeFilePath = UTF8Encode(exeFilePathW);
	String exeDir = GetFileDir(exeFilePath);
	String crashPath = exeDir + "\\CrashDumps";
	if (BfpDirectory_Exists(crashPath.c_str()))
	{
		crashPath += "\\" + GetFileName(exeFilePath);
		crashPath.RemoveToEnd((int)crashPath.length() - 4);
		crashPath += "_";

		time_t curTime = time(NULL);
		auto time_info = localtime(&curTime);
		crashPath += StrFormat("%4d%02d%02d_%02d%02d%02d",
			time_info->tm_year + 1900, time_info->tm_mon + 1, time_info->tm_mday,
			time_info->tm_hour, time_info->tm_min, time_info->tm_sec);
		crashPath += ".dmp";

		if (CreateMiniDump(lpEP, crashPath))
		{
			aDebugDump += StrFormat("Crash minidump saved as %s\n", crashPath.c_str());
		}
	}

	///////////////////////////
	// first name the exception
	char  *szName = NULL;
	for (int i = 0; gMsgTable[i].dwExceptionCode != 0xFFFFFFFF; i++)
	{
		if (gMsgTable[i].dwExceptionCode == lpEP->ExceptionRecord->ExceptionCode)
		{
			szName = gMsgTable[i].szMessage;
			break;
		}
	}

	if (szName != NULL)
	{
		aDebugDump += StrFormat("Exception: %s (code 0x%x) at address %@ in thread %X\r\n",
			szName, lpEP->ExceptionRecord->ExceptionCode,
			lpEP->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
	}
	else
	{
		aDebugDump += StrFormat("Unknown exception: (code 0x%x) at address %@ in thread %X\r\n",
			lpEP->ExceptionRecord->ExceptionCode,
			lpEP->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());
	}

	///////////////////////////////////////////////////////////
	// Get logical address of the module where exception occurs
	uintptr section, offset;
	GetLogicalAddress(lpEP->ExceptionRecord->ExceptionAddress, aBuffer, sizeof(aBuffer), section, offset);


	aDebugDump += StrFormat("Logical Address: %04X:%@\r\n", section, offset);

	aDebugDump += "\r\n";

	anErrorTitle = StrFormat("Exception at %04X:%08X", section, offset);

	String aWalkString;

	if (hasImageHelp)
		aWalkString = ImageHelpWalk(lpEP->ContextRecord, 0);

	/*if (aWalkString.length() == 0)
		aWalkString = IntelWalk(lpEP->ContextRecord, 0);*/

	aDebugDump += aWalkString;

	aDebugDump += "\r\n";

#ifdef BF64
	aDebugDump += StrFormat("RAX:%@ RBX:%@ RCX:%@ RDX:%@ RSI:%@ RDI:%@\r\n",
		lpEP->ContextRecord->Rax, lpEP->ContextRecord->Rbx, lpEP->ContextRecord->Rcx, lpEP->ContextRecord->Rdx, lpEP->ContextRecord->Rsi, lpEP->ContextRecord->Rdi);
	aDebugDump += StrFormat("R8:%@ R9:%@ R10:%@ R11:%@\r\nR12:%@ R13:%@ R14:%@ R15:%@\r\n",
		lpEP->ContextRecord->R8, lpEP->ContextRecord->R9, lpEP->ContextRecord->R10, lpEP->ContextRecord->R11, lpEP->ContextRecord->R12, lpEP->ContextRecord->R13, lpEP->ContextRecord->R14, lpEP->ContextRecord->R15);
	aDebugDump += StrFormat("EIP:%@ ESP:%@ EBP:%@\r\n", lpEP->ContextRecord->Rip, lpEP->ContextRecord->Rsp, lpEP->ContextRecord->Rbp);
	aDebugDump += StrFormat("CS:%04X SS:%04X DS:%04X ES:%04X FS:%04X GS:%04X\r\n", lpEP->ContextRecord->SegCs, lpEP->ContextRecord->SegSs, lpEP->ContextRecord->SegDs, lpEP->ContextRecord->SegEs, lpEP->ContextRecord->SegFs, lpEP->ContextRecord->SegGs);
	aDebugDump += StrFormat("Flags:%@\r\n", lpEP->ContextRecord->EFlags);
#else
	aDebugDump += StrFormat("EAX:%08X EBX:%08X ECX:%08X EDX:%08X ESI:%08X EDI:%08X\r\n",
		lpEP->ContextRecord->Eax, lpEP->ContextRecord->Ebx, lpEP->ContextRecord->Ecx, lpEP->ContextRecord->Edx, lpEP->ContextRecord->Esi, lpEP->ContextRecord->Edi);
	aDebugDump += StrFormat("EIP:%08X ESP:%08X  EBP:%08X\r\n", lpEP->ContextRecord->Eip, lpEP->ContextRecord->Esp, lpEP->ContextRecord->Ebp);
	aDebugDump += StrFormat("CS:%04X SS:%04X DS:%04X ES:%04X FS:%04X GS:%04X\r\n", lpEP->ContextRecord->SegCs, lpEP->ContextRecord->SegSs, lpEP->ContextRecord->SegDs, lpEP->ContextRecord->SegEs, lpEP->ContextRecord->SegFs, lpEP->ContextRecord->SegGs);
	aDebugDump += StrFormat("Flags:%08X\r\n", lpEP->ContextRecord->EFlags);
#endif

	aDebugDump += "\r\n";
	aDebugDump += GetSysInfo();

	/*if (mApp != NULL)
	{
		String aGameSEHInfo = mApp->GetGameSEHInfo();
		if (aGameSEHInfo.length() > 0)
		{
			aDebugDump += "\r\n";
			aDebugDump += aGameSEHInfo;
		}

		mApp->CopyToClipboard(aDebugDump);
	}

	if (hasImageHelp)
		GetSymbolsFromMapFile(aDebugDump);*/

	if (isCLI)
	{
		aDebugDump += "\n";
		//fwrite(aDebugDump.c_str(), 1, aDebugDump.length(), stderr);
		//fflush(stderr);
		DWORD bytesWritten;
		::WriteFile(::GetStdHandle(STD_ERROR_HANDLE), aDebugDump.c_str(), (DWORD)aDebugDump.length(), &bytesWritten, NULL);
	}
	else
		ShowErrorDialog(anErrorTitle, aDebugDump, crashCatcher->mRelaunchCmd);
}

CrashCatcher::CrashCatcher()
{
	mCrashed = false;
	mInitialized = false;
	mExceptionPointers = NULL;
	mPreviousFilter = NULL;
	mDebugError = false;
	mCrashReportKind = BfpCrashReportKind_Default;
	mCloseRequested = false;
}

static long __stdcall SEHFilter(LPEXCEPTION_POINTERS lpExceptPtr)
{
 	OutputDebugStrF("SEH Filter! CraskReportKind:%d\n", CrashCatcher::Get()->mCrashReportKind);

	if (CrashCatcher::Get()->mCrashReportKind == BfpCrashReportKind_None)
	{
		OutputDebugStrF("Silent Exiting\n");
		::TerminateProcess(GetCurrentProcess(), lpExceptPtr->ExceptionRecord->ExceptionCode);
	}

	AutoCrit autoCrit(CrashCatcher::Get()->mBfpCritSect);
	//::ExitProcess();
	//quick_exit(1);

	if (!CrashCatcher::Get()->mCrashed)
	{
		CrashCatcher::Get()->mExceptionPointers = lpExceptPtr;
		//CreateMiniDump(lpExceptPtr);
		DoHandleDebugEvent(lpExceptPtr);
	}

 	//if (!gDebugError)
 		//SetErrorMode(SEM_NOGPFAULTERRORBOX);

	if (CrashCatcher::Get()->mCrashReportKind == BfpCrashReportKind_PrintOnly)
	{
		::TerminateProcess(GetCurrentProcess(), lpExceptPtr->ExceptionRecord->ExceptionCode);
	}

	//return EXCEPTION_CONTINUE_SEARCH;
	return (CrashCatcher::Get()->mCloseRequested) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

//PVECTORED_EXCEPTION_HANDLER(

static long __stdcall VectorExceptionHandler(LPEXCEPTION_POINTERS lpExceptPtr)
{
	OutputDebugStrF("VectorExceptionHandler\n");
	return EXCEPTION_CONTINUE_SEARCH;
}


void CrashCatcher::Init()
{
	if (mInitialized)
		return;

	mPreviousFilter = SetUnhandledExceptionFilter(SEHFilter);
 	OutputDebugStrF("Setting SEH filter %p\n", mPreviousFilter);
	mInitialized = true;

// 	OutputDebugStrF("AddVectoredExceptionHandler 2\n");
// 	AddVectoredExceptionHandler(0, VectorExceptionHandler);
}

void CrashCatcher::Test()
{
	__try
	{
		// all of code normally inside of main or WinMain here...
		int a = 123;
		int b = 0;
		a /= b;
	}
	__except (SEHFilter(GetExceptionInformation()))
	{

	}
}


void CrashCatcher::AddCrashInfoFunc(CrashInfoFunc crashInfoFunc)
{
	AutoCrit autoCrit(mBfpCritSect);
	mCrashInfoFuncs.Add(crashInfoFunc);
}

void CrashCatcher::AddInfo(const StringImpl& str)
{
	AutoCrit autoCrit(mBfpCritSect);
	mCrashInfo.Append(str);
	if (!str.EndsWith('\n'))
		mCrashInfo.Append('\n');
}

void CrashCatcher::Crash(const StringImpl& str)
{
	OutputDebugStrF("CrashCatcher::Crash\n");

	mBfpCritSect.Lock();
	mCrashInfo.Append(str);
	mCrashInfo.Append("\n");

	if (mPreviousFilter == NULL)
	{
		// A little late, but install handler now so we can catch this crash
		Init();
	}

	OutputDebugStr(str);

	mBfpCritSect.Unlock();


	__debugbreak();

	// When we catch the exception information like this, it displays the dump correctly but
	//  the minidump doesn't contain a valid callstack, so we need to rely on SetUnhandledExceptionFilter
	/*__try
	{
		::MessageBoxA(NULL, "A", "B", MB_ICONERROR);
		__debugbreak();
	}
	__except (SEHFilter(GetExceptionInformation()))
	{

	}*/

	for (auto func : CrashCatcher::Get()->mCrashInfoFuncs)
		func();

	exit(1);
}

void CrashCatcher::SetCrashReportKind(BfpCrashReportKind crashReportKind)
{
	mCrashReportKind = crashReportKind;
}

void CrashCatcher::SetRelaunchCmd(const StringImpl& relaunchCmd)
{
	mRelaunchCmd = relaunchCmd;
}

struct CrashCatchMemory
{
public:
	CrashCatcher* mBpManager;
	int mABIVersion;
	int mCounter;
};

#define CRASHCATCH_ABI_VERSION 1

static CrashCatcher* sCrashCatcher = NULL;
CrashCatcher* CrashCatcher::Get()
{
	if (sCrashCatcher != NULL)
		return sCrashCatcher;

	char mutexName[128];
	sprintf(mutexName, "BfCrashCatch_mutex_%d", GetCurrentProcessId());
	char memName[128];
	sprintf(memName, "BfCrashCatch_mem_%d", GetCurrentProcessId());

	auto mutex = ::CreateMutexA(NULL, TRUE, mutexName);
	if (mutex != NULL)
	{
		HANDLE fileMapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, memName);
		if (fileMapping != NULL)
		{
			CrashCatchMemory* sharedMem = (CrashCatchMemory*)MapViewOfFile(fileMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CrashCatchMemory));
			if (sharedMem != NULL)
			{
				if (sharedMem->mABIVersion == 0 && sharedMem->mBpManager == NULL && sharedMem->mCounter == 0)
				{
					sCrashCatcher = new CrashCatcher();
					sharedMem->mBpManager = sCrashCatcher;
					sharedMem->mABIVersion = CRASHCATCH_ABI_VERSION;
					sharedMem->mCounter = 1;
				}
				else if (sharedMem->mABIVersion == CRASHCATCH_ABI_VERSION)
				{
					sharedMem->mCounter++;
					sCrashCatcher = sharedMem->mBpManager;
				}
				::UnmapViewOfFile(sharedMem);
			}
			::CloseHandle(fileMapping);
		}
		else
		{
			fileMapping = ::CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(CrashCatchMemory), memName);
			if (fileMapping != NULL)
			{
				CrashCatchMemory* sharedMem = (CrashCatchMemory*)MapViewOfFile(fileMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CrashCatchMemory));
				if (sharedMem != NULL)
				{
					sCrashCatcher = new CrashCatcher();
					sharedMem->mBpManager = sCrashCatcher;
					sharedMem->mABIVersion = CRASHCATCH_ABI_VERSION;
					sharedMem->mCounter = 1;
					::UnmapViewOfFile(sharedMem);
					::ReleaseMutex(mutex);
				}
				else
				{
					::CloseHandle(fileMapping);
					::CloseHandle(mutex);
				}
			}
		}
	}

	if (sCrashCatcher == NULL)
		sCrashCatcher = new	CrashCatcher();
	return sCrashCatcher;
}

int CrashCatcher::Shutdown()
{
	if (sCrashCatcher == NULL)
		return 0;

	char mutexName[128];
	sprintf(mutexName, "BfCrashCatch_mutex_%d", GetCurrentProcessId());
	char memName[128];
	sprintf(memName, "BfCrashCatch_mem_%d", GetCurrentProcessId());

	auto mutex = ::CreateMutexA(NULL, TRUE, mutexName);
	if (mutex != NULL)
	{
		HANDLE fileMapping = ::OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, memName);
		if (fileMapping != NULL)
		{
			CrashCatchMemory* sharedMem = (CrashCatchMemory*)MapViewOfFile(fileMapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(CrashCatchMemory));
			if (sharedMem != NULL)
			{
				if (sharedMem->mABIVersion == CRASHCATCH_ABI_VERSION && sharedMem->mBpManager != NULL && sharedMem->mCounter > 0)
				{
					sharedMem->mCounter--;

					if (sharedMem->mCounter <= 0)
					{
						delete sharedMem->mBpManager;
						sharedMem->mBpManager = NULL;
						sharedMem->mCounter = 0;
						sharedMem->mABIVersion = 0;
					}
				}

				::UnmapViewOfFile(sharedMem);
			}

			::CloseHandle(fileMapping);
		}

		::CloseHandle(mutex);
	}

	sCrashCatcher = NULL;

	return 1;
}