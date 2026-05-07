#include "asset/Mesh.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"
#include <cstring>

// Analysed: 2026-04-11T18:30

namespace Mortar {

// --- Mesh ---

// Binary @ 0x001b0e70
Mesh::Mesh() : m_Skeleton(nullptr) {}

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

// --- Model ---

Model::Model() {
}

Model::~Model() {
    m_Meshes.clear();
}

// Matches Model::SwapSkeleton (0x001aaba8)
// Calls Skeleton::Swap(Skeleton&) (0x001a89c4) — swaps all four arrays without
// rebuilding matrices — then calls UpdateBoneLinks.
void Model::SwapSkeleton(Skeleton& skel) {
    m_Skeleton.Swap(skel);
    UpdateBoneLinks();
}

// Matches Model::UpdateBoneLinks (0x00193010)
// Calls BindSkeleton(vtable[8]) on each mesh with the model's skeleton.
void Model::UpdateBoneLinks() {
    for (int i = 0; i < (int)m_Meshes.size(); i++) {
        if (m_Meshes[i].IsValid()) {
            m_Meshes[i]->BindSkeleton(&m_Skeleton);
        }
    }
}

// Binary @ 0x0019346c
void Model::AddNode(SmartPtr<Mesh> mesh) {
    m_Meshes.push_back(mesh);
    if (mesh.IsValid()) mesh->BindSkeleton(&m_Skeleton);
}

// Binary @ 0x001933f8 — unchecked array access (matches binary).
Mortar::SmartPtr<Mesh> Model::GetNode(unsigned long index) const {
    return m_Meshes[index];
}

// Binary @ 0x001933b8 — dead code; linear scan by name.
Mortar::SmartPtr<Mesh> Model::GetNode(const std::string& name) const {
    for (int i = 0; i < (int)m_Meshes.size(); i++) {
        if (m_Meshes[i].IsValid() && m_Meshes[i]->m_Name == name) {
            return m_Meshes[i];
        }
    }
    return Mortar::SmartPtr<Mesh>();
}

// AlphaSortNode struct used by binary qsort (0x001935a0).
// Must be at file scope: GCC 4.4 rejects local structs as template arguments.
struct ModelSortEntry {
    Mesh* mesh;
    float key;  // perspective-divided clip-space z: z'/w'
};

// Matches Model::Draw (0x001930e0, 79 lines)
// Single mesh: draw directly. Multi-mesh: depth-sort back-to-front.
void Model::Draw(const Matrix44& transform) {
    int meshCount = (int)m_Meshes.size();
    if (meshCount == 0) return;

    // Single-mesh fast path (matches binary at 0x00193210).
    if (meshCount == 1) {
        m_Meshes[0]->Draw(transform);
        return;
    }

    // Multi-mesh path (binary 0x001930e0):
    //   localProj = transform * projTop    (Mul44: dest = lhs * rhs)
    //   mvp       = localProj * viewTop    => transform * proj * view
    // This engine uses row-vector convention, so MVP = T * P * V.
    // The port previously built (proj * view) * transform — wrong order.
    MatrixManager& mm = MatrixManager::GetInstance();
    const Matrix44& projTop = mm.GetProjectionStack().m_Current;
    const Matrix44& viewTop = mm.GetViewStack().m_Current;
    Matrix44 localProj = transform * projTop;
    Matrix44 mvp       = localProj * viewTop;

    std::vector<ModelSortEntry> sorted(meshCount);
    for (int i = 0; i < meshCount; i++) {
        sorted[i].mesh = m_Meshes[i].Get();

        // Per-mesh sort key from binary (0x001930e0):
        //   b = mesh->GetBounds()           (vtable +0x14, slot 5)
        //   c = (b.min + b.max) * 0.5f      (Bounds3D::Center @ 0x001936d0)
        //   z' = c.x*mvp[col2.row0] + c.y*mvp[col2.row1] + c.z*mvp[col2.row2] + mvp[col2.row3]
        //   w' = c.x*mvp[col3.row0] + c.y*mvp[col3.row1] + c.z*mvp[col3.row2] + mvp[col3.row3]
        //   key = z' / w'
        //
        // Matrix44 is column-major: m[col*4 + row].
        // Col 2 (z) = m[8],m[9],m[10],m[11]; col 3 (w) = m[12],m[13],m[14],m[15].
        Bounds3D b = m_Meshes[i]->GetBounds();
        // Bounds3D::Center @ 0x001936d0 — (min + max) * 0.5
        Vec3 c;
        c.x = (b.min.x + b.max.x) * 0.5f;
        c.y = (b.min.y + b.max.y) * 0.5f;
        c.z = (b.min.z + b.max.z) * 0.5f;

        float zp = c.x * mvp.m[8]  + c.y * mvp.m[9]  + c.z * mvp.m[10] + mvp.m[11];
        float wp = c.x * mvp.m[12] + c.y * mvp.m[13] + c.z * mvp.m[14] + mvp.m[15];
        // Guard against w'=0 (degenerate: treat as 0-depth)
        sorted[i].key = (wp != 0.0f) ? (zp / wp) : 0.0f;
    }

    // Sort descending by key (largest z'/w' first = back-to-front).
    // Matches AlphaSortNode::compare @ 0x001935a0: returns sign of (b.key - a.key).
    for (int i = 0; i < meshCount - 1; i++) {
        for (int j = i + 1; j < meshCount; j++) {
            if (sorted[j].key > sorted[i].key) {
                ModelSortEntry tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    for (int i = 0; i < meshCount; i++) {
        sorted[i].mesh->Draw(transform);
    }
}

// ---- Mesh binary stubs ----

// STUB: Mesh(SmartPtr<SharedEffectProperties> const&, AsciiString const&) -- binary @ 0x001b10d8
// Defunct: SharedEffectProperties not ported; default-constructs instead.
Mesh::Mesh(SmartPtr<SharedEffectProperties> const& /*props*/, AsciiString const& name)
    : m_Skeleton(nullptr) {
    // Defunct: SharedEffectProperties -- no-op stub; binary @ 0x001b10d8
    m_Name = name.c_str();
}

// STUB: BindSkeleton(Skeleton const&) -- binary @ 0x001b0948
// Binary signature takes const-ref; this overload matches the binary mangled symbol.
void Mesh::BindSkeleton(Skeleton const& /*skeleton*/) {
    // Defunct: const-ref BindSkeleton overload -- no-op stub; binary @ 0x001b0948
}

// STUB: AddGeometry(SmartPtr<Geometry> const&) -- binary @ 0x001b0d0c
// Defunct: port appends GeometryEntry directly in LoadMesh.
void Mesh::AddGeometry(SmartPtr<Geometry> const& /*geom*/) {
    // Defunct: Geometry/GeometryBinding stack -- no-op stub; binary @ 0x001b0d0c
}

// STUB: GetPropertiesGroup(AsciiString const&) const -- binary @ 0x001b0988
SharedEffectProperties* Mesh::GetPropertiesGroup(AsciiString const& /*name*/) const {
    // Defunct: SharedEffectProperties -- no-op stub; binary @ 0x001b0988
    return nullptr;
}

// STUB: GetPropertiesGroup(AsciiString const&, EffectPropertyDefinition const*, EffectPropertyDefinition const*) -- binary @ 0x001b1430
SharedEffectProperties* Mesh::GetPropertiesGroup(AsciiString const& /*name*/,
                                                  EffectPropertyDefinition const* /*begin*/,
                                                  EffectPropertyDefinition const* /*end*/) {
    // Defunct: SharedEffectProperties -- no-op stub; binary @ 0x001b1430
    return nullptr;
}

// STUB: RebuildEffectBindings() -- binary @ 0x001b08e8
void Mesh::RebuildEffectBindings() {
    // Defunct: EffectBinding system -- no-op stub; binary @ 0x001b08e8
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

// STUB: DrawQuad(Colour, SmartPtr<Texture>, Vec3 const&, Vec3 const&, float, float, float, float, float, DrawEffectContainer*) -- binary @ 0x001b09b0
void Mesh::DrawQuad(Colour /*colour*/, SmartPtr<Texture> /*texture*/,
                    Vec3 const& /*pos*/, Vec3 const& /*scale*/, float /*rotZ*/,
                    float /*w*/, float /*h*/, float /*uOff*/, float /*vOff*/,
                    DrawEffectContainer* /*fx*/) {
    // Defunct: DrawQuad via DrawEffectContainer -- no-op stub; binary @ 0x001b09b0
}

// STUB: DrawQuadUnCached(Colour, DrawEffectContainer*) -- binary @ 0x00194180
void Mesh::DrawQuadUnCached(Colour /*colour*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawQuadUnCached -- no-op stub; binary @ 0x00194180
}

// STUB: DrawQuadUnCached(Colour, float, float, float, float, DrawEffectContainer*) -- binary @ 0x00194060
void Mesh::DrawQuadUnCached(Colour /*colour*/, float /*w*/, float /*h*/,
                             float /*uOff*/, float /*vOff*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawQuadUnCached -- no-op stub; binary @ 0x00194060
}

// STUB: DrawTriList(QUADCUSTOMVERTEX const*, long, bool, DrawEffectContainer*) -- binary @ 0x0019404c
void Mesh::DrawTriList(QUADCUSTOMVERTEX const* /*verts*/, long /*count*/,
                       bool /*blend*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawTriList via DrawEffectContainer -- no-op stub; binary @ 0x0019404c
}

// STUB: DrawTriStrip(QUADCUSTOMVERTEX const*, long, bool, DrawEffectContainer*) -- binary @ 0x00194038
void Mesh::DrawTriStrip(QUADCUSTOMVERTEX const* /*verts*/, long /*count*/,
                        bool /*blend*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawTriStrip via DrawEffectContainer -- no-op stub; binary @ 0x00194038
}

// STUB: DrawTris(QUADCUSTOMVERTEX const*, long, int, bool, DrawEffectContainer*) -- binary @ 0x00193f5c
void Mesh::DrawTris(QUADCUSTOMVERTEX const* /*verts*/, long /*count*/,
                    int /*primType*/, bool /*blend*/, DrawEffectContainer* /*fx*/) {
    // Defunct: DrawTris via DrawEffectContainer -- no-op stub; binary @ 0x00193f5c
}

// ---- Model stubs (binary) ----

// STUB: Model(AsciiString const&) -- binary @ 0x???? (TODO RE)
Model::Model(AsciiString const& /*name*/) {
}

// STUB: Draw(Matrix44 const&) const -- binary @ 0x???? (TODO RE)
void Model::Draw(Matrix44 const& transform) const {
}

// STUB: GetBounds() const -- binary @ 0x???? (TODO RE)
Bounds3D Model::GetBounds() const {
    // Defunct: zero-arg GetBounds -- no-op stub; binary @ 0x????
    return Bounds3D();
}

// STUB: GetNode(AsciiString const&) const -- binary @ 0x???? (TODO RE)
SmartPtr<Mesh> Model::GetNode(AsciiString const& name) const {
    for (int i = 0; i < (int)m_Meshes.size(); i++) {
        if (m_Meshes[i].IsValid() &&
            m_Meshes[i]->m_Name == std::string(name.c_str())) {
            return m_Meshes[i];
        }
    }
    return SmartPtr<Mesh>();
}

// STUB: NodeCount() const -- binary @ 0x???? (TODO RE)
int Model::NodeCount() const {
    return (int)m_Meshes.size();
}

// Matches Model::SetEffectGroup (binary @ 0x0019335c). The binary does NOT
// store the EffectGroup as a field on Model -- it walks `m_Meshes` and for
// each mesh iterates its geometries calling
//   Geometry::EffectGroupSet(geom, effectGroup)   // binary stub @ 0x001a00f8
//   Geometry::SetActiveEffect(geom, 0)            // binary stub
// then calls `Mesh::RebuildEffectBindings()` on the mesh.
//
// Both `Geometry::EffectGroupSet` and `Geometry::SetActiveEffect` are
// binary stubs for the live `Mortar::Geometry` class (vs the legacy
// `GeometryBinding`); the port doesn't have a `Geometry` class at all
// (replaced by flat `GeometryEntry`), so the inner calls are simply
// omitted. `Mesh::RebuildEffectBindings` is the port's Defunct stub
// (binary @ 0x001b08e8), so it's still callable but does nothing.
// Net effect identical to the binary -- the loop runs, the inner stubs
// are no-ops on both sides, and the m_Meshes vector ordering is preserved.
void Model::SetEffectGroup(SmartPtr<EffectGroup> /*effectGroup*/) {
    for (int i = 0; i < (int)m_Meshes.size(); i++) {
        Mesh* mesh = m_Meshes[i].Get();
        if (!mesh) continue;

        // Binary @ 0x001a00f8 / `Geometry::SetActiveEffect`: per-geom
        // calls into the (binary-stub) Geometry class. Port has no
        // Geometry class -- the GeometryEntry walk in Mesh::Draw
        // doesn't go through these hooks.

        // Binary @ 0x001b08e8 -- Defunct stub in port too.
        mesh->RebuildEffectBindings();
    }
}

// EffectGroup::AddEffect (binary @ 0x001a0274) — sorted-vector-set insert.
// Binary structure:
//   it = std::lower_bound(m_Effects.begin(), m_Effects.end(), effect,
//                         EffectLessThanCompare);
//   if (it != m_Effects.end() && !EffectLessThanCompare(effect, *it)) {
//       // Equivalent already in set -> merge properties into existing entry.
//       (*it)->MergeProperties(effect->Properties());
//   } else {
//       m_Effects.insert(it, effect);
//   }
//
// Port mirrors this with linear-scan dedupe instead of lower_bound (the
// `Effect` class is a no-method shell here -- there is no
// `EffectLessThanCompare` to call, and `Effect::Properties` /
// `Effect::MergeProperties` aren't ported). Net observable behavior
// matches the binary's post-condition: each `effect` value is in
// `m_Effects` exactly once. Sorted-order semantics relax to insertion-
// order. The live render path doesn't reach this code -- every caller
// chains through binary stubs that no-op out -- so the relaxation is
// inert in practice.
void EffectGroup::AddEffect(const SmartPtr<Effect>& effect) {
    if (!effect.IsValid()) return;
    for (int i = 0; i < (int)m_Effects.size(); i++) {
        if (m_Effects[i].Get() == effect.Get()) {
            // Equivalent entry already present. Binary calls
            // MergeProperties on the existing entry; port's Effect class
            // has no such method (no Effect::Properties either), so the
            // merge is omitted.
            return;
        }
    }
    m_Effects.push_back(effect);
}

} // namespace Mortar
