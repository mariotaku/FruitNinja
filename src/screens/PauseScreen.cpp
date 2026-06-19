// Analysed: 2026-05-02T00:00
//
// PauseScreen — Tier-1 implementation.
// Binary: PauseScreen::PauseScreen @ 0x00155460 (ctor), Update @ 0x00154468.
// See docs/engine/pausescreen-deep-re.md for full spec.
//
// Tier-1 scope: SP-only path, states 0/2/3/4/5/6, three P1 buttons.
//
// Tier-2 items pending split-screen MP and BombFlashFull port:
// - L154/L168 P2 buttons (binary +0x9c/+0xa4/+0xb0): P2-side resume/retry/quit slots.
// - State 1 bomb-flash (binary @ 0x00168f24 BombFlashFull): full-screen white-flash on
//   menu-bomb hit; gates state 6 dismissal.
// - HitMenuBomb in state 6 (binary @ 0x0016b234): when game state != 1, plays "MenuBomb"
//   SFX, sets g_GameData[0xf8]=1, writes hit position to g_GameData[0xcc/d0/d4].
// All three depend on subsystems not yet ported. Specs are RE-complete.

#include "screens/PauseScreen.h"
#include "debug/Logger.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "game/WaveManager.h"
#include "game/FruitSaveData.h"
#include "game/PowerUpManager.h"
#include "game/BombHit.h"
#include "entities/Bomb.h"
#include "game/GameTaskState.h"
#include "util/StringHash.h"
#include "math/Random.h"
#include "asset/TextureManager.h"
#include "asset/Texture.h"
#include "render/MatrixManager.h"
#include "engine/render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "math/Colour.h"
#include "engine/audio/GameSound.h"
#include "engine/network/NetworkManager.h"
#include "game/GameOver.h"
#include "game/PowerUpManager.h"
#include "screens/MainScreen.h"
#include "entities/BombBlast.h"
#include "Game.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "game/GameWork.h"

// -------------------------------------------------------------------------
// Constants from binary (see docs section 4 DAT table)
// -------------------------------------------------------------------------
static const float FADE_DECAY        = 0.75f;     // state 0 / 4 / 5 / 6 alpha decay
static const float FADE_CLAMP        = 0.01f;     // DAT_00154fb4
static const float FADE_IN_RATE      = 0.25f;     // state 2 ease-in: (1-alpha)*0.25
static const float ACTIVE_THRESHOLD  = 0.999f;    // DAT_00154fbc  state 2->3
static const float EXIT_THRESHOLD   = 0.001f;    // DAT_00154fc0  states 4/5/6
// ASM-verified: 2026-05-08 binary @ DAT_00154fc4/d0/c8 (re-analyst).
// Previous port values (-2.0f / 240.0f) were wrong by orders of magnitude.
// DAT_00154fd0 = 160.0f is the SCREEN-TOP-EDGE constant, reused as the base
// for both this->pos.y (title slide-in) and quit.pos.y. DAT_00154fc8 =
// 240.0f is the SCREEN-RIGHT-EDGE constant, used as quit.pos.x base.
static const float TITLE_SLIDE_MUL  = -130.0f;   // DAT_00154fc4 (was -2.0f, WRONG)
static const float TITLE_SLIDE_BASE = 160.0f;    // DAT_00154fd0 (was 240.0f, WRONG)
static const float SCREEN_RIGHT_X   = 240.0f;    // DAT_00154fc8 (quit.pos.x base)

// flash.tex overlay: alpha = clamp(m_Alpha * 1000, 0, 128); scale = m_Alpha * 10000
// Binary: PreDraw @ 0x0016bda0
static const float FLASH_ALPHA_MUL  = 1000.0f;
static const float FLASH_ALPHA_MAX  = 128.0f;
static const float FLASH_SCALE_MUL  = 10000.0f;

// Lazy-loaded flash.tex (shared with DrawBombHit)
static Mortar::SmartPtr<Mortar::Texture> s_FlashTex;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
static inline Mortar::SmartPtr<Mortar::Texture> LoadTex(const char* name,
                                                         int* outW = nullptr,
                                                         int* outH = nullptr) {
    Mortar::SmartPtr<Mortar::Texture> t = Mortar::TextureManager::LoadLocalisedTexture(name);
    if (t.IsValid()) {
        if (outW) *outW = t->m_Width;
        if (outH) *outH = t->m_Height;
    } else {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
    }
    return t;
}

// -------------------------------------------------------------------------
// PauseGame / UnpauseGame (binary @ 0x00168f80 / 0x00168fb0)
// -------------------------------------------------------------------------

// Binary @ 0x00168f80 PauseGame():
//   gameObj+0x02 (byte) = 1   -- timer-running flag on gameObj
//   TaskState+0x0C (byte) = 0 -- isPaused = 0 (transition entering pause)
//   TaskState+0x08 (float) = 0.25f -- pause transition timer
void PauseScreen::PauseGame() {
    GameTaskState* ts = GetTaskState();
    game_work.m_Paused = true;
    ts->isPaused = 0;
    ts->pauseTransitionTimer = 0.25f;
}

// ASM-verified: 2026-05-20 binary @ 0x00168fb0 UnpauseGame (re-analyst)
// Binary writes ONLY GameTaskState+0xc and +0x10. Does NOT touch game_work.m_Paused.
// pausedFlag stays set through QUIT_EXIT/RETRY_EXIT/BOMB_FLASH so the dispatcher's
// `active = !pausedFlag && pmState == 0` evaluates to false during those transitions,
// which makes UpdateBombHit and GameOver-cross-1.5 skip. The port's RESUME_EXIT case
// in PauseScreen::Update has its own port-specific pausedFlag clear (DIFFERS) because
// the port's PowerManager stub always returns 0.
void PauseScreen::UnpauseGame() {
    GameTaskState* ts = GetTaskState();
    ts->isPaused = 1;
    ts->pauseBombHitTimer = 0.4f;        // DAT_00168fcc, GameTaskState+0x10
}

