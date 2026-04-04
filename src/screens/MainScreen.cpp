//
// MainScreen — reimplemented from docs/screens/main.md
// Original: 0x0014c430 (ctor), 0x0014b278 (Update, 677 lines), 0x0014d4ec (Draw, 171 lines)
//

#include "MainScreen.h"
#include "Game.h"
#include "HUD.h"
#include "ActorManager.h"
#include "MenuButton.h"
#include "DojoScreen.h"
#include "InputManager.h"
#include "SDLInputTranslator.h"
#include <cstdio>
#include <cmath>

// Verified constants from binary (docs/screens/main.md "Timing Constants")
static const float CAMERA_LERP_RATE    = 0.125f;
static const float CAMERA_THRESHOLD    = -0.999f;
static const float TIMER2_THRESHOLD    = 0.15f;
static const float MODE_SELECT_DECAY   = 0.85f;
static const float MODE_SELECT_THRESH  = 0.25f;
static const float GAME_START_DECAY    = 0.75f;
static const float SLIDE_IN_LERP       = 0.125f;
static const float SLIDE_IN_DURATION   = 1.5f;
static const float SLIDE_IN_RESET_T2   = -0.85f;
static const float LOGO_BOUNCE_LOSS    = -0.25f;
static const float LOGO_SETTLE_THRESH  = 3.0f;
static const float ALPHA_LERP_RATE     = 0.25f;
static const float SOUND_VOLUME_ON     = 0.5f;

// Verified button positions from binary read_memory
static const Vec3 POS_SOUND_TOGGLE  (216.0f, 135.5f, 0.0f);
static const Vec3 POS_MUSIC_TOGGLE  (176.0f, 135.5f, 0.0f);
static const Vec3 POS_PLAY_BUTTON   (16.0f, -66.0f, 0.0f);
static const Vec3 POS_DOJO_BUTTON   (-144.0f, -65.0f, 0.0f);
static const Vec3 POS_LEADERBOARD   (182.0f, -106.0f, 0.0f);

// Original centered coordinates used directly — no conversion needed
// The ortho projection is SetupOrtho(160, -160, -240, 240) matching the binary

// ======================== Constructor ========================
// Matches 0x0014c430 (159 lines)

