# Object-only cross build for ARM Thumb-2 verification.
#
# This CMakeLists is independent of the host build at the project root; it
# pulls in only the .cpp files we want to verify against the original binary
# and compiles each into an .o whose per-symbol assembly is then diffed
# against Ghidra's disassembly via tools/asm-verify.py.
#
# Configure (from the project root):
#   cmake -S cross-build -B build-bada-cross -G "MSYS Makefiles" \
#         -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm-bada.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#
# Build:
#   cmake --build build-bada-cross -j$(nproc)
#
# The OBJECT library "fnverify" produces .o files in
#   build-bada-cross/CMakeFiles/fnverify.dir/<src>.cpp.obj
# which tools/asm-verify.py then objdumps per-symbol.

cmake_minimum_required(VERSION 3.20)
project(fnverify LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

set(_PROJECT_ROOT "${CMAKE_SOURCE_DIR}/..")

# Phase A:
#  - ScoreState.cpp -- two trivial globals (BSS / .data); minimal smoke.
#  - gameover_demo.cpp -- standalone reduction of WaveManager::GameOver
#    (before/after) to verify control-flow shape against binary @ 0x121f74
#    without pulling in all of WaveManager.cpp's std::vector<...> baggage
#    that would trip libstdc++ 4.5.3's known move_iterator bug.
# WaveManager.cpp full TU is deferred to Phase B (needs PROBABILITY_OVERIDE
# shim or 4.4.1 toolchain via WSL with file staging).
set(VERIFY_SOURCES
    "${_PROJECT_ROOT}/src/game/ScoreState.cpp"
    "${CMAKE_SOURCE_DIR}/demos/gameover_demo.cpp"
    "${_PROJECT_ROOT}/src/game/TimeModifier.cpp"
    "${_PROJECT_ROOT}/src/game/ScoreModifier.cpp"
    "${_PROJECT_ROOT}/src/game/WaveModifier.cpp"
    "${_PROJECT_ROOT}/src/game/SlashModifier.cpp"
    "${_PROJECT_ROOT}/src/game/GameOver.cpp"
)

add_library(fnverify OBJECT ${VERIFY_SOURCES})

target_include_directories(fnverify PRIVATE
    # Stubbed dependencies that aren't compatible with arm-none-eabi.
    "${_PROJECT_ROOT}/cross-headers"
    # Real port headers (in include order matching the host build).
    "${_PROJECT_ROOT}/src"
    "${_PROJECT_ROOT}/src/engine"
    # Real third-party headers reachable from the host build dir.
    "${_PROJECT_ROOT}/build/_deps/tinyxml2-src"
)

# Don't try to link anything at all -- OBJECT libraries never invoke the
# linker, and our toolchain file disables the link recipes anyway.
set_target_properties(fnverify PROPERTIES
    LINKER_LANGUAGE CXX
)

# Optional: make the .o objects easy to find (they go into
# CMakeFiles/fnverify.dir/...). Print the path at configure time.
message(STATUS "fnverify OBJECTs will be at: ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/fnverify.dir/...")
