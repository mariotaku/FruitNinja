// test_math_utils.cpp -- unit tests for engine math/util free functions (batch 4 of #195).
//
// Covers:
//   FileStringHash @ 0x00252e98: dsstofile normalization (backslash->slash, A-Z->a-z),
//     bit-exact with StringHash for lowercase-no-backslash input.
//   HashTypeConvert @ 0x001d8e64: entity-type name-hash->typeId lookup (7 entries).
//   EncodeUTF8FromUCS @ 0x0018ce0c: UCS-4 codepoint array -> UTF-8 std::string.
//
// Pure logic: no SDL_Init, no GL, no audio.
// Uses fn_add_game_test (links fruit-ninja-game for HashTypeConvert; SDL2main wraps main).
// Cross-build safe: no lambdas, no range-for, no auto, no enum class.

#include "util/StringHash.h"
#include "game/EntityTypes.h"
#include "util/Utf8Encode.h"
#include "math/Math.h"
#include "math/_Vector3.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_EQ(a, b) \
    do { \
        uint32_t _a = (uint32_t)(a); uint32_t _b = (uint32_t)(b); \
        if (_a != _b) { \
            std::printf("FAIL (%s:%d): 0x%08x != 0x%08x  (%s vs %s)\n", \
                __FILE__, __LINE__, _a, _b, #a, #b); \
            ::exit(1); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// FileStringHash
// ---------------------------------------------------------------------------

static void test_filestringhash_lowercase_identity()
{
    // Lowercase input with no backslashes: FileStringHash must equal StringHash.
    CHECK_EQ(FileStringHash("abc"),       StringHash("abc"));
    CHECK_EQ(FileStringHash("watermelon"),StringHash("watermelon"));
    CHECK_EQ(FileStringHash("fruit"),     StringHash("fruit"));
    CHECK_EQ(FileStringHash("a/b/c"),     StringHash("a/b/c"));
}

static void test_filestringhash_uppercase_fold()
{
    // A-Z normalised to a-z by dsstofile.
    CHECK_EQ(FileStringHash("ABC"), FileStringHash("abc"));
    CHECK_EQ(FileStringHash("ABC"), StringHash("abc"));
    CHECK_EQ(FileStringHash("Fruit"), FileStringHash("fruit"));
}

static void test_filestringhash_backslash_to_slash()
{
    // Backslash normalised to forward slash.
    CHECK_EQ(FileStringHash("a\\b"), FileStringHash("a/b"));
    CHECK_EQ(FileStringHash("a\\b"), StringHash("a/b"));
    // Mixed: both normalizations apply.
    CHECK_EQ(FileStringHash("A\\B"), FileStringHash("a/b"));
}

// ---------------------------------------------------------------------------
// HashTypeConvert
// ---------------------------------------------------------------------------

static void test_hashtypeconvert_hits()
{
    bool found;

    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("fruit"),   found), 0);
    CHECK(found);

    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("bomb"),    found), 1);
    CHECK(found);

    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("coin"),    found), 2);
    CHECK(found);

    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("slash"),   found), 3);
    CHECK(found);

    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("blast"),   found), 4);
    CHECK(found);

    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("jiblet"),  found), 5);
    CHECK(found);

    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("fruitray"),found), 6);
    CHECK(found);
}

static void test_hashtypeconvert_miss()
{
    bool found = true;
    long result = HashTypeConvert((unsigned long)0x12345678u, found);
    CHECK(result == -1L);
    CHECK(!found);

    found = true;
    result = HashTypeConvert(StringHash("unknown_entity_xyz"), found);
    CHECK(result == -1L);
    CHECK(!found);
}

static void test_hashtypeconvert_idempotent()
{
    // Second call (after table init) must return same results.
    bool found;
    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("fruit"),found), 0);
    CHECK(found);
    found = false;
    CHECK_EQ(HashTypeConvert(StringHash("bomb"), found), 1);
    CHECK(found);
}

// ---------------------------------------------------------------------------
// EncodeUTF8FromUCS
// ---------------------------------------------------------------------------

static void test_encodeutf8_ascii()
{
    unsigned long cp[1];
    cp[0] = (unsigned long)'A';  // U+0041
    std::string out;
    EncodeUTF8FromUCS(cp, 1, out);
    CHECK(out.size() == 1);
    CHECK(out[0] == 'A');
}

static void test_encodeutf8_2byte()
{
    // U+00E9 (e-acute): expected UTF-8 = {0xC3, 0xA9}
    unsigned long cp[1];
    cp[0] = (unsigned long)0x00E9u;
    std::string out;
    EncodeUTF8FromUCS(cp, 1, out);
    CHECK(out.size() == 2);
    CHECK((unsigned char)out[0] == 0xC3u);
    CHECK((unsigned char)out[1] == 0xA9u);
}

static void test_encodeutf8_3byte()
{
    // U+4E2D (Chinese middle character): expected UTF-8 = {0xE4, 0xB8, 0xAD}
    unsigned long cp[1];
    cp[0] = (unsigned long)0x4E2Du;
    std::string out;
    EncodeUTF8FromUCS(cp, 1, out);
    CHECK(out.size() == 3);
    CHECK((unsigned char)out[0] == 0xE4u);
    CHECK((unsigned char)out[1] == 0xB8u);
    CHECK((unsigned char)out[2] == 0xADu);
}

