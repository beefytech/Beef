cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

################### Variables. ####################
# Change if you want modify path or other values. #
###################################################

set(PROJECT_NAME BeefRT)
# Output Variables
set(OUTPUT_DEBUG Debug/bin)
set(CMAKE_DEBUG_POSTFIX "_d")
set(OUTPUT_RELEASE Release/bin)

############## CMake Project ################
#        The main options of project        #
#############################################

project(${PROJECT_NAME} CXX C)

# Define Release by default.
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
   -DBFRT_NODBGFLAGS
   -DBFRTMERGED
)

if (${IOS})  
  include_directories(
    .
    ../BeefySysLib/  
    ../BeefySysLib/third_party
    ../BeefySysLib/third_party/freetype/include
    ../BeefySysLib/third_party/libffi/build_iphoneos-arm64/include
    ../  
    ../extern
    ../extern/llvm/include  
    ../extern/llvm_linux/include
    ../extern/llvm/lib/Target

    ../BeefySysLib/platform/ios
  )
elseif (${APPLE})
  include_directories(
    .
    ../BeefySysLib/  
    ../BeefySysLib/third_party
    ../BeefySysLib/third_party/freetype/include
    ../BeefySysLib/third_party/libffi/x86_64-apple-darwin12.5.0/include
    ../  
    ../extern
    ../extern/llvm/include  
    ../extern/llvm_linux/include
    ../extern/llvm/lib/Target

    ../BeefySysLib/platform/osx
  )
elseif (${ANDROID})
  if (ANDROID_ABI STREQUAL "x86")
    include_directories(
      .
      ../BeefySysLib/  
      ../BeefySysLib/third_party
      ../BeefySysLib/third_party/freetype/include
      ../BeefySysLib/third_party/libffi/i686-pc-linux-gnu/include
      ../  
      ../extern
      ../extern/llvm/include  
      ../extern/llvm_linux/include
      ../extern/llvm/lib/Target

      ../BeefySysLib/platform/android
    )
  elseif (ANDROID_ABI STREQUAL "x86_64")
    include_directories(
      .
      ../BeefySysLib/  
      ../BeefySysLib/third_party
      ../BeefySysLib/third_party/freetype/include
      ../BeefySysLib/third_party/libffi/x86_64-pc-linux-gnu/include
      ../  
      ../extern
      ../extern/llvm/include  
      ../extern/llvm_linux/include
      ../extern/llvm/lib/Target

      ../BeefySysLib/platform/android
    )
  elseif (ANDROID_ABI STREQUAL "armeabi-v7a")
    include_directories(
      .
      ../BeefySysLib/  
      ../BeefySysLib/third_party
      ../BeefySysLib/third_party/freetype/include
      ../BeefySysLib/third_party/libffi/arm-unknown-linux-gnu/include
      ../  
      ../extern
      ../extern/llvm/include  
      ../extern/llvm_linux/include
      ../extern/llvm/lib/Target

      ../BeefySysLib/platform/android
    )
  else()    
    include_directories(
      .
      ../BeefySysLib/  
      ../BeefySysLib/third_party
      ../BeefySysLib/third_party/freetype/include
      ../BeefySysLib/third_party/libffi/aarch64-unknown-linux-gnu/include
      ../  
      ../extern
      ../extern/llvm/include  
      ../extern/llvm_linux/include
      ../extern/llvm/lib/Target

      ../BeefySysLib/platform/android
    )
  endif()
