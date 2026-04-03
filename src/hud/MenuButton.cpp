#include "MenuButton.h"
#include "Renderer.h"
#include "Game.h"
#include "math3d.h"
#include <cstdio>
#include <cmath>

MenuButton::MenuButton()
    : pressed(false), rotation_speed(0.5f),
      fruit_atlas_tex(0), fruit_rotation(0.0f), has_fruit(false),
      m_HitScaleX(1.0f), m_HitScaleY(1.0f) {
    m_LayerFlags = 0x40;  // menu button layer
}

MenuButton::~MenuButton() {
    fruit_mesh.destroy();
    // fruit_atlas_tex is shared — NOT deleted here
}

void MenuButton::init(GLuint tex, float tex_w, float tex_h, float cx, float cy,
                      std::function<void()> callback) {
    m_Texture = tex;
    // Size = full texture dimensions (HUDControl3d draws unit quad scaled by size)
    size = Vec3(tex_w, tex_h, 1.0f);
    // Position = center of button in game coords
    pos = Vec3(cx, cy, 0.0f);
    on_click = callback;
    m_Alpha = 255;
    m_bActive = true;
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

void MenuButton::Update(float dt) {
    m_Timer += rotation_speed * dt;
    if (has_fruit) {
        fruit_rotation += dt * 2.0f;
    }
}

void MenuButton::Draw(Renderer& r, const Vec3& hudScale, int layerMask) {
    (void)layerMask;
    if (!m_Texture || m_Alpha == 0) return;

    float draw_alpha_f = (float)m_Alpha / 255.0f;
    float draw_scale = 1.0f;
    if (pressed) {
        draw_scale = 0.95f;
        draw_alpha_f *= 0.8f;
    }

    // Layer 1: Button texture quad using draw_sprite (handles matrix setup)
    float w = size.x * draw_scale;
    float h = size.y * draw_scale;
    // pos is center, draw_sprite expects bottom-left
    float dx = pos.x - w / 2.0f;
    float dy = pos.y - h / 2.0f;
    r.draw_sprite(m_Texture, dx, dy, w, h, m_Timer, draw_alpha_f);

    // Layer 2: 3D fruit inside button
    // The fruit mesh sits INSIDE the button ring texture, scaled to ~40% of button size.
    // Original: fruit is a real Entity rendered by ActorManager::Draw.
    // Port: we render it inline with a small ortho projection.
    if (has_fruit && fruit_mesh.vbo && fruit_atlas_tex) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glClear(GL_DEPTH_BUFFER_BIT);

        // Ortho matching the game's 0-480 × 0-320 screen coords
        float proj[16];
        memset(proj, 0, sizeof(proj));
        proj[0]  = 2.0f / FN_SCREEN_W;       // maps 0..480 to -1..1
        proj[5]  = 2.0f / FN_SCREEN_H;       // maps 0..320 to -1..1
        proj[10] = -2.0f / 200.0f;
        proj[12] = -1.0f;                     // offset for 0-based coords
        proj[13] = -1.0f;
        proj[15] = 1.0f;

        // Fruit scale: mesh coords are ~10-25 units, button is ~64-256 px.
        // Scale fruit to fit ~35% of the smaller button dimension in screen pixels.
        float buttonPx = (size.x < size.y ? size.x : size.y);
        float fruit_scale = buttonPx * 0.35f / 25.0f * draw_scale;  // 25.0 = approx mesh radius

        float scl[16], rot[16], sr[16], trans[16], model[16], mvp[16];
        mat4_scale(scl, fruit_scale, fruit_scale, fruit_scale);
        mat4_rotate_y(rot, fruit_rotation);
        mat4_multiply(sr, rot, scl);
        mat4_translate(trans, pos.x, pos.y, 0.0f);  // center of button in game coords
        mat4_multiply(model, trans, sr);
        mat4_multiply(mvp, proj, model);

        r.draw_mesh(fruit_mesh, fruit_atlas_tex, mvp, model, draw_alpha_f);

        glDisable(GL_DEPTH_TEST);
    }
}

bool MenuButton::hit_test(float gx, float gy) {
    float hw = size.x * m_HitScaleX / 2.0f;
    float hh = size.y * m_HitScaleY / 2.0f;
    return gx >= pos.x - hw && gx <= pos.x + hw &&
           gy >= pos.y - hh && gy <= pos.y + hh;
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
