//
// MainScreen — reimplemented from docs/screens/main.md
// Original: ctor 0x0014c430 (159 lines), Update 0x0014b278 (677 lines),
//           Draw 0x0014d4ec (171 lines)
//

#include "MainScreen.h"
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
#include "game/WaveManager.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "core/SystemManager.h"
// Analysed: 2026-05-04T00:00
#include "audio/GameSound.h"
#include "audio/SoundManager.h"
#include "debug/DebugFlags.h"
#include <cstdio>
#include <cmath>

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
    return tex.IsValid() ? tex->m_TexId : 0;
}

// Button positions (verified from read_memory, docs/screens/main.md)
static const Vec3 POS_PLAY_BUTTON(16.0f, -66.0f, 0.0f);
static const Vec3 POS_DOJO_BUTTON(-144.0f, -65.0f, 0.0f);
static const Vec3 POS_QUIT(182.0f, -106.0f, 0.0f);
static const Vec3 POS_MORE_GAMES(182.0f, -106.0f, 0.0f);
static const Vec3 POS_SOUND_TOGGLE(216.0f, 135.5f, 0.0f);
static const Vec3 POS_MUSIC_TOGGLE(176.0f, 135.5f, 0.0f);

// Matches ctor at 0x0014c430 (159 lines)
MainScreen::MainScreen(Game& g)
    : pPlayButton(nullptr), pDojoButton(nullptr),
      pQuitBtn(nullptr), pMoreGamesBtn(nullptr),
      pSoundToggle(nullptr), pMusicToggle(nullptr),
      m_Alpha(1.0f),
      m_LogoNinjaTextX(0.0f), m_WindowCenter(0.0f), field_0x100(0.0f),
      m_BounceVelocity(0.0f), m_field108(0.0f),
      m_State(STATE_CAMERA_ZOOM), m_StateTimer(0.0f),
      m_Timer2(0.0f),
      m_CameraTransition(0.0f), m_GlobalAlphaTarget(1.0f), m_Time(0.0f),
      m_bGameStartReset(false),
      m_pDojoScreen(nullptr),
      m_pGameModeScreen(nullptr),
      game(g)
{
    // Load global textures (assigned to globals via GOT in original)
    m_blurryBackingTex = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    m_fruitTextTex     = Mortar::TextureManager::LoadLocalisedTexture("fruit_text.tex");
    m_ninjaTextTex     = Mortar::TextureManager::LoadLocalisedTexture("ninja_text.tex");

    // Load background decoration
    m_TexSliceFruit = Mortar::TextureManager::LoadLocalisedTexture("slice_fruit.tex");

    // Load button textures
    m_TexNewGame        = Mortar::TextureManager::LoadLocalisedTexture("newgame.tex");
    m_TexDojoIcon       = Mortar::TextureManager::LoadLocalisedTexture("dojo_icon.tex");
    m_TexGCAchievements = Mortar::TextureManager::LoadLocalisedTexture("gc_achievements.tex");
    m_TexMoreGames      = Mortar::TextureManager::LoadLocalisedTexture("more_games.tex");
    m_TexQuit           = Mortar::TextureManager::LoadLocalisedTexture("quit.tex");
    m_TexOpenFeint      = Mortar::TextureManager::LoadLocalisedTexture("openfeint.tex");

    // Load toggle textures
    m_TexSoundOn  = Mortar::TextureManager::LoadLocalisedTexture("sound.tex");
    m_TexSoundOff = Mortar::TextureManager::LoadLocalisedTexture("sound_cross.tex");
    m_TexMusicOn  = Mortar::TextureManager::LoadLocalisedTexture("music.tex");
    m_TexMusicOff = Mortar::TextureManager::LoadLocalisedTexture("music_cross.tex");

    // Load logo overlay
    m_TexCommingSoon = Mortar::TextureManager::LoadLocalisedTexture("comming_soon.tex");

    // Load verdana.fnt into MainScreen::m_pFont (+0x11c).
    // Binary ctor @ 0x0014c430: operator_new(0x438) + Font::Font + Font::Load("fonts/verdana.fnt").
    // Spec: "verdana.fnt loads into MainScreen::m_pFont, NOT g_GameData."
    // See docs/engine/font.md "Font Asset Cross-Reference" (string @ 0x001bbcb3).
    {
        // logical path; FileSystem_Direct (via Mortar::File in Font::Load) prepends data_dir
        m_pFont = Mortar::Font::Create("fonts/verdana.fnt");
    }

    // Set size = (480.0, 138.0, 1.0)
    size = Vec3(480.0f, 138.0f, 1.0f);

    // Set position = (0.0, (320.0 - size_y) * 0.5, 0.0) = (0.0, 91.0, 0.0)
    pos = Vec3(0.0f, (320.0f - size.y) * 0.5f, 0.0f);

    // m_WindowCenter = ninja_text.tex height / 2 + 160.0
    // Matches binary ctor (0x0014c430): calls ninja_text_tex->GetHeight()
    // (vtable +0x18) → shifts right by 1 → adds 160.0.
    // The logo's bottom edge starts at Y=+160 (top of ortho) and bounces
    // down to rest at pos.y + 3 = 94.
    const float ninjaH = m_ninjaTextTex.IsValid()
                       ? (float)(m_ninjaTextTex->m_Height / 2)
                       : 0.0f;
    m_WindowCenter = ninjaH + 160.0f;

    // Copy original size
    m_OrigSize = size;

    // Zero all button pointers (already done in init list)
    // state=0, timers=0 (already done in init list)
}

MainScreen::~MainScreen() {
    Release();
}

// Matches 0x0014ac80 (13 lines): calls Reset via vtable
void MainScreen::Init() {
    Reset();
}

// Matches 0x0014ac8c: no-op
void MainScreen::Reset() {
}

// Matches 0x0014cd20 (~40 lines): cleanup all resources
void MainScreen::Release() {
    // Zero all button pointers (HUD owns them, don't delete here)
    pPlayButton = nullptr;
    pDojoButton = nullptr;
    pQuitBtn = nullptr;
    pMoreGamesBtn = nullptr;
    pSoundToggle = nullptr;
    pMusicToggle = nullptr;

    // Note: SmartPtr members self-destruct in dtor; no explicit texture cleanup needed.
}

