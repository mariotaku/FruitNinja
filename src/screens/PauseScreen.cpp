// Analysed: 2026-05-02T00:00
//
// PauseScreen — Tier-1 implementation.
// Binary: PauseScreen::PauseScreen @ 0x001a7204 (ctor), Update @ 0x001a5ebc.
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
#include "hud/BSButton.h"
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
#include "engine/network/P2PMessageHandling.h"
#include "game/GameOver.h"
#include "game/PowerUpManager.h"
#include "screens/MainScreen.h"
#include "entities/BombBlast.h"
#include "entities/SuperFruitControl.h"
#include "Game.h"
#include "render/Layout.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include "game/GameWork.h"
#include "render/BakedStringBox.h"
#include "game/UpdateMusic.h"
#include "render/Font.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "util/StringTable.h"
#include "math/_Vector2.h"

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

// ---------------------------------------------------------------------------
// Rate-independence macros for m_Alpha / m_ButtonFadeAlpha easing (all states).
// Mirrors ShopScreen's SS_APPROACH_F/SS_DECAY_F pattern (see ShopScreen.cpp),
// which itself mirrors ScrollingMenu's SM_DECAY_F/SM_SPRING_F. Under __bada__
// these expand to the ORIGINAL per-60Hz-tick scalar forms (byte-identical to
// the binary, no powf); under the port the same call sites expand to
// dt-scaled forms using a local `float f` in scope at each use site
// (f = clamp(dtSeconds,0,0.1)*60 in UpdateRealtime()) so f==1 exactly
// reproduces one 60Hz tick's worth of easing.
// ---------------------------------------------------------------------------
#ifdef __bada__
    // v += (to - v) * k  (spring towards `to` by factor k each call)
    #define PS_APPROACH_F(v, to, k)  ((v) += ((to) - (v)) * (k))
    // v *= k  (decay towards zero by factor k each call)
    #define PS_DECAY_F(v, k)         ((v) *= (k))
#else
    #define PS_APPROACH_F(v, to, k)  ((v) += ((to) - (v)) * (1.0f - powf(1.0f - (k), f)))
    #define PS_DECAY_F(v, k)         ((v) *= powf((k), f))
#endif

// flash.tex overlay: alpha = clamp(m_Alpha * 1000, 0, 128); scale = m_Alpha * 10000
// Binary: PreDraw @ 0x0016bda0
static const float FLASH_ALPHA_MUL  = 1000.0f;
static const float FLASH_ALPHA_MAX  = 128.0f;
static const float FLASH_SCALE_MUL  = 10000.0f;

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

// Shared TTF face for m_PausedText BakedStringBox in PauseScreen.
// v1.6.1 PauseScreen::PauseScreen @0x001a7204: reads game_work.m_pTTFFontMain
//   (GameWork+0x614, the locale face PreloadFontsTTF @0x0011c1fc sets to
//   arabic.ttf when languageFlag==0x14, else gangofchinese.ttf). Falls back to
//   a lazily-created gangofchinese.ttf only if PreloadFontsTTF hasn't run yet.
static Mortar::FontCacheObjectTTF* GetPauseTTFFont() {
    if (game_work.m_pTTFFontMain) {
        return game_work.m_pTTFFontMain;
    }
    static Mortar::SmartPtr<Mortar::Font> s_Font =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_Font.IsValid()) {
        return 0;
    }
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_Font.Get());
}
static inline Mortar::SmartPtr<Mortar::Texture> LoadTex(const char* name,
                                                         int* outW = nullptr,
                                                         int* outH = nullptr) {
    Mortar::SmartPtr<Mortar::Texture> t = Mortar::TextureManager::LoadLocalisedTexture(name);
    if (t.IsValid()) {
        if (outW) *outW = t->GetWidth();
        if (outH) *outH = t->GetHeight();
    } else {
        if (outW) *outW = 0;
        if (outH) *outH = 0;
    }
    return t;
}

// -------------------------------------------------------------------------
// PauseGame / UnpauseGame (binary @ 0x001ca48c / 0x001ca4b4)
// -------------------------------------------------------------------------

// v1.6.1 PauseGame @0x001ca48c:
//   game_work.bM_Mode = 1;   unpause_game = 0;   unpauseDelay = 0.25f;
// Note: the old stale address 0x00168f80 was a different function (ComboBox ctor area).
void PauseGame() {
    game_work.bM_Mode = true;
    g_unpause_game  = 0;
    g_unpauseDelay  = 0.25f;
}

// v1.6.1 UnpauseGame @0x001ca4b4:
//   repauseDelay = 0.4f;   unpause_game = 1;
// Does NOT write bM_Mode directly. GameDraw tail @0x001cdd64 fires the actual
// bM_Mode clear + ClearActions on the next rendered frame after unpause_game is armed.
void UnpauseGame() {
    g_repauseDelay = 0.4f;
    g_unpause_game = 1;
}

// -------------------------------------------------------------------------
// SkipToPause (binary @ 0x001cb424)
// -------------------------------------------------------------------------

// ASM-spec v1.6.1 SkipToPause @ 0x001cb424
// Binary body: if (force || (ps && ps->IsEnabled())) { m_PauseAmount=0; SkipTo;
//   bM_bPaused=0; bM_Mode=true; MainScreen::Hide; HUD::Skip; PreloadInGameSounds. }
void SkipToPause(bool force) {
    PauseScreen* pauseScreen = GetTaskState()->pPauseScreen;
    if (force || (pauseScreen && pauseScreen->IsEnabled())) {
        game_work.m_PauseAmount = 0.0f;
        if (pauseScreen) pauseScreen->SkipTo();
        game_work.bM_bPaused = 0;
        game_work.bM_Mode = true;
        if (game_work.mMainScreen) game_work.mMainScreen->Hide();
        if (game_work.mHud) game_work.mHud->Skip();
        PreloadInGameSounds();
    }
}

// -------------------------------------------------------------------------
// IsEnabled -- v1.6.1 PauseScreen::IsEnabled @0x001a5588
// -------------------------------------------------------------------------

// ASM-verified: 2026-05-09 v1.6.1 PauseScreen::IsEnabled @ 0x001a5588 (re-analyst)
// Returns TRUE when pause overlay is available -- transition timer at rest
// (|t| < 0.001), no bomb-hit pause, and levelTransitionFlag == 0. Earlier port had
// the comparison inverted -- the binary uses `bpl` after vcmpe which means
// "branch if |val| >= epsilon -> return false". Inversion was masked while
// m_TransitionTimer sat permanently at -1.0f in the port; once MainScreen
// started mirroring the camera transition (settles to 0 during gameplay),
// the inversion hid the pause button entirely.
bool PauseScreen::IsEnabled() {
    if (fabsf(game_work.m_PauseAmount) >= 0.001f) return false;  // [+0xc] epsilon
    if (game_work.m_BombHitTimer > 0.0f)                return false;  // [+0x10]
    return (game_work.bM_bPaused ^ 1) != 0;                           // [+0x05] XOR 1
}

