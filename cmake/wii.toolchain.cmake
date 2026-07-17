# devkitPPC / libogc2 toolchain file for the Wii port target.
#
# SCAFFOLDING -- not exercised by any build in this repo/session (no
# devkitPPC install here). Provided so a machine that HAS devkitPro/devkitPPC
# installed can configure the Wii target:
#
#   cmake -DCMAKE_TOOLCHAIN_FILE=cmake/wii.toolchain.cmake \
#         -DFRUIT_PLATFORM_WII=ON -B build/wii
#   cmake --build build/wii
#
# This file is NEVER included by the default host/web configure -- CMake
# only reads a toolchain file when -DCMAKE_TOOLCHAIN_FILE explicitly points
# at it (or a preset does), so its mere presence in the repo has zero effect
# on any other build.
#
# Requires $DEVKITPRO and $DEVKITPPC environment variables, set by the
# devkitPro installer / pacman (https://devkitpro.org/wiki/Getting_Started).

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR
        "wii.toolchain.cmake: $DEVKITPRO is not set. Install devkitPro "
        "(https://devkitpro.org/wiki/Getting_Started) and devkitPPC + libogc2 "
        "via its package manager, then re-run cmake.")
endif()
if(NOT DEFINED ENV{DEVKITPPC})
    message(FATAL_ERROR
        "wii.toolchain.cmake: $DEVKITPPC is not set. Install the devkitPPC "
        "package via devkitPro's pacman (pacman -S wii-dev) and re-run cmake.")
endif()

set(DEVKITPRO $ENV{DEVKITPRO})
set(DEVKITPPC $ENV{DEVKITPPC})

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR ppc)

# TODO(wii): point at the real devkitPPC cross-compilers once verified on a
# machine with the toolchain installed. Expected layout:
#   ${DEVKITPPC}/bin/powerpc-eabi-gcc / powerpc-eabi-g++ / powerpc-eabi-ar
set(CMAKE_C_COMPILER   "${DEVKITPPC}/bin/powerpc-eabi-gcc")
set(CMAKE_CXX_COMPILER "${DEVKITPPC}/bin/powerpc-eabi-g++")
set(CMAKE_AR           "${DEVKITPPC}/bin/powerpc-eabi-ar" CACHE FILEPATH "")

# TODO(wii): confirm exact Wii CPU flags. Broadwell/libogc2 samples
# conventionally use: -mcpu=750 -meabi -mhard-float -DGEKKO
set(CMAKE_C_FLAGS_INIT   "-mcpu=750 -meabi -mhard-float -DGEKKO")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=750 -meabi -mhard-float -DGEKKO")

# libogc2 install location (portlibs + libogc headers/libs).
set(FN_LIBOGC2_DIR "${DEVKITPRO}/libogc2" CACHE PATH "libogc2 install root")
set(FN_PORTLIBS_DIR "${DEVKITPRO}/portlibs/wii" CACHE PATH "devkitPro portlibs (wii)")

include_directories(SYSTEM
    "${FN_LIBOGC2_DIR}/include"
    "${FN_PORTLIBS_DIR}/include"
)
link_directories(
    "${FN_LIBOGC2_DIR}/lib/wii"
    "${FN_PORTLIBS_DIR}/lib"
)

set(CMAKE_FIND_ROOT_PATH "${DEVKITPPC}" "${FN_LIBOGC2_DIR}" "${FN_PORTLIBS_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# TODO(wii): elf2dol step (devkitPPC ships elf2dol) to produce fruit-ninja.dol
# from the linked .elf -- add as a POST_BUILD custom command once the
# executable target actually links.
