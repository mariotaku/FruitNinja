// FruitFactPageControl -- v1.6.1 "page book" controller.
// Binary class name: FruitFactControl (v1.6.1 @ 0x00170c78).
// Port name: FruitFactPageControl (renamed to avoid collision with v1.5.1 FruitFactControl).
//
// Singleton: DATA @ 0x002d7520 (v1.6.1), constructed at static-init time.
// Not reached via a menu button -- it is a persistent always-on HUD control.

#include "hud/FruitFactPageControl.h"
#include "screens/FruitFactPage.h"
#include "hud/MenuButton.h"
#include "hud/HUD.h"
#include "engine/audio/GameSound.h"
#include "engine/audio/MortarSound.h"
#include "engine/util/Delegate.h"
#include "engine/core/MortarTypes.h"
#include "game/GameWork.h"
#include "game/FruitSaveData.h"
#include "entities/Fruit.h"
#include "entities/FruitInfo.h"
#include <cstddef>

// ---------------------------------------------------------------------------
// Static content state (Binary @ base 0x002d7130)
// LoadContent / UnLoadContent are STATIC and operate on file-scope statics,
// NOT on the instance texture SmartPtr fields. The binary keeps a guard byte
// (g_contentLoaded, base+0x43408) plus three static SmartPtr<Texture> slots
// (base+0x6C74, base+0x724C, base+0x6C3C) released in that order.
// ---------------------------------------------------------------------------
namespace {
    bool g_factContentLoaded = false;                      // binary guard @ base+0x43408
    Mortar::SmartPtr<Mortar::Texture> g_factTex0;          // binary slot @ base+0x6C74
    Mortar::SmartPtr<Mortar::Texture> g_factTex1;          // binary slot @ base+0x724C
    Mortar::SmartPtr<Mortar::Texture> g_factTex2;          // binary slot @ base+0x6C3C
}

// File-scope static definition for the shared paging-arrow texture.
Mortar::SmartPtr<Mortar::Texture> FruitFactPageControl::s_TexArrow;

// ---------------------------------------------------------------------------
// LoadContent / UnLoadContent  (Binary @ 0x00170b1c / 0x00171a4c)
// ---------------------------------------------------------------------------

void FruitFactPageControl::LoadContent() {
    // TODO: 0x00170b1c -- load title/header textures via LoadLocalisedTexture
}

void FruitFactPageControl::UnLoadContent() {
    // Binary @ 0x00171a4c: clear guard, then release the 3 static SmartPtrs in
    // the same order LoadContent populated them (0x6C74, 0x724C, 0x6C3C).
    g_factContentLoaded = false;
    g_factTex0.SetNull();   // SmartPtr release -> binary @ 0x00171800
    g_factTex1.SetNull();
    g_factTex2.SetNull();
}

// ---------------------------------------------------------------------------
// ctor  (Binary @ 0x00170c78)
// ---------------------------------------------------------------------------

FruitFactPageControl::FruitFactPageControl()
    : HUDControl3d()
    , m_pCurFactString(NULL)
    , m_FruitIdx(-1)
    , m_FactIdx(-1)
    , m_curPage(-1)
    , m_pLeftArrow(NULL)
    , m_pRightArrow(NULL)
    , m_flags(1)
    , m_flagsB(0)
{
    _pad_A9[0] = 0; _pad_A9[1] = 0; _pad_A9[2] = 0;
}

// ---------------------------------------------------------------------------
// dtor  (Binary @ 0x001718ac)
// ---------------------------------------------------------------------------

FruitFactPageControl::~FruitFactPageControl() {
    Release();
}

// ---------------------------------------------------------------------------
// Init  (Binary @ 0x0017160c)
// ---------------------------------------------------------------------------

void FruitFactPageControl::Init() {
    // TODO: 0x0017160c -- load title/header tex via LoadLocalisedTexture+OS_SPrintf,
    // size header quad, call vtable+0x10 (HUDControl base Init).
}

// ---------------------------------------------------------------------------
// Release  (Binary @ 0x00171808)
// ---------------------------------------------------------------------------

