#include "asset/ResourceLoader.h"
#include "asset/DataReader.h"
#include "asset/FileDataReader.h"
#include "asset/VectorDataReader.h"
#include "asset/File.h"
#include "debug/Logger.h"
#include <cstring>

namespace Mortar {

// PathGetParent: returns the parent directory of 'path' (up to and including the
// last '/' or '\', or empty string if no separator). Used by the path ctor
// (0x00255730) to derive m_BasePath from the file path.
static AsciiString PathGetParent(const AsciiString& path)
{
    const char* s = path.CStr();
    if (!s) return AsciiString("");
    size_t len = strlen(s);
    size_t i = len;
    while (i > 0) {
        --i;
        if (s[i] == '/' || s[i] == '\\') {
            char buf[512];
            size_t n = i + 1;
            if (n >= sizeof(buf)) n = sizeof(buf) - 1;
            memcpy(buf, s, n);
            buf[n] = '\0';
            return AsciiString(buf);
        }
    }
    return AsciiString("");
}

// Default ctor (no binary equivalent; port convenience for m_Children push_back).
ResourceLoader::ResourceLoader()
    : m_ReadCursor(0)
{
}

// Binary ctor @ 0x00255730:
//   m_ReadCursor=0; AsciiString(m_BasePath,nullptr); m_Data={}; m_Children={};
//   m_BasePath.Set(PathGetParent(path));
//   FileDataReader fr(path);
//   Initialize(fr);
//   fr.~FileDataReader();
ResourceLoader::ResourceLoader(const AsciiString& filePath)
    : m_ReadCursor(0)
{
    m_BasePath.Set(PathGetParent(filePath));
    FileDataReader fr(filePath);
    Initialize(fr);
}

// Port convenience wrapper: const char* path ctor delegates to the AsciiString ctor.
ResourceLoader::ResourceLoader(const char* filePath)
    : m_ReadCursor(0)
{
    if (filePath) {
        AsciiString path(filePath);
        m_BasePath.Set(PathGetParent(path));
        FileDataReader fr(path);
        Initialize(fr);
    }
}

// Binary ctor @ 0x002556B4:
//   m_ReadCursor=0 first; AsciiString(m_BasePath,nullptr); m_Data={}; m_Children={};
//   m_BasePath.Set(basePath);
//   Initialize(r);
ResourceLoader::ResourceLoader(DataReader& reader, const AsciiString& basePath)
    : m_ReadCursor(0)
{
    m_BasePath.Set(basePath);
    Initialize(reader);
}

// Binary dtor @ 0x002554A0 -- destroys m_Children (+0x38), then m_Data (+0x2c), then
// m_BasePath (+0x04). C++ destroys members in reverse declaration order
// (m_Children, m_Data, m_BasePath), which is identical to the binary's order, so
// the implicit member dtors reproduce the binary exactly.
ResourceLoader::~ResourceLoader()
{
}

// Binary Initialize(DataReader&) @ 0x002554EC -- the real HBR0 format parser.
// Format: skip_u32, childCount, [len + blob(len)]..., typeIdCount, typeIds..., rawSize, rawData.
// The skip_u32 is the file magic ("HBR0") at every recursion level.
void ResourceLoader::Initialize(DataReader& r)
{
    r.ReadLE<uint32_t>();                        // [0] skip (magic / "HBR0")
    uint32_t childCount = r.ReadLE<uint32_t>();  // [1] child count
    m_Children.reserve(childCount);
    for (uint32_t i = 0; i < childCount; ++i) {
        uint32_t len = r.ReadLE<uint32_t>();                  // child blob length
        std::vector<unsigned char> blob = r.Read(len);        // vtable slot 0
        VectorDataReader vr(blob);                            // inline reader over blob
        ResourceLoader child(vr, m_BasePath);                 // recurse (same basePath)
        m_Children.push_back(child);
    }
    uint32_t typeIdCount = r.ReadLE<uint32_t>();              // type-id table count
    for (uint32_t j = 0; j < typeIdCount; ++j) {
        r.ReadLE<uint32_t>();                                  // discard each type id
    }
    uint32_t rawSize = r.ReadLE<uint32_t>();                   // trailing raw payload size
    if (rawSize != 0) {
        m_Data = r.Read(rawSize);                              // vtable slot 0 -> m_Data
    }
}

// Port convenience: raw buffer overload wraps a VectorDataReader and delegates
// to the canonical DataReader& path.
void ResourceLoader::Initialize(const uint8_t* data, size_t dataSize)
{
    std::vector<unsigned char> buf(data, data + dataSize);
    VectorDataReader vr(buf);
    Initialize(vr);
}

// Binary @ 0x00255398 -- if (count != 0) { memcpy(dest, &m_Data[cursor], count); cursor += count; }
// ASM-spec v1.6.1 ResourceLoader::ReadBytes @ 0x00255398:
//   if(count==0)return; memcpy(dest, m_Data.begin()+m_ReadCursor, count); m_ReadCursor += count.
//   Cursor IS the +0x00 member (m_ReadCursor).
// DIFFERS: original = no upper-bound check (only count != 0), using extra
//   m_ReadCursor + count <= m_Data.size() guard because the port lacks the binary's
//   DataReader stream-end invariants and would otherwise read out of bounds on
//   malformed assets.
void ResourceLoader::ReadBytes(void* dest, unsigned long count)
{
    if (count > 0 && (size_t)m_ReadCursor + count <= m_Data.size()) {
        memcpy(dest, &m_Data[m_ReadCursor], count);
        m_ReadCursor += (int32_t)count;
    }
}

// ReadString funnels through ReadBytes (Read<AsciiString> equivalent).
// Binary: len = Read<u16>(); AsciiString s; s.Resize(len); ReadBytes(s.GetPtr(), len); return s;
// Port-specific: the binary writes ReadBytes() straight into the AsciiString's
//   buffer via a mutable GetPtr(); the port's AsciiString exposes only a const
//   CStr(), so we stage the bytes in a std::string of the same length and
//   build the AsciiString from it. Same observable result.
AsciiString ResourceLoader::ReadString()
{
    uint16_t len = Read<uint16_t>();
    if (len == 0) return AsciiString("");
    std::string str(len, '\0');
    ReadBytes(&str[0], len);
    return AsciiString(str);
}

// Binary @ 0x002553cc -- index = Endian::ConvertFromLittle(Read<u32>());
//   if (index != 0 && index-1 < m_Children.size()) {
//       ResourceLoader* c = &m_Children[index-1];
//       *(u32*)c = 0;   // reset child's m_ReadCursor (+0x00) to 0
//       return c;
//   }
//   return nullptr;
// Port-specific: Read<u32> already reads native little-endian on LE hosts, so the
//   binary's explicit Endian::ConvertFromLittle is a no-op on the SDL targets.
ResourceLoader* ResourceLoader::ReadSubResourceLookup()
{
    uint32_t index = Read<uint32_t>();
    if (index > 0 && index - 1 < (uint32_t)m_Children.size()) {
        ResourceLoader* child = &m_Children[index - 1];
        child->m_ReadCursor = 0;
        return child;
    }
    return nullptr;
}

} // namespace Mortar
