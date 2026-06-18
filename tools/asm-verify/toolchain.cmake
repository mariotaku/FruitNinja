# Cross-compile toolchain for ARM Thumb-2 / VFPv3 hard-float.
# Uses Samsung Sourcery G++ 4.4-157 (= GCC 4.4.1, the ACTUAL compiler
# that built FruitNinja.exe). The bada-sdk:1.0.0 Docker image provides
# this toolchain at /opt/codesourcery/.

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

set(_TC "/opt/codesourcery")

set(CMAKE_C_COMPILER    "${_TC}/bin/arm-samsung-nucleuseabi-gcc")
set(CMAKE_CXX_COMPILER  "${_TC}/bin/arm-samsung-nucleuseabi-g++")
set(CMAKE_AR            "${_TC}/bin/arm-samsung-nucleuseabi-ar"      CACHE FILEPATH "")
set(CMAKE_RANLIB        "${_TC}/bin/arm-samsung-nucleuseabi-ranlib"  CACHE FILEPATH "")
set(CMAKE_OBJCOPY       "${_TC}/bin/arm-samsung-nucleuseabi-objcopy" CACHE FILEPATH "")
set(CMAKE_OBJDUMP       "${_TC}/bin/arm-samsung-nucleuseabi-objdump" CACHE FILEPATH "")
set(CMAKE_NM            "${_TC}/bin/arm-samsung-nucleuseabi-nm"      CACHE FILEPATH "")
set(CMAKE_STRIP         "${_TC}/bin/arm-samsung-nucleuseabi-strip"   CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER  "${_TC}/bin/arm-samsung-nucleuseabi-as"      CACHE FILEPATH "")

set(CMAKE_C_COMPILER_WORKS    1)
set(CMAKE_CXX_COMPILER_WORKS  1)

# ABI / arch flags match the binary's .ARM.attributes:
#   Tag_CPU_name:        "CORTEX-A8"
#   Tag_CPU_arch:         v7
#   Tag_THUMB_ISA_use:    Thumb-2
#   Tag_FP_arch:          VFPv3
#   Tag_ABI_HardFP_use:   SP and DP
#   Tag_ABI_VFP_args:     VFP registers
#   Tag_ABI_enum_size:    small         -> -fshort-enums
#   Tag_ABI_PCS_wchar_t:  2             -> -fshort-wchar
#
# -fpic: binary is ELF DYN (shared object), produced with Position
# Independent Code. The GOT-relative addressing pattern `ldr r0, [r4, r3]`
# in the binary vs. the cross-build's `R_ARM_THM_MOVW_ABS` absolute
# relocations is the dominant source of asm-differ noise across nearly
# every Class::function pair. -fpic switches the cross-build to emit the
# same GOT-relative pattern, eliminating that whole diff cluster.
set(_BADA_FLAGS "-mthumb -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3 -fshort-enums -fshort-wchar -fpic")

# -include cross-headers/fn-cxx11-shims.h: maps post-4.5 keywords (noexcept,
# override, final, nullptr) to era-correct equivalents and forward-declares
# snprintf for Sourcery 4.4 newlib. -fpermissive: demote residual libstdc++
# glitches from error to warning.
set(_ASM_VERIFY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(_BADA_CXX_FLAGS
    "${_BADA_FLAGS} -std=gnu++0x -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fpermissive -include ${_ASM_VERIFY_DIR}/cross-headers/fn-cxx11-shims.h -D__bada__ -DFN_ASM_VERIFY_CROSS")

# -D__bada__ matches arm-bada-eabi's `builtin_define_std("bada")` so port-side
# `#ifdef __bada__` static_asserts on binary-faithful struct layouts fire here too.
#
# -DFN_ASM_VERIFY_CROSS marks "this is the asm-verify cross-build, NOT the
# real Bada toolchain". A small set of struct-layout asserts depends on
# stdlib ABI details (e.g. std::map's exact sizeof) where Sourcery 2010q1
# (the cross-build) and Bada's Sourcery 4.4-157 disagree — those asserts
# are skipped via `#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)`.
# Use sparingly; prefer the existing FIXME-and-comment pattern over a new
# guard if the divergence is port code, not stdlib ABI.

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
