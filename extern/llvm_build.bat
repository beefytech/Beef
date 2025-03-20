PUSHD %~dp0

@IF EXIST llvm-project_19_1_7 GOTO LLVM_HAS
git clone --depth 1 --branch llvmorg-19.1.7 --config core.autocrlf=false https://github.com/llvm/llvm-project.git llvm-project_19_1_7
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

:LLVM_HAS

@IF EXIST llvm_win64_19_1_7 GOTO HAS_CONFIG
mkdir llvm_win64_19_1_7
cd llvm_win64_19_1_7
@REM cmake ../llvm-project_19_1_7/llvm -G"Visual Studio 17 2022" -Ax64 -Thost=x64 -DLLVM_ENABLE_PROJECTS=clang -D CMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly"
cmake ../llvm-project_19_1_7/llvm -G"Visual Studio 17 2022" -Ax64 -Thost=x64 -D CMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly"
@REM cmake ../llvm-project_19_1_7/llvm -G"Visual Studio 17 2022"
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
@GOTO DOBUILD

:HAS_CONFIG
cd llvm_win64_19_1_7
@GOTO DOBUILD

:DOBUILD

@REM set /p LLVM_TARGETS=<../llvm_targets.txt
@REM cmake --build . -t %LLVM_TARGETS% --config Debug
@REM @IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
@REM cmake --build . -t %LLVM_TARGETS% --config Release
@REM @IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

cmake --build . --config Debug
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build . --config Release
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