// -------------------------------------------------------------------------
// IsEnabled (binary @ 0x00153e4c)
// -------------------------------------------------------------------------

// ASM-verified: 2026-05-09 binary @ 0x00153e4c (re-analyst)
// Returns TRUE when pause overlay is available -- transition timer at rest
// (|t| < 0.001), no bomb-hit pause, and levelTransitionFlag == 0. Earlier port had
// the comparison inverted -- the binary uses `bpl` after vcmpe which means
// "branch if |val| >= epsilon -> return false". Inversion was masked while
// m_TransitionTimer sat permanently at -1.0f in the port; once MainScreen
// started mirroring the camera transition (settles to 0 during gameplay),
// the inversion hid the pause button entirely.
bool PauseScreen::IsEnabled() {
    if (fabsf(game_work.m_GameDt) >= 0.001f) return false;  // [+0xc] epsilon
    if (game_work.m_BombHitTimer > 0.0f)                return false;  // [+0x10]
    return (game_work.m_LevelTransitionFlag ^ 1) != 0;                           // [+0x05] XOR 1
}

// -------------------------------------------------------------------------
// QuitToMenu / EndRetryLevel -- binary @ 0x00169e50 / 0x0016a208
// -------------------------------------------------------------------------
static void QuitToMenu() {
    #ifndef FN_ASM_VERIFY_CROSS
    LOG_INFO("SCREEN/PauseScreen", "%s (%s)", "QuitToMenu enter", "binary @ 0x00169e50");
    #endif
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);   // 0x169e58/60
    game_work.m_LevelTransitionFlag = 1;                               // 0x169e6e: strb 1, [+0x05]

    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_ZOOM); // 0x169e7c [+0x10c] = 0
        game_work.mMainScreen->SetStateTimer(0.5f);         // 0x169e80 [+0x110]
        // TODO: v1.6.1 QuitToMenu @0x001cb6e4 -- seed m_TexMoreGames.f0=0.5f on gameplay->menu return.
        // That binary function (distinct from 0x00169e50) also writes 0.5f to +0x11c so the
        // case-0 hold branch runs for ~0.5s before sliding in. Not yet confirmed whether
        // 0x001cb6e4 is a second QuitToMenu variant or a wrapper; needs re-analyst pass.
        // DIFFERS: binary does NOT call DeleteMenuButtons here. Binary's
        // menu fruit/bomb entities survive gameplay (ResetGameEntities
        // re-chucks them, doesn't destroy them, per re-analyst
        // 2026-05-16). Port's gameplay teardown path destroys those
        // entities (or nulls m_pFruitPiece via the OOB-kill back-ref
        // clear in KillBomb), leaving the MenuButtons rendering an
        // empty ring after quit + Bomb::SetCallback rotation state
        // lost. Forcing a delete+re-create round-trip fixes both
        // symptoms until the entity-survival path is ported.
        game_work.mMainScreen->DeleteMenuButtons();
    }

    // 0x169e84/0x169e86 binary writes PauseScreen->m_bPendingRemoval = 1
    // (re-analyst 2026-05-10), destructing the overlay. Port relies on
    // PauseScreen's own state machine (BOMB_FLASH -> HIDDEN with m_Alpha
    // decay) to fade the overlay out gradually -- if we deactivated here
    // (m_Active=0), Update would stop running and the BOMB_FLASH state
    // would never advance.

    FN::SetScore(0, -1);                               // 0x169e90

    // Defunct: P2P / online disconnect -- no-op stub; binary @ 0x00169e9e
    // (binary: NetworkManager::GetInstance()->vtable[3](0))
    Mortar::NetworkManager::GetInstance()->SpawnThreadController();

    // 0x169eaa: vstr 0.0f, [+0x1a0] (DAT_00169ec4 = 0x00000000 verified
    // via read_memory). Earlier port asm-inspector misread the literal as
    // -2.0f; binary actually CLEARS the timer on quit (resets the
    // vestigial ramp at 0x0016c5fe back to disarmed).
    game_work.m_QuitTransitionTimer = 0.0f;

    // Binary @ 0x00169eae..0x00169ebe: clear-on-quit flags.
    // m_bDisconnectPending and m_bP2PGameStarted removed in v1.6.1
    // (those byte slots at +0x19c/+0x19d are now interior to m_FrameTimer int).
    game_work.m_bMPRetryPending = 0;
    game_work.m_bP2PHostMatched = 0;
    game_work.m_bP2PClientJoined = 0;

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

// EndRetryLevel moved to BombHit.cpp (FN::EndRetryLevel) so GameUpdate can call it
// directly from the retry dispatch tail. PauseScreen calls FN::EndRetryLevel() below.

