#include "SplashScreen.h"
#include "Game.h"
#include "MainScreen.h"
#include <cstdio>

static const int SPLASH_FRAMES = 60; // ~1 second at 60fps

SplashScreen::SplashScreen(Game& g) : game(g), frame_count(0) {}

void SplashScreen::enter() {
    frame_count = 0;
    printf("SplashScreen: enter\n");
}

void SplashScreen::update(float dt) {
    (void)dt;
    frame_count++;
    if (frame_count >= SPLASH_FRAMES) {
        game.set_screen(new MainScreen(game));
    }
}

void SplashScreen::draw(Renderer& r) {
    // hb_logo.tex is a full splash screen image — draw it fullscreen
    if (game.hb_logo_tex) {
        glDisable(GL_BLEND);
        r.draw_fullscreen_quad(game.hb_logo_tex);
        glEnable(GL_BLEND);
    }
}

void SplashScreen::exit() {
    printf("SplashScreen: exit\n");
}
