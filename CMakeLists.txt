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

# --- webOS TV port scaffolding ---
# Reuses the existing SDL2 backend (mainSDL.cpp, GameSDL.cpp,
# InputTranslatorSDL, gl_funcsSDL, DisplayManagerSDL, SoundManagerSDL,
# FileSystemPosix) built with the webOS buildroot cross-toolchain, linking
# the SDK's SYSTEM SDL2 + GLESv2 + EGL via pkg-config -- no vendoring.
# Auto-enabled when cross-compiling with the arm-webos-linux-gnueabi
# toolchain (see the CMAKE_CXX_COMPILER_TARGET check further below, after
# project() -- CMAKE_CXX_COMPILER_TARGET/CMAKE_SYSROOT aren't populated from
# the toolchain file until the first project() call); can also be forced
# explicitly with -DFRUIT_PLATFORM_WEBOS=ON.
option(FRUIT_PLATFORM_WEBOS "Build for webOS TV (SDL2 + GLES2)" OFF)

# App version string, substituted into deploy/webos/appinfo.json (@FN_APPINFO_VERSION@)
# and matching tools/wii/hbc/meta.xml's <version>. No PROJECT_VERSION is set on
# the project() call below (this isn't versioned via CMake's own mechanism), so
# this is the single source of truth for the shipped webOS package version.
set(FN_APP_VERSION "1.6.1" CACHE STRING "App version string (webOS appinfo.json, etc.)")

# --- Dynamic-TTF rasterizer backend (compile-time swappable) ---
# FontCacheObjectTTF (src/engine/render/FontCacheObjectTTF.cpp) rasterizes
# CJK/Arabic TTF glyphs through the Mortar::TtfFace seam
# (src/engine/render/TtfBackend.h); exactly one of TtfBackendFreetype.cpp /
# TtfBackendStb.cpp is compiled, selected here. "freetype" is the default on
# every platform that has it available (host/web).
# Task #54: Wii is TTF-BACKEND-FREE -- neither backend is compiled/linked for
# FRUIT_PLATFORM_WII (see the `if (NOT FRUIT_PLATFORM_WII)` guard in
# src/engine/CMakeLists.txt). Every glyph + face metric on Wii comes from the
# offline-baked FNT3 atlas (BakedFontWii.cpp), not a runtime rasterizer. This
# cache entry is simply unused/inert when FRUIT_PLATFORM_WII is ON.
set(FN_TTF_BACKEND "freetype" CACHE STRING "Dynamic TTF rasterizer backend: freetype or stb (ignored on Wii)")
set_property(CACHE FN_TTF_BACKEND PROPERTY STRINGS freetype stb)
message(STATUS "FN_TTF_BACKEND = ${FN_TTF_BACKEND}")
if (FRUIT_PLATFORM_WII)
    message(STATUS "  (Wii: FN_TTF_BACKEND ignored -- no runtime TTF backend is linked, see task #54)")
endif()

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

# HD texture fallback: when ON, TextureManager::Load silently prefers an
# "hd_"-prefixed sibling file (2x pixel art) when present on disk. Default ON,
# but forced OFF on Wii -- MEM1 (~24MB) can't afford the doubled texture RAM.
# Override with -DFN_ENABLE_HD_ASSETS=ON to re-enable on Wii anyway.
option(FN_ENABLE_HD_ASSETS "Load hd_-prefixed 2x textures when available" ON)
if(FRUIT_PLATFORM_WII)
    set(FN_ENABLE_HD_ASSETS OFF)
endif()

# --- Prebaked bitmap font atlases (FreeType-baked, offline) ---
# Wii (task #51/#52/#54): the SOLE glyph source -- stb_truetype clips CJK
# glyphs and breaks Korean composition, and no TTF backend links on Wii at
# all. Default ON there (existing behaviour, unconditional).
#
# Non-Wii (host/web): same shape, opt-in. When ON, the prebaked atlas is
# likewise the SOLE glyph source -- FontCacheObjectTTF's TtfFace path (both
# FreeType and stb_truetype) is excluded from the build entirely (see
# src/engine/CMakeLists.txt), no fallback on a baked miss. The baked glyph
# set is pruned (task #55) to cover every codepoint the game actually
# renders, so this is safe. Default OFF: zero behaviour change on host/web
# unless a dev opts in with -DFN_PREBAKED_FONTS=ON (and has baked
# tools/wii/bake-fonts.py --tex output staged under fonts/prebaked -- see
# the fn_prebaked_fonts target below).
option(FN_PREBAKED_FONTS "Prebaked bitmap font atlases (FreeType-baked offline)" ${FRUIT_PLATFORM_WII})

# --- Ogg Vorbis SFX/music (SDL backend) ---
# webOS ships Ogg-transcoded sfx/*.ogg (via stage-assets.py --ogg-audio,
# mirroring the web build's Ogg transcode) instead of raw sfx/*.wav.pcm, to
# shrink the IPK. SoundManagerSDL::LoadSound decodes the .ogg with
# third_party/stb/stb_vorbis.c (src/engine/CMakeLists.txt) and reads the loop
# point from the build-generated SfxLoopTable.generated.cpp (see
# src/engine/audio/SfxLoopTable.h) instead of the .wav.pcm header (gone once
# audio ships as .ogg). Default ON for webOS, OFF elsewhere: host/emscripten
# builds are unaffected (emscripten already has its own Ogg path via the Web
# Audio backend, SoundManagerWebAudio.cpp -- this option is SDL-backend-only).
option(FN_OGG_AUDIO "Ogg Vorbis SFX/music (SDL backend)" ${FRUIT_PLATFORM_WEBOS})

# --- Block-preload resource loader (task #36) ---
# Force-loads a screen/mode's texture/SFX/mesh/font manifest synchronously at
# a block-entry hook (GameModeScreen::SetupLevel, ShopScreen ctor, etc)
# instead of letting each asset lazy-load the first time it's touched.
# Originally Wii-only (MEM1/MEM2 budget forced eager boot-load off); the
# loader itself (src/resource/{BlockLoader,ResBlock,IResourceSource}.*) is
# ~95% platform-agnostic, so this option lets any target build+enable it for
# testing. Default ON when FRUIT_PLATFORM_WII (unchanged existing Wii
# behaviour), else OFF (zero behaviour change for host/web by default).
if(FRUIT_PLATFORM_WII)
    set(_fn_block_preload_default ON)
else()
    set(_fn_block_preload_default OFF)
endif()
option(FN_BLOCK_PRELOAD "Block-preload resource loader" ${_fn_block_preload_default})

# --- Simulated slow SD IO (dev/test enhancement) ---
# Injects an artificial delay (proportional to bytes read, plus a fixed
# per-read latency) into both file-read funnels (FileSystem_Direct::
# GetFileData, IFile_Direct::Read) so the loading UX can be exercised on fast
# host/web storage. Default OFF -- zero behaviour change unless enabled.
option(FN_SIMULATE_SLOW_IO "Inject artificial delay into file reads (simulate slow SD)" OFF)
set(FN_SLOW_IO_KBPS "2048" CACHE STRING "Simulated read throughput in KB/s")
set(FN_SLOW_IO_LATENCY_US "0" CACHE STRING "Fixed per-read latency in microseconds")

project(FruitNinja LANGUAGES CXX)

# Propagate FRUIT_PLATFORM_WII as a compile definition so the #ifdef
# FRUIT_PLATFORM_WII guards in src/platform/wii/*.{h,cpp} actually gate
# compilation when this path is deliberately opted into. OFF by default, so
# this add_compile_definitions call is a no-op for every other configure.
if (FRUIT_PLATFORM_WII)
    add_compile_definitions(FRUIT_PLATFORM_WII=1)
