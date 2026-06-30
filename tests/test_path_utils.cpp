// test_path_utils.cpp -- Unit tests for Mortar path-utility free functions.
// Covers: Tokenize (array), PathFromTokens, PathGetParent, PathNormalize,
//         PathConcatenate, PathGetRelative.
// Pure in-process: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "util/PathFunctions.h"
#include "util/AsciiString.h"
#include <vector>
#include <cstdio>
#include <cstring>

using namespace Mortar;

static int g_failures = 0;

static void check(bool cond, const char* desc)
{
    if (!cond) {
        printf("FAIL: %s\n", desc);
        g_failures++;
    }
}

static void check_str(const AsciiString& got, const char* expected, const char* desc)
{
    const char* s = got.CStr();
    if (strcmp(s, expected) != 0) {
        printf("FAIL: %s -- expected '%s', got '%s'\n", desc, expected, s);
        g_failures++;
    }
}

static void test_tokenize()
{
    // Tokenize("a/b/c", {/, \}, false) -> ["a","b","c"]
    {
        AsciiString seps[2];
        seps[0].Set("/");
        seps[1].Set("\\");
        std::vector<AsciiString> toks = Tokenize(AsciiString("a/b/c"), seps, 2, false);
        check(toks.size() == 3, "Tokenize(a/b/c) size==3");
        if (toks.size() == 3) {
            check(strcmp(toks[0].CStr(), "a") == 0, "Tokenize tok[0]=='a'");
            check(strcmp(toks[1].CStr(), "b") == 0, "Tokenize tok[1]=='b'");
            check(strcmp(toks[2].CStr(), "c") == 0, "Tokenize tok[2]=='c'");
        }
    }

    // Backslash separator
    {
        AsciiString seps[2];
        seps[0].Set("\\");
        seps[1].Set("/");
        std::vector<AsciiString> toks = Tokenize(AsciiString("a\\b\\c"), seps, 2, false);
        check(toks.size() == 3, "Tokenize(a\\b\\c backslash) size==3");
    }

    // Mixed separators
    {
        AsciiString seps[2];
        seps[0].Set("/");
        seps[1].Set("\\");
        std::vector<AsciiString> toks = Tokenize(AsciiString("a/b\\c"), seps, 2, false);
        check(toks.size() == 3, "Tokenize(a/b\\c mixed) size==3");
    }

    // Empty string -> empty vector
    {
        AsciiString seps[2];
        seps[0].Set("/");
        seps[1].Set("\\");
        std::vector<AsciiString> toks = Tokenize(AsciiString(""), seps, 2, false);
        check(toks.size() == 0, "Tokenize('') empty");
    }

    // No separator -> single token
    {
        AsciiString seps[2];
        seps[0].Set("/");
        seps[1].Set("\\");
        std::vector<AsciiString> toks = Tokenize(AsciiString("file.txt"), seps, 2, false);
        check(toks.size() == 1, "Tokenize(file.txt) single token");
        if (toks.size() == 1)
            check(strcmp(toks[0].CStr(), "file.txt") == 0, "Tokenize(file.txt) tok[0]");
    }

    // includeEmpty=false skips empties between adjacent separators
    {
        AsciiString seps[2];
        seps[0].Set("/");
        seps[1].Set("\\");
        std::vector<AsciiString> toks = Tokenize(AsciiString("a//b"), seps, 2, false);
        check(toks.size() == 2, "Tokenize(a//b) include_empty=false skips middle empty");
    }
}

static void test_path_from_tokens()
{
    // PathFromTokens([a,b,c], 0, 2) -> "a/b"
    {
        std::vector<AsciiString> toks;
        toks.push_back(AsciiString("a"));
        toks.push_back(AsciiString("b"));
        toks.push_back(AsciiString("c"));
        check_str(PathFromTokens(toks, 0, 2), "a/b", "PathFromTokens([a,b,c],0,2)");
    }

    // PathFromTokens([a,b,c], 0, 3) -> "a/b/c"
    {
        std::vector<AsciiString> toks;
        toks.push_back(AsciiString("a"));
        toks.push_back(AsciiString("b"));
        toks.push_back(AsciiString("c"));
        check_str(PathFromTokens(toks, 0, 3), "a/b/c", "PathFromTokens([a,b,c],0,3)");
    }

    // PathFromTokens([a,b,c], 0, UINT_MAX) -> "a/b/c"
    {
        std::vector<AsciiString> toks;
        toks.push_back(AsciiString("a"));
        toks.push_back(AsciiString("b"));
        toks.push_back(AsciiString("c"));
        check_str(PathFromTokens(toks, 0, (unsigned long)-1), "a/b/c", "PathFromTokens([a,b,c],0,-1)");
    }

    // PathFromTokens([], 0, 0) -> ""
    {
        std::vector<AsciiString> empty;
        check_str(PathFromTokens(empty, 0, 0), "", "PathFromTokens(empty,0,0)");
    }

    // count=0 -> empty
    {
        std::vector<AsciiString> toks;
        toks.push_back(AsciiString("a"));
        check_str(PathFromTokens(toks, 0, 0), "", "PathFromTokens([a],0,0) count=0");
    }

    // start >= size -> empty
    {
        std::vector<AsciiString> toks;
        toks.push_back(AsciiString("a"));
        check_str(PathFromTokens(toks, 5, 1), "", "PathFromTokens([a],5,1) start oob");
    }
}

