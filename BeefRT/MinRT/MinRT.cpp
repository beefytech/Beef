#define SUPPORT_TLS

#ifdef _WIN64
typedef unsigned __int64 size_t;
typedef __int64          ptrdiff_t;
typedef __int64          intptr_t;
#else
typedef unsigned int     size_t;
typedef int              ptrdiff_t;
typedef int              intptr_t;
#endif
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef char* LPSTR;
typedef char* LPBYTE;

typedef void* HINSTANCE;
typedef void* HANDLE;
typedef void* HMODULE;

#define WINAPI __stdcall
#define NTAPI __stdcall
typedef int	BOOL;
#define FALSE					0
#define TRUE					1

#define NULL 0
#define _CONSOLE_APP			1
#define _GUI_APP				2
#define _O_RDONLY				0x0000  // open for reading only
#define _O_WRONLY				0x0001  // open for writing only
#define STD_INPUT_HANDLE		-10
#define STD_OUTPUT_HANDLE		-11
#define STD_ERROR_HANDLE		-12
#define SW_SHOWDEFAULT			10
#define STARTF_USESHOWWINDOW	0x00000001
#define DLL_THREAD_ATTACH		2

#define _MCW_PC					0x00030000              // Precision Control
#define _PC_53					0x00010000              //     53 bits

struct FILE;
typedef char* va_list;
typedef int errno_t;
struct _startupinfo
{
	int newmode;
};

struct STARTUPINFOA 
{
	DWORD    cb;
	LPSTR   lpReserved;
	LPSTR   lpDesktop;
	LPSTR   lpTitle;
	DWORD   dwX;
	DWORD   dwY;
	DWORD   dwXSize;
	DWORD   dwYSize;
	DWORD   dwXCountChars;
	DWORD   dwYCountChars;
	DWORD   dwFillAttribute;
	DWORD   dwFlags;
	WORD    wShowWindow;
	WORD    cbReserved2;
	LPBYTE  lpReserved2;
	HANDLE  hStdInput;
	HANDLE  hStdOutput;
	HANDLE  hStdError;
};

typedef void(__cdecl* VoidFunc)();
typedef int(__cdecl* IntFunc)();

char** gArgV = NULL;
char** gEnv = NULL;
int gArgC = 0;
bool gIsGUIApp = false;
_startupinfo gStartupInfo = { 0 };
extern "C" int _fltused = 0;
extern "C" int _tls_index = 0;
extern "C"
{
	extern char* _acmdln;
	extern wchar_t* _wcmdln;
}
//extern "C" extern void* _image_base;
extern "C" extern void* __ImageBase;

extern "C" int main(int argc, char** argv, char** env);
extern "C" int __stdcall WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, char* lpCmdLine, int nCmdShow);
extern "C" int __getmainargs(int* _Argc, char*** _Argv, char *** _Env, int _DoWildCard, _startupinfo* _StartInfo);
extern "C" void __set_app_type(int at);
extern "C" void exit(int exitCode);
extern "C" void _exit(int exitCode);
extern "C" void _cexit();
extern "C" char* _strdup(const char* str);
extern "C" int _open_osfhandle(intptr_t osfhandle, int flags);
extern "C" intptr_t __stdcall GetStdHandle(int nStdHandle);
extern "C" FILE* _fdopen(int fd, const char *mode);
extern "C" void setbuf(FILE *stream, char *buffer);
extern "C" int printf_s(const char* str, va_list arglist);
extern "C" int fprintf_s(FILE* fp, const char* str, va_list arglist);
extern "C" void free(void* ptr);
extern "C" void _clearfp();
void __cdecl terminate();
extern "C" float floorf(float val);
extern "C" double floor(double val);
extern "C" int _finite(double val);
extern "C" int _finitef(float val);

extern "C" void _initterm(VoidFunc* begin, VoidFunc* end);
extern "C" int _initterm_e(IntFunc* begin, IntFunc * end);
extern "C" errno_t _controlfp_s(unsigned int *currentControl, unsigned int newControl, unsigned int mask);

extern "C" char* __stdcall GetCommandLineA();
extern "C" wchar_t* __stdcall GetCommandLineW();
extern "C" void __stdcall GetStartupInfoA(STARTUPINFOA* lpStartupInfo);
extern "C" HMODULE LoadLibraryA(char* lpFileName);
extern "C" void* GetProcAddress(HMODULE mod, const char* name);

//extern "C" void __stdcall OutputDebugStringA(char* ptr);

#pragma section(".CRT$XCA",    long, read) // First C++ Initializer
#pragma section(".CRT$XCAA",   long, read) // Startup C++ Initializer
#pragma section(".CRT$XCZ",    long, read) // Last C++ Initializer