// -------------------------------------------------------------------------
// QuitToMenu / EndRetryLevel -- binary @ 0x001cb6e4
// TODO: re-verify v1.6.1 EndRetryLevel addr
// -------------------------------------------------------------------------
// v1.6.1 Model A: quit-to-menu pauses in place (bM_bPaused=1), stays in task state 2 --
// the binary (QuitToMenu @0x001cb6e4) never hops task state or tears down HUD/WaveManager;
// GameExit @0x001cfed4 runs only on real app exit. #179
void QuitToMenu() {
    LOG_INFO("SCREEN/PauseScreen", "QuitToMenu enter (v1.6.1 @0x001cb6e4)");
    // Binary first call: SuperFruitControl::StopAllPomegranates(...) @0x001cb6e4.
    // StopAllPomegranates is not yet ported; use ResetAll() (binary symbol: Reset()
    // @0x001bb52c; renamed port-side to avoid colliding with the HUDControl3d-inherited
    // virtual Reset()) which kills all ActorManager group-6 entities and clears
    // SuperFruitControls, preventing a lingering SuperFruitControl from advancing
    // m_Timer on the menu (bM_Mode==0) and calling GetNextWave(0) on a stale/empty
    // m_WaveInfo list -> OOB (#178).
    // TODO: v1.6.1 QuitToMenu @0x001cb6e4 -- replace with SuperFruitControl::StopAllPomegranates
    //   once that function is RE'd and added to the public API.
    SuperFruitControl::ResetAll();
    WaveManager::GetInstance()->ResetGlobalDt(1.0f);   // v1.6.1 QuitToMenu @0x001cb6e4
    game_work.bM_bPaused = 1;                          // v1.6.1 QuitToMenu @0x001cb6e4: strb 1, [+0x05]
    // bM_Mode is NOT cleared here. The binary QuitToMenu @0x001cb6e4 never writes bM_Mode.
    // The camera-settle auto-clear in GameUpdate @0x001cfaec clears bM_Mode once
    // m_PauseAmount settles and PauseScreen leaves state ACTIVE (m_State != 3).

    if (game_work.mMainScreen) {
        game_work.mMainScreen->SetState(STATE_CAMERA_ZOOM);    // v1.6.1 QuitToMenu @0x001cb6e4: m_State (+0x118) = 0
        game_work.mMainScreen->SetIntroHoldTimer(0.5f);        // v1.6.1 QuitToMenu @0x001cb6e4: vstr s15,[r1,#0x11c]
        // Binary keeps menu buttons and fruit alive on quit; ResetGameEntities flings in-game
        // fruit/bombs during the bomb-flash phase (Bomb::HitMenuBomb -> UpdateBombHit
        // crosses 1.5s -> ResetGameEntities(false)). No DeleteMenuButtons here.
    }

    // v1.6.1 QuitToMenu @0x001cb6e4: binary writes PauseScreen->m_bPendingRemoval = 1
    // (re-analyst 2026-05-10), destructing the overlay. Port relies on
    // PauseScreen's own state machine (BOMB_FLASH -> HIDDEN with m_Alpha
    // decay) to fade the overlay out gradually -- if we deactivated here
    // (m_Active=0), Update would stop running and the BOMB_FLASH state
    // would never advance.

    // TODO: v1.6.1 QuitToMenu @0x001cb6e4 -- if (game_work.pM_pTransientScreen)
    //   *(reinterpret_cast<uint8_t*>(game_work.pM_pTransientScreen)+0x33) = 1;
    //   pM_pTransientScreen not yet in GameWork layout; add when that field is RE'd.

    SetScore(0, -1);                               // v1.6.1 QuitToMenu @0x001cb6e4

    // Defunct: P2P / online disconnect -- no-op stub; v1.6.1 QuitToMenu @ 0x001cb6e4
    // (binary: NetworkManager::GetInstance()->vtable[3](0))
    Mortar::NetworkManager::GetInstance()->SpawnThreadController();

    // v1.6.1 QuitToMenu @0x001cb6e4: vstr 0.0f, [+0x1a0] (verified via read_memory).
    // Earlier port asm-inspector misread the literal as -2.0f; binary actually CLEARS
    // the timer on quit (resets the vestigial ramp back to disarmed).
    game_work.m_QuitTransitionTimer = 0.0f;

    // v1.6.1 QuitToMenu @0x001cb764: clear-on-quit flags -- m_bMPRetryPending (+0x174)
    // then the four P2P session bytes +0x1A2..+0x1A5. SetupGameWork does NOT zero these
    // four; this is their only writer besides the dead-stripped networking code.
    game_work.m_bMPRetryPending = 0;
    game_work.m_reserved1a2 = 0;
    game_work.m_reserved1a3 = 0;
    game_work.m_reserved1a4 = 0;
    game_work.m_reserved1a5 = 0;
}

// EndRetryLevel moved to BombHit.cpp so GameUpdate can call it
// directly from the retry dispatch tail.

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
      m_MenuBombIndex(-1),
      m_PressIndex(0),
      m_RevealTimer(0.0f),
      m_PausedText(nullptr),
      m_State(PAUSE_STATE_HIDDEN)
{
    m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;

    // Load textures -- strings resolved from GOT in ctor (doc section 6 asset table)
    // pause_title.tex goes into the inherited HUDControl3d::m_Texture (+0x74),
    // the primary display texture HUDControl3d::Draw renders.
    {
        Mortar::SmartPtr<Mortar::Texture> tex = LoadTex("pause_title.tex");
        m_Texture = tex;   // +0x74: binary @0x001a7204 field_0x74 (read +0x24/+0x28 for title W/H)
    }

    // +0xb8 m_PauseButtonTex: pause_button.tex (in-game pause icon)
    {
        m_PauseButtonTex = LoadTex("pause_button.tex");
    }

    // +0xbc m_PlayButtonTex: play_button.tex (resume icon)
    {
        m_PlayButtonTex = LoadTex("play_button.tex");
    }

    // +0xc0 m_QuitTitleTex: quit_title.tex
    {
        m_QuitTitleTex = LoadTex("quit_title.tex");
    }

    // +0xc4 m_RetryButtonTex: retry_button.tex
    // Reset() assigns this to m_RetryButton->m_Texture on every level reset.
    {
        m_RetryButtonTex = LoadTex("retry_button.tex");
    }

    // ASM-spec v1.6.1 PauseScreen::PauseScreen @0x001a7204: the title quad's
    // dimensions are read straight off the pause_title Texture (+0x24 width,
    // +0x28 height) at ctor time and stored, in this order, into size
    // (+0x20/+0x24/+0x28), m_TitleSize (+0x80) and pos (+0x08/+0x0c/+0x10).
    // Note the Z component: size/m_TitleSize get 1.0f, pos gets 0.0f.
    {
        const float texW = m_Texture.IsValid() ? (float)m_Texture->GetWidth()  : 0.0f;
        const float texH = m_Texture.IsValid() ? (float)m_Texture->GetHeight() : 0.0f;

        // size for HUDControl3d::Draw quad
        size = _Vector3<float>(texW, texH, 1.0f);

        // Title size retained for the slide-in math in Update.
        m_TitleSize = size;

        // Initial pos: centered along Y by texture height.
        pos = _Vector3<float>(0.0f, (320.0f - size.y) * 0.5f, 0.0f);
    }

    // ASM-spec v1.6.1 PauseScreen::PauseScreen @0x001a7204: build m_PausedText.
    // Binary: operator new(200=0xc8); BakedStringBox(box, *(g_GameData+0x614), 100, 0x1e);
    //   SetHorizontalLineSpacing(-1); SetText(GETSTRING(0x3c8, 0));
    //   SetColour(game_work[+0x6a0], true).
    {
        Mortar::FontCacheObjectTTF* font = GetPauseTTFFont();
        if (font) {
            m_PausedText = new Mortar::BakedStringBox(
                font, 14.0f, 100, 30, (Mortar::ALIGNMENT_TYPE)0xf, 1, 0);
            m_PausedText->SetHorizontalLineSpacing(-1);
            m_PausedText->SetText(GETSTRING(LSTR_PAUSED, 0));
            m_PausedText->SetColour(game_work.m_TitleColour, true);
        }
    }
}

