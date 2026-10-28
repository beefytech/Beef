cmake_minimum_required(VERSION 3.12)

set(PROJECT_NAME DbgModuleCheck)
set(OUTPUT_DEBUG Debug/bin)
set(OUTPUT_RELEASE Release/bin)

project(${PROJECT_NAME} CXX)

set (CMAKE_CXX_STANDARD 17)

add_definitions(
   -DIDEHELPER_EXPORTS
   -DBFSYSLIB_DYNAMIC
   -DUNICODE
   -D_UNICODE
   -DBF_NO_FBX
   -DBF_DBG_64
)

include_directories(
  ../../
  ../../BeefySysLib/
  ../../BeefySysLib/third_party
  ../../BeefySysLib/platform/linux
  ../../IDEHelper
)

find_package(LLVM 22.1 CONFIG COMPONENTS)

if (LLVM_FOUND)
  include_directories(${LLVM_INCLUDE_DIRS})
  add_definitions(${LLVM_DEFINITIONS})
  link_directories(${LLVM_LIBRARY_DIRS})
else()
  message(FATAL_ERROR "LLVM not found")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-D_DEBUG)
  set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/${OUTPUT_DEBUG}")
else()
  set(EXECUTABLE_OUTPUT_PATH "${CMAKE_BINARY_DIR}/${OUTPUT_RELEASE}")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -Wno-multichar -Wno-invalid-offsetof")

add_executable(${PROJECT_NAME} DbgModuleCheck.cpp)

target_link_libraries(${PROJECT_NAME} IDEHelper BeefySysLib)
