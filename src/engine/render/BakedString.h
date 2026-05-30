#ifndef MORTAR_BAKED_STRING_H
#define MORTAR_BAKED_STRING_H

#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include <cstdint>

// Binary layout (sizeof == 0x1C = 28):
//   +0x00  uint32_t        m_unknown0       (struct-only; never touched by ctor/dtor/Draw)
//   +0x04  SmartPtr<Texture2D>* m_pPageTextures (heap block [refcount, count, SmartPtrs x N])
//   +0x08  uint32_t        m_PageCount      (loop bound in dtor + Draw)
//   +0x0C  QUADCUSTOMVERTEX** m_pPageVertices (per-page vertex-buffer array)
//   +0x10  uint32_t*       m_pPageQuadCounts (per-page used-quad counts)
//   +0x14  float           m_Width
//   +0x18  float           m_Height
// Non-polymorphic (no vtable). Two ctor addresses: 0x0019789c / 0x00197d64 (same body).
// Draw @ 0x0019738c. Dtor @ 0x00197564.

namespace Mortar {

class Font;
class Texture2D;
struct QUADCUSTOMVERTEX;
class Vec3;
class Colour;
class Utf8StringIterator;

class BakedString {
public:
    BakedString();
    ~BakedString();

    // Binary @ 0x0019789c — build glyph quads from Font + iterator + colour.
    void Bake(Font* font, float scale, float maxWidth, float z,
              const char* text, const Vec3& pos,
              const Colour& colour, int alignment = 0);

    // Binary @ 0x0019738c — draw pre-baked vertex cache.
    void Draw();

    // Clear cached data (mirrors dtor semantics without destroying the object).
    void Clear();

    bool IsValid() const { return m_PageCount > 0; }

    // Binary field accessors
    float GetWidth()  const { return m_Width; }
    float GetHeight() const { return m_Height; }

    // STUB: BakedString::AddDropShadow -- auto stub from binary missing-symbol set
    void AddDropShadow();
    // STUB: BakedString::LayoutToCircle -- auto stub from binary missing-symbol set
    void LayoutToCircle(float);

private:
    uint32_t            m_unknown0;        // +0x00 struct-only (never written by known methods)
    SmartPtr<Texture2D>* m_pPageTextures;  // +0x04 heap: [refcount, count, SmartPtr<Texture2D> x N]
    uint32_t            m_PageCount;       // +0x08
    QUADCUSTOMVERTEX**  m_pPageVertices;   // +0x0C per-page vertex-buffer array
    uint32_t*           m_pPageQuadCounts; // +0x10 per-page used-quad counts
    float               m_Width;           // +0x14
    float               m_Height;          // +0x18
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(BakedString) == 0x1C, "BakedString sizeof mismatch");
#endif

} // namespace Mortar

#endif
