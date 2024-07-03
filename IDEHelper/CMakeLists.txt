cmake_minimum_required(VERSION 3.5)

################### Variables. ####################
# Change if you want modify path or other values. #
###################################################

set(PROJECT_NAME IDEHelper)
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
)

set (CMAKE_CXX_STANDARD 17)
#add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)

INCLUDE(CheckIncludeFiles)
CHECK_INCLUDE_FILES(backtrace.h HAVE_BACKTRACE_HEADERS)
if (HAVE_BACKTRACE_HEADERS)
   add_definitions(-DBFP_HAS_BACKTRACE)
endif ()

if (${APPLE})
  include_directories(
    .
    ../
    ../BeefySysLib/
    ../BeefySysLib/third_party
    ../BeefySysLib/third_party/freetype/include

    ../BeefySysLib/platform/osx
  )
else()
  include_directories(
    .
    ../
    ../BeefySysLib/
    ../BeefySysLib/third_party
    ../BeefySysLib/third_party/freetype/include

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
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -fno-rtti -Wno-multichar -Wno-invalid-offsetof")
endif(NOT MSVC)

################ Files ################
#   --   Add files to project.   --   #
#######################################

file(GLOB SRC_FILES
    BfDiff.cpp
    Debugger.cpp
    DebugManager.cpp
    DebugVisualizers.cpp
    NetManager.cpp
    SpellChecker.cpp
    Targets.cpp
    X86XmmInfo.cpp

    LinuxDebugger.cpp

    Beef/BfCommon.cpp
    Clang/CDepChecker.cpp
    Clang/ClangHelper.cpp
    Compiler/BfAst.cpp
    Compiler/BfAstAllocator.cpp
    Compiler/BfAutoComplete.cpp
    Compiler/BfCodeGen.cpp
    Compiler/BfCompiler.cpp
    Compiler/BfConstResolver.cpp
    Compiler/BfContext.cpp
    Compiler/BfDefBuilder.cpp
    Compiler/BfDeferEvalChecker.cpp
    Compiler/BfDemangler.cpp
    Compiler/BfElementVisitor.cpp
    Compiler/BfNamespaceVisitor.cpp
    Compiler/BfExprEvaluator.cpp
    Compiler/BfIRBuilder.cpp
    Compiler/BfIRCodeGen.cpp
    Compiler/BfMangler.cpp
    Compiler/BfModule.cpp
    Compiler/BfModuleTypeUtils.cpp
    Compiler/BfParser.cpp
    Compiler/BfPrinter.cpp
    Compiler/BfReducer.cpp
    Compiler/BfResolvedTypeUtils.cpp
    Compiler/BfResolvePass.cpp
    Compiler/BfSource.cpp
    Compiler/BfSourceClassifier.cpp
    Compiler/BfSourcePositionFinder.cpp
    Compiler/BfStmtEvaluator.cpp
    Compiler/BfSystem.cpp
    Compiler/BfUtil.cpp
    Compiler/BfVarDeclChecker.cpp
    Compiler/BfTargetTriple.cpp
    Compiler/CeMachine.cpp
    Compiler/CeDebugger.cpp
    Compiler/MemReporter.cpp

    Backend/BeContext.cpp
    Backend/BeIRCodeGen.cpp
    Backend/BeModule.cpp
)

find_package(LLVM 18.1 CONFIG COMPONENTS)

if (LLVM_FOUND)
  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

  include_directories(${LLVM_INCLUDE_DIRS})
  add_definitions(${LLVM_DEFINITIONS})

  set(STRIP ${TARGET_LIBS_OS} "-lLLVM-18 ${LLVM_SYSTEM_LIBS}")
else()
  message(FATAL_ERROR "LLVM not found")
endif()

# Add library to build.
add_library(${PROJECT_NAME} STATIC
   ${SRC_FILES}
)

if (HAVE_BACKTRACE_HEADERS)
  string(APPEND TARGET_LIBS_OS " -lbacktrace")
  string(STRIP ${TARGET_LIBS_OS} TARGET_LIBS_OS)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug" )
  FILE(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/../IDE/dist/IDEHelper_libs_d.txt" ${TARGET_LIBS_OS})
else()
  FILE(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/../IDE/dist/IDEHelper_libs.txt" ${TARGET_LIBS_OS})
endif()

# Link with other dependencies.
if(MSVC)
  target_link_libraries(${PROJECT_NAME} BeefySysLib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib LLVMX86Disassembler.lib LLVMMCDisassembler.lib LLVMSupport.lib LLVMX86Info.lib LLVMX86Desc.lib %(AdditionalDependencies) LLVMMC.lib LLVMObject.lib LLVMCore.lib LLVMBitReader.lib LLVMAsmParser.lib LLVMMCParser.lib LLVMCodeGen.lib LLVMTarget.lib LLVMX86CodeGen.lib LLVMScalarOpts.lib LLVMInstCombine.lib LLVMSelectionDAG.lib LLVMProfileData.lib LLVMTransformUtils.lib LLVMAnalysis.lib LLVMX86AsmParser.lib LLVMAsmPrinter.lib LLVMBitWriter.lib LLVMVectorize.lib LLVMipo.lib LLVMInstrumentation.lib LLVMDebugInfoDWARF.lib LLVMDebugInfoPDB.lib LLVMDebugInfoCodeView.lib LLVMGlobalISel.lib LLVMBinaryFormat.lib LLVMAggressiveInstCombine.lib libcurl_a.lib)
else()
  target_link_libraries(${PROJECT_NAME} BeefySysLib hunspell pthread dl ${TARGET_LIBS_OS})
endif()
