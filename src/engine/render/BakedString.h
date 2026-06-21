#ifndef MORTAR_BAKED_STRING_H
#define MORTAR_BAKED_STRING_H

#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/Vec3.h"
#include "math/Colour.h"
#include "render/Utf8StringIterator.h"
#include <cstdint>

// Binary layout (sizeof == 0x1C = 28):
//   +0x00  uint32_t        m_unknown0       (struct-only; never touched by ctor/dtor/Draw)
//   +0x04  SmartPtr<Texture>* m_pPageTextures (heap block [tag, count, SmartPtrs x N])
//   +0x08  uint32_t        m_PageCount      (loop bound in dtor + Draw)
//   +0x0C  QUADCUSTOMVERTEX** m_pPageVertices (per-page vertex-buffer array)
//   +0x10  uint32_t*       m_pPageQuadCounts (per-page used-vertex counts)
//   +0x14  float           m_Width
//   +0x18  float           m_Height
// Non-polymorphic (no vtable). Two ctor addresses: 0x0019789c / 0x00197d64 (same body).
// Draw @ 0x0019738c. Dtor @ 0x00197564.

struct QUADCUSTOMVERTEX;

namespace Mortar {

class Font;
class Texture;

class BakedString {
public:
    BakedString();

    // Binary @ 0x0019789c — build glyph-quad page cache from Font + iterator + colour.
    // Faithful port of the binary constructor (the port keeps a default ctor + this
    // explicit Bake() entry instead of an overloaded ctor so existing default-construct
    // call sites stay valid).
    BakedString(Font* font, Utf8StringIterator iter, const Colour& colour);

    ~BakedString();

    // Binary @ 0x0019789c — (re)build the page cache. Same body as the ctor above.
    void Bake(Font* font, Utf8StringIterator iter, const Colour& colour);

    // Binary @ 0x0019738c — draw the pre-baked vertex cache.
    //   scale  : uniform world-units-per-em-height (s0)
    //   rotZ   : Z rotation in degrees (s1)
    //   align  : alignment flag bits (r2) — bits 0..1 horiz, bits 2..3 vert
    //   pos    : world-space anchor (r1, by-ref Vec3*)
    void Draw(float scale, float rotZ, uint32_t align, const Vec3& pos);

    // Clear cached data (mirrors dtor semantics without destroying the object).
    void Clear();

    bool IsValid() const { return m_PageCount > 0; }

    // Binary field accessors
    float GetWidth()  const { return m_Width; }
    float GetHeight() const { return m_Height; }

    // Binary @ 0x001971c8 — genuine no-op in the shipped Bada build (single `bx lr`).
    void AddDropShadow();

    // TODO: 0x0019762c -- LayoutToCircle: arrange baked glyph quads onto a circular arc.
    void LayoutToCircle(float);

private:
    uint32_t            m_unknown0;        // +0x00 struct-only (never written by known methods)
    // +0x04 heap block: [tag@-2, count@-1, SmartPtr<Texture> x N]. Pointer stored is &block[2].
    // Port specific: binary types this SmartPtr<Texture2D>; the port merged Texture2D
    // into Texture (see Texture.h), so the page-texture refs are SmartPtr<Texture>.
    SmartPtr<Texture>*  m_pPageTextures;   // +0x04
    uint32_t            m_PageCount;       // +0x08
    QUADCUSTOMVERTEX**  m_pPageVertices;   // +0x0C per-page vertex-buffer array
    uint32_t*           m_pPageQuadCounts; // +0x10 per-page used-vertex counts (6 per glyph)
    float               m_Width;           // +0x14
    float               m_Height;          // +0x18
};

#if defined(__bada__)
static_assert(sizeof(BakedString) == 0x1C, "BakedString sizeof mismatch");
#endif

} // namespace Mortar

#endif
