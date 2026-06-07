#ifndef FN_HUD_SCORE_CONTROL_H
#define FN_HUD_SCORE_CONTROL_H

// Analysed: 2026-04-30T00:00
//
// ScoreControl : HUDControl3d (size = 0x100)
// Struct size confirmed: operator_new(0x100) in GameInit.
// Main score HUD: 16-digit display with per-digit alpha animation, sin-wobble
// pulse on score change, scale pulse driven by combo timer, new-highscore banner.
//
// Binary addresses:
//   ctor (real)     0x00158c7c
//   ctor (alias)    0x00158d4c
//   ctor thunk      0x000f6bdc
//   dtor (in-place) 0x00158418
//   dtor (deleting) 0x00158394
//   Reset           0x001582e4
//   Update          0x0015853c
//   PreDraw         0x00158e1c
//   Draw            0x001581d4
//   GetType         0x00159d18  returns 3

#include "HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include <cstdint>

class ScoreControl : public HUDControl3d {
public:
    // +0x7C
    uint8_t  m_bDirty;             // 1 = snap m_ScoreSmoothed to currentScore next Update
    uint8_t  _pad7D;
    uint16_t m_PulseAngle;         // sin-table angle; set 0x8000 on score increase, decays to 0

    // +0x80
    float    m_ScoreSmoothed;      // eased toward GetCurrentScore
    int      m_DisplayedScore;     // (int)m_ScoreSmoothed — drives formatted text
    int      m_HighscoreToShow;    // highscore value in banner (0 = no banner)
    // Defunct: unused +0x8C float; binary @ 0x00158c7c init -1.0f only (re-analyst)
    float    _pad8C;               // +0x8C: written -1.0f in ctor, never read/written again

    // +0x90
    float    m_ScalePulse;         // 1.0..2.0 during wave-active scale pulse
    float    m_DrawPosX;           // cached draw X
    float    m_DrawPosY;           // cached draw Y
    float    m_DrawPosZ;           // cached draw Z (always 0)

    // +0xA0
    Mortar::SmartPtr<Mortar::Texture> m_ScoreIconTex;       // score.tex
    Mortar::SmartPtr<Mortar::Texture> m_HighscoreBannerTex; // new_best_score.tex
    float    m_BannerScaleTime;    // banner scale anim timer; -2.0 = inactive sentinel
    uint16_t m_BannerSinIdx;       // sin-table angle for banner wobble
    uint8_t  _padAE[2];

    // +0xB0
    int      m_DigitCount;         // active digit count (0..16)
    int      m_LastDigitCount;     // previous digit count (change detection)
    float    m_DigitAlpha[16];     // per-digit alpha 0..1; index 0 = ones place

    // +0xF8
    Mortar::SmartPtr<Mortar::Texture> m_FruitDigitTex;  // hud_fruit.tex (loaded by ctor; Reset copies to +0x74)

    // +0xFC
    int      m_PlayerIdx;          // 0 = P1, 1 = P2

    ScoreControl();
    ~ScoreControl() override;

    void Init() override;
    void Release() override;
    void Reset() override;
    void Update(float dt) override;
    void PreDraw(const Vec3& hudScale) override;
    void Draw(const Vec3& hudScale, int layerMask) override;
    int  GetType() override { return 3; }
    void Skip() override;

    // TODO: 0x0015819c -- ScoreControl::AddMultipliyer(int): binary body is a trivial
    //                      passthrough (returns its int argument); port the one-line body.
    int AddMultipliyer(int);
};

#endif // FN_HUD_SCORE_CONTROL_H
