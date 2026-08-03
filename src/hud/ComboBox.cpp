//
// ComboBox : HUDControl3d
// v1.6.1 ComboBox @ 0x001682d4..0x001690fc
//
// Collapsed-bar half of the dead-code dropdown widget stack. Complete faithful
// implementation. Field offsets re-verified against the ctor @0x001682d4; the
// composition (ComboBox creates the ListBox on tap) is ported exactly.
//

#include "ComboBox.h"
#include "ListBox.h"
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
#include "engine/util/Delegate.h"
#include "game/GameWork.h"
#include <cstdint>

// Collapsed-bar textures (binary: SmartPtr<Texture> GOT slots).
static Mortar::SmartPtr<Mortar::Texture> s_bar;          // box.tex (shared with ListBox/SliderControl)
static Mortar::SmartPtr<Mortar::Texture> s_expandArrow;  // expand_arrow.tex

// ---------------------------------------------------------------------------
// Constructor -- Binary @ 0x001682d4
// ASM-spec v1.6.1 ComboBox::ComboBox @0x001682d4: field writes
//   m_pItems(+0x7C)=&items, m_SelectedIter(+0x80)=items.begin()+defaultIdx,
//   m_TextFlag(+0x84), m_pComboLabel(+0x88), m_TextScaleX(+0x8C), m_TextScaleY(+0x8E),
//   m_Width(+0x90), m_pFont(+0x9C)=fonts[1], m_TouchIndex(+0xAC)=-1, m_pListBox(+0xA4)=0,
//   m_bCleanupPending(+0xA8)=0, m_LayerFlags(+0x34)=0x80,
//   m_DrawWidth(+0x94)=m_TextScaleX*size.x, m_DrawHeight(+0x98)=m_TextScaleY*size.y.
ComboBox::ComboBox(_Vector3<float> inPos, _Vector3<float> inSize, std::vector<std::string>& items,
                   uint16_t defaultIdx, const char* comboLabel, uint8_t textFlag,
                   uint16_t width, uint16_t textScaleX, uint16_t textScaleY)
    : HUDControl3d()
    , m_pItems(&items)
    , m_SelectedIter(items.empty() ? NULL : (&items[0] + defaultIdx))  // begin()+idx
    , m_TextFlag(textFlag)
    , m_pComboLabel(comboLabel)
    , m_TextScaleX(textScaleX)
    , m_TextScaleY(textScaleY)
    , m_Width(width)
    , _pad92(0)
    , m_DrawWidth(0.0f)
    , m_DrawHeight(0.0f)
    , m_pFont(game_work.pFontMain.Get())                               // fonts[1]
    , m_TextColour()                                                   // opaque BLACK (Colour::Colour() @0x0011afa8, ASM-confirmed) -- see header
    , m_pListBox(NULL)
    , m_bCleanupPending(0)
    , m_TouchIndex(-1)
    , m_TouchPos(0.0f, 0.0f, 0.0f)
    , m_ListSelectedRowColour(0, 0, 255, 255)   // unused until m_bListSelectedRowColourSet
    , m_ListHoverRowColour(0x50, 0x96, 0xFF, 0xFF)  // unused until m_bListHoverRowColourSet
    , m_ListTextColour()
    , m_bListSelectedRowColourSet(false)
    , m_bListHoverRowColourSet(false)
    , m_bListTextColourSet(false)
{
    _pad85[0] = _pad85[1] = _pad85[2] = 0;
    _padA9[0] = _padA9[1] = _padA9[2] = 0;
    pos  = inPos;
    size = inSize;

    m_DrawWidth  = (float)textScaleX * size.x;   // +0x94
    m_DrawHeight = (float)textScaleY * size.y;   // +0x98

    m_LayerFlags = Mortar::HUD_LAYER_POST_ACTOR;  // +0x34 = 0x80
}

ComboBox::~ComboBox() {
    Release();
}

// vtable slot 2 -- Binary @ 0x00167ddc
void ComboBox::Init() {
}

// vtable slot 3 -- Binary @ 0x001681b8
// ASM-spec v1.6.1 ComboBox::Release @0x001681b8: bl CleanUpListBox; m_pFont(+0x9c)=NULL.
// Does NOT tail-call HUDControl3d::Release -- port previously did, which skipped
// the listbox/scroller teardown entirely when a ComboBox died while open.
void ComboBox::Release() {
    CleanUpListBox();
    m_pFont = NULL;
}

