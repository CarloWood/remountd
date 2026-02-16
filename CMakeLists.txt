cmake_minimum_required(VERSION 3.16...4.2.3)

project(remountd
        LANGUAGES CXX
        DESCRIPTION "Socket-activated daemon to remount preconfigured mount points ro/rw; controlled by an unprivileged client.")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Exit if someone tries to contaminates the source directory with an in-source build.
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "Please do out-of-source builds.\nCleanup: \"rm -rf CMake CMakeCache.txt CMakeFiles/\"")
endif()

add_subdirectory(src)
