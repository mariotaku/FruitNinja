#ifndef FN_UI_WIDGET_H
#define FN_UI_WIDGET_H

//
// UiWidget -- Port specific: shared base for the port-only settings widget
// toolkit (src/ui/). NO binary counterpart -- this factors out the touch
// state machine + NineSlice box drawing shared by CheckBox/SliderControl's
// dead-code binary implementations (src/hud/CheckBox.cpp, SliderControl.cpp)
// into one reusable base, so new port-only widgets (UiCheckbox, UiSlider,
// UiDropdown) don't each re-derive the same latch/hold/release logic.
//
// Still subclasses the REAL binary base HUDControl3d so it slots into the
// existing HUD draw/update call shape (Init/Release/Update/Draw/GetType/
// SetToMultiplayerState) -- see HUDControl3d.h / HUDControl.h for the vtable.
//
// Usage contract:
//   - Set `pos` (inherited from HUDControl) to the widget centre in centered
//     ortho coords, and call SetSize(halfW, halfH) to define the touch hit-rect
//     (pos +/- half). Subclasses read m_HalfW/m_HalfH for their own hit-test.
//   - Call PollTouch() once per Update() to drive the CheckBox/SliderControl-
//     style latch state machine: it scans for a new touch on kNone, tracks the
//     live capture position while held, and reports kReleasedInside /
//     kReleasedOutside exactly once on release. Subclasses decide what each
//     result means (toggle on release-inside, drag on held, etc).
//   - If the owner takes a gesture over mid-drag (a scroller latching, a modal
//     opening) and stops calling Update(), it MUST call CancelTouch(slot) at
//     that moment -- otherwise the widget's next tick resolves a stale capture
//     as a release and fires a phantom toggle. See CancelTouch below.
//   - DrawBox()/DrawGlyphQuad()/DrawText() are thin wrappers around the
//     established NineSlice / raw MatrixManager+Mesh / Font::DrawString
//     idioms (see CheckBox.cpp / SliderControl.cpp for the reference pattern)
//     so subclasses don't hand-roll matrix stack code.
//   - Textures/fonts are INJECTED per-instance via the Set* setters -- this
//     toolkit has no static texture-loading lifecycle (unlike CheckBox/
//     SliderControl's LoadContent/UnloadContent); the owning screen loads
//     once and hands SmartPtrs to each widget instance.
//   - Widgets are NOT AddControl'd to HUD -- the owning screen (e.g.
//     SettingsScreen) must call Update()/Draw() on each widget directly every
//     frame itself (see plan note: HUD::Update only ticks modal + TOP_MOST,
//     so AddControl'd widgets would freeze under a modal).
//

#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "render/Font.h"
#include "util/SmartPtr.h"
#include "math/_Vector3.h"
#include "math/Colour.h"
#include "util/Delegate.h"

class UiWidget : public HUDControl3d {
public:
    // Result of PollTouch() -- see method doc below.
    enum PressResult {
        kNone,             // no touch acquired, nothing changed this frame
        kPressed,          // touch slot just acquired this frame (press-edge)
        kHeld,             // touch slot held, capture position refreshed
        kReleasedInside,   // touch released; captured press position was inside the hit-rect
        kReleasedOutside   // touch released; captured press position was outside the hit-rect
    };

    UiWidget();
    virtual ~UiWidget();

    // HUDControl3d vtable overrides -- see HUDControl.h for slot order.
    void Init() override {}
    void Release() override { m_BoxTex.SetNull(); }
    bool SetToMultiplayerState() override { return false; }
    int GetType() override { return 100; }

    // Drop the touch latch WITHOUT resolving it. Call this from whoever takes
    // ownership of a gesture the widget is already tracking -- the owning
    // screen's scroller, a modal that opens mid-drag -- so the widget stops at
    // kNone instead of firing a release later.
    //
    // Why it exists: the widget only learns a touch ended by seeing
    // IsTouchDown() go to 0, and it reports that as kReleasedInside/Outside off
    // the CAPTURED press position. An owner that stops calling Update() while
    // it drives the gesture leaves m_TouchId latched on a slot that is by then
    // dead, so the widget's next tick takes the release branch and fires a
    // phantom toggle. Cancel at the moment ownership transfers instead.
    //
    // `slot` is the Mortar::Touch slot being taken over; the call is a no-op
    // unless the widget is latched on exactly that slot. Pass -1 to cancel
    // whatever slot it holds. Safe to call when nothing is latched.
    // Note UiDropdown runs its own state machine over the same m_TouchId, so
    // this cancels its bar-tap latch too (it re-acquires cleanly next press).
    void CancelTouch(int slot);

