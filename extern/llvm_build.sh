#!/bin/bash
set -e

if [ ! -d llvm-project_11_0_0 ]; then
	if [ -f llvm-11.0.0.src.tar.xz ]; then # if user downloaded llvm-11.0.0.src.tar.xz then use it instead
		tar -xf llvm-11.0.0.src.tar.xz
		mkdir llvm-project_11_0_0
		mv llvm-11.0.0.src llvm-project_11_0_0/llvm
	else # shallow git clone llvm repo if llvm-11.0.0.src.tar.xz does not exists
		git clone --depth 1 --branch llvmorg-11.0.0 https://github.com/llvm/llvm-project.git llvm-project_11_0_0
	fi
fi #end if llvm-project_11_0_0 exists

if [ ! -d llvm_linux_11_0_0 ]; then
	mkdir llvm_linux_11_0_0
fi

if [ ! -d llvm_linux_11_0_0/bin ]; then
	cd llvm_linux_11_0_0
	cmake ../llvm-project_11_0_0/llvm -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly" -DCMAKE_BUILD_TYPE:String="Debug"
	cmake --build . -t $(cat ../llvm_targets.txt)
	cd ..
fi

if [ ! -d llvm_linux_rel_11_0_0 ]; then
	mkdir llvm_linux_rel_11_0_0
fi

if [ ! -d llvm_linux_rel_11_0_0/bin ]; then
	cd llvm_linux_rel_11_0_0
	cmake ../llvm-project_11_0_0/llvm -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly" -DCMAKE_BUILD_TYPE:String="Release"
	cmake --build . -t $(cat ../llvm_targets.txt)
	cd ..
fi

if [ ! -d ../IDE/dist/llvm/bin ]; then
	mkdir ../IDE/dist/llvm
	mkdir ../IDE/dist/llvm/bin
fi
cp llvm_linux_rel_11_0_0/bin/llvm-ar ../IDE/dist/llvm/bin

