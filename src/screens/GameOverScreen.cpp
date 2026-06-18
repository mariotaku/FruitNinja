// Analysed: 2026-05-02T00:00
// GameOverScreen -- binary ctor 0x00142900, Initialise 0x00142674, Update 0x00141b34
// PreDrawOrder 0x0014171c, DrawOrder 0x00141448.
// No per-class Draw -- inherits HUDControl3d::Draw (0x0014428c).

#include "GameOverScreen.h"
#include "BonusScreen.h"
#include "Game.h"
#include "game/GameMode.h"
#include "game/WaveManager.h"
#include "game/FruitSaveData.h"
#include "game/AchievementManager.h"
#include "game/BonusManager.h"
#include "game/BombHit.h"
#include "entities/ActorManager.h"
#include "entities/FruitInfo.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/FruitFactControl.h"
#include "engine/audio/GameSound.h"
#include "engine/audio/MortarSound.h"
#include "asset/TextureManager.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include "math/Vec3.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/Font.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include "game/GameWork.h"
#include "game/GameOver.h"
#include "engine/network/NetworkManager.h"
#include "screens/MainScreen.h"
#include "engine/util/StringHash.h"

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// File-scope texture SmartPtr arrays (binary static class members)
// Binary @ 0x00140cde loads these in GameOverScreen::LoadContent.
// ---------------------------------------------------------------------------
// Static class textures (binary @ 0x00130d40 literal pool).
// LoadContent loads these once on first call (guarded); UnLoadContent
// nulls them. Per-instance fields in the class read from these statics.
// Order matches the binary's LoadContent call sequence exactly so
// asm-verify groups the loads identically.
//   GOT slot          string                       load order
//   ----------------- ---------------------------- ----------
//   0x75d8            "arcade_time_up.tex"         call 1
//   0x7800            "gameover.tex"               call 2
//   0x76f0            "time_up.tex"                call 3  (substring of arcade_time_up.tex per -fmerge-constants)
//   0x7310            "retry.tex"                  call 4
//   0x73fc            "quit.tex"                   call 5
//   0x740c[+0]        "leaderboards.tex"           call 6  (array element 0 -- mid-string of gc_leaderboards.tex)
//   0x740c[+4]        "gc_leaderboards.tex"        call 7  (array element 1)
//   0x7160[+0..+8]    "sensei_head_0%d.tex"        loop 1  (sensei_head array -- s_ExpressionTexArr)
//   0x7abc[+0..+8]    "sensei_body_0%d.tex"        loop 2  (sensei_body array -- s_BgPatternTexArr)
static Mortar::SmartPtr<Mortar::Texture> g_ArcadeTimeUpTitleTex;        // arcade_time_up.tex (Arcade title)
static Mortar::SmartPtr<Mortar::Texture> g_GameOverTitleTex;            // gameover.tex (Classic title)
static Mortar::SmartPtr<Mortar::Texture> g_TimeUpTitleTex;              // time_up.tex (Zen title)
static Mortar::SmartPtr<Mortar::Texture> g_RetryTex;                    // retry.tex (own GOT slot, not part of array)
static Mortar::SmartPtr<Mortar::Texture> g_QuitTex;                     // quit.tex (own GOT slot, not part of array)
static Mortar::SmartPtr<Mortar::Texture> g_LeaderboardsTexPair[2];      // 0: leaderboards.tex, 1: gc_leaderboards.tex
static Mortar::SmartPtr<Mortar::Texture> g_ExpressionTexArr[3];         // sensei_head_01..03.tex
static Mortar::SmartPtr<Mortar::Texture> g_BgPatternTexArr[3];          // sensei_body_01..03.tex

// blurry_backing.tex is NOT loaded here — binary loads it from MenuButton::LoadContent.
// This static stays null; DrawOrder checks IsValid() and early-returns if not loaded.
// NOT nulled in UnLoadContent (binary's UnLoadContent has no GOT entry for it).
static Mortar::SmartPtr<Mortar::Texture> g_StarburstTex;

// Defunct: binary @ 0x00140f68 — 3 dead-texture SmartPtr statics at .bss
// 0x001e9f50/0x0018d5c0/0x0011cb58. Nulled only; never assigned in binary.
static Mortar::SmartPtr<Mortar::Texture> s_DeadTex_7af8;   // .bss 0x001e9f50 -- never assigned in binary
static Mortar::SmartPtr<Mortar::Texture> s_DeadTex_75f4;   // .bss 0x0018d5c0
static Mortar::SmartPtr<Mortar::Texture> s_DeadTex_7a88;   // .bss 0x0011cb58

// comming_soon_highscore.tex (+0x114) is loaded by Update state-6 first-entry,
// NOT by LoadContent. Kept as instance field m_CommingSoonHighscoreTex.

static bool g_LoadContentGuard = false;

// ---------------------------------------------------------------------------
// Starburst halo mesh (48 verts; lazy-init on first DrawOrder)
// Binary @ 0x00141448 uses a static 48-vertex tri-list for the halo.
// ---------------------------------------------------------------------------
struct StarburstMesh {
    bool initialised;
    QUADCUSTOMVERTEX verts[48];
};
static StarburstMesh g_StarMesh = { false, {} };

// ---------------------------------------------------------------------------
// Helpers — local to this TU (match binary static helpers)
// ---------------------------------------------------------------------------

static int GetCurrentScore(int playerIdx) {
    if (playerIdx != 0) return 0;
    Game* g = Game::GetInstance();
    return g ? game_work.currentScore : 0;
}

static int GetCurrentModeHighscore() {
    Game* g = Game::GetInstance();
    if (!g || !game_work.m_SaveData) return 0;
    int mode = game_work.gameMode & 0x03;
    return game_work.m_SaveData->m_ModeHighScores[mode];
}

// SetTerminate: game[+0x33] = 1. Reuses this->SetTerminate().
// The free function wrapper is used from within Update.
static void DoSetTerminate() {
    // game[+0x33] is m_bPendingRemoval in the port's Game struct.
    // Binary: *(uint8_t*)(game + 0x33) = 1; CancelHUDProgressionTimer (no-op stub).
    // Port: mark the HUD screen for removal via game_work.pGameOverScreen->m_bPendingRemoval.
    Game* g = Game::GetInstance();
    if (g && game_work.pGameOverScreen) {
        game_work.pGameOverScreen->m_bPendingRemoval = 1;
    }
}

// ASM-spec for QuitToMenu (binary @ 0x00169e50):
//   ResetGlobalDt(1.0f)
//   game_work.m_LevelTransitionFlag = 1                        // +0x05
//   mainScreen->m_State      = STATE_CAMERA_ZOOM (0)           // +0x10c
//   mainScreen->m_StateTimer = 0.5f                            // +0x110
//   if (game_work.m_pActiveHUDControl) m_pActiveHUDControl->m_bPendingRemoval = 1
//   SetScore(0, -1)
//   NetworkManager::GetInstance()->VTable[3](0)   // defunct
//   game_work.m_QuitTransitionTimer = 0.0f                      // +0x1a8
//   game_work.m_bMPRetryPending = 0                             // +0x174
//   game_work.m_bP2PHostMatched/m_bP2PClientJoined = 0         // +0x19a..+0x19b
//   (m_bP2PGameStarted/m_bDisconnectPending removed in v1.6.1 -- those bytes
//    are now interior to m_FrameTimer int @ +0x19c)
//
// NOTE: This is THE SAME binary function called by PauseScreen quit path
// (ported at src/screens/PauseScreen.cpp:144-194). Duplication faithful
// to the binary's call sites. GameOverScreen omits the
// MainScreen::DeleteMenuButtons() workaround PauseScreen uses (no menu
// buttons live on this screen).
// ASM-verified: 2026-05-20 binary @ 0x00169e50 (re-analyst)
static void DoQuitToMenu() {
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);              // 0x169e58/60
    Game* game = Game::GetInstance();
    if (!game) return;

    game_work.m_LevelTransitionFlag = 1;                          // 0x169e6e

    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_ZOOM);       // 0x169e7c -> m_State = 0
        game_work.mMainScreen->SetStateTimer(0.5f);               // 0x169e80 -> m_StateTimer = 0.5f
        // TODO: v1.6.1 QuitToMenu @0x001cb6e4 -- seed m_TexMoreGames.f0=0.5f on gameplay->menu return.
        // That binary function (distinct from 0x00169e50) also writes 0.5f to +0x11c so the
        // case-0 hold branch runs for ~0.5s before sliding in. Needs re-analyst pass.
    }

    // 0x169e84/0x169e86: dismiss the active HUD overlay if any.
    if (game_work.m_pActiveHUDControl) {
        game_work.m_pActiveHUDControl->m_bPendingRemoval = 1;
    }

    FN::SetScore(0, -1);                                          // 0x169e90

    // Defunct: P2P / online disconnect -- no-op stub; binary @ 0x00169e9e
    Mortar::NetworkManager::GetInstance()->SpawnThreadController(); // vtable[3](0)

    game_work.m_QuitTransitionTimer = 0.0f;                       // 0x169eaa
    game_work.m_bMPRetryPending   = 0;                            // 0x169eb2
    game_work.m_bP2PHostMatched   = 0;                            // 0x169eb6
    game_work.m_bP2PClientJoined  = 0;                            // 0x169eba
    // m_bP2PGameStarted and m_bDisconnectPending removed in v1.6.1
    // (those byte slots at +0x19c/+0x19d are now interior to m_FrameTimer int).

    // DIFFERS: binary relies on the OS task scheduler swapping from Game task
    // to Frontend task, which triggers GameExit_Handler via GameTaskExit.
    // Port collapses both tasks into one; we drive the transition explicitly
    // by flipping taskStateIndex to 1 (Frontend). FrontendInit immediately
    // writes taskStateIndex=2, so the net effect on the next two GameTaskUpdate
    // ticks is: GameExit_Handler (teardown HUD + WaveManager) then GameInit
    // (fresh game). Without this flip GameExit_Handler never runs and
    // SpeedControl / other HUD controls are never released.
    game_work.taskStateIndex = 1;
}

