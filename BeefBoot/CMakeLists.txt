cmake_minimum_required(VERSION 3.0.0 FATAL_ERROR)

################### Variables. ####################
# Change if you want modify path or other values. #
###################################################

set(PROJECT_NAME BeefBoot)
# Output Variables
set(OUTPUT_DEBUG Debug/bin)
set(OUTPUT_RELEASE Release/bin)

############## CMake Project ################
#        The main options of project        #
#############################################

project(${PROJECT_NAME} CXX C)

set (CMAKE_CXX_STANDARD 17)
add_definitions(-D_GLIBCXX_USE_CXX11_ABI=1)

#set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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

if (${APPLE})
  include_directories(
    .
    ../
    ../BeefySysLib/
    ../BeefySysLib/third_party
    ../BeefySysLib/third_party/freetype/include
    ../IDEHelper

    ../BeefySysLib/platform/osx
  )
else()
  include_directories(
    .
    ../
    ../BeefySysLib/
    ../BeefySysLib/third_party
    ../BeefySysLib/third_party/freetype/include
    ../IDEHelper

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
  set(EXECUTABLE_OUTPUT_PATH  "${CMAKE_BINARY_DIR}/${OUTPUT_DEBUG}")
else()
  set(EXECUTABLE_OUTPUT_PATH  "${CMAKE_BINARY_DIR}/${OUTPUT_RELEASE}")
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
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -Wno-multichar -Wno-invalid-offsetof")
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    #set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  endif()
endif(NOT MSVC)

################ Files ################
#   --   Add files to project.   --   #
#######################################

file(GLOB SRC_FILES
    BeefBoot.cpp
    BootApp.cpp
)

# Add executable to build.
add_executable(${PROJECT_NAME}
   ${SRC_FILES}
)

find_package(LLVM 18.1 CONFIG COMPONENTS)

if (LLVM_FOUND)
  message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
  message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

  include_directories(${LLVM_INCLUDE_DIRS})
  add_definitions(${LLVM_DEFINITIONS})

  set(TARGET_LIBS_OS "-lLLVM-18")
else()
  message(FATAL_ERROR "LLVM not found")
endif()

if (${APPLE})
    set(TARGET_LIBS_OS "")
else()
    #set(TARGET_LIBS_OS "curses")
    #set(TARGET_LIBS_OS "-Xlinker --no-demangle -v")

    set(TARGET_LIBS_OS "${LLVM_SYSTEM_LIBS}")
endif()

# Link with other dependencies.
if(MSVC)
    target_link_libraries(${PROJECT_NAME} BeefySysLib IDEHelper kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib)
else()
    target_link_libraries(${PROJECT_NAME} BeefySysLib
      IDEHelper
      ${TARGET_LIBS_OS}
    )
endif()