// vtable slot 6 -- Binary @ 0x00167e40
void ComboBox::PreDraw(float* hudScale) {
    (void)hudScale;
}

// ---------------------------------------------------------------------------
// vtable slot 7 -- Binary @ 0x001687f4
// ASM-spec v1.6.1 ComboBox::Draw @0x001687f4: draws
//   1. combo label (m_pComboLabel, Yellow tinted by hudScale) at
//      (pos.x + m_DrawWidth*0.5 + size.x*25, pos.y + size.y*7), font size m_Width*size.x
//   2. bar quad (s_bar, White) scale (m_DrawWidth, m_DrawHeight) at pos
//   3. expand arrow (s_expandArrow, White) scale (arrowW*size.x, m_DrawHeight) at
//      (pos.x + arrowW*size.x*0.5 + m_DrawWidth*0.5, pos.y) -- binary formula;
//      the port additionally pulls this left by a small overlap (see the
//      kBoxTexRightMarginFrac Port specific note below) to close a visible
//      gap caused by box.tex's/expand_arrow.tex's own asset margins, not by
//      this placement formula.
//   4. selected-item label (m_SelectedIter, m_TextColour tinted) at
//      (pos.x - m_DrawWidth*0.5 + size.x*5, pos.y + size.y*7), font size m_Width*size.x
void ComboBox::Draw(float* hudScaleRaw) {
    const _Vector3<float>& hudScale = *reinterpret_cast<const _Vector3<float>*>(hudScaleRaw);
    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    const float fontSize = (float)m_Width * size.x;

    MatrixManager& mm = MatrixManager::GetInstance();

    // --- 1. combo header label (Yellow) ---
    if (m_pFont && m_pComboLabel) {
        Mortar::Utf8StringIterator it(m_pComboLabel);
        // Colour::Yellow = (255,255,0,255).
        Colour col = Colour::TintColour(Colour(255, 255, 0, 255), tintRGB);
        float x = pos.x + m_DrawWidth * 0.5f + size.x * 25.0f;
        float y = pos.y + size.y * 7.0f;
        m_pFont->DrawString(it, x, y, 0.0f, col, fontSize, 0.0f, 0.0f, 1, NULL, 0.0f);
    }

    // --- 2. collapsed bar (s_bar) ---
    if (s_bar.IsValid()) {
        mm.GetWorldStack().Reset();
        s_bar->Set();
        Matrix44 mat = Matrix44::MakeScale(m_DrawWidth, m_DrawHeight, 1.0f);
        mat.GlobalTranslate44(pos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);
        s_bar->UnSet();
    }

    // --- 3. expand arrow (s_expandArrow) ---
    if (s_expandArrow.IsValid()) {
        float arrowW = (float)s_expandArrow->GetWidth();
        mm.GetWorldStack().Reset();
        s_expandArrow->Set();
        Matrix44 mat = Matrix44::MakeScale(arrowW * size.x, m_DrawHeight, 1.0f);
        _Vector3<float> p = pos;
        // Port specific: box.tex (see box.svg) insets its opaque wood rim
        // 2px inside its 64px-wide canvas, so the bar quad's own RIGHT edge
        // (at pos.x + m_DrawWidth*0.5) is ~3% transparent margin, not visible
        // wood -- edge-adjacent placement (the binary's actual formula, kept
        // above) reads as a gap between the value box and the caret cell.
        // expand_arrow.tex's flat-left path starts at canvas x=0 (no margin
        // of its own), so pull the caret cell left by that same fraction of
        // the bar's width to butt its flat edge against the bar's real
        // (visible) right edge; a touch more (kCaretOverlapPad) folds the
        // caret's rounded-right-corner-vs-box's-square-corner mismatch under
        // the caret so no wood-rim notch peeks out on the left.
        static const float kBoxTexRightMarginFrac = 2.0f / 64.0f;
        static const float kCaretOverlapPad = 1.0f;
        float overlap = m_DrawWidth * kBoxTexRightMarginFrac + kCaretOverlapPad * size.x;
        p.x = p.x + arrowW * size.x * 0.5f + m_DrawWidth * 0.5f - overlap;
        mat.GlobalTranslate44(p);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);
        s_expandArrow->UnSet();
    }

    // --- 4. selected-item label (m_TextColour) ---
    if (m_pFont && m_SelectedIter) {
        Mortar::Utf8StringIterator it(m_SelectedIter->c_str());
        Colour col = Colour::TintColour(m_TextColour, tintRGB);
        float x = pos.x + m_DrawWidth * -0.5f + size.x * 5.0f;
        float y = pos.y + size.y * 7.0f;
        m_pFont->DrawString(it, x, y, 0.0f, col, fontSize, 0.0f, 0.0f, 1, NULL, 0.0f);
    }
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- Binary @ 0x00167f70
// ASM-spec v1.6.1 ComboBox::Update @0x00167f70: pending-cleanup drain, then
//   hit-test the bar+arrow rect. On a fresh press (IsTouchDown==2) with no open
//   ListBox: create operator new(0xDC) ListBox just below the bar
//   (pos.y - m_DrawHeight - 1), register ComboBox::ListBoxClosed as its commit
//   callback, and HUD::AddControl it. If a ListBox is already open, CleanUpListBox.
void ComboBox::Update(float dt) {
    (void)dt;

    if (m_bCleanupPending != 0) {
        CleanUpListBox();
        m_bCleanupPending = 0;
    }

    if (m_TouchIndex == -1) {
        float arrowW = s_expandArrow.IsValid() ? (float)s_expandArrow->GetWidth() : 0.0f;
        float left   = pos.x + m_DrawWidth * -0.5f;
        float right  = pos.x + m_DrawWidth *  0.5f + arrowW * size.x;
        float bottom = pos.y + m_DrawHeight * -0.5f;
        float top    = pos.y + m_DrawHeight *  0.5f;

        m_TouchIndex = TouchInRegion(left, right, bottom, top, -1);
        if (m_TouchIndex != -1) {
            if (IsTouchDown(m_TouchIndex) != 2) {
                m_TouchIndex = -1;
                return;
            }
            if (m_pListBox == NULL) {
                _Vector3<float> lbPos(pos.x, (pos.y - m_DrawHeight) - 1.0f, -1.0f);
                _Vector3<float> lbSize = size;
                // ListBox args: selIter=m_SelectedIter, visibleRows=m_TextFlag,
                // cellHeightParam=14, cellWidthParam=128, fontScaleParam=16.
                m_pListBox = new ListBox(lbPos, lbSize, *m_pItems, m_SelectedIter,
                                         m_TextFlag, 14, 128, 16);
                m_pListBox->Init();
                // DIFFERS: v1.6.1 ComboBox::Update @0x00167f70 leaves the ListBox
                // on fonts[1]; the port propagates the combo's own font so a TTF
                // combo (e.g. the language selector's native CJK/Cyrillic names)
                // renders in the dropdown rows too.
                if (m_pFont) {
                    m_pListBox->SetFont(m_pFont);
                }
                // Port specific: no binary counterpart -- propagate any cached
                // row-tint theme (see SetListSelectedRowColour/etc.) onto the
                // freshly-created ListBox.
                if (m_bListSelectedRowColourSet) {
                    m_pListBox->SetSelectedRowColour(m_ListSelectedRowColour);
                }
                if (m_bListHoverRowColourSet) {
                    m_pListBox->SetHoverRowColour(m_ListHoverRowColour);
                }
                if (m_bListTextColourSet) {
                    m_pListBox->SetTextColour(m_ListTextColour);
                }
                Mortar::Delegate0<void> cb =
                    Mortar::Delegate0<void>::QCallee<ComboBox>(this, &ComboBox::ListBoxClosed);
                m_pListBox->SetCallback(cb);
                // v1.6.1 ComboBox::Update @0x00167f70 tail: HUD::AddControl(game_work.mHud,
                // m_pListBox, false) -- no null test on the GOT-resolved game_work.mHud.
                game_work.mHud->AddControl(m_pListBox, false);
                return;
            }
            CleanUpListBox();
            return;
        }
    } else {
        if (IsTouchDown(m_TouchIndex) == 0) {
            m_TouchIndex = -1;
            return;
        }
    }
    UpdateTouchPosition();
}

