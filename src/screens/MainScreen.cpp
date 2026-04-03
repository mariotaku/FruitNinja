#include "MainScreen.h"
#include "Game.h"
#include "DojoScreen.h"
#include <cstdio>
#include <cmath>

MainScreen::MainScreen(Game& g)
    : game(g),
      size_x(480.0f), size_y(138.0f), size_z(1.0f),
      pos_x(0.0f), pos_y((320.0f - 138.0f) * 0.5f), pos_z(0.0f),
      m_OrigSizeX(480.0f), m_OrigSizeY(138.0f), m_OrigSizeZ(1.0f),
      m_TexNewGame(0), m_TexDojoIcon(0), m_TexQuit(0),
      m_TexOpenFeint(0), m_TexMoreGames(0),
      m_TexSliceFruit(0), m_TexCommingSoon(0), m_TexGCAchievements(0),
      m_TexSoundOn(0), m_TexSoundOff(0), m_TexMusicOn(0), m_TexMusicOff(0),
      pPlayButton(NULL), pDojoButton(NULL),
      pLeaderboardBtn(NULL), pMoreGamesBtn(NULL),
      pSoundToggle(NULL), pMusicToggle(NULL),
      m_LogoFruitPos_x(0), m_LogoFruitPos_y(0),
      m_LogoNinjaPos_x(0), m_LogoNinjaPos_y(0),
      m_LogoFruitPos2_x(0), m_LogoFruitPos2_y(0),
      m_Alpha(1.0f),
      m_WindowCenter(160.0f),
      m_BounceVelocity(0.0f),
      m_field108(0.0f),
      m_State(0),
      m_Timer(0.0f),
      m_Timer2(0.0f),
      m_FruitAtlasTex(0),
      m_CameraTransition(1.0f),
      m_GlobalAlphaTarget(1.0f),
      m_Time(0.0f)
{
    // Load fruit atlas texture (shared by all fruit meshes in buttons)
    {
        TexImage atlas_img;
        std::string atlas_path = game.data_dir + "/models/fruit/textures/fruit_atlas.tex";
        if (tex_load(atlas_path, atlas_img)) {
            m_FruitAtlasTex = game.renderer.upload_texture(atlas_img);
        }
    }

    // Load global textures (on Game struct, if not already loaded)
    if (!game.blurry_backing_tex)
        game.blurry_backing_tex = game.load_texture("blurry_backing.tex", m_ImgBlurryBacking);
    if (!game.fruit_text_tex)
        game.fruit_text_tex = game.load_texture("fruit_text.tex", m_ImgFruitText);
    if (!game.ninja_text_tex)
        game.ninja_text_tex = game.load_texture("ninja_text.tex", m_ImgNinjaText);

    // Load dojo decoration texture
    m_TexSliceFruit = game.load_texture("slice_fruit.tex", m_ImgSliceFruit);

    // Load button textures
    m_TexNewGame = game.load_texture("newgame.tex", m_ImgNewGame);
    m_TexDojoIcon = game.load_texture("dojo_icon.tex", m_ImgDojoIcon);
    m_TexQuit = game.load_texture("quit.tex", m_ImgQuit);
    m_TexOpenFeint = game.load_texture("openfeint.tex", m_ImgOpenFeint);
    m_TexMoreGames = game.load_texture("more_games.tex", m_ImgMoreGames);
    m_TexGCAchievements = game.load_texture("gc_achievements.tex", m_ImgGCAchievements);

    // Load logo
    m_TexCommingSoon = game.load_texture("swipe_fruit_begin.tex", m_ImgCommingSoon);

    // Load sound/music toggle textures
    m_TexSoundOn = game.load_texture("sound.tex", m_ImgSoundOn);
    m_TexSoundOff = game.load_texture("sound_cross.tex", m_ImgSoundOff);
    m_TexMusicOn = game.load_texture("music.tex", m_ImgMusicOn);
    m_TexMusicOff = game.load_texture("music_cross.tex", m_ImgMusicOff);
}

