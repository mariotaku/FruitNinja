// ResourceLoader / DataReader / VectorDataReader parser unit test.
//
// Tests the HBR0 binary format parser (Initialize @ 0x002554EC) using two
// deterministic in-memory vectors -- no file I/O, no GPU, no audio:
//
//   (a) apple.mad parse trace (spec section 7): 126-byte buffer, 0 children,
//       rawSize=110.  Verifies the linear path through Initialize.
//
//   (b) Hand-built 1-child nested buffer (spec section 7): exercises
//       child blob slicing, VectorDataReader clamp, recursion, typeId discard,
//       and trailing rawSize all at once.
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "engine/asset/DataReader.h"
#include "engine/asset/VectorDataReader.h"
#include "engine/asset/ResourceLoader.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// Helper: build a std::vector<unsigned char> from a plain array of bytes.
static std::vector<unsigned char> make_buf(const unsigned char* data, size_t n)
{
    return std::vector<unsigned char>(data, data + n);
}

// ---- Test (a): apple.mad parse trace -----------------------------------
// Spec section 7, first test vector.
// Full 126 bytes from xxd of FruitNinjaBada/Data/models/fruit/apple.mad:
//   @0x00  magic     = 48 42 52 30  ("HBR0", skip)
//   @0x04  children  = 00 00 00 00  (0)
//   @0x08  typeIds   = 00 00 00 00  (0)
//   @0x0C  rawSize   = 6E 00 00 00  (110)
//   @0x10  payload   = 110 bytes (01 00 00 00 3E 00 44 3A ...)
static void test_apple_mad()
{
    // Exact 126 bytes from xxd apple.mad (confirmed file size = 126).
    // xxd output:
    //   0000: 4842 5230 0000 0000  0000 0000 6e00 0000
    //   0010: 0100 0000 3e00 443a  5c50 726f 6a65 6374
    //   0020: 735c 6950 686f 6e65  4465 765c 4672 7569
    //   0030: 744e 696e 6a61 5c41  7373 6574 5f77 6f72
    //   0040: 6b69 6e67 5c46 7275  6974 5c46 7275 6974
    //   0050: 2e6d 6178 dfdd dd3e  8988 883c 0200 0000
    //   0060: 0900 615f 7069 6563  655f 3100 0000 0009
    //   0070: 0061 5f70 6965 6365  5f32 0000 0000
    static const unsigned char kAppleMad[126] = {
        /* 0000 */ 0x48,0x42,0x52,0x30, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x6E,0x00,0x00,0x00,
        /* 0010 */ 0x01,0x00,0x00,0x00, 0x3E,0x00,0x44,0x3A, 0x5C,0x50,0x72,0x6F, 0x6A,0x65,0x63,0x74,
        /* 0020 */ 0x73,0x5C,0x69,0x50, 0x68,0x6F,0x6E,0x65, 0x44,0x65,0x76,0x5C, 0x46,0x72,0x75,0x69,
        /* 0030 */ 0x74,0x4E,0x69,0x6E, 0x6A,0x61,0x5C,0x41, 0x73,0x73,0x65,0x74, 0x5F,0x77,0x6F,0x72,
        /* 0040 */ 0x6B,0x69,0x6E,0x67, 0x5C,0x46,0x72,0x75, 0x69,0x74,0x5C,0x46, 0x72,0x75,0x69,0x74,
        /* 0050 */ 0x2E,0x6D,0x61,0x78, 0xDF,0xDD,0xDD,0x3E, 0x89,0x88,0x88,0x3C, 0x02,0x00,0x00,0x00,
        /* 0060 */ 0x09,0x00,0x61,0x5F, 0x70,0x69,0x65,0x63, 0x65,0x5F,0x31,0x00, 0x00,0x00,0x09,0x00,
        /* 0070 */ 0x00,0x61,0x5F,0x70, 0x69,0x65,0x63,0x65, 0x5F,0x32,0x00,0x00, 0x00,0x00
    };

    CHECK(sizeof(kAppleMad) == 126);

    std::vector<unsigned char> buf = make_buf(kAppleMad, sizeof(kAppleMad));
    Mortar::VectorDataReader vr(buf);
    Mortar::ResourceLoader loader(vr, Mortar::AsciiString("models/fruit/"));

    CHECK(loader.m_Children.size() == 0);
    CHECK(loader.m_Data.size() == 110);
    // payload[0..3] == {0x01,0x00,0x00,0x00}
    CHECK(loader.m_Data[0] == 0x01);
    CHECK(loader.m_Data[1] == 0x00);
    CHECK(loader.m_Data[2] == 0x00);
    CHECK(loader.m_Data[3] == 0x00);
    // payload[4] == 0x3E (string length byte lo)
    CHECK(loader.m_Data[4] == 0x3E);
    // payload[6..8] == "D:\\" (0x44, 0x3A, 0x5C)
    CHECK(loader.m_Data[6] == 0x44);  // 'D'
    CHECK(loader.m_Data[7] == 0x3A);  // ':'
    CHECK(loader.m_Data[8] == 0x5C);  // '\\'
}