endif()

# Auto-enable FRUIT_PLATFORM_WEBOS when cross-compiling with the webOS
# buildroot toolchain (CMAKE_CXX_COMPILER_TARGET / CMAKE_SYSROOT are only
# populated after project() processes CMAKE_TOOLCHAIN_FILE, hence this runs
# here rather than next to the option() declaration above). Explicit
# -DFRUIT_PLATFORM_WEBOS=ON always works regardless of toolchain detection.
if (NOT FRUIT_PLATFORM_WEBOS AND
    (CMAKE_CXX_COMPILER_TARGET MATCHES "arm-webos-linux-gnueabi" OR
     CMAKE_SYSROOT MATCHES "webos"))
    message(STATUS "webOS buildroot toolchain detected -- auto-enabling FRUIT_PLATFORM_WEBOS")
    set(FRUIT_PLATFORM_WEBOS ON)
endif()
if (FRUIT_PLATFORM_WEBOS)
    add_compile_definitions(FRUIT_PLATFORM_WEBOS=1)
endif()

# HD texture fallback gate (see the FN_ENABLE_HD_ASSETS option above). Consumed
# by TextureManager::Load. Defined engine-wide so the asset layer sees it.
if (FN_ENABLE_HD_ASSETS)
    add_compile_definitions(FN_ENABLE_HD_ASSETS=1)
endif()

# ---------------------------------------------------------------------------
# fn_link_game_engine_group -- link fruit-ninja-game + mortar_engine as a
# resolved-together pair.
#
# The MP-revival wiring made mortar_engine (NetworkManager::Update ->
# PacketFactory::Create; P2PMessageHandling.cpp's GlobalP2PMessageHandler ->
# Fruit/WaveManager/AddToCurrentScore/MissControl/EntityTracker) call back
# into fruit-ninja-game, while fruit-ninja-game already PUBLIC-links
# mortar_engine. The two static libs are genuinely mutually dependent -- this
# mirrors the original binary, which is one monolithic module where
# NetworkManager legitimately calls into game-side packet/entity code; the
# engine/game split into two static libs is only a port-side convenience.
#
# MSVC multi-passes archives and links this cycle fine (target_link_libraries
# PRIVATE fruit-ninja-game already pulls mortar_engine in transitively via its
# PUBLIC link). GNU ld (webOS toolchain, and any other GNU-like linker) is
# single-pass and leaves mortar_engine's references to game symbols
# unresolved unless the two archives are wrapped in --start-group/--end-group
# so the linker re-scans them until everything resolves.
#
# cmake_minimum_required here is 3.20, below the 3.24 needed for the
# $<LINK_GROUP:RESCAN,...> generator expression, so this uses the raw
# -Wl,--start-group/--end-group linker flags instead. MSVC (and any other
# non-GNU-like linker) just gets the plain, already-working link.
function(fn_link_game_engine_group _target _visibility)
    if (MSVC)
        target_link_libraries(${_target} ${_visibility} fruit-ninja-game)
    else()
        target_link_libraries(${_target} ${_visibility}
            -Wl,--start-group fruit-ninja-game mortar_engine -Wl,--end-group)
    endif()
endfunction()

# Prebaked font atlas gate (see the FN_PREBAKED_FONTS option above). Consumed
# by src/engine/render/BakedFontWii.{h,cpp} (outer guard, was FRUIT_PLATFORM_WII
# only) and the prebaked-atlas branches in FontCacheObjectTTF.{h,cpp} /
# BakedStringTTF.cpp.
if (FN_PREBAKED_FONTS)
    add_compile_definitions(FN_PREBAKED_FONTS=1)
endif()

# Ogg Vorbis gate (see the FN_OGG_AUDIO option above). Consumed by
# SoundManagerSDL.cpp (LoadSound's #if defined(FN_OGG_AUDIO) branch) and
# src/engine/CMakeLists.txt (adds stb_vorbis.c + the generated loop table).
if (FN_OGG_AUDIO)
    add_compile_definitions(FN_OGG_AUDIO=1)
    # third_party/stb/stb_vorbis.c is a C source; the project() above declares
    # only CXX, so CMAKE_C_COMPILE_OBJECT is unset and the generate step fails.
    # Enable C here (only when the Ogg path is compiled in) so the buildroot
    # toolchain's C compiler is configured for that one TU.
    enable_language(C)
endif()

# Block-preload loader gate (see the FN_BLOCK_PRELOAD option above). Consumed
# by src/resource/{BlockLoader,ResBlock,IResourceSource}.* and every hook call
# site (GameInit.cpp, GameInitialise.cpp, MainScreen.cpp, GameModeScreen.cpp,
# ShopScreen.cpp, GameOver.cpp, FruitInfo.cpp, Fruit.h, TextureManager.cpp,
# MeshManager.cpp, PreloadFontsTTF.*, BakedFontWii.cpp).
if (FN_BLOCK_PRELOAD)
    add_compile_definitions(FN_BLOCK_PRELOAD=1)
endif()

# Simulated slow-IO gate (see the FN_SIMULATE_SLOW_IO option above). Consumed
# by src/engine/util/SlowIo.h, called from the two read funnels
# (FileSystem_Direct::GetFileData, IFile_Direct::Read).
if (FN_SIMULATE_SLOW_IO)
    add_compile_definitions(
        FN_SIMULATE_SLOW_IO=1
        FN_SLOW_IO_KBPS=${FN_SLOW_IO_KBPS}
        FN_SLOW_IO_LATENCY_US=${FN_SLOW_IO_LATENCY_US}
    )
endif()

# --- fn_add_platform_source (task #63 item #8 dedup) ---
# Picks WII_SRC when FRUIT_PLATFORM_WII is ON, else DEFAULT_SRC, and writes the
# chosen path into OUT_VAR. Factors the `if(FRUIT_PLATFORM_WII) ... else() ...`
# platform-source twins that recur across this file, src/game/CMakeLists.txt,
# and src/engine/CMakeLists.txt (Game/GameWii, Licensing/LicensingWii,
# Renderer GL/GX) -- those pick exactly one file, nothing else differs.
macro(fn_add_platform_source OUT_VAR WII_SRC DEFAULT_SRC)
    if (FRUIT_PLATFORM_WII)
        set(${OUT_VAR} ${WII_SRC})
    else()
        set(${OUT_VAR} ${DEFAULT_SRC})
    endif()
endmacro()

# --- fn_copy_dll_dependency (task #63 item #4 dedup) ---
# Copies a dependency's runtime DLL next to a target's output directory on
# Windows, POST_BUILD. Resolves DEP_TARGET through ALIASED_TARGET first (the
# SDL2::SDL2 / SDL2_image::SDL2_image aliases point at different underlying
# targets depending on which acquisition tier provided them -- system,
# IMPORTED SHARED prebuilt zip, or a FetchContent source build) and only
# copies when the resolved target's TYPE is SHARED_LIBRARY (a system/
# interface target has no importable file location, so the generator
# expression would be empty -- skipping avoids a hard configure error).
#
# Mirrors the shape shared by tests/CMakeLists.txt's fn_add_game_test and
# tests/scenes/CMakeLists.txt's add_scene_test (SDL2 + SDL2_image copies).
# NOT used at the root fruit-ninja.exe site: that one intentionally uses an
# OUTPUT-form stamp file instead of POST_BUILD (see the copy_sdl2_dll comment
# there) so the COMMENT only prints on an actual copy, not every build; the
# WIN32-vs-non-WIN32 + TARGET-existence guards must still be applied by the
# caller (this macro assumes both are already true).
#
# Params:
#   TGT            target to attach the POST_BUILD copy to
#   DEP_TARGET     dependency target to resolve + copy (e.g. SDL2::SDL2)
macro(fn_copy_dll_dependency TGT DEP_TARGET)
    set(_fn_dll_real_target ${DEP_TARGET})
    get_target_property(_fn_dll_alias ${_fn_dll_real_target} ALIASED_TARGET)
    if (_fn_dll_alias)
        set(_fn_dll_real_target ${_fn_dll_alias})
    endif()
    get_target_property(_fn_dll_type ${_fn_dll_real_target} TYPE)
    if (_fn_dll_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET ${TGT} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${DEP_TARGET}>
                $<TARGET_FILE_DIR:${TGT}>)
    endif()