MainScreen::MainScreen(Game& g)
    : game(g),
      m_OrigSize(480.0f, 138.0f, 1.0f),
      m_TexNewGame(0), m_TexDojoIcon(0), m_TexQuit(0),
      m_TexOpenFeint(0), m_TexMoreGames(0),
      pPlayButton(NULL), pDojoButton(NULL),
      pLeaderboardBtn(NULL), pMoreGamesBtn(NULL),
      pSoundToggle(NULL), pMusicToggle(NULL),
      m_TexCommingSoon(0),
      m_TexSoundOn(0), m_TexSoundOff(0), m_TexMusicOn(0), m_TexMusicOff(0),
      m_TexSliceFruit(0),
      m_Alpha(1.0f),
      m_WindowCenter(320.0f / 2.0f + 160.0f),
      m_BounceVelocity(0.0f),
      m_field108(0.0f),
      m_State(STATE_CAMERA_ZOOM),
      m_Timer(0.0f), m_Timer2(0.0f),
      m_CameraTransition(1.0f),
      m_GlobalAlphaTarget(1.0f),
      m_Time(0.0f)
{
    // Fruit entities are created via ActorManager in CreateFruitEntity,
    // not loaded here. Mesh loading happens in Fruit::Init.

    // Step 3: Load global textures
    if (!game.blurry_backing_tex)
        game.blurry_backing_tex = game.load_texture("blurry_backing.tex", m_ImgBlurryBacking);
    if (!game.fruit_text_tex)
        game.fruit_text_tex = game.load_texture("fruit_text.tex", m_ImgFruitText);
    if (!game.ninja_text_tex)
        game.ninja_text_tex = game.load_texture("ninja_text.tex", m_ImgNinjaText);

    // Step 4: Dojo decoration
    m_TexSliceFruit = game.load_texture("slice_fruit.tex", m_ImgSliceFruit);

    // Step 6: Button textures
    m_TexNewGame = game.load_texture("newgame.tex", m_ImgNewGame);
    m_TexDojoIcon = game.load_texture("dojo_icon.tex", m_ImgDojoIcon);
    {
        TexImage tmp;
        m_TexQuit = game.load_texture("quit.tex", tmp);
        m_TexOpenFeint = game.load_texture("openfeint.tex", tmp);
        m_TexMoreGames = game.load_texture("more_games.tex", tmp);
        m_TexGCAchievements = game.load_texture("gc_achievements.tex", tmp);
    }

    // Step 8: Toggle textures
    {
        TexImage tmp;
        m_TexSoundOn  = game.load_texture("sound.tex", tmp);
        m_TexSoundOff = game.load_texture("sound_cross.tex", tmp);
        m_TexMusicOn  = game.load_texture("music.tex", tmp);
        m_TexMusicOff = game.load_texture("music_cross.tex", tmp);
    }

    // Step 9: Logo overlay
    m_TexCommingSoon = game.load_texture("swipe_fruit_begin.tex", m_ImgCommingSoon);

    // Step 10-13: Set HUDControl3d base fields (matching original ctor)
    size = Vec3(480.0f, 138.0f, 1.0f);
    pos = Vec3(0.0f, (320.0f - 138.0f) * 0.5f, 0.0f);
    m_bActive = true;
    m_LayerFlags = 0x01;  // MainScreen drawn in foreground layer
    m_Alpha = 255;

    // Register touch input via InputManager (matches original SplashInit pattern)
    InputManager* mgr = InputManager::GetInstance();
    if (mgr) {
        // Touch down — route to buttons
        mgr->RegisterInputCallback(
            game.inputTranslator.hashTouchScreen, INPUT_ACTION_DOWN,
            [this](InputEvent* ev) -> bool {
                return HandleTouchDown(ev->x, ev->y);
            });
        // Touch up on all channels — release buttons
        for (int i = 0; i < 16; i++) {
            mgr->RegisterInputCallback(
                game.inputTranslator.hashTouchUp[i], INPUT_ACTION_UP,
                [this](InputEvent* ev) -> bool {
                    HandleTouchUp(ev->x, ev->y);
                    return false;
                });
        }
    }
}

MainScreen::~MainScreen() {
    // Remove all buttons from HUD
    RemoveButton(pPlayButton);
    RemoveButton(pDojoButton);
    RemoveButton(pLeaderboardBtn);
    RemoveButton(pMoreGamesBtn);
    RemoveButton(pSoundToggle);
    RemoveButton(pMusicToggle);

    // Delete textures
    GLuint textures[] = {
        m_TexNewGame, m_TexDojoIcon, m_TexQuit, m_TexOpenFeint, m_TexMoreGames,
        m_TexGCAchievements, m_TexSliceFruit, m_TexCommingSoon,
    };
    for (int i = 0; i < 13; i++) {
        if (textures[i]) glDeleteTextures(1, &textures[i]);
    }
}

void MainScreen::RemoveButton(MenuButton*& btn) {
    if (btn && game.hud) {
        game.hud->RemoveControl(btn);
        btn = NULL;
    }
}

// Note: no enter()/exit() — MainScreen is activated by adding to HUD.
// State is initialized in the constructor.

// ======================== Button Creation ========================
// Matches button creation pattern from docs/screens/main.md

void MainScreen::CreateToggles() {
    // Sound toggle — verified: (216.0, 135.5), 32×32
    if (!pSoundToggle) {
        GLuint tex = game.soundEnabled ? m_TexSoundOn : m_TexSoundOff;
        if (tex) {
            Vec3 sp = POS_SOUND_TOGGLE;
            pSoundToggle = new MenuButton();
            pSoundToggle->init(tex, 32.0f, 32.0f, sp.x, sp.y,
                               [this]() { SoundCallback(); });
            pSoundToggle->rotation_speed = 0.0f;
            pSoundToggle->m_LayerFlags = 0x08;
            game.hud->AddControl(pSoundToggle);
        }
    }

    // Music toggle — verified: (176.0, 135.5), 32×32
    if (!pMusicToggle) {
        GLuint tex = game.musicEnabled ? m_TexMusicOn : m_TexMusicOff;
        if (tex) {
            Vec3 mp = POS_MUSIC_TOGGLE;
            pMusicToggle = new MenuButton();
            pMusicToggle->init(tex, 32.0f, 32.0f, mp.x, mp.y,
                               [this]() { MusicCallback(); });
            pMusicToggle->rotation_speed = 0.0f;
            pMusicToggle->m_LayerFlags = 0x08;
            game.hud->AddControl(pMusicToggle);
        }
    }
}