void FruitFactPageControl::Release() {
    // Binary @ 0x00171808. Order from disassembly:
    //   1) SmartPtr<Texture>::SetPtr(NULL) on slot @+0x74 (HUDControl3d base
    //      texture slot, not a named member in this port -- unmodeled here).
    //   2) SmartPtr<Texture>::SetPtr(NULL) on slot @+0x88 (m_Tex88).
    //   3) if (m_pLeftArrow)  HUD::RemoveControl(hud, m_pLeftArrow);  delete it (vtable+4 deleting dtor); null it.
    //   4) if (m_pRightArrow) HUD::RemoveControl(hud, m_pRightArrow); delete it; null it.
    // The binary does NOT touch the m_pages vector in Release -- m_pages is
    // destroyed by ~vector in the destructor (0x001718ac), not here.
    m_Tex88.SetNull();

    // HUD owner: binary reads [singleton+0x40]; the port models this as game_work.mHud.
    HUD* hud = game_work.mHud;

    if (m_pLeftArrow) {
        if (hud) hud->RemoveControl(m_pLeftArrow);
        m_pLeftArrow->Release();
        delete m_pLeftArrow;
        m_pLeftArrow = NULL;
    }
    if (m_pRightArrow) {
        if (hud) hud->RemoveControl(m_pRightArrow);
        m_pRightArrow->Release();
        delete m_pRightArrow;
        m_pRightArrow = NULL;
    }
}

// ---------------------------------------------------------------------------
// Reset  (Binary @ 0x00170800 -- no-op)
// ---------------------------------------------------------------------------

void FruitFactPageControl::Reset() {
    // Binary @ 0x00170800: bare BX LR (no-op).
}

// ---------------------------------------------------------------------------
// SetPos  (Binary @ 0x00170814)
// ---------------------------------------------------------------------------

void FruitFactPageControl::SetPos(const Vec3& p) {
    pos = p;
    // Binary copies pos into each registered page as well
    for (std::vector<FruitFactPage*>::iterator it = m_pages.begin();
         it != m_pages.end(); ++it) {
        if (*it) (*it)->pos = p;
    }
}

// ---------------------------------------------------------------------------
// BeginDraw  (Binary @ 0x00170804 -- no-op)
// ---------------------------------------------------------------------------

void FruitFactPageControl::BeginDraw(float /*dt*/) {
    // Binary @ 0x00170804: bare BX LR (no-op).
}

// ---------------------------------------------------------------------------
// DrawOrder  (Binary @ 0x00170810 -- no-op)
// ---------------------------------------------------------------------------

void FruitFactPageControl::DrawOrder(const Vec3& /*hudScale*/, int /*layerMask*/) {
    // Binary @ 0x00170810: bare BX LR (no-op).
}

// ---------------------------------------------------------------------------
// Update  (Binary @ 0x00170eb4)
// ---------------------------------------------------------------------------

