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
// Analysed: 2026-04-25T12:00
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

// Helper: get GLuint from SmartPtr<Texture>
static GLuint TexId(const SmartPtr<Mortar::Texture>& tex) {
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
    : game(g),
      pPlayButton(nullptr), pDojoButton(nullptr),
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
      m_pGameModeScreen(nullptr)
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
        std::string fontPath = game.data_dir + "/fonts/verdana.fnt";
        m_pFont = Mortar::Font::Load(fontPath.c_str());
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

    printf("MainScreen: ctor (size=%.0f,%.0f pos=%.0f,%.0f)\n",
           size.x, size.y, pos.x, pos.y);
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

    // TODO: delete textures and font when proper resource management exists
}

// Matches Update at 0x0014b278 (677 lines) — state machine
void MainScreen::Update(float dt) {
    m_Time += dt;

    switch (m_State) {
    case STATE_CAMERA_ZOOM: {
        // Camera zoom-in from splash. Create toggles + play/dojo buttons.
        // Lerp camera transition toward -1.0
        m_CameraTransition += (-1.0f - m_CameraTransition) * CAMERA_LERP_RATE;
        m_Timer2 += dt;

        // Create toggles if they don't exist
        if (!pSoundToggle) {
            CreateToggles();
        }

        // Create play/dojo buttons if they don't exist
        if (!pPlayButton && m_Timer2 > TIMER2_THRESHOLD) {
            CreatePlayDojo();
        }

        // Transition to CREATE_BUTTONS when camera settles
        if (m_CameraTransition < CAMERA_THRESHOLD && m_Timer2 > TIMER2_THRESHOLD) {
            m_State = STATE_CREATE_BUTTONS;
            CreateQuitButton();
        }
        break;
    }

    case STATE_CREATE_BUTTONS:
        // Active menu state — nothing to do, buttons handle themselves
        // TODO: check ItemManager::AreNewItems() for "new" badge
        break;

    case STATE_GAME_START: {
        // Binary @ 0x0014bb58 case 2:
        //   if (cameraTransition > DAT_0014bb70) {
        //       game->field_0x28 = game->field_0x20;     // copy some state
        //       WaveManager::Reset(true);                 // full wave reset
        //       game->pauseFlag = 1;                      // gate updates
        //   }
        //   cameraTransition *= 0.75;
        //   if (|cameraTransition| < DAT_0014bb74) {
        //       game->pauseFlag = 0;
        //       cameraTransition = 0;
        //       m_State = STATE_CAMERA_FADE (0x11);
        //   }
        //   pos.y animation (shared LAB_0014c166 formula).
        if (!m_bGameStartReset) {
            WaveManager::GetInstance()->Reset(true);
            m_bGameStartReset = true;
        }
        m_CameraTransition *= 1.0f - (1.0f - STATE_2_DECAY) * FN::g_DebugTimeScale;
        if (m_CameraTransition > 0.999f) {
            m_CameraTransition = 1.0f;
        }
        if (fabsf(m_CameraTransition) < 0.01f) {
            m_CameraTransition = 0.0f;
            m_State = STATE_CAMERA_FADE;
            m_bGameStartReset = false;   // arm for next entry
        }
        break;
    }

    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B:
    case STATE_DOJO_WAIT_C:
    case STATE_DOJO_WAIT_D: {
        // Binary @ 0x0014be80: cases 3/4/0x15/0x16 share one block:
        //   if (ActorManager::GetNumEntities(0) == 0) {
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
        ActorManager* am = ActorManager::GetInstance();
        const int fruitCount = am ? am->GetNumEntities(0) : 0;

        if (fruitCount == 0) {
            m_Timer2 *= 1.0f - (1.0f - 0.75f) * FN::g_DebugTimeScale;
        }

        // Binary LAB_0014c166: apply pos.y slide formula with m_Timer2 as alpha.
        // As Timer2 decays 1→0, pos.y animates 91→229 (off-screen top).
        const float sizeY_d = size.y;
        const float tt_d = sizeY_d * m_Timer2;
        pos.y = (sizeY_d + 320.0f - 2.0f * tt_d) * 0.5f;

        if (!m_pDojoScreen && fruitCount == 0 && m_Timer2 < 0.01f) {
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
            m_pDojoScreen->m_RemoveCallback = [this](HUDControl*) {
                m_pDojoScreen = nullptr;
            };
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
        // waits on ActorManager empty then calls LaunchDashboard /
        // OpenMatchmaker on NetworkManager. All online services are
        // skipped in the port, so these states immediately bounce back
        // to the main menu flow with buttons cleared.
        m_State = STATE_CAMERA_ZOOM;
        m_Timer2 = 0.0f;
        m_StateTimer = 0.0f;
        m_CameraTransition = 0.0f;
        DeleteMenuButtons();
        break;

    case STATE_NEWS:            // binary case 0xb
        // Defunct — NetworkManager::UpdateNews polls for remote news
        // updates. Port skips the network call and returns straight to
        // the active-menu state.
        m_State = STATE_CREATE_BUTTONS;
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
            m_pGameModeScreen->m_RemoveCallback = [this](HUDControl*) {
                m_pGameModeScreen = nullptr;
            };
            game.hud->AddControl(m_pGameModeScreen);
        }
        break;
    }

    case STATE_CAMERA_FADE:
        // Camera fade after game return. Decay × 0.75 until settled.
        m_CameraTransition *= 1.0f - (1.0f - STATE_2_DECAY) * FN::g_DebugTimeScale;
        break;

    case STATE_LOADING_A:
    case STATE_LOADING_B:
        // Accumulate field108 += dt × 8. When >= 8.0 → reset.
        m_field108 += dt * 8.0f;
        if (m_field108 >= 8.0f) {
            m_field108 = 0.0f;
            m_State = STATE_CAMERA_ZOOM;
            m_StateTimer = 0.0f;
            m_CameraTransition = 0.0f;
            DeleteMenuButtons();
        }
        break;

    case STATE_QUIT_WAIT: {
        // Binary @ 0x0014c098 case 0x17:
        //   TutorialControl::ResetTutePos(pTC, nullptr)
        //   if (ActorManager::GetNumEntities(0) != 0) break;
        //   pLeaderboardBtn = null;
        //   gameMode = *(task_state+0x4c);
        //   if (gameMode == 2) { HitMenuBomb(pos); state = 0x18; }
        //   else if (gameMode == 3) { state = 0; timer = ... }
        //
        // Port: wait for ActorManager to clear (so the BombBlast spawned
        // by QuitGamesCallback finishes), then fire HitMenuBomb and
        // advance to STATE_QUIT_BOMB. Port skips the gameMode branch --
        // on the Main screen the Quit path is deterministic (always the
        // zen-like exit).
        if (game.pTutorialCtrl) {
            game.pTutorialCtrl->ResetTutePos((MenuButton*)nullptr);
        }
        m_StateTimer += dt;
        ActorManager* am = ActorManager::GetInstance();
        const int liveEntities = am ? am->GetNumEntities(0) : 0;
        const float QUIT_MAX_WAIT = 1.5f;   // belt-and-braces timeout
        if (liveEntities == 0 || m_StateTimer >= QUIT_MAX_WAIT) {
            // Pick the Quit bomb position for HitMenuBomb; fall back to
            // MainScreen origin if the button/entity is already gone.
            Vec3 hitPos(0.0f, 0.0f, 0.0f);
            if (pQuitBtn && pQuitBtn->m_pEntity) {
                hitPos = pQuitBtn->m_pEntity->pos;
            }
            FN::HitMenuBomb(hitPos);   // sets bombHitTimer = 2.0, plays SFX
            m_State = STATE_QUIT_BOMB;
            m_StateTimer = 0.0f;
        }
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
            Mortar::SystemManager::GetInstance().QuitGame();
            game.running = false;
        }
        break;
    }
    }

    // Position update (end of Update, all states)
    // Sound/music toggle texture swap
    if (pSoundToggle) {
        pSoundToggle->m_Texture = TexId(game.soundEnabled ? m_TexSoundOn : m_TexSoundOff);
    }
    if (pMusicToggle) {
        pMusicToggle->m_Texture = TexId(game.musicEnabled ? m_TexMusicOn : m_TexMusicOff);
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
}