PauseScreen::~PauseScreen() {
    // ASM-spec v1.6.1 PauseScreen::~PauseScreen @0x001a5ce4 area: delete m_PausedText.
    // Mirrors ScoreControl::~ScoreControl pattern for BakedStringBox* members.
    if (m_PausedText) {
        delete m_PausedText;
        m_PausedText = nullptr;
    }
}

// -------------------------------------------------------------------------
// vtable[2]: Init -- forwards to Reset (HUDControl::Reset)
// Binary: 0x001a5554 -- 5-instr thunk: (*vtable[4])(this)
// -------------------------------------------------------------------------
void PauseScreen::Init() {
    Reset();
}

// -------------------------------------------------------------------------
// vtable[3]: Release -- nulls 5 SmartPtr<Texture> slots, then deletes m_PausedText.
// ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::Release @0x001a5bbc (re-analyst)
//
// Slots nulled (5x SmartPtr::SetNull, binary order):
//   +0x74 m_Texture (HUDControl3d primary)  @0x001a5bc4
//   +0xbc m_PlayButtonTex                   @0x001a5bcc
//   +0xb8 m_PauseButtonTex                  @0x001a5bd4
//   +0xc0 m_QuitTitleTex                    @0x001a5bdc
//   +0xc4 m_RetryButtonTex                  @0x001a5be4
// Then @0x001a5bec..0x001a5c0c (null-guarded, after the five nulls):
//   if (m_PausedText) { delete m_PausedText; m_PausedText = 0; }
// -------------------------------------------------------------------------
void PauseScreen::Release() {
    m_Texture.SetNull();
    m_PlayButtonTex.SetNull();
    m_PauseButtonTex.SetNull();
    m_QuitTitleTex.SetNull();
    m_RetryButtonTex.SetNull();
    if (m_PausedText) {
        delete m_PausedText;
        m_PausedText = nullptr;
    }
}

// -------------------------------------------------------------------------
// vtable[4]: Reset -- restores SP-mode tex assignments on resume/retry buttons
// ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::Reset @0x001a58ac (re-analyst)
//
// Two if-blocks, no other PauseScreen field is touched:
//   if (m_RetryButton) {
//       retry->m_bAcceptsTouch = 1;          // +0x149
//       retry->m_Texture = m_RetryButtonTex;   // src is +0xc4 (binary)
//   }
//   if (m_ResumeButton) resume->m_Texture = m_PlayButtonTex;
//
// Inverse of SetToMultiplayerState: re-enables RetryButton and restores
// SecondaryTex assignments so SP layout is correct after MP session ends.
// -------------------------------------------------------------------------
void PauseScreen::Reset() {
    if (m_RetryButton) {
        m_RetryButton->m_bAcceptsTouch = 1;
        m_RetryButton->m_Texture = m_RetryButtonTex;
    }
    if (m_ResumeButton) {
        m_ResumeButton->m_Texture = m_PlayButtonTex;
    }
}

// -------------------------------------------------------------------------
// vtable[5]: BeginDraw
// ASM-spec v1.6.1 PauseScreen::BeginDraw @ 0x001a557c: writes m_LayerFlags (+0x34)
//   = 0x108 (HUD_LAYER_BUTTONS 0x08 | HUD_LAYER_P2_SCORE 0x100) -- overlay drawn in
//   BOTH HUD::Draw(0x08) and HUD::Draw(0x100) passes. Port previously wrote only 8,
//   dropping the pause overlay from the 0x100 (P1-score) pass.
// -------------------------------------------------------------------------
void PauseScreen::BeginDraw(float dt) {
    (void)dt;
    m_LayerFlags = Mortar::HUD_LAYER_BUTTONS | Mortar::HUD_LAYER_P2_SCORE;
}

