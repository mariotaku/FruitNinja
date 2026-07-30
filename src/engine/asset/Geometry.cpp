#include "asset/Geometry.h"
#include "asset/SharedEffectProperties.h"
#include "asset/IStreamTypes.h"
#include "asset/Effect.h"
#include "asset/Texture.h"
#include "render/Renderer.h"
#include "debug/GLHandleLeakCheck.h"
#include <cstring>


namespace Mortar {

#ifndef __bada__
// Port specific: GameDestroy teardown leak guard -- see debug/GLHandleLeakCheck.h
// for the invariant and why this is __bada__-gated. STATIC storage only; the
// Geometry layout is unchanged.
static int s_LiveGeometry = 0;
#endif

// TODO: re-verify v1.6.1 GeometryBinding_Bada address (symbol absent in Bada v1.6.1 binary; the binding is GeometryBinding_GLES1, ctor folded into GeometryBinding @0x00263e90) -- zero-initialises pads (via memset of _pad regions)
// and default-constructs m_EffectGroup / m_VertexStreams / m_IndexStream.
GeometryBinding_Bada::GeometryBinding_Bada() {
    memset(_pad_namedstreams, 0, sizeof(_pad_namedstreams));
    memset(_pad_effectbindings, 0, sizeof(_pad_effectbindings));
}

GeometryBinding_Bada::~GeometryBinding_Bada() {
}

// v1.6.1 Mortar::GeometryBinding::GeometryBinding @0x00263e90 -- constructs base then zeros Event1.
GeometryBinding::GeometryBinding() {
}

GeometryBinding::~GeometryBinding() {
}

// ASM-spec v1.6.1 GeometryBinding::VertexStreamAdd @0x002640c8: find-or-push_back SmartPtr<IVertexStream> into m_VertexStreams.
// The binary's body: searches m_VertexStreams for a matching ptr (by address comparison);
// if not found, push_back. In LoadMesh, each stream is unique so the push_back path always fires.
void GeometryBinding::VertexStreamAdd(const Mortar::SmartPtr<IVertexStream>& stream) {
    // Search for duplicate (matching binary -- linear scan before push_back).
    for (std::vector<SmartPtr<IVertexStream> >::iterator it = m_VertexStreams.begin();
         it != m_VertexStreams.end(); ++it) {
        if (it->Get() == stream.Get()) return;
    }
    m_VertexStreams.push_back(stream);
}

// ASM-spec v1.6.1 GeometryBinding::IndexStreamSet @0x00264108: name.empty() -> m_IndexStream
// path (ptr-equality early-out, then assign); non-empty name -> m_NamedIndexStreams[name] path.
// LoadMesh @0x0023890c always passes "", so only the empty-name path is exercised in practice.
void GeometryBinding::IndexStreamSet(const Mortar::SmartPtr<IIndexStream>& stream,
                                     const std::string& name) {
    if (name.empty()) {
        if (m_IndexStream.Get() == stream.Get()) return;
        m_IndexStream = stream;
    } else {
        // TODO: v1.6.1 0x00264108 (IndexStreamSet) -- non-empty name -> m_NamedIndexStreams[name]
        // ptr-equality early-out + assign (map path unported; m_NamedIndexStreams is a raw pad).
        if (m_IndexStream.Get() == stream.Get()) return;
        m_IndexStream = stream;
    }
    // Defunct: GLES1 binding rebuild -- GeometryBinding_GLES1::Rebind @tail-call from binary 0x0026416c (no-op in port GL path)
}

// ASM-spec v1.6.1 GeometryBinding::EffectGroupSet @0x0026406c: stores EffectGroup into m_EffectGroup.
void GeometryBinding::EffectGroupSet(const Mortar::SmartPtr<EffectGroup>& group) {
    m_EffectGroup = group;
}

// TODO: re-verify v1.6.1 Geometry ctor (C1/C2) address (v1.5.1 @ 0x001a3c50/0x001a3cc4 stale; dtor confirmed @0x00264f40)
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
#ifndef __bada__
    ++s_LiveGeometry;
#endif
    BuildPropList(props);
}

// v1.6.1 Mortar::Geometry::~Geometry @0x00264f40 (D0/D2)
Geometry::~Geometry() {
#ifndef __bada__
    --s_LiveGeometry;
#endif
    if (m_Vbo) { glDeleteBuffers(1, &m_Vbo); }
    if (m_Ibo) { glDeleteBuffers(1, &m_Ibo); }
}

