#ifndef FN_HUD_CONTROL_3D_H
#define FN_HUD_CONTROL_3D_H

//
// HUDControl3d : HUDControl (size = 0x7C)
// Verified from Ghidra: ctor at 0x1443f4, Draw at 0x14428c (57 lines)
// See docs/structs/hud.md for full layout.
//
// Binary layout (ARM32):
//   +0x00..+0x73: HUDControl base (0x74 bytes, includes UV floats at +0x64..+0x70)
//   +0x74: Mortar::SmartPtr<Texture> m_Texture    (primary display texture)
//   +0x78: Mortar::SmartPtr<Texture> m_SecondaryTex (secondary; initialized but unused by Draw)
//
// UV floats live in HUDControl base (not here), per ctor ASM at 0x001443f4
// which only touches +0x74 and +0x78.
//
// ASM-verified: 2026-04-28T16:35Z binary @ 0x001443f4 (asm-inspector)
//

#include "HUDControl.h"
#include "render/gl_funcs.h"

class HUDControl3d : public HUDControl {
public:
    // +0x74: main display texture (binary: Mortar::SmartPtr<Texture>; port: raw GLuint)
    // Gate: nullptr/0 = don't draw (checked first in Draw)
    GLuint m_Texture;

    // +0x78: secondary texture (binary: Mortar::SmartPtr<Texture>; port: raw GLuint)
    // Initialized to 0 by ctor; not used by HUDControl3d::Draw.
    // Subclasses (TutorialControl) use this slot for their own textures.
    GLuint m_SecondaryTex;

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
