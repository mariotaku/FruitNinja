// ShopScreen — Sensei's Swag blade/background shop, launched from DojoScreen.
// Binary: ShopScreen(DojoScreen*) @ 0x001b3f94, Update @ 0x001b321c (387 lines),
//         DrawOrder @ 0x001b4e48, LoadContent @ 0x001b61c8.
//
// Analysed: 2026-04-28T14:00

#include "ShopScreen.h"
#include "DojoScreen.h"
#include "MainScreen.h"
#include "Game.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
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
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "math/Colour.h"
#include "math/MathUtil.h"
#include "debug/Logger.h"
#include "engine/util/StringTable.h"
#include <cstdlib>
#include "game/GameWork.h"

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

// SHOP_SHRINK_VEC -- Vec3 stored at .got + 0x77cc, initialised to
// (1,1,1) by _GLOBAL__I_ShopScreen.cpp @ 0x0015d7a0. Copied to the
// equip-button fruit's m_HalfB_vel by ShrinkBuyButton @ 0x0015c4cc.
static const Vec3 SHOP_SHRINK_VEC(1.0f, 1.0f, 1.0f);

// Note: the EquipCallback shrink branch uses Vec3::ZERO (not a "fling"
// vector). The earlier (0,1,0) interpretation came from misreading the
// initialiser in _GLOBAL__I_ShopScreen.cpp; the actual GOT-resolved
// pointer at GOT+0x73ec is a zero Vec3, not the (0,1,0) global.

// Fling velocity base (state 3 and QuitShopCallback)
static const float FLING_VEL_BASE = 5.0f;           // from decompile literal

struct SplatShiftCtx { float up; float down; };
static void SplatShiftVisitor(SplatEntity* s, void* user) {
    if (!s || !s->m_bAlive) return;
    if ((int8_t)s->m_SplatType < 0) return;
    SplatShiftCtx* c = static_cast<SplatShiftCtx*>(user);
    if (s->m_Pos.x > 50.0f) s->m_Pos.x += c->up;
    else                    s->m_Pos.x -= c->down;
}

// ---------------------------------------------------------------------------
// Static texture storage
// ---------------------------------------------------------------------------

Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexLocked;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexSelectItem;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexLoading;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexScratch;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexDialogBox;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexSelected;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexSelectedSml;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexLockedStroke;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexNewItemSmlBadge;
Mortar::SmartPtr<Mortar::Texture> ShopScreen::s_TexBGStore;
bool ShopScreen::s_bContentLoaded = false;

// Binary BSS global g_bShopButtonShrinking @.got+0x451b4: "the equip-button fruit piece is
// currently flying off-screen". Set by ShrinkBuyButton, read by Move/EquipCallback/DeletedMenuItem,
// cleared on completion. The binary uses a process-wide static (NOT a ShopScreen member), so model
// it as a TU-local static -- present in BOTH host and cross builds, no sizeof impact.
static bool g_bShopButtonShrinking = false;

// Binary g_ShopStaticBlock->m_SelCounter @.got+0x451b4+0x88: SetSelected rate-limiter --
// increments (mod 10) every frame; SetSelected fires only when ==0. Binary uses a
// process-wide static block field (NOT a ShopScreen member); model as a TU-local static.
static int g_ShopSelCounter = 0;

// Port-only helpers (mirror DojoScreen pattern).
static GLuint TexIdOf(const Mortar::SmartPtr<Mortar::Texture>& tex) {
    return tex.IsValid() ? tex->GetTexId() : 0;
}

// ---------------------------------------------------------------------------
// ShopScreen::LoadContent @ 0x001b61c8
// Loads 10 textures into static slots (binary-faithful: no guard).
// Binary pattern: LoadLocalisedTexture(name) -> store in static slot.
// Conditional at end: if LowResBackgrounds() load BG_store_sml.tex else BG_store.tex.
// ---------------------------------------------------------------------------
void ShopScreen::LoadContent() {
    // Binary @ 0x0015cb08 has NO singleton guard — loads unconditionally,
    // then sets s_isContentLoaded = 1 at the end.
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
}

