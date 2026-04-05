//
// MenuButton : HUDControl3d (0x15C bytes)
// Reimplemented from docs/structs/gameplay-misc.md
//
// Architecture:
//   Layer 0 (3D): Spinning fruit entity (ActorManager::Draw, NOT MenuButton)
//   Layer 1 (2D): Button texture quad via HUDControl3d::Draw
//   Layer 2 (2D): "New item" star indicator (TODO)
//   Layer 3 (2D): Sparkle ring (TODO)
//

#include "MenuButton.h"
#include "Game.h"
#include "entities/Fruit.h"
#include "entities/ActorManager.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

// Constants from binary (verified via read_memory)
static const float FRUIT_SCALE_FOR_MENU = 0.2f;     // DAT_0014f194
static const float FRUIT_ZPOS = 150.0f;              // DAT_0014f198
static const float ROT_SPEED_MIN = 8.0f;
static const float ROT_SPEED_RANGE = 4.0f;           // 8-12 range
static const float ROT_CLAMP_X = 0.75f;
static const float ROT_CLAMP_Y = 0.5f;

MenuButton::MenuButton()
    : m_pEntity(NULL),
      m_FruitType(-1),
      m_FadeCounter(0),
      m_RandomOffset(0.0f),
      m_bFlipped(false),
      m_RotationSpeed(0.0f),
      m_SparkleTimer(-1.0f),
      m_NewIndicatorTimer(-1.0f),
      m_HitBoundsScale(0.0f, 0.0f, 0.0f),
      m_pLabel1(NULL), m_pLabel2(NULL),
      m_PlayerIndex(0),
      m_bScoreSubmitted(0),
      m_bVisible(1),
      m_bInteractive(1),
      m_bEnabled(1),
      m_TargetSize(0.0f, 0.0f, 0.0f),
      m_bHasHitArea(false),
      m_bHighlighted(0),
      m_pFruitPiece(NULL),
      m_bRemovalPending(0),
      m_AnimScale(1.0f),
      m_BounceParams(0.0f, 0.0f, 0.0f),
      m_AnimSpeed2(5.0f),
      m_AnimSpeed(5.0f),
      m_field154(0.0f),
      m_ShakeTimer(0.0f)
{
    m_LayerFlags = 0x40;  // menu button layer
}

MenuButton::~MenuButton() {
    Release();
}

// Matches MenuButton::Init (0x0014ee40, 222 lines)
void MenuButton::Init(const Vec3& buttonPos, std::function<void()> clickCb,
                      int fruitType, const Vec3& hitBounds,
                      std::function<void()> deletedCb) {
    pos = buttonPos;
    m_ClickCallback = clickCb;
    m_DeletedCallback = deletedCb;
    m_FruitType = fruitType;
    m_HitBoundsScale = hitBounds;
    m_bHasHitArea = (hitBounds.x > 0.0f || hitBounds.y > 0.0f);
    m_TargetSize = hitBounds;
    m_bVisible = 1;
    m_bInteractive = 1;
    m_bEnabled = 1;
    m_AnimScale = 1.0f;
    m_AnimSpeed = 5.0f;
    m_AnimSpeed2 = 5.0f;

    // Create fruit entity if fruitType >= 0 (toggles use -1)
    if (fruitType >= 0) {
        Game* game = Game::GetInstance();
        if (game && game->actorManager) {
            // Original: entityType = (fruitType >= bombThreshold) ? 1 : 0
            int entityType = 0;  // 0 = Fruit
            Entity* e = game->actorManager->Add(entityType, true);
            if (e) {
                Fruit* fruit = static_cast<Fruit*>(e);
                fruit->pos = buttonPos;
                fruit->Init(0, fruitType, 0);

                // Post-init: shrink fruit for menu display
                fruit->scale = fruit->scale * FRUIT_SCALE_FOR_MENU;
                fruit->m_ScaleAnim = 1.0f;
                fruit->m_ChuckDelay = 0.0f;
                fruit->m_ZPosition = FRUIT_ZPOS;
                fruit->flags &= ~0x10;  // unhide

                m_pEntity = e;
                m_pFruitPiece = fruit;

                // Random rotation speed (8-12 deg/frame, random direction)
                m_RotationSpeed = ROT_SPEED_MIN + (float)(rand() % 40) / 10.0f;
                if (rand() % 2) m_RotationSpeed = -m_RotationSpeed;

                // Clamp rotation magnitude
                if (fabsf(fruit->m_RotVel1.x) < ROT_CLAMP_X)
                    fruit->m_RotVel1.x = (fruit->m_RotVel1.x >= 0 ? ROT_CLAMP_X : -ROT_CLAMP_X);
                if (fabsf(fruit->m_RotVel1.y) < ROT_CLAMP_Y)
                    fruit->m_RotVel1.y = (fruit->m_RotVel1.y >= 0 ? ROT_CLAMP_Y : -ROT_CLAMP_Y);
            }
        }

        m_LayerFlags = 0x40;  // menu draw layer
    }
}

