cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0079 NEW)
# CMP0135 (CMake 3.24+): extracted file timestamps are the extract-time,
# not the archive's archived mtimes. Explicitly opt in so re-downloads
# (e.g. when the URL_HASH changes) trigger a rebuild reliably.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
option(FN_ASM_VERIFY "Cross-compile for asm-verify (ARM Thumb-2, object files only)" OFF)

# --- Wii port scaffolding (OFF by default; not buildable without devkitPPC) ---
# When ON, adds src/platform/wii sources and a fruit-ninja.dol-style
# executable target. Requires -DCMAKE_TOOLCHAIN_FILE=cmake/wii.toolchain.cmake
# (a devkitPPC/libogc2 cross-compiler) -- see src/platform/wii/README.md.
# Default OFF: the option exists purely so a machine WITH devkitPPC installed
# can opt in; it has zero effect on the host/web/asm-verify configures below.
option(FRUIT_PLATFORM_WII "Build the Wii (GX/libogc2) platform target -- requires devkitPPC" OFF)

# --- Dynamic-TTF rasterizer backend (compile-time swappable) ---
# FontCacheObjectTTF (src/engine/render/FontCacheObjectTTF.cpp) rasterizes
# CJK/Arabic TTF glyphs through the Mortar::TtfFace seam
# (src/engine/render/TtfBackend.h); exactly one of TtfBackendFreetype.cpp /
# TtfBackendStb.cpp is compiled, selected here. "freetype" is the default on
# every platform that has it available (host/web); Wii has no FreeType port
# (see the FreeType gating further down) so it is forced to "stb" regardless
# of what the cache entry says.
set(FN_TTF_BACKEND "freetype" CACHE STRING "Dynamic TTF rasterizer backend: freetype or stb")
set_property(CACHE FN_TTF_BACKEND PROPERTY STRINGS freetype stb)
if (FRUIT_PLATFORM_WII)
    set(FN_TTF_BACKEND "stb")
endif()
message(STATUS "FN_TTF_BACKEND = ${FN_TTF_BACKEND}")

# Port specific: compile-time render interpolation toggle.
# Default ON for host/web builds; always forced OFF under FN_ASM_VERIFY
# (cross-build must never see RenderInterp.* or unordered_map churn).
option(FN_RENDER_INTERP "Enable render interpolation for smooth high-refresh motion" ON)
if(FN_ASM_VERIFY)
    set(FN_RENDER_INTERP OFF)
endif()

# Web-only debug isolation switch: drive the emscripten main loop at a fixed
# 60Hz setTimeout cadence instead of requestAnimationFrame (display rate), so
# pollInput()+render run at 60Hz regardless of a 120/144Hz panel (1:1
# drain:dispatch). Used to confirm whether the high-refresh decoupling is what
# breaks touch. Default OFF (RAF). No effect on native builds.
option(FN_WEB_FORCE_60FPS "Force emscripten main loop to a fixed 60fps (web debug)" OFF)

# Touch-path debug logging: when ON, scopes FN_DEBUG_TOUCH to InputTranslatorSDL.cpp
# so its drain/dispatch TLOG trace compiles, and that file raises the SDL log
# priority to DEBUG at startup so the trace reaches stdout / the browser console.
# Default OFF. Used to capture device touch logs (filter console by "[TOUCH]").
option(FN_DEBUG_TOUCH "Enable touch drain/dispatch debug logging (InputTranslatorSDL)" OFF)

project(FruitNinja LANGUAGES CXX)

# Propagate FRUIT_PLATFORM_WII as a compile definition so the #ifdef
# FRUIT_PLATFORM_WII guards in src/platform/wii/*.{h,cpp} actually gate
# compilation when this path is deliberately opted into. OFF by default, so
# this add_compile_definitions call is a no-op for every other configure.
if (FRUIT_PLATFORM_WII)
    add_compile_definitions(FRUIT_PLATFORM_WII=1)
endif()

# Used to invoke tools/assets/stage-assets.py (asset staging: textures ->
# WebP for both host + web) and tools/web/web-hash-assets.py (web cache
# busting) portably -- avoids relying on a `python3` shim being on PATH
# (unreliable on Windows).
find_package(Python3 COMPONENTS Interpreter REQUIRED)

# Required: node runs tools/assets/svg-to-webp.mjs (widget SVG -> WebP .tex
# rasterization via the sharp npm package) as part of fn_asset_staging below.
# Resolved at CONFIGURE time -- a fresh `cmake` run is needed after installing
# node for the first time. REQUIRED: SettingsScreen and the widget render tests
# load the generated widget textures directly (no placeholder-art runtime
# fallback), so node must be present to produce them.
find_program(NODE_EXECUTABLE NAMES node)
if(NOT FN_ASM_VERIFY AND NOT NODE_EXECUTABLE)
    message(FATAL_ERROR "node not found -- required to rasterize assets/ui-widgets/*.svg "
                         "(tools/assets/svg-to-webp.mjs). Install Node.js and re-run cmake.")
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- ASM verify cross-build mode ---
# When ON, this CMakeLists produces an OBJECT library (fnverify) suitable
# for the asm-verify pipeline. No linking, no SDL2/GL/FreeType deps, no
# tests, no executable. Set by tools/asm-verify/cross-build/CMakeLists.txt
# when invoked with the Sourcery 2010q1 toolchain.
if(FN_ASM_VERIFY)
    # Absolute path to asm-verify tools dir for cross-headers.
    get_filename_component(FN_ASM_VERIFY_TOOLS
        "${CMAKE_CURRENT_LIST_DIR}/tools/asm-verify" ABSOLUTE)
    set(FN_LIB_TYPE OBJECT)
