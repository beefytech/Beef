#!/bin/bash
set -e

if [ ! -d llvm-project_11_0_0 ]; then
	git clone https://github.com/llvm/llvm-project.git llvm-project_11_0_0

	if [ -d llvm-project_11_0_0 ]; then
		cd llvm-project_11_0_0
 		git checkout llvmorg-11.0.0
		cd ..
	fi 

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