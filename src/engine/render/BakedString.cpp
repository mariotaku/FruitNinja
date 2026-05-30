#include "render/BakedString.h"
#include "render/QUADCUSTOMVERTEX.h"

namespace Mortar {

// Binary @ 0x0019789c (default ctor — zero-init)
BakedString::BakedString()
    : m_unknown0(0)
    , m_pPageTextures(0)
    , m_PageCount(0)
    , m_pPageVertices(0)
    , m_pPageQuadCounts(0)
    , m_Width(0.0f)
    , m_Height(0.0f)
{
}

// Binary @ 0x00197564
BakedString::~BakedString() {
    Clear();
}

// Binary @ 0x0019789c — full Bake not yet ported (uses internal glyph-quad capture path)
// TODO: 0x0019789c — port the BakedString(Font*, Utf8StringIterator, Colour) ctor body:
//   allocates m_pPageTextures heap block, per-page QUADCUSTOMVERTEX arrays, and
//   m_pPageQuadCounts; sets m_Width/m_Height from Font metrics.
void BakedString::Bake(Font* /*font*/, float /*scale*/, float /*maxWidth*/, float /*z*/,
                       const char* /*text*/, const Vec3& /*pos*/,
                       const Colour& /*colour*/, int /*alignment*/) {
    Clear();
}

// Binary @ 0x0019738c
void BakedString::Draw() {
    // TODO: 0x0019738c — iterate m_PageCount pages; bind texture from m_pPageTextures[i];
    //   draw m_pPageQuadCounts[i] quads from m_pPageVertices[i] using the GL pipeline.
}

void BakedString::Clear() {
    if (m_pPageVertices) {
        for (uint32_t i = 0; i < m_PageCount; i++) {
            delete[] m_pPageVertices[i];
        }
        delete[] m_pPageVertices;
        m_pPageVertices = 0;
    }
    if (m_pPageQuadCounts) {
        delete[] m_pPageQuadCounts;
        m_pPageQuadCounts = 0;
    }
    // m_pPageTextures block is freed via SmartPtr refcount logic in the binary dtor;
    // port uses plain heap allocation convention for now.
    // TODO: 0x00197564 — dtor frees m_pPageTextures as (ptr-2) after destroying N SmartPtrs
    //   using the embedded count at ptr[-1]. Implement proper ref-counted page-texture block.
    m_pPageTextures = 0;
    m_PageCount = 0;
    m_Width = 0.0f;
    m_Height = 0.0f;
}

// STUB: BakedString::AddDropShadow -- auto stub from binary missing-symbol set
void BakedString::AddDropShadow() {}
// STUB: BakedString::LayoutToCircle -- auto stub from binary missing-symbol set
void BakedString::LayoutToCircle(float) {}

} // namespace Mortar
