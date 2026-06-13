#ifndef FN_ENGINE_ASSET_VECTORDATAREADER_H
#define FN_ENGINE_ASSET_VECTORDATAREADER_H

#include "asset/DataReader.h"
#include <cstdint>
#include <vector>

// Mortar::VectorDataReader -- DataReader subclass that reads from an in-memory
// std::vector<unsigned char> with a byte cursor.
// Binary sizeof == 0x14 (20 bytes):
//   +0x00  vptr                          (4 bytes; = VectorDataReader vtable + 8)
//   +0x04  std::vector<uchar> m_buf      (12 bytes: begin@+0x04, finish@+0x08, end_cap@+0x0C)
//   +0x10  size_t m_pos                  (4 bytes; byte cursor, init 0)
// Vtable @ 0x2D0598: slot0 = Read @ 0x00255D38.
// In the binary this class is built inline on the stack inside
// ResourceLoader::Initialize @ 0x002554EC; there is no standalone ctor symbol.
// The port provides a ctor(const std::vector<unsigned char>&) for use in
// ResourceLoader::Initialize.

namespace Mortar {

class VectorDataReader : public DataReader {
public:
    // Inline construction pattern from Initialize @ 0x255578-0x2555A0:
    //   vptr  = VectorDataReaderVTable + 8;
    //   m_buf = src;    // copy-construct byte vector
    //   m_pos = 0;
    explicit VectorDataReader(const std::vector<unsigned char>& src);

    ~VectorDataReader();

    // Binary vtable slot 0 @ 0x00255D38:
    //   avail = (m_buf.finish - m_buf.begin) - m_pos;
    //   n = (count <= avail) ? count : avail;   // clamp to remaining
    //   v.resize(n, 0); if(n) memcpy(v.data(), m_buf.begin + m_pos, n);
    //   m_pos += n; return v;
    // NOTE: clamps to remaining (returns short vector at EOF), unlike FileDataReader.
    virtual std::vector<unsigned char> Read(size_t count);

    // +0x04: backing byte buffer (copy-constructed from caller's vector)
    std::vector<unsigned char> m_buf;
    // +0x10: byte cursor
    size_t m_pos;
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(VectorDataReader) == 0x14, "VectorDataReader sizeof mismatch");
static_assert(offsetof(VectorDataReader, m_buf) == 0x04, "VectorDataReader::m_buf offset");
static_assert(offsetof(VectorDataReader, m_pos) == 0x10, "VectorDataReader::m_pos offset");
#endif

} // namespace Mortar

#endif
