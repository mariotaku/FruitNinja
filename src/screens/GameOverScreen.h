#ifndef FN_GAME_OVER_SCREEN_H
#define FN_GAME_OVER_SCREEN_H

// GameOverScreen : HUDControl3d (size = 0x13C)
// Binary: ctor 0x00142900, Update 0x00141b34, Initialise 0x00142674
// No per-class Draw — inherits HUDControl3d::Draw (0x0014428c).
// Class-specific: PreDrawOrder (0x0014171c), DrawOrder (0x00141448).
//
// Analysed: 2026-05-02T00:00

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

class GameOverScreen : public HUDControl3d {
public:
    // +0x7C: dummy zero slot written =0 in Initialise. Legacy m_PrevState.
    float    field_0x7c;

    // State-machine enumeration. Stored as plain int at +0x080 to match
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

    // +0x080: state machine index (one of STATE_* above)
    int      m_State;                // +0x080

    // +0x084: state-0 entry timer; reused as state-1 progression timer
    float    m_Timer;                // +0x084

    // +0x088..+0x090: title-tex size (width, height, 0)
    float    m_TitleSizeX;          // +0x088
    float    m_TitleSizeY;          // +0x08C
    float    m_TitleSizeZ;          // +0x090

    // +0x094: always 0 (Initialise sets it; never read elsewhere)
    int      field_0x94;            // +0x094

    // +0x098: m_RetryButton (created by CreateRetryButton @ 0x141188)
    MenuButton* m_pRetryBtn;        // +0x098

    // +0x09C: secondary control slot (auxiliary; never created in stock build)
    HUDControl* m_pSlot9c;         // +0x09C

    // +0x0A0: dead slot (OpenFeint achievements button remnant)
    int         field_0xa0;         // +0x0A0

    // +0x0A4: m_QuitButton (created by CreateQuitButton @ 0x1412e4)
    MenuButton* m_pQuitBtn;         // +0x0A4

    // +0x0A8: tertiary auxiliary control slot
    HUDControl* m_pSlotA8;         // +0x0A8

    // +0x0AC: millisecond-resolution circular animation counter
    int         m_AnimCounter;      // +0x0AC

    // +0x0B0..+0x0B8: content-block centre position after layout
    float       m_OffsetPosX;      // +0x0B0
    float       m_OffsetPosY;      // +0x0B4
    float       m_OffsetPosZ;      // +0x0B8

    // +0x0BC: FruitFactControl* — created in state 6
    FruitFactControl* m_pFruitFact; // +0x0BC

    // +0x0C0: unnamed extra button slot
    MenuButton* m_pSlotC0;         // +0x0C0

    // +0x0C4: BonusScreen* — created in state 1 (Arcade only)
    BonusScreen* m_pBonusScreen;   // +0x0C4

    // +0x0C8: notice/popup control (sign-in prompt, etc.)
    HUDControl* m_pNoticeCtrl;     // +0x0C8

    // +0x0CC: Twitter share completion flag (PostCallback(0) sets =1)
    uint8_t     m_PostOk;          // +0x0CC

    // +0x0CD: Twitter share in-progress flag (PostCallback clears)
    uint8_t     m_PostInProgress;  // +0x0CD

    // +0x0CE: "X days left" label formatted in Initialise (64 bytes)
    char        m_CoinsEarnedLabel[64]; // +0x0CE..+0x10D

    // +0x110: progress counter (0->11 in state 6; ==10 triggers score commit)
    int         m_ProgressCounter; // +0x110

    // +0x114: comming_soon_highscore.tex (loaded in Update state-6 tail).
    // Asset isn't shipped -- LoadLocalisedTexture returns null; PreDraw's
    // IsValid() gate skips the overlay. Defunct: "coming soon" highscore
    // placeholder.
    Mortar::SmartPtr<Mortar::Texture> m_CommingSoonHighscoreTex;  // +0x114

    // +0x118: always 0; never set elsewhere after Initialise
    int         field_0x118;       // +0x118

    // +0x11C: most-fruit count (-1 = none found). FindMostOfFruit writes here.
    int         m_MostFruitCount;  // +0x11C

    // +0x120: score-submitted guard (single-shot; set =1 on first state-6 frame 10)
    uint8_t     m_bScoreSubmitted; // +0x120

    // +0x124: expression index (1..3; randomised when ctor param < 1)
    int         m_ExpressionIdx;   // +0x124

