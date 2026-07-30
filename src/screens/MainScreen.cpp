//
// MainScreen — v1.6.1 faithful port
// v1.6.1 addresses:
//   ctor @0x0019811c, Update @0x00196e1c, Draw @0x001993ac,
//   UpdateScreenElements @0x00195a58, CreateButtons @0x001961f8
// v1.6.1 struct layout: m_StateTimer=bounce velocity (+0x110), m_Timer2=transition timer (+0x124),
//   m_ButtonsCreatedFlag(+0x7c), intro-hold countdown via SetIntroHoldTimer (+0x11c).
//

#include "MainScreen.h"
#include "render/BakedStringBox.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "DojoScreen.h"
#include "GameModeScreen.h"
#include "entities/ActorManager.h"
#include "Game.h"
#include "entities/FruitInfo.h"
#include "entities/Bomb.h"
#include "entities/BombBlast.h"
#include "entities/Fruit.h"
#include "entities/Entity.h"
#include "game/BombHit.h"
#include "game/FruitCamera.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "math/Random.h"
#include "math/MathUtil.h"
#include "math/Colour.h"
#include "asset/Mesh.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "screens/SettingsScreen.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "core/SystemManager.h"
#include "audio/GameSound.h"
#include "audio/SoundManager.h"
#include "debug/DebugFlags.h"
#include "debug/Logger.h"
#include "engine/util/StringTable.h"
#include "engine/network/NetworkManager.h"
#include "engine/network/P2PMessageHandling.h"
#include "render/Layout.h"
#include <cmath>
#include "game/GameWork.h"
#include "game/ItemManager.h"

#if defined(FN_BLOCK_PRELOAD)
#include "resource/ResBlock.h"
#endif

// Timing constants (verified from binary, see docs/screens/main.md)
static const float CAMERA_LERP_RATE    = 0.125f;
static const float CAMERA_THRESHOLD    = -0.999f;
static const float TIMER2_THRESHOLD    = 0.15f;
static const float STATE_0E_DECAY      = 0.85f;
static const float STATE_0E_THRESHOLD  = 0.25f;
static const float STATE_2_DECAY       = 0.75f;
static const float STATE_8_LERP_RATE     = 0.125f;
static const float STATE_8_LERP_THRESHOLD = 0.999f;   // DAT_0014bf28 = 0x3f7fbe77
static const float STATE_8_DURATION      = 1.5f;
static const float STATE_8_RESET_TIMER   = 0.15f;     // DAT_0014bf2c (was -0.85)
static const float BOUNCE_LOSS         = -0.25f;
static const float BOUNCE_SETTLE       = 3.0f;
static const float ALPHA_LERP_RATE     = 0.25f;
static const float PAUSE_VISIBILITY    = 0.01f;
static const float SOUND_VOLUME_ON     = 0.5f;

// Helper: get GLuint from Mortar::SmartPtr<Texture>
static GLuint TexId(const Mortar::SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->GetTexId() : 0;
}

// File-scope globals mirroring the binary's GOT globals (s_blurTex/m_fruitTex/m_ninjaTex).
// Loaded once in MainScreen ctor (unconditionally, cross-visible).
static Mortar::SmartPtr<Mortar::Texture> s_blurTex;
static Mortar::SmartPtr<Mortar::Texture> m_fruitTex;
static Mortar::SmartPtr<Mortar::Texture> m_ninjaTex;

// See MainScreen.h for why this port-only hook exists (binary defers to atexit).
void MainScreen_UnloadStatics() {
    s_blurTex.SetNull();
    m_fruitTex.SetNull();
    m_ninjaTex.SetNull();
}

// Button positions (verified from read_memory, docs/screens/main.md)
// ASM-spec v1.6.1 @0x00196264: NEW GAME pos.x = 24.0f
static const _Vector3<float> POS_PLAY_BUTTON(24.0f, -66.0f, 0.0f);
static const _Vector3<float> POS_DOJO_BUTTON(-144.0f, -65.0f, 0.0f);
static const _Vector3<float> POS_MORE_GAMES(182.0f, -106.0f, 0.0f);
static const _Vector3<float> POS_SOUND_TOGGLE(216.0f, 135.5f, 0.0f);
static const _Vector3<float> POS_MUSIC_TOGGLE(176.0f, 135.5f, 0.0f);
// Port specific: no binary counterpart. BOTTOM-left SETTINGS button. +Y is up
// (sound/music toggles at y=+135.5 are TOP-right), so the bottom edge is
// negative y; x near the left edge.
//
// Position/margin matched to the in-game PAUSE resume button's ACTUAL idle
// on-screen rect (re-verified from PauseScreen::Update, src/screens/
// PauseScreen.cpp ~line 908-968, with the real steady-state field values --
// NOT assumed): in PAUSE_STATE_HIDDEN, m_Alpha decays via `*= 0.75` each
// frame with a hard clamp to exactly 0.0 once < FADE_CLAMP (0.01) -- at idle
// (any time after the ~16-frame/0.27s decay settles) m_Alpha IS exactly 0.0,
// confirming m_RestScale = m_ButtonOriginPos*resumeScale = 64*0.75 = 48 (not
// 64). But position has a second-order effect: PauseScreen::Update's Phase 2
// on-screen lerp (`pos += (target-pos)*m_Alpha`, target=(-64,-20,0)) is
// GATED on `m_Alpha > 0.0f` -- since m_Alpha==0.0 exactly at idle, Phase 2
// NEVER RUNS. The button sits at its Phase-1 BASE position instead:
//   pos.x = -((244-0.5*OX) + |m_ButtonFadeAlpha|*(10+0.75*OX)), OX=64
//   pos.y = 0.375*OX - 165
// with m_ButtonFadeAlpha also decayed to ~0 at idle (same *=0.75 decay,
// gameplay-enabled branch) so the fade term drops out: pos = (-212,-141).
// Half-size = 48/2 = 24, so the idle rect is x[-236,-188] / y[-165,-117] --
// LEFT margin only +4 from the x=-240 screen edge, and BOTTOM margin -5
// (the icon hangs 5 units off the bottom edge, clipped). This tight/
// negative-margin corner-hugging placement -- not just the 48 size -- is
// what makes the pause icon read as "bigger"/edge-tight versus a fully-inset
// button. Settings mirrors both the size (48) and this exact margin
// treatment (same corner, same 48 half-size, same +4/-5 margins), landing
// on the SAME coordinates as the pause button's idle pos: (-212,-141).
static const _Vector3<float> POS_SETTINGS_TOGGLE(-212.0f, -141.0f, 0.0f);
// buttonOriginPos(64,64,64) * idleResumeScale(0.75) -- see ctor-site note
// where this formula is derived from PauseScreen's idle resumeScale.
static const _Vector3<float> kSettingsRestScale(48.0f, 48.0f, 48.0f);
// Port specific: how far (world units) the settings button slides toward its
// bottom-left corner as it hides (driven by the ring growFactor, frame-synced
// with them). Large enough (~55) that the full-size 48px button clears the
// screen edge BEFORE the m_Active gate cuts it off (so it exits by sliding,
// not popping). Slide-only -- the button does NOT shrink.
static const float kSettingsSlideOut = 55.0f;

void MainScreen::SetState(MainScreenState s) {
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(s), "SetState");
    m_State = s;
}

// ASM-spec v1.6.1 MainScreen::MainScreen @0x0019811c
// Zero-arg in the binary (prologue reads only r0). The LoadLocalisedTexture
// sequence below is in the binary's exact call order; slot offsets are the
// binary's, not a guess. Several of the loaded textures are never read again by
// MainScreen -- see the per-slot notes in MainScreen.h.
MainScreen::MainScreen()
    : m_ButtonsCreatedFlag(false),
      m_pGameModeButton(nullptr), m_pStoreButton(nullptr),
      m_pQuitButton(nullptr), m_pMoreGamesBtn(nullptr),
      pToggleA(nullptr), pSoundToggle(nullptr),
      pMusicToggle(nullptr),
      _pad_c0(0), _pad_c4(0), _pad_c8(0),
      m_pSliceInstrBox(nullptr),
      m_Lean(1.0f),
      m_NinjaTextX(0.0f), m_NinjaTextY(0.0f), m_NinjaTextZ(0.0f),
      m_BounceVel(0.0f), m_BounceY(0.0f), m_BounceZ(0.0f),
      m_StateTimer(0.0f),
      m_Field114(0.0f),
      m_State(STATE_CAMERA_ZOOM),
      m_IntroHoldTimer(0.0f),
      m_Timer2(0.0f)
#ifndef __bada__
      , m_Time(0.0f)
      , m_bGameStartReset(false)
      , m_pSettingsButton(nullptr)
      , m_TimeRemainingDisplay(-1.0f)
