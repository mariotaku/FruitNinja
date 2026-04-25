// ShopScreen — Sensei's Swag blade/background shop, launched from DojoScreen.
// Binary: ShopScreen(DojoScreen*) @ 0x0015cdac, Update @ 0x0015e1f4 (387 lines),
//         Draw @ 0x0015dd50, LoadContent @ 0x0015cb08.
//
// Analysed: 2026-04-25T16:30

#include "ShopScreen.h"
#include "DojoScreen.h"
#include "MainScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/MenuButton.h"
#include "hud/TutorialControl.h"
#include "hud/ScrollingMenu.h"
#include "hud/ShopListItem.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include "entities/ActorManager.h"
#include "game/ItemInfo.h"
#include "game/ItemManager.h"
#include "game/FruitSaveData.h"
#include "engine/audio/GameSound.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "math/Colour.h"
#include <cstdlib>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constants (resolved from binary DAT addresses via read_memory)
// ---------------------------------------------------------------------------

// Transition alpha rates (from Update state 0 decompile)
// DAT_0015e554 = 77 be 7f 3f = 0x3f7fbe77 ~ 0.999f
static const float ALPHA_LERP_IN       = 0.125f;   // state 0: += (1-alpha)*0.125
static const float ALPHA_IN_DONE       = 0.999f;   // DAT_0015e554

// States 2/7 decay (uses DAT_0015e90c, not literal 0.75):
// DAT_0015e90c = 9a 99 59 3f = 0x3f59999a = 0.85f
static const float ALPHA_DECAY_STATE27 = 0.85f;    // DAT_0015e90c

// State 3 decay uses literal 0.75 in decompile (not a DAT constant)
static const float ALPHA_DECAY_STATE3  = 0.75f;

// States 2/7 trigger threshold:
// DAT_0015e910 = 0a d7 23 3c = 0x3c23d70a ~ 0.01f
static const float ALPHA_STATE27_DONE  = 0.01f;    // DAT_0015e910

// State 3 fade completion threshold:
// DAT_0015e914 = 6f 12 83 3a = 0x3a83126f ~ 0.001f
static const float ALPHA_STATE3_DONE   = 0.001f;   // DAT_0015e914

// Buy delay initial value:
// DAT_0015e558 = 00 00 00 00 = 0.0f  (also used for initial m_TransitionAlpha)
static const float BUY_DELAY_INIT  = 0.0f;         // DAT_0015e558

// Animation frame increment per dt:
// DAT_0015e904 = ff 47 d5 47 = 0x47d547ff = ~109260.0f
// DAT_0015e908 = 00 f0 7f 46 = 0x467ff000 = 16380.0f = (float)0x3ffc
static const float ANIM_FRAME_RATE = 109260.0f;    // DAT_0015e904
static const int   ANIM_FRAME_MAX  = 0x3ffc;       // from decompile literal

// Quit/back button position (field_0x84, created in state 0):
// DAT_0015e55c = 00 00 39 43 = 185.0f
// DAT_0015e560 = 00 00 d2 c2 = -105.0f
// DAT_0015e558 = 0.0f (z)
static const Vec3 POS_BACK_BUTTON(185.0f, -105.0f, 0.0f);  // DAT_0015e55c/560/558

// Equip button position (field_0x8c, created in state 1):
// DAT_0015e564 = 00 00 11 43 = 145.0f
// DAT_0015e568 = 00 00 d0 42 = 104.0f
// z = DAT_0015e558 = 0.0f
static const Vec3 POS_EQUIP_BUTTON(145.0f, 104.0f, 0.0f);  // DAT_0015e564/568/558

// State-3 replacement back button position:
// DAT_0015e918 = 00 00 39 43 = 185.0f (same x)
// DAT_0015e91c = 00 00 d2 c2 = -105.0f (same y)
// z = DAT_0015e93c = 00 00 00 00 = 0.0f
static const Vec3 POS_BACK_BUTTON_NEW(185.0f, -105.0f, 0.0f);  // DAT_0015e918/91c/93c

// Post-creation scale multiplier for both buttons:
// DAT_0015e920 = 33 33 53 3f = 0x3f533333 = 0.825f
static const float BUTTON_SCALE = 0.825f;           // DAT_0015e920

// Equip button scale override after creation (hardcoded literal 0.75 in decompile)
static const float EQUIP_BUTTON_SCALE = 0.75f;