void FruitFactPageControl::Update(float /*dt*/) {
    // Binary @ 0x00170eb4 (FruitFactControl::Update).
    // Force this control's draw layer to 0x80 every frame.
    m_LayerFlags = 0x80;                                  // this+0x34

    // Arrows only exist for multi-page books.
    if (m_pages.size() <= 1) return;                      // bls 0x1712c0

    // Size vector handed to the arrow buttons as their hit-bounds / rest
    // scale. Binary reads the native back-buffer dimensions via
    // VectorUnsignedToFloat(display[+0x24], display[+0x28]) then * 1.0.
    // DIFFERS: original = framebuffer width/height from the Bada display
    // object (rotated portrait surface); port uses the landscape logical
    // screen size FN_SCREEN_W x FN_SCREEN_H (480x320), which is the
    // coordinate space all FruitNinja HUD geometry lives in. The * 1.0
    // scale in the binary is a no-op and is dropped.
    Vec3 arrowSize((float)FN_SCREEN_W, (float)FN_SCREEN_H, 0.0f);

    // ---- Left arrow (this+0xA0) ----
    if (m_pLeftArrow == NULL) {                           // ldr r6,[r4,#0xa0]; cmp #0
        Mortar::SmartPtr<Mortar::Texture> tex(s_TexArrow);
        Vec3 spawnPos = pos;
        Mortar::Delegate0<void> onTap =
            Mortar::Delegate0<void>::QCallee(this, &FruitFactPageControl::LeftButton);
        Vec3 size = arrowSize;
        Mortar::Delegate1<void, HUDControl*> onRemove;   // T_1070: empty/Global no-op
        m_pLeftArrow = new MenuButton(&tex, &spawnPos, &onTap, -1, &size, &onRemove);
        m_pLeftArrow->m_AnimFlag = 1;                    // strb #1,[btn+0xd2]
        m_pLeftArrow->Init();                            // vtable slot 2 (0-arg Init)
        // Left arrow re-uses the right-arrow texture mirrored: UVLeft=1, UVRight=0.
        m_pLeftArrow->m_UVLeft  = 1.0f;                  // [btn+0x64]
        m_pLeftArrow->m_UVRight = 0.0f;                  // [btn+0x6c]
        if (game_work.mHud) game_work.mHud->AddControl(m_pLeftArrow);
    }

    // ---- Right arrow (this+0xA4) ----
    if (m_pRightArrow == NULL) {                          // ldr r6,[r4,#0xa4]; cmp #0
        Mortar::SmartPtr<Mortar::Texture> tex(s_TexArrow);
        Vec3 spawnPos = pos;
        Mortar::Delegate0<void> onTap =
            Mortar::Delegate0<void>::QCallee(this, &FruitFactPageControl::RightButton);
        Vec3 size = arrowSize;
        Mortar::Delegate1<void, HUDControl*> onRemove;
        m_pRightArrow = new MenuButton(&tex, &spawnPos, &onTap, -1, &size, &onRemove);
        m_pRightArrow->Init();                           // vtable slot 2
        m_pRightArrow->m_AnimFlag = 1;                   // strb #1,[btn+0xd2]
        if (game_work.mHud) game_work.mHud->AddControl(m_pRightArrow);
    }

    // ---- Per-frame repositioning (still inside the pages>1 guard) ----
    m_pLeftArrow->m_Active = 1;                           // strb #1,[leftArrow+0x30]
    // DAT_001712cc=-158.0, vmov 0x41000000=8.0, DAT_001712c8=0.0
    m_pLeftArrow->pos  = pos + Vec3(-158.0f, 8.0f, 0.0f);
    // DAT_001712d0=142.0
    m_pRightArrow->pos = pos + Vec3(142.0f, 8.0f, 0.0f);
}

// ---------------------------------------------------------------------------
// SetPage  (Binary @ 0x0017132c)
// ---------------------------------------------------------------------------

void FruitFactPageControl::SetPage(int idx, bool playSound) {
    // Binary @ 0x0017132c. Order is fixed by the disassembly and does NOT
    // bounds-check idx / m_curPage -- it uses raw std::vector::operator[].
    //
    // Resolved constants (v1.6.1):
    //   - SFX name literal @ 0x0028211d = "Next-screen-button"
    //   - SetTotal key literal @ 0x00282130 = "factMode"
    //   - vol = 1.0f (s0), pitch = 1.0f (s1)
    //   - singleton @ GOT 0x2d8924 -> game object: +0x188 == game_work.mGameSound,
    //     +0x4C == game_work.m_SaveData

    // 1) Play the page-flip SFX. UNCONDITIONAL in the binary -- playSound does
    //    NOT gate this. The finish-callback is a default (empty) global delegate.
    if (game_work.mGameSound) {
        Mortar::Delegate1<bool, Mortar::MortarSound*> finishCb;
        game_work.mGameSound->SFXPlay("Next-screen-button", 1.0f, 1.0f, finishCb);
    }

    // 2) Hide the currently-shown page (vtable +0x40 = HidePage). Binary indexes
    //    m_pages[m_curPage] with no bounds check.
    m_pages[m_curPage]->HidePage();

    // 3) Switch.
    m_curPage = idx;

    // 4) Show the new page (vtable +0x44 = ShowPage). Binary indexes m_pages[idx].
    m_pages[idx]->ShowPage();

    // 5) playSound actually gates a save-data write: record that this fact page
    //    has been viewed (1-based page index) under the "factMode" total.
    if (playSound && game_work.m_SaveData) {
        game_work.m_SaveData->SetTotal("factMode", m_curPage + 1, true, true);
    }
}

