cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

################### Variables. ####################
# Change if you want modify path or other values. #
###################################################

set(PROJECT_NAME BeefySysLib)
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

# Definition of Macros
add_definitions(
   -DIDEHELPER_EXPORTS
   -DBFSYSLIB_DYNAMIC
   -DUNICODE
   -D_UNICODE
   -DBF_NO_FBX
   -DFT2_BUILD_LIBRARY
   -DBFSYSLIB_DYNAMIC
   -DBP_DYNAMIC
)

INCLUDE(CheckIncludeFileCXX)
CHECK_INCLUDE_FILE_CXX(backtrace.h HAVE_BACKTRACE_HEADERS)
if (HAVE_BACKTRACE_HEADERS)
   add_definitions(-DBFP_HAS_BACKTRACE)
endif ()

set (CMAKE_CXX_STANDARD 11)

if (${APPLE})
    include_directories(
      .
      platform/osx
      platform/darwin
      third_party
      third_party/freetype/include
      ../extern
    )
else()
    include_directories(
      .
      platform/linux
      third_party
      third_party/freetype/include
      ../extern
    )
endif()

############## Artefacts Output #################
# Defines outputs , depending Debug or Release. #
#################################################

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(
    -D_DEBUG
  )
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_DEBUG}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_DEBUG}")
  set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_DEBUG}")
else()
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_RELEASE}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_RELEASE}")
  set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_RELEASE}")
endif()

################### Dependencies ##################
# Add Dependencies to project.                    #
###################################################

option(BUILD_DEPENDS
   "Build other CMake project."
   ON
)

# Dependencies : disable BUILD_DEPENDS to link with lib already build.
if(BUILD_DEPENDS)

else()

endif()

################# Flags ################
# Defines Flags for Windows and Linux. #
########################################

if(MSVC)
   set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /W3 /MD /MDd /Od /EHsc")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /W3 /GL /Od /Oi /Gy /EHsc")
