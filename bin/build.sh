#!/bin/bash
echo Starting build.sh

PATH=/usr/local/bin:$PATH:$HOME/bin
SCRIPTPATH=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
ROOTPATH="$(dirname "$SCRIPTPATH")"
echo Building from from $SCRIPTPATH
cd $SCRIPTPATH

# exit when any command fails
set -e

### Dependencies ###

if [ ! -f ../BeefySysLib/third_party/libffi/Makefile ]; then
	echo Building libffi...
	cd ../BeefySysLib/third_party/libffi
	./configure
	make	
	cd $SCRIPTPATH
fi

if [ ! -d ../extern/llvm_linux_11_0_0/bin ]; then
	echo Building LLVM...
	cd ../extern
	./llvm_build.sh
	cd $SCRIPTPATH
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
	LINKOPTS="-Wl,-no_compact_unwind -Wl,-rpath -Wl,@executable_path"	
else
	LIBEXT=so	
	LINKOPTS="-ldl -lpthread -Wl,-rpath -Wl,\$ORIGIN"
fi

ln -s -f $ROOTPATH/jbuild_d/Debug/bin/libBeefRT_d.a libBeefRT_d.a
ln -s -f $ROOTPATH/jbuild_d/Debug/bin/libBeefySysLib_d.$LIBEXT libBeefySysLib_d.$LIBEXT
ln -s -f $ROOTPATH/jbuild_d/Debug/bin/libIDEHelper_d.$LIBEXT libIDEHelper_d.$LIBEXT

ln -s -f $ROOTPATH/jbuild/Release/bin/libBeefRT.a libBeefRT.a
ln -s -f $ROOTPATH/jbuild/Release/bin/libBeefySysLib.$LIBEXT libBeefySysLib.$LIBEXT
ln -s -f $ROOTPATH/jbuild/Release/bin/libIDEHelper.$LIBEXT libIDEHelper.$LIBEXT

### DEBUG ###

echo Building BeefBuild_bootd
../../jbuild_d/Debug/bin/BeefBoot --out="BeefBuild_bootd" --src=../src --src=../../BeefBuild/src --src=../../BeefLibs/corlib/src --src=../../BeefLibs/Beefy2D/src --define=CLI --define=DEBUG --startup=BeefBuild.Program --linkparams="./libBeefRT_d.a ./libIDEHelper_d.$LIBEXT ./libBeefySysLib_d.$LIBEXT $(< ../../IDE/dist/IDEHelper_libs_d.txt) $LINKOPTS"
echo Building BeefBuild_d
./BeefBuild_bootd -clean -proddir=../../BeefBuild -config=Debug
echo Testing IDEHelper/Tests in BeefBuild_d
./BeefBuild_d -proddir=../../IDEHelper/Tests -test

### RELEASE ###

echo Building BeefBuild_boot
../../jbuild/Release/bin/BeefBoot --out="BeefBuild_boot" --src=../src --src=../../BeefBuild/src --src=../../BeefLibs/corlib/src --src=../../BeefLibs/Beefy2D/src --define=CLI --startup=BeefBuild.Program --linkparams="./libBeefRT.a ./libIDEHelper.$LIBEXT ./libBeefySysLib.$LIBEXT $(< ../../IDE/dist/IDEHelper_libs.txt) $LINKOPTS"
echo Building BeefBuild
./BeefBuild_boot -clean -proddir=../../BeefBuild -config=Release
echo Testing IDEHelper/Tests in BeefBuild
./BeefBuild -proddir=../../IDEHelper/Tests -test
