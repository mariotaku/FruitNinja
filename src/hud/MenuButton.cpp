//
// MenuButton : HUDControl3d (0x178 bytes, v1.6.1)
// Binary ctors @ 0x0019bb08 / 0x0019bcac / 0x0019be50 / 0x0019bff8
// Binary Init  @ 0x0019b994
// Binary Update@ 0x0019a860
// Binary Draw  @ 0x0019c2e4
//

// Analysed: 2026-04-28T14:00
#include "MenuButton.h"
#include "debug/Logger.h"
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
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include "game/GameWork.h"

// Class-static textures loaded by MenuButton::LoadContent (binary @ 0x0014f674).
//   Slot 1  scratchs.tex        (Phase-A backdrop)
//   Slot 2  blurry_backing.tex  (sparkle ring base)
//   Slot 3  new_item.tex        (Layer-2 NEW star)
static Mortar::SmartPtr<Mortar::Texture> s_TexScratchs;
static Mortar::SmartPtr<Mortar::Texture> s_TexBlurryBacking;
static Mortar::SmartPtr<Mortar::Texture> s_TexNewItem;

// Matches ClearMenuItems @ 0x0016ac7c -- binary-exact.
static float RandScaled(float s) {
    return ((float)rand() / (float)RAND_MAX) * s;
}