    // +0x128: background pattern index (1..3; randomised when ctor param < 1)
    int         m_BgPatternIdx;    // +0x128

    // +0x12C: fruit-fact tab index (0 or 1; passed to FruitFactControl::m_TabIndex at +0xE4)
    int         m_TabIndex;        // +0x12C

    // +0x130: star decoration count (passed to FruitFactControl +0xE9)
    int         m_StarCount;       // +0x130

    // +0x134: 1 when game_work.gameMode == 0 (Classic); gates expression/pattern overlay
    uint8_t     m_bIsClassic;      // +0x134

    // +0x138: pop-in alpha interpolator (0->1, ramps at 0.125/frame)
    float       m_FruitFactAlpha;  // +0x138

    // Binary: LoadContent / UnLoadContent (gated by static guard)
    static void LoadContent();
    static void UnLoadContent();

    // Parameterised ctor @ 0x00142900:
    //   param2/param3 are state/timer overrides for fast-skip path, NOT endReason/endScore.
    //   Score is read via GetCurrentScore(0) in state 6.
    GameOverScreen(const char* modeName, int param2, float param3,
                   int expressionIdx, int bgPatternIdx, int tabIndex, int starCount);
    ~GameOverScreen() override;

    // vtable slot 2: Init — trivial pass-through (0x00140548)
    void Init() override;

    // vtable slot 4: Reset — empty (0x00140554, single bx lr)
    void Reset() override;

    // vtable slot 5: BeginDraw — sets m_LayerFlags (0x00140590)
    void BeginDraw(float dt) override;

    // vtable slot 10: Update — full state machine (0x00141b34)
    void Update(float dt) override;

    // vtable slot 8: PreDrawOrder (0x0014171c)
    void PreDrawOrder(const Vec3& hudScale, int layerMask) override;

    // vtable slot 9: DrawOrder (0x00141448)
    void DrawOrder(const Vec3& hudScale, int layerMask) override;

    // vtable slot 3: Release — cleanup (0x00140d98)
    void Release() override;

    // vtable slot 12: GetType — returns 5 (0x0014305c)
    int GetType() override { return 5; }

    // vtable slot 7: inherited HUDControl3d::Draw — NO override needed
    // (omitted: compiler uses HUDControl3d::Draw at 0x0014428c)

    // IsAllowedToExit — always 1 in binary (0x0014061c)
    bool IsAllowedToExit();

private:
    // 0x00142674
    void Initialise(const char* modeName, int param2, float param3,
                    int expressionIdx, int bgPatternIdx, int tabIndex, int starCount);

    // 0x00141188
    void CreateRetryButton();

    // 0x001412e4
    void CreateQuitButton();

    // 0x00141a18 — populates field_0x118/m_MostFruitCount
    void FindMostOfFruit();

    // 0x00140688 — goes to state 6 (or leaderboard dialog if needed)
    void SetStateWait();

    // 0x00140604 — sets game[+0x33]=1
    void SetTerminate();

    // ProgressionTimer no-op stubs (empty in binary)
    // Defunct: ProgressionTimer -- empty in binary @ 0x001405fc
    void StartProgressionTimer();
    // Defunct: ProgressionTimer -- empty in binary @ 0x00140600
    void CancelHUDProgressionTimer();
    // Defunct: ProgressionTimer -- empty in binary @ 0x00140614
    void OnProgressionTimerUp();
    // Defunct: ProgressionTimer -- empty in binary @ 0x00140618
    void HandleProgressionTimerExpiration();

    // Social share callbacks
    // Defunct: Facebook share -- no-op stub; binary @ 0x0014083c (NetworkManager::PublishText)
    void FacebookCallback();
    // Defunct: Twitter share -- empty in binary @ 0x001405f8
    void TwitterCallback();
    // Binary @ 0x001405e8 -- PostCallback(result): m_bPostInProgress=0; m_bPostOk=(result==0)
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
    // m_pBonusScreen / m_pSlot9c / m_pNoticeCtrl; clears slot, forces state=6 where applicable.
    void DeletedControl(HUDControl* ctrl);

    // Port-internal helper: body of STATE_MAIN_DISPLAY, extracted so case 7
    // can fall through into it in the same tick (binary uses goto). Not a
    // binary symbol -- call-shape matches the binary's fall-through exactly.
    void RunStateMainDisplay(int prevState);
};

#endif // FN_GAME_OVER_SCREEN_H
