// DataReader / VectorDataReader unit test.
//
// Exercises the byte-stream reader API used by all mesh/texture/resource
// parsers: ctor, position tracking, Read(n) exact/clamp/EOF, and ReadLE<T>
// for uint16_t, uint32_t, and float.
//
// Pure CPU: no GL, no audio, no file I/O, no SDL.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "asset/DataReader.h"
#include "asset/VectorDataReader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <stdint.h>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// ---- test_initial_position -------------------------------------------------
// Ctor must initialise m_pos to 0 and copy m_buf verbatim.
static void test_initial_position()
{
    static const unsigned char kData[] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0
    };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);

    // m_pos must be 0 after construction.
    CHECK(vr.m_pos == 0);
    // m_buf must be a copy of the source vector.
    CHECK(vr.m_buf.size() == 10);
    CHECK(vr.m_buf[0] == 0x10);
    CHECK(vr.m_buf[9] == 0xA0);
}

// ---- test_read_exact -------------------------------------------------------
// Read(4) on a 10-byte buffer: returns exactly 4 bytes with correct content
// and advances m_pos to 4.
static void test_read_exact()
{
    static const unsigned char kData[] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0
    };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);

    std::vector<unsigned char> v = vr.Read(4);
    CHECK(v.size() == 4);
    CHECK(v[0] == 0x10);
    CHECK(v[1] == 0x20);
    CHECK(v[2] == 0x30);
    CHECK(v[3] == 0x40);
    CHECK(vr.m_pos == 4);
}

// ---- test_read_remaining ---------------------------------------------------
// After reading 4 of 10 bytes, Read(6) returns the remaining 6 bytes and
// positions m_pos at end.
static void test_read_remaining()
{
    static const unsigned char kData[] = {
        0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x90, 0xA0
    };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);

    // Consume first 4 bytes.
    vr.Read(4);
    CHECK(vr.m_pos == 4);

    // Read remaining 6 bytes.
    std::vector<unsigned char> v = vr.Read(6);
    CHECK(v.size() == 6);
    CHECK(v[0] == 0x50);
    CHECK(v[5] == 0xA0);
    CHECK(vr.m_pos == 10);
}

// ---- test_read_clamp_to_remaining ------------------------------------------
// Read(N) where N > remaining must clamp and return only the bytes left.
// Binary spec (VectorDataReader::Read @ 0x00255D38):
//   n = (count <= avail) ? count : avail; returns vector of size n.
static void test_read_clamp_to_remaining()
{
    static const unsigned char kData[] = { 0x01, 0x02, 0x03 };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);

    // Read 2 bytes -- exact.
    std::vector<unsigned char> v1 = vr.Read(2);
    CHECK(v1.size() == 2);
    CHECK(v1[0] == 0x01);
    CHECK(v1[1] == 0x02);

    // Read 10 bytes from 1 remaining -- must clamp to 1.
    std::vector<unsigned char> v2 = vr.Read(10);
    CHECK(v2.size() == 1);
    CHECK(v2[0] == 0x03);
    CHECK(vr.m_pos == 3);
}

// ---- test_read_past_eof ----------------------------------------------------
// Read at EOF must return an empty vector (size == 0).
static void test_read_past_eof()
{
    static const unsigned char kData[] = { 0xAA, 0xBB };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);

    // Consume all.
    vr.Read(2);
    CHECK(vr.m_pos == 2);

    // Read past EOF -- avail = 0, so n = 0 -> empty vector.
    std::vector<unsigned char> v = vr.Read(5);
    CHECK(v.size() == 0);
    CHECK(vr.m_pos == 2);
}

// ---- test_read_le_u32 ------------------------------------------------------
// ReadLE<uint32_t> on {0x34,0x12,0x78,0x56} -> 0x56781234.
// Little-endian: byte[0] is LSB.
static void test_read_le_u32()
{
    static const unsigned char kData[] = { 0x34, 0x12, 0x78, 0x56 };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);
    uint32_t val = vr.ReadLE<uint32_t>();
    CHECK(val == 0x56781234u);
    CHECK(vr.m_pos == 4);
}

// ---- test_read_le_u16 ------------------------------------------------------
// ReadLE<uint16_t> on {0x34,0x12} -> 0x1234.
static void test_read_le_u16()
{
    static const unsigned char kData[] = { 0x34, 0x12 };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);
    uint16_t val = vr.ReadLE<uint16_t>();
    CHECK(val == 0x1234u);
    CHECK(vr.m_pos == 2);
}

