# exit when any command fails
set -e

SCRIPTPATH=$(cd -P -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)

for i in "$@"
do
	if [[ $i == "clean" ]]; then
		echo "Cleaning..."
		rm -rf ../builds/ios
		exit
	fi
done

if [ ! -d ../BeefySysLib/third_party/libffi/aarch64-apple-ios ]; then
	echo Building libffi...
	cd ../BeefySysLib/third_party/libffi
	
	./configure \
		--host=aarch64-apple-ios \
		--enable-static \
		--disable-shared \
		--disable-docs

	make
	
	cd $SCRIPTPATH
fi

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