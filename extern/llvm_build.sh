#!/bin/bash
set -e

USE_NINJA=""
if command -v ninja >/dev/null 2>&1 ; then
    USE_NINJA="-GNinja"
fi

FORCE_BUILD=0
for i in "$@"
do
	if [[ $i == "force" ]]; then
		FORCE_BUILD=1
	fi
done

if [ ! -d llvm-project_18_1_4 ]; then
	if [ -f llvm-18.1.4.src.tar.xz ]; then # if user downloaded llvm-18.1.4.src.tar.xz then use it instead
		tar -xf llvm-18.1.4.src.tar.xz
		mkdir llvm-project_18_1_4
		mv llvm-18.1.4.src llvm-project_18_1_4/llvm
	else # shallow git clone llvm repo if llvm-18.1.4.src.tar.xz does not exists
		git clone --depth 1 --branch llvmorg-18.1.4 https://github.com/llvm/llvm-project.git llvm-project_18_1_4
	fi
fi #end if llvm-project_18_1_4 exists

if [ ! -d llvm_linux_18_1_4 ]; then
	mkdir llvm_linux_18_1_4
fi

if [ ! -d llvm_linux_18_1_4/bin ] || [ $FORCE_BUILD == 1 ]; then
	cd llvm_linux_18_1_4
	cmake $USE_NINJA ../llvm-project_18_1_4/llvm -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly" -DCMAKE_BUILD_TYPE:String="Debug"
	cmake --build . -t $(cat ../llvm_targets.txt)
	cd ..
fi

if [ ! -d llvm_linux_rel_18_1_4 ]; then
	mkdir llvm_linux_rel_18_1_4
fi

if [ ! -d llvm_linux_rel_18_1_4/bin ] || [ $FORCE_BUILD == 1 ]; then
	cd llvm_linux_rel_18_1_4
	cmake $USE_NINJA ../llvm-project_18_1_4/llvm -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly" -DCMAKE_BUILD_TYPE:String="Release"
	cmake --build . -t $(cat ../llvm_targets.txt)
	cd ..
fi

if [ ! -d ../IDE/dist/llvm/bin ]; then
	mkdir ../IDE/dist/llvm
	mkdir ../IDE/dist/llvm/bin
fi
cp llvm_linux_rel_18_1_4/bin/llvm-ar ../IDE/dist/llvm/bin

echo done > llvm_linux_18_1_4/_Done.txt