void MainScreen::CreatePlayDojo() {
    // Play button — verified: (16.0, -66.0), fruitType 3 (watermelon)
    if (!pPlayButton && m_TexNewGame) {
        Vec3 pp = POS_PLAY_BUTTON;
        pPlayButton = new MenuButton();
        pPlayButton->init(m_TexNewGame,
                          (float)m_ImgNewGame.width, (float)m_ImgNewGame.height,
                          pp.x, pp.y,
                          [this]() { GameModeCallback(); });
            // TODO: pPlayButton->CreateFruitEntity(game, 3); // fruitType 3 = watermelon
        pPlayButton->m_LayerFlags = 0x08;
        game.hud->AddControl(pPlayButton);
    }

    // Dojo button — verified: (-144.0, -65.0), scale 0.9×
    if (!pDojoButton && m_TexDojoIcon) {
        Vec3 dp = POS_DOJO_BUTTON;
        pDojoButton = new MenuButton();
        pDojoButton->init(m_TexDojoIcon,
                          (float)m_ImgDojoIcon.width * 0.9f,
                          (float)m_ImgDojoIcon.height * 0.9f,
                          dp.x, dp.y,
                          [this]() { AboutCallback(); });
            // TODO: pDojoButton->CreateFruitEntity(game, 9); // fruitType 9 = mango
        pDojoButton->m_LayerFlags = 0x08;
        game.hud->AddControl(pDojoButton);
    }
}

void MainScreen::CreateLeaderboard() {
    // Leaderboard — verified: (182.0, -106.0)
    if (!pLeaderboardBtn && m_TexOpenFeint) {
        TexImage tmp;
        Vec3 lp = POS_LEADERBOARD;
        pLeaderboardBtn = new MenuButton();
        pLeaderboardBtn->init(m_TexOpenFeint, 64.0f, 64.0f, lp.x, lp.y,
                              []() { /* LeaderboardsCallback — skip for port */ });
            // TODO: pLeaderboardBtn->CreateFruitEntity(game, 5); // fruitType 5 = kiwifruit
        pLeaderboardBtn->m_LayerFlags = 0x08;
        game.hud->AddControl(pLeaderboardBtn);
    }
}

void MainScreen::DeleteMenuButtons() {
    // Matches 0x0014aee8: removes Play, Dojo, MoreGames — NOT toggles or leaderboard
    RemoveButton(pPlayButton);
    RemoveButton(pDojoButton);
    RemoveButton(pMoreGamesBtn);
}

void MainScreen::Hide() {
    // Matches 0x0014ad04
    m_State = STATE_CAMERA_FADE;
    pos = Vec3(0, 0, 0);
}

// ======================== Update ========================
// Matches 0x0014b278 (677 lines) — state machine

