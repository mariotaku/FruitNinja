# PS3 platform backend -- only included when FRUIT_PLATFORM_PS3 is ON
# (set by src/platform/CMakeLists.txt or by cmake/ps3.toolchain.cmake).
# This entire file is a no-op under host/web/webOS/Wii/asm-verify configs.
#
# Scaffolding only for now. Real sources will be added as the port progresses.
# Requires PSL1GHT + ps3toolchain (see cmake/ps3.toolchain.cmake).

target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/InputTranslatorPS3.cpp
    # Future:
    # ${CMAKE_CURRENT_SOURCE_DIR}/SoundManagerPS3.cpp
    # ${CMAKE_CURRENT_SOURCE_DIR}/FileSystemPS3.cpp
)

# mainPS3.cpp is the PS3 entry point (fruit-ninja-game excludes main*.cpp
# files, same as mainSDL.cpp / mainWii.cpp) -- added directly to the
# fruit-ninja executable target by the top-level CMakeLists.txt's
# FRUIT_PLATFORM_PS3 branch, not here.
