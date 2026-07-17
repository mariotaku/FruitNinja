# Wii platform backend -- only included when FRUIT_PLATFORM_WII is ON
# (set by src/platform/CMakeLists.txt). This entire file is a no-op include
# under the default host/web/asm-verify configurations.
#
# NOTE: this target_sources call cannot succeed without a devkitPPC/libogc2
# toolchain (cmake/wii.toolchain.cmake) -- it is scaffolding for a machine
# that has one, not something buildable in this repo today.
target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/InputTranslatorWii.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundManagerWii.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileSystemWii.cpp
)
# mainWii.cpp is the Wii entry point (fruit-ninja-game excludes main*.cpp
# files, same as mainSDL.cpp/mainEmscripten.cpp) -- added directly to the
# fruit-ninja executable target by the top-level CMakeLists.txt's
# FRUIT_PLATFORM_WII branch, not here.
