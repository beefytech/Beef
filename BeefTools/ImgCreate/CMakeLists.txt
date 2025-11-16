cmake_minimum_required(VERSION 3.5)

################### Variables. ####################
# Change if you want modify path or other values. #
###################################################

set(PROJECT_NAME ImgCreate)
# Output Variables
set(OUTPUT_DEBUG Debug/bin)
set(CMAKE_DEBUG_POSTFIX "_d")
set(OUTPUT_RELEASE Release/bin)

############## CMake Project ################
#        The main options of project        #
#############################################

project(${PROJECT_NAME} CXX C)

# Define Debug by default.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
  message(STATUS "Build type not specified: Use Debug by default.")
endif(NOT CMAKE_BUILD_TYPE)

include_directories(
    .
    ../../
    ../../BeefySysLib/
)

file(GLOB SRC_FILES
    ../../BeefySysLib/Common.cpp
    ../../BeefySysLib/BFApp.cpp
    ../../BeefySysLib/BFWindow.cpp
    ../../BeefySysLib/DataStream.cpp
    ../../BeefySysLib/FileStream.cpp
    ../../BeefySysLib/util/Hash.cpp
    ../../BeefySysLib/util/UTF8.cpp
    ../../BeefySysLib/util/String.cpp
    ../../BeefySysLib/util/PerfTimer.cpp
    ../../BeefySysLib/util/CubicFuncSpline.cpp
    ../../BeefySysLib/util/Vector.cpp
    ../../BeefySysLib/img/ImageAdjustments.cpp
    ../../BeefySysLib/img/ImageData.cpp
    ../../BeefySysLib/img/ImageUtils.cpp
    ../../BeefySysLib/img/ImgEffects.cpp
    ../../BeefySysLib/img/PNGData.cpp
    ../../BeefySysLib/img/PSDReader.cpp   
    ../../BeefySysLib/third_party/png/png.c
    ../../BeefySysLib/third_party/png/pngerror.c
    ../../BeefySysLib/third_party/png/pngget.c
    ../../BeefySysLib/third_party/png/pngmem.c
    ../../BeefySysLib/third_party/png/pngpread.c
    ../../BeefySysLib/third_party/png/pngread.c
    ../../BeefySysLib/third_party/png/pngrio.c
    ../../BeefySysLib/third_party/png/pngrtran.c
    ../../BeefySysLib/third_party/png/pngrutil.c
    ../../BeefySysLib/third_party/png/pngset.c
    ../../BeefySysLib/third_party/png/pngtrans.c
    ../../BeefySysLib/third_party/png/pngvcrd.c
    ../../BeefySysLib/third_party/png/pngwio.c
    ../../BeefySysLib/third_party/png/pngwrite.c
    ../../BeefySysLib/third_party/png/pngwtran.c
    ../../BeefySysLib/third_party/png/pngwutil.c
    ../../BeefySysLib/third_party/utf8proc/utf8proc.c
    ../../BeefySysLib/third_party/zlib/adler32.c
    ../../BeefySysLib/third_party/zlib/compress.c
    ../../BeefySysLib/third_party/zlib/crc32.c
    ../../BeefySysLib/third_party/zlib/deflate.c
    ../../BeefySysLib/third_party/zlib/gzio.c
    ../../BeefySysLib/third_party/zlib/infblock.c
    ../../BeefySysLib/third_party/zlib/infcodes.c
    ../../BeefySysLib/third_party/zlib/inffast.c
    ../../BeefySysLib/third_party/zlib/inflate.c
    ../../BeefySysLib/third_party/zlib/inftrees.c
    ../../BeefySysLib/third_party/zlib/infutil.c
    ../../BeefySysLib/third_party/zlib/trees.c
    ../../BeefySysLib/third_party/zlib/uncompr.c
    ../../BeefySysLib/third_party/zlib/zutil.c
    ../../BeefySysLib/third_party/putty/wildcard.c
)

if (${WIN32})
    include_directories(
        ../../BeefySysLib/platform/win32
    )
    file(GLOB SRC_FILES_OS
        ../../BeefySysLib/platform/win32/BFPlatform.cpp
        ../../BeefySysLib/platform/win32/Platform.cpp    
    )
elseif (${APPLE})
    include_directories(
        ../../BeefySysLib/platform/darwin
    )
    file(GLOB SRC_FILES_OS
        ../../BeefySysLib/platform/darwin/BFPlatform.cpp
        ../../BeefySysLib/platform/darwin/DarwinCommon.cpp    
    )
else()
    include_directories(
        ../../BeefySysLib/platform/linux
    )
    file(GLOB SRC_FILES_OS
        ../../BeefySysLib/platform/linux/BFPlatform.cpp
        ../../BeefySysLib/platform/linux/LinuxCommon.cpp    
    )
endif()

add_executable(${PROJECT_NAME} ImgCreate.cpp ${SRC_FILES_OS} ${SRC_FILES})