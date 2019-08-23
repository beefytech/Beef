@IF EXIST llvm-project GOTO LLVM_HAS
git clone --config core.autocrlf=false https://github.com/llvm/llvm-project.git llvm-project_8_0_0
pushd llvm-project_8_0_0
GOTO :LLVM_SET

:LLVM_HAS
pushd llvm-project_8_0_0
git pull origin master

:LLVM_SET
git checkout llvmorg-8.0.0
popd
@IF EXIST llvm_win64_8_0_0 GOTO HAS_CONFIG

mkdir llvm_win64_8_0_0
cd llvm_win64_8_0_0
cmake ../llvm-project_8_0_0/llvm -Thost=x64 -G"Visual Studio 15 2017 Win64"
@GOTO DOBUILD

:HAS_CONFIG
cd llvm_win64_8_0_0
@GOTO DOBUILD

:DOBUILD
cmake --build . --config Debug
cmake --build . --config Release
