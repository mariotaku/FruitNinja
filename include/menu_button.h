#ifndef MENU_BUTTON_H
#define MENU_BUTTON_H

#include "gl_funcs.h"
#include <functional>

struct Renderer;

class MenuButton {
public:
    GLuint texture;
    float x, y;          // bottom-left corner in game coords
    float width, height;  // size in game coords
    float alpha;
    bool pressed;
    bool visible;

    std::function<void()> on_click;

    MenuButton();

    // cx, cy = center position in game coords
    void init(GLuint tex, float tex_w, float tex_h, float cx, float cy,
              std::function<void()> callback);

    void draw(Renderer& r);

    // Returns true if point is inside button bounds
    bool hit_test(float gx, float gy);
    void touch_down(float gx, float gy);
    void touch_up(float gx, float gy);
};

#endif
