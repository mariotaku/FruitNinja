#ifndef FN_TUTORIAL_CONTROL_H
#define FN_TUTORIAL_CONTROL_H

// Analysed: 2026-04-25T10:30
//
// TutorialControl : HUDControl3d (size = 0xa0 / 160 bytes)
//
// Binary refs:
//   Constructor         v1.6.1 @0x001c2fdc (C1) / dup C2 @0x001c30cc (asm-verified 2026-07-04)
//   Init                0x00162e38
//   Reset               0x00162e4c
//   Release             0x00162e48 (no-op)
//   Update              v1.6.1 @0x001c27ac
//   Draw                0x00163360
//   ResetTutePos        0x00162f04 (MenuButton* overload)
//   ResetTutePos        0x00162f84 (Vec3 overload)
//   CanShowTute         0x00162fb8
//   ButtonPressedAtPos  0x00162e58
//
// TODO: v1.6.1 0x001c2728 (TutorialControl::PreDraw) -- not yet ported
// TODO: v1.6.1 0x001c272c (TutorialControl::SetToMultiplayerState) -- not yet ported
//
// Tutorial arrow that appears over menu buttons during first-play.
// Only visible during slow-motion (timeScale < 1.0) or transitions.
// Texture assignment:
//   swipe_fruit_begin.tex -> super.m_Texture (+0x74) -- used to draw ARROW
//   press_indicate.tex    -> m_PressTex (+0x8C)      -- used for TRAIL quads
// 2.75-second animation: fade-in, bounce, hold, fade-out.
//
// Purely cosmetic -- no other system reads from TutorialControl.
//

#include "HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "math/Colour.h"

class MenuButton;

class TutorialControl : public HUDControl3d {
public:
    TutorialControl();
    ~TutorialControl();

    void Init() override;
    void Release() override;
    void Update(float dt) override;
    void Draw(float* hudScaleRaw) override;
    int  GetType() override { return 1; }

    // Reset animation timer to -10 (inactive sentinel).
    // Matches vtable slot 4 @ 0x00162e4c.
    void Reset() override;

    // Matches TutorialControl::ResetTutePos @ 0x00162f04.
    // Copies button position, computes arrow width/flip, resets timer.
    // btn=nullptr -> just resets timer (hides arrow).
    void ResetTutePos(MenuButton* btn);

    // Matches TutorialControl::ResetTutePos @ 0x00162f84.
    // Raw position overload -- sets pos directly, resets timer.
    void ResetTutePos(const _Vector3<float>& targetPos);

    // Matches TutorialControl::CanShowTute @ 0x00162fb8.
    // Returns true during slow-motion or screen transitions.
    bool CanShowTute();

    // Matches TutorialControl::ButtonPressedAtPos @ 0x00162e58.
    // Advances a nearly-complete or inactive animation forward by 9.5s.
    // Only fires when m_AnimTimer < 0 (animation is inactive/reset).
    // If btn != nullptr: copies button pos, computes halfWidth, sets flip.
    // After update: clamps m_AnimTimer to 0.0 if it went positive.
    void ButtonPressedAtPos(MenuButton* btn);

private:
    // +0x7C: animation lifecycle timer. -10.0 = inactive/reset.
    // Advances each frame in Update; animation runs from 0 to 2.75.
    float m_AnimTimer;                        // +0x7C

    // +0x80: computed draw position (scale * pos, with bounce offset)
    _Vector3<float> m_DrawPos;                           // +0x80

    // +0x8C: trail texture (press_indicate.tex) -- used for the 4-quad trail loop.
    // Draw uses super.m_Texture (+0x74, swipe_fruit_begin.tex) for the ARROW quad,
    // and this field for the TRAIL quads.
    // This matches the binary's texture assignment (verified @ 0x001636f8).
    Mortar::SmartPtr<Mortar::Texture> m_PressTex;     // +0x8C

    // +0x90: draw colour (alpha driven by animation phase)
    Colour m_Colour;                          // +0x90

    // +0x94: UV frame selector for the arrow (NOT a visibility gate).
    // 0 = UV frame 0 (u0=0.0, u1=0.5); 1 = UV frame 1 (u0=0.5, u1=1.0).
    // Update sets this to 1 at the start of every frame; active phases clear to 0.
    int m_bHidden;                            // +0x94

    // +0x98: half-width computed from button bounds in ResetTutePos
    float m_HalfWidth;                        // +0x98

    // +0x9C: true if arrow points left (button is right of center)
    bool m_bFlipX;                            // +0x9C
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(TutorialControl) == 0xa0, "TutorialControl size mismatch"); // v1.6.1 GameInit @0x001ce7d0 -- operator new(0xa0) sizes TutorialControl
#endif

#endif
