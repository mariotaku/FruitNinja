#ifndef FN_SHOP_SCREEN_H
#define FN_SHOP_SCREEN_H

//
// ShopScreen : HUDControl3d  (NOT BaseScreen)
// Sensei's Swag — the in-game blade/background shop.
//
// Binary refs:
//   ctor             0x0015cdac  ShopScreen(DojoScreen*)
//   dtor             0x0015ce98 / 0x0015ced8 / 0x0015cf14
//   LoadContent      0x0015cb08  (static, loads 11+ textures)
//   UnLoadContent    0x0015d080  (static, clears all textures)
//   Update           0x0015e1f4  (387 lines)
//   Draw             0x0015dd50
//   SetSelected      0x0015c870
//   ShrinkBuyButton  0x0015c4cc
//   ClickedOnShopItem 0x0015d4b4
//   QuitShopCallback 0x0015d55c
//   EquipCallback    0x0015d630
//   NewItem          0x0015c498
//
// Struct size: 0xBC bytes
//
// Base:   HUDControl3d (0x00..0x7b)
//   +0x74  SmartPtr<Texture>  m_Texture  (field_0x74)
//   +0x7C  float  m_TransitionAlpha      (lerps 0->1 on entry)
//   +0x80  int    m_LayerFlagsAlt        (0x40 or 0x80 depending on splats)
//   +0x84  MenuButton*   m_pBuyButton    (lazily created in state 0)
//   +0x88  float  m_BuyDelay             (timer decremented by dt)
//   +0x8C  MenuButton*   m_pEquipButton  (lazily created in state 1)
//   +0x90  DojoScreen*   m_pParent
//   +0x94  ScrollingMenu* m_pShopList    (the scrollable item list; null in ctor)
//   +0x98  ShopListItem*  m_pSelectedItem (ptr to currently selected list item)
//   +0x9C..+0xA8  ShopListItem* m_pSlotItems[3]  (cached selection per slot type)
//   +0xAC  float  m_ScrollOffset         (computed from item count + 0.5)
//   +0xB4  int    m_AnimFrame            (sin-based animation counter)
//   +0xB8  int    m_State                (state machine index)
//
// State machine (Update):
//   0: Transition in. alpha += (1-alpha)*0.125. On alpha>DAT_0015e554:
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
//   5/6: Wait for ActorManager empty then equip item via ItemManager.
//   7: Same as 2 (alternate quit path).
//
// Textures (static, loaded by LoadContent at 0x0015cb08):
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
//   - GL rendering stubbed (TODO: port Draw @ 0x0015dd50).
//   - ScrollingMenu is a stub (items visible but not scrollable).
//   - ItemManager stubs always return "unlocked/not-equipped".
//   - FruitSaveData::CheckDatesHaveChanged called in DojoScreen before push.
//
// Analysed: 2026-04-25T14:00
//

#include "hud/HUDControl3d.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"

class MenuButton;
class DojoScreen;
class ScrollingMenu;
class ShopListItem;
struct Game;
class HUD;

class ShopScreen : public HUDControl3d {
public:
    // Matches ShopScreen::ShopScreen(DojoScreen*) @ 0x0015cdac
    // Binary: calls HUDControl3d ctor, calls LoadContent if not loaded,
    //         initialises all fields, computes m_ScrollOffset from item count.
    // Port: adds Game& for HUD/GameSound access (binary uses GOT navigation).
    ShopScreen(Game& g, DojoScreen* parent);

    // Matches ~ShopScreen @ 0x0015ce98
    ~ShopScreen() override;

    // Matches vtable Init slot — called from DojoScreen state 2 launch:
    //   (**(code**)(*(int*)shop + 8))(shop)  == shop->Init()
    void Init() override;

    // Matches vtable Release slot
    void Release() override;

    // Matches ShopScreen::Update(float) @ 0x0015e1f4 (387 lines)
    void Update(float dt) override;

    // Matches ShopScreen::Draw(float*) @ 0x0015dd50
    // Binary param is actually a layerMask passed as float* (ARM calling convention),
    // but the port prototype matches HUDControl3d::Draw.
    void Draw(const Vec3& hudScale, int layerMask) override;

    int GetType() override { return 1; }

    // Matches ShopScreen::LoadContent @ 0x0015cb08
    // Loads all static textures. Called lazily from ctor if not yet loaded.
    static void LoadContent();

    // Matches ShopScreen::UnLoadContent @ 0x0015d080
    // Releases all static textures.
    static void UnLoadContent();

private:
    // +0x74: per-instance texture copy (set null in ctor)
    SmartPtr<Mortar::Texture> m_TexInst;      // field_0x74