// Scroll list position animation parameters (from 0x0015ead8, 32 bytes):
// DAT_0015ead8 = 00 00 20 42 = 40.0f   (list pos y)
// DAT_0015eadc = 00 00 00 00 = 0.0f    (list pos z)
// DAT_0015eae0 = 00 00 be 42 = 95.0f   (slide offset from right edge)
// DAT_0015eae4 = 00 00 91 43 = 290.0f  (slide multiplier)
// Slide formula: pos.x = (1 - alpha) * 290.0 * -1.5 - 95.0
static const float LIST_POS_Y     = 40.0f;          // DAT_0015ead8
static const float LIST_POS_Z     = 0.0f;           // DAT_0015eadc
static const float LIST_SLIDE_OFF  = 95.0f;         // DAT_0015eae0
static const float LIST_SLIDE_MUL  = 290.0f;        // DAT_0015eae4

// Fling velocity base (state 3 and QuitShopCallback)
static const float FLING_VEL_BASE = 5.0f;           // from decompile literal

// ---------------------------------------------------------------------------
// Static texture storage
// ---------------------------------------------------------------------------

SmartPtr<Mortar::Texture> ShopScreen::s_TexSelectItem;
SmartPtr<Mortar::Texture> ShopScreen::s_TexNewItemSml;
SmartPtr<Mortar::Texture> ShopScreen::s_TexDialogBox;
SmartPtr<Mortar::Texture> ShopScreen::s_TexLocked;
SmartPtr<Mortar::Texture> ShopScreen::s_TexLockedStroke;
SmartPtr<Mortar::Texture> ShopScreen::s_TexScratch;
SmartPtr<Mortar::Texture> ShopScreen::s_TexSelected;
SmartPtr<Mortar::Texture> ShopScreen::s_TexSelectedSml;
SmartPtr<Mortar::Texture> ShopScreen::s_TexUnknown44;
SmartPtr<Mortar::Texture> ShopScreen::s_TexBGStore;
bool ShopScreen::s_bContentLoaded = false;

// ---------------------------------------------------------------------------
// ShopScreen::LoadContent @ 0x0015cb08
// Loads 11 textures into static slots.
// Binary pattern: LoadLocalisedTexture(name) -> store in static slot.
// Conditional at end: if LowResBackgrounds() load BG_store_sml.tex else BG_store.tex.
// ---------------------------------------------------------------------------
void ShopScreen::LoadContent() {
    if (s_bContentLoaded) return;

    // Slot +0x14: select_item.tex
    s_TexSelectItem   = Mortar::TextureManager::LoadLocalisedTexture("select_item.tex");
    // Slot +0x18: new_item_sml.tex
    s_TexNewItemSml   = Mortar::TextureManager::LoadLocalisedTexture("new_item_sml.tex");
    // Slot +0x2c: dialog_box_shop.tex
    s_TexDialogBox    = Mortar::TextureManager::LoadLocalisedTexture("dialog_box_shop.tex");
    // Slot +0x30: locked.tex
    s_TexLocked       = Mortar::TextureManager::LoadLocalisedTexture("locked.tex");
    // Slot +0x34: locked_stroke.tex
    s_TexLockedStroke = Mortar::TextureManager::LoadLocalisedTexture("locked_stroke.tex");
    // Slot +0x38: scratch_deviders.tex
    s_TexScratch      = Mortar::TextureManager::LoadLocalisedTexture("scratch_deviders.tex");
    // Slot +0x3c: selected.tex
    s_TexSelected     = Mortar::TextureManager::LoadLocalisedTexture("selected.tex");
    // Slot +0x40: selected_sml.tex
    s_TexSelectedSml  = Mortar::TextureManager::LoadLocalisedTexture("selected_sml.tex");
    // Slot +0x44: (9th tex — not yet identified from binary string list)
    // DIFFERS: name not resolved; slot left null for now
    // TODO: resolve DAT_0015cccc string to identify this texture
    // Slot +0x48: BG_store.tex or BG_store_sml.tex (low-res conditional)
    // Binary: if (LowResBackgrounds()) load BG_store_sml.tex else BG_store.tex
    // LowResBackgrounds() stub — always false in port
    s_TexBGStore = Mortar::TextureManager::LoadLocalisedTexture("BG_store.tex");

    s_bContentLoaded = true;
}

// ---------------------------------------------------------------------------
// ShopScreen::UnLoadContent @ 0x0015d080
// Clears all static texture slots in the same order as LoadContent.
// ---------------------------------------------------------------------------
void ShopScreen::UnLoadContent() {
    s_bContentLoaded = false;
    s_TexSelectItem.SetNull();
    s_TexNewItemSml.SetNull();
    s_TexDialogBox.SetNull();
    s_TexLocked.SetNull();
    s_TexLockedStroke.SetNull();
    s_TexScratch.SetNull();
    s_TexSelected.SetNull();
    s_TexSelectedSml.SetNull();
    s_TexUnknown44.SetNull();
    s_TexBGStore.SetNull();
}