#endif
{
    // Load global textures (assigned to file-scope globals mirroring binary GOT globals).
    s_blurTex  = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    m_fruitTex = Mortar::TextureManager::LoadLocalisedTexture("fruit_text.tex");
    m_ninjaTex = Mortar::TextureManager::LoadLocalisedTexture("ninja_text.tex");

    // +0xe4: the binary loads its OWN slice_fruit copy here (it is not an alias
    // of the fruit_text global). Draw renders it as the parchment frame.
    m_TexSliceFruit = Mortar::TextureManager::LoadLocalisedTexture("slice_fruit.tex");

    // +0x8c/+0x90/+0x120/+0x9c: ring-button + more-games art. Loaded here, but
    // CreateButtons @0x001961f8 takes its MenuButton textures from
    // game_work.pM_Textures[], so nothing in MainScreen reads these back.
    m_TexNewGame  = Mortar::TextureManager::LoadLocalisedTexture("newgame.tex");
    m_TexDojoIcon = Mortar::TextureManager::LoadLocalisedTexture("dojo_icon.tex");
    m_TexMoreGames = Mortar::TextureManager::LoadLocalisedTexture("more_games.tex");
    m_TexQuit     = Mortar::TextureManager::LoadLocalisedTexture("quit.tex");

    // Defunct: OpenFeint / GameCenter menu art — loaded, never drawn; v1.6.1 MainScreen::MainScreen @ 0x0019811c
    // Do NOT "fix" the absent draw: Draw @0x001993ac, Update @0x00196e1c and
    // CreateButtons @0x001961f8 have no reference to +0x94 / +0x98 in v1.6.1.
    m_TexOpenFeint      = Mortar::TextureManager::LoadLocalisedTexture("openfeint.tex");
    m_TexGCAchievements = Mortar::TextureManager::LoadLocalisedTexture("gc_achievements.tex");

    // Load verdana.fnt into m_pFont (+0x128).
    {
        m_pFont = Mortar::Font::Create("fonts/verdana.fnt");
    }

    // +0xd4/+0xd8: the sound toggle's on/off pair, indexed by bSoundOn^1 in Update.
    m_TexSoundOn  = Mortar::TextureManager::LoadLocalisedTexture("sound.tex");
    m_TexSoundOff = Mortar::TextureManager::LoadLocalisedTexture("sound_cross.tex");
    // +0xcc/+0xd0: the music toggle's on/off pair, indexed by bMusicOn^1.
    m_TexMusicOn  = Mortar::TextureManager::LoadLocalisedTexture("music.tex");
    m_TexMusicOff = Mortar::TextureManager::LoadLocalisedTexture("music_cross.tex");

    m_TexCommingSoon = Mortar::TextureManager::LoadLocalisedTexture("comming_soon.tex");

    // v1.6.1 MainScreen ctor @0x0019811c: BakedStringBox(..., game_work.m_pTTFFontMain, ...)
    // for the "SLICE FRUIT TO BEGIN" plate -- reads the shared locale TTF face (GameWork+0x614,
    // set by PreloadFontsTTF to arabic.ttf when languageFlag==0x14, else gangofchinese.ttf).
    // m_pSliceInstrBox holds the BakedStringBox* (binary +0xe0).
#ifndef __bada__
    {
        Mortar::FontCacheObjectTTF* ttf = game_work.m_pTTFFontMain;
        if (!ttf) {
            // Lazy fallback only if PreloadFontsTTF hasn't run yet.
            m_BakedStrSmart = Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
            if (m_BakedStrSmart.IsValid()) {
                ttf = Mortar::FontTTFRegistry::GetInstance().Lookup(m_BakedStrSmart.Get());
            }
        }
        if (ttf) {
            m_pSliceInstrBox = new Mortar::BakedStringBox(
                ttf,
                9.0f,   // fontSize
                75.0f,  // width
                30.0f,  // height
                (Mortar::ALIGNMENT_TYPE)0x0d,   // align: centre-H(0x01) | centre-V(0x04) | fit(0x08)
                3,      // maxLines
                3.0f    // lineSpacing
            );
            const char* sliceText = GETSTRING_CAST_0(LSTR_MENU_TEXTURE_13);
            m_pSliceInstrBox->SetText(sliceText ? sliceText : "SLICE FRUIT TO BEGIN");
            // Colour: binary reads game_work.m_TitleColour (+0x6a0) = RGB(0x6F,0x46,0x1E).
            m_pSliceInstrBox->SetColour(game_work.m_TitleColour, /*setBase*/0);
            m_pSliceInstrBox->SetHorizontalLineSpacing(-1);
            m_pSliceInstrBox->FitIntoVerticalBounds();
        }
    }
#endif // !defined(__bada__)

    // Set size = (480.0, 138.0, 1.0)
    size = _Vector3<float>(480.0f, 138.0f, 1.0f);

    // Set position = (0.0, (320.0 - size_y) * 0.5, 0.0) = (0.0, 91.0, 0.0)
    pos = _Vector3<float>(0.0f, (320.0f - size.y) * 0.5f, 0.0f);

    // m_BounceY = ninja_text.tex height / 2 + 160.0
    // Binary ctor v1.6.1 MainScreen ctor @0x0019811c: calls ninja_text_tex->GetHeight() (vtable +0x18)
    // -> shifts right by 1 -> adds 160.0.
    // m_BounceY is the intentional off-screen START position; sprung down to pos.y+3 each frame
    // by UpdateScreenElements bounce physics. NOT a stale 0-based formula.
    const float ninjaH = m_ninjaTex.IsValid()
                       ? (float)(m_ninjaTex->GetHeight() / 2)
                       : 0.0f;
    m_BounceY = ninjaH + 160.0f;
}

MainScreen::~MainScreen() {
    Release();
}

// v1.6.1 MainScreen::Init @0x00195964: calls vtable slot 4 (Reset).
void MainScreen::Init() {
    Reset();
}

// v1.6.1 MainScreen::Reset @0x0014ac8c: no-op
void MainScreen::Reset() {
}

// v1.6.1 MainScreen::Release @0x0014cd20
void MainScreen::Release() {
    m_pGameModeButton    = nullptr;
    m_pStoreButton    = nullptr;
    m_pQuitButton = nullptr;
    m_pMoreGamesBtn  = nullptr;
    pToggleA       = nullptr;
    pSoundToggle   = nullptr;
    pMusicToggle   = nullptr;
#ifndef __bada__
    m_pSettingsButton = nullptr;
#endif // !defined(__bada__)

    delete m_pSliceInstrBox;
    m_pSliceInstrBox = nullptr;
}