// ---------------------------------------------------------------------------
// Static content load/unload (binary: gated by static guard)
// ---------------------------------------------------------------------------

// Binary @ 0x00130bb0: 7 explicit LoadLocalisedTexture (calls 1..7) + a
// 3-iter loop with 2 snprintfs per iter (loops 1..2 for sensei head/body).
// String order verified by objdump of binary literal pool @ 0x00130d40
// against .rodata string table @ 0x001ab89c. Substring sharing per
// GCC -fmerge-constants: "time_up.tex" is a substring pointer of
// "arcade_time_up.tex" (offset +7); "leaderboards.tex" is a substring
// pointer of "gc_leaderboards.tex" (offset +3).
void GameOverScreen::LoadContent() {
    if (g_LoadContentGuard) return;
    char acStack_bc[128];  // function-scope buffer, matching binary's stack frame
    g_ArcadeTimeUpTitleTex   = TextureManager::LoadLocalisedTexture("arcade_time_up.tex");
    g_GameOverTitleTex       = TextureManager::LoadLocalisedTexture("gameover.tex");
    g_TimeUpTitleTex         = TextureManager::LoadLocalisedTexture("time_up.tex");
    g_RetryTex               = TextureManager::LoadLocalisedTexture("retry.tex");
    g_QuitTex                = TextureManager::LoadLocalisedTexture("quit.tex");
    // Array-of-2 at GOT+0x740c -- stored adjacently.
    g_LeaderboardsTexPair[0] = TextureManager::LoadLocalisedTexture("leaderboards.tex");
    g_LeaderboardsTexPair[1] = TextureManager::LoadLocalisedTexture("gc_leaderboards.tex");
    {
        // Binary uses iVar2=0 do{...}while(iVar1!=3) with pre-declared counter.
        int i = 0;
        do {
            int iVar1 = i + 1;
            snprintf(acStack_bc, 0x80, "sensei_head_0%d.tex", iVar1);
            g_ExpressionTexArr[i] = TextureManager::LoadLocalisedTexture(acStack_bc);
            snprintf(acStack_bc, 0x80, "sensei_body_0%d.tex", iVar1);
            g_BgPatternTexArr[i]  = TextureManager::LoadLocalisedTexture(acStack_bc);
            i = iVar1;
        } while (i != 3);
    }
    g_LoadContentGuard = true;
}

// Binary-faithful tail-merged helper. Sourcery G++ 4.4.1 emits a TU-local
// thunk `T.967` for UnLoadContent's repeating SmartPtr::SetNull pattern
// (asm-inspector verified). Without the noinline, GCC inlines SetNull at
// every call site which differs structurally even though the behaviour
// is identical. Wrap the SetNull call so the cross-toolchain emits one
// out-of-line helper that all 13 SmartPtr slots `bl` to.
#if defined(_MSC_VER)
#  define FN_NOINLINE __declspec(noinline)
#else
#  define FN_NOINLINE __attribute__((noinline))
#endif
static FN_NOINLINE void NullTex(Mortar::SmartPtr<Mortar::Texture>* p) {
    p->SetNull();
}
#undef FN_NOINLINE

// ASM-spec v1.6.1 GameOverScreen::UnLoadContent @0x00185e68:
//   - 16 NullTex calls in GOT-slot order (8 individuals + 1 array-of-2
//     + 2 arrays-of-3). 3 extra GOT slots (dead statics, never assigned)
//     are interleaved among the ordinal textures, NOT appended at the end.
//   - Order matches the binary's literal pool entries @0x00185f48..0x00185f70.
//   - No g_StarburstTex -- the starburst lives in MenuButton's statics.
//   - s_DeadTex_* names are from the old v1.5.1 addresses; the v1.6.1 GOT
//     offsets are 0x79d8/0x73b8/0x7938 but these are internal statics so
//     only ORDER matters for asm-verify, not the name.
void GameOverScreen::UnLoadContent() {
    g_LoadContentGuard = false;
    NullTex(&g_GameOverTitleTex);            // GOT 0x761c -- gameover.tex (2nd loaded)
    NullTex(&g_TimeUpTitleTex);              // GOT 0x74ec -- time_up.tex (3rd loaded)
    NullTex(&g_RetryTex);                    // GOT 0x700c -- retry.tex (4th loaded)
    // Array-of-2 at GOT 0x713c
    NullTex(&g_LeaderboardsTexPair[0]);      // leaderboards.tex
    NullTex(&g_LeaderboardsTexPair[1]);      // gc_leaderboards.tex
    NullTex(&g_QuitTex);                     // GOT 0x7128 -- quit
    NullTex(&s_DeadTex_7af8);                // GOT 0x79d8 (DEAD)
    NullTex(&g_ArcadeTimeUpTitleTex);        // GOT 0x7390 -- arcade_time_up
    NullTex(&s_DeadTex_75f4);                // GOT 0x73b8 (DEAD)
    NullTex(&s_DeadTex_7a88);                // GOT 0x7938 (DEAD)
    // 2x3-element arrays interleaved: head[0],body[0],head[1],body[1],head[2],body[2]
    // GOT 0x6dbc (sensei_head), GOT 0x798c (sensei_body)
    for (int i = 0; i < 3; ++i) {
        NullTex(&g_ExpressionTexArr[i]);
        NullTex(&g_BgPatternTexArr[i]);
    }
}

// ---------------------------------------------------------------------------
// Reset — empty in binary (0x00140554, single bx lr)
// ---------------------------------------------------------------------------

void GameOverScreen::Reset() {}

// ---------------------------------------------------------------------------
// IsAllowedToExit — always 1 in binary (0x0014061c)
// ---------------------------------------------------------------------------

bool GameOverScreen::IsAllowedToExit() { return true; }

// (Per re-analyst 2026-05-08: the asm-verify pipeline reports
// `_ZN14GameOverScreen12PreDrawOrderEPfi` (Pf,i = float*, int) but the
// binary at 0x0014171c actually takes (HUDControl* parent, int layer)
// -- the float* mangling is Ghidra noise from a missing prototype. The
// real impl is the (const Vec3&, int) overload at the bottom of the
// file. The previous duplicate float*-stub overloads were deleted.)

// ---------------------------------------------------------------------------
// GameOverScreen constructor (0x00142900) -- thin wrapper over Initialise
// ---------------------------------------------------------------------------

GameOverScreen::GameOverScreen(const char* modeName, int param2, float param3,
                               int expressionIdx, int bgPatternIdx,
                               int tabIndex, int starCount)
    : HUDControl3d(),
      field_0x7c(0.0f),
      m_State(0),
      m_Timer(0.0f),
      m_TitleSizeX(0.0f),
      m_TitleSizeY(0.0f),
      m_TitleSizeZ(0.0f),
      field_0x94(0),
      m_pRetryBtn(nullptr),
      m_pSlot9c(nullptr),
      field_0xa0(0),
      m_pQuitBtn(nullptr),
      m_pSlotA8(nullptr),
      m_AnimCounter(0),
      m_OffsetPosX(0.0f),
      m_OffsetPosY(0.0f),
      m_OffsetPosZ(0.0f),
      m_pFruitFact(nullptr),
      m_pSlotC0(nullptr),
      m_pBonusScreen(nullptr),
      m_pNoticeCtrl(nullptr),
      m_PostOk(0),
      m_PostInProgress(0),
      m_ProgressCounter(0),
      field_0x118(0),
      m_MostFruitCount(-1),
      m_bScoreSubmitted(0),
      m_ExpressionIdx(expressionIdx),
      m_BgPatternIdx(bgPatternIdx),
      m_TabIndex(tabIndex),
      m_StarCount(starCount),
      m_bIsClassic(0),
      m_FruitFactAlpha(0.0f)
{
    memset(m_CoinsEarnedLabel, 0, sizeof(m_CoinsEarnedLabel));
    Initialise(modeName, param2, param3, expressionIdx, bgPatternIdx, tabIndex, starCount);
}

GameOverScreen::~GameOverScreen() {
    // Binary dtor body calls Release() (vtable slot 3) before the C++ member dtors run.
    // HUD::Release / HUD::Update only call delete ctrl — they do NOT call ctrl->Release().
    // Without this call, FruitFactControl (m_bNoDestructor=1) and the other externally-
    // owned controls in the global HUD list are never removed or freed on game-quit.
    // Binary: GameOverScreen::Release @ 0x00140d98 / HUD::RemoveControl @ 0x00144c40.
    Release();
}

// ---------------------------------------------------------------------------
// Initialise (0x00142674)
// ---------------------------------------------------------------------------

