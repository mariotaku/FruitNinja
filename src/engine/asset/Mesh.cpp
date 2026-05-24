#include "asset/Mesh.h"
#include "asset/SharedEffectProperties.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include <cstring>
#include <cmath>

// Analysed: 2026-04-11T18:30

namespace Mortar {

// --- Mesh ---

// Binary @ 0x001b0e70
Mesh::Mesh()
    : m_Skeleton(nullptr)
    , m_WorldProp(NULL), m_ViewProp(NULL), m_ProjProp(NULL), m_WVPProp(NULL)
{}

// Binary @ 0x001b0a5c
Mesh::~Mesh() {
    for (int i = 0; i < (int)m_Geometries.size(); i++) {
        if (m_Geometries[i].vbo) { glDeleteBuffers(1, &m_Geometries[i].vbo); }
        if (m_Geometries[i].ibo) { glDeleteBuffers(1, &m_Geometries[i].ibo); }
    }
}

// Matches Mesh::SetBones (0x001b1340)
// Resizes m_BoneBindings and copies each entry
void Mesh::SetBones(const BoneBinding* bones, unsigned long count) {
    m_BoneBindings.resize(count);
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
        uint32_t idx = skeleton->FindIndex(m_BoneBindings[i].m_Name.c_str());
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
    Matrix44 identity;
    if (!m_Skeleton) return identity;
    if (index >= m_BoneBindings.size()) return identity;
    int skelIdx = m_BoneBindings[index].m_SkeletonIndex;
    if (skelIdx < 0) return identity;
    return *m_Skeleton->GetWorld((uint32_t)skelIdx);
}

// Binary @ 0x001b0778 — has no callers; emitted for API parity.
//                    Mirrors GetBoneVertTransform / GetBoneWorldTransform.
Matrix44 Mesh::GetBoneLocalTransform(unsigned long idx) const {
    Matrix44 out;
    if (m_Skeleton != NULL) {
        const BoneBinding& bind = m_BoneBindings[idx];
        if (bind.m_SkeletonIndex >= 0) {
            const Matrix44* src = m_Skeleton->GetLocal((uint32_t)bind.m_SkeletonIndex);
            memcpy(&out, src, sizeof(Matrix44));
            return out;
        }
    }
    out = Matrix44();
    return out;
}

// Matches Mesh::GetBounds (0x001b07f0).
// Binary signature: Bounds3D GetBounds() const (struct-return; r0 = hidden
// retval ptr to a 24-byte Bounds3D). Computes AABB by transforming each
// bone's local bounds through its world matrix, then min/max-reducing
// across all bones. Initial values: ±1e30 (DAT_001b08e0 / DAT_001b08e4).
// ASM-verified: 2026-04-29T00:00Z binary @ 0x001b07f0 (asm-inspector)
Bounds3D Mesh::GetBounds() const {
    Bounds3D out(Vec3( 1e30f,  1e30f,  1e30f),
                 Vec3(-1e30f, -1e30f, -1e30f));

    for (int i = 0; i < (int)m_BoneBindings.size(); i++) {
        const BoneBinding& bone = m_BoneBindings[i];
        Matrix44 W = GetBoneWorldTransform(i);
        const float* M = W.m;  // column-major: col c, row r = M[c*4+r]

        // Transform bmin and bmax as homogeneous points (w=1).
        const Vec3& bmin = bone.m_BoundsMin;
        const Vec3& bmax = bone.m_BoundsMax;
        Vec3 wMin(
            M[0]*bmin.x + M[4]*bmin.y + M[8]*bmin.z  + M[12],
            M[1]*bmin.x + M[5]*bmin.y + M[9]*bmin.z  + M[13],
            M[2]*bmin.x + M[6]*bmin.y + M[10]*bmin.z + M[14]
        );
        Vec3 wMax(
            M[0]*bmax.x + M[4]*bmax.y + M[8]*bmax.z  + M[12],
            M[1]*bmax.x + M[5]*bmax.y + M[9]*bmax.z  + M[13],
            M[2]*bmax.x + M[6]*bmax.y + M[10]*bmax.z + M[14]
        );

        // Reduce both transformed corners into the running AABB.
        Vec3& outMin = out.min;
        Vec3& outMax = out.max;
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

void Mesh::SetDiffuseTexture(const Mortar::SmartPtr<Texture>& tex) {
    for (int i = 0; i < (int)m_Materials.size(); i++) {
        if (!m_Materials[i].m_Texture.IsValid()) {
            m_Materials[i].m_Texture = tex;
        }
    }
}

bool Mesh::HasDiffuseTexture() const {
    for (int i = 0; i < (int)m_Materials.size(); i++) {
        if (m_Materials[i].m_Texture.IsValid()) return true;
    }
    return false;
}

// Draw a single geometry entry with its bound material.
// Near-direct translation of PassBinding::Apply (0x001a39f8) +
// Geometry::Render (0x001a3e98):
//   glMatrixMode(PROJECTION) + glPushMatrix + glLoadMatrixf
//   glMatrixMode(MODELVIEW)  + glPushMatrix + glLoadMatrixf
//   one-shot glEnable(GL_CULL_FACE)
//   Texture::Set() + glTexEnvf(GL_MODULATE)
//   glEnableClientState + glVertexPointer / glNormalPointer / glColorPointer / glTexCoordPointer
//   glDrawElements or glDrawArrays
//   glMatrixMode(PROJECTION) + glPopMatrix
//   glMatrixMode(MODELVIEW)  + glPopMatrix
//   glBindBuffer(ARRAY_BUFFER, 0) + glBindBuffer(ELEMENT_ARRAY_BUFFER, 0)
static void DrawGeometry(Renderer* /*renderer*/, const GeometryEntry& geom,
                         const MeshMaterial& mat, const Matrix44& mvp,
                         const Matrix44& /*world*/) {
    if (!geom.vbo || geom.vertCount == 0) return;

    // CULL_FACE: do NOT enable. The binary's Geometry::Render @ 0x001a3ec8
    // guards its `glEnable(GL_CULL_FACE)` behind a one-shot static byte at
    // DAT_001a4050 -- after frame 0, that byte is set, the enable never
    // executes again, and DisplayManagerBada::BeginFrame's two glDisable
    // calls (0x0019e012 and 0x0019e066) leave CULL_FACE off for the rest
    // of the program's life. Net effect: the binary renders 3D meshes
    // with cull disabled. Asm-inspector confirmed via Geometry::Render
    // disassembly + BeginFrame trace.
    //
    // The earlier port enabled CULL_FACE per draw under the theory that
    // dropping CW back-faces was needed for effect-fruit meshes; that was
    // wrong -- those meshes were never culled in the binary either, and
    // the per-draw enable produced "fruit half-rendered" / "banana speed
    // outline missing" artefacts when the FruitCamera's view direction
    // happens to put a triangle's GL-default CCW winding on the back side.

    // Matrix stacks: push current, load MVP, pop on exit. Binary splits
    // the upload into PROJECTION = screenRot*World and MODELVIEW = view
    // composition; our MatrixManager already produced the final MVP, so
    // we upload it to PROJECTION and leave MODELVIEW as identity.
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadMatrixf(mvp.ptr());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    if (mat.m_Texture.IsValid()) {
        mat.m_Texture->Set();
        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, (GLfloat)GL_MODULATE);
    } else {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Material. Field names in MeshMaterial are inverted vs binary:
    //   m_Diffuse  = GetColourRGB(color0) stored as "Ambience" prop
    //   m_Ambience = GetColourRGB(color1) stored as "Diffuse"  prop
    // LoadMesh always sets m_IsLit = false in the current port, so this
    // path usually short-circuits to glDisable(GL_LIGHTING) + white colour.
    if (mat.m_IsLit) {
        glEnable(GL_LIGHTING);
        const GLfloat amb[4] = { mat.m_Ambience.x, mat.m_Ambience.y, mat.m_Ambience.z, 1.0f };
        const GLfloat dif[4] = { mat.m_Diffuse.x,  mat.m_Diffuse.y,  mat.m_Diffuse.z,  1.0f };
        const GLfloat emi[4] = { mat.m_SelfIllum.x,mat.m_SelfIllum.y,mat.m_SelfIllum.z,1.0f };
        glMaterialfv(GL_AMBIENT,  GL_AMBIENT,  amb);
        glMaterialfv(GL_DIFFUSE,  GL_DIFFUSE,  dif);
        glMaterialfv(GL_EMISSION, GL_EMISSION, emi);
    } else {
        glDisable(GL_LIGHTING);
        glColor4ub(255, 255, 255, 255);
    }

    glBindBuffer(GL_ARRAY_BUFFER, geom.vbo);
    if (geom.ibo) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geom.ibo);
    }

    const VertexLayout& L = geom.layout;

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, L.totalStride, (void*)(intptr_t)L.posOffset);

