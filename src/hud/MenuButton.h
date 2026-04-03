#ifndef MENU_BUTTON_H
#define MENU_BUTTON_H

#include "gl_funcs.h"
#include "Mesh.h"
#include <functional>
#include <string>

struct Renderer;
struct Game;

class MenuButton {
public:
    GLuint texture;
    float x, y;          // bottom-left corner in game coords
    float width, height;  // size in game coords
    float alpha;
    bool pressed;
    bool visible;

    // Ring rotation (original: field_0x2c / field_0xf4)
    float rotation;       // current angle in radians
    float rotation_speed; // radians per second

    // 3D fruit inside ring
    Mesh fruit_mesh;
    GLuint fruit_atlas_tex;  // shared atlas (not owned — don't delete)
    float fruit_rotation;    // fruit spin angle
    bool has_fruit;

    std::function<void()> on_click;

    MenuButton();
    ~MenuButton();

    // cx, cy = center position in game coords
    void init(GLuint tex, float tex_w, float tex_h, float cx, float cy,
              std::function<void()> callback);

    // Load a fruit mesh for this button. data_dir = game data path.
    // fruit_name = e.g. "watermelon", "mango", "kiwifruit" — loads <name>_single.mmd
    // atlas_tex = pre-loaded fruit atlas GL texture (shared, not owned)
    void load_fruit(Game& game, const char* fruit_name, GLuint atlas_tex);

    void update(float dt);
    void draw(Renderer& r);

    // Returns true if point is inside button bounds
    bool hit_test(float gx, float gy);
    void touch_down(float gx, float gy);
    void touch_up(float gx, float gy);
};

#endif
