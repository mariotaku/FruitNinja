//
// ListBox : HUDControl3d
// v1.6.1 ListBox @ 0x001941b8..0x001954a8
//
// Dropdown-list half of the dead-code dropdown widget stack. Complete faithful
// implementation. Field offsets re-verified against the ctor @0x00194a74; the
// composition (ListBox owns a VerticalScroller) and the scroll-field read
// (m_pScroller->m_CurrentValue) are ported exactly.
//

#include "ListBox.h"
#include "VerticalScroller.h"
#include "hud/HUD.h"
#include "hud/HUDLayer.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "render/Utf8StringIterator.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "engine/input/Touch.h"
#include "game/GameWork.h"
#include <cstdint>

// Row-background texture (binary: SmartPtr<Texture> s_bar). LoadContent loads
// box.tex -- the same shared texture ComboBox/SliderControl load for their
// own bar/track art.
static Mortar::SmartPtr<Mortar::Texture> s_bar;

// ---------------------------------------------------------------------------
// Constructor -- Binary @ 0x00194a74 (C1) / 0x00194d18 (C2)
// ASM-spec v1.6.1 ListBox::ListBox @0x00194a74: field writes
//   m_pItems(+0x7C)=&items, m_TopVisibleIt(+0x80)=selIter, m_HoverIt(+0x84)=end(),
//   m_pTextFont(+0x8C)=fonts[1], m_CellHeightParam(+0x98), m_CellWidthParam(+0x94),
//   m_FontScaleParam(+0x96), m_VisibleRows(+0xD8), m_CellWidth(+0x9C)=cellWidthParam*size.x,
//   m_CellHeight(+0xA0)=fontScaleParam*size.y, m_ActiveTouchId(+0xC8)=-1, m_LayerFlags(+0x34)=0x800.
//   When items.size() > visibleRows: creates + HUD-adds a VerticalScroller (see header).
ListBox::ListBox(Vec3 inPos, Vec3 inSize, std::vector<std::string>& items,
                 std::string* selIter, uint8_t visibleRows,
                 uint16_t cellHeightParam, uint16_t cellWidthParam, uint16_t fontScaleParam)
    : HUDControl3d()
    , m_pItems(&items)
    , m_TopVisibleIt(selIter)
    , m_HoverIt(items.empty() ? NULL : (&items[0] + items.size()))  // end()
    , m_pScroller(NULL)
    , m_pTextFont(game_work.pFontMain.Get())                          // fonts[1]
    , m_TextColour()                                                  // opaque BLACK (Colour::Colour() @0x0011afa8, ASM-confirmed) -- see header
    , m_CellWidthParam(cellWidthParam)
    , m_FontScaleParam(fontScaleParam)
    , m_CellHeightParam(cellHeightParam)
    , _pad9A(0)
    , m_CellWidth(0.0f)
    , m_CellHeight(0.0f)
    , m_OnSelect()
    , m_ActiveTouchId(-1)
    // DIFFERS: original m_TouchX sentinel is -NaN (0xFFC00000). The port uses
    //   -1.0f as the "no held slot" marker -- observable-equivalent on the dead
    //   touch path, and avoids NaN-compare portability quirks.
    , m_TouchX(-1.0f)
    , m_TouchY(0.0f)
    , _padD4(0.0f)
    , m_VisibleRows(visibleRows)
{
    _padD9[0] = _padD9[1] = _padD9[2] = 0;
    pos  = inPos;
    size = inSize;

    m_CellWidth  = (float)cellWidthParam * size.x;   // +0x9C
    m_CellHeight = (float)fontScaleParam * size.y;   // +0xA0

    m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;        // +0x34 = 0x800

    size_t n = items.size();
    if (n > (size_t)visibleRows) {
        int scrollRange = (int)n - (int)visibleRows;                 // VS maxValue
        int selIndex = (selIter && !items.empty())
                     ? (int)(selIter - &items[0]) : 0;
        int initScroll = selIndex - 2;
        if (initScroll < 0)                 initScroll = 0;
        else if (initScroll > scrollRange)  initScroll = scrollRange;

        Vec3 scrollerPos(pos.x + m_CellWidth * 0.5f,
                         pos.y + (float)(visibleRows - 1) * m_CellHeight * -0.5f,
                         -1.0f);
        m_pScroller = new VerticalScroller(scrollerPos, size,
                                           /*min*/ 0, scrollRange, /*step*/ 1,
                                           initScroll, /*reverse*/ true,
                                           visibleRows, /*visibleHeight*/ 0,
                                           (uint16_t)(visibleRows * 16));
        m_pScroller->AdjustByWidth();
        m_pScroller->Init();
        if (game_work.mHud) {
            game_work.mHud->AddControl(m_pScroller, false);
        }
    }
}