// Matches MenuButton::Release
void MenuButton::Release() {
    // Entity is owned by ActorManager — deactivate but don't delete
    if (m_pEntity) {
        m_pEntity->Deactivate();
        m_pEntity = NULL;
        m_pFruitPiece = NULL;
    }

    // TODO: delete BakedString labels
    m_pLabel1 = NULL;
    m_pLabel2 = NULL;
}

// Matches MenuButton::Update (0x0014e614)
void MenuButton::Update(float dt) {
    // m_Timer stays 0 — ring texture does NOT rotate.
    // The spinning 3D fruit entity rotates via its own Update (ActorManager tick).

    // Keep entity positioned at button center
    if (m_pEntity && m_pEntity->IsActive()) {
        m_pEntity->pos = pos;
    }

    // Shake timer decay
    if (m_ShakeTimer > 0.0f) {
        m_ShakeTimer -= dt;
        if (m_ShakeTimer < 0.0f) m_ShakeTimer = 0.0f;
    }

    // TODO: fade counter animation
    // TODO: sparkle timer tick
    // TODO: "new" indicator bounce animation
}

// Matches MenuButton::Draw (0x0014f9cc, 359 lines)
void MenuButton::Draw(const Vec3& hudScale, int layerMask) {
    if (!m_bVisible || m_DrawColour.a == 0) return;

    // Layer 1: Button texture quad
    // Original applies shake offset if m_ShakeTimer > 0 (random ±3.0)
    if (m_ShakeTimer > 0.0f) {
        Vec3 savedPos = pos;
        pos.x += ((float)(rand() % 600) / 100.0f) - 3.0f;
        pos.y += ((float)(rand() % 600) / 100.0f) - 3.0f;
        HUDControl3d::Draw(hudScale, layerMask);
        pos = savedPos;
    } else {
        HUDControl3d::Draw(hudScale, layerMask);
    }

    // Layer 2: "New item" star indicator — TODO
    // if (m_NewIndicatorTimer >= 0) { ... SinIdx bounce, dimmed/highlighted ... }

    // Layer 3: Sparkle ring — TODO
    // if (m_SparkleTimer >= 0) { ... 8 segments × 6 verts = 48 QUADCUSTOMVERTEX ... }
}

// Touch input — matches original (uses m_bInteractive, not pressed bool)
bool MenuButton::HitTest(float gx, float gy) {
    if (!m_bInteractive || !m_bEnabled) return false;

    float hw, hh;
    if (m_bHasHitArea) {
        hw = m_TargetSize.x / 2.0f;
        hh = m_TargetSize.y / 2.0f;
    } else {
        hw = size.x / 2.0f;
        hh = size.y / 2.0f;
    }

    return gx >= pos.x - hw && gx <= pos.x + hw &&
           gy >= pos.y - hh && gy <= pos.y + hh;
}

void MenuButton::TouchDown(float gx, float gy) {
    if (HitTest(gx, gy)) {
        m_bHighlighted = 1;
    }
}

void MenuButton::TouchUp(float gx, float gy) {
    if (m_bHighlighted && HitTest(gx, gy) && m_ClickCallback) {
        m_ClickCallback();
    }
    m_bHighlighted = 0;
}
