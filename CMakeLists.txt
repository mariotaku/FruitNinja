cmake_minimum_required(VERSION 3.13)
cmake_policy(SET CMP0079 NEW)
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

# Prefer system SDL2 (MSYS2 / Linux package, etc.) but fall back to source
# fetch for environments without a system install (most commonly MSVC + VS
# Build Tools). FetchContent compiles SDL2 once into the project tree.
include(FetchContent)
find_package(SDL2 QUIET)
if (NOT SDL2_FOUND)
    message(STATUS "System SDL2 not found -- fetching SDL2 2.30.11 source")
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.11
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

# --- Executable ---

add_executable(fruit-ninja src/main.cpp src/Game.cpp)
target_include_directories(fruit-ninja PRIVATE
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

# --- Link (after all subdirectories define their targets) ---
target_link_libraries(fruit-ninja PRIVATE SDL2::SDL2main SDL2::SDL2 tinyxml2::tinyxml2 mortar_engine)

# Copy SDL2.dll next to fruit-ninja.exe so the launcher finds it without
# having to add the FetchContent build dir to PATH. Only when SDL2 is
# built as a shared library inside our build (FetchContent path) -- on
# MSYS2 / Linux the system SDL2 is found via PATH/ldconfig and no copy
# is needed.
if (TARGET SDL2 AND WIN32)
    get_target_property(_sdl2_type SDL2 TYPE)
    if (_sdl2_type STREQUAL "SHARED_LIBRARY")
        add_custom_command(TARGET fruit-ninja POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:SDL2>
                $<TARGET_FILE_DIR:fruit-ninja>
            COMMENT "Copying SDL2.dll next to fruit-ninja.exe")
    endif()
endif()

# --- ctest ---
enable_testing()
add_subdirectory(tests)
