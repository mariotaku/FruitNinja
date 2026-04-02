#include "dojo_screen.h"
#include "game.h"
#include <cstdio>

DojoScreen::DojoScreen(Game& g)
    : game(g), state(TRANSITION_IN), alpha(0.0f),
      play_button(NULL), shop_button(NULL), about_button(NULL),
      play_tex(0), shop_tex(0), about_tex(0),
      buttons_created(false) {}

DojoScreen::~DojoScreen() {
    destroy_buttons();
}

void DojoScreen::enter() {
    state = TRANSITION_IN;
    alpha = 0.0f;
    printf("DojoScreen: enter\n");
}

void DojoScreen::create_buttons() {
    if (buttons_created) return;

    // Load button textures
    play_tex = game.load_texture("play_button.tex", play_img);
    shop_tex = game.load_texture("dojo_icon.tex", shop_img);
    about_tex = game.load_texture("about.tex", about_img);

    // Play button — center of screen, slightly above middle
    if (play_tex) {
        play_button = new MenuButton();
        play_button->init(play_tex, (float)play_img.width, (float)play_img.height,
                          FN_SCREEN_W / 2.0f, FN_SCREEN_H * 0.45f,
                          [this]() { on_play(); });
    }

    // Shop button — left of center, below play
    if (shop_tex) {
        shop_button = new MenuButton();
        shop_button->init(shop_tex, (float)shop_img.width, (float)shop_img.height,
                          FN_SCREEN_W * 0.35f, FN_SCREEN_H * 0.25f,
                          [this]() { on_shop(); });
    }

    // About button — right of center, below play
    if (about_tex) {
        about_button = new MenuButton();
        about_button->init(about_tex, (float)about_img.width, (float)about_img.height,
                           FN_SCREEN_W * 0.65f, FN_SCREEN_H * 0.25f,
                           [this]() { on_about(); });
    }

    buttons_created = true;
    printf("DojoScreen: buttons created\n");
}

void DojoScreen::destroy_buttons() {
    delete play_button;  play_button = NULL;
    delete shop_button;  shop_button = NULL;
    delete about_button; about_button = NULL;

    if (play_tex) { glDeleteTextures(1, &play_tex); play_tex = 0; }
    if (shop_tex) { glDeleteTextures(1, &shop_tex); shop_tex = 0; }
    if (about_tex) { glDeleteTextures(1, &about_tex); about_tex = 0; }

    buttons_created = false;
}

void DojoScreen::update(float dt) {
    (void)dt;

    switch (state) {
    case TRANSITION_IN:
        // Lerp alpha toward 1.0 (original: alpha += (1.0 - alpha) * 0.25)
        alpha += (1.0f - alpha) * 0.25f;

        // Lazily create buttons during transition
        if (alpha > 0.5f && !buttons_created) {
            create_buttons();
        }

        if (alpha > 0.99f) {
            alpha = 1.0f;
            state = IDLE;
        }
        break;

    case IDLE:
        // Buttons are interactive, nothing else to do
        break;

    case TRANSITION_OUT_SHOP:
    case TRANSITION_OUT_ABOUT:
        // Fade out (original: alpha *= 0.75)
        alpha *= 0.75f;
        if (alpha < 0.01f) {
            alpha = 0.0f;
            if (state == TRANSITION_OUT_SHOP) {
                printf("DojoScreen: would transition to ShopScreen (stub)\n");
            } else {
                printf("DojoScreen: would transition to AboutScreen (stub)\n");
            }
            // Return to idle for now (stub — no ShopScreen/AboutScreen yet)
            alpha = 0.0f;
            state = TRANSITION_IN;
        }
        break;

    case FADE_OUT_GAME:
        // Fade out for game start (original: alpha *= 0.75, then GameState = 8)
        alpha *= 0.75f;
        if (alpha < 0.01f) {
            printf("DojoScreen: would start game (stub)\n");
            // Return to idle for now (stub)
            alpha = 0.0f;
            state = TRANSITION_IN;
        }
        break;
    }

    // Update button alphas to match screen alpha
    if (play_button) play_button->alpha = alpha;
    if (shop_button) shop_button->alpha = alpha;
    if (about_button) about_button->alpha = alpha;
}

void DojoScreen::draw(Renderer& r) {
    // Background (always full alpha)
    if (game.bg_tex) {
        glDisable(GL_BLEND);
        r.draw_fullscreen_quad(game.bg_tex);
        glEnable(GL_BLEND);
    }

    // Title/logo at top
    if (game.title_tex) {
        float tw = 200.0f;
        float th = 200.0f;
        r.draw_sprite(game.title_tex,
                      (FN_SCREEN_W - tw) / 2.0f,
                      FN_SCREEN_H - th - 20.0f,
                      tw, th, 0.0f, alpha);
    }

    // Draw buttons
    if (play_button) play_button->draw(r);
    if (shop_button) shop_button->draw(r);
    if (about_button) about_button->draw(r);
}

void DojoScreen::exit() {
    printf("DojoScreen: exit\n");
}

void DojoScreen::on_touch_down(float x, float y) {
    if (state != IDLE) return;

    // Singular hit: stop at first button hit
    if (play_button && play_button->hit_test(x, y)) {
        play_button->touch_down(x, y);
        return;
    }
    if (shop_button && shop_button->hit_test(x, y)) {
        shop_button->touch_down(x, y);
        return;
    }
    if (about_button && about_button->hit_test(x, y)) {
        about_button->touch_down(x, y);
        return;
    }
}

void DojoScreen::on_touch_up(float x, float y) {
    if (play_button) play_button->touch_up(x, y);
    if (shop_button) shop_button->touch_up(x, y);
    if (about_button) about_button->touch_up(x, y);
}

void DojoScreen::on_play() {
    printf("DojoScreen: Play pressed\n");
    state = FADE_OUT_GAME;
}

void DojoScreen::on_shop() {
    printf("DojoScreen: Shop pressed\n");
    state = TRANSITION_OUT_SHOP;
}

void DojoScreen::on_about() {
    printf("DojoScreen: About pressed\n");
    state = TRANSITION_OUT_ABOUT;
}