void MainScreen::Update(float dt) {
    m_Time += dt;

    switch (m_State) {

    case STATE_CAMERA_ZOOM: {
        // Create toggles + play/dojo
        CreateToggles();
        CreatePlayDojo();

        // Lerp camera toward -1.0 at rate 0.125
        m_CameraTransition += (-1.0f - m_CameraTransition) * CAMERA_LERP_RATE;
        m_Timer2 += dt;

        // Transition to CREATE_BUTTONS when timer2 > 0.15 and camera < 0
        if (m_Timer2 > TIMER2_THRESHOLD && m_CameraTransition < 0.0f) {
            m_State = STATE_CREATE_BUTTONS;
        }
        break;
    }

    case STATE_CREATE_BUTTONS:
        // Create leaderboard if not yet
        CreateLeaderboard();
        break;

    case STATE_GAME_START: {
        // Camera decay
        m_CameraTransition *= GAME_START_DECAY;
        if (fabsf(m_CameraTransition) < 0.001f) {
            m_State = STATE_CAMERA_FADE;
        }
        break;
    }

    case STATE_DOJO_WAIT_A:
    case STATE_DOJO_WAIT_B: {
        // Wait then transition to DojoScreen
        m_Timer2 *= GAME_START_DECAY;
        if (m_Timer2 < 0.01f) {
            m_Timer2 = 0.0f;
            // TODO: transition to DojoScreen (Phase 8)
        }
        break;
    }

    case STATE_SLIDE_IN: {
        // Slide-in return: lerp timer2 → 1.0
        m_Timer2 += (1.0f - m_Timer2) * SLIDE_IN_LERP;
        if (m_Timer2 > 0.99f) {
            m_Timer += dt;
            if (m_Timer >= SLIDE_IN_DURATION) {
                m_State = STATE_CAMERA_ZOOM;
                m_Timer = TIMER2_THRESHOLD;
                m_Timer2 = SLIDE_IN_RESET_T2;
            }
        }
        break;
    }

    case STATE_LEADERBOARD:
    case STATE_MORE_GAMES:
    case STATE_NEWS:
    case STATE_MATCHMAKER:
        // Network states — skip for port, return to menu
        m_State = STATE_CAMERA_ZOOM;
        break;

    case STATE_MODE_SELECT:
    case STATE_MODE_SELECT_2: {
        // Slide-out: decay timer2
        m_Timer2 *= MODE_SELECT_DECAY;
        if (m_Timer2 < MODE_SELECT_THRESH) {
            // TODO: create GameModeScreen when implemented
            printf("MainScreen: would create GameModeScreen\n");
            m_State = STATE_CREATE_BUTTONS;
            m_Timer2 = 0.0f;
        }
        break;
    }

    case STATE_CAMERA_FADE: {
        m_CameraTransition *= GAME_START_DECAY;
        if (fabsf(m_CameraTransition) < 0.001f)
            m_CameraTransition = 0.0f;
        break;
    }

    case STATE_LOADING_A:
    case STATE_LOADING_B: {
        m_field108 += dt * 8.0f;
        if (m_field108 >= 8.0f) {
            m_field108 = 0.0f;
            m_State = STATE_CAMERA_ZOOM;
        }
        break;
    }

    case STATE_DOJO_WAIT_C:
    case STATE_DOJO_WAIT_D: {
        m_Timer2 *= GAME_START_DECAY;
        if (m_Timer2 < 0.01f) {
            m_Timer2 = 0.0f;
            // TODO: transition to DojoScreen (Phase 8)
        }
        break;
    }

    case STATE_QUIT_WAIT:
    case STATE_QUIT_BOMB:
        game.running = false;
        break;
    }

    // === Position update (runs every frame, all states) ===

    // Toggle texture swap (matches original: game->soundEnabled ^ 1)
    if (pSoundToggle)
        pSoundToggle->m_Texture = game.soundEnabled ? m_TexSoundOn : m_TexSoundOff;
    if (pMusicToggle)
        pMusicToggle->m_Texture = game.musicEnabled ? m_TexMusicOn : m_TexMusicOff;

    // Toggle position update (original: pos.y=135.5, pos.x=216/176 when camera<=0)
    if (pSoundToggle && pMusicToggle) {
        Vec3 sp = POS_SOUND_TOGGLE;
        Vec3 mp = POS_MUSIC_TOGGLE;
        pSoundToggle->pos = sp;
        pMusicToggle->pos = mp;

        // Slide offset based on camera transition (simplified: no GetPauseAmount)
        float pauseAmount = (-m_CameraTransition);
        if (pauseAmount < 0.0f) pauseAmount = 0.0f;
        if (pauseAmount > 1.0f) pauseAmount = 1.0f;
        float slideOffset = size.y * 2.0f * (1.0f - pauseAmount);
        pSoundToggle->pos.y += slideOffset;
        pMusicToggle->pos.y += slideOffset;
        pSoundToggle->m_bActive = (pauseAmount > 0.01f);
        pMusicToggle->m_bActive = (pauseAmount > 0.01f);
    }

    // Button alpha
    uint8_t buttonAlpha = (uint8_t)(m_Alpha * 255.0f);
    if (m_State == STATE_CAMERA_ZOOM)
        buttonAlpha = (m_Timer2 > TIMER2_THRESHOLD) ? (uint8_t)(m_Alpha * 255.0f) : 0;

    if (pPlayButton) pPlayButton->m_Alpha = buttonAlpha;
    if (pDojoButton) pDojoButton->m_Alpha = buttonAlpha;
    if (pLeaderboardBtn) pLeaderboardBtn->m_Alpha = buttonAlpha;
    if (pSoundToggle) pSoundToggle->m_Alpha = (uint8_t)(m_Alpha * 255.0f);
    if (pMusicToggle) pMusicToggle->m_Alpha = (uint8_t)(m_Alpha * 255.0f);

    // Logo bounce physics
    UpdateScreenElements(m_CameraTransition, m_Time);
}

