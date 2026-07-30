// DataStreamReader unit test -- task #195 Wave B.
//
// DataStreamReader is a high-fan-in parser leaf: bugs here cascade into every
// .mmd/.mad/.tex asset that the engine loads. This test validates the exact
// binary-specified behaviors from v1.6.1 before any consumer TUs rely on it.
//
// Covers:
//   - ReadBasicType<uint32_t>/<uint16_t>/<float> round-trip on LE buffer
//   - Read(std::string&) for length-prefixed string
//   - ReadRaw<T> -- always raw bytes, no swap (identical to ReadBasicType on LE host)
//   - Underflow sets m_Error and yields 0; cursor at end
//   - Sequential reads advance cursor correctly
//   - MakeSubReader(count) sizes to the explicit count, not remaining bytes
//
// Pure in-process: no GPU, no SDL, no audio.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "asset/DataStreamReader.h"
#include "util/Endian.h"
#include "util/Immutable.h"
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

// ---------------------------------------------------------------------------
// test_setSource_init
// SetSource must set m_pStart == m_pCursor == data, m_Size = size, m_Error = false.
// ---------------------------------------------------------------------------
static void test_setSource_init() {
    static const uint8_t kData[] = { 0xAA, 0xBB, 0xCC, 0xDD };
    Mortar::DataStreamReader r;
    r.SetSource(kData, 4, Endian::LITTLE);
    CHECK(r.m_pStart  == (void*)kData);
    CHECK(r.m_pCursor == (void*)kData);
    CHECK(r.m_Size    == 4u);
    CHECK(r.m_Endian  == (uint32_t)Endian::LITTLE);
    CHECK(r.m_Error   == false);
}

// ---------------------------------------------------------------------------
// test_ctor_delegates
// The (data,size,endian) ctor must produce the same state as SetSource.
// ---------------------------------------------------------------------------
static void test_ctor_delegates() {
    static const uint8_t kData[] = { 0x01, 0x02, 0x03 };
    Mortar::DataStreamReader r(kData, 3, Mortar::Endian::LITTLE);
    CHECK(r.m_pStart  == (void*)kData);
    CHECK(r.m_pCursor == (void*)kData);
    CHECK(r.m_Size    == 3u);
    CHECK(r.m_Error   == false);
}

// ---------------------------------------------------------------------------
// test_readBasicType_u32
// ReadBasicType<uint32_t> on LE bytes {0x78,0x56,0x34,0x12} -> 0x12345678.
// No endian swap on LE host.
// ---------------------------------------------------------------------------
static void test_readBasicType_u32() {
    static const uint8_t kData[] = { 0x78, 0x56, 0x34, 0x12 };
    Mortar::DataStreamReader r(kData, 4, Mortar::Endian::LITTLE);
    uint32_t val = 0;
    r.ReadBasicType<uint32_t>(val);
    CHECK(val == 0x12345678u);
    CHECK(r.m_Error == false);
    CHECK(r.m_pCursor == (void*)(kData + 4));
}

// ---------------------------------------------------------------------------
// test_readBasicType_u16
// ReadBasicType<uint16_t> on {0x34, 0x12} -> 0x1234.
// ---------------------------------------------------------------------------
static void test_readBasicType_u16() {
    static const uint8_t kData[] = { 0x34, 0x12 };
    Mortar::DataStreamReader r(kData, 2, Mortar::Endian::LITTLE);
    uint16_t val = 0;
    r.ReadBasicType<uint16_t>(val);
    CHECK(val == 0x1234u);
    CHECK(r.m_Error == false);
}

// ---------------------------------------------------------------------------
// test_readBasicType_float
// ReadBasicType<float> on IEEE-754 LE encoding of 3.14f.
// 3.14f bits = 0x4048F5C3 -> LE bytes {0xC3, 0xF5, 0x48, 0x40}.
// ---------------------------------------------------------------------------
static void test_readBasicType_float() {
    // 3.14f IEEE-754: 0x4048F5C3, LE bytes = C3 F5 48 40
    static const uint8_t kData[] = { 0xC3, 0xF5, 0x48, 0x40 };
    Mortar::DataStreamReader r(kData, 4, Mortar::Endian::LITTLE);
    float val = 0.0f;
    r.ReadBasicType<float>(val);
    // Bit-exact reinterpret: must round-trip through memcpy
    uint32_t bits = 0;
    memcpy(&bits, &val, 4);
    CHECK(bits == 0x4048F5C3u);
    CHECK(r.m_Error == false);
}