// v1.6.1 MainScreen::Update @0x00196e1c — state machine
// ASM-spec v1.6.1 MainScreen::Update @0x00196e1c: toggle create+texswap INLINE at top (no
// CreateToggles symbol); snapshot elapsedTime=-PauseAmount once; case-0 gate gameMode=0 on
// !IsMultiplayer(); PauseAmount ramp else=-1.0 clamp.
void MainScreen::Update(float dt) {
    // Port specific: dt-normalize the per-frame eases below (dtN = dt*60) so the menu
    //   camera zoom / slide transitions play at the intended ~60Hz speed regardless of
    //   render framerate (binary is frame-based@fixed-60Hz).
    float dtN = dt * 60.0f;

#ifndef __bada__
    m_Time += dt;
#endif

    // Binary builds BOTH toggles inline at the top of Update, each under its OWN
    // null guard, so a destroyed toggle is recreated in any state. Per toggle:
    // new MenuButton (0x178), Init with Delegate0, HUD::AddControl,
    // layer=8 (HUD_LAYER_BUTTONS), SetSingular.
    // v1.6.1 @0x00197188-0x001971c4: sound toggle -> +0xb4 (pSoundToggle),
    // music toggle -> +0xb8 (pMusicToggle).
    if (pSoundToggle == nullptr && game_work.mHud) {
        pSoundToggle = new MenuButton();
        // DIFFERS: opt-in widescreen -- MapX the initial-creation X; Update()'s
        // per-frame positioning block below (top-right corner branch) re-applies
        // the same MapX key every frame regardless.
        pSoundToggle->Init(_Vector3<float>(MapX(POS_SOUND_TOGGLE.x, "menu.sound"), POS_SOUND_TOGGLE.y, POS_SOUND_TOGGLE.z),
            Mortar::Delegate0<void>::Make(this, &MainScreen::SoundCallback), -1,
            _Vector3<float>(32.0f, 32.0f, 1.0f), nullptr);
        game_work.mHud->AddControl(pSoundToggle);
        pSoundToggle->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
        pSoundToggle->SetSingular();
    }
    if (pMusicToggle == nullptr && game_work.mHud) {
        pMusicToggle = new MenuButton();
        // DIFFERS: opt-in widescreen -- see pSoundToggle note above.
        pMusicToggle->Init(_Vector3<float>(MapX(POS_MUSIC_TOGGLE.x, "menu.music"), POS_MUSIC_TOGGLE.y, POS_MUSIC_TOGGLE.z),
            Mortar::Delegate0<void>::Make(this, &MainScreen::MusicCallback), -1,
            _Vector3<float>(32.0f, 32.0f, 1.0f), nullptr);
        game_work.mHud->AddControl(pMusicToggle);
        pMusicToggle->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
        pMusicToggle->SetSingular();
    }

#ifndef __bada__
    // Port specific: no binary counterpart. Bottom-left SETTINGS button;
    // built the same way as the sound/music toggles above (recreated under a
    // null guard, never explicitly torn down while MainScreen persists).
    //
    // Size: matched to the in-game PAUSE icon's ACTUAL rendered size, not its
    // ctor-time m_RestScale=64. PauseScreen::Update (src/screens/PauseScreen.cpp
    // ~line 913-916) recomputes m_ResumeButton->m_RestScale EVERY frame as
    // m_ButtonOriginPos * resumeScale, where m_ButtonOriginPos=(64,64,64) is
    // captured once at lazy-create and resumeScale = m_Alpha*1.25 + 0.75. In
    // normal (idle, not-paused) gameplay m_State==PAUSE_STATE_HIDDEN decays
    // m_Alpha to 0 (PauseScreen.cpp line ~700-701), so resumeScale settles at
    // 0.75 and the pause icon's steady-state rendered size is 64*0.75 = 48, not
    // 64. Setting the same 48 here (via the same OriginPos*0.75 formula, not a
    // bare literal) makes the two icons match at the size the player actually
    // sees side-by-side (pause icon in gameplay HUD vs settings icon on the
    // main menu) rather than the two buttons' differing ctor-time constants.
    if (m_pSettingsButton == nullptr && game_work.mHud) {
        m_pSettingsButton = new MenuButton();
        m_pSettingsButton->m_Texture = Mortar::TextureManager::LoadLocalisedTexture("settings_button.tex");
        // DIFFERS: opt-in widescreen -- MapX the initial-creation X; the per-frame
        // slide block below (Update tail) re-applies the same MapX key every frame.
        m_pSettingsButton->Init(_Vector3<float>(MapX(POS_SETTINGS_TOGGLE.x, "menu.settings"), POS_SETTINGS_TOGGLE.y, POS_SETTINGS_TOGGLE.z),
            Mortar::Delegate0<void>::Make(this, &MainScreen::SettingsCallback), -1,
            _Vector3<float>(0.0f, 0.0f, 0.0f), nullptr);
        m_pSettingsButton->m_RestScale = kSettingsRestScale;
        m_pSettingsButton->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
        game_work.mHud->AddControl(m_pSettingsButton);
        m_pSettingsButton->SetSingular();
    }

    // Port specific: no binary counterpart. Deferred settings-quit poll -- see
    // SettingsScreen::s_QuitAfterClose header note. SettingsScreen::Toggle()'s
    // close branch can't call the quit trigger synchronously (it runs
    // mid-teardown of the modal); instead it latches s_QuitAfterClose and this
    // poll fires the real quit trigger once the modal has fully closed: no
    // instance open, and no HUD control still owns input as a modal.
    if (SettingsScreen::s_QuitAfterClose && !SettingsScreen::IsOpen() &&
        game_work.mHud && game_work.mHud->GetInputModal() == nullptr) {
        // Clear FIRST -- TriggerQuitFromSettings() (QuitGamesCallback) sets
        // m_State=STATE_QUIT_WAIT, which must not re-enter this block next frame.
        SettingsScreen::s_QuitAfterClose = false;
        TriggerQuitFromSettings();
    }
#endif // !defined(__bada__)

    // Toggle texture swap runs right after creation, BEFORE the state switch
    // (binary indexes the on/off texture pair by bMusicOn^1 / bSoundOn^1).
    if (pMusicToggle) {
        pMusicToggle->m_Texture = (game_work.m_bMusicOn ? m_TexMusicOn : m_TexMusicOff);
    }
    if (pSoundToggle) {
        pSoundToggle->m_Texture = (game_work.m_bSoundOn ? m_TexSoundOn : m_TexSoundOff);
    }

    // Binary snapshots fVar15 = -flM_PauseAmount ONCE here; cases 1/2 use it for
    // pos.y (computed with the pre-ramp value) and the tail uses it as the
    // toggle-slide transition timer.
    float elapsedTime = -game_work.m_PauseAmount;

    switch (m_State) {
    case STATE_CAMERA_ZOOM: {
        // ASM-spec v1.6.1 MainScreen::Update @0x00197430: f0-countdown gates the intro slide.
        // Binary case-0 order: CreateButtons(this) first, then gameMode=0 if !IsMultiplayer(),
        // then the f0 sub-block: read +0x11c; if f0>0 OR bombHitTimer>1.45,
        // tick the countdown and hold the camera; otherwise settle branch:
        // m_Timer2 += dt, ramp m_PauseAmount toward -1. Advance to state 1 when camera
        // settled (m_PauseAmount < threshold) AND m_Timer2 > 0.15f.
        // m_StateTimer is the BOUNCE VELOCITY (set to 0.5f by QuitToMenu to seed
        // logo bounce on menu return). NOT a flash countdown in v1.6.1.
        // The countdown lives at +0x11c (m_IntroHoldTimer), a plain float in the
        // binary -- no texture is loaded into that slot.

        // v1.6.1 @0x00197430: CreateButtons called FIRST, unconditionally every frame;
        // internal gate on flM_BombHitTimer<1.45 + per-button null guards.
        CreateButtons();

        // v1.6.1 @0x00197430: gameMode=0 only when not in a multiplayer session.
        if (!IsMultiplayer()) {
            game_work.gameMode = 0;
        }

        float f0 = m_IntroHoldTimer;
        if (f0 > 0.0f || game_work.m_BombHitTimer > 1.45f) {
            // Hold/flash branch: tick countdown, ramp camera but clamp to >=0 (off-screen).
            m_IntroHoldTimer = f0 - dt;
            // Port specific: dt-normalize the per-frame ease (dtN = dt*60) so the menu camera
            //   zoom plays at the intended ~60Hz speed regardless of render framerate (binary is frame-based@fixed-60Hz).
            game_work.m_PauseAmount = -1.0f - (-1.0f - game_work.m_PauseAmount) * powf(1.0f - CAMERA_LERP_RATE, dtN);
            if (game_work.m_PauseAmount < 0.0f) {
                game_work.m_PauseAmount = 0.0f;
            }
        } else {
            // Settle branch: advance timer, ramp camera toward -1
            // (else-arm snaps to -1.0 once past the -0.999 threshold).
            // The binary re-zeroes gameMode here UNCONDITIONALLY (strb r2,[r3,#4]
            // @0x001972f8), i.e. without the IsMultiplayer() gate the pre-branch
            // write above carries.
            game_work.gameMode = 0;
            m_Timer2 += dt;
            if (game_work.m_PauseAmount >= CAMERA_THRESHOLD) {
                // Port specific: dt-normalize the per-frame ease (dtN = dt*60) so the menu camera
                //   zoom plays at the intended ~60Hz speed regardless of render framerate (binary is frame-based@fixed-60Hz).
                game_work.m_PauseAmount = -1.0f - (-1.0f - game_work.m_PauseAmount) * powf(1.0f - CAMERA_LERP_RATE, dtN);
            } else {
                game_work.m_PauseAmount = -1.0f;
            }
        }

        // v1.6.1 MainScreen::Update @0x00196e1c case 0 (binary @0x00197334..0x00197360):
        //   advance iff (m_Timer2 > 0.15) AND (flM_PauseAmount < 0.0). Binary uses bmi
        //   (branch-if-negative). The settle branch ramps m_PauseAmount toward -1.0, so the
        //   gate tests < 0, not >= 0 (an earlier RE mis-read the bmi sign as >=).
        if (m_Timer2 > TIMER2_THRESHOLD && game_work.m_PauseAmount < 0.0f) {
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CREATE_BUTTONS), "Update/CAMERA_ZOOM camera settled");
            m_State = STATE_CREATE_BUTTONS;
        }
        break;
    }

    case STATE_CREATE_BUTTONS: {
        // v1.6.1 MainScreen::Update @0x001974e0: badge the Store/Dojo button (+0xa4) from AreNewItems().
        if (m_pStoreButton) {
            m_pStoreButton->SetNewSymbol(ItemManager::GetInstance()->AreNewItems());
        }

        if (m_ButtonsCreatedFlag == 0) {
            CreateButtons();
        }

        // Binary computes pos.y from the top-of-function -PauseAmount snapshot
        // (fVar15), BEFORE ramping PauseAmount.
        {
            const float sizeY_1 = size.y;
            pos.y = (sizeY_1 + 320.0f - 2.0f * sizeY_1 * elapsedTime) * 0.5f;
        }

        if (game_work.m_PauseAmount >= CAMERA_THRESHOLD) {
            // Port specific: dt-normalize the per-frame ease (dtN = dt*60) so the menu camera
            //   zoom plays at the intended ~60Hz speed regardless of render framerate (binary is frame-based@fixed-60Hz).
            game_work.m_PauseAmount = -1.0f - (-1.0f - game_work.m_PauseAmount) * powf(1.0f - CAMERA_LERP_RATE, dtN);
        } else {
            game_work.m_PauseAmount = -1.0f;
        }
        break;
    }

    case STATE_GAME_START: {
        // v1.6.1 MainScreen::Update @0x00197468: WaveManager::Reset(true) + bM_bPaused=1 fire
        // UNCONDITIONALLY inside the m_PauseAmount guard (no latch in binary).
        // Port adds m_bGameStartReset latch (one-shot, port-only) to prevent repeated
        // resets on re-entry; guard only the latch reads/writes, not the binary calls.
        if (-game_work.m_PauseAmount > 0.999f
#ifndef __bada__
            && !m_bGameStartReset
#endif // !defined(__bada__)
        ) {
            WaveManager::GetInstance()->Reset(true);
#ifndef __bada__
            m_bGameStartReset = true;
#endif // !defined(__bada__)
            game_work.bM_bPaused = 1;
            // v1.6.1 @0x00197468: snapshot coins at game-start (cold path; no inbound xrefs in binary)
            game_work.m_CoinsAtGameStart = game_work.m_CoinsBalance;
        }
        game_work.m_PauseAmount *= 1.0f - (1.0f - STATE_2_DECAY) * FN::g_DebugTimeScale;
        if (fabsf(game_work.m_PauseAmount) < 0.001f) {
            game_work.m_PauseAmount = 0.0f;
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_FADE), "Update/GAME_START camera settled");
            m_State = STATE_CAMERA_FADE;
#ifndef __bada__
            m_bGameStartReset = false;
