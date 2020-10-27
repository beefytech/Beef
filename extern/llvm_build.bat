PUSHD %~dp0

@IF EXIST llvm-project_11_0_0 GOTO LLVM_HAS
git clone --config core.autocrlf=false https://github.com/llvm/llvm-project.git llvm-project_11_0_0
pushd llvm-project_11_0_0
GOTO :LLVM_SET

:LLVM_HAS
pushd llvm-project_11_0_0
git pull origin master

:LLVM_SET
git checkout llvmorg-11.0.0
popd
@IF EXIST llvm_win64_11_0_0 GOTO HAS_CONFIG

mkdir llvm_win64_11_0_0
cd llvm_win64_11_0_0
cmake ../llvm-project_11_0_0/llvm -Thost=x64 -DLLVM_USE_CRT_DEBUG:STRING="MTd" -DLLVM_USE_CRT_RELEASE:STRING="MT"
@GOTO DOBUILD

:HAS_CONFIG
cd llvm_win64_11_0_0
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