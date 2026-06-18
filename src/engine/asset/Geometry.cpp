#include "asset/Geometry.h"
#include "asset/SharedEffectProperties.h"
#include "asset/Texture.h"
#include "render/Renderer.h"
#include <cstring>


namespace Mortar {

// Binary @ 0x001a3c50 (C1) / 0x001a3cc4 (C2)
Geometry::Geometry(SmartPtr<GeometryBinding> binding,
                   SmartPtr<SharedEffectProperties> props)
    : m_ActiveBindingIdx(0)
    , m_Binding(binding)
    , m_PropList(NULL)
    , m_Vbo(0)
    , m_Ibo(0)
    , m_VertCount(0)
    , m_IndexCount(0)
    , m_PrimType(GL_TRIANGLES)
    , m_MaterialIndex(0)
{
    memset(&m_Layout, 0, sizeof(m_Layout));
    BuildPropList(props);
}

// Binary @ 0x001a4de0 (D0) / 0x001a4e38 (D2/D1)
Geometry::~Geometry() {
    if (m_Vbo) { glDeleteBuffers(1, &m_Vbo); }
    if (m_Ibo) { glDeleteBuffers(1, &m_Ibo); }
}

// Binary @ 0x001a3e98 -- non-virtual member; called directly by Mesh::Draw.
// DIFFERS: binary walks m_Binding->GetBindings()[m_ActiveBindingIdx].m_PassBindings,
//   calling PassBinding::Apply (glVertexPointer/etc.) then glDrawArrays/Elements
//   via the IIndexStream vtable. Port renders directly from the loader-cached
//   m_Vbo/m_Ibo/m_Layout because GLES2 has no fixed-pipeline client state.
//   Port uses m_DiffuseTex instead of MeshMaterial for texture binding.
void Geometry::Render(Matrix44 const& mvp) {
    if (!m_Vbo || m_VertCount == 0) return;

    // CULL_FACE: do NOT enable. The binary's Geometry::Render @ 0x001a3ec8
    // guards its `glEnable(GL_CULL_FACE)` behind a one-shot static byte at
    // DAT_001a4050 -- after frame 0, that byte is set, the enable never
    // executes again, and DisplayManagerBada::BeginFrame's two glDisable
    // calls (0x0019e012 and 0x0019e066) leave CULL_FACE off for the rest
    // of the program's life. Net effect: the binary renders 3D meshes
    // with cull disabled. Asm-inspector confirmed via Geometry::Render
    // disassembly + BeginFrame trace.

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

    if (m_DiffuseTex.IsValid()) {
        m_DiffuseTex->Set();
        TexEnvModulate();
    } else {
        glDisable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // Always unlit. All meshes in FruitNinja have IsLit=false, so the
    // former MeshMaterial material-colour branch (ambient/diffuse/emission)
    // is hardcoded to the unlit path.
    glDisable(GL_LIGHTING);
    glColor4ub(255, 255, 255, 255);

    glBindBuffer(GL_ARRAY_BUFFER, m_Vbo);
    if (m_Ibo) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_Ibo);
    }

    const VertexLayout& L = m_Layout;

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

    if (m_Ibo && m_IndexCount > 0) {
        glDrawElements(m_PrimType, m_IndexCount, GL_UNSIGNED_SHORT, (void*)0);
    } else {
        glDrawArrays(m_PrimType, 0, m_VertCount);
    }

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    // Restore matrix stacks + unbind VBOs, matching the tail of the binary's
    // Geometry::Render.
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

// Binary @ 0x001a3e7c
bool Geometry::HasActiveEffect() const {
    // Defunct: GeometryBinding stack not constructed -- port's binding is always null.
    // Binary: return m_ActiveBindingIdx < m_Binding->GetBindings().size();
    return false;
}

// Binary @ 0x001a3e5c
bool Geometry::SetActiveEffect(uint32_t idx) {
    // Defunct: GeometryBinding stack not constructed -- always false in port.
    // Binary: if (idx < size) { m_ActiveBindingIdx = idx; return true; } return false;
    (void)idx;
    return false;
}

// Binary @ 0x001a3c00
void Geometry::BuildPropList(SmartPtr<SharedEffectProperties> /*props*/) {
    // Defunct: EffectPropertyList not load-bearing in port; m_PropList stays null.
    m_PropList = NULL;
}

// Binary @ 0x0025ee7c — 3-instruction wrapper matching binary body.
EffectProperty* Geometry::GetProperty(uint32_t nameHash) {
    if (!m_PropList) return nullptr;
    return m_PropList->GetProperty((const char*)nameHash);
}

}  // namespace Mortar