endmacro()

# --- fn_fetch_prebuilt_msvc_zip (task #63 item #5 dedup) ---
# Shared sub-part of the SDL2 / SDL2_image "3-tier" dependency acquisition
# (system/find_package -> prebuilt-zip FetchContent -> source FetchContent):
# the MSVC-only prebuilt-zip tier. Declares + fetches a libsdl-org release
# zip, then wires an IMPORTED SHARED <NS>::<NAME> target from its lib/x64
# layout (both SDL2-devel-*-VC.zip and SDL2_image-devel-*-VC.zip share this
# layout). URL_HASH is optional (SDL2_image's zip release doesn't pin one).
#
# The two callers diverge beyond this tier -- SDL2 additionally has an
# Emscripten branch and a non-MSVC GIT_REPOSITORY source-build fallback;
# SDL2_image has neither (it's test-only, never built for web, and MSVC-only
# for the zip path with just a warning elsewhere) -- so those tiers stay
# inline at each call site rather than forcing a single mega-macro.
#
# Params:
#   FC_NAME       FetchContent_Declare name (e.g. SDL2_prebuilt_msvc)
#   URL           zip URL
#   URL_HASH      SHA256=... or "" to omit
#   NS_TARGET     the IMPORTED SHARED target to define (e.g. SDL2::SDL2)
#   DLL_BASENAME  DLL/lib basename without extension (e.g. SDL2, SDL2_image)
#   OUT_FOUND     variable set to TRUE on success (mirrors *_FOUND)
function(fn_fetch_prebuilt_msvc_zip FC_NAME URL URL_HASH NS_TARGET DLL_BASENAME OUT_FOUND)
    if (URL_HASH)
        FetchContent_Declare(${FC_NAME} URL ${URL} URL_HASH ${URL_HASH})
    else()
        FetchContent_Declare(${FC_NAME} URL ${URL})
    endif()
    FetchContent_MakeAvailable(${FC_NAME})

    string(TOLOWER ${FC_NAME} _fn_fc_name_lower)
    set(_fn_zip_root "${${_fn_fc_name_lower}_SOURCE_DIR}")
    set(_fn_zip_arch "x64")  # FN binary is 64-bit MSVC
    set(_fn_zip_lib_dir "${_fn_zip_root}/lib/${_fn_zip_arch}")

    add_library(${NS_TARGET} SHARED IMPORTED)
    set_target_properties(${NS_TARGET} PROPERTIES
        IMPORTED_LOCATION             "${_fn_zip_lib_dir}/${DLL_BASENAME}.dll"
        IMPORTED_IMPLIB               "${_fn_zip_lib_dir}/${DLL_BASENAME}.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_fn_zip_root}/include")

    set(${OUT_FOUND} TRUE PARENT_SCOPE)
endfunction()

# Used to invoke tools/assets/stage-assets.py (asset staging: textures ->
# WebP for both host + web) and tools/web/web-hash-assets.py (web cache
# busting) portably -- avoids relying on a `python3` shim being on PATH
# (unreliable on Windows).
find_package(Python3 COMPONENTS Interpreter REQUIRED)

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

# --- Emscripten: strip non-error logging (Port specific) ---
# On web every surviving log line reaches the browser console, and Chrome
# DevTools captures a stack trace for each one while the Performance panel is
# recording -- ~230 lines cost ~650ms in a 26s profile, more than any single GL
# call, which was enough to misdirect optimisation work. Compiling the sub-Error
# tiers out removes the cost outright (no formatting, no call) rather than
# managing it in the shell.
# LOG_ERROR survives: errors are rare, and a silent web build is much harder to
# diagnose. FN_WEB_DEBUG builds keep every tier.
if (EMSCRIPTEN AND NOT FN_WEB_DEBUG)
    add_compile_definitions(FN_LOG_ERRORS_ONLY=1)
    message(STATUS "Emscripten: non-error logging compiled out (FN_LOG_ERRORS_ONLY)")
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
#   ES1        - OpenGL ES 1.1 (<GLES/gl.h>) for embedded fixed-function.
#   ES2        - OpenGL ES 2.0 (<GLES2/gl2.h>) for webOS TV -- real shader
#                entry points (the ES1 path lacks them; libGLESv1_CM does
#                not export glCreateShader et al.).
if (EMSCRIPTEN)
    # Force FRUIT_GL_API to the new EMSCRIPTEN path; skip the user-visible cache entry.
    set(FRUIT_GL_API "EMSCRIPTEN")
else()
    # webOS defaults to ES2 (the only backend with real GLES2 shader
    # entry points) unless the user explicitly set FRUIT_GL_API themselves.
    if (FRUIT_PLATFORM_WEBOS AND NOT DEFINED CACHE{FRUIT_GL_API})
        set(FRUIT_GL_API_DEFAULT "ES2")
    else()
        set(FRUIT_GL_API_DEFAULT "GL_COMPAT")
    endif()
    set(FRUIT_GL_API "${FRUIT_GL_API_DEFAULT}" CACHE STRING "GL backend: GL_COMPAT, ES1, or ES2")
    set_property(CACHE FRUIT_GL_API PROPERTY STRINGS GL_COMPAT ES1 ES2)
endif()
message(STATUS "FRUIT_GL_API = ${FRUIT_GL_API}")

if (FRUIT_GL_API STREQUAL "GL_COMPAT")
    add_compile_definitions(FRUIT_GL_API_GL_COMPAT=1)
elseif (FRUIT_GL_API STREQUAL "ES1")
    add_compile_definitions(FRUIT_GL_API_ES1=1)
elseif (FRUIT_GL_API STREQUAL "ES2")
    add_compile_definitions(FRUIT_GL_API_ES2=1)
elseif (FRUIT_GL_API STREQUAL "EMSCRIPTEN")
    add_compile_definitions(FRUIT_GL_API_EMSCRIPTEN=1)
else()
    message(FATAL_ERROR "Unknown FRUIT_GL_API '${FRUIT_GL_API}'")
endif()

# --- Dependencies ---

include(FetchContent)
set(_SDL2_VER 2.30.11)

# Wii has no SDL2/SDL2_image/FreeType -- GX handles video, ASND/AESND audio,
# libfat filesystem, WPAD input. Guard each dependency block below so a Wii
# configure never touches FetchContent/find_package for these; nothing in
# src/platform/wii links against them (see src/platform/wii/README.md).
if(NOT FN_ASM_VERIFY AND NOT FRUIT_PLATFORM_WII)

if (FRUIT_PLATFORM_WEBOS)
    # webOS TV: use the SDK sysroot's SYSTEM SDL2 via pkg-config -- confirmed
    # present as the "sdl2" pkg-config module in the buildroot sysroot. No
    # FetchContent/vendored SDL2 path runs for this platform.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    if (NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
    endif()
    # webOS has no separate SDL2main static lib in the sysroot (that's a
    # Windows/MSVC-only shim to redirect WinMain -> main); the executable
    # target below still links against SDL2::SDL2main, so provide it as an
    # empty INTERFACE alias -- main() is the real entry point on Linux/webOS.
    if (NOT TARGET SDL2::SDL2main)
        add_library(SDL2main_webos_iface INTERFACE)
        add_library(SDL2::SDL2main ALIAS SDL2main_webos_iface)
    endif()
