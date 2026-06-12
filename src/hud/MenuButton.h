#ifndef FN_MENU_BUTTON_H
#define FN_MENU_BUTTON_H

//
// MenuButton : HUDControl3d (size = 0x178, leaf class)
// RE spec v1.6.1
//
// 3-layer rendering + 1 entity:
//   Layer 0 (3D): Spinning fruit entity (NOT drawn by MenuButton — Mortar::ActorManager::Draw)
//   Layer 1 (2D): Button texture quad (+0x74)
//   Layer 2 (2D): "New item" star indicator (+0xFC)
//   Layer 3 (2D): Sparkle ring (+0xF8)
//

#include "HUDControl3d.h"
#include "engine/asset/Texture.h"
#include "engine/math/Vec2.h"
#include "engine/util/Delegate.h"
#include "engine/util/SmartPtr.h"
#include <cstdint>
#include <list>

namespace Mortar { class Entity; }
class Fruit;

// MenuButtonAddOn — child sprite metadata for AddPeice/UpdatePeices.
// 24 bytes; copy-constructible POD. Stored in std::list<MenuButtonAddOn> m_AddOns at MenuButton +0x10C.
// Binary @ 0x00150240 (AddPeice)
struct MenuButtonAddOn {
    HUDControl3d* control;   // +0x00
    Vec2*         texCoord;  // +0x04 (param_5 from AddPeice; usually NULL)
    Vec3          offset;    // +0x08 local position relative to parent
    Vec3          sizeScale; // +0x14 local size multiplier
    // NOTE: offset.y is reused as per-frame angular velocity by UpdatePeices
};

// Matches ClearMenuItems @ 0x0016ac7c. Cascades release on every active
// menu fruit/bomb: sets m_bSliced + outward random velocity on fruits,
// disables+flings bombs, sets m_bDrawWhole/m_bMovement. MenuButtons
// whose entity is released this way detect velSq > 0.001 and start the
// FadeCounter shrink. Called from MenuButton::Update on user slice of a
// fruit button, and from GameModeScreen::QuitCallback so the back-out
// animation mirrors the main -> mode-select cascade.
namespace FN { void ClearMenuItems(); }

// ASM-verified: 2026-04-29T00:00Z binary @ 0x0014ee40 + 0x0014e614 + 0x0014f7e0 (asm-inspector, base-shift unaffected)
class MenuButton : public HUDControl3d {
public:
    // +0x7C: TODO: re-verify MenuButton +0x7C field from binary (4-byte gap between base end and m_pEntity)
    uint32_t m_Pad_0x7C;

    // +0x80: real Fruit/Bomb entity spinning on button (nullptr for toggles)
    Mortar::Entity* m_pEntity;

    // +0x84: -1 = no fruit, 0+ = fruit index, >=bombThreshold = bomb
    int m_FruitType;

    // +0x88: fired on touch release. 36 bytes (binary Mortar::Delegate0).
    Mortar::Delegate0<void> m_ClickCallback;

    // +0xAC: fired when button removed from HUD. 36 bytes (binary Mortar::Delegate0).
    Mortar::Delegate0<void> m_DeletedCallback;

    // +0xD0: 16-bit Q14 angle (0..0x3ffc = 0..pi/2) feeding Math::SinIdx for grow-in AND shrink-out quarter-sine ease. Was m_FadeCounter (misleading: not a fade).
    int m_AnimPhase;

    // +0xD4: padding / reserved (binary has a field here we haven't RE'd yet)
    int m_fieldD4;

    // +0xD8: slot index currently tracking a touch (-1 = none).
    // Matches binary MenuButton::Update touch block (0x0014e614).
    int m_TouchSlot;

    // +0xDC/+0xE0/+0xE4: last-known touch x/y/phase copied by
    // UpdateTouchPosition from the tracked slot each frame.
    float m_TouchX;
    float m_TouchY;
    float m_TouchPhase;

    // +0xE8: random visual offset (-20 to +20)
    float m_RandomOffset;

