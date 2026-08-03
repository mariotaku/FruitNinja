#ifndef FN_SHOP_LIST_ITEM_H
#define FN_SHOP_LIST_ITEM_H

//
// ShopListItem : ScrollingMenuItem
//
// One row in the Sensei's Swag shop list. Owns five BakedStringBox
// labels (title, category, [unused], description, prompt) built lazily
// in NewDraw() and DrawDescription() and drawn via the TTF font chain
// (game_work.m_pTTFFontMain). The v1.6.1 Draw() is a thin dispatcher:
//   - onscreen  -> NewDraw() (all rendering, calls DrawDarkness() at end)
//   - offscreen -> DrawDarkness() only (loading.tex stripe when m_bIsNew)
//
// Box ownership: ctor zero-inits all five pointers; NewDraw() and
// DrawDescription() build lazily; dtor deletes all non-null boxes.
//
// Binary refs (v1.6.1):
//   ctor (0-param) 0x001b41f0
//   ctor (5-param) 0x001b27f0  (takes ItemInfo* + texture data)
//   dtor           0x001b4270 / 0x001b42b0 / 0x001b4308
//   Create         0x001b27f0
//   Move           0x001b54b0  (vtable slot 6 +0x18)
//   Draw           0x001b5da4  (vtable slot 12 +0x30, thin dispatcher)
//   NewDraw        0x001b58e8  (all visible rendering, TTF)
//   DrawDividers   0x001b1a98
//   DrawIcon       0x001b578c
//   DrawFloatingText 0x001b4bc8
//   DrawInAppPurchaseTags 0x001b1798  (defunct no-op stub)
//   DrawDescription 0x001b1f20
//   DrawDarkness   (loading.tex stripes; extracted from Draw)
//
// Vtable overrides (v1.6.1):
//   slot  0 (+0x00)  ~ShopListItem dtor1  0x001b42b0
//   slot  1 (+0x04)  ~ShopListItem dtor2  0x001b4308
//   slot  6 (+0x18)  ShopListItem::Move   0x001b54b0
//   slot 12 (+0x30)  ShopListItem::Draw   0x001b5da4  (v1.6.1 thin dispatcher)
//
// Binary ScrollingMenuItem ends at +0x58 (88 bytes; base ctor 0x0015b5dc).
// ShopListItem own-fields begin immediately at +0x58 in the binary:
//   +0x58  ShopScreen*  m_pShopScreen  (binary m_field58: void* set to ShopScreen*)
//   +0x5C  char[128]    m_DescText     (inline description text buffer)
//   +0xDC  (end of m_DescText)
//
// Extended ShopListItem fields (absolute offsets from ShopListItem* this):
//   +0x25C  float  m_NewItemAlpha   >0 -> draw new_item_sml badge; fades from init
//   +0x260  float  m_SelectedAlpha  >0 -> draw selected_sml highlight ring
//   +0x264  float  m_LockFlashAlpha (init 0.0; set to 0.25 on locked-item tap)
//   +0x268  Vec3   m_IconPos        icon translate cache; Move writes each frame
//   +0x274  Mortar::SmartPtr<Texture>  m_pIconTex
//   +0x278  ItemInfo*  m_pItemInfo
//   +0x27C  byte   m_bOnscreenItem  1 in ctor
//   +0x27D  byte   m_bSelected      0 in ctor; resets box-colour cache when set
//   +0x27E  byte   m_bIsNew         0 in ctor; non-zero = draw loading.tex stripe
//   +0x27F  pad
//   +0x280  float  m_CostAlpha      (m_CostAlpha * 255.0f) -> byte alpha for desc text
//   +0x284  [4-byte gap]
//   +0x288  BakedStringBox* m_pBox0   title label; built once in NewDraw
//   +0x28C  BakedStringBox* m_pBox1   category label; rebuilt when m_TintA != m_Type
//   +0x290  byte   m_TintA           caches last m_Type for m_pBox1 rebuild; ctor=0xFF
//   +0x291  [3-byte pad]
//   +0x294  BakedStringBox* m_pBox2   reserved/unused; ctor=null
//   +0x298  BakedStringBox* m_pBox3   description body; built in DrawDescription
//   +0x29C  BakedStringBox* m_pBox4   unlock-requirement prompt; built in DrawDescription
//   +0x2A0  byte   m_TrailFlag        caches bVar6 (locked+req) for box3/4 rebuild; ctor=0
//   +0x2A4  (end, sizeof=0x2A4)
//
// Gap from end of ScrollingMenuItem (+0xDC) to +0x25C = 0x180 bytes.
// Intermediate layout not yet fully RE'd. Filled with zeros.
//