// -------------------------------------------------------------------------
// ctor
// -------------------------------------------------------------------------
PauseScreen::PauseScreen()
    : m_Alpha(0.0f),
      m_TitleSize(0.0f, 0.0f, 0.0f),
      m_ButtonOriginPos(0.0f, 0.0f, 0.0f),
      m_ResumeButton(nullptr),
      m_P2ResumeButton(nullptr),
      m_QuitButton(nullptr),
      m_P2QuitButton(nullptr),
      m_Pad_0xA8(nullptr),
      m_RetryButton(nullptr),
      m_P2RetryButton(nullptr),
      m_ButtonFadeAlpha(1.0f),
      m_LastHitButton(-1),
      m_PressIndex(0),
      m_RevealTimer(0.0f),
      m_PausedText(nullptr),
      m_State(PAUSE_STATE_HIDDEN),
      m_TitleTexW(0.0f), m_TitleTexH(0.0f),
      m_PauseButtonTexW(0.0f), m_PauseButtonTexH(0.0f),
      m_QuitTitleTexW(0.0f), m_QuitTitleTexH(0.0f),
      m_RetryButtonTexW(0.0f), m_RetryButtonTexH(0.0f)
{
    m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;

    // Load textures -- strings resolved from GOT in ctor (doc section 6 asset table)
    // pause_title.tex goes into inherited m_Texture (+0x78 in binary / +0x74 in port)
    // Port: m_Texture is the inherited GLuint; m_Texture (the primary display tex)
    // is set to the title texture so HUDControl3d::Draw renders it.
    {
        int w = 0, h = 0;
        Mortar::SmartPtr<Mortar::Texture> tex = LoadTex("pause_title.tex", &w, &h);
        m_Texture      = tex;     // +0x74 slot (PauseScreen::DrawOrder uses its own path)
        m_Texture = tex;     // +0x78 slot (binary stores here; HUDControl3d::Draw base reads this)
        m_TitleTexW = (float)w;
        m_TitleTexH = (float)h;
    }

    // +0xb8 m_PauseButtonTex: pause_button.tex (in-game pause icon)
    {
        int w = 0, h = 0;
        m_PauseButtonTex = LoadTex("pause_button.tex", &w, &h);
        m_PauseButtonTexW = (float)w;
        m_PauseButtonTexH = (float)h;
    }

    // +0xbc m_PlayButtonTex: play_button.tex (resume icon)
    {
        m_PlayButtonTex = LoadTex("play_button.tex");
    }

    // +0xc0 m_QuitTitleTex: quit_title.tex
    {
        int w = 0, h = 0;
        m_QuitTitleTex = LoadTex("quit_title.tex", &w, &h);
        m_QuitTitleTexW = (float)w;
        m_QuitTitleTexH = (float)h;
    }

    // +0xc4 m_RetryButtonTex: retry_button.tex
    // Reset() assigns this to m_RetryButton->m_Texture on every level reset.
    {
        int w = 0, h = 0;
        m_RetryButtonTex = LoadTex("retry_button.tex", &w, &h);
        m_RetryButtonTexW = (float)w;
        m_RetryButtonTexH = (float)h;
    }

    // Title size stored in m_TitleSize for slide-in math (doc section 4 #6)
    m_TitleSize = Vec3(m_TitleTexW, m_TitleTexH, 0.0f);

    // Initial pos: centered along Y by texture height (doc section 2 notes)
    // pos = (0, (320 - sizeY) * 0.5, 0)
    pos = Vec3(0.0f, (320.0f - m_TitleTexH) * 0.5f, 0.0f);

    // size for HUDControl3d::Draw quad
    size = Vec3(m_TitleTexW, m_TitleTexH, 0.0f);
}

PauseScreen::~PauseScreen() {}

// -------------------------------------------------------------------------
// vtable[2]: Init -- forwards to Reset (HUDControl::Reset)
// Binary: 0x00153e28 -- 5-instr thunk: (*vtable[4])(this)
// -------------------------------------------------------------------------
void PauseScreen::Init() {
    Reset();
}

// -------------------------------------------------------------------------
// vtable[3]: Release -- nulls 5 SmartPtr<Texture> slots.
// ASM-verified: 2026-05-08T00:00 binary @ 0x0015408C (re-analyst)
//
// Slots nulled (5x SmartPtrNull_Tex calls at 0x00154054):
//   +0x74 m_Texture (HUDControl3d primary)
//   +0xb8 m_PauseButtonTex
//   +0xbc m_PlayButtonTex
//   +0xc0 m_QuitTitleTex
//   +0xc4 m_RetryButtonTex
// -------------------------------------------------------------------------
void PauseScreen::Release() {
    m_Texture.SetNull();
    m_PauseButtonTex.SetNull();
    m_PlayButtonTex.SetNull();
    m_QuitTitleTex.SetNull();
    m_RetryButtonTex.SetNull();
}

// -------------------------------------------------------------------------
// vtable[4]: Reset -- restores SP-mode tex assignments on resume/retry buttons
// ASM-verified: 2026-05-08T00:00 binary @ 0x00154024 (re-analyst)
//
// Two if-blocks, no other PauseScreen field is touched:
//   if (m_RetryButton) {
//       retry->m_bTouchHeld = 1;            // +0x131
//       retry->m_Texture = m_RetryButtonTex;   // src is +0xc4 (binary)
//   }
//   if (m_ResumeButton) resume->m_Texture = m_PlayButtonTex;
//
// Inverse of SetToMultiplayerState: re-enables RetryButton and restores
// SecondaryTex assignments so SP layout is correct after MP session ends.
// -------------------------------------------------------------------------
void PauseScreen::Reset() {
    if (m_RetryButton) {
        m_RetryButton->m_bTouchHeld = 1;
        m_RetryButton->m_Texture = m_RetryButtonTex;
    }
    if (m_ResumeButton) {
        m_ResumeButton->m_Texture = m_PlayButtonTex;
    }
}

// -------------------------------------------------------------------------
// vtable[5]: BeginDraw -- asserts m_LayerFlags = 8
// Binary: 0x00153e44
// -------------------------------------------------------------------------
void PauseScreen::BeginDraw(float dt) {
    (void)dt;
    m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
}

