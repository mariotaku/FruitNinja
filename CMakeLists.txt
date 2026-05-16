cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0079 NEW)
# CMP0135 (CMake 3.24+): extracted file timestamps are the extract-time,
# not the archive's archived mtimes. Explicitly opt in so re-downloads
# (e.g. when the URL_HASH changes) trigger a rebuild reliably.
if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
project(FruitNinja LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
#   cmake -G "MSYS Makefiles" -B build-asan -DENABLE_ASAN=ON \
#         -DCMAKE_C_COMPILER=/clang64/bin/clang.exe \
#         -DCMAKE_CXX_COMPILER=/clang64/bin/clang++.exe
#   cmake --build build-asan -j$(nproc)
#
# Run (the asan DLL must be on PATH — clang64/bin already has it
# if you launch from the MSYS2 CLANG64 shell, otherwise prefix:):
#   PATH="/clang64/bin:$PATH" ./build-asan/fruit-ninja.exe
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
set(FRUIT_GL_API "GL_COMPAT" CACHE STRING "GL backend: GL_COMPAT or ES1")
set_property(CACHE FRUIT_GL_API PROPERTY STRINGS GL_COMPAT ES1)
message(STATUS "FRUIT_GL_API = ${FRUIT_GL_API}")

if (FRUIT_GL_API STREQUAL "GL_COMPAT")
    add_compile_definitions(FRUIT_GL_API_GL_COMPAT=1)
elseif (FRUIT_GL_API STREQUAL "ES1")
    add_compile_definitions(FRUIT_GL_API_ES1=1)
else()
    message(FATAL_ERROR "Unknown FRUIT_GL_API '${FRUIT_GL_API}'")
endif()

# --- Dependencies ---

# Prefer system SDL2 (MSYS2 / Linux package, etc.); fall back to a fetch
# only when not installed:
#   - On MSVC: download libsdl-org's prebuilt VC binary (SDL2-devel-*-VC.zip)
#     -- ~5 sec download vs ~2 min source build, no extra dependencies.
#   - Elsewhere (or as a safety net on MSVC if the prebuilt download fails):
#     fall through to the source-build path.
include(FetchContent)
set(_SDL2_VER 2.30.11)

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

FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG        10.0.0
)
FetchContent_MakeAvailable(tinyxml2)

# --- Game library (everything except mainSDL.cpp) ---
# Collected as a STATIC lib so both the fruit-ninja executable and the
# tests/test_screen smoke test can link against the same compiled units.
# Subsystem CMakeLists.txt files contribute their sources via
# target_sources(fruit-ninja-game PRIVATE ...).

add_library(fruit-ninja-game STATIC src/Game.cpp src/GameSDL.cpp)
target_include_directories(fruit-ninja-game PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)

file(TO_CMAKE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/FruitNinjaBada/Data" FN_DATA_DIR_PATH)
configure_file(src/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h @ONLY)

# --- Subsystems ---
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

# --- Executable (just mainSDL.cpp; everything else lives in fruit-ninja-game) ---
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

option(FRUITNINJA_BOMB_TRACE "Enable per-frame Bomb::Update trace logging" OFF)
if (FRUITNINJA_BOMB_TRACE)
    target_compile_definitions(fruit-ninja-game PRIVATE MORTAR_BOMB_TRACE=1)
endif()

# --- ctest ---
enable_testing()
add_subdirectory(tests)