// Helper: setup world matrix for a textured quad at given position
static void SetupQuadMatrix(Mortar::MatrixManager& mm, const Vec3& hudScale,
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
void MainScreen::Draw(const Vec3& hudScale, int layerMask) {
    (void)layerMask;

    // Skip drawing for certain states
    if (m_State == STATE_CAMERA_FADE) return;
    if ((m_State == STATE_DOJO_WAIT_A || m_State == STATE_DOJO_WAIT_B ||
         m_State == STATE_DOJO_WAIT_C || m_State == STATE_DOJO_WAIT_D) &&
        m_Timer2 == 0.0f) return;
    // For STATE_MODE_SELECT: binary keeps drawing but pos.y is animated
    // off-screen in Update, which pulls the logo positions with it.

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();

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

    // 5. Loading symbol (states 0x13, 0x14 only) — TODO

    // 6. "Coming soon" logo overlay — TODO (depends on pPlayButton existence)
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
        // Trigger the fade-out animation. The button decays its own
        // alpha over ~16 frames in Update, then sets m_bPendingRemoval
        // when alpha hits zero — at which point HUD::Update fires the
        // destructor → MenuButton::Release → entity->Deactivate,
        // cleaning up both the button AND the attached bomb/fruit
        // entity. Calling HUD::RemoveControl directly would leak
        // both because it just unlinks from the list without deleting.
        btn->StartFadeOut();
        btn = nullptr;
    }
}

