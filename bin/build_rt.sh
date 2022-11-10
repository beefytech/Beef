#!/bin/bash
echo Starting build_rt.sh

PATH=/usr/local/bin:$PATH:$HOME/bin
SCRIPTPATH=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
ROOTPATH="$(dirname "$SCRIPTPATH")"
echo Building from $SCRIPTPATH
cd $SCRIPTPATH

if [[ $1 == "clean" ]]; then
	rm -rf ../build_rt
	rm -rf ../build_rt_d
	exit
fi

if command -v ninja >/dev/null 2>&1 ; then
	CAN_USE_NINJA=1
	if [ -d ../build_rt_d ] && [ ! -f ../build_rt_d/build.ninja ]; then
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
if [ ! -d build_rt_d ]; then
	mkdir build_rt_d
	mkdir build_rt
fi

cd build_rt_d

cmake $USE_NINJA -DBF_ENABLE_SDL=1 -DBF_ONLY_RUNTIME=1 -DCMAKE_BUILD_TYPE=Debug ../
cmake --build .
cd ../build_rt
cmake $USE_NINJA -DBF_ENABLE_SDL=1 -DBF_ONLY_RUNTIME=1 -DCMAKE_BUILD_TYPE=RelWithDebInfo ../
cmake --build .
