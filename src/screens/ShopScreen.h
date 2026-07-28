#ifndef FN_SHOP_SCREEN_H
#define FN_SHOP_SCREEN_H

//
// ShopScreen : HUDControl3d  (NOT BaseScreen)
// Sensei's Swag — the in-game blade/background shop.
//
// Binary refs:
//   ctor              0x001b3f94  ShopScreen(DojoScreen*)
//   ~ShopScreen       0x001b49f8  (_deleting 0x001b4a54, base 0x001b4aa8)
//   Init              0x001b42ac
//   LoadContent       0x001b2a20  (static, loads 10 textures; PLT thunk @0x001047b8)
//   UnLoadContent     0x001b4afc  (static, clears all textures)
//   Update            0x001b321c  (387 lines; body spans 0x001b321c..0x001b3f8b)
//   DrawOrder         0x001b4e48  (there is NO ShopScreen::Draw symbol in v1.6.1)
//   SetSelected       0x001b24f0
//   ShrinkBuyButton   0x001b17b4
//   ClickedOnShopItem 0x001b2df4
//   QuitShopCallback  0x001b2ef0
//   EquipCallback     0x001b3008
//   NewItem           0x001b1774
//   Reset             0x001b179c  (vtable slot +0x10)
//   BuyButtonCallback 0x001b1874
//   GetDescriptionTextXPos 0x001b1830
//   DeletedMenuItem   0x001b53d4
//
// Struct size: 0xBC bytes
//
// Base:   HUDControl3d (0x00..0x7b)
//   +0x74  Mortar::SmartPtr<Texture>  m_Texture  (field_0x74)
//   +0x7C  float  m_TransitionAlpha      (lerps 0->1 on entry)
//   +0x80  int    m_LayerFlagsAlt        (0x40 or 0x80 depending on splats)
//   +0x84  MenuButton*   m_pBuyButton    (lazily created in state 0)
//   +0x88  float  m_BuyDelay             (timer decremented by dt)
//   +0x8C  MenuButton*   m_pEquipButton  (lazily created in state 1)
//   +0x90  DojoScreen*   m_pParent
//   +0x94  ScrollingMenu* m_pShopList    (the scrollable item list; null in ctor)
//   +0x98  ShopListItem*  m_pSelectedItem (ptr to currently selected list item)
//   +0x9C..+0xAB  ShopListItem* m_pSlotItems[4]  (cached selection per slot type; [3]=REMOVEADS defunct)
//   +0xAC  float  m_ScrollOffset         (computed from item count + 0.5)
//   +0xB4  int    m_AnimFrame            (sin-based animation counter)
//   +0xB8  int    m_State                (state machine index)
//
// State machine (Update):
//   0: Transition in. alpha += (1-alpha)*0.125. On alpha > 0.999 (@0x001b3698):
//        RemoveAllSplats, set m_BuyDelay, state=1.
//        Lazily creates m_pBuyButton (QCallee<ShopScreen> for buy action).
//        Registers buy button with TutorialControl.
//   1: Active. Decrements m_BuyDelay by dt.
//        If list changed selection: calls SetSelected().
//        If buy delay <= 0 and item is scrollable: ShrinkBuyButton().
//        If m_pEquipButton null and item not equipped: creates equip button.
//        Fires IsEquipped/IsLocked checks from ItemManager.
//   2: Transition out to dojo (alpha *= 0.75). On alpha < threshold:
//        mark parent m_bNoDestructor=1, self pending removal, state=8.
//   3: Buy animation (alpha *= 0.75). On completion: fling old buy-button
//        fruit piece, create new buy button with updated fruit type, state=4.
//   4: Resets m_LayerFlagsAlt to 0x80.
//   5/6: Wait for Mortar::ActorManager empty then equip item via ItemManager.
//   7: Same as 2 (alternate quit path).
//
// Textures (static, loaded by LoadContent @0x001b2a20):
//   BG_store.tex / BG_store_sml.tex  — background panel  (+0x48)
//   dialog_box_shop.tex              — info dialog box   (+0x2c)
//   locked.tex                       — lock icon         (+0x30)
//   locked_stroke.tex                — lock border       (+0x34)
//   scratch_deviders.tex             — list divider      (+0x38)
//   selected.tex                     — selected marker   (+0x3c)
//   selected_sml.tex                 — small selected    (+0x40)
//   select_item.tex                  — highlight ring    (+0x14)
//   new_item_sml.tex                 — new-item badge    (+0x18)
//   (one more tex at +0x44)
//
// Port status:
//   - State machine ported with full logic.
//   - ScrollingMenu is a stub (items visible but not scrollable).
//   - ItemManager stubs always return "unlocked/not-equipped".
//   - FruitSaveData::CheckDatesHaveChanged called in DojoScreen before push.
//
// Analysed: 2026-04-25T16:30
//

