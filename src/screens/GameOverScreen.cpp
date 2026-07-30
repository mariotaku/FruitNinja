// GameOverScreen -- binary ctor 0x001882a0, Initialise 0x00187c90, Update 0x00186c80
// PreDrawOrder 0x00186894, DrawOrder 0x00186484, Release 0x00185970, dtor 0x00185d40.
// vtable @ 0x002cd5c0. Size 0x160 (operator new @ 0x001cb788).

#include "GameOverScreen.h"
#include "BonusScreen.h"
#include "FruitFactZenPage.h"
#include "FruitFactBonusFactPage.h"
#include "FruitFactClassicFactPage.h"
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
#include "hud/FruitFactCombo.h"
#include "engine/audio/GameSound.h"
#include "engine/audio/MortarSound.h"
#include "asset/TextureManager.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include "math/_Vector3.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/Font.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <vector>
#include "game/GameWork.h"
#include "game/GameOver.h"
#include "engine/network/NetworkManager.h"
#include "game/Leaderboard.h"
#include "screens/MainScreen.h"
#include "engine/util/StringHash.h"
#include "engine/util/StringTable.h"
#include "screens/FruitFactPage.h"
#include "render/Layout.h"

#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#include "resource/BlockLoader.h"
#endif

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// File-scope texture SmartPtr arrays (binary static class members)
// Binary @ LoadContent 0x00130bb0. Order matches binary literal pool @ 0x00130d40.
// ---------------------------------------------------------------------------
static Mortar::SmartPtr<Mortar::Texture> g_ArcadeTimeUpTitleTex;
static Mortar::SmartPtr<Mortar::Texture> g_GameOverTitleTex;
static Mortar::SmartPtr<Mortar::Texture> g_TimeUpTitleTex;
static Mortar::SmartPtr<Mortar::Texture> g_RetryTex;
static Mortar::SmartPtr<Mortar::Texture> g_QuitTex;
static Mortar::SmartPtr<Mortar::Texture> g_LeaderboardsTexPair[2];
static Mortar::SmartPtr<Mortar::Texture> g_ExpressionTexArr[3];
static Mortar::SmartPtr<Mortar::Texture> g_BgPatternTexArr[3];

// 3 dead-texture statics (never assigned in binary, nulled in UnLoadContent)
// Defunct: dead-texture statics -- no-op stub; v1.6.1 GameOverScreen @ 0x00185e68
static Mortar::SmartPtr<Mortar::Texture> s_DeadTex_7af8;
static Mortar::SmartPtr<Mortar::Texture> s_DeadTex_75f4;
static Mortar::SmartPtr<Mortar::Texture> s_DeadTex_7a88;

static bool g_LoadContentGuard = false;

#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
// Task #36 Stage 4 -- port-specific (no binary counterpart). Set true only
// by DoQuitToMenu() (the STATE_QUIT_WAIT-only path), consumed once in
// ~GameOverScreen(). Distinguishes "player quit to menu" (INGAME assets no
// longer needed) from "player retried" (STATE_RETRY_FADE -> SetTerminate()
// also destroys this GameOverScreen, but a NEW game starts and still needs
// them) -- both paths call SetTerminate(), so m_State alone can't tell them
// apart once the destructor runs.
static bool g_bQuitToMenuPending = false;
#endif

// File-static flag used by Update prologue per binary
// Binary @ Update 0x00186c80: s_bounceValue static inside this TU
static float s_bounceValue = -1.0f;

// ---------------------------------------------------------------------------
// Starburst halo mesh (48 verts; lazy-init on first DrawOrder state 0xe)
// ---------------------------------------------------------------------------
struct StarburstMesh {
    bool initialised;
    QUADCUSTOMVERTEX verts[48];
};
static StarburstMesh g_StarMesh = { false, {} };

// ---------------------------------------------------------------------------
// TTF font accessor for BakedStringBox title string.
// v1.6.1 GameOverScreen::Initialise @0x00187c90: reads game_work.m_pTTFFontMain
//   (GameWork+0x614, the locale face PreloadFontsTTF @0x0011c1fc sets to
//   arabic.ttf when languageFlag==0x14, else gangofchinese.ttf). Falls back to
//   a lazily-created gangofchinese.ttf only if PreloadFontsTTF hasn't run yet.
static Mortar::FontCacheObjectTTF* GetGameOverTTFFont() {
    if (game_work.m_pTTFFontMain) return game_work.m_pTTFFontMain;
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) return 0;
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// ASM-spec v1.6.1 GetCurrentScore @0x0011a0cc: playerIdx is ignored by the binary.
// The whole body is `return game_work.currentScore;` -- six instructions, no guard.
static int GetCurrentScore(int /*playerIdx*/) {
    return game_work.currentScore;
}

// ASM-spec v1.6.1 GetCurrentModeHighscore @0x00119ee4:
//   ldrb mode,[game_work+4]; if (mode > 3) return 0;         <- range gate, NOT (mode & 3)
//   sd = game_work.pM_SaveData; if (sd == 0) return 0;       <- GENUINE null guard, keep it
//   return sd->m_ModeHighScores[mode];                        (sd + 0x44 + mode*4)
static int GetCurrentModeHighscore() {
    unsigned mode = (unsigned)game_work.gameMode;
    if (mode > 3) return 0;
    if (!game_work.m_SaveData) return 0;
    return game_work.m_SaveData->m_ModeHighScores[mode];
}

// ASM-spec v1.6.1 SetCurrentModeHighscore @0x00119f24:
//   ldrb mode,[game_work+4]; if (mode > 3) return false;
//   sd = game_work.pM_SaveData; if (sd == 0) return false;   <- GENUINE null guard, keep it
//   then compares/stores m_ModeHighScores[mode] inline.
bool SetCurrentModeHighscore(int score) {
    unsigned idx = (unsigned)game_work.gameMode;
    if (idx > 3) return false;
    if (!game_work.m_SaveData) return false;
    return game_work.m_SaveData->SetCurrentModeHighscore(score);
}

// Binary @ 0x00140604
static void DoSetTerminate(GameOverScreen* self) {
    self->m_bPendingRemoval = 1;
}

// v1.6.1 Model A: quit-to-menu pauses in place (bM_bPaused=1), stays in task state 2 --
// the binary (QuitToMenu @0x001cb6e4) never hops task state or tears down HUD/WaveManager;
// GameExit @0x001cfed4 runs only on real app exit. #179
// STATE_QUIT_WAIT gates on GetNumEntities(0)==0, so in-game fruit are already flung by
// ResetGameEntities (via Bomb::HitMenuBomb -> UpdateBombHit @0x001cbbac 1.5s threshold)
// before this fires. No taskStateIndex hop or WaveManager::Destroy needed here.
static void DoQuitToMenu() {
    // No Game guard: v1.6.1 QuitToMenu @0x001cb6e4 calls WaveManager::GetInstance ->
    // ResetGlobalDt(1.0f), then writes game_work through a plain GOT load
    // (ldr r3,[r4,r3] @0x001cb710; strb #1,[r3,#0x5]).
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);

#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 4 -- latch "this is a real quit", consumed by
    // ~GameOverScreen() to gate the INGAME FreeBlock (see g_bQuitToMenuPending
    // comment above). Only DoQuitToMenu (STATE_QUIT_WAIT) sets this; the
    // retry path (STATE_RETRY_FADE) never calls DoQuitToMenu.
    g_bQuitToMenuPending = true;
#endif

    game_work.bM_bPaused = 1;
    // bM_Mode is NOT cleared here. The binary QuitToMenu @0x001cb6e4 never writes bM_Mode.
    // The camera-settle auto-clear in GameUpdate @0x001cfaec handles it once
    // m_PauseAmount settles and PauseScreen leaves state ACTIVE.

    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_ZOOM);    // v1.6.1 QuitToMenu @0x001cb6e4: m_State (+0x118) = 0
        game_work.mMainScreen->SetIntroHoldTimer(0.5f);        // v1.6.1 QuitToMenu @0x001cb6e4: vstr s15,[r1,#0x11c]
    }

    if (game_work.m_pActiveHUDControl) {
        game_work.m_pActiveHUDControl->m_bPendingRemoval = 1;
    }

    SetScore(0, -1);

    // Defunct: P2P / online disconnect -- no-op stub; v1.6.1 binary @ 0x00169e9e
    Mortar::NetworkManager::GetInstance()->SpawnThreadController();

    game_work.m_QuitTransitionTimer = 0.0f;
    // v1.6.1 QuitToMenu @0x001cb764: m_bMPRetryPending (+0x174) then the four P2P
    // session bytes +0x1A2..+0x1A5 (not the dead +0x19A/+0x19B pair).
    game_work.m_bMPRetryPending = 0;
    game_work.m_reserved1a2 = 0;
    game_work.m_reserved1a3 = 0;
    game_work.m_reserved1a4 = 0;
    game_work.m_reserved1a5 = 0;
}

// ---------------------------------------------------------------------------
// Static content load/unload
// ---------------------------------------------------------------------------

