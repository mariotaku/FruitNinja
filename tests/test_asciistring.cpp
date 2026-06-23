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

    printf("test_asciistring: PASS\n");
    return 0;
}
