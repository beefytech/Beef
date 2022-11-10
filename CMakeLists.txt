cmake_minimum_required(VERSION 3.5)
project("Beef")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
  message(STATUS "Build type not specified: Use Debug by default.")
endif(NOT CMAKE_BUILD_TYPE)


if (DEFINED BF_ONLY_RUNTIME)

add_subdirectory(BeefRT)
add_subdirectory(BeefySysLib)

else()

add_subdirectory(BeefRT)
add_subdirectory(BeefySysLib)
add_subdirectory(extern/hunspell)
add_subdirectory(IDEHelper)
add_subdirectory(BeefBoot)


endif()