    // +0xEC: scratchs.tex backdrop per-instance scale. Binary @ 0x0014eb84
    // (in MenuButton::Update tail) computes this every frame as
    //   m_BackdropScale = size.x * 1.125f * m_AnimScale
    // and Draw Phase A @ 0x0014fa86 multiplies the (sx, 1, 1) flip-vector
    // by *(this+0xEC) before building the Scale44 matrix. The earlier
    // claim that this field was unwritten / dead was a re-analyst miss --
    // the audit at 0x0014eb68..0x0014eb84 (asm-inspector 2026-05-06)
    // confirmed the per-frame write. The scratchs.tex backdrop IS visible
    // in the shipped binary; the port now writes it in Update too.
    float m_BackdropScale;

    // +0xF0: random horizontal flip
    bool m_bFlipped;

    // +0xF4: rotates the 2D button quad via m_Timer (8-12 deg/s, random sign)
    // m_Timer (HUDControl +0x2c) accumulates: m_Timer += dt * m_RotationSpeed
    float m_RotationSpeed;

    // +0xF8: >= 0 = sparkle ring active
    float m_SparkleTimer;

    // +0xFC: >= 0 = "new" star indicator active
    float m_NewIndicatorTimer;

    // +0x100 — entity base scale captured on first frame; Update writes entity->scale = m_BaseScale * sizeFrac. Was m_HitBoundsScale (misleading).
    Vec3 m_BaseScale;

    // +0x10C: child sprite list for AddPeice/UpdatePeices/DeletePeices.
    // Binary: std::list<MenuButtonAddOn> = 8 bytes (Sourcery 2010q1 pre-C++11).
    std::list<MenuButtonAddOn> m_AddOns;   // +0x10C

    // +0x114, +0x118: text labels (original: BakedString* fg / shadow).
    // RE'd 2026-04-29: MenuButton::SetText (0x0014ebc0) is the only
    // writer, and the binary contains ZERO call sites for it. Both
    // pointers are always NULL at runtime; the label-render block in
    // MenuButton::Draw (0x0015015e..0x0015020a) is dead code in the
    // shipped Bada build. The port intentionally does not render labels.
    // See docs/engine/baked-string.md for the BakedString spec if a
    // reused-elsewhere consumer surfaces.
    void* m_pLabel1;   // +0x114 dead in shipped binary
    void* m_pLabel2;   // +0x118 dead in shipped binary

    // +0x11C: unnamed 4-byte field (default 0). Purpose not yet RE'd.
    uint32_t m_field11C;

    // +0x120
    uint8_t m_bScoreSubmitted;

    // +0x121 — set to 0 by KeyboardControl::Update for press-edge fire (keyboard keys);
    //           default 1 for normal/toggle buttons (fire on release).
    //           Real semantics: m_bFireOnRelease.
    uint8_t m_bFireOnRelease;

    // +0x122: = 1 — accepts touch input
    uint8_t m_bInteractive;

    // +0x123: = 1
    uint8_t m_bEnabled;

    // +0x124: hit-test bounds target (lerped toward). x=+0x124, y=+0x128.
    // +0x12C (z) aliases the multiplayer colour tint (m_PlayerIndex) for
    // MP mode; Init writes hitBounds.z into the field and MP colour lives
    // in the same 4 bytes. Only x/y are used for hit-testing.
    Vec3 m_TargetSize;

    // +0x130: unnamed gate byte written 0 by Init; purpose not yet RE'd.
    uint8_t m_field130;

    // +0x131: Touch-held state gate; set on press, cleared when finger leaves rect or releases. Was m_bHighlighted (misleading: not visual state).
    uint8_t m_bTouchHeld;

    // +0x132..+0x133: padding
    uint8_t m_pad132[2];

    // +0x134: startup countdown (=0.25f). While >0, sets fruit piece flag
    // bit 0 and early-returns from Update. Decremented each frame.
    float m_StartupTimer;

    // +0x138: when 1, this button auto-fires its click delegate when the
    // hardware Back/Menu key (Game::m_BackKeyPressed at +0x604) is set.
    // Default 0 (set by Init); screen creation code opts a single button
    // per screen into this role.
    uint8_t m_bRespondsToBackKey;

    // +0x139: default 1. Controls swipe-release behaviour.
    uint8_t m_bSwipeReleaseEnabled;