static void test_encodeutf8_mixed()
{
    // "A" + U+00E9: expected = {0x41, 0xC3, 0xA9}
    unsigned long cp[2];
    cp[0] = (unsigned long)'A';
    cp[1] = (unsigned long)0x00E9u;
    std::string out;
    EncodeUTF8FromUCS(cp, 2, out);
    CHECK(out.size() == 3);
    CHECK((unsigned char)out[0] == 0x41u);
    CHECK((unsigned char)out[1] == 0xC3u);
    CHECK((unsigned char)out[2] == 0xA9u);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

// Math::LineIntersect @ 0x002752cc..0x0027542b -- 2D segment-segment intersection.
//
// Regression pin. The port shipped three sign errors against the binary: denom
// used dyA*dxB + dyB*dxA instead of dxA*dyB - dyA*dxB, S_B had a flipped sign AND
// mixed B2.x with B1.y, and the Y numerator was negated. The denom bug alone made
// the canonical X-crossing below produce denom == 0, so LineIntersect returned
// false for two segments that plainly cross.
//
// Expected values are the BINARY's, decoded from the vmul/vnmls pairs at
// 0x2752f8/0x275304 (denom), 0x275314/0x27531c (S_B) and 0x275364 (Y):
// denom = -200 and out = (5,5) for this input.
static void test_lineintersect_crossing()
{
    _Vector3<float> A1(0.0f, 0.0f, 0.0f), A2(10.0f, 10.0f, 0.0f);
    _Vector3<float> B1(0.0f, 10.0f, 0.0f), B2(10.0f, 0.0f, 0.0f);
    _Vector3<float> out(-1.0f, -1.0f, 42.0f);

    CHECK(Math::LineIntersect(A1, A2, B1, B2, out));
    CHECK(out.x > 4.999f && out.x < 5.001f);
    CHECK(out.y > 4.999f && out.y < 5.001f);
    // The binary writes only x and y on success; z is left untouched.
    CHECK(out.z > 41.999f && out.z < 42.001f);
}

// Parallel segments share no point: denom == 0 is the binary's only early-out
// before the bounds tests, and it must still reject.
static void test_lineintersect_parallel_rejects()
{
    _Vector3<float> A1(0.0f, 0.0f, 0.0f), A2(10.0f, 0.0f, 0.0f);
    _Vector3<float> B1(0.0f, 5.0f, 0.0f), B2(10.0f, 5.0f, 0.0f);
    _Vector3<float> out(0.0f, 0.0f, 0.0f);

    CHECK(!Math::LineIntersect(A1, A2, B1, B2, out));
}

// Infinite lines cross at (20,20), but that point is outside both segments'
// XY bounding boxes, so the containment tests must reject it. Guards against a
// "fix" that gets denom right but drops the bounds checks.
static void test_lineintersect_out_of_span_rejects()
{
    _Vector3<float> A1(0.0f, 0.0f, 0.0f), A2(10.0f, 10.0f, 0.0f);
    _Vector3<float> B1(30.0f, 10.0f, 0.0f), B2(40.0f, 0.0f, 0.0f);
    _Vector3<float> out(0.0f, 0.0f, 0.0f);

    CHECK(!Math::LineIntersect(A1, A2, B1, B2, out));
}

int main(int /*argc*/, char** /*argv*/)
{
    std::printf("test_math_utils: start\n");

    test_filestringhash_lowercase_identity();
    std::printf("  FileStringHash: lowercase == StringHash: OK\n");

    test_filestringhash_uppercase_fold();
    std::printf("  FileStringHash: uppercase fold (ABC==abc): OK\n");

    test_filestringhash_backslash_to_slash();
    std::printf("  FileStringHash: backslash->slash: OK\n");

    test_hashtypeconvert_hits();
    std::printf("  HashTypeConvert: all 7 type hits (fruit/bomb/coin/slash/blast/jiblet/fruitray): OK\n");

    test_hashtypeconvert_miss();
    std::printf("  HashTypeConvert: unknown hash -> -1 + found=false: OK\n");

    test_hashtypeconvert_idempotent();
    std::printf("  HashTypeConvert: idempotent (second call same results): OK\n");

    test_encodeutf8_ascii();
    std::printf("  EncodeUTF8FromUCS: ASCII 'A' -> \"A\": OK\n");

    test_encodeutf8_2byte();
    std::printf("  EncodeUTF8FromUCS: U+00E9 -> {0xC3,0xA9}: OK\n");

    test_encodeutf8_3byte();
    std::printf("  EncodeUTF8FromUCS: U+4E2D -> {0xE4,0xB8,0xAD}: OK\n");

    test_encodeutf8_mixed();
    std::printf("  EncodeUTF8FromUCS: mixed 'A'+U+00E9 -> 3 bytes: OK\n");

    test_lineintersect_crossing();
    std::printf("  LineIntersect: X-crossing -> (5,5), z untouched: OK\n");

    test_lineintersect_parallel_rejects();
    std::printf("  LineIntersect: parallel (denom==0) -> false: OK\n");

    test_lineintersect_out_of_span_rejects();
    std::printf("  LineIntersect: crossing outside both spans -> false: OK\n");

    std::printf("test_math_utils: PASS\n");
    return 0;
}
