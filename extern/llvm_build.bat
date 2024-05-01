PUSHD %~dp0

@IF EXIST llvm-project_18_1_4 GOTO LLVM_HAS
git clone --depth 1 --branch llvmorg-18.1.4 --config core.autocrlf=false https://github.com/llvm/llvm-project.git llvm-project_18_1_4
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:LLVM_HAS

@IF EXIST llvm_win64_18_1_4 GOTO HAS_CONFIG
mkdir llvm_win64_18_1_4
cd llvm_win64_18_1_4
cmake ../llvm-project_18_1_4/llvm -G"Visual Studio 17 2022" -Ax64 -Thost=x64 -D CMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly"
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
@GOTO DOBUILD

:HAS_CONFIG
cd llvm_win64_18_1_4
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
