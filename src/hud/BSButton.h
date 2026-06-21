#ifndef FN_HUD_BSBUTTON_H
#define FN_HUD_BSBUTTON_H

//
// BSButton : HUDControl3d  (size = 0xe8)
// Binary ctor @ 0x15eb58  BSButton(Vec3 pos, char const* label, Vec3 textOffset)
// operator new(0xe8) @ DojoScreen::DojoScreen 0x16bad8
// vtable base 0x2ccaf0, stored vptr = base+8 (0x2ccaf8)
// 18 slots: HUDControl3d's 16 + 2 new virtuals (slot16=SetVisible, slot17=SetDrawOrder)
//

#include "HUDControl3d.h"
#include "engine/asset/Texture.h"
#include "engine/util/Delegate.h"
#include "engine/util/SmartPtr.h"
#include "engine/math/Vec3.h"
#include <cstdint>
#include <cstddef>

namespace Mortar { class FontCacheObjectTTF; class BakedStringBox; }

class BSButton : public HUDControl3d {
public:
    // HUDControl3d ends at +0x7b (size = 0x7c). BSButton own fields follow.

    // +0x7c: button texture SmartPtr (null-init in ctor; SetPtr(null) in Init)
    Mortar::SmartPtr<Mortar::Texture> m_Texture2;   // +0x7c

    // +0x80: raw label string pointer (ctor arg)
    const char* m_pLabel;                           // +0x80

    // +0x84: constructed BakedStringBox* (created in Init, NOT ctor)
    Mortar::BakedStringBox* m_pLabelBox;            // +0x84

    // +0x88: active touch id (-1 = none)
    int m_TouchId;                                  // +0x88

    // +0x8c: last touch X (written by UpdateTouchPosition free fn 0x10b32c)
    float m_TouchX;                                 // +0x8c

    // +0x90: last touch Y (written by UpdateTouchPosition free fn 0x10b32c)
    float m_TouchY;                                 // +0x90

    // +0x94: padding between touch Y and extents
    uint8_t _pad94[4];                              // +0x94

    // +0x98: half-width (extent X) -- Update hit-test uses this
    float m_ExtentX;                                // +0x98

    // +0x9c: half-height (extent Y) -- Update hit-test uses this
    float m_ExtentY;                                // +0x9c

    // +0xa0: padding between extents and textOffset
    uint8_t _pada0[4];                              // +0xa0

    // +0xa4: text offset Vec3 (ctor arg, stored via Vec3 ctor)
    Vec3 m_TextOffset;                              // +0xa4..+0xaf

    // +0xb0: draw text-offset / rotation seed Vec3
    //   +0xb0: rotation angle seed (Draw reads as angle for optional Z-rotation)
    //   +0xb4: label translate offset (Draw reads as translation X)
    //   +0xb8: (z component, Init writes 0)
    Vec3 m_DrawRotation;                            // +0xb0..+0xbb

    // +0xbc: padding to +0xc0
    uint8_t _padbc[4];                              // +0xbc

    // +0xc0: click callback Delegate0<void> (StackAllocatedPointer, 36 bytes)
    Mortar::Delegate0<void> m_ClickCallback;        // +0xc0..+0xe3

    // +0xe4: enabled flag (1 in ctor; slot16=SetVisible writes here)
    uint8_t m_bEnabled;                             // +0xe4

    // +0xe5: pressed/highlight flag (0 in ctor)
    uint8_t m_bPressed;                             // +0xe5

    // +0xe6: text-colour-override alpha index (0xff in ctor = "no override")
    uint8_t m_AlphaOverride;                        // +0xe6

    // +0xe7: padding to 0xe8
    uint8_t _pade7;                                 // +0xe7

    // ctor: BSButton(Vec3 pos, char const* label, Vec3 textOffset)
    // Binary @ 0x15eb58
    BSButton(Vec3 pos, const char* label, Vec3 textOffset);
    virtual ~BSButton();                            // slot 0/1

    // slot 2: Init -- builds BakedStringBox, sets touch id, drawOrder
    // Binary @ 0x15ea40
    void Init() override;

    // slot 3: Release -- destroys BakedStringBox, zeroes +0x84
    // Binary @ 0x15e5c0
    void Release() override;

    // slot 6: PreDraw -- empty no-op override
    // Binary @ 0x15e468: bx lr
    void PreDraw(const Vec3& hudScale) override;

