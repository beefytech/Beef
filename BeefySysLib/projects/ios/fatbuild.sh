# shell script goes here

XCODEBUILD_PATH=/Developer/usr/bin
XCODEBUILD=$XCODEBUILD_PATH/xcodebuild
LIBRARY_NAME=BeefySysLib
CONFIG=Release

SDKROOTDIR=~/Beefy/BeefySysLib/projects/ios

rm lib$LIBRARY_NAME.a
rm $SDKROOTDIR/$LIBRARY_NAME/build/$CONFIG-iphoneos/lib$LIBRARY_NAME.a
rm $SDKROOTDIR/$LIBRARY_NAME/build/$CONFIG-iphonesimulator/lib$LIBRARY_NAME.a
rm $SDKROOTDIR/build_SDK/*

cd $LIBRARY_NAME
# replace 'build' with 'clean build'?
xcodebuild -target $LIBRARY_NAME -sdk "iphonesimulator" -arch i386 -configuration $CONFIG build 
xcodebuild -target $LIBRARY_NAME -sdk "iphoneos" -configuration $CONFIG build 
cd ..

mkdir $SDKROOTDIR
mkdir $SDKROOTDIR/build_SDK

cp $SDKROOTDIR/$LIBRARY_NAME/build/$CONFIG-iphoneos/lib$LIBRARY_NAME.a $SDKROOTDIR/build_SDK/lib_device.a
cp $SDKROOTDIR/$LIBRARY_NAME/build/$CONFIG-iphonesimulator/lib$LIBRARY_NAME.a $SDKROOTDIR/build_SDK/lib_simulator.a

lipo -create $SDKROOTDIR/build_SDK/lib_device.a $SDKROOTDIR/build_SDK/lib_simulator.a -output lib$LIBRARY_NAME.a

exit 0