#include "ScrollingMenuItem.h"
#include "asset/Texture.h"
#include "util/SmartPtr.h"
#include "engine/math/_Vector3.h"
#include <cstddef>
#include <cstdint>

class ItemInfo;
class ShopScreen;

namespace Mortar { class BakedStringBox; }

// v1.6.1: ShopListItem vtable @ 0x1ea030 (17 slots, inherits v1.6.1 ScrollingMenuItem).
// Draw override at vtable slot 12 (+0x30).
class ShopListItem : public ScrollingMenuItem {
public:
    // ctor @ v1.6.1 ShopListItem::ShopListItem() @ 0x001b41f0
    // Zeros all box ptrs, sets m_TintA=0xFF, m_TrailFlag=0.
    ShopListItem();
    ~ShopListItem() override;

    // ShopListItem::Create @ v1.6.1 0x001b27f0
    // Called by ShopScreen::Init after each ShopListItem() ctor.
    // Sets row metrics, stores ItemInfo* and ShopScreen* back-pointer,
    // loads icon texture, and fills m_DescText from ItemInfo strings.
    void Create(ItemInfo* pItemInfo, ShopScreen* pShopScreen);

    // vtable slot 6 (+0x18): Move override
    // v1.6.1 ShopListItem::Move @0x001b54b0:
    //   1. Sin-jitter bounce (s_ShimmerPhase/s_ShimmerY) when m_bSelected.
    //   2. Sets pos.x/y/z.
    //   3. Copies pos -> m_IconPos; if m_pIconTex: m_IconPos.x += 95.2f.
    //   4. When m_LockFlashAlpha > 0: subtract dt, scatter m_IconPos x/y by ±2.5.
    //   5. Per-frame alpha ramps (m_NewItemAlpha, m_SelectedAlpha, m_CostAlpha).
    //      Steps 1/4/5 are __bada__ only -- port carries all time-dependent
    //      state advances out to AdvanceAnim() below so they aren't
    //      double-advanced by the port's two Move() call sites
    //      (ScrollingMenu::Update 60Hz + ScrollingMenu::UpdateRealtime
    //      per-present). Positioning (2/3, and the m_LockFlashAlpha > 0
    //      scatter/offset math in step 4) stays in Move -- idempotent, safe
    //      to repeat. See AdvanceAnim.
    void Move(_Vector3<float> v) override;

#ifndef __bada__
    // Port specific: no binary counterpart. Carries every time-dependent
    // state advance out of Move -- the sin-jitter bounce (s_ShimmerPhase/
    // s_ShimmerY, gated by m_bSelected), the m_LockFlashAlpha countdown, and
    // the m_NewItemAlpha/m_SelectedAlpha/m_CostAlpha linear rate*dt ramps
    // (rate 5.0/sec) -- so each can be called exactly ONCE per present with
    // the real per-present dtSeconds, independent of how many times Move()
    // itself runs (positioning in Move is idempotent and safe to repeat;
    // these timers are not). Call from ScrollingMenu::UpdateRealtime's
    // Phase 5 loop only -- NOT from ScrollingMenu::Update's Phase 5.
    void AdvanceAnim(float dtSeconds) override;
#endif

    // vtable slot 12 (+0x30): Draw() dispatcher + offscreen description fade.
    // v1.6.1 ShopListItem::Draw @0x001b5da4:
    //   if (m_bSelected) reset s_lastDrawnType colour cache.
    //   if (m_bOnscreen)  { NewDraw(); return; }
    //   OFFSCREEN: while m_pShopScreen && m_pItemInfo &&
    //   (int)(m_CostAlpha*255) > 0, draws the fading side-panel description
    //   with the LEGACY BITMAP font (pM_Fonts[1] = game_work.pFontMain) --
    //   auto-shrink from 18.0 by 0.25 while GetStringHeight(desc,size,160)
    //   > 82.5; locked + reqType 1/2 draws the red/green requirement prompt
    //   at (GetDescriptionTextXPos(),-20) then white desc at (x,+10);
    //   else desc at (x,0), white if locked else 0x745D3B.
    //   Then DrawDarkness() always.
    void Draw() override;

