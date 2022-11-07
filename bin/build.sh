#!/bin/bash
echo Starting build.sh

PATH=/usr/local/bin:$PATH:$HOME/bin
SCRIPTPATH=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
ROOTPATH="$(dirname "$SCRIPTPATH")"
echo Building from $SCRIPTPATH
cd $SCRIPTPATH

for i in "$@"
do
	if [[ $i == "clean" ]]; then
		echo "Cleaning..."
		rm -rf ../jbuild
		rm -rf ../jbuild_d
		exit
	fi

	if [[ $i == "sdl" ]]; then
		echo "Using SDL"
		USE_SDL="-DBF_ENABLE_SDL=1"
	fi

	if [[ $i == "no_ffi" ]]; then
		echo "Disabling FFI"
		USE_FFI="-DBF_DISABLE_FFI=1"
	fi
done

if command -v ninja >/dev/null 2>&1 ; then
	CAN_USE_NINJA=1
	if [ -d ../jbuild_d ] && [ ! -f ../jbuild_d/build.ninja ]; then
		CAN_USE_NINJA=0
	fi

	if [ $CAN_USE_NINJA == 1 ]; then
		echo "Ninja is enabled for this build."
		USE_NINJA="-GNinja"
	else
		echo "Ninja couldn't be enabled for this build, consider doing a clean build to start using Ninja for faster build speeds."
	fi
else
	echo "Ninja isn't installed, consider installing it for faster build speeds."
fi

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

if [ ! -f ../extern/llvm_linux_13_0_1/_Done.txt ]; then
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

echo cmake $USE_NINJA $USE_SDL -DCMAKE_BUILD_TYPE=Debug ../

cmake $USE_NINJA $USE_SDL $USE_FFI -DCMAKE_BUILD_TYPE=Debug ../
cmake --build .
cd ../jbuild
cmake $USE_NINJA $USE_SDL $USE_FFI -DCMAKE_BUILD_TYPE=RelWithDebInfo ../
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

ln -s -f $ROOTPATH/jbuild_d/Debug/bin/libBeefRT_d.a ../../BeefLibs/Beefy2D/dist/libBeefRT_d.a
ln -s -f $ROOTPATH/jbuild_d/Debug/bin/libBeefySysLib_d.$LIBEXT ../../BeefLibs/Beefy2D/dist/libBeefySysLib_d.$LIBEXT
ln -s -f $ROOTPATH/jbuild_d/Debug/bin/libIDEHelper_d.$LIBEXT ../../BeefLibs/Beefy2D/dist/libIDEHelper_d.$LIBEXT

ln -s -f $ROOTPATH/jbuild/Release/bin/libBeefRT.a ../../BeefLibs/Beefy2D/dist/libBeefRT.a
ln -s -f $ROOTPATH/jbuild/Release/bin/libBeefySysLib.$LIBEXT ../../BeefLibs/Beefy2D/dist/libBeefySysLib.$LIBEXT
ln -s -f $ROOTPATH/jbuild/Release/bin/libIDEHelper.$LIBEXT ../../BeefLibs/Beefy2D/dist/libIDEHelper.$LIBEXT

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
