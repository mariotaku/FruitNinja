#ifndef FN_HUD_CONTROL_3D_H
#define FN_HUD_CONTROL_3D_H

//
// HUDControl3d : HUDControl (size = 0x7C)
// Verified from Ghidra: ctor at 0x1443f4, Draw at 0x14428c (57 lines)
// See docs/structs/hud.md for full layout.
//
// Binary layout (ARM32):
//   +0x00..+0x73: HUDControl base (0x74 bytes, includes UV floats at +0x64..+0x70)
//   +0x74: Mortar::SmartPtr<Texture> m_Texture     (NOT used by HUDControl3d::Draw base)
//   +0x78: Mortar::SmartPtr<Texture> m_SecondaryTex (the texture drawn by HUDControl3d::Draw)
//
// Draw reads +0x78 (m_SecondaryTex), not +0x74.  Subclasses that need a primary
// tex (MenuButton, MissControl) write m_Texture and either override Draw or use
// their own path.  The 2026-04-28 ASM-verified marker on Draw was a false positive.
//
// ASM-verified: 2026-04-28T16:35Z binary @ 0x001443f4 (asm-inspector) -- ctor layout only
//

#include "HUDControl.h"
#include "render/gl_funcs.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class HUDControl3d : public HUDControl {
public:
    // +0x74: m_Texture (Mortar::SmartPtr<Texture>, matches binary).
    // NOT read by HUDControl3d::Draw -- base Draw uses m_SecondaryTex.
    // Subclasses that need this slot write it and use their own draw path.
    Mortar::SmartPtr<Mortar::Texture> m_Texture;

    // +0x78: m_SecondaryTex (Mortar::SmartPtr<Texture>, matches binary).
    // THIS is the texture gate-checked and drawn by HUDControl3d::Draw base.
    // BonusScreen, and any subclass relying on base Draw, loads here.
    Mortar::SmartPtr<Mortar::Texture> m_SecondaryTex;

    HUDControl3d();

    // vtable +0x1c: Draw — matches 0x14428c (57 lines)
    void Draw(const Vec3& hudScale, int layerMask) override;

    // vtable +0x30
    int GetType() override { return 1; }

    // ---- STUBS (binary) ----
    // STUB: HUDControl3d::~HUDControl3d -- binary @ 0x???? (TODO RE)
    virtual ~HUDControl3d();
    // STUB: HUDControl3d::Release -- binary @ 0x???? (TODO RE)
    void Release() override;
    // STUB: HUDControl3d::PreDraw -- binary @ 0x???? (TODO RE)
    void PreDraw(const Vec3& hudScale) override;
    // STUB: HUDControl3d::Update -- binary @ 0x???? (TODO RE)
    void Update(float dt) override;
    // ---- end STUBS ----
};

#endif