// Binary @ 0x00142674
void GameOverScreen::Initialise(const char* modeName, int param2, float param3,
                                int expressionIdx, int bgPatternIdx,
                                int tabIndex, int starCount)
{
    // One-shot LoadContent (gated in binary by static guard; stub no-ops)
    LoadContent();

    // Defunct: NetworkManager.InvalidatePublishTextCallback -- no-op stub; binary @ 0x0014268c
    // Single call, no return value used; safe to skip on the SDL port.

    m_pNoticeCtrl    = nullptr; // +0xC8
    m_TabIndex       = tabIndex;
    m_Timer          = 0.0f;
    m_MostFruitCount = -1;
    field_0x118      = 0;
    m_StarCount      = starCount;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game_work.gameMode : 0;

    // Load mode-specific title texture into m_Texture (+0x74) so the
    // inherited HUDControl3d::Draw (vtable slot 7) renders it during the
    // state-0 entry animation. The state machine in Update sets size =
    // m_TitleSize * scale (sin-eased 0 -> 2x over 1.9s) and pos = (0,0,0)
    // each frame; HUDControl3d::Draw binds m_Texture and renders the
    // animated quad.
    //   gameMode 2 (Arcade) -> arcade_time_up.tex
    //   gameMode 3 (Zen)    -> time_up.tex
    //   default (Classic)   -> gameover.tex
    // ASM-verified: 2026-05-11 (asm-inspector). Binary's HUDControl3d::Draw
    // @ 0x0014428c gates on +0x74 only -- +0x78 is never read for drawing,
    // contrary to an earlier RE pass that conflated Ghidra's variable-name
    // inference with the actual offset literal.
    {
        Mortar::SmartPtr<Mortar::Texture> bgTex;
        if (gameMode == Mortar::GAME_MODE_ARCADE)
            bgTex = g_ArcadeTimeUpTitleTex;
        else if (gameMode == Mortar::GAME_MODE_ZEN)
            bgTex = g_TimeUpTitleTex;
        else
            bgTex = g_GameOverTitleTex;
        m_Texture = bgTex;
        if (bgTex) {
            m_TitleSizeX = (float)bgTex->m_Width;
            m_TitleSizeY = (float)bgTex->m_Height;
        } else {
            // DIFFERS: binary skips the m_TitleSize write when the texture
            // load fails (leaving stale data) -- port substitutes a hard-coded
            // 256x128 fallback so the state-0 grow animation has sensible
            // dimensions even when the localised .tex is missing.
            m_TitleSizeX = 256.0f;
            m_TitleSizeY = 128.0f;
        }
        m_TitleSizeZ = 0.0f;
    }

    m_State          = STATE_ENTRY_ANIM;
    m_LayerFlags     = Mortar::HUD_LAYER_NONE; // binary: m_LayerFlags = 0 in Initialise (BeginDraw sets it each frame)
    m_CommingSoonHighscoreTex.SetNull();   // +0x114 binary nulls in Initialise; loaded later in Update case-6 tail
    m_AnimCounter    = 0;
    m_bScoreSubmitted = 0;
    m_BgPatternIdx   = bgPatternIdx;
    field_0x94       = 0;
    m_pBonusScreen   = nullptr;
    m_FruitFactAlpha = game ? game_work.m_GameDt : 0.0f; // game[+0xC] = game.alpha (m_TransitionTimer)
    m_ExpressionIdx  = expressionIdx;
    field_0xa0       = 0;
    m_pSlot9c        = nullptr;
    m_bIsClassic     = (gameMode == Mortar::GAME_MODE_CLASSIC) ? 1 : 0;
    m_pQuitBtn       = nullptr;
    m_pSlotA8        = nullptr;
    m_pRetryBtn      = nullptr;
    field_0x7c       = 0.0f;

    // Randomise expression when caller passed -1
    if (expressionIdx < 1) {
        m_ExpressionIdx = 1;
        FindMostOfFruit();
        int score = GetCurrentScore(0);
        int hi    = GetCurrentModeHighscore();
        if (score > hi / 2) {
            // 2 or 3 (better expressions)
            m_ExpressionIdx = (rand() % 2) + 2;
        }
    }
    if (bgPatternIdx < 1) {
        m_BgPatternIdx = (rand() % 3) + 1;  // 1..3
    }

    pos.x = 0.0f; pos.y = 0.0f; pos.z = 0.0f;
    // Initial off-screen offset (DAT_001428d0=184.0, DAT_001428d4=75.0)
    m_OffsetPosX = 184.0f;
    m_OffsetPosY = 75.0f;
    m_OffsetPosZ = 0.0f;

    m_ProgressCounter  = 0;
    m_pFruitFact       = nullptr;
    m_pSlotC0          = nullptr;
    m_PostOk           = 0;
    m_PostInProgress   = 0;

    // Format the coin-earned label.
    // ASM-verified: 2026-05-08 binary @ 0x00142810 (re-analyst):
    //   sprintf(m_CoinsEarnedLabel, "YOU JUST EARNT %i COINS",
    //           game_work.m_CoinsBalance - game_work.m_CoinsAtGameStart)
    // The "X days left" placeholder string was a mis-guess; the real
    // format string at DAT_001428fc / 0x001bb926 is "YOU JUST EARNT %i
    // COINS". Note: CoinsEnabled() @ 0x0010a428 returns 0 in shipping
    // builds so the label is computed but never displayed -- still
    // load-bearing for the binary's call shape.
    {
        const int coinsEarned = game
            ? (game_work.m_CoinsBalance - game_work.m_CoinsAtGameStart) : 0;
        snprintf(m_CoinsEarnedLabel, sizeof(m_CoinsEarnedLabel),
                 "YOU JUST EARNT %d COINS", coinsEarned);
    }

    // FAST-PATH: caller passed valid score/state and we're past wave 5 with running progress
    if (param3 >= 0.0f && param2 >= 0) {
        FindMostOfFruit();
        // ASM-spec for binary @ 0x001428a0..0x001428bc (re-analyst):
        //   gate is `wave.alpha > 0.99899f` (DAT_001428d8 = 0x3F7FBE77)
        //   write is `wave.alpha = 0.99982f` (DAT_001428dc = 0x3F7FF2E5)
        //   NOT 0.999/1.0 as previously assumed.
        const float kWaveAlphaGate  = 0.99899f;  // DAT_001428d8
        const float kWaveAlphaSet   = 0.99982f;  // DAT_001428dc
        float waveAlpha = game ? game_work.m_GameDt : 0.0f;
        if (param2 > 5 && waveAlpha > kWaveAlphaGate) {
            if (game) game_work.m_GameDt = kWaveAlphaSet;
            m_State           = STATE_MAIN_DISPLAY;
            m_bScoreSubmitted = 1;
            m_FruitFactAlpha  = 1.0f;
            // Immediate state-6 invocation
            Update(0.0f);
        }
        m_State = param2;
        m_Timer = param3;
    }
}

// ---------------------------------------------------------------------------
// Init (vtable slot 2, 0x00140548) — trivial pass-through
// ---------------------------------------------------------------------------

// Binary @ 0x00140548
void GameOverScreen::Init() {
    // Binary @ 0x00140548: vtable[4] dispatch -- this calls Reset() virtually.
    // Reset() is empty in this class but the call shape preserves vtable parity.
    Reset();
}

// ---------------------------------------------------------------------------
// BeginDraw (vtable slot 5, 0x00140590)
// ---------------------------------------------------------------------------

// Binary @ 0x00140590
void GameOverScreen::BeginDraw(float /*dt*/) {
    // Binary: m_LayerFlags = (m_State != 0) ? 0x81 : 1
    // 0x81 = HUD_LAYER_POST_ACTOR | HUD_LAYER_DEFAULT -- Draw runs in BOTH
    // the post-actor (0x80) and default (0x01) HUD::Draw passes during
    // animations / transitions; once settled (m_State == 0) only the
    // default pass renders, suppressing the second draw.
    m_LayerFlags = (m_State != STATE_ENTRY_ANIM)
        ? (int)(Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT)
        : (int)Mortar::HUD_LAYER_DEFAULT;
}

// ---------------------------------------------------------------------------
// Release (vtable slot 3, 0x00140d98)
// ---------------------------------------------------------------------------

// Binary @ 0x00140d98
void GameOverScreen::Release() {
    m_CommingSoonHighscoreTex.SetNull();

    Game* game = Game::GetInstance();
    if (game && game_work.pGameOverScreen == this) {
        game_work.pGameOverScreen = nullptr;
        // ASM-verified: 2026-05-02 binary @ 0x00140d98 -- clear 5 cached slots
        if (FruitSaveData* sd = game_work.m_SaveData) {
            int* base = reinterpret_cast<int*>(sd);
            base[0x11C / 4] = -1;
            base[0x120 / 4] = 0;
            base[0x124 / 4] = -1;
            base[0x128 / 4] = -1;
            sd->newBestThisGame = 0;   // Binary writes BYTE 0 to +0x12c (not int -1)
        }
    }

    // Remove and free 4 aux HUDControls.
    // ASM-verified: 2026-05-02 binary @ 0x00140e14..0x00140e58 -- order: +0x9C, +0xBC, +0xC0, +0xA8
    if (game && game_work.mHud) {
        HUDControl* slots[4] = {
            m_pSlot9c,
            (HUDControl*)m_pFruitFact,
            (HUDControl*)m_pSlotC0,
            m_pSlotA8
        };
        for (int i = 0; i < 4; ++i) {
            if (slots[i]) game_work.mHud->RemoveControl(slots[i]);
        }
        for (int i = 0; i < 4; ++i) {
            if (slots[i]) {
                slots[i]->m_bNoDestructor = 0;
                delete slots[i];
            }
        }
    }
    m_pFruitFact = nullptr;
    m_pSlotC0    = nullptr;
    m_pSlot9c    = nullptr;
    m_pSlotA8    = nullptr;

    BonusManager::GetInstance()->ClearBestBonuses();
}

// ---------------------------------------------------------------------------
// SetTerminate (0x00140604)
// ---------------------------------------------------------------------------

// Binary @ 0x00140604
void GameOverScreen::SetTerminate() {
    // Binary: *(uint8_t*)(game + 0x33) = 1; CancelHUDProgressionTimer (no-op stub)
    m_bPendingRemoval = 1;
}

// ---------------------------------------------------------------------------
// SetStateWait (0x00140688)
// ---------------------------------------------------------------------------

// Binary @ 0x00140688
void GameOverScreen::SetStateWait() {
    Game* game = Game::GetInstance();
    // Binary @ 0x001406b4: increment lifetime "HighScoresAchieved" counter.
    // The leaderboard sign-in dialog gate that follows is Defunct in port,
    // but the AddToTotal side effect must still fire.
    if (game && game_work.m_SaveData) {
        game_work.m_SaveData->AddToTotal("HighScoresAchieved", 1);
    }
    // Defunct: leaderboard sign-in dialog gate -- binary opens a confirm
    // dialog when (saveData[+0x32]==0 && count>5 && score>50 &&
    // score > hiScore - 10). Port: always go to state 6.
    m_State = STATE_MAIN_DISPLAY;
}

// ---------------------------------------------------------------------------
// ProgressionTimer no-op stubs (empty in binary)
// ---------------------------------------------------------------------------

// Defunct: ProgressionTimer -- empty in binary @ 0x001405fc
void GameOverScreen::StartProgressionTimer() {}

// Defunct: ProgressionTimer -- empty in binary @ 0x00140600
void GameOverScreen::CancelHUDProgressionTimer() {}

// Defunct: ProgressionTimer -- empty in binary @ 0x00140614
void GameOverScreen::OnProgressionTimerUp() {}

