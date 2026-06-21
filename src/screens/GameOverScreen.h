#ifndef FN_GAME_OVER_SCREEN_H
#define FN_GAME_OVER_SCREEN_H

// ASM-spec v1.6.1 GameOverScreen @ 0x001882a0 (ctor), Initialise @ 0x00187c90,
// PostCallback @ 0x00184d2c, Release @ 0x00185970, dtor @ 0x00185d40 -- size 0x160
// (operator new at GameOver @ 0x001cb788). Base HUDControl3d=0x7C.
// m_PostOk @ +0xEC, m_PostInProgress @ +0xED; m_DaysLeftLabel char[64] @ +0xEE;
// m_TitleTex SmartPtr @ +0x138; param tail +0x148..+0x15C.

#include "hud/HUDControl3d.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>
#include "game/GameWork.h"

class MenuButton;
class HUDControl;
// BonusScreen: full port in BonusScreen.h / BonusScreen.cpp
class BonusScreen;
// FruitFactControl: HUDControl3d (size 0x204), forward-declared
class FruitFactControl;
namespace Mortar { class BakedStringBox; }

class GameOverScreen : public HUDControl3d {
public:
    // State-machine enumeration. Stored as plain int at +0x8C to match
    // the binary's int slot -- these are static constexpr ints, NOT a
    // strongly-typed enum class, so they can be used directly in case
    // labels and equality comparisons without casts. Numeric values must
    // match the binary's switch arms.
    static constexpr int STATE_ENTRY_ANIM     = 0;   // title grow-in (1.9s)
    static constexpr int STATE_BONUS_PHASE    = 1;   // Arcade-only BonusScreen slide
    static constexpr int STATE_MAIN_DISPLAY   = 6;   // settled view (retry/quit live)
    static constexpr int STATE_RETRY_PREPARE  = 7;   // clear entities, reset wave
    static constexpr int STATE_RETRY_FADE     = 8;   // camera fade-out for retry
    static constexpr int STATE_QUIT_WAIT      = 9;   // wait for entities, then QuitToMenu
    static constexpr int STATE_LEADERBOARD    = 10;  // online leaderboard (defunct)
    static constexpr int STATE_FINAL_FADE     = 11;  // terminate on alpha < 0
    static constexpr int STATE_QUICK_RESTART  = 14;  // hot-path quick-restart

    // +0x7C: unnamed control pointer slots (null in ctor; exact role unresolved)
    // TODO: v1.6.1 0x001882a0 (GameOverScreen ctor) -- role of m_pCtrl7C/m_pCtrl80/m_LinkedScreen unknown
    void*    m_pCtrl7C;               // +0x7C
    void*    m_pCtrl80;               // +0x80
    void*    m_LinkedScreen;          // +0x84

    // +0x88: title-tex display width (set from tex->m_Width in Initialise)
    float    m_TitleSizeX;            // +0x88

    // +0x8C: state machine index (one of STATE_* above)
    int      m_State;                 // +0x8C

    // +0x90: state-0 entry timer; reused as state-1 progression timer
    float    m_Timer;                 // +0x90

    // +0x94..+0x9F: title-tex size Vec3 (width, height, 0)
    Vec3     m_TitleSize;             // +0x94

    // +0xA0: dead slot (OpenFeint remnant; strb=0 in ctor)
    int      field_0xa0;              // +0xA0

    // +0xA4: m_QuitButton (created by CreateQuitButton)
    MenuButton* m_pQuitBtn;           // +0xA4

    // +0xA8: tertiary auxiliary control slot
    HUDControl* m_pSlotA8;            // +0xA8

    // +0xAC: millisecond-resolution circular animation counter
    int         m_AnimCounter;        // +0xAC

    // +0xB0: retry button slot
    // TODO: v1.6.1 0x001882a0 (GameOverScreen ctor) -- confirm m_pSlotB0 is the retry button pointer
    MenuButton* m_pSlotB0;            // +0xB0 (used as m_pRetryBtn in port logic)

    // +0xB4: secondary control slot (auxiliary; null in ctor)
    // TODO: v1.6.1 0x001882a0 (GameOverScreen ctor) -- role of m_pSlotB4 unknown; null-inited
    HUDControl* m_pSlotB4;            // +0xB4