#pragma section(".CRT$XDA",    long, read) // First Dynamic TLS Initializer
#pragma section(".CRT$XDZ",    long, read) // Last Dynamic TLS Initializer

#pragma section(".CRT$XIA",    long, read) // First C Initializer
#pragma section(".CRT$XIAA",   long, read) // Startup C Initializer
#pragma section(".CRT$XIAB",   long, read) // PGO C Initializer
#pragma section(".CRT$XIAC",   long, read) // Post-PGO C Initializer
#pragma section(".CRT$XIC",    long, read) // CRT C Initializers
#pragma section(".CRT$XIYA",   long, read) // VCCorLib Threading Model Initializer
#pragma section(".CRT$XIYAA",  long, read) // XAML Designer Threading Model Override Initializer
#pragma section(".CRT$XIYB",   long, read) // VCCorLib Main Initializer
#pragma section(".CRT$XIZ",    long, read) // Last C Initializer

#pragma section(".CRT$XLA",    long, read) // First Loader TLS Callback
#pragma section(".CRT$XLC",    long, read) // CRT TLS Constructor
#pragma section(".CRT$XLD",    long, read) // CRT TLS Terminator
#pragma section(".CRT$XLZ",    long, read) // Last Loader TLS Callback

#pragma section(".CRT$XPA",    long, read) // First Pre-Terminator
#pragma section(".CRT$XPB",    long, read) // CRT ConcRT Pre-Terminator
#pragma section(".CRT$XPX",    long, read) // CRT Pre-Terminators
#pragma section(".CRT$XPXA",   long, read) // CRT stdio Pre-Terminator
#pragma section(".CRT$XPZ",    long, read) // Last Pre-Terminator

#pragma section(".CRT$XTA",    long, read) // First Terminator
#pragma section(".CRT$XTZ",    long, read) // Last Terminator

#pragma section(".CRTMA$XCA",  long, read) // First Managed C++ Initializer
#pragma section(".CRTMA$XCZ",  long, read) // Last Managed C++ Initializer

#pragma section(".CRTVT$XCA",  long, read) // First Managed VTable Initializer
#pragma section(".CRTVT$XCZ",  long, read) // Last Managed VTable Initializer

#pragma section(".rdata$T",    long, read)

#pragma section(".rtc$IAA",    long, read) // First RTC Initializer
#pragma section(".rtc$IZZ",    long, read) // Last RTC Initializer

#pragma section(".rtc$TAA",    long, read) // First RTC Terminator
#pragma section(".rtc$TZZ",    long, read) // Last RTC Terminator

#ifdef SUPPORT_TLS
#pragma section(".tls",long,read,write)
#pragma section(".tls$",long,read,write)
#pragma section(".tls$ZZZ",long,read,write)
#endif

static int __cdecl pre_c_initialization()
{
#ifdef MINRT_CONSOLE
	__set_app_type(_CONSOLE_APP);
#else
	__set_app_type(_GUI_APP);
#endif

#ifdef _M_IX86
	_clearfp();
#endif
	//__setusermatherr		

#ifdef _M_IX86
	_controlfp_s(nullptr, _PC_53, _MCW_PC); // _initialize_default_precision()
#endif

	return 0;
}

typedef void (NTAPI* PIMAGE_TLS_CALLBACK)(void* DllHandle, DWORD Reason, void* Reserved);

#define _CRTALLOC(x) __declspec(allocate(x))
_CRTALLOC(".CRT$XIAA") static IntFunc pre_c_initializer = pre_c_initialization;

extern "C" _CRTALLOC(".CRT$XIA") IntFunc __xi_a[] = { nullptr }; // C initializers (first)
extern "C" _CRTALLOC(".CRT$XIZ") IntFunc __xi_z[] = { nullptr }; // C initializers (last)
extern "C" _CRTALLOC(".CRT$XCA") VoidFunc __xc_a[] = { nullptr }; // C++ initializers (first)
extern "C" _CRTALLOC(".CRT$XCZ") VoidFunc __xc_z[] = { nullptr }; // C++ initializers (last)
extern "C" _CRTALLOC(".CRT$XPA") VoidFunc __xp_a[] = { nullptr }; // C pre-terminators (first)
extern "C" _CRTALLOC(".CRT$XPZ") VoidFunc __xp_z[] = { nullptr }; // C pre-terminators (last)
extern "C" _CRTALLOC(".CRT$XTA") VoidFunc __xt_a[] = { nullptr }; // C terminators (first)
extern "C" _CRTALLOC(".CRT$XTZ") VoidFunc __xt_z[] = { nullptr }; // C terminators (last)

