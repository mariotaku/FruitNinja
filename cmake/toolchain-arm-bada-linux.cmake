# Cross-compile toolchain for ARM Thumb-2 / VFPv3 hard-float, run entirely
# inside Linux (WSL Debian). Uses the Sourcery G++ Lite 2010q1-188 i386
# toolchain at ~/toolchain/sourcery-2010q1 -- ground-truth GCC 4.4.1, the
# upstream of Samsung's "Sourcery G++ 4.4-157" fork.
#
# Use:
#   wsl.exe -d Debian -- bash -c '
#     cd ~/fn-src && \
#     cmake -S cross-build -B ~/fn-build -G "Unix Makefiles" \
#           -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm-bada-linux.cmake \
#           -DCMAKE_BUILD_TYPE=Release && \
#     cmake --build ~/fn-build -j
#   '

set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  arm)

set(_TC "$ENV{HOME}/toolchain/sourcery-2010q1")
if(NOT EXISTS "${_TC}/bin/arm-none-eabi-gcc")
    message(FATAL_ERROR "Sourcery 2010q1 toolchain missing at ${_TC}. Re-clone from "
        "https://github.com/khadas/buildroot_toolchain_gcc_linux-x86_arm_Sourcery_Gpp_Lite-2010q1.git")
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

# ABI / arch flags must match the binary's .ARM.attributes:
#   Tag_CPU_name:        "CORTEX-A8"
#   Tag_CPU_arch:         v7
#   Tag_THUMB_ISA_use:    Thumb-2
#   Tag_FP_arch:          VFPv3
#   Tag_ABI_HardFP_use:   SP and DP
#   Tag_ABI_VFP_args:     VFP registers
set(_BADA_FLAGS "-mthumb -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3")

# -include cross-headers/fn-cxx11-shims.h: maps post-4.5 keywords (noexcept,
# override, final, nullptr) to era-correct equivalents so GCC 4.4.1 can parse.
# -fpermissive: demotes residual libstdc++ glitches from error to warning.
set(_PROJECT_ROOT  ${CMAKE_CURRENT_LIST_DIR}/..)
set(_BADA_CXX_FLAGS
    "${_BADA_FLAGS} -std=gnu++0x -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables -fpermissive -include ${_PROJECT_ROOT}/cross-headers/fn-cxx11-shims.h")

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
