#ifndef FN_TUTORIAL_CONTROL_H
#define FN_TUTORIAL_CONTROL_H

//
// TutorialControl : HUDControl3d (size = 0xa0 / 160 bytes)
//
// Binary refs:
//   Constructor      0x001636f8
//   Init             0x00162e38
//   Reset            0x00162e4c
//   Release          0x00162e48 (no-op)
//   Update           0x00163014
//   Draw             0x00163360
//   ResetTutePos     0x00162f04 (MenuButton* overload)
//   ResetTutePos     0x00162f84 (Vec3 overload)
//   CanShowTute      0x00162fb8
//
// Tutorial arrow that appears over menu buttons during first-play.
// Only visible during slow-motion (timeScale < 1.0) or transitions.
// Draws press_indicate.tex (arrow) + swipe_fruit_begin.tex (trail).
// 2.75-second animation: fade-in, bounce, hold, fade-out.
//
// Purely cosmetic — no other system reads from TutorialControl.
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
    void Draw(const Vec3& hudScale, int layerMask) override;
    int  GetType() override { return 1; }

    // Reset animation timer to -10 (inactive sentinel).
    // Matches vtable slot 4 @ 0x00162e4c.
    void Reset();

    // Matches TutorialControl::ResetTutePos @ 0x00162f04.
    // Copies button position, computes arrow width/flip, resets timer.
    // btn=NULL → just resets timer (hides arrow).
    void ResetTutePos(MenuButton* btn);

    // Matches TutorialControl::ResetTutePos @ 0x00162f84.
    // Raw position overload — sets pos directly, resets timer.
    void ResetTutePos(const Vec3& targetPos);

    // Matches TutorialControl::CanShowTute @ 0x00162fb8.
    // Returns true during slow-motion or screen transitions.
    bool CanShowTute() const;

private:
    // +0x7C: animation lifecycle timer. -10.0 = inactive/reset.
    // Advances each frame in Update; animation runs from 0 to 2.75.
    float m_AnimTimer;                        // +0x7C

    // +0x80: computed draw position (scale * pos, with bounce offset)
    Vec3 m_DrawPos;                           // +0x80

    // +0x8C: arrow texture (press_indicate.tex)
    SmartPtr<Mortar::Texture> m_PrimaryTex;   // +0x8C

    // +0x90: draw colour (alpha driven by animation phase)
    Colour m_Colour;                          // +0x90

    // +0x94: hidden flag (1 = entering animation, 0 = visible)
    int m_bHidden;                            // +0x94

    // +0x98: half-width computed from button bounds in ResetTutePos
    float m_HalfWidth;                        // +0x98

    // +0x9C: true if arrow points left (button is right of center)
    bool m_bFlipX;                            // +0x9C
};

#endif
