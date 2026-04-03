#include "MenuButton.h"
#include "Renderer.h"
#include "Game.h"
#include "math3d.h"
#include <cstdio>
#include <cmath>

MenuButton::MenuButton()
    : texture(0), x(0), y(0), width(0), height(0),
      alpha(1.0f), pressed(false), visible(true),
      rotation(0.0f), rotation_speed(0.5f),
      fruit_atlas_tex(0), fruit_rotation(0.0f), has_fruit(false) {}

MenuButton::~MenuButton() {
    fruit_mesh.destroy();
    // fruit_atlas_tex is shared — NOT deleted here
}

void MenuButton::init(GLuint tex, float tex_w, float tex_h, float cx, float cy,
                      std::function<void()> callback) {
    texture = tex;
    width = tex_w;
    height = tex_h;
    x = cx - width / 2.0f;
    y = cy - height / 2.0f;
    on_click = callback;
}

void MenuButton::load_fruit(Game& game, const char* fruit_name, GLuint atlas_tex) {
    std::string path = game.data_dir + "/models/fruit/" + fruit_name + "_single.mmd";
    if (fruit_mesh.load(path)) {
        fruit_atlas_tex = atlas_tex;
        has_fruit = true;
        printf("MenuButton: loaded fruit mesh '%s'\n", fruit_name);
    } else {
        fprintf(stderr, "MenuButton: failed to load fruit mesh '%s'\n", fruit_name);
    }
}

void MenuButton::update(float dt) {
    rotation += rotation_speed * dt;
    if (has_fruit) {
        fruit_rotation += dt * 2.0f;
    }
}

void MenuButton::draw(Renderer& r) {
    if (!visible || !texture) return;

    float draw_alpha = alpha;
    float draw_scale = 1.0f;
    if (pressed) {
        draw_scale = 0.95f;
        draw_alpha *= 0.8f;
    }

    float w = width * draw_scale;
    float h = height * draw_scale;
    float dx = x + (width - w) / 2.0f;
    float dy = y + (height - h) / 2.0f;

    // Draw ring texture with rotation
    r.draw_sprite(texture, dx, dy, w, h, rotation, draw_alpha);

    // Draw 3D fruit inside ring
    if (has_fruit && fruit_mesh.vbo && fruit_atlas_tex) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClear(GL_DEPTH_BUFFER_BIT);

        // Ortho projection matching game coords
        float hw = FN_SCREEN_W / 2.0f;
        float hh = FN_SCREEN_H / 2.0f;
        float proj[16];
        memset(proj, 0, sizeof(proj));
        proj[0]  = 1.0f / hw;
        proj[5]  = 1.0f / hh;
        proj[10] = -2.0f / 200.0f;
        proj[15] = 1.0f;

        float view[16];
        mat4_identity(view);

        float pv[16];
        mat4_multiply(pv, proj, view);

        // Fruit center = button center in game coords
        float cx = x + width / 2.0f;
        float cy = y + height / 2.0f;
        // Convert from game coords (0..480, 0..320) to ortho coords (-240..240, -160..160)
        float fx = cx - hw;
        float fy = cy - hh;

        // Scale fruit to fit inside ring (~40% of button size)
        float fruit_scale = (width < height ? width : height) * 0.006f * draw_scale;

        float scl[16], rot[16], sr[16], trans[16], model[16], mvp[16];
        mat4_scale(scl, fruit_scale, fruit_scale, fruit_scale);
        mat4_rotate_y(rot, fruit_rotation);
        mat4_multiply(sr, rot, scl);
        mat4_translate(trans, fx, fy, 0.0f);
        mat4_multiply(model, trans, sr);
        mat4_multiply(mvp, pv, model);

        r.draw_mesh(fruit_mesh, fruit_atlas_tex, mvp, model, draw_alpha);

        glDisable(GL_DEPTH_TEST);
    }
}

bool MenuButton::hit_test(float gx, float gy) {
    return gx >= x && gx <= x + width && gy >= y && gy <= y + height;
}

void MenuButton::touch_down(float gx, float gy) {
    if (hit_test(gx, gy)) {
        pressed = true;
    }
}

void MenuButton::touch_up(float gx, float gy) {
    if (pressed && hit_test(gx, gy) && on_click) {
        on_click();
    }
    pressed = false;
}
