//
// MenuButton — reimplemented from docs/structs/gameplay-misc.md
//
// Key finding: "The spinning 3D fruit is NOT drawn by MenuButton.
// It's a real Fruit/Bomb entity created via ActorManager::Add()
// and stored at MenuButton+0x80. The entity gets its position
// from the button, and ActorManager::Draw() renders it."
//
// MenuButton::Draw only renders 2D layers:
//   Layer 1: Button texture quad (ring graphic)
//   Layer 2: "New item" star indicator (TODO)
//   Layer 3: Sparkle ring (TODO)
//

#include "MenuButton.h"
#include "Renderer.h"
#include "Game.h"
#include "Fruit.h"
#include "ActorManager.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

MenuButton::MenuButton()
    : pressed(false), rotation_speed(0.5f),
      m_pEntity(NULL),
      m_HitScaleX(1.0f), m_HitScaleY(1.0f) {
    m_LayerFlags = 0x40;  // menu button layer
}

MenuButton::~MenuButton() {
    // Entity is owned by ActorManager — deactivate but don't delete
    if (m_pEntity) {
        m_pEntity->Deactivate();
        m_pEntity = NULL;
    }
}

void MenuButton::init(GLuint tex, float tex_w, float tex_h, float cx, float cy,
                      std::function<void()> callback) {
    m_Texture = tex;
    size = Vec3(tex_w, tex_h, 1.0f);
    pos = Vec3(cx, cy, 0.0f);
    on_click = callback;
    m_Alpha = 255;
    m_bActive = true;
}

// Matches MenuButton::Init (0x14ee40) fruit entity creation
void MenuButton::CreateFruitEntity(Game& game, int fruitType) {
    if (fruitType < 0 || !game.actorManager) return;

    // Create real entity via ActorManager (matches original: ActorManager::Add)
    Entity* e = game.actorManager->Add(0, false);  // type 0 = Fruit
    if (!e) return;

    Fruit* fruit = static_cast<Fruit*>(e);
    fruit->Init(0, fruitType, 0);

    // Position entity at button center
    fruit->pos = pos;
    fruit->scale = Vec3(25.0f, 25.0f, 25.0f);

    // Set layer to 0x40 (menu layer, matches original: field_0x34 = 0x40)
    fruit->flags &= ~0x10;  // unhide

    // Random rotation speed (original: 8-12 deg/frame, random direction)
    float speed = 8.0f + (float)(rand() % 40) / 10.0f;
    if (rand() % 2) speed = -speed;

    // Clamp rotation magnitude (original: min 0.75 for X, 0.5 for Y)
    if (fabsf(fruit->m_RotVel1.x) < 0.75f)
        fruit->m_RotVel1.x = (fruit->m_RotVel1.x >= 0 ? 0.75f : -0.75f);
    if (fabsf(fruit->m_RotVel1.y) < 0.5f)
        fruit->m_RotVel1.y = (fruit->m_RotVel1.y >= 0 ? 0.5f : -0.5f);

    m_pEntity = e;
    printf("MenuButton: created fruit entity type %d at (%.0f, %.0f)\n",
           fruitType, pos.x, pos.y);
}

void MenuButton::Update(float dt) {
    m_Timer += rotation_speed * dt;

    // Keep entity positioned at button center
    if (m_pEntity && m_pEntity->IsActive()) {
        m_pEntity->pos = pos;
    }
}

// MenuButton::Draw — only 2D layers, NO 3D fruit
// The fruit entity is drawn by ActorManager::Draw in GameDraw
void MenuButton::Draw(Renderer& r, const Vec3& hudScale, int layerMask) {
    (void)layerMask;
    if (!m_Texture || m_Alpha == 0) return;

    float draw_alpha_f = (float)m_Alpha / 255.0f;
    float draw_scale = 1.0f;
    if (pressed) {
        draw_scale = 0.95f;
        draw_alpha_f *= 0.8f;
    }

    // Layer 1: Button texture quad (the ring graphic)
    float w = size.x * draw_scale;
    float h = size.y * draw_scale;
    float dx = pos.x - w / 2.0f;
    float dy = pos.y - h / 2.0f;
    r.draw_sprite(m_Texture, dx, dy, w, h, m_Timer, draw_alpha_f);

    // Layer 2: "New item" star indicator — TODO
    // Layer 3: Sparkle ring (8-segment tri-list) — TODO
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
