cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

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

set (CMAKE_CXX_STANDARD 14)
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

find_package(LLVM 13 CONFIG COMPONENTS)

if (LLVM_FOUND)
  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

  include_directories(${LLVM_INCLUDE_DIRS})
  add_definitions(${LLVM_DEFINITIONS})

  set(TARGET_LIBS_OS "-lLLVM-13 ${LLVM_SYSTEM_LIBS}")
else()
  list(APPEND SRC_FILES
    X64.cpp
    X86.cpp
    X86Target.cpp
  )

  include_directories(h
    ../extern/llvm-project_18_1_4/llvm/include
    ../extern/llvm-project_18_1_4/llvm/lib/Target18_1_4
  )

  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    include_directories(
      ../extern/llvm_linux_18_1_4/include
      ../extern/llvm_linux_18_1_4/lib/Target/X86
      ../extern/llvm_linux_18_1_4/lib/Target/AArch64
    )
    set(LLVM_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../extern/llvm_linux_18_1_4/lib")
  else()
    include_directories(
      ../extern/llvm_linux_rel_18_1_4/include
      ../extern/llvm_linux_rel_18_1_4/lib/Target/X86
      ../extern/llvm_linux_rel_18_1_4/lib/Target/AArch64
    )
    set(LLVM_LIB "${CMAKE_CURRENT_SOURCE_DIR}/../extern/llvm_linux_rel_18_1_4/lib")
  endif()

  execute_process(
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/../extern/llvm_linux_18_1_4/bin/llvm-config --system-libs --link-static
    OUTPUT_VARIABLE LLVM_SYSTEM_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE EXEC_RESULT
  )

  if (EXEC_RESULT AND NOT EXEC_RESULT EQUAL 0)
    if (EXEC_RESULT MATCHES "^[0-9]+$")
      message(FATAL_ERROR "llvm-config exited with code ${EXEC_RESULT}.")
    else()
      message(FATAL_ERROR "llvm-config couldn't be executed: ${EXEC_RESULT}")
    endif()
  endif()

  message(STATUS "Found LLVM 18.1.4 (local build)")

  set(TARGET_LIBS_OS "${LLVM_SYSTEM_LIBS}")

  list(APPEND LLVM_LIBS
    ${LLVM_LIB}/libLLVMCore.a
    ${LLVM_LIB}/libLLVMCodeGen.a
    ${LLVM_LIB}/libLLVMMC.a
    ${LLVM_LIB}/libLLVMMCParser.a
    ${LLVM_LIB}/libLLVMMCDisassembler.a
    ${LLVM_LIB}/libLLVMObject.a
    ${LLVM_LIB}/libLLVMBitReader.a
    ${LLVM_LIB}/libLLVMAsmParser.a
    ${LLVM_LIB}/libLLVMTarget.a
    ${LLVM_LIB}/libLLVMScalarOpts.a
    ${LLVM_LIB}/libLLVMInstCombine.a
    ${LLVM_LIB}/libLLVMSelectionDAG.a
    ${LLVM_LIB}/libLLVMProfileData.a

    ${LLVM_LIB}/libLLVMAnalysis.a
    ${LLVM_LIB}/libLLVMAsmPrinter.a
    ${LLVM_LIB}/libLLVMBitWriter.a
    ${LLVM_LIB}/libLLVMVectorize.a
    ${LLVM_LIB}/libLLVMipo.a
    ${LLVM_LIB}/libLLVMInstrumentation.a
    ${LLVM_LIB}/libLLVMDebugInfoDWARF.a
    ${LLVM_LIB}/libLLVMDebugInfoPDB.a
    ${LLVM_LIB}/libLLVMDebugInfoCodeView.a
    ${LLVM_LIB}/libLLVMGlobalISel.a
    ${LLVM_LIB}/libLLVMTransformUtils.a
    ${LLVM_LIB}/libLLVMBinaryFormat.a
    ${LLVM_LIB}/libLLVMIRReader.a
    ${LLVM_LIB}/libLLVMLinker.a
    ${LLVM_LIB}/libLLVMAggressiveInstCombine.a

    ${LLVM_LIB}/libLLVMBitstreamReader.a
    ${LLVM_LIB}/libLLVMCFGuard.a
    ${LLVM_LIB}/libLLVMTextAPI.a
    ${LLVM_LIB}/libLLVMRemarks.a

    ${LLVM_LIB}/libLLVMX86Info.a
    ${LLVM_LIB}/libLLVMX86Desc.a
    ${LLVM_LIB}/libLLVMX86CodeGen.a
    ${LLVM_LIB}/libLLVMX86AsmParser.a
    ${LLVM_LIB}/libLLVMX86Disassembler.a

    ${LLVM_LIB}/libLLVMARMDesc.a
    ${LLVM_LIB}/libLLVMARMUtils.a
    ${LLVM_LIB}/libLLVMARMInfo.a
    ${LLVM_LIB}/libLLVMARMCodeGen.a
    ${LLVM_LIB}/libLLVMARMAsmParser.a
    ${LLVM_LIB}/libLLVMARMDisassembler.a

    ${LLVM_LIB}/libLLVMAArch64Desc.a
    ${LLVM_LIB}/libLLVMAArch64Utils.a
    ${LLVM_LIB}/libLLVMAArch64Info.a
    ${LLVM_LIB}/libLLVMAArch64CodeGen.a
    ${LLVM_LIB}/libLLVMAArch64AsmParser.a
    ${LLVM_LIB}/libLLVMAArch64Disassembler.a

    ${LLVM_LIB}/libLLVMWebAssemblyDesc.a
    ${LLVM_LIB}/libLLVMWebAssemblyInfo.a
    ${LLVM_LIB}/libLLVMWebAssemblyCodeGen.a
    ${LLVM_LIB}/libLLVMWebAssemblyAsmParser.a
    ${LLVM_LIB}/libLLVMWebAssemblyDisassembler.a
    ${LLVM_LIB}/libLLVMWebAssemblyUtils.a

    ${LLVM_LIB}/libLLVMSupport.a
    ${LLVM_LIB}/libLLVMDemangle.a
  )

  FOREACH (lib ${LLVM_LIBS})
    string(APPEND TARGET_LIBS_OS " " ${lib})
  ENDFOREACH()
endif()

# Add library to build.
add_library(${PROJECT_NAME} SHARED
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