#endif // !defined(__bada__)
            game_work.bM_bPaused = 0;
        }

        // Binary: pos.y from the top-of-function -PauseAmount snapshot (fVar15),
        // i.e. the pre-decay value.
        const float sizeY_2 = size.y;
        const float tt_2 = sizeY_2 * elapsedTime;
        pos.y = (sizeY_2 + 320.0f - 2.0f * tt_2) * 0.5f;
        break;
    }

    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B: {
        // v1.6.1 MainScreen::Update @0x00197494: cases 3/4 share one block.
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        const int fruitCount = am ? am->GetNumEntities(0) : 0;

        if (fruitCount == 0) {
            CancelNews();  // Defunct: NetworkManager news -- called in the entity-count-0 block; v1.6.1 MainScreen::Update @0x00197494
            // DIFFERS: original = flat *0.75 (v1.6.1 MainScreen::Update @0x00197494), port scales by g_DebugTimeScale for debug time-scale tooling
            m_Timer2 *= 1.0f - (1.0f - 0.75f) * FN::g_DebugTimeScale;
        }

        const float sizeY_d = size.y;
        const float tt_d = sizeY_d * m_Timer2;
        pos.y = (sizeY_d + 320.0f - 2.0f * tt_d) * 0.5f;

        // Binary gates ONLY on entity-count==0 AND (m_Timer2 != 0 && m_Timer2 < 0.001).
        // A port-only "already spawned" pointer guard here was a stale-latch bug that
        // suppressed re-creation; the binary has no such guard.
        if (fruitCount == 0 && m_Timer2 != 0.0f && m_Timer2 < 0.001f) {
            m_Timer2 = 0.0f;
            DojoScreen* dojoScreen = new DojoScreen();
            // Binary @ 0x197494: vtable->Init(scr) is called BEFORE HUD::AddControl.
            // HUD::AddControl only appends to list; it does NOT call Init internally.
            dojoScreen->Init();
            game_work.mHud->AddControl(dojoScreen);
        }
        break;
    }

    case STATE_SLIDE_IN: {
        // v1.6.1 MainScreen::Update @0x00196e1c case 8: two-phase lerp + pos.y animation.
        float posAlpha;
        if (m_Timer2 <= STATE_8_LERP_THRESHOLD) {
            // Port specific: dt-normalize the per-frame ease (dtN = dt*60) so the menu slide-in
            //   plays at the intended ~60Hz speed regardless of render framerate (binary is frame-based@fixed-60Hz).
            m_Timer2 = 1.0f - (1.0f - m_Timer2) * powf(1.0f - (STATE_8_LERP_RATE * FN::g_DebugTimeScale), dtN);
            posAlpha = m_Timer2;
        } else {
            m_Timer2 += dt;

            if (m_Timer2 > STATE_8_DURATION) {
                // ASM-spec v1.6.1 MainScreen::Update @0x00196e1c case-8 exit:
                // binary sets m_Timer2=0.15f, +0x11c=0.0f, m_State=STATE_CAMERA_ZOOM.
                // Does NOT touch m_PauseAmount/flM_PauseAmount on this path.
                // f0=0.0f means case-0's hold branch is skipped immediately on the next tick,
                // so the slide-in animation starts right away on return.
                m_Timer2 = STATE_8_RESET_TIMER;
                m_IntroHoldTimer = 0.0f;
                LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/SLIDE_IN hold expired");
                m_State = STATE_CAMERA_ZOOM;
            }
            posAlpha = 1.0f;
        }
        const float sizeY_8 = size.y;
        const float tt_8 = sizeY_8 * posAlpha;
        pos.y = (sizeY_8 + 320.0f - 2.0f * tt_8) * 0.5f;
        break;
    }

    case STATE_LEADERBOARD:     // v1.6.1 case 9 @0x0019765c
    case STATE_MORE_GAMES: {    // v1.6.1 case 10 @0x0019765c
        // Defunct: NetworkManager states 9/10 — entity-gate + stubbed LaunchDashboard; v1.6.1 @0x0019765c
        // Binary: gate on GetNumEntities(0)==0; suspend-toggle around LaunchDashboard
        // (state==10 -> dashboard id 3, clears m_pQuitButton); then m_State=0;
        // f0(+0x11c)=0; m_Timer2=-0.85. No DeleteMenuButtons (persisting instance).
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        if ((am ? am->GetNumEntities(0) : 0) == 0) {
            game_work.m_bUpdatesSuspended = 0;
            Mortar::NetworkManager::GetInstance()->LaunchDashboard(
                m_State == STATE_MORE_GAMES ? 3 : 0);  // defunct stub
            if (m_State == STATE_MORE_GAMES) {
                m_pQuitButton = nullptr;
            }
            game_work.m_bUpdatesSuspended = 1;
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/defunct-network state");
            m_State = STATE_CAMERA_ZOOM;
            SetIntroHoldTimer(0.0f);
            m_Timer2 = -0.85f;
        }
        break;
    }

    case STATE_MATCHMAKER: {    // v1.6.1 case 0x10 @0x001975f4
        // Defunct: NetworkManager::OpenMatchmaker — entity-gate + m_Timer2 decay; v1.6.1 @0x001975f4
        // Binary: when GetNumEntities(0)==0, decay m_Timer2 *= 0.85; once <= 0.025:
        // m_Timer2=0, OpenMatchmaker(0,-1,2,2), m_State=0. pos.y follows m_Timer2.
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        if ((am ? am->GetNumEntities(0) : 0) == 0) {
            m_Timer2 *= 0.85f;
            if (m_Timer2 <= 0.025f) {
                m_Timer2 = 0.0f;
                Mortar::NetworkManager::GetInstance()->OpenMatchmaker(0, -1, 2, 2);  // defunct stub
                LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/defunct-matchmaker state");
                m_State = STATE_CAMERA_ZOOM;
            }
        }
        {
            const float sizeY_m = size.y;
            pos.y = (sizeY_m + 320.0f - 2.0f * sizeY_m * m_Timer2) * 0.5f;
        }
        break;
    }

    case STATE_NEWS:            // v1.6.1 case 0xb @0x001975c0
        // Defunct: NetworkManager::UpdateNews — stub returns 0; v1.6.1 @0x001975c0
        // Binary: if (UpdateNews(dt) != 0) break; else m_State=1; f0(+0x11c)=0; m_Timer2=-0.85.
        if (Mortar::NetworkManager::GetInstance()->UpdateNews(dt) != 0) {
            break;
        }
        LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CREATE_BUTTONS), "Update/defunct-news state");
        m_State = STATE_CREATE_BUTTONS;
        SetIntroHoldTimer(0.0f);
        m_Timer2 = -0.85f;
        break;

    case STATE_MODE_SELECT:
    case STATE_MODE_SELECT_2: {
        // v1.6.1 MainScreen::Update @0x00197560 cases 0xe/0xf: decay m_Timer2 and slide pos.y upward.
        const float oldTimer2 = m_Timer2;
        // DIFFERS: original = flat *0.85 (v1.6.1 MainScreen::Update @0x00197560), port scales by g_DebugTimeScale for debug time-scale tooling
        const float decay = 1.0f - (1.0f - STATE_0E_DECAY) * FN::g_DebugTimeScale;
        m_Timer2 *= decay;

        const float sizeY = size.y;
        const float tt = sizeY * m_Timer2;
        pos.y = (sizeY + 320.0f - 2.0f * tt) * 0.5f;

        if (oldTimer2 > STATE_0E_THRESHOLD && m_Timer2 <= STATE_0E_THRESHOLD) {
            CancelNews();  // Defunct: NetworkManager news -- called in the mode-select spawn block; v1.6.1 MainScreen::Update @0x00197560
            GameModeScreen* gms = new GameModeScreen(false);
            // Binary @0x00197594: vtable slot 2 (Init) is dispatched BEFORE HUD::AddControl,
            // same as the cases-3/4 DojoScreen spawn above.
            gms->Init();
            game_work.mHud->AddControl(gms);
        }
        break;
    }

    case STATE_CAMERA_FADE:
        // v1.6.1 MainScreen::Update @0x00197828
        if (game_work.m_PauseAmount < 0.0f) {
            game_work.m_PauseAmount *= 0.75f;
            if (game_work.m_PauseAmount > -0.001f) {
                game_work.m_PauseAmount = 0.0f;
                game_work.bM_bPaused = 0;
                LOG_INFO("SCREEN/MainScreen", "STATE_CAMERA_FADE: timer clamped to 0.0f, levelTransitionFlag cleared");
            }
        }
        break;

    case STATE_LOADING_A:
    case STATE_LOADING_B:
        // v1.6.1 @0x001976b8: m_bUpdatesSuspended=0; m_Field114+=dt*8 (wrap@8); m_State=0; f0=0; m_Timer2=-0.85.
        // No DeleteMenuButtons (persisting instance keeps buttons).
        game_work.m_bUpdatesSuspended = 0;
        m_Field114 += dt * 8.0f;
        if (m_Field114 >= 8.0f) {
            m_Field114 = 0.0f;
        }
        LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/LOADING");
        m_State = STATE_CAMERA_ZOOM;
        SetIntroHoldTimer(0.0f);
        m_Timer2 = -0.85f;
        break;

    case STATE_QUIT_WAIT: {
        // v1.6.1 MainScreen::Update @0x00197700
        if (game_work.m_TutorialControl) {
            game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
        }
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        const int liveEntities = am ? am->GetNumEntities(0) : 0;
        if (liveEntities != 0) {
            break;
        }

        m_pMoreGamesBtn = nullptr;

        const uint8_t qs = SystemManager::GetInstance().GetQuitState();
        if (qs == 2) {
            HitMenuBomb(_Vector3<float>(163.0f, -96.0f, 0.0f));
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_QUIT_BOMB), "Update/QUIT_WAIT qs==2");
            m_State = STATE_QUIT_BOMB;
        } else if (qs == 3) {
            LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_ZOOM), "Update/QUIT_WAIT qs==3 cancelled");
            m_State = STATE_CAMERA_ZOOM;
            SetIntroHoldTimer(0.0f);
            m_Timer2 = 0.15f;
        }
        break;
    }

    case STATE_QUIT_BOMB: {
        // v1.6.1 MainScreen::Update @0x00196e1c case 0x16
        if (game_work.m_TutorialControl) {
            game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
        }
        if (BombFlashFull()) {
            SystemManager::GetInstance().QuitGame();
            // Port specific: no binary counterpart -- Bada's QuitGame tears the app
            // down itself; the SDL/host main loop needs its run flag cleared.
            if (Game* g = Game::GetInstance()) {
                g->running = false;
            }
        }
        break;
    }
    }

    // State-dependent override of the toggle-slide driver. The default arm keeps
    // the -PauseAmount snapshot taken at the top of the function (binary fVar15).
    switch (m_State) {
    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B:
    case STATE_MODE_SELECT:
    case STATE_MODE_SELECT_2:
    case STATE_SLIDE_IN:
    case STATE_MATCHMAKER:      // v1.6.1 @0x00196e1c tail: case 0x10 slides on m_Timer2
        elapsedTime = m_Timer2;
        break;
    case STATE_CAMERA_FADE:
        // v1.6.1 @0x00196e1c tail: fade state forces 0 so the toggles stay hidden
        // (no slide during the camera fade back into gameplay).
        elapsedTime = 0.0f;
        break;
    default:
        break;
    }

    // Toggle button positioning.
    if (pSoundToggle && pMusicToggle) {
        pSoundToggle->pos.y = 135.5f;
        pMusicToggle->pos.y = 135.5f;

        if (elapsedTime <= 0.0f) {
            // Paused (top-center, on the pause overlay): centered, not MapX'd
            // -- PauseScreen's own reveal is explicitly out of widescreen scope.
            pSoundToggle->pos.x = 20.0f;
            pMusicToggle->pos.x = -20.0f;
        } else {
            // Idle main menu (top-right corner): edge-anchored -> MapX.
            // DIFFERS: opt-in widescreen.
            pSoundToggle->pos.x = MapX(216.0f, "menu.sound");
            pMusicToggle->pos.x = MapX(176.0f, "menu.music");
        }

        // ASM-spec v1.6.1 MainScreen::Update @0x00196e1c (tail): t = clamp01(elapsedTime
        // + GetPauseAmount()); the GetPauseAmount() (PauseScreen reveal 0..1) addend shows
        // the toggles top-center (x=+20 sound / -20 music, y=135.5) while paused;
        // slide = 2*size.y*(1-t); SetActive(t > 0.01f).
        float pauseAmount = elapsedTime + GetPauseAmount();
        if (pauseAmount < 0.0f) pauseAmount = 0.0f;
        if (pauseAmount > 1.0f) pauseAmount = 1.0f;

        float slideOffset = size.y * 2.0f * (1.0f - pauseAmount);
        pSoundToggle->m_Active = (pauseAmount > PAUSE_VISIBILITY) ? 1 : 0;
        pMusicToggle->m_Active = (pauseAmount > PAUSE_VISIBILITY) ? 1 : 0;
        pSoundToggle->pos.y += slideOffset;
        pMusicToggle->pos.y += slideOffset;

        // Defunct: SSMP tail — IsSameScreenMultiplayer()&&GetPausedBy() negate toggle pos; v1.6.1 @0x00196e1c
        // Dead in SP (IsSameScreenMultiplayer() always false); shape preserved.
        if (IsSameScreenMultiplayer() && -0.01f < game_work.m_PauseAmount && GetPausedBy()) {
            pSoundToggle->pos = -pos;
            pMusicToggle->pos = -pos;
        }
    }

