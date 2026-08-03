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
ListBox::ListBox(_Vector3<float> inPos, _Vector3<float> inSize, std::vector<std::string>& items,
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
    // Port specific: the binary ctor writes only m_ActiveTouchId(+0xC8) = -1 and
    // leaves the +0xCC..+0xD7 finger record indeterminate. Zeroing is safe --
    // Update only reads it while a slot is held, and UpdateTouchPosition
    // overwrites the whole 12-byte block on every held frame.
    , m_TouchX(0.0f)
    , m_TouchY(0.0f)
    , m_TouchPhase(0.0f)
    , m_VisibleRows(visibleRows)
    , m_SelectedRowColour(0, 0, 255, 255)                             // binary literal default; see header
    , m_HoverRowColour(0x50, 0x96, 0xFF, 0xFF)                        // binary literal default; see header
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

        _Vector3<float> scrollerPos(pos.x + m_CellWidth * 0.5f,
                                    pos.y + (float)(visibleRows - 1) * m_CellHeight * -0.5f,
                                    -1.0f);
        m_pScroller = new VerticalScroller(scrollerPos, size,
                                           /*min*/ 0, scrollRange, /*step*/ 1,
                                           initScroll, /*reverse*/ true,
                                           visibleRows, /*visibleHeight*/ 0,
                                           (uint16_t)(visibleRows * 16));
        m_pScroller->AdjustByWidth();
        m_pScroller->Init();
        // v1.6.1 ListBox::ListBox @0x00194a74: HUD::AddControl(game_work.mHud, ...) unguarded.
        game_work.mHud->AddControl(m_pScroller, false);
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
        game_work.mHud->RemoveControl(m_pScroller);
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
//   (pos.x, rowY)) tinted: hover row (== m_HoverIt) m_HoverRowColour (binary literal
//   default RGB(0x50,0x96,0xFF)); committed row (== m_TopVisibleIt) m_SelectedRowColour
//   (binary literal default Blue); else White. m_SelectedRowColour/m_HoverRowColour are
//   port-specific settable overrides -- see header. Row text (m_TextColour, tinted by
//   hudScale) at (pos.x - m_CellWidth*0.5 + size.x*5, rowY + size.y*7), font size
//   = m_CellHeightParam * size.x.
void ListBox::Draw(float* hudScaleRaw) {
    if (!m_pItems) return;

    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);
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
            rowColour = m_HoverRowColour;                      // Port specific: settable (default = binary hover literal)
        } else if (topIter == m_TopVisibleIt) {
            rowColour = m_SelectedRowColour;                   // Port specific: settable (default = binary Colour::Blue)
        } else {
            rowColour = Colour::White;
        }

        if (s_bar.IsValid()) {
            mm.GetWorldStack().Reset();
            s_bar->Set();
            Matrix44 mat = Matrix44::MakeScale(m_CellWidth, m_CellHeight, 1.0f);
            mat.GlobalTranslate44(_Vector3<float>(pos.x, rowY, pos.z));
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
// Row index under a given Y, as the binary computes it three times inline in
// Update (@0x00194360, @0x00194420, @0x001944b8):
//   vdiv (top - y) / rowH -> vcvt.u32.f32 -> uxth -> (+ scroller->m_CurrentValue -> uxth)
// vcvt.u32.f32 rounds toward zero and SATURATES, so a y above `top` yields
// row 0 rather than a negative index; uxth then keeps the low 16 bits.
static uint32_t ListBoxRowIndex(float top, float y, float rowH, const VerticalScroller* scroller) {
    float f = (top - y) / rowH;
    uint32_t idx;
    if (!(f > 0.0f))                 idx = 0u;               // saturate negatives / NaN to 0
    else if (f >= 4294967296.0f)     idx = 0xFFFFFFFFu;      // saturate overflow
    else                             idx = (uint32_t)f;
    idx &= 0xFFFFu;                                          // uxth
    if (scroller) {
        idx = (idx + (uint32_t)scroller->m_CurrentValue) & 0xFFFFu;
    }
    return idx;
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- Binary @ 0x00194298
// ASM-spec v1.6.1 ListBox::Update @ 0x00194298:
//   left/right = pos.x -/+ m_CellWidth*0.5, top = pos.y + rowH*0.5,
//   bottom = (pos.y - rowH*0.5) - rowH * (float)(unsigned)(rows - 1),
//   rows = min(items.size(), m_VisibleRows).
//   1. Live hover: when game_work.worldPos (+0x94/+0x98) is inside the body,
//      m_HoverIt = begin() + rowIndex(worldPos.y).
//   2. Slot machine, keyed on the INT m_ActiveTouchId (+0xC8):
//      -1 -> TouchInRegion(left,right,bottom,top,-1); store the result; if it
//            is -1 fall through to UpdateTouchPosition, else keep the slot only
//            when IsTouchDown == 2 and return WITHOUT capturing (the capture
//            happens on the following held frames).
//      held -> re-hover from the CAPTURED finger pos (m_TouchX/m_TouchY) when
//            it is inside the body, then: still down -> UpdateTouchPosition;
//            released -> drop the slot and commit m_TopVisibleIt + m_OnSelect()
//            only when the captured pos passes BOTH the x and the y range test.
//   The x test on release (m_TouchX vs left/right, @0x0019447c/@0x00194490) was
//   missing while an earlier port pass overloaded m_TouchX as the slot index.
void ListBox::Update(float dt) {
    (void)dt;
    // Port specific: the binary holds m_pItems as a reference and never null-checks it.
    if (!m_pItems) return;

    const float rowH   = m_CellHeight;
    const float left   = pos.x + m_CellWidth * -0.5f;
    const float right  = pos.x + m_CellWidth *  0.5f;
    const float top    = pos.y + rowH * 0.5f;

    const size_t n = m_pItems->size();
    const uint32_t rows = (n > (size_t)m_VisibleRows) ? (uint32_t)m_VisibleRows : (uint32_t)n;
    // (rows - 1) is converted UNSIGNED (vcvt.f32.u32 @0x00194320) -- an empty
    // list wraps to 0xFFFFFFFF and pushes `bottom` far below the screen, exactly
    // as the binary does. Harmless: `base` is then null and nothing commits.
    const float bottom = (pos.y + rowH * -0.5f) - rowH * (float)(uint32_t)(rows - 1u);

    std::string* base = m_pItems->empty() ? NULL : &(*m_pItems)[0];   // begin()

    // 1. Live hover from the global pointer position.
    const _Vector3<float>& wp = game_work.worldPos;
    if (wp.x >= left && wp.x <= right && wp.y >= bottom && wp.y <= top && base) {
        m_HoverIt = base + ListBoxRowIndex(top, wp.y, rowH, m_pScroller);
    }

    // 2. Touch slot machine.
    if (m_ActiveTouchId == -1) {
        m_ActiveTouchId = TouchInRegion(left, right, bottom, top, -1);
        if (m_ActiveTouchId != -1) {
            // Keep the slot only on the press edge; no capture on this frame.
            if (IsTouchDown(m_ActiveTouchId) != 2) m_ActiveTouchId = -1;
            return;
        }
        // No slot -- falls through to UpdateTouchPosition (a no-op at id == -1).
    } else {
        // Re-hover from the captured finger position.
        if (m_TouchX >= left && m_TouchX <= right &&
            m_TouchY >= bottom && m_TouchY <= top && base) {
            m_HoverIt = base + ListBoxRowIndex(top, m_TouchY, rowH, m_pScroller);
        }
        if (IsTouchDown(m_ActiveTouchId) == 0) {
            m_ActiveTouchId = -1;
            // Commit only when the captured release position is inside the body.
            if (m_TouchX < left   || m_TouchX > right) return;
            if (m_TouchY < bottom || m_TouchY > top)   return;
            if (!base) return;
            m_TopVisibleIt = base + ListBoxRowIndex(top, m_TouchY, rowH, m_pScroller);
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

// ASM-spec v1.6.1 ListBox::UpdateTouchPosition @0x001941bc (PLT veneer 0x00111a00):
//   if (m_ActiveTouchId == -1) return;
//   ldmia/stmia one 12-byte block: game_work.m_FingerSpawnPos[m_ActiveTouchId]
//   (GameWork+0xA4, stride 12) -> this+0xCC..+0xD7 (x, y, phase).
// An earlier port pass captured game_work.worldPos.y instead, so the x half of
// the release hit-test had no value to test against.
void ListBox::UpdateTouchPosition() {
    if (m_ActiveTouchId == -1) return;
    const _Vector3<float>& finger = game_work.m_FingerSpawnPos[m_ActiveTouchId];
    m_TouchX     = finger.x;
    m_TouchY     = finger.y;
    m_TouchPhase = finger.z;
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
