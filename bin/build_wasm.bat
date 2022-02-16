PUSHD %~dp0..\

mkdir wasm
cd wasm
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

IF NOT EXIST ..\BeefRT\rt\Chars.cpp GOTO SKIPCOPY
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
:SKIPCOPY

IF "%1" EQU "setup" GOTO SUCCESS

call emcc src\rt\Chars.cpp src\rt\Math.cpp src\rt\Object.cpp src\rt\Thread.cpp src\rt\Internal.cpp src\BeefySysLib\platform\wasm\WasmCommon.cpp src\BeefySysLib\Common.cpp src\BeefySysLib\util\String.cpp src\BeefySysLib\util\UTF8.cpp src\BeefySysLib\third_party\utf8proc\utf8proc.c -Isrc\ -Isrc\BeefySysLib -Isrc\BeefySysLib\platform\wasm -g -DBF_DISABLE_FFI -c -s WASM=1 -s USE_PTHREADS=1
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
call emar r ..\IDE\dist\Beef042RT32_wasm.a Common.o Internal.o Chars.o Math.o Object.o String.o Thread.o UTF8.o utf8proc.o WasmCommon.o
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