// ---------------------------------------------------------------------------
// vtable slot 12 -- Binary @ 0x001690fc (returns 5)
int ComboBox::GetType() {
    return 5;
}

// ---------------------------------------------------------------------------
// Non-virtual -- Binary @ 0x00167de0
// Copies the ListBox's committed selection back into m_SelectedIter and flags
// the ListBox for teardown next Update. Registered as the ListBox commit callback.
void ComboBox::ListBoxClosed() {
    m_SelectedIter = m_pListBox->GetSelected();
    m_bCleanupPending = 1;
}

// Non-virtual setters.
void ComboBox::SetTextColour(Colour c) {
    m_TextColour = c;
}

void ComboBox::SetFont(Mortar::Font* font) {
    m_pFont = font;
}

void ComboBox::SetPosition(float x, float y) {
    pos.x = x;
    pos.y = y;
}

// Port specific: no binary counterpart -- see header. Cache + apply
// immediately if a ListBox is already open; Update() also applies the cache
// when it creates a ListBox later.
void ComboBox::SetListSelectedRowColour(Colour c) {
    m_ListSelectedRowColour = c;
    m_bListSelectedRowColourSet = true;
    if (m_pListBox) {
        m_pListBox->SetSelectedRowColour(c);
    }
}

void ComboBox::SetListHoverRowColour(Colour c) {
    m_ListHoverRowColour = c;
    m_bListHoverRowColourSet = true;
    if (m_pListBox) {
        m_pListBox->SetHoverRowColour(c);
    }
}

