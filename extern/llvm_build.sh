#!/bin/bash
set -e

if [ ! -d llvm-project_8_0_0 ]; then
	git clone https://github.com/llvm/llvm-project.git llvm-project_8_0_0
else
	cd llvm-project_8_0_0
	git fetch origin
	cd ..
fi

cd llvm-project_8_0_0
git checkout llvmorg-8.0.0
cd ..

mkdir -p llvm_linux_8_0_0
cd llvm_linux_8_0_0
cmake -G Ninja ../llvm-project_8_0_0/llvm
ninja
cd ..

mkdir -p llvm_linux_rel_8_0_0
cd llvm_linux_rel_8_0_0
cmake -G Ninja ../llvm-project_8_0_0/llvm -DCMAKE_BUILD_TYPE:String=Release
ninja
cd ..

mkdir -p ../IDE/dist/llvm
mkdir -p ../IDE/dist/llvm/bin
cp llvm_linux_rel_8_0_0/bin/llvm-ar ../IDE/dist/llvm/bin
