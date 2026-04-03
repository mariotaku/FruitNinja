#ifndef SPLASH_SCREEN_H
#define SPLASH_SCREEN_H

#include "Screen.h"

struct Game;

class SplashScreen : public Screen {
    Game& game;
    int frame_count;

public:
    SplashScreen(Game& g);
    void enter() override;
    void update(float dt) override;
    void draw(Renderer& r) override;
    void exit() override;
};

#endif
