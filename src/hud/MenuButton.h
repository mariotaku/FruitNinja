#ifndef FN_MENU_BUTTON_H
#define FN_MENU_BUTTON_H

//
// MenuButton : HUDControl3d (size = 0x178 / 376 bytes)
// v1.6.1 layout. operator_new(0x178) confirmed @ LeaderboardScreen::CreateQuitButton 0x00193198.
//
// ASM-spec v1.6.1 MenuButton @ ctor 0x0019bb08 / Init 0x0019b994, sizeof 0x178:
//   Delegate0 is 0x24 bytes; m_DeletedCallback@0xAC fills to 0xCF (no field @0xCC --
//   m_FadeAlphaIdx was a v1.5.x phantom); m_SparkleTimer@0xF8, m_NewIndicatorTimer@0xFC;
//   m_FlipDirection@0x130 (byte, Init writes 0; TutorialControl reads for flip direction);
//   m_RestScale@0x13C is a plain Vec3 (all 3 floats); m_bHasHitArea@0x148, m_bAcceptsTouch@0x149,
//   2 pad bytes @0x14A; m_pTrackedFruit@0x14C; m_ShakeScale@0x154 (Init=(1,0.85,0.85));
//   m_ShakeTimer@0x174.
//   Init @0x0019b994 sets m_bAcceptsTouch(+0x149)=1 unconditionally.
//   Update @0x0019a860 gates the entire touch block on byte @+0x149 != 0.
//   CreateButtons (DojoScreen @0x0016ad9c, MainScreen): sets m_ShakeScale.x=0.5 for shop/play btns;
//   bounce writes m_ShakeScale.y*=0.575; m_ShakeScale.z*=0.575; m_ShakeScale.y=-m_ShakeScale.y.
// ASM-spec v1.6.1 MenuButton::HasNewSymbol @0x0019a5a0: m_NewIndicatorTimer(+0xFC) >= 0
// ASM-spec v1.6.1 MenuButton::IsLoadingSymbol @0x0019a608: m_SparkleTimer(+0xF8) >= 0
// ASM-spec v1.6.1 MenuButton::Shake @0x0019a510: m_ShakeTimer(+0x174) = arg
//
// 3-layer rendering + 1 entity:
//   Layer 0 (3D): Spinning fruit entity (NOT drawn by MenuButton -- Mortar::ActorManager::Draw)
//   Layer 1 (2D): Button texture quad (+0x70)
//   Layer 2 (2D): "New item" star indicator (+0xfc)
//   Layer 3 (2D): Sparkle ring (armed when m_SparkleTimer (+0xf8) >= 0)
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
#include "engine/math/_Vector2.h"
#include "engine/util/Delegate.h"
#include "engine/util/SmartPtr.h"
#include <cstdint>
#include <list>

namespace Mortar { class Entity; class BakedStringTTF; }
class Fruit;

// MenuButtonAddOn -- child sprite metadata for AddPiece/UpdatePieces.
// Stored in std::list<MenuButtonAddOn> m_AddOns at MenuButton +0x10C.
// v1.6.1 MenuButton::AddPiece @0x0019cd34 (thunk 0x00105524)
struct MenuButtonAddOn {
    HUDControl3d*   m_pControl;      // +0x00
    float           m_RotationSpeed; // +0x04
    _Vector3<float> m_Scale;         // +0x08 local size multiplier
    _Vector3<float> m_Offset;        // +0x14 local position relative to parent
};

#if defined(__bada__)
static_assert(__builtin_offsetof(MenuButtonAddOn, m_RotationSpeed) == 0x04, "MenuButtonAddOn m_RotationSpeed offset");
static_assert(__builtin_offsetof(MenuButtonAddOn, m_Scale)         == 0x08, "MenuButtonAddOn m_Scale offset");
static_assert(__builtin_offsetof(MenuButtonAddOn, m_Offset)        == 0x14, "MenuButtonAddOn m_Offset offset");
static_assert(sizeof(MenuButtonAddOn) == 0x20, "MenuButtonAddOn sizeof mismatch");
#endif

