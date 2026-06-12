// FruitFactPageControl -- v1.6.1 "page book" controller.
// Binary class name: FruitFactControl (v1.6.1 @ 0x00170c78).
// Port name: FruitFactPageControl (renamed to avoid collision with v1.5.1 FruitFactControl).
//
// Singleton: DATA @ 0x002d7520 (v1.6.1), constructed at static-init time.
// Not reached via a menu button -- it is a persistent always-on HUD control.

#include "hud/FruitFactPageControl.h"
#include "screens/FruitFactPage.h"
#include "hud/MenuButton.h"
#include "engine/audio/GameSound.h"
#include "game/GameWork.h"
#include <cstddef>

// ---------------------------------------------------------------------------
// LoadContent / UnLoadContent  (Binary @ 0x00170b1c / 0x00171a4c)
// ---------------------------------------------------------------------------

void FruitFactPageControl::LoadContent() {
    // TODO: 0x00170b1c -- load title/header textures via LoadLocalisedTexture
}

void FruitFactPageControl::UnLoadContent() {
    // TODO: 0x00171a4c -- release all static texture SmartPtrs
}

// ---------------------------------------------------------------------------
// ctor  (Binary @ 0x00170c78)
// ---------------------------------------------------------------------------

FruitFactPageControl::FruitFactPageControl()
    : HUDControl3d()
    , m_curPage(-1)
    , m_pLeftArrow(NULL)
    , m_pRightArrow(NULL)
    , m_flags(1)
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
    // TODO: 0x00171808 -- delete m_pLeftArrow / m_pRightArrow, clear pages vector
    m_pLeftArrow = NULL;
    m_pRightArrow = NULL;
    m_pages.clear();
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
    // TODO: 0x00170eb4 -- when m_pages.size()>1, lazily create m_pLeftArrow /
    // m_pRightArrow MenuButton pairs (operator new(0x178), position relative to
    // control pos). Button positions and textures are not yet resolved.
}

// ---------------------------------------------------------------------------
// SetPage  (Binary @ 0x0017132c)
// ---------------------------------------------------------------------------

void FruitFactPageControl::SetPage(int idx, bool playSound) {
    if (idx < 0 || idx >= (int)m_pages.size()) return;
    if (m_curPage >= 0 && m_curPage < (int)m_pages.size()) {
        if (m_pages[m_curPage]) m_pages[m_curPage]->HidePage();
    }
    m_curPage = idx;
    if (m_pages[m_curPage]) m_pages[m_curPage]->ShowPage();
    if (playSound && game_work.mGameSound) {
        // TODO: 0x0017132c -- play sfx name from binary (not yet resolved)
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
    // TODO: 0x00171534 -- page nav: decrement m_curPage (wrap), call SetPage
}

void FruitFactPageControl::RightButton() {
    // TODO: 0x00171458 -- page nav: increment m_curPage (wrap), call SetPage
}

// ---------------------------------------------------------------------------
// Input handlers  (Binary @ 0x001708b8 / 0x0017086c / 0x00170a20 / 0x00170924)
// ---------------------------------------------------------------------------

bool FruitFactPageControl::LeftPressed(InputEvent* /*ev*/) {
    // TODO: 0x001708b8 -- forward to left-arrow or active page
    return false;
}

bool FruitFactPageControl::RightPressed(InputEvent* /*ev*/) {
    // TODO: 0x0017086c -- forward to right-arrow or active page
    return false;
}

bool FruitFactPageControl::UpPressed(InputEvent* /*ev*/) {
    // TODO: 0x00170a20 -- forward to active page
    return false;
}

bool FruitFactPageControl::DownPressed(InputEvent* /*ev*/) {
    // TODO: 0x00170924 -- forward to active page
    return false;
}
