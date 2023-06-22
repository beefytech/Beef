PUSHD %~dp0

SETLOCAL

@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

set LIBPATH=..\bin
IF NOT EXIST ..\BeefRT\rt\Chars.cpp GOTO SKIPCOPY
set LIBPATH=..\IDE\dist
mkdir src
mkdir src\rt
copy ..\BeefRT\rt\* src\rt\
mkdir src\BeefySysLib
copy ..\BeefySysLib\*.h src\BeefySysLib\
copy ..\BeefySysLib\Common.cpp src\BeefySysLib\
mkdir src\BeefySysLib\platform
copy ..\BeefySysLib\platform\* src\BeefySysLib\platform\
mkdir src\BeefySysLib\platform\posix
copy ..\BeefySysLib\platform\posix\* src\BeefySysLib\platform\posix\
mkdir src\BeefySysLib\platform\wasm
copy ..\BeefySysLib\platform\wasm\* src\BeefySysLib\platform\wasm\
mkdir src\BeefySysLib\util
copy ..\BeefySysLib\util\* src\BeefySysLib\util\
mkdir src\BeefySysLib\third_party
mkdir src\BeefySysLib\third_party\utf8proc
copy ..\BeefySysLib\third_party\utf8proc\* src\BeefySysLib\third_party\utf8proc
mkdir src\BeefySysLib\third_party\stb
copy ..\BeefySysLib\third_party\stb\* src\BeefySysLib\third_party\stb
mkdir src\BeefySysLib\third_party\putty
copy ..\BeefySysLib\third_party\putty\* src\BeefySysLib\third_party\putty
:SKIPCOPY

IF "%1" EQU "setup" GOTO SUCCESS

call emcc src\rt\Chars.cpp src\rt\Math.cpp src\rt\Object.cpp src\rt\Thread.cpp src\rt\Internal.cpp src\BeefySysLib\platform\wasm\WasmCommon.cpp src\BeefySysLib\Common.cpp src\BeefySysLib\util\String.cpp src\BeefySysLib\util\UTF8.cpp src\BeefySysLib\third_party\utf8proc\utf8proc.c src\BeefySysLib\third_party\putty\wildcard.c -Isrc\ -Isrc\BeefySysLib -Isrc\BeefySysLib\platform\wasm -g -DBF_DISABLE_FFI -c
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
call emar r %LIBPATH%\Beef042RT32_wasm.a Common.o Internal.o Chars.o Math.o Object.o String.o Thread.o UTF8.o utf8proc.o wildcard.o WasmCommon.o
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
call emcc src\rt\Chars.cpp src\rt\Math.cpp src\rt\Object.cpp src\rt\Thread.cpp src\rt\Internal.cpp src\BeefySysLib\platform\wasm\WasmCommon.cpp src\BeefySysLib\Common.cpp src\BeefySysLib\util\String.cpp src\BeefySysLib\util\UTF8.cpp src\BeefySysLib\third_party\utf8proc\utf8proc.c src\BeefySysLib\third_party\putty\wildcard.c -Isrc\ -Isrc\BeefySysLib -Isrc\BeefySysLib\platform\wasm -g -DBF_DISABLE_FFI -c -pthread
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
call emar r %LIBPATH%\Beef042RT32_wasm_pthread.a Common.o Internal.o Chars.o Math.o Object.o String.o Thread.o UTF8.o utf8proc.o wildcard.o WasmCommon.o
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
