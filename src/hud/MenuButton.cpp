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

// Matches ClearMenuItems @ 0x0016ac7c — binary-exact. Two passes:
//   Pass 1 (fruits, type 0):
//     guard:  if entity->m_bSliced != 0 → skip
//     vx = RandFloat_Scaled(10.0) - 5.0    // [-5, +5)
//     vy = RandFloat_Scaled(5.0)           // [0, +5)
//     vel = (vx, vy, 0)
//     entity->m_bDrawWhole = 1             // +0x114
//     vel.x = |vel.x| * sign(pos.x)        // fly outward toward nearest edge
//     m_SecondVel = vel                    // copy to half-B
//     entity->m_bSliced = 1                // +0xb4 (set BEFORE the vel writes
//                                          //         in the binary order, but
//                                          //         the order doesn't matter)
//   Pass 2 (bombs, type 1):
//     guard:  if Bomb::Enabled() — also fling
//             Bomb::Disable()
//             same vel formula as fruits
//     unconditional: m_bMovement = 1       // +0x80
//
// RandFloat_Scaled(s) @ 0x0016a960 uses the engine's Rand32(0x7FFFF)
// divided by 524287.875, giving a uniform [0,1) × s. Port substitutes
// stdlib rand() / RAND_MAX which is functionally equivalent for visuals.
static float RandScaled(float s) {
    return ((float)rand() / (float)RAND_MAX) * s;
}

