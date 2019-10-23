PUSHD %~dp0..\

SET NDK=C:\Users\Brian\AppData\Local\Android\Sdk\ndk\20.0.5594570
SET NINJA=C:\Users\Brian\AppData\Local\Android\Sdk\cmake\3.10.2.4988404\bin\ninja.exe
@REM SET NDK=C:\NVPACK\android-ndk-r14b

@REM i686-none-linux-android16
@REM i686-linux-android armv7-none-linux-androideabi16 

cd builds
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR

@IF EXIST android_x86 GOTO DO_BUILD

mkdir android_x86_d
cd android_x86_d
cmake -GNinja ^
	-DANDROID_ABI:UNINITIALIZED=x86 ^
	-DANDROID_NDK=%NDK% ^
	-DANDROID_PLATFORM=android-16 ^
	-DCMAKE_ANDROID_ARCH_ABI=x86 ^
	-DCMAKE_ANDROID_NDK=%NDK% ^
	-DCMAKE_SYSTEM_NAME=Android ^
	-DCMAKE_SYSTEM_VERSION=16 ^
	-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
	-DCMAKE_MAKE_PROGRAM=%NINJA% ^
	-DCMAKE_BUILD_TYPE=Debug ^
	..\..\BeefRT
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR	
cd ..

mkdir android_x86
cd android_x86
cmake -GNinja ^
	-DANDROID_ABI:UNINITIALIZED=x86 ^
	-DANDROID_NDK=%NDK% ^
	-DANDROID_PLATFORM=android-16 ^
	-DCMAKE_ANDROID_ARCH_ABI=x86 ^
	-DCMAKE_ANDROID_NDK=%NDK% ^
	-DCMAKE_SYSTEM_NAME=Android ^
	-DCMAKE_SYSTEM_VERSION=16 ^
	-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
	-DCMAKE_MAKE_PROGRAM=%NINJA% ^
	-DCMAKE_BUILD_TYPE=Release ^
	..\..\BeefRT
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR	
cd ..

mkdir android_x86_64_d
cd android_x86_64_d
cmake -GNinja ^
	-DANDROID_ABI:UNINITIALIZED=x86_64 ^
	-DANDROID_NDK=%NDK% ^
	-DANDROID_PLATFORM=android-16 ^
	-DCMAKE_ANDROID_ARCH_ABI=x86_64 ^
	-DCMAKE_ANDROID_NDK=%NDK% ^
	-DCMAKE_SYSTEM_NAME=Android ^
	-DCMAKE_SYSTEM_VERSION=16 ^
	-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
	-DCMAKE_MAKE_PROGRAM=%NINJA% ^
	-DCMAKE_BUILD_TYPE=Debug ^
	..\..\BeefRT
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR	
cd ..

mkdir android_x86_64
cd android_x86_64
cmake -GNinja ^
	-DANDROID_ABI:UNINITIALIZED=x86_64 ^
	-DANDROID_NDK=%NDK% ^
	-DANDROID_PLATFORM=android-16 ^
	-DCMAKE_ANDROID_ARCH_ABI=x86_64 ^
	-DCMAKE_ANDROID_NDK=%NDK% ^
	-DCMAKE_SYSTEM_NAME=Android ^
	-DCMAKE_SYSTEM_VERSION=16 ^
	-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
	-DCMAKE_MAKE_PROGRAM=%NINJA% ^
	-DCMAKE_BUILD_TYPE=Release ^
	..\..\BeefRT
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR	
cd ..

mkdir android_arm_d
cd android_arm_d
cmake -GNinja ^
	-DANDROID_ABI:UNINITIALIZED=armeabi-v7a ^
	-DANDROID_NDK=%NDK% ^
	-DANDROID_PLATFORM=android-16 ^
	-DCMAKE_ANDROID_ARCH_ABI=armeabi-v7a ^
	-DCMAKE_ANDROID_NDK=%NDK% ^
	-DCMAKE_SYSTEM_NAME=Android ^
	-DCMAKE_SYSTEM_VERSION=16 ^
	-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
	-DCMAKE_MAKE_PROGRAM=%NINJA% ^
	-DCMAKE_BUILD_TYPE=Debug ^
	..\..\BeefRT
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR	
cd ..

mkdir android_arm
cd android_arm
cmake -GNinja ^
	-DANDROID_ABI:UNINITIALIZED=armeabi-v7a ^
	-DANDROID_NDK=%NDK% ^
	-DANDROID_PLATFORM=android-16 ^
	-DCMAKE_ANDROID_ARCH_ABI=armeabi-v7a ^
	-DCMAKE_ANDROID_NDK=%NDK% ^
	-DCMAKE_SYSTEM_NAME=Android ^
	-DCMAKE_SYSTEM_VERSION=16 ^
	-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
	-DCMAKE_MAKE_PROGRAM=%NINJA% ^
	-DCMAKE_BUILD_TYPE=Release ^
	..\..\BeefRT
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR	
cd ..

mkdir android_arm64_d
cd android_arm64_d
cmake -GNinja ^
	-DANDROID_ABI:UNINITIALIZED=arm64-v8a ^
	-DANDROID_NDK=%NDK% ^
	-DANDROID_PLATFORM=android-21 ^
	-DCMAKE_ANDROID_ARCH_ABI=arm64-v8a ^
	-DCMAKE_ANDROID_NDK=%NDK% ^
	-DCMAKE_SYSTEM_NAME=Android ^
	-DCMAKE_SYSTEM_VERSION=21 ^
	-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
	-DCMAKE_MAKE_PROGRAM=%NINJA% ^
	-DCMAKE_BUILD_TYPE=Debug ^
	..\..\BeefRT
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR	
cd ..

mkdir android_arm64
cd android_arm64
cmake -GNinja ^
	-DANDROID_ABI:UNINITIALIZED=arm64-v8a ^
	-DANDROID_NDK=%NDK% ^
	-DANDROID_PLATFORM=android-21 ^
	-DCMAKE_ANDROID_ARCH_ABI=arm64-v8a ^
	-DCMAKE_ANDROID_NDK=%NDK% ^
	-DCMAKE_SYSTEM_NAME=Android ^
	-DCMAKE_SYSTEM_VERSION=21 ^
	-DCMAKE_TOOLCHAIN_FILE=%NDK%\build\cmake\android.toolchain.cmake ^
	-DCMAKE_MAKE_PROGRAM=%NINJA% ^
	-DCMAKE_BUILD_TYPE=Release ^
	..\..\BeefRT
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR	
cd ..

@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
:DO_BUILD

cd android_x86_d
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build .
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cd ..

cd android_x86
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build .
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cd ..

cd android_x86_64_d
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build .
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cd ..

cd android_x86_64
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build .
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cd ..

cd android_arm_d
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build .
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cd ..

cd android_arm
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build .
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cd ..

cd android_arm64_d
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build .
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cd ..

cd android_arm64
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cmake --build .
@IF %ERRORLEVEL% NEQ 0 GOTO HADERROR
cd ..

:SUCCESS
@ECHO SUCCESS!
@POPD
@EXIT /b 0

:HADERROR
@ECHO =================FAILED=================
@POPD
@EXIT /b %ERRORLEVEL%