    // slot 7: Draw -- textured quad + label
    // Binary @ 0x15e60c
    void Draw(const Vec3& hudScale, int layerMask) override;

    // slot 10: Update -- touch hit-test + click fire
    // Binary @ 0x15e470
    void Update(float dt) override;

    // slot 12: GetType -> 5
    // Binary @ 0x15f3e4: mov r0,#5; bx lr
    int GetType() override { return 5; }

    // slot 16: SetVisible -- writes enabled flag at +0xe4
    // Binary @ 0x15f3ec: strb r1,[r0,#0xe4]
    virtual void SetVisible(bool v);

    // slot 17: SetDrawOrder -- writes drawOrder at +0x34
    // Binary @ 0x15f3f4: str r1,[r0,#0x34]
    virtual void SetDrawOrder(int order);

    // Non-virtual setters (binary call sites in PauseScreen::Update @0x001a5ebc)
    // ASM-spec v1.6.1 PauseScreen::Update @0x001a5ebc: direct member writes.

    // Stores callback Delegate0 into m_ClickCallback (+0xc0).
    void SetCallback(const Mortar::Delegate0<void>& cb);

    // Stores tex into m_Texture2 (+0x7c). flag arg: binary passes true;
    // stored at m_bEnabled (+0xe4) on the BSButton -- but callers in PauseScreen
    // always set active separately, so this is treated as (void)flag for now.
    // TODO: v1.6.1 BSButton::SetTexture @unknown -- confirm bool flag field (may be m_bEnabled or a separate member).
    void SetTexture(Mortar::SmartPtr<Mortar::Texture> tex, bool flag);

    // Writes base pos field (+0x08).
    void SetPosition(const Vec3& p);

    // Writes label offset: .y -> m_DrawRotation.y (+0xb4), .z -> m_DrawRotation.z (+0xb8).
    // (Draw reads m_DrawRotation.y/.z as label translate offset.)
    void SetTextOffset(const Vec3& o);

private:
    // BSButton::UpdateTouchPosition -- copy latched slot's live touch pos into m_TouchX/m_TouchY.
    // Binary @ 0x15e428 (thunked via 0x10b32c). Member thiscall in binary.
    void UpdateTouchPosition();
};

#ifdef __bada__
#include <cstddef>
static_assert(__builtin_offsetof(BSButton, m_Texture2)   == 0x7c, "BSButton m_Texture2 offset");
static_assert(__builtin_offsetof(BSButton, m_pLabel)     == 0x80, "BSButton m_pLabel offset");
static_assert(__builtin_offsetof(BSButton, m_pLabelBox)  == 0x84, "BSButton m_pLabelBox offset");
static_assert(__builtin_offsetof(BSButton, m_TouchId)    == 0x88, "BSButton m_TouchId offset");
static_assert(__builtin_offsetof(BSButton, m_TouchX)     == 0x8c, "BSButton m_TouchX offset");
static_assert(__builtin_offsetof(BSButton, m_TouchY)     == 0x90, "BSButton m_TouchY offset");
static_assert(__builtin_offsetof(BSButton, m_ExtentX)    == 0x98, "BSButton m_ExtentX offset");
static_assert(__builtin_offsetof(BSButton, m_ExtentY)    == 0x9c, "BSButton m_ExtentY offset");
static_assert(__builtin_offsetof(BSButton, m_TextOffset) == 0xa4, "BSButton m_TextOffset offset");
static_assert(__builtin_offsetof(BSButton, m_DrawRotation) == 0xb0, "BSButton m_DrawRotation offset");
static_assert(__builtin_offsetof(BSButton, m_ClickCallback) == 0xc0, "BSButton m_ClickCallback offset");
static_assert(__builtin_offsetof(BSButton, m_bEnabled)   == 0xe4, "BSButton m_bEnabled offset");
static_assert(__builtin_offsetof(BSButton, m_bPressed)   == 0xe5, "BSButton m_bPressed offset");
static_assert(__builtin_offsetof(BSButton, m_AlphaOverride) == 0xe6, "BSButton m_AlphaOverride offset");
static_assert(sizeof(BSButton) == 0xe8, "BSButton sizeof mismatch");
#endif

#endif // FN_HUD_BSBUTTON_H
