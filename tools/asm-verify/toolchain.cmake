# Cross-compile toolchain for ARM Thumb-2 / VFPv3 hard-float.
# Uses Samsung Sourcery G++ 4.4-157 (= GCC 4.4.1, the ACTUAL compiler
# that built FruitNinja.exe). The ghcr.io/mariotaku/bada-sdk:1.1.0 Docker
# image provides this toolchain at /opt/codesourcery/.

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
# Default mode is ARM: the binary is ~94% ARM ($a=7951) vs ~6% Thumb ($t=486),
# so -marm matches the original for the vast majority of functions. Switching the
# old -mthumb default to -marm lifted BinDiff near-identical matches 1144 -> 1395
# (encoding skew + predication were the dominant small-function noise source).
#
# No selective per-file -mthumb: (1) GCC 4.4.1 does NOT support per-function mode
# selection -- __attribute__((target("thumb"))) and "#pragma GCC target" both emit
# "target attribute is not supported on this machine" and have zero effect, so the
# only granularity is per-translation-unit; (2) the binary's Thumb code is almost
# entirely platform (*Bada, excluded), third-party libs (FreeType, libstdc++
# templates), and scattered template instantiations -- no cross-build .cpp is
# homogeneously Thumb, so flipping a whole file to -mthumb would regress its ARM
# functions to recover the odd Thumb one. The small Thumb residual is accepted.
#
# FN_ARM_MODE env override: the twin-build pipeline (bindiff-pipeline.sh) sets this
# to -mthumb to compile a second "Thumb twin" .so, so each function can be diffed
# in the binary's actual mode (mode-matched merge). Default stays -marm so a plain
# asm-verify run is unaffected.
if(DEFINED ENV{FN_ARM_MODE})
  set(_FN_ARM_MODE "$ENV{FN_ARM_MODE}")
else()
  set(_FN_ARM_MODE "-marm")
endif()
set(_BADA_FLAGS "${_FN_ARM_MODE} -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3 -fshort-enums -fshort-wchar -fpic")

# -include cross-headers/fn-cxx11-shims.h: maps post-4.5 keywords (noexcept,
# override, final, nullptr) to era-correct equivalents and forward-declares
# snprintf for Sourcery 4.4 newlib. -fpermissive: demote residual libstdc++
# glitches from error to warning.
set(_ASM_VERIFY_DIR ${CMAKE_CURRENT_LIST_DIR})
set(_BADA_CXX_FLAGS
    "${_BADA_FLAGS} -std=gnu++0x -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fpermissive -include ${_ASM_VERIFY_DIR}/cross-headers/fn-cxx11-shims.h -D__bada__")

# -D__bada__ matches arm-bada-eabi's `builtin_define_std("bada")` so port-side
# `#ifdef __bada__` binary-sizeof static_asserts fire here, enforcing layout
# fidelity. Port-only fields are excluded via `#if !defined(__bada__)` so they
# don't inflate cross-build sizeof past the binary value.

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