// Binary @ 0x00130bb0
void GameOverScreen::LoadContent() {
    if (g_LoadContentGuard) return;
    char acStack_bc[128];
    g_ArcadeTimeUpTitleTex   = TextureManager::LoadLocalisedTexture("arcade_time_up.tex");
    g_GameOverTitleTex       = TextureManager::LoadLocalisedTexture("gameover.tex");
    g_TimeUpTitleTex         = TextureManager::LoadLocalisedTexture("time_up.tex");
    g_RetryTex               = TextureManager::LoadLocalisedTexture("retry.tex");
    g_QuitTex                = TextureManager::LoadLocalisedTexture("quit.tex");
    g_LeaderboardsTexPair[0] = TextureManager::LoadLocalisedTexture("leaderboards.tex");
    g_LeaderboardsTexPair[1] = TextureManager::LoadLocalisedTexture("gc_leaderboards.tex");
    {
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

#if defined(_MSC_VER)
#  define FN_NOINLINE __declspec(noinline)
#else
#  define FN_NOINLINE __attribute__((noinline))
#endif
static FN_NOINLINE void NullTex(Mortar::SmartPtr<Mortar::Texture>* p) {
    p->SetNull();
}
#undef FN_NOINLINE

// ASM-spec v1.6.1 GameOverScreen::UnLoadContent @0x00185e68
void GameOverScreen::UnLoadContent() {
    g_LoadContentGuard = false;
    NullTex(&g_GameOverTitleTex);
    NullTex(&g_TimeUpTitleTex);
    NullTex(&g_RetryTex);
    NullTex(&g_LeaderboardsTexPair[0]);
    NullTex(&g_LeaderboardsTexPair[1]);
    NullTex(&g_QuitTex);
    NullTex(&s_DeadTex_7af8);
    NullTex(&g_ArcadeTimeUpTitleTex);
    NullTex(&s_DeadTex_75f4);
    NullTex(&s_DeadTex_7a88);
    for (int i = 0; i < 3; ++i) {
        NullTex(&g_ExpressionTexArr[i]);
        NullTex(&g_BgPatternTexArr[i]);
    }
}

// Binary: GameOverScreen::m_senseiHeads[idx] (static SmartPtr<Texture>[3])
// Port: g_ExpressionTexArr file-static, loaded by LoadContent.
// v1.6.1 FruitFactClassicFactPage ctor @0x00174e30 reads GameOverScreen::m_senseiHeads.
Mortar::SmartPtr<Mortar::Texture> GameOverScreen::GetSenseiHeadTex(int idx) {
    if (idx < 0 || idx >= 3) return Mortar::SmartPtr<Mortar::Texture>();
    return g_ExpressionTexArr[idx];
}

// Binary: GameOverScreen::m_senseiBody[idx] (static SmartPtr<Texture>[3])
// Port: g_BgPatternTexArr file-static, loaded by LoadContent.
// v1.6.1 FruitFactClassicFactPage ctor @0x00174e30 reads GameOverScreen::m_senseiBody.
Mortar::SmartPtr<Mortar::Texture> GameOverScreen::GetSenseiBodyTex(int idx) {
    if (idx < 0 || idx >= 3) return Mortar::SmartPtr<Mortar::Texture>();
    return g_BgPatternTexArr[idx];
}

// ---------------------------------------------------------------------------
// Reset — empty in binary (0x00140554, single bx lr)
// ---------------------------------------------------------------------------

void GameOverScreen::Reset() {}

// ---------------------------------------------------------------------------
// IsAllowedToExit — always 1 in binary (0x0014061c)
// ---------------------------------------------------------------------------

bool GameOverScreen::IsAllowedToExit() { return true; }

// ---------------------------------------------------------------------------
// Constructor (0x001882a0)
// ---------------------------------------------------------------------------

GameOverScreen::GameOverScreen(const char* modeName, int param2, float param3,
                               int expressionIdx, int bgPatternIdx,
                               int tabIndex, int starCount)
    : HUDControl3d(),
      m_pCtrl7C(0),
      m_pCtrl80(0),
      m_LinkedScreen(0),
      m_TitleSizeX(0.0f),
      m_State(0),
      m_Timer(0.0f),
      m_TitleSize(0.0f, 0.0f, 0.0f),
      m_reservedA0(0),
      m_pRetryBtn(0),
      m_pSlotA8(0),
      m_AnimCounter(0),
      m_pQuitBtn(0),
      m_pSlotB4(0),
      m_AnimTimeMs(0),
      m_OffsetPos(0.0f, 0.0f, 0.0f),
      m_pFruitFact(0),
      m_pZenPage(0),
      m_pBonusFactPage(0),
      m_pClassicFactPage(0),
      m_pChildCtrlD8(0),
      m_pChildCtrlDC(0),
      m_pNoticeCtrl(0),
      m_pBonusScreen(0),
      m_reservedE8(0),
      m_PostOk(0),
      m_PostInProgress(0),
      m_StarCount(0),
      m_pTitleString(0),
      m_SubObjectVptr(0),
      m_MostFruitCount(-1),
      m_bScoreSubmitted(0),
      m_reserved145(0),
      m_ExpressionIdx(expressionIdx),
      m_BgPatternIdx(bgPatternIdx),
      m_TabIndex(tabIndex),
      m_StarCountArg(starCount),
      m_bIsClassic(0),
      m_FruitFactAlpha(0.0f)
{
    memset(m_DaysLeftLabel, 0, sizeof(m_DaysLeftLabel));
    // Binary ctor: SmartPtr<Texture>::ctor(&m_TitleTex)
    new (&m_TitleTex) Mortar::SmartPtr<Mortar::Texture>();
    Initialise(modeName, param2, param3, expressionIdx, bgPatternIdx, tabIndex, starCount);
}

GameOverScreen::~GameOverScreen() {
    Release();
#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 4 -- memory reclaim, port-specific (no binary
    // counterpart). Only free INGAME assets when this teardown was reached
    // via DoQuitToMenu (real quit-to-menu) -- retry destroys and recreates a
    // GameOverScreen too (STATE_RETRY_FADE -> SetTerminate()) but starts a
    // NEW game that still needs the same INGAME manifest, so that path must
    // NOT free it. Release() above has already torn down every GameOverScreen-
    // owned control (fact pages, sensei sub-controls, retry/quit buttons), so
    // by this point nothing in the HUD tree still references INGAME's held
    // textures/mesh except MissControl/SuperFruitControl/Fruit's own members,
    // which are themselves reloaded (cache hit) on next level start.
    if (g_bQuitToMenuPending) {
        g_bQuitToMenuPending = false;
        fn::wii::BlockLoader::FreeBlock(fn::wii::RES_BLOCK_INGAME);
    }
#endif
}

// ---------------------------------------------------------------------------
// Initialise (0x00187c90)
// ---------------------------------------------------------------------------

void GameOverScreen::Initialise(const char* modeName, int param2, float param3,
                                int expressionIdx, int bgPatternIdx,
                                int tabIndex, int starCount)
{
    // 1. One-shot content load
    if (!g_LoadContentGuard) LoadContent();

    // 2. Init sub-object and misc fields
    m_SubObjectVptr  = 0;
    m_reservedE8       = 0;
    m_MostFruitCount = -1;
    m_TitleSizeX     = 0.0f;   // +0x88
    m_TabIndex       = tabIndex;
    m_StarCountArg   = starCount;

    Game* game = Game::GetInstance();
    uint8_t gameMode = game ? game_work.gameMode : 0;

    // 3. Title tex select by gameMode
    // Binary: Arcade(2)→GOT+0x7390, Zen(3)→GOT+0x74ec, else Classic→GOT+0x761c
    {
        Mortar::SmartPtr<Mortar::Texture> bgTex;
        if (gameMode == GAME_MODE_ARCADE)
            bgTex = g_ArcadeTimeUpTitleTex;
        else if (gameMode == GAME_MODE_ZEN)
            bgTex = g_TimeUpTitleTex;
        else
            bgTex = g_GameOverTitleTex;
        m_TitleTex = bgTex;
        // ASM-spec v1.6.1 GameOverScreen::Initialise @0x00187c90: the title texture is assigned to
        // m_TitleTex ONLY, never to base m_Texture. The big centered title is the localized TTF
        // (m_pTitleString: SetText id 0x2db Classic / 0x2f9 Arcade&Zen, with a (-6,-6) drop-shadow);
        // m_TitleTex's only live draw is the small "new highscore" stamp in PreDrawOrder pass 0x80.
        // Setting base m_Texture here made HUDControl3d::Draw render the baked title OVER the TTF
        // (doubled, English-baked) -- so it is intentionally NOT set. (TTF shadow pending #257.)
        if (bgTex.IsValid()) {
            m_TitleSizeX  = (float)bgTex->GetWidth();
            m_TitleSize.x = (float)bgTex->GetWidth();
            m_TitleSize.y = (float)bgTex->GetHeight();
        } else {
            m_TitleSizeX  = 256.0f;
            m_TitleSize.x = 256.0f;
            m_TitleSize.y = 64.0f;
        }
        m_TitleSize.z = 0.0f;
    }

    // 5. Create BakedStringBox title string
    // Binary: BakedStringBox(font, 56.0f, 450.0f/*0x1c2*/, 40.0f/*0x28*/, align, maxLines, lineSpacing)
    // ASM-spec v1.6.1 GameOverScreen::Initialise @0x00187c90
    {
        Mortar::FontCacheObjectTTF* font = GetGameOverTTFFont();
        if (font) {
            m_pTitleString = new Mortar::BakedStringBox(font, 56.0f, 450, 40, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);

            // 6. SetShadow
            // ASM-spec v1.6.1 GameOverScreen::Initialise @0x00187c90
            m_pTitleString->SetShadow(2.0f, Colour(0,0,0,255), _Vector3<float>(-6.0f,-6.0f,0.0f), false);

            // 7. SetGradient by mode
            if (gameMode == GAME_MODE_ARCADE) {
                m_pTitleString->SetGradient(
                    Colour(0xf3, 0xfb, 0, 255), Colour(7, 0x33, 0, 255), false);
            } else if (gameMode == GAME_MODE_ZEN) {
                m_pTitleString->SetGradient(
                    Colour(0xd1, 0x4e, 0x17, 255), Colour(0x78, 0x1a, 0xd, 255), false);
            } else {
                // Classic
                m_pTitleString->SetGradient(
                    Colour(0xd1, 0x47, 0x16, 255), Colour(0x78, 0x1a, 0xd, 255), false);
            }

            // 8. SetText by mode: Classic→0x2db, Arcade&Zen→0x2f9
            if (gameMode == GAME_MODE_CLASSIC) {
                m_pTitleString->SetText(
                    GETSTRING((LocalizedString)0x2db, 0));
            } else {
                m_pTitleString->SetText(
                    GETSTRING((LocalizedString)0x2f9, 0));
            }
        }
    }

    // 9. m_TitleSize FINAL = (256, 64, 0)
    m_TitleSize = _Vector3<float>(256.0f, 64.0f, 0.0f);

    // 10.
    m_Timer           = 0.0f;
    m_State           = STATE_ENTRY_ANIM;
    m_LayerFlags      = 0;   // field_0x32 = 0

    // 11.
    m_ExpressionIdx   = expressionIdx;
    m_AnimTimeMs      = 0;
    m_bScoreSubmitted = 0;
    m_BgPatternIdx    = bgPatternIdx;
    m_reservedA0        = 0;
    m_pBonusScreen    = 0;
    m_AnimCounter     = 0;
    m_FruitFactAlpha  = game ? game_work.m_PauseAmount : 0.0f;
    m_pSlotA8         = 0;
    m_pQuitBtn        = 0;   // +0xB0
    m_pSlotB4         = 0;
    m_pRetryBtn       = 0;   // +0xA4
    m_bIsClassic      = (gameMode == GAME_MODE_CLASSIC) ? 1 : 0;

    // 12. Expression randomise
    if (expressionIdx < 1) {
        m_ExpressionIdx = 1;
        FindMostOfFruit();
        int score = GetCurrentScore(0);
        int hi    = GetCurrentModeHighscore();
        if (score > hi / 2) {
            // ASM-spec v1.6.1 GameOverScreen::Initialise @0x00187c90 (outlined
            // helper T.1285 @0x00185024): Math::g_random.Rand32(2) x1, + 2
            m_ExpressionIdx = (int)Math::g_Random.Rand32(2) + 2;
        }
    }

    // 13. BgPattern randomise
    if (bgPatternIdx < 1) {
        // ASM-spec v1.6.1 GameOverScreen::Initialise @0x00187c90 (outlined
        // helper T.1285 @0x00185024): Math::g_random.Rand32(3) x1, + 1
        m_BgPatternIdx = (int)Math::g_Random.Rand32(3) + 1;
    }

    // 14.
    pos.x = 0.0f; pos.y = 0.0f; pos.z = 0.0f;
    m_OffsetPos = _Vector3<float>(368.0f, 55.0f, 0.0f);
    m_StarCount = 0;

    // 15. Null page pointers
    m_pFruitFact       = 0;
    m_pZenPage         = 0;
    m_pCtrl7C          = 0;
    m_pCtrl80          = 0;
    m_pBonusFactPage   = 0;
    m_pClassicFactPage = 0;
    m_pChildCtrlD8         = 0;
    m_pChildCtrlDC         = 0;
    m_LinkedScreen     = 0;
    m_pNoticeCtrl      = 0;
    m_PostOk           = 0;
    m_PostInProgress   = 0;

    // 16. Coin label
    // ASM-spec v1.6.1 GameOverScreen::Initialise @0x00187c90: OS_SPrintf(m_DaysLeftLabel,
    //   0x40, "YOU JUST EARNT %i COINS", m_CoinsBalance - m_CoinsAtGameStart). The format
    //   string is a hardcoded English literal at 0x00282A04 -- the binary does NOT localize
    //   this label via StringTable (#284 premise was wrong), so the port stays English too.
    {
        const int coinsEarned = game
            ? (game_work.m_CoinsBalance - game_work.m_CoinsAtGameStart) : 0;
        snprintf(m_DaysLeftLabel, sizeof(m_DaysLeftLabel),
                 "YOU JUST EARNT %i COINS", coinsEarned);
    }
    m_reserved145 = 1;

    // 17. Arcade off-screen seed
    if (gameMode == GAME_MODE_ARCADE) {
        pos = _Vector3<float>(0.0f, -320.0f, 0.0f);
        if (param2 == 1 && param3 <= 0.0f) {
            m_State = STATE_BONUS_PHASE;
            m_Timer = param3 = -0.333f;
            pos = _Vector3<float>(0.0f, -320.0f, 0.0f);
        }
    }

    // 18. Fast path
    // Binary constants: gate 0.999, set 0.9998
    // ASM-spec v1.6.1 GameOverScreen::Initialise @0x00187c90
    if (param2 >= 0 && param3 >= 0.0f) {
        FindMostOfFruit();
        if (param2 > 5 && game && game_work.m_PauseAmount > 0.999f) {
            game_work.m_PauseAmount = 0.9998f;
            m_State              = STATE_MAIN_DISPLAY;
            m_bScoreSubmitted    = 1;
            m_FruitFactAlpha     = 1.0f;
            Update(0.0f);
        }
        m_Timer = param3;
        m_State = param2;
    }
}

// ---------------------------------------------------------------------------
// Init (vtable slot 2, 0x00140548)
// ---------------------------------------------------------------------------

void GameOverScreen::Init() {
    Reset();
}

// ---------------------------------------------------------------------------
// BeginDraw (vtable slot 5, 0x00140590)
// ---------------------------------------------------------------------------

void GameOverScreen::BeginDraw(float /*dt*/) {
    m_LayerFlags = (m_State != STATE_ENTRY_ANIM)
        ? (int)(Mortar::HUD_LAYER_POST_ACTOR | Mortar::HUD_LAYER_DEFAULT)
        : (int)Mortar::HUD_LAYER_DEFAULT;
}

// ---------------------------------------------------------------------------
// Release (vtable slot 3, 0x00185970)
// ---------------------------------------------------------------------------

void GameOverScreen::Release() {
    // Defunct: CancelHUDProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::CancelHUDProgressionTimer @ 0x00184d4c
    CancelHUDProgressionTimer();

    // No Game guard: v1.6.1 GameOverScreen::Release @0x00185970 loads game_work from
    // the GOT and compares +0x168 against `this` directly (cmp r3,r4 @0x00185994).
    if (game_work.pGameOverScreen == this) {
        // ASM-spec v1.6.1 GameOverScreen::Release @0x00185970
        FruitSaveData* sd = game_work.m_SaveData;
        if (sd) {
            sd->m_GameOverField4 = -1;
            sd->m_GameOverField3 = -1;
            sd->m_GameOverField1 = -1;
            sd->m_GameOverField2 = -1;
            sd->newBestThisGame  = 0;
        }
        game_work.pGameOverScreen = 0;
    }

    // NOTE: do NOT touch m_pRetryBtn / m_pQuitBtn here. They are HUD-owned and are
    // reaped (~MenuButton) by HUD::Update BEFORE this Release runs, so both are
    // dangling raw pointers by now. The binary GameOverScreen::Release @0x00185970
    // never reads or writes them (verified: its RemoveControl/delete slots are
    // m_pFruitFact/m_pZenPage/.../m_pNoticeCtrl/m_pSlotA8/m_pSlotB4, then
    // BonusManager::ClearBestBonuses -- retry/quit at +0xA4/+0xB0 never appear).
    // A prior port added `m_pRetryBtn->m_RemoveCallback = nullptr` here; that
    // Delegate::operator= read m_bEmpty inside the freed button (+0x58) = the
    // ASan-confirmed wasm heap-use-after-free (#367). Removed.

    // Full 12-slot RemoveControl pass (exact order per spec)
    // ASM-spec v1.6.1 GameOverScreen::Release @0x00185970
    if (game_work.mHud) {
        HUD* hud = game_work.mHud;
        if (m_pFruitFact)       hud->RemoveControl(m_pFruitFact);
        if (m_pZenPage)         hud->RemoveControl(m_pZenPage);
        if (m_pCtrl7C)          hud->RemoveControl((HUDControl*)m_pCtrl7C);
        if (m_pCtrl80)          hud->RemoveControl((HUDControl*)m_pCtrl80);
        if (m_pBonusFactPage)   hud->RemoveControl(m_pBonusFactPage);
        if (m_pClassicFactPage) hud->RemoveControl(m_pClassicFactPage);
        if (m_pChildCtrlD8)         hud->RemoveControl((HUDControl*)m_pChildCtrlD8);
        if (m_pChildCtrlDC)         hud->RemoveControl((HUDControl*)m_pChildCtrlDC);
        if (m_LinkedScreen)     hud->RemoveControl((HUDControl*)m_LinkedScreen);
        if (m_pNoticeCtrl)      hud->RemoveControl(m_pNoticeCtrl);
        if (m_pSlotA8)          hud->RemoveControl(m_pSlotA8);
        if (m_pSlotB4)          hud->RemoveControl(m_pSlotB4);
    }

    // Full 12-slot delete pass (vt[1] deleting-dtor order per spec)
    // ASM-spec v1.6.1 GameOverScreen::Release @0x00185970
    {
        if (m_pSlotA8) {
            m_pSlotA8->m_bNoDestructor = 0;
            delete m_pSlotA8;
            m_pSlotA8 = 0;
        }
        if (m_pFruitFact) {
            m_pFruitFact->m_bNoDestructor = 0;
            delete m_pFruitFact;
            m_pFruitFact = 0;
        }
        if (m_pZenPage) {
            m_pZenPage->m_bNoDestructor = 0;
            delete m_pZenPage;
            m_pZenPage = 0;
        }
        if (m_pCtrl7C) {
            ((HUDControl*)m_pCtrl7C)->m_bNoDestructor = 0;
            delete (HUDControl*)m_pCtrl7C;
            m_pCtrl7C = 0;
        }
        if (m_pCtrl80) {
            ((HUDControl*)m_pCtrl80)->m_bNoDestructor = 0;
            delete (HUDControl*)m_pCtrl80;
            m_pCtrl80 = 0;
        }
        if (m_pBonusFactPage) {
            m_pBonusFactPage->m_bNoDestructor = 0;
            delete m_pBonusFactPage;
            m_pBonusFactPage = 0;
        }
        if (m_pClassicFactPage) {
            m_pClassicFactPage->m_bNoDestructor = 0;
            delete m_pClassicFactPage;
            m_pClassicFactPage = 0;
        }
        if (m_pChildCtrlD8) {
            ((HUDControl*)m_pChildCtrlD8)->m_bNoDestructor = 0;
            delete (HUDControl*)m_pChildCtrlD8;
            m_pChildCtrlD8 = 0;
        }
        if (m_pChildCtrlDC) {
            ((HUDControl*)m_pChildCtrlDC)->m_bNoDestructor = 0;
            delete (HUDControl*)m_pChildCtrlDC;
            m_pChildCtrlDC = 0;
        }
        if (m_LinkedScreen) {
            ((HUDControl*)m_LinkedScreen)->m_bNoDestructor = 0;
            delete (HUDControl*)m_LinkedScreen;
            m_LinkedScreen = 0;
        }
        if (m_pNoticeCtrl) {
            m_pNoticeCtrl->m_bNoDestructor = 0;
            delete m_pNoticeCtrl;
            m_pNoticeCtrl = 0;
        }
        if (m_pSlotB4) {
            m_pSlotB4->m_bNoDestructor = 0;
            delete m_pSlotB4;
            m_pSlotB4 = 0;
        }
    }

    BonusManager::GetInstance()->ClearBestBonuses();
}

// ---------------------------------------------------------------------------
// SetTerminate (0x00140604)
// ---------------------------------------------------------------------------

void GameOverScreen::SetTerminate() {
    m_bPendingRemoval = 1;
}

// ---------------------------------------------------------------------------
// SetStateWait -- v1.6.1 GameOverScreen::SetStateWait @0x00184e04
// ---------------------------------------------------------------------------

void GameOverScreen::SetStateWait() {
    int score = GetCurrentScore(0);
    int unrated = game_work.m_SaveData->AddToTotal(
        "unrated_games", StringHash("unrated_games"), 1, true, true);
    if (game_work.m_SaveData->m_bRated == 0 &&
        score > 50 && unrated > 5 &&
        GetCurrentModeHighscore() - 10 < score) {
        game_work.m_SaveData->m_bRated = 1;
        // Defunct: NetworkManager::SetLeaderboardScore -- no-op stub; v1.6.1 Mortar::NetworkManager::SetLeaderboardScore @0x002312b4
        Mortar::NetworkManager::GetInstance()->SetLeaderboardScore(
            (const char*)(intptr_t)GetCurrentModeLeaderboardID(-1),
            (long long)score, 0, 0);
        // DIFFERS: original shows the DialogManager rate-app dialog
        // (v1.6.1 GameOverScreen::SetStateWait @0x00184e04) and defers m_State
        // to its Reject/Accept callbacks; dropped by decision -- dead store
        // URL, meaningless in a homebrew port -- so we fall through to
        // STATE_MAIN_DISPLAY.
    }
    m_State = STATE_MAIN_DISPLAY;
}

// ---------------------------------------------------------------------------
// ProgressionTimer no-op stubs
// ---------------------------------------------------------------------------

// Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::StartProgressionTimer @ 0x00184d48
void GameOverScreen::StartProgressionTimer() {}
// Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::CancelHUDProgressionTimer @ 0x00184d4c
void GameOverScreen::CancelHUDProgressionTimer() {}
// Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::OnProgressionTimerUp @ 0x00184d5c
void GameOverScreen::OnProgressionTimerUp() {}
// Defunct: ProgressionTimer -- no-op stub; v1.6.1 GameOverScreen::HandleProgressionTimerExpiration @ 0x00184d60
void GameOverScreen::HandleProgressionTimerExpiration() {}

// ---------------------------------------------------------------------------
// Social share callbacks
// ---------------------------------------------------------------------------

// Defunct: Facebook share -- no-op stub; (v1.6.1: symbol absent -- defunct/inlined)
void GameOverScreen::FacebookCallback() {}
// Defunct: Twitter share -- no-op stub; (v1.6.1: symbol absent -- defunct/inlined)
void GameOverScreen::TwitterCallback() {}

// Binary @ 0x00184d2c
void GameOverScreen::PostCallback(int result) {
    m_PostInProgress = 0;
    m_PostOk = (result == 0) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// LeaderboardsCallback -- v1.6.1 GameOverScreen::LeaderboardsCallback @0x00184cd8.
// Leaderboards is a defunct online stub.
// ASM-spec: state gate (m_State +0x8c == 0 || == 6), then game_work from the GOT and
// m_PauseAmount (+0x0c) compared against the pool constant. No Game null guard.
// ---------------------------------------------------------------------------

void GameOverScreen::LeaderboardsCallback() {
    if (m_State == STATE_ENTRY_ANIM || m_State == STATE_MAIN_DISPLAY) {
        if (game_work.m_PauseAmount > 0.999f) {
            m_Timer = 0.0f;
            m_State = STATE_LEADERBOARD;
        }
    }
}

// ---------------------------------------------------------------------------
// DeletedControl (0x00140558)
// ---------------------------------------------------------------------------
//
// ASM-spec v1.6.1 GameOverScreen::DeletedControl @0x00184c40:
//   Handles EXACTLY 3 pointer slots:
//     if (ctrl == m_pBonusScreen)  { m_pBonusScreen = 0; m_State = 6; }
//     if (ctrl == m_pRetryBtn)     { m_pRetryBtn = 0; }        // +0xA4
//     if (ctrl == nM_reservedE8)   { nM_reservedE8 = 0; m_State = 6; }
//   m_pBonusScreen is at +0xE4, m_pRetryBtn at +0xA4, nM_reservedE8 at +0xE8.
//   NOTE: Ghidra mislabels +0xA4 as "m_pQuitBtn"; the raw ASM nulls +0xA4, which is
//   the RETRY button (CreateRetryButton stores + guards on it, CreateQuitButton reads
//   its m_RestScale). A prior port nulled m_pQuitBtn (+0xB0) here -- the WRONG field --
//   so the reaped retry button's pointer stayed dangling: CreateRetryButton's
//   `if (m_pRetryBtn) return` guard then skipped recreation and CreateQuitButton read
//   the freed retry button (#369 UAF on retry). The binary does NOT null +0xB0
//   (m_pQuitBtn), nor m_pClassicFactPage (+0xD4), m_pFruitFact (+0xC8),
//   m_pZenPage (+0xCC), m_pBonusFactPage (+0xD0), m_pNoticeCtrl (+0xE0), or any other.
//
//   Over-broad nulling of m_pClassicFactPage caused the sensei-board-lingers bug:
//   Release calls hud->RemoveControl(m_pClassicFactPage), which fires DeletedControl,
//   which (wrongly) zeroed m_pClassicFactPage, so the subsequent
//   `if (m_pClassicFactPage) delete m_pClassicFactPage` guard was always skipped.
//   ~FruitFactClassicFactPage (BaseScreen::Release) never ran, so m_bPendingRemoval
//   was never set on the 2 sensei GenericHUDControl children, and HUD::Update
//   never reaped them -- they lingered in the HUD after game-over teardown.

void GameOverScreen::DeletedControl(HUDControl* ctrl) {
    // +0xE4 = m_pBonusScreen
    if (ctrl == (HUDControl*)m_pBonusScreen) {
        m_pBonusScreen = 0;
        m_State = STATE_MAIN_DISPLAY;
    }
    // +0xA4 = m_pRetryBtn (binary DeletedControl @0x00184c40 nulls +0xA4; Ghidra mislabels
    // it m_pQuitBtn). Nulling it lets the next state-6 pass recreate a live retry button
    // before CreateQuitButton reads its m_RestScale -- fixes the #369 retry-path UAF.
    if (ctrl == (HUDControl*)m_pRetryBtn) {
        m_pRetryBtn = 0;
    }
    // +0xE8 = nM_reservedE8 (always 0 in this build; check is always-false but binary-faithful)
    if (ctrl == (HUDControl*)(intptr_t)m_reservedE8) {
        m_reservedE8 = 0;
        m_State = STATE_MAIN_DISPLAY;
    }
}

// ---------------------------------------------------------------------------
// FindMostOfFruit (v1.6.1 @0x00186ac8)
// ---------------------------------------------------------------------------

void GameOverScreen::FindMostOfFruit() {
    const int count = g_FruitInfoCount;
    const uint8_t gameMode = game_work.gameMode;

    // Candidate filter: i runs to MAX_FRUIT_TYPES - 1 (the last registered type
    // is never a candidate); arcade additionally requires a powers block, and
    // super-fruit entries (+0x330) are always skipped.
    std::vector<int> src;
    for (int i = 0; i < count - 1; ++i) {
        const FruitInfo* fi = FruitInfo_Get(i);
        if (gameMode == GAME_MODE_ARCADE && fi->m_pPowers == 0) continue;
        if (fi->m_bIsSuperFruit != 0) continue;
        src.push_back(i);
    }

    // The binary does NOT shuffle in place: it repeatedly pops a random element
    // out of the shrinking candidate vector into a second vector. That is
    // exactly src.size() draws (a Fisher-Yates shuffle would be one fewer) and
    // yields a different permutation, so both the RNG stream and the tie-break
    // order depend on reproducing this form.
    std::vector<int> order;
    while (!src.empty()) {
        uint32_t k = Math::g_Random.Rand32((uint32_t)src.size());
        std::vector<int>::iterator it = src.begin() + (int)k;
        order.push_back(*it);
        src.erase(it);
    }

    int bestCount = 0;
    int bestIdx   = -1;
    for (size_t k = 0; k < order.size(); ++k) {
        FruitSaveData* save = game_work.m_SaveData;
        if (!save) break;
        int i = order[k];
        const FruitInfo* fi = FruitInfo_Get(i);
        int c = save->GetTotal(fi->m_TotalStatHash);
        if (c > bestCount) {
            bestCount = c;
            bestIdx   = i;
        }
    }

    if (bestCount > 0) {
        // v1.6.1 FindMostOfFruit @0x00186ac8 stores bestIdx at this[1]+0x00 and
        // bestCount at this[1]+0x04. Only the count is wired here -- the bestIdx
        // slot collides with m_bScoreSubmitted and has no confirmed reader.
        m_MostFruitCount = bestCount;
    }
}

// ---------------------------------------------------------------------------
// CreateRetryButton (0x00185f98) -- writes +0xA4 = m_pRetryBtn
// ---------------------------------------------------------------------------

void GameOverScreen::CreateRetryButton() {
    // Binary guard: if(m_pRetryBtn != 0) return  (ldr r6,[r0,#0xa4]; cmp #0; bne exit)
    if (m_pRetryBtn != 0) return;

    // No Game / mHud guard: the binary reaches game_work through the GOT and calls
    // HUD::AddControl on game_work.mHud (+0x40) unguarded (@0x001861ec).

    // ASM-spec v1.6.1 GameOverScreen::CreateRetryButton @0x00185f98
    // DIFFERS: opt-in widescreen -- MapX proportionally spreads Retry away from
    // centre (same "near-centre pair" treatment as PauseScreen's Resume/Retry
    // buttons). Identity (-80.0f) when disabled/__bada__.
    _Vector3<float> btnPos(MapX(-80.0f, "gameover.retry"), -96.0f, 0.0f);
    _Vector3<float> globalCenter(0.0f, 0.0f, 0.0f);
    // ASM-spec v1.6.1 GameOverScreen::CreateRetryButton @0x00185f98: ring background is
    // game_work.m_RingTex[1] (blue_ring.tex -- plain ring, NO baked text). The label is the
    // TTF drawn by SetText; using retry.tex (which has "RETRY" baked into the art) doubled it.
    Mortar::SmartPtr<Mortar::Texture> tex = game_work.m_RingTex[1];

    m_pRetryBtn = new MenuButton();
    m_pRetryBtn->m_Texture    = tex;
    m_pRetryBtn->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    m_pRetryBtn->Init(
        btnPos,
        Mortar::Delegate0<void>::Make(this, &GameOverScreen::OnRetryClicked),
        0,
        globalCenter,
        Mortar::Delegate0<void>()
    );
    // v1.6.1 GameOverScreen::CreateRetryButton @0x00185f98:
    // SetText(GETSTRING(0x3b5), pM_Colours[4], pM_Colours[5], 42.0, 12.0, true, true)
    // pM_Colours[4]=(147,238,255) pM_Colours[5]=(45,144,245) -- cyan/blue gradient
    m_pRetryBtn->SetText(
        GETSTRING_CAST_0((LocalizedString)0x3b5),
        Colour(147, 238, 255, 255),
        Colour(45,  144, 245, 255),
        42.0f, 12.0f, true, true);

    game_work.mHud->AddControl(m_pRetryBtn, false);
    m_pRetryBtn->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);
}

// v1.6.1 GameOverScreen::RetryCallback @0x001857ec -- no Game null guard; game_work is
// a GOT load (ldr r5,[r4,r3] @0x00185840) and pM_SaveData is dereferenced unguarded.
void GameOverScreen::RetryCallback() {
    if (m_State != STATE_ENTRY_ANIM && m_State != STATE_MAIN_DISPLAY &&
        m_State != STATE_QUICK_RESTART && m_State != STATE_LEADERBOARD) return;
    if (game_work.m_PauseAmount <= 0.989945f) return;
    CancelHUDProgressionTimer();
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
    if (game_work.m_SaveData) game_work.m_SaveData->ClearCombo();
    m_State = STATE_RETRY_PREPARE;
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("Game-start", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
}

void GameOverScreen::OnRetryClicked() {
    RetryCallback();
}

// ---------------------------------------------------------------------------
// CreateQuitButton (0x00186220) -- writes +0xB0 = m_pQuitBtn
// ---------------------------------------------------------------------------

void GameOverScreen::CreateQuitButton() {
    // No entry guard and no Game / mHud null test in the binary: it builds the button
    // unconditionally and calls HUD::AddControl on game_work.mHud (+0x40) @0x00186440.

    // ASM-spec v1.6.1 GameOverScreen::CreateQuitButton @0x00186220
    // DIFFERS: opt-in widescreen -- MapX proportionally spreads Quit away from
    // centre (same "near-centre pair" treatment as PauseScreen's Resume/Retry
    // buttons). Identity (80.0f) when disabled/__bada__.
    _Vector3<float> btnPos(MapX(80.0f, "gameover.quit"), -96.0f, 0.0f);
    _Vector3<float> globalCenter(0.0f, 0.0f, 0.0f);
    // ASM-spec v1.6.1 GameOverScreen::CreateQuitButton @0x00186220: ring background is
    // game_work.m_RingTex[16] (red_ring.tex -- plain ring, NO baked text). The label is the
    // TTF drawn by SetText; using quit.tex (which has "QUIT" baked into the art) doubled it.
    Mortar::SmartPtr<Mortar::Texture> tex = game_work.m_RingTex[16];

    // fruitType = Fruit::MAX_FRUIT_TYPES (bomb sentinel)
    m_pQuitBtn = new MenuButton();
    m_pQuitBtn->m_Texture    = tex;
    m_pQuitBtn->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    m_pQuitBtn->Init(
        btnPos,
        Mortar::Delegate0<void>::Make(this, &GameOverScreen::OnQuitClicked),
        g_FruitInfoCount,
        globalCenter,
        Mortar::Delegate0<void>()
    );
    // v1.6.1 GameOverScreen::CreateQuitButton @0x00186220:
    // SetText(GETSTRING(0x35f), pM_Colours[0], pM_Colours[1], 42.0, 12.0, true, true)
    // pM_Colours[0]=(249,62,19) pM_Colours[1]=(195,15,0) -- orange/red gradient
    m_pQuitBtn->SetText(
        GETSTRING_CAST_0(LSTR_QUIT),
        Colour(249, 62,  19, 255),
        Colour(195, 15,  0,  255),
        42.0f, 12.0f, true, true);

    // Copy retry btn m_RestScale region if retry exists
    if (m_pRetryBtn) {
        m_pQuitBtn->m_RestScale = m_pRetryBtn->m_RestScale;
    }
    m_pQuitBtn->m_bRespondsToBackKey = 1;
    m_pQuitBtn->m_bBackdropActive = 1; // v1.6.1 GameOverScreen::CreateQuitButton @0x00186430

    game_work.mHud->AddControl(m_pQuitBtn, false);
    m_pQuitBtn->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);

    if (game_work.m_TutorialControl) {
        MenuButton* tutBtn = m_pRetryBtn ? m_pRetryBtn : m_pQuitBtn;
        game_work.m_TutorialControl->ResetTutePos(tutBtn);
    }
}

// v1.6.1 GameOverScreen::QuitCallback @0x00184d6c -- no Game null guard; the state gate
// (m_State +0x8c in {0,6,0xe,0xa}) is the first thing the binary tests.
void GameOverScreen::QuitCallback() {
    if (m_State != STATE_ENTRY_ANIM && m_State != STATE_MAIN_DISPLAY &&
        m_State != STATE_QUICK_RESTART && m_State != STATE_LEADERBOARD) return;
    CancelHUDProgressionTimer();
    m_State = STATE_QUIT_WAIT;
    if (game_work.m_SaveData) game_work.m_SaveData->ClearCombo();
    HitMenuBomb(_Vector3<float>(163.0f, -96.0f, 0.0f));
}

void GameOverScreen::OnQuitClicked() {
    QuitCallback();
}

// ---------------------------------------------------------------------------
// Combo name table (binary @ 0x001e84b8, GetComboName @ 0x00110c94)
// ---------------------------------------------------------------------------

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

// v1.6.1 GetComboName @0x00132094 (_Z12GetComboName10COMBO_TYPE)
const char* GetComboName(COMBO_TYPE starType) {
    return g_ComboNameTable[starType];
}

// ---------------------------------------------------------------------------
// Update (vtable slot 10, 0x00186c80)
// ASM-verified: 2026-07-14T21:45Z v1.6.1 GameOverScreen::Update @ 0x00186c80..0x00187c8f (asm-inspector)
// ---------------------------------------------------------------------------

void GameOverScreen::Update(float dt) {
    Game* game = Game::GetInstance();

#ifndef __bada__
    // Port specific: DIFFERS: v1.6.1 GameOverScreen::Update @0x00186c80 eases the
    // entry-reveal m_Timer per 60Hz sim tick; port eases it per rendered frame
    // (dt-scaled) so the reveal tracks display refresh. __bada__ keeps the
    // faithful 60Hz path. The easing itself has already been advanced by
    // UpdateRealtime() (called once per presented frame via HUD::UpdateRealtime);
    // this 60Hz Update only reads the current m_Timer value to fire the
    // (rate-independent, threshold-based) state transitions below.
    //
    // m_AnimTimeMs / s_bounceValue (below) and STATE_BONUS_PHASE's m_Timer
    // advance are NOT converted: m_AnimTimeMs and s_bounceValue are write-only
    // in this build (no reader anywhere in GameOverScreen -- cosmetic counters
    // with zero observable effect), and STATE_BONUS_PHASE's m_Timer advance is
    // gated on live ActorManager entity counts re-evaluated every call --
    // moving it to UpdateRealtime would require duplicating that gate against
    // gameplay state that can change between the decoupled Update()/
    // UpdateRealtime() cadences, risking double/under-advancement. Left
    // binary-faithful (60Hz-coupled) per the "no clean split point" rule.
#endif

    // Prologue
    // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80
    // m_AnimTimeMs = (int)((float)m_AnimTimeMs + dt*1000.0); if(>=1000) -=1000
    m_AnimTimeMs = (int)((float)m_AnimTimeMs + dt * 1000.0f);
    if (m_AnimTimeMs >= 1000) m_AnimTimeMs -= 1000;

    // Defunct: linked-screen callback -- m_LinkedScreen is always NULL in v1.6.1
    // (Initialise @0x00188158 stores 0, no setter); v1.6.1 GameOverScreen::Update @0x00186c80
    if (m_LinkedScreen) {
#if defined(__bada__)
        void** linkedVtable = *reinterpret_cast<void***>(
            reinterpret_cast<uint8_t*>(m_LinkedScreen) + 0x98);
        if (linkedVtable) {
            uint8_t flagByte = *reinterpret_cast<uint8_t*>(
                reinterpret_cast<uint8_t*>(m_LinkedScreen) + 0x30);
            typedef void (*VtFn)(void*, uint8_t);
            reinterpret_cast<VtFn>(linkedVtable[0x40])(m_LinkedScreen, flagByte);
        }
#endif
    }

    // buttonCalled defunct stub -- NetworkManager::DeregisterAllPopupAlertButtons
    // Defunct: popup alert buttons -- no-op stub; v1.6.1 GameOverScreen @0x00186c80

    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // s_bounceValue advance
    // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80
    if (s_bounceValue >= 0.0f) {
        s_bounceValue += 2.0f * dt;
        if (s_bounceValue >= 1.0f) s_bounceValue = 0.0f;
    }

    switch (m_State) {

    // -----------------------------------------------------------------------
    // State 0: entry animation
    // -----------------------------------------------------------------------
    case STATE_ENTRY_ANIM: {
        if (m_bScoreSubmitted == 0) {
            uint8_t gm = game_work.gameMode;
            Mortar::ActorManager* am = game ? game->actorManager : 0;
            if ((uint8_t)(gm - 2) < 2) {
                if (am && am->GetNumEntities(0) == 0 && am->GetNumEntities(1) == 0)
                    game_work.m_bSlowMotion = 1;
            } else {
                game_work.m_bSlowMotion = 1;
            }
        }

        m_LayerFlags = Mortar::HUD_LAYER_DEFAULT;
#ifdef __bada__
        m_Timer += dt;
#endif
        // DIFFERS: v1.6.1 GameOverScreen::Update @0x00186c80 advances the entry
        // reveal m_Timer per 60Hz sim tick; port advances it per rendered frame
        // (dt-scaled) via UpdateRealtime() to track display refresh, so the
        // 0.2s scale-in and 1.9s state-advance thresholds take the same real
        // wall-clock time at 60 and 120 Hz alike. __bada__ keeps the faithful
        // 60Hz path (unconditional += dt above). This 60Hz Update only reads
        // the current m_Timer value below to derive `size` and fire the
        // rate-independent, threshold-based state transition.

        // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80
        // SIN_FULL=20020 (WRONG in old port: 20000), clamp 0x4E34
        if (m_Timer >= 0.2f) {
            size = m_TitleSize * 2.0f;
        } else {
            float t = (m_Timer / 0.2f) * 20020.0f;
            uint16_t idx;
            if (t > 20020.0f)    idx = 0x4E34;
            else if (t > 0.0f)   idx = (uint16_t)(int)t;
            else                 idx = 0;
            float curr = SinIdx(idx);
            float full = SinIdx(0x4E34);
            float ease = (full != 0.0f) ? (curr / full) : 0.0f;
            size = (m_TitleSize * ease) * 2.0f;
        }

        if (m_Timer > 1.9f) {
            if (game_work.gameMode == GAME_MODE_ARCADE) {
                m_State = STATE_BONUS_PHASE;
                m_Timer = -0.333f;
            } else {
                SetStateWait();
            }
        }
        pos = _Vector3<float>(0.0f, 0.0f, 0.0f);
        break;
    }

    // -----------------------------------------------------------------------
    // State 1: bonus phase (Arcade only)
    // -----------------------------------------------------------------------
    case STATE_BONUS_PHASE: {
        Mortar::ActorManager* am = game ? game->actorManager : 0;
        if (am && am->GetNumEntities(0) == 0 && am->GetNumEntities(1) == 0) {
            if (!m_pBonusScreen) {
                FindMostOfFruit();
                m_pBonusScreen = new BonusScreen();
                m_pBonusScreen->pos = _Vector3<float>(0.0f, -20.0f, 0.0f);
                m_pBonusScreen->m_RemoveCallback =
                    Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);
                if (game_work.mHud) game_work.mHud->AddControl(m_pBonusScreen, false);
                BonusManager::GetInstance()->SetUpBonusScreen(m_pBonusScreen);
            } else {
                // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80:
                // pos.y = max(pos.y, bonus->pos.y[+0xC] + bonus->m_AnimPos.y[+0xE4] + 135.0)
                float ny = m_pBonusScreen->pos.y + m_pBonusScreen->m_AnimPos.y + 135.0f;
                if (ny < pos.y) ny = pos.y;
                pos.y = ny;
                const float sf = pos.y / -224.0f + 1.0f;
                size = m_TitleSize * sf;
            }
            // HALT (no dt-scaling applied here): the advance is gated on
            // ActorManager entity counts (the outer `if` above) that are
            // re-evaluated every Update() call -- moving this to
            // UpdateRealtime() would require duplicating that live-gameplay
            // gate against a decoupled per-present cadence, risking double-
            // or under-advancement relative to the binary's single
            // 60Hz-tick-coupled read. Left binary-faithful (60Hz-coupled).
            m_Timer += dt;
            if (m_pBonusScreen) m_pBonusScreen->m_Timer = m_Timer;
            game_work.m_bSlowMotion = 1;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // State 7: retry prepare
    // -----------------------------------------------------------------------
    case STATE_RETRY_PREPARE: {
        Mortar::ActorManager* am = game ? game->actorManager : 0;
        if (am && am->GetNumEntities(0) != 0 && m_pSlotA8 == 0) {
            game_work.m_PauseAmount = 1.0f;
            // fall through to STATE_MAIN_DISPLAY (state stays 7 until game_work sets it to 8)
        } else {
            game_work.m_CoinsAtGameStart = game_work.m_CoinsBalance;
            WaveManager::GetInstance()->Reset(false);
            game_work.bM_bPaused = 1;
            m_State = STATE_RETRY_FADE;
            break;
        }
        // fall-through to case 6 intentional
    }

    // -----------------------------------------------------------------------
    // State 6: main display + score submission
    // -----------------------------------------------------------------------
    case STATE_MAIN_DISPLAY:
    {
        const int prevState = m_State;

        // 2) Create FruitFactControl on first entry (LAB_00187220)
        if (m_pFruitFact == 0) {
            m_pFruitFact = new FruitFactControl();
            m_pFruitFact->pos = _Vector3<float>(183.0f + m_OffsetPos.x, 12.0f + m_OffsetPos.y, 0.0f);
            // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80 (0x00187280):
            //   tab/star args -> FruitFactControl +0x80/+0x84 (m_ComboA/m_ComboB).
            m_pFruitFact->m_ComboA = m_TabIndex;      // +0x80
            m_pFruitFact->m_ComboB = m_StarCountArg;  // +0x84
            if (game_work.mHud) game_work.mHud->AddControl(m_pFruitFact, false);
            m_pFruitFact->Init();

            // m_pFruitFact owns itself: m_bNoDestructor=1 (set in ctor) prevents
            // HUD::Release from deleting it; GameOverScreen::Release is the sole
            // deleter (clears m_bNoDestructor=0 before delete).
            m_pFruitFact->m_RemoveCallback =
                Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);

            // Per-mode page creation (once)
            // m_bNoDestructor=1: GameOverScreen::Release is the sole deleter (it
            // clears to 0 before delete); prevents HUD::Release from double-freeing
            // the page when it iterates the HUD list before reaching gos.
            // Binary evidence: GameOverScreen::Release @0x00185970 explicitly clears
            // m_bNoDestructor=0 on every page before deleting, which implies they
            // were created with m_bNoDestructor=1 (same pattern as m_pFruitFact).
            // Binary (v1.6.1 GameOverScreen::Update @0x00187220) uses three INDEPENDENT
            // if-checks: gm==3, gm==2, gm==0. COMBO (gm==1) receives no fact page.
            uint8_t gm = game_work.gameMode;
            if (gm == GAME_MODE_ZEN) {
                m_pZenPage = new FruitFactZenPage(m_pFruitFact);
                // DIFFERS: port adds m_bNoDestructor=1 (and RemoveCallback) for double-free safety;
                // v1.6.1 GameOverScreen::Update @0x00187220 leaves ctor default (0) / no callback
                m_pZenPage->m_bNoDestructor = 1;
                if (game_work.mHud) game_work.mHud->AddControl(m_pZenPage, false);
                m_pZenPage->Init();
                m_pFruitFact->RegisterPage(m_pZenPage);
                m_pZenPage->m_RemoveCallback =
                    Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);
            } if (gm == GAME_MODE_ARCADE) {
                m_pBonusFactPage = new FruitFactBonusFactPage(m_pFruitFact);
                // DIFFERS: port adds m_bNoDestructor=1 (and RemoveCallback) for double-free safety;
                // v1.6.1 GameOverScreen::Update @0x00187220 leaves ctor default (0) / no callback
                m_pBonusFactPage->m_bNoDestructor = 1;
                if (game_work.mHud) game_work.mHud->AddControl(m_pBonusFactPage, false);
                m_pBonusFactPage->Init();
                m_pFruitFact->RegisterPage(m_pBonusFactPage);
                m_pBonusFactPage->m_RemoveCallback =
                    Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);
            } if (gm == GAME_MODE_CLASSIC) {
                m_pClassicFactPage = new FruitFactClassicFactPage(m_pFruitFact, 0, 0);
                // DIFFERS: port adds m_bNoDestructor=1 (and RemoveCallback) for double-free safety;
                // v1.6.1 GameOverScreen::Update @0x00187220 leaves ctor default (0) / no callback
                m_pClassicFactPage->m_bNoDestructor = 1;
                if (game_work.mHud) game_work.mHud->AddControl(m_pClassicFactPage, false);
                m_pClassicFactPage->Init();
                m_pFruitFact->RegisterPage(m_pClassicFactPage);
                m_pClassicFactPage->m_RemoveCallback =
                    Mortar::Delegate1<void, HUDControl*>::Make(this, &GameOverScreen::DeletedControl);
            }
        }

        // 3) Alpha ramp
        // HALT (no dt-scaling applied here): game_work.m_PauseAmount is a
        // cross-screen shared global (also written by PauseScreen and read
        // across FruitCamera/WaveManager/BombHit/etc. -- see game/GameWork.h).
        // This block reads it to pick a branch, then in the else-branch
        // advances it AND re-reads the just-written value in the same call
        // (m_bSlowMotion gate + >=0.999 clamp) with no clean split point --
        // exactly the "cross-screen shared global" / "write-then-branch in
        // the same Update()" entanglement that blocks a per-present rewrite
        // (see MainScreen's equivalent precedent). m_StarCount (the
        // 0->11 reveal counter) is likewise entangled: it is reset/incremented
        // in the same branches as alpha and gates the single-shot score-commit
        // below. Left binary-faithful (60Hz-coupled); do not invent a
        // port-only cached copy of m_PauseAmount to route around this.
        float& alpha = game_work.m_PauseAmount;
        if (alpha >= 0.999f) {
            if (m_FruitFactAlpha < 1.0f)
                m_FruitFactAlpha += (1.0f - m_FruitFactAlpha) * 0.125f;
            if (m_StarCount < 0xb) m_StarCount++;
        } else {
            m_StarCount = 0;
            alpha += (1.0f - alpha) * 0.125f;
            if (alpha < 0.75f) game_work.m_bSlowMotion = 1;
            if (alpha >= 0.999f) alpha = 1.0f;
            m_FruitFactAlpha = alpha;
        }

        // 4) Score commit on m_StarCount==10 (single-shot via m_bScoreSubmitted)
        if (m_StarCount == 10) {
            m_StarCount = 0xb;
            if (m_bScoreSubmitted == 0) {
                int score = GetCurrentScore(0);
                m_bScoreSubmitted = 1;
                FruitSaveData* save = game_work.m_SaveData;
                if (save) {
                    save->secondaryFlag = 0;
                    // ASM-spec v1.6.1 GameOverScreen @0x001875dc/0x001875fc:
                    // both calls pass trackSession=true, achievementGate=true
                    // (r3 = count, [sp,#0] = 1, [sp,#4] = 1) -- these lifetime
                    // totals must land in m_SessionTotals, which survives
                    // ClearTotals() on quit/retry.
                    save->AddToTotal("games", StringHash("games"), 1, true, true);
                    save->AddToTotal("totalscore", StringHash("totalscore"), score, true, true);
                    save->UnlockTotals();
                    AchievementManager::GetInstance()->UnlockScoreAchievement(score);
                    AchievementManager::GetInstance()->UnlockTotalFruitAchievement((int)(intptr_t)game_work.m_pLastScoredSaveEntry);
                    AchievementManager::GetInstance()->UnlockEndScoreAchievement(score, GetCurrentModeHighscore());

                    // Defunct: leaderboard score submit -- no-op stub; v1.6.1 GameOverScreen @0x00186c80
                    if (game_work.gameMode != GAME_MODE_ARCADE) {
                        // Defunct: RefreshLeaderboard/FNHighscoreList::AddPlayerScore
                        // no-op stubs per online-services-audit
                    }

                    // Zen combo star achievement
                    if (m_pZenPage && game_work.gameMode == GAME_MODE_ZEN) {
                        int comboLevel = (int8_t)m_pZenPage->m_ComboLevel;
                        if (comboLevel >= 0 && comboLevel < 25) {
                            AchievementManager::GetInstance()->UnlockComboStarAchievement(
                                m_pZenPage->m_NumFacts,
                                StringHash(GetComboName((COMBO_TYPE)comboLevel)));
                        }
                    }

                    // Highscore update
                    int hi = GetCurrentModeHighscore();
                    if (hi / 2 < score) {
                        save->newBestThisGame =
                            (uint8_t)SetCurrentModeHighscore(score);
                    }

                    // Per-mode leaderboard ID + ModeBestCombos save
                    // Defunct: GetCurrentModeLeaderboardID / IsP2POnline / NetworkManager::SetLeaderboardScore
                    //   -- no-op stubs; v1.6.1 GameOverScreen @0x00186c80
                    {
                        uint8_t gm = game_work.gameMode;
                        // save->m_ModeBestCombos[gameMode] = score (if score > current)
                        if (gm < 4 && save->m_ModeBestCombos[gm] < score) {
                            save->m_ModeBestCombos[gm] = score;
                        }
                        // Defunct: NetworkManager::SetLeaderboardScore -- no-op stub
                    }

                    if (game_work.gameMode == GAME_MODE_ARCADE) {
                        BonusManager::GetInstance()->UnlockPostGameAchievements();
                    }

                    save->FinishedGame();
                    save->ClearTotals();
                    SaveCurrentData(false);
                }
            }

            // 5) Reset alpha, state -- NESTED in the m_StarCount==10 guard so it
            // runs exactly once (the frame the stars finish). Hoisting it out ran
            // it per-frame: m_State=6 killed the state-7 fall-through (NewGame
            // unreachable) and the buttons respawned every frame after slicing.
            // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80: state-commit +
            // button spawn nested in the m_StarCount==10 guard (runs once);
            // prevState (binary iVar29) is m_State captured at case entry.
            game_work.m_PauseAmount = 1.0f;
            m_State = STATE_MAIN_DISPLAY;

            // 6) Spawn buttons only when entering from state 6
            if (prevState == STATE_MAIN_DISPLAY && IsAllowedToExit()) {
                CreateRetryButton();
            }
            if (m_pQuitBtn == 0 && prevState == STATE_MAIN_DISPLAY && IsAllowedToExit()) {
                CreateQuitButton();
            }
        }

        // TODO: v1.6.1 0x0018782c (GameOverScreen::Update) — binary calls
        // LoadLocalisedTexture("comming_soon_highscore.tex" @0x002829E9) once inside the
        // m_StarCount==10 one-shot. The port previously mis-ported it as a per-frame load
        // that clobbered the mode-title m_TitleTex set in Initialise()
        // (g_GameOverTitleTex / g_TimeUpTitleTex / g_ArcadeTimeUpTitleTex), so it was
        // removed. Restoring the faithful one-shot call is pending an HLE check of the
        // user-visible behaviour.

        // 8) Settle (always): if(pos.y < 212.8)
        if (pos.y < 212.8f) {
            float a = game_work.m_PauseAmount;
            size.x = m_TitleSize.x * (2.0f - a);
            size.y = m_TitleSize.y * (2.0f - a);
            size.z = m_TitleSize.z * (2.0f - a);
            pos.x  = 0.0f;
            pos.y  = 224.0f * a;
            pos.z  = 0.0f;
        }
        break;
    }

    // -----------------------------------------------------------------------
    // State 8: retry fade
    // -----------------------------------------------------------------------
    case STATE_RETRY_FADE: {
        // HALT (no dt-scaling applied here): same shared-global entanglement
        // as STATE_MAIN_DISPLAY's alpha ramp above -- game_work.m_PauseAmount
        // is decayed and re-read/branched (WaveManager::Reset + SetTerminate
        // one-shot side effects) in the same Update() call. Left
        // binary-faithful (60Hz-coupled).
        float& alpha = game_work.m_PauseAmount;
        alpha *= 0.75f;
        m_FruitFactAlpha = alpha;

        if (alpha < 0.001f) {
            WaveManager::GetInstance()->Reset(false);
            alpha = 0.0f;
            game_work.bM_bPaused = 0;
            m_FruitFactAlpha = 0.0f;
            // v1.6.1 GameOverScreen::Update @0x00187178: `bl WaveManager::GetInstance ;
            // bl WaveManager::NewGame` -- NewGame is a __thiscall member, not a static.
            WaveManager::GetInstance()->NewGame();
            SetTerminate();
        }

        // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80
        // pos.y = (1-m_FruitFactAlpha)*224 + 224  (NOT just 224*(1-alpha))
        if (pos.y >= 0.0f) break;
        pos.x = 0.0f;
        pos.y = (1.0f - m_FruitFactAlpha) * 224.0f + 224.0f;
        pos.z = 0.0f;
        break;
    }

    // -----------------------------------------------------------------------
    // State 9: quit wait
    // -----------------------------------------------------------------------
    case STATE_QUIT_WAIT: {
        Mortar::ActorManager* am = game ? game->actorManager : 0;
        if (am && am->GetNumEntities(0) != 0) break;
        DoQuitToMenu();
        m_State = STATE_FINAL_FADE;
        break;
    }

    // -----------------------------------------------------------------------
    // State 10: online leaderboard (defunct)
    // -----------------------------------------------------------------------
    case STATE_LEADERBOARD: {
        Mortar::ActorManager* amLb = game ? game->actorManager : 0;
        const int totalEnts = amLb ? (amLb->GetNumEntities(0) + amLb->GetNumEntities(1)) : 0;
        if (totalEnts != 0) break;
        // Defunct: NetworkManager::LaunchDashboard(2) -- no-op stub
        m_AnimCounter = 0;
        m_pQuitBtn    = 0;
        m_pRetryBtn   = 0;
        m_StarCount   = 0;
        m_State       = STATE_MAIN_DISPLAY;
        break;
    }

    // -----------------------------------------------------------------------
    // State 11: final fade
    // -----------------------------------------------------------------------
    case STATE_FINAL_FADE: {
        if (game_work.m_PauseAmount < 0.0f) SetTerminate();
        break;
    }

    // -----------------------------------------------------------------------
    // State 14: quick-restart
    // -----------------------------------------------------------------------
    case STATE_QUICK_RESTART: {
        // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80
        // game_work.dwField_0x194._1_1_ = 0
        game_work.m_bUpdatesSuspended = 0;
        m_State = STATE_MAIN_DISPLAY;
        m_Timer += dt * 8.0f;
        if (m_Timer >= 8.0f) m_Timer = 0.0f;
        if (m_pSlotA8 == 0) m_StarCount = 0;
        m_Timer = 2.0f;
        break;
    }

    default:
        break;
    }

    // -----------------------------------------------------------------------
    // Common tail (every frame)
    // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80 common tail
    // -----------------------------------------------------------------------
    uint8_t gm = game_work.gameMode;
    if ((uint8_t)(gm - 2) < 2 && m_pFruitFact != 0) {
        // Arcade/Zen layout
        // DIFFERS: opt-in widescreen -- same rigid-group reasoning as the
        // Classic branch below: sensei + fact board share this one pos, so
        // MapX the steady-state base (75.0f) with the SAME "gameover.factboard"
        // edge-anchor-right key, so all three modes nudge right consistently.
        // The alpha-driven slide-in term is left as-is (transient animation,
        // not the resting position). Identity at 3:2/__bada__.
        float ffX = (1.0f - m_FruitFactAlpha) * 480.0f + MapX(75.0f, "gameover.factboard");
        m_pFruitFact->SetPos(_Vector3<float>(ffX, 53.0f, 0.0f));
    } else {
        // Classic/other layout
        m_OffsetPos.x = m_FruitFactAlpha * -386.0f + 368.0f;
        m_OffsetPos.y = 55.0f;
        m_OffsetPos.z = 0.0f;
        // DIFFERS: opt-in widescreen -- the sensei figure (body+head, drawn by
        // FruitFactClassicFactPage's own GenericHUDControls at fixed offsets
        // from this pos) and the "SENSEI'S FRUIT FACT" board + title/body text
        // (drawn at other fixed offsets from the SAME pos, see
        // FruitFactClassicFactPage::DrawOrder) are one rigid group anchored on
        // FruitFactControl::pos -- there is no independent position to move
        // the sensei separately from the board without breaking their binary-
        // fixed relative layout. MapX the steady-state base (183.0f) as a
        // right-edge anchor: the group is a right-side panel, so this keeps
        // its gap from the right edge constant as the field widens (pulling
        // it into the widened space) instead of drifting proportionally
        // in from its small original offset. The alpha-driven slide-in
        // term (m_OffsetPos.x) is left as-is -- a transient entry animation
        // offset from this anchor, not the resting position. Identity at
        // 3:2/__bada__.
        if (m_pFruitFact)
            m_pFruitFact->SetPos(_Vector3<float>(MapX(183.0f, "gameover.factboard") + m_OffsetPos.x, 12.0f + m_OffsetPos.y, 0.0f));
        if (m_pNoticeCtrl)
            m_pNoticeCtrl->pos = _Vector3<float>(0.0f, (1.0f - m_FruitFactAlpha) * 300.0f + 65.0f, -5000.0f);
    }

    // Page-sync loop: copy m_pFruitFact->pos into every registered page
    // ASM-spec v1.6.1 GameOverScreen::Update @0x00186c80
    if (m_pFruitFact) {
        std::vector<FruitFactPage*>& pages = m_pFruitFact->m_Pages;
        for (std::vector<FruitFactPage*>::iterator it = pages.begin(); it != pages.end(); ++it) {
            FruitFactPage* page = *it;
            if (page) page->pos = m_pFruitFact->pos;
        }
    }

    // Slot A8 and B4 slide
    if (m_pSlotA8) m_pSlotA8->pos = _Vector3<float>(190.0f + (1.0f - m_FruitFactAlpha) * 120.0f, -50.0f, 0.0f);
    if (m_pSlotB4) m_pSlotB4->pos = _Vector3<float>(190.0f + (1.0f - m_FruitFactAlpha) * 120.0f, -125.0f, 0.0f);
}