else()
    set(FN_LIB_TYPE STATIC)
endif()

# --- MSVC-specific compile options ---
# /utf-8: source + execution charset = UTF-8 (silences C4819 on files that
#         contain non-ASCII chars like Unicode arrows in comments).
# _CRT_SECURE_NO_WARNINGS: silence C4996 deprecation noise for
#         strncpy/fopen etc; the binary uses these idioms intentionally.
if (MSVC)
    add_compile_options(/utf-8)
    # _CRT_SECURE_NO_WARNINGS:  strncpy / fopen "may be unsafe" (C4996)
    # _CRT_NONSTDC_NO_WARNINGS: strdup / strcasecmp "POSIX name deprecated" (C4996)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()

# --- Sanitizers (opt-in) ---
#
# AddressSanitizer + UBSan. Caught a real bug already (HUD::RemoveControl
# leak in commit 8ea6556). Only works with clang on Windows — the
# MSYS2 ucrt64 GCC ships sanitizer codegen but not the libasan runtime.
# MSYS2 clang64 ships libclang_rt.asan_dynamic-x86_64.dll which works.
#
# Build:
#   cmake -G "MSYS Makefiles" -B build/asan -DENABLE_ASAN=ON \
#         -DCMAKE_C_COMPILER=/clang64/bin/clang.exe \
#         -DCMAKE_CXX_COMPILER=/clang64/bin/clang++.exe
#   cmake --build build/asan -j$(nproc)
#
# Run (the asan DLL must be on PATH — clang64/bin already has it
# if you launch from the MSYS2 CLANG64 shell, otherwise prefix:):
#   PATH="/clang64/bin:$PATH" ./build/asan/fruit-ninja.exe
#
# Useful runtime flags:
#   ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1
#   LSAN_OPTIONS=verbosity=1
option(ENABLE_ASAN "Enable AddressSanitizer + UBSan (clang only)" OFF)
if (ENABLE_ASAN)
    if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(WARNING
            "ENABLE_ASAN=ON but compiler is ${CMAKE_CXX_COMPILER_ID}. "
            "MSYS2 ucrt64 GCC does not ship libasan; configure with "
            "-DCMAKE_CXX_COMPILER=/clang64/bin/clang++.exe instead.")
    endif()
    message(STATUS "AddressSanitizer + UBSan enabled")
    add_compile_options(
        -fsanitize=address
        -fsanitize=undefined
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all
        -O1
        -g
    )
    add_link_options(
        -fsanitize=address
        -fsanitize=undefined
    )
endif()

# --- Emscripten AddressSanitizer (web UAF debugging) ---
# Instruments the whole wasm build with ASan so heap-use-after-free / OOB in the
# web build produce a real alloc/free stack trace in the browser console (which
# host x64 ASan can't reproduce -- wasm heap layout differs). ASan needs the
# frame pointers + names and a large reserved heap for its shadow memory.
# Enable with: cmake -B build/web ... -DFN_WEB_ASAN=ON  (then rebuild via Docker).
option(FN_WEB_ASAN "Emscripten AddressSanitizer for the web build" OFF)
if (FN_WEB_ASAN)
    if (NOT EMSCRIPTEN)
        message(WARNING "FN_WEB_ASAN=ON but this is not an Emscripten build; ignoring.")
    else()
        message(STATUS "Emscripten AddressSanitizer enabled (FN_WEB_ASAN)")
        add_compile_options(
            -fsanitize=address
            -fno-omit-frame-pointer
            -O1
            -g
        )
        add_link_options(
            -fsanitize=address
            -fno-omit-frame-pointer
            # ASan reserves shadow memory over the whole heap; give it a large
            # fixed reservation so allocation doesn't fail under the 49MB asset
            # preload + runtime heap. Named frames + assertions for readable traces.
            "SHELL:-sINITIAL_MEMORY=1073741824"
            "SHELL:-sASSERTIONS=1"
            "SHELL:--profiling-funcs"
        )
    endif()
endif()

# --- Emscripten DWARF debug build (Chrome source-level debugging) ---
# Build the wasm with full DWARF debug info so the Chrome "C/C++ DevTools
# Support (DWARF)" extension can set C++ source breakpoints and inspect locals
# on the actual failing target (some bugs reproduce only on wasm, not native).
# DWARF is emitted to a SEPARATE sidecar file (fruit-ninja.debug.wasm): the main
# .wasm carries only a small `external_debug_info` custom section naming the sidecar,
# which Chrome's DWARF extension fetches on demand. Keeps the shipped .wasm lean
# while giving full source-level debugging. Serve the .debug.wasm beside the .wasm.
# Pair with -DCMAKE_BUILD_TYPE=Debug for -O0 so locals aren't optimised out and
# stepping is 1:1.
# Enable: cmake -B build/web ... -DFN_WEB_DEBUG=ON -DCMAKE_BUILD_TYPE=Debug
option(FN_WEB_DEBUG "Emscripten separate-DWARF debug build for Chrome source-level debugging" OFF)
if (FN_WEB_DEBUG)
    if (NOT EMSCRIPTEN)
        message(WARNING "FN_WEB_DEBUG=ON but this is not an Emscripten build; ignoring.")
    else()
        message(STATUS "Emscripten separate-DWARF debug build enabled (FN_WEB_DEBUG)")
        add_compile_options(-g)
        # Link: extract DWARF into fruit-ninja.debug.wasm (relative to the wasm output
        # dir) and reference it via external_debug_info in the main .wasm.
        add_link_options("SHELL:-gseparate-dwarf=fruit-ninja.debug.wasm")
        # Crash LOUDLY on stack overflow instead of silently clobbering statics.
        # =2 instruments every function to check SP vs the limit, so it aborts AT
        # the overflowing frame (the failure mode that caused the game_work /
        # AchievementManager corruption). Debug-only; production build omits it.
        add_link_options("SHELL:-sSTACK_OVERFLOW_CHECK=2")
    endif()
