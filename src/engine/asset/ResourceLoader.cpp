#include "asset/ResourceLoader.h"
#include "asset/File.h"
#include <cstdio>

namespace Mortar {

ResourceLoader::ResourceLoader()
    : m_ReadPos(0)
{
}

// Matches original ResourceLoader::Load<T>(AsciiString&) flow:
// FileDataReader opens file, Initialize reads from sequential stream.
// The file starts directly with Initialize format (skip_u32, childCount, ...).
// "HBR0" at offset 0 is just the skip_u32 value, NOT a separate header.
ResourceLoader::ResourceLoader(const char* filePath)
    : m_ReadPos(0)
{
    File f(filePath, 0, 0);
    if (!f.Load(nullptr, 0)) {
        fprintf(stderr, "ResourceLoader: failed to open '%s'\n", filePath);
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

// Matches ResourceLoader::Initialize (0x001b4708)
// Format: skip_u32, childCount, [childSize + childData]..., typeIdCount, typeIds..., rawSize, rawData
// Each child is recursively in the same format.
void ResourceLoader::Initialize(const uint8_t* data, size_t dataSize) {
    if (dataSize < 8) return;

    size_t pos = 0;

    // Skip unknown value (u32) — often "HBR0" text
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

} // namespace Mortar
