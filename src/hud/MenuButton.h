#ifndef FN_MENU_BUTTON_H
#define FN_MENU_BUTTON_H

//
// MenuButton : HUDControl3d (0x15C bytes)
// Reimplemented from docs/structs/gameplay-misc.md
//
// 3-layer rendering: button quad + "new" indicator + sparkle ring
// The spinning 3D fruit is a real Entity from ActorManager, NOT drawn by MenuButton.
// MenuButton stores a pointer to the entity at +0x80 and positions it each frame.
//

#include "HUDControl3d.h"
#include <functional>

struct Renderer;
struct Game;
class Entity;

class MenuButton : public HUDControl3d {
public:
    // Click callback (replaces Delegate0<void>)
    std::function<void()> on_click;

    // Press state
    bool pressed;

    // +0xf4: rotation speed (8-12 deg/s random in original, 0 for toggles)
    float rotation_speed;

    // +0x80: real Fruit/Bomb entity created via ActorManager::Add
    // This entity is drawn by ActorManager::Draw, NOT by MenuButton.
    Entity* m_pEntity;

    // Hit-test scale factors (original: +0x124..+0x12C)
    float m_HitScaleX, m_HitScaleY;

    MenuButton();
    ~MenuButton();

    // Initialize button at center position (cx, cy) in game coords
    void init(GLuint tex, float tex_w, float tex_h, float cx, float cy,
              std::function<void()> callback);

    // Create a real Fruit entity via ActorManager and attach to this button
    // fruitType: 0+ = fruit index, -1 = no fruit (toggles)
    void CreateFruitEntity(Game& game, int fruitType);

    // HUDControl overrides
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override;

    // Touch input
    bool hit_test(float gx, float gy);
    void touch_down(float gx, float gy);
    void touch_up(float gx, float gy);
};

#endif
