#ifndef MORTAR_TEXTURE_H
#define MORTAR_TEXTURE_H

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "asset/TextureSource.h"
#include "render/gl_funcs.h"
#include "math/_Vector3.h"
#include <string>
#include <vector>

// Vec2f forward (needed by Texture2D's UV vectors)
#ifndef MORTAR_VEC2F_DEFINED
#define MORTAR_VEC2F_DEFINED
namespace Mortar {
struct Vec2f { float x, y; Vec2f() : x(0), y(0) {} Vec2f(float x_, float y_) : x(x_), y(y_) {} };
}
#endif

namespace Mortar {

// Mortar::Texture — 56 bytes (0x38).
// MI: ReferenceCounter (primary @+0x00) + DeviceResource/AutoInstanceList (secondary @+0x30).
// Binary base ctor @0x00268ea4.
//
// Binary layout:
//   +0x00  void*  vptr              (primary vtable)
//   +0x04  int    m_RefCount        (ReferenceCounter strong; ctor=0)
//   +0x08  void*  m_WeakData        (ReferenceCounter weak; ctor=0)
//   +0x0C  TextureInfo::DataInfo m_DataInfo (32B, occupies +0x0C..+0x2C)
//           DataInfo +0x18 = apparentWidth  (Texture+0x24)  -- MissControl ldr r2,[texptr,#0x24]
//           DataInfo +0x1C = apparentHeight (Texture+0x28)
//   +0x2C  bool   m_HasAlpha        (ctor=true)
//   +0x2D  3 pad
//   +0x30  void*  m_DeviceResVptr   (secondary DeviceResource/AutoInstanceList base vptr placeholder)
//   +0x34  void*  m_RegistryNext    (AutoInstanceList intrusive next; ctor=0)
//   +0x38  end (sizeof==56)
//
// Vtable slot order (shared by subclasses, primary vtable):
//   0  dtor(in-place)  1  dtor(deleting)  2  GetRefCounter  3  Set
//   4  UnSet(bool)     5  GetHash         6  Debug_ToString  7  GetUVMeshID
class Texture : public Mortar::ReferenceCounter {
public:
    // Binary ctor @0x00268ea4: calls ReferenceCounter ctor, inits DataInfo, m_HasAlpha=true,
    // m_DeviceResVptr=0, m_RegistryNext=0, pushes onto AutoInstanceList.
    Texture();
    // Vtable slot 0/1.
    virtual ~Texture();

    // Vtable slot 3 @0x00229710 (Texture2D_Bada::Set).
    // Bind this texture to GL_TEXTURE_2D on unit GL_TEXTURE0.
    virtual void Set();

    // Vtable slot 4 @0x002296ac (Texture2D_Bada::UnSet).
    // Unbind (bind texture 0).
    virtual void UnSet(bool flag = false);

    // Vtable slot 5 @0x0022964c.
    virtual unsigned int GetHash() const { return 0; }

    // Vtable slot 6 @0x0022a8d0.
    virtual const char* Debug_ToString() { return "Texture"; }

    // Vtable slot 7 @0x0022a8b4 -- returns UVMesh ID (stub: 0).
    virtual unsigned int GetUVMeshID() const { return 0; }

    // Accessor: apparent pixel width (reads DataInfo::apparentWidth at Texture+0x24).
    int GetWidth()  const { return (int)m_DataInfo.apparentWidth; }
    // Accessor: apparent pixel height (reads DataInfo::apparentHeight at Texture+0x28).
    int GetHeight() const { return (int)m_DataInfo.apparentHeight; }

    // Virtual accessor for GL texture id: overridden by Texture2D_Bada to return m_TexId.
    virtual GLuint GetTexId() const { return 0; }

    // Load a .tex file from disk, create GL texture.
    // Returns SmartPtr (null on failure). Binary @0x0022a854 (Texture2D::Load).
    static Mortar::SmartPtr<Texture> Load(const char* path);

    // Binary @0x00189d80 -- create Texture from memory buffer (ptr,len).
    static Mortar::SmartPtr<Texture> LoadFromMemory(void const* buf, int len);

    // v1.6.1 addition: AlternativeTextureLoader path-rewrite toggle.
    // Binary: bool global @ data segment, default false.
    static bool UseAlternativeTextureLoader;

    // Binary @0x00188da4 -- cache-gated bind: forwards to Set().
    void SetUnCached();
    // Binary @0x00188d9c -- uncached unbind: forwards to UnSet().
    void UnSetUnCached();

    // Called by Texture2D_Bada::Cache() to fill the DataInfo dims.
    // Also used by UploadTex1ToGL to set apparentWidth/Height after pixel upload.
    void SetDimensions(int w, int h) {
        m_DataInfo.apparentWidth  = (uint32_t)w;
        m_DataInfo.apparentHeight = (uint32_t)h;
    }

