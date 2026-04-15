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
#include "entities/Bomb.h"
#include "entities/FruitInfo.h"
#include "entities/ActorManager.h"
#include "input/Touch.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

// Constants from binary (verified via read_memory / disassembly)
// IMPORTANT: DAT_0014f194 = 0.2f is applied to Fruit::m_RotVel1 (rotation slowdown),
// NOT to scale. The scale is left at its gameplay value.
static const float FRUIT_ROTVEL_MULT = 0.2f;        // DAT_0014f194 — slows fruit spin
static const float FRUIT_ZPOS = 150.0f;              // DAT_0014f198
static const float BOMB_MENU_SCALE = 0.85f;          // DAT_0014f1a0 — bomb scale in menu
static const float ROT_SPEED_MIN = 8.0f;
static const float ROT_SPEED_RANGE = 4.0f;           // 8-12 range
static const float ROT_CLAMP_X = 0.75f;
static const float ROT_CLAMP_Y = 0.5f;

MenuButton::MenuButton()
    : m_pEntity(NULL),
      m_FruitType(-1),
      m_FadeCounter(0),
      m_fieldD4(0),
      m_TouchSlot(-1),
      m_TouchX(0.0f), m_TouchY(0.0f), m_TouchPhase(0.0f),
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
            // Original: entityType = (FruitInfo_GetCount() <= fruitType) ? 1 : 0
            int bombThreshold = FruitInfo_GetCount();
            int entityType = (bombThreshold <= fruitType) ? 1 : 0;  // 0=Fruit, 1=Bomb
            printf("[MenuButton] Init: fruitType=%d bombThreshold=%d → entityType=%d\n",
                   fruitType, bombThreshold, entityType);
            Entity* e = game->actorManager->Add(entityType, true);
            if (e) {
                e->pos = buttonPos;
                e->Init(0, fruitType, 0);
                e->flags &= ~0x10;  // unhide
                m_pEntity = e;

                if (entityType == 0) {
                    // Fruit entity: post-init adjustments (matches MenuButton::Init 0x0014ee40)
                    // Key finding: the 0.2 multiplier (DAT_0014f194) is applied to
                    // m_RotVel1 (fruit+0xF0), NOT to scale. Fruit keeps gameplay scale.
                    Fruit* fruit = static_cast<Fruit*>(e);
                    fruit->m_RotVel1 = fruit->m_RotVel1 * FRUIT_ROTVEL_MULT;
                    fruit->m_ScaleAnim = 1.0f;
                    fruit->m_ChuckDelay = 0.0f;
                    fruit->m_ZPosition = FRUIT_ZPOS;
                    m_pFruitPiece = fruit;

                    // Clamp rotation magnitude (after the ×0.2 reduction)
                    if (fabsf(fruit->m_RotVel1.x) < ROT_CLAMP_X)
                        fruit->m_RotVel1.x = (fruit->m_RotVel1.x >= 0 ? ROT_CLAMP_X : -ROT_CLAMP_X);
                    if (fabsf(fruit->m_RotVel1.y) < ROT_CLAMP_Y)
                        fruit->m_RotVel1.y = (fruit->m_RotVel1.y >= 0 ? ROT_CLAMP_Y : -ROT_CLAMP_Y);
                } else {
                    // Bomb entity: disable physics and scale by 0.85 (DAT_0014f1a0)
                    // MenuButton::Init (0x0014ee40): writes 0 to bomb+0x80 (m_bMovement)
                    // then bomb->scale *= 0.85.
                    //
                    // Binary then calls Bomb::SetCallback (0x0017121c) which
                    // sets m_bMenuBombHit = 1 (marking it as a menu-decoration
                    // bomb) and installs the click callback into the bomb's
                    // m_HitCallback delegate. When the player slices this
                    // bomb, Bomb::CollisionResponse takes the else branch
                    // and fires the callback — for the Quit button this
                    // triggers QuitGamesCallback. See
                    // docs/engine/bomb-collision-response.md.
                    Bomb* bomb = static_cast<Bomb*>(e);
                    bomb->m_bMovement = 0;
                    bomb->scale = bomb->scale * BOMB_MENU_SCALE;
                    bomb->m_bMenuBombHit = 1;
                    if (clickCb) {
                        bomb->m_HitCallback = clickCb;
                    }
                }

                // Random rotation speed (8-12 deg/frame, random direction)
                m_RotationSpeed = ROT_SPEED_MIN + (float)(rand() % 40) / 10.0f;
                if (rand() % 2) m_RotationSpeed = -m_RotationSpeed;
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

// Trigger the fade-out-then-remove animation used by screen
// transitions. After this is called, the button stops accepting
// touches, fades its alpha each frame, and self-removes via
// m_bPendingRemoval once alpha hits near zero.
void MenuButton::StartFadeOut() {
    m_bRemovalPending = 1;
    m_bInteractive = 0;
    // Pre-load the fade counter so the lerp starts from the
    // current alpha rather than full opaque. m_DrawColour.a is
    // already valid (255 for normal, 128 for highlighted).
}

// Matches MenuButton::Update (0x0014e614)
void MenuButton::Update(float dt) {
    // Fade-out animation. Decay alpha each frame; when below the
    // threshold, mark the control for HUD::Update to delete next
    // frame. ~16 frames at 0.85 decay = ~0.27s fade. Tuned to roughly
    // match the screen-transition timer decay (m_Timer2 *= 0.75 in
    // MainScreen DOJO_WAIT_*) so the button alpha hits zero around
    // the same time the screen state is ready to spawn DojoScreen.
    if (m_bRemovalPending) {
        const float FADE_DECAY  = 0.85f;
        const int   ALPHA_FLOOR = 6;
        int newAlpha = (int)((float)m_DrawColour.a * FADE_DECAY);
        m_DrawColour.a = (uint8_t)newAlpha;
        if (newAlpha <= ALPHA_FLOOR) {
            m_DrawColour.a = 0;
            m_bPendingRemoval = 1;
        }
    }

    // Sparkle timer tick (field_0x2c = m_SparkleTimer — rate × 8.0/s, cap at DAT=8.0)
    // TODO: full sparkle/new-indicator logic

    // Rotate button quad: m_Timer accumulates at m_RotationSpeed deg/s (DAT_0014e974=360.0 wrap)
    if (m_FruitType >= 0 && dt > 0.0f) {
        m_Timer += dt * m_RotationSpeed;
        if (m_Timer < 0.0f) m_Timer += 360.0f;  // DAT_0014e974 = 360.0
    }

    // Keep entity positioned at button center each frame and zero its
    // velocity so accumulated gravity from Fruit::Update doesn't carry
    // over. Exception: a sliced fruit piece is released — its two
    // halves fall away under their own halfVel and gravity instead of
    // staying pinned. When the slice edge fires, also trigger the
    // click callback so menu-fruit buttons (Play / Dojo) transition
    // on slash-through, matching the binary's menu button flow.
    if (m_pEntity && m_pEntity->IsActive()) {
        // "Hit" edge detection — for fruits it's m_bSliced, for bombs
        // it's m_bHit. Both types keep the entity pinned to the button
        // until the hit moment, then release it so its physics animation
        // (fruit halves falling, bomb launching via QuitGamesCallback)
        // can play out.
        bool hit = false;
        if (m_pEntity->entityType == 0) {   // Fruit
            hit = m_pFruitPiece && m_pFruitPiece->m_bSliced;
        } else if (m_pEntity->entityType == 1) { // Bomb
            Bomb* bomb = static_cast<Bomb*>(m_pEntity);
            hit = (bomb->m_bHit != 0);
        }

        if (!hit) {
            m_pEntity->pos = pos;
            m_pEntity->vel = Vec3(0, 0, 0);
        } else if (!m_bRemovalPending && m_ClickCallback &&
                   m_pEntity->entityType == 0) {
            // Rising-edge slice callback for fruit buttons only. Bombs
            // fire their hit callback directly via Bomb::OnSliced's
            // m_HitCallback path; firing here too would double-invoke.
            auto cb = m_ClickCallback;
            m_ClickCallback = nullptr;
            cb();
        }
    }

    // Shake timer decay
    if (m_ShakeTimer > 0.0f) {
        m_ShakeTimer -= dt;
        if (m_ShakeTimer < 0.0f) m_ShakeTimer = 0.0f;
    }

    // -----------------------------------------------------------------------
    // Touch block — matches binary MenuButton::Update (0x0014e614).
    // Poll-based: iterates Touch slots inside the button rect. If none was
    // tracked last frame, latch the first slot inside. On release, fire the
    // callback if the release position is still inside the rect.
    // -----------------------------------------------------------------------
    if (!m_bInteractive || !m_bEnabled) return;

    // Compute rect bounds. Binary inflates by m_AnimSpeed/m_AnimSpeed2 which
    // are 5.0 defaults — small inset/outset that gives the button a touch-up
    // "grace zone". The port mirrors that.
    float hw, hh;
    if (m_bHasHitArea) {
        hw = m_TargetSize.x * 0.5f;
        hh = m_TargetSize.y * 0.5f;
    } else {
        hw = size.x * 0.5f;
        hh = size.y * 0.5f;
    }
    const float left   = pos.x - hw - m_AnimSpeed2;
    const float right  = pos.x + hw + m_AnimSpeed2;
    const float bottom = pos.y - hh - m_AnimSpeed;
    const float top    = pos.y + hh + m_AnimSpeed;

    Mortar::Touch& touch = Mortar::Touch::GetInstance();

    if (m_TouchSlot == -1) {
        // Not tracking — scan for a new touch inside the rect.
        int slot = touch.GetTouchInRegion(left, right, bottom, top, -1);
        if (slot >= 0) {
            // Latch the slot. Phase will be -1 (just pressed) on the first
            // frame of a new touch; the binary fires the callback on the
            // press edge for toggle buttons (FruitType < 0). Regular button
            // buttons wait for release.
            m_TouchSlot = slot;
            m_bHighlighted = 1;
            UpdateTouchPosition();
        }
    } else {
        // Tracking — refresh position and check for release.
        UpdateTouchPosition();
        // phase >= 1 means released. Fire callback if release was inside rect.
        // Only toggle buttons (m_FruitType < 0, e.g. sound/music toggles)
        // trigger on tap-release. Fruit/bomb buttons (Play / Dojo / Quit)
        // require a slash-through instead — their callback fires via the
        // rising-edge hit detection above (fruits) or Bomb::m_HitCallback
        // (bombs), matching the "slice to play" gameplay intent.
        if (m_TouchPhase >= 1.0f) {
            const bool insideOnRelease =
                m_TouchX >= left && m_TouchX <= right &&
                m_TouchY >= bottom && m_TouchY <= top;
            const bool isToggle = (m_FruitType < 0);
            if (insideOnRelease && isToggle && m_ClickCallback) {
                m_ClickCallback();
            }
            m_TouchSlot = -1;
            m_bHighlighted = 0;
        }
    }
}

// Matches binary MenuButton::UpdateTouchPosition (0x0014e3c4).
// Copies x/y/phase from the tracked Touch slot into m_TouchX/Y/Phase.
void MenuButton::UpdateTouchPosition() {
    if (m_TouchSlot < 0) return;
    const Mortar::TouchState* s =
        Mortar::Touch::GetInstance().GetSlot(m_TouchSlot);
    if (!s) return;
    m_TouchX     = (float)s->currX;
    m_TouchY     = (float)s->currY;
    m_TouchPhase = (float)s->phase;
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

// Removed: HitTest, TouchDown, TouchUp. Touch input is now polled inside
// MenuButton::Update via Mortar::Touch::GetTouchInRegion — matching the
// binary's poll-based flow. See touch-rewrite-plan.md.
