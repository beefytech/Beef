PUSHD %~dp0..\

@IF EXIST llvm-project_8_0_1 GOTO LLVM_HAS
git clone --config core.autocrlf=false https://github.com/llvm/llvm-project.git llvm-project_8_0_1
pushd llvm-project_8_0_1
GOTO :LLVM_SET

:LLVM_HAS
pushd llvm-project_8_0_1
git pull origin master

:LLVM_SET
git checkout llvmorg-8.0.1
popd
@IF EXIST llvm_win64_8_0_1 GOTO HAS_CONFIG

mkdir llvm_win64_8_0_1
cd llvm_win64_8_0_1
cmake ../llvm-project_8_0_1/llvm -Thost=x64 -G"Visual Studio 15 2017 Win64" -DLLVM_USE_CRT_DEBUG:STRING="MTd" -DLLVM_USE_CRT_RELEASE:STRING="MT"
@GOTO DOBUILD

:HAS_CONFIG
cd llvm_win64_8_0_1
@GOTO DOBUILD

:DOBUILD
cmake --build . --config Debug 
cmake --build . --config Release

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%