elseif (EMSCRIPTEN)
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
        fn_fetch_prebuilt_msvc_zip(
            SDL2_prebuilt_msvc
            "https://github.com/libsdl-org/SDL/releases/download/release-${_SDL2_VER}/SDL2-devel-${_SDL2_VER}-VC.zip"
            "SHA256=6e524b5f0e3ecd9fe3b439336b5e7c3fe597e8823e8e62c8e3074d335e9ca7d9"
            SDL2::SDL2 SDL2 SDL2_FOUND)

        # SDL2main has no counterpart in the shared zip-fetch helper (SDL2_image
        # ships no *main target) -- wire it directly from the same lib dir.
        add_library(SDL2::SDL2main STATIC IMPORTED)
        set_target_properties(SDL2::SDL2main PROPERTIES
            IMPORTED_LOCATION "${sdl2_prebuilt_msvc_SOURCE_DIR}/lib/x64/SDL2main.lib")
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

if(NOT FN_ASM_VERIFY AND NOT FRUIT_PLATFORM_WII)

find_package(SDL2_image QUIET)
if (NOT SDL2_image_FOUND AND MSVC)
    message(STATUS "System SDL2_image not found -- fetching prebuilt MSVC zip ${_SDL2_IMAGE_VER}")
    fn_fetch_prebuilt_msvc_zip(
        SDL2_image_prebuilt_msvc
        "https://github.com/libsdl-org/SDL_image/releases/download/release-${_SDL2_IMAGE_VER}/SDL2_image-devel-${_SDL2_IMAGE_VER}-VC.zip"
        ""
        SDL2_image::SDL2_image SDL2_image SDL2_image_FOUND)
endif()

if (NOT SDL2_image_FOUND AND NOT MSVC)
    # MSYS2/Linux should have it as a system package; warn rather than pulling
    # source (SDL_image has many optional codec dependencies that complicate
    # a source build significantly).
    message(WARNING "SDL2_image not found; install mingw-w64-x86_64-SDL2_image (MSYS2) or libsdl2-image-dev (Debian). Test screenshots will not link.")
endif()

endif() # NOT FN_ASM_VERIFY AND NOT FRUIT_PLATFORM_WII (SDL2_image block)

# Prefer a system/vcpkg tinyxml2 (provides tinyxml2::tinyxml2); fall back to a
# FetchContent source build when not installed. Both providers export the same
# tinyxml2::tinyxml2 target, so the link line is identical either way.
# NOTE: this block only runs for host/web (it's inside the
# `NOT FN_ASM_VERIFY AND NOT FRUIT_PLATFORM_WII` wrapper). The Wii tinyxml2
# source build lives in that wrapper's else() branch below, and asm-verify
# stubs it -- both providers export the same tinyxml2::tinyxml2 target.
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
# FN_ASM_VERIFY=ON or FRUIT_PLATFORM_WII=ON: stub INTERFACE targets so any
# stray target_link_libraries reference resolves to a no-op rather than a
# configure error. Cross-headers provide replacement headers for the
# asm-verify branch; no SDL2/GL/FreeType libraries are actually linked for
# either branch. tinyxml2 headers are copied by verify.sh from the host
# build into build/host/_deps/tinyxml2-src (asm-verify only).
foreach(_tgt SDL2::SDL2 SDL2::SDL2main)
    if(NOT TARGET ${_tgt})
        add_library(${_tgt} INTERFACE IMPORTED)
    endif()
