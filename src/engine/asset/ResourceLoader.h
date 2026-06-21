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

// ResourceLoader (68 bytes = 0x44)
// Binary layout confirmed via ctor @ 0x002556B4 / path ctor @ 0x00255730:
//   +0x00  int32_t               m_flag     (zero-inited by ctor)
//   +0x04  AsciiString           m_BasePath (40 bytes)
//   +0x2C  vector<uint8_t>       m_Data     (12 bytes; raw payload set by Initialize)
//   +0x38  vector<ResourceLoader> m_Children (12 bytes; nested children)
//   +0x44  end (sizeof = 68)
class ResourceLoader {
public:
    int32_t m_flag;                         // +0x00 (zero-initialised by ctor)
    AsciiString m_BasePath;                 // +0x04 (base path for resolving refs)
    std::vector<uint8_t> m_Data;            // +0x2C (raw data bytes)
    std::vector<ResourceLoader> m_Children; // +0x38 (nested child loaders)

#if !defined(__bada__)
    // Port specific: sequential read cursor; no binary equivalent (binary uses DataReader).
    size_t m_ReadPos;
#endif

    ResourceLoader();
    ResourceLoader(const char* filePath);
    // Binary ctor @ 0x00255730: PathGetParent(path)->BasePath, FileDataReader(path), Initialize(reader)
    ResourceLoader(const AsciiString& filePath);
    // Binary ctor @ 0x002556B4: BasePath.Set(basePath), Initialize(reader)
    ResourceLoader(DataReader& reader, const AsciiString& basePath);
    // Binary copy ctor @ 0x00255B84: memberwise copy of m_flag, m_BasePath, m_Data, m_Children
    ~ResourceLoader();

    // Binary Initialize(DataReader&) @ 0x002554EC: HBR0 parser (skip, childCount, children, typeIds, rawSize, data)
    void Initialize(DataReader& reader);
    // Port convenience: wraps data+size in a VectorDataReader and calls Initialize(DataReader&)
    void Initialize(const uint8_t* data, size_t dataSize);

    const AsciiString& BasePathGet() const { return m_BasePath; }
    void BasePathSet(const AsciiString& path) { m_BasePath = path; }

    size_t DataSize() const { return m_Data.size(); }
    const uint8_t* DataPtr() const { return m_Data.data(); }
    size_t ChildCount() const { return m_Children.size(); }

#if !defined(__bada__)
    // Sequential read methods (port specific: use m_ReadPos cursor).
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
#endif // !defined(__bada__)

    // ---- binary symbol map ----
    // Binary @ 0x002554A0 -- ~ResourceLoader(): destroy m_Children, m_Data, m_BasePath (reverse-decl order == implicit member dtors)
    // Binary @ 0x001b45bc -- ReadBytes(void*, unsigned long): memcpy from m_Data[cursor] (binary cursor == m_flag@+0x00; port uses m_ReadPos)
    // Binary @ 0x001b45e0 -- ReadString(): Read<u16> length, Resize buffer, ReadBytes into AsciiString
    // Binary @ 0x001b46d0 -- ReadSubResourceLookup(): Read<u32> 1-based index, ConvertFromLittle, return &m_Children[index-1]
    // ---- end binary symbol map ----
};

} // namespace Mortar

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(Mortar::ResourceLoader)                   == 0x44, "ResourceLoader size mismatch");
static_assert(offsetof(Mortar::ResourceLoader, m_flag)        == 0x00, "ResourceLoader::m_flag offset");
static_assert(offsetof(Mortar::ResourceLoader, m_BasePath)    == 0x04, "ResourceLoader::m_BasePath offset");
static_assert(offsetof(Mortar::ResourceLoader, m_Data)        == 0x2C, "ResourceLoader::m_Data offset");
static_assert(offsetof(Mortar::ResourceLoader, m_Children)    == 0x38, "ResourceLoader::m_Children offset");
#endif

#endif
