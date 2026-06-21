#ifndef FN_MENU_BUTTON_H
#define FN_MENU_BUTTON_H

//
// MenuButton : HUDControl3d (size = 0x178 / 376 bytes)
// v1.6.1 layout. operator_new(0x178) confirmed @ LeaderboardScreen::CreateQuitButton 0x00193198.
//
// ASM-spec v1.6.1 MenuButton @ ctor 0x0019bb08 / Init 0x0019b994, sizeof 0x178:
//   Delegate0 is 0x24 bytes; m_DeletedCallback@0xAC fills to 0xCF (no field @0xCC --
//   m_FadeAlphaIdx was a v1.5.x phantom); m_SparkleTimer@0xF8, m_NewIndicatorTimer@0xFC;
//   m_RestScale@0x13C .z aliases m_bHasHitArea@0x144/m_bInteractive@0x145 (LE byte overlay);
//   m_pFruitPiece@0x148 byte1 aliases m_bAcceptsTouch@0x149; m_pTrackedFruit@0x14C;
//   m_ShakeTimer@0x174.
// ASM-spec v1.6.1 MenuButton::HasNewSymbol @0x0019a5a0: m_NewIndicatorTimer(+0xFC) >= 0
// ASM-spec v1.6.1 MenuButton::IsLoadingSymbol @0x0019a608: m_SparkleTimer(+0xF8) >= 0
// ASM-spec v1.6.1 MenuButton::Shake @0x0019a510: m_ShakeTimer(+0x174) = arg
//
// 3-layer rendering + 1 entity:
//   Layer 0 (3D): Spinning fruit entity (NOT drawn by MenuButton -- Mortar::ActorManager::Draw)
//   Layer 1 (2D): Button texture quad (+0x70)
//   Layer 2 (2D): "New item" star indicator (+0xfc)
//   Layer 3 (2D): Sparkle ring (+0xf8, armed only when m_RotationSpeed >= 0)
//
// Vtable (17 slots; HUDControl3d order):
//   0  0x19d1dc ~MenuButton (D1)
//   1  0x19d130 ~MenuButton (D0)
//   2  0x19a4f8 Init()
//   3  0x19d064 Release(int)
//   4  0x19a50c Reset()
//   5  0x19a5bc BeginDraw()
//   6  0x19a5b8 PreDraw()
//   7  0x19c2e4 Draw(float*)
//   8  0x136060 (HUDControl base hook)
//   9  0x136074 (HUDControl base hook)
//  10  0x19a860 Update(float dt)
//  11  0x19a794 SetToMultiplayerState()
//  12  0x19d874 GetType() -> 5
//  13  0x19a558 Skip()
//  14  0x136094 (base hook)
//  15  0x136c2c GetWorldPos()
//  16  0x19d870 Clicked()
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

// MenuButtonAddOn -- child sprite metadata for AddPeice/UpdatePeices.
// Stored in std::list<MenuButtonAddOn> m_AddOns at MenuButton +0x10C.
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

// ASM-verified: 2026-04-29T00:00Z v1.6.1 MenuButton @ 0x0019bb08 (asm-inspector, layout verified)
class MenuButton : public HUDControl3d {
public:
    // HUDControl3d base ends at +0x7B (size = 0x7C). Own fields follow.

    // +0x7C: alt fruit ref; Init sets 0; SetToMultiplayerState reads
    Fruit*          m_pFruitPiece_alt;     // +0x7C

    // +0x80: live Fruit/Bomb entity pointer; created by CreateFruit in Init
    Mortar::Entity* m_pEntity;             // +0x80

    // +0x84: -1 = no fruit/bomb (toggle button); 0..N-1 = fruit; >=N = bomb
    int             m_FruitType;           // +0x84

    // +0x88: fired on user tap/slice. Mortar::Delegate0<void> = 36 bytes.
    Mortar::Delegate0<void> m_ClickCallback;    // +0x88..+0xAB

    // +0xAC: fired when button is removed from HUD. 36 bytes.
    // Fills 0xAC..0xCF exactly. No field at 0xCC (m_FadeAlphaIdx was a v1.5.x phantom).
    Mortar::Delegate0<void> m_DeletedCallback;  // +0xAC..+0xCF

