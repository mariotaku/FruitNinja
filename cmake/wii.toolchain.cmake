# devkitPPC / libogc2 toolchain file for the Wii port target.
#
# Thin wrapper over devkitPro's own CMake toolchain support, plus a
# Windows-host MSYS2 workaround (see below). Configure with:
#
#   cmake -B build/wii -G "Unix Makefiles" \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/wii.toolchain.cmake \
#         -DFRUIT_PLATFORM_WII=ON
#   cmake --build build/wii
#
# Requires $DEVKITPRO set (devkitPro installer / pacman:
# https://devkitpro.org/wiki/Getting_Started; `pacman -S wii-dev`).

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR
        "wii.toolchain.cmake: $DEVKITPRO is not set. Install devkitPro "
        "(https://devkitpro.org/wiki/Getting_Started) and the wii-dev "
        "package (devkitPPC + libogc2) via its package manager, then "
        "re-run cmake.")
endif()

# devkitPro ships its own CMake toolchain file that sets up the
# powerpc-eabi-* compilers, CMAKE_SYSTEM_NAME/PROCESSOR, find_root_path,
# and the libogc2 include/lib dirs. Delegate to it instead of hand-rolling
# those bits (they're kept in sync with devkitPro releases upstream).
include("$ENV{DEVKITPRO}/cmake/Wii.cmake")

# --- Windows-host MSYS2 workaround (NOT fidelity-relevant) ---
#
# MSYS `make` mangles the TEMP/TMP env vars it hands to child processes, so
# the native (non-MSYS) devkitPPC gcc.exe falls back to an unwritable
# `C:\WINDOWS` for its temp files and fails to compile anything. Fix: wrap
# the compiler in a tiny shell script that re-exports a writable TEMP/TMP
# before exec'ing the real compiler, and point CMAKE_C/CXX_COMPILER at the
# wrapper instead of the raw exe. Linux/macOS hosts don't hit this and skip
# the block entirely.
if(CMAKE_HOST_WIN32)
    set(_fn_wii_tmp_dir "C:/msys64/tmp")
    file(MAKE_DIRECTORY "${_fn_wii_tmp_dir}")

    set(_fn_wii_real_gcc "$ENV{DEVKITPRO}/devkitPPC/bin/powerpc-eabi-gcc.exe")
    set(_fn_wii_real_gxx "$ENV{DEVKITPRO}/devkitPPC/bin/powerpc-eabi-g++.exe")

    set(_fn_wii_gcc_wrapper "${CMAKE_BINARY_DIR}/powerpc-eabi-gcc-wrap.sh")
    set(_fn_wii_gxx_wrapper "${CMAKE_BINARY_DIR}/powerpc-eabi-g++-wrap.sh")

    file(WRITE "${_fn_wii_gcc_wrapper}"
"#!/bin/sh
export TEMP='${_fn_wii_tmp_dir}'
export TMP='${_fn_wii_tmp_dir}'
exec \"${_fn_wii_real_gcc}\" \"$@\"
")
    file(WRITE "${_fn_wii_gxx_wrapper}"
"#!/bin/sh
export TEMP='${_fn_wii_tmp_dir}'
export TMP='${_fn_wii_tmp_dir}'
exec \"${_fn_wii_real_gxx}\" \"$@\"
")
    execute_process(COMMAND chmod +x "${_fn_wii_gcc_wrapper}" "${_fn_wii_gxx_wrapper}")

    set(CMAKE_C_COMPILER   "${_fn_wii_gcc_wrapper}" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${_fn_wii_gxx_wrapper}" CACHE FILEPATH "" FORCE)
endif()
