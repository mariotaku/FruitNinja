#ifndef FN_GAME_OVER_SCREEN_H
#define FN_GAME_OVER_SCREEN_H

// GameOverScreen : HUDControl3d (size = 0x13C)
// Binary: ctor 0x00141218, Update 0x00141960, Draw 0x00141da4
// State machine: 0 -> 6 (main display) -> 7/8 (retry/quit cleanup) -> done
//
// Analysed: 2026-04-30T12:00

#include "hud/HUDControl3d.h"
#include <cstring>

class MenuButton;

class GameOverScreen : public HUDControl3d {
public:
    // +0x7C: state machine index
    int  m_State;
    // +0x80: transition/entry timer
    float m_Timer;
    // +0x84: endReason passed to ctor (-1 = time/miss, other = bomb reason code)
    int  m_EndReason;
    // +0x88: score at game-over
    float m_EndScore;
    // +0x8C: expressionIdx (facial expression on result screen)
    int  m_ExpressionIdx;
    // +0x90: bgPatternIdx (background pattern index)
    int  m_BgPatternIdx;
    // +0x94: pomCount (pom-pom decoration count)
    int  m_PomCount;
    // +0x98: starCount (star decoration count)
    int  m_StarCount;
    // +0x9C: mode name (e.g. "GameOver")
    char m_ModeName[32];   // +0x9C..+0xBB
    // +0xBC: retry button pointer (created in state 6)
    MenuButton* m_pRetryBtn;
    // +0xC0: quit button pointer
    MenuButton* m_pQuitBtn;

    // Binary: LoadContent 0x1305cc
    static void LoadContent() {}
    // Binary: UnLoadContent 0x12efd8
    static void UnLoadContent() {}

    // ctor: 0x00141218 — size 0x13C
    GameOverScreen(const char* modeName, int startState, float startTimer,
                   int expressionIdx, int bgPatternIdx, int pomCount, int starCount);
    ~GameOverScreen() override {}

    // HUDControl vtable overrides
    void Init() override;
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override;
    void Reset() override;
    int  GetType() override { return 5; }

private:
    void CreateButtons();
};

#endif