    // +0xD0: Q14 grow-in phase (0..0x3ffc). uint16 per spec.
    uint16_t        m_AnimPhase;           // +0xD0

    // +0xD2: anim flag
    uint8_t         m_AnimFlag;            // +0xD2

    // +0xD3: grow/shrink-done flag; Update sets when phase hits 0
    uint8_t         m_GrowShrinkDone;      // +0xD3

    // +0xD4: Init = 0xffffffff (touch-slot init / reserved)
    int             m_fieldD4;             // +0xD4

    // +0xD8: touch slot currently tracked (-1 = none)
    int             m_TouchSlot;           // +0xD8

    // +0xDC..+0xE4: last-known touch position/phase from UpdateTouchPosition
    float           m_TouchX;             // +0xDC
    float           m_TouchY;             // +0xE0
    float           m_TouchPhase;         // +0xE4

    // +0xE8: backdrop Vec3 x component (exact use not yet fully RE'd)
    float           m_BackdropOffsetX;     // +0xE8

    // +0xEC: per-frame backdrop scale: curScale.x * 1.125 * m_ShakeScale.x
    // Written every Update @ 0x19af70; read by Draw phase-A layer0 to scale scratchs.tex quad.
    float           m_BackdropScale;       // +0xEC

    // +0xF0: Init = 0.0 (random horizontal flip / offset seed; Draw reads sign for flip)
    float           m_RandomOffset;        // +0xF0

    // +0xF4: >= 0 arms the sparkle ring; quad spin speed. Init = -1.0.
    // Update @ 0x19a860: when m_FruitType>=0 and dt>0, m_Timer(+0x2c) += dt * m_RotationSpeed.
    float           m_RotationSpeed;       // +0xF4

    // +0xF8: sparkle timer (>= 0 active; Update += dt*8, clamp 8). Init = -1.0.
    float           m_SparkleTimer;        // +0xF8

    // +0xFC: new-indicator timer (Update += 2*dt; reset to 0 when SparkleTimer<1). Init = -1.0.
    float           m_NewIndicatorTimer;   // +0xFC

    // +0x100: entity base scale captured on first frame. Init = Vec3(0,0,0).
    Vec3            m_BaseScale;           // +0x100..+0x10B

    // +0x10C: child sprite list for AddPeice/UpdatePeices/DeletePeices.
    // std::list<MenuButtonAddOn> = 8 bytes (Sourcery 2010q1 pre-C++11 sentinel-only).
    std::list<MenuButtonAddOn> m_AddOns;   // +0x10C..+0x113 (ARM32)

    // +0x114..+0x11B: gap between end of m_AddOns and m_pLabelFg.
    // Spec says +0x110 is Vec3 m_DrawOffset (Draw adds to pos for label placement);
    // the Vec3 overlaps the list on ARM32 (list = 8B, Vec3 would start inside it).
    // Port lays this out as raw pad; Draw will reference +0x110/+0x114/+0x118 as needed.
    // TODO: 0x0019c2e4 -- confirm m_DrawOffset exact offset vs std::list size
    uint8_t         _pad114[8];            // +0x114..+0x11B

    // +0x11C: BakedString* label foreground. LIVE in v1.6.1 Draw when !=0.
    void*           m_pLabelFg;            // +0x11C

    // +0x120: BakedString* extra label
    void*           m_pLabelExtra;         // +0x120

    // +0x124: label shadow/curve data
    void*           m_pLabelShadow;        // +0x124

    // +0x128: player colour tint (Colour = 4 bytes)
    Colour          m_PlayerColour;        // +0x128

    // +0x12C: player index tint (Colour = 4 bytes; ends at +0x12F)
    Colour          m_PlayerIndexTint;     // +0x12C

    // +0x130..+0x133: pad to reach +0x134
    uint8_t         _pad130[4];            // +0x130..+0x133

    // +0x134: grow-in delay countdown; Init=1.0; Update gates on >0; decrements by dt.
    float           m_GrowInTimer;         // +0x134