// Matches Update at 0x0014b278 (677 lines) — state machine
void MainScreen::Update(float dt) {
    m_Time += dt;

    // Binary @ 0x0014b2a4: toggle null-check + create runs at the top of Update,
    // before the state switch, so destroyed toggles are recreated in any state.
    if (!pSoundToggle || !pMusicToggle) {
        CreateToggles();
    }

    switch (m_State) {
    case STATE_CAMERA_ZOOM: {
        // Camera zoom-in from splash. Create play/dojo buttons.
        // Lerp camera transition toward -1.0
        m_CameraTransition += (-1.0f - m_CameraTransition) * CAMERA_LERP_RATE;
        m_Timer2 += dt;

        // Create play/dojo buttons if they don't exist
        if (!pPlayButton && m_Timer2 > TIMER2_THRESHOLD) {
            CreatePlayDojo();
        }

        // Transition to CREATE_BUTTONS when camera settles. Binary checks
        // `camera < 0` (sign), but the lerp converges to -1 either way.
        // The Quit button is lazy-created in state 1 itself, not on the
        // transition (matches binary's "only on first frame of state 1").
        if (m_CameraTransition < CAMERA_THRESHOLD && m_Timer2 > TIMER2_THRESHOLD) {
            m_State = STATE_CREATE_BUTTONS;
        }
        break;
    }

    case STATE_CREATE_BUTTONS: {
        // Binary @ 0x0014bbe2..0x0014bdf2.
        //
        // Per-frame:
        //   - SetNewSymbol on the dojo button with ItemManager::AreNewItems().
        //   - Continue camera lerp toward -1.0 at rate 0.125.
        //   - pos.y animation (inline formula, alpha = m_CameraTransition).
        //
        // Lazy creation: only the bomb-Quit button. The decompiler shows a
        // second "MoreGames" creation block at the same field +0xA4, but
        // disassembly proves it's unreachable dead code in this build.

        // SetNewSymbol every frame (port stub: ItemManager::AreNewItems
        // unported — pass false to keep the badge hidden).
        if (pDojoButton) {
            pDojoButton->SetNewSymbol(false);
        }

        // Lazy-create the Quit button on first state-1 frame.
        if (!pQuitBtn) {
            CreateQuitButton();
        }

        // Continue camera lerp toward -1.0 (binary continues state 0's lerp).
        m_CameraTransition += (-1.0f - m_CameraTransition) * CAMERA_LERP_RATE;

        // pos.y animation: alpha = m_CameraTransition (negative). Binary
        // formula: pos.y = (size.y + 320 + size.y * (-cameraTransition) * -2) * 0.5
        //                 = (size.y + 320 - 2*size.y*(-cameraTransition)) * 0.5.
        // At cameraTransition = -1: pos.y = (size.y + 320 - 2*size.y) * 0.5
        //                                 = (320 - size.y) * 0.5.
        const float sizeY_1 = size.y;
        const float alpha_1 = -m_CameraTransition;     // 0..1 as zoom completes
        pos.y = (sizeY_1 + 320.0f - 2.0f * sizeY_1 * alpha_1) * 0.5f;
        break;
    }

    case STATE_GAME_START: {
        // Binary @ 0x0014bb58 case 2:
        //   if (-cameraTransition > 0.999) {                 // gate fully zoomed in
        //       game->field_0x28 = game->field_0x20;
        //       WaveManager::Reset(true);
        //       game->pauseFlag = 1;
        //   }
        //   cameraTransition *= 0.75;
        //   if (|cameraTransition| < 0.001) {                // DAT_0014bb74
        //       game->pauseFlag = 0;
        //       cameraTransition = 0;
        //       m_State = STATE_CAMERA_FADE (0x11);
        //   }
        if (-m_CameraTransition > 0.999f && !m_bGameStartReset) {
            WaveManager::GetInstance()->Reset(true);
            m_bGameStartReset = true;
            // Binary @ 0x0014bb6c: game->pauseFlag = 1 (suppresses
            // WaveManager spawn pump until the camera-settle clear below).
            // game->field_0x28 = field_0x20 still TODO (field not in port struct).
            game.pauseFlag = 1;
        }
        m_CameraTransition *= 1.0f - (1.0f - STATE_2_DECAY) * FN::g_DebugTimeScale;
        if (fabsf(m_CameraTransition) < 0.001f) {
            m_CameraTransition = 0.0f;
            m_State = STATE_CAMERA_FADE;
            m_bGameStartReset = false;
            // Binary @ 0x0014bb78: clear pauseFlag once the camera
            // animation has settled into gameplay.
            game.pauseFlag = 0;
        }

        // Shared LAB_0014c166 pos.y animation (binary uses cameraTransition
        // as alpha; here we use its absolute magnitude). Logos track pos.y.
        const float sizeY_2 = size.y;
        const float alpha_2 = fabsf(m_CameraTransition);
        const float tt_2 = sizeY_2 * alpha_2;
        pos.y = (sizeY_2 + 320.0f - 2.0f * tt_2) * 0.5f;
        break;
    }

    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B:
    case STATE_DOJO_WAIT_C:
    case STATE_DOJO_WAIT_D: {
        // Binary @ 0x0014be80: cases 3/4/0x15/0x16 share one block:
        //   if (Mortar::ActorManager::GetNumEntities(0) == 0) {
        //       m_Timer2 *= 0.75
        //       if (m_Timer2 < ~1e-4) {
        //           m_Timer2 = 0
        //           new DojoScreen()
        //           HUD::AddControl(...)
        //       }
        //   }
        // The block does NOT touch pPlayButton/pDojoButton/pQuitBtn —
        // those persist throughout the dojo trip. The slide-out is
        // purely the panel pos formula based on m_Timer2 (see Draw).
        // The entity-count gate is critical: ClearMenuItems (called from
        // MenuButton::Update on slice) releases all menu fruits so they
        // fall offscreen and the count drops to 0; only then can the
        // decay start and the DojoScreen spawn.
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        const int fruitCount = am ? am->GetNumEntities(0) : 0;

        if (fruitCount == 0) {
            m_Timer2 *= 1.0f - (1.0f - 0.75f) * FN::g_DebugTimeScale;
        }

        // Binary LAB_0014c166: apply pos.y slide formula with m_Timer2 as alpha.
        // As Timer2 decays 1→0, pos.y animates 91→229 (off-screen top).
        const float sizeY_d = size.y;
        const float tt_d = sizeY_d * m_Timer2;
        pos.y = (sizeY_d + 320.0f - 2.0f * tt_d) * 0.5f;

        // Binary DAT_0014bf10 = 0.001f (NOT 0.01f).
        if (!m_pDojoScreen && fruitCount == 0 && m_Timer2 < 0.001f) {
            m_Timer2 = 0.0f;
            m_pDojoScreen = new DojoScreen(game);
            // RemoveCallback nulls our weak ref BEFORE HUD::Update
            // deletes the child — without it, polling
            // m_pDojoScreen->IsPendingRemoval() the frame after the
            // child sets pending would deref freed memory (UAF crash).
            // The binary's DojoScreen state 6 directly writes
            // mainScreen->m_State = 8 from inside DojoScreen::Update,
            // so we don't need to set the state transition here — it
            // happens in DojoScreen.cpp before HUD::Update fires this
            // callback.
            m_pDojoScreen->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::DojoScreenRemoved);
            game.hud->AddControl(m_pDojoScreen);
        }
        break;
    }

    case STATE_SLIDE_IN: {
        // Binary @ ~0x0014beec, two-phase lerp + pos.y animation
        // (shared LAB_0014c166 formula with states 0xe/0xf/3/4/0x15/0x16).
        //   if (m_Timer2 <= 0.999) {
        //       m_Timer2 += (1.0 - m_Timer2) * 0.125     // lerp toward 1
        //       posAlpha  = m_Timer2
        //   } else {
        //       m_Timer2 += dt                            // hold + tick
        //       if (m_Timer2 > 1.5) {
        //           m_Timer2 = 0.15; m_State = CAMERA_ZOOM; m_field108 = 0
        //       }
        //       posAlpha  = 1.0                            // held at final
        //   }
        //   pos.y = (size.y + 320 - 2*size.y*posAlpha) * 0.5
        float posAlpha;
        if (m_Timer2 <= STATE_8_LERP_THRESHOLD) {
            m_Timer2 += (1.0f - m_Timer2) * STATE_8_LERP_RATE * FN::g_DebugTimeScale;
            posAlpha = m_Timer2;
        } else {
            m_Timer2 += dt;  // dt already scaled by g_DebugTimeScale
            if (m_Timer2 > STATE_8_DURATION) {
                m_Timer2 = STATE_8_RESET_TIMER;
                m_State = STATE_CAMERA_ZOOM;
                // Binary does NOT reset m_StateTimer here; removed.
            }
            posAlpha = 1.0f;
        }
        // Shared LAB_0014c166: slide pos.y back to on-screen as alpha→1
        const float sizeY_8 = size.y;
        const float tt_8 = sizeY_8 * posAlpha;
        pos.y = (sizeY_8 + 320.0f - 2.0f * tt_8) * 0.5f;
        break;
    }

    case STATE_LEADERBOARD:     // binary case 9
    case STATE_MORE_GAMES:      // binary case 10
    case STATE_MATCHMAKER:      // binary case 0x10
        // Defunct — OpenFeint / GameCenter / matchmaker states. Binary
        // resets m_StateTimer = 0 and m_Timer2 = -0.85 (DAT_0014c28c),
        // bouncing back to STATE_CAMERA_ZOOM with the slide-in animation
        // already armed for the next frame.
        m_State = STATE_CAMERA_ZOOM;
        m_StateTimer = 0.0f;
        m_Timer2 = -0.85f;
        m_CameraTransition = 0.0f;
        DeleteMenuButtons();
        break;

    case STATE_NEWS:            // binary case 0xb
        // Defunct — NetworkManager::UpdateNews polls for remote news.
        // Binary @ 0x0014c0..: m_StateTimer=0, m_State=1, m_Timer2=-0.85.
        m_State = STATE_CREATE_BUTTONS;
        m_StateTimer = 0.0f;
        m_Timer2 = -0.85f;
        break;

    case STATE_MODE_SELECT:
    case STATE_MODE_SELECT_2: {
        // Binary @ 0x0014bf40 cases 0xe/0xf: decay m_Timer2 (binary uses
        // m_TexMoreGames.ptr repurposed as float) and slide pos.y upward
        // off-screen. UpdateScreenElements writes m_LogoFruitTextPos.y =
        // pos.y + 18, and m_WindowCenter's bounce floor tracks pos.y + 3,
        // so both logos slide off-screen with pos.y.
        //
        // When alpha crosses 0.25 downward, spawn GameModeScreen.
        // After spawning, MainScreen stays in this state — GameModeScreen
        // writes mainScreen->m_State later (CAMERA_FADE on mode pick,
        // SLIDE_IN on back-out).
        const float oldTimer2 = m_Timer2;
        // Port specific: per-frame decay needs to slow with the debug
        // time-scale. Binary x *= 0.85 each frame → 15% decay. With
        // scale s, decay = 1 - 0.15*s. At s=1 matches binary, at s=0.1
        // decay is 1.5% per frame (10x slower, matching dt scaling).
        const float decay = 1.0f - (1.0f - STATE_0E_DECAY) * FN::g_DebugTimeScale;
        m_Timer2 *= decay;

        // Binary: pos.y = (size.y + 320 - 2*size.y*alpha) * 0.5
        // At alpha=1: pos.y=40 (on-screen). At alpha=0.25: pos.y=220.
        // At alpha=0: pos.y=280 (off top). Logos track pos.y+18.
        const float sizeY = size.y;
        const float tt = sizeY * m_Timer2;
        pos.y = (sizeY + 320.0f - 2.0f * tt) * 0.5f;

        if (oldTimer2 > STATE_0E_THRESHOLD &&
            m_Timer2 <= STATE_0E_THRESHOLD &&
            !m_pGameModeScreen) {
            m_pGameModeScreen = new GameModeScreen(game, false);
            m_pGameModeScreen->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::GameModeScreenRemoved);
            game.hud->AddControl(m_pGameModeScreen);
        }
        break;
    }

    case STATE_CAMERA_FADE:
        // Binary @ 0x0014c19a: only decay when camera < 0; clamp to 0 once
        // camera > -0.001 (DAT_0014c2a8) and clear pauseFlag. Without the
        // clamp the value keeps decaying forever and pauseFlag stays set.
        if (m_CameraTransition < 0.0f) {
            m_CameraTransition *= 1.0f - (1.0f - STATE_2_DECAY) * FN::g_DebugTimeScale;
            if (m_CameraTransition > -0.001f) {
                m_CameraTransition = 0.0f;
                // Binary @ 0x0014c1a4: clear pauseFlag once the post-game
                // camera fade has reached zero (back-to-menu return path).
                game.pauseFlag = 0;
            }
        }
        break;

    case STATE_LOADING_A:
    case STATE_LOADING_B:
        // Binary @ 0x0014c010: state always resets to 0 + clears menu buttons.
        // The `field108 >= 8.0` check only resets m_field108 = 0; the broader
        // reset is unconditional. Port previously gated everything on >= 8.0,
        // making the loading state run for ~8s before bouncing.
        m_field108 += dt * 8.0f;
        if (m_field108 >= 8.0f) {
            m_field108 = 0.0f;
        }
        m_State = STATE_CAMERA_ZOOM;
        m_StateTimer = 0.0f;
        m_CameraTransition = 0.0f;
        DeleteMenuButtons();
        break;

    case STATE_QUIT_WAIT: {
        // ASM-verified: 2026-04-30 binary @ 0x0014c078..0x0014c0ea (asm-inspector)
        // Case 0x17:
        //   TutorialControl::ResetTutePos(pTC, nullptr)
        //   if (Mortar::ActorManager::GetNumEntities(0) != 0) break;
        //   pLeaderboardBtn = nullptr;
        //   qs = SystemManager::m_QuitState  (NOT Game::gameMode -- earlier
        //       RE conflated GOT slots; +0x4c is on SystemManager, GOT slot
        //       0x000074f8, byte field initialised to 3)
        //   if (qs == 2):  HitMenuBomb (163,-96,0); state = 0x18
        //   else if (qs == 3): m_State=0; m_Timer2=0.15
        //   else (0,1):    pending OS dialog; stay in QUIT_WAIT
        if (game.pTutorialCtrl) {
            game.pTutorialCtrl->ResetTutePos((MenuButton*)nullptr);
        }
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        const int liveEntities = am ? am->GetNumEntities(0) : 0;
        if (liveEntities != 0) break;

        const uint8_t qs = SystemManager::GetInstance().GetQuitState();
        if (qs == 2) {
            FN::HitMenuBomb(Vec3(163.0f, -96.0f, 0.0f));
            m_State = STATE_QUIT_BOMB;
            m_StateTimer = 0.0f;
        } else if (qs == 3) {
            // OS-cancelled / idle. Reset to camera-zoom flow.
            m_State = STATE_CAMERA_ZOOM;
            m_StateTimer = 0.0f;
            m_Timer2 = 0.15f;       // DAT_0014c298
        }
        // Otherwise (0/1): OS dialog pending -- stay in QUIT_WAIT.
        break;
    }

    case STATE_QUIT_BOMB: {
        // Binary @ 0x0014c0f2 case 0x18:
        //   TutorialControl::ResetTutePos(pTC, nullptr)
        //   if (BombFlashFull()) SystemManager::QuitGame();
        //
        // BombFlashFull returns true once bombHitTimer < 1.0s (the flash
        // has peaked and is on its way out). HitMenuBomb in QUIT_WAIT
        // primed it to 2.0; GameUpdate ticks it down each frame.
        if (game.pTutorialCtrl) {
            game.pTutorialCtrl->ResetTutePos((MenuButton*)nullptr);
        }
        if (FN::BombFlashFull()) {
            SystemManager::GetInstance().QuitGame();
            game.running = false;
        }
        break;
    }
    }

    // Position update (end of Update, all states)
    // Sound/music toggle texture swap
    if (pSoundToggle) {
        pSoundToggle->m_Texture = (game.m_bSoundOn ? m_TexSoundOn : m_TexSoundOff);
    }
    if (pMusicToggle) {
        pMusicToggle->m_Texture = (game.m_bMusicOn ? m_TexMusicOn : m_TexMusicOff);
    }

    // Compute the state-dependent "elapsedTime" / pause driver used by
    // BOTH the toggle positioning block and UpdateScreenElements below.
    // Binary: at the top of Update, pTVar13 = -m_CameraTransition. The
    // OUT switch cases (0xe/0xf/3/4) and SLIDE_IN overwrite pTVar13 with
    // their decayed/growing m_Timer2 before reaching this point. All
    // other states retain pTVar13 = -m_CameraTransition.
    float elapsedTime;
    switch (m_State) {
    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B:
    case STATE_DOJO_WAIT_C:
    case STATE_DOJO_WAIT_D:
    case STATE_MODE_SELECT:
    case STATE_MODE_SELECT_2:
    case STATE_SLIDE_IN:
        // OUT (m_Timer2 decays 1->0) or SLIDE_IN (m_Timer2 lerps 0->1).
        elapsedTime = m_Timer2;
        break;
    default:
        elapsedTime = -m_CameraTransition;  // 0 -> +1 as camera zooms in
        break;
    }

    // Toggle button positioning.
    // Binary: pauseAmount = clamp(pTVar13 + GetPauseAmount(), 0, 1)
    // The port's earlier version used fabsf(m_CameraTransition) which
    // flipped -1 -> +1 and forced slideOffset=0 during OUT, so the
    // toggles stayed on-screen while blurry_backing slid off. Using the
    // routed elapsedTime (which tracks m_Timer2 during OUT/SLIDE_IN)
    // lets the toggles slide up with blurry_backing as the screen
    // leaves. GetPauseAmount() stubbed at 0 (no pause in main menu).
    if (pSoundToggle && pMusicToggle) {
        pSoundToggle->pos.y = 135.5f;
        pMusicToggle->pos.y = 135.5f;

        // Binary x-flip: pTVar13 <= 0 -> (20, -20); else -> (216, 176)
        if (elapsedTime <= 0.0f) {
            pSoundToggle->pos.x = 20.0f;
            pMusicToggle->pos.x = -20.0f;
        } else {
            pSoundToggle->pos.x = 216.0f;
            pMusicToggle->pos.x = 176.0f;
        }

        float pauseAmount = elapsedTime;                 // + GetPauseAmount() (=0 stub)
        if (pauseAmount < 0.0f) pauseAmount = 0.0f;
        if (pauseAmount > 1.0f) pauseAmount = 1.0f;

        float slideOffset = size.y * 2.0f * (1.0f - pauseAmount);
        pSoundToggle->m_bActive = (pauseAmount > PAUSE_VISIBILITY) ? 1 : 0;
        pMusicToggle->m_bActive = (pauseAmount > PAUSE_VISIBILITY) ? 1 : 0;
        pSoundToggle->pos.y += slideOffset;
        pMusicToggle->pos.y += slideOffset;
    }

    UpdateScreenElements(dt, elapsedTime);

    // Binary @ 0x0014b278: cameraTransition lives on game.m_TransitionTimer
    // (+0x0c) so peers (ScoreControl, MissControl, UpdateMusic, etc.) can
    // read it. Port owns the value on MainScreen but mirrors it here so
    // the same ABI surface is preserved for downstream subsystems.
    game.m_TransitionTimer = m_CameraTransition;
}

