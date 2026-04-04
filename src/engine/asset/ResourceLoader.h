#ifndef MORTAR_RESOURCE_LOADER_H
#define MORTAR_RESOURCE_LOADER_H

#include "util/AsciiString.h"
#include "util/SmartPtr.h"
#include <vector>
#include <cstdint>
#include <cstring>

namespace Mortar {

// Matches original ResourceLoader (68 bytes = 0x44)
// HBR0 container parser with recursive children
// Ref: docs/engine/utility-types.md
class ResourceLoader {
public:
    AsciiString m_BasePath;                 // +0x04 (base path for resolving refs)
    std::vector<uint8_t> m_Data;            // +0x2C (raw data bytes)
    std::vector<ResourceLoader> m_Children; // +0x38 (nested child loaders)
    size_t m_ReadPos;                       // read cursor for sequential access

    ResourceLoader();
    ResourceLoader(const char* filePath);

    // Initialize from raw data buffer (recursive HBR0 parsing)
    // Matches 0x001b4708
    void Initialize(const uint8_t* data, size_t dataSize);

    // Sequential read methods
    template<typename T>
    T Read() {
        T val;
        memcpy(&val, &m_Data[m_ReadPos], sizeof(T));
        m_ReadPos += sizeof(T);
        return val;
    }

    void ReadBytes(void* dest, size_t count) {
        if (count > 0 && m_ReadPos + count <= m_Data.size()) {
            memcpy(dest, &m_Data[m_ReadPos], count);
            m_ReadPos += count;
        }
    }

    // Matches 0x001b45e0
    AsciiString ReadString() {
        uint16_t len = Read<uint16_t>();
        if (len == 0) return AsciiString("");
        std::string str(len, '\0');
        ReadBytes(&str[0], len);
        return AsciiString(str);
    }

    // Matches 0x001b46d0 — 1-based index into children
    ResourceLoader* ReadSubResourceLookup() {
        uint32_t index = Read<uint32_t>();
        if (index > 0 && index - 1 < (uint32_t)m_Children.size()) {
            return &m_Children[index - 1];
        }
        return NULL;
    }

    const AsciiString& BasePathGet() const { return m_BasePath; }
    void BasePathSet(const AsciiString& path) { m_BasePath = path; }

    size_t DataSize() const { return m_Data.size(); }
    const uint8_t* DataPtr() const { return m_Data.data(); }
    size_t ChildCount() const { return m_Children.size(); }

    void ResetReadPos() { m_ReadPos = 0; }
};

} // namespace Mortar

#endif