endforeach()
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype INTERFACE IMPORTED)
endif()
# tinyxml2 is REAL for Wii (portable C++, cross-compiles for PPC). Only the
# asm-verify branch keeps the header-stub path (verify.sh copies headers); the
# Wii branch builds tinyxml2 from source so tinyxml2::tinyxml2 exists to link.
if(FRUIT_PLATFORM_WII AND NOT TARGET tinyxml2::tinyxml2)
    message(STATUS "Wii: fetching tinyxml2 10.0.0 source (no system/vcpkg config under the cross-root)")
    FetchContent_Declare(
        tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG        10.0.0
        # EXCLUDE_FROM_ALL (CMake 3.28+): keep tinyxml2's own install() rules out
        # of our install/CPack package -- otherwise its header + static lib +
        # cmake/pkgconfig files leak into the shippable apps/ zip (include/, lib/).
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(tinyxml2)
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
elseif (FRUIT_PLATFORM_WII)
    # Wii: assets live on the SD card under the homebrew app dir; libfat mounts
    # sd:/ at boot (see mainWii.cpp fatInitDefault), so this is a device path,
    # NOT a host path. Deploy build/wii/staging/Data -> sd:/apps/fruitninja/Data
    # on the card (alongside boot.dol). Uncompressed Tex1/.wav.pcm (--wii staging).
    set(FN_DATA_DIR_PATH "sd:/apps/fruitninja/Data")
    # Writable save dir, kept SEPARATE from the (read-only) asset dir above --
    # see FileSystem_Direct::Initialise(data_dir, /*writable=*/false) in
    # GameInitialise.cpp. mainWii.cpp mkdir()s this at boot (it doesn't ship
    # with the staged asset deploy).
    set(FN_SAVE_DIR_PATH "sd:/apps/fruitninja/save")
else()
    # Host now reads from the staged dir (WebP-transcoded textures), not
    # FruitNinjaBada/Data directly -- see fn_asset_staging below.
    file(TO_CMAKE_PATH "${FN_STAGING_DATA_DIR}" FN_DATA_DIR_PATH)
endif()

if(NOT FN_ASM_VERIFY)
    # Platform twin for the Game:: methods (init/pollInput/step/render/run):
    # GameWii.cpp on Wii (GX, no SDL), GameSDL.cpp everywhere else.
    fn_add_platform_source(_fn_game_platform_src src/GameWii.cpp src/GameSDL.cpp)
    add_library(fruit-ninja-game STATIC src/Game.cpp ${_fn_game_platform_src})
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
    add_subdirectory(src/resource)

    if (FRUIT_PLATFORM_WII)
        # No SDL2 on Wii -- GX/ASND/libfat/WPAD replace it entirely (see
        # src/platform/wii/README.md). tinyxml2 is portable C++ and still
        # links; mortar_engine's own OpenGL::GL link is skipped for this
        # platform too (see src/engine/CMakeLists.txt).
        target_link_libraries(fruit-ninja-game PUBLIC
            tinyxml2::tinyxml2 mortar_engine
        )
    else()
        target_link_libraries(fruit-ninja-game PUBLIC
            SDL2::SDL2 tinyxml2::tinyxml2 mortar_engine
        )
    endif()
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
    # AT BUILD TIME (not checked in) by tools/assets/svg-to-webp.py -- a
    # resvg-py + Pillow step run FIRST, before stage-assets.py; it rasterizes
    # assets/ui-widgets/*.svg on every platform including Windows/CLion (no
    # Node/npm/sharp dependency, no offline pre-generation step needed).
    # Widget textures have no runtime placeholder fallback (SettingsScreen /
    # the widget render tests load them directly), so svg-to-webp.py exits
    # non-zero on any failure and fails this build step loudly.
    # Under --web (EMSCRIPTEN) stage-assets.py additionally transcodes
    # sfx/*.wav.pcm to Ogg/Vorbis and subsets fontstruetype/gangofchinese.ttf,
    # for the Web Audio backend (SoundManagerWebAudio.cpp) and the wasm
    # preload size budget; loop points ship via the generated C++ loop table
    # (--gen-loop-table below), linked into the wasm. The host build ships
    # raw .wav.pcm (SDL mixer) and the full TTF, unchanged.
    #
    # Host: FN_DATA_DIR_PATH (set above) points straight at FN_STAGING_DATA_DIR,
    # so GameSDL.cpp's existing SetDataDir call transparently reads staged
    # (WebP) textures -- no runtime code changes. Web: FN_STAGING_DATA_DIR is
    # additionally --preload-file'd into MEMFS at /FruitNinjaBada/Data (below,
    # unchanged mechanism from before, new source dir). add_dependencies below
    # guarantees this runs before fruit-ninja links on both branches (build
    # time, not configure time -- FruitNinjaBada/Data need not exist yet when
    # CMake configures, only when this target actually runs).
    # Wii ships uncompressed assets (raw Tex1 textures, raw .wav.pcm audio) and
    # has no WebP toolchain requirement -- skip svg-to-webp.py (its Wii .rgba
    # sidecars, if present from a prior non-Wii configure of the same tree,
    # are consumed by stage-assets.py --wii; see that script's WII MODE
    # docstring) and pass --wii to stage-assets.py instead of --web, which
    # selects its raw copy-only mode.
    if (FRUIT_PLATFORM_WII)
        set(FN_ASSET_STAGING_COMMANDS
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/assets/stage-assets.py
                    ${CMAKE_SOURCE_DIR} ${FN_STAGING_DATA_DIR} --wii
        )
    else()
        # --subset-font (independent of --web): webOS wants the gangofchinese.ttf
        # glyph-subset shrink for a smaller IPK but NOT --web's Ogg/WebP-audio
        # transcoding. --web already implies --subset-font internally, so web
        # behaviour is unchanged.
        # --ogg-audio + --gen-loop-table: transcodes sfx/*.wav.pcm to
        # sfx/*.ogg and emits the generated C++ loop table
        # (SfxLoopTable.generated.cpp -- {loopStart samples, source rate} per
        # looping sound, see src/engine/audio/SfxLoopTable.h). EVERY Ogg
        # consumer links it: SoundManagerSDL.cpp (FN_OGG_AUDIO, e.g. webOS)
        # reads SfxLoopStartSamples, SoundManagerWebAudio.cpp (EMSCRIPTEN,
        # where --web implies --ogg-audio) reads SfxLoopStartSeconds -- no
        # runtime loop-metadata file exists, so a build missing the table
        # fails at link, never by silently degrading loops at runtime.
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
        if (FN_OGG_AUDIO OR EMSCRIPTEN)
            set(FN_GEN_LOOP_TABLE ON)
        else()
            set(FN_GEN_LOOP_TABLE OFF)
        endif()
        set(FN_ASSET_STAGING_COMMANDS
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/assets/svg-to-webp.py ${CMAKE_SOURCE_DIR}
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/assets/stage-assets.py
                    ${CMAKE_SOURCE_DIR} ${FN_STAGING_DATA_DIR}
                    $<$<BOOL:${EMSCRIPTEN}>:--web>
                    $<$<BOOL:${FRUIT_PLATFORM_WEBOS}>:--subset-font>
                    $<$<BOOL:${FN_OGG_AUDIO}>:--ogg-audio>
                    $<$<BOOL:${FN_GEN_LOOP_TABLE}>:--gen-loop-table>
                    $<$<BOOL:${FN_GEN_LOOP_TABLE}>:${CMAKE_BINARY_DIR}/generated/SfxLoopTable.generated.cpp>
        )
    endif()
    # ALL: the host runtime + every test read assets from FN_STAGING_DATA_DIR
    # (FN_DATA_DIR_PATH points there), so staging must run on ANY build -- not just
    # when the fruit-ninja exe target is built. Idempotent (mtime), so it's a no-op
    # once staged.
    add_custom_target(fn_asset_staging ALL
        ${FN_ASSET_STAGING_COMMANDS}
        COMMENT "Staging assets (svg->webp via resvg-py; textures->WebP; web:+ogg+font)"
        VERBATIM
    )

    # FN_GEN_LOOP_TABLE (FN_OGG_AUDIO or EMSCRIPTEN): mortar_engine's
    # audio/SfxLoopTable.generated.cpp source (added by
    # src/engine/CMakeLists.txt) is WRITTEN by fn_asset_staging above
    # (stage-assets.py --gen-loop-table), so mortar_engine must not compile
    # before staging runs. mortar_engine already exists here (created by
    # add_subdirectory(src/engine) further up), so the dependency is added
    # from this scope rather than from within src/engine/CMakeLists.txt
    # (fn_asset_staging doesn't exist yet at that point in the file).
    if (FN_GEN_LOOP_TABLE)
        add_dependencies(mortar_engine fn_asset_staging)
    endif()

    # --- Prebaked font bake, non-Wii (FN_PREBAKED_FONTS) ---
    # Host/web counterpart of the Wii fn_wii_fonts target below (see that
    # target's comment for the full rationale) -- runs tools/wii/bake-fonts.py
    # with --tex so pages land as lossless WebP-in-.tex instead of Wii GXTX
    # (the .idx metrics sidecar is identical either way; see BakedFontWii.cpp's
    # non-Wii arm). Same graceful-skip shape as fn_wii_fonts: no
    # tmp/prebake/bake_plan.json, or no freetype-py on the bake interpreter,
    # just means fonts/prebaked stays empty -- BUT NOTE this configuration has
    # NO FreeType/stb fallback (same as Wii; see FontCacheObjectTTF's
    # FN_PREBAKED_FONTS guards + src/engine/CMakeLists.txt's TU exclusion), so
    # a skipped bake means text renders nothing on an FN_PREBAKED_FONTS=ON
    # build. Only opt in once a bake is actually staged.
    if (FN_PREBAKED_FONTS AND NOT FRUIT_PLATFORM_WII)
        # Discover a default interpreter (Windows-form path, NOT the MSYS
        # "/mingw64/..." form, so the native host/emscripten cmake can resolve
        # it); overridable via -D on other setups. bake-fonts.py self-provisions
        # its libfreetype DLL search.
        # On MSYS2/Windows the discovered python (e.g. plain "python") likely
        # lacks freetype-py -- point FN_PREBAKED_FONTS_PYTHON at the mingw64
        # python that has it installed (pacman -S mingw-w64-x86_64-python-freetype-py),
        # e.g. -DFN_PREBAKED_FONTS_PYTHON=C:/msys64/mingw64/bin/python.exe.
        find_program(_fn_prebaked_fonts_python_default NAMES python3 python)
        set(FN_PREBAKED_FONTS_PYTHON "${_fn_prebaked_fonts_python_default}" CACHE FILEPATH
            "Python interpreter (with freetype-py) used to bake non-Wii prebaked fonts")

        set(_fn_prebaked_bake_plan "${CMAKE_SOURCE_DIR}/tmp/prebake/bake_plan.json")
        set(_fn_prebaked_bake_ok FALSE)
        if (EXISTS "${_fn_prebaked_bake_plan}")
            # Mirror bake-fonts.py's self-contained DLL search (prepend the
            # interpreter's own dir to PATH so freetype-py finds libfreetype),
            # rather than a hardcoded PATH=/mingw64/bin which is malformed on a
            # native Windows cmake (wrong separator/path form).
            execute_process(
                COMMAND "${FN_PREBAKED_FONTS_PYTHON}" -c "import os,sys; os.environ['PATH']=os.path.dirname(os.path.abspath(sys.executable))+os.pathsep+os.environ.get('PATH',''); import freetype"
                RESULT_VARIABLE _fn_prebaked_bake_python_rc
                OUTPUT_QUIET ERROR_QUIET
            )
            if (_fn_prebaked_bake_python_rc EQUAL 0)
                set(_fn_prebaked_bake_ok TRUE)
            endif()
        endif()

        if (_fn_prebaked_bake_ok)
            message(STATUS "Prebaked fonts: bake enabled (${FN_PREBAKED_FONTS_PYTHON})")
            add_custom_target(fn_prebaked_fonts
                # bake-fonts.py self-provisions its libfreetype DLL search (adds
                # the interpreter's own dir to PATH), so no external PATH= hack.
                COMMAND "${FN_PREBAKED_FONTS_PYTHON}" "${CMAKE_SOURCE_DIR}/tools/wii/bake-fonts.py"
                        "${CMAKE_SOURCE_DIR}/tmp/prebake"
                        "${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data/fontstruetype"
                        "${FN_STAGING_DATA_DIR}/fonts/prebaked"
                        --tex
                COMMENT "Baking non-Wii prebaked font atlases (freetype-py, WebP-in-.tex)"
                VERBATIM
            )
            add_dependencies(fn_prebaked_fonts fn_asset_staging)
            # fruit-ninja does not exist yet at this point in the file (it is
            # created per-platform further below) -- each add_executable(fruit-ninja)
            # branch adds this dependency itself, guarded the same way.
        else()
            message(WARNING
                "Prebaked fonts: bake SKIPPED (no tmp/prebake/bake_plan.json, "
                "or ${FN_PREBAKED_FONTS_PYTHON} lacks freetype-py) -- fonts/prebaked "
                "will be empty and FN_PREBAKED_FONTS builds have NO FreeType/stb "
                "fallback, so text will render nothing until a bake is staged")
        endif()
    endif()

if (EMSCRIPTEN)
    # Port specific: Emscripten entry point uses emscripten_set_main_loop_arg
    # instead of the blocking Game::run() loop.
    add_executable(fruit-ninja src/mainEmscripten.cpp)
    add_dependencies(fruit-ninja fn_asset_staging)
    if (TARGET fn_prebaked_fonts)
        add_dependencies(fruit-ninja fn_prebaked_fonts)
    endif()
    fn_link_game_engine_group(fruit-ninja PRIVATE)
    target_link_libraries(fruit-ninja PRIVATE SDL2::SDL2main)
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
        # Source is the STAGED data dir (sfx transcoded to Ogg/Vorbis,
        # textures re-encoded to WebP, at build time, everything else
        # identical to FruitNinjaBada/Data) -- see fn_asset_staging above.
        # Loop points are NOT in this bundle: they ship as the generated C++
        # loop table linked into the wasm (see the --gen-loop-table comment
        # above). Virtual mount path is unchanged, so runtime code
        # (TextureManager::GetDataDir() etc.) needs no changes; the Web
        # Audio backend reads sfx/<name>.ogg.
        "SHELL:--preload-file ${FN_STAGING_DATA_DIR}@/FruitNinjaBada/Data"
        "SHELL:--exclude-file *FruitySave.xml"
        "SHELL:--exclude-file *ItemSave.xml"
        # Port specific: link idbfs.js so FS.mount(IDBFS) is available.
        "SHELL:-lidbfs.js"
        # Port specific: export fn_idbfs_ready, fn_user_started,
        # fn_web_synth_touch, fn_set_audio_enabled, and fn_audio_consent_done
        # so JS can call them as Module._fn_idbfs_ready() /
        # Module._fn_user_started() / Module._fn_web_synth_touch() /
        # Module._fn_set_audio_enabled() / Module._fn_audio_consent_done().
        # The third is called by shell.html's document-level outside-canvas
        # touch capture (see mainEmscripten.cpp fn_web_synth_touch for why);
        # the fourth is called ONLY by the true-first-run two-button overlay
        # (PLAY WITH SOUND / PLAY MUTED) to unlock the AudioContext (the
        # gesture) and set the first-run game_work.m_bSoundOn/m_bMusicOn
        # default (safe -- no saved preference exists yet to clobber); the
        # fifth is called whenever the overlay's job (unlock only, or nothing
        # to unlock) is done, WITHOUT touching game_work.m_bSoundOn/
        # m_bMusicOn -- see mainEmscripten.cpp's g_gameInited comment block
        # for the full 4-way decision. The sfx/music PREFERENCE itself is
        # genuinely persisted via FruitSaveData's "soundOff"/"musicOff"
        # totals (GameInitialise.cpp) -- this overlay only gates the
        # browser's separate AudioContext unlock requirement, never the
        # preference.
        # EMSCRIPTEN_KEEPALIVE alone is sufficient for most linker configs but
        # an explicit -sEXPORTED_FUNCTIONS ensures they survive aggressive DCE.
        # BUG HISTORY: fn_set_audio_enabled was added to mainEmscripten.cpp
        # without adding it here -- the JS call site's `Module._fn_set_audio_enabled`
        # guard silently no-op'd (function never existed on Module), so the
        # audio unlock/session-state setter never ran on ANY tap.
        # fn_save_on_exit: called by the tab-hide lifecycle handlers
        # (visibilitychange/pagehide/beforeunload, installed in
        # mainEmscripten.cpp main()) so the C++ side writes its save files
        # BEFORE the FS.syncfs(false) push to IndexedDB -- its JS call site
        # uses the same existence-guard pattern, so forgetting it here would
        # silently no-op exactly like the fn_set_audio_enabled bug above.
        "SHELL:-sEXPORTED_FUNCTIONS=['_main','_fn_idbfs_ready','_fn_user_started','_fn_web_synth_touch','_fn_set_audio_enabled','_fn_audio_consent_done','_fn_save_on_exit']"
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
    add_dependencies(fruit-ninja fn_asset_staging)
    # libogc link set. -lfat (SD/USB), -lwiiuse + -lbte (Wiimote/Bluetooth),
    # -lasnd (audio, SoundManagerWii), -logc (GX/VIDEO/core), -lm (libm).
    # devkitPro's Wii.cmake toolchain already put $DEVKITPRO/libogc/lib/wii on
    # the link path, so bare -l names resolve. --gc-sections is fine: main()
    # transitively references every needed symbol, and libogc functions are
    # pulled by the shim/backends -- nothing live is dropped.
    fn_link_game_engine_group(fruit-ninja PRIVATE)
    target_link_libraries(fruit-ninja PRIVATE
        -lfat -lwiiuse -lbte -lasnd -logc -lm)

    # --- elf2dol (POST_BUILD) ---
    # cmake --build alone only produces fruit-ninja.elf; the Homebrew Channel
    # loads a .dol. Previously tools/wii/build.sh manually re-ran elf2dol with
    # an mtime guard after every `cmake --build`; folding it into the target's
    # own POST_BUILD means `cmake --build build/wii` alone always regenerates
    # boot.dol from the just-linked .elf -- no separate script/step needed, and
    # no possibility of shipping a stale .dol (the mtime-guard trap build.sh
    # used to work around no longer exists because there's nothing to guard).
    find_program(FN_ELF2DOL elf2dol HINTS "$ENV{DEVKITPRO}/tools/bin")
    if (NOT FN_ELF2DOL)
        message(FATAL_ERROR
            "elf2dol not found (looked on PATH and in $DEVKITPRO/tools/bin). "
            "Install devkitPro's general-tools package.")
    endif()
    add_custom_command(TARGET fruit-ninja POST_BUILD
        COMMAND ${FN_ELF2DOL} $<TARGET_FILE:fruit-ninja> ${CMAKE_BINARY_DIR}/boot.dol
        BYPRODUCTS ${CMAKE_BINARY_DIR}/boot.dol
        COMMENT "elf2dol -> boot.dol"
        VERBATIM
    )

    # --- Prebaked font bake (task #51/#52) ---
    # Bakes FreeType-rendered glyph atlases (see tools/wii/bake-fonts.py) into
    # staging/Data/fonts/prebaked. The Wii build links NO runtime TTF backend,
    # so a skipped bake ships a package that renders no text at all -- this
    # step is therefore load-bearing, not optional.
    #
    # The bake plan is GENERATED at build time into ${CMAKE_BINARY_DIR}/prebake
    # by tools/wii/prebake/*.py (pure stdlib, derived from
    # FruitNinjaBada/Data/{stringtables,xml} alone). It used to be read from
    # tmp/prebake/, which is gitignored -- so a fresh checkout (CI) silently
    # skipped the bake and shipped a textless build. Generating into the build
    # tree makes a fresh checkout and a dev machine behave identically.
    #
    # Two interpreters are involved:
    #   Python3_EXECUTABLE  -- the plan generators (pure stdlib, any python3).
    #   FN_WII_BAKE_PYTHON  -- the baker; needs freetype-py plus a libfreetype
    #                          loadable via ctypes. On a Windows/MSYS2 host that
    #                          is the MinGW python (freetype-py +
    #                          libfreetype-6.dll once /mingw64/bin is on PATH);
    #                          elsewhere the discovered python3. Override it if
    #                          freetype-py lives in a venv (CI does this).
    if (CMAKE_HOST_WIN32)
        set(_fn_wii_bake_python_default "/mingw64/bin/python.exe")
    else()
        set(_fn_wii_bake_python_default "${Python3_EXECUTABLE}")
    endif()
    set(FN_WII_BAKE_PYTHON "${_fn_wii_bake_python_default}" CACHE FILEPATH
        "Python interpreter (with freetype-py) used to bake Wii prebaked fonts")

    # The /mingw64/bin PATH prefix only exists (and is only a valid PATH form)
    # on the MSYS2 host; on Linux it is noise, so invoke the baker directly.
    if (CMAKE_HOST_WIN32)
        set(_fn_wii_bake_cmd ${CMAKE_COMMAND} -E env "PATH=/mingw64/bin:$ENV{PATH}"
                             "${FN_WII_BAKE_PYTHON}")
    else()
        set(_fn_wii_bake_cmd "${FN_WII_BAKE_PYTHON}")
    endif()

    set(_fn_wii_prebake_dir "${CMAKE_BINARY_DIR}/prebake")
    set(_fn_wii_prebake_src "${CMAKE_SOURCE_DIR}/tools/wii/prebake")
    set(_fn_wii_prebaked_out "${FN_STAGING_DATA_DIR}/fonts/prebaked")

    # The only remaining graceful skip: the raw asset dump is genuinely absent,
    # so there is nothing to derive a plan from (and staging has no fonts to
    # bake from either). Everything else is a hard error -- see below.
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data/stringtables/translations_header.str")
        message(STATUS
            "Wii: prebaked font bake SKIPPED -- FruitNinjaBada/Data is not present. "
            "The resulting build renders NO TEXT; extract the game asset dump first.")
    else()
        execute_process(
            COMMAND ${_fn_wii_bake_cmd} -c "import freetype"
            RESULT_VARIABLE _fn_wii_bake_python_rc
            OUTPUT_QUIET ERROR_QUIET
        )
        if (NOT _fn_wii_bake_python_rc EQUAL 0)
            message(FATAL_ERROR
                "Wii: ${FN_WII_BAKE_PYTHON} cannot import freetype-py, so the "
                "prebaked font atlases cannot be baked -- the build would render "
                "no text (no runtime TTF backend links on Wii).\n"
                "Install it (pip install freetype-py, or pacman -S "
                "mingw-w64-x86_64-python-freetype-py on MSYS2) or point "
                "-DFN_WII_BAKE_PYTHON=<python> at an interpreter that has it.")
        endif()

        message(STATUS "Wii: prebaked font bake enabled (${FN_WII_BAKE_PYTHON})")
        add_custom_target(fn_wii_fonts
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_fn_wii_prebake_dir}"
            COMMAND "${Python3_EXECUTABLE}" "${_fn_wii_prebake_src}/extract_chars.py"
                    --root "${CMAKE_SOURCE_DIR}" --out "${_fn_wii_prebake_dir}"
            COMMAND "${Python3_EXECUTABLE}" "${_fn_wii_prebake_src}/collect_used.py"
                    --root "${CMAKE_SOURCE_DIR}" --out "${_fn_wii_prebake_dir}"
            COMMAND "${Python3_EXECUTABLE}" "${_fn_wii_prebake_src}/build_plan.py"
                    --root "${CMAKE_SOURCE_DIR}" --out "${_fn_wii_prebake_dir}"
            COMMAND ${_fn_wii_bake_cmd} "${CMAKE_SOURCE_DIR}/tools/wii/bake-fonts.py"
                    "${_fn_wii_prebake_dir}"
                    "${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data/fontstruetype"
                    "${_fn_wii_prebaked_out}"
            # Fail the BUILD (not just warn) if the bake produced nothing --
            # install/CPack would otherwise happily ship a textless package.
            COMMAND ${CMAKE_COMMAND} "-DFN_ASSERT_DIR=${_fn_wii_prebaked_out}"
                    "-DFN_ASSERT_WHAT=Wii prebaked font atlases"
                    -P "${CMAKE_SOURCE_DIR}/cmake/assert-nonempty-dir.cmake"
            COMMENT "Baking Wii prebaked font atlases (freetype-py)"
            VERBATIM
        )
        add_dependencies(fn_wii_fonts fn_asset_staging)
        add_dependencies(fruit-ninja fn_wii_fonts)
    endif()

    # --- Homebrew Channel icon (generated at build time, never committed) ---
    # tools/wii/hbc/icon.png is composed from copyrighted Halfbrick artwork in
    # the game dump (see tools/wii/hbc/make-icon.py), so it is gitignored and
    # must not be committed. Generate it into the build tree instead and install
    # from there -- same fix shape as the bake plan moving out of tmp/prebake.
    #
    #   FN_WII_ICON_PYTHON -- needs Pillow. Unlike the font baker it does NOT
    #                         need a ctypes-loadable libfreetype, so any python3
    #                         with Pillow works; defaults to the discovered
    #                         python3. Override when Pillow lives in a venv
    #                         (CI does this).
    set(FN_WII_ICON_PYTHON "${Python3_EXECUTABLE}" CACHE FILEPATH
        "Python interpreter (with Pillow) used to compose the Wii HBC icon")

    set(_fn_wii_icon "${CMAKE_BINARY_DIR}/hbc/icon.png")
    # Graceful skip only when the dump is absent (nothing to compose from) --
    # a dump-less configure must still work. The package then ships without an
    # icon and the Homebrew Channel draws its generic entry.
    if (NOT EXISTS "${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data/textures/gb_game.tex")
        set(FN_WII_HAVE_ICON OFF)
        message(STATUS
            "Wii: HBC icon SKIPPED -- FruitNinjaBada/Data is not present. "
            "The package ships without apps/fruitninja/icon.png.")
    else()
        execute_process(
            COMMAND "${FN_WII_ICON_PYTHON}" -c "import PIL"
            RESULT_VARIABLE _fn_wii_icon_python_rc
            OUTPUT_QUIET ERROR_QUIET
        )
        if (NOT _fn_wii_icon_python_rc EQUAL 0)
            message(FATAL_ERROR
                "Wii: ${FN_WII_ICON_PYTHON} cannot import Pillow, so the Homebrew "
                "Channel icon cannot be composed (it is generated from the game "
                "dump, never committed).\n"
                "Install it (pip install pillow, or pacman -S "
                "mingw-w64-x86_64-python-pillow on MSYS2) or point "
                "-DFN_WII_ICON_PYTHON=<python> at an interpreter that has it.")
        endif()

        set(FN_WII_HAVE_ICON ON)
        message(STATUS "Wii: HBC icon generation enabled (${FN_WII_ICON_PYTHON})")
        add_custom_command(OUTPUT "${_fn_wii_icon}"
            COMMAND "${FN_WII_ICON_PYTHON}"
                    "${CMAKE_SOURCE_DIR}/tools/wii/hbc/make-icon.py"
                    --out "${_fn_wii_icon}"
            DEPENDS "${CMAKE_SOURCE_DIR}/tools/wii/hbc/make-icon.py"
                    "${CMAKE_SOURCE_DIR}/tools/lib/tex_decoder.py"
            COMMENT "Composing Wii HBC icon (Pillow)"
            VERBATIM
        )
        add_custom_target(fn_wii_icon DEPENDS "${_fn_wii_icon}")
        add_dependencies(fruit-ninja fn_wii_icon)
    endif()

    # --- Shippable homebrew package (install + CPack) ---
    # `cmake --install build/wii --prefix <dir>` assembles the standard HBC
    # apps/<name>/ layout; `cpack -G ZIP` from build/wii wraps that same layout
    # into fruit-ninja-wii.zip with apps/ at the archive root, ready to unzip
    # onto an SD/USB root. This replaces build.sh's old direct-to-AppData
    # mtools deploy as the build's output -- deploying to a Dolphin SD image is
    # now a separate, explicit opt-in step (tools/wii/deploy-dolphin.sh).
    install(FILES ${CMAKE_BINARY_DIR}/boot.dol
        DESTINATION apps/fruitninja)
    # Trailing slash on the source dir: copy CONTENTS of staging/Data into
    # apps/fruitninja/Data (not staging/Data itself nested one level deeper).
    # Includes fonts/prebaked when the bake above ran.
    install(DIRECTORY ${FN_STAGING_DATA_DIR}/
        DESTINATION apps/fruitninja/Data
        PATTERN "*.bak" EXCLUDE)   # don't ship editor/backup cruft from source Data
    install(FILES ${CMAKE_SOURCE_DIR}/tools/wii/hbc/meta.xml
        DESTINATION apps/fruitninja)
    # icon.png is generated (above), not committed -- install it from the build
    # tree, and only when it was actually generated.
    if (FN_WII_HAVE_ICON)
        install(FILES ${_fn_wii_icon}
            DESTINATION apps/fruitninja)
    endif()

    set(CPACK_GENERATOR "ZIP")
    set(CPACK_PACKAGE_FILE_NAME "fruit-ninja-wii")
    # "/" so the install() DESTINATION apps/fruitninja paths above land at the
    # archive root (apps/fruitninja/...), not nested under an extra
    # package-name/version prefix dir -- i.e. unzip-to-SD-root works directly.
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    # Drop the default "<pkgname>/" top-level wrapper dir the ZIP generator adds,
    # so the archive root is literally apps/fruitninja/... (unzip onto the SD/USB
    # root gives /apps/fruitninja directly, no intermediate folder to flatten).
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_PACKAGE_DIRECTORY ${CMAKE_BINARY_DIR})
    include(CPack)
