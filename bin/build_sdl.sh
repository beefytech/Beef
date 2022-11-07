#!/bin/bash
echo Starting build.sh

PATH=/usr/local/bin:$PATH:$HOME/bin
SCRIPTPATH=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
ROOTPATH="$(dirname "$SCRIPTPATH")"
echo Building from $SCRIPTPATH
cd $SCRIPTPATH

if [[ $1 == "clean" ]]; then
	rm -rf ../jbuild_sdl
	rm -rf ../jbuild_sdl_d
	exit
fi

if [[ $1 == "sdl" ]]; then
	echo "Using SDL"
	USE_SDL="-DBF_ENABLE_SDL=1"
fi

if command -v ninja >/dev/null 2>&1 ; then
	CAN_USE_NINJA=1
	if [ -d ../jbuild_sdl_d ] && [ ! -f ../jbuild_sdl_d/build.ninja ]; then
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

### LIBS ###

cd ..
if [ ! -d jbuild_sdl_d ]; then
	mkdir jbuild_sdl_d
	mkdir jbuild_sdl
fi

cd jbuild_sdl_d

echo cmake $USE_NINJA $USE_SDL -DCMAKE_BUILD_TYPE=Debug ../

cmake $USE_NINJA -DBF_ENABLE_SDL=1 -DCMAKE_BUILD_TYPE=Debug ../
cmake --build .
cd ../jbuild_sdl
cmake $USE_NINJA -DBF_ENABLE_SDL=1 -DCMAKE_BUILD_TYPE=RelWithDebInfo ../
cmake --build .