    // +0x13A: default 1. Fire-tutorial gate flag.
    uint8_t m_bFireTutorial;

    // +0x13B: padding
    uint8_t m_pad13B;

    // +0x13C: hit bounds scale factor (Vec3, 12 bytes).
    Vec3 m_HitBoundsScale;

    // +0x148: true if hitBounds > 0 (moved from +0x130)
    bool m_bHasHitArea;

    // +0x149: default 1. Hit-test enable flag.
    uint8_t m_bDoHitTest;

    // +0x14A..+0x14B: padding
    uint8_t m_pad14A[2];

    // +0x14C: direct fruit reference for scale/rotate access (pointer, moved from +0x134)
    Fruit* m_pFruitPiece;

    // +0x150: gate byte (purpose not yet RE'd)
    uint8_t m_field150;

    // +0x151..+0x153: padding
    uint8_t m_pad151[3];

    // +0x154: per-button backdrop scale factor (=1.0 in Init, =0.5 in CreateButtons).
    // Multiplied into the per-frame m_BackdropScale computation.
    // RE spec v1.6.1 names this m_BackdropScaleFactor; port keeps m_AnimScale
    // for call-site stability (MainScreen, DojoScreen set it by name).
    float m_AnimScale;

    // +0x158: for "new" indicator bounce (moved from +0x140, values unchanged: 0.85, 0.85, 0)
    Vec3 m_BounceParams;

    // +0x164: > 0 = shaking (random ±3.0 offset). Moved from +0x158.
    float m_ShakeTimer;

    // +0x168: touch-area X inset (grace zone, default 5px). Moved from +0x14C.
    float m_HitInsetX;

    // +0x16C: touch-area Y inset (grace zone, default 5px). Moved from +0x150.
    float m_HitInsetY;

    // +0x170: unnamed float (=100.0 in Init). Consumer is in Draw (not yet fully RE'd).
    float m_field170;

    // +0x174: unnamed float (=0.0 in Init). Decremented in Update tail.
    float m_field174;

    MenuButton();

    // Binary ctor @ 0x0014f24c — construction-time init with all parameters.
    // tex may be NULL (entity visuals via fruitType take precedence when fruitType >= 0).
    // onTap binds to m_ClickCallback; onRemove binds to m_RemoveCallback.
    // Proxies to MenuButton::Init(5-arg) then wires the remove callback.
    MenuButton(Mortar::SmartPtr<Mortar::Texture>* tex, Vec3* spawnPos,
               Mortar::Delegate0<void>* onTap,
               int fruitType, Vec3* restPos,
               Mortar::Delegate1<void, HUDControl*>* onRemove);

    ~MenuButton();

    // HUDControl overrides
    void Init() override;
    void Reset() override;
    void BeginDraw(float dt) override;
    void PreDraw(const Vec3& hudScale) override;
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override;
    void Release() override;
    void Skip() override;
    bool SetToMultiplayerState() override;

    // Matches MenuButton::Init (0x0014ee40, 222 lines)
    // Creates entity, sets callbacks, random rotation
    void Init(Vec3 buttonPos, Mortar::Delegate0<void> clickCb,
              int fruitType, Vec3 hitBounds,
              Mortar::Delegate0<void> deletedCb);

    // Matches MenuButton::SetNewSymbol (0x0014e404).
    void SetNewSymbol(bool show);

    // Binary @ 0x0014e3bc — sets m_ShakeTimer (zero call sites in binary)
    void Shake(float t);

    // Binary @ 0x0014e434 — returns (m_NewIndicatorTimer >= 0)
    bool HasNewSymbol();

    // Binary @ 0x0014e484 — returns (m_SparkleTimer >= 0); dead in shipped binary
    bool IsLoadingSymbol();

    // Binary @ 0x0014e45c — arms sparkle timer; dead in shipped binary
    void SetLoadingSymbol(bool show);

    // Binary @ 0x0014ebc0 — builds curved-text BakedString pair; zero call sites in shipped binary
    void SetText(const char* text, Colour fg, Colour shadow, float radius);

    // Binary @ 0x0014ed18 — release fruit piece with upward fling; dead in shipped binary
    void Remove();

