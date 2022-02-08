mkdir ..\wasm
cd ..\wasm
call emcc ..\BeefRT\rt\Chars.cpp ..\BeefRT\rt\Math.cpp ..\BeefRT\rt\Object.cpp ..\BeefRT\rt\Thread.cpp ..\BeefRT\rt\Internal.cpp ..\BeefySysLib\platform\wasm\WasmCommon.cpp ..\BeefySysLib\Common.cpp ..\BeefySysLib\util\String.cpp ..\BeefySysLib\util\UTF8.cpp ..\BeefySysLib\third_party\utf8proc\utf8proc.c -I..\ -I..\BeefySysLib -I..\BeefySysLib\platform\wasm -g -DBF_DISABLE_FFI -c -s WASM=1 -s USE_PTHREADS=1
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
