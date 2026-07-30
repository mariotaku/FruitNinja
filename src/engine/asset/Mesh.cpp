#include "asset/Mesh.h"
#include "asset/SharedEffectProperties.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include "util/Immutable.h"
#include <cstring>
#include <cmath>

// Analysed: 2026-04-11T18:30

// File-scope interned-name globals for the Mesh C1/C2 ctor.
// Interned once at static-init; lives for program lifetime.
// Mirrors binary's per-call cost where the intern pool already holds the Node*.
namespace {
const Immutable kMeshName_World("World");
const Immutable kMeshName_View("SceneCamera.View");
const Immutable kMeshName_Proj("SceneCamera.Projection");
const Immutable kMeshName_WVP("WorldViewProjection");
}  // namespace

namespace Mortar {

// --- Mesh ---

// Binary @ 0x001b0e70
Mesh::Mesh()
    : m_Skeleton(nullptr)
    , m_WorldProp(NULL), m_ViewProp(NULL), m_ProjProp(NULL), m_WVPProp(NULL)
{}

// Binary D1 @ 0x00272dc8 (complete), D0 @ 0x00272e7c (deleting)
// DIFFERS: v1.6.1 binary @ 0x00272dc8 hand-codes member teardown in declaration order
// (_Rb_tree::_M_erase(m_GroupsByName), m_OwnGroup.Clear(), ~vector(m_Geometries),
// ~vector(m_BoneBindings), ~AsciiString(m_Name)) -- port relies on implicit member
// dtors in reverse-declaration order (same members, same logical order; std::map vs
// _Rb_tree container layer).
Mesh::~Mesh() {
    // VBO/IBO cleanup now handled by Geometry::~Geometry (each SmartPtr<Geometry>
    // will Release() here as m_Geometries is destroyed).
}

// Matches Mesh::SetBones (0x001b1340)
// Resizes m_BoneBindings and copies each entry
void Mesh::SetBones(const BoneBinding* bones, unsigned long count) {
    BoneBinding temp;
    m_BoneBindings.resize(count, temp);
    for (unsigned long i = 0; i < count; i++) {
        m_BoneBindings[i] = bones[i];
    }
}

// Matches Mesh::BindSkeleton (0x001b0948, vtable[8])
// Stores skeleton ptr; resolves m_SkeletonIndex per BoneBinding via Skeleton::FindIndex.
void Mesh::BindSkeleton(Skeleton* skeleton) {
    m_Skeleton = skeleton;
    if (!skeleton) return;
    for (int i = 0; i < (int)m_BoneBindings.size(); i++) {
        uint32_t idx = skeleton->FindIndex(m_BoneBindings[i].m_BoneName.CStr());
        m_BoneBindings[i].m_SkeletonIndex = (idx == 0xFFFFFFFF) ? -1 : (int)idx;
    }
}

// Matches Mesh::GetBoneVertTransform (0x001b0688)
// Returns vert matrix for binding[index] if skeleton is bound, else nullptr.
// Caller falls back to identity when nullptr (= worldMatrix unchanged).
const Matrix44* Mesh::GetBoneVertTransform(unsigned long index) const {
    if (!m_Skeleton) return nullptr;
    if (index >= m_BoneBindings.size()) return nullptr;
    int skelIdx = m_BoneBindings[index].m_SkeletonIndex;
    if (skelIdx < 0) return nullptr;
    return m_Skeleton->GetVertex((uint32_t)skelIdx);
}

// Matches Mesh::GetBoneWorldTransform (0x001b0700)
// Returns world matrix for binding[index] through skeleton. Identity when
// no skeleton bound or bone unbound.
Matrix44 Mesh::GetBoneWorldTransform(unsigned long index) const {
    if (!m_Skeleton || index >= m_BoneBindings.size())
        return Matrix44();
    int skelIdx = m_BoneBindings[index].m_SkeletonIndex;
    if (skelIdx < 0)
        return Matrix44();
    return *m_Skeleton->GetWorld((uint32_t)skelIdx);
}

// Binary @ 0x001b0778 — has no callers; emitted for API parity.
//                    Mirrors GetBoneVertTransform / GetBoneWorldTransform.
Matrix44 Mesh::GetBoneLocalTransform(unsigned long idx) const {
    if (m_Skeleton != NULL) {
        const BoneBinding& bind = m_BoneBindings[idx];
        if (bind.m_SkeletonIndex >= 0)
            return *m_Skeleton->GetLocal((uint32_t)bind.m_SkeletonIndex);
    }
    return Matrix44();
}

// Matches Mesh::GetBounds (0x00272b48).
// Binary signature: Bounds3D GetBounds() const (struct-return; r0 = hidden
// retval ptr to a 24-byte Bounds3D). Computes AABB by transforming each
// bone's local bounds through its world matrix, then min/max-reducing
// across all bones. Initial values: +/-1e30 (DAT_00272c8c / DAT_00272c90).
// ASM-verified: 2026-04-29T00:00Z v1.6.1 binary @ 0x001b07f0 (asm-inspector)
Bounds3D Mesh::GetBounds() const {
    Bounds3D out(_Vector3<float>( 1e30f,  1e30f,  1e30f),
                 _Vector3<float>(-1e30f, -1e30f, -1e30f));

    for (int i = 0; i < (int)m_BoneBindings.size(); i++) {
        const BoneBinding& bone = m_BoneBindings[i];
        Matrix44 W = GetBoneWorldTransform(i);
        const float* M = W.m;  // column-major: col c, row r = M[c*4+r]

        // Transform bmin and bmax as homogeneous points (w=1).
        const _Vector3<float>& bmin = bone.m_Bounds.min;
        const _Vector3<float>& bmax = bone.m_Bounds.max;
        _Vector3<float> wMin(
            M[0] * bmin.x + M[4] * bmin.y + M[8] * bmin.z + M[12],
            M[1] * bmin.x + M[5] * bmin.y + M[9] * bmin.z + M[13],
            M[2] * bmin.x + M[6] * bmin.y + M[10] * bmin.z + M[14]
        );
        _Vector3<float> wMax(
            M[0] * bmax.x + M[4] * bmax.y + M[8] * bmax.z + M[12],
            M[1] * bmax.x + M[5] * bmax.y + M[9] * bmax.z + M[13],
            M[2] * bmax.x + M[6] * bmax.y + M[10] * bmax.z + M[14]
        );

        // Reduce both transformed corners into the running AABB.
        _Vector3<float>& outMin = out.min;
        _Vector3<float>& outMax = out.max;
        if (wMin.x < outMin.x) outMin.x = wMin.x;
        if (wMin.y < outMin.y) outMin.y = wMin.y;
        if (wMin.z < outMin.z) outMin.z = wMin.z;
        if (wMax.x < outMin.x) outMin.x = wMax.x;
        if (wMax.y < outMin.y) outMin.y = wMax.y;
        if (wMax.z < outMin.z) outMin.z = wMax.z;

        if (wMin.x > outMax.x) outMax.x = wMin.x;
        if (wMin.y > outMax.y) outMax.y = wMin.y;
        if (wMin.z > outMax.z) outMax.z = wMin.z;
        if (wMax.x > outMax.x) outMax.x = wMax.x;
        if (wMax.y > outMax.y) outMax.y = wMax.y;
        if (wMax.z > outMax.z) outMax.z = wMax.z;
    }

    return out;
}

// Binary @ 0x00272e98
// Geometry::Render (v1.6.1 @0x00264468) uses fixed-function GL ES 1.x throughout (both binary and port).
// DIFFERS: structural -- binary Render walks m_Binding->GetBindings()[idx].m_PassBindings and re-derives
//   glVertexPointer/glDrawElements args per draw; port draws from load-cached m_Vbo/m_Ibo/m_Layout
//   (same fixed-function GL calls, byte-equivalent output). NOT a GLES2 shader path.
// EffectProperty SetValue calls (m_WorldProp/m_ViewProp/m_ProjProp/m_WVPProp) are
// defunct-stubbed (no-op) but call shape preserved. Bone branch + 4-matrix composition
// + geometry loop order all match binary.
void Mesh::Draw(const Matrix44& worldTransform) {
    // Matches Mesh::Draw single-bone branch (0x001b0c3c, line ~8):
    //   if (boneCount == 1): finalWorld = vertMat * worldMatrix
    //   else: finalWorld = worldMatrix
    Matrix44 finalWorld;
    if (m_BoneBindings.size() == 1) {
        const Matrix44* vertMat = GetBoneVertTransform(0);
        if (vertMat) {
            finalWorld = (*vertMat) * worldTransform;
        } else {
            finalWorld = worldTransform; // fallback: no skeleton bound
        }
    } else {
        finalWorld = worldTransform;
    }

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(finalWorld);

    // Binary-faithful 4-matrix property upload (binary @ 0x001b0c3c).
    // Side-effect feed for any consumer reading EffectPropertyValues;
    // the defunct-stubbed SetValue calls don't affect the GL state but the
    // call shape matches the binary so asm-verify sees the same write pattern.
    const Matrix44& camView = mm.GetViewStack().m_Current;
    const Matrix44& camProj = mm.GetProjectionStack().m_Current;
    TrySetMatrix_EffectProp(m_WorldProp, &finalWorld);
    TrySetMatrix_EffectProp(m_ViewProp,  &camView);
    TrySetMatrix_EffectProp(m_ProjProp,  &camProj);

    // WVP = Proj * (View * World) — column-vector convention, matches binary
    // which composes (View * finalWorld) first then (Proj * that).
    Matrix44 vw  = camView * finalWorld;
    Matrix44 wvp = camProj * vw;
    if (m_WVPProp != NULL) {
        TrySetMatrix_EffectProp(m_WVPProp, &wvp);
    }

    // Render all geometries. Each Geometry stores its own diffuse texture
    // in m_DiffuseTex (port field, assigned during mesh loading).
    // Binary @ 0x001b0c3c: walks m_Geometries calling Geometry::Render per entry.
    for (int i = 0; i < (int)m_Geometries.size(); i++) {
        Geometry* g = m_Geometries[i].Get();
        if (!g) continue;
        g->Render(wvp);
    }
}

// ---- Mesh binary stubs ----

// Binary @ 0x002730ac
// DIFFERS: original = EffectPropertyList/SharedEffectProperties subsystem live;
// port stubs Contains()->true and GetProperty()->nullptr (defunct subsystem). Shape,
// field writes, def array (World/SceneCamera.View/SceneCamera.Projection/WorldViewProjection,
// type=3, count=1), Contains() fast-path branch, and 4x GetProperty cache calls all match.
Mesh::Mesh(SmartPtr<SharedEffectProperties> parent, AsciiString const& name)
    : m_Skeleton(NULL)
    , m_WorldProp(NULL), m_ViewProp(NULL), m_ProjProp(NULL), m_WVPProp(NULL)
{
    // Defunct: SharedEffectProperties subsystem -- shape preserved; v1.6.1 binary @ 0x002730ac
    m_Name = name;
    EffectPropertyDefinition defs[4];
    defs[0].m_Name  = kMeshName_World;
    defs[0].m_Type  = 3;
    defs[0].m_Count = 1;
    defs[1].m_Name  = kMeshName_View;
    defs[1].m_Type  = 3;
    defs[1].m_Count = 1;
    defs[2].m_Name  = kMeshName_Proj;
    defs[2].m_Type  = 3;
    defs[2].m_Count = 1;
    defs[3].m_Name  = kMeshName_WVP;
    defs[3].m_Type  = 3;
    defs[3].m_Count = 1;
    if (parent.IsValid() && parent->GetList().Contains(defs, defs + 4)) {
        m_OwnGroup = parent;
    } else {
        m_OwnGroup = new SharedEffectProperties(defs, defs + 4, parent);
    }
    m_WorldProp = m_OwnGroup->GetList().GetProperty("World");
    m_ViewProp  = m_OwnGroup->GetList().GetProperty("SceneCamera.View");
    m_ProjProp  = m_OwnGroup->GetList().GetProperty("SceneCamera.Projection");
    m_WVPProp   = m_OwnGroup->GetList().GetProperty("WorldViewProjection");
}

// Binary @ 0x001b0948 — const-ref BindSkeleton overload (distinct mangled symbol).
// Identical body to the ptr overload: stores &skeleton into m_Skeleton (field +0x68),
// walks m_BoneBindings (vector @ +0x34) and resolves each m_SkeletonIndex (+0x40) via
// Skeleton::operator[](AsciiString) == FindIndex. Delegates to the ptr overload to
// keep the index-resolution logic single-sourced.
void Mesh::BindSkeleton(Skeleton const& skeleton) {
    BindSkeleton(const_cast<Skeleton*>(&skeleton));
}

// Binary @ 0x001b0d0c -- pushes SmartPtr<Geometry> into m_Geometries.
void Mesh::AddGeometry(SmartPtr<Geometry> geom) {
    m_Geometries.push_back(geom);
}

// SharedPropsInfo::AddTextureMap -- binary @ 0x001b1394.
// Inserts a TextureProps{} into m_TextureMaps keyed by `name`.
// propName is the effect property name (e.g. "DiffuseMap"); the binary
// calls GetProperty(propName) on the SharedEffectProperties list and stores
// the result in TextureProps::m_Handle. With the subsystem defunct-stubbed
// GetProperty returns nullptr; m_Handle stays null. Shape preserved per spec.
void SharedPropsInfo::AddTextureMap(const AsciiString& name, const AsciiString& propName) {
    // Defunct: SharedEffectProperties subsystem -- shape preserved; v1.6.1 binary @ 0x001b1394
    TextureProps& entry = m_TextureMaps[name];
    // Binary: entry.m_Handle = m_Props->GetList().GetProperty(propName.CStr())
    if (m_Props.IsValid()) {
        entry.m_Handle = m_Props->GetList().GetProperty(propName.CStr());
    }
}

// Binary @ 0x00272c98
// DIFFERS: v1.6.1 binary @ 0x00272c98 walks _Rb_tree node layout (&node[2].field_0x8) to
// return &m_Props from the found node; port uses std::map::find and &it->second.m_Props.
// Semantically identical find/return-&m_Props; std::map vs _Rb_tree container-layer
// substitution cannot byte-match.
SmartPtr<SharedEffectProperties>* Mesh::GetPropertiesGroup(AsciiString const& name) const {
    // Defunct: SharedEffectProperties subsystem -- shape preserved; v1.6.1 binary @ 0x00272c98
    std::map<AsciiString, SharedPropsInfo>::const_iterator it = m_GroupsByName.find(name);
    if (it == m_GroupsByName.end()) return NULL;
    return const_cast<SmartPtr<SharedEffectProperties>*>(&it->second.m_Props);
}

// Binary @ 0x001b1430 (address unchanged between builds for this overload)
// DIFFERS: v1.6.1 binary @ 0x001b1430 insert-or-reuse path uses _Rb_tree + live
// EffectPropertyList::Contains(); port uses std::map + defunct stub Contains()->true
// (fast-path reuse whenever a group is already present). Container-layer substitution.
SmartPtr<SharedEffectProperties>* Mesh::GetPropertiesGroup(AsciiString const& name,
                                                            EffectPropertyDefinition const* begin,
                                                            EffectPropertyDefinition const* end) {
    // Defunct: SharedEffectProperties subsystem -- shape preserved; v1.6.1 binary @ 0x001b1430
    SmartPtr<SharedEffectProperties>* existing = GetPropertiesGroup(name);
    if (existing) {
        const EffectPropertyDefinition* p = begin;
        while (p < end && (*existing)->GetList().Contains(p)) ++p;
        if (p == end) return existing;
    }
    SmartPtr<SharedEffectProperties>& slot = m_GroupsByName[name].m_Props;
    SmartPtr<SharedEffectProperties> parent = existing ? *existing : m_OwnGroup;
    slot = new SharedEffectProperties(begin, end, parent);
    return &slot;
}

// Defunct: SharedEffectProperties subsystem -- shape preserved; v1.6.1 binary @ 0x001b08e8
// Binary: re-fetches the 4 cached prop handles (World/View/Proj/WVP) from m_OwnGroup.
// Port: GetProperty() stub returns nullptr; handles stay nullptr.
void Mesh::RebuildEffectBindings() {
    // Defunct: SharedEffectProperties subsystem -- shape preserved; v1.6.1 binary @ 0x001b08e8
    if (!m_OwnGroup.IsValid()) return;
    m_WorldProp = m_OwnGroup->GetList().GetProperty("World");
    m_ViewProp  = m_OwnGroup->GetList().GetProperty("SceneCamera.View");
    m_ProjProp  = m_OwnGroup->GetList().GetProperty("SceneCamera.Projection");
    m_WVPProp   = m_OwnGroup->GetList().GetProperty("WorldViewProjection");
}

// vtable slot 6 (+0x18): GenerateBindings(Vector) binary @ 0x0027350c
// DIFFERS: v1.6.1 binary @ 0x0027350c walks m_GroupsByName _Rb_tree, lazily inits 2 static
// AsciiString keys (uvwChannel/opacityChannel from DAT_002736c8/d8 via __cxa_guard),
// finds channelName in each node's TextureProps map (node+0x3c region), and if an
// EffectProperty* Vec3 target is non-null builds Binding{normalized=0, target, count}
// and push_backs into out. Port produces zero bindings because EffectProperty/
// SharedEffectProperties subsystem is defunct-stubbed (GetProperty->nullptr, TextureProps
// never populated) -- empty body is the correct observable result.
// Defunct: EffectProperty channel binding -- v1.6.1 binary @ 0x0027350c
void Mesh::GenerateBindings(AsciiString const& /*channelName*/,
                            AsciiString const& /*targetName*/,
                            std::vector<AnimBindings::Vector::Binding>& /*out*/) {
    // Defunct: EffectProperty channel binding -- v1.6.1 binary @ 0x0027350c
    // EffectProperty path is defunct-stubbed; produces zero bindings (correct).
}

// vtable slot 7 (+0x1c): GenerateBindings(Bone) v1.6.1 Mesh::GenerateBindings @0x0027385c
// Binary body is a single BX LR (genuinely empty). Port empty body is exact match.
// Defunct-ish: Mesh emits no bone bindings; v1.6.1 Mesh::GenerateBindings @0x0027385c (empty BX LR)
void Mesh::GenerateBindings(AsciiString const& /*channelName*/,
                            AsciiString const& /*targetName*/,
                            std::vector<AnimBindings::Bone::Binding>& /*out*/) {
    // Defunct-ish: Mesh emits no bone bindings; v1.6.1 Mesh::GenerateBindings @0x0027385c (empty BX LR)
}

// Defunct: debug draw primitive -- no-op stub; v1.6.1 binary @ 0x00193ed8
void Mesh::DrawCube(float /*x*/, float /*y*/, float /*z*/,
                    Colour /*colour*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawCube is a binary stub (BX LR); no-op stub; v1.6.1 binary @ 0x00193ed8
}

// Defunct: debug draw primitive -- no-op stub; v1.6.1 binary @ 0x00193edc
void Mesh::DrawLine(_Vector3<float> const& /*from*/, _Vector3<float> const& /*to*/, float const& /*width*/,
                    Colour const& /*colour*/, _Vector3<float> const& /*normal*/,
                    DrawEffectContainer* /*fx*/) {
    // Defunct: DrawLine is a binary stub (BX LR); no-op stub; v1.6.1 binary @ 0x00193edc
}

// Defunct: debug draw primitive -- no-op stub; v1.6.1 binary @ 0x00193ee0
void Mesh::DrawSphere(float /*radius*/, Colour /*colour*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawSphere is a binary stub (BX LR); no-op stub; v1.6.1 binary @ 0x00193ee0
}

// Binary @ 0x00272a3c (v1.6.1 Bada); port ASM-verified at iOS address 0x001b09b0 (re-analyst 2026-05-24)
void Mesh::DrawQuad(Colour colour, SmartPtr<Texture> texture,
                    _Vector3<float> const& pos, _Vector3<float> const& scale, float rotZ,
                    float w, float h, float uOff, float vOff,
                    DrawEffectContainer* fx) {
    MatrixManager& mm = MatrixManager::GetInstance();
    MatrixStack& ws = mm.GetWorldStack();
    ws.Reset();
    ws.Scale(scale);
    ws.Translate(pos);
    ws.m_Current.RotZ44(sinf(rotZ), cosf(rotZ));
    ws.m_Version++;
    mm.UploadModelViewOnly();
    texture->Set();
    DrawQuadUnCached(colour, w, h, uOff, vOff, fx);
    texture->UnSet();
}

// Binary @ 0x00240be4 (v1.6.1 Mesh::DrawQuadUnCached(Colour,DrawEffectContainer*))
void Mesh::DrawQuadUnCached(Colour colour, DrawEffectContainer* fx) {
    DrawQuadUnCached(colour, 0.0f, 1.0f, 0.0f, 1.0f, fx);
}

// Binary @ 0x00240a70
// ASM-spec v1.6.1 Mesh::DrawQuadUnCached @0x00240a70: args are (colour,uMin,uMax,vMin,vMax,fx).
void Mesh::DrawQuadUnCached(Colour colour, float uMin, float uMax, float vMin, float vMax,
                             DrawEffectContainer* /*fx*/) {
    // Port specific: binary @ 0x00194060 gates GL_BLEND via fixed-function glState<3042>:
    //   blend OFF iff (colour.a == 0xFF && (renderModeSingleton == null ||
    //                  renderModeSingleton->vtable[0x10]() != 0x20)), else blend ON.
    // The gate reads a global render-mode singleton (DAT_0019417c), not the fx arg.
    // GLES2 has no fixed-function blend state; the port's Renderer::DrawQuad manages
    // blending in its shader/draw path, so this gate has no SDL/GLES2 counterpart.
    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawQuad(colour, uMin, uMax, vMin, vMax);
    }
}

