// ShopScreen — Sensei's Swag blade/background shop, launched from DojoScreen.
// Binary: ShopScreen(DojoScreen*) @ 0x0015cdac, Update @ 0x0015e1f4 (387 lines),
//         Draw @ 0x0015dd50, LoadContent @ 0x0015cb08.
//
// Analysed: 2026-04-25T18:15

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
#include "entities/Bomb.h"
#include "entities/FruitInfo.h"
#include "entities/SplatEntity.h"
#include "entities/ActorManager.h"
#include "game/ItemInfo.h"
#include "game/ItemManager.h"
#include "game/FruitSaveData.h"
#include "engine/audio/GameSound.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "math/Colour.h"
#include "math/MathUtil.h"
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

SmartPtr<Mortar::Texture> ShopScreen::s_TexLocked;
SmartPtr<Mortar::Texture> ShopScreen::s_TexSelectItem;
SmartPtr<Mortar::Texture> ShopScreen::s_TexLoading;
SmartPtr<Mortar::Texture> ShopScreen::s_TexScratch;
SmartPtr<Mortar::Texture> ShopScreen::s_TexDialogBox;
SmartPtr<Mortar::Texture> ShopScreen::s_TexSelected;
SmartPtr<Mortar::Texture> ShopScreen::s_TexSelectedSml;
SmartPtr<Mortar::Texture> ShopScreen::s_TexLockedStroke;
SmartPtr<Mortar::Texture> ShopScreen::s_TexNewItemSmlBadge;
SmartPtr<Mortar::Texture> ShopScreen::s_TexBGStore;
SmartPtr<Mortar::Texture> ShopScreen::s_TexBackIcon;
bool ShopScreen::s_bContentLoaded = false;

// Port-only helpers (mirror DojoScreen pattern).
static GLuint TexIdOf(const SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->m_TexId : 0;
}
static Vec3 TexSizeOf(const SmartPtr<Mortar::Texture>& tex,
                      float defW, float defH) {
    if (tex.IsValid())
        return Vec3((float)tex->m_Width, (float)tex->m_Height, 1.0f);
    return Vec3(defW, defH, 1.0f);
}

// ---------------------------------------------------------------------------
// ShopScreen::LoadContent @ 0x0015cb08
// Loads 11 textures into static slots.
// Binary pattern: LoadLocalisedTexture(name) -> store in static slot.
// Conditional at end: if LowResBackgrounds() load BG_store_sml.tex else BG_store.tex.
// ---------------------------------------------------------------------------
void ShopScreen::LoadContent() {
    if (s_bContentLoaded) return;

    // Corrected slot order from LoadContent @ 0x0015cb08 disasm + string reads.
    // Slot +0x14: locked.tex          DAT_0015ccb8 -> 0x001bc15e
    s_TexLocked          = Mortar::TextureManager::LoadLocalisedTexture("locked.tex");
    // Slot +0x18: select_item.tex     DAT_0015ccbc -> 0x001bc169
    s_TexSelectItem      = Mortar::TextureManager::LoadLocalisedTexture("select_item.tex");
    // Slot +0x2c: loading.tex         DAT_0015cca8 -> 0x001bb184
    s_TexLoading         = Mortar::TextureManager::LoadLocalisedTexture("loading.tex");
    // Slot +0x30: scratch_deviders.tex  DAT_0015ccb0 -> 0x001bc135
    s_TexScratch         = Mortar::TextureManager::LoadLocalisedTexture("scratch_deviders.tex");
    // Slot +0x34: dialog_box_shop.tex  DAT_0015ccb4 -> 0x001bc14a
    s_TexDialogBox       = Mortar::TextureManager::LoadLocalisedTexture("dialog_box_shop.tex");
    // Slot +0x38: selected.tex         DAT_0015ccc0 -> 0x001bc179
    s_TexSelected        = Mortar::TextureManager::LoadLocalisedTexture("selected.tex");
    // Slot +0x3c: selected_sml.tex     DAT_0015ccc4 -> 0x001bc186
    s_TexSelectedSml     = Mortar::TextureManager::LoadLocalisedTexture("selected_sml.tex");
    // Slot +0x40: locked_stroke.tex    DAT_0015ccc8 -> 0x001bc197
    s_TexLockedStroke    = Mortar::TextureManager::LoadLocalisedTexture("locked_stroke.tex");
    // Slot +0x44: new_item_sml.tex     DAT_0015cccc -> 0x001bc1a9
    s_TexNewItemSmlBadge = Mortar::TextureManager::LoadLocalisedTexture("new_item_sml.tex");
    // Slot +0x48: BG_store.tex or BG_store_sml.tex (low-res conditional)
    // Binary: if (LowResBackgrounds()) load BG_store_sml.tex else BG_store.tex
    // LowResBackgrounds() stub -- always false in port
    s_TexBGStore         = Mortar::TextureManager::LoadLocalisedTexture("BG_store.tex");

    // Port-only: back-icon for the back/quit button. Binary reads this
    // from a per-task slot (*(GameTask + 0x17c)); port loads back_icon.tex
    // directly (matches DojoScreen back-button texture).
    s_TexBackIcon = Mortar::TextureManager::LoadLocalisedTexture("back_icon.tex");

    s_bContentLoaded = true;
}