// Helper: get texture size as Vec3, fallback to default
static Vec3 TexSize(const SmartPtr<Mortar::Texture>& tex, float defW, float defH) {
    if (tex.IsValid() && tex->m_Width > 0)
        return Vec3((float)tex->m_Width, (float)tex->m_Height, 1.0f);
    return Vec3(defW, defH, 1.0f);
}

void MainScreen::CreateToggles() {
    if (!game.hud) return;

    // Sound toggle: (216.0, 135.5, 0.0), fruitType=-1 (no fruit).
    // Size comes from the texture dimensions (TexSize fallback = 32×32).
    pSoundToggle = new MenuButton();
    pSoundToggle->m_Texture = TexId(game.soundEnabled ? m_TexSoundOn : m_TexSoundOff);
    pSoundToggle->size = TexSize(m_TexSoundOn, 32.0f, 32.0f);
    pSoundToggle->Init(POS_SOUND_TOGGLE,
        [this]() { SoundCallback(); }, -1, Vec3(0,0,0), nullptr);
    pSoundToggle->m_LayerFlags = 8;
    game.hud->AddControl(pSoundToggle);

    // Music toggle: (176.0, 135.5, 0.0)
    pMusicToggle = new MenuButton();
    pMusicToggle->m_Texture = TexId(game.musicEnabled ? m_TexMusicOn : m_TexMusicOff);
    pMusicToggle->size = TexSize(m_TexMusicOn, 32.0f, 32.0f);
    pMusicToggle->Init(POS_MUSIC_TOGGLE,
        [this]() { MusicCallback(); }, -1, Vec3(0,0,0), nullptr);
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
    pPlayButton->m_Texture = TexId(m_TexNewGame);
    pPlayButton->size = TexSize(m_TexNewGame, 64.0f, 64.0f);
    pPlayButton->Init(POS_PLAY_BUTTON,
        [this]() { GameModeCallback(); }, 3, Vec3(0,0,0), nullptr);
    pPlayButton->m_LayerFlags = 8;
    // RemoveCallback: matches binary MainScreen::ButtonDeleted @ 0x0014acc0.
    // HUD::Update fires this right before deleting the MenuButton so we
    // can null our weak ref. Required for the dojo back-slice flow:
    // FN::ClearMenuItems releases Play/Dojo fruits -> MenuButton shrink
    // path deletes the button -> without this callback pPlayButton
    // stays dangling and STATE_CAMERA_ZOOM's `if (!pPlayButton)` guard
    // skips CreatePlayDojo on return.
    pPlayButton->m_RemoveCallback =
        [this](HUDControl* c) { ButtonDeleted(c); };
    game.hud->AddControl(pPlayButton);

    // Dojo button: (-144.0, -65.0, 0.0). Binary calls
    // Fruit::FruitType("mango", false) at runtime — resolves to 9
    // in the current fruitlist, but use the runtime call per CLAUDE.md
    // "no shortcuts or abbreviations" rule.
    pDojoButton = new MenuButton();
    pDojoButton->m_Texture = TexId(m_TexDojoIcon);
    pDojoButton->size = TexSize(m_TexDojoIcon, 64.0f, 64.0f);
    pDojoButton->Init(POS_DOJO_BUTTON,
        [this]() { AboutCallback(); },
        Fruit::FruitType("mango", false), Vec3(0,0,0), nullptr);
    pDojoButton->m_LayerFlags = 8;
    pDojoButton->m_RemoveCallback =
        [this](HUDControl* c) { ButtonDeleted(c); };
    game.hud->AddControl(pDojoButton);
}