static void test_path_get_parent()
{
    // "a/b/file.txt" -> "a/b"
    check_str(PathGetParent(AsciiString("a/b/file.txt")), "a/b", "PathGetParent(a/b/file.txt)");

    // "file" -> ""
    check_str(PathGetParent(AsciiString("file")), "", "PathGetParent(file) no slash");

    // "" -> ""
    check_str(PathGetParent(AsciiString("")), "", "PathGetParent('') empty");

    // "a/" -> "a"  (trailing slash: parent of empty-named file in a/)
    // Actually Tokenize("a/", {/,\}, false) = ["a"] (empty trailing token skipped)
    // PathFromTokens(["a"], 0, 0) = "" (count=size-1=0 -> empty)
    check_str(PathGetParent(AsciiString("a/")), "", "PathGetParent(a/) trailing slash");

    // "a\\b\\file.txt" (backslash) -> "a/b"
    check_str(PathGetParent(AsciiString("a\\b\\file.txt")), "a/b", "PathGetParent(a\\b\\file.txt backslash)");

    // "a/b" -> "a"
    check_str(PathGetParent(AsciiString("a/b")), "a", "PathGetParent(a/b)");
}

static void test_path_normalize()
{
    // "a/./b/../c" -> "a/c"
    check_str(PathNormalize(AsciiString("a/./b/../c")), "a/c", "PathNormalize(a/./b/../c)");

    // "a/b/c" -> "a/b/c" (no change)
    check_str(PathNormalize(AsciiString("a/b/c")), "a/b/c", "PathNormalize(a/b/c) no change");

    // "a/." -> "a"
    check_str(PathNormalize(AsciiString("a/.")), "a", "PathNormalize(a/.)");

    // "a/b/.." -> "a"
    check_str(PathNormalize(AsciiString("a/b/..")), "a", "PathNormalize(a/b/..)");

    // "a/../.." -> ".." (second .. at root)
    check_str(PathNormalize(AsciiString("a/../..")), "..", "PathNormalize(a/../..)");

    // "" -> ""
    check_str(PathNormalize(AsciiString("")), "", "PathNormalize('') empty");
}

static void test_path_concatenate()
{
    // "a/b" + "../c" -> "a/c"
    check_str(PathConcatenate(AsciiString("a/b"), AsciiString("../c")), "a/c",
              "PathConcatenate(a/b,../c)");

    // "" + "file.txt" -> "file.txt"
    check_str(PathConcatenate(AsciiString(""), AsciiString("file.txt")), "file.txt",
              "PathConcatenate('',file.txt)");

    // "a/b" + "c/d" -> "a/b/c/d"
    check_str(PathConcatenate(AsciiString("a/b"), AsciiString("c/d")), "a/b/c/d",
              "PathConcatenate(a/b,c/d)");

    // "a/b" + "" -> "a/b"
    check_str(PathConcatenate(AsciiString("a/b"), AsciiString("")), "a/b",
              "PathConcatenate(a/b,'')");
}

static void test_path_get_relative()
{
    // PathGetRelative("a/b/c","a/x/y") -> "../../x/y"
    check_str(PathGetRelative(AsciiString("a/b/c"), AsciiString("a/x/y")), "../../x/y",
              "PathGetRelative(a/b/c,a/x/y)");

    // Same directory -> ""
    check_str(PathGetRelative(AsciiString("a/b"), AsciiString("a/b")), "",
              "PathGetRelative(same dir)");

    // Go deeper: PathGetRelative("a","a/b/c") -> "b/c"
    check_str(PathGetRelative(AsciiString("a"), AsciiString("a/b/c")), "b/c",
              "PathGetRelative(a,a/b/c) descend");

    // Go up: PathGetRelative("a/b/c","a") -> "../../" (binary appends "../" per ascent
    // level unconditionally; the trailing '/' is only absorbed when a descent token
    // follows, as in the "../../x/y" case above).
    check_str(PathGetRelative(AsciiString("a/b/c"), AsciiString("a")), "../../",
              "PathGetRelative(a/b/c,a) ascend");

    // Completely disjoint roots (no common prefix)
    // PathGetRelative("x/y","a/b") -> "../../a/b"
    check_str(PathGetRelative(AsciiString("x/y"), AsciiString("a/b")), "../../a/b",
              "PathGetRelative(x/y,a/b) disjoint");

    // Case-insensitive common prefix
    // PathGetRelative("A/B","a/c") -> "../c" (A==a, B!=c; common=1)
    check_str(PathGetRelative(AsciiString("A/B"), AsciiString("a/c")), "../c",
              "PathGetRelative(A/B,a/c) case-insensitive");
}

int main()
{
    test_tokenize();
    test_path_from_tokens();
    test_path_get_parent();
    test_path_normalize();
    test_path_concatenate();
    test_path_get_relative();

    if (g_failures == 0) {
        printf("All path_utils tests passed.\n");
        return 0;
    } else {
        printf("%d test(s) FAILED.\n", g_failures);
        return 1;
    }
}