#ifndef __bada__
    // Port specific: no binary counterpart. Settings button uses the SAME
    // m_Active-gate mechanism + PAUSE_VISIBILITY threshold as pSoundToggle/
    // pMusicToggle immediately above (`pauseAmount > PAUSE_VISIBILITY ? 1 : 0`,
    // slide = size.y*2*(1-t)) -- but DELIBERATELY on raw `elapsedTime` alone,
    // WITHOUT the toggles' `+ GetPauseAmount()` addend.
    //
    // GetPauseAmount() == PauseScreen::GetTime() (~= m_Alpha) is the pause
    // OVERLAY's own reveal progress -- it is HIGH while the player is actually
    // on the pause screen. That addend is precisely what the toggles use to
    // become VISIBLE (slid to top-center) while paused (see commit 104ea8f8,
    // "MainScreen: show music/sfx toggles on pause screen" -- binary-faithful,
    // v1.6.1 shows the audio toggles on the pause overlay on purpose). Settings
    // has no binary counterpart and must be visible ONLY on the actual main
    // menu, so it must NOT pick up that pause-reveal signal -- copying the
    // toggle expression verbatim (including +GetPauseAmount()) would show
    // settings on the pause screen right along with them.
    //
    // Raw elapsedTime is exactly the "is MainScreen actually the foreground
    // screen" signal: MainScreen::Hide() (called once, at the game-start
    // transition into gameplay/pause -- see PauseScreen::SkipToPause) sets
    // m_State = STATE_CAMERA_FADE and nothing changes it back while off-main
    // (no button-click callback fires without user input on the main menu),
    // so the switch above pins elapsedTime = 0.0f (STATE_CAMERA_FADE case)
    // for the entire time MainScreen is not foreground -- covering gameplay,
    // pause, and (transitively, since dojo/gameover are reached via the same
    // gameplay task) dojo and gameover too. On the idle main menu itself,
    // elapsedTime is the normal -m_PauseAmount snapshot (~1.0 at rest).
    //
    // Retimed to lock-step with the three ring MenuButtons (m_pGameModeButton
    // NEW GAME / m_pStoreButton DOJO / m_pQuitButton QUIT) and with
    // MainScreen's own logo/content, which slides on this exact same
    // `elapsedTime` value (see the pos.y formulas in STATE_CAMERA_ZOOM/
    // STATE_CREATE_BUTTONS/STATE_GAME_START/STATE_DOJO_WAIT_A/B/
    // STATE_MODE_SELECT(_2), all `(sizeY+320-2*sizeY*elapsedTime)*0.5`).
    //
    // `elapsedTime` is ALREADY the single decayed/eased factor for both
    // directions by the time we reach this point in Update():
    //  - Enter: elapsedTime = -game_work.m_PauseAmount (top of Update), eased
    //    every frame via the CAMERA_LERP_RATE ramp in STATE_CAMERA_ZOOM/
    //    STATE_CREATE_BUTTONS -- the same ramp the rings' own creation timing
    //    rides on (CreateButtons() runs every frame in STATE_CAMERA_ZOOM).
    //  - Leave: the state-dependent override switch immediately above
    //    (STATE_DOJO_WAIT_A/B, STATE_MODE_SELECT(_2), STATE_SLIDE_IN,
    //    STATE_MATCHMAKER) overwrites elapsedTime = m_Timer2. Ring taps
    //    (GameModeCallback/AboutCallback) arm m_Timer2 = 1.0f the instant a
    //    ring is sliced; STATE_MODE_SELECT(_2) decays it *0.85/frame and
    //    STATE_DOJO_WAIT_A/B decays it *0.75/frame (gated on fruitCount==0,
    //    i.e. once the sliced fruit has dropped) -- the SAME m_Timer2 that
    //    drives MainScreen's own pos.y slide-out. So elapsedTime falls in
    //    lockstep with the rings/logo starting the exact slice frame.
    //
    // Previously this block ran elapsedTime through an INDEPENDENT eased
    // follower (m_SettingsVisibility, own CAMERA_LERP_RATE lerp) plus a fixed
    // SETTINGS_SHOW_DELAY hold on the show side. Both are removed: since
    // elapsedTime is already the fully-eased/decayed factor, re-easing it a
    // second time only added a trailing lag in both directions, and the
    // rings have no real re-creation delay to mirror (Hide()/
    // DeleteMenuButtons() is dead code -- the button instances persist across
    // every return to main, so there is no grow-in hold after the first
    // launch). Settings now reads elapsedTime directly, clamped 0..1 (see
    // growFactor below).
    //
    // Port specific: no binary counterpart for this button (see ctor-site note
    // above). m_pSettingsButton is a toggle MenuButton (m_FruitType < 0);
    // MenuButton::Update unconditionally does `size = m_RestScale` every frame
    // (src/hud/MenuButton.cpp ~line 735-737) AFTER MainScreen::Update runs, so
    // any direct `size`/pos-slide write here is clobbered before Draw -- the
    // only surviving levers are m_RestScale (copied into size) and pos. The
    // m_AnimPhase grow-in ease (MenuButton.cpp ~706-717) is fruit-only
    // (m_FruitType >= 0) and never runs for toggles, so there is no native
    // pop-in animation to reuse; growFactor below fills that gap by driving
    // m_RestScale's magnitude (and a corner slide) from the same live ring
    // scale the three ring MenuButtons use, frame-syncing the settings icon
    // with the rings' grow/shrink.
    if (m_pSettingsButton) {
        // Port specific: settings button (a toggle MenuButton -- can only be scaled
        // via m_RestScale, since MenuButton::Update copies m_RestScale->size each
        // frame) mirrors the ring MenuButtons' OWN live scale so it grows/shrinks in
        // exact lock-step with them. Each ring's instantaneous 0..1 scale is
        // size.y / m_RestScale.y (MenuButton::Update grow/shrink via m_AnimPhase).
        // Only the sliced ring shrinks; on return to main all three re-grow from 0.
        // max() tracks whichever is animating; null-guard (rings are nulled at
        // slice-callback time / when shrink completes) + divide-by-zero guard + clamp.
        float ringFactor = 0.0f;
        const MenuButton* rings[3] = { m_pGameModeButton, m_pStoreButton, m_pQuitButton };
        for (int i = 0; i < 3; ++i) {
            const MenuButton* b = rings[i];
            if (!b) continue;
            float ry = b->m_RestScale.y;
            if (ry <= 0.0001f) continue;
            float f = b->size.y / ry;
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            if (f > ringFactor) ringFactor = f;
        }
        float growFactor = ringFactor;

        // SLIDE-ONLY (no shrink): pos DOES survive MenuButton::Update for
        // toggles (it only clobbers size), so slide the button toward its
        // bottom-left corner as it hides (growFactor 1->0) and back to rest as
        // it grows (0->1), frame-synced with the rings. m_RestScale is held at
        // full size -- the button stays 48px the whole time and simply slides
        // off/onto the corner.
        // DIFFERS: opt-in widescreen -- MapX the bottom-left corner anchor; the
        // slide distance itself is a fixed-unit offset, not spread.
        float slide = (1.0f - growFactor) * kSettingsSlideOut;
        m_pSettingsButton->pos.x = MapX(POS_SETTINGS_TOGGLE.x, "menu.settings") - slide;
        m_pSettingsButton->pos.y = POS_SETTINGS_TOGGLE.y - slide;

        m_pSettingsButton->m_RestScale = kSettingsRestScale;
        m_pSettingsButton->m_Active = (growFactor > PAUSE_VISIBILITY) ? 1 : 0;
    }
#endif // !defined(__bada__)

    // Binary @ 0x00195a58: UpdateScreenElements(dt, stateVar).
    // dt = frame delta (used for bounce physics integration and tute gate).
    // stateVar = transition timer (used for settle condition: time > 0.99).
    UpdateScreenElements(dt, elapsedTime);
}

// v1.6.1 MainScreen: Game+0x0c is the camera-transition timer (game_work.m_PauseAmount).
float MainScreen::GetCameraTransition() const { return game_work.m_PauseAmount; }
void  MainScreen::SetCameraTransition(float v) { game_work.m_PauseAmount = v; }

