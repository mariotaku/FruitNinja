#ifndef FN_HUD_CONTROL_H
#define FN_HUD_CONTROL_H

//
// HUDControl — base class for all HUD elements
// Verified from Ghidra: ctor at 0x144104, size = 0x74
// See docs/structs/hud.md for full layout and vtable.
//
// ASM-verified: 2026-04-28T16:35Z binary @ 0x00144104 (asm-inspector)
//

#include "math/Vec3.h"
#include "math/Colour.h"
#include "util/Delegate.h"
#include <cstdint>

struct Renderer;

class HUDControl {
public:
    // +0x04: if 0, SetToMultiplayerState marks for removal; if 1, preserved.
    // Binary uses strb (byte store) @ 0x00143fac.
    uint8_t m_bPreserveOnMP;

    // +0x08: position in centered coords
    Vec3 pos;

    // +0x14: pivot point (rarely used)
    Vec3 pivot;

    // +0x20: size (half-extents)
    Vec3 size;

    // +0x2c: rotation angle / animation timer
    float m_Timer;

    // +0x30: non-zero = visible + receives updates
    uint8_t m_bActive;

    // +0x31
    uint8_t field_0x31;

    // +0x32: if true, HUD won't call destructor on removal
    uint8_t m_bNoDestructor;

    // +0x33: set to true → removed next HUD::Update
    uint8_t m_bPendingRemoval;

    // +0x34: bit mask for layered drawing (default = 1)
    int m_LayerFlags;

    // +0x38: callback fired before removal. 36 bytes (binary Delegate1).
    Mortar::Delegate<void(HUDControl*)> m_RemoveCallback;

    // +0x5c: tint colour (BGRA, default white)
    Colour m_DrawColour;

    // +0x60: set to 1 by HUDControl ctor (binary @ 0x00144162: strb.w r8,[r4,#0x60]
    // with r8=1). Set to 0 by SpeedControl ctor only — opts out of HUD
    // pulse-modulation, gets identity tint vec3(1,1,1).
    uint8_t m_bUseHUDScales;

    // +0x64..+0x70: UV rectangle (belong in HUDControl base per binary layout)
    // HUDControl3d::Draw reads these at binary +0x64/+0x68/+0x6c/+0x70.
    // Binary ctor copies two 8-byte Vec2 globals (GOT 0x000078c0 / 0x00007170)
    // for the (0,0)/(1,1) defaults.
    float m_UVLeft, m_UVTop, m_UVRight, m_UVBottom;

    HUDControl()
        : m_bPreserveOnMP(0),
          m_Timer(0.0f),
          m_bActive(1),
          field_0x31(0),
          m_bNoDestructor(0),
          m_bPendingRemoval(0),
          m_LayerFlags(1),
          m_DrawColour(255, 255, 255, 255),
          m_bUseHUDScales(1),
          m_UVLeft(0.0f), m_UVTop(0.0f), m_UVRight(1.0f), m_UVBottom(1.0f) {}

    virtual ~HUDControl() {}

    // Vtable matches docs/structs/hud.md:
    // +0x00/+0x04: dtors (handled by C++ vtable)
    // +0x08: Init
    // +0x0c: Release
    // +0x10: Reset
    // +0x14: BeginDraw
    // +0x18: PreDraw
    // +0x1c: Draw
    // +0x20: PreDrawOrder (wrapper → PreDraw)
    // +0x24: DrawOrder (wrapper → Draw)
    // +0x28: Update
    // +0x2c: SetToMultiplayerState
    // +0x30: GetType
    // +0x34: Skip
    // +0x38: Save

    virtual void Init() {}
    virtual void Release() {}
    virtual void Reset() {}
    virtual void BeginDraw(float dt) { (void)dt; }
    virtual void PreDraw(const Vec3& hudScale) { (void)hudScale; }
    virtual void Draw(const Vec3& hudScale, int layerMask) { (void)hudScale; (void)layerMask; }
    virtual void PreDrawOrder(const Vec3& hudScale, int layerMask) { PreDraw(hudScale); (void)layerMask; }
    virtual void DrawOrder(const Vec3& hudScale, int layerMask) { Draw(hudScale, layerMask); }
    virtual void Update(float dt) { (void)dt; }
    // Binary @ 0x00143fac returns bool; port vtable uses void — side-effects preserved.
    // DIFFERS: return value dropped (vtable ABI uses void in port).
    virtual void SetToMultiplayerState() {
        if (m_bPreserveOnMP == 0) {
            m_bNoDestructor = 0;
            m_bPendingRemoval = 1;
        }
    }
    virtual int GetType() { return 0; }
    virtual void Skip() {}
    virtual void Save() {}

    void SetPendingRemoval() { m_bPendingRemoval = 1; }

    // Binary: called after HUD::AddControl to pin the control to a single
    // layer slot instead of cycling. Tier-1 stub — full RE pending.
    // Binary addr not yet resolved (referenced from PauseScreen::Update lazy-create block).
    void SetSingular() {}
};

#endif
