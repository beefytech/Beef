#!/bin/bash
set -e

USE_NINJA=""
if command -v ninja >/dev/null 2>&1 ; then
    USE_NINJA="-GNinja"
fi

if [ ! -d llvm-project_13_0_1 ]; then
	# download source tarball, unless it is already present.
	if [ ! -f llvm-13.0.1.src.tar.xz ]; then
		# download llvm: https://releases.llvm.org/download.html#13.0.1
		wget --quiet --show-progress https://github.com/llvm/llvm-project/releases/download/llvmorg-13.0.1/llvm-13.0.1.src.tar.xz
	fi

	# verify checksum
	SUM=`sha256sum llvm_targets.txt | cut -d ' ' -f 1`
	if [[ $SUM != "e2b5637470cb5c227b7811b805cec6bc506e10c74062cd866fd9b56cdd6d7dc9" ]]; then
		echo Error! Wrong checksum: $SUM
		echo Probably the download is incomplete.
		echo Try deleting llvm-13.0.1.src.tar.xz and rerun this script.
		echo Exiting.
		exit 1
	else
		echo Checksum: OK
	fi

	# extract source tarball
	tar -xf llvm-13.0.1.src.tar.xz

	# recreate directory structure of LLVM git repo.
	# this step is obsolete, since we don't clone the git repo anymore.
	# but paths need to be adjusted below, if this is skipped.
	mkdir llvm-project_13_0_1
	mv llvm-13.0.1.src llvm-project_13_0_1/llvm

fi #end if llvm-project_13_0_1 exists

if [ ! -d llvm_linux_13_0_1 ]; then
	mkdir llvm_linux_13_0_1
fi

if [ ! -d llvm_linux_13_0_1/bin ]; then
	cd llvm_linux_13_0_1
	cmake $USE_NINJA ../llvm-project_13_0_1/llvm -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly" -DCMAKE_BUILD_TYPE:String="Debug"
	cmake --build . -t $(cat ../llvm_targets.txt)
	cd ..
fi

if [ ! -d llvm_linux_rel_13_0_1 ]; then
	mkdir llvm_linux_rel_13_0_1
fi

if [ ! -d llvm_linux_rel_13_0_1/bin ]; then
	cd llvm_linux_rel_13_0_1
	cmake $USE_NINJA ../llvm-project_13_0_1/llvm -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;WebAssembly" -DCMAKE_BUILD_TYPE:String="Release"
	cmake --build . -t $(cat ../llvm_targets.txt)
	cd ..
fi

if [ ! -d ../IDE/dist/llvm/bin ]; then
	mkdir ../IDE/dist/llvm
	mkdir ../IDE/dist/llvm/bin
fi
cp llvm_linux_rel_13_0_1/bin/llvm-ar ../IDE/dist/llvm/bin

echo done > llvm_linux_13_0_1/_Done.txt
