#include "asset/Mesh.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/DisplayManager.h"

// Analysed: 2026-04-11T18:30

namespace Mortar {

// --- Mesh ---

Mesh::Mesh() : m_Skeleton(nullptr) {}

Mesh::~Mesh() {
    for (int i = 0; i < (int)m_Geometries.size(); i++) {
        if (m_Geometries[i].vbo) { glDeleteBuffers(1, &m_Geometries[i].vbo); }
        if (m_Geometries[i].ibo) { glDeleteBuffers(1, &m_Geometries[i].ibo); }
    }
}

// Matches Mesh::SetBones (0x001b1340)
// Resizes m_BoneBindings and copies each entry
void Mesh::SetBones(const BoneBinding* bones, int count) {
    m_BoneBindings.resize(count);
    for (int i = 0; i < count; i++) {
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
const Matrix44* Mesh::GetBoneVertTransform(int index) const {
    if (!m_Skeleton) return nullptr;
    if (index < 0 || index >= (int)m_BoneBindings.size()) return nullptr;
    int skelIdx = m_BoneBindings[index].m_SkeletonIndex;
    if (skelIdx < 0) return nullptr;
    return m_Skeleton->GetVertex(skelIdx);
}

// Matches Mesh::GetBoneWorldTransform (0x001b0700)
// Returns world matrix for binding[index] through skeleton. Identity when
// no skeleton bound or bone unbound.
Matrix44 Mesh::GetBoneWorldTransform(int index) const {
    Matrix44 identity;
    if (!m_Skeleton) return identity;
    if (index < 0 || index >= (int)m_BoneBindings.size()) return identity;
    int skelIdx = m_BoneBindings[index].m_SkeletonIndex;
    if (skelIdx < 0) return identity;
    const Matrix44* w = m_Skeleton->GetWorld(skelIdx);
    if (!w) return identity;
    return *w;
}

// Matches Mesh::GetBounds (0x001b07f0)
// Computes AABB by transforming each bone's local bounds through its world
// matrix (0x001b0840-0x001b08c0), then min/max-reducing across all bones.
// ASM-verified: 2026-04-29T00:00Z binary @ 0x001b07f0 (asm-inspector)
void Mesh::GetBounds(Vec3& outMin, Vec3& outMax) const {
    outMin = Vec3(1e30f, 1e30f, 1e30f);
    outMax = Vec3(-1e30f, -1e30f, -1e30f);

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
}

void Mesh::SetDiffuseTexture(const SmartPtr<Texture>& tex) {
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

    // Per-draw CULL_FACE enable with GL defaults (GL_BACK / GL_CCW).
    // Confirmed via the WebGL model gallery that dropping CW back-faces
    // is what makes effect-fruit meshes (banana_speed, dragon, plum,
    // bomb...) render correctly — the outline / decorative shells in
    // those meshes are modelled as slightly-larger shells fully
    // enclosing the body with CW winding, so GL_BACK culls their
    // camera-facing triangles, leaving only the far side visible as a
    // silhouette border while the body renders normally.
    //
    // The old code held CULL_FACE enabled via a static guard, matching
    // Geometry::Render's latch at 0x001a3ec8. That breaks in practice
    // because DisplayManagerBada::BeginFrame issues `glDisable(
    // GL_CULL_FACE)` every frame — so after frame 0 the latch had no
    // effect and the port rendered with cull permanently off (which is
    // what produced the long-standing "mirror through bomb fuse hole"
    // and "solid colour over effect fruit" artefacts). Enabling per
    // draw overrides the BeginFrame disable.
    glEnable(GL_CULL_FACE);

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

    // Restore cull state to the per-frame default (disabled by
    // DisplayManagerBada::BeginFrame). Leaving cull enabled leaks to
    // later-drawing tri-strips (SlashEntity blade, SliceEffect) whose
    // geometry has mixed winding — the "wrong" side gets back-face
    // culled and the visual vanishes. Binary's DisplayManagerBada
    // disables cull every frame and the mesh-path enables it just
    // around each Geometry::Render; the port mirrors that scoping here.
    glDisable(GL_CULL_FACE);

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

    // AlphaSortNode struct used by binary qsort (0x001935a0).
    struct SortEntry {
        Mesh* mesh;
        float key;  // perspective-divided clip-space z: z'/w'
    };

    std::vector<SortEntry> sorted(meshCount);
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
        Vec3 bmin, bmax;
        m_Meshes[i]->GetBounds(bmin, bmax);
        Vec3 c;
        c.x = (bmin.x + bmax.x) * 0.5f;
        c.y = (bmin.y + bmax.y) * 0.5f;
        c.z = (bmin.z + bmax.z) * 0.5f;

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
                SortEntry tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    for (int i = 0; i < meshCount; i++) {
        sorted[i].mesh->Draw(transform);
    }
}

} // namespace Mortar
