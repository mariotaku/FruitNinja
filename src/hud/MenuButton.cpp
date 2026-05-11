//
// MenuButton : HUDControl3d (0x15C bytes)
// Reimplemented from docs/structs/gameplay-misc.md
//
// Architecture:
//   Layer 0 (3D): Spinning fruit entity (Mortar::ActorManager::Draw, NOT MenuButton)
//   Layer 1 (2D): Button texture quad via HUDControl3d::Draw
//   Layer 2 (2D): "New item" star indicator (TODO)
//   Layer 3 (2D): Sparkle ring (TODO)
//

// Analysed: 2026-04-28T14:00
#include "MenuButton.h"
#include "HUD.h"
#include "hud/HUDLayer.h"
#include "Game.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "TutorialControl.h"
#include "screens/MainScreen.h"
#include "entities/FruitInfo.h"
#include "entities/ActorManager.h"
#include "input/Touch.h"
#include "asset/TextureManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>

// Class-static textures loaded by MenuButton::LoadContent (binary @ 0x0014f674).
// LoadContent loads three shared SmartPtrs in this order, verified by RE
// pass against literal pool 0x0014f6f0..0x0014f70c:
//   Slot 1  GOT+0x77E0  scratchs.tex        (Phase-A backdrop — used below)
//   Slot 2  GOT+0x79DC  blurry_backing.tex  (sparkle ring base — TODO sparkle Draw)
//   Slot 3  GOT+0x7894  new_item.tex        (Layer-2 NEW star — used below)
static Mortar::SmartPtr<Mortar::Texture> s_TexScratchs;
static Mortar::SmartPtr<Mortar::Texture> s_TexBlurryBacking;
static Mortar::SmartPtr<Mortar::Texture> s_TexNewItem;

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