extern "C" _CRTALLOC(".CRT$XDA") VoidFunc __xd_a = { nullptr };
extern "C" _CRTALLOC(".CRT$XDZ") VoidFunc __xd_z = { nullptr };
/* TLS raw template data start and end.
We use here pointer-types for start/end so that tls-data remains
aligned on pointer-size-width.  This seems to be required for
pe-loader. */

#ifdef SUPPORT_TLS
extern "C" _CRTALLOC(".tls$") char* _tls_start = NULL;
extern "C" _CRTALLOC(".tls$ZZZ") char* _tls_end = NULL;
#endif

struct IMAGE_TLS_DIRECTORY 
{
	void* StartAddressOfRawData;
	void* EndAddressOfRawData;
	void* AddressOfIndex;         // PDWORD
	void* AddressOfCallBacks;     // PIMAGE_TLS_CALLBACK *;
	DWORD SizeOfZeroFill;
	union {
		DWORD Characteristics;
		struct {
			DWORD Reserved0 : 20;
			DWORD Alignment : 4;
			DWORD Reserved1 : 8;
		} DUMMYSTRUCTNAME;
	} DUMMYUNIONNAME;
};

#ifdef SUPPORT_TLS
BOOL WINAPI __dyn_tls_init(HANDLE hDllHandle, DWORD dwReason, void* lpreserved)
{		
	VoidFunc* ps = &__xd_a;
	ps++;
	for (; ps != &__xd_z; ps++)
	{		
		if (*ps != NULL)
			(*ps)();
	}

	return TRUE;
}

const PIMAGE_TLS_CALLBACK __dyn_tls_init_callback = (const PIMAGE_TLS_CALLBACK)__dyn_tls_init;
extern "C" _CRTALLOC(".CRT$XLC") PIMAGE_TLS_CALLBACK __xl_c = (PIMAGE_TLS_CALLBACK)__dyn_tls_init;
extern "C" _CRTALLOC(".CRT$XLA") PIMAGE_TLS_CALLBACK __xl_a = 0;
extern "C" _CRTALLOC(".CRT$XLZ") PIMAGE_TLS_CALLBACK __xl_z = 0;

//extern "C" _CRTALLOC(".tls") IMAGE_TLS_DIRECTORY _tls_used = {

extern "C" IMAGE_TLS_DIRECTORY _tls_used = {
	&_tls_start, &_tls_end,
	&_tls_index, (&__xl_a + 1),
	0, 0
};
#endif


#pragma comment(linker, "/merge:.CRT=.rdata")

FILE* gStdFiles[3];
FILE* GetStdFile(int stdHandleId, bool isIn)
{
// 	int fd = _open_osfhandle(GetStdHandle(stdHandleId), 0);
// 	if (fd == -1)
// 		return NULL;
// 	FILE* fp = _fdopen(fd, isIn ? "r" : "w");
 	
 	FILE* fp = _fdopen(stdHandleId, isIn ? "r" : "w");

	setbuf(fp, NULL);
	return fp;
}

int Init()
{
	/*gStdFiles[0] = GetStdFile(STD_INPUT_HANDLE, true);
	gStdFiles[1] = GetStdFile(STD_OUTPUT_HANDLE, false);
	gStdFiles[2] = GetStdFile(STD_ERROR_HANDLE, false);*/

	gStdFiles[0] = GetStdFile(0, true);
	gStdFiles[1] = GetStdFile(1, false);
	gStdFiles[2] = GetStdFile(2, false);

	int result = _initterm_e(__xi_a, __xi_z);
	if (result != 0)
		return result;
	_initterm(__xc_a, __xc_z);

#ifdef __dyn_tls_init
	if (__dyn_tls_init_callback != NULL)
		__dyn_tls_init(NULL, DLL_THREAD_ATTACH, NULL);
#endif


	return 0;
}

void Finish(int result)
{
	_initterm(__xp_a, __xp_z);
	_initterm(__xt_a, __xt_z);

	exit(result);
	_cexit();	
}

