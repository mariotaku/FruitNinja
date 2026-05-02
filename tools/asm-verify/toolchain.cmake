# Cross-compile toolchain for ARM Thumb-2 / VFPv3 hard-float, run inside
# Linux (WSL Debian). Uses Sourcery G++ Lite 2010q1-188 (= GCC 4.4.1, the
# upstream of Samsung's Sourcery G++ 4.4-157 that built FruitNinja.exe).
#
# The toolchain binaries are i386 ELF and can't stat() WSL drvfs (/c/...)
# files due to 32-bit inode overflow, so they MUST live on ext4 (e.g.
# ~/fnverify-toolchain/sourcery-2010q1/). Bootstrap with
# tools/asm-verify/setup.sh, which pulls the upstream tarball straight
# into ext4.
#
# Override the install path via FN_TOOLCHAIN_DIR if you keep the toolchain
# elsewhere -- e.g. a system-wide /opt/sourcery-2010q1/.

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

if(DEFINED ENV{FN_TOOLCHAIN_DIR})
    set(_TC "$ENV{FN_TOOLCHAIN_DIR}")
else()
    set(_TC "$ENV{HOME}/fnverify-toolchain/sourcery-2010q1")
endif()

if(NOT EXISTS "${_TC}/bin/arm-none-eabi-gcc")
    message(FATAL_ERROR "Sourcery 2010q1 toolchain missing at ${_TC}. "
        "Run tools/asm-verify/setup.sh once to bootstrap, or set "
        "FN_TOOLCHAIN_DIR to your existing install path.")
endif()

set(CMAKE_C_COMPILER    "${_TC}/bin/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER  "${_TC}/bin/arm-none-eabi-g++")
set(CMAKE_AR            "${_TC}/bin/arm-none-eabi-ar"      CACHE FILEPATH "")
set(CMAKE_RANLIB        "${_TC}/bin/arm-none-eabi-ranlib"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY       "${_TC}/bin/arm-none-eabi-objcopy" CACHE FILEPATH "")
set(CMAKE_OBJDUMP       "${_TC}/bin/arm-none-eabi-objdump" CACHE FILEPATH "")
set(CMAKE_NM            "${_TC}/bin/arm-none-eabi-nm"      CACHE FILEPATH "")
set(CMAKE_STRIP         "${_TC}/bin/arm-none-eabi-strip"   CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER  "${_TC}/bin/arm-none-eabi-as"      CACHE FILEPATH "")

set(CMAKE_C_COMPILER_WORKS    1)
set(CMAKE_CXX_COMPILER_WORKS  1)

# ABI / arch flags match the binary's .ARM.attributes:
#   Tag_CPU_name:        "CORTEX-A8"
#   Tag_CPU_arch:         v7
#   Tag_THUMB_ISA_use:    Thumb-2
#   Tag_FP_arch:          VFPv3
#   Tag_ABI_HardFP_use:   SP and DP
#   Tag_ABI_VFP_args:     VFP registers
set(_BADA_FLAGS "-mthumb -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3")

# -include cross-headers/fn-cxx11-shims.h: maps post-4.5 keywords (noexcept,
# override, final, nullptr) to era-correct equivalents and forward-declares
# snprintf for Sourcery 4.4 newlib. -fpermissive: demote residual libstdc++
# glitches from error to warning.
set(_ASM_VERIFY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(_BADA_CXX_FLAGS
    "${_BADA_FLAGS} -std=gnu++0x -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fpermissive -include ${_ASM_VERIFY_DIR}/cross-headers/fn-cxx11-shims.h")

set(CMAKE_C_FLAGS_INIT     "${_BADA_FLAGS} -O2 -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables")
set(CMAKE_CXX_FLAGS_INIT   "${_BADA_CXX_FLAGS}")

# Object files only -- no linking.
set(CMAKE_EXECUTABLE_SUFFIX           "")
set(CMAKE_C_LINK_EXECUTABLE           ":")
set(CMAKE_CXX_LINK_EXECUTABLE         ":")
set(CMAKE_C_CREATE_STATIC_LIBRARY     ":")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY   ":")
set(CMAKE_C_CREATE_SHARED_LIBRARY     ":")
set(CMAKE_CXX_CREATE_SHARED_LIBRARY   ":")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