#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include <cstdint>

class MenuButton;
class DojoScreen;
class ScrollingMenu;
class ScrollingMenuItem;
class ShopListItem;
class HUD;

class ShopScreen : public HUDControl3d {
public:
    // Matches ShopScreen::ShopScreen(DojoScreen*) @ 0x001b3f94
    // Binary: calls HUDControl3d ctor, calls LoadContent if not loaded,
    //         initialises all fields, computes m_ScrollOffset from item count.
    ShopScreen(DojoScreen* parent);

    // Matches ~ShopScreen @ 0x001b49f8
    ~ShopScreen() override;

    // Matches ShopScreen::Init @ 0x001b42ac (vtable slot 2) — called from DojoScreen
    // state 2 launch: (**(code**)(*(int*)shop + 8))(shop)  == shop->Init()
    // Body is the CreateShopList per-item population loop (see ShopScreen.cpp).
    void Init() override;

    // Matches vtable Release slot
    void Release() override;

    // Matches ShopScreen::Reset @ 0x001b179c (HUDControl vtable slot +0x10).
    // Rewinds the state machine: m_State = 0, m_TransitionAlpha = 0.
    // Reached only via the vtable (HUD::ResetControls walks every registered
    // control); ShopScreen never calls it itself, matching the binary.
    void Reset() override;

    // Matches v1.6.1 ShopScreen::Update(float) @ 0x001b321c (387 lines)
    void Update(float dt) override;

#ifndef __bada__
    // Port specific: no binary counterpart -- see HUDControl::UpdateRealtime.
    // Eases m_TransitionAlpha (states 0/2/3/7) dt-scaled, once per PRESENTED
    // frame (Game::tickRealtimeUi via HUD::UpdateRealtime), so the shop
    // slide-in/out tracks the display's actual present rate (60/90/120fps)
    // instead of the fixed 60Hz sim tick. The STATE MACHINE itself (which
    // state, when to transition, one-shot side effects like button creation)
    // stays in Update() at 60Hz -- it reads the alpha this function advances
    // and fires threshold-crossing transitions there, exactly once per sim
    // tick. See ShopScreen.cpp for the SS_APPROACH/SS_DECAY macros shared
    // with the __bada__ path (mirrors ScrollingMenu's SM_DECAY_F/SM_SPRING_F).
    void UpdateRealtime(float dtSeconds) override;
#endif

    // ShopScreen::DrawOrder @ 0x001b4e48 (v1.6.1, vtable slot 9 +0x24)
    // Binary gates BG block on `layerMask == m_LayerFlagsAlt`; ring block on
    // `else if (m_AnimFrame > 0)`. Never writes m_LayerFlags.
    // ASM-spec v1.6.1 ShopScreen::DrawOrder @0x001b4e48
    void DrawOrder(float* hudScale, int layerMask) override;

    int GetType() override { return 1; }

#ifndef __bada__
    // Port specific: desktop mouse-wheel scroll hook -- see HUDControl::GetScrollList.
    ScrollingMenu* GetScrollList() override { return m_pShopList; }
    // Port specific: gates the wheel-scroll route while the shop is sliding in/out.
    float GetTransitionAlpha() const override { return m_TransitionAlpha; }
#endif

    // Matches ShopScreen::LoadContent @ 0x001b2a20 (PLT thunk @0x001047b8)
    // Loads all static textures. No internal guard -- the guard lives at the ctor
    // call site (`if (!s_bContentLoaded) LoadContent();`).
    static void LoadContent();

    // Matches ShopScreen::UnLoadContent @ 0x001b4afc
    // Releases all static textures.
    static void UnLoadContent();

public:
    // +0x7C: transition alpha (0->1 lerp-in, *= 0.75 fade-out)
    // field_0x74 (inherited HUDControl3d::m_Texture / Mortar::SmartPtr<Texture>) is zeroed by
    // HUDControl3d ctor and not used further in ShopScreen; not re-declared here.
    float m_TransitionAlpha;

    // +0x80: layer flags alt (toggled between 0x40 and 0x80 based on splats)
    int m_LayerFlagsAlt;

    // +0x84: buy/equip action button (lazily created)
    MenuButton* m_pBuyButton;

    // +0x88: delay timer before buy button appears (decremented by dt)
    float m_BuyDelay;