    // +0xB8: animation timer in ms
    // TODO: v1.6.1 0x001882a0 (GameOverScreen ctor) -- m_AnimTimeMs exact usage unknown; 0-inited
    int         m_AnimTimeMs;         // +0xB8

    // +0xBC..+0xC7: content-block centre position after layout
    Vec3        m_OffsetPos;          // +0xBC

    // +0xC8: FruitFactControl* -- created in state 6
    FruitFactControl* m_pFruitFact;   // +0xC8

    // +0xCC..+0xD4: page control slots (null in ctor; exact roles unresolved)
    // TODO: v1.6.1 0x001882a0 (GameOverScreen ctor) -- role of m_pZenPage/m_pBonusFactPage/m_pClassicFactPage unknown
    void*       m_pZenPage;           // +0xCC
    void*       m_pBonusFactPage;     // +0xD0
    void*       m_pClassicFactPage;   // +0xD4 (used as m_pSlotC0 in port layout logic)

    // +0xD8..+0xDC: placeholder int slots
    int         field_0xd8;           // +0xD8
    int         field_0xdc;           // +0xDC

    // +0xE0: notice/popup control (sign-in prompt, etc.)
    HUDControl* m_pNoticeCtrl;        // +0xE0

    // +0xE4: BonusScreen* -- created in state 1 (Arcade only)
    BonusScreen* m_pBonusScreen;      // +0xE4

    // +0xE8: placeholder int slot
    int         field_0xe8;           // +0xE8

    // +0xEC: Twitter share completion flag (PostCallback(0) sets =1)
    uint8_t     m_PostOk;             // +0xEC

    // +0xED: Twitter share in-progress flag (PostCallback clears)
    uint8_t     m_PostInProgress;     // +0xED

    // +0xEE: coin-earned label formatted in Initialise (64 bytes)
    // sprintf format: "YOU JUST EARNT %i COINS" (DAT_001428fc/0x001bb926)
    char        m_DaysLeftLabel[64];  // +0xEE..+0x12F

    // +0x130: star decoration count (passed to FruitFactControl +0xE9)
    int         m_StarCount;          // +0x130

    // +0x134: BakedStringBox* for title string (null in ctor)
    // TODO: v1.6.1 0x001882a0 (GameOverScreen ctor) -- m_pTitleString exact usage unknown
    Mortar::BakedStringBox* m_pTitleString; // +0x134

    // +0x138: title texture SmartPtr (null in ctor; separate from m_Texture at +0x74)
    // TODO: v1.6.1 0x001882a0 (GameOverScreen ctor) -- m_TitleTex exact usage unknown
    Mortar::SmartPtr<Mortar::Texture> m_TitleTex; // +0x138

    // +0x13C: score accumulator (0 in ctor)
    // TODO: v1.6.1 0x001882a0 (GameOverScreen ctor) -- m_ScoreAccum exact usage unknown
    int         m_ScoreAccum;         // +0x13C

    // +0x140: most-fruit count (-1 = none found). FindMostOfFruit writes here.
    int         m_MostFruitCount;     // +0x140

    // +0x144: progress counter (0->11 in state 6; ==10 triggers score commit)
    // BYTE (strb in binary)
    uint8_t     m_ProgressCounter;    // +0x144

    // +0x145: score-submitted guard (single-shot; ctor = 1 per binary)
    uint8_t     m_bScoreSubmitted;    // +0x145

    // +0x148: expression index (1..3; randomised when ctor param < 1)
    int         m_ExpressionIdx;      // +0x148

    // +0x14C: background pattern index (1..3; randomised when ctor param < 1)
    int         m_BgPatternIdx;       // +0x14C

    // +0x150: fruit-fact tab index (0 or 1; passed to FruitFactControl::m_TabIndex)
    int         m_TabIndex;           // +0x150

    // +0x154: star count arg (from ctor param)
    int         m_StarCountArg;       // +0x154

    // +0x158: 1 when game_work.gameMode == 0 (Classic); gates expression/pattern overlay
    uint8_t     m_bIsClassic;         // +0x158