else()
  include_directories(
    .
    ../BeefySysLib/  
    ../BeefySysLib/third_party
    ../BeefySysLib/third_party/freetype/include
    ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/include
    ../  
    ../extern
    ../extern/llvm/include  
    ../extern/llvm_linux/include
    ../extern/llvm/lib/Target

    ../BeefySysLib/platform/linux
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
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fno-rtti -Wno-multichar -Wno-invalid-offsetof")  
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  endif()
endif(NOT MSVC)

################ Files ################
#   --   Add files to project.   --   #
#######################################

file(GLOB SRC_FILES        
    rt/Internal.cpp
    rt/Chars.cpp
    rt/Object.cpp
    rt/Math.cpp
    rt/StompAlloc.cpp
    rt/Thread.cpp
    rt/ThreadLocalStorage.cpp
    dbg/DbgInternal.cpp
    dbg/gc.cpp
    dbg/gc_raw.cpp

    ../BeefySysLib/Common.cpp
    ../BeefySysLib/util/BeefPerf.cpp
    ../BeefySysLib/util/String.cpp
    ../BeefySysLib/util/UTF8.cpp
    ../BeefySysLib/util/Hash.cpp    
    ../BeefySysLib/third_party/utf8proc/utf8proc.c    
)

if (${IOS})
    file(GLOB SRC_FILES_OS
        ../BeefySysLib/platform/darwin/BFPlatform.cpp
        ../BeefySysLib/platform/darwin/DarwinCommon.cpp
        ../BeefySysLib/third_party/libffi/build_iphoneos-arm64/src/.libs/prep_cif.o
        ../BeefySysLib/third_party/libffi/build_iphoneos-arm64/src/.libs/types.o
        ../BeefySysLib/third_party/libffi/build_iphoneos-arm64/src/.libs/raw_api.o
        ../BeefySysLib/third_party/libffi/build_iphoneos-arm64/src/.libs/java_raw_api.o
        ../BeefySysLib/third_party/libffi/build_iphoneos-arm64/src/.libs/closures.o
        ../BeefySysLib/third_party/libffi/build_iphoneos-arm64/src/aarch64/.libs/ffi.o
        ../BeefySysLib/third_party/libffi/build_iphoneos-arm64/src/aarch64/.libs/sysv.o
    )
    # Simulator
    # src/prep_cif.o src/types.o src/raw_api.o src/java_raw_api.o src/closures.o src/x86/ffi64.o src/x86/unix64.o src/x86/ffiw64.o src/x86/win64.o
elseif (${APPLE})
    file(GLOB SRC_FILES_OS
        ../BeefySysLib/platform/darwin/BFPlatform.cpp
        ../BeefySysLib/platform/darwin/DarwinCommon.cpp
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/prep_cif.o
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/types.o
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/raw_api.o
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/java_raw_api.o
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/closures.o
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/x86/ffi64.o
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/x86/unix64.o
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/x86/ffiw64.o
        ../BeefySysLib/third_party/libffi/build_macosx-x86_64/src/x86/win64.o
    )
elseif (${ANDROID})
  file(GLOB SRC_FILES_OS
        ../BeefySysLib/platform/android/BFPlatform.cpp
        ../BeefySysLib/platform/android/AndroidCommon.cpp
    )
else()
    file(GLOB SRC_FILES_OS
        ../BeefySysLib/platform/linux/BFPlatform.cpp
        ../BeefySysLib/platform/linux/LinuxCommon.cpp
        ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/src/prep_cif.o
        ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/src/types.o
        ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/src/raw_api.o
        ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/src/java_raw_api.o
        ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/src/closures.o
        ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/src/x86/ffi64.o
        ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/src/x86/unix64.o
        ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/src/x86/ffiw64.o
    )
endif()

# Add library to build.
add_library(${PROJECT_NAME} STATIC
   ${SRC_FILES}
   ${SRC_FILES_OS}
)

if (${APPLE})
  target_link_libraries(${PROJECT_NAME} pthread ffi)
elseif (${ANDROID})
  set(TARGET_LIBS_OS "")
  #target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../BeefySysLib/third_party/libffi/aarch64-unknown-linux-gnu/.libs/libffi.a)
  #target_link_libraries(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/../BeefySysLib/third_party/libffi/i686-pc-linux-gnu/.libs/libffi.a)
else()
  target_link_libraries(${PROJECT_NAME} pthread ffi backtrace)
endif()

# Link with other dependencies.
if(MSVC)
  target_link_libraries(${PROJECT_NAME} BeefySysLib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib LLVMX86Disassembler.lib LLVMMCDisassembler.lib LLVMSupport.lib LLVMX86Info.lib LLVMX86Utils.lib LLVMX86AsmPrinter.lib LLVMX86Desc.lib %(AdditionalDependencies) LLVMMC.lib LLVMObject.lib LLVMCore.lib LLVMBitReader.lib LLVMAsmParser.lib LLVMMCParser.lib LLVMCodeGen.lib LLVMTarget.lib LLVMX86CodeGen.lib LLVMScalarOpts.lib LLVMInstCombine.lib LLVMSelectionDAG.lib LLVMProfileData.lib LLVMTransformUtils.lib LLVMAnalysis.lib LLVMX86AsmParser.lib LLVMAsmPrinter.lib LLVMBitWriter.lib LLVMVectorize.lib LLVMipo.lib LLVMInstrumentation.lib LLVMDebugInfoDWARF.lib LLVMDebugInfoPDB.lib LLVMDebugInfoCodeView.lib LLVMGlobalISel.lib LLVMBinaryFormat.lib libcurl_a.lib)
else()
  set(LLVM_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../extern/llvm_linux/lib")
  target_link_libraries(${PROJECT_NAME} dl
  )
endif()

#pthread ffi