void FN_ClearMenuItems() {
    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;

    for (auto it = am->entities.begin(); it != am->entities.end(); ++it) {
        Entity* e = *it;
        if (!e || !e->IsActive()) continue;

        if (e->entityType == 0) {
            // --- Fruit pass ---
            Fruit* f = static_cast<Fruit*>(e);
            if (f->m_bSliced) continue;     // already released
            f->m_bSliced = 1;

            float vx = RandScaled(10.0f) - 5.0f;  // [-5, +5)
            float vy = RandScaled(5.0f);          // [0, +5)
            f->vel = Vec3(vx, vy, 0.0f);

            f->m_bDrawWhole = true;               // +0x114 — render whole

            // Sign-correct vel.x by sign(pos.x) so each fruit flies
            // outward toward the nearest screen edge.
            const float absVx = vx < 0 ? -vx : vx;
            const float sign  = (f->pos.x < 0.0f) ? -1.0f : 1.0f;
            f->vel.x = absVx * sign;

            f->m_SecondVel = f->vel;              // m_HalfB_vel = vel
        } else if (e->entityType == 1) {
            // --- Bomb pass ---
            Bomb* b = static_cast<Bomb*>(e);
            // Binary: if (Bomb::Enabled()) { Disable(); set vel; }
            // Port equivalent of Enabled() = active && !m_bHit.
            if (b->active && b->m_bHit == 0) {
                b->Deactivate();
                float vx = RandScaled(10.0f) - 5.0f;
                float vy = RandScaled(5.0f);
                b->vel = Vec3(vx, vy, 0.0f);
            }
            // m_bMovement = 1 fires unconditionally per binary.
            b->m_bMovement = 1;
        }
    }
}

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
    // m_TargetSize is the "full size" the FadeCounter shrink curve
    // multiplies against in MenuButton::Update (binary 0x0014e94?). If
    // explicit hit bounds were passed use those; otherwise fall back to
    // the button's own `size` (set by the caller from the texture
    // dimensions). Without this, dojo sub-buttons whose hitBounds is
    // (0,0,0) would shrink from zero to zero (invisible).
    m_TargetSize = m_bHasHitArea ? hitBounds : size;
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
            printf("[MenuButton] Init: fruitType=%d bombThreshold=%d -> entityType=%d\n",
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
                    // Binary then calls Bomb::SetCallback (0x0017121c) which:
                    //   - sets m_bMenuBombHit = 1 (menu-decoration marker)
                    //   - installs the click callback into m_HitCallback
                    //   - OVERWRITES rotation state so the menu bomb barely
                    //     rotates: one axis slow, the other locked. User-
                    //     confirmed axis mapping via in-game observation:
                    //       m_RotX    = 0x2d   (45)
                    //       m_RotVelY = 2      (slow spin, one axis)
                    //       m_RotY    = 0
                    //       m_RotVelX = 0      (other axis locked)
                    //   The rotation overwrites are why menu bombs look
                    //   almost static in the original — default 1..7
                    //   random vels from Bomb::Init get replaced.
                    Bomb* bomb = static_cast<Bomb*>(e);
                    bomb->m_bMovement = 0;
                    bomb->scale = bomb->scale * BOMB_MENU_SCALE;
                    bomb->m_bMenuBombHit = 1;
                    if (clickCb) {
                        bomb->m_HitCallback = clickCb;
                    }
                    bomb->m_RotX    = 0x2d;
                    bomb->m_RotVelY = 2;
                    bomb->m_RotY    = 0;
                    bomb->m_RotVelX = 0;
                    // Matches binary MenuButton::Init bomb branch @ 0x0014f144:
                    //   vstr.32 s15,[r0,#0x6c]   ; *(bomb+0x6c) = 150.0
                    // (s15 = DAT = FRUIT_ZPOS = 150.0). Overrides the depth
                    // cycling value that Bomb::Init assigned via
                    // GetBombZPosition() — so menu bombs share the same
                    // +150 layer as menu fruits and render in front of the
                    // ring instead of behind it.
                    bomb->m_ZPosition = FRUIT_ZPOS;
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

// Matches MenuButton::SetNewSymbol (0x0014e404)
void MenuButton::SetNewSymbol(bool show) {
    if (show) {
        if (m_NewIndicatorTimer < 0.0f)
            m_NewIndicatorTimer = 0.0f;
    } else {
        if (m_NewIndicatorTimer >= 0.0f)
            m_NewIndicatorTimer = -1.0f;
    }
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
    // Binary MenuButton::Update entity-tracking + shrink path
    // (0x0014e7?? .. 0x0014e962). Two branches:
    //
    //   if (m_pEntity != NULL):
    //       if (entity->m_bSliced != 0):              # released by ClearMenuItems
    //           if (|vel|² > 0.001):                  # actually moving
    //               fire m_ClickCallback once
    //               ClearMenuItems()
    //               m_pEntity = NULL                  # detach
    //       else:
    //           pin entity to button center (vel=0)
    //
    //   if (m_pEntity == NULL):
    //       m_FadeCounter -= dt * 108543              # ~9 frames to 0
    //       if (m_FadeCounter < 1):
    //           m_FadeCounter = 0
    //           m_bPendingRemoval = 1                 # HUD deletes button
    //       size = m_TargetSize * (sin(counter) / sin(0x3ffc))
    if (m_pEntity && m_pEntity->IsActive()) {
        bool released = false;
        if (m_pEntity->entityType == 0) {   // Fruit
            released = m_pFruitPiece && m_pFruitPiece->m_bSliced;
        } else if (m_pEntity->entityType == 1) { // Bomb
            Bomb* bomb = static_cast<Bomb*>(m_pEntity);
            released = (bomb->m_bHit != 0);
        }

        if (!released) {
            // Pin entity to button centre.
            m_pEntity->pos = pos;
            m_pEntity->vel = Vec3(0, 0, 0);

            // Snapshot the entity's base scale on the first frame once
            // CreateControls' per-button fruit->scale multiplier has
            // already been applied. m_HitBoundsScale starts at (0,0,0)
            // from MenuButton::Init (hitBounds arg is Vec3(0,0,0)); on
            // the first Update frame we capture m_pEntity->scale into
            // it, then per-frame scale the entity by the grow-in ratio.
            // Matches binary MenuButton::Update @ 0x0014e614:
            //   if (m_HitBoundsScale.x == 0.0) m_HitBoundsScale = entity->scale
            //   entity->scale = m_HitBoundsScale * (size.x / m_TargetSize.x)
            if (m_HitBoundsScale.x == 0.0f) {
                m_HitBoundsScale = m_pEntity->scale;
            }

            // Grow-in animation (binary MenuButton::Update @ 0x0014e614).
            // m_FadeCounter starts at 0 from Init, ramps to 0x3ffc at
            // DAT_0014e97c = 109200.0 counts/sec (~9 frames to full).
            // size = m_TargetSize * sin(counter * 2pi/65536). The 0x3ffc
            // index = pi/2 so sin(0x3ffc) = 1.0 -- the ratio simplifies
            // to just sin(counter), tracing a quarter-sine ease-out.
            // Entity scale tracks the same ratio so fruit zooms together
            // with the button ring.
            if (m_FadeCounter < 0x3ffc) {
                float next = (float)m_FadeCounter + dt * 109200.0f;
                if (next > 16380.0f) next = 16380.0f;
                m_FadeCounter = (int)next;
                const float counterRad =
                    (float)m_FadeCounter * (6.2831853f / 65536.0f);
                const float sizeFrac = sinf(counterRad);
                size             = m_TargetSize     * sizeFrac;
                m_pEntity->scale = m_HitBoundsScale * sizeFrac;
            } else {
                size             = m_TargetSize;
                m_pEntity->scale = m_HitBoundsScale;
            }
        } else {
            // Velocity-magnitude check matches binary
            // DAT_0014e978 = 0x3a83126f ≈ 0.001f.
            const Vec3& v = m_pEntity->vel;
            const float velSq = v.x * v.x + v.y * v.y + v.z * v.z;
            if (velSq > 0.001f) {
                // Distinguish "user actually sliced this fruit" from
                // "ClearMenuItems released this fruit as a sibling
                // when a different button was sliced". Only the
                // user-sliced fruit fires its callback — the others
                // just clear+detach silently. The flag we use:
                // m_bDrawWhole is set by ClearMenuItems but NOT by
                // the normal Fruit::Slice path, so:
                //   m_bDrawWhole == 0 -> user-sliced
                //   m_bDrawWhole == 1 -> ClearMenuItems-released
                // Without this gate the cascade fires every menu
                // button's callback in turn (Dojo -> Play -> Quit),
                // which thrashes MainScreen's state machine through
                // STATE_DOJO_WAIT_B -> MODE_SELECT -> GAME_START -> ...
                bool userSliced = (m_pEntity->entityType == 0) &&
                                  m_pFruitPiece &&
                                  !m_pFruitPiece->m_bDrawWhole;
                if (!m_bRemovalPending && m_ClickCallback && userSliced) {
                    auto cb = m_ClickCallback;
                    m_ClickCallback = nullptr;
                    cb();
                    // ClearMenuItems @ 0x0016ac7c — releases every
                    // other menu fruit so the dojo transition can
                    // proceed. Only fired alongside the user-slice
                    // callback to avoid recursive re-clearing.
                    FN_ClearMenuItems();
                }
                // Detach the entity → next frame enters the FadeCounter
                // shrink path below.
                m_pEntity = NULL;
                m_pFruitPiece = NULL;
                // Initialise the shrink counter — binary uses 0x3ffc.
                m_FadeCounter = 0x3ffc;
            }
        }
    }

    if (m_pEntity == NULL && m_FadeCounter > 0) {
        // Shrink-to-disappearance phase. Binary DAT_0014e97c = 108543.0
        // is the per-second decrement rate; over a 60Hz tick that's
        // ~1809 counts/frame → ~9 frames from 0x3ffc to 0.
        m_FadeCounter -= (int)(dt * 108543.0f);
        if (m_FadeCounter < 1) {
            m_FadeCounter = 0;
            m_bPendingRemoval = 1;
        }
        // size = m_TargetSize * (sin(counter * 2π/65536) / sin(0x3ffc * 2π/65536))
        // The 0x3ffc index is exactly π/2, so sin(0x3ffc) = 1.0 — the
        // ratio simplifies to just sin(counter). For counter in
        // [0, 0x3ffc] this traces the first quarter of a sine wave,
        // giving an ease-out shrink curve.
        const float counterRad = (float)m_FadeCounter * (6.2831853f / 65536.0f);
        const float scaleFrac  = sinf(counterRad);
        size = m_TargetSize * scaleFrac;
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
