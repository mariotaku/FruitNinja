// PathFunctions.cpp -- Mortar path-utility free functions.
// v1.6.1 TU: global.constructors.keyed.to.PathFunctions.cpp @ 0x00252458.
// Functions span 0x00251bf8..0x002524b7.
//
// IsThisFolderToken / IsParentFolderToken are declared in AsciiString.h
// (they live in this TU per the binary's symbol layout).

#include "util/PathFunctions.h"
#include "util/AsciiString.h"
#include <vector>
#include <cstring>

namespace Mortar {

// Binary: g_PathSeperators @ 0x0035cd98 (note binary typo: "Seperators")
// Two AsciiString elements; initialized by global ctor @ 0x00252458.
// Cross-build note: GCC 4.4.1 rejects aggregate-init of non-POD arrays.
// Use default-ctor array + init-struct pattern instead.
static AsciiString g_PathSeperators[2];

namespace {
    struct PathSeperatorsInit {
        PathSeperatorsInit() {
            g_PathSeperators[0].Set("\\");
            g_PathSeperators[1].Set("/");
        }
    } g_pathSeperatorsInit;
} // anonymous namespace

// IsThisFolderToken @0x00251d6c / IsParentFolderToken @0x00251d20 are already
// defined in AsciiString.cpp (declared in AsciiString.h, which is included above).
// In the v1.6.1 binary they live in this TU, but the port keeps the existing
// AsciiString.cpp definitions to avoid a duplicate-symbol clash.

// ASM-spec v1.6.1 Mortar::Tokenize @0x00251d9c
// Multi-separator scan: tries each sep at cursor, first match wins.
// Cross-build: plain index loops only (no range-for, no auto).
std::vector<AsciiString> Tokenize(const AsciiString& str,
                                   const AsciiString* separators,
                                   unsigned long sepCount,
                                   bool includeEmpty)
{
    std::vector<AsciiString> result;
    AsciiString cur;             // current accumulating token (empty on entry)
    unsigned long pos = 0;
    bool justFlushed = true;
    unsigned long len = str.Length();

    while (true) {
        if (pos >= len) {
            if (!cur.Empty() || (includeEmpty && justFlushed)) {
                result.push_back(cur);
            }
            return result;
        }

        bool matched = false;
        for (unsigned long i = 0; i < sepCount; i++) {
            unsigned long sepLen = separators[i].Length();
            if (sepLen == 0) continue;
            if (pos + sepLen > len) continue;
            const char* s   = str.c_str();
            const char* sep = separators[i].c_str();
            bool eq = true;
            for (unsigned long k = 0; k < sepLen; k++) {
                if (s[pos + k] != sep[k]) { eq = false; break; }
            }
            if (eq) {
                if (!cur.Empty() || includeEmpty) {
                    result.push_back(cur);
                    cur.Set("");
                    justFlushed = true;
                }
                pos += sepLen;
                matched = true;
                break;
            }
        }
        if (!matched) {
            cur.Append(str.c_str()[pos]);
            pos++;
            justFlushed = false;
        }
    }
}

// ASM-spec v1.6.1 Mortar::Tokenize @0x00251ff4
// Delegates to the array overload. Guards empty vector to avoid &v[0] UB.
std::vector<AsciiString> Tokenize(const AsciiString& str,
                                   const std::vector<AsciiString>& separators,
                                   bool includeEmpty)
{
    if (separators.empty()) {
        std::vector<AsciiString> result;
        return result;
    }
    return Tokenize(str, &separators[0], (unsigned long)separators.size(), includeEmpty);
}

// ASM-spec v1.6.1 Mortar::PathFromTokens @0x00251c70
// count == (unsigned long)-1 is the "all remaining" sentinel (passed by
// PathNormalize). PathGetParent passes size()-1 to strip the leaf token.
AsciiString PathFromTokens(const std::vector<AsciiString>& tokens,
                            unsigned long start,
                            unsigned long count)
{
    AsciiString result;
    unsigned long n = (unsigned long)tokens.size();
    if (start >= n || count == 0) return result;
    unsigned long remain = n - start;
    unsigned long actual = (count < remain) ? count : remain;
    unsigned long end = start + actual;
    for (unsigned long i = start; i < end; i++) {
        result.Append(tokens[i]);
        if (i + 1 < end) result.Append('/');
    }
    return result;
}

// ASM-spec v1.6.1 Mortar::PathGetParent @0x00251f70
// Returns parent directory, no trailing slash, '/' separator.
// "a/b/c.txt" -> "a/b";  "file" -> "";  "" -> "".
// FIX: supersedes the wrong backward-scan impl that was in ResourceLoader.cpp
// (that version returned a trailing slash; this is the faithful binary version).
AsciiString PathGetParent(const AsciiString& path)
{
    std::vector<AsciiString> toks = Tokenize(path, g_PathSeperators, 2, false);
    unsigned long n = (unsigned long)toks.size();
    if (n == 0) return AsciiString();
    return PathFromTokens(toks, 0, n - 1);
}

// ASM-spec v1.6.1 Mortar::PathNormalize @0x0025202c
// Tokenize -> drop "." -> collapse ".." (pop parent unless at ".." root)
// -> PathFromTokens all. Leading '/' is lost (separator tokenized away;
// this is binary behavior, not a bug).
AsciiString PathNormalize(const AsciiString& path)
{
    std::vector<AsciiString> raw = Tokenize(path, g_PathSeperators, 2, false);
    std::vector<AsciiString> filtered;
    filtered.reserve(raw.size());
    for (unsigned long i = 0; i < (unsigned long)raw.size(); i++) {
        const AsciiString& tok = raw[i];
        if (IsThisFolderToken(tok)) {
            // drop "."
        } else if (IsParentFolderToken(tok)) {
            if (filtered.empty() || IsParentFolderToken(filtered.back())) {
                filtered.push_back(tok);  // keep ".." at root or after another ".."
            } else {
                filtered.pop_back();      // consume previous component
            }
        } else {
            filtered.push_back(tok);
        }
    }
    return PathFromTokens(filtered, 0, (unsigned long)-1);
}

// ASM-spec v1.6.1 Mortar::PathConcatenate @0x002523e4
AsciiString PathConcatenate(const AsciiString& a, const AsciiString& b)
{
    AsciiString tmp(a);
    tmp.Append('/');
    tmp.Append(b);
    return PathNormalize(tmp);
}

// ASM-spec v1.6.1 Mortar::PathGetRelative @0x002521a4
// Both args by value (binary mangling: two AsciiString values, not refs).
// Semantics: relative path to navigate from `from` (current) to `to` (target).
// Uses CompareI for case-insensitive common-prefix detection (the port's
// EqualsI(char*,hash,len) signature differs from binary EqualsI; use CompareI==0).
AsciiString PathGetRelative(AsciiString from, AsciiString to)
{
    from = PathNormalize(from);
    to   = PathNormalize(to);

    std::vector<AsciiString> fromToks = Tokenize(from, g_PathSeperators, 2, false);
    std::vector<AsciiString> toToks   = Tokenize(to,   g_PathSeperators, 2, false);

    unsigned long fromN = (unsigned long)fromToks.size();
    unsigned long toN   = (unsigned long)toToks.size();
    unsigned long k     = (fromN < toN) ? fromN : toN;
    unsigned long common = 0;
    for (unsigned long i = 0; i < k; i++) {
        if (fromToks[i].CompareI(toToks[i]) != 0) break;
        common++;
    }

    AsciiString result;
    // Step 1: emit "../" for each non-shared component of `from` (current location).
    unsigned long fromExtra = fromN - common;
    for (unsigned long i = 0; i < fromExtra; i++) {
        AsciiString dotdot("..");
        result.Append(dotdot);
        result.Append('/');
    }
    // Step 2: append unique components of `to` (destination).
    for (unsigned long i = common; i < toN; i++) {
        result.Append(toToks[i]);
        if (i + 1 < toN) result.Append('/');
    }
    return result;
}

} // namespace Mortar