endif(MSVC)
if(NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wno-multichar")
endif(NOT MSVC)

if (DEFINED BF_ENABLE_SDL)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBF_ENABLE_SDL")
endif()

################ Files ################
#   --   Add files to project.   --   #
#######################################

file(GLOB SRC_FILES
    BeefySysLib.cpp
    BFApp.cpp
    BFWindow.cpp
    CachedDataStream.cpp
    Common.cpp
    DataStream.cpp
    FileStream.cpp    
    MemStream.cpp
    ResLib.cpp
    Startup.cpp

    fbx/FBXReader.cpp
    gfx/DrawLayer.cpp
    gfx/FTFont.cpp
    gfx/ModelDef.cpp
    gfx/ModelInstance.cpp
    gfx/RenderCmd.cpp
    gfx/RenderDevice.cpp
    gfx/RenderTarget.cpp
    gfx/Shader.cpp
    gfx/Texture.cpp
    img/BFIData.cpp
    img/ImageAdjustments.cpp
    img/ImageData.cpp
    img/ImageUtils.cpp
    img/ImgEffects.cpp
    img/JPEGData.cpp
    img/PNGData.cpp
    img/PSDReader.cpp
    img/PVRData.cpp
    img/TGAData.cpp

    third_party/freetype/src/autofit/autofit.c
    third_party/freetype/src/base/ftbase.c
    third_party/freetype/src/base/ftbbox.c
    third_party/freetype/src/base/ftbitmap.c
    third_party/freetype/src/base/ftdebug.c
    third_party/freetype/src/base/ftfntfmt.c
    third_party/freetype/src/base/ftfstype.c
    third_party/freetype/src/base/ftgasp.c
    third_party/freetype/src/base/ftglyph.c
    third_party/freetype/src/base/ftgxval.c
    third_party/freetype/src/base/ftinit.c
    third_party/freetype/src/base/ftlcdfil.c
    third_party/freetype/src/base/ftmm.c
    third_party/freetype/src/base/ftotval.c
    third_party/freetype/src/base/ftpatent.c
    third_party/freetype/src/base/ftstroke.c
    third_party/freetype/src/base/ftsynth.c
    third_party/freetype/src/base/ftsystem.c
    third_party/freetype/src/base/fttype1.c
    third_party/freetype/src/base/ftwinfnt.c
    third_party/freetype/src/bdf/bdf.c
    third_party/freetype/src/cache/ftcache.c
    third_party/freetype/src/cff/cff.c
    third_party/freetype/src/cid/type1cid.c
    third_party/freetype/src/gzip/ftgzip.c
    third_party/freetype/src/lzw/ftlzw.c
    third_party/freetype/src/pcf/pcf.c
    third_party/freetype/src/pfr/pfr.c
    third_party/freetype/src/psaux/psaux.c
    third_party/freetype/src/pshinter/pshinter.c
    third_party/freetype/src/psnames/psmodule.c
    third_party/freetype/src/raster/raster.c
    third_party/freetype/src/sfnt/sfnt.c
    third_party/freetype/src/smooth/smooth.c
    third_party/freetype/src/truetype/truetype.c
    third_party/freetype/src/type1/type1.c
    third_party/freetype/src/type42/type42.c
    third_party/freetype/src/winfonts/winfnt.c
    third_party/jpeg/cdjpeg.c
    third_party/jpeg/jcapimin.c
    third_party/jpeg/jcapistd.c
    third_party/jpeg/jccoefct.c
    third_party/jpeg/jccolor.c
    third_party/jpeg/jcdctmgr.c
    third_party/jpeg/jchuff.c
    third_party/jpeg/jcinit.c
    third_party/jpeg/jcmainct.c
    third_party/jpeg/jcmarker.c
    third_party/jpeg/jcmaster.c
    third_party/jpeg/jcomapi.c
    third_party/jpeg/jcparam.c
    third_party/jpeg/jcphuff.c
    third_party/jpeg/jcprepct.c
    third_party/jpeg/jcsample.c
    third_party/jpeg/jctrans.c
    third_party/jpeg/jdapimin.c
    third_party/jpeg/jdapistd.c
    third_party/jpeg/jdatadst.c
    third_party/jpeg/jdatasrc.c
    third_party/jpeg/jdcoefct.c
    third_party/jpeg/jdcolor.c
    third_party/jpeg/jddctmgr.c
    third_party/jpeg/jdhuff.c
    third_party/jpeg/jdinput.c
    third_party/jpeg/jdmainct.c
    third_party/jpeg/jdmarker.c
    third_party/jpeg/jdmaster.c
    third_party/jpeg/jdmerge.c
    third_party/jpeg/jdphuff.c
    third_party/jpeg/jdpostct.c
    third_party/jpeg/jdsample.c
    third_party/jpeg/jdtrans.c
    third_party/jpeg/jerror.c
    third_party/jpeg/jfdctflt.c
    third_party/jpeg/jfdctfst.c
    third_party/jpeg/jfdctint.c
    third_party/jpeg/jidctflt.c
    third_party/jpeg/jidctfst.c
    third_party/jpeg/jidctint.c
    third_party/jpeg/jidctred.c
    third_party/jpeg/jmemansi.c
    third_party/jpeg/jmemmgr.c
    third_party/jpeg/jquant1.c
    third_party/jpeg/jquant2.c
    third_party/jpeg/jutils.c
    third_party/jpeg/rdbmp.c
    third_party/jpeg/rdcolmap.c
    third_party/jpeg/rdgif.c
    third_party/jpeg/rdppm.c
    third_party/jpeg/rdrle.c
    third_party/jpeg/rdswitch.c
    third_party/jpeg/rdtarga.c
    third_party/jpeg/transupp.c
    third_party/jpeg/wrbmp.c
    third_party/jpeg/wrgif.c
    third_party/jpeg/wrppm.c
    third_party/jpeg/wrrle.c
    third_party/jpeg/wrtarga.c
    third_party/png/png.c
    third_party/png/pngerror.c
    third_party/png/pngget.c
    third_party/png/pngmem.c
    third_party/png/pngpread.c
    third_party/png/pngread.c
    third_party/png/pngrio.c
    third_party/png/pngrtran.c
    third_party/png/pngrutil.c
    third_party/png/pngset.c
    third_party/png/pngtrans.c
    third_party/png/pngvcrd.c
    third_party/png/pngwio.c
    third_party/png/pngwrite.c
    third_party/png/pngwtran.c
    third_party/png/pngwutil.c
    third_party/utf8proc/utf8proc.c
    third_party/zlib/adler32.c
    third_party/zlib/compress.c
    third_party/zlib/crc32.c
    third_party/zlib/deflate.c
    third_party/zlib/gzio.c
    third_party/zlib/infblock.c
    third_party/zlib/infcodes.c
    third_party/zlib/inffast.c
    third_party/zlib/inflate.c
    third_party/zlib/inftrees.c
    third_party/zlib/infutil.c
    third_party/zlib/trees.c
    third_party/zlib/uncompr.c
    third_party/zlib/zutil.c
    third_party/miniz/miniz.c
    third_party/putty/wildcard.c
    util/AllocDebug.cpp
    util/BeefPerf.cpp
    util/BSpline.cpp
    util/CatmullRom.cpp
    util/ChunkedDataBuffer.cpp
    util/Compress.cpp
    util/CubicFuncSpline.cpp
    util/CubicSpline.cpp
    util/FileEnumerator.cpp
    util/Hash.cpp
    util/Heap.cpp
    util/Json.cpp
    util/MappedFile.cpp
    util/MathUtils.cpp
    util/Matrix4.cpp
    util/PerfTimer.cpp
    util/Point.cpp
    util/PolySpline.cpp
    util/Quaternion.cpp
    util/Sphere.cpp
    util/String.cpp
    util/StackHelper.cpp
    util/ThreadPool.cpp
    util/UTF8.cpp
    util/WorkThread.cpp
    util/Vector.cpp
    util/ZipFile.cpp
)

if (${APPLE})
    file(GLOB SRC_FILES_OS
        platform/darwin/BFPlatform.cpp
        platform/darwin/DarwinCommon.cpp
        HeadlessApp.cpp
    )
else()
    file(GLOB SRC_FILES_OS
        platform/linux/BFPlatform.cpp
        platform/linux/LinuxCommon.cpp
        HeadlessApp.cpp
    )
endif()

if (DEFINED BF_ENABLE_SDL)
    file(GLOB SRC_FILES_OS
        platform/sdl/SdlBFApp.cpp
        platform/sdl/GLRenderDevice.cpp
    )
endif()

# Add library to build.
add_library(${PROJECT_NAME} SHARED
   ${SRC_FILES}
   ${SRC_FILES_OS}
)

# Link with other dependencies.
if(MSVC)
   target_link_libraries(${PROJECT_NAME} imm32.lib version.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib
        )
endif(MSVC)