// ---------------------------------------------------------------------------
// ShopScreen::ShopScreen(DojoScreen*) @ 0x0015cdac
// ---------------------------------------------------------------------------
ShopScreen::ShopScreen(Game& g, DojoScreen* parent)
    : HUDControl3d()
    , game(g)
    , m_TexInst()
    , m_TransitionAlpha(0.0f)
    , m_LayerFlagsAlt(0)
    , m_pBuyButton(nullptr)
    , m_BuyDelay(0.0f)
    , m_pEquipButton(nullptr)
    , m_pParent(parent)
    , m_pShopList(nullptr)
    , m_pSelectedItem(nullptr)
    , m_AnimFrame(0)
    , m_State(0)
{
    // Binary: call LoadContent if guard not set
    LoadContent();

    // Binary: field_0x34 (m_LayerFlags from HUDControl base) = 0x80
    m_LayerFlags = 0x80;

    // Binary: field_0x32 (m_bNoDestructor) = 0
    m_bNoDestructor = 0;

    // Binary: field_0x74 SmartPtr SetNull
    m_TexInst.SetNull();

    // Initialise slot items array
    m_pSlotItems[0] = nullptr;
    m_pSlotItems[1] = nullptr;
    m_pSlotItems[2] = nullptr;

    // Binary: m_ScrollOffset = (float)(ItemManager->GetNumItems()) + 0.5
    // Binary reads via vtable+0x20 on the GameTask's ItemManager ref.
    // Port: compute from stub (returns 0).
    ItemManager* im = ItemManager::GetInstance();
    int numItems = im ? im->GetNumItems() : 0;
    m_ScrollOffset = (float)numItems + 0.5f;

    // Binary: m_BuyDelay = DAT_0015cd98 (same constant used for m_TransitionAlpha
    // initial value — both set to 0 in port since alpha starts at 0).
    m_BuyDelay = 0.0f;
    m_TransitionAlpha = 0.0f;
}

// ---------------------------------------------------------------------------
// ShopScreen::~ShopScreen @ 0x0015ce98
// ---------------------------------------------------------------------------
ShopScreen::~ShopScreen() {
    Release();
}

// ---------------------------------------------------------------------------
// ShopScreen::Init (vtable slot 2, called by DojoScreen after AddControl)
// Binary: (**(code**)(*(int*)shop + 8))(shop)
// HUDControl3d::Init sets m_bActive = 1 typically. No symbol in binary
// for ShopScreen::Init — inherited or trivial override.
// ---------------------------------------------------------------------------
void ShopScreen::Init() {
    // Binary: InitVec3_ShopScreen @ 0x00153f20 sets a global Vec3 to (0,0,0)
    // Binary: ZeroInit_ShopScreen @ 0x00154460 zeros a global scroll var
    // Port: create the shop list here since the binary's Init populates it.
    CreateShopList();
}

