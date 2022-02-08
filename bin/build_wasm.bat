mkdir ..\wasm
cd ..\wasm
call emcc ..\BeefRT\rt\Math.cpp ..\BeefRT\rt\Object.cpp ..\BeefRT\rt\Thread.cpp ..\BeefRT\rt\Internal.cpp ..\BeefySysLib\platform\wasm\WasmCommon.cpp ..\BeefySysLib\Common.cpp ..\BeefySysLib\util\String.cpp ..\BeefySysLib\util\UTF8.cpp ..\BeefySysLib\third_party\utf8proc\utf8proc.c -I..\ -I..\BeefySysLib -I..\BeefySysLib\platform\wasm -g -DBF_DISABLE_FFI -c -s WASM=1 -s USE_PTHREADS=1
emar r BeefRT.a Common.o Internal.o Math.o Object.o String.o Thread.o UTF8.o utf8proc.o WasmCommon.o