// Helper: setup world matrix for a textured quad at given position
static void SetupQuadMatrix(MatrixManager& mm, const Vec3& hudScale,
                            float w, float h, const Vec3& drawPos) {
    (void)hudScale;
    // Positions are already in the binary-centred ortho space
    // [-240..240, -160..160]. See docs/engine/coordinate-system.md.
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
    mat.GlobalTranslate44(drawPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();
}

// Matches Draw at 0x0014d4ec (171 lines)
// DIFFERS: binary signature is Draw(float*) at vtable slot 7 @ 0x0014D4EC;
//   port uses Draw(Vec3&, int) for ergonomic param-passing.
//   Body logic verified equivalent in R4 W2 RE.
void MainScreen::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    // Skip drawing for certain states
    if (m_State == STATE_CAMERA_FADE) return;
    if ((m_State == STATE_DOJO_WAIT_A || m_State == STATE_DOJO_WAIT_B ||
         m_State == STATE_DOJO_WAIT_C || m_State == STATE_DOJO_WAIT_D) &&
        m_Timer2 == 0.0f) return;
    // For STATE_MODE_SELECT: binary keeps drawing but pos.y is animated
    // off-screen in Update, which pulls the logo positions with it.

    MatrixManager& mm = MatrixManager::GetInstance();

    // 1+2. Shade triangle + fruit_text — guarded by fruit_text (GOT+0x6FCC, DAT_0014d844)
    // Original: single if-block for shade + fruit_text draw
    if (m_fruitTextTex.IsValid()) {
        // 1a. Background shade (blurry_backing.tex) — angled triangle, NOT a quad
        // Original: 3-vertex triangle (DrawTriList at 0x14d4ec lines 46-80)
        // Vertex cache at global+0x6cc, drawn with Scale(size)+Translate(pos)
        if (m_blurryBackingTex.IsValid()) {
            m_blurryBackingTex->Set();
            SetupQuadMatrix(mm, hudScale, size.x, size.y, pos);

            // Colour(0,0,0,0x80).PlatformColour() = 0x80000000
            static const uint32_t kShadeCol = 0x80000000u;
            // V0: bottom-left (Y=-0.6875 not -1.0 → creates angle)
            // V1: far-right top (X=3.5 extends past screen → clipped)
            // V2: top-left
            // UVs: DAT_0014d854=0.0, DAT_0014d830=0.0078125(=1/128), DAT_0014d834≈0.065694
            QUADCUSTOMVERTEX shadeVerts[3] = {
                { -1.0f, -0.6875f, 0.0f,  0,0,1,  kShadeCol,  0.0f,      0.0078125f },  // 0xBF300000
                {  3.5f,  1.0f,    0.0f,  0,0,1,  kShadeCol,  1.0f,      0.0078125f },  // 0x40600000
                { -1.0f,  1.0f,    0.0f,  0,0,1,  kShadeCol,  0.065694f, 1.0f       },  // 0x3D868A48
            };
            game.renderer.DrawTriList(shadeVerts, 3);

            m_blurryBackingTex->UnSet();
        }

        // 1b. "FRUIT" text logo (fruit_text.tex) — drawn at +0xEC (m_LogoFruitTextPos)
        // Original: Scale(texSize * 0.85) — DAT_0014d838 = 0.85
        static const float FRUIT_TEXT_SCALE = 0.85f;  // DAT_0014d838
        // Original: tint from global Colour at GOT+0x73A4 (NOT m_Alpha)
        // m_Alpha only controls slice_fruit POSITION, not logo opacity
        m_fruitTextTex->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_fruitTextTex->m_Width * FRUIT_TEXT_SCALE,
            (float)m_fruitTextTex->m_Height * FRUIT_TEXT_SCALE,
            m_LogoFruitTextPos);
        game.renderer.DrawQuad(m_DrawColour);
        m_fruitTextTex->UnSet();
    }

    // 3. "NINJA" text logo (ninja_text.tex) — drawn at +0xF8
    // Original: TranslateMatrix(&this+0xF8) reads 3 consecutive floats:
    //   +0xF8 = m_LogoNinjaTextX, +0xFC = m_WindowCenter, +0x100 = field_0x100
    if (m_ninjaTextTex.IsValid()) {
        Vec3 ninjaDrawPos(m_LogoNinjaTextX, m_WindowCenter, field_0x100);
        m_ninjaTextTex->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_ninjaTextTex->m_Width, (float)m_ninjaTextTex->m_Height,
            ninjaDrawPos);
        game.renderer.DrawQuad(m_DrawColour);
        m_ninjaTextTex->UnSet();
    }

    // 4. Dojo decoration (slice_fruit.tex)
    if (m_TexSliceFruit.IsValid()) {
        m_TexSliceFruit->Set();
        SetupQuadMatrix(mm, hudScale,
            (float)m_TexSliceFruit->m_Width, (float)m_TexSliceFruit->m_Height,
            m_LogoFruitPos);
        game.renderer.DrawQuad(m_DrawColour);
        m_TexSliceFruit->UnSet();
    }

    // 5. Loading symbol (states 0x13, 0x14 only)
    // Binary Draw step 6 @ 0x0014D1F8 area: call DrawLoadingSymbol when loading.
    if (m_State == STATE_LOADING_A || m_State == STATE_LOADING_B) {
        DrawLoadingSymbol(&hudScale.x);
    }

    // 6. "Coming soon" overlay — drawn when m_TexCommingSoon valid AND pPlayButton exists.
    // Binary: Scale(0.5, 0.5*texH/texW, 1), Translate(0, 7, 0), DrawQuad with alpha-tinted white.
    // DAT_0014d850 = 0.0f (X offset for comming_soon).
    if (m_TexCommingSoon.IsValid() && pPlayButton != NULL) {
        float csW = (float)m_TexCommingSoon->m_Width;
        float csH = (float)m_TexCommingSoon->m_Height;
        float scaleX = csW * 0.5f;
        float scaleY = csH * 0.5f * (csW > 0.0f ? (csH / csW) : 1.0f);
        Vec3 csPos(0.0f, 7.0f, 0.0f);  // DAT_0014d850=0.0, 7.0, 0.0
        m_TexCommingSoon->Set();
        SetupQuadMatrix(mm, hudScale, scaleX, scaleY, csPos);
        game.renderer.DrawQuad(m_DrawColour);
        m_TexCommingSoon->UnSet();
    }
}