// Defunct: ProgressionTimer -- empty in binary @ 0x00140618
void GameOverScreen::HandleProgressionTimerExpiration() {}

// ---------------------------------------------------------------------------
// Social share callbacks
// ---------------------------------------------------------------------------

// Defunct: Facebook share -- no-op stub; binary @ 0x0014083c (NetworkManager::PublishText)
void GameOverScreen::FacebookCallback() {}

// Defunct: Twitter share -- empty in binary @ 0x001405f8
void GameOverScreen::TwitterCallback() {}

// Binary @ 0x001405e8 -- PostCallback(result): m_bPostInProgress=0; m_bPostOk=(result==0)
void GameOverScreen::PostCallback(int result) {
    m_PostInProgress = false;
    m_PostOk = (result == 0);
}

// ---------------------------------------------------------------------------
// LeaderboardsCallback (Binary @ 0x001405a0)
// ---------------------------------------------------------------------------

// Binary @ 0x001405a0 -- LeaderboardsCallback: state-0/6 + alpha>0.999 -> m_State=10
//                       (launches NetworkManager dashboard, defunct)
void GameOverScreen::LeaderboardsCallback() {
    if (m_State == STATE_ENTRY_ANIM || m_State == STATE_MAIN_DISPLAY) {
        Game* game = Game::GetInstance();
        if (game && game_work.m_GameDt > 0.999f) {
            m_Timer = 0.0f;
            m_State = STATE_LEADERBOARD;
        }
    }
}

// ---------------------------------------------------------------------------
// DeletedControl (Binary @ 0x00140558)
// ---------------------------------------------------------------------------

// Binary @ 0x00140558 -- wired as remove-callback on m_pBonusScreen/m_pSlot9c/m_pNoticeCtrl.
// On removal, clears the slot and (for bonusScreen+noticeCtrl) forces state=6.
void GameOverScreen::DeletedControl(HUDControl* ctrl) {
    if (ctrl == (HUDControl*)m_pBonusScreen) { m_pBonusScreen = nullptr; m_State = STATE_MAIN_DISPLAY; }
    // Binary @ 0x00140558: middle slot is m_pRetryBtn (+0x98), not m_pSlot9c (+0x9c).
    // No state change in this branch -- just clear the pointer.
    if (ctrl == (HUDControl*)m_pRetryBtn)    { m_pRetryBtn = nullptr; }
    // Binary @ 0x00141408 wires quit to the same DeletedControl -- clear m_pQuitBtn on removal.
    if (ctrl == (HUDControl*)m_pQuitBtn)     { m_pQuitBtn = nullptr; }
    if (ctrl == m_pNoticeCtrl)               { m_pNoticeCtrl = nullptr; m_State = STATE_MAIN_DISPLAY; }
}

// ---------------------------------------------------------------------------
// FindMostOfFruit (0x00141a18)
// ---------------------------------------------------------------------------

// Binary @ 0x00141a18
void GameOverScreen::FindMostOfFruit() {
    Game* game = Game::GetInstance();
    FruitSaveData* save = game ? game_work.m_SaveData : nullptr;
    if (!save) return;

    int count = FruitInfo_GetCount();
    if (count <= 0) return;

    uint8_t gameMode = game_work.gameMode;

    // Step 1: build candidate index list, filtering power-fruits in Arcade mode
    int candidates[FRUIT_INFO_MAX];
    int numCandidates = 0;
    for (int i = 0; i < count && i < FRUIT_INFO_MAX; ++i) {
        const FruitInfo* fi = FruitInfo_Get(i);
        if (!fi) continue;
        // Binary @ 0x00141a18: in Arcade (mode==2) include only POWER fruits
        // (fi->m_pPowers != nullptr); other modes include all fruits.
        if (gameMode == Mortar::GAME_MODE_ARCADE && fi->m_pPowers == nullptr) continue;
        candidates[numCandidates++] = i;
    }

    if (numCandidates == 0) return;

    // Step 2: shuffle candidates (random fruit order eliminates ties bias)
    for (int i = numCandidates - 1; i > 0; --i) {
        int j = rand() % (i + 1);
        int tmp = candidates[i];
        candidates[i] = candidates[j];
        candidates[j] = tmp;
    }

    // Step 3+4: fetch GetTotal for each candidate, track max
    int bestCount = 0;
    int bestIdx   = -1;
    for (int k = 0; k < numCandidates; ++k) {
        int i = candidates[k];
        const FruitInfo* fi = FruitInfo_Get(i);
        if (!fi) continue;
        int c = save->GetTotal(fi->m_TotalStatHash);
        if (c > bestCount) {
            bestCount = c;
            bestIdx   = i;
        }
    }

    // Step 5: write result
    if (bestCount > 0) {
        field_0x118      = bestIdx;   // most-eaten fruit type index
        m_MostFruitCount = bestCount;
    }
}

// ---------------------------------------------------------------------------
// CreateRetryButton (0x00141188)
// ---------------------------------------------------------------------------

// Binary @ 0x00141188
void GameOverScreen::CreateRetryButton() {
    if (m_pRetryBtn != nullptr) return;

    Game* game = Game::GetInstance();
    if (!game || !game_work.mHud) return;

    // ASM-spec for binary @ 0x00141188 (re-analyst):
    //   pos               = (-80, -96, 0)              [DAT_001412c4..cc]
    //   tex               = g_RetryTexSP @ GOT+0x7310  [retry.tex, loaded in LoadContent]
    //   clickDelegate     = &RetryCallback
    //   fruitType         = 0 (literal apple in the call site, NOT a DAT lookup)
    //   globalCenterVec   = HUD::g_GlobalCenterVec @ 0x001f4328 (singleton)
    //   deletedDelegate   = HUD::g_DeleteControlDelegate (HUD-wide remove cb)
    // Port maps:
    //   - texture: TextureManager::LoadLocalisedTexture("retry.tex") --
    //     port-side single-shot since LoadContent is a no-op stub.
    //   - globalCenterVec / deletedDelegate: route via MenuButton::Init's
    //     hitBounds / deletedCb args (port's Init takes 5 args matching
    //     the binary's ctor minus the texture, which is set via the
    //     m_Texture SmartPtr field).
    //   - HUD-wide deleted delegate: binary's MakeDelegate_HUD constructs
    //     Delegate0<void>{HUD::DeleteControl, hud_this}. Port leaves it
    //     empty -- MenuButton's deletedCb dispatch is never wired in port,
    //     so the cleanup path differs but no observable break today.
    Vec3 btnPos(-80.0f, -96.0f, 0.0f);
    Vec3 globalCenter(0.0f, 0.0f, 0.0f);  // HUD::g_GlobalCenterVec; HUD::Init sets to (0,0,0)
    Mortar::SmartPtr<Mortar::Texture> tex = g_RetryTex;  // GOT+0x7310

    m_pRetryBtn = new MenuButton();
    // m_Texture (+0x74) is the binary's used slot: binary ctor writes ring tex
    // to +0x74, HUDControl3d::Draw Phase B reads it from +0x74. No divergence.
    m_pRetryBtn->m_Texture    = tex;
    // Binary CreateRetryButton @ 0x00141188 does NOT explicitly write +0x34;
    // MenuButton::Init writes HUD_LAYER_MENU_BG for FruitType >= 0 (here 0).
    m_pRetryBtn->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    m_pRetryBtn->Init(
        btnPos,
        Mortar::Delegate0<void>::Make(this, &GameOverScreen::OnRetryClicked),
        /*fruitType=*/0,
        globalCenter,
        // Binary builds Delegate0{HUD::DeleteControl, game_work.mHud} here.
        // Port leaves empty -- MenuButton's deletedCb dispatch is unwired.
        Mortar::Delegate0<void>()
    );

    game_work.mHud->AddControl(m_pRetryBtn, false);
    // Binary @ 0x001412a0: wires HUD-removal callback to DeletedControl.
    // Without this, HUD::Remove leaves a dangling pointer in m_pRetryBtn.
    m_pRetryBtn->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);
}