    void SetOnChange(const Mortar::Delegate0<void>& cb) { m_OnChange = cb; }
    void SetTint(Colour c) { m_Tint = c; }
    void SetTextColour(Colour c) { m_TextColour = c; }
    void SetFont(Mortar::Font* font) { m_pFont = font; }
    void SetBoxTexture(const Mortar::SmartPtr<Mortar::Texture>& tex) { m_BoxTex = tex; }
    // Hit-rect half-extents (pos +/- half). Also the default DrawBox() extents
    // when a subclass doesn't pass explicit w/h.
    void SetSize(float halfW, float halfH) { m_HalfW = halfW; m_HalfH = halfH; }

protected:
    // Hit-test a point against the widget's pos +/- (m_HalfW, m_HalfH) rect.
    bool HitTest(float x, float y) const;

    // The CheckBox/SliderControl touch latch state machine, factored out.
    // Reference pattern: CheckBox::Update (src/hud/CheckBox.cpp) latches a
    // slot on press-edge, captures game_work.m_FingerSpawnPos[slot] every
    // held frame, and validates the CAPTURED press position (not the live
    // release position) against the rect on release.
    //
    //   m_TouchId == -1: scan via TouchInRegion(pos +/- half); on acquire,
    //     only keep the slot if IsTouchDown() reports press-edge (2) this
    //     frame -- returns kPressed. Otherwise stays idle -- returns kNone.
    //   m_TouchId != -1: if IsTouchDown() reports up (0), drop the slot and
    //     return kReleasedInside/kReleasedOutside based on HitTest(m_TouchCapture).
    //     Otherwise refresh m_TouchCapture from game_work.m_FingerSpawnPos[slot]
    //     and return kHeld.
    PressResult PollTouch();

    // Draw a NineSlice box centred at (cx, cy) sized (w, h), tinted `tint`.
    // Border constants are in LOGICAL texels: box.tex's HD variant (hd_box)
    // is loaded at 2x pixel density but the loader HALVES the reported
    // GetWidth()/GetHeight() back to the logical 64x40 size (matching the SD
    // variant) -- so the 9-slice border must be specified in the SAME logical
    // space (~6px corner per box.svg), not the raw HD pixel count, or the SD
    // and HD builds would render different-looking corners for the same
    // destBorder. See NineSlice.h for the srcBorderXPx/destBorderX split.
    void DrawBox(float cx, float cy, float w, float h, Colour tint);

    // Raw glyph quad idiom (CheckBox::Draw / SliderControl::Draw pattern):
    // reset world stack, bind tex, scale+translate a unit quad, draw, unbind.
    // Full-texture UV (no sub-rect).
    void DrawGlyphQuad(Mortar::Texture* tex, float cx, float cy, float w, float h, Colour c);

    // Draw text with m_pFont if set, else game_work.pFontMain. `yTop` is the
    // TOP of the glyphs (Font::DrawString convention). `clip`, when non-NULL,
    // is forwarded to Font::DrawString's clipRect (Mortar::MortarRectangleT<float>,
    // left/top/right/bottom in the SAME world/ortho space as x/yTop) -- per-glyph
    // clamp+UV-lerp so text partially outside the rect is cut cleanly instead of
    // spilling past it. See Font::DrawString @0x0024c7f0.
    void DrawText(const char* s, float x, float yTop, float scale, Colour c,
                  const Mortar::MortarRectangleT<float>* clip = nullptr);

    float m_HalfW;
    float m_HalfH;
    int   m_TouchId;
    _Vector3<float> m_TouchCapture;
    Mortar::Delegate0<void> m_OnChange;
    Colour m_Tint;
    Colour m_TextColour;
    Mortar::Font* m_pFont;
    Mortar::SmartPtr<Mortar::Texture> m_BoxTex;

    // Logical (post-HD-halving) box.tex corner size in texels -- see DrawBox doc.
    static const float kBoxSrcBorderX;
    static const float kBoxSrcBorderY;
    static const float kBoxDestBorderX;
    static const float kBoxDestBorderY;
};

#endif // FN_UI_WIDGET_H