// Matches 0x0014ad3c — constants verified from Ghidra decompilation + read_memory
//
// Binary constants (literal pool at 0x14aec4):
//   CLAMP_THRESHOLD    = 0.04   (DAT_0014aec4)
//   BOUNCE_GRAVITY     = -55.0  (DAT_0014aecc)
//   LOGO_FRUIT_X_BASE  = -175.0 (DAT_0014aedc)
//   ELAPSED_THRESHOLD  = 0.99   (DAT_0014aed8)
//
// All positions are in the binary-centred ortho space (X horizontal ±240,
// Y vertical ±160). No axis swap needed — see docs/engine/coordinate-system.md.
//
void MainScreen::UpdateScreenElements(float cameraTransition, float time) {
    static const float MAX_CT            = 0.04f;    // DAT_0014aec4 — MAXIMUM clamp, not minimum
    static const float BOUNCE_GRAVITY    = -55.0f;   // DAT_0014aecc
    static const float ELAPSED_THRESHOLD = 0.99f;    // DAT_0014aed8

    // Original ARM: if (-1 < (int)((uint)(ct < 0.04) << 31)) ct = 0.04
    // This fires when ct >= 0.04, clamping DOWN. Small dt passes through unchanged.
    if (cameraTransition > MAX_CT) {
        cameraTransition = MAX_CT;
    }

    // Original: m_LogoFruitTextPos.z and field_0x100 = DAT_0014aec8 (=0.0)
    // These act as z components for fruit text and ninja text draw positions
    m_LogoFruitTextPos.z = 0.0f;
    field_0x100 = 0.0f;

    // Ninja text X = 60.0 (DAT_0014aed0); Y comes from m_WindowCenter in Draw
    m_LogoNinjaTextX = 60.0f;  // DAT_0014aed0

    // Bounce physics (matches binary exactly)
    float newVel = m_BounceVelocity + cameraTransition * BOUNCE_GRAVITY;
    m_BounceVelocity = newVel;

    float newCenter = m_WindowCenter + newVel * cameraTransition * 15.0f;
    m_WindowCenter = newCenter;

    float floorPos = pos.y + 18.0f;

    // Fruit text position: use original values directly (no X↔Y swap)
    m_LogoFruitTextPos.x = -120.0f;     // DAT_0014aed4 (LOGO_NINJA_OFFSET_Y)
    m_LogoFruitTextPos.y = floorPos;     // pos.y + 18.0
    // m_LogoFruitTextPos.z (+0xF4) = 0.0, set above

    // Temporary copy (overwritten at end of function with correct formula)
    m_LogoFruitPos = m_LogoFruitTextPos;

    if (cameraTransition > 0.0f) {
        m_GlobalAlphaTarget = 1.0f;
    }

    float floorLimit = floorPos - 15.0f;
    if (newCenter < floorLimit) {
        m_WindowCenter = floorLimit;
        m_BounceVelocity = newVel * BOUNCE_LOSS;

        if (fabsf(newVel * BOUNCE_LOSS) < BOUNCE_SETTLE &&
            time > ELAPSED_THRESHOLD &&
            cameraTransition > 0.0f) {
            m_BounceVelocity = 0.0f;
            m_GlobalAlphaTarget = 0.0f;
        }
    }

    // Per-frame lerp (no dt) — scale by g_DebugTimeScale so slow-mo slows
    // the slice_fruit slide-in/out matching other transitions.
    m_Alpha += (m_GlobalAlphaTarget - m_Alpha) * ALPHA_LERP_RATE * FN::g_DebugTimeScale;

    // LogoFruitPos (slice_fruit decoration): matches binary at end of 0x0014ad3c
    // m_LogoFruitPos = (-175, 26, 0) + (-120, -17, 0) * m_Alpha * 2.0
    // Binary DOES leave slice_fruit visible during state 0xe/3/4 transitions
    // (verified from Draw decomp — no state gate or pos.y dependency).
    Vec3 base(-175.0f, 26.0f, 0.0f);        // DAT_0014aedc, 26.0, DAT_0014aec8
    Vec3 offset(-120.0f, -17.0f, 0.0f);     // DAT_0014aed4, -17.0, DAT_0014aec8
    Vec3 scaled = offset * m_Alpha * 2.0f;
    m_LogoFruitPos = base + scaled;
}

