#ifndef MAIN_SCREEN_H
#define MAIN_SCREEN_H

#include "screen.h"
#include "menu_button.h"
#include "tex_loader.h"

struct Game;

class MainScreen : public Screen {
public:
    MainScreen(Game& g);
    ~MainScreen();

    void enter() override;
    void update(float dt) override;
    void draw(Renderer& r) override;
    void exit() override;
    void on_touch_down(float x, float y) override;
    void on_touch_up(float x, float y) override;

private:
    Game& game;

    // Size/position (from HUDControl3d base)
    float size_x, size_y, size_z;
    float pos_x, pos_y, pos_z;

    // +0x7c-0x84: original size copies
    float m_OrigSizeX, m_OrigSizeY, m_OrigSizeZ;

    // Button textures (+0x88-0x98)
    GLuint m_TexNewGame;       // newgame.tex
    GLuint m_TexDojoIcon;      // dojo_icon.tex
    GLuint m_TexQuit;          // quit.tex
    GLuint m_TexOpenFeint;     // openfeint.tex
    GLuint m_TexMoreGames;     // more_games.tex
    TexImage m_ImgNewGame, m_ImgDojoIcon, m_ImgQuit;
    TexImage m_ImgOpenFeint, m_ImgMoreGames;

    // Dojo decoration / logo textures
    GLuint m_TexSliceFruit;    // slice_fruit.tex (+0xd8)
    GLuint m_TexCommingSoon;   // comming_soon.tex (+0xb4)
    GLuint m_TexGCAchievements; // gc_achievements.tex (+0x114)
    TexImage m_ImgSliceFruit, m_ImgCommingSoon, m_ImgGCAchievements;

    // Sound/music toggle textures (+0xc4-0xd0)
    GLuint m_TexSoundOn;       // sound.tex
    GLuint m_TexSoundOff;      // sound_cross.tex
    GLuint m_TexMusicOn;       // music.tex
    GLuint m_TexMusicOff;      // music_cross.tex
    TexImage m_ImgSoundOn, m_ImgSoundOff, m_ImgMusicOn, m_ImgMusicOff;

    // TexImage for global textures (dimensions needed for drawing)
    TexImage m_ImgFruitText, m_ImgNinjaText, m_ImgBlurryBacking;

    // Shared fruit atlas texture for 3D fruit in buttons
    GLuint m_FruitAtlasTex;

    // Button pointers (+0x9c-0xb0)
    MenuButton* pPlayButton;       // +0x9c
    MenuButton* pDojoButton;       // +0xa0
    MenuButton* pLeaderboardBtn;   // +0xa4
    MenuButton* pMoreGamesBtn;     // +0xa8
    MenuButton* pSoundToggle;      // +0xac
    MenuButton* pMusicToggle;      // +0xb0

    // Logo positions
    float m_LogoFruitPos_x, m_LogoFruitPos_y;     // +0xdc "FRUIT" floor pos
    float m_LogoNinjaPos_x, m_LogoNinjaPos_y;     // +0xec "NINJA" text pos
    float m_LogoFruitPos2_x, m_LogoFruitPos2_y;   // +0xf8 secondary (bounce)

    // State machine
    float m_Alpha;             // +0xe8 = 1.0
    float m_WindowCenter;      // +0xfc = screenH/2 + 160
    float m_BounceVelocity;    // +0x104
    float m_field108;          // +0x108 accumulator for state 0x13/0x14
    int   m_State;             // +0x10c
    float m_Timer;             // +0x110
    float m_Timer2;            // +0x118

    // Camera transition factor (simulates original camera zoom)
    float m_CameraTransition;

    // Global alpha target for logo settle
    float m_GlobalAlphaTarget;

    // Accumulated time
    float m_Time;

    void UpdateScreenElements(float cameraTransition, float time);
    void DeleteMenuButtons();
    void CreateButtons();

    // Callbacks
    void GameModeCallback();
    void AboutCallback();
    void SoundCallback();
    void MusicCallback();
};

#endif
