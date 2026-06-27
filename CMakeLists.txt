cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0079 NEW)
# CMP0135 (CMake 3.24+): extracted file timestamps are the extract-time,
# not the archive's archived mtimes. Explicitly opt in so re-downloads
# (e.g. when the URL_HASH changes) trigger a rebuild reliably.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
option(FN_ASM_VERIFY "Cross-compile for asm-verify (ARM Thumb-2, object files only)" OFF)
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

# --- GL backend selection ---
#
# The port targets the same fixed-function pipeline the binary uses
# (ES 1.x): glMatrixMode / glVertexPointer / glMaterialfv / ... —
# a near 1:1 translation of the binary's PassBinding::Apply.
# ES 2.0 shader translation was removed to keep fidelity and make it
# easy to rule out shader-side deviations when comparing behaviour.
#
# FRUIT_GL_API selects which OS header to include:
#   GL_COMPAT  - Desktop GL compatibility profile (<GL/gl.h>).
#                Windows/macOS/Linux dev; Mesa llvmpipe for swrast.
#   ES1        - OpenGL ES 1.1 (<GLES/gl.h>) for webOS / embedded.
if (EMSCRIPTEN)
    # Port specific: Emscripten uses LEGACY_GL_EMULATION (fixed-function over WebGL).
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
    # Port specific: LEGACY_GL_EMULATION injects FFP shim; no separate define needed
    # beyond __EMSCRIPTEN__ which emcc sets automatically.
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
# (Data/fontstruetype/arabic.ttf, gangofchinese.ttf).
# The existing .fnt bitmap path is unchanged; FreeType is additive.
if (EMSCRIPTEN)
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

if (EMSCRIPTEN)
    # Port specific: assets are preloaded into MEMFS at /FruitNinjaBada/Data.
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
    # FN_DATA_DIR for Emscripten: MEMFS mount path (absolute, POSIX-style).
    set(FN_DATA_DIR_PATH "/FruitNinjaBada/Data")
else()
    file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/FruitNinjaBada/Data" FN_DATA_DIR_PATH)
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
if (EMSCRIPTEN)
    # Port specific: Emscripten entry point uses emscripten_set_main_loop_arg
    # instead of the blocking Game::run() loop.
    add_executable(fruit-ninja src/mainEmscripten.cpp)
    target_link_libraries(fruit-ninja PRIVATE fruit-ninja-game SDL2::SDL2main)
    target_link_options(fruit-ninja PRIVATE
        "SHELL:-sUSE_SDL=2"
        "SHELL:-sUSE_FREETYPE=1"
        "SHELL:-sLEGACY_GL_EMULATION=1"
        "SHELL:-sGL_FFP_ONLY=1"
        "SHELL:-sALLOW_MEMORY_GROWTH=1"
        "SHELL:-sEXIT_RUNTIME=0"
        # Port specific: custom HTML shell (Phase 5+7: letterbox + loading bar
        # + TAP TO START overlay that doubles as the audio gesture unlock).
        "SHELL:--shell-file ${CMAKE_SOURCE_DIR}/src/platform/emscripten/shell.html"
        # Port specific: preload read-only assets into MEMFS; exclude the two
        # writable save files so they are NOT packed into the read-only bundle.
        # Saves are routed to /save (IDBFS) at runtime; see mainEmscripten.cpp.
        "SHELL:--preload-file ${CMAKE_SOURCE_DIR}/FruitNinjaBada/Data@/FruitNinjaBada/Data"
        "SHELL:--exclude-file *FruitySave.xml"
        "SHELL:--exclude-file *ItemSave.xml"
        # Port specific: link idbfs.js so FS.mount(IDBFS) is available.
        "SHELL:-lidbfs.js"
        # Port specific: export fn_idbfs_ready and fn_user_started so JS can
        # call them as Module._fn_idbfs_ready() / Module._fn_user_started().
        # EMSCRIPTEN_KEEPALIVE alone is sufficient for most linker configs but
        # an explicit -sEXPORTED_FUNCTIONS ensures they survive aggressive DCE.
        "SHELL:-sEXPORTED_FUNCTIONS=['_main','_fn_idbfs_ready','_fn_user_started']"
    )
    # Debug build (FN_DEBUG_TOUCH): keep C++ function names in the wasm name
    # section so browser crash stack traces name the actual C++ frames.
    # --profiling-funcs is pure metadata (no codegen/timing change), so it does
    # not perturb timing-sensitive intermittent crashes. (Deliberately NOT adding
    # -sASSERTIONS, which would change memory init/timing and could mask the repro.)
    if (FN_DEBUG_TOUCH)
        target_link_options(fruit-ninja PRIVATE "SHELL:--profiling-funcs")
    endif()
    # Port specific: copy the shell's splash image next to fruit-ninja.html
    # so the loading screen can reference it by relative URL before MEMFS is
    # populated.  splash.webp is the loading-screen backdrop (HB_logo.tex region).
    add_custom_command(TARGET fruit-ninja POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/src/platform/emscripten/splash.webp
            ${CMAKE_CURRENT_BINARY_DIR}/splash.webp
        COMMENT "Copying shell image assets (splash) to web build output"
        VERBATIM
    )
    # Port specific: content-hash web output files to defeat browser/mobile caching.
    # Renames fruit-ninja.{wasm,data,js} and splash.webp to name-<sha8>.ext,
    # rewrites all internal cross-references (wasm/data refs in js, js/splash refs
    # in html), and prunes stale hashed files from prior builds.
    # fruit-ninja.html stays unhashed (stable entry point).
    add_custom_command(TARGET fruit-ninja POST_BUILD
        COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/web/web-hash-assets.py
                        ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Content-hashing web output files (cache busting)"
        VERBATIM
    )
else()
    # Native desktop/webOS entry point.
    add_executable(fruit-ninja src/mainSDL.cpp)
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