// ---------------------------------------------------------------------------
// test_readRaw_vs_readBasicType
// On an LE host, ReadRaw and ReadBasicType must give identical results.
// ---------------------------------------------------------------------------
static void test_readRaw_vs_readBasicType() {
    static const uint8_t kData[] = {
        0x78, 0x56, 0x34, 0x12,   // first uint32
        0x78, 0x56, 0x34, 0x12    // second uint32 (identical)
    };
    Mortar::DataStreamReader r(kData, 8, Mortar::Endian::LITTLE);

    uint32_t fromRaw = 0;
    r.ReadRaw<uint32_t>(fromRaw);

    uint32_t fromBasic = 0;
    r.ReadBasicType<uint32_t>(fromBasic);

    CHECK(fromRaw == fromBasic);
    CHECK(fromRaw == 0x12345678u);
    CHECK(r.m_Error == false);
}

// ---------------------------------------------------------------------------
// test_readString
// Read(std::string&) reads a uint32 length prefix then the string bytes.
// Buffer: {0x05,0x00,0x00,0x00, 'H','e','l','l','o'}
// ---------------------------------------------------------------------------
static void test_readString() {
    static const uint8_t kData[] = {
        0x05, 0x00, 0x00, 0x00,   // length = 5 (LE uint32)
        'H', 'e', 'l', 'l', 'o'
    };
    Mortar::DataStreamReader r(kData, 9, Mortar::Endian::LITTLE);
    std::string s;
    r.Read(s);
    CHECK(s == "Hello");
    CHECK(s.size() == 5u);
    CHECK(r.m_Error == false);
    // Cursor should be at end.
    CHECK(r.m_pCursor == (void*)(kData + 9));
}

// ---------------------------------------------------------------------------
// test_readString_empty
// Length-prefixed empty string: {0,0,0,0} -> "".
// ---------------------------------------------------------------------------
static void test_readString_empty() {
    static const uint8_t kData[] = { 0x00, 0x00, 0x00, 0x00 };
    Mortar::DataStreamReader r(kData, 4, Mortar::Endian::LITTLE);
    std::string s = "was_non_empty";
    r.Read(s);
    CHECK(s.empty());
    CHECK(r.m_Error == false);
}

// ---------------------------------------------------------------------------
// test_underflow_sets_error
// ReadBasicType<uint32_t> on a 2-byte buffer -> m_Error=true, returns 0.
// ---------------------------------------------------------------------------
static void test_underflow_sets_error() {
    static const uint8_t kData[] = { 0xFF, 0xFF };
    Mortar::DataStreamReader r(kData, 2, Mortar::Endian::LITTLE);
    uint32_t val = 0xDEADBEEFu;
    r.ReadBasicType<uint32_t>(val);
    CHECK(val == 0u);
    CHECK(r.m_Error == true);
    // Cursor must be at end after underflow.
    CHECK(r.m_pCursor == (void*)(kData + 2));
}

// ---------------------------------------------------------------------------
// test_underflow_u8_at_eof
// ReadRaw<uint8_t> at exact EOF -> underflow, m_Error=true.
// ---------------------------------------------------------------------------
static void test_underflow_u8_at_eof() {
    static const uint8_t kData[] = { 0x42 };
    Mortar::DataStreamReader r(kData, 1, Mortar::Endian::LITTLE);
    // Consume the one byte.
    uint8_t v = 0;
    r.ReadRaw<uint8_t>(v);
    CHECK(v == 0x42u);
    CHECK(r.m_Error == false);
    // Now at EOF: next ReadRaw must underflow.
    r.ReadRaw<uint8_t>(v);
    CHECK(v == 0u);
    CHECK(r.m_Error == true);
}

// ---------------------------------------------------------------------------
// test_sequential_reads
// Multiple reads advance cursor; reads are in order.
// Buffer: uint32 + uint16 + uint8
// ---------------------------------------------------------------------------
static void test_sequential_reads() {
    static const uint8_t kData[] = {
        0x01, 0x00, 0x00, 0x00,   // uint32 = 1
        0x02, 0x00,               // uint16 = 2
        0x03                      // uint8  = 3
    };
    Mortar::DataStreamReader r(kData, 7, Mortar::Endian::LITTLE);

    uint32_t a = 0;
    r.ReadBasicType<uint32_t>(a);
    CHECK(a == 1u);

    uint16_t b = 0;
    r.ReadBasicType<uint16_t>(b);
    CHECK(b == 2u);

    uint8_t c = 0;
    r.ReadRaw<uint8_t>(c);
    CHECK(c == 3u);

    CHECK(r.m_Error == false);
    CHECK(r.m_pCursor == (void*)(kData + 7));
}