    // NewDraw -- all visible rendering, v1.6.1 TTF path.
    // v1.6.1 ShopListItem::NewDraw @0x001b58e8:
    //   Head gate re-checks m_bOnscreen (+0x2D, ldrb @0x001b5910); when clear it
    //   runs DrawDarkness() and returns. m_pItemInfo is NOT tested -- it is
    //   dereferenced unguarded from 0x001b5960 on.
    //   Lazily builds m_pBox0 (title, fontSize=16, 195x30) and
    //   m_pBox1 (category, fontSize=14, 175x30) from game_work.m_pTTFFontMain.
    //   m_pBox1 is rebuilt whenever m_TintA != m_Type (m_TintA caches last type).
    //   Draws both boxes with shadow, then calls:
    //   DrawDividers() -> DrawFloatingText() -> DrawIcon() ->
    //   DrawInAppPurchaseTags() -> DrawDescription() -> DrawDarkness().
    void NewDraw();

    // DrawDividers -- 257x17 divider quads at top/bottom of row.
    // v1.6.1 ShopListItem::DrawDividers @0x001b1a98:
    //   Divider 1 at pos + UnitY*GetHeight()/2. Colour: white(200) if
    //   s_lastDrawnType==m_Type, else grey(255); updates s_lastDrawnType.
    //   Divider 2 only when m_bIsNew: at pos - UnitY*GetHeight()/2, grey.
    void DrawDividers();

    // DrawIcon -- 64x64 icon quad at m_IconPos.
    // v1.6.1 ShopListItem::DrawIcon @0x001b578c:
    //   When m_pIconTex valid: draws m_pIconTex (unlocked) or
    //   ShopScreen::s_TexLockedStroke (locked) at Vec3(0,0,0) + m_IconPos.
    void DrawIcon();

    // DrawFloatingText -- ingame-popup badges NEW and SELECTED.
    // v1.6.1 ShopListItem::DrawFloatingText @0x001b4bc8:
    //   pM_Popups[0x10] at scale 0.8 when m_NewItemAlpha > 0.
    //   pM_Popups[0x11] at scale 0.5 when m_SelectedAlpha > 0.
    void DrawFloatingText();

    // DrawInAppPurchaseTags -- defunct in v1.6.1.
    // Defunct: in-app purchase tags -- no-op stub;
    // v1.6.1 ShopListItem::DrawInAppPurchaseTags @0x001b1798
    void DrawInAppPurchaseTags();

    // DrawDescription -- TTF description + unlock-requirement prompt.
    // v1.6.1 ShopListItem::DrawDescription @0x001b1f20:
    //   Gate: m_pShopScreen!=0 && m_pItemInfo!=0 && alpha>0.
    //   bVar6/reqFlag = isLocked && m_RequirementType != 0 && != 3
    //   (m_RequirementType is 0..3, so equivalently: in {1,2}).
    //   Prompt string id AND prompt colour are recomputed EVERY frame
    //   (0x001b1f74-0x001b2084), never cached on the box.
    //   On a reqFlag change BOTH m_pBox3 and m_pBox4 are destroyed
    //   (0x001b2090 / 0x001b20b4) before m_TrailFlag is updated.
    //   Lazily builds m_pBox3 (desc body, w=160, h=62|82 based on reqFlag
    //   and language; Arabic h-=20; Japanese fontSize=12 else 14).
    //   Builds m_pBox4 (prompt, 160x21, fontSize=12) when a prompt string
    //   exists and the box is null -- SetColour is NOT called at build time.
    //   Category strings: LSTR_SHOP_BLADE(0xCA)/BACKGROUND(0xC9)/
    //                     FULL_VERSION(0xCB)/SPECIAL(0x12F) per m_Type.
    //   Japanese side-effect: GETSTRING_CAST_0(0x111) called and discarded
    //   (inside the box3-build block, 0x001b211c).
    //   box4 draw (0x001b2224): gated only on m_pBox4 != NULL; per-frame
    //   SetTranslation -> SetColour(colour,1) @0x001b229c -> Draw, so the
    //   prompt follows the live m_CostAlpha fade and the live met/unmet
    //   colour: red(0xBD,0,0,alpha) unmet or green(0xA0,0xDC,0,alpha) met.
    //   box3 colour: white (locked) or (0x74,0x5D,0x3B,alpha) (unlocked).
    void DrawDescription();