#ifndef __bada__
// ---------------------------------------------------------------------------
// Port specific: no binary counterpart -- see HUDControl::UpdateRealtime and
// the state-machine split comment above Update(). Advances the STATE_ENTRY_ANIM
// entry-reveal m_Timer per PRESENTED frame using the real measured dtSeconds
// (m_Timer += dt is a plain linear accumulator in the binary -- already
// expressed in seconds, not a spring/decay -- so no GOS_APPROACH_F/GOS_DECAY_F
// rate-conversion is needed here, just decoupling the accumulation from
// Update()'s call frequency). Update() (60Hz) reads the resulting value to
// derive `size` (sine-ease scale-in) and fire the (already rate-independent,
// threshold-based) state transition at m_Timer > 1.9f -- that stays in
// Update() exactly like ShopScreen/PauseScreen keep their state-transition
// side effects in Update() rather than UpdateRealtime().
//
// Only STATE_ENTRY_ANIM is handled here. STATE_BONUS_PHASE's m_Timer advance,
// game_work.m_PauseAmount (STATE_MAIN_DISPLAY / STATE_RETRY_FADE), m_AnimTimeMs,
// and s_bounceValue are intentionally NOT converted -- see the HALT comments
// at each site in Update() for why (live-gameplay-gated advance / cross-screen
// shared global entangled with same-call branches / write-only dead counters).
//
// Under __bada__ this function does not exist (see GameOverScreen.h); Update()
// advances m_Timer inline in STATE_ENTRY_ANIM, byte-identical to the binary.
//
// DIFFERS: v1.6.1 GameOverScreen::Update @0x00186c80 advances the entry-reveal
// m_Timer per 60Hz sim tick; port advances it per rendered frame using the real
// measured dtSeconds to track display refresh. __bada__ keeps the faithful
// 60Hz path (unconditional += dt in Update()).
// ---------------------------------------------------------------------------
void GameOverScreen::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches

    if (m_State == STATE_ENTRY_ANIM) {
        // Binary: m_Timer += dt (unconditional every tick in state 0)
        m_Timer += dtSeconds;
    }
}
#endif

