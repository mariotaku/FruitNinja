// Forced-include: provides era-correct names for C++11 keywords that
// GCC 4.4/4.5 don't recognise but the port code uses.
//
// Used via -include cross-headers/fn-cxx11-shims.h in toolchain-arm-bada.cmake.
#ifndef FN_CXX11_SHIMS_H
#define FN_CXX11_SHIMS_H

// `noexcept` keyword: introduced in GCC 4.6. Map to C++03 throw() exception
// specification (close enough -- both promise no thrown exceptions).
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) < 406
#  define noexcept throw()
#endif

// `override` keyword: introduced in GCC 4.7. Map to nothing.
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) < 407
#  define override
#  define final
#endif

// `nullptr` literal: introduced in GCC 4.6. Map to GCC's `__null` builtin
// (typed null-pointer constant; better than plain `0` because it has correct
// pointer arithmetic / overload-resolution semantics).
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) < 406
#  define nullptr __null
#endif

#endif
