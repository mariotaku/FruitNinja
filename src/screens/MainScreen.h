#ifndef FN_MAIN_SCREEN_H
#define FN_MAIN_SCREEN_H

//
// MainScreen : HUDControl3d (size = 0x120)
// Reimplemented from docs/screens/main.md
// Original: ctor 0x0014c430, Update 0x0014b278 (677 lines), Draw 0x0014d4ec (171 lines)
//
// MainScreen is a HUDControl added to the game's HUD. It is NOT a separate
// "Screen" — the HUD system calls Update/Draw on it like any other control.
//

#include "HUDControl3d.h"
#include "tex_loader.h"
#include "Vec3.h"

struct Game;
class MenuButton;

class MainScreen : public HUDControl3d {
public:
    MainScreen(Game& g);
    ~MainScreen();

    // HUDControl overrides
    void Update(float dt) override;
    void Draw(Renderer& r, const Vec3& hudScale, int layerMask) override;
    void OnTouchDown(float x, float y) override;
    void OnTouchUp(float x, float y) override;

private:
    Game& game;

    // +0x7c: original size copies
    Vec3 m_OrigSize;

    // Button textures (+0x88..+0x98)
    GLuint m_TexNewGame;
    GLuint m_TexDojoIcon;
    GLuint m_TexQuit;
    GLuint m_TexOpenFeint;
    GLuint m_TexMoreGames;
    TexImage m_ImgNewGame, m_ImgDojoIcon;

    // Button pointers (+0x9c..+0xb0) — owned by HUD after AddControl
    MenuButton* pPlayButton;
    MenuButton* pDojoButton;
    MenuButton* pLeaderboardBtn;
    MenuButton* pMoreGamesBtn;
    MenuButton* pSoundToggle;
    MenuButton* pMusicToggle;

    // +0xb4: logo overlay
    GLuint m_TexCommingSoon;
    TexImage m_ImgCommingSoon;

    // Toggle textures (+0xc4..+0xd0)
    GLuint m_TexSoundOn, m_TexSoundOff;
    GLuint m_TexMusicOn, m_TexMusicOff;

    // +0xd8: dojo decoration
    GLuint m_TexSliceFruit;
    TexImage m_ImgSliceFruit;

    // Global texture dimensions (for drawing)
    TexImage m_ImgFruitText, m_ImgNinjaText, m_ImgBlurryBacking;

    // +0xdc, +0xec, +0xf8: logo positions
    Vec3 m_LogoFruitPos;
    Vec3 m_LogoNinjaPos;
    Vec3 m_LogoFruitPos2;

    // +0xe8
    float m_Alpha;
    // +0xfc
    float m_WindowCenter;
    // +0x104
    float m_BounceVelocity;
    // +0x108
    float m_field108;
    // +0x10c
    int m_State;
    // +0x110
    float m_Timer;
    // +0x118
    float m_Timer2;
    // +0x114
    GLuint m_TexGCAchievements;

    GLuint m_FruitAtlasTex;

    // Port-specific
    float m_CameraTransition;
    float m_GlobalAlphaTarget;
    float m_Time;

    enum State {
        STATE_CAMERA_ZOOM    = 0,
        STATE_CREATE_BUTTONS = 1,
        STATE_GAME_START     = 2,
        STATE_DOJO_WAIT_A    = 3,
        STATE_DOJO_WAIT_B    = 4,
        STATE_SLIDE_IN       = 8,
        STATE_LEADERBOARD    = 9,
        STATE_MORE_GAMES     = 10,
        STATE_NEWS           = 0x0b,
        STATE_MODE_SELECT    = 0x0e,
        STATE_MODE_SELECT_2  = 0x0f,
        STATE_MATCHMAKER     = 0x10,
        STATE_CAMERA_FADE    = 0x11,
        STATE_LOADING_A      = 0x13,
        STATE_LOADING_B      = 0x14,
        STATE_DOJO_WAIT_C    = 0x15,
        STATE_DOJO_WAIT_D    = 0x16,
        STATE_QUIT_WAIT      = 0x17,
        STATE_QUIT_BOMB      = 0x18,
    };

    void UpdateScreenElements(float cameraTransition, float time);
    void DeleteMenuButtons();
    void Hide();
    void CreateToggles();
    void CreatePlayDojo();
    void CreateLeaderboard();

    void GameModeCallback();
    void NewGameCallback();
    void AboutCallback();
    void SoundCallback();
    void MusicCallback();
    void QuitGamesCallback();

    void RemoveButton(MenuButton*& btn);
};

#endif