// v1.6.1 Geometry::Render @0x00264468 -- non-virtual member; called directly by Mesh::Draw.
// DIFFERS: structural -- binary v1.6.1 Geometry::Render @0x00264468 walks m_Binding->GetBindings()[idx].m_PassBindings
//   and re-derives the vertex-array/glDrawElements args per draw; port draws from the load-cached
//   m_Vbo/m_Ibo/m_Layout through the GLES2 Mesh3D shader (Renderer::DrawMesh3D) -- the ES1->ES2
//   translation is the sole allowed deviation, output is byte-equivalent (unlit modulate).
//   Port uses m_DiffuseTex instead of MeshMaterial for texture binding.
//   GeometryBinding_GLES1::PassBinding::Apply v1.6.1 @0x00263b7c (thunk 0x00111e24).
// TODO: v1.6.1 -- binary gates Render on HasActiveEffect; port draws unconditionally (bindings are constructed so the gate would pass)
// Note: the binary's Geometry::Render @0x00264468 consumes only the 3 matrices + DiffuseMap; IsLit/Ambience/Diffuse/Specular
//   are NOT applied to GL at draw (no glMaterialfv/glLightfv import) -- one bare glEnable(GL_LIGHTING) -> GL-default white.
//   The material props are structurally carried but render-dead. All meshes are IsLit=false
//   (MeshManager forces SetValue<bool>("IsLit",false)), so the unlit texture2D * v_color shader
//   IS the fixed-function output; the normal slot is never bound (no normal attribute).
void Geometry::Render(Matrix44 const& mvp) {
    if (!m_Vbo || m_VertCount == 0) return;

    // ASM-spec v1.6.1 Geometry::Render @0x00264468 (+ GlClientStates::Reset @0x00258000): GLES1 3D-mesh pass forces BLEND off + CULL_FACE on. The v1.5.x BeginFrame cull-disable (0x0019e012/66) is GONE in v1.6.1; Reset disables cull at frame top, Render re-enables it, so cull is ON only during 3D mesh draws. Effect PCDX render-states are not consumed by GLES1.
    Renderer* renderer = Renderer::GetInstance();
    renderer->SetBlendEnabled(false);    // f00c one-shot: 3D mesh pass blend OFF
    renderer->SetCullFaceEnabled(true);  // f00d one-shot: 3D mesh pass cull ON
    glCullFace(GL_BACK);        // GL default; binary relies on InitGL/default

    const VertexLayout& L = m_Layout;

    // The FF path bound the texture via m_DiffuseTex->Set() + TexEnvModulate;
    // the shader path passes the raw tex id and DrawMesh3D binds it (0 ->
    // Renderer's 1x1 white texture, reproducing the untextured FF fragment).
    GLuint tex = m_DiffuseTex.IsValid() ? m_DiffuseTex->GetTexId() : 0;

    // Same gate the FF glColorPointer branch used: only colorFmt==3
    // (RGBA8888) binds the colour channel; anything else falls back to the
    // constant white the old glColor4ub(255,255,255,255) provided.
    const int colSize = (L.colorSize > 0 && L.colorFmt == 3) ? L.colorSize : 0;

    renderer->DrawMesh3D(m_Vbo, m_Ibo, m_VertCount, m_IndexCount, m_PrimType,
                         L.totalStride, L.posOffset, L.texOffset, L.colorOffset,
                         L.texSize, colSize, tex, mvp);
}

// v1.6.1 Mortar::Geometry::HasActiveEffect @0x00264440
bool Geometry::HasActiveEffect() const {
    // Defunct: GeometryBinding stack not constructed -- port's binding is always null.
    // Binary: return m_ActiveBindingIdx < m_Binding->GetBindings().size();
    return false;
}

// v1.6.1 Mortar::Geometry::SetActiveEffect @0x00264410
bool Geometry::SetActiveEffect(uint32_t idx) {
    // Defunct: GeometryBinding stack not constructed -- always false in port.
    // Binary: if (idx < size) { m_ActiveBindingIdx = idx; return true; } return false;
    (void)idx;
    return false;
}

// v1.6.1 Mortar::Geometry::BuildPropList @0x00264170
void Geometry::BuildPropList(SmartPtr<SharedEffectProperties> /*props*/) {
    // Defunct: EffectPropertyList not load-bearing in port; m_PropList stays null.
    m_PropList = NULL;
}

// v1.6.1 Mortar::Geometry::GetProperty @0x0025ee7c -- matches binary body exactly.
EffectProperty* Geometry::GetProperty(const char* name) {
    return m_PropList ? m_PropList->GetProperty(name) : NULL;
}

}  // namespace Mortar

#ifndef __bada__
namespace FN {
int GLLiveCount_Geometry() { return Mortar::s_LiveGeometry; }
}
#endif