else()
    # Native desktop/webOS entry point. FRUIT_PLATFORM_WEBOS reuses this
    # exact branch (it is neither EMSCRIPTEN nor FRUIT_PLATFORM_WII) --
    # mainSDL.cpp + fruit-ninja-game + SDL2::SDL2main, all guarded WIN32
    # blocks below are simply false when cross-compiling for webOS/ARM Linux.
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
    if (TARGET fn_prebaked_fonts)
        add_dependencies(fruit-ninja fn_prebaked_fonts)
    endif()
    fn_link_game_engine_group(fruit-ninja PRIVATE)
    target_link_libraries(fruit-ninja PRIVATE SDL2::SDL2main)

    # webOS TV: statically link libstdc++/libgcc so the shipped ELF embeds
    # the toolchain's newer GLIBCXX/CXXABI symbol versions instead of
    # depending on whatever (older) libstdc++ the device ships -- older
    # webOS TVs' system libstdc++ predates symbols this build emits.
    # Host/web/Wii are unaffected (this block is webOS-only).
    if (FRUIT_PLATFORM_WEBOS)
        target_link_options(fruit-ninja PRIVATE -static-libstdc++ -static-libgcc)
        # LicensingWebOS.cpp resolves SDL_OpenURL at runtime via dlsym (keeps the
        # symbol out of the import table so it loads on old TVs that lack it).
        target_link_libraries(fruit-ninja PRIVATE ${CMAKE_DL_LIBS})

        # --- webOS .ipk app root (install + CPack) ---
        # Mirrors the Wii apps/fruitninja layout (see the FRUIT_PLATFORM_WII
        # install()/CPack block above): `cmake --install build/webos --prefix
        # <dir>` assembles a flat webOS app root -- binary + Data/ + appinfo.json
        # + icons all directly under <dir>, exactly what `ares-package <dir>`
        # expects (a webOS app root has no "apps/<name>/" nesting, unlike HBC).
        # tools/webos/build.sh drives install + ares-package; this CMake stage
        # stops at the assembled app root (no CPack "External" generator here --
        # ares-package isn't a CPack-integrated tool the way elf2dol/ZIP are for
        # Wii, so the packaging step lives in the build.sh wrapper instead).
        configure_file(${CMAKE_SOURCE_DIR}/deploy/webos/appinfo.json
            ${CMAKE_CURRENT_BINARY_DIR}/webos-appinfo/appinfo.json @ONLY)

        # COMPONENT fruitninja on every rule below: `cmake --install
        # --component fruitninja` then installs ONLY these files and skips the
        # default (uncomponented) install() rules that FetchContent deps
        # (tinyxml2, libwebp) register for themselves -- notably libwebp's,
        # which errors ("file INSTALL cannot find ...") because we only build
        # its decoder, not the full lib set its install() rule expects.
        install(TARGETS fruit-ninja RUNTIME DESTINATION . COMPONENT fruitninja)
        # Trailing slash: copy CONTENTS of staging/Data into <approot>/Data.
        install(DIRECTORY ${FN_STAGING_DATA_DIR}/
            DESTINATION Data
            COMPONENT fruitninja
            PATTERN "*.bak" EXCLUDE)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/webos-appinfo/appinfo.json
            DESTINATION . COMPONENT fruitninja)
        install(FILES
                ${CMAKE_SOURCE_DIR}/deploy/webos/icon.png
                ${CMAKE_SOURCE_DIR}/deploy/webos/icon_large.png
            DESTINATION . COMPONENT fruitninja)
    endif()

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

if (NOT EMSCRIPTEN AND NOT FRUIT_PLATFORM_WII AND NOT FRUIT_PLATFORM_WEBOS)
    # --- ctest ---
    # Wii/webOS: cross-compiled ARM/PPC binaries can't run on the host anyway,
    # and the test executables link SDL2_image/desktop GL (find_package(OpenGL
    # REQUIRED) in tests/CMakeLists.txt + tests/scenes/CMakeLists.txt) which
    # the arm-webos-linux-gnueabi buildroot sysroot doesn't provide (no GLX/X11).
    enable_testing()
    add_subdirectory(tests)
endif()
endif()  # NOT FN_ASM_VERIFY
