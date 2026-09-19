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

REM build_wasm.bat          build both libraries, optimized (-O2)
REM build_wasm.bat debug    build both unoptimized with debug info (-O0 -g), for debugging
REM                         the runtime itself
REM build_wasm.bat setup    stage the sources only
REM Debug and Release wasm32 configs both link the same Beef042RT32_wasm.a, so whichever
REM was built last is what every wasm32 program gets.
IF "%1" EQU "setup" GOTO SUCCESS
set FLAGS=-O2
IF "%1" EQU "debug" set FLAGS=-O0 -g
IF "%1" EQU "" GOTO BUILD
IF "%1" EQU "release" GOTO BUILD
IF "%1" EQU "debug" GOTO BUILD
@ECHO usage: build_wasm.bat [release ^| debug ^| setup]
@POPD
@EXIT /b 1
:BUILD

call emcc src\rt\Chars.cpp src\rt\Math.cpp src\rt\Object.cpp src\rt\Thread.cpp src\rt\Internal.cpp src\rt\zmij.c src\BeefySysLib\platform\wasm\WasmCommon.cpp src\BeefySysLib\Common.cpp src\BeefySysLib\util\String.cpp src\BeefySysLib\util\Hash.cpp src\BeefySysLib\util\UTF8.cpp src\BeefySysLib\third_party\utf8proc\utf8proc.c src\BeefySysLib\third_party\putty\wildcard.c -Isrc\ -Isrc\BeefySysLib -Isrc\BeefySysLib\platform\wasm %FLAGS% -DBF_DISABLE_FFI -c
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
call emar r %LIBPATH%\Beef042RT32_wasm.a Common.o Internal.o Chars.o Math.o Object.o String.o Thread.o Hash.o UTF8.o utf8proc.o wildcard.o WasmCommon.o zmij.o
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
call emcc src\rt\Chars.cpp src\rt\Math.cpp src\rt\Object.cpp src\rt\Thread.cpp src\rt\Internal.cpp src\rt\zmij.c src\BeefySysLib\platform\wasm\WasmCommon.cpp src\BeefySysLib\Common.cpp src\BeefySysLib\util\String.cpp src\BeefySysLib\util\Hash.cpp src\BeefySysLib\util\UTF8.cpp src\BeefySysLib\third_party\utf8proc\utf8proc.c src\BeefySysLib\third_party\putty\wildcard.c -Isrc\ -Isrc\BeefySysLib -Isrc\BeefySysLib\platform\wasm %FLAGS% -DBF_DISABLE_FFI -c -pthread
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
call emar r %LIBPATH%\Beef042RT32_wasm_pthread.a Common.o Internal.o Chars.o Math.o Object.o String.o Thread.o Hash.o UTF8.o utf8proc.o wildcard.o WasmCommon.o zmij.o
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
