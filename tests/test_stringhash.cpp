// StringHash unit test -- regression guard for the Jenkins lookup3 case-folding hash.
//
// Verified test vectors are taken verbatim from the comment block in
// src/engine/util/StringHash.cpp (ASM-verified 2026-06-12 v1.6.1 binary
// @ 0x00252a10 / 0x0019c5d4).
//
// Pure in-process test: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "util/StringHash.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_EQ(a, b) \
    do { \
        uint32_t _a = (a); uint32_t _b = (b); \
        if (_a != _b) { \
            std::printf("FAIL (%s:%d): 0x%08x != 0x%08x  (%s)\n", \
                __FILE__, __LINE__, _a, _b, #a); \
            ::exit(1); \
        } \
    } while(0)

// Known-good vectors from StringHash.cpp comment block (ASM-verified binary constants).
static void test_known_vectors()
{
    // "watermelon" -> 0x158bc245
    CHECK_EQ(StringHash("watermelon"), 0x158bc245u);
    // "apple_red"  -> 0xdac1f38f
    CHECK_EQ(StringHash("apple_red"),  0xdac1f38fu);
    // "banana"     -> 0x5ff2eb92
    CHECK_EQ(StringHash("banana"),     0x5ff2eb92u);
}

// Case-insensitivity: the hash case-folds A-Z to a-z.
static void test_case_insensitive()
{
    CHECK_EQ(StringHash("APPLE"),      StringHash("apple"));
    CHECK_EQ(StringHash("APPLE_RED"),  StringHash("apple_red"));
    CHECK_EQ(StringHash("WATERMELON"), StringHash("watermelon"));
    CHECK_EQ(StringHash("BANANA"),     StringHash("banana"));
    // Mixed case.
    CHECK_EQ(StringHash("Apple"),      StringHash("apple"));
    CHECK_EQ(StringHash("WaterMelon"), StringHash("watermelon"));
}

// Short strings exercise the switch fall-through (remaining <= 11, no full-round loop).
static void test_short_strings()
{
    // strlen 1 -- case 1 only.
    uint32_t h_a = StringHash("a");
    CHECK(h_a != 0u);
    CHECK_EQ(StringHash("A"), h_a);

    // strlen 2 -- cases 2, 1.
    uint32_t h_ab = StringHash("ab");
    CHECK(h_ab != 0u);
    CHECK_EQ(StringHash("AB"), h_ab);

    // strlen 3 -- cases 3, 2, 1.
    uint32_t h_abc = StringHash("abc");
    CHECK(h_abc != 0u);
    CHECK_EQ(StringHash("ABC"), h_abc);

    // strlen 4 -- cases 4, 3, 2, 1.
    uint32_t h_abcd = StringHash("abcd");
    CHECK(h_abcd != 0u);
    CHECK_EQ(StringHash("ABCD"), h_abcd);

    // Stability: two calls with the same string return the same value.
    CHECK_EQ(StringHash("abc"), h_abc);
    CHECK_EQ(StringHash("ab"),  h_ab);
}

// Empty string: len == 0, only the final mix runs; result must be deterministic.
static void test_empty()
{
    uint32_t h1 = StringHash("",  0);
    uint32_t h2 = StringHash("",  0);
    uint32_t h3 = StringHash("");   // 1-arg overload

    CHECK_EQ(h1, h2);
    CHECK_EQ(h1, h3);
    std::printf("  empty string hash: 0x%08x\n", h1);
}

// Distinctness: strings that differ in one character must have different hashes.
static void test_distinctness()
{
    // "appl" vs "apple" -- differ in length and content.
    uint32_t h_appl  = StringHash("appl");
    uint32_t h_apple = StringHash("apple");
    CHECK(h_appl != h_apple);

    // "banana" vs "banane" -- last char differs.
    CHECK(StringHash("banana") != StringHash("banane"));

    // "apple_red" vs "apple_red2" -- suffix differs.
    CHECK(StringHash("apple_red") != StringHash("apple_red2"));
}

// Boundary between switch path and full-round loop: strings of length 11 and 12.
static void test_boundary_11_12()
{
    // strlen 11: exercises all switch cases 11..1 without entering the while loop.
    uint32_t h_11 = StringHash("abcdefghijk");  // exactly 11 chars
    CHECK(h_11 != 0u);
    CHECK_EQ(StringHash("ABCDEFGHIJK"), h_11);

    // strlen 12: one full mixing round, remaining == 0 after loop, switch is skipped.
    uint32_t h_12 = StringHash("abcdefghijkl");  // exactly 12 chars
    CHECK(h_12 != 0u);
    CHECK_EQ(StringHash("ABCDEFGHIJKL"), h_12);

    // The two must be distinct.
    CHECK(h_11 != h_12);
}

int main()
{
    std::printf("test_stringhash: start\n");

    test_known_vectors();
    std::printf("  known vectors (watermelon/apple_red/banana): OK\n");

    test_case_insensitive();
    std::printf("  case-insensitive folding: OK\n");

    test_short_strings();
    std::printf("  short strings (len 1-4): OK\n");

    test_empty();
    std::printf("  empty string (deterministic): OK\n");

    test_distinctness();
    std::printf("  distinctness (appl != apple, etc.): OK\n");

    test_boundary_11_12();
    std::printf("  switch/loop boundary (len 11 vs 12): OK\n");

    std::printf("test_stringhash: PASS\n");
    return 0;
}
