#ifndef MORTAR_RESOURCE_LOADER_H
#define MORTAR_RESOURCE_LOADER_H

#include "util/AsciiString.h"
#include "util/SmartPtr.h"
#include "util/ReferenceCounter.h"
#include "util/Delegate.h"
#include "util/Endian.h"
#include "asset/DataReader.h"
#include "asset/Skeleton.h"
#include "math/_Vector3.h"
#include <vector>
#include <map>
#include <cstdint>
#include <cstring>

namespace Mortar {

// ============================================================
// ResourceLoader loader-dispatch machinery
// v1.6.1 MeshManager::LoadMeshInternal @0x00238644:
//   Calls RegisterLoader<IVertexStream>, RegisterLoader<IIndexStream>,
//   RegisterLoader<Model>, RegisterLoader<Mesh>, then Load<Model>(path).
//
// GetLoaders @0x0023c89c / RegisterLoader @0x0023de70 / Load<Model> @0x0023e80c
// ============================================================

// LoaderHelperBase: polymorphic loader dispatch node.
// Binary LoaderHelper +0x00 = vptr, +0x04 = Delegate1<SmartPtr<T>,ResourceLoader&> (0x20 bytes),
// +0x24 = bool owns-flag. Virtual slot 0 = LoadResource (pure).
// v1.6.1 binary @ 0x0023c89c area (GetLoaders).
class ResourceLoader;

class LoaderHelperBase {
public:
    virtual ~LoaderHelperBase() {}
    // v1.6.1 LoaderHelper::LoadResource: invokes delegate(rl), returns SmartPtr<ReferenceCounter>.
    virtual SmartPtr<ReferenceCounter> LoadResource(ResourceLoader& rl) = 0;
};

// LoaderHelper<T>: concrete LoaderHelperBase that holds a free-function delegate
// returning SmartPtr<T>. T must derive from ReferenceCounter.
// Binary: struct LoaderHelper<T> { vptr, Delegate1<SmartPtr<T>,ResourceLoader&>, bool }.
template<typename T>
class LoaderHelper : public LoaderHelperBase {
public:
    // The delegate holds a free function: SmartPtr<T>(*)(ResourceLoader&)
    Delegate1<SmartPtr<T>, ResourceLoader&> m_Delegate;

    explicit LoaderHelper(Delegate1<SmartPtr<T>, ResourceLoader&> d)
        : m_Delegate(d) {}

    virtual SmartPtr<ReferenceCounter> LoadResource(ResourceLoader& rl) {
        SmartPtr<T> result = m_Delegate(rl);
        // Upcast: T derives from ReferenceCounter.
        return SmartPtr<ReferenceCounter>(result.Get());
    }
};

// ConstFreeAutoPtr<T>: owning pointer that deletes on Reset/dtor/re-assign.
// Binary equivalent: used as the map value type in GetLoaders' s_loaders.
// Non-copyable; default-constructible (null); assignable from raw T*.
// std::map::operator[] default-constructs the value, then we assign via operator=(T*).
template<typename T>
class ConstFreeAutoPtr {
public:
    ConstFreeAutoPtr() : m_ptr(0) {}
    ~ConstFreeAutoPtr() { delete m_ptr; }

    void Reset() { delete m_ptr; m_ptr = 0; }

    ConstFreeAutoPtr& operator=(T* p) {
        if (m_ptr != p) {
            delete m_ptr;
            m_ptr = p;
        }
        return *this;
    }

    T*       Get()       { return m_ptr; }
    const T* Get() const { return m_ptr; }
    operator bool() const { return m_ptr != 0; }

private:
    // Non-copyable.
    ConstFreeAutoPtr(const ConstFreeAutoPtr&);
    ConstFreeAutoPtr& operator=(const ConstFreeAutoPtr&);