    // +0x8C: equip button (lazily created when item is not equipped)
    MenuButton* m_pEquipButton;

    // +0x90: parent dojo screen (used to trigger GameState=8 on quit)
    DojoScreen* m_pParent;

    // +0x94: the scrollable item list (ScrollingMenu*)
    ScrollingMenu* m_pShopList;

    // +0x98: currently selected list item
    ShopListItem* m_pSelectedItem;

    // Port-specific trailing field (not in the 0xBC-byte binary struct).
    // Excluded on the __bada__ production build so sizeof stays at 0xbc.
#if !defined(__bada__)
#if defined(FN_BLOCK_PRELOAD)
    // Task #66 Phase 2 -- true while BlockLoader::PreloadBlockStep() is still
    // draining the SHOP work-queue during state-0 transition-in. Holds the
    // screen in state 0 (alpha stays at its current lerp value, no completion
    // gate fires) and keeps HUD::SetInputModal(this) armed until the queue
    // drains. Mirrors GameModeScreen::m_bLoading (Phase 1).
    bool m_bLoading;
#endif
#endif // !defined(__bada__)

public:
    // Read-only accessor used by ShopListItem::Move to ramp m_CostAlpha
    // toward 1 only on the centered row (description-text fade-in).
    ShopListItem* GetSelectedItem() const { return m_pSelectedItem; }

    // +0x9C..+0xAB: per-slot cached selection (4 entries, ItemType 0-3)
    // Type 3 = REMOVEADS (defunct IAP). Binary: Init explicitly zeroes field_0xa8.
    ShopListItem* m_pSlotItems[4];

    // +0xAC: scroll position / animation offset (from item count + 0.5)
    float m_ScrollOffset;

    // +0xB0: 4-byte field, zero-init in ctor; no read site identified.
    // Sits as a 4-byte gap between m_ScrollOffset and m_AnimFrame. Reserved.
    uint32_t m_reservedB0;  // purpose unknown

    // +0xB4: sin-wave animation frame counter (0..0x3FFC range)
    int m_AnimFrame;

    // +0xB8: state machine index
    int m_State;

    // --- Static textures (GOT-relative globals in binary) ---
    // Corrected slot layout verified from LoadContent @ 0x001b2a20 disasm + string reads.
public:
    // ShopListItem::Draw accesses these via the same GOT static block.
    // Making them public so ShopListItem::Draw can reference them without a friend.
    static Mortar::SmartPtr<Mortar::Texture> s_TexLocked;          // +0x14: locked.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexSelectItem;      // +0x18: select_item.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexLoading;         // +0x2c: loading.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexScratch;         // +0x30: scratch_deviders.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexDialogBox;       // +0x34: dialog_box_shop.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexSelected;        // +0x38: selected.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexSelectedSml;     // +0x3c: selected_sml.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexLockedStroke;    // +0x40: locked_stroke.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexNewItemSmlBadge; // +0x44: new_item_sml.tex
    static Mortar::SmartPtr<Mortar::Texture> s_TexBGStore;         // +0x48: BG_store.tex / BG_store_sml.tex
    static bool s_bContentLoaded;                           // +0x4c: set =1 at end of LoadContent, =0 in UnLoadContent

    // ShopScreen::GetDescriptionTextXPos @ 0x001b1830
    // Returns the X anchor for description text, sliding with m_TransitionAlpha.
    // Binary: (alpha < 1 ? 145 + (1 - alpha) * 190 * 1.5 : 145) - 80
    //   (DAT_001b1870 = 145, DAT_001b186c = 190, DAT_001b1868 = 80).
    // At alpha=1: returns 145.0f - 80.0f = 65.0f. At alpha=0: returns 430.0f - 80.0f = 350.0f.
    float GetDescriptionTextXPos();

    // --- Callbacks ---

    // Matches ShopScreen::QuitShopCallback @ 0x001b2ef0
    // Plays SFX "menu-bomb", sets state=2 (or 7), flings buy-button fruit.
    void QuitShopCallback();

    // Matches ShopScreen::EquipCallback @ 0x001b3008
    // Calls ItemManager::SetEquippedItem with selected item, plays equip SFX.
    void EquipCallback();

    // Matches ShopScreen::ClickedOnShopItem(ScrollingMenuItem*) @ 0x001b2df4
    // Binary sig: Delegate1<void,ScrollingMenuItem*> confirmed via CopyConstruct
    //   @0x001b6ffc. If item locked: play SFX, set item alpha=0.25. If equip
    //   button exists: call TutorialControl::ButtonPressedAtPos.
    // Param is ScrollingMenuItem*; body casts to ShopListItem* (only ShopListItems
    // are ever placed in the list).
    void ClickedOnShopItem(ScrollingMenuItem* item);

