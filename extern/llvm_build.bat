PUSHD %~dp0

@IF EXIST llvm-project_13_0_1 GOTO LLVM_HAS
git clone --depth 1 --branch llvmorg-13.0.1 --config core.autocrlf=false https://github.com/llvm/llvm-project.git llvm-project_13_0_1
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:LLVM_HAS

@IF EXIST llvm_win64_13_0_1 GOTO HAS_CONFIG
mkdir llvm_win64_13_0_1
cd llvm_win64_13_0_1
cmake ../llvm-project_13_0_1/llvm -G"Visual Studio 17 2022" -Ax64 -Thost=x64 -DLLVM_USE_CRT_DEBUG:STRING="MTd" -DLLVM_USE_CRT_RELEASE:STRING="MT" -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly"
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
@GOTO DOBUILD

:HAS_CONFIG
cd llvm_win64_13_0_1
@GOTO DOBUILD

:DOBUILD
set /p LLVM_TARGETS=<../llvm_targets.txt
cmake --build . -t %LLVM_TARGETS% --config Debug
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build . -t %LLVM_TARGETS% --config Release
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
echo done > _Done.txt

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
