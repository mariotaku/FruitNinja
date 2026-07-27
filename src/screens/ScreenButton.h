#ifndef FN_SCREEN_BUTTON_H
#define FN_SCREEN_BUTTON_H

//
// ScreenButton — button descriptor for BaseScreen's lazy button system.
// Plain struct (no vtable), 0xC8 = 200 bytes.
//
// Stored in BaseScreen::m_ScreenButtons (std::list<ScreenButton>).
// BaseScreen::UpdateButtons iterates this list each frame:
//   - If m_pButton == nullptr and m_visCheck(dt) returns true,
//     a MenuButton is lazily created from the descriptor fields.
//   - If m_pButton != nullptr, m_updateCb is called each frame;
//     returning true triggers button removal/shrink.
//
// Binary refs:
//   ~ScreenButton           0x00131628 (destroys 5 delegate/SmartPtr members)
//   ControlDeleted          0x00160fbc (called when MenuButton is removed by HUD)
//   ShrinkButtonCall        0x0015f53c (called when fruit shrink animation completes)
//   DefaultCreateDelegate   0x001300e8 (always returns true)
//   DefaultButtonDelegate   0x001300ec (always returns false)
//
// There is no AddScreenButton method — callers construct on the stack
// and push_back into the list (inlined by the compiler).
//

#include "math/_Vector3.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "util/Delegate.h"
#include <cstdint>

class MenuButton;
class HUDControl;
struct ScreenButton; // forward decl for ScreenButtonDefaults below

// GCC 4.4 does not support lambdas (C++11, added in GCC 4.5). These named
// free functions replace the in-place lambda defaults in ScreenButton's ctor.
// They match the binary stubs at 0x001300e8 (AlwaysVisible) and 0x001300ec
// (NoUpdate) which always return true / false respectively.
namespace ScreenButtonDefaults {
    inline bool AlwaysVisible(float) { return true; }
    inline bool NoUpdate(MenuButton*, float, ScreenButton&) { return false; }
}

struct ScreenButton {
    // +0x00: tutorial slot ID (-1 = no tutorial)
    int m_tutorID;

    // +0x04: live MenuButton (nullptr until UpdateButtons creates it)
    MenuButton* m_pButton;

    // +0x08: button texture
    Mortar::SmartPtr<Mortar::Texture> m_tex;

    // +0x0C (36 bytes in binary): visibility predicate.
    // Called with dt each frame when m_pButton is nullptr.
    // Return true to create the button this frame.
    // Binary default (0x001300e8): always returns true.
    Mortar::Delegate1<bool, float> m_visCheck;

    // +0x30 (36 bytes): per-frame update delegate.
    // Called each frame when m_pButton exists.
    // Return true to trigger button removal/shrink.
    // Binary default (0x001300ec): always returns false.
    Mortar::Delegate3<bool, MenuButton*, float, ScreenButton&> m_updateCb;

    // +0x54 (36 bytes): on-click callback.
    // Wired as the MenuButton's tap callback.
    Mortar::Delegate0<void> m_clickCb;

    // +0x78 (36 bytes): "button deleted" callback.
    // Fired by ControlDeleted when the MenuButton is removed.
    Mortar::Delegate1<void, HUDControl*> m_deletedCb;

    // +0x9C: button world position
    _Vector3<float> m_pos;

    // +0xA8: fruit/badge position (6th param to MenuButton ctor).
    // NOTE: fruitPos.z (+0xB0) doubles as the optional conditional rest-scale
    // multiplier -- the binary has no separate "m_scaleA" field, it just reads
    // this Vec3's z component. ASM v1.6.1 BaseScreen::UpdateButtons @0x00160430.
    _Vector3<float> m_fruitPos;

    // +0xB4: always-applied scale on m_TargetSize.
    // ASM v1.6.1 BaseScreen::UpdateButtons @0x00160450/0x001604d0.
    float m_scaleB;

    // +0xB8: fruit facing rotation X. ASM v1.6.1 UpdateButtons @0x001604fc.
    float m_rotX;

    // +0xBC: fruit facing rotation Y. ASM v1.6.1 UpdateButtons @0x00160504.
    float m_rotY;

    // +0xC0: gate that must be nonzero (together with !m_bSliced) before the
    // rotate-facing-up block runs at all; independent of m_rotX/m_rotY, which
    // only decide the bool ARGUMENT passed to RotateFacingUp once the gate is
    // open. ASM v1.6.1 UpdateButtons @0x001604ec/0x001604f4 (vcmp #0, beq 0x160570).
    float m_bRotateGate;

    // +0xC4: flag set by ShrinkButtonCall (fruit shrink done). Offset confirmed
    // from v1.6.1 ScreenButton::ShrinkButtonCall @0x0015f53c -- `strb r3,[r5,#0xc4]`
    // @0x0015f5a0 with r5 = this. UpdateButtons never reads it; ControlDeleted does.
    uint8_t m_bShrunk;

    ScreenButton()
        : m_tutorID(-1)
        , m_pButton(nullptr)
        , m_visCheck(Mortar::Delegate1<bool, float>::MakeFree(&ScreenButtonDefaults::AlwaysVisible))          // 0x001300e8
        , m_updateCb(Mortar::Delegate3<bool, MenuButton*, float, ScreenButton&>::MakeFree(&ScreenButtonDefaults::NoUpdate))  // 0x001300ec
        , m_pos(0, 0, 0)
        , m_fruitPos(0, 0, 0)
        , m_scaleB(1.0f)
        , m_rotX(0.0f)
        , m_rotY(0.0f)
        , m_bRotateGate(0.0f)
        , m_bShrunk(0)
    {}

    // Matches v1.6.1 ScreenButton::ControlDeleted @0x00160fbc.
    // Called when HUD removes the MenuButton. When m_bShrunk is set it parks the
    // tracked fruit off-screen (pos.y / m_SecondPos.y = -480), sets m_Gravity to
    // -g_slideVec and vel.y / m_SecondVel.y to -10, then fires m_deletedCb.
    void ControlDeleted(HUDControl* ctrl);

    // Matches ScreenButton::ShrinkButtonCall @ 0x0015f53c.
    // Called when fruit shrink animation completes.
    void ShrinkButtonCall();

public:
    // Matches ScreenButton::DefaultButtonDelegate @ 0x001300ec.
    // Default per-frame update predicate (the value m_updateCb defaults to);
    // trivial no-op that always returns false. Mirrors the named free function
    // ScreenButtonDefaults::NoUpdate above.
    void DefaultButtonDelegate(MenuButton*, float, ScreenButton&);
};

#endif