// Helper: setup world matrix for a textured quad at given position
static void SetupQuadMatrix(MatrixManager& mm, const _Vector3<float>& hudScale,
                            float w, float h, const _Vector3<float>& drawPos) {
    (void)hudScale;
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
    mat.GlobalTranslate44(drawPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();
}

// ASM-spec v1.6.1 MainScreen::Draw @0x001993ac
void MainScreen::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

    // Binary early-return: cmp r3,#0xd / cmpne r3,#0x11 — state 0xd has no
    // name in the port enum yet (unreachable today; the binary gave it a
    // dedicated loading-spinner position, see DrawLoadingSymbol).
    if (m_State == STATE_CAMERA_FADE || m_State == 0x0d) return;
    if ((m_State == STATE_DOJO_WAIT_A || m_State == STATE_DOJO_WAIT_B) &&
        m_Timer2 == 0.0f) return;

    Game* game = Game::GetInstance();
    MatrixManager& mm = MatrixManager::GetInstance();

    // DIFFERS: opt-in widescreen enhancement (main-menu logo cluster).
    // Pins the FN logo + "SLICE FRUIT TO BEGIN" parchment toward the LEFT
    // edge as the field widens, instead of leaving them centred with dead
    // space on their left. dxLogo is a fixed gap to the new left edge
    // (-HalfWidth()); dyLogo follows the tilt of the "blurry_backing" shade
    // triangle drawn immediately below (site 1a) -- the logo cluster visually
    // sits ON that tilted backdrop, so the correct slope is the backdrop's
    // own geometric tilt, not the offset between two unrelated rest-position
    // anchors (an earlier version derived slope from m_LogoPos vs
    // m_NinjaTextX/Y and got both the sign and magnitude wrong -- those two
    // anchors don't lie on the backdrop's slant).
    //
    // Backdrop tilt, from the shadeVerts local coords below (edge A(-1,-0.6875)
    // -> B(3.5,1.0)), scaled by the quad's Scale(size.x, size.y) transform:
    //   dx_local = 3.5-(-1) = 4.5, dy_local = 1.0-(-0.6875) = 1.6875
    //   dx_world = dx_local*size.x = 4.5*480  = 2160
    //   dy_world = dy_local*size.y = 1.6875*138 = 232.875
    //   slope = dy_world/dx_world = 232.875/2160 = 69/640 = 0.1078125
    // With this slope, dy = SLOPE*dx: shifting LEFT (dx<0) yields dy<0 (Y
    // decreases) -- moving the elements DOWN, matching the backdrop's downward
    // slant toward the left edge.
    static const float LOGO_SLOPE = 69.0f / 640.0f;  // 0.1078125; backdrop tilt (blurry_backing shade triangle), see derivation above
    float dxLogo = 0.0f, dyLogo = 0.0f;
#ifndef __bada__
    if (Layout::IsWideLayout()) {
        dxLogo = -(Layout::HalfWidth() - 240.0f);  // fixed gap to the left edge
        dyLogo = LOGO_SLOPE * dxLogo;
    }
#endif // !defined(__bada__)

    // 1+2. fruit_text guard (GOT+0x6FCC, binary: gate on the fruit_text GLOBAL --
    // there is no fruit_text member; +0xe4 holds slice_fruit, drawn at site 4).
    if (m_fruitTex.IsValid()) {
        // 1a. Background shade (blurry_backing.tex) — angled triangle
        if (s_blurTex.IsValid()) {
            s_blurTex->Set();
            SetupQuadMatrix(mm, hudScale, size.x, size.y, pos);

            static const uint32_t kShadeCol = 0x80000000u;
            QUADCUSTOMVERTEX shadeVerts[3] = {
                { -1.0f, -0.6875f, 0.0f,  0,0,1,  kShadeCol,  0.0f,      0.0078125f },
                {  3.5f,  1.0f,    0.0f,  0,0,1,  kShadeCol,  1.0f,      0.0078125f },
                { -1.0f,  1.0f,    0.0f,  0,0,1,  kShadeCol,  0.065694f, 1.0f       },
            };
            if (game) game->renderer.DrawTriList(shadeVerts, 3);

            s_blurTex->UnSet();
        }

        // 1b. fruit_text logo drawn at {m_NinjaTextX, m_NinjaTextY, m_NinjaTextZ}
        // Binary: TranslateMatrix(&this+0xF8) reads 3 consecutive floats at binary +0xF8..+0x100.
        // Every quad in this function draws with Colour::White (GOT @0x199804)
        // in the binary — never m_DrawColour.
        static const float FRUIT_TEXT_SCALE = 0.85f;  // DAT_0014d838
        m_fruitTex->Set();
        _Vector3<float> fruitTextDrawPos(m_NinjaTextX + dxLogo, m_NinjaTextY + dyLogo, m_NinjaTextZ);
        SetupQuadMatrix(mm, hudScale,
            (float)m_fruitTex->GetWidth() * FRUIT_TEXT_SCALE,
            (float)m_fruitTex->GetHeight() * FRUIT_TEXT_SCALE,
            fruitTextDrawPos);
        if (game) game->renderer.DrawQuad(Colour::White);
        m_fruitTex->UnSet();
    }

    // 3. ninja_text drawn at {m_BounceVel, m_BounceY, m_BounceZ} (+0x104..+0x10C binary)
    // Binary: TranslateMatrix(&this+0x104) reads 3 consecutive floats.
    // m_BounceY is the bounce POSITION (the Y of ninja_text in Draw).
    if (m_ninjaTex.IsValid()) {
        // dxLogo/dyLogo shift the whole logo cluster together along the
        // backdrop's tilt (see dyLogo derivation above); dyLogo composes
        // additively with the bounce physics baseline (m_BounceY), same as
        // fruit_text composes dyLogo with its own rest Y.
        _Vector3<float> ninjaDrawPos(m_BounceVel + dxLogo, m_BounceY + dyLogo, m_BounceZ);
        m_ninjaTex->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_ninjaTex->GetWidth(), (float)m_ninjaTex->GetHeight(),
            ninjaDrawPos);
        if (game) game->renderer.DrawQuad(Colour::White);
        m_ninjaTex->UnSet();
    }

    // 4. Parchment frame (slice_fruit.tex) at m_LogoPos, then BakedStringBox on top.
    // logoPos composes the widescreen left-anchor (dxLogo/dyLogo) additively
    // with the existing m_Lean intro-animation offset already baked into
    // m_LogoPos -- same anchor point, just shifted, so both keep tracking
    // together during the lean-in.
    _Vector3<float> logoPos = m_LogoPos + _Vector3<float>(dxLogo, dyLogo, 0.0f);
    if (m_TexSliceFruit.IsValid()) {
        m_TexSliceFruit->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_TexSliceFruit->GetWidth(), (float)m_TexSliceFruit->GetHeight(),
            logoPos);
        if (game) game->renderer.DrawQuad(Colour::White);
        m_TexSliceFruit->UnSet();

        // Binary draws the instruction string only inside this
        // parchment-texture-valid block (and with no null check).
        if (m_pSliceInstrBox) {
            _Vector3<float> instrPos = logoPos + _Vector3<float>(-4.0f, -4.0f, 0.0f);
            m_pSliceInstrBox->SetTranslation(instrPos, 1);
            m_pSliceInstrBox->Draw(_Vector2<float>(1.0f, 1.0f), 8.0f, 1);
        }
    }

    // 5. Loading symbol (v1.6.1 Draw @0x001993ac: states 0x13/0x14 only)
    if (m_State == STATE_LOADING_A || m_State == STATE_LOADING_B) {
        DrawLoadingSymbol(const_cast<float*>(&hudScale.x));
    }

    // 6. m_TexCommingSoon (comming_soon overlay) — drawn when valid AND m_pGameModeButton exists.
    // Scale scalar @0x001999a0 is m_pGameModeButton->size.x ([r6,#0xa0] + 0x20),
    // so the overlay tracks the button's animated size; translate = (148, 7, 0)
    // (@0x001999c0, pool 0x199808).
    if (m_TexCommingSoon.IsValid() && m_pGameModeButton != NULL) {
        float csW = (float)m_TexCommingSoon->GetWidth();
        float csH = (float)m_TexCommingSoon->GetHeight();
        float btnSize = m_pGameModeButton->size.x;
        float scaleX = btnSize * 0.5f;
        float scaleY = btnSize * 0.5f * (csH / csW);
        _Vector3<float> csPos(148.0f, 7.0f, 0.0f);
        m_TexCommingSoon->Set();
        SetupQuadMatrix(mm, hudScale, scaleX, scaleY, csPos);
        if (game) game->renderer.DrawQuad(Colour::White);
        m_TexCommingSoon->UnSet();
    }
}

// ASM-verified: v1.6.1 MainScreen::UpdateScreenElements @0x00195a58
//
// Binary signature: (float dt, float stateVar)
//   dt       = frame delta (used for bounce physics integration and tute gate).
//              Bounce: vel += dt * -55; pos += vel * dt * 15.
//              Since dt ≈ 0.0167, these are small per-frame increments.
//   stateVar = state-dependent timer (used for settle gate: stateVar > 0.99).
//              Menu idle: stateVar = -m_PauseAmount ≈ 1.0 → settle active.
//              Transitions: stateVar = m_Timer2 (decays from 1.0) → settle only early.
//   tute = static local; set to 1.0 when dt > 0 (always during gameplay), set to 0.0
//          ONLY by the floor-bounce settle path. NEVER reset to 0 when dt drops to 0.
//
// v1.6.1 field semantics:
//   m_StateTimer (+0xF8 port / +0x110 binary) = BOUNCE VELOCITY accumulator
//   m_BounceY    (+0xF0 port / +0x108 binary) = bounce POSITION (ninja_text Y in Draw)
//   m_BounceVel  (+0xEC port / +0x104 binary) = 60.0 decorative; ninja_text X in Draw
//   m_NinjaTextX (+0xE0 port / +0xF8 binary)  = fruit_text X (-120/frame)
//   m_NinjaTextY (+0xE4 port / +0xFC binary)  = fruit_text Y (pos.y+18)
//   m_NinjaTextZ (+0xE8 port / +0x100 binary) = 0
//   m_BounceZ    (+0xF4 port / +0x10C binary) = 0; ninja_text Z
//   m_Lean       (+0xDC port / +0xF4 binary)  = logo lean lerp (init 1.0)
//   m_LogoPos    (+0xD0 port / +0xE8 binary)  = fruit_text + sliceInstrBox draw pos
//
// Binary constants (literal pool):
//   CLAMP_THRESHOLD    = 0.04   (clamp dt to max 0.04)
//   BOUNCE_GRAVITY     = -55.0  (gravity per unit of dt)
//   ELAPSED_THRESHOLD  = 0.99   (settle gate on stateVar)
//
void MainScreen::UpdateScreenElements(float dt, float transitionTimer) {
    static const float MAX_DT            = 0.04f;    // Clamp dt to max 0.04
    static const float BOUNCE_GRAVITY    = -55.0f;   // Gravity per unit of dt
    static const float ELAPSED_THRESHOLD = 0.99f;    // Settle gate on stateVar

    // Binary's `tute` is a FUNCTION-LOCAL STATIC, not a member: it survives
    // across screen re-entry and is only ever written by the two sites below.
    // v1.6.1 MainScreen::UpdateScreenElements @0x00195a58.
    static float s_Tute = 1.0f;

    if (dt > MAX_DT) {
        dt = MAX_DT;
    }

    // m_NinjaTextZ and m_BounceZ = 0.0
    m_NinjaTextZ = 0.0f;
    m_BounceZ    = 0.0f;

    // fruit_text X = -120.0 (constant per binary @ 0x00195a58)
    m_NinjaTextX = -120.0f;

    // m_BounceVel = 60.0 decorative (set per-frame; also used as ninja_text X in Draw)
    m_BounceVel = 60.0f;

    // Bounce physics: per-frame integration with dt.
    // m_StateTimer = VELOCITY accumulator; m_BounceY = POSITION integrator.
    float newVel = m_StateTimer + dt * BOUNCE_GRAVITY;
    m_StateTimer = newVel;

    float newPos = m_BounceY + newVel * dt * 15.0f;
    m_BounceY = newPos;

    float floorPos = pos.y + 18.0f;

    // fruit_text Y = pos.y + 18 (the floor level)
    m_NinjaTextY = floorPos;

    // Binary: tute = 1.0 while dt > 0 (which is always true during gameplay).
    // tute is a static local — it is NEVER reset to 0 when dt drops to 0.
    // The only path to tute = 0 is the floor-bounce settle below.
    if (dt > 0.0f) {
        s_Tute = 1.0f;
    }
    // NOTE: No else branch. Binary's static tute keeps its last value when dt <= 0,
    // unlike the old port code which incorrectly reset it to 0.

    // Bounce floor: floorLimit = pos.y + 18 - 15 = pos.y + 3
    float floorLimit = floorPos - 15.0f;
    if (newPos < floorLimit) {
        m_BounceY    = floorLimit;
        m_StateTimer = newVel * BOUNCE_LOSS;

        // Settle: floor hit + low velocity + stateVar settled + dt active
        if (fabsf(newVel * BOUNCE_LOSS) < BOUNCE_SETTLE &&
            transitionTimer > ELAPSED_THRESHOLD &&
            dt > 0.0f) {
            m_StateTimer = 0.0f;
            s_Tute = 0.0f;
        }
    }

    // m_Lean lerp: m_Lean += (tute - m_Lean) * 0.25
    m_Lean += (s_Tute - m_Lean) * ALPHA_LERP_RATE * FN::g_DebugTimeScale;

    // m_LogoPos = (-175, 26, 0) + (-120, -17, 0) * m_Lean * 2.0
    // fruit_text + sliceInstrBox draw position (binary @ 0x00195a58)
    _Vector3<float> base(-175.0f, 26.0f, 0.0f);
    _Vector3<float> offset(-120.0f, -17.0f, 0.0f);
    m_LogoPos = base + offset * m_Lean * 2.0f;
}

