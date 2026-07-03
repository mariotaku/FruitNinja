#include "asset/ResourceLoader.h"
#include "asset/DataReader.h"
#include "asset/FileDataReader.h"
#include "asset/VectorDataReader.h"
#include "asset/File.h"
#include "asset/IStreamTypes.h"
#include "asset/Mesh.h"
#include "asset/Model.h"
#include "util/PathFunctions.h"
#include "util/StringHash.h"
#include "debug/Logger.h"
#include <cstring>

namespace Mortar {

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

// v1.6.1 ResourceLoader::ResourceLoader(ResourceLoader const&) @0x00255b84: pure memberwise copy
// of m_ReadCursor, m_BasePath, m_Data, m_Children -- no refcount/side-effects.
ResourceLoader::ResourceLoader(const ResourceLoader& other)
    : m_ReadCursor(other.m_ReadCursor), m_BasePath(other.m_BasePath), m_Data(other.m_Data), m_Children(other.m_Children)
{
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

// ============================================================
// ResourceLoader loader-dispatch machinery
// v1.6.1 GetLoaders @0x0023c89c / RegisterLoader @0x0023de70 / Load<Model> @0x0023e80c
// ============================================================

// v1.6.1 GetLoaders @0x0023c89c: function-local static s_loaders map.
// DIFFERS: binary guards with m_loadersCriticalSection (port single-threaded).
std::map<uint32_t, LoaderHelperBase*>& ResourceLoader::GetLoaders()
{
    // Function-local static: initialized on first call, persists for process lifetime.
    // Binary mirrors this with a global static guarded by a critical section.
    static std::map<uint32_t, LoaderHelperBase*> s_loaders;
    return s_loaders;
}

// v1.6.1 RegisterLoader @0x0023de70 (template body; explicit instantiations below).
// Installs a delegate for type T. Deletes any previous helper at the same key.
template<typename T>
void ResourceLoader::RegisterLoader(Delegate1<SmartPtr<T>, ResourceLoader&> d)
{
    std::map<uint32_t, LoaderHelperBase*>& loaders = GetLoaders();
    uint32_t key = LoaderTypeId<T>::value;
    std::map<uint32_t, LoaderHelperBase*>::iterator it = loaders.find(key);
    if (it != loaders.end()) {
        delete it->second;
        it->second = new LoaderHelper<T>(d);
    } else {
        loaders[key] = new LoaderHelper<T>(d);
    }
}

// v1.6.1 Load<T> (template body; explicit instantiations below).
// Looks up the registered helper by type ID and invokes LoadResource(*this).
// Returns empty SmartPtr<T> if no loader registered for T.
template<typename T>
SmartPtr<T> ResourceLoader::Load()
{
    std::map<uint32_t, LoaderHelperBase*>& loaders = GetLoaders();
    uint32_t key = LoaderTypeId<T>::value;
    std::map<uint32_t, LoaderHelperBase*>::iterator it = loaders.find(key);
    if (it == loaders.end() || it->second == 0) {
        return SmartPtr<T>();
    }
    SmartPtr<ReferenceCounter> base = it->second->LoadResource(*this);
    return SmartPtrCast<T>(base);
}

// v1.6.1 Load<Model>(path) @0x0023e80c: opens the file, constructs a child ResourceLoader
// with PathGetParent(path) as the base path, then dispatches Load<Model>().
SmartPtr<Model> ResourceLoader::LoadModel(const AsciiString& path)
{
    ResourceLoader child(path);
    return child.Load<Model>();
}

// Explicit instantiations for the four registered types.
// These emit T-symbols in this TU so RegisterLoader / Load are linkable from
// MeshManager.cpp and the cross-build can match them.
template void       ResourceLoader::RegisterLoader<IVertexStream>(Delegate1<SmartPtr<IVertexStream>, ResourceLoader&>);
template void       ResourceLoader::RegisterLoader<IIndexStream> (Delegate1<SmartPtr<IIndexStream>,  ResourceLoader&>);
template void       ResourceLoader::RegisterLoader<Model>        (Delegate1<SmartPtr<Model>,         ResourceLoader&>);
template void       ResourceLoader::RegisterLoader<Mesh>         (Delegate1<SmartPtr<Mesh>,          ResourceLoader&>);

template SmartPtr<IVertexStream> ResourceLoader::Load<IVertexStream>();
template SmartPtr<IIndexStream>  ResourceLoader::Load<IIndexStream>();
template SmartPtr<Model>         ResourceLoader::Load<Model>();
template SmartPtr<Mesh>          ResourceLoader::Load<Mesh>();

// ---------------------------------------------------------------------------
// Binary chunk-stream reader free functions.
// These read a different stream format than ResourceLoader::ReadString()
// (which reads uint16 length prefix, no trailing null).
// ---------------------------------------------------------------------------

// ASM-spec v1.6.1 Mortar::ReadString @0x002381e0
// Stream: [uint32 len][len bytes][\0]  -- cursor advances by 4 + len + 1.
// Length is clamped to 511 (binary uses 512-byte stack buffer).
AsciiString ReadString(unsigned char** cursor) {
    unsigned char* p = *cursor;
    uint32_t len = *(uint32_t*)p;
    p += 4;                         // advance past 4-byte length
    if (len > 511) len = 511;
    char buf[512];
    memcpy(buf, p, len);
    buf[len] = '\0';
    *cursor = p + len + 1;          // advance past string bytes + null terminator
    return AsciiString(buf);
}

// ASM-spec v1.6.1 Mortar::ReadChunkHash @0x0023819c
// Stream: [uint32 len][len bytes][\0].
// If len >= 101, returns 0 (sanity guard). Otherwise returns StringHash(bytes, len).
uint32_t ReadChunkHash(unsigned char** cursor) {
    uint32_t len = **(uint32_t**)cursor;
    if (len >= 101) return 0;
    unsigned char* p = *(unsigned char**)cursor + 4;  // skip uint32 length
    *(unsigned char**)cursor = p;                     // advance past length
    uint32_t hash = StringHash((const char*)p, (int)len);
    *(unsigned char**)cursor = p + len + 1;           // advance past string + null
    return hash;
}

// ASM-spec v1.6.1 Mortar::ReadFloat @0x00238250
// Reads 4 bytes as float (plain VFP load); advances cursor by 4.
// Decompile was misleading (Ghidra missed VFP hard-float ABI return in s0);
// the disassembly confirms: vldmia r3!, {s0}; str r3, [r0].
float ReadFloat(unsigned char** cursor) {
    float* p = (float*)*cursor;
    float val = *p;
    *cursor = (unsigned char*)(p + 1);  // advance by sizeof(float) = 4
    return val;
}

// ASM-spec v1.6.1 Mortar::ReadVec3 @0x00238260
// Reads 12 bytes as 3 consecutive floats (x, y, z); advances cursor by 12.
Vec3 ReadVec3(unsigned char** cursor) {
    unsigned char* p = *cursor;
    *cursor = p + 12;
    Vec3 out;
    out.x = *(float*)(p + 0);
    out.y = *(float*)(p + 4);
    out.z = *(float*)(p + 8);
    return out;
}

} // namespace Mortar