// ---------------------------------------------------------------------------
// PreDrawOrder (vtable slot 8, 0x00186894)
// ---------------------------------------------------------------------------

void GameOverScreen::PreDrawOrder(float* hudScaleRaw, int layerMask) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

    // Layer 0x80 path -- highscore label + title-tex quad
    // ASM-verified: 2026-06-26T16:02Z v1.6.1 GameOverScreen::PreDrawOrder @0x00186894..0x00186aac (asm-inspector)
    //   scale = m_pRetryBtn(+0xA4).size.x(+0x20) * 0.5 [vmul s0,s14,s15 @0x186980; s15=0.5];
    //   pos (-163,-96,0) [DAT @0x186aa0]; align 0xF; font pM_Fonts[2]; settled size.x=120 -> scale 60.
    //   The "5000-over-Retry" overlap is binary-faithful: Bada's 2-button layout (no Leaderboards)
    //   puts Retry at x=-80, next to the highscore at x=-163. iOS/Android center Retry (3 buttons) so
    //   they don't collide -- a real Bada-vs-mobile UI difference, NOT a port bug. Keep as-is.
    //   (+0xA4 holds the RETRY button; Ghidra's "m_pQuitBtn" label at +0xA4 is wrong.)
    if ((layerMask & Mortar::HUD_LAYER_POST_ACTOR) != 0) {
        Game* game = Game::GetInstance();
        // Gate: m_pRetryBtn(+0xA4) != 0 && save->m_highscore > 0
        if (m_pRetryBtn != 0 && game && game_work.m_SaveData &&
            game_work.m_SaveData->m_highscore > 0)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "%d", game_work.m_SaveData->m_highscore);

            // DIFFERS: opt-in widescreen -- MapX with the SAME proportional
            // spread as the Retry button (m_pRetryBtn, "gameover.retry") this
            // number sits beside, so it tracks the button instead of drifting
            // to a stale 3:2 anchor next to a widened-position button.
            // Identity (-163.0f) when disabled/__bada__.
            const _Vector3<float> daysPos(MapX(-163.0f, "gameover.retry"), -96.0f, 0.0f);
            // font = game_work numbers font (binary pM_Fonts[2])
            const float scaleArg = m_pRetryBtn->size.x * 0.5f;
            if (game_work.pFontNumbers.IsValid()) {
                game_work.pFontNumbers->DrawString(scaleArg, 1.0f, 0.0f,
                    buf, daysPos,
                    Colour(255, 255, 255, 255),
                    0xF);
            }

            // Overlay quad: m_TitleTex (NOT g_CommingSoonHighscoreTex)
            // ASM-spec v1.6.1 GameOverScreen::PreDrawOrder @0x00186894
            const float btnScaleX = m_pRetryBtn->size.x;
            if (m_TitleTex.IsValid() && btnScaleX > 0.0f && btnScaleX < 600.0f) {
                m_TitleTex->Set();

                MatrixManager& mm = MatrixManager::GetInstance();
                mm.GetWorldStack().Reset();
                Matrix44 mat = Matrix44::MakeScale(btnScaleX, btnScaleX, btnScaleX);
                mat.GlobalTranslate44(daysPos);
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();

                Mortar::Mesh::DrawQuadUnCached(Colour(255, 255, 255, 255), NULL);

                m_TitleTex->UnSet();
            }
        }
    }

    // Layer 1 path: field_0x5f = clamp(hudScale.x * 255) + HUDControl3d::Draw
    // field_0x5f = m_DrawColour.a (+0x5c+3 = alpha byte of tint colour)
    // ASM-spec v1.6.1 GameOverScreen::PreDrawOrder @0x00186894:
    //   title-texture alpha = clamp(game_work.mHud->m_DrawAlpha * 255, 0, 255) -- NOT hudScale.x.
    //   CORRECTION: m_DrawAlpha (HUD+0x20) is written 1.0f EVERY tick by HUD::Update @0x0018c3c0
    //   (HUD.cpp:98), so alpha = 255 (full opacity) -- it does NOT read 0. The baked title is
    //   suppressed because base m_Texture is left UNASSIGNED in Initialise (only m_TitleTex is set;
    //   see @0x00187c90) -> HUDControl3d::Draw @0x0018b544 skips on the invalid-texture gate. Only
    //   the BakedStringBox TTF title (DrawOrder) shows. (The old `m_Texture = bgTex` made it draw
    //   the baked title over the TTF -> the visible duplicate; that line is removed in Initialise.)
    // Real binary layer-1 = ONLY this -- NO sensei/expression/bg-pattern overlay
    if ((layerMask & Mortar::HUD_LAYER_DEFAULT) != 0) {
        float v = game_work.mHud ? game_work.mHud->m_DrawAlpha * 255.0f : 0.0f;
        if (v < 0.0f)   v = 0.0f;
        if (v > 255.0f) v = 255.0f;
        m_DrawColour.a = (uint8_t)(int)v;
        HUDControl3d::Draw(hudScaleRaw);
    }
}

