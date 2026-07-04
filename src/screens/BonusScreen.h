#ifndef FN_SCREENS_BONUS_SCREEN_H
#define FN_SCREENS_BONUS_SCREEN_H

// BonusScreen : HUDControl3d (sizeof == 0xEC)
// v1.6.1: ctor @0x00162d1c, dtor D2 @0x00162724 / D1 @0x0016283c / D0 @0x00162954,
//         Update @0x00163dd0, Draw @0x0016492c
// AddAward / Reset -- TODO: re-verify v1.6.1 addr (prior 0x00133664 / 0x00131D90 are stale v1.5.x)

#include "hud/HUDControl3d.h"
#include "engine/util/SmartPtr.h"
#include "engine/asset/Texture.h"
#include "engine/math/Colour.h"
#include "engine/math/Vec3.h"
#include "engine/render/BakedStringBox.h"
#include <vector>
#include <cstdint>

namespace Mortar { class MortarSound; }

// BonusAwardHud -- per-award data block. Binary size 0x60.
// v1.6.1 BonusScreen::Draw @0x0016492c reads these fields per-award.
struct BonusAwardHud {
    char       m_Name[64];        // +0x00
    int        m_TierBase;        // +0x40  (binary: m_Score)
    int        m_Multiplier;      // +0x44
    int        _pad48;            // +0x48
    int        m_DisplayedScore;  // +0x4C
    Colour     m_Colour;          // +0x50 (4 bytes, BGRA) -- star tint + text-box colour
    float      m_Alpha;           // +0x54 -- per-award draw alpha (passed to BakedStringBox::Draw as s0)
    Colour     m_Colour2;         // +0x58 -- second palette colour (populated by AddAward, unread by Draw)
    Mortar::SmartPtr<Mortar::Texture> m_StarTex; // +0x5C (binary: m_Icon; 4 bytes on ARM SmartPtr)

    BonusAwardHud()
        : m_TierBase(0), m_Multiplier(1), _pad48(0), m_DisplayedScore(0),
          m_Alpha(1.0f) {
        m_Name[0] = '\0';
    }
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(BonusAwardHud) == 0x60, "BonusAwardHud size mismatch");
#endif

class BonusScreen : public HUDControl3d {
public:
    // Binary layout from +0x7C (immediately after HUDControl3d's +0x78 SmartPtr pair):
    int                               m_TotalScore;           // +0x7C  (accumulator; AddAward sums tier into this)
    int                               m_DisplayedScore;       // +0x80  (animated toward total)
    std::vector<BonusAwardHud>        m_Awards;               // +0x84  (cap 3 in binary)
    float                             m_ShakeAmplitude;       // +0x90
    float                             m_ShakeTimer;           // +0x94
    float                             m_ShakeDuration;        // +0x98  (init 1.0)
    uint16_t                          m_ShakeAngle;           // +0x9C
    uint16_t                          _padShake;              // +0x9E
    Vec3                              m_ShakeOffset;          // +0xA0  (3 floats, 12 bytes)
    float                             m_NamePulseScale;       // +0xAC  (init 1.0)
    bool                              m_FinaleFired;          // +0xB0
    bool                              field_0xB1;             // +0xB1  // TODO: re-verify (ctor writes 0 only)
    bool                              field_0xB2;             // +0xB2  // TODO: re-verify (ctor writes 0 only)
    uint8_t                           _padB3;                 // +0xB3
    Mortar::MortarSound*              m_RushLoopSFX;          // +0xB4
    Mortar::BakedStringBox*           m_ScoreBox;             // +0xB8
    Mortar::BakedStringBox*           m_TotalBox;             // +0xBC
    Mortar::BakedStringBox*           m_RankLabelBoxes[3];    // +0xC0..+0xCB
    Mortar::BakedStringBox*           m_RankValueBoxes[3];    // +0xCC..+0xD7
    bool                              m_bSkipIntro;           // +0xD8
    uint8_t                           _padD9[3];              // +0xD9
    float                             m_Timer;                // +0xDC  (ctor = -TRANSITION_IN_TIME)
    Vec3                              m_AnimPos;              // +0xE0  (3 floats, 12 bytes -> ends 0xEC)

