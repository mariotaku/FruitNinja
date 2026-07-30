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
#include "render/QUADCUSTOMVERTEX.h"
#include "render/BakedStringTTF.h"
#include "render/FontCacheObjectTTF.h"
#include "render/FontTTFRegistry.h"
#include "render/Font.h"
#include "render/gl_funcs.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <string>
#include "game/GameWork.h"
#include "hud/IngamePopup.h"

namespace {
// Shared TTF face for MenuButton BakedStringTTF labels.
// Binary: reads *(g_GameData+0x614) -- the shared localized TTF face loaded once
//   by PreloadFontsTTF @0x0011c1fc (gangofchinese.ttf or arabic.ttf).
// Port: returns game_work.m_pTTFFontMain (populated at GameInitialise time by
//   PreloadFontsTTF, before MenuButton::LoadContent is called). Falls back to a
//   lazy local load if somehow null (e.g. TTF file missing).
static Mortar::FontCacheObjectTTF* GetSharedTTFFont() {
    if (game_work.m_pTTFFontMain) return game_work.m_pTTFFontMain;
    static Mortar::SmartPtr<Mortar::Font> s_TTFFont =
        Mortar::Font::Create("fontstruetype/gangofchinese.ttf");
    if (!s_TTFFont.IsValid()) return 0;
    return Mortar::FontTTFRegistry::GetInstance().Lookup(s_TTFFont.Get());
}
} // namespace

// Class-static textures loaded by MenuButton::LoadContent (v1.6.1 @0x0019d640).
//   Slot 1  scratchs.tex        (Phase-A backdrop)
//   Slot 2  blurry_backing.tex  (sparkle ring base)
//   Slot 3  new_item.tex        (Layer-2 NEW star)
static Mortar::SmartPtr<Mortar::Texture> s_TexScratchs;
static Mortar::SmartPtr<Mortar::Texture> s_TexBlurryBacking;
static Mortar::SmartPtr<Mortar::Texture> s_TexNewItem;

// Port-side stand-in for the compiler-OUTLINED per-TU RandF helpers the binary
// emits around Math::g_random (reached via GOT, which is why a `bl Rand32` scan
// misses these call sites):
//   T.1661 @0x001cc5ec -- ClearMenuItems @0x001cc6d0
//   T.1164 @0x0019b414 -- MenuButton::Draw @0x0019c2e4 / MenuButton::Remove
// ASM-spec v1.6.1 ClearMenuItems @0x001cc6d0: Math::g_random.RandF(10.0),
// RandF(5.0) x2 per unsliced fruit and x2 per enabled bomb.
static float RandScaled(float s) {
    return Math::g_Random.RandF(s);
}