// ======================== UpdateScreenElements ========================
// Matches 0x0014ad3c (~60 lines)

void MainScreen::UpdateScreenElements(float cameraTransition, float time) {
    float param1 = cameraTransition;
    if (param1 < -1.0f) param1 = -1.0f;
    float absCam = fabsf(param1);

    // Logo Y positions (in original centered coords)
    m_LogoNinjaPos = Vec3(0.0f, 0.0f, 0.0f);
    m_LogoFruitPos2 = Vec3(0.0f, 0.0f, 0.0f);

    // Bounce physics
    m_BounceVelocity += absCam * 0.5f;
    m_WindowCenter += m_BounceVelocity * absCam * 15.0f;

    // Floor = pos.y + 18 (in original centered coords)
    float floorY = pos.y + 18.0f;
    m_LogoFruitPos.y = floorY;

    m_LogoFruitPos2.y = m_WindowCenter;
    m_LogoNinjaPos.y = m_WindowCenter;

    // Bounce at floor
    if (m_WindowCenter < floorY - 15.0f) {
        m_WindowCenter = floorY - 15.0f;
        m_BounceVelocity *= LOGO_BOUNCE_LOSS;

        if (fabsf(m_BounceVelocity) < LOGO_SETTLE_THRESH && time > 0.5f && absCam > 0.0f) {
            m_BounceVelocity = 0.0f;
            m_GlobalAlphaTarget = 0.0f;
        }
    }

    // Alpha lerp
    m_Alpha += (m_GlobalAlphaTarget - m_Alpha) * ALPHA_LERP_RATE;

    // Final offset: "FRUIT" text = "NINJA" pos + (0, -17, 0) * 2 = (0, -34, 0)
    m_LogoFruitPos2.y = m_LogoNinjaPos.y - 34.0f;
}

// ======================== Draw ========================
// Matches 0x0014d4ec (171 lines)

void MainScreen::Draw(Renderer& r, const Vec3& hudScale, int layerMask) {
    // Skip drawing for certain states
    if (m_State == STATE_CAMERA_FADE || m_State == 0x0d) return;
    if ((m_State == STATE_DOJO_WAIT_A || m_State == STATE_DOJO_WAIT_B ||
         m_State == STATE_DOJO_WAIT_C || m_State == STATE_DOJO_WAIT_D)
        && m_Timer2 == 0.0f) return;

    // 1. Game background
    if (game.bg_tex) {
        glDisable(GL_BLEND);
        r.draw_fullscreen_quad(game.bg_tex);
        glEnable(GL_BLEND);
    }

    // 2. Blurry backing overlay — Colour(0, 0, 0, 0x80)
    if (game.blurry_backing_tex) {
        r.draw_fullscreen_quad(game.blurry_backing_tex, 0.5f);
    }

    // 3. "FRUIT" text logo at m_LogoFruitPos2
    if (game.fruit_text_tex && m_ImgFruitText.width > 0) {
        float tw = (float)m_ImgFruitText.width;
        float th = (float)m_ImgFruitText.height;
        r.draw_sprite(game.fruit_text_tex,
                      m_LogoFruitPos2.x - tw / 2.0f,
                      m_LogoFruitPos2.y - th / 2.0f,
                      tw, th, 0.0f, m_Alpha);
    }

    // 4. "NINJA" text logo at m_LogoNinjaPos
    if (game.ninja_text_tex && m_ImgNinjaText.width > 0) {
        float tw = (float)m_ImgNinjaText.width;
        float th = (float)m_ImgNinjaText.height;
        r.draw_sprite(game.ninja_text_tex,
                      m_LogoNinjaPos.x - tw / 2.0f,
                      m_LogoNinjaPos.y - th / 2.0f,
                      tw, th, 0.0f, m_Alpha);
    }

    // 5. Dojo decoration (slice_fruit.tex) at m_LogoFruitPos
    if (m_TexSliceFruit && m_ImgSliceFruit.width > 0) {
        float tw = (float)m_ImgSliceFruit.width;
        float th = (float)m_ImgSliceFruit.height;
        r.draw_sprite(m_TexSliceFruit,
                      m_LogoFruitPos.x - tw / 2.0f,
                      m_LogoFruitPos.y - th / 2.0f,
                      tw, th, 0.0f, m_Alpha);
    }

    // 6. Buttons drawn by HUD::Draw (registered via game.hud->AddControl)

    // 7. Logo overlay (swipe_fruit_begin.tex)
    if (m_TexCommingSoon && pPlayButton && m_ImgCommingSoon.width > 0) {
        float tw = (float)m_ImgCommingSoon.width * 0.5f;
        float th = (float)m_ImgCommingSoon.height * 0.5f;
        r.draw_sprite(m_TexCommingSoon,
                      0.0f - tw / 2.0f,
                      7.0f - th / 2.0f,
                      tw, th, 0.0f, m_Alpha);
    }
}

