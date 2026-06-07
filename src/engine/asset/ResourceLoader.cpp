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

// TODO: 0x001b48c4 -- binary does PathGetParent(filePath)->BasePathSet, then FileDataReader(filePath)+Initialize(reader); port delegates to const char* ctor instead.
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

// TODO: 0x001b4804 -- binary calls BasePathSet(basePath) then Initialize(reader); port leaves reader unread (Initialize(DataReader&) unported).
ResourceLoader::ResourceLoader(DataReader& /*reader*/, const AsciiString& basePath)
    : m_flag(0)
    , m_BasePath(basePath)
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    , m_ReadPos(0)
#endif
{
}

// TODO: 0x001b465c -- binary destroys m_Children vector then m_BasePath AsciiString; port relies on implicit member dtors (equivalent).
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

// TODO: 0x001b4708 -- DataReader& Initialize is the binary's real parser: ReadLE child count, recurse per child via VectorDataReader, reserve+push_back into m_Children. Port currently parses via the raw uint8_t* overload above; this DataReader& path is an empty stub.
void ResourceLoader::Initialize(DataReader& /*reader*/)
{
}

// TODO: 0x001b45bc -- binary memcpy's count bytes from m_Data[m_ReadPos] (reads via DataReader cursor); port body matches semantically.
void ResourceLoader::ReadBytes(void* dest, unsigned long count)
{
    if (count > 0 && m_ReadPos + count <= m_Data.size()) {
        memcpy(dest, &m_Data[m_ReadPos], count);
        m_ReadPos += count;
    }
}

// TODO: 0x001b45e0 -- binary Read<u16> length, Resize buffer, ReadBytes into AsciiString; port body matches semantically.
AsciiString ResourceLoader::ReadString()
{
    uint16_t len = Read<uint16_t>();
    if (len == 0) return AsciiString("");
    std::string str(len, '\0');
    ReadBytes(&str[0], len);
    return AsciiString(str);
}

// TODO: 0x001b46d0 -- binary Read<u32> 1-based index, ConvertFromLittle, return &m_Children[index-1]; port body matches semantically.
ResourceLoader* ResourceLoader::ReadSubResourceLookup()
{
    uint32_t index = Read<uint32_t>();
    if (index > 0 && index - 1 < (uint32_t)m_Children.size()) {
        return &m_Children[index - 1];
    }
    return nullptr;
}

} // namespace Mortar