// Matches ClearMenuItems @ 0x0016ac7c. Cascades release on every active
// menu fruit/bomb: sets m_bSliced + outward random velocity on fruits,
// disables+flings bombs, sets m_bDrawWhole/m_bMovement. MenuButtons
// whose entity is released this way detect velSq > 0.001 and start the
// FadeCounter shrink. Called from MenuButton::Update on user slice of a
// fruit button, and from GameModeScreen::QuitCallback so the back-out
// animation mirrors the main -> mode-select cascade.
void ClearMenuItems();

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

    // +0xD4: purpose unknown -- write-only, no reader found.
    // Init @0x0019b994 zeroes all 4 bytes; BaseScreen::UpdateButtons @0x0016060c shrink-out
    // path blind-copies Vec3::One into +0xD4..+0xDF (the contiguous str of Vec3::One.x lands
    // here, .y/.z spill into m_TouchSlot/m_TouchX). No Draw/Update read site.
    int             m_reservedD4;          // +0xD4

    // +0xD8: touch slot currently tracked (-1 = none)
    int             m_TouchSlot;           // +0xD8

    // +0xDC..+0xE4: last-known touch position/phase from UpdateTouchPosition
    float           m_TouchX;             // +0xDC
    float           m_TouchY;             // +0xE0
    float           m_TouchPhase;         // +0xE4

    // +0xE8: write-only in v1.6.1 -- no reader anywhere (Update and Draw fully
    // decompiled, program-wide xref search found none). Rolled in CreateFruit
    // purely to keep the g_Random draw sequence byte-faithful.
    // ASM-spec v1.6.1 MenuButton::CreateFruit @0x0019b634: = (float)(uint32_t)(Rand32(0x28) - 0x14).
    float           m_BackdropOffsetX;     // +0xE8

    // +0xEC: per-frame backdrop scale: curScale.x * 1.125 * m_ShakeScale.x
    // Written every Update @ 0x19af70; read by Draw phase-A layer0 to scale scratchs.tex quad.
    float           m_BackdropScale;       // +0xEC

    // +0xF0: ONE byte (strb/ldrb), not a float. Random backdrop-mirror flag.
    // ASM-spec v1.6.1 MenuButton::CreateFruit @0x0019b634: = (Rand32(2) != 0) ? 1 : 0.
    // ASM-spec v1.6.1 MenuButton::Draw @0x0019c39c: ldrb; sx = byte ? -1.0f : 1.0f
    // (8-bit integer compare, never a float sign test). Init zeroes it.
    // +0xF1..+0xF3 padding.
    uint8_t         m_RandomOffset;        // +0xF0

    // +0xF4: quad spin speed (NOT the sparkle-ring gate; Draw gates the ring on
    // m_SparkleTimer +0xF8). Init = -1.0.
    // Update @ 0x19a860: when m_FruitType>=0 and dt>0, m_Timer(+0x2c) += dt * m_RotationSpeed.
    float           m_RotationSpeed;       // +0xF4

    // +0xF8: sparkle timer (>= 0 active; Update += dt*8, clamp 8). Init = -1.0.
    float           m_SparkleTimer;        // +0xF8

    // +0xFC: new-indicator timer (Update += 2*dt; reset to 0 while SparkleTimer >= 1,
    // i.e. only while the loading sparkle is active). Init = -1.0.
    float           m_NewIndicatorTimer;   // +0xFC

    // +0x100: entity base scale captured on first frame. Init = Vec3(0,0,0).
    _Vector3<float> m_BaseScale;           // +0x100..+0x10B

    // +0x10C: child sprite list for AddPiece/UpdatePieces/DeletePieces.
    // std::list<MenuButtonAddOn> = 8 bytes (Sourcery 2010q1 pre-C++11 sentinel-only).
    std::list<MenuButtonAddOn> m_AddOns;   // +0x10C..+0x113 (ARM32)

    // +0x114: label anchor offset added to GetAdjustedPos() in Draw.
    // v1.6.1 MenuButton::Draw @0x0019c764: anchor = GetWorldPos() + m_DrawOffset.
    _Vector3<float> m_DrawOffset;          // +0x114..+0x11F

    // +0x120: white FG label (gradient-tinted). BakedStringTTF alignSigned=-1, effectSize=0.
    // v1.6.1 MenuButton::SetText @0x0019b0ac.
    Mortar::BakedStringTTF* m_pLabelFg;   // +0x120

    // +0x124: inner glow/shadow label. BakedStringTTF alignSigned=-1, effectSize=2 (INNER_GLOW).
    // v1.6.1 MenuButton::SetInnerGlow @0x0019afbc.
    Mortar::BakedStringTTF* m_pLabelShadow; // +0x124

    // +0x128: outer glow label. BakedStringTTF alignSigned=-1, effectSize=5 (BLUR), black.
    // v1.6.1 MenuButton::SetText @0x0019b0ac (wantGlow path).
    Mortar::BakedStringTTF* m_pLabelGlow;  // +0x128

    // +0x12C: per-player tint colour (P2P multiplayer). ctor @0x0019bb08
    // default-constructs it (Colour::Colour) then copies it into a scratch; Draw
    // @0x0019c2e4 gates a tint branch on (m_PlayerColour != Colour::White).
    // Defunct: P2P multiplayer tint -- effectively always White in single-player;
    //          v1.6.1 MenuButton @0x0019bb08 / Draw @0x0019c2e4.
    Colour          m_PlayerColour;        // +0x12C

    // +0x130: flip-direction flag. Init @0x0019b994 writes 0; TutorialControl
    // (ResetTutePos / ButtonPressedAtPos) XORs it with (pos.x > 0) to pick the
    // tute-arrow flip side.
    uint8_t         m_FlipDirection;       // +0x130
    uint8_t         _pad131[3];            // +0x131..+0x133

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

    // +0x13C: target rest scale Vec3 (all 3 floats; Init writes all 3 via stmia).
    // v1.6.1 MenuButton::Init @0x0019b994 / CreateFruit @0x0019b8c8 both write .z.
    _Vector3<float> m_RestScale;           // +0x13C..+0x147

    // +0x148: hit-area presence flag (Init sets from |hitBounds.x|+|hitBounds.y|>0)
    uint8_t         m_bHasHitArea;        // +0x148

    // +0x149: accepts-touch flag (Init sets =1 unconditionally; Update gates touch block here)
    // v1.6.1 MenuButton::Update @0x0019a860: gate at +0x149 != 0
    uint8_t         m_bAcceptsTouch;      // +0x149

    // +0x14A: padding
    uint8_t         _pad14A[2];           // +0x14A..+0x14B

    // +0x14C: tracked-fruit pointer for per-frame scale writes and KillFruit back-ref
    // (Fruit::KillFruit nulls this via raw offset +0x14C on m_pOwner)
    Fruit*          m_pTrackedFruit;       // +0x14C

    // Thin accessors backed by real flag members.
    // v1.6.1 MenuButton::Init @0x0019b994 / Update @0x0019a860
    bool HasHitArea()    const { return m_bHasHitArea != 0; }
    void SetHasHitArea(bool v) { m_bHasHitArea = v ? 1 : 0; }

    bool AcceptsTouch()    const { return m_bAcceptsTouch != 0; }
    void SetAcceptsTouch(bool v) { m_bAcceptsTouch = v ? 1 : 0; }

    // +0x150: backdrop-active flag (init = 1 for most buttons)
    uint8_t         m_bBackdropActive;     // +0x150

    // +0x151..+0x153: pad
    uint8_t         _pad151[3];            // +0x151..+0x153

    // +0x154: shake/backdrop scale factors Vec3; Init = Vec3(1, 0.85, 0.85) (DAT_0019bafc=0.85f)
    _Vector3<float> m_ShakeScale;          // +0x154..+0x15F

    // +0x160: label extra alpha (Draw second-pass gate >0). Init=0.
    float           m_LabelExtraAlpha;     // +0x160

    // +0x164: arc radius for label layout (SetText stores here; TutorialControl::ResetTutePos
    // and ButtonPressedAtPos read it as the arc radius for tute-arrow half-width).
    // v1.6.1 MenuButton::SetText @0x0019b0ac stores radius here.
    float           m_LabelRadius;         // +0x164

    // +0x168: hit-area X inset (default 5.0; Init @0x0019b994).
    // hit-test: left  = cx - 0.5*restX - HitInsetX
    //           right = cx + 0.5*restX + HitInsetX
    // v1.6.1 MenuButton::Update hit-test block @0x0019ad94 reads +0x168.
    float           m_HitInsetX;           // +0x168

    // +0x16C: hit-area Y inset (default 5.0; Init @0x0019b994).
    // hit-test: bottom = cy - 0.5*restY - HitInsetY
    //           top    = cy + 0.5*restY + HitInsetY
    // v1.6.1 MenuButton::Update hit-test block @0x0019ad94 reads +0x16C.
    float           m_HitInsetY;           // +0x16C

    // +0x170: Init-write-only (100.0, DAT@0x19baf8=0x42c80000); NOT read by Draw --
    // the NEW-badge gate is m_NewIndicatorTimer (+0xFC). No reader found.
    float           m_NewBouncePhase;      // +0x170

    // +0x174: shake timer countdown (Update)
    float           m_ShakeTimer;          // +0x174


    // DIFFERS: the binary has no default ctor -- v1.6.1 MenuButton::MenuButton
    // only exists as the value-ctor below (@0x0019bb08 etc). The port keeps this
    // parameterless ctor for the ~22 call sites that build-then-Init() separately;
    // each replicates the value-ctor's field defaults via the member init-list below.
    MenuButton();

    // v1.6.1 MenuButton::MenuButton C1 @0x0019bb08 / C2 @0x0019bcac. ALL params
    // by value. param6 is Delegate0<void> -> forwarded to Init as deletedCb
    // (m_DeletedCallback +0xAC); the binary ctor never touches m_RemoveCallback (+0x38).
    MenuButton(Mortar::SmartPtr<Mortar::Texture> tex, _Vector3<float> spawnPos,
               Mortar::Delegate0<void> clickCb,
               int fruitType, _Vector3<float> hitBounds,
               Mortar::Delegate0<void> deletedCb);

    // v1.6.1 MenuButton::MenuButton C1 @0x0019be50 / C2 @0x0019bff8. `textureName`
    // is a texture FILENAME (e.g. "openfeint_gamecenter.tex", "upsell_continue.tex"),
    // NOT label text -- both binary call sites (KeyboardControl::Update,
    // UpsellScreen::CreateBuyNowRing/TurnIntoBuyNowRing) pass literal .tex names.
    // Body is LoadLocalisedTexture(textureName) into m_Texture, then the same
    // Init() tail as the SmartPtr<Texture> overload above. Callers are stubbed in
    // the port (KeyboardControl bypassed for SDL text input; UpsellScreen defunct)
    // so this overload has no live call site -- kept for public-API shape parity.
    MenuButton(const char* textureName, _Vector3<float> spawnPos,
               Mortar::Delegate0<void> clickCb,
               int fruitType, _Vector3<float> hitBounds,
               Mortar::Delegate0<void> deletedCb);

    ~MenuButton();

    // HUDControl vtable overrides
    void Init() override;
    void Release() override;
    void Reset() override;
    void BeginDraw(float dt) override;
    void PreDraw(float* hudScale) override;
    // v1.6.1 MenuButton::Draw @0x0019c2e4. Draws backdrop pass (layer 0x40,
    // demotes itself to 0x80) OR quad+labels+NEW-badge+sparkle-ring. The quad is
    // inlined (NOT delegated to HUDControl3d::Draw): press-dim RGB*0.5, anim-alpha
    // override, shake jitter. No m_DrawColour.a==0 early-out -- the binary draws
    // (invisibly) at alpha 0 and always runs the layer demotion.
    void Draw(float* hudScaleRaw) override;
    void Update(float dt) override;
