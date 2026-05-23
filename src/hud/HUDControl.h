#ifndef FN_HUD_CONTROL_H
#define FN_HUD_CONTROL_H

//
// HUDControl — base class for all HUD elements
// Verified from Ghidra: ctor at 0x144104, size = 0x74
//

#include "math/Vec3.h"
#include "math/Colour.h"
#include "util/Delegate.h"
#include <cstdint>
#ifndef __bada__
#include <list>
#endif

struct Renderer;

class HUDControl {
public:
    // +0x04: if 0, SetToMultiplayerState marks for removal; if 1, preserved.
    // Binary uses strb (byte store) @ 0x00143fac.
    uint8_t m_Singular;

    // +0x08: position in centered coords
    Vec3 pos;

    // +0x14: per-control HUD-scale multiplier. Multiplied with the
    // (480, 320, 0) screen-anchor in HUDControl3d::Draw / MissControl::Draw
    // / etc. before the per-control pos translate. Named "pivot" in earlier
    // port iterations -- the binary's actual semantic is m_HudScale, set
    // per-frame by the PreDraw chain (per-instance hudScale arg).
    Vec3 m_HudScale;

    // +0x20: size (half-extents)
    Vec3 size;

    // +0x2c: rotation angle / animation timer
    float m_Timer;

    // +0x30: non-zero = visible + receives updates
    uint8_t m_Active;

    // +0x31
    uint8_t field_0x31;

    // +0x32: if true, HUD won't call destructor on removal
    uint8_t m_bNoDestructor;

    // +0x33: set to true → removed next HUD::Update
    uint8_t m_bPendingRemoval;

    // +0x34: bit mask for layered drawing (default = 1)
    int m_LayerFlags;

    // +0x38: callback fired before removal. 36 bytes (binary Mortar::Delegate1).
    Mortar::Delegate1<void, HUDControl*> m_RemoveCallback;

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

    HUDControl();
    virtual ~HUDControl();

#ifndef __bada__
    // Port specific: debug registry — iterate all currently active HUDControls.
    // Populated by HUDControl ctor / cleared by HUDControl dtor. Covers all subclasses.
    static const std::list<HUDControl*>& GetActiveControls();
#endif

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

    virtual void Init();
    virtual void Release();
    virtual void Reset();
    virtual void BeginDraw(float dt) { (void)dt; }
    virtual void PreDraw(const Vec3& hudScale) { (void)hudScale; }
    virtual void Draw(const Vec3& hudScale, int layerMask) { (void)hudScale; (void)layerMask; }
    virtual void PreDrawOrder(const Vec3& hudScale, int layerMask) { PreDraw(hudScale); (void)layerMask; }
    virtual void DrawOrder(const Vec3& hudScale, int layerMask) { Draw(hudScale, layerMask); }
    virtual void Update(float dt);
    // Binary @ 0x00143fac — returns true if this control should be removed (m_Singular == 0).
    virtual bool SetToMultiplayerState();
    virtual int GetType() { return 0; }
    virtual void Skip() {}
    virtual void Save() {}

    void SetPendingRemoval() { m_bPendingRemoval = 1; }

    // Port specific: debug overlay (F1 hitbox toggle) needs the effective
    // draw-space position, not the raw `pos` field. Subclasses whose Draw
    // transforms `pos` through a non-identity anchor override this so the
    // overlay AABB matches the rendered quad. Default returns `pos` unmodified.
    // Not in binary; appended after binary vtable.
    virtual Vec3 GetDrawPos() const { return pos; }

    void SetSingular() {
        m_Singular = 1;
        // ASM-verified: 2026-05-24 binary @ 0x0014dda8 (re-analyst)
    }

    void SetActive(bool b) {
        m_Active = b ? 1 : 0;
        // ASM-verified: 2026-05-24 binary @ 0x0013cdd0 (re-analyst)
    }
};

#endif