MainScreen::~MainScreen() {
    DeleteMenuButtons();
    delete pLeaderboardBtn; pLeaderboardBtn = NULL;
    delete pMoreGamesBtn; pMoreGamesBtn = NULL;
    delete pSoundToggle; pSoundToggle = NULL;
    delete pMusicToggle; pMusicToggle = NULL;

    if (m_FruitAtlasTex) { glDeleteTextures(1, &m_FruitAtlasTex); m_FruitAtlasTex = 0; }
    if (m_TexNewGame) { glDeleteTextures(1, &m_TexNewGame); m_TexNewGame = 0; }
    if (m_TexDojoIcon) { glDeleteTextures(1, &m_TexDojoIcon); m_TexDojoIcon = 0; }
    if (m_TexQuit) { glDeleteTextures(1, &m_TexQuit); m_TexQuit = 0; }
    if (m_TexOpenFeint) { glDeleteTextures(1, &m_TexOpenFeint); m_TexOpenFeint = 0; }
    if (m_TexMoreGames) { glDeleteTextures(1, &m_TexMoreGames); m_TexMoreGames = 0; }
    if (m_TexGCAchievements) { glDeleteTextures(1, &m_TexGCAchievements); m_TexGCAchievements = 0; }
    if (m_TexSliceFruit) { glDeleteTextures(1, &m_TexSliceFruit); m_TexSliceFruit = 0; }
    if (m_TexCommingSoon) { glDeleteTextures(1, &m_TexCommingSoon); m_TexCommingSoon = 0; }
    if (m_TexSoundOn) { glDeleteTextures(1, &m_TexSoundOn); m_TexSoundOn = 0; }
    if (m_TexSoundOff) { glDeleteTextures(1, &m_TexSoundOff); m_TexSoundOff = 0; }
    if (m_TexMusicOn) { glDeleteTextures(1, &m_TexMusicOn); m_TexMusicOn = 0; }
    if (m_TexMusicOff) { glDeleteTextures(1, &m_TexMusicOff); m_TexMusicOff = 0; }
}

void MainScreen::enter() {
    m_State = 0;
    m_Timer = 0.0f;
    m_Timer2 = 0.0f;
    m_CameraTransition = 1.0f;
    m_Alpha = 1.0f;
    m_GlobalAlphaTarget = 1.0f;
    m_BounceVelocity = 0.0f;
    m_WindowCenter = 160.0f;
    m_Time = 0.0f;
    printf("MainScreen: enter\n");
}

// Convert original centered coords to port screen coords (bottom-left origin)
// Original: centered system, x-axis = 480 (long) dimension, y-axis = 320 (short) dimension
// Port: 480x320 landscape, (0,0) = bottom-left
static inline float orig_to_port_x(float orig_x) { return FN_SCREEN_W / 2.0f + orig_x; }
static inline float orig_to_port_y(float orig_y) { return FN_SCREEN_H / 2.0f + orig_y; }

void MainScreen::CreateButtons() {
    // Sound toggle — doc: (216, 135.5), 32x32, fruitType -1 (no fruit)
    if (!pSoundToggle) {
        GLuint tex = game.soundEnabled ? m_TexSoundOn : m_TexSoundOff;
        if (tex) {
            pSoundToggle = new MenuButton();
            pSoundToggle->init(tex, 32.0f, 32.0f,
                               orig_to_port_x(216.0f),
                               orig_to_port_y(135.5f),
                               [this]() { SoundCallback(); });
            pSoundToggle->rotation_speed = 0.0f;  // toggles don't spin
        }
    }

    // Music toggle — doc: (176, 135.5), 32x32, fruitType -1 (no fruit)
    if (!pMusicToggle) {
        GLuint tex = game.musicEnabled ? m_TexMusicOn : m_TexMusicOff;
        if (tex) {
            pMusicToggle = new MenuButton();
            pMusicToggle->init(tex, 32.0f, 32.0f,
                               orig_to_port_x(176.0f),
                               orig_to_port_y(135.5f),
                               [this]() { MusicCallback(); });
            pMusicToggle->rotation_speed = 0.0f;
        }
    }

    // New Game button — doc: (16, -66, -50), fruitType 3 (watermelon)
    if (!pPlayButton && m_TexNewGame) {
        pPlayButton = new MenuButton();
        pPlayButton->init(m_TexNewGame,
                          (float)m_ImgNewGame.width, (float)m_ImgNewGame.height,
                          orig_to_port_x(16.0f),
                          orig_to_port_y(-66.0f),
                          [this]() { GameModeCallback(); });
        if (m_FruitAtlasTex)
            pPlayButton->load_fruit(game, "watermelon", m_FruitAtlasTex);
    }

    // Dojo button — doc: (-144, -65), scale 0.9x, fruitType "mango"
    if (!pDojoButton && m_TexDojoIcon) {
        pDojoButton = new MenuButton();
        pDojoButton->init(m_TexDojoIcon,
                          (float)m_ImgDojoIcon.width * 0.9f,
                          (float)m_ImgDojoIcon.height * 0.9f,
                          orig_to_port_x(-144.0f),
                          orig_to_port_y(-65.0f),
                          [this]() { AboutCallback(); });
        if (m_FruitAtlasTex)
            pDojoButton->load_fruit(game, "mango", m_FruitAtlasTex);
    }

    // Leaderboard button — doc: openfeint.tex, (182, -106), fruitType (GOT ref)
    if (!pLeaderboardBtn && m_TexOpenFeint) {
        pLeaderboardBtn = new MenuButton();
        pLeaderboardBtn->init(m_TexOpenFeint,
                              (float)m_ImgOpenFeint.width, (float)m_ImgOpenFeint.height,
                              orig_to_port_x(182.0f),
                              orig_to_port_y(-106.0f),
                              [this]() {
                                  printf("MainScreen: LeaderboardsCallback (skipped)\n");
                              });
        if (m_FruitAtlasTex)
            pLeaderboardBtn->load_fruit(game, "openfeint", m_FruitAtlasTex);
    }

    // MoreGames button — doc: gc_achievements.tex, (182, -106), fruitType "kiwifruit"
    // Note: same position as leaderboard — original may offset; skip for port
}