void ComboBox::SetListTextColour(Colour c) {
    m_ListTextColour = c;
    m_bListTextColourSet = true;
    if (m_pListBox) {
        m_pListBox->SetTextColour(c);
    }
}

// Tears the open ListBox down: removes it from the HUD control list
// IMMEDIATELY (not a deferred SetPendingRemoval flag) and destroys it inline,
// then clears the pointer.
// ASM-verified: 2026-07-11 v1.6.1 ComboBox::CleanUpListBox @ 0x00167f10 (re-analyst)
//   if (m_pListBox) { HUD::RemoveControl(hud, m_pListBox);   // immediate list erase
//                      m_pListBox->vtable[1](); }             // deleting dtor -> ListBox::Release
//                                                              //   (which in turn tears down the
//                                                              //   scroller -- see ListBox::Release)
//   m_pListBox = NULL;
// The port previously called SetPendingRemoval() (a deferred flag consumed by
// HUD::Update's own sweep), which orphaned the ListBox's VerticalScroller: the
// scroller is a SEPARATE HUD control that only ListBox::Release tears down, and
// that dtor never ran because nothing here (or the deferred sweep) triggered it
// in the same frame the flag was set -- this was the (b) never-closes bug.
void ComboBox::CleanUpListBox() {
    // v1.6.1 ComboBox::CleanUpListBox @0x00167f10: the only test is `m_pListBox != 0`;
    // HUD::RemoveControl(game_work.mHud, m_pListBox) then runs unguarded.
    if (m_pListBox) {
        game_work.mHud->RemoveControl(m_pListBox);
        delete m_pListBox;
        m_pListBox = NULL;
    }
}

// Captures the finger position each held frame.
// ASM-spec v1.6.1 ComboBox::UpdateTouchPosition (via PLT @0x00115e28):
//   reconstructed capture (no read site in the collapsed path).
void ComboBox::UpdateTouchPosition() {
    if (m_TouchIndex != -1 && m_TouchIndex < 16) {
        m_TouchPos = game_work.m_FingerSpawnPos[m_TouchIndex];
    }
}

// ---------------------------------------------------------------------------
// Static -- Binary @ 0x00168b3c
// ASM-spec v1.6.1 ComboBox::LoadContent @0x00168b3c: LoadLocalisedTexture
//   box.tex -> s_bar, expand_arrow.tex -> s_expandArrow.
// box.tex is the SAME shared texture ListBox (@0x00194fdc) and SliderControl
//   (@0x001b7bc0) load for their own bar/row/track art -- not a ComboBox-only
//   asset (Ghidra-confirmed string reference at each LoadContent call site).
// DIFFERS: box.tex ships in v1.6.1, but expand_arrow.tex does NOT (dropdown
//   stack is dead code, so its dedicated art was dropped). The faithful name
//   is kept; s_expandArrow stays null when absent and Draw no-ops the arrow.
//   The visual test injects a substitute via SetTexturesForTest.
void ComboBox::LoadContent() {
    s_bar         = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
    s_expandArrow = Mortar::TextureManager::LoadLocalisedTexture("expand_arrow.tex");
}

void ComboBox::UnloadContent() {
    s_bar.SetNull();
    s_expandArrow.SetNull();
}

// Port/test-only injector (no binary counterpart) -- box.tex ships, but
// expand_arrow.tex does not -- see header note.
void ComboBox::SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& bar,
                                  const Mortar::SmartPtr<Mortar::Texture>& arrow) {
    s_bar         = bar;
    s_expandArrow = arrow;
}

// Binary empty bx lr.
// Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple -- no-op stub.
void ComboBox::UpdateFromGameWork() {}