// Port-only helper -- no binary counterpart; binary keeps menu buttons alive across
// gameplay (persisting MainScreen). Do not call on the game->menu path.
void MainScreen::DeleteMenuButtons() {
    RemoveButton(m_pGameModeButton);
    RemoveButton(m_pStoreButton);
    RemoveButton(m_pMoreGamesBtn);
    RemoveButton(m_pQuitButton);
#ifndef __bada__
    // Port specific: no binary counterpart -- see m_pSettingsButton note in
    // MainScreen.h. Torn down alongside the other menu buttons; Update()'s
    // null-guarded creation block rebuilds it on the next case-0/1 pass.
    RemoveButton(m_pSettingsButton);
#endif // !defined(__bada__)
}

// v1.6.1 MainScreen::Hide @0x0014ad04
void MainScreen::Hide() {
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_CAMERA_FADE), "Hide");
    m_State = STATE_CAMERA_FADE;
    pos = _Vector3<float>(0.0f, 0.0f, 0.0f);
    // Port specific: no binary counterpart. m_pSettingsButton is NOT torn down
    // here (an earlier attempt did RemoveButton() here, which nulled the
    // pointer -- but MainScreen::Update keeps running every frame while
    // gameplay/pause is active, since the menu is the persisting PAUSED Game
    // task, not a separate suspended screen. The null-guarded creation block
    // in Update() immediately rebuilt the button off-main the very next frame,
    // and since Hide() only fires ONCE at the game-start transition, nothing
    // tore it down again -- it stayed alive and visible for the rest of
    // gameplay/pause, a regression. Visibility is governed ENTIRELY by the
    // per-frame m_Active gate in Update() (see POS_SETTINGS_TOGGLE block),
    // same pattern as pSoundToggle/pMusicToggle: those are also created via
    // an unconditional null-guard and never torn down by Hide() either --
    // being on-main vs off-main is purely an m_Active question, not a
    // create/destroy question.
}

void MainScreen::RemoveButton(MenuButton*& btn) {
    if (btn) {
        btn->m_bPendingRemoval = 1;
        btn = nullptr;
    }
}

// v1.6.1 MainScreen::CreateButtons @0x001961f8
// strb #1,[r0,#0x7c] at 0x0019620c is the FIRST instruction after prologue --
// flag is set unconditionally, even on the BombHitTimer early-out frames.
// Button CREATION is gated (per-pointer null guards + BombHitTimer < 1.45);
// the flag write is not. Called every frame from case 0 (cheap due to
// per-pointer guards) and gated on m_ButtonsCreatedFlag==0 from case 1.
void MainScreen::CreateButtons() {
#if defined(FN_BLOCK_PRELOAD)
    // Task #36 Stage 1 -- block-enter hook (log-only labelling, see
    // tmp/wii/loader-blueprint.md section 2/7). Called every frame from
    // Update() case 0/1 (per-pointer guards make repeats cheap); a plain
    // int write here is likewise cheap to repeat.
    fn::wii::SetCurrentBlock(fn::wii::RES_BLOCK_MENU);
#endif
    m_ButtonsCreatedFlag = 1;

    if (!game_work.mHud) return;

    if (game_work.m_BombHitTimer >= 1.45f) return;

    if (m_pGameModeButton == nullptr) {
        // ASM-spec v1.6.1 MainScreen::CreateButtons @0x001961f8: ring = m_RingTex[3] + SetText(GETSTRING(0x398))
        Mortar::SmartPtr<Mortar::Texture> texNewGame = game_work.m_RingTex[3];
        m_pGameModeButton = new MenuButton();
        m_pGameModeButton->m_Texture = texNewGame;
        // DIFFERS: opt-in widescreen -- MapX the right-of-center ring anchor.
        m_pGameModeButton->Init(_Vector3<float>(MapX(POS_PLAY_BUTTON.x, "menu.play"), POS_PLAY_BUTTON.y, POS_PLAY_BUTTON.z),
            Mortar::Delegate0<void>::Make(this, &MainScreen::GameModeCallback), 3, _Vector3<float>(0,0,0), nullptr);
        if (texNewGame.IsValid()) {
            m_pGameModeButton->m_RestScale.x = (float)(texNewGame->GetWidth()  + 1);
            m_pGameModeButton->m_RestScale.y = (float)(texNewGame->GetHeight() + 1);
            m_pGameModeButton->m_RestScale.z = 1.0f;
        }
        m_pGameModeButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
        m_pGameModeButton->m_RemoveCallback =
            Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
        // ASM-spec v1.6.1 MainScreen::CreateButtons @0x001961f8: Play(GameMode) button sets m_HitInsetX=m_HitInsetY=-50.0.
        m_pGameModeButton->m_HitInsetX   = -50.0f;
        m_pGameModeButton->m_HitInsetY   = -50.0f;
        m_pGameModeButton->m_ShakeScale.x = 0.5f;
        m_pGameModeButton->m_GrowInTimer = 0.25f;
        m_pGameModeButton->SetText(
            GETSTRING_CAST_0(LSTR_NEW_GAME),
            game_work.m_RingColours[4],
            game_work.m_RingColours[5],
            58.0f, 12.0f, true, true);
        game_work.mHud->AddControl(m_pGameModeButton);

        if (game_work.m_TutorialControl)
            game_work.m_TutorialControl->ResetTutePos(m_pGameModeButton);
    }

    if (m_pStoreButton == nullptr) {
        // ASM-spec v1.6.1 MainScreen::CreateButtons @0x001961f8: ring = m_RingTex[8] + SetText(GETSTRING(0x397))
        m_pStoreButton = new MenuButton();
        m_pStoreButton->m_Texture = game_work.m_RingTex[8];
        // DIFFERS: opt-in widescreen -- MapX the left-of-center ring anchor.
        m_pStoreButton->Init(_Vector3<float>(MapX(POS_DOJO_BUTTON.x, "menu.dojo"), POS_DOJO_BUTTON.y, POS_DOJO_BUTTON.z),
            Mortar::Delegate0<void>::Make(this, &MainScreen::AboutCallback),
            Fruit::FruitType("mango", false), _Vector3<float>(0,0,0), nullptr);
        m_pStoreButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
        m_pStoreButton->m_RemoveCallback =
            Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
        if (m_pStoreButton->m_pTrackedFruit) {
            m_pStoreButton->m_pTrackedFruit->scale = m_pStoreButton->m_pTrackedFruit->scale * 0.9f;
        }
        m_pStoreButton->m_RestScale   = m_pStoreButton->m_RestScale * 1.05f;
        m_pStoreButton->m_ShakeScale.x = 0.5f;
        m_pStoreButton->m_GrowInTimer  = 0.25f;
        m_pStoreButton->SetText(
            GETSTRING_CAST_0(LSTR_DOJO_TITLE),
            game_work.m_RingColours[6],
            game_work.m_RingColours[7],
            50.0f, 12.0f, true, true);
        game_work.mHud->AddControl(m_pStoreButton);
        // v1.6.1 MainScreen::CreateButtons @0x001961f8: badge set at creation (and refreshed in Update case 1).
        m_pStoreButton->SetNewSymbol(ItemManager::GetInstance()->AreNewItems());
    }

    // v1.6.1 @0x001961f8: quit button recreated per-frame via if(pX==nullptr) guard.
    if (m_pQuitButton == nullptr) {
        CreateQuitButton();
    }
}

void MainScreen::CreateQuitButton() {
    if (!game_work.mHud) return;

    // ASM-spec v1.6.1 MainScreen::CreateButtons @0x001961f8: ring = m_RingTex[16] + SetText(GETSTRING(0x35f))
    Mortar::SmartPtr<Mortar::Texture> texQuit = game_work.m_RingTex[16];

    m_pQuitButton = new MenuButton();
    m_pQuitButton->m_Texture = texQuit;
    // Port specific: no confirmed binary counterpart at this call site (v1.6.1
    // CreateQuitButton, part of MainScreen::CreateButtons @0x001961f8) -- not
    // disassembly-checked in this pass (#84). Flagged for follow-up: other
    // screens' equivalent back/quit buttons DO have an ASM-confirmed store of
    // this same field (GameModeScreen::CreateMenuItems @0x0013e86a, ShopScreen
    // @0x0015e3c6), so this may in fact be genuine binary behaviour not yet
    // located here rather than a port addition -- don't treat as settled.
    m_pQuitButton->m_bRespondsToBackKey = 1;
    int fruitCount = g_FruitInfoCount;
    // ASM-spec v1.6.1 MainScreen::CreateButtons @0x0019687c: quit button Init pos = (0,0,0)
    m_pQuitButton->Init(_Vector3<float>(0.0f, 0.0f, 0.0f),
        Mortar::Delegate0<void>::Make(this, &MainScreen::QuitGamesCallback), fruitCount, _Vector3<float>(0,0,0), nullptr);
    m_pQuitButton->m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    m_pQuitButton->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
    m_pQuitButton->SetText(
        GETSTRING_CAST_0(LSTR_QUIT),
        game_work.m_RingColours[0],
        game_work.m_RingColours[1],
        35.0f, 10.0f, true, true);
    // ASM-spec v1.6.1 MainScreen::CreateButtons @0x00196a5c-0x00196b3c:
    // Quit button is placed + scaled via m_HudScale (not pos). GetAdjustedPos =
    // pos + Vec3(480,320,0)*m_HudScale = (180,-96,0).
    // DIFFERS: opt-in widescreen -- this is the right-edge back/quit bomb button
    // (red ring, m_RingTex[16], LSTR_QUIT text, back-key responder). Back/quit
    // buttons edge-anchor universally; GetAdjustedPos() feeds BOTH Draw and the
    // hit-test rect (MenuButton.cpp ~line 809) from pos+480*m_HudScale.x, so MapX
    // the pre-scale x (180) and re-derive the HudScale fraction from the mapped
    // value (identity divide-by-480 at __bada__/non-wide, matching the literal
    // 0.375f exactly).
    m_pQuitButton->m_HudScale.x = MapX(180.0f, "menu.quit") / 480.0f;    // 0.5 * 0.75f  @0x00196a74
    m_pQuitButton->m_HudScale.y = -0.3f;     // -0.5 * 0.6f  @0x00196a9c
    m_pQuitButton->m_bBackdropActive = 1;    // @0x00196a5c
    m_pQuitButton->m_GrowInTimer = 0.25f;    // @0x00196b3c
    game_work.mHud->AddControl(m_pQuitButton);
}

