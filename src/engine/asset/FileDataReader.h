#ifndef FN_ENGINE_ASSET_FILEDATAREADER_H
#define FN_ENGINE_ASSET_FILEDATAREADER_H

#include "asset/DataReader.h"
#include "asset/File.h"
#include "util/AsciiString.h"
#include <vector>

// Mortar::FileDataReader -- DataReader subclass that reads from a Mortar::File.
// Binary sizeof == 0x48 (72 bytes):
//   +0x00  vptr              (4 bytes; = FileDataReader vtable + 8)
//   +0x04  Mortar::File      (64 bytes embedded object)
//   +0x44  bool m_open       (result of File::Open(); strb, single byte)
//   +0x45..+0x47  padding
// Vtable @ 0x2D0300: slot0 = Read @ 0x002404B8.
// ctor @ 0x0023B7EC, dtor @ 0x0023B858.

namespace Mortar {

class FileDataReader : public DataReader {
public:
    // Binary ctor @ 0x0023B7EC:
    //   vptr = FileDataReaderVTable + 8;
    //   File::File(&m_file, path._GetPtr(), 0, 0);
    //   m_open = File::Open(&m_file, nullptr);   // single-byte bool @ +0x44
    explicit FileDataReader(const AsciiString& path);

    // Binary dtor @ 0x0023B858:
    //   vptr = vtable+8; File::Close(&m_file); File::~File(&m_file);
    ~FileDataReader();

    // Binary vtable slot 0 @ 0x002404B8:
    //   vector<uchar> v; if(count) v.resize(count,0);
    //   File::Read(&m_file, v.data(), count); return v;
    virtual std::vector<unsigned char> Read(size_t count);

    // +0x04: embedded File object (64 bytes)
    File m_file;
    // +0x44: result of File::Open()
    bool m_open;
};

#if defined(__bada__)
static_assert(sizeof(FileDataReader) == 0x48, "FileDataReader sizeof mismatch");
static_assert(offsetof(FileDataReader, m_file) == 0x04, "FileDataReader::m_file offset");
static_assert(offsetof(FileDataReader, m_open) == 0x44, "FileDataReader::m_open offset");
#endif

} // namespace Mortar

#endif
