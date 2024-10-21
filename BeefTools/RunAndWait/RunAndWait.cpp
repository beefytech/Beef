#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <string>
#include <psapi.h>
#include <shlobj.h>
#include <algorithm>
#include <stdio.h>
#include <time.h>

static bool CreatePipeWithSecurityAttributes(HANDLE& hReadPipe, HANDLE& hWritePipe, SECURITY_ATTRIBUTES* lpPipeAttributes, int nSize)
{
	hReadPipe = 0;
	hWritePipe = 0;
	bool ret = ::CreatePipe(&hReadPipe, &hWritePipe, lpPipeAttributes, nSize);
	if (!ret || (hReadPipe == INVALID_HANDLE_VALUE) || (hWritePipe == INVALID_HANDLE_VALUE))
		return false;
	return true;
}

// Using synchronous Anonymous pipes for process input/output redirection means we would end up 
// wasting a worker threadpool thread per pipe instance. Overlapped pipe IO is desirable, since 
// it will take advantage of the NT IO completion port infrastructure. But we can't really use 
// Overlapped I/O for process input/output as it would break Console apps (managed Console class 
// methods such as WriteLine as well as native CRT functions like printf) which are making an
// assumption that the console standard handles (obtained via GetStdHandle()) are opened
// for synchronous I/O and hence they can work fine with ReadFile/WriteFile synchrnously!
bool CreatePipe(HANDLE& parentHandle, HANDLE& childHandle, bool parentInputs)
{
	SECURITY_ATTRIBUTES securityAttributesParent = { 0 };
	securityAttributesParent.bInheritHandle = 1;

	HANDLE hTmp = INVALID_HANDLE_VALUE;
	if (parentInputs)
		CreatePipeWithSecurityAttributes(childHandle, hTmp, &securityAttributesParent, 0);
	else
		CreatePipeWithSecurityAttributes(hTmp, childHandle, &securityAttributesParent, 0);

	HANDLE dupHandle = 0;

	// Duplicate the parent handle to be non-inheritable so that the child process 
	// doesn't have access. This is done for correctness sake, exact reason is unclear.
	// One potential theory is that child process can do something brain dead like 
	// closing the parent end of the pipe and there by getting into a blocking situation
	// as parent will not be draining the pipe at the other end anymore. 
	if (!::DuplicateHandle(GetCurrentProcess(), hTmp,
		GetCurrentProcess(), &dupHandle,
		0, false, DUPLICATE_SAME_ACCESS))
	{
		return false;
	}

	parentHandle = dupHandle;

	if (hTmp != INVALID_HANDLE_VALUE)
		::CloseHandle(hTmp);

	return true;
}

struct ProcParams
{
	HANDLE mReadHandle;
	HANDLE mWriteHandle;
};

DWORD WINAPI ReadProc(void* lpThreadParameter)
{
	ProcParams* procParams = (ProcParams*)lpThreadParameter;
	while (true)
	{
		char buffer[2048];
		DWORD bytesRead = 0;
		if (::ReadFile(procParams->mReadHandle, buffer, (DWORD)2048, &bytesRead, NULL))
		{
			DWORD bytesWritten;
			::WriteFile(procParams->mWriteHandle, buffer, bytesRead, &bytesWritten, NULL);
		}
		else
		{
			int err = GetLastError();
			break;
		}
	}

	return 0;
}

int main()
{	
	char* cmdLineStr = ::GetCommandLineA();
	
	DWORD flags = CREATE_DEFAULT_ERROR_MODE;
	void* envPtr = NULL;
		
	char* useCmdLineStr = cmdLineStr;

	if (cmdLineStr[0] != 0)
	{
		bool nameQuoted = cmdLineStr[0] == '\"';

		std::string passedName;
		int i;
		for (i = (nameQuoted ? 1 : 0); cmdLineStr[i] != 0; i++)
		{
			wchar_t c = cmdLineStr[i];

			if (((nameQuoted) && (c == '"')) ||
				((!nameQuoted) && (c == ' ')))
			{
				i++;
				break;
			}
			passedName += cmdLineStr[i];
		}

		useCmdLineStr += i;
		while (*useCmdLineStr == L' ')
			useCmdLineStr++;
	}
	
	std::string cmdLine = useCmdLineStr;	
	
	for (int pass = 0; pass < 2; pass++)
	{
		PROCESS_INFORMATION processInfo;

		STARTUPINFOA si;
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		memset(&processInfo, 0, sizeof(processInfo));

		si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

		HANDLE stdOut;
		CreatePipe(stdOut, si.hStdOutput, false);

		HANDLE stdErr;
		CreatePipe(stdErr, si.hStdError, false);

		si.dwFlags = STARTF_USESTDHANDLES;

		DWORD startTick = GetTickCount();
		BOOL worked = CreateProcessA(NULL, (char*)cmdLine.c_str(), NULL, NULL, TRUE,
			flags, envPtr, NULL, &si, &processInfo);

		::CloseHandle(si.hStdOutput);
		::CloseHandle(si.hStdError);

		if (!worked)
			return 1;

		DWORD threadId;
		ProcParams stdOutParams = { stdOut, GetStdHandle(STD_OUTPUT_HANDLE) };
		HANDLE stdOutThread = ::CreateThread(NULL, (SIZE_T)128 * 1024, (LPTHREAD_START_ROUTINE)ReadProc, (void*)&stdOutParams, 0, &threadId);

		ProcParams stdErrParams = { stdErr, GetStdHandle(STD_ERROR_HANDLE) };
		HANDLE stdErrThread = ::CreateThread(NULL, (SIZE_T)128 * 1024, (LPTHREAD_START_ROUTINE)ReadProc, (void*)&stdErrParams, 0, &threadId);

		while (true)
		{
			if (::WaitForSingleObject(processInfo.hProcess, 20) == WAIT_OBJECT_0)
				break;
		}
		::WaitForSingleObject(stdOutThread, INFINITE);
		::WaitForSingleObject(stdErrThread, INFINITE);

		DWORD exitCode = 0;
		::GetExitCodeProcess(processInfo.hProcess, &exitCode);

		printf("Exit code: %d\n", exitCode);
		if ((exitCode == 0) || (pass == 1))
			return exitCode;
		
		printf("FAILED! Starting second attempt.\n");
	}

	return 0;
}