// v1.6.1 MainScreen::ButtonDeleted @0x0014acc0
void MainScreen::ButtonDeleted(HUDControl* ctrl) {
    if (ctrl == m_pStoreButton)    m_pStoreButton    = nullptr;
    if (ctrl == m_pGameModeButton)    m_pGameModeButton    = nullptr;
    if (ctrl == m_pQuitButton) m_pQuitButton = nullptr;
    if (ctrl == m_pMoreGamesBtn)  m_pMoreGamesBtn  = nullptr;
}

// --- Callbacks ---

// v1.6.1 MainScreen::GameModeCallback @0x0014b068
void MainScreen::GameModeCallback() {
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_MODE_SELECT), "GameModeCallback");
    m_State = STATE_MODE_SELECT;
    m_Timer2 = 1.0f;
    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
    FruitSaveData::DownloadTweaks();  // defunct stub
    m_pQuitButton = nullptr;
    // Binary @ 0x0014b020: re-seed engine PRNG with m_FrameTimer.
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
}

// Defunct: orphaned callback in shipping binary -- v1.6.1 @0x0014c384
// ZERO inbound xrefs. STATE_GAME_START is genuinely unreachable in shipping FruitNinja.exe.
// Body retained for vtable / layout fidelity.
void MainScreen::NewGameCallback() {
    CancelNews();  // defunct stub
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_GAME_START), "NewGameCallback");
    m_State = STATE_GAME_START;
    // ASM-verified: 2026-05-08 v1.6.1 binary @ 0x0014c3ce (re-analyst).
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay(
            "Game-start", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
}

// v1.6.1 MainScreen::AboutCallback @0x0014afc4
void MainScreen::AboutCallback() {
    CancelNews();  // defunct stub
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_DOJO_WAIT_B), "AboutCallback");
    m_State = STATE_DOJO_WAIT_B;
    m_Timer2 = 1.0f;
    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
    // v1.6.1 AboutCallback @0x195d88: clears m_pQuitButton (+0xa8) and pToggleA (+0xb0).
    m_pQuitButton = nullptr;
    pToggleA = nullptr;
}

// v1.6.1 MainScreen::SoundCallback @0x00195d14
void MainScreen::SoundCallback() {
    game_work.m_bSoundOn = !game_work.m_bSoundOn;
    Mortar::SoundManager::GetInstance().SetSFXVolume(
        game_work.m_bSoundOn ? SOUND_VOLUME_ON : 0.0f);
}

// v1.6.1 MainScreen::MusicCallback @0x00195988
void MainScreen::MusicCallback() {
    game_work.m_bMusicOn = !game_work.m_bMusicOn;
}

#ifndef __bada__
// Port specific: no binary counterpart. Opens/closes the SettingsScreen
// modal via SettingsScreen::Toggle().
void MainScreen::SettingsCallback() {
    SettingsScreen::Toggle();
}
#endif // !defined(__bada__)

// v1.6.1 MainScreen::LeaderboardsCallback @0x0014b010
void MainScreen::LeaderboardsCallback() {
    CancelNews();  // defunct stub
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_LEADERBOARD), "LeaderboardsCallback");
    m_State = STATE_LEADERBOARD;
}

// v1.6.1 MainScreen::MoreGamesCallback @0x0014b000
void MainScreen::MoreGamesCallback() {
    CancelNews();  // defunct stub
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_MORE_GAMES), "MoreGamesCallback");
    m_State = STATE_MORE_GAMES;
}

// ASM-verified: v1.6.1 MainScreen::QuitGamesCallback @0x00196008 (re-analyst).
void MainScreen::QuitGamesCallback() {
    SystemManager::GetInstance().RequestQuit();

    if (m_pQuitButton && m_pQuitButton->m_pTrackedFruit) {
        Bomb* bomb = static_cast<Bomb*>(
            static_cast<Mortar::Entity*>(m_pQuitButton->m_pTrackedFruit));
        bomb->m_bMovement = 1;
        // -(Vector3::UnitY) * 10.0 -- downward accel override (replaces Bomb::Init
        // -12.0 gravity); decelerates the ClearMenuItems upward pop so the bomb
        // falls off-screen. Port previously had +10 (up) -> bomb flew upward forever.
        bomb->m_AccelForce = _Vector3<float>(0.0f, -10.0f, 0.0f);
    }

    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_QUIT_WAIT), "QuitGamesCallback");
    m_State = STATE_QUIT_WAIT;
}


// @ 0x0016bbb0
void MainScreen::DrawPostEffects() {
    // TODO: implement -- post-effect overlays (score flash, bonus anim, etc.)
}

// ASM-spec v1.6.1 MainScreen::DrawLoadingSymbol @0x00198fd4
// Byte-for-byte the same 8-wedge spinner algorithm as the GameOverScreen
// state-0xe halo (GameOverScreen::DrawOrder @0x00186484): radial quads between
// r = 0.5 and r = 0.6*0.5 with perpendicular half-width SinIdx(a+0x3ffc)*0.075,
// UVs sweeping (0,0)-(1,1) across each wedge (samples blurry_backing), constant
// scale Vector3::One * 64.0 (0x42800000). Position @0x00199174..:
//   state 0x14 -> (168, -106, 0); state 0x0d -> (0, -50, 0);
//   else (incl. 0x13) -> (148, 7, 0).
void MainScreen::DrawLoadingSymbol(float* hudScale) {
    (void)hudScale;  // binary scale is the constant 64.0, not hudScale-derived
    if (!s_blurTex.IsValid()) return;

    static QUADCUSTOMVERTEX s_verts[48];
    static bool s_built = false;
    if (!s_built) {
        for (int wedge = 0; wedge < 8; ++wedge) {
            const uint16_t baseAng = (uint16_t)(wedge * 0x1FFE);
            const float s0 = SinIdx(baseAng) * 0.5f;
            const float c0 = CosIdx(baseAng) * 0.5f;
            const float s1 = SinIdx((uint16_t)(baseAng + 0x3FFC)) * 0.075f;
            const float c1 = CosIdx((uint16_t)(baseAng + 0x3FFC)) * 0.075f;

            QUADCUSTOMVERTEX* v = &s_verts[wedge * 6];
            v[0].x = s0 - s1;        v[0].y = c0 - c1;        v[0].u = 0.0f; v[0].v = 0.0f;
            v[1].x = s0 + s1;        v[1].y = c0 + c1;        v[1].u = 1.0f; v[1].v = 0.0f;
            v[2].x = s0*0.6f - s1;   v[2].y = c0*0.6f - c1;   v[2].u = 0.0f; v[2].v = 1.0f;
            v[3].x = s0 + s1;        v[3].y = c0 + c1;        v[3].u = 1.0f; v[3].v = 0.0f;
            v[4].x = s0*0.6f - s1;   v[4].y = c0*0.6f - c1;   v[4].u = 0.0f; v[4].v = 1.0f;
            v[5].x = s0*0.6f + s1;   v[5].y = c0*0.6f + c1;   v[5].u = 1.0f; v[5].v = 1.0f;
            for (int i = 0; i < 6; ++i) {
                v[i].z = 0.0f;
                v[i].nx = 0.0f; v[i].ny = 0.0f; v[i].nz = 1.0f;
            }
        }
        s_built = true;
    }

    // Per-frame wedge brightness: (phase + wedge) mod 8 (binary INCREMENTS).
    int phase = (7 - ((int)m_Field114 & 7)) & 7;  // DAT_0014D4B8
    for (int wedge = 0; wedge < 8; ++wedge) {
        int alphaIdx = (phase + wedge) & 7;
        int a = alphaIdx * 0x20;
        if (a > 0xFF) a = 0xFF;
        if (a < 0x40) a = 0x40;
        const Colour wedgeCol((uint8_t)a, (uint8_t)a, (uint8_t)a, 200);
        const uint32_t packed = wedgeCol.PlatformColour();
        QUADCUSTOMVERTEX* v = &s_verts[wedge * 6];
        for (int i = 0; i < 6; ++i) v[i].colour = packed;
    }

    _Vector3<float> drawPos(148.0f, 7.0f, 0.0f);
    if (m_State == STATE_LOADING_B) {
        drawPos = _Vector3<float>(168.0f, -106.0f, 0.0f);
    } else if (m_State == 0x0d) {  // state 0xd has no port enum name yet
        drawPos = _Vector3<float>(0.0f, -50.0f, 0.0f);
    }

    s_blurTex->Set();
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(64.0f, 64.0f, 64.0f);
    mat.GlobalTranslate44(drawPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();
    Mortar::Mesh::DrawTriList(s_verts, 48, false, NULL);
    s_blurTex->UnSet();
}

// Defunct: vtable PreDraw — no-op stub; (v1.6.1: symbol absent -- defunct/inlined)
void MainScreen::PreDraw(float* /*hudScale*/) {
}

// Defunct: NetworkManager::CancelNewsDisplay — no-op stub; v1.6.1 binary @ 0x0014AFB8
void MainScreen::CancelNews() {
    // Defunct: NetworkManager — no-op stub; v1.6.1 binary @ 0x0014AFB8
}

// Defunct: network UI button — empty in binary (single bx lr); v1.6.1 binary @ 0x0014ACFC
void MainScreen::ClearNetworkButton() {
    // Defunct: network UI button — no-op stub; v1.6.1 binary @ 0x0014ACFC
}

int MainScreen::CreateNormalLeaderboardButton(float x) {
    (void)x;
    return 0;  // Defunct: online leaderboard UI — no-op stub; v1.6.1 MainScreen::CreateNormalLeaderboardButton @0x00195a08
}

// Binary @ 0x0014AC98 — empty event hook.
void MainScreen::OnMenuItemsCleared() {
    // no-op — empty in binary; binary @ 0x0014AC98
}

// Binary @ 0x0014B0AC — multiplayer variant of GameModeCallback (state 0xF).
void MainScreen::MultiplayerGameModeCallback() {
    LOG_INFO("SCREEN/MainScreen", "%d -> %d (%s)", (int)(m_State), (int)(STATE_MODE_SELECT_2), "MultiplayerGameModeCallback");
    m_State = STATE_MODE_SELECT_2;
    m_Timer2 = 1.0f;
    if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
    m_pQuitButton = nullptr;
    Math::SeedGlobalRng((uint32_t)game_work.m_FrameTimer);
}
