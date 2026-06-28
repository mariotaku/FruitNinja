// AsciiString unit test -- regression guard for the strlen+1 SSO bug.
//
// The binary stores m_size = strlen+1 (byte count including null terminator).
// The 32-byte inline buffer holds at most 31 chars + null terminator.
// A 32-char string (strlen==32) must therefore go to heap (m_size==33 > 32).
// Previously the port stored m_size = strlen and used threshold > 32, which
// caused strlen==32 to stay inline and write buf[32] past the end of the
// 32-byte inline buffer, corrupting m_hashCache at +0x24.
//
// Regression guard: construct strings of strlen 31 / 32 / 33, call Hash()
// (which triggers hashing and previously caused corruption), then assert
// c_str() still equals the original input and Length() == strlen.
//
// Pure in-process test: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "util/AsciiString.h"
#include "util/StringHash.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_STR(a, b) \
    do { \
        if (std::strcmp((a), (b)) != 0) { \
            std::printf("FAIL (%s:%d): \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
            ::exit(1); \
        } \
    } while(0)

static void test_empty()
{
    Mortar::AsciiString s;
    CHECK(s.Empty());
    CHECK(s.Length() == 0);
    CHECK_STR(s.c_str(), "");
    // Hash of empty string should be 0 (no content to hash).
    CHECK(s.Hash() == 0);
}

static void test_short()
{
    // strlen == 5, well within inline
    Mortar::AsciiString s("hello");
    CHECK(!s.Empty());
    CHECK(s.Length() == 5);
    CHECK_STR(s.c_str(), "hello");
    unsigned int h = s.Hash();
    CHECK(h != 0);
    // Hash must be stable after first call.
    CHECK(s.Hash() == h);
    // c_str must still be intact after Hash().
    CHECK_STR(s.c_str(), "hello");
}

static void test_strlen_31_inline()
{
    // 31 chars: max inline (strlen==31 -> m_size==32 <= 32 -> inline).
    // "1234567890123456789012345678901" -- exactly 31 chars
    const char* p31 = "1234567890123456789012345678901";
    CHECK(strlen(p31) == 31);

    Mortar::AsciiString s(p31);
    CHECK(!s.Empty());
    CHECK(s.Length() == 31);
    CHECK_STR(s.c_str(), p31);

    unsigned int h = s.Hash();
    CHECK(h != 0);
    // THE REGRESSION: c_str() must equal original after Hash().
    CHECK_STR(s.c_str(), p31);
    CHECK(s.Length() == 31);
}

static void test_strlen_32_heap()
{
    // 32 chars: must go to heap (strlen==32 -> m_size==33 > 32).
    // Historically this was the bug: inline path wrote buf[32] past the 32-byte
    // buffer, corrupting m_hashCache. Now it must use heap.
    const char* p32 = "12345678901234567890123456789012";
    CHECK(strlen(p32) == 32);

    Mortar::AsciiString s(p32);
    CHECK(!s.Empty());
    CHECK(s.Length() == 32);
    CHECK_STR(s.c_str(), p32);

    unsigned int h = s.Hash();
    CHECK(h != 0);
    // THE KEY REGRESSION GUARD: c_str() must still equal original after Hash().
    CHECK_STR(s.c_str(), p32);
    CHECK(s.Length() == 32);
}

static void test_strlen_33_heap()
{
    // 33 chars: comfortably heap.
    const char* p33 = "123456789012345678901234567890123";
    CHECK(strlen(p33) == 33);

    Mortar::AsciiString s(p33);
    CHECK(!s.Empty());
    CHECK(s.Length() == 33);
    CHECK_STR(s.c_str(), p33);

    unsigned int h = s.Hash();
    CHECK(h != 0);
    CHECK_STR(s.c_str(), p33);
    CHECK(s.Length() == 33);
}

static void test_mesh_path_32()
{
    // Exact reproduction of the bug report: "models/Fruit/apple_a_piece_2.mmd"
    // This path is 32 chars; must survive Hash() intact.
    const char* meshPath = "models/Fruit/apple_a_piece_2.mmd";
    CHECK(strlen(meshPath) == 32);

    Mortar::AsciiString s(meshPath);
    CHECK(s.Length() == 32);
    CHECK_STR(s.c_str(), meshPath);

    // Trigger hash (this was the crash path: corrupted buffer -> garbage c_str()).
    unsigned int h = s.Hash();
    CHECK(h != 0);
    CHECK_STR(s.c_str(), meshPath);
    CHECK(s.Length() == 32);
}

static void test_copy_ctor()
{
    const char* p32 = "12345678901234567890123456789012";
    Mortar::AsciiString a(p32);
    Mortar::AsciiString b(a);
    CHECK(b.Length() == 32);
    CHECK_STR(b.c_str(), p32);
    CHECK(b.Hash() == a.Hash());
}

static void test_assign()
{
    const char* p32 = "12345678901234567890123456789012";
    Mortar::AsciiString a;
    a = p32;
    CHECK(a.Length() == 32);
    CHECK_STR(a.c_str(), p32);

    // Assign shorter string -- transition heap -> inline.
    a = "hi";
    CHECK(a.Length() == 2);
    CHECK_STR(a.c_str(), "hi");

    // Assign 32-char back -- transition inline -> heap.
    a = p32;
    CHECK(a.Length() == 32);
    CHECK_STR(a.c_str(), p32);
}

static void test_compare_equal_lengths()
{
    // Two 32-char strings that differ in last char: lengths equal, hashes differ.
    const char* pa = "12345678901234567890123456789012";
    const char* pb = "12345678901234567890123456789013";
    Mortar::AsciiString a(pa);
    Mortar::AsciiString b(pb);
    CHECK(a.Compare(b) != 0);
    CHECK(!(a == b));
}

static void test_equals()
{
    const char* p32 = "12345678901234567890123456789012";
    Mortar::AsciiString a(p32);
    unsigned long len = (unsigned long)strlen(p32);
    unsigned int h = StringHash(p32);
    CHECK(a.Equals(p32, h, len));
    CHECK(!a.Equals("different", 0, 9));
}

static void test_append_across_boundary()
{
    // Append two short strings to cross from inline to heap.
    const char* p20 = "01234567890123456789";  // 20 chars
    Mortar::AsciiString a(p20);
    Mortar::AsciiString b(p20);
    a.Append(b);  // now 40 chars -> heap
    CHECK(a.Length() == 40);
    // First 20 chars are p20, next 20 are p20.
    const char* cs = a.c_str();
    CHECK(memcmp(cs, p20, 20) == 0);
    CHECK(memcmp(cs + 20, p20, 20) == 0);
    CHECK(cs[40] == '\0');
}

static void test_set_from_len()
{
    // Set(s, len) with a 32-char string.
    const char* p32 = "12345678901234567890123456789012";
    Mortar::AsciiString a;
    a.Set(p32, 32);
    CHECK(a.Length() == 32);
    CHECK_STR(a.c_str(), p32);
    unsigned int h = a.Hash();
    CHECK(h != 0);
    CHECK_STR(a.c_str(), p32);
}

// ============================================================================
// GROUP A free-function tests
// ============================================================================

static void test_make_case()
{
    char buf[16];
    // MakeLowerCase: ASCII A-Z only
    memcpy(buf, "Hello, WORLD!", 14);
    MakeLowerCase(buf);
    CHECK_STR(buf, "hello, world!");
    // MakeUpperCase: ASCII a-z only
    memcpy(buf, "Hello, world!", 14);
    MakeUpperCase(buf);
    CHECK_STR(buf, "HELLO, WORLD!");
    // NULL safety (must not crash)
    MakeLowerCase(NULL);
    MakeUpperCase(NULL);
    // Non-ASCII characters are left unchanged by Make* (not libc)
    buf[0] = (char)0xC0; buf[1] = '\0';
    char save = buf[0];
    MakeLowerCase(buf);
    CHECK(buf[0] == save);
}

static void test_string_to_case()
{
    char buf[16];
    // StringToLower: libc tolower
    memcpy(buf, "Hello World", 12);
    StringToLower(buf);
    CHECK_STR(buf, "hello world");
    // StringToUpper: libc toupper
    memcpy(buf, "Hello World", 12);
    StringToUpper(buf);
    CHECK_STR(buf, "HELLO WORLD");
    // NULL safety
    StringToLower(NULL);
    StringToUpper(NULL);
}

static void test_find_substring_degenerate()
{
    // Binary FindSubstring is always degenerate: always returns 0xFFFFFFFF.
    // This test LOCKS that behavior -- do NOT change it to strstr.
    CHECK(FindSubstring("hello world", "world") == 0xFFFFFFFFu);
    CHECK(FindSubstring("hello world", "hello") == 0xFFFFFFFFu);
    CHECK(FindSubstring("abc", "abc") == 0xFFFFFFFFu);
    CHECK(FindSubstring("", "x") == 0xFFFFFFFFu);
}

static void test_string_find_last_index()
{
    CHECK(StringFindLastIndex("hello", 'l') == 3);
    CHECK(StringFindLastIndex("hello", 'h') == 0);
    CHECK(StringFindLastIndex("hello", 'o') == 4);
    CHECK(StringFindLastIndex("hello", 'z') == -1);
    CHECK(StringFindLastIndex(NULL, 'a') == -1);
    // char == '\0' reports the terminator index
    CHECK(StringFindLastIndex("abc", '\0') == 3);
}

static void test_starts_with_word()
{
    CHECK(StartsWithWord("hello world", "hello") == true);
    CHECK(StartsWithWord("hello", "hello") == true);
    CHECK(StartsWithWord("hello", "helo") == false);
    CHECK(StartsWithWord("hi", "hello") == false);  // word longer than str
    CHECK(StartsWithWord(NULL, "hi") == false);
    CHECK(StartsWithWord("hi", NULL) == false);
    CHECK(StartsWithWord("", "") == true);
    // Case-sensitive
    CHECK(StartsWithWord("Hello", "hello") == false);
}

static void test_is_string_in_delimited_list()
{
    // Basic delimited list
    CHECK(IsStringInDelimitedList("alpha,beta,gamma", "beta", ',') == true);
    CHECK(IsStringInDelimitedList("alpha,beta,gamma", "alpha", ',') == true);
    CHECK(IsStringInDelimitedList("alpha,beta,gamma", "gamma", ',') == true);
    // Not in list
    CHECK(IsStringInDelimitedList("alpha,beta,gamma", "delta", ',') == false);
    // Partial match is not a whole-token match
    CHECK(IsStringInDelimitedList("alpha,beta,gamma", "alph", ',') == false);
    CHECK(IsStringInDelimitedList("alpha,beta,gamma", "bet", ',') == false);
    // Empty list
    CHECK(IsStringInDelimitedList("", "alpha", ',') == false);
    // Single element list
    CHECK(IsStringInDelimitedList("alpha", "alpha", ',') == true);
    CHECK(IsStringInDelimitedList("alpha", "beta", ',') == false);
}

static void test_parse_floats()
{
    float out[4] = {0,0,0,0};
    // Normal parse
    ParseFloats("1.0,2.0,3.0,4.0", out, 4);
    CHECK(out[0] == 1.0f);
    CHECK(out[1] == 2.0f);
    CHECK(out[2] == 3.0f);
    CHECK(out[3] == 4.0f);
    // Exhausted slots replicate last value
    ParseFloats("5.0,6.0", out, 4);
    CHECK(out[0] == 5.0f);
    CHECK(out[1] == 6.0f);
    CHECK(out[2] == 6.0f);  // replicated
    CHECK(out[3] == 6.0f);  // replicated
    // Single value -- all 4 slots get it
    ParseFloats("7.0", out, 4);
    CHECK(out[0] == 7.0f);
    CHECK(out[1] == 7.0f);
    CHECK(out[2] == 7.0f);
    CHECK(out[3] == 7.0f);
    // NULL / empty -- no crash, output unchanged
    float save0 = out[0];
    ParseFloats(NULL, out, 4);
    CHECK(out[0] == save0);
}

static void test_combine_file_paths()
{
    char buf[256];
    // Basic join
    CombineFilePaths("models", "Fruit/apple.mmd", buf, false);
    CHECK_STR(buf, "models/Fruit/apple.mmd");
    // Normalise backslash
    CombineFilePaths("models\\Fruit", "apple.mmd", buf, false);
    CHECK_STR(buf, "models/Fruit/apple.mmd");
    // Preserve .. segments
    CombineFilePaths("..", "foo", buf, false);
    CHECK_STR(buf, "../foo");
    // Preserve . segments
    CombineFilePaths(".", "foo", buf, false);
    CHECK_STR(buf, "./foo");
    // Empty first segment
    CombineFilePaths("", "foo/bar", buf, false);
    CHECK_STR(buf, "foo/bar");
    // Both paths non-trivial
    CombineFilePaths("data/textures", "ui/button.tex", buf, false);
    CHECK_STR(buf, "data/textures/ui/button.tex");
    // Leading separator stripped
    CombineFilePaths("/models", "apple.mmd", buf, false);
    CHECK_STR(buf, "models/apple.mmd");
}

static void test_wildcard_fit()
{
    // Plain exact match (case-insensitive)
    char w1[] = "hello", t1[] = "hello";
    CHECK(WildCardFit(w1, t1) == 1);
    char w2[] = "Hello", t2[] = "hello";
    CHECK(WildCardFit(w2, t2) == 1);
    // Plain no match
    char w3[] = "hello", t3[] = "world";
    CHECK(WildCardFit(w3, t3) == 0);
    // '*' matches anything
    char w4[] = "h*o", t4[] = "hello";
    CHECK(WildCardFit(w4, t4) == 1);
    char w5[] = "*", t5[] = "anything";
    CHECK(WildCardFit(w5, t5) == 1);
    char w6[] = "*", t6[] = "";
    CHECK(WildCardFit(w6, t6) == 1);
    // '?' matches exactly one char
    char w7[] = "h?llo", t7[] = "hello";
    CHECK(WildCardFit(w7, t7) == 1);
    char w8[] = "h?llo", t8[] = "hllo";
    CHECK(WildCardFit(w8, t8) == 0);
    // Trailing '*'
    char w9[] = "hel*", t9[] = "hello";
    CHECK(WildCardFit(w9, t9) == 1);
    char w10[] = "hel*", t10[] = "hel";
    CHECK(WildCardFit(w10, t10) == 1);
    char w11[] = "hel*", t11[] = "world";
    CHECK(WildCardFit(w11, t11) == 0);
    // No match when test is longer
    char w12[] = "hello", t12[] = "hellos";
    CHECK(WildCardFit(w12, t12) == 0);
    // [set] -- case-sensitive (binary behavior)
    char w13[] = "[hH]ello", t13[] = "hello";
    CHECK(WildCardFit(w13, t13) == 1);
    char w14[] = "[hH]ello", t14[] = "Hello";
    CHECK(WildCardFit(w14, t14) == 1);
    char w15[] = "[abc]ello", t15[] = "hello";
    CHECK(WildCardFit(w15, t15) == 0);
    // [!set] negated
    char w16[] = "[!abc]ello", t16[] = "hello";
    CHECK(WildCardFit(w16, t16) == 1);
    char w17[] = "[!hH]ello", t17[] = "hello";
    CHECK(WildCardFit(w17, t17) == 0);
}

int main()
{
    printf("test_asciistring: start\n");

    test_empty();
    printf("  empty: OK\n");

    test_short();
    printf("  short (strlen=5): OK\n");

    test_strlen_31_inline();
    printf("  strlen=31 (inline boundary): OK\n");

    test_strlen_32_heap();
    printf("  strlen=32 (heap; was bug): OK\n");

    test_strlen_33_heap();
    printf("  strlen=33 (heap): OK\n");

    test_mesh_path_32();
    printf("  mesh path strlen=32 (regression guard): OK\n");

    test_copy_ctor();
    printf("  copy ctor: OK\n");

    test_assign();
    printf("  assign: OK\n");

    test_compare_equal_lengths();
    printf("  compare equal lengths: OK\n");

    test_equals();
    printf("  Equals: OK\n");

    test_append_across_boundary();
    printf("  Append across inline->heap: OK\n");

    test_set_from_len();
    printf("  Set(s,len) strlen=32: OK\n");

    // GROUP A free-function tests
    test_make_case();
    printf("  MakeLowerCase/MakeUpperCase: OK\n");

    test_string_to_case();
    printf("  StringToLower/StringToUpper: OK\n");

    test_find_substring_degenerate();
    printf("  FindSubstring (degenerate 0xFFFFFFFF lock): OK\n");

    test_string_find_last_index();
    printf("  StringFindLastIndex: OK\n");

    test_starts_with_word();
    printf("  StartsWithWord: OK\n");

    test_is_string_in_delimited_list();
    printf("  IsStringInDelimitedList: OK\n");

    test_parse_floats();
    printf("  ParseFloats: OK\n");

    test_combine_file_paths();
    printf("  CombineFilePaths: OK\n");

    test_wildcard_fit();
    printf("  WildCardFit: OK\n");

    printf("test_asciistring: PASS\n");
    return 0;
}
