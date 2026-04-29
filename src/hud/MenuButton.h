#ifndef FN_MENU_BUTTON_H
#define FN_MENU_BUTTON_H

//
// MenuButton : HUDControl3d (size = 0x15C, leaf class)
// Reimplemented from docs/structs/gameplay-misc.md
//
// 3-layer rendering + 1 entity:
//   Layer 0 (3D): Spinning fruit entity (NOT drawn by MenuButton — ActorManager::Draw)
//   Layer 1 (2D): Button texture quad (+0x74)
//   Layer 2 (2D): "New item" star indicator (+0xFC)
//   Layer 3 (2D): Sparkle ring (+0xF8)
//

#include "HUDControl3d.h"
#include <functional>
#include <cstdint>

class Entity;
class Fruit;

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
    Entity* m_pEntity;

    // +0x84: -1 = no fruit, 0+ = fruit index, >=bombThreshold = bomb
    int m_FruitType;

    // +0x88: fired on touch release (original: Delegate0<void>)
    // Port specific: std::function instead of Delegate0
    std::function<void()> m_ClickCallback;

    // +0xAC: fired when button removed from HUD (original: Delegate0<void>)
    // Port specific: std::function instead of Delegate0
    std::function<void()> m_DeletedCallback;

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

    // +0x114, +0x118: text labels (original: BakedString*)
    // TODO: implement BakedString
    void* m_pLabel1;
    void* m_pLabel2;

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

    // +0x138
    uint8_t m_bRemovalPending;

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

    // Matches MenuButton::Init (0x0014ee40, 222 lines)
    // Creates entity, sets callbacks, random rotation
    void Init(const Vec3& buttonPos, std::function<void()> clickCb,
              int fruitType, const Vec3& hitBounds,
              std::function<void()> deletedCb);

    // HUDControl overrides
    void Update(float dt) override;
    void Draw(const Vec3& hudScale, int layerMask) override;
    void Release() override;

    // Begin the fade-out-then-self-remove animation. Sets
    // m_bRemovalPending; Update will then decay m_DrawColour.a per
    // frame and set m_bPendingRemoval once alpha hits near zero,
    // at which point HUD::Update deletes the control. Also disables
    // touch input via m_bInteractive so the user can't tap a fading
    // button mid-transition.
    void StartFadeOut();

    // Matches MenuButton::SetNewSymbol (0x0014e404).
    // If show=true and timer<0: sets timer=0.0 (start showing badge).
    // If show=false and timer>=0: sets timer=-1.0 (hide badge).
    void SetNewSymbol(bool show);

    // Matches MenuButton::LoadContent (0x148030) — called once from GameInitialise step 23
    static void LoadContent() {}   // TODO
    static void UnLoadContent() {} // TODO: 0x148660

private:
    // Matches binary MenuButton::UpdateTouchPosition (0x0014e3c4). Copies
    // x/y/phase from the currently tracked Touch slot into m_TouchX/Y/Phase.
    void UpdateTouchPosition();
};

#endif
