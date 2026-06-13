#include "asset/FileDataReader.h"

namespace Mortar {

// Binary ctor @ 0x0023B7EC
FileDataReader::FileDataReader(const AsciiString& path)
    : m_file(path.CStr(), 0, 0)
    , m_open(false)
{
    m_open = m_file.Open();
}

// Binary dtor @ 0x0023B858
FileDataReader::~FileDataReader()
{
    m_file.Close();
}

// Binary vtable slot 0 @ 0x002404B8
std::vector<unsigned char> FileDataReader::Read(size_t count)
{
    std::vector<unsigned char> v;
    if (count) {
        v.resize(count, 0);
    }
    m_file.Read(v.data(), (unsigned long)count);
    return v;
}

} // namespace Mortar