// Matches 0x0014aee8 (~35 lines).
// Note: Sound/Music toggles persist across screens (they're global UI),
// so they're NOT removed here. The Quit button IS removed because it's
// a main-menu-only control. Binary's GameModeCallback/AboutCallback
// also null pQuitBtn but rely on this helper to do the actual HUD
// removal — port matches.
void MainScreen::DeleteMenuButtons() {
    RemoveButton(pPlayButton);
    RemoveButton(pDojoButton);
    RemoveButton(pMoreGamesBtn);
    RemoveButton(pQuitBtn);
}

// Matches 0x0014ad04 (7 lines)
void MainScreen::Hide() {
    m_State = STATE_CAMERA_FADE;
    pos = Vec3(0.0f, 0.0f, 0.0f);
}

void MainScreen::RemoveButton(MenuButton*& btn) {
    if (btn) {
        // Mark the button for HUD removal. HUD::Update fires the
        // destructor → MenuButton::Release → entity->Deactivate,
        // cleaning up both the button AND the attached bomb/fruit
        // entity. Calling HUD::RemoveControl directly would leak
        // both because it just unlinks from the list without deleting.
        btn->m_bPendingRemoval = 1;
        btn = nullptr;
    }
}

// Helper: get texture size as Vec3, fallback to default
static Vec3 TexSize(const Mortar::SmartPtr<Mortar::Texture>& tex, float defW, float defH) {
    if (tex.IsValid() && tex->m_Width > 0)
        return Vec3((float)tex->m_Width, (float)tex->m_Height, 1.0f);
    return Vec3(defW, defH, 1.0f);
}