// Binary @ 0x0014105c
void GameOverScreen::RetryCallback() {
    Game* game = Game::GetInstance();
    if (!game) return;
    if (m_State != STATE_ENTRY_ANIM && m_State != STATE_MAIN_DISPLAY &&
        m_State != STATE_QUICK_RESTART && m_State != STATE_LEADERBOARD) return;
    // ASM-verified: 2026-05-18 binary @ 0x0014105c (re-analyst). STATE_BONUS_PHASE intentionally excluded -- binary silent-absorbs taps during the bonus animation; the real "tap doesn't work" symptom is BONUS_PHASE stalling, tracked separately.
    if (game_work.m_GameDt <= 0.989945f) return;
    // ASM-verified: 2026-05-18 binary @ 0x0014105c (re-analyst). m_TransitionTimer > 0.989945f gate matches DAT_00141158; floating-point literal reproduced exactly from binary rodata.
    CancelHUDProgressionTimer();
    // Binary @ 0x001410a8..0x001410d2: inlined SeedGlobalRng equivalent --
    // same 6-word PRNG reseed that PauseScreen::RetryGameCallback bl's at
    // 0x00153fbc. Seeded from Game+0x194 (the port's m_FrameTimer field --
    // binary reuses the frame counter bits as RNG entropy on retry).
    // Constants in the inlined version (0x6c078965, 0x5d588b65) are
    // Knuth/MMIX LCG multipliers, not Marsaglia.
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
    // Binary @ 0x001410d6: FruitSaveData::ClearCombo(pSaveData)
    if (game_work.m_SaveData) game_work.m_SaveData->ClearCombo();
    // Defunct: MP scene-alpha bypass -- IsMultiplayer always false in port
    m_State = STATE_RETRY_PREPARE;
    // ASM-verified: 2026-05-13 binary @ 0x0014110c (re-analyst).
    //   DAT_00141170 resolves to rodata @ 0x001B96AF = "Game-start" --
    //   retry reuses the game-launch SFX, not a "menu-retry" sound.
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("Game-start", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
}

void GameOverScreen::OnRetryClicked() {
    RetryCallback();
}

// ---------------------------------------------------------------------------
// CreateQuitButton (0x001412e4)
// ---------------------------------------------------------------------------

// Binary @ 0x001412e4
void GameOverScreen::CreateQuitButton() {
    Game* game = Game::GetInstance();
    if (!game || !game_work.mHud) return;

    // ASM-spec for binary @ 0x001412e4 (re-analyst 2026-05-18):
    //   pos               = (80, -96, 0)               [DAT_00141428..30]
    //   tex               = g_QuitTexSP @ GOT+0x73fc   [quit.tex, loaded in LoadContent]
    //   clickDelegate     = &QuitCallback
    //   fruitType         = **(int**)(GOT+DAT_00141440) -- the GOT chain
    //                       0x00007060 -> 0x001f3190 -> g_pFruitInfoArrayHead,
    //                       deref to first 4 bytes of FRUIT_INFO[0].m_Name
    //                       = "Appl" little-endian = 0x6C707041 = 1818849377.
    //                       Intentional "huge sentinel" idiom: any printable
    //                       4-char string read as int exceeds the bomb
    //                       threshold (FruitInfo_GetCount() = 23), so
    //                       MenuButton::Init's `(count <= fruitType)` check
    //                       routes ActorManager::Add to type 1 = BOMB.
    //                       (Earlier port comment labelled this a "binary bug"
    //                       and substituted 0, which silently turned the Quit
    //                       bomb into an Apple -- user-reported symptom.)
    //   globalCenterVec   = HUD::g_GlobalCenterVec
    //   deletedDelegate   = HUD::g_DeleteControlDelegate
    // 3 quit-only post-init steps (binary 0x00141420..0x00141438) are
    // implemented below at lines 720/724/727 -- m_TargetSize copy,
    // m_bRespondsToBackKey, TutorialControl::ResetTutePos.
    Vec3 btnPos(80.0f, -96.0f, 0.0f);
    Vec3 globalCenter(0.0f, 0.0f, 0.0f);
    Mortar::SmartPtr<Mortar::Texture> tex = g_QuitTex;  // GOT+0x73fc

    m_pQuitBtn = new MenuButton();
    // m_Texture (+0x74) is the binary's used slot: binary ctor writes ring tex
    // to +0x74, HUDControl3d::Draw Phase B reads it from +0x74. No divergence.
    m_pQuitBtn->m_Texture    = tex;
    // Binary CreateQuitButton @ 0x001412e4 does NOT explicitly write +0x34;
    // MenuButton::Init writes HUD_LAYER_MENU_BG for FruitType >= 0 (the
    // FruitType comes from a global int; shipped data keeps it >= 0).
    m_pQuitBtn->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    m_pQuitBtn->Init(
        btnPos,
        Mortar::Delegate0<void>::Make(this, &GameOverScreen::OnQuitClicked),
        // Binary passes 1818849377 ("Appl" reinterpreted as int); any value
        // >= FruitInfo_GetCount() triggers MenuButton::Init's bomb branch.
        // Use the threshold directly -- equivalent runtime semantics, more
        // readable, won't break if the fruit set changes.
        /*fruitType=Bomb*/ FruitInfo_GetCount(),
        globalCenter,
        Mortar::Delegate0<void>()  // see CreateRetryButton -- HUD-delete delegate unwired
    );

    game_work.mHud->AddControl(m_pQuitBtn, false);
    // Binary @ 0x00141408: wires HUD-removal callback to DeletedControl.
    // Without this, HUD::Remove leaves a dangling pointer in m_pQuitBtn.
    m_pQuitBtn->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);

    // Binary @ 0x001413d2: copy 12 bytes from retry button at +0x124..+0x12c.
    // MenuButton+0x124 is m_TargetSize (Vec3).
    if (m_pRetryBtn) {
        m_pQuitBtn->m_RestScale = m_pRetryBtn->m_RestScale;
    }
    // Binary @ 0x00141400: byte at MenuButton+0x138 = 1.
    // +0x138 is m_bRespondsToBackKey -- quit button captures the back-key.
    m_pQuitBtn->m_bRespondsToBackKey = 1;
    // Binary @ 0x0014141e: TutorialControl::ResetTutePos UNCONDITIONALLY
    // (with retry-or-quit arg).
    if (game_work.m_TutorialControl) {
        MenuButton* tutBtn = m_pRetryBtn ? m_pRetryBtn : m_pQuitBtn;
        game_work.m_TutorialControl->ResetTutePos(tutBtn);
    }
}

// Binary @ 0x00140620
void GameOverScreen::QuitCallback() {
    Game* game = Game::GetInstance();
    if (!game) return;
    if (m_State != STATE_ENTRY_ANIM && m_State != STATE_MAIN_DISPLAY &&
        m_State != STATE_QUICK_RESTART && m_State != STATE_LEADERBOARD) return;
    // ASM-verified: 2026-05-18 binary @ 0x00140620 (re-analyst). STATE_BONUS_PHASE intentionally excluded -- binary silent-absorbs taps during the bonus animation; the real "tap doesn't work" symptom is BONUS_PHASE stalling, tracked separately.
    CancelHUDProgressionTimer();
    m_State = STATE_QUIT_WAIT;
    // Binary @ 0x001410d6: FruitSaveData::ClearCombo
    if (game_work.m_SaveData) game_work.m_SaveData->ClearCombo();
    // Binary @ 0x00140674: HitMenuBomb at quit-button position (DAT_00140674 = Vec3(163.0, -96.0, 0.0))
    Bomb::HitMenuBomb(Vec3(163.0f, -96.0f, 0.0f));
}

void GameOverScreen::OnQuitClicked() {
    QuitCallback();
}

// Binary @ 0x001e84b8 -- 25 const char* entries indexed by FruitFactControl::m_ComboType.
// ASM-verified: 2026-05-20 binary @ 0x00110c94 GetComboName (re-analyst).
// Used only to feed StringHash() into AchievementManager::UnlockComboStarAchievement.
static const char* const g_ComboNameTable[25] = {
    "3_FRUIT", "4_FRUIT", "5_FRUIT", "6_FRUIT",
    "ALL_DIFFERENT", "7_FRUIT_PLUS",
    "ALL_APPLES", "ALL_ORANGES", "ALL_PINEAPPLES", "ALL_WATERMELONS",
    "ALL_KIWIS", "ALL_MANGOES", "ALL_STRAWBERRIES", "ALL_PEARS",
    "ALL_BANANAS", "ALL_LIMES", "ALL_LEMONS", "ALL_COCONUTS",
    "ALL_PASSIONFRUITS",
    "ALPHABETICAL", "FULLHOUSE",
    "2_PAIR", "3_OF_A_KIND", "4_OF_A_KIND", "5_OF_A_KIND",
};

static const char* GetComboName(int starType) {
    // Binary @ 0x00110c94 -- no bounds check; caller gates 0 <= starType <= 24.
    return g_ComboNameTable[starType];
}


// ---------------------------------------------------------------------------
// Update (vtable slot 10, 0x00141b34) — main state machine (529 lines)
// ---------------------------------------------------------------------------