    T* m_ptr;
};

// LoaderTypeId<T>: maps each registered type to a stable uint32_t ID.
// DIFFERS: binary TypeInfo<T>::ID are runtime-counter-assigned (non-deterministic);
//   port hand-assigns stable IDs via enum to avoid non-determinism.
// v1.6.1 TypeInfo<T>::ID: monotonic counter assigned at static-init time.
enum LoaderTypeEnum {
    TYPEID_IVertexStream  = 1,
    TYPEID_IIndexStream   = 2,
    TYPEID_Model          = 3,
    TYPEID_Mesh           = 4,
    TYPEID_AnimationList  = 5
};

// Forward declarations for registered types.
class IVertexStream;
class IIndexStream;
class Model;
class Mesh;
class AnimationList;

template<typename T> struct LoaderTypeId;
template<> struct LoaderTypeId<IVertexStream>  { static const uint32_t value = TYPEID_IVertexStream; };
template<> struct LoaderTypeId<IIndexStream>   { static const uint32_t value = TYPEID_IIndexStream; };
template<> struct LoaderTypeId<Model>          { static const uint32_t value = TYPEID_Model; };
template<> struct LoaderTypeId<Mesh>           { static const uint32_t value = TYPEID_Mesh; };
template<> struct LoaderTypeId<AnimationList>  { static const uint32_t value = TYPEID_AnimationList; };

#if defined(FN_BIG_ENDIAN)
// Port specific: compile-time size-dispatch tag for ResourceLoader::Read<T>'s
// byteswap. No binary counterpart -- exists solely to select the right
// fnByteSwap* overload (or no-op for 1-byte T) at compile time without
// needing C++11 if-constexpr (GCC 4.4.1 cross-build has none).
template<int Size> struct IntToType {};

inline void ByteSwapInPlace(uint8_t&, IntToType<1>) { /* no-op: single byte */ }
inline void ByteSwapInPlace(int8_t&, IntToType<1>) { /* no-op: single byte */ }

inline void ByteSwapInPlace(uint16_t& v, IntToType<2>) { v = Endian::fnByteSwap16(v); }
inline void ByteSwapInPlace(int16_t& v, IntToType<2>) {
    v = (int16_t)Endian::fnByteSwap16((uint16_t)v);
}

inline void ByteSwapInPlace(uint32_t& v, IntToType<4>) { v = Endian::fnByteSwap32(v); }
inline void ByteSwapInPlace(int32_t& v, IntToType<4>) {
    v = (int32_t)Endian::fnByteSwap32((uint32_t)v);
}
inline void ByteSwapInPlace(float& v, IntToType<4>) { v = Endian::fnByteSwapFloat(v); }

inline void ByteSwapInPlace(uint64_t& v, IntToType<8>) { v = Endian::fnByteSwap64(v); }
inline void ByteSwapInPlace(int64_t& v, IntToType<8>) {
    v = (int64_t)Endian::fnByteSwap64((uint64_t)v);
}
inline void ByteSwapInPlace(double& v, IntToType<8>) {
    uint64_t bits;
    memcpy(&bits, &v, sizeof(bits));
    bits = Endian::fnByteSwap64(bits);
    memcpy(&v, &bits, sizeof(v));
}
#endif // FN_BIG_ENDIAN

// ============================================================
// ResourceLoader (68 bytes = 0x44)
// Binary layout confirmed via ctor @ 0x002556B4 / path ctor @ 0x00255730:
//   +0x00  int32_t               m_ReadCursor (sequential read cursor; zero-inited by ctor)
//   +0x04  AsciiString           m_BasePath   (40 bytes)
//   +0x2C  vector<uint8_t>       m_Data       (12 bytes; raw payload set by Initialize)
//   +0x38  vector<ResourceLoader> m_Children  (12 bytes; nested children)
//   +0x44  end (sizeof = 68)
// ASM-spec v1.6.1 ResourceLoader::ReadBytes @ 0x00255398:
//   if(count==0)return; memcpy(dest, m_Data.begin()+m_ReadCursor, count); m_ReadCursor += count.
//   Cursor IS the +0x00 member (Ghidra: m_ReadCursor). All Read<T>/ReadString(=Read<AsciiString>)/
//   ReadSubResourceLookup funnel through ReadBytes.
class ResourceLoader {
public:
    int32_t m_ReadCursor;                   // +0x00 (sequential read cursor; zero-inited by ctor)
    AsciiString m_BasePath;                 // +0x04 (base path for resolving refs)
    std::vector<uint8_t> m_Data;            // +0x2C (raw data bytes)
    std::vector<ResourceLoader> m_Children; // +0x38 (nested child loaders)

    ResourceLoader();
    ResourceLoader(const char* filePath);
    // Binary ctor @ 0x00255730: PathGetParent(path)->BasePath, FileDataReader(path), Initialize(reader)
    ResourceLoader(const AsciiString& filePath);
    // Binary ctor @ 0x002556B4: m_ReadCursor=0 first, BasePath.Set(basePath), Initialize(reader)
    ResourceLoader(DataReader& reader, const AsciiString& basePath);
    // v1.6.1 ResourceLoader::ResourceLoader(ResourceLoader const&) @0x00255b84: memberwise copy
    // (compiler-generated, emitted out-of-line); declared explicitly so the symbol exists for asm-verify.
    ResourceLoader(const ResourceLoader& other);
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

