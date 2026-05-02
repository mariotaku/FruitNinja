# Object-only cross build for ARM Thumb-2 verification.
#
# This CMakeLists pulls in selected .cpp files from the port and compiles
# each into a .o whose per-symbol assembly is then diffed against the
# original binary via ../asm-verify.py.
#
# Driven by ../run.sh; not intended for direct invocation. If you need to,
# from the project root:
#   cmake -S tools/asm-verify/cross-build -B ~/fn-build -G "Unix Makefiles" \
#         -DCMAKE_TOOLCHAIN_FILE=$(pwd)/tools/asm-verify/toolchain.cmake
#   cmake --build ~/fn-build -j$(nproc)

cmake_minimum_required(VERSION 3.20)
project(fnverify LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_EXTENSIONS OFF)

# This file lives at tools/asm-verify/cross-build/CMakeLists.txt; project
# root is three parents up.
set(_PROJECT_ROOT "${CMAKE_SOURCE_DIR}/../../..")
set(_ASM_VERIFY   "${CMAKE_SOURCE_DIR}/..")

# Selected cross-buildable TUs. Add more here as cross-headers/ stubs grow.
set(VERIFY_SOURCES
    "${_PROJECT_ROOT}/src/game/ScoreState.cpp"
    "${_ASM_VERIFY}/cross-build/demos/gameover_demo.cpp"
    "${_PROJECT_ROOT}/src/game/TimeModifier.cpp"
    "${_PROJECT_ROOT}/src/game/ScoreModifier.cpp"
    "${_PROJECT_ROOT}/src/game/WaveModifier.cpp"
    "${_PROJECT_ROOT}/src/game/SlashModifier.cpp"
    "${_PROJECT_ROOT}/src/game/GameOver.cpp"
    # Heavier TUs added in Step 2 once their C++11 features (range-for,
    # lambdas) were backported to GCC 4.4-compatible iterator/named-fn forms.
    "${_PROJECT_ROOT}/src/game/PowerUpManager.cpp"
    "${_PROJECT_ROOT}/src/game/PowerUp.cpp"
    "${_PROJECT_ROOT}/src/game/FruitSaveData.cpp"
    # Pending Step 2 follow-up (still has range-for / lambda / strdup):
    #   src/game/WaveManager.cpp
    #   src/screens/GameOverScreen.cpp
    #   src/screens/PauseScreen.cpp
)

add_library(fnverify OBJECT ${VERIFY_SOURCES})

target_include_directories(fnverify PRIVATE
    # Stubbed dependencies that aren't compatible with arm-none-eabi.
    "${_ASM_VERIFY}/cross-headers"
    # Real port headers, in include order matching the host build.
    "${_PROJECT_ROOT}/src"
    "${_PROJECT_ROOT}/src/engine"
    # Real third-party headers reachable from the host build dir.
    "${_PROJECT_ROOT}/build/_deps/tinyxml2-src"
)

set_target_properties(fnverify PROPERTIES LINKER_LANGUAGE CXX)

message(STATUS "fnverify OBJECTs will be at: ${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/fnverify.dir/...")