// Binary @ 0x00141b34
//
// Re-analyst 2026-05-08 corrections to legacy TODOs:
//   - "fast-skip path m_TransitionTimer = 0.0f" — no such write exists in
//     Update; deleted (was a port-time speculation). The fast-path lives
//     in Initialise (now handled, see 0.99899/0.99982 constants there).
//   - pSaveData[+0x12C] is a uint8_t = (uint8_t)SetCurrentModeHighscore(score)
//     — port wrote -1 (int) on a different code path; corrected below.
//   - state 6 ComboStarAchievement gate is
//       (m_pBonusScreen != null && gameMode == 3 &&
//        (int8_t)bonus[+0xE0] in [0, 25)).
//     Combo length is bonus[+0xD0] (uint64_t); star type is bonus[+0xE0]
//     (int8_t); hash via StringHash(GetComboName(starType)).
void GameOverScreen::Update(float dt) {
    Game* game = Game::GetInstance();

    // Default m_LayerFlags for the frame (binary @ 0x00141b40, before
    // the state switch). Case 0 overrides to 1 for the entry animation.
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // Advance animation counter (millisecond-resolution circular)
    // Binary: field_0xac = (float)(int)(field_0xac) + dt*1000.0, then mod 1000
    m_AnimCounter = (int)(((float)m_AnimCounter + dt * 1000.0f));
    if (m_AnimCounter >= 1000) m_AnimCounter -= 1000;

    switch (m_State) {

    // -----------------------------------------------------------------------
    // State 0: entry animation — sin-eased scale-in over 1.9s
    // -----------------------------------------------------------------------
    case STATE_ENTRY_ANIM: {
        // First frame: force game.processing=1 based on game mode + entity count
        if (m_bScoreSubmitted == 0) {
            uint8_t gm = game_work.gameMode;
            Mortar::ActorManager* am = game->actorManager;
            if ((uint8_t)(gm - 2) < 2) { // Arcade (2) or Zen (3)
                if (am && am->GetNumEntities(0) == 0 && am->GetNumEntities(1) == 0)
                    game_work.m_bSlowMotion = 1; // game[+0x35] = m_bProcessing
            } else {
                game_work.m_bSlowMotion = 1;
            }
        }

        m_LayerFlags = Mortar::HUD_LAYER_DEFAULT; // single-layer during entry

        m_Timer += dt;
        const float ENTRY_DURATION = 1.9f;   // DAT_00141dac
        const float SIN_FULL       = 20000.0f; // DAT_00141da8

        if (m_Timer < ENTRY_DURATION) {
            float t = (m_Timer / ENTRY_DURATION) * SIN_FULL;
            uint16_t idx;
            if (t > SIN_FULL) idx = 0x4E34;
            else              idx = (uint16_t)(int)t;
            float curr = SinIdx(idx);
            float full = SinIdx(0x4E34);
            float scaleF = (full != 0.0f) ? (curr / full) : 0.0f;
            size.x = m_TitleSizeX * scaleF * 2.0f;
            size.y = m_TitleSizeY * scaleF * 2.0f;
            size.z = m_TitleSizeZ * scaleF * 2.0f;
        } else {
            size.x = m_TitleSizeX * 2.0f;
            size.y = m_TitleSizeY * 2.0f;
            size.z = m_TitleSizeZ * 2.0f;
        }

        if (m_Timer > ENTRY_DURATION) {
            if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
                m_State = STATE_BONUS_PHASE;
                m_Timer = -0.333f; // DAT_00141db0
            } else {
                SetStateWait(); // goes to state 6 or pops sign-in dialog
            }
        }
        pos.x = 0.0f; pos.y = 0.0f; pos.z = 0.0f;
        break;
    }

    // -----------------------------------------------------------------------
    // State 1: bonus phase (Arcade only) — BonusScreen creation + slide
    // -----------------------------------------------------------------------
    // TODO: BONUS_PHASE stall investigation (Claude task #41).
    // Binary has NO fallback timeout here; trusts entities to drain
    // via gravity + Fruit/Bomb CheckHasGoneOffscreen. Port stalls in
    // Arcade -- some entity isn't draining. RE @ 0x00141b34 found
    // m_bSlowMotion is NOT a gate (cosmetic write-only). Likely
    // candidates: (a) Fruit::ClearUnspawned semantic -- binary calls
    //   KillFruit(this, 0), port calls am->Deactivate(f); (b) some
    //   fruit-half pos/vel state staying on-screen indefinitely.
    // Needs runtime logging of surviving (pos, vel, flags,
    // m_SpawnDelay, m_bSliced) per frame to identify. See
    // tmp/bonus-phase-stall-spec.md for full RE.
    case STATE_BONUS_PHASE: {
        Mortar::ActorManager* am = game->actorManager;
        if (am && am->GetNumEntities(0) == 0 && am->GetNumEntities(1) == 0) {
            if (!m_pBonusScreen) {
                FindMostOfFruit();
                m_pBonusScreen = new BonusScreen();
                m_pBonusScreen->pos = Vec3(0.0f, -20.0f, 0.0f);
                // Binary @ 0x00141d50: bonus->m_RemoveCallback = Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl)
                m_pBonusScreen->m_RemoveCallback =
                    Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);
                if (game_work.mHud) game_work.mHud->AddControl(m_pBonusScreen, false);
                BonusManager::GetInstance()->SetUpBonusScreen(m_pBonusScreen);
            } else {
                // GO screen tracks bonus position for layout alignment.
                // ASM-spec for binary @ 0x00141bd0..0x00141bf0 (re-analyst):
                //   ny = bonus.pos.y + bonus[+0xC0] + 135.0f   (DAT_00141DC8)
                //   then m_OffsetPosY = max(m_OffsetPosY, ny)
                //   plus a size_factor = pos.y / -224.0f + 1.0f rescale
                //   (DAT_00141DCC) on the row above.
                // bonus[+0xC0] is BonusScreen::m_PosOffset.y (Vec3 starts at
                // +0xBC, .y at +0xC0). Per re-analyst 2026-05-18 (was
                // previously misread as m_PhaseTimer). The 135.0f bias is
                // DAT_00141DC8.
                float ny = m_pBonusScreen->pos.y + m_pBonusScreen->m_PosOffset.y + 135.0f;
                m_OffsetPosY = std::max(m_OffsetPosY, ny);

                // Binary @ 0x00141d??: pos.y rescale + size = m_TitleSize * scale.
                //   fVar23 = bonus->size.y + bonus->pos.y + DAT_00141dc8 (small bias)
                //   if (fVar23 < pos.y) fVar23 = pos.y;
                //   pos.y = fVar23;
                //   scale = pos.y / DAT_00141dcc + 1.0f   -- DAT_00141dcc = -224.0f
                //   size = m_TitleSize * scale
                // ASM-verified: 2026-05-10 binary @ 0x00141dc8 / 0x00141dcc (re-analyst).
                //   DAT_00141dc8 = 135.0f -- size-rescale Y bias.
                //   DAT_00141dcc = -224.0f -- divisor for pos.y / D + 1.0 scale.
                const float bias = 135.0f;
                const float divisor = -224.0f;
                float newPosY = m_pBonusScreen->size.y + m_pBonusScreen->pos.y + bias;
                if (newPosY < pos.y) newPosY = pos.y;
                pos.y = newPosY;
                const float scaleFactor = pos.y / divisor + 1.0f;
                size.x = m_TitleSizeX * scaleFactor;
                size.y = m_TitleSizeY * scaleFactor;
                size.z = m_TitleSizeZ * scaleFactor;

                // When BonusScreen sets m_bPendingRemoval, transition to main display.
                if (m_pBonusScreen->m_bPendingRemoval) {
                    SetStateWait();
                }
            }
            // Binary: m_Timer += dt and m_PhaseTimer write happen in BOTH
            // create + existing branches (hoisted out of else to after the if/else).
            m_Timer += dt;
            if (m_pBonusScreen) m_pBonusScreen->m_PhaseTimer = m_Timer;
            // Binary @ 0x00141ce8: per-frame in this branch, force
            // game_work.m_bSlowMotion = 1 (suppress entity processing during
            // the bonus phase).
            game_work.m_bSlowMotion = 1;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // State 7: retry — guard entities & reset wave & flag pause
    // -----------------------------------------------------------------------
    case STATE_RETRY_PREPARE: {
        Mortar::ActorManager* am = game->actorManager;
        if (am && am->GetNumEntities(0) != 0 && m_pSlot9c == nullptr) {
            game_work.m_GameDt = 1.0f;
            // Fall through to STATE_MAIN_DISPLAY (m_State is still 7)
        } else {
            game_work.m_CoinsAtGameStart = game_work.m_CoinsBalance;
            WaveManager::GetInstance()->Reset(false);
            game_work.m_LevelTransitionFlag = 1;
            m_State = STATE_RETRY_FADE;
            break;
        }
    }
    // Fall-through from STATE_RETRY_PREPARE
    // -----------------------------------------------------------------------
    // State 6: main display + score submission
    // -----------------------------------------------------------------------
    case STATE_MAIN_DISPLAY:
    {
        const int prevState = m_State;

        // 1) Create FruitFactControl on first entry
        if (m_pFruitFact == nullptr) {
            m_pFruitFact = new FruitFactControl();
            m_pFruitFact->pos.x = 183.0f + m_OffsetPosX;
            m_pFruitFact->pos.y = 12.0f  + m_OffsetPosY;
            m_pFruitFact->pos.z = 0.0f;
            m_pFruitFact->m_TabIndex  = (uint8_t)m_TabIndex;
            m_pFruitFact->m_StarType  = (uint8_t)m_StarCount;
            if (game_work.mHud) game_work.mHud->AddControl(m_pFruitFact, false);
            m_pFruitFact->Init();
        }

        // 2) Pop-in animation: game.alpha ramps toward 1.0
        float& alpha = game_work.m_GameDt;
        const float ALPHA_THRESH = 0.999f;
        if (alpha < ALPHA_THRESH) {
            m_ProgressCounter = 0;
            alpha += (1.0f - alpha) * 0.125f;
            if (alpha < 0.75f) game_work.m_bSlowMotion = 1;
            if (alpha >= ALPHA_THRESH) alpha = 1.0f;
            m_FruitFactAlpha = alpha;
        } else {
            if (m_FruitFactAlpha < 1.0f)
                m_FruitFactAlpha += (1.0f - m_FruitFactAlpha) * 0.125f;
            if (m_ProgressCounter < 11) m_ProgressCounter++;
        }

        // 3) On frame 10 (single-shot via m_bScoreSubmitted): commit scores
        if (m_ProgressCounter == 10) {
            m_ProgressCounter = 11;
            if (m_bScoreSubmitted == 0) {
                int score = GetCurrentScore(0);
                m_bScoreSubmitted = 1;

                FruitSaveData* save = game_work.m_SaveData;
                if (save) {
                    save->secondaryFlag = 0;

                    save->AddToTotal("FruitsCollected", 1);
                    save->AddToTotal("TotalScore", score);
                    save->UnlockTotals();

                    AchievementManager::GetInstance()->UnlockScoreAchievement(score);
                    AchievementManager::GetInstance()->UnlockTotalFruitAchievement(game_work.fruitTotal);
                    AchievementManager::GetInstance()->UnlockEndScoreAchievement(score, GetCurrentModeHighscore());

                    if (m_pFruitFact && game_work.gameMode == Mortar::GAME_MODE_ZEN) {
                        int comboType = (int8_t)m_pFruitFact->m_ComboType;
                        if (comboType >= 0 && comboType < 25) {
                            int comboLen = m_pFruitFact->m_ComboLength;
                            AchievementManager::GetInstance()->UnlockComboStarAchievement(
                                comboLen, StringHash(GetComboName(comboType)));
                        }
                    }

                    int hi = GetCurrentModeHighscore();
                    if (hi / 2 < score) {
                        save->newBestThisGame =
                            save->SetCurrentModeHighscore(score) ? 1 : 0;
                    }

                    if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
                        BonusManager::GetInstance()->UnlockPostGameAchievements();
                    }

                    save->FinishedGame();
                    save->ClearTotals();
                    FruitNinja_SaveCurrentData(false);
                }

                // Defunct: "coming soon" highscore-leaderboard placeholder
                m_CommingSoonHighscoreTex = TextureManager::LoadLocalisedTexture("comming_soon_highscore.tex");
            }

            game_work.m_GameDt = 1.0f;

            // Spawn retry/quit buttons only when entering from state 6
            if (prevState == 6 && IsAllowedToExit()) {
                CreateRetryButton();
            }
            if (m_pQuitBtn == nullptr && prevState == 6 && IsAllowedToExit()) {
                CreateQuitButton();
            }
        }

        // 6) Vertical "settle"
        if (pos.y < 212.8f) {
            float a = game_work.m_GameDt;
            float sf = 2.0f - a;
            size.x = m_TitleSizeX * sf;
            size.y = m_TitleSizeY * sf;
            pos.x = 0.0f;
            pos.y = 224.0f * a;
            pos.z = 0.0f;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // State 8: camera fade-out for retry
    // -----------------------------------------------------------------------
    case STATE_RETRY_FADE: {
        float& alpha = game_work.m_GameDt;
        alpha *= 0.75f;
        m_FruitFactAlpha = alpha;

        const float ALPHA_LOW = 0.001f; // DAT_00142114
        if (alpha < ALPHA_LOW) {
            WaveManager::GetInstance()->Reset(false);
            alpha = 0.0f;
            game_work.m_LevelTransitionFlag = 0;
            m_FruitFactAlpha = 0.0f;
            WaveManager::NewGame();
            SetTerminate();
        }

        // Slide up off-screen as alpha decays
        if (pos.y < 0.0f) {
            // pos.y = 0.0 + (1-m_FruitFactAlpha) * 224.0
            pos.x = 0.0f;
            pos.y = (1.0f - m_FruitFactAlpha) * 224.0f; // DAT_0014211c = 224.0
            pos.z = 0.0f;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // State 9: quit path — wait for entities, then QuitToMenu
    // -----------------------------------------------------------------------
    case STATE_QUIT_WAIT: {
        Mortar::ActorManager* am = game->actorManager;
        if (am && am->GetNumEntities(0) != 0) break; // wait
        DoQuitToMenu();
        m_State = STATE_FINAL_FADE;
        break;
    }

    // -----------------------------------------------------------------------
    // State 10: online leaderboard launch (defunct) — no-op, back to state 6
    // -----------------------------------------------------------------------
    case STATE_LEADERBOARD: {
        // Binary gate: if (ActorManager::GetNumEntities(0) + GetNumEntities(1) == 0)
        Mortar::ActorManager* amLb = game->actorManager;
        const int totalEnts = amLb ? (amLb->GetNumEntities(0) + amLb->GetNumEntities(1)) : 0;
        if (totalEnts != 0) break;

        // Note: NetworkManager::LaunchDashboard() -- defunct (online-services-audit).
        m_ProgressCounter = 0;
        m_pQuitBtn        = nullptr;
        m_pRetryBtn       = nullptr;
        field_0xa0        = 0;
        m_State           = STATE_MAIN_DISPLAY;
        break;
    }

    // -----------------------------------------------------------------------
    // State 11: final fade-out
    // -----------------------------------------------------------------------
    case STATE_FINAL_FADE: {
        // Binary: if (game.alpha < 0.0f) SetTerminate()
        if (game_work.m_GameDt < 0.0f) SetTerminate();
        break;
    }

    // -----------------------------------------------------------------------
    // State 14: quick-restart hot path (binary @ 0x001423b4-0x001423f8)
    // -----------------------------------------------------------------------
    case STATE_QUICK_RESTART: {
        const int prevSlot9c = (m_pSlot9c != nullptr) ? 1 : 0;  // saved before zero-out
        m_Timer += dt * 8.0f;
        if (m_Timer >= 8.0f) {
            m_Timer = 0.0f;       // transient -- overwritten below
        }
        m_State = STATE_MAIN_DISPLAY;
        game_work.m_bUpdatesSuspended = 0;
        if (prevSlot9c == 0) m_ProgressCounter = 0;
        m_Timer = 2.0f;           // FINAL unconditional write
        break;
    }

    default:
        // Unhandled states (2..5, 12, 13, 15+): do nothing (matches binary)
        break;
    }

    // -----------------------------------------------------------------------
    // Common layout block (runs every frame after switch).
    //
    // ASM-spec for binary @ 0x001424dc..0x00142516 (re-analyst):
    //   DAT_00142624 =   75.0f   bonus.x base
    //   DAT_00142628 =  480.0f   bonus.x alpha-coeff (multiplied by 1-alpha)
    //   DAT_0014262c = -102.0f   bonus.y
    //   DAT_00142630 = -386.0f   classic OffsetX alpha-coeff / bonus FruitFact x-offset
    //   DAT_00142634 =  368.0f   classic OffsetX base
    //   DAT_00142638 =   55.0f   classic OffsetY
    //   DAT_0014263c =  183.0f   classic FruitFact x-offset
    //   DAT_00142640 = -5000.0f  m_pSlotC0 Z (far-behind camera during slide)
    //   DAT_00142644 =   65.0f   m_pSlotC0 Y base
    //   DAT_00142648 =  300.0f   m_pSlotC0 Y slide coeff
    //   DAT_0014264c =  190.0f   retry/quit X base
    //   DAT_00142650 =  -50.0f   retry Y
    //   DAT_00142654 =  120.0f   retry/quit X slide coeff
    //   DAT_00142658 = -125.0f   quit Y
    //   DAT_00142670 -> &g_JitterVec3 (per-frame jitter source; zero in shipping binary)
    //
    // The previous port-side numeric guesses (-204 / -193 / 75 / 130 / -50 /
    // 240 / -56) didn't come from these DATs and produced noticeably wrong
    // layout vs the binary. Constants below are now binary-faithful.
    // -----------------------------------------------------------------------
    // ASM-verified: 2026-05-09 binary @ 0x00141b34..0x00142613 (re-analyst).
    // DAT constants:
    //   0x00142620 = -20.0  (unused in shipping layout; earlier port used as BonusScreen.y)
    //   0x00142624 =  75.0  (Arcade-Zen FruitFact x base)
    //   0x00142628 = 480.0  (Arcade-Zen FruitFact x slide coeff)
    //   0x0014262c = -102.0 (Arcade-Zen m_OffsetPosX delta from fruitFact.x)
    //   0x00142630 = -386.0 (Classic FruitFact slide coeff)
    //   0x00142634 = 368.0  (Classic m_OffsetPosX base)
    //   0x00142638 =  55.0  (Classic m_OffsetPosY)
    //   0x0014263c = 183.0  (Classic FruitFact +x offset)
    //   0x0014264c = 190.0  (Retry/Quit X)
    //   0x00142650 = -50.0  (Retry Y base)
    //   0x00142654 = 120.0  (jitter magnitude)
    //   0x00142658 = -125.0 (Quit Y base)
    //   0x00142670 = &g_JitterVec3 (shake source — port-side stub returns 0)
    uint8_t gm = game_work.gameMode;
    if ((uint8_t)(gm - 2) < 2 && m_pFruitFact != nullptr) {
        // Binary @ 0x0014245c..0x00142498 — online/Arcade modes (gameMode in {2,3})
        // fruitfact layout. The gate is on m_pFruitFact != nullptr; the binary does
        // NOT touch m_pBonusScreen->pos here. Earlier port mis-attributed the gate
        // + included a stray bonus-pos write that wasn't in the binary.
        const float ffX = (1.0f - m_FruitFactAlpha) * 480.0f + 75.0f;
        m_pFruitFact->pos.x = ffX;
        m_pFruitFact->pos.y = 53.0f;
        m_pFruitFact->pos.z = 0.0f;
        // m_OffsetPos = fruitFact->pos + (-102, 4, 0)
        m_OffsetPosX = ffX - 102.0f;
        m_OffsetPosY = 53.0f + 4.0f;   // = 57.0f
        m_OffsetPosZ = 0.0f;
    } else {
        // Classic / single-player layout
        m_OffsetPosX = 368.0f + (-386.0f) * m_FruitFactAlpha;
        m_OffsetPosY = 55.0f;
        m_OffsetPosZ = 0.0f;
        if (m_pFruitFact) {
            m_pFruitFact->pos.x = m_OffsetPosX + 183.0f;
            m_pFruitFact->pos.y = m_OffsetPosY + 12.0f;
            m_pFruitFact->pos.z = 0.0f;
        }
        // Binary @ 0x001424b4..0x001424d8 — m_pSlotC0 slides vertically when set.
        // Z = -5000 puts it far behind the camera (the binary uses this to hide it
        // when m_FruitFactAlpha is low / ramping up). The earlier port comment
        // claiming -5000 was an unused sentinel was incorrect.
        // DAT_00142640 = -5000.0f, DAT_00142644 = 65.0f, DAT_00142648 = 300.0f.
        if (m_pSlotC0) {
            m_pSlotC0->pos.x = 0.0f;
            m_pSlotC0->pos.y = (1.0f - m_FruitFactAlpha) * 300.0f + 65.0f;
            m_pSlotC0->pos.z = -5000.0f;
        }
        // Binary @ 0x001424ec..0x00142520: retry+quit slide in from off-screen-right
        // via pos.x = 190 + (1 - m_FruitFactAlpha) * 120.0f. The +120 UnitX term IS
        // the slide formula -- earlier port comment conflating it with the unused
        // g_JitterVec3 was incorrect. The pos.y is fixed (-50 for retry, -125 for
        // quit). DAT_0014264c = 190.0f, DAT_00142654 = 120.0f, DAT_00142650 = -50.0f,
        // DAT_00142658 = -125.0f.
        const float slideX = 190.0f + (1.0f - m_FruitFactAlpha) * 120.0f;
        if (m_pSlot9c) {
            m_pSlot9c->pos.x = slideX;
            m_pSlot9c->pos.y = -50.0f;
            m_pSlot9c->pos.z = 0.0f;
        }
        if (m_pQuitBtn) {
            m_pQuitBtn->pos.x = slideX;
            m_pQuitBtn->pos.y = -125.0f;
            m_pQuitBtn->pos.z = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// PreDrawOrder (vtable slot 8, 0x0014171c)
// ---------------------------------------------------------------------------

// ASM-verified: 2026-05-10 binary @ 0x0014171c (re-analyst)
void GameOverScreen::PreDrawOrder(const Vec3& hudScale, int layerMask) {

    // -----------------------------------------------------------------
    // Layer 0x80 path -- highscore label + game-over overlay quad.
    // ASM-verified: 2026-05-11 binary @ 0x00141742..0x0014186a (asm-inspector).
    // The %d text is FruitSaveData::m_highscore (+0x40, all-time best),
    // gated on m_highscore > 0. Earlier port misnamed the field as
    // m_DaysRemaining -- there is no days-remaining concept at +0x40.
    // -----------------------------------------------------------------
    if ((layerMask & Mortar::HUD_LAYER_POST_ACTOR) != 0) {
        Game* game = Game::GetInstance();
        if (m_pRetryBtn && game && game_work.m_SaveData &&
            game_work.m_SaveData->m_highscore > 0)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", game_work.m_SaveData->m_highscore);

            const Vec3 daysPos(-163.0f, -96.0f, 0.0f);  // DAT_001419e0/4/8
            // ASM-verified: 2026-05-11 binary @ 0x00141790..0x001417d0 (asm-inspector)
            //   ldr r1,[r4,#0x98]    ; r1 = m_pRetryBtn
            //   vldr s14,[r1,#0x20]  ; s14 = m_pRetryBtn->size.x
            //   vmul s0,s14,0.5      ; s0 = size.x * 0.5
            //   font = pFontNumbers (Game+0x58), align 0xF, white.
            const float scaleArg = m_pRetryBtn->size.x * 0.5f;
            if (game_work.pFontNumbers.IsValid()) {
                game_work.pFontNumbers->DrawString(scaleArg, 1.0f, 0.0f,
                    buf, daysPos,
                    Colour(255, 255, 255, 255),
                    0xF);
            }

            // Overlay quad (comming_soon_highscore.tex via
            // m_CommingSoonHighscoreTex) -- only when texture is valid AND
            // the retry button's size.x is in (0, 600]. DAT_001419ec = 600.0f.
            // Asset isn't shipped so IsValid() is always false in practice;
            // the gate keeps the call-shape parity with the binary.
            const float btnScaleX = m_pRetryBtn->size.x;
            if (m_CommingSoonHighscoreTex.IsValid() &&
                btnScaleX > 0.0f && btnScaleX < 600.0f)
            {
                m_CommingSoonHighscoreTex->Set();

                MatrixManager& mm = MatrixManager::GetInstance();
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(
                    m_pRetryBtn->size.x,
                    m_pRetryBtn->size.y,
                    m_pRetryBtn->size.z);
                mat.GlobalTranslate44(daysPos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();

                Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);

                m_CommingSoonHighscoreTex->UnSet();
            }
        }
    }

    // -----------------------------------------------------------------
    // Layer 1 path -- expression + bg-pattern overlays
    // -----------------------------------------------------------------
    if ((layerMask & Mortar::HUD_LAYER_DEFAULT) != 0) {
        Game* game = Game::GetInstance();
        // Gate (binary @ 0x00141810):
        //   m_bIsClassic && (gameMode != Arcade && gameMode != Zen
        //       || (m_pBonusScreen != null && bonus->field_0xe4 == 0))
        bool gate = false;
        if (m_bIsClassic && game) {
            const uint8_t gm = game_work.gameMode;
            const bool notArcadeZen =
                gm != Mortar::GAME_MODE_ARCADE &&
                gm != Mortar::GAME_MODE_ZEN;
            // m_pBonusScreen->field_0xe4 == 0 sub-clause:
            // BonusScreen has a "ready"/"settled" byte at +0xe4 we may
            // not have ported yet. Treat as 0 if BonusScreen present.
            const bool bonusReady = (m_pBonusScreen != nullptr);
            gate = notArcadeZen || bonusReady;
        }

        if (gate) {
            const Colour white(255, 255, 255, 255);
            MatrixManager& mm = MatrixManager::GetInstance();

            // ASM-verified: 2026-05-11 binary @ 0x001418cc-0x001419a0 (re-analyst)
            //   body scale = 257.0 (DAT_001419f0) -- 256x256 tex + 1 bleed
            //   head scale = 129.0 (DAT_001419f4) -- 128x128 tex + 1 bleed
            //   body draws first centered on m_OffsetPos.
            //   head draws second, translated by (9, 40, 0) * hudScale + m_OffsetPos.
            //   unit quad (-0.5..0.5); texture m_Width/m_Height NOT multiplied --
            //   the scale constants are pixel sizes.

            // Bg-pattern (sensei_body_0N.tex) -- draws FIRST as backdrop
            if (m_BgPatternIdx > 0 && m_BgPatternIdx <= 3) {
                Mortar::SmartPtr<Mortar::Texture>& tex =
                    g_BgPatternTexArr[m_BgPatternIdx - 1];
                if (tex.IsValid()) {
                    tex->Set();

                    mm.GetWorldStack().Reset();
                    Matrix44 mat = Matrix44::MakeScale(
                        257.0f * hudScale.x,
                        257.0f * hudScale.y,
                        0.0f);
                    mat.GlobalTranslate44(Vec3(
                        m_OffsetPosX, m_OffsetPosY, m_OffsetPosZ));
                    mm.GetWorldStack().SetCurrentMatrix(mat);
                    mm.UploadModelViewOnly();

                    Mortar::Mesh::DrawQuadUnCached(white, NULL);
                    tex->UnSet();
                }
            }

            // Expression (sensei_head_0N.tex) -- draws SECOND on top
            if (m_ExpressionIdx > 0 && m_ExpressionIdx <= 3) {
                Mortar::SmartPtr<Mortar::Texture>& tex =
                    g_ExpressionTexArr[m_ExpressionIdx - 1];
                if (tex.IsValid()) {
                    tex->Set();

                    mm.GetWorldStack().Reset();
                    Matrix44 mat = Matrix44::MakeScale(
                        129.0f * hudScale.x,
                        129.0f * hudScale.y,
                        0.0f);
                    mat.GlobalTranslate44(Vec3(
                        9.0f * hudScale.x + m_OffsetPosX,
                        40.0f * hudScale.y + m_OffsetPosY,
                        m_OffsetPosZ));
                    mm.GetWorldStack().SetCurrentMatrix(mat);
                    mm.UploadModelViewOnly();

                    Mortar::Mesh::DrawQuadUnCached(white, NULL);
                    tex->UnSet();
                }
            }
        }

        // Layer 1 -- always call HUDControl3d base draw last.
        HUDControl3d::Draw(hudScale, layerMask);
    }
}

// ---------------------------------------------------------------------------
// DrawOrder (vtable slot 9, 0x00141448)
// ---------------------------------------------------------------------------

// ASM-verified: 2026-05-10 binary @ 0x00141448 (re-analyst)
void GameOverScreen::DrawOrder(const Vec3& hudScale, int /*layerMask*/) {
    if (m_State != STATE_QUICK_RESTART) return;
    if (!g_StarburstTex.IsValid()) return;

    // -----------------------------------------------------------------
    // Lazy-init: 8 wedges x 6 verts = 48 vertices forming the halo.
    // Per-wedge angle step = 0x1FFE (8190 / 65536 = 1/8 turn).
    // Outer ring radius = 0.5; inner ring (offset 90 deg) radius = 0.075.
    // -----------------------------------------------------------------
    if (!g_StarMesh.initialised) {
        for (int wedge = 0; wedge < 8; ++wedge) {
            const uint16_t baseAng = (uint16_t)(wedge * 0x1FFE);
            const float s0 = SinIdx(baseAng) * 0.5f;
            const float c0 = CosIdx(baseAng) * 0.5f;
            const float s1 = SinIdx((uint16_t)(baseAng + 0x3FFC)) * 0.075f;
            const float c1 = CosIdx((uint16_t)(baseAng + 0x3FFC)) * 0.075f;

            QUADCUSTOMVERTEX* v = &g_StarMesh.verts[wedge * 6];

            v[0].x = s0 - s1; v[0].y = c0 - c1; v[0].z = 0; v[0].u = 0;      v[0].v = 0;
            v[1].x = s0 + s1; v[1].y = c0 + c1; v[1].z = 0; v[1].u = 1.0f;   v[1].v = 0;
            v[2].x = s0 * 0.6f - s1; v[2].y = c0 * 0.6f - c1; v[2].z = 0;
                v[2].u = 0; v[2].v = 1.0f;
            v[3].x = s0 + s1; v[3].y = c0 + c1; v[3].z = 0;
                v[3].u = 1.0f; v[3].v = 0;
            v[4].x = s0 * 0.6f - s1; v[4].y = c0 * 0.6f - c1; v[4].z = 0;
                v[4].u = 0; v[4].v = 1.0f;
            v[5].x = s0 * 0.6f + s1; v[5].y = c0 * 0.6f + c1; v[5].z = 0;
                v[5].u = 1.0f; v[5].v = 1.0f;

            for (int i = 0; i < 6; ++i) {
                v[i].nx = 0; v[i].ny = 0; v[i].nz = 1.0f;
            }
        }
        g_StarMesh.initialised = true;
    }

    // -----------------------------------------------------------------
    // Per-frame: pulse each wedge brightness based on m_Timer.
    // alpha = ((timer & 7) - wedgeIdx) wraps; clamped to [64, 255];
    // BGRA = (a, a, a, 200).
    // -----------------------------------------------------------------
    const int timerInt = (int)m_Timer;
    const int phase = timerInt & 7;
    for (int wedge = 0; wedge < 8; ++wedge) {
        // Binary computes (~((idx-1)*0x20000000) >> 0x1d), simplified:
        //   alphaIdx = (phase - wedge) & 7
        // then alpha = alphaIdx * 32, clamped [64..255].
        int alphaIdx = (phase - wedge) & 7;
        int alpha = alphaIdx * 32;
        if (alpha > 0xFE) alpha = 0xFF;
        if (alpha < 0x40) alpha = 0x40;

        const Colour wedgeCol((uint8_t)alpha, (uint8_t)alpha,
                              (uint8_t)alpha, 200);
        const uint32_t packed = wedgeCol.PlatformColour();
        QUADCUSTOMVERTEX* v = &g_StarMesh.verts[wedge * 6];
        for (int i = 0; i < 6; ++i) v[i].colour = packed;
    }

    // -----------------------------------------------------------------
    // Draw: scale 64 * hudScale, translate (-280, -96, 0).
    // Binary @ 0x001416bc DrawTriList(verts, 0x30, false, nullptr).
    // -----------------------------------------------------------------
    g_StarburstTex->Set();

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(
        64.0f * hudScale.x,
        64.0f * hudScale.y,
        64.0f * hudScale.z);
    mat.GlobalTranslate44(Vec3(-280.0f, -96.0f, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    // Binary @ 0x001416bc DrawTriList(verts, 0x30, false, nullptr)
    Mortar::Mesh::DrawTriList(g_StarMesh.verts, 48, false, NULL);

    g_StarburstTex->UnSet();
}