void MainScreen::DeleteMenuButtons() {
    // Doc: removes Play, Dojo, and MoreGames — NOT sound/music toggles or leaderboard
    delete pPlayButton; pPlayButton = NULL;
    delete pDojoButton; pDojoButton = NULL;
    delete pMoreGamesBtn; pMoreGamesBtn = NULL;
}

void MainScreen::update(float dt) {
    m_Time += dt;

    switch (m_State) {
    case 0: {
        // State 0: Camera zoom-in from splash
        // Create sound/music toggles + play/dojo buttons
        CreateButtons();

        // Lerp camera toward -1.0 at rate 0.125
        m_CameraTransition += (-1.0f - m_CameraTransition) * 0.125f;
        m_Timer2 += dt;

        // When timer2 > 0.15 AND camera < 0 → state 1
        if (m_Timer2 > 0.15f && m_CameraTransition < 0.0f) {
            m_State = 1;
        }
        break;
    }

    case 1:
        // State 1: Active menu — idle, buttons interactive
        // Create leaderboard/moregames if not yet created
        if (!pLeaderboardBtn) CreateButtons();
        break;

    case 2: {
        // State 2: Direct game start
        if (m_CameraTransition > 0.999f) {
            // Reset wave manager etc. would go here
        }
        m_CameraTransition *= 0.75f;
        if (fabsf(m_CameraTransition) < 0.001f) {
            m_State = 0x11;
        }
        break;
    }

    case 3:
    case 4: {
        // States 3/4: Wait then transition to DojoScreen
        m_Timer2 *= 0.75f;
        if (m_Timer2 < 0.01f) {
            m_Timer2 = 0.0f;
            game.set_screen(new DojoScreen(game));
        }
        break;
    }

    case 8: {
        // State 8: Slide-in return
        m_Timer2 += (1.0f - m_Timer2) * 0.125f;
        if (m_Timer2 > 0.99f) {
            m_Timer += dt;
            if (m_Timer >= 1.5f) {
                m_State = 0;
                m_Timer = 0.15f;
                m_Timer2 = -0.85f;
            }
        }
        break;
    }

    case 0xe:
    case 0xf: {
        // State 0xe/0xf: Mode selection slide-out
        m_Timer2 *= 0.85f;
        if (m_Timer2 < 0.25f) {
            // GameModeScreen not yet implemented
            printf("MainScreen: would create GameModeScreen\n");
            m_State = 1;
            m_Timer2 = 0.0f;
        }
        break;
    }

    case 0x11: {
        // State 0x11: Camera fade after game
        m_CameraTransition *= 0.75f;
        if (fabsf(m_CameraTransition) < 0.001f) {
            m_CameraTransition = 0.0f;
        }
        break;
    }

    case 0x13:
    case 0x14: {
        // States 0x13/0x14: Timer accumulate
        m_field108 += dt * 8.0f;
        if (m_field108 >= 8.0f) {
            m_field108 = 0.0f;
            m_State = 0;
        }
        break;
    }

    case 0x17: {
        // State 0x17: Quit
        game.running = false;
        break;
    }

    default:
        break;
    }

    // Update logo bounce physics
    UpdateScreenElements(m_CameraTransition, m_Time);

    // Update sound/music toggle textures
    if (pSoundToggle) {
        pSoundToggle->texture = game.soundEnabled ? m_TexSoundOn : m_TexSoundOff;
    }
    if (pMusicToggle) {
        pMusicToggle->texture = game.musicEnabled ? m_TexMusicOn : m_TexMusicOff;
    }

    // Toggle button positions — doc: y=135.5, x=216/176 (top-right corner)
    if (pSoundToggle && pMusicToggle) {
        float toggle_y = orig_to_port_y(135.5f);
        pSoundToggle->y = toggle_y - pSoundToggle->height / 2.0f;
        pMusicToggle->y = toggle_y - pMusicToggle->height / 2.0f;
        pSoundToggle->x = orig_to_port_x(216.0f) - pSoundToggle->width / 2.0f;
        pMusicToggle->x = orig_to_port_x(176.0f) - pMusicToggle->width / 2.0f;
    }

    // Update button alpha based on camera transition
    float buttonAlpha = m_Alpha;
    if (m_State == 0) {
        buttonAlpha = (m_Timer2 > 0.15f) ? m_Alpha : 0.0f;
    }
    if (pPlayButton) pPlayButton->alpha = buttonAlpha;
    if (pDojoButton) pDojoButton->alpha = buttonAlpha;
    if (pLeaderboardBtn) pLeaderboardBtn->alpha = buttonAlpha;
    if (pMoreGamesBtn) pMoreGamesBtn->alpha = buttonAlpha;
    if (pSoundToggle) pSoundToggle->alpha = m_Alpha;
    if (pMusicToggle) pMusicToggle->alpha = m_Alpha;

    // Update button animations (ring rotation + fruit spin)
    if (pPlayButton) pPlayButton->update(dt);
    if (pDojoButton) pDojoButton->update(dt);
    if (pLeaderboardBtn) pLeaderboardBtn->update(dt);
    if (pMoreGamesBtn) pMoreGamesBtn->update(dt);
    if (pSoundToggle) pSoundToggle->update(dt);
    if (pMusicToggle) pMusicToggle->update(dt);
}