#ifdef MINRT_CONSOLE
extern "C" int mainCRTStartup()
{
	gIsGUIApp = false;

	int result = Init();
	if (result != 0)
		return result;

	__getmainargs(&gArgC, &gArgV, &gEnv, 0, &gStartupInfo);
	result = main(gArgC, gArgV, gEnv);
	
	Finish(result);
	return result;
}
#else
extern "C" int WinMainCRTStartup()
{
	STARTUPINFOA startupInfo;
	GetStartupInfoA(&startupInfo);

	int result = Init();
	if (result != 0)
		return result;

	HINSTANCE hInstance = (HINSTANCE)&__ImageBase;
	char* cmdLine = GetCommandLineA();

	if (cmdLine)
	{
		bool inDoubleQuote = false;
		while (*cmdLine > ' ' || (*cmdLine && inDoubleQuote))
		{
			if (*cmdLine == '"')
				inDoubleQuote = !inDoubleQuote;
			++cmdLine;
		}
		while (*cmdLine && (*cmdLine <= ' '))
			cmdLine++;
	}

	result = WinMain(hInstance, NULL, cmdLine, startupInfo.dwFlags & STARTF_USESHOWWINDOW ?
		startupInfo.wShowWindow : SW_SHOWDEFAULT);

	Finish(result);
	return result;
}
#endif

void __cdecl operator delete(void* val, unsigned __int64 size)
{
	free(val);
}

void __cdecl operator delete(void* val, int a, const char* name, int line)
{
	free(val);
}

extern "C" __declspec(dllexport) void* __cdecl _set_purecall_handler(void*  _Handler)
{
	return 0;
}

extern "C" __declspec(dllexport) void* __cdecl _set_invalid_parameter_handler(void*  _Handler)
{
	return 0;
}

extern "C" __declspec(dllexport) unsigned int __cdecl _set_abort_behavior(unsigned int _Flags, unsigned int _Mask)
{
	return 0;
}

extern "C" int _vsnprintf_s_l(
	char *buffer,
	size_t sizeOfBuffer,
	size_t count,
	const char *format,
	void* locale,
	va_list argptr
);

typedef int (PROC_vsnprintf_s_l)(
	char *buffer,
	size_t sizeOfBuffer,
	size_t count,
	const char *format,
	void* locale,
	va_list argptr
);

typedef int (PROC_vfprintf_l)(
	FILE *stream,
	const char *format,
	void* locale,
	va_list argptr
);

 static HMODULE gCrtLib = NULL;
 void* GetCrtProc(const char* name)
 {
 	if (gCrtLib == NULL)
 		gCrtLib = ::LoadLibraryA("msvcrt.dll");
 	return ::GetProcAddress(gCrtLib, name);
 }

extern "C" __declspec(dllexport) int __cdecl __stdio_common_vsprintf(
	unsigned __int64 const options,
	char*            const buffer,
	size_t           const buffer_count,
	char const*      const format,
	void*			const locale,
	va_list          const arglist
)
{	
	static PROC_vsnprintf_s_l* p_vsnprintf_s_l = (PROC_vsnprintf_s_l*)GetCrtProc("_vsnprintf_s_l");
	return p_vsnprintf_s_l(buffer, buffer_count, -1, format, locale, arglist);
}

extern "C" __declspec(dllexport) int __cdecl __stdio_common_vfprintf(
	unsigned __int64 const options,
	FILE*            const stream,
	char const*      const format,
	void*        const locale,
	va_list          const arglist
)
{	
	static PROC_vfprintf_l* p_vfprintf_l = (PROC_vfprintf_l*)GetCrtProc("_vfprintf_l");
	return p_vfprintf_l(stream, format, locale, arglist);	
}

extern "C" __declspec(dllexport) FILE* __cdecl __acrt_iob_func(unsigned const id)
{
	return gStdFiles[id];
}

extern "C" __declspec(noreturn) void __cdecl __std_terminate()
{
	terminate();
}

extern "C" __declspec(dllexport) float roundf(float val)
{	
	if (!_finitef(val))
	{
		return val;
	}
	else if (val >= 0) 
	{
		float result = floorf(val);
		if (result - val <= -0.5f)
			result += 1.0f;
		return result;
	}
	else 
	{
		float result = floorf(-val);
		if (result + val <= -0.5f)
			result += 1.0f;
		return -result;
	}
}

extern "C" __declspec(dllexport) double round(double val)
{
	if (!_finite(val))
	{
		return val;
	}
	else if (val >= 0)
	{
		double result = floor(val);
		if (result - val <= -0.5)
			result += 1.0;
		return result;
	}
	else
	{
		double result = floor(-val);
		if (result + val <= -0.5)
			result += 1.0;
		return -result;
	}
}

extern "C" __declspec(dllexport) char* strdup(const char* str)
{
	return _strdup(str);
}

extern "C" __declspec(dllexport) void __CxxFrameHandler4()
{
}

// if (TSS > _Init_thread_epoch) {
//   _Init_thread_header(&TSS);
// Test our bit from the guard variable.	     
//   if (TSS == -1) {
//     ... initialize the object ...;
//     _Init_thread_footer(&TSS);
//   }
// }