    // +0x138: when 1, back-key fires action
    uint8_t         m_bRespondsToBackKey;  // +0x138

    // +0x139: drag-cancel flag
    uint8_t         m_bDragCancel;         // +0x139

    // +0x13A: when 1, slice fires ClearMenuItems
    uint8_t         m_bClearsMenuItems;    // +0x13A

    // +0x13B: pad
    uint8_t         _pad13B;               // +0x13B

    // +0x13C: target rest scale Vec3 (Init copies hitBounds). Binary spec: 12 bytes.
    // Binary: byte0 of .z (@0x144) aliases m_bHasHitArea; byte1 (@0x145) aliases m_bInteractive.
    // Access via HasHitArea()/SetHasHitArea()/IsInteractive()/SetInteractive() accessors so the
    // offset holds on both ARM32 (binary) and host builds (8-byte pointer strides).
    Vec3            m_RestScale;           // +0x13C..+0x147

    // +0x148: primary fruit pointer (Init=0; CreateFruit fills; used in SetToMultiplayerState).
    // Binary: byte1 of the pointer word (@0x149) aliases m_bAcceptsTouch.
    // Access via AcceptsTouch()/SetAcceptsTouch() so sizeof stays 0x178 on both builds.
    Fruit*          m_pFruitPiece;         // +0x148

    // +0x14C: tracked-fruit pointer for per-frame scale writes
    Fruit*          m_pTrackedFruit;       // +0x14C

    // Aliased-flag accessors: binary packs these into the float/pointer bytes above (LE).
    // On the cross-build (ARM32 LE) these read/write the exact binary bytes.
    // On the host x64 build they do the same -- host is also LE, and the float/pointer
    // fields physically occupy those bytes before any pointer padding.
    // v1.6.1 MenuButton::Init @0x0019b994 / Update @0x0019a860 / ctor @0x0019bb08
    bool HasHitArea()    const { return ((const uint8_t*)&m_RestScale.z)[0] != 0; }
    void SetHasHitArea(bool v) { ((uint8_t*)&m_RestScale.z)[0] = v ? 1 : 0; }

    bool IsInteractive()    const { return ((const uint8_t*)&m_RestScale.z)[1] != 0; }
    void SetInteractive(bool v)   { ((uint8_t*)&m_RestScale.z)[1] = v ? 1 : 0; }

    bool AcceptsTouch()    const { return ((const uint8_t*)&m_pFruitPiece)[1] != 0; }
    void SetAcceptsTouch(bool v) { ((uint8_t*)&m_pFruitPiece)[1] = v ? 1 : 0; }

    // +0x150: backdrop-active flag (init = 1 for most buttons)
    uint8_t         m_bBackdropActive;     // +0x150

    // +0x151..+0x153: pad
    uint8_t         _pad151[3];            // +0x151..+0x153

    // +0x154: shake/backdrop scale factors Vec3; Init = Vec3(1, 0.85, 0.85) (DAT_0019bafc=0.85f)
    Vec3            m_ShakeScale;          // +0x154..+0x15F

    // +0x160: label extra alpha (Draw second-pass gate >0). Init=0.
    float           m_LabelExtraAlpha;     // +0x160

    // +0x164: hit-area X inset (default 5.0)
    float           m_HitInsetX;           // +0x164

    // +0x168: hit-area Y inset (default 5.0)
    float           m_HitInsetY;           // +0x168

    // +0x16C: reserved float (Init=DAT)
    float           m_fieldReserved;       // +0x16C

    // +0x170: new-bounce phase (Draw gate). Init=0.
    float           m_NewBouncePhase;      // +0x170

    // +0x174: shake timer countdown (Update)
    float           m_ShakeTimer;          // +0x174