    if (L.normalSize > 0) {
        glEnableClientState(GL_NORMAL_ARRAY);
        glNormalPointer(GL_FLOAT, L.totalStride, (void*)(intptr_t)L.normalOffset);
    } else {
        glDisableClientState(GL_NORMAL_ARRAY);
    }

    if (L.colorSize > 0 && L.colorFmt == 3) {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_UNSIGNED_BYTE, L.totalStride,
                       (void*)(intptr_t)L.colorOffset);
    } else {
        glDisableClientState(GL_COLOR_ARRAY);
    }

    glClientActiveTexture(GL_TEXTURE0);
    if (L.texSize > 0) {
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, L.totalStride, (void*)(intptr_t)L.texOffset);
    } else {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    }

    if (geom.ibo && geom.indexCount > 0) {
        glDrawElements(geom.primType, geom.indexCount, GL_UNSIGNED_SHORT, (void*)0);
    } else {
        glDrawArrays(geom.primType, 0, geom.vertCount);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    // No cull-state restore needed: we never enabled it (binary doesn't
    // either; see comment above the geometry-render block).

    // Restore matrix stacks + unbind VBOs, matching the tail of
    // Geometry::Render.
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// Matches Mesh::Draw (0x001b0c3c)
// Original behavior:
//   if boneCount == 1: finalWorld = GetBoneVertTransform(0) * worldMatrix
//   else:              finalWorld = worldMatrix
//   Set World, View, Proj, WVP effect properties
//   Render all geometries (each with its own material)
// Port: uses Renderer::setup_3d_shader() for GLES2; skeleton system implemented.
void Mesh::Draw(const Matrix44& worldTransform) {
    if (m_Geometries.empty()) return;

    Renderer* renderer = Renderer::GetInstance();
    if (!renderer) return;

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

    // Port specific: compute MVP via MatrixManager (replaces Effect property system)
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(finalWorld);
    Matrix44 mvp = mm.GetMVP();

    // Render all geometry entries, each with its own material
    for (int i = 0; i < (int)m_Geometries.size(); i++) {
        const GeometryEntry& geom = m_Geometries[i];
        int matIdx = geom.materialIndex;

        // Fallback: use first material if index out of range
        const MeshMaterial& mat = (matIdx >= 0 && matIdx < (int)m_Materials.size())
                                  ? m_Materials[matIdx]
                                  : (m_Materials.empty() ? MeshMaterial() : m_Materials[0]);

        DrawGeometry(renderer, geom, mat, mvp, finalWorld);
    }
}

// ---- Mesh binary stubs ----

// Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b10d8
// Binary: builds a 4-entry EffectPropertyDefinition array (World, SceneCamera.View,
// SceneCamera.Projection, WorldViewProjection; type=3, count=1 each), then either
// reuses parent if it Contains() all defs, or news up SharedEffectProperties as
// m_OwnGroup. Finally caches the 4 EffectProperty* handles via GetProperty(name).
// Port: EffectPropertyList::Contains() stub returns true, so parent is always reused
// when valid; GetProperty() stub returns nullptr for all 4 cached handles.
Mesh::Mesh(SmartPtr<SharedEffectProperties> const& parent, AsciiString const& name)
    : m_Skeleton(NULL)
    , m_WorldProp(NULL), m_ViewProp(NULL), m_ProjProp(NULL), m_WVPProp(NULL)
{
    // Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b10d8
    m_Name = name.c_str();
    EffectPropertyDefinition defs[4] = {
        { NULL, 3, 1 },  // "World"
        { NULL, 3, 1 },  // "SceneCamera.View"
        { NULL, 3, 1 },  // "SceneCamera.Projection"
        { NULL, 3, 1 },  // "WorldViewProjection"
    };
    if (parent.IsValid() && parent->GetList().Contains(defs)) {
        m_OwnGroup = parent;
    } else {
        m_OwnGroup = new SharedEffectProperties(defs, defs + 4, parent);
    }
    m_WorldProp = m_OwnGroup->GetList().GetProperty("World");
    m_ViewProp  = m_OwnGroup->GetList().GetProperty("SceneCamera.View");
    m_ProjProp  = m_OwnGroup->GetList().GetProperty("SceneCamera.Projection");
    m_WVPProp   = m_OwnGroup->GetList().GetProperty("WorldViewProjection");
}

// STUB: BindSkeleton(Skeleton const&) -- binary @ 0x001b0948
// Binary signature takes const-ref; this overload matches the binary mangled symbol.
void Mesh::BindSkeleton(Skeleton const& /*skeleton*/) {
    // Defunct: const-ref BindSkeleton overload -- no-op stub; binary @ 0x001b0948
}

// Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b0d0c
// Binary: pushes SmartPtr<Geometry> into m_Geometries (vector<SmartPtr<Geometry>>).
// Port: m_Geometries is vector<GeometryEntry>; storage types don't match.
// DIFFERS: binary appends SmartPtr<Geometry>; port uses GeometryEntry directly loaded
// in LoadMesh -- this overload is unreachable at runtime but kept for call-graph parity.
void Mesh::AddGeometry(SmartPtr<Geometry> const& /*geom*/) {
    // Defunct: SharedEffectProperties subsystem -- no-op stub; binary @ 0x001b0d0c
}

// Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b0988
// Binary: looks up name in m_GroupsByName; returns &slot.m_Group if found, nullptr if not.
SmartPtr<SharedEffectProperties>* Mesh::GetPropertiesGroup(AsciiString const& name) const {
    // Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b0988
    std::map<AsciiString, SharedPropsInfo>::const_iterator it = m_GroupsByName.find(name);
    if (it == m_GroupsByName.end()) return NULL;
    return const_cast<SmartPtr<SharedEffectProperties>*>(&it->second.m_Group);
}

// Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b1430
// Binary: checks if existing group already contains all defs; if so returns it.
// Otherwise inserts a new SharedEffectProperties(begin, end, parent) into m_GroupsByName.
// Port: EffectPropertyList::Contains() stub returns true, so the fast-path wins
// whenever a group is already present; new-group construction fires only on first call.
SmartPtr<SharedEffectProperties>* Mesh::GetPropertiesGroup(AsciiString const& name,
                                                            EffectPropertyDefinition const* begin,
                                                            EffectPropertyDefinition const* end) {
    // Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b1430
    SmartPtr<SharedEffectProperties>* existing = GetPropertiesGroup(name);
    if (existing) {
        const EffectPropertyDefinition* p = begin;
        while (p < end && (*existing)->GetList().Contains(p)) ++p;
        if (p == end) return existing;
    }
    SmartPtr<SharedEffectProperties>& slot = m_GroupsByName[name].m_Group;
    SmartPtr<SharedEffectProperties> parent = existing ? *existing : m_OwnGroup;
    slot = new SharedEffectProperties(begin, end, parent);
    return &slot;
}

// Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b08e8
// Binary: re-fetches the 4 cached prop handles (World/View/Proj/WVP) from m_OwnGroup.
// Port: GetProperty() stub returns nullptr; handles stay nullptr.
void Mesh::RebuildEffectBindings() {
    // Defunct: SharedEffectProperties subsystem -- shape preserved; binary @ 0x001b08e8
    if (!m_OwnGroup.IsValid()) return;
    m_WorldProp = m_OwnGroup->GetList().GetProperty("World");
    m_ViewProp  = m_OwnGroup->GetList().GetProperty("SceneCamera.View");
    m_ProjProp  = m_OwnGroup->GetList().GetProperty("SceneCamera.Projection");
    m_WVPProp   = m_OwnGroup->GetList().GetProperty("WorldViewProjection");
}

// STUB: DrawCube(float, float, float, Colour, DrawEffectContainer*) -- binary @ 0x00193ed8
void Mesh::DrawCube(float /*x*/, float /*y*/, float /*z*/,
                    Colour /*colour*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawCube is a binary stub (BX LR); no-op stub; binary @ 0x00193ed8
}

// STUB: DrawLine(Vec3 const&, Vec3 const&, float const&, Colour const&, Vec3 const&, DrawEffectContainer*) -- binary @ 0x00193edc
void Mesh::DrawLine(Vec3 const& /*from*/, Vec3 const& /*to*/, float const& /*width*/,
                    Colour const& /*colour*/, Vec3 const& /*normal*/,
                    DrawEffectContainer* /*fx*/) {
    // Defunct: DrawLine is a binary stub (BX LR); no-op stub; binary @ 0x00193edc
}

// STUB: DrawSphere(float, Colour, DrawEffectContainer*) -- binary @ 0x00193ee0
void Mesh::DrawSphere(float /*radius*/, Colour /*colour*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawSphere is a binary stub (BX LR); no-op stub; binary @ 0x00193ee0
}

// Binary @ 0x001b09b0
// ASM-verified: 2026-05-24 binary @ 0x001b09b0 (re-analyst)
void Mesh::DrawQuad(Colour colour, SmartPtr<Texture> texture,
                    Vec3 const& pos, Vec3 const& scale, float rotZ,
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

// Binary @ 0x00194180
// ASM-verified: 2026-05-24 binary @ 0x00194180 (re-analyst)
void Mesh::DrawQuadUnCached(Colour colour, DrawEffectContainer* fx) {
    DrawQuadUnCached(colour, 0.0f, 1.0f, 0.0f, 1.0f, fx);
}

// Binary @ 0x00194060
// ASM-verified: 2026-05-24 binary @ 0x00194060 (re-analyst)
void Mesh::DrawQuadUnCached(Colour colour, float u0, float v0, float u1, float v1,
                             DrawEffectContainer* /*fx*/) {
    // TODO: 0x00194060 -- fx non-null path: fx->GetType()==0x20 forces blend ON.
    // fx is always null in current port (DrawEffectContainer is defunct).
    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawQuad(colour, u0, v0, u1, v1);
    }
}

