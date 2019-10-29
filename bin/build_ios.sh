# exit when any command fails
set -e

cd "$(dirname "$0")"
cd ../builds

if [ ! -d ios ]; then
	mkdir ios
	cd ios
	cmake ../../BeefRT -G Xcode -DCMAKE_TOOLCHAIN_FILE=../../bin/ios.toolchain.cmake -DPLATFORM=OS64
	cd ..
fi

cd ios
cmake --build . --config Debug