    // DrawDarkness -- loading.tex stripe overlay when m_bIsNew.
    // Extracted from v1.6.1 ShopListItem::Draw @0x001b5da4:
    //   Two 290x120 black(0,0,0,128) quads at parent->pos.x-2, ±105.
    //   Gated internally on m_bIsNew.
    // DIFFERS: opt-in widescreen -- the quad is off-center on the list column
    // (rest center = -97, not the field center), so a symmetric width*k scale
    // under-reaches the widened LEFT field edge while overshooting the right
    // needlessly. Left/right edges are computed independently instead: right
    // edge stays at its 3:2 value (clear of the description plate at X=145),
    // left edge extends by the exact field-edge shift (Layout::HalfWidth()-240)
    // so it reaches -HalfWidth() same as A2's field-centred BG panel. Identity
    // under __bada__/3:2 (byte-identical to the pre-widescreen quad). Vertical
    // size (120) / ±105 y offsets untouched.
    void DrawDarkness();

    // Process-wide shimmer oscillator (static_block +0x68 phase, +0x6c Y).
    static uint16_t s_ShimmerPhase;
    static float    s_ShimmerY;

    // --- ShopListItem own fields ---

    // +0x58: ShopScreen* back-pointer (port maps void* from binary).
    ShopScreen* m_pShopScreen;        // +0x58 ARM32

    // +0x5C: inline description text buffer (128 bytes).
    char m_DescText[128];             // +0x5C..+0xDB ARM32

    // Padding to bridge from end-of-m_DescText (+0xDC on ARM32) to m_NewItemAlpha (+0x25C).
    // Binary ground truth: 0x25C - 0x58 (ScrollingMenuItem ARM32) - 0x04 (m_pShopScreen 32-bit)
    //                       - 0x80 (m_DescText) = 0x180.
    // On host x64/wasm32 this pad places m_NewItemAlpha at a different (larger) offset;
    // the __bada__-gated static_asserts below only fire on the 32-bit ARM32 cross-build
    // where the offset must be exactly 0x25C. Code always accesses by field name.
    char _pad[0x180];  // bridge +0xDC..+0x25B (ARM32 binary-faithful)

    // +0x25C: new-item badge alpha (>0 => draw new_item_sml badge)
    float m_NewItemAlpha;             // +0x25C

    // +0x260: selected-ring alpha (>0 => draw selected_sml highlight)
    float m_SelectedAlpha;            // +0x260

    // +0x264: flash alpha (set to 0.25 on locked-item tap)
    float m_LockFlashAlpha;           // +0x264

    // +0x268: icon translate cache, written by Move each frame.
    // x = pos.x + 95.2f (when m_pIconTex valid), y = pos.y, z = pos.z.
    // ASM-spec v1.6.1 ShopListItem::Move @0x001b54b0
    _Vector3<float> m_IconPos;                   // +0x268..+0x273 (ARM32)

    // +0x274: item icon texture SmartPtr (4 bytes on ARM32 / 8 bytes x86_64).
    // Fields after m_pIconTex and m_pItemInfo (both pointer-sized) cannot
    // satisfy ARM32 absolute offsets on x86_64; static_asserts for them are gated.
    Mortar::SmartPtr<Mortar::Texture> m_pIconTex;  // +0x274 (ARM32)

    // +0x278: pointer to ItemInfo for this row (null = no item).
    ItemInfo* m_pItemInfo;            // +0x278 (ARM32)

    // +0x27C: onscreen flag (1 = on-screen; 0 = offscreen path in Draw)
    uint8_t m_bOnscreenItem;          // +0x27C
    // +0x27D: selected flag (resets colour cache when set)
    uint8_t m_bSelected;              // +0x27D
    // +0x27E: new-item flag (non-zero = draw loading.tex badge stripe)
    uint8_t m_bIsNew;                 // +0x27E
    // +0x27F: alignment pad
    uint8_t _pad3;                    // +0x27F
    // +0x280: cost text alpha (m_CostAlpha * 255.0f clamped -> byte alpha)
    float m_CostAlpha;                // +0x280

    // +0x284: 4-byte gap (binary: not written by ctor or Create).
    uint8_t _pad4[0x04];              // +0x284..+0x287

