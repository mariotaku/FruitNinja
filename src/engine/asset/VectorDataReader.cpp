#include "asset/VectorDataReader.h"
#include <cstring>

namespace Mortar {

// Inline construction pattern from Initialize @ 0x255578-0x2555A0.
VectorDataReader::VectorDataReader(const std::vector<unsigned char>& src)
    : m_buf(src)
    , m_pos(0)
{
}

VectorDataReader::~VectorDataReader()
{
}

// Binary vtable slot 0 @ 0x00255D38
std::vector<unsigned char> VectorDataReader::Read(size_t count)
{
    size_t avail = m_buf.size() - m_pos;
    size_t n = (count <= avail) ? count : avail;
    std::vector<unsigned char> v;
    v.resize(n, 0);
    if (n) {
        memcpy(v.data(), &m_buf[m_pos], n);
    }
    m_pos += n;
    return v;
}

} // namespace Mortar