    // +0x15C: pop-in alpha interpolator (0->1, ramps at 0.125/frame)
    float       m_FruitFactAlpha;     // +0x15C

    // End = 0x160

    // Binary: LoadContent / UnLoadContent (gated by static guard)
    static void LoadContent();
    static void UnLoadContent();

    // Parameterised ctor @ 0x001882a0:
    //   param2/param3 are state/timer overrides for fast-skip path, NOT endReason/endScore.
    //   Score is read via GetCurrentScore(0) in state 6.
    GameOverScreen(const char* modeName, int param2, float param3,
                   int expressionIdx, int bgPatternIdx, int tabIndex, int starCount);
    ~GameOverScreen() override;

    // vtable slot 2: Init -- trivial pass-through (0x00140548)
    void Init() override;

    // vtable slot 4: Reset -- empty (0x00140554, single bx lr)
    void Reset() override;

    // vtable slot 5: BeginDraw -- sets m_LayerFlags (0x00140590)
    void BeginDraw(float dt) override;

    // vtable slot 10: Update -- full state machine (0x00141b34)
    void Update(float dt) override;

    // vtable slot 8: PreDrawOrder (0x0014171c)
    void PreDrawOrder(const Vec3& hudScale, int layerMask) override;

    // vtable slot 9: DrawOrder (0x00141448)
    void DrawOrder(const Vec3& hudScale, int layerMask) override;

    // vtable slot 3: Release -- cleanup (0x00185970)
    void Release() override;

    // vtable slot 12: GetType -- returns 5 (0x0014305c)
    int GetType() override { return 5; }

    // vtable slot 7: inherited HUDControl3d::Draw -- NO override needed
    // (omitted: compiler uses HUDControl3d::Draw at 0x0014428c)

    // IsAllowedToExit -- always 1 in binary (0x0014061c)
    bool IsAllowedToExit();

private:
    // 0x00187c90
    void Initialise(const char* modeName, int param2, float param3,
                    int expressionIdx, int bgPatternIdx, int tabIndex, int starCount);

    // 0x00141188
    void CreateRetryButton();

    // 0x001412e4
    void CreateQuitButton();

    // 0x00141a18 -- populates field_0x118/m_MostFruitCount
    void FindMostOfFruit();

    // 0x00140688 -- goes to state 6 (or leaderboard dialog if needed)
    void SetStateWait();

    // 0x00140604 -- sets game[+0x33]=1
    void SetTerminate();

    // ProgressionTimer no-op stubs (empty in binary)
    // Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen @ 0x001405fc
    void StartProgressionTimer();
    // Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen @ 0x00140600
    void CancelHUDProgressionTimer();
    // Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen @ 0x00140614
    void OnProgressionTimerUp();
    // Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen @ 0x00140618
    void HandleProgressionTimerExpiration();

    // Social share callbacks
    // Defunct: Facebook share -- no-op stub; binary @ 0x0014083c (NetworkManager::PublishText)
    void FacebookCallback();
    // Defunct: Twitter share -- empty in binary @ 0x001405f8
    void TwitterCallback();
    // Binary @ 0x00184d2c -- PostCallback(result): m_PostInProgress=0; m_PostOk=(result==0)
    void PostCallback(int result);

    // Binary @ 0x001405a0 -- LeaderboardsCallback: state-0/6 + alpha>0.999 -> m_State=10
    void LeaderboardsCallback();

    // Binary @ 0x0014105c -- RetryCallback: alpha gate + state guard, stats reset, m_State=7
    void RetryCallback();
    // Binary @ 0x00140620 -- QuitCallback: state guard, ClearCombo, m_State=9, HitMenuBomb
    void QuitCallback();

    // Thin wrappers kept for existing button binding sites
    void OnRetryClicked();
    void OnQuitClicked();

    // Binary @ 0x00140558 -- DeletedControl: wired as remove-callback on
    // m_pBonusScreen / m_pSlotB4 / m_pNoticeCtrl; clears slot, forces state=6 where applicable.
    void DeletedControl(HUDControl* ctrl);

};

#ifdef __bada__
static_assert(sizeof(GameOverScreen) == 0x160,
              "GameOverScreen size mismatch -- expected 0x160");
#endif

#endif // FN_GAME_OVER_SCREEN_H