// ---- test_read_le_float ----------------------------------------------------
// ReadLE<float> on the IEEE-754 LE encoding of 1.0f:
//   1.0f = sign=0, exp=127 (0x7F), mantissa=0
//   bits = 0x3F800000 -> LE bytes {0x00, 0x00, 0x80, 0x3F}
static void test_read_le_float()
{
    static const unsigned char kData[] = { 0x00, 0x00, 0x80, 0x3F };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);
    float val = vr.ReadLE<float>();
    // The memcpy in ReadLE must reproduce exactly 1.0f.
    CHECK(val == 1.0f);
    CHECK(vr.m_pos == 4);
}

// ---- test_read_le_short_buffer_returns_zero --------------------------------
// ReadLE<uint32_t> when fewer than 4 bytes remain must return T() == 0.
// DataReader::ReadLE<T> (DataReader.h):
//   v = Read(sizeof(T));
//   T val = T();
//   if (v.size() >= sizeof(T)) memcpy(&val, v.data(), sizeof(T));
//   return val;
// At EOF, Read() returns empty -> v.size() == 0 < 4 -> memcpy skipped -> val == 0.
static void test_read_le_short_buffer_returns_zero()
{
    // Only 2 bytes available -- not enough for uint32_t (4 bytes).
    static const unsigned char kData[] = { 0xFF, 0xFF };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);

    // Consume the 2 bytes first so EOF is triggered for the ReadLE call.
    vr.Read(2);
    CHECK(vr.m_pos == 2);

    // ReadLE<uint32_t> at EOF: Read(4) returns empty vector -> returns T() = 0.
    uint32_t val = vr.ReadLE<uint32_t>();
    CHECK(val == 0u);
}

// ---- test_empty_buffer -----------------------------------------------------
// Empty buffer: Read(N) -> empty; ReadLE -> 0.
static void test_empty_buffer()
{
    std::vector<unsigned char> buf;
    Mortar::VectorDataReader vr(buf);

    CHECK(vr.m_pos == 0);
    CHECK(vr.m_buf.size() == 0);

    std::vector<unsigned char> v = vr.Read(4);
    CHECK(v.size() == 0);

    uint32_t u = vr.ReadLE<uint32_t>();
    CHECK(u == 0u);
}

// ---- test_sequential_reads -------------------------------------------------
// Multiple sequential reads advance position correctly.
static void test_sequential_reads()
{
    // 8-byte buffer split into two uint32_t values.
    // {0x01,0x00,0x00,0x00, 0x02,0x00,0x00,0x00} -> 1u, 2u LE.
    static const unsigned char kData[] = {
        0x01, 0x00, 0x00, 0x00,
        0x02, 0x00, 0x00, 0x00
    };
    std::vector<unsigned char> buf(kData, kData + sizeof(kData));
    Mortar::VectorDataReader vr(buf);

    uint32_t a = vr.ReadLE<uint32_t>();
    CHECK(a == 1u);
    CHECK(vr.m_pos == 4);

    uint32_t b = vr.ReadLE<uint32_t>();
    CHECK(b == 2u);
    CHECK(vr.m_pos == 8);

    // EOF -- next read is empty.
    std::vector<unsigned char> tail = vr.Read(1);
    CHECK(tail.size() == 0);
}

int main()
{
    std::printf("test_datareader: start\n");

    test_initial_position();
    std::printf("  initial_position: PASS\n");

    test_read_exact();
    std::printf("  read_exact: PASS\n");

    test_read_remaining();
    std::printf("  read_remaining: PASS\n");

    test_read_clamp_to_remaining();
    std::printf("  read_clamp_to_remaining: PASS\n");

    test_read_past_eof();
    std::printf("  read_past_eof: PASS\n");

    test_read_le_u32();
    std::printf("  read_le_u32: PASS\n");

    test_read_le_u16();
    std::printf("  read_le_u16: PASS\n");

    test_read_le_float();
    std::printf("  read_le_float: PASS\n");

    test_read_le_short_buffer_returns_zero();
    std::printf("  read_le_short_buffer_returns_zero: PASS\n");

    test_empty_buffer();
    std::printf("  empty_buffer: PASS\n");

    test_sequential_reads();
    std::printf("  sequential_reads: PASS\n");

    std::printf("test_datareader: ALL PASS\n");
    return 0;
}
