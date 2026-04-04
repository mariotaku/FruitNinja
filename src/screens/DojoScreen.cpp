#include "DojoScreen.h"
#include "Game.h"
#include "math/math3d.h"
#include <cstdio>
#include <cmath>

static const char* fruit_models[DojoScreen::NUM_FRUITS] = {
    "models/fruit/apple_single.mmd",
    "models/fruit/orange_single.mmd",
    "models/fruit/watermelon_single.mmd",
};

DojoScreen::DojoScreen(Game& g)
    : game(g), state(TRANSITION_IN), alpha(0.0f),
      play_button(NULL), shop_button(NULL), about_button(NULL),
      play_tex(0), shop_tex(0), about_tex(0),
      buttons_created(false),
      fruit_atlas_tex(0), ring_angle(0.0f) {
    for (int i = 0; i < NUM_FRUITS; i++)
        fruit_rotations[i] = 0.0f;
}

DojoScreen::~DojoScreen() {
    destroy_buttons();
    for (int i = 0; i < NUM_FRUITS; i++)
        fruit_meshes[i].destroy();
    if (fruit_atlas_tex) {
        glDeleteTextures(1, &fruit_atlas_tex);
        fruit_atlas_tex = 0;
    }
}

void DojoScreen::enter() {
    state = TRANSITION_IN;
    alpha = 0.0f;
    load_fruit_meshes();
    printf("DojoScreen: enter\n");
}

void DojoScreen::load_fruit_meshes() {
    // Load fruit atlas texture
    if (!fruit_atlas_tex) {
        TexImage atlas_img;
        std::string path = game.data_dir + "/models/fruit/textures/fruit_atlas.tex";
        if (tex_load(path, atlas_img)) {
            fruit_atlas_tex = game.renderer.upload_texture(atlas_img);
            printf("Loaded fruit atlas: %dx%d\n", atlas_img.width, atlas_img.height);
        } else {
            fprintf(stderr, "Failed to load fruit atlas from: %s\n", path.c_str());
        }
    }

    // Load fruit meshes
    for (int i = 0; i < NUM_FRUITS; i++) {
        if (fruit_meshes[i].vbo) continue; // already loaded
        std::string path = game.data_dir + "/" + fruit_models[i];
        if (!fruit_meshes[i].load(path)) {
            fprintf(stderr, "Failed to load fruit mesh: %s\n", fruit_models[i]);
        }
    }
}

void DojoScreen::create_buttons() {
    if (buttons_created) return;

    play_tex = game.load_texture("play_button.tex", play_img);
    shop_tex = game.load_texture("dojo_icon.tex", shop_img);
    about_tex = game.load_texture("about.tex", about_img);

    if (play_tex) {
        play_button = new MenuButton();
        play_button->init(play_tex, (float)play_img.width, (float)play_img.height,
                          FN_SCREEN_W / 2.0f, FN_SCREEN_H * 0.45f,
                          [this]() { on_play(); });
    }

    if (shop_tex) {
        shop_button = new MenuButton();
        shop_button->init(shop_tex, (float)shop_img.width, (float)shop_img.height,
                          FN_SCREEN_W * 0.35f, FN_SCREEN_H * 0.25f,
                          [this]() { on_shop(); });
    }

    if (about_tex) {
        about_button = new MenuButton();
        about_button->init(about_tex, (float)about_img.width, (float)about_img.height,
                           FN_SCREEN_W * 0.65f, FN_SCREEN_H * 0.25f,
                           [this]() { on_about(); });
    }

    buttons_created = true;
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
    // Update fruit rotations
    ring_angle += dt * 0.3f; // slow ring orbit
    for (int i = 0; i < NUM_FRUITS; i++) {
        fruit_rotations[i] += dt * (2.0f + i * 0.5f); // each fruit spins at different speed
    }

    switch (state) {
    case TRANSITION_IN:
        alpha += (1.0f - alpha) * 0.25f;
        if (alpha > 0.5f && !buttons_created)
            create_buttons();
        if (alpha > 0.99f) {
            alpha = 1.0f;
            state = IDLE;
        }
        break;

    case IDLE:
        break;

    case TRANSITION_OUT_SHOP:
    case TRANSITION_OUT_ABOUT:
        alpha *= 0.75f;
        if (alpha < 0.01f) {
            alpha = 0.0f;
            state = TRANSITION_IN;
        }
        break;

    case FADE_OUT_GAME:
        alpha *= 0.75f;
        if (alpha < 0.01f) {
            alpha = 0.0f;
            state = TRANSITION_IN;
        }
        break;
    }

    if (play_button) play_button->m_Alpha = alpha;
    if (shop_button) shop_button->m_Alpha = alpha;
    if (about_button) about_button->m_Alpha = alpha;
}

void DojoScreen::draw(Renderer& r) {
    // Background
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
    if (play_button) play_button->Draw(r, Vec3(1,1,1), 0xFFFF);
    if (shop_button) shop_button->Draw(r, Vec3(1,1,1), 0xFFFF);
    if (about_button) about_button->Draw(r, Vec3(1,1,1), 0xFFFF);

    // Draw 3D fruit ring — original uses orthographic projection
    if (fruit_atlas_tex) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);

        // Ortho projection matching original game coords
        // Screen is 480x320, center at (0,0), so bounds are [-240,240] x [-160,160]
        float hw = FN_SCREEN_W / 2.0f;  // 240
        float hh = FN_SCREEN_H / 2.0f;  // 160
        float proj[16];
        memset(proj, 0, sizeof(proj));
        proj[0]  = 1.0f / hw;           // 2/(right-left) with centered ortho
        proj[5]  = 1.0f / hh;
        proj[10] = -2.0f / 200.0f;      // near=-100, far=100
        proj[15] = 1.0f;

        // View: look from front (identity — camera at z looking toward -z)
        float view[16];
        mat4_identity(view);

        float pv[16];
        mat4_multiply(pv, proj, view);

        float ring_radius = 80.0f;
        float fruit_scale = 1.5f;

        for (int i = 0; i < NUM_FRUITS; i++) {
            if (!fruit_meshes[i].vbo) continue;

            float angle = ring_angle + i * (6.28318f / NUM_FRUITS);
            float fx = cosf(angle) * ring_radius;
            float fy = sinf(angle) * ring_radius * 0.3f - 30.0f; // elliptical, lower on screen

            float scl[16], rot[16], sr[16], trans[16], model[16], mvp[16];
            mat4_scale(scl, fruit_scale, fruit_scale, fruit_scale);
            mat4_rotate_y(rot, fruit_rotations[i]);
            mat4_multiply(sr, rot, scl);
            mat4_translate(trans, fx, fy, 0.0f);
            mat4_multiply(model, trans, sr);
            mat4_multiply(mvp, pv, model);

            r.draw_mesh(fruit_meshes[i], fruit_atlas_tex, mvp, model, alpha);
        }

        glDisable(GL_DEPTH_TEST);
    }
}

void DojoScreen::exit() {
    printf("DojoScreen: exit\n");
}

void DojoScreen::on_touch_down(float x, float y) {
    if (state != IDLE) return;

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