void MainScreen::CreateQuitButton() {
    if (!game.hud) return;

    // Quit button: (182.0, -106.0, 0.0) — binary uses quit.tex (+0x98) at +0xA4
    pQuitBtn = new MenuButton();
    pQuitBtn->m_Texture = TexId(m_TexQuit);
    pQuitBtn->size = TexSize(m_TexQuit, 48.0f, 48.0f);
    // Binary: fruitType = *g_pFruitInfo = fruitCount (>= count → Bomb entity via MenuButton)
    int fruitCount = FruitInfo_GetCount();
    pQuitBtn->Init(POS_QUIT,
        [this]() { QuitGamesCallback(); }, fruitCount, Vec3(0,0,0), nullptr);
    pQuitBtn->m_LayerFlags = 8;
    pQuitBtn->m_RemoveCallback =
        [this](HUDControl* c) { ButtonDeleted(c); };
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
    if (game.pGameSound) game.pGameSound->SFXPlay("swoosh_sound", 1.0f, 1.0f);
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
    game.soundEnabled = !game.soundEnabled;
    Mortar::SoundManager::GetInstance().SetSFXVolume(
        game.soundEnabled ? SOUND_VOLUME_ON : 0.0f);
    printf("MainScreen: Sound %s\n", game.soundEnabled ? "ON" : "OFF");
}

// Matches 0x0014ac9c
void MainScreen::MusicCallback() {
    game.musicEnabled = !game.musicEnabled;
    // Note: no direct music play/stop — just flips flag
    printf("MainScreen: Music %s\n", game.musicEnabled ? "ON" : "OFF");
}

// Matches 0x0014b010
void MainScreen::LeaderboardsCallback() {
    m_State = STATE_LEADERBOARD;  // network — skip for port
}

// Matches 0x0014b000
void MainScreen::MoreGamesCallback() {
    m_State = STATE_MORE_GAMES;  // network — skip for port
}

// Matches MainScreen::QuitGamesCallback (0x0014b1a0).
// Binary flow (what we can confirm from the ARM disassembly):
//   1. SystemManager::RequestQuit() — set the quit-pending flag
//   2. Writes to MainScreen[+0xa4]->field_at_0x134 (some Entity*): sets
//      one byte at offset +0x80 = 1 and writes a Vec3 * 10.0 at
//      offset +0x8c..+0x94. Ghidra auto-named +0xa4 as pDojoButton and
//      +0x134 as m_pFruitPiece, which would point at the mango Fruit,
//      but that mapping produces a Vec3 write that straddles unrelated
//      Fruit fields (m_RotAxis.z + m_PlayerIdx + m_TimeScale) — likely
//      a Ghidra struct-inference mistake, not the real intent. We
//      intentionally don't port the launch write; its target is unclear.
//   3. MainScreen::m_State = STATE_QUIT_WAIT (0x17)
//
// Note: the binary's menu-rehit branch in Bomb::CollisionResponse
// (0x0017280c) fires this callback and clears menu items — no camera
// shake, no BombBlast, no HitMenuBomb SFX in the binary itself. The
// BombBlast + shake + SFX below are a port-specific deviation to keep
// the slice gesture visually satisfying (pre-9567eb9 port bug used to
// trigger them by accident via the Classic/Arcade branch).
void MainScreen::QuitGamesCallback() {
    Mortar::SystemManager::GetInstance().RequestQuit();

    // Port specific: spawn a one-shot BombBlast + camera shake at the
    // Quit bomb's position so slicing the bomb has visual punch. Binary's
    // menu-rehit branch does not spawn a BombBlast (those are gameplay-
    // bomb specific) and does not shake the camera. The "menu-bomb" SFX
    // and the screen flash are handled later via HitMenuBomb() in
    // STATE_QUIT_WAIT once ActorManager has cleared.
    if (pQuitBtn && pQuitBtn->m_pEntity) {
        const Vec3& bombPos = pQuitBtn->m_pEntity->pos;
        if (ActorManager* am = ActorManager::GetInstance()) {
            if (Entity* e = am->Add(4, true)) {   // entity type 4 = BombBlast
                e->pos = bombPos;
                e->Init(0, 0, 0);
            }
        }
        if (game.pCamera) {
            game.pCamera->CreateCameraShake(bombPos, 1.6f, 2.0f);
        }
    }

    m_State = STATE_QUIT_WAIT;
    m_StateTimer = 0.0f;
}