// -------------------------------------------------------------------------
// vtable[6]: PreDraw -- full-screen black-tinted flash.tex overlay
// Binary: 0x0016bda0
// Only runs when m_LayerFlags == 8 (asserted by BeginDraw each frame).
// alpha = clamp(m_Alpha * 1000.0, 0, 128); tint = (0,0,0,alpha)
// scale = m_Alpha * 10000.0
// -------------------------------------------------------------------------
void PauseScreen::PreDraw(const Vec3& hudScale) {
    (void)hudScale;

    if (m_Alpha <= 0.0f) return;

    if (!s_FlashTex.IsValid()) {
        s_FlashTex = Mortar::TextureManager::LoadLocalisedTexture("flash.tex");
        if (!s_FlashTex.IsValid()) return;
    }

    float alphaF = m_Alpha * FLASH_ALPHA_MUL;
    if (alphaF < 0.0f) alphaF = 0.0f;
    if (alphaF > FLASH_ALPHA_MAX) alphaF = FLASH_ALPHA_MAX;
    const uint8_t alpha = (uint8_t)(int)alphaF;

    const float scale = m_Alpha * FLASH_SCALE_MUL;
    const Colour tint(0, 0, 0, alpha);

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(scale, scale, 1.0f);
    // Centered at origin (full-screen coverage)
    mat.GlobalTranslate44(Vec3(0.0f, 0.0f, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    s_FlashTex->Set();
    Game* game = Game::GetInstance();
    if (game) {
        game->renderer.DrawQuad(tint);
    }
    s_FlashTex->UnSet();
}

// -------------------------------------------------------------------------
// vtable[9]: DrawOrder -- gate on m_Alpha > 0 (Tier-1: skip online-MP branch)
// Binary: 0x00153e98
// -------------------------------------------------------------------------
void PauseScreen::DrawOrder(const Vec3& hudScale, int layerMask) {
    if (m_Alpha > 0.0f) {
        HUDControl3d::Draw(hudScale, layerMask);
    }
}

// -------------------------------------------------------------------------
// vtable[11]: SetToMultiplayerState -- Tier-2 stub
// Binary: 0x00154060
// -------------------------------------------------------------------------
bool PauseScreen::SetToMultiplayerState() {
    // Tier-2 deferred (binary @ 0x00154060): vtable[11], called from PauseScreen::Reset on MP entry.
    // Body (3 stores):
    //   1. m_RetryButton->m_Texture = SmartPtr::Null;     // retry+0x74
    //   2. m_RetryButton->m_bTouchHeld = 0;               // retry+0x131 -- disable interactability
    //   3. m_ResumeButton->m_Texture = m_RetryButtonTex;  // resume+0x74 -- show retry icon (+0xc4) on resume btn
    // Activates only when split-screen MP is enabled. Trivial 3-line port -- RE complete.
    return HUDControl::SetToMultiplayerState();
}

// -------------------------------------------------------------------------
// Button delegate callbacks
// -------------------------------------------------------------------------

// PauseGameCallback (binary 0x001542d0)
// Resume button press-action:
//   State 0 -> 2 (pause from gameplay)
//   State 3 -> 4 (resume)
void PauseScreen::PauseGameCallback() {
    if (m_State == PAUSE_STATE_HIDDEN) {
        #ifndef FN_ASM_VERIFY_CROSS
        LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_FADE_IN), "PauseGameCallback");
        #endif
        m_State = PAUSE_STATE_FADE_IN;
        PauseGame();
        // SFX "Pause"
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("Pause", 1.0f);
        }
    } else if (m_State == PAUSE_STATE_ACTIVE) {
        #ifndef FN_ASM_VERIFY_CROSS
        LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_RESUME_EXIT), "PauseGameCallback");
        #endif
        m_State = PAUSE_STATE_RESUME_EXIT;
        // SFX "Unpause"
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("Unpause", 1.0f);
        }
    }
}

// ASM-verified: 2026-05-02 binary @ 0x00154400 (asm-inspector)
void PauseScreen::PauseGameCallback2() {
    bool wasIdle = (m_ButtonFadeAlpha == 0.0f) && (m_State == PAUSE_STATE_HIDDEN);
    PauseGameCallback();   // drives state 0->2 or 3->4
    if (wasIdle) {
        m_PressIndex = 1;
    }
    // state 5 is unreachable from this callback in single-player
}

// ASM-verified: 2026-05-08T00:00 binary @ 0x00153ebc (re-analyst)
// Binary @ 0x00153ebc QuitGameCallback():
//   if (m_State != 3) return;
//   FruitSaveData::ClearTotals(); FruitSaveData::ClearCombo(saveData);
//   game_work.m_bTutorialShown = 0; m_LastHitButton = 0; m_State = 6;
// NOTE: m_Alpha *= 0.5 and SaveCurrentData happen in Update case-6 entry, NOT here.
// NOTE: SFX "MenuQuit" also happens in Update state-6 path, not this callback.
void PauseScreen::QuitGameCallback() {
    if (m_State != PAUSE_STATE_ACTIVE) return;
    if (game_work.m_SaveData) game_work.m_SaveData->ClearTotals();
    if (game_work.m_SaveData) game_work.m_SaveData->ClearCombo();
    game_work.m_bTutorialShown = 0;
    m_LastHitButton = 0;
    #ifndef FN_ASM_VERIFY_CROSS
    LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_QUIT_EXIT), "QuitGameCallback @ 0x00153ebc");
    #endif
    m_State = PAUSE_STATE_QUIT_EXIT;
}

// Binary @ 0x00153ef8 QuitGameCallback2():
//   QuitGameCallback(); m_LastHitButton = 1; g->field_0x85 = 0;
// Used by P2-Quit button in MP path.
// ASM-verified: 2026-05-08T00:00 binary @ 0x00153ef8 (re-analyst)
// Defunct: multiplayer Quit2 path -- single-player port still wires the
// tutorial-clear write so the cb retains its post-call observable state.
void PauseScreen::QuitGameCallback2() {
    QuitGameCallback();
    m_LastHitButton = 1;
    game_work.m_bTutorialShown = 0;
}