// ---------------------------------------------------------------------------
// ShopScreen::ShopScreen(DojoScreen*) @ 0x001b3f94
// ---------------------------------------------------------------------------
ShopScreen::ShopScreen(DojoScreen* parent)
    : HUDControl3d()
    , m_TransitionAlpha(0.0f)
    // m_LayerFlagsAlt drives the panel's draw layer (copied into m_LayerFlags each
    // Update). Binary @ 0x0015cdac does not init +0x80; the first Update sets it
    // before the first Draw. We init to 0x80 (HUD_LAYER_POST_ACTOR) defensively --
    // matches the only pre-Update value reachable. Update demotes it to 0x40 only
    // while NumActiveSplats()==0 (binary @ 0x0015e216), so splats (alive only during
    // buy/transition) are never on-screen while the panel is at 0x40 -> never
    // overdrawn. This is the binary's data-state gate, not a fixed layer.
    , m_LayerFlagsAlt(Mortar::HUD_LAYER_POST_ACTOR)
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
    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // Binary: field_0x32 (m_bNoDestructor) = 0
    m_bNoDestructor = 0;

    // Binary: field_0x74 SmartPtr SetNull — port's HUDControl3d ctor already zeroes m_Texture.

    // Initialise slot items array (4 slots: SLASH_MODIFIER, BACKGROUND, UPSELL, REMOVEADS)
    m_pSlotItems[0] = nullptr;
    m_pSlotItems[1] = nullptr;
    m_pSlotItems[2] = nullptr;
    m_pSlotItems[3] = nullptr;  // +0xa8 -- REMOVEADS slot (defunct IAP); binary Init @ 0x001b42ac explicitly zeroes field_0xa8

    // Binary @0x001b3f94: m_ScrollOffset = (float)game_work.m_CoinsBalance + 0.5 (VectorSignedToFloat = vcvt.f32.s32)
    m_ScrollOffset = (float)game_work.m_CoinsBalance + 0.5f;

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
// HUDControl3d::Init sets m_Active = 1 typically. No symbol in binary
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
// Binary ShopScreen::Release @0x001b498c removes only m_pShopList synchronously
// and relies on the equip button self-removing via ShrinkBuyButton (shop state 1).
// The port's state machine does not reliably reach that self-removal before
// teardown, so m_pBuyButton/m_pEquipButton are removed synchronously here.
// This prevents the equip hit-region (pos 145,104) leaking onto MainScreen.
// Re-entrancy: Release() is called from ~ShopScreen() inside HUD::Update's
// deletion block; std::list::remove() on a different element does not
// invalidate the iterator currently held by HUD::Update, so this is safe.
// DIFFERS: binary v1.6.1 ShopScreen::Release @0x001b498c removes only m_pShopList
//          and relies on the equip button self-removing in shop state 1;
//          port removes m_pBuyButton/m_pEquipButton synchronously here because
//          the port state machine does not reliably reach state-1 self-removal
//          before teardown -- prevents the equip hit-region (pos 145,104)
//          leaking onto MainScreen.
// ---------------------------------------------------------------------------
void ShopScreen::Release() {
    // Snapshot before RemoveControl fires DeletedMenuItem (which nulls the ptrs).
    MenuButton* buy = m_pBuyButton;
    MenuButton* eq  = m_pEquipButton;

    if (buy && game_work.mHud) {
        game_work.mHud->RemoveControl(buy);  // fires DeletedMenuItem -> nulls m_pBuyButton
        if (!buy->m_bNoDestructor) delete buy;
    }
    if (eq && game_work.mHud) {
        game_work.mHud->RemoveControl(eq);   // fires DeletedMenuItem -> nulls m_pEquipButton
        if (!eq->m_bNoDestructor) delete eq;
    }

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

    // Binary ShopScreen::Init (0x0015f7ac) makes THREE setter calls in order:
    //   @ 0x0015f7fc: vtable[+0x50](290.0f) -- SetWidth(290)     writes +0xa4 (m_ItemHeight) + 4 derived region fields
    //   @ 0x0015f810: vtable[+0x4c](80.0f)  -- SetHeight(80)     writes +0xa0 (m_Height) = scroll-boundary field
    //   @ 0x0015f828: vtable[+0x54](80.0f)  -- SetItemHeight(80) writes +0x9c (m_Width)
    // Port field names (m_Width/m_Height/m_ItemHeight) are name-swapped vs binary semantics
    // -- preserved to avoid mangled-symbol drift.
    // ASM-verified: 2026-06-06 v1.6.1 binary @ 0x0015f7fc (asm-inspector) -- SetWidth=vtable+0x50->+0xa4, SetHeight=vtable+0x4c->+0xa0, SetItemHeight=vtable+0x54->+0x9c. Shop: SetWidth(290)/SetHeight(80)/SetItemHeight(80).
    m_pShopList->SetWidth(290.0f);
    m_pShopList->SetHeight(80.0f);
    m_pShopList->SetItemHeight(80.0f);

    // Port-only: binary has no ScrollingMenu object (ShopScreen::Draw @ 0x0015dd50
    // draws the list inline in its own pass). The port models m_pShopList as a real
    // HUD control, so it needs a layer that tracks the panel. Use 0x80
    // (HUD_LAYER_POST_ACTOR) so the list draws AFTER the splat pass like the panel
    // does while splats are alive. (The panel itself oscillates 0x40<->0x80 via
    // Update; the list following 0x80 is the safe match -- splats only coincide with
    // the panel's 0x80 frames anyway. // Port specific.)
    m_pShopList->m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;

    // Populate from ItemManager
    // Binary (ShopScreen::Init @ 0x0015f7ac): for each ItemInfo from GetFirst/GetNext:
    //   operator_new(0x284) -> ShopListItem::ShopListItem() -> ShopListItem::Create(item, screen)
    //   -> ScrollingMenu::AddItem()
    // ShopListItem::Create sets m_ParamWidth (+0x24) = 80.0f (DAT_0015cae8),
    // which is what GetHeight() returns, giving each row a pitch of 80 units.
    // TODO: Init zebra-stripe m_Colour.b toggle -- see tmp/re-shopscreen.md Init body
    // Binary Init @ 0x0015f820: bVar11 starts = 1, toggles (^= 1) per row.
    // Written to ShopListItem::m_Colour.b. Exact byte position within the
    // packed unsigned int m_Colour (ScrollingMenuItem +0x14) not confirmed
    // from ShopListItem RE -- dispatch re-analyst on ShopListItem::Create to verify.

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

    if (game_work.mHud) {
        game_work.mHud->AddControl(m_pShopList);
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::RemoveBuyButton
// Binary: Release() calls SetPendingRemoval on each button. HUD's pending-
// removal mechanism then calls m_RemoveCallback (DeletedMenuItem), which
// nulls the pointer. We do NOT null the pointer here so DeletedMenuItem's
// pointer-compare still works.
// ---------------------------------------------------------------------------
void ShopScreen::RemoveBuyButton() {
    if (m_pBuyButton) {
        m_pBuyButton->SetPendingRemoval();
        // Do NOT null m_pBuyButton here — DeletedMenuItem will null it
        // when HUD fires the m_RemoveCallback.
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::RemoveEquipButton
// ---------------------------------------------------------------------------
void ShopScreen::RemoveEquipButton() {
    if (m_pEquipButton) {
        m_pEquipButton->SetPendingRemoval();
        // Do NOT null m_pEquipButton here — DeletedMenuItem will null it
        // when HUD fires the m_RemoveCallback.
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
float ShopScreen::GetDescriptionTextXPos() {
    // Slide formula matches Block A/B: 145.0 + (1 - alpha) * 190.0 * 1.5
    // then subtract 80.0f for the text indent inside the dialog box.
    float slide_X = 145.0f + (1.0f - m_TransitionAlpha) * 190.0f * 1.5f;
    return slide_X - 80.0f;
}

// ---------------------------------------------------------------------------
// ShopScreen::ShrinkBuyButton @ 0x001b17b4 (v1.6.1)
//
// ASM-verified binary writes (after null/Sliced early-outs):
//   fruit->m_bSliced = 1;               // +0xb8
//   g_bShopButtonShrinking = 1;         // BSS bool (file-static g_bShopButtonShrinking)
//   m_pEquipButton->m_bClearsMenuItems = 0;  // +0x13a  (prevents ClearMenuItems on retract)
//   fruit->m_SecondVel = SHOP_SHRINK_VEC;    // +0xd4 = (1,1,1)
//
// The m_bClearsMenuItems=0 write is the bomb-drop fix: MenuButton::Update's
// slice gate fires m_ClickCallback AND calls ClearMenuItems when
// m_bClearsMenuItems!=0.  Zeroing it here suppresses the bomb-clearing
// cascade while still letting the click callback fire on retract.
// Binary does NOT write vel, m_Gravity, scale, or m_bEnabled here.
// ---------------------------------------------------------------------------
void ShopScreen::ShrinkBuyButton() {
    if (!m_pEquipButton) return;
    Fruit* fruit = m_pEquipButton->m_pTrackedFruit;
    if (!fruit) return;
    if (fruit->Sliced()) return;       // already retracting -- noop

    #ifndef __bada__
    LOG_INFO("FRUIT", "m_bSliced=1 set on entity=%p pos=(%.1f,%.1f) type=%d (in ShrinkBuyButton)",
             static_cast<void*>(fruit), fruit->pos.x, fruit->pos.y, (int)fruit->m_FruitType);
    #endif
    fruit->m_bSliced                      = true;  // *(fruit+0xb8) = 1
    g_bShopButtonShrinking                = true;
    m_pEquipButton->m_bClearsMenuItems    = 0;     // *(button+0x13a) = 0 (suppresses ClearMenuItems)
    fruit->m_SecondVel                    = SHOP_SHRINK_VEC;  // *(fruit+0xd4..+0xdf) = (1,1,1)
}

// ---------------------------------------------------------------------------
// ShopScreen::DeletedMenuItem(HUDControl*) @ 0x0015d14c
//
// Registered as m_RemoveCallback on BOTH m_pBuyButton and m_pEquipButton
// immediately after HUD::AddControl. Fires when HUD removes a button
// from its control list (after m_bPendingRemoval propagates through
// MenuButton::Update's FadeCounter-to-zero path).
//
// Binary pseudocode (re-RE'd 2026-05-09):
//   if (param_1 == m_pEquipButton) {
//       if (g_bShopButtonShrinking != 0) {
//           fruit = param_1->m_pTrackedFruit
//           if (fruit) {
//               fruit->m_SecondPos.y = -480.0   // DAT_0015d1e8 = 0xC3F00000
//               fruit->pos.y         = -480.0   // DAT_0015d1e8
//               fruit->m_Gravity     = -g_ShopFlingVec = (0,-1,0)  // +0xa0, NOT m_SecondVel
//               fruit->m_SecondVel.y = -10.0    // DAT = 0xC1200000 (overlapping +0xc8)
//               fruit->vel.y         = -10.0
//           }
//       }
//       m_pEquipButton = null
//       m_BuyDelay += 0.05f   // DAT_0015d1ec = 0x3D4CCCCD
//   }
//   if (param_1 == m_pBuyButton) {
//       m_pBuyButton = null
//   }
// ---------------------------------------------------------------------------
void ShopScreen::DeletedMenuItem(HUDControl* removed) {
    if (removed == m_pEquipButton) {
        if (g_bShopButtonShrinking) {
            // Kick the fruit off-screen when the button was shrunk programmatically.
            // Binary @ 0x0015d14c writes (re-RE'd 2026-05-09 by re-analyst):
            //   *(fruit+0xbc) = -480.0   -> m_SecondPos.y
            //   *(fruit+0x14) = -480.0   -> entity pos.y
            //   *(fruit+0xa0) = -g_ShopFlingVec = (0,-1,0) -> m_Gravity (NEGATE, not zero)
            //   *(fruit+0xc8) = -10.0    -> m_SecondVel.y
            //   *(fruit+0x20) = -10.0    -> vel.y
            // The earlier port skipped the m_Gravity write claiming it overlapped
            // m_SecondVel (it does NOT — m_Gravity is +0xa0, m_SecondVel is +0xd4).
            // Without restoring downward gravity here, EquipCallback's prior
            // m_Gravity=(0,0,0) leaves Fruit::CheckHasGoneOffscreen unable to fire
            // — every return-true branch in that function is gated on a non-zero
            // gravity component (verified @ binary 0x00175218). The orphan watermelon
            // then falls forever, accumulating in ActorManager and soft-locking
            // MainScreen::STATE_DOJO_WAIT_B. Negating g_ShopFlingVec=(0,1,0) gives
            // m_Gravity=(0,-1,0) so the downward branch eventually returns true and
            // KillFruit reaps the fruit (Fruit::KillFruit sets flags|=0x10, which
            // ActorManager::Update polls per-tick).
            Fruit* fruit = m_pEquipButton->m_pTrackedFruit;
            if (fruit) {
                fruit->m_SecondPos.y = -480.0f;
                fruit->pos.y         = -480.0f;
                // SHOP_FLING_VEC = (0, 1, 0); negated = (0, -1, 0)
                fruit->m_Gravity     = Vec3(0.0f, -1.0f, 0.0f);
                fruit->m_SecondVel.y = -10.0f;
                fruit->vel.y         = -10.0f;
            }
        }
        // Always null the pointer and add delay (binary: unconditional)
        m_pEquipButton = nullptr;
        m_BuyDelay += 0.05f;   // DAT_0015d1ec = 0x3D4CCCCD = 0.05f
    }

    if (removed == m_pBuyButton) {
        m_pBuyButton = nullptr;
        // No delay added for buy button removal (binary confirms)
    }
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

    // ASM-spec v1.6.1 ShopScreen::SetSelected @0x001b24f0 (DAT_0028350f="pineapple", DAT_00283509="black_pineapple")
    // Binary lazily inits three locals via __cxa_guard_acquire; port calls FruitType each time
    // (idempotent -- pure string lookup, safe to call repeatedly).
    // DAT_0028350f is a pointer into the middle of "black_pineapple\0" at offset +6 = "pineapple\0".
    // DAT_00283519 = "starfruit" is initialised but never read in the dispatch (dead init).
    ItemInfo* info = item->m_pItemInfo;
    const int type_unlocked = Fruit::FruitType("pineapple",       false);  // DAT_0028350f
    const int type_locked   = Fruit::FruitType("black_pineapple", false);  // DAT_00283509
    Fruit* equipFruit = m_pEquipButton->m_pTrackedFruit;
    if (info->IsLocked() == 0) {
        // ASM-spec v1.6.1 ShopScreen::SetSelected @0x001b24f0: unlocked branch
        // Binary: SmartPtr::operator= on (m_pEquipButton+0x74) <- m_RingTex[1] (blue_ring.tex)
        //         Fruit::SetFruitType(fruit, type_unlocked, 1.0f) @ 0x001dc054
        m_pEquipButton->m_Texture = game_work.m_RingTex[1];
        m_pEquipButton->SetText(GETSTRING_CAST_0((LocalizedString)0xed),
            game_work.m_RingColours[4], game_work.m_RingColours[5],
            39.0f, 12.0f, true, true);
        if (equipFruit) {
            equipFruit->SetFruitType(type_unlocked, 1.0f);
        }
    } else {
        // ASM-spec v1.6.1 ShopScreen::SetSelected @0x001b24f0: locked branch
        // Binary: SmartPtr::operator= on (m_pEquipButton+0x74) <- m_RingTex[10] (locked_ring.tex)
        //         Fruit::SetFruitType(fruit, type_locked, 1.0f) @ 0x001dc054
        m_pEquipButton->m_Texture = game_work.m_RingTex[10];
        m_pEquipButton->SetText(GETSTRING_CAST_0((LocalizedString)0x3c7),
            game_work.m_RingColours[12], game_work.m_Colour69C,
            39.0f, 12.0f, true, true);
        if (equipFruit) {
            equipFruit->SetFruitType(type_locked, 1.0f);
        }
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
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("equip-locked", 1.0f, 1.0f);
        }
        item->m_LockFlashAlpha = 0.25f;   // 0x3e800000 in binary; offset +0x264
    } else {
        if (m_pEquipButton) {
            // Matches ShopScreen::ClickedOnShopItem @ 0x0015d4e4
            if (game_work.m_TutorialControl)
                game_work.m_TutorialControl->ButtonPressedAtPos(m_pEquipButton);
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
    // DIFFERS (intentional bug fix): the binary v1.6.1 ShopScreen::QuitShopCallback @0x001b2ef0 does NOT call
    // ShrinkBuyButton on shop-quit, and `~MenuButton` / `MenuButton::Release`
    // neither KillFruit nor remove the entity from ActorManager. So if the
    // user taps Quit during the 0.25s post-equip m_BuyDelay window (set by
    // EquipCallback's user-path), the equip-button's watermelon fruit leaks
    // into ActorManager forever. ASM-inspector verified the binary has the
    // same bug -- it just goes unnoticed on Bada due to timing/QA. The
    // fruit-count==0 gate in MainScreen::STATE_DOJO_WAIT_B then never opens
    // on the next dojo entry, soft-locking re-entry. Force the gate clear
    // + call ShrinkBuyButton directly here; ShrinkBuyButton's internal
    // Sliced() guard makes it idempotent.
    m_BuyDelay = 0.0f;
    ShrinkBuyButton();

    // Binary: GameSound::SFXPlay(gameSound, "menu-bomb", 1.0, 1.0)
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    }

    // Set state to transition-out (state 2)
    m_State = 2;

    // Fling the back/quit button. Binary @ 0x0015d55c indirects through
    // m_pBuyButton->m_pTrackedFruit (+0x14C) and writes *(byte*)(piece+0x80)=1
    // (Fruit+0x80 unconfirmed, no reader). Port omits the write.
    if (m_pBuyButton && m_pBuyButton->m_pTrackedFruit) {
        Fruit* piece = m_pBuyButton->m_pTrackedFruit;
        float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
        piece->vel = Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
    }

    // Binary: TutorialControl::ResetTutePos(tute, 0) — null MenuButton* hides arrow
    if (game_work.m_TutorialControl) {
        game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::EquipCallback @ 0x0015d630
//
// Binary gate at 0x0015d63c: reads g_bShopButtonShrinking (GOT+0x451b4).
//   if != 0 (programmatic shrink path):
//     copy equip-button fruit's current pos to m_HalfB_pos (fruit+0xb8)
//     set vel, m_SecondVel, and m_HalfB_vel all from g_ShopFlingVec (0,1,0)
//     return WITHOUT equipping
//   if == 0 (user-sliced path):
//     m_BuyDelay = 0.25f (0x3e800000)
//     ItemManager::SetEquippedItem; play equip SFX
// ---------------------------------------------------------------------------
void ShopScreen::EquipCallback() {
    LOG_DEBUG("Shop", "EquipCallback fired: m_pEquipButton=%p m_pSelectedItem=%p info=%p",
              (void*)m_pEquipButton, (void*)m_pSelectedItem,
              m_pSelectedItem ? (void*)m_pSelectedItem->m_pItemInfo : nullptr);
    if (!m_pEquipButton) return;

    // Binary: if (g_bShopButtonShrinking != 0): programmatic path
    if (g_bShopButtonShrinking) {
        // Programmatic-shrink path (EquipCallback @ 0x0015d649):
        // Copy current entity pos to m_HalfB_pos, then set all three
        // velocity fields from g_ShopFlingVec (SHOP_FLING_VEC = (0,1,0)).
        Fruit* fruit = m_pEquipButton->m_pTrackedFruit;
        if (fruit) {
            // Binary EquipCallback shrink branch (0x0015d734..0x0015d76c).
            // The Vec3 source at GOT+0x73ec is the global zero vector
            // (BSS-initialised). The four writes are:
            //   fruit->m_HalfB_pos (+0xb8) = fruit->pos    -- snapshot pos
            //   fruit->vel         (+0x1c) = (0, 0, 0)
            //   fruit->m_HalfB_vel (+0xc4) = (0, 0, 0)
            //   fruit->m_Gravity   (+0xa0) = (0, 0, 0)
            // After this, the fruit is completely frozen (vel=0, gravity=0).
            // MenuButton::Update's "if (vel.x==0 && vel.y==0)" gate (in the
            // m_pEntity==null path) then runs the pin+scale path that
            // shrinks the fruit alongside the ring rather than letting
            // physics carry it off-screen.
            fruit->m_SecondPos = fruit->pos;
            fruit->vel         = Vec3(0.0f, 0.0f, 0.0f);
            fruit->m_SecondVel = Vec3(0.0f, 0.0f, 0.0f);
            fruit->m_Gravity   = Vec3(0.0f, 0.0f, 0.0f);
        }
        // Does NOT call ItemManager::SetEquippedItem — equip does not happen
        return;
    }

    // User-sliced path (EquipCallback @ 0x0015d63c, else branch):
    // m_BuyDelay = 0x3e800000 = 0.25f before equip action
    m_BuyDelay = 0.25f;   // DAT = 0x3e800000

    if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
        ItemInfo* info = m_pSelectedItem->m_pItemInfo;
        ItemManager* im = ItemManager::GetInstance();
        #ifndef __bada__
        LOG_DEBUG("Shop", "EquipCallback user-path: type=%d name='%s' im=%p",
                  (int)info->m_Type, info->m_pName ? info->m_pName : "(null)", (void*)im);
        #endif
        if (im) {
            im->SetEquippedItem((int)info->m_Type, info);
            #ifndef __bada__
            LOG_DEBUG("Shop", "EquipCallback after SetEquippedItem: m_DefaultItems[%d]=%p (=info?%d)",
                      (int)info->m_Type, (void*)im->GetEquipped((int)info->m_Type),
                      im->GetEquipped((int)info->m_Type) == info ? 1 : 0);
            #endif

            // Binary @ 0x0015d630 EquipCallback does NOT touch m_DescText.
            // The "currently equipped" visual is the m_SelectedAlpha highlight
            // ring driven per-frame by ShopListItem::Move @ 0x0015d1fc polling
            // ItemManager::IsEquipped(m_pItemInfo); description text is set
            // ONCE in ShopListItem::Create @ 0x0015c988 and never rewritten by
            // an equip action. The earlier port-side strncpy("EQUIPPED", ...)
            // here was a fabrication: it permanently overwrote the row's
            // description and was never restored, leaving the previously-
            // equipped row showing stale "EQUIPPED" forever.

            // Port specific: the binary defers ItemSave.xml write until
            // GameTaskSaveOnExit / SaveCurrentData. The port's SDL exit
            // path does the same via GameDestroy, but a hard kill (Ctrl+C
            // / segfault) would lose the equip. Force-save here so the
            // equip persists immediately.
            im->SaveItemInfo();
            LOG_DEBUG("Shop", "EquipCallback SaveItemInfo done");
        }
        // Binary: SFX depends on item type:
        //   type == 0 (blade):      SFXPlay("equip-new-sword")
        //   type == 1 (background): SFXPlay("equip-new-wallpaper")
        if (game_work.mGameSound) {
            const char* sfxName = (info->m_Type == ITEM_TYPE_BLADE)
                                  ? "equip-new-sword"
                                  : "equip-new-wallpaper";
            game_work.mGameSound->SFXPlay(sfxName, 1.0f, 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::Update(float) @ 0x001b321c (387 lines)
// ---------------------------------------------------------------------------
void ShopScreen::Update(float dt) {
    float prevAlpha = m_TransitionAlpha;

    // Binary @ 0x0015e216: demote the panel to 0x40 ONLY when no splats are alive.
    // 0x40 draws before SplatEntity::DrawActiveSplats, but splats only exist during
    // the buy/transition states (never plain browsing), so the 0x40 frames never
    // coincide with a live splat -> no overdraw. While any splat is alive Alt keeps
    // its 0x80 value (ctor/post-buy) and the panel draws in HUD::Draw(0x80), AFTER
    // the splat pass. Data-state gate -- do NOT pin 0x80 unconditionally.
    if (SplatEntity::NumActiveSplats() == 0) {
        m_LayerFlagsAlt = Mortar::HUD_LAYER_MENU_BG;
    }

    // Binary pre-amble (0x0015e212..0x0015e244):
    // 1. If m_pShopList && GetItemClosestToZero() != m_pSelectedItem (pointer compare)
    //    && g_ShopSelCounter == 0: call SetSelected (rate-limited every 10 frames).
    // 2. Increment g_ShopSelCounter unconditionally: (g_ShopSelCounter+1) % 10.
    // Binary: __aeabi_idivmod(counter+1, 10) unconditionally, gate on counter==0.
    if (m_pShopList) {
        ShopListItem* closest = static_cast<ShopListItem*>(m_pShopList->GetItemClosestToZero());
        if (closest != m_pSelectedItem && g_ShopSelCounter == 0) {
            SetSelected(closest);
        }
    }
    // Increment unconditionally (binary: (g_ShopSelCounter+1) % 10 every frame)
    g_ShopSelCounter = (g_ShopSelCounter + 1) % 10;

    // Binary @ 0x001b321c ShopScreen::Update: *(this+0x34) = *(this+0x80) | 1
    // The |1 ensures m_LayerFlags always has the DEFAULT pass bit set so the ring
    // (Block B, which uses `layerMask != m_LayerFlagsAlt`) can also run in the
    // default pass. Without |1 the ring is invisible when m_LayerFlagsAlt==0x80
    // and the HUD dispatches a separate 0x01 call.
    // ASM-spec v1.6.1 ShopScreen::Update @0x001b321c: +0x34 = m_LayerFlagsAlt | 1
    m_LayerFlags = (uint32_t)m_LayerFlagsAlt | 1u;

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
            // Texture comes from *(GameTask + 0x17c) — a per-task Mortar::SmartPtr<Texture>.
            // Fruit type: *(GameTask + GOT_DAT_0015e578) — int pre-stored in task.
            // Port uses bomb fruit type (FruitInfo_GetCount()) matching the
            // DojoScreen back-button pattern: out-of-range index forces a bomb.
            // Binary: after AddControl, scales m_TargetSize and fruit piece by
            // DAT_0015e920 = 0.825f.
            if (!m_pBuyButton) {
                const int backFruitType = FruitInfo_GetCount();  // forces bomb spawn
                m_pBuyButton = new MenuButton();
                // ASM-spec v1.6.1 ShopScreen::Update @0x001b321c: BACK ring uses m_RingTex[16] (red_ring.tex).
                m_pBuyButton->m_Texture = game_work.m_RingTex[16];
                // Binary: spawn/rest pos = Vec3::Zero; m_HudScale (below) anchors the
                // button to lower-right. Port previously passed POS_BACK_BUTTON here AND
                // set m_HudScale -> double offset -> off-screen.
                m_pBuyButton->Init(Vec3(0.0f, 0.0f, 0.0f),
                    Mortar::Delegate0<void>::Make(this, &ShopScreen::QuitShopCallback),
                    backFruitType, Vec3(0.0f, 0.0f, 0.0f), nullptr);
                // Binary @ 0x0015e3c6: m_bRespondsToBackKey = 1.
                m_pBuyButton->m_bRespondsToBackKey = 1;
                m_pBuyButton->m_bBackdropActive = 1; // v1.6.1 ShopScreen::Update @0x001b3570
                if (game_work.mHud) game_work.mHud->AddControl(m_pBuyButton, false);
                // Binary (0x0015e3e2..0x0015e3f0): register DeletedMenuItem as m_RemoveCallback
                m_pBuyButton->m_RemoveCallback =
                    Mortar::Delegate1<void, HUDControl*>::Make(this, &ShopScreen::DeletedMenuItem);
                if (game_work.m_TutorialControl) game_work.m_TutorialControl->ResetTutePos(m_pBuyButton);
                // Binary: m_TargetSize *= 0.825; fruit piece scale *= 0.825
                m_pBuyButton->m_RestScale = m_pBuyButton->m_RestScale * BUTTON_SCALE;
                if (m_pBuyButton->m_pTrackedFruit) {
                    m_pBuyButton->m_pTrackedFruit->scale =
                        m_pBuyButton->m_pTrackedFruit->scale * BUTTON_SCALE;
                }
                // ASM-spec v1.6.1 ShopScreen::Update @0x001b321c: BACK ring SetText(GETSTRING 0x352)
                m_pBuyButton->SetText(
                    GETSTRING_CAST_0((LocalizedString)0x352),
                    game_work.m_RingColours[0],
                    game_work.m_RingColours[1],
                    30.525f, 9.9f, true, true);
                // ASM-spec v1.6.1 ShopScreen::Update @0x001b321c (case 0, after SetText):
                //   m_HudScale.x=0.5*0.75=0.375, m_HudScale.y=-0.5*0.7=-0.35 on m_pBuyButton.
                //   m_HudScale is a POSITION anchor (not render scale): display pos =
                //   pos + m_HudScale*(480,320,0). With pos=Zero -> (180,-112) lower-right.
                //   HLE-confirmed: m_RestScale stays full-size while m_HudScale=(0.375,-0.35).
                m_pBuyButton->m_HudScale.x = 0.5f * 0.75f;  // +0x14 = 0.375f
                m_pBuyButton->m_HudScale.y = -0.5f * 0.7f;  // +0x18 = -0.35f
            }
        }
        break;
    }

    // ---- STATE 1: Active / idle ----
    case 1: {
        // ASM-verified: 2026-05-09 v1.6.1 binary @ 0x0015e208 (re-analyst).
        // Selection-ring counter (m_AnimFrame at +0xb4) defaults to DECAY
        // each frame: ringSignedDt = -dt. Only ONE control path keeps it
        // at +dt: when the selected item IS EQUIPPED. The ring marks the
        // user's currently-equipped item; the watermelon equip-button is
        // a sibling branch that only exists on UN-equipped items, so the
        // two paths are mutually exclusive.
        // Earlier port version had the gate inverted (ramp-up on
        // un-equipped) which made the ring follow the equip-button
        // instead of the equipped loadout.
        float ringSignedDt = -dt;

        // Binary (0x0015e3a0): m_BuyDelay only decremented when > 0;
        // the else branch (shrink/create) only runs when already <= 0.
        if (m_BuyDelay > 0.0f) {
            m_BuyDelay -= dt;   // decrements toward zero
        } else {
            // Binary (0x0015e438..0x0015e442): only gate is m_bTouchProcessed
            // (m_pShopList is always non-null after Init).
            if (!m_pShopList->m_bTouchProcessed) {
                // List is settled (not being tapped): shrink/retract the equip button.
                // Fires every frame; ShrinkBuyButton's Fruit::Sliced() guard makes it safe.
                ShrinkBuyButton();  // binary @ 0x0015e442 beq to shrink
            } else {
                // One-frame tap-release event: check equipped/locked, maybe create equip button.
                // Binary (0x0015e480..0x0015e5be):

                // Check if item is equipped/locked — hide tutorial arrow if so
                if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
                    ItemManager* im = ItemManager::GetInstance();
                    int equipped = im->IsEquipped(m_pSelectedItem->m_pItemInfo);
                    bool locked  = m_pSelectedItem->m_pItemInfo->IsLocked() != 0;
                    if (equipped || locked) {
                        // Binary: TutorialControl::ResetTutePos(tute, null) — hide arrow
                        if (game_work.m_TutorialControl)
                            game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
                    }
                    // Selection-ring ramp-up gate: binary keeps ringSignedDt = +dt
                    // only when IsEquipped != 0 (current loadout).
                    if (equipped != 0) {
                        ringSignedDt = dt;
                    }
                    // Create equip button if item is not equipped and button doesn't exist.
                    // Binary: guarded by (m_pEquipButton == null) — single-shot creation.
                    if (equipped == 0 && !m_pEquipButton) {
                        // Binary: Fruit::FruitType(DAT_0015e58c, false) -> "watermelon"
                        const int equipFruitType =
                            Fruit::FruitType("watermelon", false);  // DAT_0015e58c -> 0x001bb539
                        m_pEquipButton = new MenuButton();
                        // Creation default: locked_ring.tex (immediately overridden by SetSelected below).
                        // ASM-spec v1.6.1 ShopScreen::Update @0x001b321c: binary writes m_RingTex[10] at creation.
                        m_pEquipButton->m_Texture = game_work.m_RingTex[10];
                        m_pEquipButton->Init(POS_EQUIP_BUTTON,
                            Mortar::Delegate0<void>::Make(this, &ShopScreen::EquipCallback),
                            equipFruitType, Vec3(0.0f, 0.0f, 0.0f), nullptr);
                        // Binary (0x0015e5f6): disables touch on equip button at creation
                        m_pEquipButton->m_bAcceptsTouch = 0;
                        // ASM-spec v1.6.1 ShopScreen::Update @0x001b321c: EQUIP ring SetText(GETSTRING 0x3c7)
                        // Binary order: SetText -> m_bClearsMenuItems=0 -> SetSelected -> AddControl.
                        m_pEquipButton->SetText(
                            GETSTRING_CAST_0((LocalizedString)0x3c7),
                            game_work.m_RingColours[12],
                            game_work.m_Colour69C,  // +0x69c
                            39.0f, 12.0f, true, true);
                        // Binary @0x001b321c (ShopScreen::Update state-1 equip build): the equip
                        // button is born with m_bClearsMenuItems=0 (MenuButton +0x13a) so slicing
                        // it to EQUIP does NOT cascade ClearMenuItems() and destroy the bomb/back
                        // button. MenuButton::Init sets it to 1 for every button; the binary clears
                        // it here at creation, right after Init and before SetSelected. The port
                        // previously relied only on ShrinkBuyButton's later write, which is skipped
                        // if the user slices the equip button before the list settles.
                        m_pEquipButton->m_bClearsMenuItems = 0;
                        // Binary (0x0015e5fa): SetSelected(m_pSelectedItem) — update fruit type
                        SetSelected(m_pSelectedItem);
                        if (game_work.mHud) game_work.mHud->AddControl(m_pEquipButton, false);
                        // Binary (0x0015e60e): register DeletedMenuItem as m_RemoveCallback
                        m_pEquipButton->m_RemoveCallback =
                            Mortar::Delegate1<void, HUDControl*>::Make(this, &ShopScreen::DeletedMenuItem);
                        if (game_work.m_TutorialControl)
                            game_work.m_TutorialControl->ResetTutePos(m_pEquipButton);
                        // Binary (0x0015e60a): g_bShopButtonShrinking = 0 (clear flag)
                        g_bShopButtonShrinking = false;
                        // Binary: m_TargetSize *= 0.75; fruit piece scale *= 0.75
                        m_pEquipButton->m_RestScale =
                            m_pEquipButton->m_RestScale * EQUIP_BUTTON_SCALE;
                        m_pEquipButton->m_pTrackedFruit->scale =
                            m_pEquipButton->m_pTrackedFruit->scale * EQUIP_BUTTON_SCALE;
                        // Binary (0x0015e622): Fruit::RotateFacingUp(fruit, false, (0,1,0))
                        m_pEquipButton->m_pTrackedFruit->RotateFacingUp(false, Vec3(0.0f, 1.0f, 0.0f));
                    }
                }
            }
            // LAB_0015e68a: update animation frame counter (runs regardless of above)
        }

        // Update selection-ring animation counter.
        // Binary: fVar = (float)m_AnimFrame + signedDt * DAT_0015e904
        //         clamp to [0, 0x3ffc]
        // signedDt defaults to -dt (decay); flips to +dt only on the
        // "selected unlocked non-equipped item" branch above.
        float fFrame = (float)m_AnimFrame + ringSignedDt * ANIM_FRAME_RATE;
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
            if (game_work.mMainScreen) {
                game_work.mMainScreen->SetState(STATE_SLIDE_IN);
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
            // Binary: m_pBuyButton->m_pTrackedFruit (+0x14C); *(byte*)(piece+0x80)=1
            // (Fruit+0x80 unconfirmed, no reader). Port omits the write.
            if (m_pBuyButton && m_pBuyButton->m_pTrackedFruit) {
                Fruit* piece = m_pBuyButton->m_pTrackedFruit;
                float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
                float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
                piece->vel = Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
                // Binary: TutorialControl::ResetTutePos(tute, 0)
                if (game_work.m_TutorialControl)
                    game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
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
            // ASM-spec v1.6.1 ShopScreen::Update @0x001b321c: BACK ring uses m_RingTex[16] (red_ring.tex).
            m_pBuyButton->m_Texture = game_work.m_RingTex[16];
            m_pBuyButton->Init(POS_BACK_BUTTON_NEW,
                Mortar::Delegate0<void>::Make(this, &ShopScreen::QuitShopCallback),
                backFruitType, Vec3(0.0f, 0.0f, 0.0f), nullptr);
            if (game_work.mHud) game_work.mHud->AddControl(m_pBuyButton, false);
            // Binary (0x0015e848..0x0015e84c): register DeletedMenuItem as m_RemoveCallback
            m_pBuyButton->m_RemoveCallback =
                Mortar::Delegate1<void, HUDControl*>::Make(this, &ShopScreen::DeletedMenuItem);
        }
        // LAB_0015e874: scale new button (reached by both state 0 and state 3 paths)
        m_pBuyButton->m_RestScale = m_pBuyButton->m_RestScale * BUTTON_SCALE;
        if (m_pBuyButton->m_pTrackedFruit) {
            m_pBuyButton->m_pTrackedFruit->scale =
                m_pBuyButton->m_pTrackedFruit->scale * BUTTON_SCALE;
        }
        break;
    }

    // ---- STATE 4: Reset layer flags ----
    case 4:
        m_LayerFlagsAlt = Mortar::HUD_LAYER_POST_ACTOR;
        break;

    // ---- STATES 5 and 6: Wait for actors empty, then equip item ----
    case 5:
    case 6: {
        // Fling buy button (same as state 3): use m_pTrackedFruit per binary.
        // Binary writes *(byte*)(piece+0x80)=1 (Fruit+0x80 unconfirmed, no reader);
        // port omits the write.
        if (m_pBuyButton && m_pBuyButton->m_pTrackedFruit) {
            Fruit* piece = m_pBuyButton->m_pTrackedFruit;
            float r1 = ((float)rand() / (float)RAND_MAX) * 5.0f;
            float r2 = ((float)rand() / (float)RAND_MAX) * 5.0f;
            piece->vel = Vec3(r1 + FLING_VEL_BASE, -r2, 0.0f);
            // Binary: TutorialControl::ResetTutePos(tute, 0)
            if (game_work.m_TutorialControl)
                game_work.m_TutorialControl->ResetTutePos((MenuButton*)nullptr);
            m_pBuyButton = nullptr;
        }

        // Binary: wait until Mortar::ActorManager has no active entities (both pools)
        // Then equip selected item via ItemManager.
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        int nLayer1 = am->GetNumEntities(1);
        int nLayer0 = am->GetNumEntities(0);
        if (nLayer1 == 0 && nLayer0 == 0) {
            if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
                ItemInfo* info = m_pSelectedItem->m_pItemInfo;
                char type = info->m_Type;
                if (type < 4) {
                    // Binary: if cached slot item != selected item,
                    //   call ItemManager::SetEquippedItem(type, old_slot_item->ItemInfo)
                    ShopListItem* slotItem = m_pSlotItems[(int)type];
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
    // Binary @ 0x0015ea50-0x0015eabe: alpha-decrease X-shift on the
    // SplatEntity pool (NOT HUD controls). Splats above x=50 drift up,
    // below drift down, by `delta * (190 or 290) * 1.5`. Constants:
    //   DAT_0015eae4 = 290.0f   (down multiplier, lower half)
    //   DAT_0015eae8 = 190.0f   (up multiplier, upper half)
    //   DAT_0015eaec =  50.0f   (split threshold on x)
    // Note: the binary "x" axis is the screen-vertical per project coord
    // convention (X=+160 top, -160 bottom) — semantically a y-shift.
    if (m_TransitionAlpha < prevAlpha) {
        const float delta = prevAlpha - m_TransitionAlpha;
        SplatShiftCtx ctx = {
            delta * 190.0f * 1.5f,
            delta * 290.0f * 1.5f
        };
        SplatEntity::ForEachInPool(SplatShiftVisitor, &ctx);
    }
}

// ---------------------------------------------------------------------------
// ShopScreen::DrawOrder(float*, int) @ 0x001b4e48
//
// Top-level control flow (binary-faithful):
//   if (layerMask == m_LayerFlagsAlt)  -> Block A (BG + dialog; the alt-layer pass)
//   else if (m_AnimFrame > 0)          -> Block B (pulsing ring; the default pass)
//   else                               -> return
//
// The binary reads the layerMask parameter and compares it to m_LayerFlagsAlt;
// it NEVER writes m_LayerFlags inside DrawOrder. The self-mutating latch
// (`m_LayerFlags = HUD_LAYER_DEFAULT`) was a port-only fabrication that caused
// a 1-frame BG skip on touch-down (splat toggles m_LayerFlagsAlt -> latch desyncs).
// Fix: compare layerMask to m_LayerFlagsAlt (stateless, like the binary).
// ASM-spec v1.6.1 ShopScreen::DrawOrder @0x001b4e48: param == m_LayerFlagsAlt gates Block A.
// ---------------------------------------------------------------------------
void ShopScreen::DrawOrder(float* /*hudScale*/, int layerMask) {
    // Static dial_alpha lives at static_block+0x84 in the binary (BSS).
    // Port uses a function-local static — same lifetime (process lifetime).
    static float s_DialAlpha = 0.0f;  // static_block+0x84

    // Static one-time cache for ring scale Vec3 (static_block+0x74 / +0x78 in binary).
    // binary uses __cxa_guard_acquire; port uses a bool + Vec3 static pair.
    static bool  s_RingVecInited = false;  // static_block+0x74
    static Vec3  s_RingVec(0.0f, 0.0f, 1.0f);  // static_block+0x78

    MatrixManager& mm = MatrixManager::GetInstance();

    // All quads use white full-alpha colour (*(Colour**)(GOT+0x73a4) at runtime).
    // The binary reads a runtime GOT entry assumed to be {0xFF,0xFF,0xFF,0xFF}.
    const Colour colourWhite(255, 255, 255, 255);  // DAT_0015e08c = 0x000073a4 GOT entry

    // -----------------------------------------------------------------------
    // Block A gate: binary compares the passed layerMask to m_LayerFlagsAlt.
    // Runs when HUD dispatches the alt-layer pass (0x40 or 0x80).
    // ASM-spec v1.6.1 ShopScreen::DrawOrder @0x001b4e48: param == m_LayerFlagsAlt
    // -----------------------------------------------------------------------
    if (layerMask == (int)m_LayerFlagsAlt) {
        // ===================================================================
        // Block A — BG + dialog box (0x001b4f58 .. 0x001b52cd)
        // Binary does NOT write m_LayerFlags here; the gate is purely
        // based on the passed layerMask each call.
        // ===================================================================

        const float alpha = m_TransitionAlpha;

        // slide_X persists from A1 into A3 (or is set to 145.0f by A2).
        float slide_X;

        if (alpha < 1.0f) {
            // ---------------------------------------------------------------
            // Sub-Block A1 — Sliding BG, two quads  (0x0015def6..0x0015dff9)
            // ---------------------------------------------------------------

            // --- Left quad: anchored to scroll pos, U=[0.03125..0.597656] ---
            // Use Texture::Set so s_LastBoundTexId is tracked --
            // Renderer::DrawQuad skips the draw when the tracker says
            // nothing is bound (raw glBindTexture doesn't update it).
            if (s_TexBGStore.IsValid()) {
                s_TexBGStore->Set();
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
                Mortar::Mesh::DrawQuadUnCached(c,
                    0.03125f, 0.597656f,  // uMin, uMax
                    0.1875f, 0.8125f,     // vMin, vMax
                    NULL);
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
                Mortar::Mesh::DrawQuadUnCached(c,
                    0.597656f, 0.96875f,  // uMin, uMax
                    0.1875f, 0.8125f,     // vMin, vMax
                    NULL);
            }

            if (s_TexBGStore.IsValid()) {
                s_TexBGStore->UnSet();
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
                s_TexBGStore->Set();
            }

            {
                Colour c = colourWhite;
                // DrawQuadSized_GameTask(u0=0.03125f, u1=0.96875f, colour)
                // DAT_0015e06c=0.03125f; u1=0.96875f literal (0x3f780000)
                Mortar::Mesh::DrawQuadUnCached(c,
                    0.03125f, 0.96875f,  // uMin, uMax
                    0.1875f, 0.8125f,    // vMin, vMax
                    NULL);
            }

            if (s_TexBGStore.IsValid()) {
                s_TexBGStore->UnSet();
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
            float texW = (float)(s_TexDialogBox->GetWidth());
            float texH = (float)(s_TexDialogBox->GetHeight());

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
            // dt = game_work.dt  (Game+0x38, DAT_0015e1f0=0x7990 GOT offset to Game*)
            const float dt = game_work.dt;

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

            s_TexDialogBox->Set();
            Mortar::Mesh::DrawQuadUnCached(colDialog, NULL);
            s_TexDialogBox->UnSet();
        }

        return;
    }

    // -----------------------------------------------------------------------
    // Block B — Animated selection ring  (0x001b4e78..0x001b4f54)
    // else if (m_AnimFrame > 0): only runs on the non-alt pass (layerMask != m_LayerFlagsAlt).
    // Binary: `else if (0 < m_AnimFrame)` -- mirrors the `else if` by falling through
    // from Block A's return and gating on AnimFrame here.
    // ASM-spec v1.6.1 ShopScreen::DrawOrder @0x001b4e48: else if (0 < m_AnimFrame)
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
        float w = (float)(s_TexSelected->GetWidth());
        float h = (float)(s_TexSelected->GetHeight());
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
        s_TexSelected->Set();
        Colour c = colourWhite;
        Mortar::Mesh::DrawQuadUnCached(c, NULL);
        s_TexSelected->UnSet();
    }
}

// Binary @ 0x0015c568 (re-analyst 2026-05-18). 3-way branch on selected
// item state: locked -> buy; equipped -> unequip; unlocked-not-equipped
// -> swap into slot. Status-text writes (binary writes "BUY"/"EQUIP"/
// "UNEQUIP"/"SELECTED" into selected->m_StatusText@+0x54) are skipped:
// port's ShopListItem doesn't expose that char* slot. Visible button-
// label updates happen elsewhere via SetSelected reading current state.
void ShopScreen::BuyButtonCallback() {
    ShopListItem* sel = m_pSelectedItem;
    if (!sel || !sel->m_pItemInfo) return;
    ItemInfo* info = sel->m_pItemInfo;
    int type = (int)info->m_Type;
    ItemManager* mgr = ItemManager::GetInstance();
    if (!mgr) return;

    if (info->IsLocked()) {
        // LOCKED -> attempt purchase. BuyItem deducts coins, marks owned.
        mgr->BuyItem(info->m_Hash);
        return;
    }

    if (mgr->IsEquipped(info)) {
        // EQUIPPED -> unequip; clear slot cache.
        mgr->SetEquippedItem(type, nullptr);
        if (type >= 0 && type < 4) m_pSlotItems[type] = nullptr;
        return;
    }

    // UNLOCKED & NOT EQUIPPED -> swap into slot.
    if (type >= 0 && type < 4) m_pSlotItems[type] = sel;
    mgr->SetEquippedItem(type, info);
}

// Binary @ 0x0015c758 (re-analyst 2026-05-18). Commits the in-flight
// selection to the per-type slot cache and transitions to state 5 (exit
// confirm sub-screen). Repositions tutorial ninja off-screen.
void ShopScreen::ConfirmCallback() {
    ShopListItem* sel = m_pSelectedItem;
    if (sel && sel->m_pItemInfo) {
        int type = (int)sel->m_pItemInfo->m_Type;
        if (type >= 0 && type < 4) m_pSlotItems[type] = sel;
    }
    m_State = 5;
    if (game_work.m_TutorialControl) {
        float rx = ((float)(rand() % 500) / 100.0f) + 5.0f;
        float ry = -((float)(rand() % 500) / 100.0f);
        game_work.m_TutorialControl->ResetTutePos(Vec3(rx, ry, 0.0f));
    }
}

// Binary @ 0x0015c7f0 (re-analyst 2026-05-18). Skip the slot-commit;
// transition to state 6. Same tutorial-ninja reposition as Confirm.
void ShopScreen::CancelCallback() {
    m_State = 6;
    if (game_work.m_TutorialControl) {
        float rx = ((float)(rand() % 500) / 100.0f) + 5.0f;
        float ry = -((float)(rand() % 500) / 100.0f);
        game_work.m_TutorialControl->ResetTutePos(Vec3(rx, ry, 0.0f));
    }
}