void MainScreen::CreateToggles() {
    if (!game.hud) return;

    // Sound toggle: (216.0, 135.5, 0.0), fruitType=-1 (no fruit).
    // Size comes from the texture dimensions (TexSize fallback = 32×32).
    pSoundToggle = new MenuButton();
    pSoundToggle->m_Texture = (game.m_bSoundOn ? m_TexSoundOn : m_TexSoundOff);
    pSoundToggle->size = TexSize(m_TexSoundOn, 32.0f, 32.0f);
    pSoundToggle->Init(POS_SOUND_TOGGLE,
        Mortar::Delegate0<void>::Make(this, &MainScreen::SoundCallback), -1, Vec3(0,0,0), nullptr);
    pSoundToggle->m_LayerFlags = 8;
    game.hud->AddControl(pSoundToggle);

    // Music toggle: (176.0, 135.5, 0.0)
    pMusicToggle = new MenuButton();
    pMusicToggle->m_Texture = (game.m_bMusicOn ? m_TexMusicOn : m_TexMusicOff);
    pMusicToggle->size = TexSize(m_TexMusicOn, 32.0f, 32.0f);
    pMusicToggle->Init(POS_MUSIC_TOGGLE,
        Mortar::Delegate0<void>::Make(this, &MainScreen::MusicCallback), -1, Vec3(0,0,0), nullptr);
    pMusicToggle->m_LayerFlags = 8;
    game.hud->AddControl(pMusicToggle);
}

void MainScreen::CreatePlayDojo() {
    if (!game.hud) return;

    // Play button: (16.0, -66.0, -50.0), fruitType=3 (watermelon)
    // Ring texture drawn at native texture size — matches the binary's
    // HUDControl3d Scale44(size) path where size comes from the texture's
    // reported width/height.
    pPlayButton = new MenuButton();
    pPlayButton->m_Texture = (m_TexNewGame);
    pPlayButton->size = TexSize(m_TexNewGame, 64.0f, 64.0f);
    pPlayButton->Init(POS_PLAY_BUTTON,
        Mortar::Delegate0<void>::Make(this, &MainScreen::GameModeCallback), 3, Vec3(0,0,0), nullptr);
    pPlayButton->m_LayerFlags = 8;
    // RemoveCallback: matches binary MainScreen::ButtonDeleted @ 0x0014acc0.
    // HUD::Update fires this right before deleting the MenuButton so we
    // can null our weak ref. Required for the dojo back-slice flow:
    // FN::ClearMenuItems releases Play/Dojo fruits -> MenuButton shrink
    // path deletes the button -> without this callback pPlayButton
    // stays dangling and STATE_CAMERA_ZOOM's `if (!pPlayButton)` guard
    // skips CreatePlayDojo on return.
    pPlayButton->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
    // ASM-verified: 2026-05-09 binary @ 0x0014b818..0x0014b82c (re-analyst).
    // pPlayButton is the only MenuButton in the game whose Init defaults
    // (m_AnimSpeed=5, m_AnimSpeed2=5, m_AnimScale=1) are overridden:
    //   m_AnimSpeed  = -50.0f   (DAT_0014b860)
    //   m_AnimSpeed2 = -50.0f
    //   m_AnimScale  = 0.5f
    // m_AnimScale halves the scratchs backdrop scale formula
    //   (size.x * 1.125 * m_AnimScale) on the big NEW GAME button so it
    // doesn't dominate the splash screen.
    pPlayButton->m_AnimSpeed  = -50.0f;
    pPlayButton->m_AnimSpeed2 = -50.0f;
    pPlayButton->m_AnimScale  = 0.5f;
    game.hud->AddControl(pPlayButton);

    // Binary @ 0x0014b6f8: ResetTutePos called immediately after play button
    // is created and wired, so the tutorial arrow targets the Play button.
    if (game.pTutorialCtrl)
        game.pTutorialCtrl->ResetTutePos(pPlayButton);

    // Dojo button: (-144.0, -65.0, 0.0). Binary calls
    // Fruit::FruitType("mango", false) at runtime — resolves to 9
    // in the current fruitlist, but use the runtime call per CLAUDE.md
    // "no shortcuts or abbreviations" rule.
    pDojoButton = new MenuButton();
    pDojoButton->m_Texture = (m_TexDojoIcon);
    pDojoButton->size = TexSize(m_TexDojoIcon, 64.0f, 64.0f);
    pDojoButton->Init(POS_DOJO_BUTTON,
        Mortar::Delegate0<void>::Make(this, &MainScreen::AboutCallback),
        Fruit::FruitType("mango", false), Vec3(0,0,0), nullptr);
    pDojoButton->m_LayerFlags = 8;
    pDojoButton->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
    game.hud->AddControl(pDojoButton);
}

