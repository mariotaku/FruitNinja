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
    Colour                           m_PulseColour;           // +0xA0 (4 bytes)
    int                              _padA4;                  // +0xA4
    int                              _padA8;                  // +0xA8
    float                            m_NameScale;             // +0xAC (init 1.0)
    uint8_t                          m_LeaderboardSubmitted;  // +0xB0
    uint8_t                          _pad1;                   // +0xB1
    uint8_t                          _pad2;                   // +0xB2
    uint8_t                          _pad3;                   // +0xB3
    Mortar::MortarSound*             m_RushSFX;               // +0xB4
    float                            m_PhaseTimer;            // +0xB8
    Vec3                             m_PosOffset;             // +0xBC
    int                              _padfield23;             // +0xC8
    int                              _padfield24;             // +0xCC

    // Port-specific: background texture handle (beyond binary struct size)
    Mortar::SmartPtr<Mortar::Texture> m_SecondaryTex;         // port-specific

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

    // GetType -- returns 8 (TODO: confirm from binary)
    int GetType() override { return 8; }

    // Binary @ 0x00133664 — real binary AddAward signature (Colour).
    void AddAward(Colour colour, Mortar::SmartPtr<Mortar::Texture> tex,
                  const char* name, int tier);

    // Binary form of Draw — float* arg (port uses const Vec3&, int; both overloads present)
    // STUB: BonusScreen::Draw -- binary @ 0x???? (TODO RE)
    void Draw(float* mtx);

    // STUB: BonusScreen::GetTimeFirstAward -- binary @ 0x???? (TODO RE)
    float GetTimeFirstAward();

    // STUB: BonusScreen::GetTimePerAward -- binary @ 0x???? (TODO RE)
    float GetTimePerAward();

    // STUB: BonusScreen::LoadContent -- binary @ 0x???? (TODO RE)
    void LoadContent();

    // STUB: BonusScreen::Shake -- binary @ 0x???? (TODO RE)
    void Shake(float amplitude, float duration);

    // STUB: BonusScreen::UnLoadContent -- binary @ 0x???? (TODO RE)
    void UnLoadContent();
    // ---- end STUBS ----

private:
    // Binary @ 0x0013260C — one-shot finale: coin spawn, camera shake, finish SFX
    void AwardScores();
};

#endif // FN_SCREENS_BONUS_SCREEN_H
