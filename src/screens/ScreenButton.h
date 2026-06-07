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
//   ControlDeleted          0x00130f40 (called when MenuButton is removed by HUD)
//   ShrinkButtonCall        0x001300f0 (called when fruit shrink animation completes)
//   DefaultCreateDelegate   0x001300e8 (always returns true)
//   DefaultButtonDelegate   0x001300ec (always returns false)
//
// There is no AddScreenButton method — callers construct on the stack
// and push_back into the list (inlined by the compiler).
//

#include "math/Vec3.h"
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
    Vec3 m_pos;

    // +0xA8: fruit/badge position (6th param to MenuButton ctor)
    Vec3 m_fruitPos;

    // +0xB4: optional scale (applied to m_TargetSize if != 0.0)
    float m_scaleA;

    // +0xB8: always-applied scale on m_TargetSize
    float m_scaleB;

    // +0xBC: fruit facing rotation X
    float m_rotX;

    // +0xC0: fruit facing rotation Y
    float m_rotY;

    // +0xC4: flag set by ShrinkButtonCall (fruit shrink done)
    uint8_t m_bShrunk;

    ScreenButton()
        : m_tutorID(-1)
        , m_pButton(nullptr)
        , m_visCheck(Mortar::Delegate1<bool, float>::MakeFree(&ScreenButtonDefaults::AlwaysVisible))          // 0x001300e8
        , m_updateCb(Mortar::Delegate3<bool, MenuButton*, float, ScreenButton&>::MakeFree(&ScreenButtonDefaults::NoUpdate))  // 0x001300ec
        , m_pos(0, 0, 0)
        , m_fruitPos(0, 0, 0)
        , m_scaleA(0.0f)
        , m_scaleB(1.0f)
        , m_rotX(0.0f)
        , m_rotY(0.0f)
        , m_bShrunk(0)
    {}

    // Matches ScreenButton::ControlDeleted @ 0x00130f40.
    // Called when HUD removes the MenuButton.
    void ControlDeleted(HUDControl* ctrl);

    // Matches ScreenButton::ShrinkButtonCall @ 0x001300f0.
    // Called when fruit shrink animation completes.
    void ShrinkButtonCall();

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: ScreenButton::DefaultButtonDelegate -- auto stub from binary missing-symbol set
    void DefaultButtonDelegate(MenuButton*, float, ScreenButton&);
    // ---- end AUTO-STUB MERGE ----
};

#endif