void MainScreen::CreateQuitButton() {
    if (!game.hud) return;

    // Quit button: (182.0, -106.0, 0.0) — binary uses quit.tex (+0x98) at +0xA4
    pQuitBtn = new MenuButton();
    pQuitBtn->m_Texture = (m_TexQuit);
    pQuitBtn->size = TexSize(m_TexQuit, 48.0f, 48.0f);
    // Binary: m_bRespondsToBackKey set to 1 BEFORE Init.
    pQuitBtn->m_bRespondsToBackKey = 1;
    // Binary: fruitType = *g_pFruitInfo = fruitCount (>= count → Bomb entity via MenuButton)
    int fruitCount = FruitInfo_GetCount();
    pQuitBtn->Init(POS_QUIT,
        Mortar::Delegate0<void>::Make(this, &MainScreen::QuitGamesCallback), fruitCount, Vec3(0,0,0), nullptr);
    pQuitBtn->m_LayerFlags = 8;
    pQuitBtn->m_RemoveCallback =
        Mortar::Delegate1<void, HUDControl*>::Make(this, &MainScreen::ButtonDeleted);
    game.hud->AddControl(pQuitBtn);
}

// Matches MainScreen::ButtonDeleted @ 0x0014acc0. Binary dispatches by
// identity on all four button pointers — port mirrors 1:1.
void MainScreen::ButtonDeleted(HUDControl* ctrl) {
    if (ctrl == pDojoButton)    pDojoButton    = nullptr;
    if (ctrl == pPlayButton)    pPlayButton    = nullptr;
    if (ctrl == pQuitBtn)       pQuitBtn       = nullptr;
    if (ctrl == pMoreGamesBtn)  pMoreGamesBtn  = nullptr;
}

// --- Callbacks (all fully decompiled in docs/screens/main.md) ---

// Matches 0x0014b068
void MainScreen::GameModeCallback() {
    m_State = STATE_MODE_SELECT;
    m_Timer2 = 1.0f;
    // Remove the Quit button immediately. Binary just nulls the
    // pointer here and relies on DeleteMenuButtons / the screen
    // teardown to release the control; the port can't do that
    // safely because RemoveButton requires a non-null pointer.
    RemoveButton(pQuitBtn);
}

// Matches 0x0014c384
void MainScreen::NewGameCallback() {
    m_State = STATE_GAME_START;
    // ASM-verified: 2026-05-08 binary @ 0x0014c3ce (re-analyst). Literal at
    // 0x001b96af is "Game-start" (Title-Case). Bada's sound loader resolves
    // filenames case-insensitively; the SDL port mirrors that in
    // SoundManager::LoadSound's POSIX dirent fallback.
    if (game.pGameSound) game.pGameSound->SFXPlay("Game-start", 1.0f, 1.0f);
}

// Matches 0x0014afc4
void MainScreen::AboutCallback() {
    m_State = STATE_DOJO_WAIT_B;
    m_Timer2 = 1.0f;
    // Same fix as GameModeCallback — remove from HUD instead of
    // just nulling the pointer.
    RemoveButton(pQuitBtn);
}

// Matches 0x0014af64
void MainScreen::SoundCallback() {
    game.m_bSoundOn = !game.m_bSoundOn;
    Mortar::SoundManager::GetInstance().SetSFXVolume(
        game.m_bSoundOn ? SOUND_VOLUME_ON : 0.0f);
}

// Matches 0x0014ac9c
void MainScreen::MusicCallback() {
    // Binary only flips the m_bMusicOn flag (+0x45); UpdateMusic (0x0016a68c)
    // consults it and ramps SetMusicVolume up/down via its crossfade state
    // machine. Do NOT add a SetMusicVolume call here -- the spec confirms the
    // binary does not.
    game.m_bMusicOn = !game.m_bMusicOn;
}

// Matches 0x0014b010
void MainScreen::LeaderboardsCallback() {
    m_State = STATE_LEADERBOARD;  // network — skip for port
}

// Matches 0x0014b000
void MainScreen::MoreGamesCallback() {
    m_State = STATE_MORE_GAMES;  // network — skip for port
}

// ASM-verified: 2026-04-30 binary @ 0x0014b1a0..0x0014b1ed (asm-inspector + re-analyst).
// Sequence (4 logical operations only):
//   1. SystemManager::RequestQuit()                        // sets m_QuitState = 2
//   2. bomb = pQuitBtn->m_pFruitPiece (bomb-typed MenuButton)
//      bomb->m_bMovement = 1                                // +0x80
//      bomb->m_AccelForce = Vec3(0,1,0) * 10.0f             // +0x8c, kick upward
//   3. m_State = STATE_QUIT_WAIT (0x17)
//
// Bomb::Update consumes m_bMovement + m_AccelForce: each frame adds
// accelForce*dt to vel, then grows accelForce when direction-aligned —
// the bomb self-amplifies upward off-screen.
//
// No BombBlast / camera shake here in the binary — both come later via
// HitMenuBomb (called from STATE_QUIT_WAIT once Mortar::ActorManager clears).
// Earlier port-specific BombBlast spawn + camera shake removed.
//
// Vec3 const at GOT slot 0x00007214: runtime-init by _GLOBAL__I_MainScreen_cpp
// to (0,1,0) per binary @ 0x0014dab2. Port hardcodes the resolved value.
void MainScreen::QuitGamesCallback() {
    SystemManager::GetInstance().RequestQuit();

    // Bomb-typed Quit button: m_pFruitPiece IS the Bomb pointer (binary
    // @ 0x0014b1c2 reads MenuButton+0x134; bomb stores back-ref at
    // Bomb+0x84 used by KillBomb to null this slot).
    if (pQuitBtn && pQuitBtn->m_pFruitPiece) {
        Bomb* bomb = static_cast<Bomb*>(
            static_cast<Mortar::Entity*>(pQuitBtn->m_pFruitPiece));
        bomb->m_bMovement = 1;
        bomb->m_AccelForce = Vec3(0.0f, 10.0f, 0.0f);  // (0,1,0) * 10
    }

    m_State = STATE_QUIT_WAIT;
    m_StateTimer = 0.0f;
}


// @ 0x0016bbb0
void MainScreen::DrawPostEffects() {
    // TODO: implement -- post-effect overlays (score flash, bonus anim, etc.)
}

