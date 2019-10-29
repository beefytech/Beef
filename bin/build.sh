#!/bin/bash
echo Starting build.sh

PATH=/usr/local/bin:$PATH:$HOME/bin
cd "$(dirname "$0")"

# exit when any command fails
set -e

### Dependencies ###

if [ ! -f ../BeefySysLib/third_party/libffi/Makefile ]; then
	echo Building libffi...
	cd ../BeefySysLib/third_party/libffi
	./configure
	make
	cd "$(dirname "$0")"
fi

if [ ! -d ../extern/llvm_linux_8_0_0 ]; then
	echo Building LLVM...
	cd ../extern
	./llvm_build.sh
	cd "$(dirname "$0")"
fi

### LIBS ###

cd ..
if [ ! -d jbuild_d ]; then
	mkdir jbuild_d
	mkdir jbuild
fi
cd jbuild_d
cmake -DCMAKE_BUILD_TYPE=Debug ../
cmake --build .
cd ../jbuild
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ../
cmake --build .

cd ../IDE/dist

if [[ "$OSTYPE" == "darwin"* ]]; then
	LIBEXT=dylib
	LINKOPTS="-Wl,-rpath -Wl,@executable_path"	
else
	LIBEXT=so
	LINKOPTS="-ltinfo -Wl,-rpath -Wl,\$ORIGIN"
fi

if [ ! -L libBeefRT_d.a ]; then	
	ln -s ../../jbuild_d/Debug/bin/libBeefRT_d.a libBeefRT_d.a
	ln -s ../../jbuild_d/Debug/bin/libBeefySysLib_d.$LIBEXT libBeefySysLib_d.$LIBEXT
	ln -s ../../jbuild_d/Debug/bin/libIDEHelper_d.$LIBEXT libIDEHelper_d.$LIBEXT

	ln -s ../../jbuild/Release/bin/libBeefRT.a libBeefRT.a
	ln -s  ../../jbuild/Release/bin/libBeefySysLib.$LIBEXT libBeefySysLib.$LIBEXT
	ln -s ../../jbuild/Release/bin/libIDEHelper.$LIBEXT libIDEHelper.$LIBEXT
fi

### DEBUG ###

echo Building BeefBuild_bootd
../../jbuild_d/Debug/bin/BeefBoot --out="BeefBuild_bootd" --src=../src --src=../../BeefBuild/src --src=../../BeefLibs/corlib/src --src=../../BeefLibs/Beefy2D/src --define=CLI --define=DEBUG --startup=BeefBuild.Program --linkparams="./libBeefRT_d.a ./libIDEHelper_d.$LIBEXT ./libBeefySysLib_d.$LIBEXT ../../extern/llvm_linux_8_0_0/lib/libLLVMCore.a ../../extern/llvm_linux_8_0_0/lib/libLLVMMC.a ../../extern/llvm_linux_8_0_0/lib/libLLVMMCParser.a ../../extern/llvm_linux_8_0_0/lib/libLLVMCodeGen.a ../../extern/llvm_linux_8_0_0/lib/libLLVMX86Disassembler.a ../../extern/llvm_linux_8_0_0/lib/libLLVMMCDisassembler.a ../../extern/llvm_linux_8_0_0/lib/libLLVMSupport.a ../../extern/llvm_linux_8_0_0/lib/libLLVMX86Info.a ../../extern/llvm_linux_8_0_0/lib/libLLVMX86Utils.a ../../extern/llvm_linux_8_0_0/lib/libLLVMX86AsmPrinter.a ../../extern/llvm_linux_8_0_0/lib/libLLVMX86Desc.a ../../extern/llvm_linux_8_0_0/lib/libLLVMObject.a ../../extern/llvm_linux_8_0_0/lib/libLLVMBitReader.a ../../extern/llvm_linux_8_0_0/lib/libLLVMAsmParser.a ../../extern/llvm_linux_8_0_0/lib/libLLVMTarget.a ../../extern/llvm_linux_8_0_0/lib/libLLVMX86CodeGen.a ../../extern/llvm_linux_8_0_0/lib/libLLVMScalarOpts.a ../../extern/llvm_linux_8_0_0/lib/libLLVMInstCombine.a ../../extern/llvm_linux_8_0_0/lib/libLLVMSelectionDAG.a ../../extern/llvm_linux_8_0_0/lib/libLLVMProfileData.a ../../extern/llvm_linux_8_0_0/lib/libLLVMTransformUtils.a ../../extern/llvm_linux_8_0_0/lib/libLLVMAnalysis.a ../../extern/llvm_linux_8_0_0/lib/libLLVMX86AsmParser.a ../../extern/llvm_linux_8_0_0/lib/libLLVMAsmPrinter.a ../../extern/llvm_linux_8_0_0/lib/libLLVMBitWriter.a ../../extern/llvm_linux_8_0_0/lib/libLLVMVectorize.a ../../extern/llvm_linux_8_0_0/lib/libLLVMipo.a ../../extern/llvm_linux_8_0_0/lib/libLLVMInstrumentation.a ../../extern/llvm_linux_8_0_0/lib/libLLVMDebugInfoDWARF.a ../../extern/llvm_linux_8_0_0/lib/libLLVMDebugInfoPDB.a ../../extern/llvm_linux_8_0_0/lib/libLLVMDebugInfoCodeView.a ../../extern/llvm_linux_8_0_0/lib/libLLVMGlobalISel.a ../../extern/llvm_linux_8_0_0/lib/libLLVMBinaryFormat.a ../../extern/llvm_linux_8_0_0/lib/libLLVMDemangle.a $LINKOPTS"
echo Building BeefBuild_d
./BeefBuild_bootd -clean -proddir=../../BeefBuild -config=Debug
#./BeefBuild_d -proddir=../../TestApp
#../../TestApp/build/Debug_Linux64/TestApp/TestApp
echo Testing IDEHelper/Tests in BeefBuild_d
./BeefBuild_d -proddir=../../IDEHelper/Tests -test

### RELEASE ###

echo Building BeefBuild_boot
../../jbuild/Release/bin/BeefBoot --out="BeefBuild_boot" --src=../src --src=../../BeefBuild/src --src=../../BeefLibs/corlib/src --src=../../BeefLibs/Beefy2D/src --define=CLI --define=DEBUG --startup=BeefBuild.Program --linkparams="./libBeefRT.a ./libIDEHelper.$LIBEXT ./libBeefySysLib.$LIBEXT ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMCore.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMMC.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMMCParser.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMCodeGen.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMX86Disassembler.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMMCDisassembler.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMSupport.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMX86Info.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMX86Utils.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMX86AsmPrinter.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMX86Desc.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMObject.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMBitReader.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMAsmParser.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMTarget.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMX86CodeGen.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMScalarOpts.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMInstCombine.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMSelectionDAG.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMProfileData.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMTransformUtils.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMAnalysis.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMX86AsmParser.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMAsmPrinter.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMBitWriter.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMVectorize.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMipo.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMInstrumentation.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMDebugInfoDWARF.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMDebugInfoPDB.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMDebugInfoCodeView.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMGlobalISel.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMBinaryFormat.a ../../extern/llvm_linux_rel_8_0_0/lib/libLLVMDemangle.a $LINKOPTS"
echo Building BeedBuild
./BeefBuild_boot -clean -proddir=../../BeefBuild -config=Release
#./BeefBuild_d -proddir=../../TestApp
#../../TestApp/build/Debug_Linux64/TestApp/TestApp
echo Testing IDEHelper/Tests in BeefBuild
./BeefBuild -proddir=../../IDEHelper/Tests -test