    // v1.6.1: ctor @0x00162d1c
    BonusScreen();
    // v1.6.1: dtor D2 @0x00162724
    ~BonusScreen() override;

    // vtable slot: Reset — TODO: re-verify v1.6.1 addr (prior 0x00131D90 stale v1.5.x)
    void Reset() override {}

    // vtable slot: Update — v1.6.1 @0x00163dd0
    void Update(float dt) override;

    // vtable slot: Draw — v1.6.1 @0x0016492c
    void Draw(float* hudScaleRaw) override;

    // GetType -- returns 8 (TODO: confirm from binary)
    int GetType() override { return 8; }

    // TODO: re-verify v1.6.1 addr (prior 0x00133664 stale v1.5.x)
    void AddAward(Colour colour, Mortar::SmartPtr<Mortar::Texture> tex,
                  const char* name, int tier);

    // STUB: BonusScreen::GetTimeFirstAward -- binary @ 0x???? (TODO RE)
    float GetTimeFirstAward();

    // STUB: BonusScreen::GetTimePerAward -- binary @ 0x???? (TODO RE)
    float GetTimePerAward();

    // STUB: BonusScreen::LoadContent -- binary @ 0x???? (TODO RE)
    void LoadContent();

    // v1.6.1 BonusScreen::Shake @ 0x00162054 (thunk 0x0011601c).
    // Seeds the per-award shake wobble: writes m_ShakeTimer/m_ShakeDuration/
    // m_ShakeAmplitude and a random m_ShakeAngle. Called from Update's
    // per-award reveal block with (0.1f, 10.0f).
    void Shake(float duration, float amplitude);

    // STUB: BonusScreen::UnLoadContent -- binary @ 0x???? (TODO RE)
    void UnLoadContent();
    // ---- end STUBS ----

    // BuildBonusText -- v1.6.1 @0x001621dc..0x0016267b
    // Creates all BakedStringBox members (rank label/value, BONUS title, TOTAL label).
    // Called once from Update tail when m_bSkipIntro becomes true.
    // ASM-verified: 2026-06-27T00:00Z v1.6.1 BonusScreen::BuildBonusText @0x001621dc..0x0016267b (asm-inspector)
    void BuildBonusText();

private:
    // v1.6.1 BonusScreen::AwardScores @ 0x0015393c. One-shot finale fired once
    // from Update when the per-award reveal window ends (m_FinaleFired latch).
    void AwardScores();
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(BonusScreen, m_TotalScore)        == 0x7C,  "BonusScreen::m_TotalScore offset");
static_assert(offsetof(BonusScreen, m_Awards)            == 0x84,  "BonusScreen::m_Awards offset");
static_assert(offsetof(BonusScreen, m_ShakeAmplitude)    == 0x90,  "BonusScreen::m_ShakeAmplitude offset");
static_assert(offsetof(BonusScreen, m_RushLoopSFX)       == 0xB4,  "BonusScreen::m_RushLoopSFX offset");
static_assert(offsetof(BonusScreen, m_ScoreBox)          == 0xB8,  "BonusScreen::m_ScoreBox offset");
static_assert(offsetof(BonusScreen, m_TotalBox)          == 0xBC,  "BonusScreen::m_TotalBox offset");
static_assert(offsetof(BonusScreen, m_RankLabelBoxes)    == 0xC0,  "BonusScreen::m_RankLabelBoxes offset");
static_assert(offsetof(BonusScreen, m_RankValueBoxes)    == 0xCC,  "BonusScreen::m_RankValueBoxes offset");
static_assert(offsetof(BonusScreen, m_bSkipIntro)        == 0xD8,  "BonusScreen::m_bSkipIntro offset");
static_assert(offsetof(BonusScreen, m_Timer)             == 0xDC,  "BonusScreen::m_Timer offset");
static_assert(offsetof(BonusScreen, m_AnimPos)           == 0xE0,  "BonusScreen::m_AnimPos offset");
static_assert(sizeof(BonusScreen)                        == 0xEC,  "BonusScreen size mismatch");
#endif

#endif // FN_SCREENS_BONUS_SCREEN_H
