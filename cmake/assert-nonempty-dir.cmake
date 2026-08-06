# Script-mode helper: fail if a build step produced no files.
#
#   cmake -DFN_ASSERT_DIR=<dir> [-DFN_ASSERT_WHAT=<label>] \
#         -P cmake/assert-nonempty-dir.cmake
#
# Used as the last COMMAND of the Wii font bake: a silently-empty
# fonts/prebaked ships a package that renders no text at all (the Wii build
# links no runtime TTF backend), which is worse than a failed build.

if (NOT DEFINED FN_ASSERT_DIR)
    message(FATAL_ERROR "assert-nonempty-dir.cmake: -DFN_ASSERT_DIR=<dir> is required")
endif()
if (NOT DEFINED FN_ASSERT_WHAT)
    set(FN_ASSERT_WHAT "${FN_ASSERT_DIR}")
endif()

file(GLOB_RECURSE _fn_assert_files LIST_DIRECTORIES false "${FN_ASSERT_DIR}/*")
if (_fn_assert_files STREQUAL "")
    message(FATAL_ERROR
        "${FN_ASSERT_WHAT}: no files were produced in ${FN_ASSERT_DIR} -- "
        "refusing to ship. See the step's output above for the real error.")
endif()

list(LENGTH _fn_assert_files _fn_assert_count)
message(STATUS "${FN_ASSERT_WHAT}: ${_fn_assert_count} files in ${FN_ASSERT_DIR}")
