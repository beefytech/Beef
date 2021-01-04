PUSHD %~dp0

@IF EXIST llvm-project_11_0_0 GOTO LLVM_HAS
git clone --depth 1 --branch llvmorg-11.0.0 https://github.com/llvm/llvm-project.git llvm-project_11_0_0
pushd llvm-project_11_0_0

:LLVM_HAS

@IF EXIST llvm_win64_11_0_0 GOTO HAS_CONFIG
mkdir llvm_win64_11_0_0
cd llvm_win64_11_0_0
cmake ../llvm-project_11_0_0/llvm -G"Visual Studio 16 2019" -Ax64 -Thost=x64 -DLLVM_USE_CRT_DEBUG:STRING="MTd" -DLLVM_USE_CRT_RELEASE:STRING="MT" -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly"
@GOTO DOBUILD

:HAS_CONFIG
cd llvm_win64_11_0_0
@GOTO DOBUILD

:DOBUILD
cmake --build . -t $(cat ../llvm_targets.txt) --config Debug
cmake --build . -t $(cat ../llvm_targets.txt) --config Release

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%