// ---------------------------------------------------------------------------
// ShopScreen::Release
// Binary: called from dtor. Marks buy/equip buttons pending-removal.
// ---------------------------------------------------------------------------
void ShopScreen::Release() {
    RemoveBuyButton();
    RemoveEquipButton();

    if (m_pShopList) {
        m_pShopList->SetPendingRemoval();
        m_pShopList = nullptr;
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::CreateShopList
// Not a binary symbol — port helper to build the ScrollingMenu from ItemManager.
// In the binary, the list appears to be a globally-owned object in the
// GameTaskState that ShopScreen receives a pointer to (via m_pShopList = GOT entry).
// Port creates it locally since ItemManager is a stub returning no items.
// ---------------------------------------------------------------------------
void ShopScreen::CreateShopList() {
    if (m_pShopList) return;

    // TODO: in the binary the ScrollingMenu lives in GameTaskState and is
    // pre-populated from items.xml at init time. Port creates a local one.
    m_pShopList = new ScrollingMenu();

    // Populate from ItemManager (stub: no items currently)
    ItemManager* im = ItemManager::GetInstance();
    if (im) {
        int n = im->GetNumItems();
        for (int i = 0; i < n; i++) {
            ItemInfo* info = im->GetItem(i);
            if (!info) continue;
            ShopListItem* row = new ShopListItem();
            row->m_pItemInfo = info;
            m_pShopList->AddItem(row);
        }
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::RemoveBuyButton
// ---------------------------------------------------------------------------
void ShopScreen::RemoveBuyButton() {
    if (m_pBuyButton) {
        m_pBuyButton->SetPendingRemoval();
        m_pBuyButton = nullptr;
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::RemoveEquipButton
// ---------------------------------------------------------------------------
void ShopScreen::RemoveEquipButton() {
    if (m_pEquipButton) {
        m_pEquipButton->SetPendingRemoval();
        m_pEquipButton = nullptr;
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::NewItem @ 0x0015c498
// Binary: **(undefined4**)(GOT+offset) = 0x3f800000 (1.0f)
// Sets a global float to 1.0 (scroll-position indicator for new items).
// ---------------------------------------------------------------------------
void ShopScreen::NewItem() {
    // TODO: set global scroll-position flag to 1.0
    // Binary: *(GOT + DAT_0015c4b4) = 0x3f800000
}

// ---------------------------------------------------------------------------
// ShopScreen::ShrinkBuyButton @ 0x0015c4cc
// Binary: if m_pEquipButton != null && fruit piece != null && !Fruit::Sliced():
//   set fruit piece b4=1 (sliced flag), copy velocity from a global Vec3,
//   set m_bEnabled=0, copy target size.
// ---------------------------------------------------------------------------
void ShopScreen::ShrinkBuyButton() {
    // Matches binary: guards on m_pEquipButton, fruit piece, and not-sliced.
    if (!m_pEquipButton) return;
    if (!m_pEquipButton->m_pFruitPiece) return;
    // Binary: Fruit::Sliced() — check if fruit is already sliced
    // DIFFERS: Fruit::Sliced() not yet ported; assume not sliced
    // TODO: call Fruit::Sliced(m_pEquipButton->m_pFruitPiece) when ported

    // Binary: mark fruit as sliced (*(byte*)(fruit+0xb4) = 1)
    // Set equip button disabled
    m_pEquipButton->m_bEnabled = 0;
    // TODO: copy velocity/target-size from the global Vec3 constants
    // (DAT_0015c518 / DAT_0015c51c)
}

// ---------------------------------------------------------------------------
// ShopScreen::SetSelected(ShopListItem*) @ 0x0015c870
// Updates m_pSelectedItem and refreshes equip-button fruit type.
// ---------------------------------------------------------------------------
void ShopScreen::SetSelected(ShopListItem* item) {
    m_pSelectedItem = item;

    // Binary guards: only do fruit-type update if:
    //   m_pShopList->m_bTouchProcessed != 0 (== scrollable)
    //   m_pEquipButton != null
    //   item->m_pItemInfo != null
    //   m_State == 1
    if (!m_pShopList || !m_pShopList->m_bTouchProcessed) return;
    if (!m_pEquipButton) return;
    if (!item || !item->m_pItemInfo) return;
    if (m_State != 1) return;

    // Binary: lazily init two fruit-type globals (guarded by __cxa_guard_acquire)
    //   type_unlocked = Fruit::FruitType(DAT_0015c970, false)  (e.g. "watermelon")
    //   type_locked   = Fruit::FruitType(DAT_0015c974, false)  (e.g. "coconut")
    // Then set equip button texture and fruit type based on IsLocked.
    // DIFFERS: fruit type strings not resolved from DAT_0015c970/c974
    // TODO: resolve via read_memory and call Fruit::FruitType(name, false)
    ItemInfo* info = item->m_pItemInfo;
    if (info->IsLocked() == 0) {
        // Item is unlocked: set buy-now texture + unlocked fruit type
        // Binary: SmartPtr::operator= on (m_pEquipButton+0x74) <- static tex +0x18
        // Then Fruit::SetFruitType(m_pEquipButton->m_pFruitPiece, type, 1.0)
        // TODO: port once fruit type names are resolved
    } else {
        // Item is locked: set locked texture + locked fruit type
        // Binary: SmartPtr::operator= on (m_pEquipButton+0x74) <- static tex +0x14
        // TODO
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::ClickedOnShopItem(ScrollingMenuItem*) @ 0x0015d4b4
// Binary: if item->m_pItemInfo null OR IsLocked: play SFX, alpha=0.25.
//         else if m_pEquipButton != null: TutorialControl::ButtonPressedAtPos.
// ---------------------------------------------------------------------------
void ShopScreen::ClickedOnShopItem(ShopListItem* item) {
    if (!item) return;

    if (!item->m_pItemInfo || item->m_pItemInfo->IsLocked() != 0) {
        // Binary: GameSound::SFXPlay(gameSound, "equip-locked", 1.0, 1.0)
        if (game.pGameSound) {
            game.pGameSound->SFXPlay("equip-locked", 1.0f, 1.0f);
        }
        item->m_Alpha = 0.25f;   // 0x3e800000 in binary
    } else {
        if (m_pEquipButton) {
            // Binary: TutorialControl::ButtonPressedAtPos(tute, no args)
            // TODO: resolve TutorialControl from GameTaskState
        }
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::QuitShopCallback @ 0x0015d55c
// Binary: SFXPlay("menu-bomb"), set m_State=2,
//         SetVisible_FruitFact(buy_button->fruit_piece),
//         fling fruit piece with random velocity,
//         TutorialControl::ResetTutePos(tute, 0).
// ---------------------------------------------------------------------------
void ShopScreen::QuitShopCallback() {
    // Binary: GameSound::SFXPlay(gameSound, "menu-bomb", 1.0, 1.0)
    if (game.pGameSound) {
        game.pGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }

    // Set state to transition-out (state 2)
    m_State = 2;

    // Fling the back/quit button's fruit piece
    if (m_pBuyButton && m_pBuyButton->m_pFruitPiece) {
        // Binary: SetVisible_FruitFact sets m_bDetached = 1 on the fruit
        m_pBuyButton->m_pFruitPiece->m_bDetached = true;
        // Binary: vel = (RandFloat5 + 5.0, -RandFloat5, 0)
        float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        m_pBuyButton->m_pFruitPiece->vel = Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
    }

    // Binary: TutorialControl::ResetTutePos(tute, 0) — null MenuButton* hides arrow
    if (game.pTutorialCtrl) {
        game.pTutorialCtrl->ResetTutePos((MenuButton*)nullptr);
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::EquipCallback @ 0x0015d630
// Binary: if m_pEquipButton exists:
//   if !s_LowRes: call ItemManager::SetEquippedItem; play equip SFX.
//   else:         animate equip button fruit piece (set vel from a global Vec3,
//                 copy target-size from global).
// ---------------------------------------------------------------------------
void ShopScreen::EquipCallback() {
    if (!m_pEquipButton) return;

    // Binary: reads a global "low-res/upsell" flag at (GOT + DAT_0015d778)
    // DIFFERS: flag not resolved; always take the "non-upsell" path
    // TODO: resolve DAT_0015d778 flag

    if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
        ItemInfo* info = m_pSelectedItem->m_pItemInfo;
        ItemManager* im = ItemManager::GetInstance();
        if (im) {
            im->SetEquippedItem((int)info->m_Type, info);
        }
        // Binary: SFX depends on item type:
        //   type == 0 (blade):      SFXPlay("equip-new-sword")
        //   type == 1 (background): SFXPlay("equip-new-wallpaper")
        if (game.pGameSound) {
            const char* sfxName = (info->m_Type == ITEM_TYPE_BLADE)
                                  ? "equip-new-sword"
                                  : "equip-new-wallpaper";
            game.pGameSound->SFXPlay(sfxName, 1.0f, 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::Update(float) @ 0x0015e1f4 (387 lines)
// ---------------------------------------------------------------------------
void ShopScreen::Update(float dt) {
    float prevAlpha = m_TransitionAlpha;

    // Binary: SplatEntity::NumActiveSplats() — if 0, set m_LayerFlagsAlt = 0x40
    // DIFFERS: SplatEntity::NumActiveSplats() not ported — always treat as 0
    m_LayerFlagsAlt = 0x40;

    // Binary: check if list selection has changed
    if (m_pShopList && m_pShopList->GetItemClosestToZeroIdx() != (int)(intptr_t)m_pSelectedItem) {
        // More precisely: compare m_pSelectedItem address (cast to int) with
        // list's GetItemClosestToZeroIdx() return (also an int/ptr).
        // Binary: if (field_0x94 != null && field_0x98 != list->GetIdx() && !m_bTouchProcessed)
        if (!m_pShopList->m_bTouchProcessed) {
            ShopListItem* newSel = static_cast<ShopListItem*>(m_pShopList->GetItemClosestToZero());
            if (newSel != m_pSelectedItem) {
                SetSelected(newSel);
            }
        }
    }

    // Binary: animation counter update (runs in state 1, after initial checks)
    // Uses __aeabi_idivmod(m_AnimFrame + 1, 10) — wraps counter mod 10
    // Actually: stored at (GameTaskState + 0x88), not field_0xb4.
    // field_0xb4 is the sin-wave frame for the buy button animation.

    // Sync layer flags from alt (binary copies field_0x80 to field_0x34 each frame)
    m_LayerFlags = (uint32_t)m_LayerFlagsAlt;

    switch (m_State) {

    // ---- STATE 0: Transition in ----
    case 0: {
        // Binary: alpha += (1 - alpha) * 0.125
        float newAlpha = m_TransitionAlpha + (1.0f - m_TransitionAlpha) * ALPHA_LERP_IN;
        m_TransitionAlpha = newAlpha;

        if (newAlpha > ALPHA_IN_DONE) {
            // Binary: SplatEntity::RemoveAllSplats()
            // TODO: call when SplatEntity ported (SplatEntity exists but
            // NumActiveSplats/RemoveAllSplats static wrappers not yet exposed)

            // Set transition alpha to 1 and buy delay
            m_TransitionAlpha = 1.0f;
            m_BuyDelay = BUY_DELAY_INIT;  // 0.0f from DAT_0015e558
            m_State = 1;

            // Create the back/quit button (field_0x84 = m_pBuyButton).
            // Binary: MenuButton ctor at state 0 completion uses QuitShopCallback
            // as the press delegate (confirmed via xref DATA at 0x0015e2fc/0x0015e2fe).
            // Texture comes from *(GameTask + 0x17c) — a per-task SmartPtr<Texture>.
            // Fruit type: *(GameTask + GOT_DAT_0015e578) — int pre-stored in task.
            // Port uses bomb fruit type (FruitInfo_GetCount()) matching the
            // DojoScreen back-button pattern: out-of-range index forces a bomb.
            // Binary: after AddControl, scales m_TargetSize and fruit piece by
            // DAT_0015e920 = 0.825f.
            if (!m_pBuyButton) {
                const int backFruitType = FruitInfo_GetCount();  // forces bomb spawn
                m_pBuyButton = new MenuButton();
                m_pBuyButton->Init(POS_BACK_BUTTON,
                    [this]() { QuitShopCallback(); },
                    backFruitType, Vec3(0.0f, 0.0f, 0.0f), nullptr);
                m_pBuyButton->m_bEnabled = 1;
                if (game.hud) game.hud->AddControl(m_pBuyButton, false);
                if (game.pTutorialCtrl) game.pTutorialCtrl->ResetTutePos(m_pBuyButton);
                // Binary: m_TargetSize *= 0.825; fruit piece scale *= 0.825
                m_pBuyButton->m_TargetSize = m_pBuyButton->m_TargetSize * BUTTON_SCALE;
                if (m_pBuyButton->m_pFruitPiece) {
                    m_pBuyButton->m_pFruitPiece->scale =
                        m_pBuyButton->m_pFruitPiece->scale * BUTTON_SCALE;
                }
            }
        }
        break;
    }

    // ---- STATE 1: Active / idle ----
    case 1: {
        // Decrement buy delay
        m_BuyDelay -= dt;

        // Check list selection
        if (m_pShopList && m_pShopList->m_bTouchProcessed) {
            ShopListItem* cur = static_cast<ShopListItem*>(m_pShopList->GetItemClosestToZero());
            if (cur != m_pSelectedItem && m_pShopList->m_bTouchProcessed) {
                SetSelected(cur);
            }
        }

        if (m_BuyDelay <= 0.0f) {
            // Binary: if list m_bTouchProcessed == 0: ShrinkBuyButton()
            if (m_pShopList && !m_pShopList->m_bTouchProcessed) {
                ShrinkBuyButton();
            } else {
                // Binary: also check IsEquipped + IsLocked to decide if equip
                // button should be removed (if already equipped or locked)
                if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
                    ItemManager* im = ItemManager::GetInstance();
                    if (im) {
                        int equipped = im->IsEquipped(m_pSelectedItem->m_pItemInfo);
                        int locked   = m_pSelectedItem->m_pItemInfo->IsLocked();
                        if (equipped != 0 || locked != 0) {
                            // Binary: TutorialControl::ResetTutePos(tute, 0) — null
                            if (game.pTutorialCtrl)
                                game.pTutorialCtrl->ResetTutePos((MenuButton*)nullptr);
                        }
                    }
                }
                // Binary: create equip button (field_0x8c) if not already present
                // and selected item is not equipped.
                // Texture: *(GameTask + s_TexSelectItem offset) — same slot +0x14
                // Fruit type: Fruit::FruitType(*(GameTask + DAT_0015e58c), false)
                // — string stored in GameTask at a GOT-relative offset.
                // Port uses Fruit::FruitType("pineapple", false) as placeholder
                // (same type DojoScreen uses for its shop button).
                // DIFFERS: original fruit type string from DAT_0015e58c not resolved.
                if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
                    ItemManager* im = ItemManager::GetInstance();
                    if (im) {
                        int equipped = im->IsEquipped(m_pSelectedItem->m_pItemInfo);
                        if (equipped == 0 && !m_pEquipButton) {
                            const int equipFruitType =
                                Fruit::FruitType("pineapple", false);  // DIFFERS: DAT_0015e58c
                            m_pEquipButton = new MenuButton();
                            m_pEquipButton->Init(POS_EQUIP_BUTTON,
                                [this]() { EquipCallback(); },
                                equipFruitType, Vec3(0.0f, 0.0f, 0.0f), nullptr);
                            m_pEquipButton->m_bEnabled = 0;
                            SetSelected(m_pSelectedItem);
                            if (game.hud) game.hud->AddControl(m_pEquipButton, false);
                            if (game.pTutorialCtrl)
                                game.pTutorialCtrl->ResetTutePos(m_pEquipButton);
                            // Binary: m_TargetSize *= 0.75; fruit piece scale *= 0.75
                            m_pEquipButton->m_TargetSize =
                                m_pEquipButton->m_TargetSize * EQUIP_BUTTON_SCALE;
                            if (m_pEquipButton->m_pFruitPiece) {
                                m_pEquipButton->m_pFruitPiece->scale =
                                    m_pEquipButton->m_pFruitPiece->scale * EQUIP_BUTTON_SCALE;
                            }
                        }
                    }
                }
            }
        }

        // Update sin-wave animation frame
        // Binary: fVar = (float)m_AnimFrame + dt * DAT_0015e904
        //         clamp to [0, 0x3ffc]
        float fFrame = (float)m_AnimFrame + dt * ANIM_FRAME_RATE;
        if (fFrame < 0.0f) {
            m_AnimFrame = 0;
        } else if (fFrame < (float)ANIM_FRAME_MAX) {
            m_AnimFrame = (int)fFrame;
        } else {
            m_AnimFrame = ANIM_FRAME_MAX;
        }
        break;
    }

    // ---- STATES 2 and 7: Transition out to dojo ----
    case 2:
    case 7: {
        // Binary: uses DAT_0015e90c = 0.85f (not 0.75f — state 3 uses 0.75 literal)
        float newAlpha = ALPHA_DECAY_STATE27 * m_TransitionAlpha;
        m_TransitionAlpha = newAlpha;

        // Binary condition: (newAlpha < DAT_0015e910) && (m_State == 2) && (m_pParent != null)
        // Only state 2 triggers the main-screen transition; state 7 fades but does nothing else.
        if (newAlpha < ALPHA_STATE27_DONE && m_State == 2 && m_pParent) {
            // Binary: *(parent + 0x33) = 1  =>  parent->m_bPendingRemoval = 1
            // (field_0x33 in HUDControl is m_bPendingRemoval, NOT m_bNoDestructor which is +0x32)
            m_pParent->m_bPendingRemoval = 1;
            // Binary: this->field_0x33 = 1  =>  self->m_bPendingRemoval = 1
            m_bPendingRemoval = 1;
            // Binary: *(*(*(GameTask + 0x7990) + 0x160) + 0x10c) = 8
            //         => mainScreen->m_State = STATE_SLIDE_IN (8)
            // DAT_0015e924 = 0x7990 (GOT offset to the GameTask/game pointer),
            // +0x160 = mainScreen ptr in Game, +0x10c = m_State in MainScreen.
            if (game.mainScreen) {
                game.mainScreen->SetState(STATE_SLIDE_IN);
            }
        }
        break;
    }

    // ---- STATE 3: Buy animation fade-out ----
    case 3: {
        // Binary: uses literal 0.75f (not the 0.85f from DAT_0015e90c).
        float newAlpha = m_TransitionAlpha * ALPHA_DECAY_STATE3;
        m_TransitionAlpha = newAlpha;

        // ARM idiom: if (-1 < (int)((uint)(newAlpha < threshold) << 0x1f))
        //   fires when newAlpha >= threshold, i.e. NOT yet done fading.
        if (newAlpha >= ALPHA_STATE3_DONE) {
            // Still fading: fling old back-button fruit piece if present.
            // Binary: SetVisible_GameTask(), then set vel, then null the ptr.
            if (m_pBuyButton && m_pBuyButton->m_pFruitPiece) {
                m_pBuyButton->m_pFruitPiece->m_bDetached = true;
                float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
                float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
                m_pBuyButton->m_pFruitPiece->vel =
                    Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
                // Binary: TutorialControl::ResetTutePos(tute, 0)
                if (game.pTutorialCtrl)
                    game.pTutorialCtrl->ResetTutePos((MenuButton*)nullptr);
                m_pBuyButton = nullptr;
            }
            break;
        }

        // Fade complete: transition to state 4, create replacement back button.
        // Binary: state = 4, alpha = 0.0f (DAT_0015e93c = 0.0f)
        m_State = 4;
        m_TransitionAlpha = 0.0f;

        // Binary: create new MenuButton at POS_BACK_BUTTON_NEW (185, -105, 0)
        // with same QuitShopCallback. Texture from *(GameTask + 0x17c).
        // Fruit type: **(GameTask + DAT_0015e938) (another pre-stored int).
        // Port uses same backFruitType (bomb) as state 0.
        // After AddControl (LAB_0015e874):
        //   m_TargetSize *= DAT_0015e920 (0.825f)
        //   fruit piece scale *= 0.825f
        {
            const int backFruitType = FruitInfo_GetCount();
            m_pBuyButton = new MenuButton();
            m_pBuyButton->Init(POS_BACK_BUTTON_NEW,
                [this]() { QuitShopCallback(); },
                backFruitType, Vec3(0.0f, 0.0f, 0.0f), nullptr);
            m_pBuyButton->m_bEnabled = 1;
            if (game.hud) game.hud->AddControl(m_pBuyButton, false);
        }
        // LAB_0015e874: scale new button (reached by both state 0 and state 3 paths)
        m_pBuyButton->m_TargetSize = m_pBuyButton->m_TargetSize * BUTTON_SCALE;
        if (m_pBuyButton->m_pFruitPiece) {
            m_pBuyButton->m_pFruitPiece->scale =
                m_pBuyButton->m_pFruitPiece->scale * BUTTON_SCALE;
        }
        break;
    }

    // ---- STATE 4: Reset layer flags ----
    case 4:
        m_LayerFlagsAlt = 0x80;
        break;

    // ---- STATES 5 and 6: Wait for actors empty, then equip item ----
    case 5:
    case 6: {
        // Fling buy button fruit piece (same as state 3)
        if (m_pBuyButton && m_pBuyButton->m_pFruitPiece) {
            m_pBuyButton->m_pFruitPiece->m_bDetached = true;
            float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
            float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
            m_pBuyButton->m_pFruitPiece->vel =
                Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
            // Binary: TutorialControl::ResetTutePos(tute, 0)
            if (game.pTutorialCtrl)
                game.pTutorialCtrl->ResetTutePos((MenuButton*)nullptr);
            m_pBuyButton = nullptr;
        }

        // Binary: wait until ActorManager has no active entities (both pools)
        // Then equip selected item via ItemManager.
        ActorManager* am = ActorManager::GetInstance();
        int nLayer1 = am ? am->GetNumEntities(1) : 0;
        int nLayer0 = am ? am->GetNumEntities(0) : 0;
        if (nLayer1 == 0 && nLayer0 == 0) {
            if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
                ItemInfo* info = m_pSelectedItem->m_pItemInfo;
                char type = info->m_Type;
                if ((int)type < 4) {
                    // Binary: if cached slot item != selected item,
                    //   call ItemManager::SetEquippedItem(type, old_slot_item->ItemInfo)
                    ShopListItem* slotItem = ((unsigned)type < 3) ? m_pSlotItems[(int)type] : nullptr;
                    if (slotItem != m_pSelectedItem) {
                        ItemManager* im = ItemManager::GetInstance();
                        ItemInfo* prevInfo = slotItem ? slotItem->m_pItemInfo : nullptr;
                        if (im) {
                            im->SetEquippedItem((int)type, prevInfo);
                        }
                    }
                }
            }
            m_State = 0;
        }
        break;
    }

    default:
        break;
    }

    // --- Animate scrolling list position ---
    // Binary: set m_pShopList->pos.x = (1 - alpha) * LIST_SLIDE_MUL * -1.5 - LIST_SLIDE_OFF
    //         and copy a colour/timer from m_pShopList to a global
    if (m_pShopList) {
        float slideX = (1.0f - m_TransitionAlpha) * LIST_SLIDE_MUL * -1.5f - LIST_SLIDE_OFF;
        m_pShopList->pos.x = slideX;
        m_pShopList->pos.y = LIST_POS_Y;
        m_pShopList->pos.z = LIST_POS_Z;
    }

    // --- Animate HUD controls that moved during alpha change ---
    // Binary: if (alpha < prevAlpha): iterate over HUD control array,
    //   shift their y-position based on alpha delta.
    // TODO: port HUD control iteration once GameTaskState layout resolved
    (void)prevAlpha;
}

// ---------------------------------------------------------------------------
// ShopScreen::Draw(float*) @ 0x0015dd50
// Draws the shop background panel and item detail display.
// Full GL rendering not yet ported — stub.
// ---------------------------------------------------------------------------
void ShopScreen::Draw(const Vec3& /*hudScale*/, int layerMask) {
    if ((layerMask & m_LayerFlags) == 0) return;

    // TODO: port full Draw @ 0x0015dd50.
    // Binary draw structure:
    //   Block A (when m_LayerFlags == m_LayerFlagsAlt):
    //     if alpha < 1.0:
    //       Draw BG_store.tex sliding from right (translate by scroll offset)
    //       Draw select_item.tex (locked/unlocked indicator ring)
    //     else:
    //       Draw BG_store.tex at full size
    //     Draw item detail: scale by (list.GetWidth+1, list.GetHeight+1, 1)
    //       Translate to (scrollOffset - 4, -3, 0)
    //       if unlocked: animate grey->white using m_BuyDelay + SFX alpha
    //       else:        animate white->grey
    //       Draw s_TexSelected or s_TexLocked accordingly
    //   Block B (when m_AnimFrame > 0):
    //     Sin-wave scale animation on buy button fruit
    //     Draw with s_TexSelected texture
}
