#include "asset/ResourceLoader.h"
#include "asset/DataReader.h"
#include "asset/File.h"
#include "debug/Logger.h"

namespace Mortar {

ResourceLoader::ResourceLoader()
    : m_flag(0)
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    , m_ReadPos(0)
#endif
{
}

// Matches original ResourceLoader::Load<T>(AsciiString&) flow:
// FileDataReader opens file, Initialize reads from sequential stream.
// The file starts directly with Initialize format (skip_u32, childCount, ...).
// "HBR0" at offset 0 is just the skip_u32 value, NOT a separate header.
ResourceLoader::ResourceLoader(const char* filePath)
    : m_flag(0)
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    , m_ReadPos(0)
#endif
{
    File f(filePath, 0, 0);
    if (!f.Load(nullptr, 0)) {
        LOG_ERROR("RESOURCE", "failed to open '%s'", filePath);
        return;
    }

    unsigned long size = f.Size();
    if (size == 0) return;

    // Extract base path from file path
    std::string pathStr(filePath);
    size_t lastSlash = pathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        m_BasePath = AsciiString(pathStr.substr(0, lastSlash + 1));
    }

    Initialize(static_cast<const uint8_t*>(f.Data()), size);
}

// TODO: 0x001b48c4 -- binary does PathGetParent(filePath)->BasePathSet, then FileDataReader(filePath)+Initialize(reader); port delegates to const char* ctor instead. [blocked: FileDataReader + DataReader::ReadLE / VectorDataReader subsystem unported]
ResourceLoader::ResourceLoader(const AsciiString& filePath)
    : m_flag(0)
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    , m_ReadPos(0)
#endif
{
    ResourceLoader tmp(filePath.CStr());
    m_BasePath = tmp.m_BasePath;
    m_Data     = tmp.m_Data;
    m_Children = tmp.m_Children;
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    m_ReadPos  = tmp.m_ReadPos;
#endif
}

// TODO: 0x001b4804 -- binary calls BasePathSet(basePath) then Initialize(reader); port leaves reader unread. [blocked: Initialize(DataReader&) / DataReader::ReadLE / VectorDataReader subsystem unported]
ResourceLoader::ResourceLoader(DataReader& /*reader*/, const AsciiString& basePath)
    : m_flag(0)
    , m_BasePath(basePath)
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    , m_ReadPos(0)
#endif
{
}

// Binary @ 0x001b465c -- destroys m_Children (+0x38), then m_Data (+0x2c), then
// m_BasePath (+0x04). C++ destroys members in reverse declaration order
// (m_Children, m_Data, m_BasePath), which is identical to the binary's order, so
// the implicit member dtors reproduce the binary exactly.
ResourceLoader::~ResourceLoader()
{
}

// Matches ResourceLoader::Initialize (0x001b4708)
// Format: skip_u32, childCount, [childSize + childData]..., typeIdCount, typeIds..., rawSize, rawData
// Each child is recursively in the same format.
void ResourceLoader::Initialize(const uint8_t* data, size_t dataSize) {
    if (dataSize < 8) return;

    size_t pos = 0;

    // Skip unknown value (u32) -- often "HBR0" text
    pos += 4;

    // Read child count
    uint32_t childCount = 0;
    memcpy(&childCount, data + pos, 4);
    pos += 4;

    // Sanity check
    if (childCount > 1000) return;

    // Read children recursively
    m_Children.reserve(childCount);
    for (uint32_t i = 0; i < childCount && pos + 4 <= dataSize; i++) {
        // Read child data size
        uint32_t childSize = 0;
        memcpy(&childSize, data + pos, 4);
        pos += 4;

        if (childSize == 0 || pos + childSize > dataSize) break;

        ResourceLoader child;
        child.m_BasePath = m_BasePath;
        child.Initialize(data + pos, childSize);

        m_Children.push_back(child);
        pos += childSize;
    }

    // Read type ID count and skip type IDs
    if (pos + 4 <= dataSize) {
        uint32_t typeIdCount = 0;
        memcpy(&typeIdCount, data + pos, 4);
        pos += 4;
        if (typeIdCount <= 1000) {
            pos += typeIdCount * 4;
        }
    }

    // Read remaining raw data
    if (pos + 4 <= dataSize) {
        uint32_t rawSize = 0;
        memcpy(&rawSize, data + pos, 4);
        pos += 4;
        if (rawSize > 0 && pos + rawSize <= dataSize) {
            m_Data.assign(data + pos, data + pos + rawSize);
        }
    }
}