// Binary @ 0x0014D1F8 — 8-segment radial loading spinner.
// Called from Draw when m_State == STATE_LOADING_A (0x13) or STATE_LOADING_B (0x14).
// Uses blurry_backing.tex as its texture (blank atlas region for coloured tris).
// 8 segments x 6 verts each = 48 verts total; alpha-fades across segments.
void MainScreen::DrawLoadingSymbol(const float* hudScale) {
    if (!m_blurryBackingTex.IsValid()) return;

    // DAT_0014D4B8 = 7 — mask for spinner phase (0..7)
    int idx   = (int)m_field108 & 7;  // DAT_0014D4B8
    int phase = (7 - idx) & 7;

    // Geometry constants:
    //   DAT_0014D4C0 = 0.03125f (1/32) — inner half-width of each segment arc
    //   DAT_0014D4C4 = 1.0f             — outer radius
    static const float kSmallR = 0.03125f;  // DAT_0014D4C0
    static const float kBigR   = 1.0f;      // DAT_0014D4C4

    // Build geometry once; the positions are constant, colours are updated per frame.
    static QUADCUSTOMVERTEX s_verts[48];
    static bool s_built = false;

    if (!s_built) {
        static const float kTwoPi = 6.283185307f;
        for (int seg = 0; seg < 8; seg++) {
            float a1    = (float)seg * (kTwoPi / 8.0f);
            float a1end = a1 + (kTwoPi / 8.0f);
            float a2    = a1 + (kTwoPi / 4.0f);

            float sx1 = sinf(a1)    * kBigR,   cy1 = cosf(a1)    * kBigR;
            float ex1 = sinf(a1end) * kBigR,   ey1 = cosf(a1end) * kBigR;
            float sx2 = sinf(a2)    * kSmallR, cy2 = cosf(a2)    * kSmallR;

            float corners[4][2] = {
                { sx1 + sx2, cy1 + cy2 },
                { sx1 - sx2, cy1 - cy2 },
                { ex1 + sx2, ey1 + cy2 },
                { ex1 - sx2, ey1 - cy2 },
            };

            // Two tris per segment (6 verts): tri0 = 0,1,2; tri1 = 1,3,2
            static const int kOrder[6][2] = {{0,0},{1,1},{2,2},{1,1},{3,3},{2,2}};
            int vbase = seg * 6;
            for (int v = 0; v < 6; v++) {
                QUADCUSTOMVERTEX& qv = s_verts[vbase + v];
                qv.x = corners[kOrder[v][0]][0];
                qv.y = corners[kOrder[v][0]][1];
                qv.z = 0.0f;
                qv.nx = 0.0f; qv.ny = 0.0f; qv.nz = 1.0f;
                qv.colour = 0xC8FFFFFFu;  // alpha=200, white placeholder
                qv.u = 0.0f; qv.v = 0.0f;
            }
        }
        s_built = true;
    }

    // Per-frame: assign per-segment alpha based on phase.
    // fadeIdx = (phase + seg) & 7; intensity = clamp(fadeIdx * 32, 64, 255)
    for (int seg = 0; seg < 8; seg++) {
        int fadeIdx   = (phase + seg) & 7;
        int raw       = fadeIdx * 32;
        int intensity = (raw < 64) ? 64 : ((raw > 255) ? 255 : raw);
        uint32_t col  = ((uint32_t)200 << 24) |
                        ((uint32_t)intensity << 16) |
                        ((uint32_t)intensity << 8) |
                        (uint32_t)intensity;
        for (int v = 0; v < 6; v++) {
            s_verts[seg * 6 + v].colour = col;
        }
    }

    // Draw: Set blurry_backing, scale by *hudScale * 0.0625 (DAT_0014D4C8 = 1/16),
    // translate per state.
    MatrixManager& mm = MatrixManager::GetInstance();
    m_blurryBackingTex->Set();

    float scale = (*hudScale) * 0.0625f;  // DAT_0014D4C8

    // Per-state translate:
    //   STATE_LOADING_B (0x14): DAT_0014D4CC (X), DAT_0014D4D0 (Y) — values unresolved
    //   STATE_LOADING_A (0x13): DAT_0014D4D8 (X), 7.0 (Y) — DAT_0014D4D8 unresolved
    // TODO: DAT_0014D4CC, DAT_0014D4D0, DAT_0014D4D8 — read_memory pass needed to resolve values
    float tx = 0.0f, ty = 0.0f;
    if (m_State == STATE_LOADING_B) {
        // TODO: DAT_0014D4CC = state 0x14 X offset (unresolved)
        // TODO: DAT_0014D4D0 = state 0x14 Y offset (unresolved)
        tx = 0.0f; ty = 0.0f;
    } else {
        // STATE_LOADING_A: DAT_0014D4D8 X (unresolved), Y = 7.0
        tx = 0.0f; ty = 7.0f;
    }

    Vec3 drawPos(tx, ty, 0.0f);
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(scale, scale, 1.0f);
    mat.GlobalTranslate44(drawPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    game.renderer.DrawTriList(s_verts, 48);

    m_blurryBackingTex->UnSet();
}

// Defunct: vtable PreDraw — no-op stub matching binary's empty body; binary @ 0x0014AC94
// Vtable slot 6. Returns this. Binary is a 1-instruction stub (bx lr with r0=this).
void* MainScreen::PreDraw(float* /*hudScale*/) {
    return this;
}

// Defunct: NetworkManager::CancelNewsDisplay (online news ticker) — no-op stub; binary @ 0x0014AFB8
// Called from AboutCallback, NewGameCallback, MoreGamesCallback, LeaderboardsCallback,
// and Update cases 3/4. Binary calls NetworkManager::GetInstance + CancelNewsDisplay.
void MainScreen::CancelNews() {
    // Defunct: NetworkManager — no-op stub; binary @ 0x0014AFB8
}

// Defunct: network UI button — empty in binary (single bx lr); binary @ 0x0014ACFC
void MainScreen::ClearNetworkButton() {
    // Defunct: network UI button — no-op stub; binary @ 0x0014ACFC
}

// Defunct: leaderboard UI — returns this in binary (single bx lr); binary @ 0x0014AD00
MainScreen* MainScreen::CreateNormalLeaderboardButton() {
    // Defunct: leaderboard UI — no-op stub; binary @ 0x0014AD00
    return this;
}

// Binary @ 0x0014AC98 — empty event hook (single bx lr in binary).
// Called when menu items are cleared. Superseded by ButtonDeleted.
void MainScreen::OnMenuItemsCleared() {
    // no-op — empty in binary; binary @ 0x0014AC98
}

// Binary @ 0x0014B0AC — multiplayer variant of GameModeCallback (state 0xF instead of 0xE).
// Near-identical to GameModeCallback @ 0x0014B068 except:
//   - m_State = 0x0F (STATE_MODE_SELECT_2) instead of 0x0E
//   - No FruitSaveData::DownloadTweaks() call
void MainScreen::MultiplayerGameModeCallback() {
    m_State = STATE_MODE_SELECT_2;
    m_Timer2 = 1.0f;
    RemoveButton(pQuitBtn);
}
