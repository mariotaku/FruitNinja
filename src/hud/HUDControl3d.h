#ifndef FN_HUD_CONTROL_3D_H
#define FN_HUD_CONTROL_3D_H

//
// HUDControl3d : HUDControl (size = 0x7C)
// Zero-divergence verified 2026-05-24 — see tmp/symdiff/hudcontrol3d-spec.md.
//
// Binary layout (ARM32):
//   +0x00..+0x73: HUDControl base (0x74 bytes, includes UV floats at +0x64..+0x70)
//   +0x74: Mortar::SmartPtr<Texture> m_Texture  — drawn by HUDControl3d::Draw base
//   +0x78: Mortar::SmartPtr<Model>   m_Model    — 3D mesh slot, RESERVED; base Draw never reads it;
//                                                  no FN subclass observed writing it.
//                                                  Dtor @ 0x00144474 calls SmartPtr<Model>::~SmartPtr
//                                                  on this+0x78 confirming the type.
//
// ASM-verified: 2026-05-24 binary @ 0x001443f4 / 0x00144434 (ctors), 0x00144474 /
//   0x001444e0 / 0x00144548 (dtors), 0x00143fc4 (Release), 0x00143fc8 (PreDraw),
//   0x00143fcc (Update), 0x0014428c (Draw) (re-analyst)
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

    // vtable +0x1c: Draw — binary @ 0x0014428c
    void Draw(const Vec3& hudScale, int layerMask) override;

    // vtable +0x30: returns 1.
    int GetType() override { return 1; }

    // Dtor — binary @ 0x00144474 (D0) / 0x001444e0 (D1) / 0x00144548 (D2).
    virtual ~HUDControl3d();

    // Vtable overrides (all verified — see .cpp markers).
    void Release() override;                       // binary @ 0x00143fc4: bx lr
    void PreDraw(const Vec3& hudScale) override;   // binary @ 0x00143fc8: bx lr
    void Update(float dt) override;                // binary @ 0x00143fcc: tail-calls base
};

#endif
