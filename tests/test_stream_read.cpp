// test_stream_read.cpp
//
// Unit tests for the binary chunk-stream reader free functions:
//   Mortar::ReadFloat     @0x00238250 -- reads 4 bytes as float, advances cursor by 4
//   Mortar::ReadVec3      @0x00238260 -- reads 12 bytes as 3 floats, advances by 12
//   Mortar::ReadString    @0x002381e0 -- reads [uint32 len][bytes][\0], advances 4+len+1
//   Mortar::ReadChunkHash @0x0023819c -- reads len; if <101 hashes bytes, advances 4+len+1
//
// These are DISTINCT from ResourceLoader::ReadString() (uint16 prefix, no null).
// Pure CPU: no GL, no SDL, no audio, no file I/O.
// Cross-build safe: no lambdas, no range-for, no auto, no enum class.

#include "engine/asset/ResourceLoader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdint.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// Helper: write a float's IEEE-754 bytes into buf at offset.
static void put_float(unsigned char* buf, float f) {
    memcpy(buf, &f, 4);
}

// ---- test_read_float -------------------------------------------------------
// ReadFloat reads exactly 4 bytes as float and advances cursor by 4.
static void test_read_float()
{
    unsigned char buf[8];
    put_float(buf + 0, 50.0f);
    put_float(buf + 4, 1.0f);

    unsigned char* cursor = buf;
    float v1 = Mortar::ReadFloat(&cursor);
    CHECK(v1 == 50.0f);
    CHECK(cursor == buf + 4);

    float v2 = Mortar::ReadFloat(&cursor);
    CHECK(v2 == 1.0f);
    CHECK(cursor == buf + 8);
}

// ---- test_read_vec3 -------------------------------------------------------
// ReadVec3 reads 3 consecutive floats and advances cursor by 12.
static void test_read_vec3()
{
    unsigned char buf[12];
    put_float(buf + 0, 1.0f);
    put_float(buf + 4, 2.5f);
    put_float(buf + 8, 3.0f);

    unsigned char* cursor = buf;
    _Vector3<float> v = Mortar::ReadVec3(&cursor);
    CHECK(v.x == 1.0f);
    CHECK(v.y == 2.5f);
    CHECK(v.z == 3.0f);
    CHECK(cursor == buf + 12);
}

// ---- test_read_string ------------------------------------------------------
// ReadString reads [uint32 len][len bytes][\0] and advances by 4 + len + 1.
static void test_read_string()
{
    // Build stream: len=3, "abc", '\0'
    unsigned char buf[8];
    buf[0] = 3; buf[1] = 0; buf[2] = 0; buf[3] = 0;  // uint32 len = 3
    buf[4] = 'a'; buf[5] = 'b'; buf[6] = 'c'; buf[7] = '\0';

    unsigned char* cursor = buf;
    Mortar::AsciiString s = Mortar::ReadString(&cursor);
    CHECK(strcmp(s.CStr(), "abc") == 0);
    CHECK(cursor == buf + 8);  // 4 (len) + 3 (bytes) + 1 (null) = 8
}

// ---- test_read_string_empty ------------------------------------------------
// ReadString with len=0: reads the null terminator, advances by 5.
static void test_read_string_empty()
{
    unsigned char buf[5];
    buf[0] = 0; buf[1] = 0; buf[2] = 0; buf[3] = 0;  // uint32 len = 0
    buf[4] = '\0';

    unsigned char* cursor = buf;
    Mortar::AsciiString s = Mortar::ReadString(&cursor);
    CHECK(strcmp(s.CStr(), "") == 0);
    CHECK(cursor == buf + 5);  // 4 (len) + 0 (bytes) + 1 (null) = 5
}

// ---- test_read_chunk_hash_determinism --------------------------------------
// ReadChunkHash is deterministic (same input -> same hash) and non-zero for "abc".
static void test_read_chunk_hash_determinism()
{
    // len=3, "abc", '\0'
    unsigned char buf[8];
    buf[0] = 3; buf[1] = 0; buf[2] = 0; buf[3] = 0;
    buf[4] = 'a'; buf[5] = 'b'; buf[6] = 'c'; buf[7] = '\0';

    unsigned char* cursor = buf;
    uint32_t hash1 = Mortar::ReadChunkHash(&cursor);
    CHECK(hash1 != 0);
    CHECK(cursor == buf + 8);  // 4 (len) + 3 (bytes) + 1 (null) = 8

    cursor = buf;
    uint32_t hash2 = Mortar::ReadChunkHash(&cursor);
    CHECK(hash1 == hash2);
}

// ---- test_read_chunk_hash_sanity_guard ------------------------------------
// ReadChunkHash returns 0 when len >= 101 (sanity guard).
static void test_read_chunk_hash_sanity_guard()
{
    // len = 101: binary returns 0 immediately
    unsigned char buf[4];
    buf[0] = 101; buf[1] = 0; buf[2] = 0; buf[3] = 0;

    unsigned char* cursor = buf;
    uint32_t hash = Mortar::ReadChunkHash(&cursor);
    CHECK(hash == 0);
}

// ---- test_read_float_vec3_sequential ---------------------------------------
// Chain ReadFloat then ReadVec3 from a single contiguous buffer.
static void test_read_float_vec3_sequential()
{
    unsigned char buf[16];
    put_float(buf +  0, 7.0f);   // standalone float
    put_float(buf +  4, 1.0f);   // vec3.x
    put_float(buf +  8, 2.0f);   // vec3.y
    put_float(buf + 12, 3.0f);   // vec3.z

    unsigned char* cursor = buf;
    float f = Mortar::ReadFloat(&cursor);
    CHECK(f == 7.0f);
    CHECK(cursor == buf + 4);

    _Vector3<float> v = Mortar::ReadVec3(&cursor);
    CHECK(v.x == 1.0f);
    CHECK(v.y == 2.0f);
    CHECK(v.z == 3.0f);
    CHECK(cursor == buf + 16);
}

int main()
{
    std::printf("test_stream_read: start\n");

    test_read_float();
    std::printf("  read_float: PASS\n");

    test_read_vec3();
    std::printf("  read_vec3: PASS\n");

    test_read_string();
    std::printf("  read_string: PASS\n");

    test_read_string_empty();
    std::printf("  read_string_empty: PASS\n");

    test_read_chunk_hash_determinism();
    std::printf("  read_chunk_hash_determinism: PASS\n");

    test_read_chunk_hash_sanity_guard();
    std::printf("  read_chunk_hash_sanity_guard: PASS\n");

    test_read_float_vec3_sequential();
    std::printf("  read_float_vec3_sequential: PASS\n");

    std::printf("test_stream_read: ALL PASS\n");
    return 0;
}