    // +0x288: title BakedStringBox; built once in NewDraw.
    // ARM32 pointer = 4 bytes (same stride as the former float placeholders).
    Mortar::BakedStringBox* m_pBox0;  // +0x288 (ARM32)
    // +0x28C: category BakedStringBox; rebuilt when m_TintA != m_Type.
    Mortar::BakedStringBox* m_pBox1;  // +0x28C (ARM32)
    // +0x290: caches last m_Type used to build m_pBox1; ctor=0xFF (sentinel).
    uint8_t m_TintA;                  // +0x290
    // +0x291..+0x293: alignment pad (keeps m_pBox2 at +0x294 on ARM32).
    uint8_t _pad5[0x03];              // +0x291..+0x293
    // +0x294: reserved/unused BakedStringBox; always null.
    Mortar::BakedStringBox* m_pBox2;  // +0x294 (ARM32)
    // +0x298: description body BakedStringBox; built in DrawDescription.
    Mortar::BakedStringBox* m_pBox3;  // +0x298 (ARM32)
    // +0x29C: unlock-requirement prompt BakedStringBox; built in DrawDescription.
    Mortar::BakedStringBox* m_pBox4;  // +0x29C (ARM32)
    // +0x2A0: caches bVar6 (locked&&req) used to build m_pBox3/4; ctor=0.
    uint8_t m_TrailFlag;              // +0x2A0
    // +0x2A1..+0x2A3: tail alignment pad (sizeof == 0x2A4 on ARM32).
    uint8_t _pad6[0x03];              // +0x2A1..+0x2A3

public:
    // Binary @ 0x001b3e10: if (m_pShopScreen) m_pShopScreen->SetSelected(this).
    void ButtonClicked();
};

// ---------------------------------------------------------------------------
// Compile-time offset verification (ARM32 binary absolute offsets).
//
// ALL asserts are gated to __bada__ (Bada cross-build with Sourcery libstdc++).
// Host x64 and wasm32 have pointer-size-dependent layouts; code accesses fields
// by name, so only the 32-bit ARM32 binary offsets need to be verified.
//
// _pad is sized for the faithful ARM32 layout (0x180); on host x64 / wasm32
// m_NewItemAlpha lands at a different offset (harmless -- the assert is compiled out).
//
// The inner __GLIBCXX__ > 20090722 sub-guard skips the asm-verify cross-build
// (Sourcery 2010q1, __GLIBCXX__ == 20090722) which uses a slightly different
// SmartPtr/string layout that would produce false positives on pointer-containing fields.
// ---------------------------------------------------------------------------

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#endif

// All offset asserts: __bada__ + Sourcery libstdc++ only.
// _pad = 0x180 ensures ARM32 offsets: 0x58 (base) + 0x04 + 0x80 (m_DescText) + 0x180 = 0x25C.
#if defined(__bada__) && defined(__GLIBCXX__) && __GLIBCXX__ > 20090722
static_assert(offsetof(ShopListItem, m_NewItemAlpha)   == 0x25C, "ShopListItem::m_NewItemAlpha must be at +0x25C");
static_assert(offsetof(ShopListItem, m_SelectedAlpha)  == 0x260, "ShopListItem::m_SelectedAlpha must be at +0x260");
static_assert(offsetof(ShopListItem, m_LockFlashAlpha) == 0x264, "ShopListItem::m_LockFlashAlpha must be at +0x264");
static_assert(offsetof(ShopListItem, m_IconPos)        == 0x268, "ShopListItem::m_IconPos must be at +0x268");
static_assert(offsetof(ShopListItem, m_bOnscreenItem)  == 0x27C, "ShopListItem::m_bOnscreenItem must be at +0x27C");
static_assert(offsetof(ShopListItem, m_bSelected)      == 0x27D, "ShopListItem::m_bSelected must be at +0x27D");
static_assert(offsetof(ShopListItem, m_bIsNew)         == 0x27E, "ShopListItem::m_bIsNew must be at +0x27E");
static_assert(offsetof(ShopListItem, m_CostAlpha)      == 0x280, "ShopListItem::m_CostAlpha must be at +0x280");
static_assert(offsetof(ShopListItem, m_pBox0)          == 0x288, "ShopListItem::m_pBox0 must be at +0x288");
static_assert(offsetof(ShopListItem, m_pBox1)          == 0x28C, "ShopListItem::m_pBox1 must be at +0x28C");
static_assert(offsetof(ShopListItem, m_TintA)          == 0x290, "ShopListItem::m_TintA must be at +0x290");
static_assert(offsetof(ShopListItem, m_pBox2)          == 0x294, "ShopListItem::m_pBox2 must be at +0x294");
static_assert(offsetof(ShopListItem, m_pBox3)          == 0x298, "ShopListItem::m_pBox3 must be at +0x298");
static_assert(offsetof(ShopListItem, m_pBox4)          == 0x29C, "ShopListItem::m_pBox4 must be at +0x29C");
static_assert(offsetof(ShopListItem, m_TrailFlag)      == 0x2A0, "ShopListItem::m_TrailFlag must be at +0x2A0");
static_assert(sizeof(ShopListItem) == 0x2a4, "ShopListItem sizeof must be 0x2a4 (676)");
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif // FN_SHOP_LIST_ITEM_H
