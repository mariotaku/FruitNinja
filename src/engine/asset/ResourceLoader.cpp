#include "asset/ResourceLoader.h"
#include <cstdio>

namespace Mortar {

ResourceLoader::ResourceLoader()
    : m_ReadPos(0)
{
}

ResourceLoader::ResourceLoader(const char* filePath)
    : m_ReadPos(0)
{
    FILE* f = fopen(filePath, "rb");
    if (!f) {
        fprintf(stderr, "ResourceLoader: failed to open '%s'\n", filePath);
        return;
    }

    // Check for HBR0 magic
    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "HBR0", 4) != 0) {
        // Not an HBR0 file — read as raw data
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        m_Data.resize(size);
        fread(m_Data.data(), 1, size, f);
        fclose(f);
        return;
    }

    // Read HBR0 header: type(u16) + pad(u16) + dataSize(u32)
    uint16_t type, pad;
    uint32_t dataSize;
    fread(&type, 2, 1, f);
    fread(&pad, 2, 1, f);
    fread(&dataSize, 4, 1, f);

    // Read full payload
    std::vector<uint8_t> payload(dataSize);
    size_t bytesRead = fread(payload.data(), 1, dataSize, f);
    fclose(f);

    if (bytesRead < dataSize) {
        fprintf(stderr, "ResourceLoader: short read for '%s' (%zu/%u)\n",
                filePath, bytesRead, dataSize);
    }

    // Extract base path from file path
    std::string pathStr(filePath);
    size_t lastSlash = pathStr.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        m_BasePath = AsciiString(pathStr.substr(0, lastSlash + 1));
    }

    Initialize(payload.data(), payload.size());
}

// Matches ResourceLoader::Initialize (0x001b4708)
// Recursive HBR0 parsing: reads children, type IDs, and raw data
void ResourceLoader::Initialize(const uint8_t* data, size_t dataSize) {
    if (dataSize < 8) return;

    size_t pos = 0;

    // Skip unknown value (u32)
    pos += 4;

    // Read child count
    uint32_t childCount = 0;
    memcpy(&childCount, data + pos, 4);
    pos += 4;

    // Read children recursively
    m_Children.reserve(childCount);
    for (uint32_t i = 0; i < childCount && pos < dataSize; i++) {
        // Read child data size
        uint32_t childSize = 0;
        memcpy(&childSize, data + pos, 4);
        pos += 4;

        if (pos + childSize > dataSize) break;

        ResourceLoader child;
        child.m_BasePath = m_BasePath;

        // Check if child has HBR0 header
        if (childSize >= 12 && memcmp(data + pos, "HBR0", 4) == 0) {
            // Skip HBR0 header (magic + type + pad + size = 12 bytes)
            uint32_t innerSize = 0;
            memcpy(&innerSize, data + pos + 8, 4);
            child.Initialize(data + pos + 12, innerSize);
        } else {
            // Raw child data
            child.m_Data.assign(data + pos, data + pos + childSize);
        }

        m_Children.push_back(child);
        pos += childSize;
    }

    // Read type ID count and skip type IDs
    if (pos + 4 <= dataSize) {
        uint32_t typeIdCount = 0;
        memcpy(&typeIdCount, data + pos, 4);
        pos += 4;
        pos += typeIdCount * 4; // skip type IDs
    }

    // Read remaining raw data
    if (pos + 4 <= dataSize) {
        uint32_t rawSize = 0;
        memcpy(&rawSize, data + pos, 4);
        pos += 4;
        if (pos + rawSize <= dataSize) {
            m_Data.assign(data + pos, data + pos + rawSize);
        }
    }
}

} // namespace Mortar
