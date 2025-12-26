#!/bin/bash

PATH=/usr/local/bin:$PATH:$HOME/bin
SCRIPTPATH=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

echo Building from $SCRIPTPATH
cd $SCRIPTPATH

if command -v ninja >/dev/null 2>&1 ; then
	if [ ! -d build ] || [ -f build/build.ninja ]; then
        USE_NINJA="-GNinja"
    else
		echo "Ninja couldn't be enabled for this build, consider doing a clean build to start using Ninja for faster build speeds."
	fi
fi

if [ ! -d build ]; then
	mkdir build
fi

cd build
cmake $USE_NINJA -DCMAKE_BUILD_TYPE=RelWithDebInfo ../
cmake --build .

cd $SCRIPTPATH/../../IDE/dist/images
ln -s -f $SCRIPTPATH/build/ImgCreate ImgCreate