void FN::ClearMenuItems() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Binary ClearMenuItems @ 0x0016ac7c is two back-to-back while loops
    // over the iterator pair -- fruits first (type 0), then bombs
    // (type 1). Mirror that structure here so the port reads 1:1 with
    // the disassembly.

    // --- Pass 1: fruits (type 0) ---
    {
        std::list<Mortar::Entity*>::iterator it;
        Mortar::Entity* e = am->GetEntityFirst(0, it);
        while (e != nullptr) {
            Fruit* f = static_cast<Fruit*>(e);
            if (f->m_bSliced == 0) {
                float vx = RandScaled(10.0f) - 5.0f;   // [-5, +5)
                float vy = RandScaled(5.0f);           // [0, +5)
                f->vel = Vec3(vx, vy, 0.0f);

                f->m_bDrawWhole = true;                // +0x114

                // Sign-correct vel.x by sign(pos.x) so each fruit flies
                // outward toward the nearest screen edge.
                const float absVx = vx < 0 ? -vx : vx;
                const float sign  = (f->pos.x < 0.0f) ? -1.0f : 1.0f;
                f->vel.x = absVx * sign;

                f->m_SecondVel = f->vel;               // m_HalfB_vel = vel
                f->m_bSliced = 1;
            }
            e = am->GetEntityNext(0, it);
        }
    }

    // --- Pass 2: bombs (type 1) ---
    {
        std::list<Mortar::Entity*>::iterator it;
        Mortar::Entity* e = am->GetEntityFirst(1, it);
        while (e != nullptr) {
            // Binary-exact:
            //   if (Bomb::Enabled()) {          // !m_bCollisionGuard
            //       Bomb::Disable();            // m_bCollisionGuard = 1
            //       vel = (Rand(10)-5, Rand(5), 0);
            //   }
            //   m_bMovement = 1;                // unconditional
            //
            // Binary never writes m_bHit here -- the bomb stays in
            // Bomb::Update's ALIVE branch, which also runs gravity
            // (via m_bMovement gate). Disabling collision alone is
            // what stops further slices.
            Bomb* b = static_cast<Bomb*>(e);
            if (b->Enabled()) {
                b->Disable();
                float vx = RandScaled(10.0f) - 5.0f;
                float vy = RandScaled(5.0f);
                b->vel = Vec3(vx, vy, 0.0f);
            }
            b->m_bMovement = 1;
            e = am->GetEntityNext(1, it);
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
    : m_pEntity(nullptr),
      m_FruitType(-1),
      m_FadeCounter(0),
      m_fieldD4(0),
      m_TouchSlot(-1),
      m_TouchX(0.0f), m_TouchY(0.0f), m_TouchPhase(0.0f),
      m_RandomOffset(0.0f),
      m_BackdropScale(0.0f),  // Binary leaves +0xEC uninitialized; zero-init for fidelity
      m_bFlipped(false),
      m_RotationSpeed(0.0f),
      m_SparkleTimer(-1.0f),
      m_NewIndicatorTimer(-1.0f),
      m_HitBoundsScale(0.0f, 0.0f, 0.0f),
      m_pLabel1(nullptr), m_pLabel2(nullptr),
      m_PlayerIndex(0),
      m_bScoreSubmitted(0),
      m_bVisible(1),
      m_bInteractive(1),
      m_bEnabled(1),
      m_TargetSize(0.0f, 0.0f, 0.0f),
      m_bHasHitArea(false),
      m_bHighlighted(0),
      m_pFruitPiece(nullptr),
      m_bRespondsToBackKey(0),
      m_AnimScale(1.0f),
      // m_BounceParams = (0.85, 0.85, 0.0) per binary @ 0x0014f240/0x0014f244.
      // Drives the new-item star anchor offset (0.425*W, 0.425*H from button centre).
      m_BounceParams(0.85f, 0.85f, 0.0f),
      m_AnimSpeed2(5.0f),
      m_AnimSpeed(5.0f),
      m_field154(0.0f),
      m_ShakeTimer(0.0f)
{
    // Binary HUDControl base ctor sets m_LayerFlags = 1. MenuButton::Init
    // bumps it to 0x40 only when fruitType >= 0 (i.e. the spinning-fruit
    // backdrop variant). Text-only buttons (fruitType < 0) stay at the
    // default 1, drawing in HUD::Draw(0x01) — which is AFTER splats.
    // Don't unconditionally promote here.
}

// ASM-verified: 2026-05-06T00:00 binary @ 0x0014f94c (asm-inspector)
// Binary D2 dtor runs only subobject teardown (vtbl install ->
// ~list<AddOn>(+0x10C) -> ~Delegate0(+0xAC) -> ~Delegate0(+0x88) ->
// ~HUDControl3d) -- NO call to MenuButton::Release. Release() is the
// SEPARATE vtable slot at 0x0014f7e0 invoked BEFORE delete by every
// HUDControl-deleting site (HUD::Release / HUD::Update pending-removal
// path / FruitFactControl::Release child-button teardown / etc.).
// Port now matches that lifecycle: dtor empty, Release() driven by
// caller. Implicit member-subobject destructors take over for
// m_AddOns / Delegates / ~HUDControl3d.
MenuButton::~MenuButton() {}

// Matches MenuButton::Init (0x0014ee40, 222 lines)
void MenuButton::Init(Vec3 buttonPos, Mortar::Delegate0<void> clickCb,
                      int fruitType, Vec3 hitBounds,
                      Mortar::Delegate0<void> deletedCb) {
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
    m_Timer = 0.0f;        // DAT_0014ee68
    m_bHighlighted = 1;    // DAT_0014ee6c

    // Create fruit entity if fruitType >= 0 (toggles use -1)
    if (fruitType >= 0) {
        Game* game = Game::GetInstance();
        if (game && game->actorManager) {
            // Original: entityType = (FruitInfo_GetCount() <= fruitType) ? 1 : 0
            int bombThreshold = FruitInfo_GetCount();
            int entityType = (bombThreshold <= fruitType) ? 1 : 0;  // 0=Fruit, 1=Bomb
            printf("[MenuButton] Init: fruitType=%d bombThreshold=%d -> entityType=%d\n",
                   fruitType, bombThreshold, entityType);
            Mortar::Entity* e = game->actorManager->Add(entityType, true);
            if (e) {
                e->pos = buttonPos;
                e->Init(nullptr, (long)fruitType, nullptr);
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
                    // Binary @ 0x0014f0dc..0x0014f0ea: copy clamped RotVel1 into RotVel2 so
                    // both halves spin identically. Without this the two halves of a menu
                    // fruit have slightly different rotational velocities once sliced.
                    fruit->m_RotVel2 = fruit->m_RotVel1;
                } else {
                    // Bomb entity: disable physics and scale by 0.85 (DAT_0014f1a0)
                    // MenuButton::Init (0x0014ee40): writes 0 to bomb+0x80 (m_bMovement)
                    // then bomb->scale *= 0.85, then calls Bomb::SetCallback.
                    Bomb* bomb = static_cast<Bomb*>(e);
                    bomb->m_bMovement = 0;
                    bomb->scale = bomb->scale * BOMB_MENU_SCALE;
                    bomb->SetCallback(clickCb);
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

        m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;  // menu draw layer
    }
}

// Binary @ 0x0014f7e0 — clears entity backrefs, deletes labels, calls DeletePeices()
// ASM-verified: 2026-04-29T00:00Z binary @ 0x0014f7e0 (asm-inspector)
void MenuButton::Release() {
    Mortar::Entity* e = m_pFruitPiece;
    if (e) {
        int bombThreshold = FruitInfo_GetCount();
        if (m_FruitType < bombThreshold) {
            // Binary writes 0 to fruit+0x108. The port's +0x108 slot is
            // modeled as m_pSlasher (SlashEntity backref); clearing it is
            // semantically equivalent to the binary's "drop owning ref"
            // intent on menu-button release.
            static_cast<Fruit*>(e)->m_pSlasher = nullptr;
        } else if (m_FruitType == bombThreshold) {
            static_cast<Bomb*>(e)->m_pOwnerButton = nullptr;
        }
    }
    // m_pLabel1 / m_pLabel2 are always nullptr (SetText has no callers, see
    // dead-code note below). Skip deletes -- they would be undefined behavior
    // on void* if ever non-null, and they're never set in the first place.
    m_pLabel1 = nullptr;
    m_pLabel2 = nullptr;
    DeletePeices();
    // Binary @ 0x0014f7e0 -- ~SmartPtr<Texture> drop on m_SecondaryTex.
    m_SecondaryTex.SetNull();
    m_pEntity = nullptr;
    m_pFruitPiece = nullptr;
}

// Binary @ 0x0014e3ac — vtable Init slot, calls vtable Reset (no-op for MenuButton)
void MenuButton::Init() { Reset(); }

// Binary @ 0x0014e3b8 — Reset is a no-op (vtable slot +0x10)
// ASM-verified: 2026-05-06T00:00 binary @ 0x0014e3b8 (asm-inspector)
// Binary body is a single `bx lr` -- empty; port's empty body matches.
void MenuButton::Reset() {}

// Binary @ 0x0014e3f8 — vtable Skip slot, snaps grow-in to full (m_FadeCounter=0x3ffc)
void MenuButton::Skip() { m_FadeCounter = 0x3ffc; }

// Binary @ 0x0014e3bc — sets m_ShakeTimer (zero call sites in binary)
void MenuButton::Shake(float t) { m_ShakeTimer = t; }

// Binary @ 0x0014e434 — returns (m_NewIndicatorTimer >= 0)
bool MenuButton::HasNewSymbol() { return m_NewIndicatorTimer >= 0.0f; }

// Binary @ 0x0014e484 — returns (m_SparkleTimer >= 0); dead in shipped binary
bool MenuButton::IsLoadingSymbol() { return m_SparkleTimer >= 0.0f; }

// Binary @ 0x0014e45c — arms sparkle timer; dead in shipped binary
void MenuButton::SetLoadingSymbol(bool show) {
    if (m_SparkleTimer < 0.0f) {
        if (show) m_SparkleTimer = 0.0f;
    } else {
        if (!show) m_SparkleTimer = -1.0f;
    }
}

// Defunct: SetText -- no-op in port; binary @ 0x0014ebc0
//
// Binary builds two `Mortar::BakedString` instances (foreground + shadow)
// arranged on a curved arc of the given `radius`, then stores the pair
// in m_pLabel1 / m_pLabel2 for the (separately-Defunct) label-draw block
// at 0x0015015e. Both the SetText constructor path AND the label-draw
// block have zero call sites in the shipped binary -- the curved-text
// feature was authored but never wired into any released menu button.
//
// Port keeps the empty body for call-graph parity (so anything the
// implementer ever wires would compile and link); not RE-ported in
// detail because the work would land 100% dead code. The port-side
// m_pLabel1 / m_pLabel2 fields are typed `void*` for the same reason
// (no BakedString allocations exist).
void MenuButton::SetText(const char* /*text*/, Colour /*fg*/,
                         Colour /*shadow*/, float /*radius*/) {
}

// Binary @ 0x0014ed18 — release fruit piece with upward fling; dead in shipped binary
void MenuButton::Remove() {
    if (!m_pFruitPiece) return;
    if (m_pFruitPiece->m_bSliced) return;
    m_pFruitPiece->m_bDrawWhole = true;
    float vx = RandScaled(10.0f) - 5.0f;
    float vy = -(RandScaled(5.0f));
    m_pFruitPiece->vel = Vec3(vx, vy, 0.0f);
    m_pFruitPiece->m_SecondVel = m_pFruitPiece->vel;
    m_pEntity = nullptr;
    m_pFruitPiece = nullptr;
}

// Binary @ 0x0014e5cc — fires m_ClickCallback (toggles only) + m_DeletedCallback (always)
bool MenuButton::TouchReleased() {
    if (m_FruitType < 0 && m_bVisible) {
        m_ClickCallback();
    } else if (m_pEntity != nullptr) {
        // Binary @ 0x0014e5e6 — TutorialControl::ButtonPressedAtPos(this).
        Game* game = Game::GetInstance();
        if (game && game->pTutorialCtrl) {
            game->pTutorialCtrl->ButtonPressedAtPos(this);
        }
    }
    m_DeletedCallback();
    return true;
}

// Binary @ 0x0014e44c — vtable BeginDraw slot; re-arms m_LayerFlags=0x40 each frame for fruit-type buttons
void MenuButton::BeginDraw(float dt) {
    (void)dt;
    if (m_FruitType >= 0) {
        m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    }
}

// Binary @ 0x0014e448 — vtable PreDraw slot, no-op
void MenuButton::PreDraw(const Vec3& hudScale) { (void)hudScale; }

// Binary @ 0x0014e590 — kills owned fruit/bomb then defers to base SetToMultiplayerState
bool MenuButton::SetToMultiplayerState() {
    Mortar::Entity* e = m_pFruitPiece ? static_cast<Mortar::Entity*>(m_pFruitPiece) : m_pEntity;
    if (e) {
        if (e->entityType == 0) {
            // Binary @ 0x0014e5a8 — Fruit::KillFruit(false) (no miss penalty
            // when the menu transitions to MP, just remove the fruit).
            static_cast<Fruit*>(e)->KillFruit(false);
        } else if (e->entityType == 1) {
            // Binary @ 0x0014e5b6 — Bomb::KillBomb().
            static_cast<Bomb*>(e)->KillBomb();
        }
    }
    m_pEntity = nullptr;
    m_pFruitPiece = nullptr;
    return HUDControl::SetToMultiplayerState();
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

// Matches MenuButton::Update (0x0014e614).
// Critical: at the END of Update, the binary writes
//   m_BackdropScale = size.x * 1.125f * m_AnimScale
// at 0x0014eb84 (read at 0x0014fa86 by Draw Phase A). This makes the
// scratchs.tex backdrop quad LIVE every frame; without this write, the
// quad collapses to a point and the backdrop appears invisible.
// ASM-verified: 2026-05-06T17:50 binary @ 0x0014eb84 (asm-inspector)
void MenuButton::Update(float dt) {
    UpdatePeices(dt);

    // Hardware Back/Menu key auto-fire. Binary @ 0x0014e9a8: when
    // m_bHighlighted && Game::m_BackKeyPressed && m_bRespondsToBackKey,
    // simulate slice (Fruit) or fire click delegate (Bomb). The port
    // doesn't track Game::m_BackKeyPressed yet (TODO), so the gate is
    // currently dead; the field still records the per-screen "this is
    // the default Back action" wiring set by screen creation code.
    // (Removed the previous fictional alpha-fade-on-m_bRemovalPending
    // block — the binary has no such behaviour. See docs/engine/menubutton-138.md.)

    // Sparkle timer tick — binary @ 0x0014e644 advances at 8/sec and wraps
    // at 8.0. SetLoadingSymbol (the only arming function) has zero call
    // sites so the timer is permanently at -1.0; no need to tick.

    // New-item-star timer tick. Binary @ 0x0014e644:
    //   if (timer >= 0): timer += 2*dt; if (m_SparkleTimer >= 1.0) timer = 0.
    // The sparkle phase-reset keeps the star and sparkle visually in sync.
    if (m_NewIndicatorTimer >= 0.0f) {
        m_NewIndicatorTimer += dt + dt;          // += 2*dt
        if (m_SparkleTimer >= 1.0f) {
            m_NewIndicatorTimer = 0.0f;
        }
    }

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
    //   if (m_pEntity != nullptr):
    //       if (entity->m_bSliced != 0):              # released by ClearMenuItems
    //           if (|vel|² > 0.001):                  # actually moving
    //               fire m_ClickCallback once
    //               ClearMenuItems()
    //               m_pEntity = nullptr                  # detach
    //       else:
    //           pin entity to button center (vel=0)
    //
    //   if (m_pEntity == nullptr):
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
            // Menu bombs start with m_bMovement=0 (pinned). ClearMenuItems
            // flips m_bMovement=1 + disables collision when a sibling
            // button is sliced — that signals release. m_bHit fires for
            // user-touched bombs (separate path). Either condition counts.
            released = (bomb->m_bHit != 0) || (bomb->m_bMovement != 0);
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
            // Mortar::Entity scale tracks the same ratio so fruit zooms together
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
            // Released path is split by entity type — the binary has two
            // distinct sub-paths gated by m_FruitType < FruitInfo_GetCount().
            if (m_pEntity->entityType == 0) {
                // ASM-verified: 2026-04-30 binary @ 0x0014e74a..0x0014e7ec (asm-inspector)
                // --- Fruit branch (binary 0x0014e74a..0x0014e7ec) ---
                //
                // Binary @ 0x0014e752: m_HalfB_pos = button.pos every frame.
                // Anchor for slice-physics references; harmless to overwrite
                // when fruit is mid-slice (Fruit::Update overwrites it again).
                if (m_pFruitPiece) {
                    m_pFruitPiece->m_SecondPos = pos;
                }

                // Structure (per ARM trace; Ghidra's nested-if obscures it):
                //   entity->m_HalfB_pos = pos          // unconditional
                //   if (entity->m_bSliced != 0) {
                //       Vec3 diff = entity->vel - entity->m_HalfB_vel
                //       if (|diff|^2 > 0.001f) {
                //           click callback; ResetTutePos; restore scale;
                //           if (vel.x==0 && vel.y==0) m_bDrawWhole = 1
                //           if (m_bEnabled) ClearMenuItems + OnMenuItemsCleared
                //       }
                //       m_pEntity = nullptr;            // UNCONDITIONAL after gate
                //   }
                //
                // The gate-fail path (sibling fruit released by ClearMenuItems
                // — its vel == m_HalfB_vel after the cascade write, so diff = 0)
                // SKIPS the click + ClearMenuItems but still detaches the
                // entity, so the m_pEntity == null branch above starts the
                // FadeCounter shrink next frame. Previously port wrapped the
                // detach inside the gate, leaving siblings pinned to their
                // entity forever.
                Vec3 relVel = m_pEntity->vel;
                if (m_pFruitPiece) {
                    relVel.x -= m_pFruitPiece->m_SecondVel.x;
                    relVel.y -= m_pFruitPiece->m_SecondVel.y;
                    relVel.z -= m_pFruitPiece->m_SecondVel.z;
                }
                const float relVelSqMag = relVel.x * relVel.x +
                                          relVel.y * relVel.y +
                                          relVel.z * relVel.z;

                // DAT_0014e978 = 0x3a83126f = 0.001f
                if (relVelSqMag > 0.001f) {
                    // Binary @ 0x0014e76c: Mortar::Delegate0::operator()(&field7_0x88).
                    if (m_ClickCallback) {
                        auto cb = m_ClickCallback;
                        m_ClickCallback = nullptr;
                        cb();
                    }
                    // Binary @ 0x0014e7c0: restore entity scale from m_HitBoundsScale
                    if (m_pFruitPiece) {
                        m_pFruitPiece->scale = m_HitBoundsScale;
                    }
                    // Binary @ 0x0014e7d0: stationary-fruit-piece edge case
                    if (m_pFruitPiece && m_pFruitPiece->entityType == 0 &&
                        m_pFruitPiece->vel.x == 0.0f && m_pFruitPiece->vel.y == 0.0f) {
                        m_pFruitPiece->m_bDrawWhole = true;
                    }
                    // Binary @ 0x0014e7e0: ClearMenuItems gated by m_bEnabled.
                    // ShrinkBuyButton sets m_bEnabled=0 to skip this for the
                    // programmatic-shrink path.
                    if (m_bEnabled != 0) {
                        FN::ClearMenuItems();
                        // Binary @ 0x0014e7e8 — MainScreen::OnMenuItemsCleared.
                        // Empty in binary (single bx lr); port matches via the
                        // explicit no-op call so the call-graph stays parity.
                        Game* game = Game::GetInstance();
                        if (game && game->mainScreen) {
                            game->mainScreen->OnMenuItemsCleared();
                        }
                    }
                }
                // Binary @ 0x0014e7ec: detach unconditionally inside the
                // m_bSliced branch. m_pFruitPiece stays valid so the
                // m_pEntity==null shrink branch can keep scaling the fruit
                // alongside the ring.
                m_pEntity = nullptr;
            } else {
                // --- Bomb branch (binary @ 0x0014e7f4..0x0014e81e) ---
                // Bombs do NOT fire MenuButton's click callback or call
                // ClearMenuItems from here. The user-hit click runs through
                // Bomb::m_HitCallback (set in MenuButton::Init bomb path).
                // Detach gate is purely Bomb::Enabled() == 0 -- once the bomb
                // finishes its own hit/explode sequence, restore the original
                // scale and detach so the ring shrink curve can run.
                //
                // Binary writes pos.z and entity Z-pos every frame in this
                // path (not just on detach). The values keep the bomb on
                // the menu Z-plane.
                Bomb* bomb = static_cast<Bomb*>(m_pEntity);
                pos.z = -5.0f;
                bomb->pos.z = 150.0f;
                if (!bomb->Enabled()) {
                    bomb->scale = m_HitBoundsScale;
                    // Binary does NOT write m_FadeCounter on detach -- it
                    // falls through to the grow-clamp at 0x3ffc and lets
                    // the next-frame m_pEntity==null shrink handle the
                    // ramp-down naturally. Don't force-write FadeCounter.
                    m_pEntity = nullptr;
                    m_pFruitPiece = nullptr;
                }
            }
        }
    }

    // If the entity was deactivated externally (e.g. FN::ClearMenuItems
    // disables a bomb before MenuButton could detect the slash), force
    // the detach here so the shrink path below can run.
    if (m_pEntity && !m_pEntity->IsActive()) {
        m_pEntity = nullptr;
        m_pFruitPiece = nullptr;
        if (m_FadeCounter == 0) m_FadeCounter = 0x3ffc;
    }

    if (m_pEntity == nullptr && m_FadeCounter > 0) {
        // Shrink-to-disappearance phase. Binary DAT_0014e97c = 109200.0
        // (0x47d547ff). Per-second decrement rate; over a 60Hz tick that's
        // ~1820 counts/frame -> ~9 frames from 0x3ffc (16380) to 0.
        m_FadeCounter -= (int)(dt * 109200.0f);
        if (m_FadeCounter < 1) {
            m_FadeCounter = 0;
            m_bPendingRemoval = 1;
        }
        const float counterRad = (float)m_FadeCounter * (6.2831853f / 65536.0f);
        const float scaleFrac  = sinf(counterRad);
        size = m_TargetSize * scaleFrac;

        // Binary MenuButton::Update m_pEntity==null path also scales
        // m_pFruitPiece IF the fruit is stationary (vel.x==0 && vel.y==0).
        // This is the ShopScreen equip-button case where EquipCallback's
        // shrink branch zeros vel/m_Gravity -- the fruit then shrinks
        // proportionally to the ring.
        // The binary's else branch (vel != 0) sets scale = m_HitBoundsScale,
        // but for general menu fruits sliced by the user, that overrides
        // the fruit's own per-frame scale animation. The port skips the
        // else branch so user-sliced halves keep their own scale.
        if (m_pFruitPiece &&
            m_pFruitPiece->vel.x == 0.0f && m_pFruitPiece->vel.y == 0.0f) {
            float ratio = (m_TargetSize.x != 0.0f)
                          ? (size.x / m_TargetSize.x) : 0.0f;
            m_pFruitPiece->scale = m_HitBoundsScale * ratio;
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
    //
    // ASM-verified: 2026-05-06T00:00 binary @ 0x0014e994..0x0014e99a (asm-inspector)
    // Binary gates the entire touch + back-key block on m_bHighlighted (+0x131):
    //   ldrb.w r3, [r4, #0x131]
    //   cmp    r3, #0
    //   beq.w  0x0014eb52     ; jump to size-sync / backdrop-scale tail
    // The earlier port-side gate `if (!m_bInteractive || !m_bEnabled) return;`
    // (a) tested fields the binary doesn't, and (b) returned from the entire
    // function — bypassing the m_BackdropScale tail (binary @ 0x0014eb84).
    // -----------------------------------------------------------------------
    if (m_bHighlighted != 0) {
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
                // ASM-verified: 2026-05-06T17:50 binary @ 0x0014e6a4 (asm-inspector)
                // Binary calls TouchReleased() on inside-release, which fires
                // both m_ClickCallback (for toggles only, m_FruitType<0) AND
                // m_DeletedCallback unconditionally. Earlier port fired only
                // m_ClickCallback directly and skipped m_DeletedCallback.
                if (insideOnRelease) {
                    TouchReleased();
                }
                m_TouchSlot = -1;
                m_bHighlighted = 0;
            }
        }
    }

    // ASM-verified: 2026-05-09 binary @ 0x0014eb84 (re-analyst).
    // m_BackdropScale (+0xEC) = size.x * 1.125f * m_AnimScale, written
    // every Update. Read by Draw Phase A @ 0x0014fa86 to scale the
    // scratchs.tex backdrop quad.
    //
    // Update writes size = m_TargetSize (or scaled grow-in version)
    // earlier in this function for fruit-typed buttons too, so size.x
    // here is the just-written m_TargetSize.x. The scratchs backdrop
    // is intentionally rendered for ALL MenuButtons (fruit + toggle).
    //
    // Per-button scaling override: only the big "NEW GAME" button has
    // m_AnimScale = 0.5 (set by MainScreen on pPlayButton creation,
    // binary @ 0x0014b82c). All others stay at the Init default 1.0.
    m_BackdropScale = size.x * 1.125f * m_AnimScale;
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
//
// Two-phase layer dance (binary @ 0x0014fa24..0x0014fa2c):
//   Frame 1 at layer 0x40: renders the round fruit-icon backdrop, then
//     demotes m_LayerFlags to 0x80 so subsequent draws fall in the later
//     HUD::Draw(0x80) bucket — AFTER SplatEntity::DrawActiveSplats and
//     BombBlast::DrawActiveBlasts. Without this, the button sprite would
//     forever render before splats and get covered by them.
//   Frame N (layer 0x80): renders the actual sprite + new-indicator + sparkle.
//
// Port currently stubs the dedicated backdrop pass; the demotion alone
// is enough to fix the visible "splat covers menu button" glitch since
// the sprite then renders at 0x80 after splats. The backdrop quad TODO
// stays for full fidelity.
void MenuButton::Draw(const Vec3& hudScale, int layerMask) {
    if (!m_bVisible || m_DrawColour.a == 0) return;

    {
        static int s_dbgCount = 0;
        if ((s_dbgCount++ % 60) == 0) {
            fprintf(stderr, "[DBG MenuBtn] pos=(%.1f,%.1f,%.1f) size=(%.1f,%.1f,%.1f) "
                    "tgt=(%.1f,%.1f,%.1f) bkS=%.2f animS=%.2f fade=%d layer=0x%x fruitType=%d\n",
                    pos.x, pos.y, pos.z, size.x, size.y, size.z,
                    m_TargetSize.x, m_TargetSize.y, m_TargetSize.z,
                    m_BackdropScale, m_AnimScale, m_FadeCounter,
                    m_LayerFlags, m_FruitType);
        }
    }

    // Compute fade-derived alpha once. Used by Phase A AND Phase B.
    // Binary @ entry of Draw, before the layer test.
    //   alpha = m_FruitType < 0 ? 0xFF
    //                          : clamp(m_FadeCounter * 256 / 16380, 0, 255)
    uint8_t alpha;
    if (m_FruitType < 0) {
        alpha = 0xFF;
    } else {
        float n = (float)m_FadeCounter * 256.0f / 16380.0f;
        int   a = (int)n;
        if (a > 254) a = 0xFF;
        if (a < 0)   a = 0;
        alpha = (uint8_t)a;
    }

    // ASM-verified: 2026-05-06T16:00 binary @ 0x0014f9cc Phase A
    // 0x0014fa24..0x0014faf8 (asm-inspector).
    // First-pass at layer 0x40: scratchs.tex backdrop quad, then demote
    // to 0x80 and return. Backdrop scale comes from m_BackdropScale
    // (+0xEC), computed every Update as size.x * 1.125f * m_AnimScale.
    // For fruit-typed buttons the binary leaves size = (0,0,0) so this
    // collapses to a point. Toggle buttons (sound/music) override size
    // from texture w/h+1 and render a real scratchs backdrop here.
    if (m_LayerFlags == (int)Mortar::HUD_LAYER_MENU_BG) {
        m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;
        if (s_TexScratchs.IsValid()) {
            MatrixManager& mm = MatrixManager::GetInstance();
            Renderer* r = Renderer::GetInstance();
            if (r) {
                // Mirror flip via X scale (m_bFlipped chosen randomly in Init).
                const float sx = m_bFlipped ? -1.0f : 1.0f;

                // Binary @ 0x0014fa86: Scale44((sx, 1, 1) * m_BackdropScale).
                // Z = -5500 (DAT_0014fcf8) puts the quad deep in the ortho
                // frustum.
                Matrix44 mat = Matrix44::MakeScale(sx * m_BackdropScale,
                                                   m_BackdropScale,
                                                   m_BackdropScale);
                mat.GlobalTranslate44(Vec3(pos.x, pos.y, -5500.0f));
                mm.GetWorldStack().Reset();
                mm.GetWorldStack().SetCurrentMatrix(mat);
                mm.UploadModelViewOnly();

                // Binary @ 0x0014faf0: DrawQuadSized(0.0, 1.0, 0.0, 1.0, &tint)
                // -- four floats are UV bounds (uMin, uMax, vMin, vMax), not
                // halfW/halfH. Geometry is unit-quad (-0.5..+0.5) inside DrawQuad.
                Colour tint(255, 255, 255, alpha);
                s_TexScratchs->Set();
                r->DrawQuad(tint, 0.0f, 0.0f, 1.0f, 1.0f);
                s_TexScratchs->UnSet();
            }
        }
        return;
    }

    // Subsequent passes (layer 0x80 etc.): render the actual button sprite.
    // Original applies shake offset if m_ShakeTimer > 0 (random ±3.0).
    if (m_ShakeTimer > 0.0f) {
        Vec3 savedPos = pos;
        pos.x += ((float)(rand() % 600) / 100.0f) - 3.0f;
        pos.y += ((float)(rand() % 600) / 100.0f) - 3.0f;
        HUDControl3d::Draw(hudScale, layerMask);
        pos = savedPos;
    } else {
        HUDControl3d::Draw(hudScale, layerMask);
    }

    // Layer 2: "New item" star indicator. Binary @ 0x0014fd18..0x0014fe98.
    // Constants verified by re-analyst pass — see docs/structs/gameplay-misc.md.
    //   Quad: 64 x 32 px, scaled by ratio = size.x / m_TargetSize.x (parent fade-in).
    //   Anchor: pos + ratio * (m_BounceParams.x * size.x * 0.5,
    //                          |sin(timer * 32760)| * 6 + m_BounceParams.y * size.y * 0.5,
    //                          0)
    //   Tint:  m_bHighlighted ? white : grey(128); alpha = m_DrawColour.a (parent fade).
    if (m_NewIndicatorTimer >= 0.0f && s_TexNewItem.IsValid()
        && m_TargetSize.x != 0.0f)
    {
        MatrixManager& mm = MatrixManager::GetInstance();
        Renderer* r = Renderer::GetInstance();
        if (r) {
            const float ratio = size.x / m_TargetSize.x;
            const uint16_t phase =
                (uint16_t)(m_NewIndicatorTimer * 180.0f * 182.0f);
            const float s = SinIdx(phase);
            const float by = (s < 0.0f ? -s : s) * 6.0f;

            // Binary @ 0x0014fdf4 reads m_TargetSize (+0x124..+0x128), NOT
            // size (HUDControl base +0x20). Using size makes the anchor
            // shrink during the grow-in animation; binary keeps the anchor
            // fixed at the target size and only the QUAD scales via ratio.
            Vec3 off(m_BounceParams.x * m_TargetSize.x * 0.5f,
                     by + m_BounceParams.y * m_TargetSize.y * 0.5f,
                     0.0f);
            off = off * ratio;
            Vec3 drawAt = pos + off;

            mm.GetWorldStack().Reset();
            // Binary @ 0x0014fdf8: Scale44(ratio*64, ratio*32, 0.0f).
            // DAT_00150044 = 0.0f for the Z-scale; the geometry's z is unused
            // (subsequent GlobalTranslate writes the final z=pos.z).
            Matrix44 mat = Matrix44::MakeScale(ratio * 64.0f,
                                               ratio * 32.0f,
                                               0.0f);
            mat.GlobalTranslate44(drawAt);
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();

            const uint8_t a = m_DrawColour.a;
            Colour tint = m_bHighlighted
                ? Colour(255, 255, 255, a)
                : Colour(128, 128, 128, a);

            s_TexNewItem->Set();
            r->DrawQuad(tint, 0.0f, 0.0f, 1.0f, 1.0f);
            s_TexNewItem->UnSet();
        }
    }

    // Layer 3: Sparkle ring — INTENTIONALLY OMITTED. Per RE pass 2026-04-29,
    // MenuButton::SetLoadingSymbol(true) (0x0014e45c) is the only writer
    // that arms m_SparkleTimer to a non-negative value, and it has ZERO
    // call sites in the shipped binary. The 48-vertex spike-ring at
    // 0x0014fa3e geometry / colour cycle / DrawTriList is preserved in
    // the binary but never fires. Port matches by leaving the timer at
    // -1.0 and skipping the Draw block.
    //
    // Layer 4: Text labels — INTENTIONALLY OMITTED. Same situation:
    // MenuButton::SetText (0x0014ebc0) has zero call sites. m_pLabel1 /
    // m_pLabel2 are always NULL; label-draw block at 0x0015015e is dead.
    // (See docs/structs/gameplay-misc.md MenuButton "dead code" notes.)
}

// ASM-verified: 2026-05-06T00:00 binary @ 0x0014f674 (asm-inspector)
// Matches MenuButton::LoadContent @ 0x0014f674 — loads three shared textures
// into class statics in the binary's order: scratchs.tex (backdrop),
// blurry_backing.tex (sparkle), new_item.tex (NEW star). Binary calls
// LoadLocalisedTexture unconditionally for each slot — idempotency is the
// caller's contract (LoadContent runs once at content-init time). Earlier
// port had `if (!s_TexX.IsValid())` guards around each load that the
// binary doesn't have; removed.
void MenuButton::LoadContent() {
    s_TexScratchs      = Mortar::TextureManager::LoadLocalisedTexture("scratchs.tex");
    s_TexBlurryBacking = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    s_TexNewItem       = Mortar::TextureManager::LoadLocalisedTexture("new_item.tex");
}

// ASM-verified: 2026-05-06T00:00 binary @ 0x0014f718 (asm-inspector)
// Three SmartPtr clears in load-order. Port matches binary 1:1; the
// asm-verify report's "341% diff" was an address-mapping artefact
// (scored against `DeletedMenuButton`'s body at 0x0013f6ac instead of
// the true UnLoadContent at 0x0014f718).
void MenuButton::UnLoadContent() {
    s_TexScratchs.SetNull();
    s_TexBlurryBacking.SetNull();
    s_TexNewItem.SetNull();
}

// Binary @ 0x00150240 — spawn child HUDControl3d sprite, attach to HUD + m_AddOns list,
// callback DeletedPeice on removal.
void MenuButton::AddPeice(Mortar::SmartPtr<Mortar::Texture> tex, Vec2* uvOverride,
                          float rotSpeed, float initialTimer,
                          Vec3 offset, Vec3 sizeScale,
                          Colour tint, int layerFlags) {
    HUDControl3d* c = new HUDControl3d();
    c->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(
        this, &MenuButton::DeletedPeice);
    c->m_LayerFlags  = layerFlags;
    c->m_DrawColour  = tint;
    if (uvOverride) {
        c->m_UVLeft   = uvOverride->x;
        c->m_UVTop    = uvOverride->y;
        // UV rect override: assume (u0,v0) from Vec2; span defaults to 1
        c->m_UVRight  = uvOverride->x + 1.0f;
        c->m_UVBottom = uvOverride->y + 1.0f;
    }
    // Binary @ 0x00150240: when sizeScale is "auto" ((0,0,z)), the binary
    // sets the AddOn's size from the texture dimensions scaled by the UV
    // span and the .z scale factor. Default UV span is (1,1) when no
    // override was supplied above.
    if (sizeScale.x == 0.0f && sizeScale.y == 0.0f) {
        if (sizeScale.z == 0.0f) sizeScale.z = 1.0f;
        const float uSpan = c->m_UVRight  - c->m_UVLeft;
        const float vSpan = c->m_UVBottom - c->m_UVTop;
        const float texW  = tex.IsValid() ? (float)tex->m_Width  : 0.0f;
        const float texH  = tex.IsValid() ? (float)tex->m_Height : 0.0f;
        sizeScale = Vec3(texW * uSpan * sizeScale.z,
                         texH * vSpan * sizeScale.z,
                         0.0f);
    }
    // Binary @ 0x00150240: store the texture SmartPtr on the AddOn's
    // m_SecondaryTex slot (HUDControl3d +0x78, Mortar::SmartPtr<Texture>).
    c->m_SecondaryTex = tex;
    c->pos   = pos;
    c->m_Timer = initialTimer;
    c->size  = size;

    Game* game = Game::GetInstance();
    if (game && game->hud) {
        game->hud->AddControl(c, false);
    }

    MenuButtonAddOn addOn;
    addOn.control   = c;
    addOn.texCoord  = uvOverride;
    addOn.offset    = offset;
    addOn.sizeScale = sizeScale;
    // NOTE: rotSpeed consumed via offset.y as per-frame angular velocity by UpdatePeices
    (void)rotSpeed;
    (void)tex;
    m_AddOns.push_back(addOn);
}

// Binary @ 0x0014e49c — per-addon: m_Timer += dt * offset.y; pos = parent.pos+offset*ratio;
// size = sizeScale * ratio
void MenuButton::UpdatePeices(float dt) {
    float ratio = (m_TargetSize.x > 0.0f) ? (size.x / m_TargetSize.x) : 1.0f;
    for (std::list<MenuButtonAddOn>::iterator it = m_AddOns.begin();
         it != m_AddOns.end(); ++it) {
        HUDControl3d* c = it->control;
        if (!c) continue;
        c->m_Timer += dt * it->offset.y;
        c->pos = pos + it->offset * ratio;
        c->size = it->sizeScale * ratio;
    }
}

// Binary @ 0x0014f74c — detach addons' remove callbacks, mark each for HUD removal,
// clear m_AddOns
void MenuButton::DeletePeices() {
    for (std::list<MenuButtonAddOn>::iterator it = m_AddOns.begin();
         it != m_AddOns.end(); ++it) {
        HUDControl3d* c = it->control;
        if (c) {
            c->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>();
            c->m_bPendingRemoval = 1;
        }
    }
    m_AddOns.clear();
}

// Binary @ 0x0014e54c — addon's HUD-side removal callback; erase matching entry from m_AddOns
void MenuButton::DeletedPeice(HUDControl* hudControl) {
    for (std::list<MenuButtonAddOn>::iterator it = m_AddOns.begin();
         it != m_AddOns.end(); ++it) {
        if (it->control == hudControl) {
            m_AddOns.erase(it);
            return;
        }
    }
}

// Removed: HitTest, TouchDown, TouchUp. Touch input is now polled inside
// MenuButton::Update via Mortar::Touch::GetTouchInRegion — matching the
// binary's poll-based flow.