    // Port specific: FN_BIG_ENDIAN byteswap. The binary is little-endian-only
    // (ARM Bada); this template has no swap on that target. On the Wii port
    // (big-endian PowerPC) every multi-byte scalar Read<T>() pulls off disk
    // (string-length prefixes, counts, indices, floats) must be byteswapped
    // here -- this is the single choke point every ResourceLoader-driven
    // reader (LoadModel/LoadMesh/ReadSkeleton/ReadString/ReadSubResourceLookup)
    // funnels through, so fixing it here fixes all of them. Dispatches on
    // sizeof(T) at compile time; 1-byte T (uint8_t) and non-scalar T are
    // left untouched. Only scalar T (uint8/16/32_t, int32_t, float) are ever
    // instantiated -- see call sites in AnimationList.cpp / MeshManager.cpp.
    template<typename T>
    T Read() {
        T val;
        memcpy(&val, &m_Data[m_ReadCursor], sizeof(T));
        m_ReadCursor += sizeof(T);
#if defined(FN_BIG_ENDIAN)
        ByteSwapInPlace(val, IntToType<sizeof(T)>());
#endif
        return val;
    }

    // Binary @ 0x00255398 -- ReadBytes(void*, unsigned long)
    void ReadBytes(void* dest, unsigned long count);
    // Binary @ 0x002553cc -- ReadSubResourceLookup(): Read<u32> 1-based index, ConvertFromLittle, return &m_Children[index-1]
    ResourceLoader* ReadSubResourceLookup();
    // ReadString funnels through ReadBytes (Read<AsciiString> equivalent)
    AsciiString ReadString();

    void ResetReadPos() { m_ReadCursor = 0; }

    // Matches Read<Mortar::Skeleton> (called from LoadModel at 0x001a8468)
    // Reads skeleton from rawData: boneCount(u32) + per-bone:
    //   AsciiString(name) + long(4,parentIndex) + float[16](bindPose) +
    //   float[3](localTranslation) + float[4](localRotation) + float[9](localScale)
    // Fixed per-bone size: 4 + 64 + 12 + 16 + 36 = 132 bytes + variable name length.
    void ReadSkeleton(Skeleton& outSkeleton) {
        if (m_ReadCursor + 4 > (int32_t)m_Data.size()) return;
        uint32_t boneCount = Read<uint32_t>();
        if (boneCount == 0 || boneCount > 1024) return;

        std::vector<Skeleton::Bone> bones(boneCount);
        for (uint32_t i = 0; i < boneCount && m_ReadCursor < (int32_t)m_Data.size(); i++) {
            if (m_ReadCursor + 2 > (int32_t)m_Data.size()) return;
            AsciiString boneName = ReadString();
            bones[i].m_Name = boneName.CStr();

            if (m_ReadCursor + 4 > (int32_t)m_Data.size()) return;
            bones[i].m_ParentIndex = (int)Read<int32_t>();   // long = 4 bytes

            if (m_ReadCursor + 64 > (int32_t)m_Data.size()) return;
            ReadBytes(bones[i].m_BindPoseMat, 64);           // float[16]

            if (m_ReadCursor + 12 > (int32_t)m_Data.size()) return;
            ReadBytes(bones[i].m_LocalTranslation, 12);      // float[3]

            if (m_ReadCursor + 16 > (int32_t)m_Data.size()) return;
            ReadBytes(bones[i].m_LocalRotation, 16);         // float[4]

            if (m_ReadCursor + 36 > (int32_t)m_Data.size()) return;
            ReadBytes(bones[i].m_LocalScale, 36);            // float[9]
        }
        outSkeleton.Swap(bones);
    }

    // Skip Skeleton data without storing (legacy -- superseded by ReadSkeleton).
    void SkipSkeleton() {
        if (m_ReadCursor + 4 > (int32_t)m_Data.size()) return;
        uint32_t boneCount = Read<uint32_t>();
        for (uint32_t i = 0; i < boneCount && m_ReadCursor < (int32_t)m_Data.size(); i++) {
            if (m_ReadCursor + 2 > (int32_t)m_Data.size()) return;
            uint16_t nameLen = Read<uint16_t>();
            if (m_ReadCursor + nameLen > (int32_t)m_Data.size()) return;
            m_ReadCursor += nameLen;
            // Skip: parent(4) + matrix44(64) + vec3(12) + quat(16) + mat3(36) = 132 bytes
            if (m_ReadCursor + 132 > (int32_t)m_Data.size()) return;
            m_ReadCursor += 132;
        }
    }

