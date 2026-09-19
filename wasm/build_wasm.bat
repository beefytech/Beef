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

REM Emits the runtime each project links (BuildContext looks for them before a wasm32 link):
REM   Beef042RT32_wasm.a            optimized (-O2)
REM   Beef042RT32_wasm_pthread.a    optimized, threads enabled (Wasm options)
REM   Beef042RT32_wasm_d.a          debug (-O0 -g), for Beef Lib Type "DynamicDebug"
REM   Beef042RT32_wasm_pthread_d.a
REM build_wasm.bat          build all four
REM build_wasm.bat release  build the optimized two only
REM build_wasm.bat debug    build the debug two only
REM build_wasm.bat setup    stage the sources only
IF "%1" EQU "setup" GOTO SUCCESS
IF "%1" EQU "" GOTO BUILDRELEASE
IF "%1" EQU "release" GOTO BUILDRELEASE
IF "%1" EQU "debug" GOTO BUILDDEBUG
@ECHO usage: build_wasm.bat [release ^| debug ^| setup]
@POPD
@EXIT /b 1

:BUILDRELEASE
call :BUILDKIND "-O2" ""
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
IF "%1" EQU "release" GOTO SUCCESS
:BUILDDEBUG
call :BUILDKIND "-O0 -g" "_d"
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%

REM Builds one kind: %1 = compile flags, %2 = archive name suffix
:BUILDKIND
call emcc src\rt\Chars.cpp src\rt\Math.cpp src\rt\Object.cpp src\rt\Thread.cpp src\rt\Internal.cpp src\rt\zmij.c src\BeefySysLib\platform\wasm\WasmCommon.cpp src\BeefySysLib\Common.cpp src\BeefySysLib\util\String.cpp src\BeefySysLib\util\Hash.cpp src\BeefySysLib\util\UTF8.cpp src\BeefySysLib\third_party\utf8proc\utf8proc.c src\BeefySysLib\third_party\putty\wildcard.c -Isrc\ -Isrc\BeefySysLib -Isrc\BeefySysLib\platform\wasm %~1 -DBF_DISABLE_FFI -c
@IF %ERRORLEVEL% NEQ 0 EXIT /b %ERRORLEVEL%
call emar r %LIBPATH%\Beef042RT32_wasm%~2.a Common.o Internal.o Chars.o Math.o Object.o String.o Thread.o Hash.o UTF8.o utf8proc.o wildcard.o WasmCommon.o zmij.o
@IF %ERRORLEVEL% NEQ 0 EXIT /b %ERRORLEVEL%
call emcc src\rt\Chars.cpp src\rt\Math.cpp src\rt\Object.cpp src\rt\Thread.cpp src\rt\Internal.cpp src\rt\zmij.c src\BeefySysLib\platform\wasm\WasmCommon.cpp src\BeefySysLib\Common.cpp src\BeefySysLib\util\String.cpp src\BeefySysLib\util\Hash.cpp src\BeefySysLib\util\UTF8.cpp src\BeefySysLib\third_party\utf8proc\utf8proc.c src\BeefySysLib\third_party\putty\wildcard.c -Isrc\ -Isrc\BeefySysLib -Isrc\BeefySysLib\platform\wasm %~1 -DBF_DISABLE_FFI -c -pthread
@IF %ERRORLEVEL% NEQ 0 EXIT /b %ERRORLEVEL%
call emar r %LIBPATH%\Beef042RT32_wasm_pthread%~2.a Common.o Internal.o Chars.o Math.o Object.o String.o Thread.o Hash.o UTF8.o utf8proc.o wildcard.o WasmCommon.o zmij.o
@IF %ERRORLEVEL% NEQ 0 EXIT /b %ERRORLEVEL%
@EXIT /b 0