// ======================== Touch input (via InputManager) ========================

bool MainScreen::HandleTouchDown(float x, float y) {
    if (m_State != STATE_CREATE_BUTTONS) return false;
    if (pPlayButton && pPlayButton->hit_test(x, y)) { pPlayButton->touch_down(x, y); return true; }
    if (pDojoButton && pDojoButton->hit_test(x, y)) { pDojoButton->touch_down(x, y); return true; }
    if (pLeaderboardBtn && pLeaderboardBtn->hit_test(x, y)) { pLeaderboardBtn->touch_down(x, y); return true; }
    if (pSoundToggle && pSoundToggle->hit_test(x, y)) { pSoundToggle->touch_down(x, y); return true; }
    if (pMusicToggle && pMusicToggle->hit_test(x, y)) { pMusicToggle->touch_down(x, y); return true; }
    return false;
}

void MainScreen::HandleTouchUp(float x, float y) {
    if (pPlayButton) pPlayButton->touch_up(x, y);
    if (pDojoButton) pDojoButton->touch_up(x, y);
    if (pLeaderboardBtn) pLeaderboardBtn->touch_up(x, y);
    if (pSoundToggle) pSoundToggle->touch_up(x, y);
    if (pMusicToggle) pMusicToggle->touch_up(x, y);
}

// ======================== Callbacks ========================
// All match decompiled functions from docs/screens/main.md

void MainScreen::GameModeCallback() {
    // Matches 0x0014b068
    m_State = STATE_MODE_SELECT;
    m_Timer2 = 1.0f;
    pLeaderboardBtn = NULL;  // nulls ptr (doesn't remove from HUD — original behavior)
    printf("MainScreen: GameModeCallback -> state 0x0e\n");
}

void MainScreen::NewGameCallback() {
    // Matches 0x0014c384
    m_State = STATE_GAME_START;
    // TODO: GameSound::SFXPlay("swoosh_sound", 1.0, 1.0)
    printf("MainScreen: NewGameCallback -> state 2\n");
}

void MainScreen::AboutCallback() {
    // Matches 0x0014afc4
    m_State = STATE_DOJO_WAIT_B;
    m_Timer2 = 1.0f;
    pLeaderboardBtn = NULL;
    printf("MainScreen: AboutCallback -> state 4\n");
}

void MainScreen::SoundCallback() {
    // Matches 0x0014af64
    game.soundEnabled = !game.soundEnabled;
    // TODO: SoundManager::SetSFXVolume(game.soundEnabled ? 0.5 : 0.0)
    printf("MainScreen: Sound %s\n", game.soundEnabled ? "ON" : "OFF");
}

void MainScreen::MusicCallback() {
    // Matches 0x0014ac9c — just flips flag, no direct music API call
    game.musicEnabled = !game.musicEnabled;
    printf("MainScreen: Music %s\n", game.musicEnabled ? "ON" : "OFF");
}

void MainScreen::QuitGamesCallback() {
    // Matches 0x0014b1a0
    m_State = STATE_QUIT_WAIT;
    printf("MainScreen: QuitGamesCallback -> state 0x17\n");
}
