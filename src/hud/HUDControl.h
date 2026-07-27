#ifndef FN_HUD_CONTROL_H
#define FN_HUD_CONTROL_H

//
// HUDControl — base class for all HUD elements
// v1.6.1 HUDControl::HUDControl C1 @0x0018b354 / C2 @0x0018b440, size = 0x74.
// TODO: v1.6.1 HUDControl -- the remaining bare `binary @ 0x0014xxxx` citations in this
//   header / HUDControl.cpp are UNVERIFIED v1.5-era leftovers; re-resolve before use.
//

#include "math/_Vector3.h"
#include "math/Colour.h"
#include "util/Delegate.h"
#include <cstdint>
#ifndef __bada__
#include <list>
#endif

struct Renderer;
namespace Mortar { class MortarSound; }
#ifndef __bada__
class ScrollingMenu;
#endif

class HUDControl {
public:
    // +0x04: if 0, SetToMultiplayerState marks for removal; if 1, preserved.
    // Binary uses strb (byte store) @ 0x00143fac.
    uint8_t m_Singular;

    // +0x08: position in centered coords
    _Vector3<float> pos;

    // +0x14: per-control HUD-scale multiplier. Multiplied with the
    // (480, 320, 0) screen-anchor in HUDControl3d::Draw / MissControl::Draw
    // / etc. before the per-control pos translate. Named "pivot" in earlier
    // port iterations -- the binary's actual semantic is m_HudScale, set
    // per-frame by the PreDraw chain (per-instance hudScale arg).
    _Vector3<float> m_HudScale;

    // +0x20: size (half-extents)
    _Vector3<float> size;

    // +0x2c: rotation angle / animation timer
    float m_Timer;

    // +0x30: non-zero = visible + receives updates
    uint8_t m_Active;

    // +0x31: written 0 by ctor; no read site identified. Reserved.
    uint8_t m_reserved31;  // purpose unknown

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

    // Vtable:
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
    virtual void PreDraw(float* hudScale) { (void)hudScale; }
    virtual void Draw(float* hudScale) { (void)hudScale; }
    virtual void PreDrawOrder(float* hudScale, int layerMask) { PreDraw(hudScale); (void)layerMask; }
    virtual void DrawOrder(float* hudScale, int layerMask) { Draw(hudScale); (void)layerMask; }
    virtual void Update(float dt);
    // Binary @ 0x00143fac — returns true if this control should be removed (m_Singular == 0).
    virtual bool SetToMultiplayerState();
    virtual int GetType() { return 0; }
    virtual void Skip() {}
    virtual void Save() {}

    // Vtable slot 15 (+0x3c): binary @ 0x136c2c (HUDControl::GetAdjustedPos).
    // Returns pos + Vec3(480, 320, 0) * m_HudScale.
    // Used by MenuButton::Update to re-anchor the held fruit/bomb entity every
    // frame. DAT_00136c88={480,320,0} confirmed.
    virtual _Vector3<float> GetAdjustedPos();

    void SetPendingRemoval() { m_bPendingRemoval = 1; }

    // Port specific: debug overlay (F1 hitbox toggle) needs the effective
    // draw-space position, not the raw `pos` field. Subclasses whose Draw
    // transforms `pos` through a non-identity anchor override this so the
    // overlay AABB matches the rendered quad. Default returns `pos` unmodified.
    // Not in binary; appended after binary vtable.
    virtual _Vector3<float> GetDrawPos() const { return pos; }

    // Port specific: no binary counterpart -- desktop mouse-wheel scroll hook.
    // Returns this control's scrollable list, or 0 if it doesn't have one.
    // Only ShopScreen overrides this today (ScrollingMenu* m_pShopList).
    // Lives on HUDControl (not BaseScreen) because ShopScreen derives from
    // HUDControl3d directly, not BaseScreen -- HUDControl is the nearest
    // common base shared with every other entry in HUD::controls, so a
    // single walk of that list can find whichever screen owns a scroll list.
    // Appended after the binary vtable; not compiled under __bada__.
#ifndef __bada__
    virtual ScrollingMenu* GetScrollList() { return 0; }

    // Port specific: no binary counterpart -- companion to GetScrollList().
    // Desktop mouse-wheel handling must not scroll a screen that is still
    // sliding in/out (mirrors the in-game m_PauseAmount transition guard).
    // Returns 1.0f (fully settled) by default; screens with a transition
    // alpha (ShopScreen, BaseScreen subclasses) override to expose it.
    virtual float GetTransitionAlpha() const { return 1.0f; }

    // Port specific: no binary counterpart -- desktop ESC-as-back discriminator.
    // taskStateIndex alone can't tell "in a menu" from "in a live round": State 1
    // (Frontend) is dead code (FrontendTask.cpp/SplashTask.cpp jump straight to
    // State 2), so BOTH menus and live gameplay run under taskStateIndex==2. The
    // real discriminator is whether a menu's back-bomb ring is currently armed --
    // true in every menu screen (Main/GameMode/Dojo/Shop/About/GameOver each set
    // m_bBackdropActive=1 on their back/regress MenuButton) and false mid-round.
    // Default false; MenuButton overrides to read its own flag.
    virtual bool HasActiveBackBomb() const { return false; }
#endif

#ifndef __bada__
    // Port specific: no binary counterpart -- optional per-PRESENT tick (called
    // once per rendered/presented frame, at native display refresh or the
    // FPS-capped rate, NOT the fixed 60Hz sim step Update() above runs at).
    // Default no-op. Only overridden by controls whose visual motion should
    // track the display refresh rate instead of the sim tick rate (see
    // SettingsScreen::UpdateRealtime, ScrollingMenu::UpdateRealtime,
    // UiDropdown::UpdateRealtime -- the latter is a child widget not
    // AddControl'd to the HUD, so its owning screen forwards the tick
    // manually rather than this virtual being reached via HUD::UpdateRealtime).
    // Kept OUT of the __bada__ compile entirely (not just a no-op override) so the
    // asm-verify cross-build's vtable layout for HUDControl and every subclass
    // is byte-identical to the binary -- this virtual adds a vtable slot that
    // has no binary counterpart and must never appear there. Lives on the
    // HUDControl base (not HUDControl3d) so HUD::UpdateRealtime can dispatch
    // to plain-HUDControl subclasses too (e.g. ScrollingMenu).
    virtual void UpdateRealtime(float dtSeconds) { (void)dtSeconds; }
#endif

    void SetSingular() {
        m_Singular = 1;
        // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x0014dda8 (re-analyst)
    }

    void SetActive(bool b) {
        m_Active = b ? 1 : 0;
        // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x0013cdd0 (re-analyst)
    }
};

// v1.6.1 DefaultSoundRemovedCallback @0x00151a74 -- default sound-remove no-op callback.
// 30+ call sites use this as the default when no specific cleanup is needed on sound removal.
// Returns 0 always.
int DefaultSoundRemovedCallback(Mortar::MortarSound* snd);

#endif