    // Matches ShopScreen::SetSelected(ShopListItem*) @ 0x001b24f0
    // Updates m_pSelectedItem, resolves fruit types for buy/equip button display.
    void SetSelected(ShopListItem* item);

    // Matches ShopScreen::ShrinkBuyButton @ 0x001b17b4
    // Triggers the "slice" shrink animation on the buy button fruit if not sliced.
    void ShrinkBuyButton();

    // Matches ShopScreen::DeletedMenuItem(HUDControl*) @ 0x001b53d4
    // Registered as m_RemoveCallback on both m_pBuyButton and m_pEquipButton.
    // Fires when HUD removes a button from its control list (after m_bPendingRemoval).
    // Nulls the relevant button pointer and optionally kicks the fruit off-screen.
    void DeletedMenuItem(HUDControl* removed);

    // Matches ShopScreen::NewItem @ 0x001b1774
    // Sets s_ScrollOffset = 1.0f (binary: *(GOT + DAT_0015c4b4) = 0x3f800000).
    // Called when a new item is available in the shop.
    void NewItem();

    // Scroll-position sentinel/cache (NOT an alpha, despite the old name).
    //   1.0f  = sentinel: "recompute scroll target on next Init" (set by NewItem()).
    //   other = cached m_pShopList->m_Velocity.y, persisted across screen re-Init
    //           (written every Update tail; read back by CreateShopList/Init).
    static float s_ScrollOffset;

    // Helper — create the scrolling item list and populate from ItemManager.
    // Binary: this happens during Init (or the list is passed externally via
    // the GameTaskState). Port creates it locally in Init.
    void CreateShopList();

    // Helper — remove buy/equip buttons from HUD.
    void RemoveBuyButton();
    void RemoveEquipButton();

#if !defined(__bada__) && defined(FN_BLOCK_PRELOAD)
    // Task #66 Phase 2 -- port-only helper, draws a centered loading.tex quad
    // while m_bLoading is set (m_pBuyButton doesn't exist yet during state-0
    // load hold, so there's no button to arm a spinner symbol on -- see
    // ShopScreen.cpp DrawOrder for why this is a static quad, not a spin).
    void DrawLoadingOverlay();
#endif

#ifdef __bada__
    friend struct ShopScreenLayoutAssert;
#endif

public:
    // v1.6.1 ShopScreen::BuyButtonCallback @0x001b1874 -- buy/equip the selected
    //   item via ItemManager (BuyItem if locked; unequip+clear slot if equipped;
    //   else swap into per-slot cache m_pSlotItems and SetEquippedItem). Body in
    //   ShopScreen.cpp. Status-text writes to ShopListItem+0x54 are
    //   documented-skipped there (port's ShopListItem doesn't expose that char* slot).
    void BuyButtonCallback();
    // v1.6.1 ShopScreen::CancelCallback @0x001b244c -- set m_State=6 and reset the
    //   TutorialControl tute position. Body in ShopScreen.cpp.
    void CancelCallback();
    // v1.6.1 ShopScreen::ConfirmCallback @0x001b2388 -- cache the selected item into
    //   its slot, set m_State=5, and reset the TutorialControl tute position. Body in
    //   ShopScreen.cpp.
    void ConfirmCallback();
};

#if defined(__bada__)
#include <cstddef>
struct ShopScreenLayoutAssert {
    static_assert(offsetof(ShopScreen, m_TransitionAlpha) == 0x7c, "m_TransitionAlpha offset");
    static_assert(offsetof(ShopScreen, m_pBuyButton)      == 0x84, "m_pBuyButton offset");
    static_assert(offsetof(ShopScreen, m_pEquipButton)    == 0x8c, "m_pEquipButton offset");
    static_assert(offsetof(ShopScreen, m_pParent)         == 0x90, "m_pParent offset");
    static_assert(offsetof(ShopScreen, m_ScrollOffset)    == 0xac, "m_ScrollOffset offset");
    static_assert(offsetof(ShopScreen, m_reservedB0)      == 0xb0, "m_reservedB0 offset");
    static_assert(offsetof(ShopScreen, m_AnimFrame)       == 0xb4, "m_AnimFrame offset");
    static_assert(offsetof(ShopScreen, m_State)           == 0xb8, "m_State offset");
    static_assert(sizeof(ShopScreen) == 0xbc, "ShopScreen size must match binary");
};
#endif

#endif // FN_SHOP_SCREEN_H
