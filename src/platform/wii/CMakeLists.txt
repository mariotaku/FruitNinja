# Wii platform backend -- only included when FRUIT_PLATFORM_WII is ON
# (set by src/platform/CMakeLists.txt). This entire file is a no-op include
# under the default host/web/asm-verify configurations.
#
# NOTE: this target_sources call cannot succeed without a devkitPPC/libogc2
# toolchain (cmake/wii.toolchain.cmake) -- it is scaffolding for a machine
# that has one, not something buildable in this repo today.
#
# Everything here is compiled as C++: the devkitPPC CMake toolchain wrapper
# (cmake/wii.toolchain.cmake) delegates to devkitPro's Wii.cmake, which only
# wires up CMake's CXX language -- neither C nor ASM has a working compile
# rule, so enable_language(C) / enable_language(ASM) both abort the generate
# step with a missing CMAKE_<LANG>_COMPILE_OBJECT rule. puff.cpp (the
# vendored zlib-contrib inflater, unmodified aside from the port-specific
# header note) is plain C that also compiles cleanly as C++, so it's named
# and compiled as C++ rather than requiring toolchain surgery.
target_sources(fruit-ninja-game PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/InputTranslatorWii.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SoundManagerWii.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FileSystemWii.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/SplashBootScreen.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/puff.cpp
    # Task #61 phase 1 -- MEM2 free-list allocator (texture pixel buffers).
    ${CMAKE_CURRENT_SOURCE_DIR}/Mem2Alloc.cpp
)
# Block-preload loader (task #36) -- relocated to src/resource/ (platform-
# neutral, gated by FN_BLOCK_PRELOAD, see src/resource/CMakeLists.txt) since
# it's ~95% platform-agnostic. Wii still gets it by default (FN_BLOCK_PRELOAD
# defaults ON when FRUIT_PLATFORM_WII, see root CMakeLists.txt); nothing to
# add here anymore.
# mainWii.cpp is the Wii entry point (fruit-ninja-game excludes main*.cpp
# files, same as mainSDL.cpp/mainEmscripten.cpp) -- added directly to the
# fruit-ninja executable target by the top-level CMakeLists.txt's
# FRUIT_PLATFORM_WII branch, not here.

# --- Boot-splash embedded asset (SplashBootScreen.cpp's hb_logo_gxtx_z) ---
#
# Compressed-transient model: rather than .incbin'ing the full staged 2 MB
# GXT1 texture, tools/wii/make-splash-blob.py pre-crops/pre-downscales the
# ORIGINAL hb_logo.tex source (FruitNinjaBada/Data/textures/hb_logo.tex) down
# to the display-native 480x320 visible-crop window, RGB5A3-tiles it, and
# raw-DEFLATE compresses the result -- see that script's docstring for the
# full pipeline. Only the compressed output (tens of KB) is emitted as a
# generated C++ byte array; SplashBootScreen.cpp inflates it into a transient
# buffer at boot via the vendored puff.cpp inflater, uploads+draws, then frees
# the buffer.
#
# Emitted as a plain C++ source (not a .s + .incbin TU): the devkitPPC CMake
# toolchain wrapper (cmake/wii.toolchain.cmake) delegates to devkitPro's
# Wii.cmake, which only sets up CMake's CXX language -- neither C nor ASM has
# a working compile rule, so enable_language(ASM) / enable_language(C) both
# abort the generate step with a missing CMAKE_<LANG>_COMPILE_OBJECT rule. A
# byte-array source needs no ASM/C toolchain support; its two globals are
# declared extern "C" (see make-splash-blob.py's emit_c_array) so they get
# external linkage under C++ (a namespace-scope const otherwise defaults to
# internal linkage in C++, unlike C) and resolve against
# SplashBootScreen.cpp's extern "C" declarations.
#
# The source is FruitNinjaBada/Data/textures/hb_logo.tex directly (checked
# into the repo), NOT a fn_asset_staging OUTPUT -- so this custom command has
# no ordering dependency on staging and can run any time the source or the
# script itself changes.
set(FN_HB_LOGO_SOURCE_PATH "${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data/textures/hb_logo.tex")
set(FN_HB_LOGO_SPLASH_BLOB_PATH "${CMAKE_CURRENT_BINARY_DIR}/hb_logo_splash_blob.cpp")
add_custom_command(
    OUTPUT ${FN_HB_LOGO_SPLASH_BLOB_PATH}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/wii/make-splash-blob.py
            ${FN_HB_LOGO_SOURCE_PATH} ${FN_HB_LOGO_SPLASH_BLOB_PATH}
    DEPENDS
        ${FN_HB_LOGO_SOURCE_PATH}
        ${CMAKE_SOURCE_DIR}/tools/wii/make-splash-blob.py
        ${CMAKE_SOURCE_DIR}/tools/lib/tex_decoder.py
        ${CMAKE_SOURCE_DIR}/tools/lib/gx_encoder.py
    COMMENT "Wii boot-splash: crop+downscale+tile+deflate hb_logo.tex -> C array"
    VERBATIM
)
set_source_files_properties(${FN_HB_LOGO_SPLASH_BLOB_PATH} PROPERTIES GENERATED TRUE)
target_sources(fruit-ninja-game PRIVATE ${FN_HB_LOGO_SPLASH_BLOB_PATH})
# fruit-ninja-game is defined in another directory, so the add_custom_command
# above is pruned unless a target IN THIS directory consumes its OUTPUT (the
# Unix Makefiles generator only emits the generation rule where a local target
# needs it). A phony custom target here forces the rule to be emitted, and the
# add_dependencies makes fruit-ninja-game wait for the blob before compiling it
# -- without this the build fails "No rule to make target hb_logo_splash_blob.cpp".
add_custom_target(fn_wii_splash_blob DEPENDS ${FN_HB_LOGO_SPLASH_BLOB_PATH})
add_dependencies(fruit-ninja-game fn_wii_splash_blob)
