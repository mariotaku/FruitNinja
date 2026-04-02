#include "menu_button.h"
#include "renderer.h"

MenuButton::MenuButton()
    : texture(0), x(0), y(0), width(0), height(0),
      alpha(1.0f), pressed(false), visible(true) {}

void MenuButton::init(GLuint tex, float tex_w, float tex_h, float cx, float cy,
                      std::function<void()> callback) {
    texture = tex;
    width = tex_w;
    height = tex_h;
    x = cx - width / 2.0f;
    y = cy - height / 2.0f;
    on_click = callback;
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

    r.draw_sprite(texture, dx, dy, w, h, 0.0f, draw_alpha);
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
