target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/InputManager.cpp
)
if(NOT FN_ASM_VERIFY AND NOT FRUIT_PLATFORM_WII AND NOT FRUIT_PLATFORM_PS3)
    target_sources(fruit-ninja-game PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/InputTranslatorSDL.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/AppDirSDL.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/SaveDirSDL.cpp
    )
endif()

# RenderInterp.cpp is platform-side (excluded from asm-verify
# automatically because the entire fruit-ninja-game target is excluded when
# FN_ASM_VERIFY is ON).  We additionally gate on FN_RENDER_INTERP so the
# source list reflects the compile-time choice (avoids compiling a file that
# evaluates to an empty TU when FN_RENDER_INTERP is OFF).
if(NOT FN_ASM_VERIFY AND FN_RENDER_INTERP AND NOT FRUIT_PLATFORM_PS3)
    target_sources(fruit-ninja-game PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/RenderInterp.cpp
    )
endif()

# Wii backend scaffolding -- OFF by default, gated by FRUIT_PLATFORM_WII
# (set in the top-level CMakeLists.txt, off unless a devkitPPC toolchain is
# explicitly requested via cmake/wii.toolchain.cmake). When OFF (the default
# for host/web/asm-verify), this add_subdirectory is a complete no-op -- no
# Wii source is added to any target, so the default configure/build is
# byte-for-byte unaffected. See src/platform/wii/README.md for the roadmap.
if(FRUIT_PLATFORM_WII)
    add_subdirectory(wii)
endif()

# PS3 backend scaffolding -- OFF by default, gated by FRUIT_PLATFORM_PS3
# (set via cmake/ps3.toolchain.cmake or -DFRUIT_PLATFORM_PS3=ON).
# When OFF this is a complete no-op. See src/platform/ps3/README.md.
if(FRUIT_PLATFORM_PS3)
    add_subdirectory(ps3)
endif()