// ---------------------------------------------------------------------------
// RegisterPage  (Binary @ 0x00171ab4)
// ---------------------------------------------------------------------------

void FruitFactPageControl::RegisterPage(FruitFactPage* page) {
    if (!page) return;
    m_pages.push_back(page);
    // Hide if not the current page; copy control pos into page
    if ((int)m_pages.size() - 1 != m_curPage) {
        page->HidePage();
    }
    page->pos = pos;
}

// ---------------------------------------------------------------------------
// LeftButton / RightButton  (Binary @ 0x00171534 / 0x00171458)
// ---------------------------------------------------------------------------

void FruitFactPageControl::LeftButton() {
    // Binary @ 0x00171534. Plays the page-flip click SFX, then moves to the
    // previous page with wrap-around.
    // Disasm: ldr r1,[r6,#0x9c] (m_curPage); cmp #0; subne r1,r1,#1
    //         (curPage-1); if ==0 -> r1 = m_pages.size()-1; SetPage(r1, 1).
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("Next-screen-button", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
    int idx;
    if (m_curPage != 0) {
        idx = m_curPage - 1;
    } else {
        idx = (int)m_pages.size() - 1;
    }
    SetPage(idx, true);
}

void FruitFactPageControl::RightButton() {
    // Binary @ 0x00171458: plays a click SFX via GameSound::SFXPlay, then advances the page.
    // Page nav: if on last page, wrap to 0; otherwise advance by 1.
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("Next-screen-button", 1.0f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
    int next;
    if (m_curPage == (int)m_pages.size() - 1) {
        next = 0;
    } else {
        next = m_curPage + 1;
    }
    SetPage(next, true);
}

// ---------------------------------------------------------------------------
// Input handlers  (Binary @ 0x001708b8 / 0x0017086c / 0x00170a20 / 0x00170924)
// ---------------------------------------------------------------------------

bool FruitFactPageControl::LeftPressed(InputEvent* /*ev*/) {
    // Binary @ 0x001708b8 -- decrement fact index, wrap, refetch fact string.
    // Field offsets from disassembly: [+0x84]=m_FactIdx, [+0x80]=m_FruitIdx,
    // [+0x7C]=m_pCurFactString. FruitInfo->[+0x270]=m_FactCount.
    --m_FactIdx;
    if (m_FactIdx < 0)
        m_FactIdx = Fruit::FruitInfo(m_FruitIdx)->m_FactCount - 1;
    m_pCurFactString = Fruit::GetFact(NULL, NULL, m_FruitIdx, m_FactIdx);
    return true;
}

bool FruitFactPageControl::RightPressed(InputEvent* /*ev*/) {
    // Binary @ 0x0017086c (FruitFactControl::RightPressed):
    //   ++m_FactIdx; info = Fruit::FruitInfo(m_FruitIdx);
    //   if (info->m_FactCount <= m_FactIdx) m_FactIdx = 0;
    //   m_pCurFactString = Fruit::GetFact(NULL, NULL, m_FruitIdx, m_FactIdx);
    //   return true;
    ++m_FactIdx;
    const ::FruitInfo* info = Fruit::FruitInfo(m_FruitIdx);
    if (info && info->m_FactCount <= m_FactIdx) {
        m_FactIdx = 0;
    }
    m_pCurFactString = Fruit::GetFact(NULL, NULL, m_FruitIdx, m_FactIdx);
    return true;
}

bool FruitFactPageControl::UpPressed(InputEvent* /*ev*/) {
    // TODO: 0x00170a20 -- forward to active page
    return false;
}

bool FruitFactPageControl::DownPressed(InputEvent* /*ev*/) {
    // TODO: 0x00170924 -- forward to active page
    return false;
}
