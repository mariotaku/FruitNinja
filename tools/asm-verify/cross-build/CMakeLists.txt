# Object-only cross build for ARM Thumb-2 verification.
#
# Thin wrapper that delegates to the root CMakeLists.txt (for dependency
# stubs, tinyxml2 headers, and cross-headers). The actual source list
# lives in ../verify-sources.cmake — a single, curated list of files
# known to cross-compile under Sourcery 2010q1.
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
get_filename_component(_PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
get_filename_component(_ASM_VERIFY   "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

# Activate verify mode in the root CMakeLists.txt (gates deps, exe, tests).
set(FN_ASM_VERIFY ON CACHE BOOL "" FORCE)

# Delegate to the main build for infrastructure:
#   - tinyxml2 headers (FetchContent_Populate)
#   - SDL2/Freetype INTERFACE stub targets
#   - cross-headers include path on fruit-ninja-game
add_subdirectory("${_PROJECT_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/FruitNinja")

# Curated source list — files known to cross-compile under Sourcery 2010q1.
include("${_ASM_VERIFY}/verify-sources.cmake")

add_library(fnverify OBJECT ${VERIFY_SOURCES})

target_include_directories(fnverify PRIVATE
    "${_ASM_VERIFY}/cross-headers"
    "${_PROJECT_ROOT}/src"
    "${_PROJECT_ROOT}/src/engine"
    # verify.sh copies host build/_deps/tinyxml2-src into the Docker
    # staging area, so this path exists even without FetchContent.
    "${_PROJECT_ROOT}/build/_deps/tinyxml2-src"
)

set_target_properties(fnverify PROPERTIES LINKER_LANGUAGE CXX)

# Force recompilation of key TUs on every cmake --build run.
add_custom_target(force_struct_recompile ALL
    COMMAND "${CMAKE_COMMAND}" -E touch
        "${_PROJECT_ROOT}/src/game/PowerUp.cpp"
        "${_PROJECT_ROOT}/src/game/WaveManager.cpp"
        "${_PROJECT_ROOT}/src/game/ScoreModifier.cpp"
    COMMENT "Touching struct-dep TUs to ensure recompile after header edits"
)
add_dependencies(fnverify force_struct_recompile)

message(STATUS "fnverify OBJECTs will be at: ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/fnverify.dir/...")
