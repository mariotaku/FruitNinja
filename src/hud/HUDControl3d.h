#ifndef FN_HUD_CONTROL_3D_H
#define FN_HUD_CONTROL_3D_H

//
// HUDControl3d : HUDControl (size = 0x7C)
// Verified from Ghidra: ctor at 0x1443f4, Draw at 0x14428c (57 lines)
//
// Binary layout (ARM32):
//   +0x00..+0x73: HUDControl base (0x74 bytes, includes UV floats at +0x64..+0x70)
//   +0x74: Mortar::SmartPtr<Texture> m_Texture  — the texture drawn by HUDControl3d::Draw base
//   +0x78: Mortar::SmartPtr<Model>   m_Model    — 3D mesh slot, RESERVED; base Draw never reads it;
//                                                  no FN subclass observed writing it.
//                                                  Dtor @ 0x00144474 calls SmartPtr<Model>::~SmartPtr
//                                                  on this+0x78 confirming the type.
//
// ASM-verified (slot semantics, not full ASM diff): 2026-05-18 binary @ 0x0014428c
//   - reads SmartPtr<Texture> at +0x74 (called m_Texture in this port)
//   - +0x78 is SmartPtr<Mortar::Model>, NOT a second texture; never read by base Draw
// ASM-verified: 2026-04-28T16:35Z binary @ 0x001443f4 (asm-inspector) -- ctor layout only
//

#include "HUDControl.h"
#include "render/gl_funcs.h"
#include "asset/Texture.h"
#include "asset/Mesh.h"
#include "util/SmartPtr.h"

class HUDControl3d : public HUDControl {
public:
    // +0x74: m_Texture (Mortar::SmartPtr<Texture>).
    // THIS is the texture gate-checked and drawn by HUDControl3d::Draw base.
    Mortar::SmartPtr<Mortar::Texture> m_Texture;

    // +0x78: m_Model (Mortar::SmartPtr<Model>).
    // RESERVED — 3D mesh slot present in the engine; base Draw never reads it;
    // no HUDControl3d subclass in FruitNinja writes it. Preserved for binary sizeof = 0x7c.
    // Dtor @ 0x00144474 calls SmartPtr<Model>::~SmartPtr on this+0x78.
    Mortar::SmartPtr<Mortar::Model> m_Model;

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