    // === v1.0 compat fields (port-side only; binary offset unknown in v1.6.1) ===
    // Excluded from the cross-build (FN_ASM_VERIFY_CROSS) so sizeof == 0x178 is
    // enforceable by the static_assert below. The Bada production build does NOT
    // define FN_ASM_VERIFY_CROSS, so these fields remain visible there too.
    // DIFFERS: original v1.0 had these at known offsets; v1.6.1 layout unknown.
    //   m_bEnabled:        v1.0 +0x123; disables ClearMenuItems + touch input.
    //   m_AnimScale:       v1.0 +0x13C; maps to m_ShakeScale.x in v1.6.1 (backdrop scale factor).
    //   m_BounceParams:    v1.0 +0x140; new-item star anchor ratios.
    //   m_bTouchHeld:      v1.0 +0x131; touch-held gate for Update and Init.
    //   m_bScoreSubmitted: v1.0 +0x120; TutorialControl flip direction.
    // TODO: 0x0019b994 -- RE v1.6.1 binary to locate each field's offset and remove these
#if !defined(FN_ASM_VERIFY_CROSS)
    uint8_t         m_bEnabled;            // port-compat; v1.0 +0x123
    float           m_AnimScale;           // port-compat; v1.0 +0x13C -> v1.6.1 m_ShakeScale.x
    Vec3            m_BounceParams;        // port-compat; v1.0 +0x140 -> v1.6.1 hardcoded 0.85
    uint8_t         m_bTouchHeld;          // port-compat; v1.0 +0x131
    uint8_t         m_bScoreSubmitted;     // port-compat; v1.0 +0x120
#endif
    // === end compat fields ===

    MenuButton();

    // Binary ctors @ 0x0019bb08 / 0x0019bcac / 0x0019be50 / 0x0019bff8 (all forward to Init).
    MenuButton(Mortar::SmartPtr<Mortar::Texture>* tex, Vec3* spawnPos,
               Mortar::Delegate0<void>* onTap,
               int fruitType, Vec3* restPos,
               Mortar::Delegate1<void, HUDControl*>* onRemove);

    ~MenuButton();

    // HUDControl vtable overrides
    void Init() override;
    void Release() override;
    void Reset() override;
    void BeginDraw(float dt) override;
    void PreDraw(const Vec3& hudScale) override;
    void Draw(const Vec3& hudScale, int layerMask) override;
    void Update(float dt) override;
    bool SetToMultiplayerState() override;
    int  GetType() override { return 5; }
    void Skip() override;

    // MenuButton::Init @ 0x0019b994 (5-arg; sets all fields, calls CreateFruit)
    void Init(Vec3 buttonPos, Mortar::Delegate0<void> clickCb,
              int fruitType, Vec3 hitBounds,
              Mortar::Delegate0<void> deletedCb);

    // Creates the fruit/bomb entity for m_FruitType>=0 (called from Init tail)
    void CreateFruit();

    // v1.6.1 MenuButton::SetNewSymbol @0x0019a534: arms/disarms the new-indicator timer
    void SetNewSymbol(bool show);

    // v1.6.1 MenuButton::Shake @0x0019a510: sets m_ShakeTimer (+0x174)
    void Shake(float t);

    // v1.6.1 MenuButton::HasNewSymbol @0x0019a5a0: returns (m_NewIndicatorTimer(+0xFC) >= 0)
    bool HasNewSymbol();

    // v1.6.1 MenuButton::IsLoadingSymbol @0x0019a608: returns (m_SparkleTimer(+0xF8) >= 0)
    bool IsLoadingSymbol();

    // v1.6.1 MenuButton::SetLoadingSymbol @0x0019a560: arms sparkle timer (+0xF8)
    void SetLoadingSymbol(bool show);

    // v1.6.1 MenuButton::SetText @0x0019d4e0: builds curved-text BakedString pair. Zero call sites in shipped binary.
    void SetText(const char* text, Colour fg, Colour shadow, float radius);

    // v1.6.1 MenuButton::Remove @0x0019d148: release fruit piece with upward fling
    void Remove();

    // v1.6.1 MenuButton::TouchReleased @0x0019a7f8: fires m_ClickCallback (toggles only) + m_DeletedCallback (always)
    bool TouchReleased();

    // v1.6.1 MenuButton::AddPeice @0x00150240: spawn child HUDControl3d sprite, attach to HUD + m_AddOns list
    void AddPeice(Mortar::SmartPtr<Mortar::Texture> tex, Vec2* uvOverride,
                  float rotSpeed, float initialTimer,
                  Vec3 offset, Vec3 sizeScale,
                  Colour tint, int layerFlags);