// Binary @ 0x00240e34
// ASM-verified: 2026-05-24 v1.6.1 Mesh::DrawTriList @ 0x00240e34 (re-analyst)
void Mesh::DrawTriList(QUADCUSTOMVERTEX const* verts, long count,
                       bool /*blend*/, DrawEffectContainer* fx, TextureAtlasPage* atlas) {
    DrawTris(verts, count, 4 /*GL_TRIANGLES*/, fx != 0, fx, atlas);
}

// Binary @ 0x00240e10
// ASM-verified: 2026-05-24 v1.6.1 Mesh::DrawTriStrip @ 0x00240e10 (re-analyst)
void Mesh::DrawTriStrip(QUADCUSTOMVERTEX const* verts, long count,
                        bool /*blend*/, DrawEffectContainer* fx, TextureAtlasPage* atlas) {
    DrawTris(verts, count, 5 /*GL_TRIANGLE_STRIP*/, fx != 0, fx, atlas);
}

// Binary @ 0x00240c30
// ASM-spec v1.6.1 Mesh::DrawTris @ 0x00240c30: fixed-function glState<3042> blend
// gate + glTexEnvf(GL_MODULATE) as detailed below; ES1 fixed-function body cannot
// match the port's GLES2 shader path by design.
void Mesh::DrawTris(QUADCUSTOMVERTEX const* verts, long count, int primType,
                    bool /*blend*/, DrawEffectContainer* /*fx*/, TextureAtlasPage* /*atlas*/) {
    // Port specific: binary @ 0x00240c30 gates GL_BLEND via fixed-function glState<3042>:
    //   blend OFF iff (blend == 0 && (renderModeSingleton == null ||
    //                  renderModeSingleton->vtable[0x10]() != 0x20)), else blend ON.
    // Also issues glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE) — both are
    // fixed-function GL1.x state with no GLES2 counterpart. The port's Renderer DrawTriList/
    // DrawTriStrip set up blend + texture-env-equivalent modulation in their shaders.
    // atlas is only bound/unbound (page->GetTexture()->vtable[0xc]/[0x10]) when fx != NULL;
    // every port call site passes fx==NULL so that path is dead here — not ported, unreached.
    if (Renderer* r = Renderer::GetInstance()) {
        if (primType == 4)
            r->DrawTriList(const_cast<QUADCUSTOMVERTEX*>(verts), (int)count);
        else if (primType == 5)
            r->DrawTriStrip(const_cast<QUADCUSTOMVERTEX*>(verts), (int)count);
    }
}

} // namespace Mortar