// ASM-verified: 2026-05-08T00:00 binary @ 0x00153f68 (re-analyst)
// Binary @ 0x00153f68 RetryGameCallback():
//   if (m_State != 3) return;
//   if (game_work.m_ElapsedGameTime < 10.5f)
//       FruitSaveData::AddToTotal("retries_in_a_row", hash, 1, true, true);
//   Math::SeedGlobalRng(game_work.m_FrameTimer);  // binary @ 0x00153f20
//   game_work.m_bTutorialShown = 0;
//   FruitSaveData::ClearTotals(); FruitSaveData::ClearCombo(saveData);
//   m_State = 5;
void PauseScreen::RetryGameCallback() {
    if (m_State != PAUSE_STATE_ACTIVE) return;
    if (game_work.m_ElapsedGameTime < 10.5f && game_work.m_SaveData) {
        // String resolved from binary DAT_00153fe4 -> 0x001ba98f.
        const char* kKey = "retries_in_a_row";
        game_work.m_SaveData->AddToTotal(kKey, ::StringHash(kKey),
                                    1, true, true);
    }
    // Binary @ 0x00153f20: re-seed Mortar::Random g_Random with frame
    // counter so retried runs are deterministic-from-frame-state rather
    // than boot-clock-seeded. Re-analyst confirmed g_Random @ 0x0026C8B0.
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
    game_work.m_bTutorialShown = 0;
    if (game_work.m_SaveData) game_work.m_SaveData->ClearTotals();
    if (game_work.m_SaveData) game_work.m_SaveData->ClearCombo();
    #ifndef FN_ASM_VERIFY_CROSS
    LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_RETRY_EXIT), "RetryGameCallback @ 0x00153f68");
    #endif
    m_State = PAUSE_STATE_RETRY_EXIT;
}