// -------------------------------------------------------------------------
// vtable[6]: PreDraw -- full-screen black-tinted flash.tex overlay
// Binary: 0x001cd35c
// alpha = clamp(m_Alpha * 1000.0, 0, 128); tint = (0,0,0,alpha)
// scale = m_Alpha * 10000.0
// -------------------------------------------------------------------------
void PauseScreen::PreDraw(float* /*hudScale*/) {

    // ASM-spec v1.6.1 PauseScreen::PreDraw @0x001cd35c: shade (flash.tex) gated on
    // m_LayerFlags == 8. BeginDraw (@0x001a557c) sets 0x108 every frame, so the gate
    // is never true during pause -> the binary draws NO pause dim; toggles (layer 0x08)
    // + frozen gameplay stay bright. (The ==8 path is a v1.5 vestige, disabled when
    // BeginDraw moved to 0x108 to render the paused-title in the 0x100 pass.)
    if (m_LayerFlags != Mortar::HUD_LAYER_BUTTONS) return;   // != 8

    if (m_Alpha <= 0.0f) return;

    if (!g_FlashTexture.IsValid()) {
        g_FlashTexture = Mortar::TextureManager::LoadLocalisedTexture("flash.tex");
        if (!g_FlashTexture.IsValid()) return;
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
    mat.GlobalTranslate44(_Vector3<float>(0.0f, 0.0f, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    g_FlashTexture->Set();
    // No Game guard: v1.6.1 PauseScreen::PreDraw @0x001cd35c calls the renderer's
    // DrawQuad directly (bl 0x001cc648 @0x001cd4ac) with no instance null test.
    Game::GetInstance()->renderer.DrawQuad(tint);
    // ASM-spec v1.6.1 PauseScreen::PreDraw @0x001cd35c: binary passes 1 (true) to
    // vtable+0x10 UnSet(bool), matching the other two shared-flash-texture call
    // sites (task #141).
    g_FlashTexture->UnSet(true);
}

// -------------------------------------------------------------------------
// vtable[9]: DrawOrder -- title quad + paused-text overlay
// ASM-spec v1.6.1 PauseScreen::DrawOrder @0x001a572c:
//   layerMask != 0x100: return immediately.
//   m_Alpha > 0 && !IsOnlineMultiplayer(): HUDControl3d::Draw (title texture quad).
//   m_Alpha > 0 && m_PausedText: SetTranslation(pos, 1); Draw(0.0f, (1,1), 1).
// -------------------------------------------------------------------------
void PauseScreen::DrawOrder(float* hudScaleRaw, int layerMask) {
    if (layerMask != 0x100) return;

    if (m_Alpha > 0.0f) {
        // TODO: v1.6.1 PauseScreen::DrawOrder @0x001a572c -- !IsOnlineMultiplayer() guard;
        //   binary skips HUDControl3d::Draw in online-MP mode. No online-MP in port;
        //   guard omitted until MP is ported.
        HUDControl3d::Draw(hudScaleRaw);
    }

    if (m_Alpha > 0.0f && m_PausedText) {
        m_PausedText->SetTranslation(this->pos, 1);
        m_PausedText->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }
}

// -------------------------------------------------------------------------
// vtable[11]: SetToMultiplayerState -- Tier-2 stub
// ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::SetToMultiplayerState @0x001a5e74 (re-analyst)
// -------------------------------------------------------------------------
bool PauseScreen::SetToMultiplayerState() {
    // Tier-2 deferred (v1.6.1 @0x001a5e74): vtable[11], called from PauseScreen::Reset on MP entry.
    // Body (3 stores):
    //   1. m_RetryButton->m_Texture = SmartPtr::Null;     // retry+0x74
    //   2. m_RetryButton->m_bAcceptsTouch = 0;             // retry+0x149 -- disable interactability
    //   3. m_ResumeButton->m_Texture = m_QuitTitleTex;    // resume+0x74 <- this+0xc0 (add r1,r5,#0xc0), NOT m_RetryButtonTex
    // Activates only when split-screen MP is enabled. Trivial 3-line port -- RE complete.
    return HUDControl::SetToMultiplayerState();
}

// -------------------------------------------------------------------------
// Button delegate callbacks
// -------------------------------------------------------------------------

// ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::PauseGameCallback @0x001a5978 (re-analyst)
// Resume button press-action:
//   Debounce: return while m_ButtonFadeAlpha != 0 (taps mid-fade are swallowed).
//   State 0 -> 2 (pause from gameplay): m_PressIndex = 0; SFX "Pause" plays BEFORE
//     the PauseGame() gate; PauseGame() skipped in online MP.
//   State 3 -> 4 (resume): disable resume touch, m_RevealTimer = 2.0 (@0x001a5a6c;
//     Update()'s RESUME_EXIT-faded branch writes it again -- both exist in binary),
//     SFX "Unpause"; then the game_work+0x89 resume-snapshot block: if pending,
//     re-seed the global RNG from m_FrameTimer (helper @0x001a566c), clear the flag
//     (same block as ContinueGameCallback).
void PauseScreen::PauseGameCallback() {
    if (m_ButtonFadeAlpha != 0.0f) return;
    if (m_State == PAUSE_STATE_HIDDEN) {
        LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_FADE_IN), "PauseGameCallback");
        m_PressIndex = 0;
        m_State = PAUSE_STATE_FADE_IN;
        // SFX "Pause"
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("Pause", 1.0f);
        }
        if (!IsOnlineMultiplayer()) {
            PauseGame();
        }
    } else if (m_State == PAUSE_STATE_ACTIVE) {
        LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_RESUME_EXIT), "PauseGameCallback");
        if (m_ResumeButton) m_ResumeButton->m_bAcceptsTouch = 0;
        m_RevealTimer = 2.0f;
        // SFX "Unpause"
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("Unpause", 1.0f);
        }
        m_State = PAUSE_STATE_RESUME_EXIT;
        if (game_work.m_bResumeSnapshotPresent != 0) {
            Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
        }
        game_work.m_bResumeSnapshotPresent = 0;
    }
}

// ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::PauseGameCallback2 @0x001a5b38 (re-analyst)
void PauseScreen::PauseGameCallback2() {
    bool wasIdle = (m_ButtonFadeAlpha == 0.0f) && (m_State == PAUSE_STATE_HIDDEN);
    PauseGameCallback();   // drives state 0->2 or 3->4
    if (wasIdle) {
        m_PressIndex = 1;
    }
    // state 5 is unreachable from this callback in single-player
}

// ASM-verified: 2026-05-08T00:00 v1.6.1 PauseScreen::QuitGameCallback @ 0x001a55e0 (re-analyst)
// Binary body:
//   if (m_State != 3) return;
//   FruitSaveData::ClearTotals(); FruitSaveData::ClearCombo(saveData);
//   game_work.m_bResumeSnapshotPresent = 0; m_State = 6; m_MenuBombIndex = 0;
// NOTE: m_Alpha *= 0.5 and SaveCurrentData happen in Update case-6 entry, NOT here.
// NOTE: SFX "MenuQuit" also happens in Update state-6 path, not this callback.
void PauseScreen::QuitGameCallback() {
    if (m_State != PAUSE_STATE_ACTIVE) return;
    if (game_work.m_SaveData) game_work.m_SaveData->ClearTotals();
    if (game_work.m_SaveData) game_work.m_SaveData->ClearCombo();
    game_work.m_bResumeSnapshotPresent = 0;
    LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_QUIT_EXIT), "QuitGameCallback @ 0x001a55e0");
    m_State = PAUSE_STATE_QUIT_EXIT;
    m_MenuBombIndex = 0;
}

// v1.6.1 PauseScreen::QuitGameCallback2 @0x001a5634:
//   QuitGameCallback(); m_MenuBombIndex = 1; game_work.m_bResumeSnapshotPresent = 0;
// (`strb r2,[r3,#0x89]` at 0x001a565c — +0x89, matching QuitGameCallback's own
//  `strb r3,[r4,#0x89]` at 0x001a5624.)
// Used by P2-Quit button in MP path.
// ASM-verified: 2026-05-08T00:00 v1.6.1 PauseScreen::QuitGameCallback2 @ 0x001a5634 (re-analyst)
// Defunct: multiplayer Quit2 path -- single-player port still wires the
// resume-snapshot-flag clear so the cb retains its post-call observable state.
void PauseScreen::QuitGameCallback2() {
    QuitGameCallback();
    m_MenuBombIndex = 1;
    game_work.m_bResumeSnapshotPresent = 0;
}