    // ---- binary symbol map ----
    // Binary @ 0x002554A0 -- ~ResourceLoader(): destroy m_Children, m_Data, m_BasePath (reverse-decl order == implicit member dtors)
    // ---- end binary symbol map ----

    // ---- loader dispatch machinery ----
    // v1.6.1 GetLoaders @0x0023c89c: returns the static s_loaders map.
    // DIFFERS: binary guards GetLoaders with m_loadersCriticalSection (port single-threaded).
    // DIFFERS: map value is LoaderHelperBase* (raw) rather than ConstFreeAutoPtr<LoaderHelperBase>
    //   because std::map requires CopyConstructible values; ownership is managed manually
    //   in RegisterLoader (delete old before inserting new).
    static std::map<uint32_t, LoaderHelperBase*>& GetLoaders();

    // v1.6.1 RegisterLoader @0x0023de70: installs a typed loader delegate.
    // Explicitly instantiated in ResourceLoader.cpp for IVertexStream, IIndexStream, Model, Mesh.
    template<typename T>
    static void RegisterLoader(Delegate1<SmartPtr<T>, ResourceLoader&> d);

    // v1.6.1 Load<T>(ResourceLoader&): dispatches through the registered helper.
    // Explicitly instantiated in ResourceLoader.cpp for IVertexStream, IIndexStream, Model, Mesh.
    template<typename T>
    SmartPtr<T> Load();

    // v1.6.1 Load<Model>(path) @0x0023e80c: opens a FileDataReader, builds a child
    // ResourceLoader over PathGetParent(path), then dispatches Load<Model>().
    SmartPtr<Model> LoadModel(const AsciiString& path);
};

// Read<AsciiString> specialization: routes to ReadString() so callers can use
// rl.Read<AsciiString>() without hitting the generic memcpy path.
// Binary calls ReadString via ResourceLoader::Read<AsciiString> at LoadAnims @0x0026f3fc.
template<>
inline AsciiString ResourceLoader::Read<AsciiString>() {
    return ReadString();
}

// SmartPtrCast<U>: downcast SmartPtr<ReferenceCounter> to SmartPtr<U> via static_cast.
// Used by ResourceLoader::Load<T>() to recover the typed pointer from the base result.
// Binary equivalent: SmartPtr<T>::SetPtrCast pattern in the dispatch path.
template<typename U>
inline SmartPtr<U> SmartPtrCast(const SmartPtr<ReferenceCounter>& sp) {
    return SmartPtr<U>(static_cast<U*>(sp.Get()));
}

// ---------------------------------------------------------------------------
// Binary chunk-stream reader free functions.
// These are DISTINCT from ResourceLoader::ReadString() (which reads uint16 length).
// These free functions read the format used by font binary chunk loading:
//   ReadString:   [uint32 len][bytes][\0]  — cursor advances by 4+len+1
//   ReadChunkHash:[uint32 len][bytes][\0]  — returns StringHash(bytes,len)
//   ReadFloat:    [4 bytes as float]       — cursor advances by 4
//   ReadVec3:     [12 bytes as 3 floats]   — cursor advances by 12
//
// ASM-spec v1.6.1:
//   Mortar::ReadString    @0x002381e0
//   Mortar::ReadChunkHash @0x0023819c
//   Mortar::ReadFloat     @0x00238250
//   Mortar::ReadVec3      @0x00238260
// ---------------------------------------------------------------------------
AsciiString ReadString(unsigned char** cursor);
uint32_t    ReadChunkHash(unsigned char** cursor);
float       ReadFloat(unsigned char** cursor);
_Vector3<float> ReadVec3(unsigned char** cursor);

} // namespace Mortar

#if defined(__bada__)
#include <cstddef>
static_assert(sizeof(Mortar::ResourceLoader)                        == 0x44, "ResourceLoader size mismatch");
static_assert(offsetof(Mortar::ResourceLoader, m_ReadCursor)        == 0x00, "ResourceLoader::m_ReadCursor offset");
static_assert(offsetof(Mortar::ResourceLoader, m_BasePath)          == 0x04, "ResourceLoader::m_BasePath offset");
static_assert(offsetof(Mortar::ResourceLoader, m_Data)              == 0x2C, "ResourceLoader::m_Data offset");
static_assert(offsetof(Mortar::ResourceLoader, m_Children)          == 0x38, "ResourceLoader::m_Children offset");
#endif

#endif