ListBox::~ListBox() {
    Release();
}

// vtable slot 2 -- Binary @ 0x001941b8 (empty)
void ListBox::Init() {
}

// vtable slot 3 -- Binary @ 0x00194528
// ASM-verified: 2026-07-11 v1.6.1 ListBox::Release @ 0x00194528 (re-analyst)
//   if (m_pScroller) { HUD::RemoveControl(hud, m_pScroller);   // +0x88
//                       m_pScroller->vtable[1](); }             // deleting dtor
//                       m_pScroller = NULL;
//   m_pTextFont(+0x8c) = NULL;
//   -- then base HUDControl3d teardown.
// The port previously called only HUDControl3d::Release(), which never removed
// the VerticalScroller from the HUD: it's a SEPARATE control the ctor
// AddControl'd, and nothing else ever tears it down. That's why the scroller
// (and its up/down arrows) stayed visible even after the ListBox itself closed.
void ListBox::Release() {
    if (m_pScroller) {
        if (game_work.mHud) {
            game_work.mHud->RemoveControl(m_pScroller);
        }
        delete m_pScroller;
        m_pScroller = NULL;
    }
    m_pTextFont = NULL;
    HUDControl3d::Release();
}

// vtable slot 6 -- Binary @ 0x001941fc (empty)
void ListBox::PreDraw(float* hudScale) {
    (void)hudScale;
}