// ASM-verified: 2026-05-08T00:00 v1.6.1 PauseScreen::RetryGameCallback @ 0x001a5800 (re-analyst)
// Binary body:
//   if (m_State != 3) return;
//   if (game_work.m_ElapsedGameTime < 10.5f)
//       FruitSaveData::AddToTotal("retries_in_a_row", hash, 1, true, true);
//   Math::SeedGlobalRng(game_work.m_FrameTimer);  // T.1054 @0x001a566c, called
//                                                 // UNCONDITIONALLY at 0x001a5878
//                                                 // (no +0x89 test, unlike Pause/Continue)
//   game_work.m_bResumeSnapshotPresent = 0;
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
    // v1.6.1 PauseScreen::RetryGameCallback @0x001a5800: re-seed Mortar::Random
    // g_Random from the frame counter, so a retried run draws a fresh stream
    // instead of continuing the previous attempt's. NOT reproducible -- the seed
    // is whatever frame the player happened to hit Retry on. g_Random @ 0x0026C8B0.
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
    game_work.m_bResumeSnapshotPresent = 0;
    if (game_work.m_SaveData) game_work.m_SaveData->ClearTotals();
    if (game_work.m_SaveData) game_work.m_SaveData->ClearCombo();
    LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_RETRY_EXIT), "RetryGameCallback @ 0x001a5800");
    m_State = PAUSE_STATE_RETRY_EXIT;
}

// -------------------------------------------------------------------------
// vtable[10]: Update -- state machine + lazy button creation
// Binary: 0x001a5ebc (569 lines)
// Tier-1: SP path only (IsSameScreenMultiplayer() branch skipped)
// ASM-verified: 2026-07-14T21:34Z v1.6.1 PauseScreen::Update @ 0x001a5ebc (asm-inspector)
// -------------------------------------------------------------------------
void PauseScreen::Update(float dt) {
    // --- Lazy button creation (SP path only) ---
    // ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::Update @0x001a5ebc, range 0x001a5ec4..0x001a64e4 (re-analyst)
    // Each of the three create blocks is gated only on null-on-self
    // (`+0x98 m_ResumeButton`, `+0xa0 m_QuitButton`, `+0xac m_RetryButton`).
    // No `IsEnabled()` / `levelTransitionFlag` / `m_State` / `m_TransitionTimer` test
    // wraps the allocations — binary creates eagerly on first Update().
    // Visibility on non-gameplay screens is an alpha/draw-time concern,
    // handled by m_ButtonFadeAlpha -> m_DrawColour.a propagation below.
    // ASM-spec v1.6.1 PauseScreen::Update @0x001a5ee4..0x001a60d8 (re-analyst):
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
        m_ResumeButton->m_Texture = m_PauseButtonTex;
        m_ResumeButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
        m_ResumeButton->Init(
            _Vector3<float>(240.0f, -160.0f, 0.0f),  // initial pos; overwritten each frame
            Mortar::Delegate0<void>::Make(this, &PauseScreen::PauseGameCallback),
            /*fruitType=*/-1,             // toggle -- no fruit entity spawned
            _Vector3<float>(0.0f, 0.0f, 0.0f),       // globalCenterVec = HUD::g_GlobalCenterVec
            Mortar::Delegate0<void>()     // TODO: bind HUD::g_DeleteControlDelegate
        );

        game_work.mHud->AddControl(m_ResumeButton);
        m_ResumeButton->SetSingular();

        // ASM-verified post-Init override (v1.6.1 PauseScreen::Update @0x001a6060..0x001a60ac):
        //   m_TargetSize = (Vector3::One @ GOT+0x77CC) * 64.0 * 1.0 = (64,64,64)
        //   m_ButtonOriginPos := m_TargetSize  (one-shot capture for OX in
        //   the per-frame position formulas).
        m_ResumeButton->m_RestScale = _Vector3<float>(64.0f, 64.0f, 64.0f);
        m_ButtonOriginPos            = m_ResumeButton->m_RestScale;
    }

    // ASM-spec v1.6.1 PauseScreen::Update @0x001a5ebc: if (m_QuitButton==0) build BSButton.
    if (!m_QuitButton) {
        m_QuitButton = new BSButton(
            _Vector3<float>(215.0f, -135.0f, 0.0f),
            GETSTRING(LSTR_QUIT, 0),
            _Vector3<float>(1.0f, 1.0f, 1.0f)
        );
        m_QuitButton->Init();
        m_QuitButton->SetCallback(
            Mortar::Delegate0<void>::Make(this, &PauseScreen::QuitGameCallback));
        if (m_QuitButton->m_pLabelBox) {
            // ASM-verified: 2026-06-21T00:00:00Z v1.6.1 PauseScreen::Update @0x001a5ebc (re-analyst)
            // Colour ctor T_1056 @0x001a5710 writes m_R=arg, m_A=0xff, m_G=m_B=0 -- the
            // 0xff/0x40 vary RED, not alpha; alpha is always 0xff. Colour(r,g,b,a) is R,G,B,A order.
            //   top    = opaque red          (R=0xff,G=0,B=0,A=0xff)
            //   bottom = dark red, 1/4 red   (R=0x40,G=0,B=0,A=0xff)  -- still fully opaque
            m_QuitButton->m_pLabelBox->SetGradient(
                Colour(0xff, 0x00, 0x00, 0xff),
                Colour(0x40, 0x00, 0x00, 0xff),
                false);
            m_QuitButton->m_pLabelBox->ReshapeBounds(0x36, 0x14, 1, 0);
            m_QuitButton->m_pLabelBox->SetStroke(1.0f, Colour::Black);
            m_QuitButton->m_pLabelBox->SetFontSize(14.0f);
            m_QuitButton->m_pLabelBox->FitIntoVerticalBounds();
        }
        m_QuitButton->SetTexture(m_QuitTitleTex, true);
        m_QuitButton->SetDrawOrder(8);
        game_work.mHud->AddControl(m_QuitButton);
    }

    if (!m_RetryButton) {
        m_RetryButton = new MenuButton();
        m_RetryButton->m_Texture = m_RetryButtonTex;
        m_RetryButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
        m_RetryButton->Init(
            _Vector3<float>(0.0f, 320.0f, 0.0f),
            Mortar::Delegate0<void>::Make(this, &PauseScreen::RetryGameCallback),
            /*fruitType=*/-1,
            _Vector3<float>(0.0f, 0.0f, 0.0f),
            Mortar::Delegate0<void>()
        );

        game_work.mHud->AddControl(m_RetryButton);
        m_RetryButton->SetSingular();
    }

    // --- State machine ---
    switch (m_State) {

    case PAUSE_STATE_HIDDEN:
        // Alpha decay toward 0
#ifdef __bada__
        PS_DECAY_F(m_Alpha, FADE_DECAY);
#endif
        // Port: easing already advanced by UpdateRealtime() (per-present, dt-scaled);
        // this 60Hz tick only reads the current value to fire the threshold check.
        if (m_Alpha < FADE_CLAMP) m_Alpha = 0.0f;

        // Reveal timer countdown (Tier-2: gates Resume re-enable)
        m_RevealTimer -= dt;
        if (m_RevealTimer <= 0.0f) {
            m_RevealTimer = 0.0f;
            // Re-arm the Resume button. v1.6.1 PauseScreen::Update @0x001a6bc8..0x001a6bd0
            // unconditionally writes m_bAcceptsTouch(+0x149) = 1 here (re-analyst confirmed
            // no = 0 write to +0x149 exists anywhere in PauseScreen::Update).
            if (m_ResumeButton) m_ResumeButton->m_bAcceptsTouch = 1;
        }
        break;

    case PAUSE_STATE_BOMB_FLASH:
        // ASM-spec v1.6.1 PauseScreen::Update @ 0x001a5ebc: case 1.
        // Hold m_Alpha = 1.0 / m_ButtonFadeAlpha = 0.0 each frame while
        // BombFlashFull() returns false (i.e. game_work.m_BombHitTimer >= 1.0).
        // When the bomb-hit timer crosses below 1.0 (half the 2.0s window),
        // set m_Alpha = 0 / m_ButtonFadeAlpha = 1, reset PowerUpManager,
        // drop to HIDDEN, and pull the pause amount to -1.0 so the
        // slide-back-to-menu animation kicks in via MainScreen.
        m_ButtonFadeAlpha = 0.0f;
        m_Alpha           = 1.0f;
        if (BombFlashFull()) {
            m_Alpha           = 0.0f;
            m_ButtonFadeAlpha = 1.0f;
            PowerUpManager::GetInstance()->Reset(false);
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_HIDDEN), "Update/BOMB_FLASH complete");
            m_State = PAUSE_STATE_HIDDEN;
            game_work.m_PauseAmount = -1.0f;
        }
        break;

    case PAUSE_STATE_FADE_IN:
