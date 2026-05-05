#ifndef MORTAR_RESOURCE_LOADER_H
#define MORTAR_RESOURCE_LOADER_H

#include "util/AsciiString.h"
#include "util/SmartPtr.h"
#include "asset/DataReader.h"
#include "asset/Skeleton.h"
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
    ResourceLoader(const AsciiString& filePath);
    ResourceLoader(DataReader& reader, const AsciiString& basePath);
    ~ResourceLoader();

    // Initialize from raw data buffer (recursive HBR0 parsing)
    // Matches 0x001b4708
    void Initialize(const uint8_t* data, size_t dataSize);
    void Initialize(DataReader& reader);

    // Sequential read methods
    template<typename T>
    T Read() {
        T val;
        memcpy(&val, &m_Data[m_ReadPos], sizeof(T));
        m_ReadPos += sizeof(T);
        return val;
    }

    void ReadBytes(void* dest, unsigned long count);
    AsciiString ReadString();
    ResourceLoader* ReadSubResourceLookup();

    const AsciiString& BasePathGet() const { return m_BasePath; }
    void BasePathSet(const AsciiString& path) { m_BasePath = path; }

    size_t DataSize() const { return m_Data.size(); }
    const uint8_t* DataPtr() const { return m_Data.data(); }
    size_t ChildCount() const { return m_Children.size(); }

    void ResetReadPos() { m_ReadPos = 0; }

    // Matches Read<Mortar::Skeleton> (called from LoadModel at 0x001a8468)
    // Reads skeleton from rawData: boneCount(u32) + per-bone:
    //   AsciiString(name) + long(4,parentIndex) + float[16](bindPose) +
    //   float[3](localTranslation) + float[4](localRotation) + float[9](localScale)
    // Fixed per-bone size: 4 + 64 + 12 + 16 + 36 = 132 bytes + variable name length.
    void ReadSkeleton(Skeleton& outSkeleton) {
        if (m_ReadPos + 4 > m_Data.size()) return;
        uint32_t boneCount = Read<uint32_t>();
        if (boneCount == 0 || boneCount > 1024) return;

        std::vector<Skeleton::Bone> bones(boneCount);
        for (uint32_t i = 0; i < boneCount && m_ReadPos < m_Data.size(); i++) {
            if (m_ReadPos + 2 > m_Data.size()) return;
            AsciiString boneName = ReadString();
            bones[i].m_Name = boneName.CStr();

            if (m_ReadPos + 4 > m_Data.size()) return;
            bones[i].m_ParentIndex = (int)Read<int32_t>();   // long = 4 bytes

            if (m_ReadPos + 64 > m_Data.size()) return;
            ReadBytes(bones[i].m_BindPoseMat, 64);           // float[16]

            if (m_ReadPos + 12 > m_Data.size()) return;
            ReadBytes(bones[i].m_LocalTranslation, 12);      // float[3]

            if (m_ReadPos + 16 > m_Data.size()) return;
            ReadBytes(bones[i].m_LocalRotation, 16);         // float[4]

            if (m_ReadPos + 36 > m_Data.size()) return;
            ReadBytes(bones[i].m_LocalScale, 36);            // float[9]
        }
        outSkeleton.Swap(bones);
    }

    // Skip Skeleton data without storing (legacy -- superseded by ReadSkeleton).
    void SkipSkeleton() {
        if (m_ReadPos + 4 > m_Data.size()) return;
        uint32_t boneCount = Read<uint32_t>();
        for (uint32_t i = 0; i < boneCount && m_ReadPos < m_Data.size(); i++) {
            if (m_ReadPos + 2 > m_Data.size()) return;
            uint16_t nameLen = Read<uint16_t>();
            if (m_ReadPos + nameLen > m_Data.size()) return;
            m_ReadPos += nameLen;
            // Skip: parent(4) + matrix44(64) + vec3(12) + quat(16) + mat3(36) = 132 bytes
            if (m_ReadPos + 132 > m_Data.size()) return;
            m_ReadPos += 132;
        }
    }

    // ---- STUBS (binary) ----
    // STUB: ResourceLoader::ResourceLoader(AsciiString const&) -- binary @ 0x???? (TODO RE)
    // STUB: ResourceLoader::ResourceLoader(DataReader&, AsciiString const&) -- binary @ 0x???? (TODO RE)
    // STUB: ResourceLoader::~ResourceLoader() -- binary @ 0x???? (TODO RE)
    // STUB: ResourceLoader::Initialize(DataReader&) -- binary @ 0x???? (TODO RE)
    // STUB: ResourceLoader::ReadBytes(void*, unsigned long) -- binary @ 0x???? (TODO RE)
    // STUB: ResourceLoader::ReadString() -- binary @ 0x???? (TODO RE)
    // STUB: ResourceLoader::ReadSubResourceLookup() -- binary @ 0x???? (TODO RE)
    // ---- end STUBS ----
};

} // namespace Mortar

#ifdef __bada__
// TODO: ResourceLoader::m_BasePath is at +0x04 in binary -- implies a field at +0x00
// not present in the current port. Resolve +0x00 field before enabling these asserts.
// static_assert(offsetof(ResourceLoader, m_BasePath) == 0x04, "ResourceLoader::m_BasePath offset");
// static_assert(offsetof(ResourceLoader, m_Data)     == 0x2C, "ResourceLoader::m_Data offset");
// static_assert(offsetof(ResourceLoader, m_Children) == 0x38, "ResourceLoader::m_Children offset");
#endif

#endif