endif()

# --- Emscripten profiling build (Chrome/Firefox flame-graph profiling) ---
# Keeps C++ function names in the wasm name section (--profiling-funcs) so the
# DevTools Performance flame chart shows real symbols instead of wasm-function[N].
# Opt-in only: OFF for lean shipped/CI builds. See the FN_DEBUG_TOUCH guard below
# where this actually gets applied to the link line.
option(FN_WEB_PROFILING "Emscripten: keep C++ function names in the wasm (--profiling-funcs) for Chrome/Firefox flame-graph profiling; OFF for lean shipped builds" OFF)

# --- GL backend selection ---
#
# The port targets the same fixed-function pipeline the binary uses
# (ES 1.x): glMatrixMode / glVertexPointer / glMaterialfv / ... —
# a near 1:1 translation of the binary's PassBinding::Apply, for the
# 3D Geometry path (and remaining ES1 desktop/webOS 3D state setup).
# The 2D quad/UI draw path (DrawQuad/DrawTriList/DrawTriStrip/
# DrawColorQuad/draw_fullscreen_quad) uses a real GLES2 shader
# (see src/engine/render/Shaders.h) for pixel-identical output on
# WebGL/ES2-only targets; see Renderer::DrawShaded2D.
#
# FRUIT_GL_API selects which OS header to include:
#   GL_COMPAT  - Desktop GL compatibility profile (<GL/gl.h>).
#                Windows/macOS/Linux dev; Mesa llvmpipe for swrast.
#   ES1        - OpenGL ES 1.1 (<GLES/gl.h>) for webOS / embedded.
if (EMSCRIPTEN)
    # Force FRUIT_GL_API to the new EMSCRIPTEN path; skip the user-visible cache entry.
    set(FRUIT_GL_API "EMSCRIPTEN")
else()
    set(FRUIT_GL_API "GL_COMPAT" CACHE STRING "GL backend: GL_COMPAT or ES1")
    set_property(CACHE FRUIT_GL_API PROPERTY STRINGS GL_COMPAT ES1)
endif()
message(STATUS "FRUIT_GL_API = ${FRUIT_GL_API}")

if (FRUIT_GL_API STREQUAL "GL_COMPAT")
    add_compile_definitions(FRUIT_GL_API_GL_COMPAT=1)
elseif (FRUIT_GL_API STREQUAL "ES1")
    add_compile_definitions(FRUIT_GL_API_ES1=1)
elseif (FRUIT_GL_API STREQUAL "EMSCRIPTEN")
    add_compile_definitions(FRUIT_GL_API_EMSCRIPTEN=1)
else()
    message(FATAL_ERROR "Unknown FRUIT_GL_API '${FRUIT_GL_API}'")
endif()

# --- Dependencies ---

include(FetchContent)
set(_SDL2_VER 2.30.11)

if(NOT FN_ASM_VERIFY)

if (EMSCRIPTEN)
    # Port specific: Emscripten ships SDL2 as a port; -sUSE_SDL=2 at compile+link
    # time activates it. Create INTERFACE targets that carry the flag so the rest
    # of the build links as usual against SDL2::SDL2 / SDL2::SDL2main.
    message(STATUS "Emscripten build: using built-in SDL2 port via -sUSE_SDL=2")
    add_library(SDL2_emscripten_iface INTERFACE)
    target_compile_options(SDL2_emscripten_iface INTERFACE "SHELL:-sUSE_SDL=2")
    target_link_options(SDL2_emscripten_iface INTERFACE "SHELL:-sUSE_SDL=2")
    if (NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS SDL2_emscripten_iface)
    endif()
    if (NOT TARGET SDL2::SDL2main)
        add_library(SDL2::SDL2main ALIAS SDL2_emscripten_iface)
    endif()