void FN::ClearMenuItems() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // --- Pass 1: fruits (type 0) ---
    {
        std::list<Mortar::Entity*>::iterator it;
        Mortar::Entity* e = am->GetEntityFirst(0, it);
        while (e != nullptr) {
            Fruit* f = static_cast<Fruit*>(e);
            if (f->m_bSliced == 0) {
                float vx = RandScaled(10.0f) - 5.0f;
                float vy = RandScaled(5.0f);
                f->vel = Vec3(vx, vy, 0.0f);
                f->m_bDrawWhole = true;
                const float absVx = vx < 0 ? -vx : vx;
                const float sign  = (f->pos.x < 0.0f) ? -1.0f : 1.0f;
                f->vel.x = absVx * sign;
                f->m_SecondVel = f->vel;
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

// Constants from binary
static const float FRUIT_ROTVEL_MULT = 0.2f;       // DAT_0014f194
static const float FRUIT_ZPOS        = 150.0f;     // DAT_0014f198
static const float BOMB_MENU_SCALE   = 0.85f;      // DAT_0014f1a0
static const float ROT_CLAMP_X      = 0.75f;
static const float ROT_CLAMP_Y      = 0.5f;

MenuButton::MenuButton()
    : m_pFruitPiece_alt(nullptr),
      m_pEntity(nullptr),
      m_FruitType(-1),
      m_FadeAlphaIdx(0),
      m_AnimPhase(0),
      m_AnimFlag(0),
      m_GrowShrinkDone(0),
      m_fieldD4(static_cast<int>(0xffffffff)),
      m_TouchSlot(-1),
      m_TouchX(0.0f), m_TouchY(0.0f), m_TouchPhase(0.0f),
      m_BackdropOffsetX(0.0f),
      m_BackdropScale(0.0f),
      m_RandomOffset(0.0f),
      m_RotationSpeed(-1.0f),
      m_SparkleTimer(-1.0f),
      m_NewIndicatorTimer(-1.0f),
      m_BaseScale(0.0f, 0.0f, 0.0f),
      m_pLabelFg(nullptr), m_pLabelExtra(nullptr), m_pLabelShadow(nullptr),
      m_PlayerColour(255, 255, 255, 255),
      m_PlayerIndexTint(255, 255, 255, 255),
      m_GrowInTimer(0.0f),
      m_bRespondsToBackKey(0),
      m_bDragCancel(0),
      m_bClearsMenuItems(0),
      _pad13B(0),
      m_RestScale(0.0f, 0.0f, 0.0f),
      m_bHasHitArea(0),
      m_bInteractive(1),
      m_pFruitPiece(nullptr),
      m_bAcceptsTouch(1),
      _pad149{0, 0, 0},
      m_pTrackedFruit(nullptr),
      m_bBackdropActive(1),
      m_ShakeScale(1.0f, 0.85f, 0.85f),
      m_LabelExtraAlpha(0.0f),
      m_HitInsetX(5.0f),
      m_HitInsetY(5.0f),
      m_fieldReserved(100.0f),
      m_NewBouncePhase(0.0f),
      m_ShakeTimer(0.0f),
      m_bEnabled(1),
      m_AnimScale(1.0f),
      m_BounceParams(0.85f, 0.85f, 0.0f),
      m_bTouchHeld(1),
      m_bScoreSubmitted(0)
{
    _pad114[0] = 0; _pad114[1] = 0; _pad114[2] = 0; _pad114[3] = 0;
    _pad114[4] = 0; _pad114[5] = 0; _pad114[6] = 0; _pad114[7] = 0;
    _pad130[0] = 0; _pad130[1] = 0; _pad130[2] = 0; _pad130[3] = 0;
    _pad146[0] = 0; _pad146[1] = 0;
    _pad151[0] = 0; _pad151[1] = 0; _pad151[2] = 0;
}

MenuButton::MenuButton(Mortar::SmartPtr<Mortar::Texture>* tex, Vec3* spawnPos,
                       Mortar::Delegate0<void>* onTap,
                       int fruitType, Vec3* restPos,
                       Mortar::Delegate1<void, HUDControl*>* onRemove)
{
    (void)tex;
    Init(*spawnPos,
         onTap ? *onTap : Mortar::Delegate0<void>(),
         fruitType,
         restPos ? *restPos : Vec3(0.0f, 0.0f, 0.0f),
         Mortar::Delegate0<void>());
    if (onRemove) {
        m_RemoveCallback = *onRemove;
    }
}

// ASM-verified: 2026-05-06T00:00 binary @ 0x0014f94c (asm-inspector)
MenuButton::~MenuButton() {}

// MenuButton::Init @ 0x0019b994
void MenuButton::Init(Vec3 buttonPos, Mortar::Delegate0<void> clickCb,
                      int fruitType, Vec3 hitBounds,
                      Mortar::Delegate0<void> deletedCb) {
    pos = buttonPos;
    m_ClickCallback  = clickCb;
    m_DeletedCallback = deletedCb;
    m_FruitType      = fruitType;
    m_RestScale.x     = hitBounds.x;
    m_RestScale.y     = hitBounds.y;
    m_bHasHitArea    = (fabsf(hitBounds.x) + fabsf(hitBounds.y)) > 0.0f ? 1 : 0;
    m_bInteractive   = 1;
    m_bAcceptsTouch  = 1;
    m_bBackdropActive = 1;
    m_GrowInTimer    = 1.0f;
    m_AnimPhase      = 0;
    m_HitInsetX      = 5.0f;
    m_HitInsetY      = 5.0f;
    m_ShakeTimer     = 0.0f;
    m_NewBouncePhase = 0.0f;
    m_LabelExtraAlpha = 0.0f;
    m_bClearsMenuItems = 0;
    m_bDragCancel    = 0;
    m_bRespondsToBackKey = 0;
    m_fieldD4        = static_cast<int>(0xffffffff);
    m_TouchSlot      = -1;
    m_SparkleTimer   = -1.0f;
    m_NewIndicatorTimer = -1.0f;
    m_RotationSpeed  = -1.0f;
    m_RandomOffset   = 0.0f;
    m_BaseScale      = Vec3(0.0f, 0.0f, 0.0f);
    m_pFruitPiece    = nullptr;
    m_pFruitPiece_alt = nullptr;
    m_pEntity        = nullptr;
    m_pTrackedFruit  = nullptr;
    m_FadeAlphaIdx   = 0;
    // m_ShakeScale @ +0x154: x=1.0, y=0.85, z=0.85 (DAT_0019bafc=0.85f).
    m_ShakeScale     = Vec3(1.0f, 0.85f, 0.85f);
    // m_fieldReserved @ +0x16C: Init writes 100.0f (DAT_0019baf8 = 0x42c80000).
    m_fieldReserved  = 100.0f;
    // compat fields
    m_bEnabled       = 1;
    m_AnimScale      = 1.0f;
    m_BounceParams   = Vec3(0.85f, 0.85f, 0.0f);
    m_bTouchHeld     = 1;
    m_bScoreSubmitted = 0;

    // Toggle-button (fruitType < 0): auto-size from texture if no hitBounds
    if (fruitType < 0 && !m_bHasHitArea && m_Texture.IsValid()) {
        Mortar::Texture* tex = m_Texture.Get();
        if (tex) {
            float w = (float)(tex->m_Width  + 1);
            float h = (float)(tex->m_Height + 1);
            m_RestScale.x = w;
            m_RestScale.y = h;
            size.x = w;
            size.y = h;
        }
    }

    // Create fruit entity if fruitType >= 0
    if (fruitType >= 0) {
        CreateFruit();
        m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    }
}

// Creates fruit/bomb entity based on m_FruitType; sets m_pEntity/m_pFruitPiece/m_pTrackedFruit.
void MenuButton::CreateFruit() {
    Game* game = Game::GetInstance();
    if (!game || !game->actorManager) return;

    int bombThreshold = FruitInfo_GetCount();
    int entityType = (bombThreshold <= m_FruitType) ? 1 : 0;
    Mortar::Entity* e = game->actorManager->Add(entityType, true);
    if (!e) return;

    e->pos = pos;
    e->Init(nullptr, (long)m_FruitType, nullptr);
    e->flags &= ~0x10;
    m_pEntity = e;
    m_pTrackedFruit = static_cast<Fruit*>(e);

    if (entityType == 0) {
        Fruit* fruit = static_cast<Fruit*>(e);
        fruit->m_RotVel1 = fruit->m_RotVel1 * FRUIT_ROTVEL_MULT;
        fruit->m_ScaleAnim = 1.0f;
        fruit->m_ChuckDelay = 0.0f;
        fruit->m_ZPosition = FRUIT_ZPOS;
        // ASM-verified: 2026-05-22 binary @ 0x0014f0de (re-analyst).
        fruit->m_bSpawnedByCriticalSplash = 1;
        m_pFruitPiece = fruit;

        if (fabsf(fruit->m_RotVel1.x) < ROT_CLAMP_X)
            fruit->m_RotVel1.x = (fruit->m_RotVel1.x >= 0 ? ROT_CLAMP_X : -ROT_CLAMP_X);
        if (fabsf(fruit->m_RotVel1.y) < ROT_CLAMP_Y)
            fruit->m_RotVel1.y = (fruit->m_RotVel1.y >= 0 ? ROT_CLAMP_Y : -ROT_CLAMP_Y);
        fruit->m_RotVel2 = fruit->m_RotVel1;
    } else {
        Bomb* bomb = static_cast<Bomb*>(e);
        bomb->m_bMovement = 0;
        bomb->scale = bomb->scale * BOMB_MENU_SCALE;
        bomb->SetCallback(m_ClickCallback, this);
        bomb->m_ZPosition = FRUIT_ZPOS;
    }

    // Random rotation speed (8-12 deg/frame, random direction)
    m_RotationSpeed = 8.0f + (float)(rand() % 40) / 10.0f;
    if (rand() % 2) m_RotationSpeed = -m_RotationSpeed;
}

// Binary @ 0x0019d064 -- clears entity backrefs, deletes labels, calls DeletePeices()
// ASM-verified: 2026-04-29T00:00Z binary @ 0x0014f7e0 (asm-inspector)
void MenuButton::Release() {
    Mortar::Entity* e = m_pFruitPiece ? static_cast<Mortar::Entity*>(m_pFruitPiece) : m_pEntity;
    if (e) {
        int bombThreshold = FruitInfo_GetCount();
        if (m_FruitType < bombThreshold) {
            static_cast<Fruit*>(e)->m_pSlasher = nullptr;
        } else if (m_FruitType == bombThreshold) {
            static_cast<Bomb*>(e)->m_pOwnerButton = nullptr;
        }
    }
    m_pLabelFg = nullptr;
    m_pLabelExtra = nullptr;
    m_pLabelShadow = nullptr;
    DeletePeices();
    m_Texture.SetNull();
    m_pEntity = nullptr;
    m_pFruitPiece = nullptr;
    m_pTrackedFruit = nullptr;
}

// Binary @ 0x19a4f8 -- vtable Init slot
void MenuButton::Init() { Reset(); }

// Binary @ 0x19a50c -- Reset is a no-op
// ASM-verified: 2026-05-06T00:00 binary @ 0x0014e3b8 (asm-inspector)
void MenuButton::Reset() {}

// Binary @ 0x19a558 -- Skip snaps animation to full
void MenuButton::Skip() { m_AnimPhase = 0x3ffc; }

// Binary @ 0x0014e3bc
void MenuButton::Shake(float t) { m_ShakeTimer = t; }

// Binary @ 0x0014e434
bool MenuButton::HasNewSymbol() { return m_NewIndicatorTimer >= 0.0f; }

// Binary @ 0x0014e484
bool MenuButton::IsLoadingSymbol() { return m_SparkleTimer >= 0.0f; }

// Binary @ 0x0014e45c
void MenuButton::SetLoadingSymbol(bool show) {
    if (m_SparkleTimer < 0.0f) {
        if (show) m_SparkleTimer = 0.0f;
    } else {
        if (!show) m_SparkleTimer = -1.0f;
    }
}

// Defunct: SetText -- no-op stub; binary @ 0x0014ebc0
// Zero call sites in shipped binary; curved-text feature authored but never wired.
void MenuButton::SetText(const char* /*text*/, Colour /*fg*/,
                         Colour /*shadow*/, float /*radius*/) {
}

// Binary @ 0x0014ed18
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
    m_pTrackedFruit = nullptr;
}

// Binary @ 0x0014e5cc
bool MenuButton::TouchReleased() {
    if (m_FruitType < 0) {
        m_ClickCallback();
    } else if (m_pEntity != nullptr) {
        Game* game = Game::GetInstance();
        if (game && game_work.m_TutorialControl) {
            game_work.m_TutorialControl->ButtonPressedAtPos(this);
        }
    }
    m_DeletedCallback();
    return true;
}

// Binary @ 0x19a5bc -- re-arms m_LayerFlags=0x40 each frame for fruit-type buttons
void MenuButton::BeginDraw(float dt) {
    (void)dt;
    if (m_FruitType >= 0) {
        m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    }
}

// Binary @ 0x19a5b8 -- PreDraw no-op
void MenuButton::PreDraw(const Vec3& hudScale) { (void)hudScale; }

// Binary @ 0x19a794 -- kills owned fruit/bomb then defers to base SetToMultiplayerState
bool MenuButton::SetToMultiplayerState() {
    Mortar::Entity* e = m_pFruitPiece ? static_cast<Mortar::Entity*>(m_pFruitPiece) : m_pEntity;
    if (!e) e = m_pFruitPiece_alt ? static_cast<Mortar::Entity*>(m_pFruitPiece_alt) : nullptr;
    if (e) {
        if (e->entityType == 0) {
            static_cast<Fruit*>(e)->KillFruit(false);
        } else if (e->entityType == 1) {
            static_cast<Bomb*>(e)->KillBomb();
        }
    }
    m_pEntity = nullptr;
    m_pFruitPiece = nullptr;
    m_pFruitPiece_alt = nullptr;
    m_pTrackedFruit = nullptr;
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

// MenuButton::Update @ 0x0019a860 (v1.6.1 pseudocode)
void MenuButton::Update(float dt) {
    Fruit* fruit = m_pTrackedFruit;  // +0x14c

    // --- grow-in delay gate ---
    if (m_GrowInTimer > 0.0f) {          // +0x134
        m_GrowInTimer -= dt;
        if (fruit) {
            // mark hidden while waiting
            // TODO: 0x0019a860 -- confirm exact flag bit written to fruit->flags(+0xc) for hidden
            fruit->flags |= 1;
        }
        return;
    }
    if (fruit) fruit->flags &= ~1;       // unhide

    // --- sparkle + new-indicator timers ---
    if (m_SparkleTimer >= 0.0f) {
        m_SparkleTimer += dt * 8.0f;
        if (m_SparkleTimer > 8.0f) m_SparkleTimer = 8.0f;
    }
    if (m_NewIndicatorTimer >= 0.0f) {
        m_NewIndicatorTimer += 2.0f * dt;
        if (m_SparkleTimer < 1.0f) m_NewIndicatorTimer = 0.0f;
    }

    UpdatePeices(dt);

    if (m_FruitType >= 0) {
        // spin the quad via m_Timer (+0x2c base field)
        if (dt > 0.0f) {
            m_Timer += dt * m_RotationSpeed;
            if (m_Timer < 0.0f) m_Timer += 360.0f;
        }

        Mortar::Entity* entity = m_pEntity;  // +0x80

        if (entity == nullptr) {
            // ---- SPAWN / no live entity branch ----
            fruit = m_pTrackedFruit;  // +0x14c
            if (fruit) {
                if (fruit->vel.x == 0.0f && fruit->vel.y == 0.0f) {
                    // at rest: request respawn
                    // TODO: 0x0019a860 -- fruit->(+0x188)=1 respawn-request field (not yet in Fruit.h; needs RE)
                    // TODO: 0x0019a860 -- fruit->flag(+0x35) respawn-pending check (not yet in Fruit.h)
                    float restY = m_RestScale.y;
                    float sizeY = (m_RestScale.y != 0.0f) ? size.y : 1.0f;
                    float s = (restY != 0.0f) ? (sizeY / restY) : 1.0f;
                    fruit->scale = m_BaseScale * s;
                } else {
                    fruit->scale = m_BaseScale;
                }
            }

            // grow-in quarter-sine ease-out (phase decrement toward 0)
            pos.z = -5.0f;
            int nextPhase = (int)m_AnimPhase - (int)(dt * 109200.0f);
            if (nextPhase < 1) {
                m_AnimPhase = 0;
                m_GrowShrinkDone = 1;
            } else {
                m_AnimPhase = (uint16_t)nextPhase;
            }
            float sinFull = SinIdx(0x3ffc);
            float s = (sinFull != 0.0f) ? (SinIdx(m_AnimPhase) / sinFull) : 0.0f;
            size.x = m_RestScale.x * s;
            size.y = m_RestScale.y * s;

        } else {
            // ---- live entity present: drive it ----
            if (m_BaseScale.x == 0.0f) {
                m_BaseScale = entity->scale;
            } else {
                float restY = m_RestScale.y;
                float ratio = (restY != 0.0f) ? (size.y / restY) : 1.0f;
                entity->scale = m_BaseScale * ratio;
            }

            // TODO: 0x0019a860 -- GetWorldPos() call (vtable slot 15); for now use pos directly
            entity->pos = pos;

            int bombThreshold = FruitInfo_GetCount();
            if (m_FruitType < bombThreshold) {
                // FRUIT branch
                // TODO: 0x0019a860 -- entity->pos2(+0xc8) = GetWorldPos(); not yet in Entity/Fruit layout
                Fruit* f = static_cast<Fruit*>(m_pEntity);
                if (f && f->m_bSliced) {     // +0xb4 sliced sentinel
                    Vec3 d;
                    d.x = f->pos.x - f->m_SecondPos.x;
                    d.y = f->pos.y - f->m_SecondPos.y;
                    d.z = f->pos.z - f->m_SecondPos.z;
                    float magSqr = d.x*d.x + d.y*d.y + d.z*d.z;
                    if (magSqr >= 0.001f) {   // SLICE_EPS
                        m_ClickCallback();
                        // TODO: 0x0019a860 -- TutorialControl::ResetTutePos() (not yet in TutorialControl.h)
                        entity->scale = m_BaseScale;
                        if (fruit && fruit->vel.x == 0.0f && fruit->vel.y == 0.0f) {
                            // TODO: 0x0019a860 -- fruit->(+0x188)=1 respawn request
                        }
                        // m_bEnabled (compat field) gates ClearMenuItems cascade
                        // per binary @ 0x0014e7e0; maps to v1.6.1 m_bClearsMenuItems gate.
                        if (m_bClearsMenuItems && m_bEnabled) {
                            FN::ClearMenuItems();
                            if (game_work.mMainScreen) {
                                game_work.mMainScreen->OnMenuItemsCleared();
                            }
                        }
                    }
                    m_pEntity = nullptr;
                }
            } else {
                // BOMB branch
                pos.z = -5.0f;
                entity->pos.z = 0.0f;  // +0x18 = 0
                // TODO: 0x0019a860 -- Bomb::Enabled() check; use existing Enabled() method
                Bomb* b = static_cast<Bomb*>(m_pEntity);
                if (b && !b->Enabled()) {
                    m_pEntity = nullptr;
                    entity->scale = m_BaseScale;
                }
            }

            // grow-in ease toward 0x3ffc
            if (m_AnimPhase < 0x3ffc) {
                int nextPhase = (int)m_AnimPhase + (int)(dt * 109200.0f);
                if (nextPhase > 0x3ffc) nextPhase = 0x3ffc;
                m_AnimPhase = (uint16_t)nextPhase;
                float sinFull = SinIdx(0x3ffc);
                float s = (sinFull != 0.0f) ? (SinIdx(m_AnimPhase) / sinFull) : 0.0f;
                size.x = m_RestScale.x * s;
                size.y = m_RestScale.y * s;
            } else {
                size.x = m_RestScale.x;
                size.y = m_RestScale.y;
            }
        }
    }

    // ---- touch handling ----
    // m_bTouchHeld (compat): port-side gate equivalent to binary's +0x131 guard in v1.0.
    if (m_bAcceptsTouch && m_bTouchHeld && m_bEnabled) {
        // TODO: 0x0019a860 -- Game::m_BackKeyPressed(+0x610) check + m_bBackdropActive gate
        // TODO: 0x0019a860 -- GetWorldPos() for rect origin; using pos directly for now

        float hw = m_RestScale.x * 0.5f;
        float hh = m_RestScale.y * 0.5f;
        const float left   = pos.x - hw - m_HitInsetX;
        const float right  = pos.x + hw + m_HitInsetX;
        const float bottom = pos.y - hh - m_HitInsetY;
        const float top    = pos.y + hh + m_HitInsetY;

        Mortar::Touch& touch = Mortar::Touch::GetInstance();

        if (m_TouchSlot == -1) {
            int slot = touch.GetTouchInRegion(left, right, bottom, top, -1);
            m_TouchSlot = slot;
            if (slot >= 0) {
                if (Mortar::IsTouchDown(slot) == 2) {
                    if (!m_bRespondsToBackKey && m_FruitType < 0) {
                        m_ClickCallback();
                    }
                } else {
                    m_TouchSlot = -1;
                }
            }
        } else {
            int down = Mortar::IsTouchDown(m_TouchSlot);
            if (down == 0) {
                UpdateTouchPosition();
                const bool insideOnRelease =
                    m_TouchX >= left && m_TouchX <= right &&
                    m_TouchY >= bottom && m_TouchY <= top;
                m_TouchSlot = -1;
                if (insideOnRelease) {
                    TouchReleased();
                }
            } else {
                UpdateTouchPosition();
                if (m_FruitType < 0 && m_bDragCancel) {
                    const bool insideNow =
                        m_TouchX >= left && m_TouchX <= right &&
                        m_TouchY >= bottom && m_TouchY <= top;
                    if (!insideNow) {
                        size.x = m_RestScale.x;
                        size.y = m_RestScale.y;
                        m_TouchSlot = -1;
                    }
                }
                // TODO: 0x0019a860 -- PRESS_SCALE(DAT_0019ac6c) shrink on held toggle; curScale = restScale * pressScale
            }
        }
    } else if (m_FruitType < 0) {
        size.x = m_RestScale.x;
        size.y = m_RestScale.y;
    }

    // ---- per-frame derived ----
    // m_BackdropScale @ +0xEC = curScale.x * 1.125 * m_ShakeScale.x (+0x154)
    // m_AnimScale (compat, v1.0 = 1.0f / 0.5f for big NEW GAME button) also applied.
    m_BackdropScale = size.x * 1.125f * m_ShakeScale.x * m_AnimScale;  // @0x19af70

    if (m_ShakeTimer > 0.0f) {
        m_ShakeTimer -= dt;
        if (m_ShakeTimer < 0.0f) m_ShakeTimer = 0.0f;
    }
}

// Binary @ 0x0014e3c4
void MenuButton::UpdateTouchPosition() {
    if (m_TouchSlot < 0) return;
    const Mortar::TouchState* s =
        Mortar::Touch::GetInstance().GetSlot(m_TouchSlot);
    if (!s) return;
    m_TouchX     = (float)s->currX;
    m_TouchY     = (float)s->currY;
    m_TouchPhase = (float)s->phase;
}

// MenuButton::Draw @ 0x0019c2e4
void MenuButton::Draw(const Vec3& hudScale, int layerMask) {
    if (m_DrawColour.a == 0) return;

    // Compute fade-derived alpha
    uint8_t alpha;
    if (m_FruitType < 0) {
        alpha = 0xFF;
    } else {
        // alpha = clamp(m_FadeAlphaIdx * K1/K2, 0, 255)
        // TODO: 0x0019c2e4 -- VectorSignedToFloat constants K1/K2 for m_FadeAlphaIdx alpha compute
        float n = (float)m_AnimPhase * 256.0f / 16380.0f;
        int   a = (int)n;
        if (a > 255) a = 255;
        if (a < 0)   a = 0;
        alpha = (uint8_t)a;
    }

    // Layer 0 (backdrop): scratchs.tex at layer 0x40, then demote to 0x80
    // ASM-verified: 2026-05-06T16:00 binary @ 0x0014f9cc Phase A (asm-inspector).
    if (m_LayerFlags == (int)Mortar::HUD_LAYER_MENU_BG) {
        m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;
        if (s_TexScratchs.IsValid()) {
            MatrixManager& mm = MatrixManager::GetInstance();
            const float sx = (m_RandomOffset < 0.0f) ? -1.0f : 1.0f;  // flip via RandomOffset sign
            Matrix44 mat = Matrix44::MakeScale(sx * m_BackdropScale,
                                               m_BackdropScale,
                                               m_BackdropScale);
            mat.GlobalTranslate44(Vec3(pos.x, pos.y, -5500.0f));
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Colour tint(255, 255, 255, alpha);
            s_TexScratchs->Set();
            Mortar::Mesh::DrawQuadUnCached(tint, 0.0f, 0.0f, 1.0f, 1.0f, NULL);
            s_TexScratchs->UnSet();
        }
        return;
    }

    // Subsequent passes: render the actual button sprite
    if (m_ShakeTimer > 0.0f) {
        Vec3 savedPos = pos;
        pos.x += ((float)(rand() % 600) / 100.0f) - 3.0f;
        pos.y += ((float)(rand() % 600) / 100.0f) - 3.0f;
        HUDControl3d::Draw(hudScale, layerMask);
        pos = savedPos;
    } else {
        HUDControl3d::Draw(hudScale, layerMask);
    }

    // Label block (v1.6.1 LIVE): if m_pLabelFg(+0x11c)!=0 -> render BakedString curve-draw
    if (m_pLabelFg != nullptr) {
        // TODO: 0x0019c2e4 -- BakedString curve-draw for m_pLabelFg / m_pLabelExtra / m_pLabelShadow
        // TODO: 0x0019c2e4 -- second pass when m_LabelExtraAlpha(+0x160) > 0
    }

    // New-indicator star indicator
    if (m_NewIndicatorTimer >= 0.0f && s_TexNewItem.IsValid()
        && m_RestScale.x != 0.0f)
    {
        MatrixManager& mm = MatrixManager::GetInstance();
        float ratio = (m_RestScale.x != 0.0f && size.x != 0.0f) ? (size.x / m_RestScale.x) : 1.0f;
        const uint16_t phase =
            (uint16_t)(m_NewIndicatorTimer * 180.0f * 182.0f);
        const float sv = SinIdx(phase);
        const float by = (sv < 0.0f ? -sv : sv) * 6.0f;

        Vec3 off(0.85f * m_RestScale.x * 0.5f,
                 by + 0.85f * m_RestScale.y * 0.5f,
                 0.0f);
        off = off * ratio;
        Vec3 drawAt = pos + off;

        mm.GetWorldStack().Reset();
        Matrix44 mat = Matrix44::MakeScale(ratio * 64.0f, ratio * 32.0f, 0.0f);
        mat.GlobalTranslate44(drawAt);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        const uint8_t a = m_DrawColour.a;
        Colour tint(255, 255, 255, a);
        s_TexNewItem->Set();
        Mortar::Mesh::DrawQuadUnCached(tint, 0.0f, 0.0f, 1.0f, 1.0f, NULL);
        s_TexNewItem->UnSet();
    }

    // Sparkle ring: armed when m_RotationSpeed(+0xf4) >= 0.
    // TODO: 0x0019c2e4 -- sparkle ring 48-vert mesh (binary @ Draw layer3): build ring once into
    //   static buffer, animate per-vertex colour by rotating index, draw via Mesh::DrawTriList(0x30 tris).
    //   Scale 0.75, MatrixStack push. Binary entry gates on m_RotationSpeed >= 0.
}

// ASM-verified: 2026-05-06T00:00 binary @ 0x0014f674 (asm-inspector)
void MenuButton::LoadContent() {
    s_TexScratchs      = Mortar::TextureManager::LoadLocalisedTexture("scratchs.tex");
    s_TexBlurryBacking = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    s_TexNewItem       = Mortar::TextureManager::LoadLocalisedTexture("new_item.tex");
}

// ASM-verified: 2026-05-06T00:00 binary @ 0x0014f718 (asm-inspector)
void MenuButton::UnLoadContent() {
    s_TexScratchs.SetNull();
    s_TexBlurryBacking.SetNull();
    s_TexNewItem.SetNull();
}

// Binary @ 0x00150240
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
        c->m_UVRight  = uvOverride->x + 1.0f;
        c->m_UVBottom = uvOverride->y + 1.0f;
    }
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
    c->m_Texture = tex;
    c->pos   = pos;
    c->m_Timer = initialTimer;
    c->size  = size;

    Game* game = Game::GetInstance();
    if (game && game_work.mHud) {
        game_work.mHud->AddControl(c, false);
    }

    MenuButtonAddOn addOn;
    addOn.control   = c;
    addOn.texCoord  = uvOverride;
    addOn.offset    = offset;
    addOn.sizeScale = sizeScale;
    (void)rotSpeed;
    (void)tex;
    m_AddOns.push_back(addOn);
}

// Binary @ 0x0014e49c
void MenuButton::UpdatePeices(float dt) {
    float restY = m_RestScale.y;
    float ratio = (restY > 0.0f && size.y > 0.0f) ? (size.y / restY) : 1.0f;
    for (std::list<MenuButtonAddOn>::iterator it = m_AddOns.begin();
         it != m_AddOns.end(); ++it) {
        HUDControl3d* c = it->control;
        if (!c) continue;
        c->m_Timer += dt * it->offset.y;
        c->pos = pos + it->offset * ratio;
        c->size = it->sizeScale * ratio;
    }
}

// Binary @ 0x0014f74c
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

// Binary @ 0x0014e54c
void MenuButton::DeletedPeice(HUDControl* hudControl) {
    for (std::list<MenuButtonAddOn>::iterator it = m_AddOns.begin();
         it != m_AddOns.end(); ++it) {
        if (it->control == hudControl) {
            m_AddOns.erase(it);
            return;
        }
    }
}