void ClearMenuItems() {
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
                f->vel = _Vector3<float>(vx, vy, 0.0f);
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
                b->vel = _Vector3<float>(vx, vy, 0.0f);
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
      m_AnimPhase(0),
      m_AnimFlag(0),
      m_GrowShrinkDone(0),
      m_reservedD4(static_cast<int>(0xffffffff)),
      m_TouchSlot(-1),
      m_TouchX(0.0f), m_TouchY(0.0f), m_TouchPhase(0.0f),
      m_BackdropOffsetX(0.0f),
      m_BackdropScale(0.0f),
      m_RandomOffset(0),
      m_RotationSpeed(0.0f),
      m_SparkleTimer(-1.0f),
      m_NewIndicatorTimer(-1.0f),
      m_BaseScale(0.0f, 0.0f, 0.0f),
      m_DrawOffset(0.0f, 0.0f, 0.0f),
      m_pLabelFg(nullptr), m_pLabelShadow(nullptr), m_pLabelGlow(nullptr),
      m_PlayerColour(),
      m_GrowInTimer(0.0f),
      m_bRespondsToBackKey(0),
      m_bDragCancel(0),
      m_bClearsMenuItems(0),
      _pad13B(0),
      m_RestScale(0.0f, 0.0f, 0.0f),
      m_bHasHitArea(0),
      m_bAcceptsTouch(1),
      m_pTrackedFruit(nullptr),
      m_bBackdropActive(0),
      m_ShakeScale(1.0f, 0.85f, 0.85f),
      m_LabelExtraAlpha(0.0f),
      m_LabelRadius(0.0f),
      m_HitInsetX(5.0f),
      m_HitInsetY(5.0f),
      m_NewBouncePhase(100.0f),
      m_ShakeTimer(0.0f)
{
    m_FlipDirection = 0;
    _pad131[0] = 0; _pad131[1] = 0; _pad131[2] = 0;
    _pad14A[0] = 0; _pad14A[1] = 0;
    _pad151[0] = 0; _pad151[1] = 0; _pad151[2] = 0;
}

MenuButton::MenuButton(Mortar::SmartPtr<Mortar::Texture> tex, _Vector3<float> spawnPos,
                       Mortar::Delegate0<void> clickCb,
                       int fruitType, _Vector3<float> hitBounds,
                       Mortar::Delegate0<void> deletedCb)
    // Port specific: ARM32 heap may be zero-initialized by Bada's operator new;
    // x64 heap is not. Initialize label pointers to null so MenuButton::Draw's
    // m_pLabelFg != nullptr guard doesn't read garbage.
    : m_pLabelFg(nullptr), m_pLabelShadow(nullptr), m_pLabelGlow(nullptr)
{
    m_Texture = tex;
    Init(spawnPos, clickCb, fruitType, hitBounds, deletedCb);
}

// v1.6.1 MenuButton::MenuButton C1 @0x0019be50 / C2 @0x0019bff8. Body is
// LoadLocalisedTexture(tmp, textureName); m_Texture = tmp; then the same
// Init() tail as the SmartPtr<Texture> overload. textureName is a texture
// filename (e.g. "openfeint_gamecenter.tex"), not label text.
MenuButton::MenuButton(const char* textureName, _Vector3<float> spawnPos,
                       Mortar::Delegate0<void> clickCb,
                       int fruitType, _Vector3<float> hitBounds,
                       Mortar::Delegate0<void> deletedCb)
    // Port specific: see the SmartPtr<Texture> ctor above for why label
    // pointers are explicitly nulled here.
    : m_pLabelFg(nullptr), m_pLabelShadow(nullptr), m_pLabelGlow(nullptr)
{
    Mortar::SmartPtr<Mortar::Texture> tmp = Mortar::TextureManager::LoadLocalisedTexture(textureName);
    m_Texture = tmp;
    Init(spawnPos, clickCb, fruitType, hitBounds, deletedCb);
}

// ASM-spec v1.6.1 MenuButton::~MenuButton D0 @0x0019d130 / D1 @0x0019d1dc: both
// call Release() FIRST (then ~list(m_AddOns)/~Delegate/~HUDControl3d). Release()
// @0x0019d064 clears the tracked fruit's m_pOwner back-pointer (+0x160) / the
// bomb's owner-button field, deletes the 3 labels + pieces, and SetNulls
// m_Texture. The port previously had an EMPTY dtor, so Release() never ran on any
// reap/delete path -> tracked fruits kept a dangling m_pOwner back-pointer
// (Fruit::KillFruit UB); restoring the dtor->Release() call is binary-faithful.
// Refcount-safe: Release SetNulls m_Texture once; the base ~HUDControl3d then sees
// null. (NOTE: the "menu ring texture corruption" once attributed to a dangling seed
// over-freeing the shared red_ring was actually the wasm 64KB stack-overflow spray,
// now root-fixed via STACK_SIZE=1MB -- unrelated to this dtor.)
MenuButton::~MenuButton() { Release(); }

// MenuButton::Init @ 0x0019b994
void MenuButton::Init(_Vector3<float> buttonPos, Mortar::Delegate0<void> clickCb,
                      int fruitType, _Vector3<float> hitBounds,
                      Mortar::Delegate0<void> deletedCb) {
    pos = buttonPos;
    m_ClickCallback  = clickCb;
    m_DeletedCallback = deletedCb;
    m_FruitType      = fruitType;
    m_RestScale.x     = hitBounds.x;
    m_RestScale.y     = hitBounds.y;
    m_RestScale.z     = hitBounds.z;
    SetHasHitArea((fabsf(hitBounds.x) + fabsf(hitBounds.y)) > 0.0f);
    m_bAcceptsTouch   = 1;
    // ASM-spec v1.6.1 MenuButton::Init @0x0019b994: m_bBackdropActive(+0x150)=0 default;
    // only each screen's back/regress ring sets it to 1 (back-key force-slice gate @0x0019ad28).
    m_bBackdropActive = 0;
    m_GrowInTimer    = 0.0f;
    m_AnimPhase      = 0;
    m_LabelRadius    = 0.0f;
    m_HitInsetX      = 5.0f;
    m_HitInsetY      = 5.0f;
    m_ShakeTimer     = 0.0f;
    m_NewBouncePhase = 100.0f;
    m_LabelExtraAlpha = 0.0f;
    m_bClearsMenuItems = 1;  // binary Init @0x19b9f8 sets 1 for EVERY button: slicing any
                             // menu button clears all menu fruits (ClearMenuItems) so the
                             // type-0 entity count reaches 0 and the target screen can open.
    // ASM-spec v1.6.1 MenuButton::Init @0x0019ba28: m_bDragCancel(+0x139)=1 (only writer in binary).
    // Enables held press-scale (Update @0x0019aeac: size=m_RestScale*0.95) -> Draw @0x0019c2e4
    // press-dim RGB*0.5 for m_FruitType<0 toggle buttons (main menu + pause).
    m_bDragCancel    = 1;   // v1.6.1 MenuButton::Init @0x0019ba28: strb r6(=1),[this,#0x139] unconditional
    m_bRespondsToBackKey = 0;
    m_reservedD4     = static_cast<int>(0xffffffff);
    m_TouchSlot      = -1;
    m_SparkleTimer   = -1.0f;
    m_NewIndicatorTimer = -1.0f;
    // v1.6.1 MenuButton::Init @0x0019b994: +0xF4 = 0.0f (the -1.0f literals go to
    // m_SparkleTimer/m_NewIndicatorTimer +0xF8/+0xFC). CreateFruit re-rolls this random per spawn.
    m_RotationSpeed  = 0.0f;
    m_RandomOffset   = 0;
    m_BaseScale      = _Vector3<float>(0.0f, 0.0f, 0.0f);
    // v1.6.1 MenuButton::Init @0x0019ba50: size (+0x20) <- Vector3::Zero (0,0,0).
    // Without this, a freshly-new'd button's size is garbage; the new-items badge
    // (Draw @0x0019c2e4 uses size.x/m_RestScale.x) flashes full on frame 1 before the
    // AnimPhase grow-in resets size to 0. Fruit-type buttons grow icon+badge from 0.
    size             = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_pFruitPiece_alt = nullptr;
    m_pEntity        = nullptr;
    m_pTrackedFruit  = nullptr;
    // m_ShakeScale @ +0x154: x=1.0, y=0.85, z=0.85 (DAT_0019bafc=0.85f).
    m_ShakeScale     = _Vector3<float>(1.0f, 0.85f, 0.85f);
    // m_FlipDirection @ +0x130: Init writes 0 (*(uchar*)&m_FlipDirection = 0).
    m_FlipDirection  = 0;

    // ASM-spec v1.6.1 MenuButton::Init @0x0019b994: m_RestScale (+0x13C) is set ONLY
    // from the caller's hitBounds/size Vec3 (param_4). Binary has NO texture-pixel
    // auto-size; do not derive size from m_Texture->m_Width/Height.

    // Create fruit entity if fruitType >= 0
    if (fruitType >= 0) {
        CreateFruit();
        m_LayerFlags = Mortar::HUD_LAYER_MENU_BG;
    }
}

// Creates fruit/bomb entity based on m_FruitType; sets m_pEntity/m_pTrackedFruit.
// ASM-verified: 2026-07-14T22:20Z v1.6.1 MenuButton::CreateFruit @ 0x0019b608 (asm-inspector)
void MenuButton::CreateFruit() {
    // v1.6.1 MenuButton::CreateFruit @0x0019b608 -- guard branch @0x0019b910:
    //   if (m_FruitType < 0 || m_pTrackedFruit != nullptr): recompute m_RestScale only, set tracked, return.
    if (m_FruitType < 0 || m_pTrackedFruit != nullptr) {
        if (!m_bHasHitArea) {                                      // ldrb [this,#0x148]
            m_RestScale.x = (float)(m_Texture->GetWidth()  + 1);  // m_Texture=+0x74; w/h from vtable
            m_RestScale.y = (float)(m_Texture->GetHeight() + 1);
            // m_RestScale.z unchanged
        }
        // Binary @0x0019b95c: raw word store (str) of m_pEntity -> m_pTrackedFruit.
        // No type check; entity may be Bomb. reinterpret_cast matches the ARM store.
        m_pTrackedFruit = reinterpret_cast<Fruit*>(m_pEntity);    // tail @0x0019b95c
        return;
    }

    // ASM-spec v1.6.1 MenuButton::CreateFruit @0x0019b634: two g_Random draws BEFORE
    // ActorManager::GetInstance(), in this order (flag first, then offset).
    m_RandomOffset = (Math::g_Random.Rand32(2) != 0) ? 1 : 0;   // strb, one byte
    // (float)(uint32_t) is deliberate -- reproduces vcvt.f32.u32 on the UNSIGNED
    // subtraction Rand32(0x28) - 0x14, including the wrap to a huge float for
    // results below 0x14. Do NOT "clean up" to a signed cast. Field is write-only;
    // rolled purely to keep the RNG draw sequence faithful.
    m_BackdropOffsetX = (float)(uint32_t)(Math::g_Random.Rand32(0x28) - 0x14);

    Game* game = Game::GetInstance();
    if (!game || !game->actorManager) return;

    int bombThreshold = g_FruitInfoCount;
    int entityType = (bombThreshold <= m_FruitType) ? 1 : 0;
    Mortar::Entity* e = game->actorManager->Add(entityType, true);
    if (!e) return;

    // Binary MenuButton::CreateFruit @0x0019b608: pos = GetAdjustedPos(), then
    // vel = Vector3::Zero, THEN Init. Zeroing vel is load-bearing: ActorManager::Add
    // recycles a pooled Bomb carrying stale velocity from its prior ClearMenuItems fling;
    // without this, Bomb::Update's ungated `pos += vel` drifts the collision sphere off
    // the pinned draw position -> menu-bomb slice near-misses (worse after dojo re-entry).
    e->pos = GetAdjustedPos();
    e->vel = _Vector3<float>(0.0f, 0.0f, 0.0f);              // binary @0x0019b67c: entity->m_Velocity = Vector3::Zero
    e->Init(nullptr, (long)m_FruitType, nullptr);
    e->flags &= ~0x10;
    m_pEntity = e;
    // Binary: raw word store; entity may be Bomb. reinterpret_cast matches ARM str.
    m_pTrackedFruit = reinterpret_cast<Fruit*>(e);
    LOG_DEBUG("MENUBTN", "CreateFruit: m_pEntity=%p entityType=%d pos=(%.1f,%.1f)",
              static_cast<void*>(m_pEntity), entityType, pos.x, pos.y);

    // v1.6.1 MenuButton::CreateFruit @0x0019b704: random icon-spin speed +/-[8,12) deg/sec.
    m_RotationSpeed = 8.0f + Math::g_Random.RandF(4.0f);
    if (Math::g_Random.Rand32(2) == 0) m_RotationSpeed = -m_RotationSpeed;   // @0x0019b734

    if (entityType == 0) {
        Fruit* fruit = static_cast<Fruit*>(e);
        fruit->m_RotVel1 = fruit->m_RotVel1 * FRUIT_ROTVEL_MULT;
        fruit->m_MenuGrowFade = 1.0f;
        fruit->m_SpawnDelay = 0.0f;
        fruit->m_ZPosition = FRUIT_ZPOS;
        // ASM-verified: 2026-05-22 v1.6.1 MenuButton::CreateFruit @ 0x0019b608 (re-analyst).
        // Binary writes m_bMenuFling=1 (0x164) to mark this as a menu-context fruit.
        fruit->m_bMenuFling = 1;
        // Binary writes m_pOwner=this (0x160) so KillFruit can clear our m_pTrackedFruit.
        // KillFruit reads owner+0x14C (= MenuButton::m_pTrackedFruit) as a raw offset.
        fruit->m_pOwner = reinterpret_cast<Mortar::Entity*>(this);
        // Binary @ 0x0019b818: strb #0,[fruit,#0x70] -- AFTER Init() (which set it to 1).
        // Disables ballistic integration so the menu fruit stays pinned at the button
        // position; Fruit::Update gates pos/vel integration on this flag (binary 0x001df828).
        fruit->m_bBallisticEnable = 0;
        // ASM-verified: 2026-06-26T16:02Z v1.6.1 MenuButton::CreateFruit @0x0019b608 (asm-inspector)
        //   m_RestScale(+0x13C) = entity->scale * 200.0f [0x43480000]; size(+0x20) settles to
        //   m_RestScale post grow-in. For scale-60 fruits -> 0.6*200 = 120 (drives the visible
        //   ring scale AND the gameover highscore text scale = size.x*0.5 = 60).
        m_RestScale = m_pEntity->scale * 200.0f;

        if (fabsf(fruit->m_RotVel1.x) < ROT_CLAMP_X)
            fruit->m_RotVel1.x = (fruit->m_RotVel1.x >= 0 ? ROT_CLAMP_X : -ROT_CLAMP_X);
        if (fabsf(fruit->m_RotVel1.y) < ROT_CLAMP_Y)
            fruit->m_RotVel1.y = (fruit->m_RotVel1.y >= 0 ? ROT_CLAMP_Y : -ROT_CLAMP_Y);
        fruit->m_RotVel2 = fruit->m_RotVel1;
    } else {
        Bomb* bomb = static_cast<Bomb*>(e);
        bomb->m_bMovement = 0;
        // NOTE: bomb->scale.x here is bombSize*0.01 (~0.55), NOT the raw bombSize (~55).
        // The binary @0x0019b8c8 uses the raw FruitInfo bomb size (2.0*bombBaseSize=110)
        // for m_RestScale, equivalent to bomb->scale*200 (same as the fruit branch pattern).
        // We use FruitInfo_GetBombSize()*2.0f to match the binary's unscaled formula directly.
        const float bombRawSize = FruitInfo_GetBombSize();   // ~55 from fruitlist.xml
        // ASM-spec v1.6.1 MenuButton::CreateFruit @0x0019b8b4: bomb entity->scale *= 0.85
        //   (const @0x19b8ac), NOT *200. bomb.mmd half-extent 48.7 (== fruit meshes); size 55
        //   -> 0.55*0.85 = 0.4675 -> renders 22.8u, fruit-sized. The *200 is the FRUIT branch's
        //   m_RestScale grow-target, never applied to the bomb entity scale.
        bomb->scale = bomb->scale * BOMB_MENU_SCALE;
        bomb->SetCallback(m_ClickCallback, this);
        bomb->m_ZPosition = FRUIT_ZPOS;
        // Binary v1.6.1 MenuButton::CreateFruit @0x0019b8c8: m_RestScale = 2.0 * bombBaseSize
        m_RestScale = _Vector3<float>(bombRawSize * 2.0f, bombRawSize * 2.0f, bombRawSize * 2.0f);
        SetHasHitArea(true);
    }
}

// v1.6.1 MenuButton::Release @0x0019d064 -- clears the tracked entity's owner backref (leaves the
// entity ALIVE), deletes the 3 labels, DeletePieces, releases m_Texture.
// ASM-verified: 2026-07-03T00:00Z v1.6.1 MenuButton::Release @ 0x0019d064 (re-analyst disasm):
//   ldr m_pTrackedFruit(+0x14C); if non-null: if m_FruitType(+0x84) < MAX_FRUIT_TYPES ->
//   fruit->m_pOwner(+0x160)=0 else if == -> bomb->m_pOwnerButton(+0x84)=0. Does NOT kill the fruit.
// (An earlier port attempt force-KillFruit'd here to remove orphans; that prematurely pools the menu
//  fruit -> DojoScreen::CreateFruit recycles it -> e->Init() call_indirect OOB. Reverted to faithful.)
void MenuButton::Release() {
    if (m_pTrackedFruit) {
        Mortar::Entity* e = static_cast<Mortar::Entity*>(m_pTrackedFruit);
        int bombThreshold = g_FruitInfoCount;
        if (m_FruitType < bombThreshold) {
            static_cast<Fruit*>(e)->m_pOwner = nullptr;
        } else if (m_FruitType == bombThreshold) {
            static_cast<Bomb*>(e)->m_pOwnerButton = nullptr;
        }
    }
    delete m_pLabelFg;     m_pLabelFg     = nullptr;
    delete m_pLabelShadow; m_pLabelShadow = nullptr;
    delete m_pLabelGlow;   m_pLabelGlow   = nullptr;
    DeletePieces();
    m_Texture.SetNull();
    m_pEntity = nullptr;
    m_pTrackedFruit = nullptr;
}

// v1.6.1 MenuButton::Init(void) @0x0019a4f8 -- vtable Init slot
void MenuButton::Init() { Reset(); }

// v1.6.1 MenuButton::Reset @0x0019a50c -- no-op
// ASM-verified: 2026-05-06T00:00 v1.6.1 MenuButton::Reset @ 0x0019a50c (asm-inspector)
void MenuButton::Reset() {}

// v1.6.1 MenuButton::Skip @0x0019a558 -- snaps animation to full
void MenuButton::Skip() { m_AnimPhase = 0x3ffc; }

// v1.6.1 MenuButton::Shake @0x0019a510
void MenuButton::Shake(float t) { m_ShakeTimer = t; }

// v1.6.1 MenuButton::HasNewSymbol @0x0019a5a0
bool MenuButton::HasNewSymbol() { return m_NewIndicatorTimer >= 0.0f; }

// v1.6.1 MenuButton::IsLoadingSymbol @0x0019a608
bool MenuButton::IsLoadingSymbol() { return m_SparkleTimer >= 0.0f; }

// v1.6.1 MenuButton::SetLoadingSymbol @0x0019a5d0
void MenuButton::SetLoadingSymbol(bool show) {
    if (m_SparkleTimer < 0.0f) {
        if (show) m_SparkleTimer = 0.0f;
    } else {
        if (!show) m_SparkleTimer = -1.0f;
    }
}

// v1.6.1 MenuButton::SetText @0x0019b0ac
void MenuButton::SetText(const char* text, Colour gradTop, Colour gradBottom,
                         float radius, float fontScale,
                         bool wantGlow, bool wantInnerGlow) {
    Mortar::FontCacheObjectTTF* font = GetSharedTTFFont();

    delete m_pLabelFg;     m_pLabelFg     = nullptr;
    delete m_pLabelShadow; m_pLabelShadow = nullptr;
    delete m_pLabelGlow;   m_pLabelGlow   = nullptr;

    if (!font || !text) return;

    // FitStringToWidth shrink: if the arc width < text width, scale font down.
    // v1.6.1 MenuButton::SetText @0x0019b0ac: arcW = radius * PI * 0.75
    float actualFontScale = fontScale;
    if (radius > 0.0f) {
        float arcW = radius * 3.14159265f * 0.75f;
        float outWidth = 0.0f;
        {
            std::string ioText(text);
            std::string remainder;
            bool truncated = false;
            // ASM-spec v1.6.1 BakedStringTTF::FitStringToWidth @0x00248734: (fc, in, out,
            // fontSize, weight, maxWidth, outWidth, outTruncated); weight=0, wrap at 500.
            Mortar::BakedStringTTF::FitStringToWidth(font, ioText, remainder, fontScale, 0L, 500, &outWidth, &truncated);
        }
        if (outWidth > arcW) {
            float newScale = fontScale / (outWidth / arcW);
            m_LabelRadius = radius + fabsf(fontScale - newScale);
            actualFontScale = newScale;
        } else {
            m_LabelRadius = radius;
        }
    } else {
        m_LabelRadius = 0.0f;
    }

    // ASM-verified v1.6.1 MenuButton::SetText @0x0019b0ac / SetInnerGlow @0x0019afbc --
    // arc labels use alignSigned=-1 (uniform m_Weight) for all 3 layers so
    // ApplyFormatting_Circle's penX-derived arc angle matches across layers; the
    // stroke/glow expansion size goes in effectSize instead (was swapped, causing
    // glyph doubling at the arc ends -- "NEW GAME" -> "NNEW GAMEE").
    // FG label (@0x0019b244): alignSigned=-1, effectSize=0, FONT_EFFECT_NONE.
    m_pLabelFg = new Mortar::BakedStringTTF(font, text, actualFontScale,
        Colour(255, 255, 255, 255), -1L, 0.0f, Mortar::FontCacheObjectTTF::FONT_EFFECT_NONE);
    if (wantGlow) {
        // Outer glow (@0x0019b2a4): alignSigned=-1, effectSize=5, FONT_EFFECT_BLUR.
        m_pLabelGlow = new Mortar::BakedStringTTF(font, text, actualFontScale,
            Colour(0, 0, 0, 255), -1L, 5.0f, Mortar::FontCacheObjectTTF::FONT_EFFECT_BLUR);
        if (m_LabelRadius > 0.0f)
            m_pLabelGlow->ApplyFormatting_Circle(m_LabelRadius);
    }
    if (wantInnerGlow) {
        // ASM-spec v1.6.1 MenuButton::SetText @0x0019b0ac (wantInnerGlow branch @0x0019b344):
        // Colour ctor r1=0xff,r2=0xff,r3=0xff,[sp+0]=0x80 => Colour(255,255,255,128) -- white
        // at 50% alpha (subtle inner sheen). Port had (255,255,255-vs-128, 255-vs-128) swapped,
        // rendering an opaque light-yellow wash instead of a translucent white highlight.
        // Shadow/inner-glow effectSize=2 (@0x0019afbc); alignSigned=-1 set inside SetInnerGlow.
        SetInnerGlow(text, Colour(255, 255, 255, 128), m_LabelRadius, actualFontScale, 2.0f);
    }
    if (m_LabelRadius > 0.0f)
        m_pLabelFg->ApplyFormatting_Circle(m_LabelRadius);
    m_pLabelFg->ApplyGradient_TopBottom(gradTop, gradBottom);
}

// ASM-verified v1.6.1 MenuButton::SetInnerGlow @0x0019afbc
void MenuButton::SetInnerGlow(const char* text, Colour colour, float radius,
                               float fontScale, float effectSize) {
    delete m_pLabelShadow;
    m_pLabelShadow = nullptr;
    Mortar::FontCacheObjectTTF* font = GetSharedTTFFont();
    if (!font || !text) return;
    // alignSigned=-1 (uniform m_Weight, matches fg/glow layers); effectSize (2.0 from
    // the caller) drives the inner-glow expansion via FONT_EFFECT_INNER_GLOW.
    m_pLabelShadow = new Mortar::BakedStringTTF(font, text, fontScale,
        colour, -1L, effectSize, Mortar::FontCacheObjectTTF::FONT_EFFECT_INNER_GLOW);
    if (radius > 0.0f)
        m_pLabelShadow->ApplyFormatting_Circle(radius);
}

// v1.6.1 MenuButton::Remove @0x0019b448
void MenuButton::Remove() {
    if (!m_pTrackedFruit) return;
    if (m_pTrackedFruit->m_bSliced) return;
    m_pTrackedFruit->m_bDrawWhole = true;
    // vel = (T.1164(10.0), -T.1164(5.0), 0). Unlike ClearMenuItems @0x001cc6d0,
    // Remove does NOT re-centre x by subtracting 5 -- the fling is always to the
    // right here.
    float vx = RandScaled(10.0f);
    float vy = -(RandScaled(5.0f));
    m_pTrackedFruit->vel = _Vector3<float>(vx, vy, 0.0f);
    m_pTrackedFruit->m_SecondVel = m_pTrackedFruit->vel;
    m_pEntity = nullptr;
    m_pTrackedFruit = nullptr;
}

// v1.6.1 MenuButton::TouchReleased @0x0019a7f8
bool MenuButton::TouchReleased() {
    // Binary gate requires BOTH m_FruitType<0 AND m_bRespondsToBackKey. Toggle
    // buttons (sound/music) set m_bRespondsToBackKey=0, so release fires only
    // m_DeletedCallback, NOT m_ClickCallback -- otherwise the press-edge toggle
    // reverts on lift (the double-toggle bug). #65.
    if (m_FruitType < 0 && m_bRespondsToBackKey) {
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
void MenuButton::PreDraw(float* hudScale) { (void)hudScale; }

// Binary @ 0x19a794 -- kills owned fruit/bomb then defers to base SetToMultiplayerState
bool MenuButton::SetToMultiplayerState() {
    Mortar::Entity* e = m_pTrackedFruit ? static_cast<Mortar::Entity*>(m_pTrackedFruit) : m_pEntity;
    if (!e) e = m_pFruitPiece_alt ? static_cast<Mortar::Entity*>(m_pFruitPiece_alt) : nullptr;
    if (e) {
        if (e->entityType == 0) {
            static_cast<Fruit*>(e)->KillFruit(false);
        } else if (e->entityType == 1) {
            static_cast<Bomb*>(e)->KillBomb();
        }
    }
    m_pEntity = nullptr;
    m_pFruitPiece_alt = nullptr;
    m_pTrackedFruit = nullptr;
    return HUDControl::SetToMultiplayerState();
}

// v1.6.1 MenuButton::SetNewSymbol @0x0019a564
void MenuButton::SetNewSymbol(bool show) {
    if (show) {
        if (m_NewIndicatorTimer < 0.0f)
            m_NewIndicatorTimer = 0.0f;
    } else {
        if (m_NewIndicatorTimer >= 0.0f)
            m_NewIndicatorTimer = -1.0f;
    }
}

#ifndef __bada__
// Port specific: no binary counterpart -- sparkle-ring / NEW-badge advance,
// called only from UpdateRealtime() below with the REAL per-present dtSeconds.
// __bada__ keeps the verbatim per-tick block inline in Update() above instead
// of calling this (which doesn't exist in that build). Factored out so future
// callers can't drift from the ASM-spec wrap/reset rules.
//
// The binary advances these as rate*dt (`m_SparkleTimer += dt*8.0f`,
// `m_NewIndicatorTimer += 2.0f*dt`) -- i.e. 8/sec and 2/sec at the fixed 60Hz
// sim dt. Because they are ALREADY rate*time (not per-tick fractional
// constants like a decay `*=k`), the frame-rate-independent port form is just
// rate*dtSeconds with the REAL present dt -- NOT an f-scaled step. (An earlier
// version multiplied the per-second rate by f = dtSeconds*60, advancing 60x
// too fast and breaking the NEW-badge bounce / sparkle ring.)
static void AdvanceSparkleAndBadge(float& sparkleTimer, float& newIndicatorTimer, float dtSeconds) {
    if (sparkleTimer >= 0.0f) {
        sparkleTimer += 8.0f * dtSeconds;   // binary: += dt*8.0f per 60Hz tick == 8/sec
        // ASM-spec v1.6.1 MenuButton::Update @0x0019a860: sparkle WRAPS to 0 at >=8.0
        // (cyclic loading ring), not clamp-and-hold at 8.0.
        if (sparkleTimer >= 8.0f) sparkleTimer = 0.0f;
    }
    if (newIndicatorTimer >= 0.0f) {
        newIndicatorTimer += 2.0f * dtSeconds;   // binary: += 2.0f*dt per tick == 2/sec
        // ASM-spec v1.6.1 MenuButton::Update @0x0019a860: reset the NEW-badge bob timer
        // ONLY when sparkle is active (>=1.0).
        if (sparkleTimer >= 1.0f) newIndicatorTimer = 0.0f;
    }
}
#endif

// MenuButton::Update @ 0x0019a860 (v1.6.1 pseudocode)
// ASM-verified: 2026-07-14T21:50Z v1.6.1 MenuButton::Update @ 0x0019a860 (asm-inspector)
void MenuButton::Update(float dt) {
    Fruit* fruit = m_pTrackedFruit;  // +0x14c

    // --- grow-in delay gate ---
    if (m_GrowInTimer > 0.0f) {          // +0x134
        m_GrowInTimer -= dt;
        if (fruit) {
            // mark hidden while waiting
            fruit->flags |= 1;
        }
        return;
    }
    if (fruit) fruit->flags &= ~1;       // unhide

    // --- sparkle + new-indicator timers ---
    // DIFFERS: v1.6.1 MenuButton::Update @0x0019a860 advances the sparkle-ring
    // and NEW-badge bounce phases per 60Hz sim tick; port advances them per
    // rendered frame (dt-scaled via UpdateRealtime()/AdvanceSparkleAndBadge)
    // to track display refresh. __bada__ keeps the faithful 60Hz path below.
#ifdef __bada__
    if (m_SparkleTimer >= 0.0f) {
        m_SparkleTimer += dt * 8.0f;
        // ASM-spec v1.6.1 MenuButton::Update @0x0019a860: sparkle WRAPS to 0 at >=8.0
        // (cyclic loading ring), not clamp-and-hold at 8.0.
        if (m_SparkleTimer >= 8.0f) m_SparkleTimer = 0.0f;
    }
    if (m_NewIndicatorTimer >= 0.0f) {
        m_NewIndicatorTimer += 2.0f * dt;
        // ASM-spec v1.6.1 MenuButton::Update @0x0019a860: reset the NEW-badge bob timer ONLY
        // when sparkle is active (>=1.0). Port had `< 1.0f` (inverted), so with sparkle=-1.0
        // (inactive, the menu's resting state) the timer reset every frame -> badge never bobbed.
        if (m_SparkleTimer >= 1.0f) m_NewIndicatorTimer = 0.0f;
    }
#else
    // Port: already advanced by UpdateRealtime() (per-present, dt-scaled); this
    // 60Hz Update() does not touch the timers (nothing downstream in Update()
    // reads them -- only Draw() does).
#endif

    UpdatePieces(dt);

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
                    // At rest: request re-whole draw. Binary @ 0x19abd8..0x19ac20 (Site A).
                    // Gate on entityType==0 (fruit bucket, not bomb) matches binary's
                    // f[0x35]==0 check in MenuButton::Update.
                    if (fruit->entityType == 0) {
                        fruit->m_bDrawWhole = 1;
                    }
                    float restY = m_RestScale.y;
                    float sizeY = (m_RestScale.y != 0.0f) ? size.y : 1.0f;
                    float s = (restY != 0.0f) ? (sizeY / restY) : 1.0f;
                    fruit->scale = m_BaseScale * s;
                } else {
                    fruit->scale = m_BaseScale;
                }
            }

            // shrink-out quarter-sine ease (phase decrement toward 0 after entity nulled by slice)
            // ASM-spec: DAT_0019ac68=109216 / clamp 16380 @ MenuButton::Update
            // ASM-spec v1.6.1 MenuButton::Update @0x0019acbc: shrink-out complete (phase<=0, entity gone) -> m_bPendingRemoval=1 (self-removal)
            pos.z = -5.0f;
            int nextPhase = (int)m_AnimPhase - (int)(dt * 109216.0f);
            if (nextPhase < 1) {
                m_AnimPhase = 0;
                m_bPendingRemoval = 1;
            } else {
                m_AnimPhase = (uint16_t)nextPhase;
            }
            float sinFull = SinIdx(0x3ffc);
            float s = (sinFull != 0.0f) ? (SinIdx(m_AnimPhase) / sinFull) : 0.0f;
            size.x = m_RestScale.x * s;
            size.y = m_RestScale.y * s;

        } else {
            // ---- live entity present: drive it ----

            // Capture m_BaseScale on the very first frame (binary also reads entity->scale
            // the first time the entity branch runs). Done before the ramp so m_BaseScale
            // is valid when we apply the entity scale below.
            if (m_BaseScale.x == 0.0f) {
                m_BaseScale = entity->scale;
            }

            // Re-anchor: binary @ 0x0019a860 calls vtable slot 15 (GetAdjustedPos @ 0x136c2c)
            // and overwrites entity->pos (+0x10) every frame. This is the entire hold mechanism.
            entity->pos = GetAdjustedPos();

            int bombThreshold = g_FruitInfoCount;
            if (m_FruitType < bombThreshold) {
                // FRUIT branch: also write pos2 (+0xc8 = m_SecondPos) to prevent lerp/streak.
                // Binary @ 0x0019a860 calls slot 15 a second time and stores into entity+0xc8.
                static_cast<Fruit*>(entity)->m_SecondPos = GetAdjustedPos();
                Fruit* f = static_cast<Fruit*>(m_pEntity);
                if (f && f->m_bSliced) {     // +0xb8 sliced sentinel
                    // Binary @0x0019aa44: the slice gate is VELOCITY-based, not position.
                    // Fruit::Slice writes vel=halfVelB (+0x1c) and m_SecondVel=halfVelA (+0xd4),
                    // which diverge after a real slice. pos/m_SecondPos are BOTH re-anchored to
                    // GetAdjustedPos() every frame, so a pos delta is always 0 -- the old bug
                    // that left m_ClickCallback unfired (no screen change) on a sliced menu fruit.
                    _Vector3<float> d;
                    d.x = f->vel.x - f->m_SecondVel.x;
                    d.y = f->vel.y - f->m_SecondVel.y;
                    d.z = f->vel.z - f->m_SecondVel.z;
                    float magSqr = d.x*d.x + d.y*d.y + d.z*d.z;
                    if (magSqr > 0.001f) {   // SLICE_EPS @0x0019ac50 (binary ble-skip => strictly >)
                        // Diagnostic: log where a menu fruit slice registered + fired its callback.
                        LOG_INFO("MENUBTN/Slice",
                                 "FRUIT slice fired: fruitType=%d backKey=%d pos=(%.1f,%.1f) velMag=%.3f",
                                 m_FruitType, (int)m_bRespondsToBackKey,
                                 entity->pos.x, entity->pos.y, magSqr);
                        m_ClickCallback();
                        // v1.6.1 MenuButton::Update @0x0019aa78: thunk 0x00105ec4 -> TutorialControl::ResetTutePos(nullptr)
                        if (game_work.m_TutorialControl) {
                            game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
                        }
                        entity->scale = m_BaseScale;
                        // Binary @ 0x19aa34..0x19ac20 (Site B): set respawn flag if halves are
                        // already at rest on the same frame as the slice (rare; normally Site A
                        // handles it a few frames later once halves decelerate to vel==0).
                        if (fruit && fruit->entityType == 0
                                  && fruit->vel.x == 0.0f && fruit->vel.y == 0.0f) {
                            fruit->m_bDrawWhole = 1;
                        }
                        // m_bClearsMenuItems gate: cascades to clear menu fruits.
                        if (m_bClearsMenuItems) {
                            ClearMenuItems();
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
                // TODO: v1.6.1 MenuButton::Update @0x0019a860 -- Bomb::Enabled() check; use existing Enabled() method
                Bomb* b = static_cast<Bomb*>(m_pEntity);
                if (b && !b->Enabled()) {
                    // Diagnostic: log where a menu bomb slice registered.
                    LOG_INFO("MENUBTN/Slice",
                             "BOMB slice fired: fruitType=%d backKey=%d pos=(%.1f,%.1f)",
                             m_FruitType, (int)m_bRespondsToBackKey,
                             entity->pos.x, entity->pos.y);
                    m_pEntity = nullptr;
                    entity->scale = m_BaseScale;
                }
            }

            // grow-in ease toward 0x3ffc: runs BEFORE entity->scale write so the
            // entity scale on the very first frame (m_BaseScale capture frame) is
            // also ramped, not left at full spawn size.
            // ASM-spec: DAT_0019ac68=109200 / clamp 16380 @ MenuButton::Update
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

            // Apply entity scale using the just-computed size (every frame, including
            // the first). Binary: entity->scale = m_BaseScale * (size.y / m_RestScale.y).
            if (m_pEntity != nullptr) {
                float restY = m_RestScale.y;
                float ratio = (restY != 0.0f) ? (size.y / restY) : 1.0f;
                m_pEntity->scale = m_BaseScale * ratio;
            }
        }
    }

    // ASM-spec v1.6.1 MenuButton::Update @0x0019af54: m_FruitType<0 (toggle) buttons
    //   copy the FULL m_RestScale(+0x13C) Vec3 -> size(+0x20) each frame (x,y,z), then
    //   the icon is scaled from size in HUDControl3d::Draw. Must copy .z too: Draw's
    //   press-dim gate is a full Vec3 compare (size != m_RestScale); writing only x/y
    //   left size.z=0 while m_RestScale.z=1, so the dim fired at rest and toggles
    //   rendered at half-brightness. Touch paths below may override size after this.
    if (m_FruitType < 0) {
        size = m_RestScale;
    }

    // ---- touch handling ----
    if (AcceptsTouch()) {
        // ASM-spec v1.6.1 MenuButton::Update @0x0019ad14 -- BACK-KEY force-slice path.
        // The menu fruit IS reached by the ActorManager blade-vs-sphere loop normally;
        // this block is the binary's separate back-key / pause-input forced slice
        // (m_bFrameDirty = back/pause input latch, set by RegressMenuCallback/ShowPauseMenuCallback).
        // It drives CollisionResponse directly, independently of the blade geometry test.
        if (game_work.m_bFrameDirty && m_bBackdropActive) {
            if (m_pEntity == nullptr) {
                TouchReleased();
            } else {
                _Vector3<float> blade(1.0f, 0.0f, 0.0f);
                m_pEntity->CollisionResponse(nullptr, 0, 0, &blade);
            }
        }
        // ASM-verified: 2026-06-21T09:00:00Z v1.6.1 MenuButton::Update @0x0019a860 (re-analyst):
        // hit rect centered on GetAdjustedPos() (HUDControl slot 15 @0x00136c2c) =
        // pos + Vec3(480,320,0)*m_HudScale -- the SAME anchor the held bomb entity model
        // is drawn at. Using raw pos offset the hit center from the model by that vector.
        _Vector3<float> hitC = GetAdjustedPos();
        float hw = m_RestScale.x * 0.5f;
        float hh = m_RestScale.y * 0.5f;
        const float left   = hitC.x - hw - m_HitInsetX;
        const float right  = hitC.x + hw + m_HitInsetX;
        const float bottom = hitC.y - hh - m_HitInsetY;
        const float top    = hitC.y + hh + m_HitInsetY;

        Mortar::Touch& touch = Mortar::Touch::GetInstance();

        if (m_TouchSlot == -1) {
            int slot = touch.GetTouchInRegion(left, right, bottom, top, -1);
            m_TouchSlot = slot;
            if (slot >= 0) {
                if (IsTouchDown(slot) == 2) {
                    if (!m_bRespondsToBackKey && m_FruitType < 0) {
                        m_ClickCallback();
                    }
                } else {
                    m_TouchSlot = -1;
                }
            }
        } else {
            int down = IsTouchDown(m_TouchSlot);
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
                    if (insideNow) {
                        // v1.6.1 MenuButton::Update @0x0019aeac-0x0019af6c: PRESS_SCALE
                        // shrink while held (literal @0x0019ac6c = 0.95f). Binary sets
                        // size = m_RestScale * 0.95f (full Vec3).
                        size = m_RestScale * 0.95f;
                    } else {
                        size = m_RestScale;
                        // v1.6.1 MenuButton::Update @0x0019af3c: touch-slot detach is
                        // gated on m_bRespondsToBackKey -- buttons that also respond to
                        // the back key keep their touch slot on drag-off.
                        if (!m_bRespondsToBackKey) {
                            m_TouchSlot = -1;
                        }
                    }
                }
            }
        }
    }

    // ---- per-frame derived ----
    // m_BackdropScale @ +0xEC = curScale.x * 1.125 * m_ShakeScale.x (+0x154)
    m_BackdropScale = size.x * 1.125f * m_ShakeScale.x;  // @0x19af70

    if (m_ShakeTimer > 0.0f) {
        m_ShakeTimer -= dt;
        if (m_ShakeTimer < 0.0f) m_ShakeTimer = 0.0f;
    }
}

