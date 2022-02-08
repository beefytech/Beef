#!/bin/bash
set -e

if [ ! -d llvm-project_13_0_1 ]; then
	if [ -f llvm-13.0.1.src.tar.xz ]; then # if user downloaded llvm-13.0.1.src.tar.xz then use it instead
		tar -xf llvm-13.0.1.src.tar.xz
		mkdir llvm-project_13_0_1
		mv llvm-13.0.1.src llvm-project_13_0_1/llvm
	else # shallow git clone llvm repo if llvm-13.0.1.src.tar.xz does not exists
		git clone --depth 1 --branch llvmorg-13.0.1 https://github.com/llvm/llvm-project.git llvm-project_13_0_1
	fi
fi #end if llvm-project_13_0_1 exists

if [ ! -d llvm_linux_13_0_1 ]; then
	mkdir llvm_linux_13_0_1
fi

if [ ! -d llvm_linux_13_0_1/bin ]; then
	cd llvm_linux_13_0_1
	cmake ../llvm-project_13_0_1/llvm -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly" -DCMAKE_BUILD_TYPE:String="Debug"
	cmake --build . -t $(cat ../llvm_targets.txt)
	cd ..
fi

if [ ! -d llvm_linux_rel_13_0_1 ]; then
	mkdir llvm_linux_rel_13_0_1
fi

if [ ! -d llvm_linux_rel_13_0_1/bin ]; then
	cd llvm_linux_rel_13_0_1
	cmake ../llvm-project_13_0_1/llvm -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly" -DCMAKE_BUILD_TYPE:String="Release"
	cmake --build . -t $(cat ../llvm_targets.txt)
	cd ..
fi

if [ ! -d ../IDE/dist/llvm/bin ]; then
	mkdir ../IDE/dist/llvm
	mkdir ../IDE/dist/llvm/bin
fi
cp llvm_linux_rel_13_0_1/bin/llvm-ar ../IDE/dist/llvm/bin

echo done > llvm_linux_13_0_1/_Done.txt