// -------------------------------------------------------------------------
// vtable[10]: Update -- state machine + lazy button creation
// Binary: 0x00154468 (569 lines)
// Tier-1: SP path only (IsSameScreenMultiplayer() branch skipped)
// -------------------------------------------------------------------------
void PauseScreen::Update(float dt) {
    // --- Lazy button creation (SP path only) ---
    // ASM-verified: 2026-05-06T00:00 binary @ 0x00154468..0x001545fc (asm-inspector)
    // Each of the three create blocks is gated only on null-on-self
    // (`+0x98 m_ResumeButton`, `+0xa0 m_QuitButton`, `+0xac m_RetryButton`).
    // No `IsEnabled()` / `levelTransitionFlag` / `m_State` / `m_TransitionTimer` test
    // wraps the allocations — binary creates eagerly on first Update().
    // Visibility on non-gameplay screens is an alpha/draw-time concern,
    // handled by m_ButtonFadeAlpha -> m_DrawColour.a propagation below.
    // ASM-spec for binary @ 0x001544e8..0x001545fc (re-analyst):
    //   MenuButton(this, texSP&, &pos, &clickDelegate, fruitType=-1,
    //              &globalCenterVec, &deletedDelegate)
    //   then SetSingular() and AddControl().
    // For PauseScreen toggles fruitType is hard-coded -1 (no fruit entity
    // spawned); globalCenterVec is HUD::g_GlobalCenterVec = (0,0,0);
    // deletedDelegate is HUD::g_DeleteControlDelegate (HUD-side cleanup
    // when the button is removed -- left empty in port until that
    // global delegate is exposed).
    //
    // Port routes through MenuButton::Init(pos, clickCb, fruitType,
    // hitBounds, deletedCb) -- the fruitType=-1 branch skips entity
    // creation but still sets m_bVisible/Interactive/Enabled and the
    // anim defaults. Texture / m_LayerFlags / m_TargetSize are set
    // before/after Init since Init doesn't manage those slots.
    if (!m_ResumeButton) {
        m_ResumeButton = new MenuButton();
        // Binary passes texture via ctor arg -> m_Texture; Init's
        // fruitType<0 text-button branch reads m_Texture to auto-size
        // m_TargetSize and size from texture dims. m_Texture is set for
        // HUDControl3d::Draw rendering.
        m_ResumeButton->m_Texture = m_PauseButtonTex;
        m_ResumeButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
        m_ResumeButton->Init(
            Vec3(240.0f, -160.0f, 0.0f),  // initial pos; overwritten each frame
            Mortar::Delegate0<void>::Make(this, &PauseScreen::PauseGameCallback),
            /*fruitType=*/-1,             // toggle -- no fruit entity spawned
            Vec3(0.0f, 0.0f, 0.0f),       // globalCenterVec = HUD::g_GlobalCenterVec
            Mortar::Delegate0<void>()     // TODO: bind HUD::g_DeleteControlDelegate
        );

        game_work.mHud->AddControl(m_ResumeButton);
        m_ResumeButton->SetSingular();

        // ASM-verified post-Init override (binary @ 0x001545d8..0x00154604):
        //   m_TargetSize = (Vector3::One @ GOT+0x77CC) * 64.0 * 1.0 = (64,64,64)
        //   m_ButtonOriginPos := m_TargetSize  (one-shot capture for OX in
        //   the per-frame position formulas).
        m_ResumeButton->m_RestScale = Vec3(64.0f, 64.0f, 64.0f);
        m_ButtonOriginPos            = m_ResumeButton->m_RestScale;
    }

    if (!m_QuitButton) {
        m_QuitButton = new MenuButton();
        // Binary @ 0x001544e8: texture via ctor arg -> m_Texture for Init auto-size.
        m_QuitButton->m_Texture = m_QuitTitleTex;
        m_QuitButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
        m_QuitButton->Init(
            Vec3(0.0f, 320.0f, 0.0f),
            Mortar::Delegate0<void>::Make(this, &PauseScreen::QuitGameCallback),
            /*fruitType=*/-1,
            Vec3(0.0f, 0.0f, 0.0f),
            Mortar::Delegate0<void>()
        );

        game_work.mHud->AddControl(m_QuitButton);
        m_QuitButton->SetSingular();
    }

    if (!m_RetryButton) {
        m_RetryButton = new MenuButton();
        // Binary @ 0x001544e8: texture via ctor arg -> m_Texture for Init auto-size.
        m_RetryButton->m_Texture = m_RetryButtonTex;
        m_RetryButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
        m_RetryButton->Init(
            Vec3(0.0f, 320.0f, 0.0f),
            Mortar::Delegate0<void>::Make(this, &PauseScreen::RetryGameCallback),
            /*fruitType=*/-1,
            Vec3(0.0f, 0.0f, 0.0f),
            Mortar::Delegate0<void>()
        );

        game_work.mHud->AddControl(m_RetryButton);
        m_RetryButton->SetSingular();
    }

    // --- State machine ---
    switch (m_State) {

    case PAUSE_STATE_HIDDEN:
        // Alpha decay toward 0
        m_Alpha *= FADE_DECAY;
        if (m_Alpha < FADE_CLAMP) m_Alpha = 0.0f;

        // Reveal timer countdown (Tier-2: gates Resume re-enable)
        m_RevealTimer -= dt;
        if (m_RevealTimer <= 0.0f) {
            m_RevealTimer = 0.0f;
            // Re-arm the Resume button. Binary @ 0x00154d24 unconditionally
            // writes `m_ResumeButton->m_bTouchHeld = 1` here (re-analyst
            // confirmed no `= 0` write to +0x131 exists anywhere in
            // PauseScreen::Update). Mirror that exactly.
            if (m_ResumeButton) m_ResumeButton->m_bTouchHeld = 1;
        }
        break;

    case PAUSE_STATE_BOMB_FLASH:
        // ASM-verified: 2026-05-10 binary @ 0x00154d2a..0x00154d72 (re-analyst).
        // Hold m_Alpha = 1.0 / m_ButtonFadeAlpha = 0.0 each frame while
        // BombFlashFull() returns false (i.e. game_work.m_BombHitTimer >= 1.0).
        // When the bomb-hit timer crosses below 1.0 (half the 2.0s window),
        // reset PowerUpManager, drop to HIDDEN, and pull m_TransitionTimer
        // to -1.0 so the slide-back-to-menu animation kicks in via MainScreen.
        m_ButtonFadeAlpha = 0.0f;
        m_Alpha           = 1.0f;
        if (Bomb::BombFlashFull()) {
            m_Alpha           = 1.0f;
            m_ButtonFadeAlpha = 1.0f;
            PowerUpManager::GetInstance()->Reset(false);
            #ifndef FN_ASM_VERIFY_CROSS
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_HIDDEN), "Update/BOMB_FLASH complete");
            #endif
            m_State = PAUSE_STATE_HIDDEN;
            game_work.m_GameDt = -1.0f;
        }
        break;

    case PAUSE_STATE_FADE_IN:
        m_Alpha += (1.0f - m_Alpha) * FADE_IN_RATE;

        // Force game pause flag each frame while fading in (SP path only)
        game_work.m_Paused = true;

        if (m_Alpha > ACTIVE_THRESHOLD) {
            m_Alpha = 1.0f;
            #ifndef FN_ASM_VERIFY_CROSS
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_ACTIVE), "Update/FADE_IN alpha settled");
            #endif
            m_State = PAUSE_STATE_ACTIVE;
        }
        break;

    case PAUSE_STATE_ACTIVE:
        // Force game pause flag each frame (SP path only)
        game_work.m_Paused = true;

        // Enable hit detection on Resume and Retry
        if (m_ResumeButton) m_ResumeButton->m_bTouchHeld = 1;
        if (m_RetryButton)  m_RetryButton->m_bTouchHeld  = 1;
        break;

    case PAUSE_STATE_RESUME_EXIT:
        m_Alpha *= FADE_DECAY;
        if (m_Alpha < EXIT_THRESHOLD) {
            m_Alpha = 0.0f;
            #ifndef FN_ASM_VERIFY_CROSS
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_HIDDEN), "Update/RESUME_EXIT faded");
            #endif
            m_State = PAUSE_STATE_HIDDEN;
            m_RevealTimer = 2.0f;
            UnpauseGame();
            // DIFFERS: binary relies on PowerManager::GetState() going non-zero on
            // background to make the dispatcher's active=(!pausedFlag&&pmState==0)
            // evaluate true again after resume. Port's PowerManager stub always
            // returns 0, so pausedFlag is the sole gate. Clear it here (resume path
            // only) so GameUpdate resumes ticking. QUIT_EXIT and RETRY_EXIT leave
            // pausedFlag set -- binary-faithful -- so active=false holds through
            // BOMB_FLASH and the 1.5f GameOver-cross check skips.
            game_work.m_Paused = false;
        }
        break;

    case PAUSE_STATE_RETRY_EXIT:
        m_Alpha *= FADE_DECAY;
        if (m_Alpha < EXIT_THRESHOLD) {
            // Binary @ 0x00154dbc: SaveCurrentData(false) before RetryLevel.
            FruitNinja_SaveCurrentData(false);
            m_Alpha = 0.0f;
            m_ButtonFadeAlpha = 0.0f;
            #ifndef FN_ASM_VERIFY_CROSS
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_HIDDEN), "Update/RETRY_EXIT faded");
            #endif
            m_State = PAUSE_STATE_HIDDEN;
            m_RevealTimer = 2.0f;
            // Binary calls RetryLevel (0x0016b008), NOT EndRetryLevel (0x0016a208).
            // RetryLevel sets retryFlag=1 + retryTimer=0.1f; GameUpdate's retry
            // dispatch tail then calls RetryUpdate per frame and EndRetryLevel at 0.
            FN::RetryLevel();
            UnpauseGame();
        }
        break;

    case PAUSE_STATE_QUIT_EXIT:
        // ASM-verified: 2026-05-10 binary @ 0x00154dc4..0x00154e1e (re-analyst).
        // Binary applies 0.5x extra fast-fade in case 6 then falls through to
        // the common 0.75 decay; both happen each frame.
        m_Alpha *= 0.5f;
        m_Alpha *= FADE_DECAY;
        if (m_Alpha < EXIT_THRESHOLD) {
            #ifndef FN_ASM_VERIFY_CROSS
            LOG_INFO("SCREEN/PauseScreen", "%s (%s)", "QuitToMenu @ 0x00169e50", "QUIT_EXIT faded");
            #endif
            QuitToMenu();
            // White-flash via HitMenuBomb at the hit button's pos. Index 0 is
            // the P1 quit button (m_QuitButton); index 1 would be P2 in MP.
            if (m_LastHitButton >= 0 && m_QuitButton) {
                Bomb::HitMenuBomb(m_QuitButton->pos);
                #ifndef FN_ASM_VERIFY_CROSS
                LOG_INFO("BOMBHIT", "QuitToMenu fires HitMenuBomb at (%.1f,%.1f); bombHitTimer set to %.3f",
                         m_QuitButton->pos.x, m_QuitButton->pos.y,
                         game_work.m_BombHitTimer);
                #endif
            }
            // Binary writes m_ButtonFadeAlpha = 1.0 (DAT_00154fb8), NOT 0.0.
            // Earlier port wrote 0.0 which left the buttons at full opacity
            // straight through the bomb-flash phase.
            m_ButtonFadeAlpha = 1.0f;
            m_LastHitButton   = -1;
            // Transition to BOMB_FLASH (1), NOT HIDDEN. The bomb-flash poll
            // in case 1 is what produces the visible white flash and tears
            // down the gameplay HUD; jumping straight to HIDDEN skipped both.
            #ifndef FN_ASM_VERIFY_CROSS
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_BOMB_FLASH), "Update/QUIT_EXIT faded");
            #endif
            m_State = PAUSE_STATE_BOMB_FLASH;
            m_Alpha = 1.0f;
            FruitNinja_SaveCurrentData(false);
            UnpauseGame();
        }
        break;

    default:
        break;
    }

    // --- Post-switch unconditional math (doc section 4 items 1-7) ---

    // 1. m_ButtonFadeAlpha decay / restore
    // IsEnabled() reads g_GameData fields (binary @ 0x00153e4c).
    const bool isEnabled = IsEnabled();
    if (isEnabled) {
        m_ButtonFadeAlpha *= FADE_DECAY;
        if (m_ButtonFadeAlpha < 0.0f) m_ButtonFadeAlpha = 0.0f;
    } else {
        m_ButtonFadeAlpha += (1.0f - m_ButtonFadeAlpha) * FADE_IN_RATE;
    }

    // 2. State 6 scratch override (binary @ 0x00154eac..0x00154ec4 +
    // 0x00155200..0x00155204).
    // ASM-verified: 2026-05-10 binary (asm-inspector). Binary saves
    // m_Alpha + m_ButtonFadeAlpha BEFORE the reset, runs the rendering math
    // with the scratch values, then RESTORES the persistent fields from
    // the saved values at function exit. The 1.0 / 0.0 reset only affects
    // button-layout / texture-swap reads in the post-switch math; the
    // persistent fade state continues to decay across frames.
    // Earlier port treated the reset as a persistent state mutation, which
    // pinned m_Alpha at 1.0 each frame and made case-6's
    // `m_Alpha *= 0.75` decay never reach EXIT_THRESHOLD -- Quit hung.
    const float savedAlpha           = m_Alpha;
    const float savedButtonFadeAlpha = m_ButtonFadeAlpha;
    if (m_State == PAUSE_STATE_QUIT_EXIT) {
        m_Alpha = 1.0f;
        m_ButtonFadeAlpha = 0.0f;
    }

    // 3. Resume button texture swap based on m_Alpha
    if (m_ResumeButton) {
        if (m_Alpha <= 0.5f) {
            m_ResumeButton->m_Texture = m_PauseButtonTex;
        } else {
            m_ResumeButton->m_Texture = m_PlayButtonTex;
        }
    }

    // 4. Title slide-in: pos.x = 0, pos.y = 240 + sizeY + (-2 * m_Alpha)
    {
        const float sizeY = m_TitleSize.y;
        pos.x = 0.0f;
        pos.y = TITLE_SLIDE_BASE + sizeY + TITLE_SLIDE_MUL * m_Alpha;
    }

    // 5. Quit button position (only when m_Alpha > 0.01 and m_PressIndex < 2).
    // ASM-verified: 2026-05-08 binary @ ~0x00154f7a..0x00154fae (re-analyst).
    //   quit.pos.y = -((160 - sizeY*0.5 - 5) + (1-alpha)*(sizeY + 10))
    //   quit.pos.x =  240 - sizeX*0.5
    // Previous port had 240/0 instead of 160/240 -- positioned the quit
    // button 80 units further off-screen and centered on x=0 instead of
    // the right edge.
    if (m_QuitButton && m_Alpha > FADE_CLAMP && m_PressIndex < 2) {
        const float qSizeY = m_QuitTitleTexH;
        const float qSizeX = m_QuitTitleTexW;
        float quitY = -((TITLE_SLIDE_BASE - qSizeY * 0.5f - 5.0f)
                        + (1.0f - m_Alpha) * (qSizeY + 10.0f));
        float quitX = SCREEN_RIGHT_X - qSizeX * 0.5f;
        m_QuitButton->pos.x = quitX;
        m_QuitButton->pos.y = quitY;
    }

    // 6. Resume + Retry button position recomputation.
    // ASM-verified: 2026-05-06T00:00 binary @ 0x00154f8a..0x001550d6 (re-analyst+asm-inspector)
    //
    // PauseScreen post-switch tail. Three RE passes converged on this:
    //   - m_ButtonOriginPos.x is a per-session constant set ONCE at
    //     lazy-create from m_ResumeButton->m_RestScale.x. Binary builds
    //     m_TargetSize via (Vector3::One @ GOT+0x77CC) * 64.0 * 1.0
    //     => (64, 64, 64). So OX = 64.
    //   - HUD/HUDControl3d does NO parent-transform composition (binary
    //     HUD::Draw @ 0x00144a90: flat std::list, each control resets
    //     matrix and translates by its own pos). MenuButtons are top-
    //     level siblings of PauseScreen; PauseScreen.pos does NOT
    //     offset child buttons.
    //   - Resume formula writes pos.x AND pos.y; pos.z untouched.
    //   - Retry formula writes pos.x, pos.y, pos.z.
    //
    // ASM-verified: 2026-05-08 binary @ 0x00154fea..0x001551d2 (re-analyst).
    //
    // Two-phase Resume + Retry layout:
    //   Phase 1 (lines below): write the off-screen base positions.
    //   Phase 2 (after the m_Alpha > 0 gate): lerp toward an on-screen
    //     to_pos by m_Alpha. WITHOUT the lerp the buttons stay at their
    //     base positions (off-screen left/right) for the entire pause
    //     overlay -- which is what the user observed visually.
    //
    // ASM-verified: 2026-05-18 binary @ 0x00154468 (re-analyst)
    // Per-frame Resume m_TargetSize: m_ButtonOriginPos (captured (64,64,64)
    // at lazy-create) scaled by resumeScale = m_Alpha * 1.25 + 0.75.
    // MenuButton::Update writes `size = m_TargetSize` each frame, so only
    // m_TargetSize matters here -- writes to size.x/y are clobbered.
    if (m_ResumeButton) {
        const float resumeScale = m_Alpha * 1.25f + 0.75f;
        m_ResumeButton->m_RestScale = m_ButtonOriginPos * resumeScale;
    }

    const float OX = m_ButtonOriginPos.x;  // = 64

    // Phase 1: write the base positions.
    // Resume base (binary @ 0x00154fea..0x00155106):
    //   pos.x = -((244 - 0.5*OX) + |fade|*(10 + 0.75*OX))
    //   pos.y = 0.375*OX - 165   (y unchanged by phase-2 lerp)
    // (Previous port had 0.375*OX in term1 -- WRONG; binary has 0.5*OX.)
    if (m_ResumeButton) {
        const float absFade = std::fabs(m_ButtonFadeAlpha);
        const float term1 = 244.0f - 0.5f * OX;
        const float term2 = absFade * (10.0f + 0.75f * OX);
        m_ResumeButton->pos.x = -(term1 + term2);
        m_ResumeButton->pos.y = 0.375f * OX - 165.0f;
    }
    // Retry base (binary @ 0x00155076..0x00155096):
    //   pos = (240 + 0.5*OX, -20, 0)
    if (m_RetryButton) {
        m_RetryButton->pos = Vec3(240.0f + 0.5f * OX, -20.0f, 0.0f);
    }

    // ASM-verified: 2026-05-18 binary @ 0x00154468 tail (re-analyst)
    // Retry m_TargetSize := Resume m_TargetSize when m_Alpha > 0.
    // Without this, Retry stays at MenuButton::Init texture-auto-size
    // (129,129,0) -- 2x oversize hitbox and .z=0 degenerate render matrix.
    {
        bool retryActive = false;
        if (m_Alpha > 0.0f && m_ResumeButton && m_RetryButton) {
            m_RetryButton->m_RestScale = m_ResumeButton->m_RestScale;
            retryActive = true;
        }
        if (m_RetryButton) {
            m_RetryButton->m_Active = retryActive ? 1 : 0;
        }
    }

    // Phase 2: lerp toward on-screen target by m_Alpha.
    // Binary @ 0x00155106..0x001551d2:
    //   button.pos += (to_pos - button.pos) * m_Alpha
    //   Resume target = (-OX, -20, 0) -- inside-left
    //   Retry  target = (+OX, -20, 0) -- inside-right
    // (Online-MP path substitutes (0,0,0) for both -- defunct.)
    if (m_Alpha > 0.0f) {
        if (m_ResumeButton) {
            const Vec3 target(-OX, -20.0f, 0.0f);
            m_ResumeButton->pos += (target - m_ResumeButton->pos) * m_Alpha;
        }
        if (m_RetryButton) {
            const Vec3 target(OX, -20.0f, 0.0f);
            m_RetryButton->pos += (target - m_RetryButton->pos) * m_Alpha;
        }
    }

    // 7. P2 buttons inactive (Tier-2 stub -- P2 buttons are nullptr in Tier-1)
    // m_P2ResumeButton / m_P2RetryButton are always nullptr in Tier-1.

    // Restore the persistent fade state -- the state-6 reset above was a
    // scratch override for rendering only. Binary @ 0x00155200/0x00155204.
    m_Alpha           = savedAlpha;
    m_ButtonFadeAlpha = savedButtonFadeAlpha;
}

// ASM-verified: 2026-05-08T00:00 binary @ 0x00153e34 (re-analyst)
// Binary @ 0x00153e34: external entry — force overlay fully visible and
// jump to state 3. Used by the Bada-app-side "skip intro" handler.
void PauseScreen::SkipTo() {
    m_State = PAUSE_STATE_ACTIVE;
    m_Alpha = 1.0f;
}

// ASM-verified: 2026-05-08T00:00 binary @ 0x00153fe8 (re-analyst)
// Binary @ 0x00153fe8: external entry (no in-screen button binds it).
// Likely call site: shop/tutorial popup-dismiss handler. Advances state
// 3 -> 4 and clears the tutorial-shown flag.
void PauseScreen::ContinueGameCallback() {
    if (m_State != PAUSE_STATE_ACTIVE) return;
    m_State = PAUSE_STATE_RESUME_EXIT;
    if (game_work.m_bTutorialShown != 0) {
        // Binary @ 0x00153f20: re-seed g_Random; see RetryGameCallback notes.
        Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
    }
    game_work.m_bTutorialShown = 0;
}
