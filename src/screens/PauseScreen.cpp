// Analysed: 2026-05-02T00:00
//
// PauseScreen.
// Binary: PauseScreen::PauseScreen @ 0x001a7204 (ctor), Update @ 0x001a5ebc.
//
// States 0/1/2/3/4/5/6, three P1 buttons, plus the P2 create arm.
//
// The P2 buttons (+0x9c / +0xa4 / +0xb0) live behind IsSameScreenMultiplayer(), which is a
// hard FALSE in v1.6.1 (::IsMultiplayer @0x0011a094 is `mov r0,#0 / bx lr`). Their create
// blocks are ported for call-graph parity per stub-don't-skip, but never run -- nothing is
// allocated, added to the HUD, or drawn. See the Update() header comment for detail.

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
// ASM-spec v1.6.1 PauseScreen::Reset @0x001a58ac
//
// Two if-blocks, no other PauseScreen field is touched:
//   if (m_RetryButton) {              // ldr r0,[r0,#0xac]
//       retry->m_bAcceptsTouch = 1;   // strb r3,[r0,#0x149]   @0x001a58c8
//       retry->m_Texture = *(this+0xc4);   // add r1,r4,#0xc4  @0x001a58c4
//   }
//   if (m_ResumeButton)               // ldr r0,[r4,#0x98]
//       resume->m_Texture = *(this+0xb8);  // add r1,r4,#0xb8  @0x001a58e4
//
// The source slot is +0xb8, NOT +0xbc. The ctor's LoadLocalisedTexture string
// literals pin the slots: +0x74 "pause_title.tex", +0xb8 "pause_button.tex",
// +0xc4 "retry_button.tex", +0xbc "play_button.tex", +0xc0 "quit_title.tex"
// (v1.6.1 PauseScreen::PauseScreen @0x001a7204, GOT base 0x002d1130, string
// pool 0x002831e4..0x00283226). An earlier marker here claimed +0xbc
// (m_PlayButtonTex) and was wrong.
//
// Inverse of SetToMultiplayerState: re-enables RetryButton and restores the
// SP texture assignments. Update() re-picks the resume icon every frame off
// m_Alpha, so this assignment only holds until the next Update.
// -------------------------------------------------------------------------
void PauseScreen::Reset() {
    if (m_RetryButton) {
        m_RetryButton->m_bAcceptsTouch = 1;
        m_RetryButton->m_Texture = m_RetryButtonTex;
    }
    if (m_ResumeButton) {
        m_ResumeButton->m_Texture = m_PauseButtonTex;
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

    // v1.6.1 PauseScreen::DrawOrder @0x001a572c: the title quad is skipped in online-MP.
    // ::IsOnlineMultiplayer @0x0011a09c is `mov r0,#0 / bx lr` in v1.6.1, so the draw always
    // happens -- the call restores shape only.
    if (m_Alpha > 0.0f && !IsOnlineMultiplayer()) {
        HUDControl3d::Draw(hudScaleRaw);
    }

    if (m_Alpha > 0.0f && m_PausedText) {
        m_PausedText->SetTranslation(this->pos, 1);
        m_PausedText->Draw(_Vector2<float>(1.0f, 1.0f), 0.0f, 1);
    }
}

// -------------------------------------------------------------------------
// vtable[11]: SetToMultiplayerState
// ASM-spec v1.6.1 PauseScreen::SetToMultiplayerState @0x001a5e74
//
// Defunct: multiplayer HUD switch -- unreachable in v1.6.1; v1.6.1
// PauseScreen::SetToMultiplayerState @ 0x001a5e74. Ported faithfully anyway
// (the body is three stores and costs nothing), per stub-don't-skip.
//
// Why it never runs -- the whole chain above it is dead:
//   * slot 11 (vtable +0x2c) has exactly ONE dispatcher in the program:
//     HUD::SetToMultiplayerState @0x0018c510 (`ldr r3,[r3,#0x2c]`). Verified
//     by a program-wide instruction scan (383228 instructions, .plt 0x00102964
//     + .text 0x00116d18..0x0027f4cf -- the "OspMain" label at 0x000e3328 is
//     bogus, that address is inside .gnu.version).
//   * HUD::SetToMultiplayerState @0x0018c4d8 has ONE caller: the PLT thunk
//     @0x00110524, called only from Game::TellGameToStart @0x001206e8.
//   * Game::TellGameToStart @0x001206c8 has NO code xrefs. It sits in the Game
//     vtable slot 13 (@0x002cc24c) and MortarGame slot 13 (@0x002cfac4), and
//     nothing anywhere loads a MortarGame vptr +0x34. GameInit @0x001ce1c0
//     (the game-start path) does not call it either.
//   * Corroboration: this function derefs m_RetryButton and m_ResumeButton with
//     NO null test (unlike Reset, which tests both). The ctor @0x001a7204 nulls
//     both and Update creates them lazily, so a call at game start would store
//     to address 0x149 and fault. It cannot be on a live path.
//
// Body (3 stores, in binary order), no null guards -- matches the binary:
//   1. SmartPtr<Texture>::operator=(m_RetryButton+0x74, NULL)  @0x001a5e88
//      (via T.1065 @0x001a5bb4 = `mov r1,#0; b 0x00104fb0`)
//   2. m_RetryButton->m_bAcceptsTouch = 0                      @0x001a5e98
//   3. SmartPtr<Texture>::operator=(m_ResumeButton+0x74, this+0xc0)  @0x001a5ea0
//      +0xc0 is m_QuitTitleTex ("quit_title.tex"), not m_RetryButtonTex.
// Returns 0. It does NOT chain to HUDControl::SetToMultiplayerState -- calling
// the base would report "not singular" and let HUD::SetToMultiplayerState
// remove the PauseScreen from the control list.
// -------------------------------------------------------------------------
bool PauseScreen::SetToMultiplayerState() {
    m_RetryButton->m_Texture.SetNull();
    m_RetryButton->m_bAcceptsTouch = 0;
    m_ResumeButton->m_Texture = m_QuitTitleTex;
    return false;
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
//
// ASM-spec v1.6.1 PauseScreen::Update @0x001a5ebc..0x001a71dc (2026-08-02, direct
//   disassembly + decompile read). Restores the call shape the 2026-07-31 audit measured
//   as missing (~149 of the binary's ~198 call edges). What landed:
//
//   1. The IsSameScreenMultiplayer() arm @0x001a64e8..0x001a6af8 (~65 calls) is now
//      present as a faithful gated arm, not omitted. REACHABILITY: v1.6.1
//      ::IsMultiplayer @0x0011a094 is literally `mov r0,#0 / bx lr`, and
//      ::IsSameScreenMultiplayer @0x0011a0a4 is `IsMultiplayer() && !IsOnlineMultiplayer()`,
//      so the gate is a compile-time-dead FALSE in this build -- the three P2 create
//      blocks never run. They are ported anyway per stub-don't-skip: same shape, same
//      call graph, zero runtime effect (nothing is instantiated, nothing is drawn).
//   2. IsOnlineMultiplayer() @0x00103d9c is now called at all five sites:
//        @0x001a6bf0 (FADE_IN)  / @0x001a6c40 (ACTIVE) / @0x001a6d48 (exit states, alpha
//        still >= EXIT_THRESHOLD) -- each gates `game_work.bM_Mode = 1`
//        @0x001a6df8 (resume-icon texture pick, m_Alpha <= 0.5 arm) -- online picks
//                     m_QuitTitleTex (+0xc0) instead of m_PauseButtonTex (+0xb8)
//        @0x001a7094 (phase-2 lerp) -- online substitutes (0,0,0) for the RESUME target
//                     only; the RETRY target is NOT online-gated.
//      ::IsOnlineMultiplayer @0x0011a09c is also `mov r0,#0 / bx lr`, so every offline
//      arm stays live -- the calls restore shape without changing behaviour.
//   3. MenuButton construction now goes through the value-ctor (bl 0x0010647c) +
//      vptr[8] (= slot 2, MenuButton::Init(), a no-op Reset()), not default-ctor+Init(5).
//      That restores the per-button temporary chain: SmartPtr copy, 2x _Vector3,
//      QCallee<PauseScreen>, 2x Delegate0, T.1063 (Global<Delegate0> deleted-callback
//      accessor @0x001a58f0), then ~Delegate0 x2 / ~Global / ~Callee / ~SmartPtr.
//   4. Resume m_RestScale is built as `_Vector3<float>::One() * 64.0f * 1.0f` (two
//      operator* calls @0x001a6074 / @0x001a608c). Retry keeps the third, dead
//      operator* @0x001a64c4 (result never read).
//   5. P2 deactivation at the tail + Resume/Retry activation now go through
//      HUDControl::SetActive (bl 0x0010b2c8) instead of writing m_Active.
//   6. The phase-2 slide targets come from the function-local static Vec3 the binary
//      builds under __cxa_guard_acquire/_release + __aeabi_atexit @0x001a6f40..0x001a6fa4:
//      retryButtonStart = (OX, -0.375*OX - 160.0, 0). Only .x is ever read.
//   7. FIELD FIX: the binary zeroes m_Timer (+0x2c) on the RESUME and RETRY buttons
//      @0x001a6e38..0x001a6e3c. The port previously zeroed m_DrawRotation.x on the QUIT
//      button -- a different field on a different object. The quit button's own reset
//      (`vstr s16,[r7,#0xb0]` @0x001a6e88) is separate and stays where it is.
//   8. resume->m_NewBouncePhase (+0x170) = 500.0f @0x001a60b0 (literal @0x001a6318 =
//      0x43fa0000; the old TODO said 1.0f -- wrong), resume->m_bAcceptsTouch (+0x149) =
//      ((unsigned)(m_State - 2) <= 1), resume->m_bBackdropActive (+0x150) = 1.
//   9. Case 6 writes m_ButtonFadeAlpha = 0.0f, not 1.0f: `vldr s15,[pc,#-0x250]` at
//      0x001a6cd4 resolves to the literal at 0x001a6a8c, which is 0x00000000.
//
//   Still deferred (single call edge each):
//   - TODO: v1.6.1 0x001a7000 (PauseScreen::Update) -- Math::Abs<float>(m_ButtonFadeAlpha)
//     @0x00114b38 is a real call; the port uses std::fabs. Adding the template belongs in
//     src/engine/math/MathUtil.h (Math::Min/Max live there), which is outside this file.
//   - TODO: v1.6.1 0x001a58f0 (T.1063, PauseScreen::Update) -- the Global<Delegate0>
//     deleted-callback accessor. The port passes a default-constructed Delegate0<void>.
//
//   Constants re-confirmed from the pool: 0x001a71ec = 160.0f (Resume pos.y base),
//   0x001a71f4 = 240.0f (Retry pos.x base), 0x001a71e8 = -40.0f, 0x001a6318 = 500.0f.
// -------------------------------------------------------------------------
void PauseScreen::Update(float dt) {
    // --- Lazy button creation (P1 path) ---
    // ASM-spec v1.6.1 PauseScreen::Update @0x001a5ec4..0x001a64e4 (2026-07-31, direct
    // disassembly read; downgraded from ASM-verified -- re-analyst provenance, and the blocks
    // are missing the ctor-arg chain / +0x170 / +0x149 / +0x150 writes listed above).
    // The claim it made IS confirmed: each of the three create blocks is gated only on
    // null-on-self
    // (`+0x98 m_ResumeButton`, `+0xa0 m_QuitButton`, `+0xac m_RetryButton`).
    // No `IsEnabled()` / `levelTransitionFlag` / `m_State` / `m_TransitionTimer` test
    // wraps the allocations — binary creates eagerly on first Update().
    // Visibility on non-gameplay screens is an alpha/draw-time concern,
    // handled by m_ButtonFadeAlpha -> m_DrawColour.a propagation below.
    // For PauseScreen toggles fruitType is hard-coded -1 (no fruit entity spawned);
    // hitBounds is _Vector3<float>::Zero; deletedCb is the T.1063 global (HUD-side cleanup
    // when the button is removed -- left default-constructed until that global is exposed).
    //
    // ASM-spec v1.6.1 PauseScreen::Update @0x001a5ee4..0x001a60d8: P1 Resume create block.
    // Binary sequence, in order:
    //   SmartPtr<Texture> copy of m_PauseButtonTex (+0xb8)
    //   _Vector3(240, -160, 0)                                  -- initial pos
    //   QCallee<PauseScreen>(PauseGameCallback) -> Delegate0
    //   _Vector3(_Vector3<float>::Zero)                         -- hitBounds
    //   T.1063 -> Global<Delegate0> -> Delegate0                -- deleted-callback
    //   operator new(0x178); MenuButton(tex, pos, clickCb, -1, hitBounds, deletedCb)
    //   4 temporary dtors + ~SmartPtr
    //   vptr[8] (= vtable slot 2 = MenuButton::Init(), a no-op Reset())
    //   HUD::AddControl(hud, resume, false)                     @0x001a6058
    //   resume->m_LayerFlags (+0x34) = 0x100                    @0x001a6060
    //   resume->m_RestScale (+0x13c) = One * 64.0 * 1.0         @0x001a6074 / 0x001a608c
    //   m_ButtonOriginPos (+0x8c) := resume->m_RestScale        @0x001a60a4..0x001a60ac
    //   resume->m_NewBouncePhase (+0x170) = 500.0f              @0x001a60b0
    //   resume->m_bAcceptsTouch (+0x149) = ((unsigned)(m_State - 2) <= 1)  @0x001a60cc
    //   resume->m_bBackdropActive (+0x150) = 1                  @0x001a60d4
    //   resume->SetSingular()                                   @0x001a60d8
    if (!m_ResumeButton) {
        m_ResumeButton = new MenuButton(
            m_PauseButtonTex,
            _Vector3<float>(240.0f, -160.0f, 0.0f),  // initial pos; overwritten each frame
            Mortar::Delegate0<void>::Make(this, &PauseScreen::PauseGameCallback),
            /*fruitType=*/-1,                        // toggle -- no fruit entity spawned
            _Vector3<float>::Zero(),                 // hitBounds = _Vector3<float>::Zero
            // TODO: v1.6.1 0x001a58f0 (T.1063) -- bind the Global<Delegate0>
            //   deleted-callback accessor once it is exposed port-side.
            Mortar::Delegate0<void>());
        m_ResumeButton->Init();                      // vptr[8] == vtable slot 2

        game_work.mHud->AddControl(m_ResumeButton);
        m_ResumeButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
        m_ResumeButton->m_RestScale  = _Vector3<float>::One() * 64.0f * 1.0f;
        m_ButtonOriginPos            = m_ResumeButton->m_RestScale;
        m_ResumeButton->m_NewBouncePhase  = 500.0f;   // +0x170, literal @0x001a6318
        m_ResumeButton->m_bAcceptsTouch   =
            (uint8_t)(((unsigned int)(m_State - 2) <= 1u) ? 1 : 0);  // 1 only in FADE_IN(2)/ACTIVE(3)
        m_ResumeButton->m_bBackdropActive = 1;       // +0x150
        m_ResumeButton->SetSingular();
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
        // No m_pLabelBox null guard: @0x001a61f4 the binary does `ldr r8,[r3,#0x84]` and
        // feeds r8 straight to SetGradient; every later use re-loads [quit+0x84] the same
        // way (@0x001a623c, 0x001a6250, 0x001a6284, 0x001a6290). No cmp anywhere.
        // Port-added guard removed.
        {
            // ASM-spec v1.6.1 PauseScreen::Update @0x001a61e4..0x001a6218 (2026-07-31, direct
            // disassembly read; downgraded from ASM-verified -- re-analyst provenance, and the
            // block carried the port-added m_pLabelBox guard removed above).
            // Claim CONFIRMED: T.1056 @0x001a5710 is a 1-byte Colour helper --
            //   mvn r2,#0 / strb r1,[r0,#0x2] / strb r2,[r0,#0x3] / add r2,r2,#1 /
            //   strb r2,[r0,#0x1] / strb r2,[r0,#0x0]
            // i.e. B=0, G=0, R=arg, A=0xff on the b,g,r,a byte layout. The 0xff/0x40 vary
            // RED, not alpha; alpha is always 0xff. Colour(r,g,b,a) is R,G,B,A order.
            //   top    = opaque red          (R=0xff,G=0,B=0,A=0xff)  @0x001a61f8
            //   bottom = dark red, 1/4 red   (R=0x40,G=0,B=0,A=0xff)  @0x001a6204
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

    // ASM-spec v1.6.1 PauseScreen::Update @0x001a6330..0x001a64e4: P1 Retry create block.
    // Same ctor shape as Resume. Tail order differs from Resume: SetSingular BEFORE
    // AddControl, no m_ButtonOriginPos capture, no +0x170 / +0x149 / +0x150 writes, and
    // one DEAD `m_RestScale * -1.0f` @0x001a64c4 whose result is never read.
    if (!m_RetryButton) {
        m_RetryButton = new MenuButton(
            m_RetryButtonTex,
            _Vector3<float>(0.0f, 320.0f, 0.0f),
            Mortar::Delegate0<void>::Make(this, &PauseScreen::RetryGameCallback),
            /*fruitType=*/-1,
            _Vector3<float>::Zero(),
            Mortar::Delegate0<void>());
        m_RetryButton->Init();                       // vptr[8] == vtable slot 2

        m_RetryButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
        // Dead in the binary too -- the product at sp+0x2f0 is never read. Kept for shape.
        (void)(m_RetryButton->m_RestScale * -1.0f);  // @0x001a64c4
        m_RetryButton->SetSingular();
        game_work.mHud->AddControl(m_RetryButton);
    }

    // Defunct: same-screen multiplayer -- v1.6.1 PauseScreen::Update @0x001a64e8..0x001a6af8.
    // `bl IsSameScreenMultiplayer @0x001070b8 / cmp r0,#0 / beq 0x001a6afc` guards three more
    // create blocks that mirror the P1 ones. ::IsMultiplayer @0x0011a094 is `mov r0,#0 / bx lr`
    // in v1.6.1, so IsSameScreenMultiplayer() @0x0011a0a4 is a hard FALSE and this arm never
    // runs -- no P2 button is ever allocated, added to the HUD, or drawn. Ported as a faithful
    // gated arm per stub-don't-skip so the call graph matches the binary.
    if (IsSameScreenMultiplayer()) {
        // P2 Resume (+0x9c) @0x001a64f4. Callback is PauseGameCallback2 (verified: the only
        // xref to @0x001a5b38 inside Update is @0x001a6538). Tail matches P1 Resume EXCEPT
        // there is no +0x150 write (@0x001a66e8 goes straight from +0x149 to SetSingular).
        if (!m_P2ResumeButton) {
            m_P2ResumeButton = new MenuButton(
                m_PauseButtonTex,
                _Vector3<float>(240.0f, -160.0f, 0.0f),
                Mortar::Delegate0<void>::Make(this, &PauseScreen::PauseGameCallback2),
                /*fruitType=*/-1,
                _Vector3<float>::Zero(),
                Mortar::Delegate0<void>());
            m_P2ResumeButton->Init();

            game_work.mHud->AddControl(m_P2ResumeButton);
            m_P2ResumeButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
            m_P2ResumeButton->m_RestScale  = _Vector3<float>::One() * 64.0f * 1.0f;
            m_ButtonOriginPos              = m_P2ResumeButton->m_RestScale;
            m_P2ResumeButton->m_NewBouncePhase = 500.0f;          // +0x170 @0x001a66cc
            m_P2ResumeButton->m_bAcceptsTouch  =
                (uint8_t)(((unsigned int)(m_State - 2) <= 1u) ? 1 : 0);  // +0x149 @0x001a66e8
            m_P2ResumeButton->SetSingular();
        }

        // P2 Quit (+0xa4) @0x001a66f0 -- a BSButton, byte-for-byte the same build as the P1
        // quit button, INCLUDING the callback: the binary binds QuitGameCallback @0x001a55e0,
        // not QuitGameCallback2 (@0x001a5634 has zero xrefs from Update).
        if (!m_P2QuitButton) {
            m_P2QuitButton = new BSButton(
                _Vector3<float>(215.0f, -135.0f, 0.0f),
                GETSTRING(LSTR_QUIT, 0),
                _Vector3<float>(1.0f, 1.0f, 1.0f));
            m_P2QuitButton->Init();
            m_P2QuitButton->SetCallback(
                Mortar::Delegate0<void>::Make(this, &PauseScreen::QuitGameCallback));
            m_P2QuitButton->m_pLabelBox->SetGradient(
                Colour(0xff, 0x00, 0x00, 0xff),
                Colour(0x40, 0x00, 0x00, 0xff),
                false);
            m_P2QuitButton->m_pLabelBox->ReshapeBounds(0x36, 0x14, 1, 0);
            m_P2QuitButton->m_pLabelBox->SetStroke(1.0f, Colour::Black);
            m_P2QuitButton->m_pLabelBox->SetFontSize(14.0f);
            m_P2QuitButton->m_pLabelBox->FitIntoVerticalBounds();
            m_P2QuitButton->SetTexture(m_QuitTitleTex, true);
            m_P2QuitButton->SetDrawOrder(8);
            game_work.mHud->AddControl(m_P2QuitButton);
        }

        // P2 Retry (+0xb0) @0x001a6928. Callback is RetryGameCallback (shared with P1).
        if (!m_P2RetryButton) {
            m_P2RetryButton = new MenuButton(
                m_RetryButtonTex,
                _Vector3<float>(0.0f, 320.0f, 0.0f),
                Mortar::Delegate0<void>::Make(this, &PauseScreen::RetryGameCallback),
                /*fruitType=*/-1,
                _Vector3<float>::Zero(),
                Mortar::Delegate0<void>());
            m_P2RetryButton->Init();

            m_P2RetryButton->m_LayerFlags = Mortar::HUD_LAYER_P2_SCORE;
            (void)(m_P2RetryButton->m_RestScale * -1.0f);   // dead, mirrors @0x001a64c4
            m_P2RetryButton->SetSingular();
            game_work.mHud->AddControl(m_P2RetryButton);
        }
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

        // Reveal timer countdown (gates Resume re-enable).
        // ASM-spec v1.6.1 PauseScreen::Update @0x001a6ba4..0x001a6bd0: the binary only
        // subtracts dt while the timer is still positive; once it is <= 0 it clamps to
        // exact 0.0 and falls through to the re-arm. It never lets the timer go negative.
        if (m_RevealTimer <= 0.0f) {
            m_RevealTimer = 0.0f;
        } else {
            m_RevealTimer -= dt;
            if (m_RevealTimer > 0.0f) break;
        }
        {
            // Re-arm the Resume button. v1.6.1 PauseScreen::Update @0x001a6bc8..0x001a6bd0
            // unconditionally writes m_bAcceptsTouch(+0x149) = 1 here.
            // Re-read 2026-07-31 @0x001a6bc8: `ldr r3,[r4,#0x98] / mov r2,#1 /
            // strb r2,[r3,#0x149]` -- no cmp. Port-added null guard removed.
            // (Correction to the old note: a `= 0` write to +0x149 DOES exist, in the
            // Resume-button create block @0x001a60cc, where it is set to
            // `(m_State - 2) <= 1`, i.e. 1 only in FADE_IN/ACTIVE. The port omits it.)
            m_ResumeButton->m_bAcceptsTouch = 1;
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

        // Force game pause flag each frame while fading in.
        // v1.6.1 PauseScreen::Update @0x001a6bf0: `bl IsOnlineMultiplayer @0x00103d9c /
        // cmp r0,#0 / strbeq #1 -> game_work+0x2`. IsOnlineMultiplayer() is a hard false
        // in v1.6.1, so the write always happens -- the call restores shape only.
        if (!IsOnlineMultiplayer()) {
            game_work.bM_Mode = true;
        }

        if (m_Alpha > ACTIVE_THRESHOLD) {
            m_Alpha = 1.0f;
            LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_ACTIVE), "Update/FADE_IN alpha settled");
            m_State = PAUSE_STATE_ACTIVE;
        }
        break;

    case PAUSE_STATE_ACTIVE:
        // Enable hit detection on Resume and Retry.
        // Binary order @0x001a6c2c..0x001a6c50 is Retry first, then Resume, then the
        // pause-flag write -- and neither pointer is null-tested
        // (`ldr r3,[r4,#0xac] / strb r6,[r3,#0x149]`, `ldr r3,[r4,#0x98] / strb r6,...`).
        // Port-added null guards removed.
        m_RetryButton->m_bAcceptsTouch  = 1;
        m_ResumeButton->m_bAcceptsTouch = 1;

        // Force game pause flag each frame.
        // v1.6.1 PauseScreen::Update @0x001a6c40: same IsOnlineMultiplayer() gate as FADE_IN.
        if (!IsOnlineMultiplayer()) {
            game_work.bM_Mode = true;
        }
        break;

    // ASM-spec v1.6.1 PauseScreen::Update @0x001a6c58..0x001a6d5c: the three exit states
    // share one block. The switch dispatches case 6 to 0x001a6c58, which applies an EXTRA
    // `m_Alpha *= 0.5` and then FALLS THROUGH into 0x001a6c68 where cases 4 and 5 also land
    // (`m_Alpha *= 0.75`). Case order below mirrors that fallthrough, so 6 precedes 4/5.
    case PAUSE_STATE_QUIT_EXIT:
#ifdef __bada__
        PS_DECAY_F(m_Alpha, 0.5f);
#endif
        // fallthrough into the shared exit block @0x001a6c68
    case PAUSE_STATE_RESUME_EXIT:
    case PAUSE_STATE_RETRY_EXIT:
#ifdef __bada__
        PS_DECAY_F(m_Alpha, FADE_DECAY);
#endif
        // Port: easing already advanced by UpdateRealtime() (case 6's two factors are
        // combined into one 0.5*FADE_DECAY constant there); read the current value.
        if (m_Alpha >= EXIT_THRESHOLD) {
            // Not faded yet -- @0x001a6d48 keeps re-asserting the pause flag every frame.
            if (!IsOnlineMultiplayer()) {
                game_work.bM_Mode = true;
            }
        } else {
            m_Alpha = 0.0f;                              // @0x001a6c8c, shared by 4/5/6
            if (m_State == PAUSE_STATE_QUIT_EXIT) {
                LOG_INFO("SCREEN/PauseScreen", "%s (%s)", "QuitToMenu @ 0x001cb6e4", "QUIT_EXIT faded");
                QuitToMenu();
                // White-flash via HitMenuBomb at the hit button's pos. Index 0 is
                // the P1 quit button (m_QuitButton); index 1 is P2 in the dead MP arm.
                // Binary @0x001a6ca0..0x001a6cc8 tests ONLY `m_MenuBombIndex >= 0`
                // (`ldr r3,[r4,#0xc8] / cmp r3,#0 / blt`), then resolves the button by
                // index: `add r3,r3,#0x28 / ldr r1,[r4,r3,lsl #2]` == *(this + 0xa0 + idx*4),
                // i.e. m_QuitButton for 0 and m_P2QuitButton for 1. No null test on the
                // result. Port-added `&& m_QuitButton` removed.
                // DIFFERS: port hard-codes m_QuitButton instead of the +0xa0[idx] lookup --
                // exact for idx 0, and idx 1 is the defunct P2 path (never set in v1.6.1).
                if (m_MenuBombIndex >= 0) {
                    HitMenuBomb(m_QuitButton->pos);
                    LOG_INFO("BOMBHIT", "QuitToMenu fires HitMenuBomb at (%.1f,%.1f); bombHitTimer set to %.3f",
                             m_QuitButton->pos.x, m_QuitButton->pos.y,
                             game_work.m_BombHitTimer);
                }
                // v1.6.1 @0x001a6cd4: `vldr s15,[pc,#-0x250] / vstr s15,[r4,#0xb4]`.
                // The literal at 0x001a6a8c is 0x00000000 -- the binary writes 0.0f here,
                // NOT 1.0f. (The old port comment cited DAT_00154fb8 = 1.0; that DAT is not
                // what this instruction loads.) Case 1 (BOMB_FLASH) re-zeroes it every frame
                // anyway, so the observable difference is at most one tick.
                m_ButtonFadeAlpha = 0.0f;
                m_MenuBombIndex   = -1;
                // Transition to BOMB_FLASH (1), NOT HIDDEN. The bomb-flash poll
                // in case 1 is what produces the visible white flash and tears
                // down the gameplay HUD; jumping straight to HIDDEN skipped both.
                LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_BOMB_FLASH), "Update/QUIT_EXIT faded");
                m_State = PAUSE_STATE_BOMB_FLASH;
                m_Alpha = 1.0f;
                SaveCurrentData(false);
            } else if (m_State == PAUSE_STATE_RETRY_EXIT) {
                // v1.6.1 @0x001a6d08: SaveCurrentData(false) before RetryLevel.
                SaveCurrentData(false);
                m_ButtonFadeAlpha = 0.0f;
                m_Alpha = 0.0f;
                LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_HIDDEN), "Update/RETRY_EXIT faded");
                m_State = PAUSE_STATE_HIDDEN;
                m_RevealTimer = 2.0f;
                // Binary calls RetryLevel (0x0016b008), NOT EndRetryLevel (0x0016a208).
                // RetryLevel sets retryFlag=1 + retryTimer=0.1f; GameUpdate's retry
                // dispatch tail then calls RetryUpdate per frame and EndRetryLevel at 0.
                RetryLevel();
            } else {
                // RESUME_EXIT @0x001a6d30.
                LOG_INFO("SCREEN/PauseScreen", "%d -> %d (%s)", (int)(m_State), (int)(PAUSE_STATE_HIDDEN), "Update/RESUME_EXIT faded");
                m_State = PAUSE_STATE_HIDDEN;
                m_RevealTimer = 2.0f;
            }
            // Shared tail @0x001a6d40. bM_Mode is cleared by the GameDraw tail
            // @0x001cdd64 when g_unpause_game fires, NOT here.
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
    // ASM-spec v1.6.1 PauseScreen::Update @0x001a6dac..0x001a6dc8 + @0x001a71cc..0x001a71d0
    // (2026-07-31, direct disassembly read; downgraded from ASM-verified -- re-analyst
    // provenance, never asm-inspector-diffed). Behaviour CONFIRMED instruction-for-instruction:
    //   ldr r3,[r4,#0xd8] / vldr s20,[r4,#0xb4] / vldr s19,[r4,#0x7c]   <- save first
    //   cmp r3,#0x6 / vldreq s15,<0.0> ; vstreq [r4,#0xb4] / vmoveq s15,1.0 ; vstreq [r4,#0x7c]
    //   ... rendering math ...
    //   vstr s19,[r4,#0x7c] / vstr s20,[r4,#0xb4]                        <- restore at exit
    // Binary saves
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

    // 3. Resume button texture swap based on m_Alpha.
    // Binary @0x001a6dcc..0x001a6e18: no null test -- it resolves the button by index
    // (`ldr r3,[r4,#0xcc] / add r3,r3,#0x26 / ldr r0,[r4,r3,lsl #2]` == *(this + 0x98 +
    // m_PressIndex*4), i.e. m_ResumeButton / m_P2ResumeButton), adds 0x74 to reach the
    // button's texture SmartPtr and calls SmartPtr::operator= @0x00108a84.
    // Port-added null guard removed.
    // v1.6.1 @0x001a6df8: the m_Alpha <= 0.5 arm calls IsOnlineMultiplayer() first and
    //   picks m_QuitTitleTex (+0xc0) when online, m_PauseButtonTex (+0xb8) when not.
    //   IsOnlineMultiplayer() is a hard false in v1.6.1, so the offline arm always wins.
    // DIFFERS: port targets m_ResumeButton directly instead of the +0x98[m_PressIndex]
    //   lookup -- exact for index 0; index 1 is the defunct P2 slot (nullptr in v1.6.1).
    if (m_Alpha <= 0.5f) {
        if (IsOnlineMultiplayer()) {
            m_ResumeButton->m_Texture = m_QuitTitleTex;
        } else {
            m_ResumeButton->m_Texture = m_PauseButtonTex;
        }
    } else {
        m_ResumeButton->m_Texture = m_PlayButtonTex;
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
    m_ResumeButton->m_Active = (game_work.mMainScreen && game_work.mMainScreen->IsInGameplay()) ? 1 : 0;

    // 4. Title slide-in: pos.x = 0, pos.y = size.y + 160 + (-130 * m_Alpha)
    // Binary @0x001a6e1c..0x001a6e54 reads size.y (+0x24), not m_TitleSize.y (+0x84) --
    // identical value, the ctor assigns m_TitleSize = size.
    // v1.6.1 @0x001a6e38..0x001a6e3c: the same block zeroes the rotation/anim-timer slot
    // (+0x2c, HUDControl::m_Timer) on BOTH the Resume and the Retry button
    // (`vstr s16,[r2,#0x2c]` / `vstr s16,[r3,#0x2c]`, s16 = 0.0f) every frame, BEFORE the
    // two pos writes. The quit button's own reset is a different field on a different
    // object (`vstr s16,[r7,#0xb0]` == BSButton::m_DrawRotation.x @0x001a6e88, below).
    {
        const float sizeY = m_TitleSize.y;
        m_ResumeButton->m_Timer = 0.0f;   // +0x2c @0x001a6e38
        m_RetryButton->m_Timer  = 0.0f;   // +0x2c @0x001a6e3c
        pos.x = 0.0f;
        pos.y = TITLE_SLIDE_BASE + sizeY + TITLE_SLIDE_MUL * m_Alpha;
    }

    // 5. Quit button (BSButton) per-frame position + active state.
    // ASM-spec v1.6.1 PauseScreen::Update @0x001a6e44..0x001a6ec4: BSButton per-frame block.
    // NOTE: this null test is GENUINE -- `ldr r0,[r4,#0xa0] / cmp r0,#0x0 / beq 0x001a6ec8`
    // @0x001a6e44..0x001a6e58 skips the whole block. (SetTextOffset below sits OUTSIDE it in
    // the binary, at @0x001a6f0c..0x001a6f24, where [r4+0xa0] is re-loaded unguarded.)
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
    }

    // Defunct: same-screen multiplayer -- P2 Quit deactivation, binary @0x001a6ec8..0x001a6ed8
    // (`ldr r0,[r4,#0xa4] / cmp r0,#0 / beq / mov r1,#0 / bl SetActive`). GENUINE null test.
    // Always skipped in v1.6.1: m_P2QuitButton is only built inside the dead
    // IsSameScreenMultiplayer() arm, so it stays nullptr.
    if (m_P2QuitButton) m_P2QuitButton->SetActive(false);

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
    // None of the m_ResumeButton / m_RetryButton null tests that used to wrap this tail
    // exist in the binary -- every site loads [r4+0x98] / [r4+0xac] and dereferences it
    // straight away (@0x001a6ee8, 0x001a6f2c, 0x001a6fbc, 0x001a6fe0, 0x001a7034,
    // 0x001a706c, 0x001a7078, 0x001a70c8, 0x001a7164). Both buttons are created
    // unconditionally at the top of this function, so they are provably non-null here.
    // Port-added guards removed.
    const float OX = m_ButtonOriginPos.x;  // = 64

    // Resume base pos.y = 0.375*OX - 165  (pool 0x001a71ec = 160.0f, then a further
    // -5.0f @0x001a6f04). The binary computes and stores it TWICE, once on either side
    // of the quit button's SetTextOffset -- @0x001a6edc..0x001a6f08 and
    // @0x001a6f28..0x001a6f3c. Both stores are kept so the ordering matches.
    m_ResumeButton->pos.y = 0.375f * OX - 165.0f;

    // SetTextOffset sits OUTSIDE the m_QuitButton null test in the binary
    // (@0x001a6f0c..0x001a6f24 re-loads [r4+0xa0] unguarded). Both buttons are created
    // unconditionally at the top of this function, so the pointer is provably non-null.
    m_QuitButton->SetTextOffset(_Vector3<float>(-29.0f, 3.0f, 0.0f));

    m_ResumeButton->pos.y = 0.375f * OX - 165.0f;   // second store @0x001a6f3c

    // v1.6.1 PauseScreen::Update @0x001a6f40..0x001a6fa4: a function-local static Vec3
    // built once under __cxa_guard_acquire / __cxa_guard_release / __aeabi_atexit.
    // Value = (OX, -0.375*OX - 160.0, 0). Only .x is ever read (the phase-2 slide targets
    // below), so the numbers are the same OX the port used inline before.
    static const _Vector3<float> retryButtonStart(OX, OX * -0.375f - 160.0f, 0.0f);

    // Retry base (v1.6.1 PauseScreen::Update @0x001a6fa8..0x001a6fe4):
    //   pos = (240 + 0.5*OX, -20, 0)
    // DIFFERS: opt-in widescreen -- MapX re-anchors the right-edge base X.
    m_RetryButton->pos = _Vector3<float>(MapX(240.0f + 0.5f * OX, "pause.retry"), -20.0f, 0.0f);

    // Resume base pos.x = -((244 - 0.375*OX) + |fade|*(10 + 0.75*OX))
    //   -- @0x001a6fe0..0x001a7028 (pool 0x001a71f4 = 240.0f, +4.0f @0x001a6ff8).
    // TODO: v1.6.1 0x001a7000 (PauseScreen::Update) -- |fade| is a real call to
    //   Math::Abs<float> @0x00114b38; the port uses std::fabs. Adding the template belongs
    //   in src/engine/math/MathUtil.h (where Math::Min/Max live), not in this file.
    // DIFFERS: opt-in widescreen -- MapX re-anchors the off-screen-left base X.
    // When IsEnabled()==false (menu / not the active pause overlay), absFade
    // eases to 1.0 and this formula pushes the button to x=-270 -- just past
    // the original +-240 field edge (see IsEnabled() note above). Under the
    // widened field that -270 base sits INSIDE the new +-HalfWidth() extent,
    // re-revealing the resume/pause icon on the menu. MapX proportionally
    // rescales it back past the widened edge. Identity when disabled/__bada__.
    {
        const float absFade = std::fabs(m_ButtonFadeAlpha);
        const float term1 = 244.0f - 0.375f * OX;
        const float term2 = absFade * (10.0f + 0.75f * OX);
        m_ResumeButton->pos.x = MapX(-(term1 + term2), "pause.resume");
    }
    // ASM-spec v1.6.1 PauseScreen::Update @0x001a702c..0x001a705c.
    // The scale is computed in DOUBLE precision --
    //   vmov.f64 d17,1.25 / vmov.f64 d16,0.75 / vcvt.f64.f32 d18,s15(m_Alpha) /
    //   vmla.f64 d16,d18,d17 / vcvt.f32.f64
    // then operator*(m_ButtonOriginPos, scale) -> resume+0x13c.
    // Per-frame Resume m_TargetSize: m_ButtonOriginPos (captured (64,64,64)
    // at lazy-create) scaled by resumeScale = m_Alpha * 1.25 + 0.75.
    // MenuButton::Update writes `size = m_TargetSize` each frame, so only
    // m_TargetSize matters here -- writes to size.x/y are clobbered.
    {
        const float resumeScale = m_Alpha * 1.25f + 0.75f;
        m_ResumeButton->m_RestScale = m_ButtonOriginPos * resumeScale;
    }

    // ASM-spec v1.6.1 PauseScreen::Update @0x001a7060..0x001a71a0.
    // Gate: `vldr s15,[r4,#0x7c] / vcmpe s15,#0 / ldrle r0,[r4,#0xac] / movle r1,#0x0 /
    // ble 0x001a71a0` -- when m_Alpha <= 0 it jumps straight to the shared
    // SetActive(m_RetryButton, false) at 0x001a71a0; otherwise it copies resume+0x13c ->
    // retry+0x13c, runs the phase-2 lerp, and falls into the same call with true.
    // Retry m_RestScale := Resume m_RestScale when m_Alpha > 0. Without this, Retry stays
    // at MenuButton::Init texture-auto-size (129,129,0) -- 2x oversize hitbox and .z=0
    // degenerate render matrix.
    //
    // Phase 2: lerp toward the on-screen target by m_Alpha.
    // v1.6.1 PauseScreen::Update @0x001a7094..0x001a7198:
    //   button.pos += (to_pos - button.pos) * m_Alpha
    //   Resume target = (-retryButtonStart.x, -20, 0) -- inside-left, and this ONE target
    //     is picked via `bl IsOnlineMultiplayer @0x00103d9c`: online substitutes (0,0,0).
    //   Retry  target = (+retryButtonStart.x, -20, 0) -- inside-right, NOT online-gated.
    {
        bool retryActive;
        if (m_Alpha > 0.0f) {
            m_RetryButton->m_RestScale = m_ResumeButton->m_RestScale;

            _Vector3<float> resumeTarget(0.0f, 0.0f, 0.0f);
            if (!IsOnlineMultiplayer()) {
                resumeTarget = _Vector3<float>(-retryButtonStart.x, -20.0f, 0.0f);
            }
            m_ResumeButton->pos += (resumeTarget - m_ResumeButton->pos) * m_Alpha;

            const _Vector3<float> retryTarget(retryButtonStart.x, -20.0f, 0.0f);
            m_RetryButton->pos += (retryTarget - m_RetryButton->pos) * m_Alpha;

            retryActive = true;
        } else {
            retryActive = false;
        }
        m_RetryButton->SetActive(retryActive);   // single join call @0x001a71a0
    }

    // 7. P2 buttons inactive.
    // Defunct: same-screen multiplayer -- these pointers stay nullptr in v1.6.1 (only the
    // dead IsSameScreenMultiplayer() arm builds them), but the call shape is preserved per
    // stub-don't-skip. Binary @0x001a71a4..0x001a71cc:
    //   ldr r0,[r4,#0x9c] / cmp r0,#0 / beq / mov r1,#0 / bl SetActive   (m_P2ResumeButton)
    //   ldr r0,[r4,#0xb0] / cmp r0,#0 / beq / mov r1,#0 / bl SetActive   (m_P2RetryButton)
    // Both null tests are GENUINE.
    if (m_P2ResumeButton) m_P2ResumeButton->SetActive(false);
    if (m_P2RetryButton)  m_P2RetryButton->SetActive(false);

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