#ifdef __bada__
        PS_APPROACH_F(m_Alpha, 1.0f, FADE_IN_RATE);
#endif
        // Port: easing already advanced by UpdateRealtime(); read current value.

        // Force game pause flag each frame while fading in (SP path only)
        game_work.bM_Mode = true;

        if (m_Alpha > ACTIVE_THRESHOLD) {
            m_Alpha = 1.0f;
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_ACTIVE), "Update/FADE_IN alpha settled");
            m_State = PAUSE_STATE_ACTIVE;
        }
        break;

    case PAUSE_STATE_ACTIVE:
        // Force game pause flag each frame (SP path only)
        game_work.bM_Mode = true;

        // Enable hit detection on Resume and Retry
        if (m_ResumeButton) m_ResumeButton->m_bAcceptsTouch = 1;
        if (m_RetryButton)  m_RetryButton->m_bAcceptsTouch  = 1;
        break;

    case PAUSE_STATE_RESUME_EXIT:
#ifdef __bada__
        PS_DECAY_F(m_Alpha, FADE_DECAY);
#endif
        // Port: easing already advanced by UpdateRealtime(); read current value.
        if (m_Alpha < EXIT_THRESHOLD) {
            m_Alpha = 0.0f;
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_HIDDEN), "Update/RESUME_EXIT faded");
            m_State = PAUSE_STATE_HIDDEN;
            m_RevealTimer = 2.0f;
            UnpauseGame();
            // bM_Mode cleared by GameDraw tail @0x001cdd64 when g_unpause_game fires,
            // NOT here. Removing the old port-specific clear restores binary behaviour.
        }
        break;

    case PAUSE_STATE_RETRY_EXIT:
#ifdef __bada__
        PS_DECAY_F(m_Alpha, FADE_DECAY);
#endif
        // Port: easing already advanced by UpdateRealtime(); read current value.
        if (m_Alpha < EXIT_THRESHOLD) {
            // v1.6.1 PauseScreen::Update @0x001a6d0c: SaveCurrentData(false) before RetryLevel.
            SaveCurrentData(false);
            m_Alpha = 0.0f;
            m_ButtonFadeAlpha = 0.0f;
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_HIDDEN), "Update/RETRY_EXIT faded");
            m_State = PAUSE_STATE_HIDDEN;
            m_RevealTimer = 2.0f;
            // Binary calls RetryLevel (0x0016b008), NOT EndRetryLevel (0x0016a208).
            // RetryLevel sets retryFlag=1 + retryTimer=0.1f; GameUpdate's retry
            // dispatch tail then calls RetryUpdate per frame and EndRetryLevel at 0.
            RetryLevel();
            UnpauseGame();
        }
        break;

    case PAUSE_STATE_QUIT_EXIT:
        // ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::Update @0x001a6c58..0x001a6c84 (re-analyst).
        // Binary applies 0.5x extra fast-fade in case 6 then falls through to
        // the common 0.75 decay; both happen each frame.
#ifdef __bada__
        PS_DECAY_F(m_Alpha, 0.5f);
        PS_DECAY_F(m_Alpha, FADE_DECAY);
#endif
        // Port: easing already advanced by UpdateRealtime() (both factors combined
        // into one 0.5*FADE_DECAY decay constant there); read current value.
        if (m_Alpha < EXIT_THRESHOLD) {
            LOG_INFO("SCREEN/PauseScreen", "%s (%s)", "QuitToMenu @ 0x001cb6e4", "QUIT_EXIT faded");
            QuitToMenu();
            // White-flash via HitMenuBomb at the hit button's pos. Index 0 is
            // the P1 quit button (m_QuitButton); index 1 would be P2 in MP.
            if (m_MenuBombIndex >= 0 && m_QuitButton) {
                HitMenuBomb(m_QuitButton->pos);
                LOG_INFO("BOMBHIT", "QuitToMenu fires HitMenuBomb at (%.1f,%.1f); bombHitTimer set to %.3f",
                         m_QuitButton->pos.x, m_QuitButton->pos.y,
                         game_work.m_BombHitTimer);
            }
            // Binary writes m_ButtonFadeAlpha = 1.0 (DAT_00154fb8), NOT 0.0.
            // Earlier port wrote 0.0 which left the buttons at full opacity
            // straight through the bomb-flash phase.
            m_ButtonFadeAlpha = 1.0f;
            m_MenuBombIndex   = -1;
            // Transition to BOMB_FLASH (1), NOT HIDDEN. The bomb-flash poll
            // in case 1 is what produces the visible white flash and tears
            // down the gameplay HUD; jumping straight to HIDDEN skipped both.
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_BOMB_FLASH), "Update/QUIT_EXIT faded");
            m_State = PAUSE_STATE_BOMB_FLASH;
            m_Alpha = 1.0f;
            SaveCurrentData(false);
            UnpauseGame();
        }
        break;

    default:
        break;
    }

    // --- Post-switch unconditional math (doc section 4 items 1-7) ---

    // 1. m_ButtonFadeAlpha decay / restore
    // IsEnabled() reads g_GameData fields (v1.6.1 PauseScreen::IsEnabled @0x001a5588).
    // v1.6.1 PauseScreen::Update @0x001a5ebc post-switch block: decay clamps to
    // exact 0.0 once the DECAYED value drops below 0.001 (EXIT_THRESHOLD,
    // DAT_00154fc0) -- NOT a <0 clamp. The exact 0.0 is load-bearing: it is
    // what releases PauseGameCallback's `m_ButtonFadeAlpha != 0.0f` debounce
    // (an exponential decay never reaches 0.0 on its own).