#ifndef __bada__
    // Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
    // Advances m_SparkleTimer / m_NewIndicatorTimer (sparkle-ring + NEW-badge
    // bounce) dt-scaled, once per PRESENTED frame (Game::tickRealtimeUi via
    // HUD::UpdateRealtime -- MenuButtons are AddControl'd to game_work.mHud
    // by their owning screens, so no extra dispatch wiring is needed), so
    // both animations track the display's actual present rate instead of the
    // fixed 60Hz sim tick. See MenuButton.cpp for the MB_ADVANCE_F macro and
    // AdvanceSparkleAndBadge (shared with the __bada__ path via Update()).
    void UpdateRealtime(float dtSeconds) override;
#endif
    bool SetToMultiplayerState() override;
    int  GetType() override { return 5; }
    void Skip() override;
#ifndef __bada__
    // Port specific: desktop ESC-as-back discriminator -- see HUDControl::HasActiveBackBomb.
    bool HasActiveBackBomb() const override { return m_bBackdropActive != 0; }
#endif

    // MenuButton::Init @ 0x0019b994 (5-arg; sets all fields, calls CreateFruit)
    void Init(_Vector3<float> buttonPos, Mortar::Delegate0<void> clickCb,
              int fruitType, _Vector3<float> hitBounds,
              Mortar::Delegate0<void> deletedCb);

    // Creates the fruit/bomb entity for m_FruitType>=0 (called from Init tail)
    void CreateFruit();

    // v1.6.1 MenuButton::SetNewSymbol @0x0019a564: arms/disarms the new-indicator timer
    void SetNewSymbol(bool show);

    // v1.6.1 MenuButton::Shake @0x0019a510: sets m_ShakeTimer (+0x174)
    void Shake(float t);

    // v1.6.1 MenuButton::HasNewSymbol @0x0019a5a0: returns (m_NewIndicatorTimer(+0xFC) >= 0)
    bool HasNewSymbol();

    // v1.6.1 MenuButton::IsLoadingSymbol @0x0019a608: returns (m_SparkleTimer(+0xF8) >= 0)
    bool IsLoadingSymbol();

    // v1.6.1 MenuButton::SetLoadingSymbol @0x0019a5d0: arms sparkle timer (+0xF8)
    void SetLoadingSymbol(bool show);

    // v1.6.1 MenuButton::SetText @0x0019b0ac: builds curved BakedStringTTF label triple.
    // text=string, gradTop/gradBottom=FG gradient, radius=arc (0=flat), fontScale=size,
    // wantGlow=outer glow, wantInnerGlow=inner glow/shadow.
    void SetText(const char* text, Colour gradTop, Colour gradBottom,
                 float radius = 42.0f, float fontScale = 12.0f,
                 bool wantGlow = true, bool wantInnerGlow = true);

    // v1.6.1 MenuButton::SetInnerGlow @0x0019afbc: (re)builds m_pLabelShadow.
    // effectSize is the BakedStringTTF effectSize (stroke/glow expansion), not alignSigned --
    // alignSigned is fixed at -1 inside so all 3 arc layers share the same m_Weight.
    void SetInnerGlow(const char* text, Colour colour, float radius, float fontScale, float effectSize);

    // v1.6.1 MenuButton::Remove @0x0019d148: release fruit piece with upward fling
    void Remove();

    // v1.6.1 MenuButton::TouchReleased @0x0019a7f8: fires m_ClickCallback (toggles only) + m_DeletedCallback (always)
    bool TouchReleased();

    // v1.6.1 MenuButton::AddPiece @0x0019cd34 (thunk 0x00105524): spawn child
    // HUDControl3d sprite, attach to HUD + m_AddOns list.
    // Real callers are KeyboardControl::Update and UpsellScreen::CreateBuyNowRing/
    // TurnIntoBuyNowRing -- both intentionally bypassed/stubbed in this port
    // (KeyboardControl uses native SDL text input; UpsellScreen is defunct), so
    // this method has no live call site. That is policy-correct, not a gap --
    // do NOT wire callers in to force a call site.
    void AddPiece(Mortar::SmartPtr<Mortar::Texture> tex, _Vector2<float>* uvOverride,
                  float rotSpeed, float initialTimer,
                  _Vector3<float> offset, _Vector3<float> sizeScale,
                  Colour tint, int layerFlags);

    // v1.6.1 MenuButton::UpdatePieces @0x0019a630: per-addon position/size update
    void UpdatePieces(float dt);

    // v1.6.1 MenuButton::DeletePieces @0x0019cf84: detach and mark addons for HUD removal
    void DeletePieces();

    // v1.6.1 MenuButton::DeletedPiece @0x0019a728: addon's HUD-side removal callback
    void DeletedPiece(HUDControl* hudControl);

    // Binary @ 0x0019d870: Clicked -- no-op override
    virtual void Clicked() {}

    // Replaces m_ClickCallback. Used by ScreenButton::ShrinkButtonCall.
    void SetCallback(const Mortar::Delegate0<void>& cb) { m_ClickCallback = cb; }

    // v1.6.1 MenuButton::LoadContent @0x0019d640: loads 3 shared textures into class statics.
    static void LoadContent();
    static void UnLoadContent();

    // Accessor for the blurry_backing.tex static (slot 2 in LoadContent).
    // Used by GameOverScreen::DrawOrder for the state-0xe loading-spinner halo.
    // The static lives in MenuButton.cpp (TU-private); this accessor exposes it
    // without making the raw static header-visible.
    // v1.6.1 GameOverScreen::DrawOrder @0x00186484 reads this static.
    static Mortar::SmartPtr<Mortar::Texture>& GetSparkleRingTex();

