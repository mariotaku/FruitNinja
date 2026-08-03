# check_build_freshness.cmake -- refuse to let ctest report green against stale exes.
#
# WHY THIS EXISTS
#
# ctest does not build. If you build ONE target (a CLion run config, or
# `cmake --build . --target test_foo`) and then run the whole suite, every OTHER
# test executable is whatever was last linked -- possibly from before the change
# you are trying to verify. Those stale exes run old code and report PASSED.
#
# Not hypothetical. On 2026-08-03 a real SEGFAULT in slash_collision sat on main
# across four pushed commits while the suite reported 243/243, because a
# single-target build had left test_slash_collision.exe predating the change that
# broke it. The same trap had already cost a separate multi-round detour chasing
# a "particle regression" that was only 96 stale test exes.
#
# A green suite is worthless if the binaries under test are older than the code.
#
# WHAT IT CHECKS, AND WHY THIS RULE
#
# Each test executable must be at least as new as the project libraries it links
# (fruit-ninja-game, mortar_engine).
#
# Deliberately NOT "newer than the newest source": that flags an exe whose inputs
# genuinely did not change, so after any one-file edit + correct incremental
# build most exes look stale and the gate becomes noise people learn to ignore.
# Comparing against the LIBS is exact for the failure we care about -- if a lib
# was relinked and an executable was not, that executable cannot contain the new
# code. It is precisely the single-target-build signature.
#
#   cmake -DBUILD_DIR=<build> -DEXE_DIR=<bin> -P tests/check_build_freshness.cmake

if(NOT DEFINED BUILD_DIR OR NOT DEFINED EXE_DIR)
    message(FATAL_ERROR "check_build_freshness: BUILD_DIR and EXE_DIR are required")
endif()

# Project libraries only -- never third-party (_deps/vcpkg), whose timestamps
# move for unrelated reasons.
file(GLOB_RECURSE _libs "${BUILD_DIR}/*.lib" "${BUILD_DIR}/*.a")
set(_proj_libs "")
foreach(_l IN LISTS _libs)
    if(NOT "${_l}" MATCHES "(_deps|vcpkg|CMakeFiles)/")
        list(APPEND _proj_libs "${_l}")
    endif()
endforeach()

if(NOT _proj_libs)
    message(FATAL_ERROR
        "check_build_freshness: no project libraries found under ${BUILD_DIR}. "
        "Refusing to pass -- a check that cannot see the build is not a check.")
endif()

# Newest project library. IS_NEWER_THAN is also true for equal mtimes, so ties
# keep the incumbent; only a strict win moves it.
list(GET _proj_libs 0 _newest_lib)
foreach(_l IN LISTS _proj_libs)
    if("${_l}" IS_NEWER_THAN "${_newest_lib}")
        set(_newest_lib "${_l}")
    endif()
endforeach()

# The executables to check: exactly those that LINK the game library, emitted at
# configure time by tests/CMakeLists.txt. Pure-logic tests that only link
# mortar_engine are excluded on purpose -- they do not depend on this lib, so
# flagging them would be a false positive, and a gate that cries wolf gets
# ignored (an earlier draft of this file flagged 17 such tests).
set(_list "${EXE_DIR}/game_linked_tests.txt")
if(NOT EXISTS "${_list}")
    message(FATAL_ERROR
        "check_build_freshness: ${_list} missing. Re-run cmake to regenerate it. "
        "Refusing to pass -- a check with no inputs is not a check.")
endif()
file(STRINGS "${_list}" _names)

set(_exes "")
foreach(_n IN LISTS _names)
    if(_n STREQUAL "")
        continue()
    endif()
    if(EXISTS "${EXE_DIR}/${_n}.exe")
        list(APPEND _exes "${EXE_DIR}/${_n}.exe")
    elseif(EXISTS "${EXE_DIR}/${_n}")
        list(APPEND _exes "${EXE_DIR}/${_n}")
    endif()
endforeach()

list(LENGTH _exes _n_exes)
if(_n_exes EQUAL 0)
    message(FATAL_ERROR
        "check_build_freshness: none of the game-linked test executables exist in "
        "${EXE_DIR}. Build the suite first.")
endif()

set(_stale "")
foreach(_e IN LISTS _exes)
    # Stale only when the library is STRICTLY newer. IS_NEWER_THAN reports true
    # in both directions for equal mtimes, so requiring the reverse to be false
    # collapses that ambiguity instead of firing on every tie.
    if("${_newest_lib}" IS_NEWER_THAN "${_e}" AND NOT "${_e}" IS_NEWER_THAN "${_newest_lib}")
        get_filename_component(_n "${_e}" NAME)
        list(APPEND _stale "${_n}")
    endif()
endforeach()

if(_stale)
    list(LENGTH _stale _n_stale)
    # Keep the message readable when most of the suite is stale.
    set(_show "${_stale}")
    if(_n_stale GREATER 12)
        list(SUBLIST _stale 0 12 _show)
        list(APPEND _show "... and ${_n_stale} total")
    endif()
    string(REPLACE ";" "\n    " _show_pretty "${_show}")
    get_filename_component(_lib_name "${_newest_lib}" NAME)
    message(FATAL_ERROR
        "STALE TEST BINARIES -- this suite run would be meaningless.\n"
        "\n"
        "  ${_n_stale} of ${_n_exes} test executables are older than ${_lib_name},\n"
        "  so they cannot contain the code that library was just built from.\n"
        "  Any PASSED they report is about old code.\n"
        "\n"
        "  Stale:\n    ${_show_pretty}\n"
        "\n"
        "  Fix: build EVERYTHING before running the suite --\n"
        "      cmake --build <build-dir>\n"
        "  Building a single target (an IDE run config, or --target test_foo)\n"
        "  relinks that one executable and leaves the rest behind.")
endif()

message(STATUS "build_freshness: ${_n_exes} test executables are current with the project libraries.")
