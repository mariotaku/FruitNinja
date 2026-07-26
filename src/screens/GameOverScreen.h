#ifndef FN_GAME_OVER_SCREEN_H
#define FN_GAME_OVER_SCREEN_H

// GameOverScreen -- binary ctor 0x001882a0, Initialise 0x00187c90,
// Update 0x00186c80, PreDrawOrder 0x00186894, DrawOrder 0x00186484,
// Release 0x00185970, dtor 0x00185d40. Size 0x160.
// operator new(0x160) @ 0x001cb788. vtable @ 0x002cd5c0.

#include "hud/HUDControl3d.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>
#include "game/GameWork.h"

class MenuButton;
class HUDControl;
class BonusScreen;
class FruitFactControl;
class FruitFactZenPage;
class FruitFactBonusFactPage;
class FruitFactClassicFactPage;
namespace Mortar { class BakedStringBox; }

class GameOverScreen : public HUDControl3d {
public:
    // State-machine values stored at +0x8C.
    static const int STATE_ENTRY_ANIM     = 0;
    static const int STATE_BONUS_PHASE    = 1;
    static const int STATE_MAIN_DISPLAY   = 6;
    static const int STATE_RETRY_PREPARE  = 7;
    static const int STATE_RETRY_FADE     = 8;
    static const int STATE_QUIT_WAIT      = 9;
    static const int STATE_LEADERBOARD    = 10;
    static const int STATE_FINAL_FADE     = 11;
    static const int STATE_QUICK_RESTART  = 14;

    // HUDControl3d ends at +0x7B (size 0x7C). Own fields follow.

    void*    m_pCtrl7C;                          // +0x7C
    void*    m_pCtrl80;                          // +0x80
    void*    m_LinkedScreen;                     // +0x84

    // +0x88: unused padding float (written as TitleSizeX in old port; actual field at +0x88)
    float    m_TitleSizeX;                       // +0x88

    int      m_State;                            // +0x8C
    float    m_Timer;                            // +0x90
    _Vector3<float> m_TitleSize;                        // +0x94  final = (256,64,0)

    // +0xA0: written 0 in Initialise, never read across any GameOverScreen
    // function (ctor/Initialise/Update/Release/DrawOrder/PreDrawOrder/dtor).
    // Binary declares it as a 1-byte field; purpose unknown.
    int      m_reservedA0;                       // +0xA0  purpose unknown (write-only)

    // +0xA4: RETRY button (binary name misleadingly m_pQuitBtn at +0xA4)
    // ASM-verified: v1.6.1 GameOverScreen @ 0x001882a0 — +0xA4 = RETRY, +0xB0 = QUIT
    MenuButton*  m_pRetryBtn;                    // +0xA4

    HUDControl*  m_pSlotA8;                      // +0xA8

    // +0xAC: NOT the ms counter — separate int counter (zero'd in Initialise)
    int          m_AnimCounter;                  // +0xAC

    // +0xB0: QUIT button
    MenuButton*  m_pQuitBtn;                     // +0xB0

    HUDControl*  m_pSlotB4;                      // +0xB4

    // +0xB8: millisecond circular counter (advanced in Update prologue)
    int          m_AnimTimeMs;                   // +0xB8

    _Vector3<float> m_OffsetPos;                    // +0xBC

    FruitFactControl* m_pFruitFact;              // +0xC8
    FruitFactZenPage*     m_pZenPage;            // +0xCC
    FruitFactBonusFactPage* m_pBonusFactPage;    // +0xD0
    FruitFactClassicFactPage* m_pClassicFactPage;// +0xD4

    // +0xD8/+0xDC: extra HUD child-control slots. Both are RemoveControl'd in
    // Release, deleted via the deleting-dtor vtable slot, and nulled by
    // DeletedControl when the HUD fires their removal callback -- identical
    // lifecycle to m_pNoticeCtrl / m_pSlotA8 / m_pSlotB4. Always 0 in this
    // build (no code path assigns them); kept for layout + teardown fidelity.
    HUDControl*  m_pChildCtrlD8;                  // +0xD8  HUD child-control slot
    HUDControl*  m_pChildCtrlDC;                  // +0xDC  HUD child-control slot