// ---------------------------------------------------------------------------
// vtable slot 7 -- Binary @ 0x00194788
// ASM-spec v1.6.1 ListBox::Draw @0x00194788: draws up to visibleRows rows top-down
//   from pos.y. Top visible row = items.begin() + (overflow ? m_pScroller->m_CurrentValue
//   : 0). Each row = s_bar background quad (scale m_CellWidth x m_CellHeight at
//   (pos.x, rowY)) tinted: hover row (== m_HoverIt) RGB(0x50,0x96,0xFF); committed
//   row (== m_TopVisibleIt) Blue; else White. Row text (m_TextColour, tinted by
//   hudScale) at (pos.x - m_CellWidth*0.5 + size.x*5, rowY + size.y*7), font size
//   = m_CellHeightParam * size.x.
void ListBox::Draw(float* hudScaleRaw) {
    if (!m_pItems) return;

    const Vec3& hudScale = *reinterpret_cast<const Vec3*>(hudScaleRaw);
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };

    size_t n = m_pItems->size();
    std::string* topIter = m_pItems->empty() ? NULL : &(*m_pItems)[0];  // begin()
    if (n > (size_t)m_VisibleRows && m_pScroller) {
        topIter += m_pScroller->m_CurrentValue;                          // scroll offset
    }

    MatrixManager& mm = MatrixManager::GetInstance();

    float rowY = pos.y + m_CellHeight;
    for (uint32_t i = 0; i < n && i < (uint32_t)m_VisibleRows; ++i) {
        rowY -= m_CellHeight;

        // --- row background quad (s_bar), tinted by row state ---
        Colour rowColour;
        if (topIter == m_HoverIt) {
            rowColour = Colour(0x50, 0x96, 0xFF, 0xFF);        // hover
        } else if (topIter == m_TopVisibleIt) {
            rowColour = Colour(0, 0, 255, 255);                // Colour::Blue
        } else {
            rowColour = Colour::White;
        }

        if (s_bar.IsValid()) {
            mm.GetWorldStack().Reset();
            s_bar->Set();
            Matrix44 mat = Matrix44::MakeScale(m_CellWidth, m_CellHeight, 1.0f);
            mat.GlobalTranslate44(Vec3(pos.x, rowY, pos.z));
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(rowColour, NULL);
            s_bar->UnSet();
        }

        // --- row text ---
        if (m_pTextFont && topIter) {
            Mortar::Utf8StringIterator it(topIter->c_str());
            Colour txtCol = Colour::TintColour(m_TextColour, tintRGB);
            float textX = pos.x + m_CellWidth * -0.5f + size.x * 5.0f;
            float textY = rowY + size.y * 7.0f;
            float fontSize = (float)m_CellHeightParam * size.x;
            m_pTextFont->DrawString(it, textX, textY, 0.0f, txtCol, fontSize,
                                    0.0f, 0.0f, 1, NULL, 0.0f);
        }

        ++topIter;
    }
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- Binary @ 0x00194298
// ASM-verified: 2026-07-11 v1.6.1 ListBox::Update @ 0x00194298 (re-analyst)
//   Hover: when the live world touch (game_work.worldPos, +0x94/+0x98) is inside
//   the row band, m_HoverIt = begin() + scrollOfs + clamp((top - worldPos.y) / rowH, 0).
//   On tap-release inside a row, m_TopVisibleIt = begin() + scrollOfs + that row
//   and m_OnSelect() fires (Delegate0 @ +0xA4).
//   scrollOfs = m_pScroller ? m_pScroller->m_CurrentValue : 0 -- the port
//   previously omitted this add, so a scrolled list always committed/hovered the
//   row at the TOP of the visible window instead of the one under the finger.
void ListBox::Update(float dt) {
    (void)dt;
    if (!m_pItems) return;

    float rowH   = m_CellHeight;
    float left   = pos.x + m_CellWidth * -0.5f;
    float right  = pos.x + m_CellWidth *  0.5f;
    float top    = pos.y + rowH * 0.5f;

    size_t n = m_pItems->size();
    uint32_t rows = (n <= (size_t)m_VisibleRows) ? (uint32_t)n : m_VisibleRows;
    float bottom = (pos.y - rowH * 0.5f) - rowH * (float)(rows > 0 ? rows - 1 : 0);

    std::string* base = m_pItems->empty() ? NULL : &(*m_pItems)[0];
    int scrollOfs = (n > (size_t)m_VisibleRows && m_pScroller) ? m_pScroller->m_CurrentValue : 0;

    // Hover: index the row under the live world touch position.
    const Vec3& wp = game_work.worldPos;
    if (wp.x >= left && wp.x <= right && wp.y >= bottom && wp.y <= top && base) {
        int idx = (int)((top - wp.y) / rowH) + scrollOfs;
        if (idx < 0) idx = 0;
        m_HoverIt = base + idx;
    }

    if (m_TouchX < 0.0f) {
        // Acquire a slot pressed inside the list body.
        int slot = TouchInRegion(left, right, bottom, top, -1);
        m_TouchX = (float)slot;
        if (slot != -1) {
            if (IsTouchDown(slot) == 2) {
                m_TouchY = wp.y;
                return;
            }
            m_TouchX = -1.0f;
            return;
        }
    } else {
        int slot = (int)m_TouchX;
        if (IsTouchDown(slot) == 0) {
            m_TouchX = -1.0f;
            // Commit only if the release lands inside a valid row band.
            if (m_TouchY < bottom || m_TouchY > top || !base) return;
            int idx = (int)((top - m_TouchY) / rowH) + scrollOfs;
            if (idx < 0) idx = 0;
            m_TopVisibleIt = base + idx;
            m_OnSelect();
            return;
        }
    }
    UpdateTouchPosition();
}

// ---------------------------------------------------------------------------
// vtable slot 12 -- Binary @ 0x001954a8 (returns 5)
int ListBox::GetType() {
    return 5;
}

// Non-virtual -- Binary @ 0x00169104: returns the committed selection.
std::string* ListBox::GetSelected() {
    return m_TopVisibleIt;
}

// Non-virtual -- Binary @ 0x001691b4: assigns the commit callback.
void ListBox::SetCallback(const Mortar::Delegate0<void>& cb) {
    m_OnSelect = cb;
}

// Non-virtual, port-side. No binary counterpart -- see header note.
void ListBox::SetFont(Mortar::Font* font) {
    m_pTextFont = font;
}

// Private helper, reached via a PLT veneer in Update. Captures the live world
// touch Y each held frame.
void ListBox::UpdateTouchPosition() {
    m_TouchY = game_work.worldPos.y;
}

// ---------------------------------------------------------------------------
// Static -- Binary @ 0x00194fdc
// ASM-spec v1.6.1 ListBox::LoadContent @0x00194fdc: LoadLocalisedTexture
//   box.tex -> s_bar.
// box.tex DOES ship in v1.6.1 (unlike the VerticalScroller art).
void ListBox::LoadContent() {
    s_bar = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
}

void ListBox::UnloadContent() {
    s_bar.SetNull();
}

// Port/test-only injector (no binary counterpart) -- box.tex ships; see header note.
void ListBox::SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& bar) {
    s_bar = bar;
}

// Binary empty bx lr.
// Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple -- no-op stub.
void ListBox::UpdateFromGameWork() {}
