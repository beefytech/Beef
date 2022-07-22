cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

################### Variables. ####################
# Change if you want modify path or other values. #
###################################################

set(PROJECT_NAME BeefRT)
# Output Variables
set(OUTPUT_DEBUG Debug/bin)
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
   -D_DEBUG
   -DIDEHELPER_EXPORTS
   -DBFSYSLIB_DYNAMIC
   -DUNICODE
   -D_UNICODE
   -DBF_NO_FBX
   -DFT2_BUILD_LIBRARY
   -DBFSYSLIB_DYNAMIC
)

include_directories(
  .
  ../BeefySysLib/
  ../BeefySysLib/platform/linux
  ../BeefySysLib/third_party
  ../BeefySysLib/third_party/freetype/include
  ../BeefySysLib/third_party/libffi/x86_64-unknown-linux-gnu/include
  ../
  ../extern
  ../extern/llvm/include
  ../extern/llvm_linux/include
  ../extern/llvm/lib/Target
  ../extern/llvm_linux/lib/Target/X86
)

############## Artefacts Output #################
# Defines outputs , depending Debug or Release. #
#################################################

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_DEBUG}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_DEBUG}")
  set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_DEBUG}")
else()
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_REL}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_REL}")
  set(CMAKE_EXECUTABLE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/${OUTPUT_REL}")
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

)

# Add library to build.
add_library(${PROJECT_NAME} SHARED
   ${SRC_FILES}
)

# Link with other dependencies.
if(MSVC)
  target_link_libraries(${PROJECT_NAME} BeefySysLib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib LLVMX86Disassembler.lib LLVMMCDisassembler.lib LLVMSupport.lib LLVMX86Info.lib LLVMX86Utils.lib LLVMX86AsmPrinter.lib LLVMX86Desc.lib %(AdditionalDependencies) LLVMMC.lib LLVMObject.lib LLVMCore.lib LLVMBitReader.lib LLVMAsmParser.lib LLVMMCParser.lib LLVMCodeGen.lib LLVMTarget.lib LLVMX86CodeGen.lib LLVMScalarOpts.lib LLVMInstCombine.lib LLVMSelectionDAG.lib LLVMProfileData.lib LLVMTransformUtils.lib LLVMAnalysis.lib LLVMX86AsmParser.lib LLVMAsmPrinter.lib LLVMBitWriter.lib LLVMVectorize.lib LLVMipo.lib LLVMInstrumentation.lib LLVMDebugInfoDWARF.lib LLVMDebugInfoPDB.lib LLVMDebugInfoCodeView.lib LLVMGlobalISel.lib LLVMBinaryFormat.lib libcurl_a.lib)
else()
  set(LLVM_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../extern/llvm_linux/lib")
  target_link_libraries(${PROJECT_NAME} BeefySysLib pthread dl backtrace ffi
  )
endif()