    HUDControl*  m_pNoticeCtrl;                  // +0xE0
    BonusScreen* m_pBonusScreen;                 // +0xE4

    // +0xE8: zeroed in ctor and Initialise, never read in any GameOverScreen
    // function; purpose unknown.
    int          m_reservedE8;                    // +0xE8  purpose unknown (write-only)

    uint8_t      m_PostOk;                       // +0xEC
    uint8_t      m_PostInProgress;               // +0xED

    char         m_DaysLeftLabel[64];            // +0xEE..+0x12F

    int          m_StarCount;                    // +0x130  0->11 progress counter

    Mortar::BakedStringBox* m_pTitleString;      // +0x134  THE TITLE

    Mortar::SmartPtr<Mortar::Texture> m_TitleTex;// +0x138  localised title tex

    // +0x13C: this[1] sub-object vptr (=0 in Initialise)
    int          m_SubObjectVptr;                // +0x13C

    int          m_MostFruitCount;               // +0x140  = -1; FindMostOfFruit writes

    // +0x144: score-submitted guard (0 until state-6 commit, then 1)
    uint8_t      m_bScoreSubmitted;              // +0x144

    // +0x145: set to 1 once near the end of Initialise (binary this[1].field_0x9),
    // never read in any GameOverScreen function; purpose unknown (likely an
    // init-complete/active flag). Kept write-only for layout fidelity.
    uint8_t      m_reserved145;                   // +0x145  purpose unknown (write-only, =1)

    int          m_ExpressionIdx;                // +0x148
    int          m_BgPatternIdx;                 // +0x14C
    int          m_TabIndex;                     // +0x150
    int          m_StarCountArg;                 // +0x154

    uint8_t      m_bIsClassic;                   // +0x158
    uint8_t      _pad159[3];                     // +0x159..+0x15B

    float        m_FruitFactAlpha;               // +0x15C

    // End = 0x160

    static void LoadContent();
    static void UnLoadContent();

    // Accessors for the sensei head/body texture arrays (indexed 0..2).
    // Binary: GameOverScreen::m_senseiHeads[idx] / m_senseiBody[idx]
    // (static SmartPtr<Texture>[3] arrays; v1.6.1 GameOverScreen ctor @0x001882a0).
    // Port maps these to g_ExpressionTexArr / g_BgPatternTexArr file-statics.
    // Returns an invalid SmartPtr if idx is out of range or content not loaded.
    static Mortar::SmartPtr<Mortar::Texture> GetSenseiHeadTex(int idx); // @0x001882a0
    static Mortar::SmartPtr<Mortar::Texture> GetSenseiBodyTex(int idx); // @0x001882a0

    GameOverScreen(const char* modeName, int param2, float param3,
                   int expressionIdx, int bgPatternIdx, int tabIndex, int starCount);
    ~GameOverScreen() override;

    void Init() override;
    void Reset() override;
    void BeginDraw(float dt) override;
    void Update(float dt) override;

#ifndef __bada__
    // Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
    // Advances the STATE_ENTRY_ANIM entry-reveal m_Timer using the real
    // measured dtSeconds, once per PRESENTED frame (Game::tickRealtimeUi via
    // HUD::UpdateRealtime), so the entry reveal (title scale-in + state-advance
    // timing) tracks the display's actual present rate (60/90/120fps) instead
    // of the fixed 60Hz sim tick. The STATE MACHINE itself (which state, when
    // to transition, one-shot side effects) stays in Update() at 60Hz -- it
    // reads the m_Timer value this function advances and fires
    // threshold-crossing transitions there, exactly once per sim tick.
    // STATE_BONUS_PHASE's m_Timer and game_work.m_PauseAmount (shared global,
    // read+branched same-call in STATE_MAIN_DISPLAY/STATE_RETRY_FADE) are
    // intentionally NOT handled here -- see the HALT comments at each site in
    // GameOverScreen.cpp's Update().
    void UpdateRealtime(float dtSeconds) override;
#endif

