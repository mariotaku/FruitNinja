// fn_png_impl.cpp -- provides the FN_PNG_WRITE_IMPLEMENTATION for all game tests.
// Exactly one TU in the test build defines FN_PNG_WRITE_IMPLEMENTATION so that
// fn_png_write.h's encoder is linked in without multiple-definition errors.
// All test executables link against the fn_png_impl OBJECT library via
// fn_add_game_test in tests/CMakeLists.txt.
#define FN_PNG_WRITE_IMPLEMENTATION
#include "third_party/fn_png_write.h"