    // Binary @ 0x0014e5cc — fires m_ClickCallback (toggles only) + m_DeletedCallback (always)
    bool TouchReleased();

    // Binary @ 0x00150240 — spawn child HUDControl3d sprite, attach to HUD + m_AddOns list
    void AddPeice(Mortar::SmartPtr<Mortar::Texture> tex, Vec2* uvOverride,
                  float rotSpeed, float initialTimer,
                  Vec3 offset, Vec3 sizeScale,
                  Colour tint, int layerFlags);

    // Binary @ 0x0014e49c — per-addon position/size update
    void UpdatePeices(float dt);

    // Binary @ 0x0014f74c — detach and mark addons for HUD removal
    void DeletePeices();

    // Binary @ 0x0014e54c — addon's HUD-side removal callback
    void DeletedPeice(HUDControl* hudControl);

    // Replaces m_ClickCallback. Used by ScreenButton::ShrinkButtonCall
    // (binary @ 0x001300f0) to swap a button's tap handler from the
    // normal action to the shrink-and-disappear handler.
    void SetCallback(const Mortar::Delegate0<void>& cb) { m_ClickCallback = cb; }

    // Matches MenuButton::LoadContent (0x0014f674) — loads 3 shared textures
    // into class statics. Called once from GameInitialise step 23.
    static void LoadContent();
    static void UnLoadContent();

private:
    // Matches binary MenuButton::UpdateTouchPosition (0x0014e3c4). Copies
    // x/y/phase from the currently tracked Touch slot into m_TouchX/Y/Phase.
    void UpdateTouchPosition();
};

#ifdef __bada__
static_assert(__builtin_offsetof(MenuButton, m_pEntity)            == 0x80,  "MenuButton m_pEntity offset");
static_assert(__builtin_offsetof(MenuButton, m_AnimPhase)          == 0xD0,  "MenuButton m_AnimPhase offset");
static_assert(__builtin_offsetof(MenuButton, m_AddOns)             == 0x10C, "MenuButton m_AddOns offset");
static_assert(__builtin_offsetof(MenuButton, m_TargetSize)         == 0x124, "MenuButton m_TargetSize offset");
static_assert(__builtin_offsetof(MenuButton, m_StartupTimer)       == 0x134, "MenuButton m_StartupTimer offset");
static_assert(__builtin_offsetof(MenuButton, m_bSwipeReleaseEnabled) == 0x139, "MenuButton m_bSwipeReleaseEnabled offset");
static_assert(__builtin_offsetof(MenuButton, m_bFireTutorial)      == 0x13A, "MenuButton m_bFireTutorial offset");
static_assert(__builtin_offsetof(MenuButton, m_HitBoundsScale)     == 0x13C, "MenuButton m_HitBoundsScale offset");
static_assert(__builtin_offsetof(MenuButton, m_bHasHitArea)        == 0x148, "MenuButton m_bHasHitArea offset");
static_assert(__builtin_offsetof(MenuButton, m_bDoHitTest)         == 0x149, "MenuButton m_bDoHitTest offset");
static_assert(__builtin_offsetof(MenuButton, m_pFruitPiece)        == 0x14C, "MenuButton m_pFruitPiece offset");
static_assert(__builtin_offsetof(MenuButton, m_AnimScale)          == 0x154, "MenuButton m_AnimScale offset");
static_assert(__builtin_offsetof(MenuButton, m_BounceParams)       == 0x158, "MenuButton m_BounceParams offset");
static_assert(__builtin_offsetof(MenuButton, m_ShakeTimer)         == 0x164, "MenuButton m_ShakeTimer offset");
static_assert(__builtin_offsetof(MenuButton, m_HitInsetX)          == 0x168, "MenuButton m_HitInsetX offset");
static_assert(__builtin_offsetof(MenuButton, m_HitInsetY)          == 0x16C, "MenuButton m_HitInsetY offset");
static_assert(__builtin_offsetof(MenuButton, m_field170)           == 0x170, "MenuButton m_field170 offset");
static_assert(__builtin_offsetof(MenuButton, m_field174)           == 0x174, "MenuButton m_field174 offset");
static_assert(sizeof(MenuButton) == 0x178, "MenuButton sizeof mismatch");
#endif

#endif
