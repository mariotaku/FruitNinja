#ifndef MORTAR_BAKED_STRING_H
#define MORTAR_BAKED_STRING_H

#include "render/Font.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include <vector>

namespace Mortar {

// Matches original BakedString (~0x1C bytes)
// Pre-baked text vertex cache for fast repeated drawing
class BakedString {
public:
    struct PageData {
        Mortar::SmartPtr<Texture> texture;
        std::vector<QUADCUSTOMVERTEX> vertices;
    };

    std::vector<PageData> m_Pages;
    float m_Width;
    float m_Height;

    BakedString();
    ~BakedString();

    // Bake text into vertex cache using Font::DrawString
    void Bake(Font* font, float scale, float maxWidth, float z,
              const char* text, const Vec3& pos,
              const Colour& colour, int alignment = 0);

    // Draw pre-baked vertices (fast path for repeated draws)
    // Matches BakedString::Draw (0x0019738c)
    void Draw();

    // Clear cached data
    void Clear();

    bool IsValid() const { return !m_Pages.empty(); }

public:

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: BakedString::AddDropShadow -- auto stub from binary missing-symbol set
    void AddDropShadow();
    // STUB: BakedString::LayoutToCircle -- auto stub from binary missing-symbol set
    void LayoutToCircle(float);
    // ---- end AUTO-STUB MERGE ----
};

} // namespace Mortar

#endif