// ---------------------------------------------------------------------------
// DrawOrder (vtable slot 9, 0x00186484)
// ---------------------------------------------------------------------------

void GameOverScreen::DrawOrder(float* hudScaleRaw, int layerMask) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);
    (void)hudScale;

    // State 0xe spinner halo -- uses MenuButton::blurry_backing.tex static
    // ASM-spec v1.6.1 GameOverScreen::DrawOrder @0x00186484
    if (m_State == STATE_QUICK_RESTART) {
        Mortar::SmartPtr<Mortar::Texture>& spinner = MenuButton::GetSparkleRingTex();
        if (spinner.IsValid()) {
            // Lazy-build 48-vert tri-list
            if (!g_StarMesh.initialised) {
                for (int wedge = 0; wedge < 8; ++wedge) {
                    const uint16_t baseAng = (uint16_t)(wedge * 0x1FFE);
                    const float s0 = SinIdx(baseAng) * 0.5f;
                    const float c0 = CosIdx(baseAng) * 0.5f;
                    const float s1 = SinIdx((uint16_t)(baseAng + 0x3FFC)) * 0.075f;
                    const float c1 = CosIdx((uint16_t)(baseAng + 0x3FFC)) * 0.075f;

                    QUADCUSTOMVERTEX* v = &g_StarMesh.verts[wedge * 6];
                    v[0].x = s0 - s1; v[0].y = c0 - c1; v[0].z = 0; v[0].u = 0;     v[0].v = 0;
                    v[1].x = s0 + s1; v[1].y = c0 + c1; v[1].z = 0; v[1].u = 1.0f;  v[1].v = 0;
                    v[2].x = s0*0.6f - s1; v[2].y = c0*0.6f - c1; v[2].z = 0;
                        v[2].u = 0; v[2].v = 1.0f;
                    v[3].x = s0 + s1; v[3].y = c0 + c1; v[3].z = 0;
                        v[3].u = 1.0f; v[3].v = 0;
                    v[4].x = s0*0.6f - s1; v[4].y = c0*0.6f - c1; v[4].z = 0;
                        v[4].u = 0; v[4].v = 1.0f;
                    v[5].x = s0*0.6f + s1; v[5].y = c0*0.6f + c1; v[5].z = 0;
                        v[5].u = 1.0f; v[5].v = 1.0f;
                    for (int i = 0; i < 6; ++i) { v[i].nx = 0; v[i].ny = 0; v[i].nz = 1.0f; }
                }
                g_StarMesh.initialised = true;
            }

            // Per-frame: pulse brightness
            // ASM-spec v1.6.1 GameOverScreen::DrawOrder @0x00186484
            // phase = 7 - (abs((int)m_Timer) & 7); wedge alpha index INCREMENTS
            // per wedge (binary: uVar9 = uVar9 + 1) -> (phase + wedge) mod 8,
            // same direction as MainScreen::DrawLoadingSymbol @0x00198fd4.
            int phase = 7 - (abs((int)m_Timer) & 7);
            for (int wedge = 0; wedge < 8; ++wedge) {
                int alphaIdx = (phase + wedge) & 7;
                int a = alphaIdx * 0x20;
                if (a > 0xFF) a = 0xFF;
                if (a < 0x40) a = 0x40;
                const Colour wedgeCol((uint8_t)a, (uint8_t)a, (uint8_t)a, 200);
                const uint32_t packed = wedgeCol.PlatformColour();
                QUADCUSTOMVERTEX* v = &g_StarMesh.verts[wedge * 6];
                for (int i = 0; i < 6; ++i) v[i].colour = packed;
            }

            // Draw: Scale44(64), Translate44(190,-50,0)
            // ASM-spec v1.6.1 GameOverScreen::DrawOrder @0x00186484
            spinner->Set();
            MatrixManager& mm = MatrixManager::GetInstance();
            mm.GetWorldStack().Reset();
            Matrix44 mat = Matrix44::MakeScale(64.0f, 64.0f, 64.0f);
            mat.GlobalTranslate44(_Vector3<float>(190.0f, -50.0f, 0.0f));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawTriList(g_StarMesh.verts, 48, false, NULL);
            spinner->UnSet();
        }
    }

    // Title string draw: layerMask==1 (param2==1 in binary)
    // ASM-spec v1.6.1 GameOverScreen::DrawOrder @0x00186484
    if (layerMask == 1) {
        if (m_pTitleString) {
            m_pTitleString->SetTranslation(pos, 1);
            float scale = size.x * (1.0f / 512.0f);  // 0.001953125 = 1/512
            m_pTitleString->Draw(_Vector2<float>(scale, scale), 0.0f, 1);
        }
    }

}
