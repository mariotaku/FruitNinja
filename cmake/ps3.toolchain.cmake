# PSL1GHT / ps3toolchain file for the PS3 port target.
#
# Configure with:
#
#   cmake -B build/ps3 -G "Unix Makefiles" \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/ps3.toolchain.cmake \
#         -DFRUIT_PLATFORM_PS3=ON
#   cmake --build build/ps3
#
# Requires $PS3DEV and $PSL1GHT set (ps3toolchain + PSL1GHT):
#   https://github.com/ps3dev/ps3toolchain
#   https://github.com/ps3dev/PSL1GHT
#
# Typical install:
#   export PS3DEV=/usr/local/ps3dev
#   export PSL1GHT=$PS3DEV/psl1ght
#   export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH

if(NOT DEFINED ENV{PS3DEV})
    message(FATAL_ERROR
        "ps3.toolchain.cmake: $PS3DEV is not set. Install ps3toolchain "
        "(https://github.com/ps3dev/ps3toolchain) and set PS3DEV to its prefix.")
endif()

if(NOT DEFINED ENV{PSL1GHT})
    message(FATAL_ERROR
        "ps3.toolchain.cmake: $PSL1GHT is not set. Build/install PSL1GHT "
        "(https://github.com/ps3dev/PSL1GHT) and set PSL1GHT to its install dir.")
endif()

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc64)

set(PS3DEV "$ENV{PS3DEV}")
set(PSL1GHT "$ENV{PSL1GHT}")

set(CMAKE_C_COMPILER   "${PS3DEV}/ppu/bin/ppu-gcc" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${PS3DEV}/ppu/bin/ppu-g++" CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${PS3DEV}/ppu/bin/ppu-gcc" CACHE FILEPATH "" FORCE)
set(CMAKE_AR           "${PS3DEV}/ppu/bin/ppu-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB       "${PS3DEV}/ppu/bin/ppu-ranlib" CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP        "${PS3DEV}/ppu/bin/ppu-strip" CACHE FILEPATH "" FORCE)

set(CMAKE_FIND_ROOT_PATH "${PS3DEV}/ppu" "${PSL1GHT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# PPU flags (Cell BE, hard-float, etc.)
set(PS3_MACHDEP "-mhard-float -fmodulo-sched -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_INIT   "${PS3_MACHDEP} -mcpu=cell")
set(CMAKE_CXX_FLAGS_INIT "${PS3_MACHDEP} -mcpu=cell -std=c++11")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${PS3_MACHDEP} -Wl,--gc-sections")

include_directories(SYSTEM
    "${PSL1GHT}/ppu/include"
    "${PS3DEV}/ppu/include"
)
link_directories(
    "${PSL1GHT}/ppu/lib"
    "${PS3DEV}/ppu/lib"
)

# Mark that we are cross-compiling for PS3 so the root CMakeLists can detect it.
set(FRUIT_PLATFORM_PS3 ON CACHE BOOL "Build the PS3 (PSL1GHT) platform target" FORCE)
