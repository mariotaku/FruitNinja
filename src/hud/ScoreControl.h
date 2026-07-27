#ifndef FN_HUD_SCORE_CONTROL_H
#define FN_HUD_SCORE_CONTROL_H

// ASM-spec v1.6.1 ScoreControl @ 0x001ad5fc — size 0x108:
//   +0x100 BakedStringBox* m_pStringBox100 (ctor=0; dtor: ~BakedStringBox+delete, null)
//   +0x104 BakedStringBox* m_pScoreBox     (ctor: new(200); BakedStringBox(font,0x8C,0x1E);
//                                           SetGradient(0xFFFC5A,0xE78308); SetText("SCORE");
//                                           SetHorizontalLineSpacing(-1); dtor: delete)
// ~ScoreControl @ 0x001ac3e0: delete m_pStringBox100, m_pScoreBox; then ~SmartPtr
//   m_FruitDigitTex/Banner/Icon; then base.
// (Previous addresses in this header were stale v1.5.x; restamped to v1.6.1.)
//
// Binary addresses (v1.6.1):
//   ctor (real)     0x001ad5fc
//   ctor (alias)    0x001ad6cc
//   ctor thunk      0x000f6bdc
//   dtor (in-place) 0x001ac3e0
//   dtor (deleting) 0x001ac454
//   Reset           0x001ac1c8
//   Update          0x0015853c
//   PreDraw         0x00158e1c
//   Draw            0x001581d4
//   GetType         0x00159d18  returns 3

#include "HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "render/BakedStringBox.h"
#include <cstdint>

// GetCurrentScore -- v1.6.1 GetCurrentScore @0x0011a0cc. Always returns
// game_work.currentScore; playerIdx is ignored by the binary (no per-player
// score split exists). Defined in ScoreControl.cpp.
int GetCurrentScore(int playerIdx);

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
    // Defunct: unused +0x8C float; v1.6.1 ScoreControl::ScoreControl @ 0x001ad5fc init -1.0f only (re-analyst)
    float    _pad8C;               // +0x8C: written -1.0f in ctor, never read/written again

    // +0x90
    float    m_ScalePulse;         // 1.0..2.0 during wave-active scale pulse
    float    m_DrawPosX;           // cached draw X
    float    m_DrawPosY;           // cached draw Y
    float    m_DrawPosZ;           // cached draw Z (always 0)

    // +0xA0
    // Defunct: unused +0xA0/+0xA4 SmartPtr<Texture>; v1.6.1 ScoreControl::ScoreControl @ 0x001ad5fc default-constructs only, never loaded/read (re-analyst)
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

    // +0x100
    // ASM-spec v1.6.1 ScoreControl @ 0x001ad5fc — size 0x108:
    //   +0x100 BakedStringBox* m_pStringBox100 (ctor=0; dtor: ~BakedStringBox+delete, null)
    //   +0x104 BakedStringBox* m_pScoreBox     (ctor: new(200); gradient; SetText "SCORE";
    //                                           SetHorizontalLineSpacing(-1); dtor: delete)
    Mortar::BakedStringBox* m_pStringBox100;  // raw pointer; ctor=0; lazy-alloc elsewhere
    Mortar::BakedStringBox* m_pScoreBox;      // raw pointer; ctor new(200) BakedStringBox

    ScoreControl();
    ~ScoreControl() override;

    void Init() override;
    void Release() override;
    void Reset() override;
    void Update(float dt) override;
    void PreDraw(float* hudScale) override;
    void Draw(float* hudScaleRaw) override;
    int  GetType() override { return 3; }
    void Skip() override;

    // Binary @ 0x0015819c -- single instruction (bx lr); returns its argument unchanged.
    int AddMultipliyer(int v);
};

#ifdef __bada__
static_assert(sizeof(ScoreControl) == 0x108, "ScoreControl size must be 0x108 (v1.6.1 ScoreControl @0x001ad5fc)");
#endif

#endif // FN_HUD_SCORE_CONTROL_H