#ifndef __bada__
// ---------------------------------------------------------------------------
// Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
// Advances the sparkle-ring (m_SparkleTimer) and NEW-badge bounce
// (m_NewIndicatorTimer) phases dt-scaled, once per PRESENTED frame, so both
// animations track the display's actual present rate (60/90/120fps) instead
// of the fixed 60Hz sim tick. Everything else in MenuButton::Update (grow-in
// gate, entity re-anchor, slice detection, touch handling, m_ShakeTimer
// countdown) stays at 60Hz in Update() -- those are one-shot/state-decision
// logic entangled with entity and touch reads, not pure continuous easing.
//
// DIFFERS: v1.6.1 MenuButton::Update @0x0019a860 advances the badge/sparkle
// animations per 60Hz sim tick; port advances them per rendered frame
// (dt-scaled) to track display refresh. __bada__ keeps the faithful 60Hz path.
// ---------------------------------------------------------------------------
void MenuButton::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches
    AdvanceSparkleAndBadge(m_SparkleTimer, m_NewIndicatorTimer, dtSeconds);
}
#endif

// v1.6.1 MenuButton::UpdateTouchPosition @0x0019a518
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
// NOTE: no top-level m_DrawColour.a==0 early-out -- the binary always runs the
// layer 0x40->0x80 demotion side-effect and draws labels/badge/sparkle even at
// alpha 0 (v1.6.1 MenuButton::Draw @0x0019c2e4 has no alpha gate).
void MenuButton::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);

    // Compute fade-derived alpha from m_AnimPhase (+0xD0).
    // v1.6.1 MenuButton::Draw @0x0019c2e4: alpha ramp from m_AnimPhase (Q14 phase, 0..0x3ffc).
    uint8_t alpha;
    if (m_FruitType < 0) {
        alpha = 0xFF;
    } else {
        float n = (float)m_AnimPhase * 256.0f / 16380.0f;
        int   a = (int)n;
        if (a > 255) a = 255;
        if (a < 0)   a = 0;
        alpha = (uint8_t)a;
    }

    // Layer 0 (backdrop): scratchs.tex at layer 0x40, then demote to 0x80
    // ASM-verified: 2026-05-06T16:00 v1.6.1 MenuButton::Draw @ 0x0019c2e4 Phase A (asm-inspector).
    if (m_LayerFlags == (int)Mortar::HUD_LAYER_MENU_BG) {
        m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;
        if (s_TexScratchs.IsValid()) {
            MatrixManager& mm = MatrixManager::GetInstance();
            // ASM-spec v1.6.1 MenuButton::Draw @0x0019c39c: ldrb +0xF0; cmp #0;
            // vmov.f32 s0,#-1.0; vmoveq.f32 s0,#1.0 -- byte compare, not a float sign test.
            const float sx = m_RandomOffset ? -1.0f : 1.0f;
            Matrix44 mat = Matrix44::MakeScale(sx * m_BackdropScale,
                                               m_BackdropScale,
                                               m_BackdropScale);
            // Binary MenuButton::Draw @0x0019c2e4 uses vtable slot 15 GetAdjustedPos @0x136c2c
            // (= pos + Vec3(480,320,0)*m_HudScale), NOT raw pos. The quit-bomb button stores
            // its whole position in m_HudScale (pos=(0,0,0)), so raw pos put the scratch at
            // screen center; the other buttons keep their coords in pos so it was masked.
            _Vector3<float> adjPos = GetAdjustedPos();
            mat.GlobalTranslate44(_Vector3<float>(adjPos.x, adjPos.y, -5500.0f));
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Colour tint(255, 255, 255, alpha);
            s_TexScratchs->Set();
            Mortar::Mesh::DrawQuadUnCached(tint, 0.0f, 1.0f, 0.0f, 1.0f, NULL);
            s_TexScratchs->UnSet();
        }
        return;
    }

    // ---- main button quad (inlined) ----
    // ASM-spec v1.6.1 MenuButton::Draw @0x0019c48c-0x0019c700: the binary inlines
    // the quad draw (HUDControl3d::Draw-shaped) rather than calling the base,
    // adding on top of it: the shake jitter, the press-dim RGB tint and the
    // anim-alpha override. There is NO m_DrawColour.a==0 gate here (unlike
    // v1.6.1 HUDControl3d::Draw @0x0018b544).
    if (m_Texture.IsValid()) {
        m_Texture->Set();
        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();

        Matrix44 mat = Matrix44::MakeScale(size.x, size.y, size.z);
        if (m_Timer != 0.0f) {
            uint16_t idx = (uint16_t)(int)(m_Timer * 182.0f);
            mat.RotZ44(SinIdx(idx), CosIdx(idx));
        }

        _Vector3<float> jitter = _Vector3<float>::Zero();
        if (m_ShakeTimer > 0.0f) {
            // ASM-spec v1.6.1 MenuButton::Draw @0x0019c2e4 (outlined helper
            // T.1164 @0x0019b414): Math::g_random.RandF(6.0) x2 -> (r1-3, r2-3, 0)
            const float jx = Math::g_Random.RandF(6.0f) - 3.0f;
            const float jy = Math::g_Random.RandF(6.0f) - 3.0f;
            jitter += _Vector3<float>(jx, jy, 0.0f);
        }
        mat.GlobalTranslate44(GetAdjustedPos() + jitter);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();

        // Press-dim @0x0019c5c4-0x0019c620: RGB * 0.5 while held/shrunk (or
        // touch disabled) on non-fruit (toggle) buttons only.
        float dim[3] = { 1.0f, 1.0f, 1.0f };
        if ((size != m_RestScale || m_bAcceptsTouch == 0) && m_FruitType < 0) {
            dim[0] = dim[1] = dim[2] = 0.5f;
        }
        Colour quadCol = Colour::TintColour(m_DrawColour, dim);
        const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
        quadCol = Colour::TintColour(quadCol, tintRGB);

        // Anim-alpha @0x0019c648-0x0019c690: a = m_DrawColour.a * animAlpha / 255.
        float fa = ((float)m_DrawColour.a * (float)alpha) / 255.0f;
        quadCol.a = (fa <= 0.0f) ? 0 : (fa < 255.0f ? (uint8_t)fa : 0xff);

        // Defunct: P2P multiplayer per-player tint -- when m_PlayerColour != White
        // the binary copies quadCol into a dead stack temp (Colour::operator=
        // @0x001119ac); result discarded, no observable effect.
        // v1.6.1 MenuButton::Draw @ 0x0019c694

        Mortar::Mesh::DrawQuadUnCached(quadCol, m_UVLeft, m_UVRight, m_UVTop,
                                       m_UVBottom, NULL);
        m_Texture->UnSet();
    }

    // Label block v1.6.1 MenuButton::Draw @0x0019c764:
    // anchor = GetAdjustedPos() + m_DrawOffset; draw order glow->fg->shadow;
    // when m_LabelRadius>0 each label also drawn a 2nd time at rotZ+180.
    if (m_pLabelFg != nullptr) {
        _Vector3<float> anchor = GetAdjustedPos();
        anchor.x += m_DrawOffset.x;
        anchor.y += m_DrawOffset.y;
        anchor.z += m_DrawOffset.z;

        // scale = size.y / m_RestScale.y (+0x140); rotZ = m_Timer (+0x2c)
        float restY = m_RestScale.y;
        float scaleF = (restY != 0.0f) ? (size.y / restY) : 1.0f;
        float rotZ = m_Timer;
        _Vector2<float> scaleV(scaleF, scaleF);

        // v1.6.1 MenuButton::Draw @0x0019c764: glow/shadow layers align to the FG-label
        // bbox (BakedStringTTF::UpdateBounds @0x00247ed0 populates m_pLabelFg's refRect at
        // BuildSurfaces time, i.e. in its ctor).
        Mortar::MortarRectangleT<long>* labelRR = m_pLabelFg->GetRefRect();
        if (m_pLabelGlow) {
            m_pLabelGlow->Draw(anchor, scaleV, rotZ, Mortar::ALIGN_CENTRE, labelRR);
            if (m_LabelRadius > 0.0f)
                m_pLabelGlow->Draw(anchor, scaleV, rotZ + 180.0f, Mortar::ALIGN_CENTRE, labelRR);
        }
        m_pLabelFg->Draw(anchor, scaleV, rotZ, Mortar::ALIGN_CENTRE);
        if (m_LabelRadius > 0.0f)
            m_pLabelFg->Draw(anchor, scaleV, rotZ + 180.0f, Mortar::ALIGN_CENTRE);
        if (m_pLabelShadow) {
            m_pLabelShadow->Draw(anchor, scaleV, rotZ, Mortar::ALIGN_CENTRE, labelRR);
            if (m_LabelRadius > 0.0f)
                m_pLabelShadow->Draw(anchor, scaleV, rotZ + 180.0f, Mortar::ALIGN_CENTRE, labelRR);
        }
    }

    // ASM-verified-candidate v1.6.1 MenuButton::Draw NEW-badge block @0x0019c858:
    //  popup = pM_Popups[0x10] ("NEW" badge, IngamePopup ctor @0x0016dbac);
    //  bob = |SinIdx((int)(m_NewIndicatorTimer*180*182) & 0xffff)| * 8.0 (looping half-sine, 2 bounces/s);
    //  anchor = GetAdjustedPos() + (ShakeScale.y*RestScale.x*0.5, ShakeScale.z*RestScale.y*0.5 + bob, 0);
    //  scale = size.x / RestScale.x. Constants @0x0019c730 (180.0f) / 0x0019c734 (182.0f).
    if (m_NewIndicatorTimer >= 0.0f) {
        IngamePopup* popup = GetIngamePopup(0x10);
        if (popup) {
            float scale = (m_RestScale.x != 0.0f) ? (size.x / m_RestScale.x) : 0.0f;
            // v1.6.1 MenuButton::Draw @0x0019c2e4: SinIdx angle index wraps mod 65536
            // (binary: `(int)(timer*180.0*182.0) & 0xffff`). float->u32->u16 replicates that
            // wrap; a direct float->u16 cast is UB on native and TRAPS on wasm when the
            // product exceeds 65535 (#346).
            const float sinV = SinIdx((uint16_t)(uint32_t)(m_NewIndicatorTimer * 180.0f * 182.0f));
            const float bob  = (sinV < 0.0f ? -sinV : sinV) * 8.0f;
            _Vector3<float> anchor = GetAdjustedPos();
            anchor.x += m_ShakeScale.y * m_RestScale.x * 0.5f;
            anchor.y += m_ShakeScale.z * m_RestScale.y * 0.5f + bob;
            popup->Draw(anchor, scale);
        }
    }

    // ---- loading-sparkle ring ----
    // ASM-spec v1.6.1 MenuButton::Draw @0x0019c98c-0x0019cd04: 8-blade 48-vert
    // tri list built ONCE into function-local statics (binary locals
    // Draw::made_mesh / Draw::symbo_tris); per-frame the 8 blade greys rotate
    // with frame = 7 - ((int)m_SparkleTimer % 8). Texture = class-static slot 2
    // (blurry_backing.tex) via GOT @0x0019c728.
    if (m_SparkleTimer >= 0.0f && s_TexBlurryBacking.IsValid()) {
        static bool made_mesh = false;
        static QUADCUSTOMVERTEX symbo_tris[48];
        int frame = 7 - (((int)m_SparkleTimer) % 8);
        if (!made_mesh) {
            made_mesh = true;
            // Blade geometry: per blade at angle index a (45 deg = 0x1ffe in the
            // binary's 182-per-degree domain, 8 blades -> 0xfff0 total):
            //   outer = (SinIdx(a), CosIdx(a)) * 0.5
            //   inner = outer * 0.6                      (const @0x0019c73c)
            //   half  = (SinIdx(a+90deg), CosIdx(a+90deg)) * 0.075  (@0x0019c738)
            // Two tris per blade: (outer-half, outer+half, inner-half) and
            // (inner-half, outer+half, inner+half); UVs (0,0)/(1,0)/(0,1) and
            // (0,1)/(1,0)/(1,1). z=0, nz=1 on all verts (nx/ny stay zero-init).
            int vi = 0;
            uint16_t angle = 0;
            do {
                const float outerX = SinIdx(angle) * 0.5f;
                const float outerY = CosIdx(angle) * 0.5f;
                const uint16_t perp = (uint16_t)(angle + 0x3ffc);  // +90 deg
                const float halfX = SinIdx(perp) * 0.075f;
                const float halfY = CosIdx(perp) * 0.075f;
                const float innerX = outerX * 0.6f;
                const float innerY = outerY * 0.6f;
                QUADCUSTOMVERTEX* q = &symbo_tris[vi];
                q[0].x = outerX - halfX; q[0].y = outerY - halfY; q[0].u = 0.0f; q[0].v = 0.0f;
                q[1].x = outerX + halfX; q[1].y = outerY + halfY; q[1].u = 1.0f; q[1].v = 0.0f;
                q[2].x = innerX - halfX; q[2].y = innerY - halfY; q[2].u = 0.0f; q[2].v = 1.0f;
                q[3].x = innerX - halfX; q[3].y = innerY - halfY; q[3].u = 0.0f; q[3].v = 1.0f;
                q[4].x = outerX + halfX; q[4].y = outerY + halfY; q[4].u = 1.0f; q[4].v = 0.0f;
                q[5].x = innerX + halfX; q[5].y = innerY + halfY; q[5].u = 1.0f; q[5].v = 1.0f;
                for (int j = 0; j < 6; ++j) { q[j].z = 0.0f; q[j].nz = 1.0f; }
                vi += 6;
                angle = (uint16_t)(angle + 0x1ffe);  // 45 deg step
            } while (angle != 0xfff0);
        }
        // Per-blade brightness: clamp(((frame + k) mod 8) * 0x20, 0x40, 0xff),
        // alpha 200. The binary ALSO computes TintColour(bladeCol, hudScale) and a
        // copy of it into dead stack temps (@0x0019cbc4-0x0019cbf8) -- results
        // unused; the vertex colour written is the UNTINTED bladeCol.
        int blade = frame;
        for (int vi = 0; vi < 0x30; vi += 6, ++blade) {
            int bright = (blade % 8) * 0x20;
            if (bright > 0xff) bright = 0xff;
            if (bright < 0x40) bright = 0x40;
            Colour bladeCol((uint8_t)bright, (uint8_t)bright, (uint8_t)bright, 200);
            for (int j = 0; j < 6; ++j) {
                symbo_tris[vi + j].colour = bladeCol.PlatformColour();
            }
        }
        s_TexBlurryBacking->Set();
        MatrixManager& mm = MatrixManager::GetInstance();
        mm.GetWorldStack().Reset();
        // Scale = Vector3::One * size.y * 0.75 (0.75 immediate @0x0019cc74),
        // then Translate(GetAdjustedPos()) on the world stack.
        _Vector3<float> ringScale = _Vector3<float>::One() * size.y * 0.75f;
        mm.GetWorldStack().Scale(ringScale);
        mm.GetWorldStack().Translate(GetAdjustedPos());
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawTriList(symbo_tris, 0x30, false, NULL);
        s_TexBlurryBacking->UnSet();
    }
}

