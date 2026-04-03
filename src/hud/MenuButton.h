#ifndef FN_MENU_BUTTON_H
#define FN_MENU_BUTTON_H

#include "HUDControl3d.h"
#include "Mesh.h"
#include <functional>
#include <string>

struct Renderer;
struct Game;

// Matches original MenuButton : HUDControl (0x15C bytes)
// 3-layer rendering: button quad + "new" indicator + sparkle ring
// Plus a real 3D fruit entity drawn separately
class MenuButton : public HUDControl3d {
public:
    // Click callback (replaces original Delegate0<void>)
    std::function<void()> on_click;

    // Press state
    bool pressed;

    // Rotation speed (original: +0xf4, 8-12 deg/s random)
    float rotation_speed;

    // 3D fruit mesh rendered on top of button
    Mesh fruit_mesh;
    GLuint fruit_atlas_tex;  // shared atlas (not owned)
    float fruit_rotation;
    bool has_fruit;

    // Hit-test scale factors (original: +0x124..+0x12C)
    float m_HitScaleX, m_HitScaleY;

    MenuButton();
    ~MenuButton();

    // Initialize button at center position (cx, cy) in game coords
    void init(GLuint tex, float tex_w, float tex_h, float cx, float cy,
              std::function<void()> callback);

    // Load 3D fruit mesh for this button
    void load_fruit(Game& game, const char* fruit_name, GLuint atlas_tex);

    // HUDControl overrides
    void Update(float dt) override;
    void Draw(Renderer& r, const Vec3& hudScale, int layerMask) override;

    // Touch input
    bool hit_test(float gx, float gy);
    void touch_down(float gx, float gy);
    void touch_up(float gx, float gy);
};

#endif