// Binary @ 0x0019404c
// ASM-verified: 2026-05-24 binary @ 0x0019404c (re-analyst)
void Mesh::DrawTriList(QUADCUSTOMVERTEX const* verts, long count,
                       bool /*blend*/, DrawEffectContainer* fx) {
    DrawTris(verts, count, 4 /*GL_TRIANGLES*/, fx != 0, 0);
}

// Binary @ 0x00194038
// ASM-verified: 2026-05-24 binary @ 0x00194038 (re-analyst)
void Mesh::DrawTriStrip(QUADCUSTOMVERTEX const* verts, long count,
                        bool /*blend*/, DrawEffectContainer* fx) {
    DrawTris(verts, count, 5 /*GL_TRIANGLE_STRIP*/, fx != 0, 0);
}

// Binary @ 0x00193f5c
// ASM-verified: 2026-05-24 binary @ 0x00193f5c (re-analyst)
void Mesh::DrawTris(QUADCUSTOMVERTEX const* verts, long count, int primType,
                    bool /*blend*/, DrawEffectContainer* /*fx*/) {
    // TODO: 0x00193f5c -- blend/fx gate: if !blend && singleton==null -> glDisable(GL_BLEND).
    // fx/blend are defunct in port; Renderer paths enable blend unconditionally.
    if (Renderer* r = Renderer::GetInstance()) {
        if (primType == 4)
            r->DrawTriList(const_cast<QUADCUSTOMVERTEX*>(verts), (int)count);
        else if (primType == 5)
            r->DrawTriStrip(const_cast<QUADCUSTOMVERTEX*>(verts), (int)count);
    }
}

} // namespace Mortar
