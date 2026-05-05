#ifndef FN_MENU_BUTTON_H
#define FN_MENU_BUTTON_H

//
// MenuButton : HUDControl3d (size = 0x15C, leaf class)
// Reimplemented from docs/structs/gameplay-misc.md
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
    // +0x80: real Fruit/Bomb entity spinning on button (nullptr for toggles)
    Mortar::Entity* m_pEntity;

    // +0x84: -1 = no fruit, 0+ = fruit index, >=bombThreshold = bomb
    int m_FruitType;

    // +0x88: fired on touch release. 36 bytes (binary Delegate0).
    Mortar::Delegate<void()> m_ClickCallback;

    // +0xAC: fired when button removed from HUD. 36 bytes (binary Delegate0).
    Mortar::Delegate<void()> m_DeletedCallback;

    // +0xD0: drives alpha fade (× 1000 / 255)
    int m_FadeCounter;

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

    // +0xF0: random horizontal flip
    bool m_bFlipped;

    // +0xF4: rotates the 2D button quad via m_Timer (8-12 deg/s, random sign)
    // m_Timer (HUDControl +0x2c) accumulates: m_Timer += dt * m_RotationSpeed
    float m_RotationSpeed;

    // +0xF8: >= 0 = sparkle ring active
    float m_SparkleTimer;

    // +0xFC: >= 0 = "new" star indicator active
    float m_NewIndicatorTimer;

    // +0x100: hit-test bounds scale from constructor
    Vec3 m_HitBoundsScale;

    // +0x10C: child sprite list for AddPeice/UpdatePeices/DeletePeices.
    // Binary: std::list<MenuButtonAddOn> = 8 bytes (Sourcery 2010q1 pre-C++11).
    std::list<MenuButtonAddOn> m_AddOns;   // +0x10C

    // +0x118, +0x11C: text labels (original: BakedString* fg / shadow).
    // RE'd 2026-04-29: MenuButton::SetText (0x0014ebc0) is the only
    // writer, and the binary contains ZERO call sites for it. Both
    // pointers are always NULL at runtime; the label-render block in
    // MenuButton::Draw (0x0015015e..0x0015020a) is dead code in the
    // shipped Bada build. The port intentionally does not render labels.
    // See docs/engine/baked-string.md for the BakedString spec if a
    // reused-elsewhere consumer surfaces.
    void* m_pLabel1;   // dead in shipped binary
    void* m_pLabel2;   // dead in shipped binary

    // +0x11C: for multiplayer colour tint
    int m_PlayerIndex;

    // +0x120
    uint8_t m_bScoreSubmitted;

    // +0x121: = 1 after Init
    uint8_t m_bVisible;

    // +0x122: = 1 — accepts touch input
    uint8_t m_bInteractive;

    // +0x123: = 1
    uint8_t m_bEnabled;

    // +0x124: hit-test bounds target (lerped toward)
    Vec3 m_TargetSize;

    // +0x130: true if hitBounds > 0
    bool m_bHasHitArea;

    // +0x131: affects tint (0.5 vs 1.0 alpha)
    uint8_t m_bHighlighted;

    // +0x134: direct fruit reference for scale/rotate access
    Fruit* m_pFruitPiece;

    // +0x138: when 1, this button auto-fires its click delegate when the
    // hardware Back/Menu key (Game::m_BackKeyPressed at +0x604) is set.
    // Default 0 (set by Init); screen creation code opts a single button
    // per screen into this role. See docs/engine/menubutton-138.md.
    uint8_t m_bRespondsToBackKey;

    // +0x13C: = 1.0
    float m_AnimScale;

    // +0x140: for "new" indicator bounce
    Vec3 m_BounceParams;

    // +0x14C: = 5.0
    float m_AnimSpeed2;

    // +0x150: = 5.0
    float m_AnimSpeed;

    // +0x154
    float m_field154;

    // +0x158: > 0 = shaking (random ±3.0 offset)
    float m_ShakeTimer;

    MenuButton();
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
    void Init(const Vec3& buttonPos, Mortar::Delegate<void()> clickCb,
              int fruitType, const Vec3& hitBounds,
              Mortar::Delegate<void()> deletedCb);

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
    void AddPeice(SmartPtr<Mortar::Texture> tex, Vec2* uvOverride,
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
    void SetCallback(const Mortar::Delegate<void()>& cb) { m_ClickCallback = cb; }

    // Matches MenuButton::LoadContent (0x0014f674) — loads 3 shared textures
    // into class statics. Called once from GameInitialise step 23.
    static void LoadContent();
    static void UnLoadContent();

private:
    // Matches binary MenuButton::UpdateTouchPosition (0x0014e3c4). Copies
    // x/y/phase from the currently tracked Touch slot into m_TouchX/Y/Phase.
    void UpdateTouchPosition();
};

#endif
