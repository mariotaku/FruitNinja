#ifndef FN_HUD_CONTROL_3D_H
#define FN_HUD_CONTROL_3D_H

//
// HUDControl3d : HUDControl (size = 0x7C)
// Zero-divergence verified 2026-05-24.
//
// Binary layout (ARM32):
//   +0x00..+0x73: HUDControl base (0x74 bytes, includes UV floats at +0x64..+0x70)
//   +0x74: Mortar::SmartPtr<Texture> m_Texture  — drawn by HUDControl3d::Draw base
//   +0x78: Mortar::SmartPtr<Model>   m_Model    — 3D mesh slot, RESERVED; base Draw never reads it;
//                                                  no FN subclass observed writing it.
//                                                  Dtor D1 @0x0018b814 calls SmartPtr<Model>::~SmartPtr
//                                                  on this+0x78 confirming the type.
//
// ASM-verified: 2026-05-24 v1.6.1 HUDControl3d C2 @0x0018b6dc / C1 @0x0018b72c (ctors),
//   D0 @0x0018b77c / D1 @0x0018b814 / D2 @0x0018b8a4 (dtors), Release @0x0018b134,
//   PreDraw @0x0018b138, Update @0x0018b13c, Draw @0x0018b544 (re-analyst)
// NOTE: the HUDControl BASE ctor is a separate symbol (C1 @0x0018b354 / C2 @0x0018b440)
//   and is documented in HUDControl.h, not here.
//

#include "HUDControl.h"
#include "render/gl_funcs.h"
#include "asset/Texture.h"
#include "asset/Mesh.h"
#include "asset/Model.h"
#include "util/SmartPtr.h"

class HUDControl3d : public HUDControl {
public:
    // +0x74: m_Texture (Mortar::SmartPtr<Texture>).
    // THIS is the texture gate-checked and drawn by HUDControl3d::Draw base.
    Mortar::SmartPtr<Mortar::Texture> m_Texture;

    // +0x78: m_Model (Mortar::SmartPtr<Model>).
    // RESERVED — 3D mesh slot present in the engine; base Draw never reads it;
    // no HUDControl3d subclass in FruitNinja writes it. Preserved for binary sizeof = 0x7c.
    // Dtor D1 @0x0018b814 calls SmartPtr<Model>::~SmartPtr on this+0x78.
    Mortar::SmartPtr<Mortar::Model> m_Model;

    HUDControl3d();

    // vtable +0x1c: Draw — v1.6.1 HUDControl3d::Draw @0x0018b544
    void Draw(float* hudScaleRaw) override;

    // vtable +0x30: returns 1.
    int GetType() override { return 1; }

    // Dtor — v1.6.1 D0 @0x0018b77c (deleting) / D1 @0x0018b814 / D2 @0x0018b8a4.
    virtual ~HUDControl3d();

    // Vtable overrides (all verified — see .cpp markers).
    void Release() override;                       // v1.6.1 @0x0018b134: bx lr
    void PreDraw(float* hudScale) override;         // v1.6.1 @0x0018b138: bx lr
    void Update(float dt) override;                // v1.6.1 @0x0018b13c: tail-calls base
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(HUDControl3d) == 0x7c, "HUDControl3d size mismatch"); // v1.6.1 HUDControl3d @0x149084
#endif

#endif