#ifdef __bada__
    const bool isEnabled = IsEnabled();
    if (isEnabled) {
        PS_DECAY_F(m_ButtonFadeAlpha, FADE_DECAY);
        if (m_ButtonFadeAlpha < EXIT_THRESHOLD) m_ButtonFadeAlpha = 0.0f;
    } else {
        PS_APPROACH_F(m_ButtonFadeAlpha, 1.0f, FADE_IN_RATE);
    }
#endif
    // Port: easing already advanced by UpdateRealtime() (per-present, dt-scaled,
    // same isEnabled gate re-evaluated there); this 60Hz tick reads the current value.

    // 2. State 6 scratch override (v1.6.1 PauseScreen::Update @0x001a6dac..0x001a6dc8 +
    // 0x001a71cc..0x001a71d0).
    // ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::Update @0x001a5ebc (re-analyst). Binary saves
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

    // Port specific: m_ResumeButton is the single pause/resume toggle icon --
    // idle (m_Alpha<=0.5) it's the in-gameplay pause icon (tap -> PauseGameCallback
    // state 0->2); once the overlay is active its texture swaps to Play and the
    // SAME control resumes (state 3->4). In the binary this icon sits off the
    // narrow 3:2 field edge whenever IsEnabled()==false (see MapX notes below) so
    // it never leaks on menu/shop/dojo; the opt-in widescreen field reveals it
    // there instead. Gate m_Active (visible+updates+touch) on MainScreen's
    // gameplay-resident state so the leak is suppressed without touching the
    // in-gameplay pause/resume behaviour: STATE_CAMERA_FADE holds throughout an
    // active PauseScreen overlay (pausing is PauseScreen-local, not a MainScreen
    // state change), so this never disables the button while gameplay is paused.
    if (m_ResumeButton) {
        m_ResumeButton->m_Active = (game_work.mMainScreen && game_work.mMainScreen->IsInGameplay()) ? 1 : 0;
    }

    // 4. Title slide-in: pos.x = 0, pos.y = 240 + sizeY + (-2 * m_Alpha)
    {
        const float sizeY = m_TitleSize.y;
        pos.x = 0.0f;
        pos.y = TITLE_SLIDE_BASE + sizeY + TITLE_SLIDE_MUL * m_Alpha;
    }

    // 5. Quit button (BSButton) per-frame position + active state.
    // ASM-spec v1.6.1 PauseScreen::Update @0x001a5ebc: BSButton per-frame block.
    if (m_QuitButton) {
        if (m_Alpha <= 0.01f) {
            m_QuitButton->SetActive(false);
        } else {
            int active = (m_PressIndex >= 2) ? 0 : (1 - m_PressIndex);
            m_QuitButton->SetActive(active != 0);
            m_QuitButton->m_DrawRotation.x = 0.0f;
            // DIFFERS: opt-in widescreen -- MapX re-anchors the corner X so the
            // Quit button tracks the widened left/right edge instead of leaking
            // into the newly-revealed field. Identity (215.0f) when disabled/__bada__.
            m_QuitButton->SetPosition(_Vector3<float>(MapX(215.0f, "pause.quit"), (1.0f - m_Alpha) * -40.0f - 135.0f, 0.0f));
        }
        m_QuitButton->SetTextOffset(_Vector3<float>(-29.0f, 3.0f, 0.0f));
    }

    // 6. Resume + Retry button position recomputation.
    // ASM-spec v1.6.1 PauseScreen::Update @ 0x001a5ebc: post-switch button layout tail.
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
    // Two-phase Resume + Retry layout:
    //   Phase 1 (lines below): write the off-screen base positions.
    //   Phase 2 (after the m_Alpha > 0 gate): lerp toward an on-screen
    //     to_pos by m_Alpha. WITHOUT the lerp the buttons stay at their
    //     base positions (off-screen left/right) for the entire pause
    //     overlay -- which is what the user observed visually.
    //
    // ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::Update @0x001a702c..0x001a705c (re-analyst)
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
    // Resume base (v1.6.1 PauseScreen::Update @0x001a5ebc):
    //   pos.x = -(OX * -0.375 + 240 + 4 + |fade|*(0.75*OX + 10))
    //          = -((244 - 0.375*OX) + |fade|*(10 + 0.75*OX))
    //   pos.y = 0.375*OX - 165   (y unchanged by phase-2 lerp)
    // DIFFERS: opt-in widescreen -- MapX re-anchors the off-screen-left base X.
    // When IsEnabled()==false (menu / not the active pause overlay), absFade
    // eases to 1.0 and this formula pushes the button to x=-270 -- just past
    // the original +-240 field edge (see IsEnabled() note above). Under the
    // widened field that -270 base sits INSIDE the new +-HalfWidth() extent,
    // re-revealing the resume/pause icon on the menu. MapX proportionally
    // rescales it back past the widened edge. Identity when disabled/__bada__.
    if (m_ResumeButton) {
        const float absFade = std::fabs(m_ButtonFadeAlpha);
        const float term1 = 244.0f - 0.375f * OX;
        const float term2 = absFade * (10.0f + 0.75f * OX);
        m_ResumeButton->pos.x = MapX(-(term1 + term2), "pause.resume");
        m_ResumeButton->pos.y = 0.375f * OX - 165.0f;
    }
    // Retry base (v1.6.1 PauseScreen::Update @0x001a6fa8..0x001a6fe4):
    //   pos = (240 + 0.5*OX, -20, 0)
    // DIFFERS: opt-in widescreen -- MapX re-anchors the right-edge base X.
    if (m_RetryButton) {
        m_RetryButton->pos = _Vector3<float>(MapX(240.0f + 0.5f * OX, "pause.retry"), -20.0f, 0.0f);
    }

    // ASM-verified: 2026-07-26T00:00Z v1.6.1 PauseScreen::Update @0x001a7078..0x001a7090 (re-analyst)
    // (gate spans 0x001a7060..0x001a71a0)
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
    // v1.6.1 PauseScreen::Update @0x001a7094..0x001a7198:
    //   button.pos += (to_pos - button.pos) * m_Alpha
    //   Resume target = (-OX, -20, 0) -- inside-left
    //   Retry  target = (+OX, -20, 0) -- inside-right
    // (Online-MP path substitutes (0,0,0) for both -- defunct.)
    if (m_Alpha > 0.0f) {
        if (m_ResumeButton) {
            const _Vector3<float> target(-OX, -20.0f, 0.0f);
            m_ResumeButton->pos += (target - m_ResumeButton->pos) * m_Alpha;
        }
        if (m_RetryButton) {
            const _Vector3<float> target(OX, -20.0f, 0.0f);
            m_RetryButton->pos += (target - m_RetryButton->pos) * m_Alpha;
        }
    }

    // 7. P2 buttons inactive (Tier-2 stub -- P2 buttons are nullptr in Tier-1)
    // m_P2ResumeButton / m_P2RetryButton are always nullptr in Tier-1.

    // Restore the persistent fade state -- the state-6 reset above was a
    // scratch override for rendering only. v1.6.1 PauseScreen::Update @0x001a71cc..0x001a71d0.
    m_Alpha           = savedAlpha;
    m_ButtonFadeAlpha = savedButtonFadeAlpha;
}