    // +0x7C: transition alpha (0->1 lerp-in, *= 0.75 fade-out)
    float m_TransitionAlpha;

    // +0x80: layer flags alt (toggled between 0x40 and 0x80 based on splats)
    int m_LayerFlagsAlt;

    // +0x84: buy/equip action button (lazily created)
    MenuButton* m_pBuyButton;

    // +0x88: delay timer before buy button appears (decremented by dt)
    float m_BuyDelay;

    // +0x8C: equip button (lazily created when item is not equipped)
    MenuButton* m_pEquipButton;

    // Port specific: Game& reference for HUD/GameSound access.
    // Binary accesses these via GOT-relative GameTaskState pointer.
    Game& game;

    // +0x90: parent dojo screen (used to trigger GameState=8 on quit)
    DojoScreen* m_pParent;

    // +0x94: the scrollable item list (ScrollingMenu*)
    ScrollingMenu* m_pShopList;

    // +0x98: currently selected list item
    ShopListItem* m_pSelectedItem;

    // +0x9C..+0xA8: per-slot cached selection (3 entries, ItemType 0-2)
    ShopListItem* m_pSlotItems[3];

    // +0xAC: scroll position / animation offset (from item count + 0.5)
    float m_ScrollOffset;

    // +0xB4: sin-wave animation frame counter (0..0x3FFC range)
    int m_AnimFrame;

    // +0xB8: state machine index
    int m_State;

    // --- Static textures (GOT-relative globals in binary) ---
    // Slot layout matches LoadContent @ 0x0015cb08 store order.
    static SmartPtr<Mortar::Texture> s_TexSelectItem;      // +0x14: select_item.tex
    static SmartPtr<Mortar::Texture> s_TexNewItemSml;      // +0x18: new_item_sml.tex
    static SmartPtr<Mortar::Texture> s_TexDialogBox;       // +0x2c: dialog_box_shop.tex
    static SmartPtr<Mortar::Texture> s_TexLocked;          // +0x30: locked.tex
    static SmartPtr<Mortar::Texture> s_TexLockedStroke;    // +0x34: locked_stroke.tex
    static SmartPtr<Mortar::Texture> s_TexScratch;         // +0x38: scratch_deviders.tex
    static SmartPtr<Mortar::Texture> s_TexSelected;        // +0x3c: selected.tex
    static SmartPtr<Mortar::Texture> s_TexSelectedSml;     // +0x40: selected_sml.tex
    static SmartPtr<Mortar::Texture> s_TexUnknown44;       // +0x44: (not yet identified)
    static SmartPtr<Mortar::Texture> s_TexBGStore;         // +0x48: BG_store.tex / BG_store_sml.tex
    static bool s_bContentLoaded;                           // +0x4c: one-time init guard

    // --- Callbacks ---

    // Matches ShopScreen::QuitShopCallback @ 0x0015d55c
    // Plays SFX "menu-bomb", sets state=2 (or 7), flings buy-button fruit.
    void QuitShopCallback();

    // Matches ShopScreen::EquipCallback @ 0x0015d630
    // Calls ItemManager::SetEquippedItem with selected item, plays equip SFX.
    void EquipCallback();

    // Matches ShopScreen::ClickedOnShopItem(ScrollingMenuItem*) @ 0x0015d4b4
    // If item locked: play SFX, set item alpha=0.25. If equip button exists:
    // call TutorialControl::ButtonPressedAtPos.
    void ClickedOnShopItem(ShopListItem* item);

    // Matches ShopScreen::SetSelected(ShopListItem*) @ 0x0015c870
    // Updates m_pSelectedItem, resolves fruit types for buy/equip button display.
    void SetSelected(ShopListItem* item);

    // Matches ShopScreen::ShrinkBuyButton @ 0x0015c4cc
    // Triggers the "slice" shrink animation on the buy button fruit if not sliced.
    void ShrinkBuyButton();

    // Matches ShopScreen::NewItem @ 0x0015c498
    // Sets some global scroll-position variable to 1.0.
    // Called when a new item is available in the shop.
    void NewItem();

    // Helper — create the scrolling item list and populate from ItemManager.
    // Binary: this happens during Init (or the list is passed externally via
    // the GameTaskState). Port creates it locally in Init.
    void CreateShopList();

    // Helper — remove buy/equip buttons from HUD.
    void RemoveBuyButton();
    void RemoveEquipButton();
};

#endif // FN_SHOP_SCREEN_H