// ---------------------------------------------------------------------------
// test_makeSubReader
// MakeSubReader(count) v1.6.1 @0x00250c08: sub-reader starts at the source's
// current cursor, covers EXACTLY `count` bytes (an explicit caller-supplied
// byte count -- not the remaining bytes in the buffer), same endianness.
// Source cursor is unchanged.
//
// The block is deliberately SMALLER than what remains in the buffer, so
// m_Size == count is distinguishable from a (wrong) m_Size == remaining.
// Buffer: uint32(skip) + uint32(sub block, count=4) + uint32(trailing, past sub)
// ---------------------------------------------------------------------------
static void test_makeSubReader() {
    static const uint8_t kData[] = {
        0x01, 0x00, 0x00, 0x00,   // skip: uint32 = 1
        0x02, 0x00, 0x00, 0x00,   // sub block payload: uint32 = 2 (count = 4 bytes)
        0x03, 0x00, 0x00, 0x00    // trailing bytes, NOT part of the sub block
    };
    Mortar::DataStreamReader parent(kData, 12, Mortar::Endian::LITTLE);

    // Advance parent past the first uint32.
    uint32_t skip = 0;
    parent.ReadBasicType<uint32_t>(skip);
    CHECK(skip == 1u);
    CHECK(parent.m_pCursor == (void*)(kData + 4));

    // Remaining bytes at this point is 8, but the sub-reader must be sized to
    // the explicit count (4), not the remaining bytes -- this is the bug the
    // old test could not catch (count == remaining there).
    Mortar::DataStreamReader sub = parent.MakeSubReader(4);

    CHECK(sub.m_pStart  == (void*)(kData + 4));
    CHECK(sub.m_pCursor == (void*)(kData + 4));
    CHECK(sub.m_Size    == 4u);              // == count, NOT remaining (8)
    CHECK(sub.m_Endian  == (uint32_t)Endian::LITTLE);
    CHECK(sub.m_Error   == false);

    // Read the payload back.
    uint32_t target = 0;
    sub.ReadBasicType<uint32_t>(target);
    CHECK(target == 2u);
    CHECK(sub.m_Error == false);
    CHECK(sub.m_pCursor == (void*)(kData + 8));

    // A further read past `count` (into what would be the trailing bytes if
    // m_Size had been sized to `remaining`) must underflow and set m_Error.
    uint32_t past = 0;
    sub.ReadBasicType<uint32_t>(past);
    CHECK(sub.m_Error == true);
    CHECK(past == 0u);

    // Source (parent) cursor must be unchanged by MakeSubReader.
    CHECK(parent.m_pCursor == (void*)(kData + 4));
    CHECK(parent.m_Error   == false);
}

// ---------------------------------------------------------------------------
// test_operator_rightshift_immutable
// operator>>(DataStreamReader&, Immutable&) reads a length-prefixed string
// and assigns it to the Immutable handle.
// ---------------------------------------------------------------------------
static void test_operator_rightshift_immutable() {
    static const uint8_t kData[] = {
        0x04, 0x00, 0x00, 0x00,   // length = 4
        'T', 'e', 's', 't'
    };
    Mortar::DataStreamReader r(kData, 8, Mortar::Endian::LITTLE);
    Immutable imm;
    Mortar::operator>>(r, imm);
    CHECK(std::string(imm.c_str()) == "Test");
    CHECK(r.m_Error == false);
    CHECK(r.m_pCursor == (void*)(kData + 8));
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::printf("test_datastreamreader: start\n");

    test_setSource_init();
    std::printf("  setSource_init: PASS\n");

    test_ctor_delegates();
    std::printf("  ctor_delegates: PASS\n");

    test_readBasicType_u32();
    std::printf("  readBasicType_u32: PASS\n");

    test_readBasicType_u16();
    std::printf("  readBasicType_u16: PASS\n");

    test_readBasicType_float();
    std::printf("  readBasicType_float: PASS\n");

    test_readRaw_vs_readBasicType();
    std::printf("  readRaw_vs_readBasicType: PASS\n");

    test_readString();
    std::printf("  readString: PASS\n");

    test_readString_empty();
    std::printf("  readString_empty: PASS\n");

    test_underflow_sets_error();
    std::printf("  underflow_sets_error: PASS\n");

    test_underflow_u8_at_eof();
    std::printf("  underflow_u8_at_eof: PASS\n");

    test_sequential_reads();
    std::printf("  sequential_reads: PASS\n");

    test_makeSubReader();
    std::printf("  makeSubReader: PASS\n");

    test_operator_rightshift_immutable();
    std::printf("  operator_rightshift_immutable: PASS\n");

    std::printf("test_datastreamreader: ALL PASS\n");
    return 0;
}