else()
    # Prefer system SDL2 (MSYS2 / Linux package, etc.); fall back to a fetch
    # only when not installed:
    #   - On MSVC: download libsdl-org's prebuilt VC binary (SDL2-devel-*-VC.zip)
    #     -- ~5 sec download vs ~2 min source build, no extra dependencies.
    #   - Elsewhere (or as a safety net on MSVC if the prebuilt download fails):
    #     fall through to the source-build path.
    find_package(SDL2 QUIET)
    if (NOT SDL2_FOUND AND MSVC)
        message(STATUS "System SDL2 not found -- fetching prebuilt MSVC zip ${_SDL2_VER}")
        FetchContent_Declare(
            SDL2_prebuilt_msvc
            URL      https://github.com/libsdl-org/SDL/releases/download/release-${_SDL2_VER}/SDL2-devel-${_SDL2_VER}-VC.zip
            URL_HASH SHA256=6e524b5f0e3ecd9fe3b439336b5e7c3fe597e8823e8e62c8e3074d335e9ca7d9
        )
        FetchContent_MakeAvailable(SDL2_prebuilt_msvc)

        set(_sdl2_root "${sdl2_prebuilt_msvc_SOURCE_DIR}")
        set(_sdl2_arch "x64")  # FN binary is 64-bit MSVC
        set(_sdl2_lib_dir "${_sdl2_root}/lib/${_sdl2_arch}")

        add_library(SDL2::SDL2 SHARED IMPORTED)
        set_target_properties(SDL2::SDL2 PROPERTIES
            IMPORTED_LOCATION             "${_sdl2_lib_dir}/SDL2.dll"
            IMPORTED_IMPLIB               "${_sdl2_lib_dir}/SDL2.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${_sdl2_root}/include")

        add_library(SDL2::SDL2main STATIC IMPORTED)
        set_target_properties(SDL2::SDL2main PROPERTIES
            IMPORTED_LOCATION "${_sdl2_lib_dir}/SDL2main.lib")

        set(SDL2_FOUND TRUE)
    endif()

    if (NOT SDL2_FOUND)
        message(STATUS "System SDL2 not found -- fetching SDL2 ${_SDL2_VER} source (slow path)")
        set(SDL_TEST OFF CACHE BOOL "" FORCE)
        set(SDL_SHARED ON CACHE BOOL "" FORCE)
        set(SDL_STATIC OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            SDL2
            GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
            GIT_TAG        release-${_SDL2_VER}
        )
        FetchContent_MakeAvailable(SDL2)
        # FetchContent uses the SDL2 source's own targets (`SDL2-static`,
        # `SDL2`, `SDL2main`) which are non-namespaced. Add the
        # `SDL2::SDL2` / `SDL2::SDL2main` aliases the rest of the build
        # expects, so the link step is identical regardless of source.
        if (TARGET SDL2 AND NOT TARGET SDL2::SDL2)
            add_library(SDL2::SDL2 ALIAS SDL2)
        endif()
        if (TARGET SDL2main AND NOT TARGET SDL2::SDL2main)
            add_library(SDL2::SDL2main ALIAS SDL2main)
        endif()
    endif()
endif()

# --- SDL2_image (test-only) ---
# Used only by test executables (IMG_SavePNG / IMG_SaveJPG); NOT linked into
# the game or engine targets.  The F12 in-game screenshot path uses SDL_SaveBMP
# and must NOT depend on SDL2_image.
#
# Provider priority mirrors the SDL2 block above:
#   1. System package (MSYS2: mingw-w64-SDL2_image, Linux: libsdl2-image-dev).
#   2. MSVC: FetchContent prebuilt VC zip from libsdl-org/SDL_image releases.
set(_SDL2_IMAGE_VER 2.8.2)

if(NOT FN_ASM_VERIFY)

find_package(SDL2_image QUIET)
if (NOT SDL2_image_FOUND AND MSVC)
    message(STATUS "System SDL2_image not found -- fetching prebuilt MSVC zip ${_SDL2_IMAGE_VER}")
    FetchContent_Declare(
        SDL2_image_prebuilt_msvc
        URL https://github.com/libsdl-org/SDL_image/releases/download/release-${_SDL2_IMAGE_VER}/SDL2_image-devel-${_SDL2_IMAGE_VER}-VC.zip
    )
    FetchContent_MakeAvailable(SDL2_image_prebuilt_msvc)

    set(_sdl2img_root "${sdl2_image_prebuilt_msvc_SOURCE_DIR}")
    set(_sdl2img_arch "x64")
    set(_sdl2img_lib_dir "${_sdl2img_root}/lib/${_sdl2img_arch}")

    add_library(SDL2_image::SDL2_image SHARED IMPORTED)
    set_target_properties(SDL2_image::SDL2_image PROPERTIES
        IMPORTED_LOCATION             "${_sdl2img_lib_dir}/SDL2_image.dll"
        IMPORTED_IMPLIB               "${_sdl2img_lib_dir}/SDL2_image.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_sdl2img_root}/include")

    set(SDL2_image_FOUND TRUE)
endif()

if (NOT SDL2_image_FOUND AND NOT MSVC)
    # MSYS2/Linux should have it as a system package; warn rather than pulling
    # source (SDL_image has many optional codec dependencies that complicate
    # a source build significantly).
    message(WARNING "SDL2_image not found; install mingw-w64-x86_64-SDL2_image (MSYS2) or libsdl2-image-dev (Debian). Test screenshots will not link.")
endif()

endif() # NOT FN_ASM_VERIFY (SDL2_image block)

# Prefer a system/vcpkg tinyxml2 (provides tinyxml2::tinyxml2); fall back to a
# FetchContent source build when not installed. Both providers export the same
# tinyxml2::tinyxml2 target, so the link line is identical either way.
find_package(tinyxml2 CONFIG QUIET)
if (NOT tinyxml2_FOUND)
    message(STATUS "System/vcpkg tinyxml2 not found -- fetching tinyxml2 10.0.0 source")
    FetchContent_Declare(
        tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG        10.0.0
    )
    FetchContent_MakeAvailable(tinyxml2)
endif()

# --- FreeType ---
# Used for dynamic glyph rendering of the game's CJK/Arabic TTF fonts
# (Data/fontstruetype/arabic.ttf, gangofchinese.ttf) when FN_TTF_BACKEND is
# "freetype" (the default -- see the FN_TTF_BACKEND option above). The
# existing .fnt bitmap path is unchanged either way; dynamic TTF is additive.
# When FN_TTF_BACKEND is "stb", this whole acquisition block is skipped --
# src/engine/CMakeLists.txt compiles TtfBackendStb.cpp instead of
# TtfBackendFreetype.cpp and never links Freetype::Freetype for that backend.
# NOTE: this whole block (and the rest of this if(NOT FN_ASM_VERIFY AND NOT
# FRUIT_PLATFORM_WII) branch) is unreachable for FRUIT_PLATFORM_WII anyway --
# that config takes the else() branch further down, which already stubs a
# no-op Freetype::Freetype INTERFACE target for both FN_ASM_VERIFY and Wii.
# This gate only matters for a non-Wii host/web build that opts into
# -DFN_TTF_BACKEND=stb explicitly.
if (FN_TTF_BACKEND STREQUAL "stb")
    # No-op: no FreeType acquisition, no Freetype::Freetype target. Nothing
    # in src/engine links it for this backend (see src/engine/CMakeLists.txt).