#ifndef __bada__
// ---------------------------------------------------------------------------
// Port specific: no binary counterpart -- see HUDControl::UpdateRealtime and
// the state-machine split comment above Update(). Advances m_Alpha (states
// HIDDEN/FADE_IN/RESUME_EXIT/RETRY_EXIT/QUIT_EXIT) and m_ButtonFadeAlpha (the
// unconditional isEnabled-gated ramp, mirrored from Update()'s post-switch
// block 1) per PRESENTED frame, dt-scaled via PS_APPROACH_F/PS_DECAY_F
// (defined near the top of this file). Update() (60Hz) reads the resulting
// values to fire the (already rate-independent, threshold-based) state
// transitions and one-shot side effects -- those stay in Update() exactly
// like ShopScreen keeps its state-transition side effects in Update() rather
// than UpdateRealtime().
//
// PAUSE_STATE_BOMB_FLASH and PAUSE_STATE_ACTIVE are excluded: both hard-set
// m_Alpha (unconditional assignment, not an approach/decay ramp) rather than
// easing it, so there is nothing here to dt-scale for those states.
//
// Under __bada__ this function does not exist (see PauseScreen.h); Update()
// eases m_Alpha/m_ButtonFadeAlpha inline per-state, byte-identical to the binary.
//
// DIFFERS: v1.6.1 PauseScreen::Update @0x001a5ebc eases the fade per 60Hz sim tick;
// port eases per rendered frame (dt-scaled) to track display refresh. __bada__
// keeps the faithful 60Hz path (macros expand to the original scalar ops).
// ---------------------------------------------------------------------------
void PauseScreen::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches
    const float f = dtSeconds * 60.0f;

    switch (m_State) {
    case PAUSE_STATE_HIDDEN:
        // Binary: m_Alpha *= 0.75 (FADE_DECAY)
        PS_DECAY_F(m_Alpha, FADE_DECAY);
        break;
    case PAUSE_STATE_FADE_IN:
        // Binary: m_Alpha += (1 - m_Alpha) * 0.25 (FADE_IN_RATE)
        PS_APPROACH_F(m_Alpha, 1.0f, FADE_IN_RATE);
        break;
    case PAUSE_STATE_RESUME_EXIT:
    case PAUSE_STATE_RETRY_EXIT:
        // Binary: m_Alpha *= 0.75 (FADE_DECAY)
        PS_DECAY_F(m_Alpha, FADE_DECAY);
        break;
    case PAUSE_STATE_QUIT_EXIT:
        // Binary: m_Alpha *= 0.5; m_Alpha *= 0.75 -- combine into one 0.375 decay
        // per tick so f-scaling (powf) is applied once, not compounded twice.
        PS_DECAY_F(m_Alpha, 0.5f * FADE_DECAY);
        break;
    default:
        // PAUSE_STATE_BOMB_FLASH / PAUSE_STATE_ACTIVE: no alpha easing (hard-set
        // assignments only); default (safety): no other states exist.
        break;
    }

    // Post-switch unconditional m_ButtonFadeAlpha ramp (mirrors Update() block 1).
    // Binary clamps the decayed value to exact 0.0 below 0.001 (EXIT_THRESHOLD,
    // v1.6.1 PauseScreen::Update @0x001a5ebc) -- required so PauseGameCallback's
    // `!= 0.0f` debounce releases; a <0 clamp never fires on exponential decay.
    if (IsEnabled()) {
        PS_DECAY_F(m_ButtonFadeAlpha, FADE_DECAY);
        if (m_ButtonFadeAlpha < EXIT_THRESHOLD) m_ButtonFadeAlpha = 0.0f;
    } else {
        PS_APPROACH_F(m_ButtonFadeAlpha, 1.0f, FADE_IN_RATE);
    }
}
#endif

// ASM-spec v1.6.1 PauseScreen::SkipTo @0x001a5568
// Binary body is exactly `m_State = 3; m_Alpha = 1.0f;`.
// External entry — force overlay fully visible and
// jump to state 3. Used by the Bada-app-side "skip intro" handler.
void PauseScreen::SkipTo() {
    m_State = PAUSE_STATE_ACTIVE;
    m_Alpha = 1.0f;
}

// ASM-spec v1.6.1 PauseScreen::GetTime @0x001d00ec
// Binary asm: ldr r3,[r0,#0xd8]; cmp r3,#6; vldrne s0,[r0,#0x7c]; vmoveq.f32 s0,1.0f; bx lr
float PauseScreen::GetTime() {
    if (m_State == PAUSE_STATE_QUIT_EXIT) return 1.0f;
    return m_Alpha;
}

// ASM-spec v1.6.1 PauseScreen::ContinueGameCallback @0x001a56b8
// Binary body:
//   if (m_State != 3) return;
//   m_State += 1;
//   if (game_work.m_bResumeSnapshotPresent /* +0x89 */)
//       T.1054(game_work.m_FrameTimer /* +0x19c */);   // = Math::Random::Seed
//   game_work.m_bResumeSnapshotPresent = 0;
// External entry (no in-screen button binds it).
// Likely call site: shop/tutorial popup-dismiss handler.
void PauseScreen::ContinueGameCallback() {
    if (m_State != PAUSE_STATE_ACTIVE) return;
    // Binary does `m_State += 1`; equivalent to this assignment under the `== 3` guard.
    m_State = PAUSE_STATE_RESUME_EXIT;
    if (game_work.m_bResumeSnapshotPresent != 0) {
        Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
    }
    game_work.m_bResumeSnapshotPresent = 0;
}
