# Cross-compile toolchain for ARM Thumb-2 / VFPv3 hard-float, mirroring the
# original FruitNinjaBada binary's build attributes (see
# docs/engine/binary-build-evidence.md).
#
# Use:
#   cmake -S . -B build-bada-cross -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-bada.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#
# Phase A produces .o files only -- linking is intentionally not configured,
# since we don't need a runnable binary, only per-symbol assembly.

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

# Cross-compilers -- thin bash wrappers that invoke the bada SDK 4.5.3
# arm-bada-eabi toolchain (see tools/wsl-armgcc.sh for rationale on why
# we use 4.5.3 rather than 4.4.1 in Phase A).
set(_PROJECT_ROOT  ${CMAKE_CURRENT_LIST_DIR}/..)
set(CMAKE_C_COMPILER    "${_PROJECT_ROOT}/tools/wsl-armgcc.sh")
set(CMAKE_CXX_COMPILER  "${_PROJECT_ROOT}/tools/wsl-armgxx.sh")

# Use the bada SDK's archiver / ranlib / assembler too, otherwise CMake
# falls back to host MSVC ar which can't read ARM .o files.
set(_BADA_TC "${_PROJECT_ROOT}/bada_SDK/Tools/Toolchains/ARM/bin")
set(CMAKE_AR        "${_BADA_TC}/arm-bada-eabi-ar.exe"      CACHE FILEPATH "")
set(CMAKE_RANLIB    "${_BADA_TC}/arm-bada-eabi-ranlib.exe"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY   "${_BADA_TC}/arm-bada-eabi-objcopy.exe" CACHE FILEPATH "")
set(CMAKE_OBJDUMP   "${_BADA_TC}/arm-bada-eabi-objdump.exe" CACHE FILEPATH "")
set(CMAKE_NM        "${_BADA_TC}/arm-bada-eabi-nm.exe"      CACHE FILEPATH "")
set(CMAKE_STRIP     "${_BADA_TC}/arm-bada-eabi-strip.exe"   CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER "${_BADA_TC}/arm-bada-eabi-as.exe"   CACHE FILEPATH "")

# Skip the compiler test that would otherwise try to LINK a tiny program;
# we deliberately don't have a usable libc/libstdc++ on this side.
set(CMAKE_C_COMPILER_WORKS    1)
set(CMAKE_CXX_COMPILER_WORKS  1)

# ABI / arch flags must match the binary's .ARM.attributes:
#   Tag_CPU_name:        "CORTEX-A8"
#   Tag_CPU_arch:         v7
#   Tag_THUMB_ISA_use:    Thumb-2
#   Tag_FP_arch:          VFPv3
#   Tag_ABI_HardFP_use:   SP and DP
#   Tag_ABI_VFP_args:     VFP registers
set(_BADA_FLAGS "-mthumb -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3")

# Codegen / mangling flags for closer asm match against the binary:
#   -O2                            : binary almost certainly used -O2
#   -fno-exceptions / -fno-rtti    : matches typical Bada game build
#   -ffunction-sections            : per-symbol .o layout for asm-differ
#   -fno-asynchronous-unwind-tables: drop .ARM.exidx noise from per-symbol diffs
#   -fdata-sections                : same as -ffunction-sections for globals
# -include cross-headers/fn-cxx11-shims.h: maps post-4.5 keywords (noexcept,
# override, final) to era-correct equivalents so GCC 4.5.3 / 4.4.1 can parse.
# -fpermissive: demote libstdc++ 4.5.3 bug (move_iterator/uninitialized_copy
# rvalue-ref binding mismatch in vector::push_back) from error to warning.
set(_BADA_CXX_FLAGS
    "${_BADA_FLAGS} -std=gnu++0x -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fpermissive -include ${_PROJECT_ROOT}/cross-headers/fn-cxx11-shims.h")

set(CMAKE_C_FLAGS_INIT     "${_BADA_FLAGS} -O2 -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables")
set(CMAKE_CXX_FLAGS_INIT   "${_BADA_CXX_FLAGS}")

# Phase A: object files only. No final binary, no linking step.
set(CMAKE_EXECUTABLE_SUFFIX           "")
set(CMAKE_C_LINK_EXECUTABLE           ":")
set(CMAKE_CXX_LINK_EXECUTABLE         ":")
set(CMAKE_C_CREATE_STATIC_LIBRARY     ":")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY   ":")
set(CMAKE_C_CREATE_SHARED_LIBRARY     ":")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY   ":")

# Drop search of the host filesystem for libraries / packages.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
