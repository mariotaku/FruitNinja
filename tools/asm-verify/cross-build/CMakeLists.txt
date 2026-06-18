# Object-only cross build for ARM Thumb-2 verification.
#
# Thin wrapper that delegates to the root CMakeLists.txt with
# FN_ASM_VERIFY=ON. The main build handles source file discovery,
# platform-file exclusion, and stub dependency injection.
#
# Driven by ../run.sh; not intended for direct invocation. If you
# need to, from the project root:
#   cmake -S tools/asm-verify/cross-build -B ~/fn-build -G "Unix Makefiles" \
#         -DCMAKE_TOOLCHAIN_FILE=$(pwd)/tools/asm-verify/toolchain.cmake
#   cmake --build ~/fn-build -j$(nproc)

cmake_minimum_required(VERSION 3.20)
project(fnverify LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

# This file lives at tools/asm-verify/cross-build/CMakeLists.txt.
# Project root is three parents up.
get_filename_component(_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
get_filename_component(_ASM_VERIFY   "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Activate verify mode in the root CMakeLists.txt.
set(FN_ASM_VERIFY ON CACHE BOOL "" FORCE)

# Delegate to the main build. All portable sources come from the
# subsystem CMakeLists.txt files (with platform files excluded via
# if(NOT FN_ASM_VERIFY) guards).
add_subdirectory("${_PROJECT_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/FruitNinja")

# Demo verification file (not part of the game library).
add_library(fnverify_demo OBJECT demos/gameover_demo.cpp)
target_include_directories(fnverify_demo PRIVATE "${_PROJECT_ROOT}/src")

message(STATUS "fnverify OBJECTs from root CMakeLists (FN_ASM_VERIFY=ON)")