    // +0x0C: DataInfo (32B, see TextureSource.h).
    TextureInfo::DataInfo m_DataInfo;
    // +0x2C: alpha flag (ctor=true).
    bool m_HasAlpha;

    // Mirrors binary global Bada::g__CurrentlySetTexture, read by
    // Mesh::DrawQuadUnCached @0x00240a70 to decide the opaque-fast-path blend
    // disable (tint.a==255 && (!tex || !tex->m_HasAlpha)). Set to `this` in
    // Set(), cleared to NULL wherever the texture is unbound / bind fails.
    // A static member doesn't affect sizeof(Texture)/offsets, so unlike
    // s_LastBoundTexId below this is declared unconditionally -- it stands in
    // for real binary global state and must stay reachable under __bada__
    // cross-builds too.
    static Mortar::Texture* s_CurrentlySetTexture;

    // Port specific: file path for debugging/reload.
    // The binary keeps the source path on TextureSource/TextureLoader (+0x1c),
    // not on the Texture itself. This field is a port convenience only; it is
    // NOT present in the binary Mortar::Texture layout (sizeof stays 56 because
    // std::string is host-only; the cross-build sees only the __bada__ layout).
#if !defined(__bada__)
    std::string m_Path;
#endif

    // Last-bound texId tracker (port-side; binary doesn't track this globally).
    // Like s_CurrentlySetTexture above, a static member doesn't affect
    // sizeof(Texture)/offsets, so it is declared unconditionally -- the ES2
    // Renderer::DrawQuad reads it outside any __bada__ guard and the
    // cross-build must still compile.
    static GLuint s_LastBoundTexId;

    // Secondary DeviceResource/AutoInstanceList base vptr placeholder (+0x30).
    // DIFFERS: original = pointer to DeviceResource vtable (secondary MI base);
    //   port = void* placeholder at the correct offset; the DeviceResource register/
    //   unregister calls in ctor/dtor are no-ops (Defunct: DeviceResource registry).
    void* m_DeviceResVptr;
    // AutoInstanceList intrusive next pointer (+0x34; ctor=0).
    void* m_RegistryNext;

private:
    static Mortar::SmartPtr<Texture> ParseTexBuffer(const void* data, long size,
                                                    const char* pathForLog);
    static Mortar::SmartPtr<Texture> ParseTex3Buffer(const void* data, long size,
                                                     const char* pathForLog);
};

} // namespace Mortar

#if defined(__bada__)
#include <cstddef>
// All asserted members are public (matching the Texture2D/Texture2D_Bada blocks),
// so plain namespace-scope static_asserts work without a friend struct.
static_assert(sizeof(Mortar::Texture) == 0x38, "Mortar::Texture size mismatch");
static_assert(offsetof(Mortar::Texture, m_DataInfo)      == 0x0c, "Texture::m_DataInfo offset");
static_assert(offsetof(Mortar::Texture, m_HasAlpha)      == 0x2c, "Texture::m_HasAlpha offset");
static_assert(offsetof(Mortar::Texture, m_DeviceResVptr) == 0x30, "Texture::m_DeviceResVptr offset");
static_assert(offsetof(Mortar::Texture, m_RegistryNext)  == 0x34, "Texture::m_RegistryNext offset");
#endif

// ---------------------------------------------------------------------------
// Mortar::Texture2D — 84 bytes (0x54).
// Adds UVMesh MI base @+0x38. Binary ctor @0x00268d44.
//
// Binary layout:
//   +0x00  Texture super (56B)
//   +0x38  void*  m_MeshVptr         (UVMesh secondary base vptr placeholder)
//   +0x3C  std::vector<Vec2f>         m_UVVerts   (12B, std::vector layout)
//   +0x48  std::vector<unsigned short> m_Indices  (12B, std::vector layout)
//   +0x54  end (sizeof==84)
// ---------------------------------------------------------------------------
namespace Mortar {

class Texture2D : public Texture {
public:
    Texture2D();
    virtual ~Texture2D();

    // +0x38: UVMesh secondary base vptr placeholder.
    // DIFFERS: original = pointer to UVMesh vtable (secondary MI base);
    //   port = void* placeholder; UVMesh interface is not called in live render path.
    void* m_MeshVptr;
    // +0x3C: UV coordinate vectors.
    std::vector<Vec2f>           m_UVVerts;   // 12B
    std::vector<unsigned short>  m_Indices;   // 12B
};

} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::Texture2D) == 0x54, "Mortar::Texture2D size mismatch");
static_assert(offsetof(Mortar::Texture2D, m_MeshVptr) == 0x38, "Texture2D::m_MeshVptr offset");
static_assert(offsetof(Mortar::Texture2D, m_UVVerts)  == 0x3c, "Texture2D::m_UVVerts offset");
static_assert(offsetof(Mortar::Texture2D, m_Indices)  == 0x48, "Texture2D::m_Indices offset");
#endif

