#ifndef FN_HUD_INGAMEPOPUP_H
#define FN_HUD_INGAMEPOPUP_H

// IngamePopup -- pre-baked text/texture overlay keyed by type id.
// Binary: v1.6.1 IngamePopup @0x0016dbac (ctor), @0x0016d3ec (Draw),
//         @0x0016db38 (DeleteAllPopups), @0x0016e578 (BuildAllPopups).
// NON-VIRTUAL: plain dtor, no vtable.
// sizeof = 0x44 (68 bytes) on ARM32.
//
// Type IDs used by live consumers:
//   0x0F  NEW BEST SCORE banner   -- drawn by ScoreControl::PreDraw
//   0x10  shop NEW badge          -- drawn by ShopListItem::DrawFloatingText
//   0x11  shop SELECTED badge     -- drawn by ShopListItem::DrawFloatingText
//   0x00  combo popup (on-demand) -- TODO: created elsewhere, skip this pass

#include "engine/math/Vec3.h"
#include "engine/math/Colour.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include <vector>
#include <cstddef>

namespace Mortar {
class BakedStringBox;
class Texture;
}

class IngamePopup {
public:
    // ctor v1.6.1 IngamePopup @0x0016dbac
    explicit IngamePopup(int type);
    ~IngamePopup();

    // Draw v1.6.1 IngamePopup::Draw @0x0016d3ec
    // pos = world-space anchor (by value, Vec3 first per binary mangled sig).
    // scale = animation curve supplied by caller (IngamePopup has no Update).
    void Draw(Vec3 pos, float scale);

    // +0x00
    std::vector<Mortar::BakedStringBox*> m_TextBoxes;       // +0x00 (12 bytes ARM32)
    // +0x0c
    std::vector<Vec3>                    m_TextPositions;    // +0x0c (12 bytes ARM32)
    // +0x18
    std::vector<Mortar::SmartPtr<Mortar::Texture>> m_Textures;     // +0x18 (12 bytes ARM32)
    // +0x24
    std::vector<Vec3>                    m_TexturePositions; // +0x24 (12 bytes ARM32)
    // +0x30
    std::vector<Vec3>                    m_TextureScales;    // +0x30 (12 bytes ARM32)
    // +0x3c
    int   m_Type;           // +0x3c
    // +0x40
    float m_VerticalOffset; // +0x40
};

#ifdef __bada__
static_assert(offsetof(IngamePopup, m_TextBoxes)        == 0x00, "IngamePopup::m_TextBoxes");
static_assert(offsetof(IngamePopup, m_TextPositions)    == 0x0c, "IngamePopup::m_TextPositions");
static_assert(offsetof(IngamePopup, m_Textures)         == 0x18, "IngamePopup::m_Textures");
static_assert(offsetof(IngamePopup, m_TexturePositions) == 0x24, "IngamePopup::m_TexturePositions");
static_assert(offsetof(IngamePopup, m_TextureScales)    == 0x30, "IngamePopup::m_TextureScales");
static_assert(offsetof(IngamePopup, m_Type)             == 0x3c, "IngamePopup::m_Type");
static_assert(offsetof(IngamePopup, m_VerticalOffset)   == 0x40, "IngamePopup::m_VerticalOffset");
static_assert(sizeof(IngamePopup)                       == 0x44, "IngamePopup sizeof");
#endif

// BuildAllPopups v1.6.1 @0x0016e578
// Creates types 0x11, 0x10, 0x0F via new+ctor; stores at pM_Popups[type].
// Called from PreloadRings.
void BuildAllPopups();

// DeleteAllPopups v1.6.1 @0x0016db38
// Deletes every non-null pM_Popups[i]. Called from GameDestroy.
void DeleteAllPopups();

// Port accessor: returns the popup for the given type id, or null if not built yet.
// Not in the binary (binary accesses game_work.pM_Popups[type] directly).
// Port specific: needed because game_work.m_Popups is a reserved byte array.
IngamePopup* GetIngamePopup(int type);

#endif // FN_HUD_INGAMEPOPUP_H