private:
    // v1.6.1 MenuButton::UpdateTouchPosition @0x0019a6d0: copies x/y/phase from tracked Touch slot into m_TouchX/Y/Phase.
    void UpdateTouchPosition();
};

// Layout asserts under __bada__ (v1.6.1 MenuButton ctor @0x0019bb08, sizeof=0x178).
#if defined(__bada__)
static_assert(__builtin_offsetof(MenuButton, m_pEntity)           == 0x80,  "MenuButton m_pEntity offset");
static_assert(__builtin_offsetof(MenuButton, m_FruitType)         == 0x84,  "MenuButton m_FruitType offset");
static_assert(__builtin_offsetof(MenuButton, m_AnimPhase)         == 0xD0,  "MenuButton m_AnimPhase offset");
static_assert(__builtin_offsetof(MenuButton, m_TouchSlot)         == 0xD8,  "MenuButton m_TouchSlot offset");
static_assert(__builtin_offsetof(MenuButton, m_BackdropOffsetX)   == 0xE8,  "MenuButton m_BackdropOffsetX offset");
static_assert(__builtin_offsetof(MenuButton, m_BackdropScale)     == 0xEC,  "MenuButton m_BackdropScale offset");
static_assert(__builtin_offsetof(MenuButton, m_RandomOffset)      == 0xF0,  "MenuButton m_RandomOffset offset");
static_assert(__builtin_offsetof(MenuButton, m_RotationSpeed)     == 0xF4,  "MenuButton m_RotationSpeed offset");
static_assert(__builtin_offsetof(MenuButton, m_SparkleTimer)      == 0xF8,  "MenuButton m_SparkleTimer offset");
static_assert(__builtin_offsetof(MenuButton, m_NewIndicatorTimer) == 0xFC,  "MenuButton m_NewIndicatorTimer offset");
static_assert(__builtin_offsetof(MenuButton, m_BaseScale)         == 0x100, "MenuButton m_BaseScale offset");
static_assert(__builtin_offsetof(MenuButton, m_DrawOffset)         == 0x114, "MenuButton m_DrawOffset offset");
static_assert(__builtin_offsetof(MenuButton, m_pLabelFg)           == 0x120, "MenuButton m_pLabelFg offset");
static_assert(__builtin_offsetof(MenuButton, m_pLabelShadow)       == 0x124, "MenuButton m_pLabelShadow offset");
static_assert(__builtin_offsetof(MenuButton, m_pLabelGlow)         == 0x128, "MenuButton m_pLabelGlow offset");
static_assert(__builtin_offsetof(MenuButton, m_FlipDirection)     == 0x130, "MenuButton m_FlipDirection offset");
static_assert(__builtin_offsetof(MenuButton, m_GrowInTimer)       == 0x134, "MenuButton m_GrowInTimer offset");
static_assert(__builtin_offsetof(MenuButton, m_RestScale)         == 0x13C, "MenuButton m_RestScale offset");
static_assert(__builtin_offsetof(MenuButton, m_bHasHitArea)       == 0x148, "MenuButton m_bHasHitArea offset");
static_assert(__builtin_offsetof(MenuButton, m_bAcceptsTouch)     == 0x149, "MenuButton m_bAcceptsTouch offset");
static_assert(__builtin_offsetof(MenuButton, m_pTrackedFruit)     == 0x14C, "MenuButton m_pTrackedFruit offset");
static_assert(__builtin_offsetof(MenuButton, m_ShakeScale)        == 0x154, "MenuButton m_ShakeScale offset");
static_assert(__builtin_offsetof(MenuButton, m_LabelRadius)        == 0x164, "MenuButton m_LabelRadius offset");
static_assert(__builtin_offsetof(MenuButton, m_HitInsetX)         == 0x168, "MenuButton m_HitInsetX offset");
static_assert(__builtin_offsetof(MenuButton, m_HitInsetY)         == 0x16C, "MenuButton m_HitInsetY offset");
static_assert(__builtin_offsetof(MenuButton, m_NewBouncePhase)    == 0x170, "MenuButton m_NewBouncePhase offset");
static_assert(__builtin_offsetof(MenuButton, m_ShakeTimer)        == 0x174, "MenuButton m_ShakeTimer offset");
static_assert(sizeof(MenuButton) == 0x178, "MenuButton sizeof mismatch");
#endif

// v1.6.1 MenuCallbackClicked @0x19a620: empty no-op default menu-click callback.
void MenuCallbackClicked();

#endif
