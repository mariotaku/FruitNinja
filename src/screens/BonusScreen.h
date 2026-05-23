#ifndef FN_SCREENS_BONUS_SCREEN_H
#define FN_SCREENS_BONUS_SCREEN_H

// BonusScreen : HUDControl3d (size ~0xC8+)
// Binary: ctor 0x00132048, dtor 0x00131F9C, Update 0x00132930,
//         Draw 0x0013325C, AddAward 0x00133664, AwardScores 0x0013260C
// Analysed: 2026-05-03T00:00

#include "hud/HUDControl3d.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/math/Colour.h"
#include "engine/math/Vec3.h"
#include <vector>
#include <cstdint>

namespace Mortar { class MortarSound; }

struct BonusAwardHud {
    char       m_Name[64];        // +0x00
    int        m_TierBase;        // +0x40
    int        m_Multiplier;      // +0x44
    int        _pad48;            // +0x48
    int        m_DisplayedScore;  // +0x4C
    Colour     m_Colour;          // +0x50 (4 bytes, BGRA)
    float      m_Scale;           // +0x54
    int        _pad58;            // +0x58
    Mortar::SmartPtr<Mortar::Texture> m_StarTex; // +0x5C (4 bytes on port; 8 in binary ARM SmartPtr)
    // pad to 0x88 in binary; ARM SmartPtr is 4-byte here so struct is shorter on port
    // Port: no ARM padding needed for asserts — see static_assert below gated on ARM.

    // ASM-verified: 2026-05-22 binary @ 0x00134130 (re-analyst).
    // Binary default ctor writes m_Multiplier = 1 (`movs r3,#0x1; str r3,[r4,#0x44]`).
    // AddAward does NOT touch +0x44, so the default value is load-bearing -- every
    // per-award DisplayedScore = TierBase * Multiplier, and Multiplier defaulting
    // to 0 made all scores render "0".
    BonusAwardHud()
        : m_TierBase(0), m_Multiplier(1), _pad48(0), m_DisplayedScore(0),
          m_Scale(1.0f), _pad58(0) {
        m_Name[0] = '\0';
    }
};

class BonusScreen : public HUDControl3d {
public:
    // Binary layout from +0x7C (immediately after HUDControl3d's +0x78 SmartPtr pair):
    int                              m_DisplayedScore;        // +0x7C (port offset)
    int                              m_TotalScore;            // +0x80
    std::vector<BonusAwardHud>       m_Awards;                // +0x84 (cap 3 in binary)
    float                            m_PulseField15;          // +0x90
    float                            m_PulseTimer;            // +0x94
    float                            m_PulseField17;          // +0x98 (init 1.0)
    int16_t                          m_PulseAngle;            // +0x9C
    int16_t                          _padPulse;               // +0x9E
    // ASM-verified: 2026-05-22 binary @ Update pulse-postlude. Vec3 drift/pulse
    // target accumulator, NOT a Colour. Prior port mis-typed as Colour+pads.
    Vec3                             m_PulseTarget;           // +0xA0..+0xAB
    float                            m_NameScale;             // +0xAC (init 1.0)
    uint8_t                          m_LeaderboardSubmitted;  // +0xB0
    uint8_t                          _pad1;                   // +0xB1
    uint8_t                          _pad2;                   // +0xB2
    uint8_t                          _pad3;                   // +0xB3
    Mortar::MortarSound*             m_RushSFX;               // +0xB4
    float                            m_PhaseTimer;            // +0xB8
    // Port: drum-roll one-shot latch (no binary field; latch needed because
    // m_PhaseTimer is written externally each frame and there is no prev-timer field).
    bool                             m_bDrumRollFired;        // port-side latch
    Vec3                             m_PosOffset;             // +0xBC
    int                              _padfield23;             // +0xC8
    int                              _padfield24;             // +0xCC

    // Binary ctor @ 0x00132048
    BonusScreen();
    // Binary dtor @ 0x00131F9C
    ~BonusScreen() override;

    // vtable slot: Reset — empty body (binary @ 0x00131D90, single bx lr)
    void Reset() override {}

    // vtable slot: Update (binary @ 0x00132930)
    void Update(float dt) override;

    // vtable slot: Draw (binary @ 0x0013325C)
    void Draw(const Vec3& hudScale, int layerMask) override;

    // ASM-verified: 2026-05-22 BonusScreen does NOT override GetType per binary
    // vtable -- inherits HUDControl3d::GetType which returns 1. Prior port
    // value of 8 collided with TYPE_SCROLLING_MENU and corrupted state on pause.

    // Binary @ 0x00133664 — called by BonusManager::SetUpBonusScreen
    // colour passed as packed BGRA uint32_t (matching BonusManager call site)
    void AddAward(uint32_t colour, Mortar::SmartPtr<Mortar::Texture> tex,
                  const char* name, int tier);

    // Binary form of AddAward — Colour arg (port uses uint32_t; both overloads present)
    // STUB: BonusScreen::AddAward(Colour,...) -- binary @ 0x???? (TODO RE)
    void AddAward(Colour colour, Mortar::SmartPtr<Mortar::Texture> tex,
                  const char* name, int tier);

    // Binary form of Draw — float* arg (port uses const Vec3&, int; both overloads present)
    // STUB: BonusScreen::Draw(float*) -- binary @ 0x???? (TODO RE)
    void Draw(float* mtx);

    // Binary @ 0x00131d58 — returns kRevealStart (0.6660f)
    float GetTimeFirstAward();

    // Binary @ 0x00131d74 — returns kPerAward (0.6f)
    float GetTimePerAward();

    // Binary @ 0x00131d50 — empty (bx lr)
    void LoadContent();

    // Binary @ 0x00131d94 — sets pulse shake fields, randomizes angle
    void Shake(float amplitude, float duration);

    // Binary @ 0x00131d54 — empty (bx lr)
    void UnLoadContent();

private:
    // Binary @ 0x0013260C — one-shot finale: coin spawn, camera shake, finish SFX
    void AwardScores();
};

#endif // FN_SCREENS_BONUS_SCREEN_H
