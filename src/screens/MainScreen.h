#ifndef FN_MAIN_SCREEN_H
#define FN_MAIN_SCREEN_H

#include "HUDControl3d.h"
#include "Screen.h"
#include "tex_loader.h"
#include "Vec3.h"

struct Game;
class MenuButton;

// Matches original MainScreen : HUDControl3d (size = 0x120)
// State machine with 25 states, controls the main menu / dojo screen.
//
// Note: also implements Screen interface for the port's game loop.
// In the original, MainScreen is purely a HUDControl added to the HUD.
class MainScreen : public Screen {
public:
    MainScreen(Game& g);
    ~MainScreen();

    // Screen interface
    void enter() override;
    void update(float dt) override;
    void draw(Renderer& r) override;
    void exit() override;
    void on_touch_down(float x, float y) override;
    void on_touch_up(float x, float y) override;

private:
    Game& game;

    // === Matches struct layout from docs/screens/main.md ===

    // HUDControl3d base: pos, size (inherited concept, stored here for port)
    Vec3 m_Size;     // (480.0, 138.0, 1.0)
    Vec3 m_Pos;      // (0.0, 91.0, 0.0)

    // +0x7c: original size copies
    Vec3 m_OrigSize;

    // Button textures (+0x88..+0x98)
    GLuint m_TexNewGame;         // +0x88
    GLuint m_TexDojoIcon;        // +0x8c
    GLuint m_TexQuit;            // +0x90
    GLuint m_TexOpenFeint;       // +0x94
    GLuint m_TexMoreGames;       // +0x98
    TexImage m_ImgNewGame, m_ImgDojoIcon;

    // Button pointers (+0x9c..+0xb0) — owned by HUD after AddControl
    MenuButton* pPlayButton;     // +0x9c
    MenuButton* pDojoButton;     // +0xa0
    MenuButton* pLeaderboardBtn; // +0xa4
    MenuButton* pMoreGamesBtn;   // +0xa8
    MenuButton* pSoundToggle;    // +0xac
    MenuButton* pMusicToggle;    // +0xb0

    // +0xb4: logo overlay texture
    GLuint m_TexCommingSoon;
    TexImage m_ImgCommingSoon;

    // Toggle textures (+0xc4..+0xd0)
    GLuint m_TexSoundOn, m_TexSoundOff;
    GLuint m_TexMusicOn, m_TexMusicOff;

    // +0xd8: dojo decoration
    GLuint m_TexSliceFruit;
    TexImage m_ImgSliceFruit;

    // Global textures (not on struct — shared via Game)
    TexImage m_ImgFruitText, m_ImgNinjaText, m_ImgBlurryBacking;

    // +0xdc: logo positions (set by UpdateScreenElements)
    Vec3 m_LogoFruitPos;       // "FRUIT" floor position
    // +0xe8
    float m_Alpha;             // lerps toward m_GlobalAlphaTarget
    // +0xec
    Vec3 m_LogoNinjaPos;       // "NINJA" text position
    // +0xf8
    Vec3 m_LogoFruitPos2;      // "FRUIT" text bounce position

    // +0xfc
    float m_WindowCenter;      // bounce accumulator
    // +0x104
    float m_BounceVelocity;
    // +0x108
    float m_field108;          // loading accumulator

    // +0x10c: state machine
    int m_State;

    // +0x110
    float m_Timer;
    // +0x118
    float m_Timer2;

    // +0x114
    GLuint m_TexGCAchievements;

    // Shared fruit atlas for button meshes
    GLuint m_FruitAtlasTex;

    // Port-specific: camera transition (simulates Game+0x0c in original)
    float m_CameraTransition;

    // Port-specific: global alpha target (driven by logo settle)
    float m_GlobalAlphaTarget;

    // Port-specific: accumulated time
    float m_Time;

    // === State enum (matches binary) ===
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

    // === Methods (match original function names) ===
    void UpdateScreenElements(float cameraTransition, float time);
    void DeleteMenuButtons();
    void Hide();

    // Button creation helpers
    void CreateToggles();
    void CreatePlayDojo();
    void CreateLeaderboard();

    // Callbacks (match decompiled functions)
    void GameModeCallback();
    void NewGameCallback();
    void AboutCallback();
    void SoundCallback();
    void MusicCallback();
    void QuitGamesCallback();

    // Helper: remove button from HUD safely
    void RemoveButton(MenuButton*& btn);
};

#endif