void MainScreen::UpdateScreenElements(float cameraTransition, float time) {
    float param1 = cameraTransition;
    if (param1 < -1.0f) param1 = -1.0f;

    float absCam = fabsf(param1);

    // Logo X positions (centered horizontally, in original coords = 0)
    m_LogoNinjaPos_x = 0.0f;
    m_LogoFruitPos2_x = 0.0f;

    // Bounce physics (all in original centered coords)
    m_BounceVelocity += absCam * 0.5f;
    m_WindowCenter += m_BounceVelocity * absCam * 15.0f;

    // Floor position — pos_y is in original coords
    float floorY = pos_y + 18.0f;
    m_LogoFruitPos_y = floorY;

    m_LogoFruitPos2_y = m_WindowCenter;
    m_LogoNinjaPos_y = m_WindowCenter;

    // Bounce at floor
    if (m_WindowCenter < floorY - 15.0f) {
        m_WindowCenter = floorY - 15.0f;
        m_BounceVelocity *= -0.25f;  // bounce with energy loss

        if (fabsf(m_BounceVelocity) < 3.0f && time > 0.5f && absCam > 0.0f) {
            m_BounceVelocity = 0.0f;  // settle
            m_GlobalAlphaTarget = 0.0f;
        }
    }

    // Alpha lerp
    m_Alpha += (m_GlobalAlphaTarget - m_Alpha) * 0.25f;

    // Final logo offset: "FRUIT" text positioned above "NINJA" (doc: += (0, -17, 0) * 2)
    m_LogoFruitPos2_y = m_LogoNinjaPos_y - 34.0f;
}