// ASM-verified: 2026-05-06T00:00 v1.6.1 MenuButton::LoadContent @ 0x0019c1a0 (asm-inspector)
void MenuButton::LoadContent() {
    s_TexScratchs      = Mortar::TextureManager::LoadLocalisedTexture("scratchs.tex");
    s_TexBlurryBacking = Mortar::TextureManager::LoadLocalisedTexture("blurry_backing.tex");
    s_TexNewItem       = Mortar::TextureManager::LoadLocalisedTexture("new_item.tex");
}

// ASM-verified: 2026-05-06T00:00 v1.6.1 MenuButton::UnLoadContent @ 0x0019c2a0 (asm-inspector)
void MenuButton::UnLoadContent() {
    s_TexScratchs.SetNull();
    s_TexBlurryBacking.SetNull();
    s_TexNewItem.SetNull();
}

// Accessor for GameOverScreen::DrawOrder state-0xe spinner halo.
// v1.6.1 GameOverScreen::DrawOrder @0x00186484 uses this static.
Mortar::SmartPtr<Mortar::Texture>& MenuButton::GetSparkleRingTex() {
    return s_TexBlurryBacking;
}

// v1.6.1 MenuButton::AddPiece @0x0019cd34 (thunk 0x00105524)
void MenuButton::AddPiece(Mortar::SmartPtr<Mortar::Texture> tex, _Vector2<float>* uvOverride,
                          float rotSpeed, float initialTimer,
                          _Vector3<float> offset, _Vector3<float> sizeScale,
                          Colour tint, int layerFlags) {
    HUDControl3d* c = new HUDControl3d();
    c->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(
        this, &MenuButton::DeletedPiece);
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
        const float texW  = tex.IsValid() ? (float)tex->GetWidth()  : 0.0f;
        const float texH  = tex.IsValid() ? (float)tex->GetHeight() : 0.0f;
        sizeScale = _Vector3<float>(texW * uSpan * sizeScale.z,
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
    addOn.m_pControl      = c;
    addOn.m_RotationSpeed = rotSpeed;
    addOn.m_Scale         = sizeScale;
    addOn.m_Offset        = offset;
    m_AddOns.push_back(addOn);
}

// v1.6.1 MenuButton::UpdatePieces @0x0019a630
void MenuButton::UpdatePieces(float dt) {
    float restY = m_RestScale.y;
    float ratio = (restY > 0.0f && size.y > 0.0f) ? (size.y / restY) : 1.0f;
    for (std::list<MenuButtonAddOn>::iterator it = m_AddOns.begin();
         it != m_AddOns.end(); ++it) {
        HUDControl3d* c = it->m_pControl;
        if (!c) continue;
        c->m_Timer += dt * it->m_RotationSpeed;
        c->pos = pos + it->m_Offset * ratio;
        c->size = it->m_Scale * ratio;
    }
}

// v1.6.1 MenuButton::DeletePieces @0x0019cf84
void MenuButton::DeletePieces() {
    for (std::list<MenuButtonAddOn>::iterator it = m_AddOns.begin();
         it != m_AddOns.end(); ++it) {
        HUDControl3d* c = it->m_pControl;
        if (c) {
            c->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>();
            c->m_bPendingRemoval = 1;
        }
    }
    m_AddOns.clear();
}

// v1.6.1 MenuButton::DeletedPiece @0x0019a728
void MenuButton::DeletedPiece(HUDControl* hudControl) {
    for (std::list<MenuButtonAddOn>::iterator it = m_AddOns.begin();
         it != m_AddOns.end(); ++it) {
        if (it->m_pControl == hudControl) {
            m_AddOns.erase(it);
            return;
        }
    }
}

// ASM-spec v1.6.1 MenuCallbackClicked @0x19a620: empty no-op default menu-click callback.
void MenuCallbackClicked() {}
