#!/bin/bash
set -e

if [ ! -d llvm-project_8_0_0 ]; then
	git clone https://github.com/llvm/llvm-project.git llvm-project_8_0_0
else
	cd llvm-project_8_0_0
	git pull origin master	
	cd ..
fi

if [ -d llvm-project_8_0_0 ]; then
	cd llvm-project_8_0_0
 	git checkout llvmorg-8.0.0
	cd ..
fi 

if [ ! -d llvm_linux_8_0_0 ]; then
	mkdir llvm_linux_8_0_0
	cd llvm_linux_8_0_0
	cmake ../llvm-project_8_0_0/llvm
	cmake --build .
	cd ..
fi

if [ ! -d llvm_linux_rel_8_0_0_rel ]; then
	mkdir llvm_linux_rel_8_0_0
	cd llvm_linux_rel_8_0_0
	cmake ../llvm-project_8_0_0/llvm -DCMAKE_BUILD_TYPE:String=Release
	cmake --build .
	cd ..
fi
	