// ---- Test (b): hand-built 1-child nested buffer -------------------------
// Spec section 7, second test vector.
//
// Child blob (20 bytes):
//   magic "HBR0" | childCount=0 | typeIdCount=0 | rawSize=4 | payload "ABCD"
//
// Top-level buffer (44 bytes):
//   magic "HBR0" | childCount=1 | len=20 | <child blob 20 bytes>
//   | typeIdCount=1 | typeId[0]=0x99999999 | rawSize=3 | payload "XYZ"
static void test_nested_child()
{
    // Child blob: 5*4 = 20 bytes
    static const unsigned char kChild[] = {
        0x48, 0x42, 0x52, 0x30,   // magic "HBR0"
        0x00, 0x00, 0x00, 0x00,   // childCount = 0
        0x00, 0x00, 0x00, 0x00,   // typeIdCount = 0
        0x04, 0x00, 0x00, 0x00,   // rawSize = 4
        0x41, 0x42, 0x43, 0x44    // payload "ABCD"
    };
    CHECK(sizeof(kChild) == 20);

    // Top-level buffer:
    //   magic(4) + childCount(4) + len(4) + child(20) + typeIdCount(4) + typeId(4) + rawSize(4) + XYZ(3)
    //   = 4 + 4 + 4 + 20 + 4 + 4 + 4 + 3 = 47 bytes
    static const unsigned char kTop[] = {
        0x48, 0x42, 0x52, 0x30,   // magic "HBR0"
        0x01, 0x00, 0x00, 0x00,   // childCount = 1
        0x14, 0x00, 0x00, 0x00,   // len = 20 (size of child blob)
        // child blob (20 bytes):
        0x48, 0x42, 0x52, 0x30,   // child magic "HBR0"
        0x00, 0x00, 0x00, 0x00,   // child childCount = 0
        0x00, 0x00, 0x00, 0x00,   // child typeIdCount = 0
        0x04, 0x00, 0x00, 0x00,   // child rawSize = 4
        0x41, 0x42, 0x43, 0x44,   // child payload "ABCD"
        // back in top-level:
        0x01, 0x00, 0x00, 0x00,   // typeIdCount = 1
        0x99, 0x99, 0x99, 0x99,   // typeId[0] = 0x99999999 (discarded)
        0x03, 0x00, 0x00, 0x00,   // rawSize = 3
        0x58, 0x59, 0x5A          // payload "XYZ"
    };
    CHECK(sizeof(kTop) == 47);

    std::vector<unsigned char> buf = make_buf(kTop, sizeof(kTop));
    Mortar::VectorDataReader vr(buf);
    Mortar::AsciiString base("base/");
    Mortar::ResourceLoader top(vr, base);

    // Top-level assertions
    CHECK(top.m_Children.size() == 1);
    CHECK(top.m_Data.size() == 3);
    CHECK(top.m_Data[0] == 0x58);  // 'X'
    CHECK(top.m_Data[1] == 0x59);  // 'Y'
    CHECK(top.m_Data[2] == 0x5A);  // 'Z'

    // Child assertions
    CHECK(top.m_Children[0].m_Children.size() == 0);
    CHECK(top.m_Children[0].m_Data.size() == 4);
    CHECK(top.m_Children[0].m_Data[0] == 0x41);  // 'A'
    CHECK(top.m_Children[0].m_Data[1] == 0x42);  // 'B'
    CHECK(top.m_Children[0].m_Data[2] == 0x43);  // 'C'
    CHECK(top.m_Children[0].m_Data[3] == 0x44);  // 'D'

    // basePath must propagate to child
    CHECK(strcmp(top.m_Children[0].m_BasePath.CStr(), "base/") == 0);
}

// ---- Test: VectorDataReader clamp at EOF --------------------------------
// Reading more bytes than available must return only the remaining bytes.
static void test_vector_datareader_clamp()
{
    static const unsigned char kData[] = { 0x01, 0x02, 0x03 };
    std::vector<unsigned char> buf = make_buf(kData, sizeof(kData));
    Mortar::VectorDataReader vr(buf);

    // Read 2 bytes -- should succeed fully.
    std::vector<unsigned char> v1 = vr.Read(2);
    CHECK(v1.size() == 2);
    CHECK(v1[0] == 0x01);
    CHECK(v1[1] == 0x02);

    // Read 10 bytes from 1 remaining -- should clamp to 1.
    std::vector<unsigned char> v2 = vr.Read(10);
    CHECK(v2.size() == 1);
    CHECK(v2[0] == 0x03);

    // Read again at EOF -- should return empty.
    std::vector<unsigned char> v3 = vr.Read(4);
    CHECK(v3.size() == 0);
}

// ---- Test: ReadLE<uint32_t> via VectorDataReader -----------------------
static void test_read_le_u32()
{
    static const unsigned char kData[] = {
        0x78, 0x56, 0x34, 0x12   // = 0x12345678 LE
    };
    std::vector<unsigned char> buf = make_buf(kData, sizeof(kData));
    Mortar::VectorDataReader vr(buf);
    uint32_t val = vr.ReadLE<uint32_t>();
    CHECK(val == 0x12345678u);
}

int main()
{
    std::printf("test_resourceloader: running\n");

    test_vector_datareader_clamp();
    std::printf("  vector_datareader_clamp: PASS\n");

    test_read_le_u32();
    std::printf("  read_le_u32: PASS\n");

    test_apple_mad();
    std::printf("  apple_mad: PASS\n");

    test_nested_child();
    std::printf("  nested_child: PASS\n");

    std::printf("test_resourceloader: ALL PASS\n");
    return 0;
}