// ---------------------------------------------------------------------------
// ShopScreen::UnLoadContent @ 0x0015d080
// Clears all static texture slots in the same order as LoadContent.
// ---------------------------------------------------------------------------
void ShopScreen::UnLoadContent() {
    s_bContentLoaded = false;
    s_TexLocked.SetNull();
    s_TexSelectItem.SetNull();
    s_TexLoading.SetNull();
    s_TexScratch.SetNull();
    s_TexDialogBox.SetNull();
    s_TexSelected.SetNull();
    s_TexSelectedSml.SetNull();
    s_TexLockedStroke.SetNull();
    s_TexNewItemSmlBadge.SetNull();
    s_TexBGStore.SetNull();
    s_TexBackIcon.SetNull();
}

// ---------------------------------------------------------------------------
// ShopScreen::ShopScreen(DojoScreen*) @ 0x0015cdac
// ---------------------------------------------------------------------------
ShopScreen::ShopScreen(Game& g, DojoScreen* parent)
    : HUDControl3d()
    , game(g)
    , m_TexInst()
    , m_TransitionAlpha(0.0f)
    // Binary ShopScreen ctor at 0x0015cdac does NOT initialize +0x80
    // (m_LayerFlagsAlt). Heap memory there is whatever the allocator
    // returns. Port previously initialized it to 0, which combined with
    // Update's `if (splats == 0) m_LayerFlagsAlt = 0x40` gate produced a
    // deterministic failure: when the user enters Shop with splats still
    // alive from Dojo's slice (which lasts ~4-6s and overlaps the ~0.5s
    // state-0 transition), m_LayerFlagsAlt stayed at 0, the Update sync
    // `m_LayerFlags = m_LayerFlagsAlt` made flags=0, HUD::Draw filtered
    // ShopScreen out, and the BG slide-in animation never rendered.
    // Default to the active drawable layer (0x40) so HUD::Draw dispatches
    // ShopScreen during state-0 even before Update has run.
    , m_LayerFlagsAlt(0x40)
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

    // Binary ShopScreen::Init at 0x0015f820 calls vtable[19] = SetHeight(80.0).
    // Port previously left m_Height at the ctor default (240), making
    // totalScrollH = 240 - 17*80 = -1120 and clipping the scroll range to
    // -1120 -- the bottom 2 snap targets (-1200, -1280) became unreachable.
    // With m_Height = 80 (one row), totalScrollH = 80 - 1360 = -1280, which
    // covers every item's snap target.
    m_pShopList->SetHeight(80.0f);

    // ScrollingMenu must live in the HUD so HUD::Update ticks its
    // Update (touch + scroll physics + per-item layout via Move) and
    // HUD::Draw dispatches its Draw (which iterates and draws items).
    // Without this, ScrollingMenu sat orphaned and the list was never
    // positioned or rendered. Layer 0x40 matches the menu/HUD layer
    // used by MenuButtons on the same screen.
    m_pShopList->m_LayerFlags = 0x40;

    // Populate from ItemManager
    // Binary (ShopScreen::Init @ 0x0015f7ac): for each ItemInfo from GetFirst/GetNext:
    //   operator_new(0x284) -> ShopListItem::ShopListItem() -> ShopListItem::Create(item, screen)
    //   -> ScrollingMenu::AddItem()
    // ShopListItem::Create sets m_ParamWidth (+0x24) = 80.0f (DAT_0015cae8),
    // which is what GetHeight() returns, giving each row a pitch of 80 units.
    ItemManager* im = ItemManager::GetInstance();
    if (im) {
        int n = im->GetNumItems();
        for (int i = 0; i < n; i++) {
            ItemInfo* info = im->GetItemAt(i);
            if (!info) continue;
            ShopListItem* row = new ShopListItem();
            // Binary: ShopListItem::Create(row, info, this) called immediately after ctor.
            // This sets GetHeight() = 80.0f (row pitch = 160 units per item).
            row->Create(info, this);
            m_pShopList->AddItem(row);
        }
    }

    if (game.hud) {
        game.hud->AddControl(m_pShopList);
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
// Sets a static float to 1.0f -- a scroll-position indicator that the
// shop list reads to highlight rows whose ItemInfo->m_bSeen is false.
// ---------------------------------------------------------------------------
float ShopScreen::s_NewItemAlpha = 0.0f;

void ShopScreen::NewItem() {
    s_NewItemAlpha = 1.0f;
}

// ---------------------------------------------------------------------------
// ShopScreen::GetDescriptionTextXPos @ 0x0015c520
// Returns the X anchor for the description text column.
// Binary: applies same slide formula as the list, offset by -80.0f.
// At alpha=1.0: 145.0f - 80.0f = 65.0f (text anchored left of dialog box).
// At alpha=0.0: 430.0f - 80.0f = 350.0f (text off-screen to the right).
// DIFFERS: exact formula not fully confirmed from binary; approximated from
//          the list-slide formula (LIST_SLIDE_OFF=95.0 + -80 offset = 65 rest).
// ---------------------------------------------------------------------------
float ShopScreen::GetDescriptionTextXPos() const {
    // Slide formula matches Block A/B: 145.0 + (1 - alpha) * 190.0 * 1.5
    // then subtract 80.0f for the text indent inside the dialog box.
    float slide_X = 145.0f + (1.0f - m_TransitionAlpha) * 190.0f * 1.5f;
    return slide_X - 80.0f;
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
        item->m_LockFlashAlpha = 0.25f;   // 0x3e800000 in binary; offset +0x264
    } else {
        if (m_pEquipButton) {
            // Matches ShopScreen::ClickedOnShopItem @ 0x0015d4e4
            if (game.pTutorialCtrl)
                game.pTutorialCtrl->ButtonPressedAtPos(m_pEquipButton);
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

    // Fling the back/quit button. Binary writes byte at entity+0x80
    // polymorphically: Fruit::m_bDetached or Bomb::m_bMovement. Falls
    // back to m_pEntity for bombs whose m_pFruitPiece was left null.
    if (m_pBuyButton && m_pBuyButton->m_pEntity) {
        Entity* e = m_pBuyButton->m_pEntity;
        float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        if (e->entityType == 0) {
            static_cast<Fruit*>(e)->m_bDetached = true;
        } else if (e->entityType == 1) {
            static_cast<Bomb*>(e)->m_bMovement = 1;
        }
        e->vel = Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
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
//
// After SetEquippedItem the binary's BuyButtonCallback path also writes
// a description string into the selected list item's m_DescText so the
// row visually updates ("EQUIPPED" / "FREE" / "BOUGHT"). String addrs
// in binary at 0x001bc104..0x001bc110.
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

            // Update the selected row's description text to reflect the
            // new equipped state. Binary uses three literal strings:
            //   0x001bc104 = "EQUIPPED"
            //   0x001bc10b = "FREE"
            //   0x001bc110 = "BOUGHT"
            // For a successful equip we always show "EQUIPPED".
            const char* newDesc = "EQUIPPED";
            strncpy(m_pSelectedItem->m_DescText, newDesc,
                    sizeof(m_pSelectedItem->m_DescText) - 1);
            m_pSelectedItem->m_DescText[sizeof(m_pSelectedItem->m_DescText) - 1] = '\0';

            // Port specific: the binary defers ItemSave.xml write until
            // GameTaskSaveOnExit / SaveCurrentData. The port's SDL exit
            // path does the same via GameDestroy, but a hard kill (Ctrl+C
            // / segfault) would lose the equip. Force-save here so the
            // equip persists immediately.
            im->SaveItemInfo();
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

    // Binary: SplatEntity::NumActiveSplats @ 0x0015e212 — only set flag when no splats
    if (SplatEntity::NumActiveSplats() == 0) {
        m_LayerFlagsAlt = 0x40;
    }

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
            // Binary: SplatEntity::RemoveAllSplats @ 0x0017eea4
            SplatEntity::RemoveAllSplats();

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
                // DIFFERS: binary uses *(GameTask + 0x17c); port uses back_icon.tex.
                m_pBuyButton->m_Texture = TexIdOf(s_TexBackIcon);
                m_pBuyButton->size      = TexSizeOf(s_TexBackIcon, 64.0f, 64.0f);
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
                            // DIFFERS: binary uses *(GameTask + slot+0x14); port
                            // uses select_item.tex (same slot the binary later
                            // assigns in SetSelected for the locked path).
                            m_pEquipButton->m_Texture = TexIdOf(s_TexSelectItem);
                            m_pEquipButton->size      = TexSizeOf(s_TexSelectItem, 64.0f, 64.0f);
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
            // Still fading: fling old back-button if present.
            if (m_pBuyButton && m_pBuyButton->m_pEntity) {
                Entity* e = m_pBuyButton->m_pEntity;
                float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
                float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
                if (e->entityType == 0) {
                    static_cast<Fruit*>(e)->m_bDetached = true;
                } else if (e->entityType == 1) {
                    static_cast<Bomb*>(e)->m_bMovement = 1;
                }
                e->vel = Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
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
            // DIFFERS: binary uses *(GameTask + 0x17c); port uses back_icon.tex.
            m_pBuyButton->m_Texture = TexIdOf(s_TexBackIcon);
            m_pBuyButton->size      = TexSizeOf(s_TexBackIcon, 64.0f, 64.0f);
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
        // Fling buy button (same as state 3)
        if (m_pBuyButton && m_pBuyButton->m_pEntity) {
            Entity* e = m_pBuyButton->m_pEntity;
            float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
            float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
            if (e->entityType == 0) {
                static_cast<Fruit*>(e)->m_bDetached = true;
            } else if (e->entityType == 1) {
                static_cast<Bomb*>(e)->m_bMovement = 1;
            }
            e->vel = Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
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
//
// Top-level control flow (binary-faithful):
//   if (m_LayerFlags == m_LayerFlagsAlt)  -> Block A (one-shot BG + dialog)
//   elif (m_AnimFrame > 0)                -> Block B (pulsing ring, every frame)
//   else                                  -> return
//
// NOTE: the binary does NOT use the standard (layerMask & m_LayerFlags) == 0
// early-return. The `layers` parameter is loaded but discarded. The port
// replicates the actual guard exactly.
// ---------------------------------------------------------------------------
void ShopScreen::Draw(const Vec3& /*hudScale*/, int /*layerMask*/) {
    // Static dial_alpha lives at static_block+0x84 in the binary (BSS).
    // Port uses a function-local static — same lifetime (process lifetime).
    static float s_DialAlpha = 0.0f;  // static_block+0x84

    // Static one-time cache for ring scale Vec3 (static_block+0x74 / +0x78 in binary).
    // binary uses __cxa_guard_acquire; port uses a bool + Vec3 static pair.
    static bool  s_RingVecInited = false;  // static_block+0x74
    static Vec3  s_RingVec(0.0f, 0.0f, 1.0f);  // static_block+0x78

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    Renderer* r = Renderer::GetInstance();
    if (!r) return;

    // All quads use white full-alpha colour (*(Colour**)(GOT+0x73a4) at runtime).
    // The binary reads a runtime GOT entry assumed to be {0xFF,0xFF,0xFF,0xFF}.
    const Colour colourWhite(255, 255, 255, 255);  // DAT_0015e08c = 0x000073a4 GOT entry

    // -----------------------------------------------------------------------
    // Guard: replicate the binary's structural flag check.
    // -----------------------------------------------------------------------
    if (m_LayerFlags == (uint32_t)m_LayerFlagsAlt) {
        // ===================================================================
        // Block A — one-shot BG + dialog box (0x0015ded8 .. 0x0015e1cd)
        // First action: mark Block A as done so next frame falls to Block B.
        // Binary: this->m_LayerFlags = 1  at 0x0015dee6
        // ===================================================================
        m_LayerFlags = 1;

        const float alpha = m_TransitionAlpha;

        // slide_X persists from A1 into A3 (or is set to 145.0f by A2).
        float slide_X;

        if (alpha < 1.0f) {
            // ---------------------------------------------------------------
            // Sub-Block A1 — Sliding BG, two quads  (0x0015def6..0x0015dff9)
            // ---------------------------------------------------------------

            // --- Left quad: anchored to scroll pos, U=[0.03125..0.597656] ---
            if (s_TexBGStore.IsValid()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, s_TexBGStore->m_TexId);
            }

            // Scale Vec3 = (291, 321, 0)  DAT_0015e064, DAT_0015e068, DAT_0015e05c
            Matrix44 matA1L = Matrix44::Scale44(291.0f, 321.0f, 0.0f);

            // Translate by (scroll_x, 0, 0) where scroll_x = m_pShopList->pos.x
            // m_pShopList + 0x8 = pos.x (ScrollingMenu inherits HUDControl3d whose
            // pos is the Vec3 starting at +0x04; +0x04 = x, +0x08 = y, +0x0c = z —
            // ambiguity resolved by spec note: field_0x8 = pos.x)
            float scroll_x = m_pShopList ? m_pShopList->pos.x : 0.0f;
            matA1L.GlobalTranslate44(scroll_x, 0.0f, 0.0f);

            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matA1L);
            mm.UploadModelViewOnly();

            {
                Colour c = colourWhite;
                // DrawQuadSized_GameTask(u0=0.03125f, u1=0.597656f, colour)
                // v0=0.1875f, v1=0.8125f hardcoded inside helper
                // DAT_0015e06c = 0.03125f, DAT_0015e070 = 0.597656f
                // Renderer::DrawQuad(tint, u0, v0, u1, v1)
                r->DrawQuad(c,
                    0.03125f, 0.1875f,   // u0, v0
                    0.597656f, 0.8125f); // u1, v1
            }

            // --- Right quad: slides from right  ---
            // slide_X = 145.0 + (1 - alpha) * 190.0 * 1.5
            // DAT_0015e054=145.0f, DAT_0015e058=190.0f, literal 1.5f
            slide_X = 145.0f + (1.0f - alpha) * 190.0f * 1.5f;  // DAT_0015e054 / DAT_0015e058

            // Scale Vec3 = (191, 321, 0)  DAT_0015e074, DAT_0015e068, DAT_0015e05c
            Matrix44 matA1R = Matrix44::Scale44(191.0f, 321.0f, 0.0f);
            matA1R.GlobalTranslate44(slide_X, 0.0f, 0.0f);

            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matA1R);
            mm.UploadModelViewOnly();

            {
                Colour c = colourWhite;
                // DrawQuadSized_GameTask(u0=0.597656f, u1=0.96875f, colour)
                // v0=0.1875f, v1=0.8125f; u1=0.96875f = 31/32 (literal 0x3f780000)
                // Renderer::DrawQuad(tint, u0, v0, u1, v1)
                r->DrawQuad(c,
                    0.597656f, 0.1875f,  // u0, v0
                    0.96875f, 0.8125f);  // u1, v1
            }

            if (s_TexBGStore.IsValid()) {
                glBindTexture(GL_TEXTURE_2D, 0);
            }

        } else {
            // ---------------------------------------------------------------
            // Sub-Block A2 — Static full BG, one quad  (0x0015dffe..0x0015e08f)
            // Pure scale, no translate — quad renders centered at origin.
            // ---------------------------------------------------------------

            // Scale Vec3 = (481, 321, 0)  DAT_0015e078, DAT_0015e068, DAT_0015e05c
            Matrix44 matA2 = Matrix44::Scale44(481.0f, 321.0f, 0.0f);
            // no GlobalTranslate44 — disasm confirms SetMatrix gets pure scale
            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matA2);
            mm.UploadModelViewOnly();

            if (s_TexBGStore.IsValid()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, s_TexBGStore->m_TexId);
            }

            {
                Colour c = colourWhite;
                // DrawQuadSized_GameTask(u0=0.03125f, u1=0.96875f, colour)
                // DAT_0015e06c=0.03125f; u1=0.96875f literal (0x3f780000)
                // Renderer::DrawQuad(tint, u0, v0, u1, v1)
                r->DrawQuad(c,
                    0.03125f, 0.1875f,  // u0, v0
                    0.96875f, 0.8125f); // u1, v1
            }

            if (s_TexBGStore.IsValid()) {
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            // Binary stores DAT_0015e1dc = 145.0f as slide_X for use in A3.
            // DAT_0015e1dc = 00 00 11 43 = 145.0f (separate read from DAT_0015e054)
            slide_X = 145.0f;  // DAT_0015e1dc
        }

        // -------------------------------------------------------------------
        // Sub-Block A3 — Dialog box  (0x0015e09e..0x0015e1cd)
        // Runs after BOTH A1 and A2. slide_X holds left-half resting X.
        // -------------------------------------------------------------------
        if (s_TexDialogBox.IsValid()) {
            // Get dialog box dimensions via vtable GetWidth/GetHeight.
            // Binary: *(int**)(static_block+0x34)->vtable[5]/[6]
            float texW = (float)(s_TexDialogBox->m_Width);
            float texH = (float)(s_TexDialogBox->m_Height);

            // Scale Vec3 = (texW+1, texH+1, 0) * 1.0f (identity multiply)
            // The decompile multiplies by local_44=1.0f via _Vector3::operator* — no-op.
            // DAT_0015e1e0 = 0.0f for z
            Matrix44 matA3 = Matrix44::Scale44(texW + 1.0f, texH + 1.0f, 0.0f);

            // Translate by (slide_X - 4.0, -3.0, 0.0)
            // 4.0f hardcoded (0x40800000), -3.0f hardcoded (0xc0400000)
            matA3.GlobalTranslate44(slide_X - 4.0f, -3.0f, 0.0f);

            mm.GetWorldStack().Reset();
            mm.GetWorldStack().SetCurrentMatrix(matA3);
            mm.UploadModelViewOnly();

            // --- Compute dial_alpha ---
            // dt = game.dt  (Game+0x38, DAT_0015e1f0=0x7990 GOT offset to Game*)
            const float dt = game.dt;

            bool is_locked = false;
            if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
                is_locked = (m_pSelectedItem->m_pItemInfo->IsLocked() != 0);
            }

            if (is_locked) {
                // Fade IN: dial_alpha += dt * 5.0f, clamp to 1.0
                s_DialAlpha += dt * 5.0f;
                if (s_DialAlpha > 1.0f) s_DialAlpha = 1.0f;  // literal 1.0f = 0x3f800000
            } else {
                // Fade OUT: dial_alpha += dt * (-5.0f), clamp to 0.0
                s_DialAlpha += dt * (-5.0f);
                if (s_DialAlpha < 0.0f) s_DialAlpha = 0.0f;  // DAT_0015e1e0 = 0.0f
            }

            // --- Compute grayscale ---
            // r_float = 255.0f + (-120.0f) * dial_alpha
            // DAT_0015e1e8 = 255.0f, DAT_0015e1e4 = -120.0f
            float r_float = 255.0f + (-120.0f) * s_DialAlpha;  // DAT_0015e1e8, DAT_0015e1e4
            uint8_t rByte = (r_float > 0.0f) ? (uint8_t)(int)r_float : (uint8_t)0;
            Colour colDialog(rByte, rByte, rByte, 0xFF);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, s_TexDialogBox->m_TexId);

            // DrawQuad_GameTask(colour) — full quad, no UV crop
            r->DrawQuad(colDialog);

            glBindTexture(GL_TEXTURE_2D, 0);
        }

        return;
    }

    // -----------------------------------------------------------------------
    // Block B — Animated selection ring  (0x0015dd78..0x0015ded4)
    // Trigger: m_LayerFlags != m_LayerFlagsAlt  AND  m_AnimFrame > 0
    // -----------------------------------------------------------------------
    if (m_AnimFrame <= 0) return;

    // --- Compute slide_X (same formula as A1 / identical to Update's list pos) ---
    float slide_X;
    {
        const float alpha = m_TransitionAlpha;
        if (alpha < 1.0f) {
            // DAT_0015e054=145.0f, DAT_0015e058=190.0f
            slide_X = 145.0f + (1.0f - alpha) * 190.0f * 1.5f;
        } else {
            slide_X = 145.0f;  // DAT_0015e054
        }
    }

    // --- One-time cache init for ring scale Vec3 ---
    // Binary: __cxa_guard_acquire(static_block+0x74), then init Vec3 at +0x78.
    // Port: plain bool guard (equivalent lifetime).
    if (!s_RingVecInited && s_TexSelected.IsValid()) {
        float w = (float)(s_TexSelected->m_Width);
        float h = (float)(s_TexSelected->m_Height);
        // z = 1.0f  (local_44 = 0x3f800000 from binary stack)
        s_RingVec = Vec3(w + 1.0f, h + 1.0f, 1.0f);
        s_RingVecInited = true;
    }

    // --- Apply sin pulse scale ---
    Vec3 scaleVec = s_RingVec;  // copy from cache (static_block+0x78)

    if (m_AnimFrame < 0x3ffc) {
        // sin_ratio = SinIdx((uint16_t)m_AnimFrame) / SinIdx(0x3ffc)
        // Math::SinIdx @ 0x000fc858; port uses SinIdx() from MathUtil.h
        float sinNum   = SinIdx((uint16_t)m_AnimFrame);
        float sinDenom = SinIdx((uint16_t)0x3ffc);
        float sin_ratio = (sinDenom != 0.0f) ? (sinNum / sinDenom) : sinNum;
        // _Vector3::operator*=(Vec3*, float) — scale Vec3 in-place
        scaleVec = scaleVec * sin_ratio;
    }

    // Build scale matrix then translate
    Matrix44 matB = Matrix44::Scale44(scaleVec.x, scaleVec.y, scaleVec.z);
    // Translate to (slide_X, 104.0, 0.0)  DAT_0015e060=104.0f, DAT_0015e05c=0.0f
    matB.GlobalTranslate44(slide_X, 104.0f, 0.0f);  // DAT_0015e060=104.0f

    mm.GetWorldStack().Reset();
    mm.GetWorldStack().SetCurrentMatrix(matB);
    mm.UploadModelViewOnly();

    // SmartPtr copy for ref-count safety (binary: SmartPtr ctor copy of +0x38 slot)
    // +0x38 in static block = s_TexSelected (selected.tex)
    if (s_TexSelected.IsValid()) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, s_TexSelected->m_TexId);
    }

    {
        Colour c = colourWhite;
        // DrawQuad_GameTask(colour) — full quad, default UVs
        r->DrawQuad(c);
    }

    if (s_TexSelected.IsValid()) {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}
