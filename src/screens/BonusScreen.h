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
#include "engine/math/_Vector3.h"
#include "engine/render/BakedStringBox.h"
#include <vector>
#include <cstdint>

namespace Mortar { class MortarSound; }

// Port specific: releases BonusScreen.cpp's file-scope s_bonusScreenBacking global
// ("arcade_diolog_box.tex"). That cache IS binary-faithful -- v1.6.1
// BonusScreen::BonusScreen @0x00162d1c guards it with a plain !IsValid() test and
// copies it into m_Texture (+0x74) -- and BonusScreen::UnLoadContent @0x0016200c is
// `bx lr`, so the binary never releases it either; it leaves the slot to
// __aeabi_atexit. The port cannot, because its atexit runs after
// SDL_GL_DeleteContext and the GL texture name would leak. Called from GameDestroy,
// before MeshManager::Destroy().
//
// Do NOT fold this into BonusScreen::UnLoadContent -- that body is empty in the
// binary and must stay empty. Idempotent; the ctor re-loads on demand.
void BonusScreen_UnloadStatics();

// BonusAwardHud -- per-award data block. Binary size 0x60.
// v1.6.1 BonusScreen::Draw @0x0016492c reads these fields per-award.
struct BonusAwardHud {
    char       m_Name[64];        // +0x00
    int        m_TierBase;        // +0x40  (binary: m_Score)
    int        m_Multiplier;      // +0x44
    int        _pad48;            // +0x48
    int        m_DisplayedScore;  // +0x4C
    Colour     m_Colour;          // +0x50 (4 bytes, BGRA) -- star tint + text-box colour;
                                  //   .a (+0x53) is the per-award reveal FADE, animated by
                                  //   Update (0->255 over the slot's first 0.1s)
    float      m_Alpha;           // +0x54 -- value-box pop SCALE (BakedStringBox::Draw(Vec2(a,a),0,1)),
                                  //   NOT an alpha multiplier; sine arc peaking ~1.155 in Update
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
    // v1.6.1 BonusScreen::AddAward @0x00163284 `str r6,[r4,#0x80]` confirms m_TotalScore
    // at +0x80; m_DisplayedScore (Draw's %d, per-award/finale animated value) is +0x7C.
    int                               m_DisplayedScore;       // +0x7C  (animated toward total)
    int                               m_TotalScore;           // +0x80  (accumulator; AddAward sums tier into this)
    std::vector<BonusAwardHud>        m_Awards;               // +0x84  (cap 3 in binary)
    float                             m_ShakeAmplitude;       // +0x90
    float                             m_ShakeTimer;           // +0x94
    float                             m_ShakeDuration;        // +0x98  (init 1.0)
    uint16_t                          m_ShakeAngle;           // +0x9C
    uint16_t                          _padShake;              // +0x9E
    _Vector3<float> m_ShakeOffset;          // +0xA0  (3 floats, 12 bytes)
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
    bool                              m_bBonusTextBuilt;      // +0xD8 build-once latch
    uint8_t                           _padD9[3];              // +0xD9
    float                             m_Timer;                // +0xDC  (ctor = -TRANSITION_IN_TIME)
    _Vector3<float> m_AnimPos;              // +0xE0  (3 floats, 12 bytes -> ends 0xEC)

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

    // GetType: inherits HUDControl3d::GetType (v1.6.1 @0x00136088 -> returns 1); BonusScreen has no override in the binary.

    // TODO: re-verify v1.6.1 addr (prior 0x00133664 stale v1.5.x)
    void AddAward(Colour colour, Mortar::SmartPtr<Mortar::Texture> tex,
                  const char* name, int tier);

    // v1.6.1 BonusScreen::GetTimeFirstAward @0x00162010
    float GetTimeFirstAward();

    // v1.6.1 BonusScreen::GetTimePerAward @0x00162030
    float GetTimePerAward();

    // v1.6.1 BonusScreen::LoadContent @0x00162008
    void LoadContent();

    // v1.6.1 BonusScreen::Shake @ 0x00162054 (thunk 0x0011601c).
    // Seeds the per-award shake wobble: writes m_ShakeTimer/m_ShakeDuration/
    // m_ShakeAmplitude and a random m_ShakeAngle. Called from Update's
    // per-award reveal block with (0.1f, 10.0f).
    void Shake(float duration, float amplitude);

    // v1.6.1 BonusScreen::UnLoadContent @0x0016200c
    void UnLoadContent();
    // ---- end STUBS ----

    // BuildBonusText -- v1.6.1 @0x001621dc..0x0016267b
    // Creates all BakedStringBox members (rank label/value, BONUS title, TOTAL label).
    // Called unconditionally every Update tick; the create-once latch (m_bBonusTextBuilt,
    // +0xD8) lives INSIDE this function, not at the call site.
    // ASM-verified: 2026-06-27T00:00Z v1.6.1 BonusScreen::BuildBonusText @0x001621dc..0x0016267b (asm-inspector)
    void BuildBonusText();

private:
    // v1.6.1 BonusScreen::AwardScores @ 0x0016393c. One-shot finale fired once
    // from Update when the per-award reveal window ends (m_FinaleFired latch).
    void AwardScores();
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(BonusScreen, m_DisplayedScore)    == 0x7C,  "BonusScreen::m_DisplayedScore offset");
static_assert(offsetof(BonusScreen, m_TotalScore)        == 0x80,  "BonusScreen::m_TotalScore offset");
static_assert(offsetof(BonusScreen, m_Awards)            == 0x84,  "BonusScreen::m_Awards offset");
static_assert(offsetof(BonusScreen, m_ShakeAmplitude)    == 0x90,  "BonusScreen::m_ShakeAmplitude offset");
static_assert(offsetof(BonusScreen, m_RushLoopSFX)       == 0xB4,  "BonusScreen::m_RushLoopSFX offset");
static_assert(offsetof(BonusScreen, m_ScoreBox)          == 0xB8,  "BonusScreen::m_ScoreBox offset");
static_assert(offsetof(BonusScreen, m_TotalBox)          == 0xBC,  "BonusScreen::m_TotalBox offset");
static_assert(offsetof(BonusScreen, m_RankLabelBoxes)    == 0xC0,  "BonusScreen::m_RankLabelBoxes offset");
static_assert(offsetof(BonusScreen, m_RankValueBoxes)    == 0xCC,  "BonusScreen::m_RankValueBoxes offset");
static_assert(offsetof(BonusScreen, m_bBonusTextBuilt)   == 0xD8,  "BonusScreen::m_bBonusTextBuilt offset");
static_assert(offsetof(BonusScreen, m_Timer)             == 0xDC,  "BonusScreen::m_Timer offset");
static_assert(offsetof(BonusScreen, m_AnimPos)           == 0xE0,  "BonusScreen::m_AnimPos offset");
static_assert(sizeof(BonusScreen)                        == 0xEC,  "BonusScreen size mismatch");
#endif

#endif // FN_SCREENS_BONUS_SCREEN_H
