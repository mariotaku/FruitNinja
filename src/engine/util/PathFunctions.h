#ifndef FN_ENGINE_UTIL_PATH_FUNCTIONS_H
#define FN_ENGINE_UTIL_PATH_FUNCTIONS_H

// PathFunctions.h -- Mortar path-utility free functions.
//
// All functions live in namespace Mortar and match the binary's
// PathFunctions.cpp TU (global.constructors.keyed.to.PathFunctions.cpp
// @ 0x00252458, v1.6.1).
//
// Coordinate note: all output paths use '/' as separator regardless of
// input. Absolute paths lose their leading '/' through PathNormalize
// (binary behavior -- separator is tokenized away).
//
// Typical call chain:
//   PathGetParent("Data/Models/apple.mad")  -> "Data/Models"
//   PathConcatenate("Data/Models","../tex") -> "Data/tex"
//   PathNormalize("a/./b/../c")             -> "a/c"
//   PathGetRelative("a/b/c","a/x/y")        -> "../../x/y"

#include "util/AsciiString.h"
#include <vector>

namespace Mortar {

// ASM-spec v1.6.1 Mortar::Tokenize @0x00251d9c
// Splits str by any separator in separators[0..sepCount-1] (first match wins).
// includeEmpty=false: empty tokens between adjacent separators are dropped.
// includeEmpty=true:  empty tokens are retained (including leading/trailing).
// All path-internal callers pass includeEmpty=false.
std::vector<AsciiString> Tokenize(const AsciiString& str,
                                   const AsciiString* separators,
                                   unsigned long sepCount,
                                   bool includeEmpty);

// ASM-spec v1.6.1 Mortar::Tokenize @0x00251ff4
// Vector-overload: delegates to the array overload above.
std::vector<AsciiString> Tokenize(const AsciiString& str,
                                   const std::vector<AsciiString>& separators,
                                   bool includeEmpty);

// ASM-spec v1.6.1 Mortar::PathFromTokens @0x00251c70
// Joins tokens[start .. start+min(count, n-start)) with '/'.
// count == (unsigned long)-1 (UINT_MAX equivalent) means "all remaining".
// Returns empty string when start >= tokens.size() or count == 0.
AsciiString PathFromTokens(const std::vector<AsciiString>& tokens,
                            unsigned long start,
                            unsigned long count);

// ASM-spec v1.6.1 Mortar::PathGetParent @0x00251f70
// Returns the parent directory component (no trailing slash).
// "a/b/file.txt" -> "a/b";  "file.txt" -> "";  "" -> "".
AsciiString PathGetParent(const AsciiString& path);

// ASM-spec v1.6.1 Mortar::PathNormalize @0x0025202c
// Tokenizes by '/' and '\', drops "." components, collapses ".." by
// popping the previous component (unless already at ".." root).
// Does NOT preserve a leading '/' for absolute paths (binary behavior).
// Output always uses '/' as separator.
AsciiString PathNormalize(const AsciiString& path);

// ASM-spec v1.6.1 Mortar::PathConcatenate @0x002523e4
// Equivalent to PathNormalize(a + '/' + b).
// Handles empty a/b and ".." traversal cleanly via normalize.
AsciiString PathConcatenate(const AsciiString& a, const AsciiString& b);

// ASM-spec v1.6.1 Mortar::PathGetRelative @0x002521a4
// Both arguments are by value (binary signature -- copies normalized in-place).
// Returns the relative path to navigate from `from` (current) to `to` (target).
// Uses case-insensitive comparison (CompareI) for common-prefix detection.
// Example: PathGetRelative("a/b/c","a/x/y") -> "../../x/y".
AsciiString PathGetRelative(AsciiString from, AsciiString to);

} // namespace Mortar

#endif // FN_ENGINE_UTIL_PATH_FUNCTIONS_H
