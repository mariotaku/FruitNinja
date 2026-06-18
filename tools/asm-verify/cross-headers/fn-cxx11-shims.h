// Forced-include: provides era-correct names for C++11 keywords that
// GCC 4.4/4.5 don't recognise but the port code uses.
//
// Used via -include cross-headers/fn-cxx11-shims.h in toolchain-arm-bada.cmake.
#ifndef FN_CXX11_SHIMS_H
#define FN_CXX11_SHIMS_H

// `constexpr` keyword: GCC 4.4 gnu++0x does not support constexpr.
// Map to const; libstdc++ 4.4.1 does not itself use constexpr so
// the define is safe against system-header conflicts.
#if defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__) < 406
#  define constexpr const
#endif

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

// strncasecmp: in POSIX, lives in <strings.h>. Newlib's arm-none-eabi toolchain
// exposes it only when _BSD_SOURCE or _GNU_SOURCE is defined, which the
// cross-build does not set. Provide a portable shim so Font.cpp compiles.
#if defined(__GNUC__) && defined(__arm__)
#  include <string.h>
inline int strncasecmp(const char* s1, const char* s2, size_t n) {
    for (; n; --n, ++s1, ++s2) {
        unsigned char a = (unsigned char)*s1;
        unsigned char b = (unsigned char)*s2;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return (int)a - (int)b;
        if (!a)     return 0;
    }
    return 0;
}
#endif

// Sourcery 4.4 newlib's <cstdio> doesn't expose snprintf via `std::snprintf`
// (newlib's stdio.h guards under __STRICT_ANSI__). Forward-declare both
// global and std-namespaced forms so cross-build code can use either.
#ifdef __cplusplus
#include <stdarg.h>
#include <stddef.h>
extern "C" int snprintf(char* __restrict, size_t, const char* __restrict, ...);
extern "C" int vsnprintf(char* __restrict, size_t, const char* __restrict, va_list);
extern "C" char* strdup(const char*);
namespace std {
    using ::snprintf;
    using ::vsnprintf;
    using ::strdup;
}
#endif

#endif