void MainScreen::draw(Renderer& r) {
    // Skip drawing for certain states
    if (m_State == 0x11 || m_State == 0x0d) return;
    if ((m_State == 3 || m_State == 4 || m_State == 0x15 || m_State == 0x16)
        && m_Timer2 == 0.0f) return;

    // 1. Background (game bg)
    if (game.bg_tex) {
        glDisable(GL_BLEND);
        r.draw_fullscreen_quad(game.bg_tex);
        glEnable(GL_BLEND);
    }

    // 2. Semi-transparent black overlay (blurry_backing.tex)
    // Doc: Colour(0, 0, 0, 0x80) — semi-transparent black
    if (game.blurry_backing_tex) {
        r.draw_fullscreen_quad(game.blurry_backing_tex, 0.5f);
    }

    // 3. "FRUIT" text logo (fruit_text.tex) — at m_LogoFruitPos2
    if (game.fruit_text_tex && m_ImgFruitText.width > 0) {
        float tw = (float)m_ImgFruitText.width;
        float th = (float)m_ImgFruitText.height;
        r.draw_sprite(game.fruit_text_tex,
                      orig_to_port_x(m_LogoFruitPos2_x) - tw / 2.0f,
                      orig_to_port_y(m_LogoFruitPos2_y) - th / 2.0f,
                      tw, th, 0.0f, m_Alpha);
    }

    // 4. "NINJA" text logo (ninja_text.tex) — at m_LogoNinjaPos
    if (game.ninja_text_tex && m_ImgNinjaText.width > 0) {
        float tw = (float)m_ImgNinjaText.width;
        float th = (float)m_ImgNinjaText.height;
        r.draw_sprite(game.ninja_text_tex,
                      orig_to_port_x(m_LogoNinjaPos_x) - tw / 2.0f,
                      orig_to_port_y(m_LogoNinjaPos_y) - th / 2.0f,
                      tw, th, 0.0f, m_Alpha);
    }

    // 5. Dojo decoration (slice_fruit.tex) — behind buttons, at m_LogoFruitPos
    if (m_TexSliceFruit && m_ImgSliceFruit.width > 0) {
        float tw = (float)m_ImgSliceFruit.width;
        float th = (float)m_ImgSliceFruit.height;
        r.draw_sprite(m_TexSliceFruit,
                      orig_to_port_x(m_LogoFruitPos_x) - tw / 2.0f,
                      orig_to_port_y(m_LogoFruitPos_y) - th / 2.0f,
                      tw, th, 0.0f, m_Alpha);
    }

    // 6. Draw buttons
    if (pPlayButton) pPlayButton->draw(r);
    if (pDojoButton) pDojoButton->draw(r);
    if (pLeaderboardBtn) pLeaderboardBtn->draw(r);
    if (pMoreGamesBtn) pMoreGamesBtn->draw(r);
    if (pSoundToggle) pSoundToggle->draw(r);
    if (pMusicToggle) pMusicToggle->draw(r);

    // 7. Logo overlay (comming_soon.tex / "SLICE FRUIT TO BEGIN") — on top
    // Doc: scale (0.5, aspect*0.5, 1), translate to (DAT, 7.0, 0)
    if (m_TexCommingSoon && pPlayButton && m_ImgCommingSoon.width > 0) {
        float tw = (float)m_ImgCommingSoon.width * 0.5f;
        float th = (float)m_ImgCommingSoon.height * 0.5f;
        r.draw_sprite(m_TexCommingSoon,
                      orig_to_port_x(0.0f) - tw / 2.0f,
                      orig_to_port_y(7.0f) - th / 2.0f,
                      tw, th, 0.0f, m_Alpha);
    }
}

void MainScreen::exit() {
    printf("MainScreen: exit\n");
}

void MainScreen::on_touch_down(float x, float y) {
    if (m_State != 1) return;

    if (pPlayButton && pPlayButton->hit_test(x, y)) {
        pPlayButton->touch_down(x, y);
        return;
    }
    if (pDojoButton && pDojoButton->hit_test(x, y)) {
        pDojoButton->touch_down(x, y);
        return;
    }
    if (pLeaderboardBtn && pLeaderboardBtn->hit_test(x, y)) {
        pLeaderboardBtn->touch_down(x, y);
        return;
    }
    if (pSoundToggle && pSoundToggle->hit_test(x, y)) {
        pSoundToggle->touch_down(x, y);
        return;
    }
    if (pMusicToggle && pMusicToggle->hit_test(x, y)) {
        pMusicToggle->touch_down(x, y);
        return;
    }
}

void MainScreen::on_touch_up(float x, float y) {
    if (pPlayButton) pPlayButton->touch_up(x, y);
    if (pDojoButton) pDojoButton->touch_up(x, y);
    if (pLeaderboardBtn) pLeaderboardBtn->touch_up(x, y);
    if (pSoundToggle) pSoundToggle->touch_up(x, y);
    if (pMusicToggle) pMusicToggle->touch_up(x, y);
}

// --- Callbacks (faithful to decompiled code) ---

void MainScreen::GameModeCallback() {
    m_State = 0x0e;
    m_Timer2 = 1.0f;
    pLeaderboardBtn = NULL;  // doc: nulls leaderboard ptr
    printf("MainScreen: GameModeCallback -> state 0xe\n");
}

void MainScreen::AboutCallback() {
    m_State = 4;
    m_Timer2 = 1.0f;
    pLeaderboardBtn = NULL;  // doc: nulls leaderboard ptr
    printf("MainScreen: AboutCallback -> state 4 (-> DojoScreen)\n");
}

void MainScreen::SoundCallback() {
    game.soundEnabled = !game.soundEnabled;
    printf("MainScreen: SoundCallback -> %s\n", game.soundEnabled ? "on" : "off");
}

void MainScreen::MusicCallback() {
    game.musicEnabled = !game.musicEnabled;
    printf("MainScreen: MusicCallback -> %s\n", game.musicEnabled ? "on" : "off");
}