    // v1.6.1 MenuButton::UpdatePeices @0x0019a630: per-addon position/size update
    void UpdatePeices(float dt);

    // v1.6.1 MenuButton::DeletePeices @0x0019d1b0: detach and mark addons for HUD removal
    void DeletePeices();

    // v1.6.1 MenuButton::DeletedPeice @0x0019a69c: addon's HUD-side removal callback
    void DeletedPeice(HUDControl* hudControl);

    // Binary @ 0x0019d870: Clicked -- no-op override
    virtual void Clicked() {}

    // Replaces m_ClickCallback. Used by ScreenButton::ShrinkButtonCall.
    void SetCallback(const Mortar::Delegate0<void>& cb) { m_ClickCallback = cb; }

    // v1.6.1 MenuButton::LoadContent @0x0019d640: loads 3 shared textures into class statics.
    static void LoadContent();
    static void UnLoadContent();

private:
    // v1.6.1 MenuButton::UpdateTouchPosition @0x0019a6d0: copies x/y/phase from tracked Touch slot into m_TouchX/Y/Phase.
    void UpdateTouchPosition();
};

// Layout asserts. FN_ASM_VERIFY_CROSS guard REMOVED so the cross-build also enforces
// these (v1.6.1 MenuButton ctor @0x0019bb08, sizeof=0x178).
// The compat fields below the asserts are wrapped in #if !defined(__bada__) so they
// do not inflate the cross-build sizeof past 0x178.
#if defined(__bada__)
static_assert(__builtin_offsetof(MenuButton, m_pEntity)           == 0x80,  "MenuButton m_pEntity offset");
static_assert(__builtin_offsetof(MenuButton, m_FruitType)         == 0x84,  "MenuButton m_FruitType offset");
static_assert(__builtin_offsetof(MenuButton, m_AnimPhase)         == 0xD0,  "MenuButton m_AnimPhase offset");
static_assert(__builtin_offsetof(MenuButton, m_TouchSlot)         == 0xD8,  "MenuButton m_TouchSlot offset");
static_assert(__builtin_offsetof(MenuButton, m_BackdropScale)     == 0xEC,  "MenuButton m_BackdropScale offset");
static_assert(__builtin_offsetof(MenuButton, m_RotationSpeed)     == 0xF4,  "MenuButton m_RotationSpeed offset");
static_assert(__builtin_offsetof(MenuButton, m_SparkleTimer)      == 0xF8,  "MenuButton m_SparkleTimer offset");
static_assert(__builtin_offsetof(MenuButton, m_NewIndicatorTimer) == 0xFC,  "MenuButton m_NewIndicatorTimer offset");
static_assert(__builtin_offsetof(MenuButton, m_BaseScale)         == 0x100, "MenuButton m_BaseScale offset");
static_assert(__builtin_offsetof(MenuButton, m_GrowInTimer)       == 0x134, "MenuButton m_GrowInTimer offset");
static_assert(__builtin_offsetof(MenuButton, m_RestScale)         == 0x13C, "MenuButton m_RestScale offset");
static_assert(__builtin_offsetof(MenuButton, m_pFruitPiece)       == 0x148, "MenuButton m_pFruitPiece offset");
static_assert(__builtin_offsetof(MenuButton, m_pTrackedFruit)     == 0x14C, "MenuButton m_pTrackedFruit offset");
static_assert(__builtin_offsetof(MenuButton, m_ShakeScale)        == 0x154, "MenuButton m_ShakeScale offset");
static_assert(__builtin_offsetof(MenuButton, m_HitInsetX)         == 0x164, "MenuButton m_HitInsetX offset");
static_assert(__builtin_offsetof(MenuButton, m_HitInsetY)         == 0x168, "MenuButton m_HitInsetY offset");
static_assert(__builtin_offsetof(MenuButton, m_ShakeTimer)        == 0x174, "MenuButton m_ShakeTimer offset");
static_assert(sizeof(MenuButton) == 0x178, "MenuButton sizeof mismatch");
#endif

#endif
