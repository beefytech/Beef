#!/bin/bash
set -e

# LLVM source code, zip file 25MB : https://releases.llvm.org/download.html#11.0.0
#https://github.com/llvm/llvm-project/releases/download/llvmorg-11.0.0/llvm-11.0.0.src.tar.xz 

if [ ! -d llvm-project_11_0_0 ]; then
	tar -xf llvm-11.0.0.src.tar.xz
	mkdir llvm-project_11_0_0
	mv llvm-11.0.0.src llvm-project_11_0_0/llvm
fi

if [ ! -d llvm_linux_11_0_0 ]; then
	mkdir llvm_linux_11_0_0	
fi

if [ ! -d llvm_linux_11_0_0/bin ]; then
	cd llvm_linux_11_0_0
	cmake ../llvm-project_11_0_0/llvm
	cmake --build .
	cd ..
fi

if [ ! -d llvm_linux_rel_11_0_0 ]; then
	mkdir llvm_linux_rel_11_0_0	
fi

if [ ! -d llvm_linux_rel_11_0_0/bin ]; then
	cd llvm_linux_rel_11_0_0
	cmake ../llvm-project_11_0_0/llvm -DCMAKE_BUILD_TYPE:String=Release
	cmake --build .
	cd ..
fi
	
if [ ! -d ../IDE/dist/llvm/bin ]; then
	mkdir ../IDE/dist/llvm
	mkdir ../IDE/dist/llvm/bin
fi
cp llvm_linux_rel_11_0_0/bin/llvm-ar ../IDE/dist/llvm/bin