elseif (EMSCRIPTEN)
    # Port specific: Emscripten ships FreeType as a port; -sUSE_FREETYPE=1
    # activates it at compile+link time. Create an INTERFACE target so the
    # rest of the build links as usual against Freetype::Freetype.
    message(STATUS "Emscripten build: using built-in FreeType port via -sUSE_FREETYPE=1")
    add_library(freetype_emscripten_iface INTERFACE)
    target_compile_options(freetype_emscripten_iface INTERFACE "SHELL:-sUSE_FREETYPE=1")
    target_link_options(freetype_emscripten_iface INTERFACE "SHELL:-sUSE_FREETYPE=1")
    # Emscripten's FreeType port installs headers at <freetype2/...>.
    # The INTERFACE target carries the include path; emcc makes it available
    # via the port system, so no manual include dir is needed.
    if (NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype_emscripten_iface)
    endif()
else()
    # Native: prefer system FreeType; fall back to FetchContent source build.
    find_package(Freetype QUIET)
    if (NOT Freetype_FOUND)
        message(STATUS "System FreeType not found -- fetching FreeType 2.13.3 source")
        set(FT_DISABLE_ZLIB        ON  CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2       ON  CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG         ON  CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ    ON  CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI      ON  CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS      OFF CACHE BOOL "" FORCE)
        FetchContent_Declare(
            freetype
            GIT_REPOSITORY https://github.com/freetype/freetype.git
            GIT_TAG        VER-2-13-3
        )
        FetchContent_MakeAvailable(freetype)
        # FetchContent builds a target named `freetype`; alias to Freetype::Freetype.
        if (TARGET freetype AND NOT TARGET Freetype::Freetype)
            add_library(Freetype::Freetype ALIAS freetype)
        endif()
    endif()
endif()

# --- libwebp (DECODER only) ---
# Port specific: web compressed textures (libwebp). Web textures are transcoded
# to WebP and shipped inside .tex-named files; the WebP reader in
# TextureFileFormat.cpp decodes them (RGBA8888). Desktop keeps raw .tex (the
# reader is present but inert). Built for BOTH host and Emscripten (portable C,
# builds under emcmake). Only the `webpdecoder` target is used
# (WebPDecodeRGBA / WebPGetInfo / WebPFree); the encoder + all tools/anim/mux
# are OFF. Kept out of the asm-verify cross-build (that branch defines __bada__
# and the WebP reader is #if'd out there).
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP      OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP      OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP   OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP   OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP      OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO   OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX    OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS     OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBP_JS    OFF CACHE BOOL "" FORCE)
set(WEBP_LINK_STATIC      ON  CACHE BOOL "" FORCE)
FetchContent_Declare(
    libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG        v1.4.0
)
FetchContent_MakeAvailable(libwebp)

else()
# FN_ASM_VERIFY=ON: stub INTERFACE targets. Cross-headers provide replacement
# headers; no SDL2/GL/FreeType libraries are linked. tinyxml2 headers are
# copied by verify.sh from the host build into build/host/_deps/tinyxml2-src.
foreach(_tgt SDL2::SDL2 SDL2::SDL2main)
    if(NOT TARGET ${_tgt})
        add_library(${_tgt} INTERFACE IMPORTED)
    endif()
endforeach()
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype INTERFACE IMPORTED)
endif()
endif()

# --- Game library (everything except mainSDL.cpp) ---
# Collected as a STATIC lib so both the fruit-ninja executable and the
# tests/test_screen smoke test can link against the same compiled units.
# Subsystem CMakeLists.txt files contribute their sources via
# target_sources(fruit-ninja-game PRIVATE ...).

# Shared staging dir: fn_asset_staging (below) stages BOTH host and web
# textures to WebP here (see tools/assets/stage-assets.py). Host reads it
# directly via FN_DATA_DIR_PATH; web additionally --preload-file's it into
# MEMFS at /FruitNinjaBada/Data (below).
set(FN_STAGING_DATA_DIR "${CMAKE_BINARY_DIR}/staging/Data")

if (EMSCRIPTEN)
    # Port specific: assets are preloaded into MEMFS at /FruitNinjaBada/Data.
    # This is the MEMFS mount path (absolute, POSIX-style) -- NOT a host
    # filesystem path, so it stays independent of FN_STAGING_DATA_DIR.
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    set(FN_DATA_DIR_PATH "/FruitNinjaBada/Data")
else()
    # Host now reads from the staged dir (WebP-transcoded textures), not
    # FruitNinjaBada/Data directly -- see fn_asset_staging below.
    file(TO_CMAKE_PATH "${FN_STAGING_DATA_DIR}" FN_DATA_DIR_PATH)
endif()