    void PreDrawOrder(float* hudScaleRaw, int layerMask) override;
    void DrawOrder(float* hudScaleRaw, int layerMask) override;
    void Release() override;
    int  GetType() override { return 5; }

    bool IsAllowedToExit();

private:
    void Initialise(const char* modeName, int param2, float param3,
                    int expressionIdx, int bgPatternIdx, int tabIndex, int starCount);
    void CreateRetryButton();   // v1.6.1 @ 0x00185f98 -- writes +0xA4
    void CreateQuitButton();    // v1.6.1 @ 0x00186220 -- writes +0xB0
    void FindMostOfFruit();     // v1.6.1 @ 0x00141a18
    // Runs once when the game-over wait state finishes: bumps the
    // "unrated_games" save total, and if the rate-app gate passes
    // (not yet rated, score > 50, unrated_games > 5, score within 10 of the
    // mode highscore) sets FruitSaveData::m_bRated + posts the score to the
    // (defunct) leaderboard, then advances to STATE_MAIN_DISPLAY. The
    // binary's rate-app dialog is still unported (see TODO in the .cpp).
    void SetStateWait();        // v1.6.1 @ 0x00184e04
    void SetTerminate();        // v1.6.1 @ 0x00140604

    // Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::StartProgressionTimer @ 0x00184d48
    void StartProgressionTimer();
    // Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::CancelHUDProgressionTimer @ 0x00184d4c
    void CancelHUDProgressionTimer();
    // Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::OnProgressionTimerUp @ 0x00184d5c
    void OnProgressionTimerUp();
    // Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::HandleProgressionTimerExpiration @ 0x00184d60
    void HandleProgressionTimerExpiration();

    // Defunct: Facebook share -- no-op stub; (v1.6.1: symbol absent -- defunct/inlined)
    void FacebookCallback();
    // Defunct: Twitter share -- no-op stub; (v1.6.1: symbol absent -- defunct/inlined)
    void TwitterCallback();

    void PostCallback(int result);        // v1.6.1 @ 0x00184d2c
    void LeaderboardsCallback();          // v1.6.1 @ 0x001405a0
    void RetryCallback();                 // v1.6.1 @ 0x0014105c
    void QuitCallback();                  // v1.6.1 @ 0x00184d6c
    void OnRetryClicked();
    void OnQuitClicked();
    void DeletedControl(HUDControl* ctrl);// v1.6.1 @ 0x00140558
};

// Binary: _Z23SetCurrentModeHighscorei @0x00119f24 (v1.6.1)
// Free wrapper: reads current mode index from game_work, guards idx<4 + saveData,
// updates m_ModeHighScores[idx] if score strictly beats stored, returns bool.
bool SetCurrentModeHighscore(int score);

#ifdef __bada__
static_assert(sizeof(GameOverScreen) == 0x160,
              "GameOverScreen size mismatch -- expected 0x160");
static_assert(offsetof(GameOverScreen, m_pRetryBtn)   == 0xA4, "m_pRetryBtn offset");
static_assert(offsetof(GameOverScreen, m_pQuitBtn)    == 0xB0, "m_pQuitBtn offset");
static_assert(offsetof(GameOverScreen, m_AnimTimeMs)  == 0xB8, "m_AnimTimeMs offset");
static_assert(offsetof(GameOverScreen, m_pFruitFact)  == 0xC8, "m_pFruitFact offset");
static_assert(offsetof(GameOverScreen, m_StarCount)   == 0x130, "m_StarCount offset");
static_assert(offsetof(GameOverScreen, m_pTitleString)== 0x134, "m_pTitleString offset");
static_assert(offsetof(GameOverScreen, m_TitleTex)    == 0x138, "m_TitleTex offset");
static_assert(offsetof(GameOverScreen, m_bScoreSubmitted) == 0x144, "m_bScoreSubmitted offset");
#endif

#endif // FN_GAME_OVER_SCREEN_H