// TODO: 0x001b4708 -- DataReader& Initialize is the binary's real parser: skip ReadLE<u32>, ReadLE<u32> child count, reserve(count), then per child: len=ReadLE<u32>; reader.Read(buf,len) (vtable slot 1); VectorDataReader(buf); ResourceLoader(vreader, BasePathGet()); push_back; then ReadLE<u32> typeIdCount loop (ReadLE<u32> each, discarded); then ReadLE<u32> rawSize; if(rawSize) reader.Read(buf,rawSize) -> m_Data = buf. Port currently parses via the raw uint8_t* overload above; this DataReader& path stays an empty stub. [blocked: DataReader::ReadLE<T> + VectorDataReader + FileDataReader subsystem unported]
void ResourceLoader::Initialize(DataReader& /*reader*/)
{
}

// Binary @ 0x001b45bc -- if (count != 0) { memcpy(dest, &m_Data[cursor], count); cursor += count; }
// The binary's read cursor lives at this+0x00 (m_flag); the port uses m_ReadPos.
// DIFFERS: original = no upper-bound check (only count != 0), using extra
//   m_ReadPos + count <= m_Data.size() guard because the port lacks the binary's
//   DataReader stream-end invariants and would otherwise read out of bounds on
//   malformed assets.
void ResourceLoader::ReadBytes(void* dest, unsigned long count)
{
    if (count > 0 && m_ReadPos + count <= m_Data.size()) {
        memcpy(dest, &m_Data[m_ReadPos], count);
        m_ReadPos += count;
    }
}

// Binary @ 0x001b45e0 -- len = Read<u16>(); AsciiString s; s.Resize(len);
//   ReadBytes(s.GetPtr(), len); return s;
// Port-specific: the binary writes ReadBytes() straight into the AsciiString's
//   buffer via a mutable GetPtr(); the port's AsciiString exposes only a const
//   _GetPtr(), so we stage the bytes in a std::string of the same length and
//   build the AsciiString from it. Same observable result (Resize(len) ==
//   std::string(len,'\0'), then len bytes copied in via the same ReadBytes cursor).
AsciiString ResourceLoader::ReadString()
{
    uint16_t len = Read<uint16_t>();
    if (len == 0) return AsciiString("");
    std::string str(len, '\0');
    ReadBytes(&str[0], len);
    return AsciiString(str);
}

// Binary @ 0x001b46d0 -- index = Endian::ConvertFromLittle(Read<u32>());
//   if (index != 0 && index-1 < m_Children.size()) {
//       ResourceLoader* c = &m_Children[index-1];
//       *(u32*)c = 0;   // reset child's read cursor (m_flag@+0x00) to 0
//       return c;
//   }
//   return nullptr;
// Port-specific: Read<u32> already reads native little-endian on LE hosts, so the
//   binary's explicit Endian::ConvertFromLittle is a no-op on the SDL targets.
//   The binary's "*(u32*)c = 0" zeroes the child's m_flag, which is the binary's
//   read cursor; in the port the cursor is m_ReadPos, so we reset that instead.
ResourceLoader* ResourceLoader::ReadSubResourceLookup()
{
    uint32_t index = Read<uint32_t>();
    if (index > 0 && index - 1 < (uint32_t)m_Children.size()) {
        ResourceLoader* child = &m_Children[index - 1];
        child->m_flag = 0;
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
        child->m_ReadPos = 0;   // binary cursor is m_flag; port cursor is m_ReadPos
#endif
        return child;
    }
    return nullptr;
}

} // namespace Mortar