if(NOT FN_ASM_VERIFY)
    add_library(fruit-ninja-game STATIC src/Game.cpp src/GameSDL.cpp)
    target_include_directories(fruit-ninja-game PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

if(NOT FN_ASM_VERIFY)
    configure_file(src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)
endif()

# --- Subsystems ---
if(NOT FN_ASM_VERIFY)
    add_subdirectory(src/engine)
    add_subdirectory(src/hud)
    add_subdirectory(src/ui)
    add_subdirectory(src/screens)
    add_subdirectory(src/platform)
    add_subdirectory(src/entities)
    add_subdirectory(src/game)
    add_subdirectory(src/debug)

    target_link_libraries(fruit-ninja-game PUBLIC
        SDL2::SDL2 tinyxml2::tinyxml2 mortar_engine
    )
endif()

# --- Executable ---
if(NOT FN_ASM_VERIFY)
    # Port specific: unified build-time asset staging, feeds BOTH host and web
    # builds. Runs tools/assets/stage-assets.py, which mirrors
    # FruitNinjaBada/Data into FN_STAGING_DATA_DIR, transcoding every *.tex
    # (Tex1) to WebP via Pillow (host + web alike -- see the libwebp reader in
    # TextureFileFormat.cpp, which dispatches by content magic not extension),
    # and merging in the pre-generated widget art from
    # assets/ui-widgets/generated/*.tex. That widget art is itself generated
    # AT BUILD TIME (not checked in) by tools/assets/svg-to-webp.mjs -- a
    # Node/sharp step run FIRST, before stage-assets.py (NODE_EXECUTABLE is
    # REQUIRED, see the find_program check above); it rasterizes
    # assets/ui-widgets/*.svg on every platform including Windows/CLion, no
    # offline pre-generation step needed. Widget textures have no runtime
    # placeholder fallback (SettingsScreen / the widget render tests load them
    # directly), so svg-to-webp.mjs exits non-zero on any failure and fails
    # this build step loudly.
    # Under --web (EMSCRIPTEN) stage-assets.py additionally transcodes
    # sfx/*.wav.pcm to Ogg/Vorbis (+ sfx-loops.json) and subsets
    # fontstruetype/gangofchinese.ttf, for the Web Audio backend
    # (SoundManagerWebAudio.cpp) and the wasm preload size budget; the host
    # build ships raw .wav.pcm (SDL mixer) and the full TTF, unchanged.
    #
    # Host: FN_DATA_DIR_PATH (set above) points straight at FN_STAGING_DATA_DIR,
    # so GameSDL.cpp's existing SetDataDir call transparently reads staged
    # (WebP) textures -- no runtime code changes. Web: FN_STAGING_DATA_DIR is
    # additionally --preload-file'd into MEMFS at /FruitNinjaBada/Data (below,
    # unchanged mechanism from before, new source dir). add_dependencies below
    # guarantees this runs before fruit-ninja links on both branches (build
    # time, not configure time -- FruitNinjaBada/Data need not exist yet when
    # CMake configures, only when this target actually runs).
    set(FN_ASSET_STAGING_COMMANDS
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/assets/svg-to-webp.mjs ${CMAKE_SOURCE_DIR}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/assets/stage-assets.py
                ${CMAKE_SOURCE_DIR} ${FN_STAGING_DATA_DIR}
                $<$<BOOL:${EMSCRIPTEN}>:--web>
    )
    # ALL: the host runtime + every test read assets from FN_STAGING_DATA_DIR
    # (FN_DATA_DIR_PATH points there), so staging must run on ANY build -- not just
    # when the fruit-ninja exe target is built. Idempotent (mtime), so it's a no-op
    # once staged.
    add_custom_target(fn_asset_staging ALL
        ${FN_ASSET_STAGING_COMMANDS}
        COMMENT "Staging assets (svg->webp via node/sharp; textures->WebP; web:+ogg+font)"
        VERBATIM
    )
if (EMSCRIPTEN)
    # Port specific: Emscripten entry point uses emscripten_set_main_loop_arg
    # instead of the blocking Game::run() loop.
    add_executable(fruit-ninja src/mainEmscripten.cpp)
    add_dependencies(fruit-ninja fn_asset_staging)
    target_link_libraries(fruit-ninja PRIVATE fruit-ninja-game SDL2::SDL2main)
    target_link_options(fruit-ninja PRIVATE
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sUSE_FREETYPE=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        # Port specific: emscripten 3.1.27+ dropped the default stack to 64 KB
        # (older emscripten defaulted to 5 MB). 64 KB is far below the original
        # Bada platform's stack: the deepest path in the game -- HUD text render
        # (ScoreControl::PreDraw -> Font::DrawString -> Renderer::DrawTriStrip ->
        # MatrixManager::GetMVP -> Matrix44::operator*, several 64-byte Matrix44
        # temps) -- overflows 64 KB and the stack descends into the static-data
        # segment, silently clobbering globals (game_work / AchievementManager /
        # MatrixManager). The HUD-text path peaks at ~112 KB of stack; size the
        # wasm stack to 1 MB (~9x headroom, embedded/native-comparable). Any deeper
        # path that ever exceeds this aborts loudly via STACK_OVERFLOW_CHECK (debug)
        # rather than silently corrupting -- so 1 MB is safe, not a gamble.
        "SHELL:-sSTACK_SIZE=1048576"
        "SHELL:-sEXIT_RUNTIME=0"
        # Port specific: custom HTML shell (Phase 5+7: letterbox + loading bar
        # + TAP TO START overlay that doubles as the audio gesture unlock).
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/src/platform/emscripten/shell.html"
        # Port specific: preload read-only assets into MEMFS; exclude the two
        # writable save files so they are NOT packed into the read-only bundle.
        # Saves are routed to /save (IDBFS) at runtime; see mainEmscripten.cpp.
        # Source is the STAGED data dir (sfx transcoded to Ogg/Vorbis + a
        # sfx-loops.json loop map, textures re-encoded to WebP, at build time,
        # everything else identical to FruitNinjaBada/Data) -- see
        # fn_asset_staging above. Virtual mount path is unchanged, so runtime
        # code (TextureManager::GetDataDir() etc.) needs no changes; the Web
        # Audio backend reads sfx/<name>.ogg.
        "SHELL:--preload-file ${FN_STAGING_DATA_DIR}@/FruitNinjaBada/Data"
        "SHELL:--exclude-file *FruitySave.xml"
        "SHELL:--exclude-file *ItemSave.xml"
        # Port specific: link idbfs.js so FS.mount(IDBFS) is available.
        "SHELL:-lidbfs.js"
        # Port specific: export fn_idbfs_ready, fn_user_started,
        # fn_web_synth_touch, fn_set_audio_enabled, and fn_audio_consent_skip
        # so JS can call them as Module._fn_idbfs_ready() /
        # Module._fn_user_started() / Module._fn_web_synth_touch() /
        # Module._fn_set_audio_enabled() / Module._fn_audio_consent_skip().
        # The third is called by shell.html's document-level outside-canvas
        # touch capture (see mainEmscripten.cpp fn_web_synth_touch for why);
        # the fourth is called by the audio-consent overlay's button tap
        # handler (shell.html _onAudioConsentTap) to unlock the AudioContext
        # (the gesture) and set game_work.m_bSoundOn/m_bMusicOn for THAT
        # SESSION ONLY (never persisted); the fifth is called by shell.html's
        # _fnMaybeShowAudioConsent when window.FNAudio.bornSuspended is false
        # (the AudioContext was already 'running' at creation -- no gesture
        # ever needed) to apply the plain first-run default and skip the
        # overlay entirely (see mainEmscripten.cpp fn_set_audio_enabled /
        # fn_audio_consent_skip). Nothing about the audio choice is stored
        # anywhere (no SettingsSave.xml fields, no localStorage) -- every load
        # re-decides from scratch off the AudioContext's born state.
        # EMSCRIPTEN_KEEPALIVE alone is sufficient for most linker configs but
        # an explicit -sEXPORTED_FUNCTIONS ensures they survive aggressive DCE.
        # BUG HISTORY: fn_set_audio_enabled was added to mainEmscripten.cpp
        # without adding it here -- the JS call site's `Module._fn_set_audio_enabled`
        # guard silently no-op'd (function never existed on Module), so the
        # audio unlock/session-state setter never ran on ANY tap.
        "SHELL:-sEXPORTED_FUNCTIONS=['_main','_fn_idbfs_ready','_fn_user_started','_fn_web_synth_touch','_fn_set_audio_enabled','_fn_audio_consent_skip']"
    )
    # Debug build (FN_DEBUG_TOUCH): keep C++ function names in the wasm name
    # section so browser crash stack traces name the actual C++ frames.
    # FN_WEB_PROFILING: same flag, opted into independently for Chrome/Firefox
    # DevTools flame-graph profiling (see the option() above).
    # --profiling-funcs is pure metadata (no codegen/timing change), so it does
    # not perturb timing-sensitive intermittent crashes and profiles stay
    # representative of the real (non-instrumented) build. (Deliberately NOT
    # adding -sASSERTIONS, which would change memory init/timing and could mask
    # the repro / skew a profile.)
    if (FN_DEBUG_TOUCH OR FN_WEB_PROFILING)
        target_link_options(fruit-ninja PRIVATE "SHELL:--profiling-funcs")
    endif()
    # Port specific: stage the PWA files (manifest.webmanifest, sw.js,
    # favicon.ico at the output root; icon PNGs under icons/) next to
    # fruit-ninja.html so the shell's <head> can reference them by relative URL
    # before MEMFS is populated.  These keep stable, unhashed names by design
    # (web-hash-assets.py does not touch them) so the manifest / service-worker
    # / favicon URLs never change.  (The old play_button/sound/sound_cross/
    # splash .webp overlays went away with the progress-bar-only loading shell.)
    add_custom_command(TARGET fruit-ninja POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/manifest.webmanifest
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/sw.js
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/icons/favicon.ico
            ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/icons
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/icons/icon-16.png
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/icons/icon-32.png
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/icons/icon-180.png
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/icons/icon-192.png
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/icons/icon-512.png
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/icons/icon-maskable-512.png
            ${CMAKE_CURRENT_BINARY_DIR}/icons
        COMMENT "Copying shell image assets + PWA files (manifest, sw.js, favicon, icons) to web build output"
        VERBATIM
    )
    # Port specific: content-hash web output files to defeat browser/mobile caching.
    # Renames fruit-ninja.{wasm,data,js} to name-<sha8>.ext, rewrites all
    # internal cross-references (wasm/data refs in js, js ref in html), and
    # prunes stale hashed files from prior builds.
    # fruit-ninja.html stays unhashed (stable entry point).
    # Skipped under FN_WEB_DEBUG: the -O0/-g emscripten shell emits a different
    # script-tag/locateFile literal form the hasher can't rewrite, which breaks
    # the html->js reference. Debug builds keep the un-hashed default names
    # (fruit-ninja.{html,js,wasm,data}), which are internally consistent.
    if (NOT FN_WEB_DEBUG)
        add_custom_command(TARGET fruit-ninja POST_BUILD
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/web/web-hash-assets.py
                            ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Content-hashing web output files (cache busting)"
            VERBATIM
        )
    endif()
elseif (FRUIT_PLATFORM_WII)
    # Wii entry point -- SCAFFOLDING ONLY. Only reachable when the caller
    # explicitly set -DFRUIT_PLATFORM_WII=ON (default OFF, see the option()
    # near the top of this file) AND configured with
    # -DCMAKE_TOOLCHAIN_FILE=cmake/wii.toolchain.cmake (a devkitPPC
    # cross-compiler). Neither is true for the default host/web configure,
    # so this branch has zero effect unless deliberately opted into.
    # See src/platform/wii/README.md for the full roadmap; this will NOT
    # link without devkitPPC/libogc2 present.
    add_executable(fruit-ninja src/platform/wii/mainWii.cpp)
    target_link_libraries(fruit-ninja PRIVATE fruit-ninja-game)
    # TODO(wii): -logc2 -lm (or the split -lwiiuse -lbte -logc -lfat set,
    # depending on which libogc2 packaging is installed) once a real
    # devkitPPC environment is available to verify link flags against.
    # TODO(wii): elf2dol POST_BUILD step to produce fruit-ninja.dol.
else()
    # Native desktop/webOS entry point.
    # Port specific: on Windows, compile the .rc resource script into the exe
    # so it carries an application icon (Explorer / taskbar). Both MSVC (rc.exe)
    # and MinGW (windres) handle a .rc listed as a source automatically.
    set(_fn_native_sources src/mainSDL.cpp)
    if (WIN32)
        enable_language(RC)
        list(APPEND _fn_native_sources src/platform/win32/fruit-ninja.rc)
    endif()
    add_executable(fruit-ninja ${_fn_native_sources})
    add_dependencies(fruit-ninja fn_asset_staging)
    target_link_libraries(fruit-ninja PRIVATE fruit-ninja-game SDL2::SDL2main)

    # Copy SDL2.dll next to fruit-ninja.exe on Windows so the launcher finds
    # it without having to add an external dir to PATH. Covers both fetch
    # paths (prebuilt MSVC zip = IMPORTED SHARED; source build = built
    # SHARED_LIBRARY) -- $<TARGET_FILE:SDL2::SDL2> resolves to the .dll
    # either way. On MSYS2 / Linux the system SDL2 is found via PATH /
    # ldconfig and no copy is needed (the alias points at a system target
    # without an importable file location, so the generator expression
    # would be empty -- skip via SDL2_FOUND only-if-fetched gate).
    if (WIN32 AND TARGET SDL2::SDL2)
        # Determine whether SDL2::SDL2 has a real DLL we can copy.
        # IMPORTED SHARED (prebuilt zip) -> TYPE = SHARED_LIBRARY, IMPORTED = TRUE
        # ALIAS of source target          -> follow alias
        # System / interface             -> skip
        set(_sdl2_real_target SDL2::SDL2)
        get_target_property(_alias ${_sdl2_real_target} ALIASED_TARGET)
        if (_alias)
            set(_sdl2_real_target ${_alias})
        endif()
        get_target_property(_sdl2_type ${_sdl2_real_target} TYPE)
        if (_sdl2_type STREQUAL "SHARED_LIBRARY")
            # OUTPUT-form (vs POST_BUILD) so the copy + COMMENT only fire when
            # the source DLL is newer than the destination. copy_if_different
            # is silent on no-change, but POST_BUILD itself prints the COMMENT
            # every build -- noise that hides real activity.
            #
            # Stamp file in $<CONFIG>/ so multi-config (VS) builds don't share
            # state across Debug/Release. Target-file gen-exprs aren't allowed
            # in OUTPUT, so a literal path + stamp is the portable form.
            set(_sdl2_stamp "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/sdl2_dll.stamp")
            add_custom_command(
                OUTPUT ${_sdl2_stamp}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:SDL2::SDL2>
                    $<TARGET_FILE_DIR:fruit-ninja>
                COMMAND ${CMAKE_COMMAND} -E touch ${_sdl2_stamp}
                DEPENDS $<TARGET_FILE:SDL2::SDL2>
                COMMENT "Copying SDL2.dll next to fruit-ninja.exe"
                VERBATIM
            )
            add_custom_target(copy_sdl2_dll ALL DEPENDS ${_sdl2_stamp})
            # Ensure $<TARGET_FILE_DIR:fruit-ninja> exists before the copy.
            add_dependencies(copy_sdl2_dll fruit-ninja)
        endif()
    endif()
endif()

option(FRUITNINJA_BOMB_TRACE "Enable per-frame Bomb::Update trace logging" OFF)
if (FRUITNINJA_BOMB_TRACE)
    target_compile_definitions(fruit-ninja-game PRIVATE MORTAR_BOMB_TRACE=1)
endif()

# Port specific: propagate FN_RENDER_INTERP as a compile definition so
# RenderInterp.h / GameSDL.cpp / mainEmscripten.cpp can gate on it.
if (FN_RENDER_INTERP)
    target_compile_definitions(fruit-ninja-game PUBLIC FN_RENDER_INTERP=1)
    message(STATUS "FN_RENDER_INTERP = ON")
else()
    message(STATUS "FN_RENDER_INTERP = OFF")
endif()

# propagate the web 60fps debug switch (gates the emscripten main-loop fps arg).
if (FN_WEB_FORCE_60FPS)
    target_compile_definitions(fruit-ninja-game PUBLIC FN_WEB_FORCE_60FPS=1)
    message(STATUS "FN_WEB_FORCE_60FPS = ON")
endif()

# touch drain/dispatch debug logging (target-wide so InputTranslatorSDL.cpp +
# Touch.cpp + SlashEntity.cpp + GameSDL.cpp all emit their [TOUCH]/[SLASH] trace).
if (FN_DEBUG_TOUCH)
    target_compile_definitions(fruit-ninja-game PUBLIC FN_DEBUG_TOUCH=1)
    message(STATUS "FN_DEBUG_TOUCH = ON")
endif()

if (NOT EMSCRIPTEN)
    # --- ctest ---
    enable_testing()
    add_subdirectory(tests)
endif()
endif()  # NOT FN_ASM_VERIFY