// ---------------------------------------------------------------------------
// Mortar::Bada::Texture2D_Bada — 100 bytes (0x64).
// Binary ctor @0x0022a7d8; dtor @0x00229b8c.
// Construction flow:
//   Load @0x0022a854 -> new(100) -> Texture2D_Bada ctor -> SetSource(src,param2)
//   SetSource @0x0022a7a8: ReleaseCache -> m_Source=src -> Cache()
//   Cache @0x0022a4f4: fill DataInfo dims/format, apparentW/H, UV verts/indices,
//                      glGenTextures, bind, filters, glTexImage2D.
//
// Binary layout:
//   +0x00  Texture2D super (84B)
//   +0x54  uint   m_PrimType    (Cache sets from src; e.g. GL_TRIANGLE_STRIP)
//   +0x58  GLuint m_TexId       (glGenTextures; ctor=0)
//   +0x5C  uint   m_Pad5c       (unused/pad)
//   +0x60  SmartPtr<TextureSource> m_Source (ctor=0; SetSource fills)
//   +0x64  end (sizeof==100)
// ---------------------------------------------------------------------------
namespace Mortar {
namespace Bada {

class Texture2D_Bada : public Texture2D {
public:
    // Port specific: no binary counterpart (used by the UploadTex1ToGL DIFFERS load path).
    Texture2D_Bada();
    // Binary ctor @0x0022a7d8: base ctors, m_TexId=0 etc. (as the default ctor above), then SetSource(src, param2).
    Texture2D_Bada(const Mortar::SmartPtr<TextureSource>& src, unsigned long param2);
    // Binary dtor @0x00229b8c (in-place) / @0x00229c04 (deleting).
    virtual ~Texture2D_Bada();

    // Vtable slot 3 @0x00229710 -- bind this GL texture.
    virtual void Set();
    // Vtable slot 4 @0x002296ac -- unbind.
    virtual void UnSet(bool flag = false);
    // Vtable slot 5 @0x0022964c -- return hash from source.
    virtual unsigned int GetHash() const;
    // Vtable slot 6 @0x0022a8d0 -- debug string.
    virtual const char* Debug_ToString();
    // Vtable slot 7 @0x0022a8b4 -- return UVMesh ID (stub: 0).
    virtual unsigned int GetUVMeshID() const;

    // Override: return m_TexId.
    virtual GLuint GetTexId() const { return m_TexId; }

    // SetSource @0x0022a7a8: ReleaseCache, set m_Source, call Cache.
    // param2: always 0, unused in v1.6.1 (Cache sets m_PrimType from source data).
    void SetSource(const Mortar::SmartPtr<TextureSource>& src, unsigned long param2);
    // Cache @0x0022a4f4: fill DataInfo, glGenTextures, upload pixels.
    void Cache();
    // ReleaseCache: delete GL texture, reset m_TexId.
    void ReleaseCache();

    // +0x54
    unsigned int m_PrimType;
    // +0x58
    GLuint m_TexId;
    // +0x5C
    unsigned int m_Pad5c;
    // +0x60
    Mortar::SmartPtr<TextureSource> m_Source;
};

} // namespace Bada
} // namespace Mortar

#if defined(__bada__)
static_assert(sizeof(Mortar::Bada::Texture2D_Bada) == 0x64, "Mortar::Bada::Texture2D_Bada size mismatch");
static_assert(offsetof(Mortar::Bada::Texture2D_Bada, m_PrimType) == 0x54, "Texture2D_Bada::m_PrimType offset");
static_assert(offsetof(Mortar::Bada::Texture2D_Bada, m_TexId)    == 0x58, "Texture2D_Bada::m_TexId offset");
static_assert(offsetof(Mortar::Bada::Texture2D_Bada, m_Pad5c)    == 0x5c, "Texture2D_Bada::m_Pad5c offset");
static_assert(offsetof(Mortar::Bada::Texture2D_Bada, m_Source)   == 0x60, "Texture2D_Bada::m_Source offset");
#endif

// ASM-spec v1.6.1 GetTextureScale1to1 @ 0x0014fa48:
//   Returns the scale vector for displaying a texture at 1:1 pixel resolution.
//   if tex valid: Vec3(tex->GetWidth(), tex->GetHeight(), 0). else: Vec3::Zero.
//   Binary multiplied/divided by 480/320 (compiler artefact = identity).
_Vector3<float> GetTextureScale1to1(Mortar::SmartPtr<Mortar::Texture> tex);

#endif // MORTAR_TEXTURE_H
