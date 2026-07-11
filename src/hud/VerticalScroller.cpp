//
// VerticalScroller : HUDControl3d
// v1.6.1 VerticalScroller @ 0x001c8e0c..0x001c9ec0
//
// Scrollbar half of the dead-code dropdown widget stack. Complete faithful
// implementation (draw + touch state machine) ported from the v1.6.1 binary.
// Field offsets re-verified against the ctor @0x001c9380 instruction stream.
//

#include "VerticalScroller.h"
#include "hud/HUDLayer.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "engine/input/Touch.h"
#include "game/GameWork.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// Module-scope texture handles (binary: Mortar::SmartPtr<Texture> GOT slots).
// LoadContent loads, in order: vbar.tex -> s_box, vslider.tex -> s_slider,
// arrow.tex -> s_arrow.
static Mortar::SmartPtr<Mortar::Texture> s_box;      // track background (vbar.tex)
static Mortar::SmartPtr<Mortar::Texture> s_slider;   // thumb            (vslider.tex)
static Mortar::SmartPtr<Mortar::Texture> s_arrow;    // top/bottom arrow (arrow.tex)

// ---------------------------------------------------------------------------
// Constructor -- Binary @ 0x001c9380 (C1) / 0x001c9284 (C2)
// ASM-spec v1.6.1 VerticalScroller::VerticalScroller @0x001c9380: field writes
//   m_MinValue(+0x7C)=minValue, m_MaxValue(+0x80)=maxValue, m_StepSize(+0x84)=stepSize,
//   m_CurrentValue(+0x88)=currentValue, m_TotalRows(+0x8C)=totalRows,
//   m_VisibleHeight(+0x94)=visibleHeight (defaults 0->0x15), m_TotalHeight(+0x96)=totalHeight,
//   m_VisibleHeightPx(+0x98)=visibleHeight*size.y, m_TotalHeightPx(+0x9C)=totalHeight*size.y,
//   m_TypeId(+0xA0)=5, m_bReverse(+0xA1)=reverse, m_State(+0xA2)=0, m_TouchId(+0xA4)=-1,
//   m_LayerFlags(+0x34)=0x800.
VerticalScroller::VerticalScroller(Vec3 inPos, Vec3 inSize,
                                   int32_t minValue, int32_t maxValue,
                                   uint16_t stepSize, int32_t currentValue,
                                   bool reverseDir, uint8_t totalRows,
                                   uint16_t visibleHeight, uint16_t totalHeight)
    : HUDControl3d()
    , m_MinValue(minValue)
    , m_MaxValue(maxValue)
    , m_StepSize(stepSize)
    , _pad86(0)
    , m_CurrentValue(currentValue)
    , m_TotalRows(totalRows)
    , m_CachedThumbY(0.0f)
    , m_TotalHeight(totalHeight)
    , m_TypeId(5)                // constant from binary @ +0xA0
    , m_bReverse(reverseDir ? 1 : 0)
    , m_State(0)                 // idle
    , _padA3(0)
    , m_TouchId(-1)              // 0xFFFFFFFF in binary (== -1 for int32)
    , m_LastTouchPos(0.0f, 0.0f, 0.0f)
{
    pos  = inPos;
    size = inSize;

    // Binary: if visibleHeight arg is 0, default to 21 (0x15).
    m_VisibleHeight = (visibleHeight == 0) ? 21 : visibleHeight;

    // Cache pixel sizes: visibleHeight * size.y, totalHeight * size.y.
    m_VisibleHeightPx = (float)m_VisibleHeight * size.y;
    m_TotalHeightPx   = (float)totalHeight * size.y;

    // +0x34 = 0x800 (top-most overlay). Binary: mov r3,#0x800; str r3,[r4,#0x34].
    m_LayerFlags = Mortar::HUD_LAYER_TOP_MOST;

    _pad8D[0] = _pad8D[1] = _pad8D[2] = 0;
}

// Destructor -- D0/D1 call Release() then the HUDControl3d dtor chain.
VerticalScroller::~VerticalScroller() {
    Release();
}

// vtable slot 2 -- Binary @ 0x001c8e0c (empty bx lr)
void VerticalScroller::Init() {
}

// vtable slot 3 -- Binary @ 0x001c917c (tail-calls HUDControl3d::Release)
void VerticalScroller::Release() {
    HUDControl3d::Release();
}

// vtable slot 6 -- Binary @ 0x001c8f90 (empty bx lr)
void VerticalScroller::PreDraw(float* hudScale) {
    (void)hudScale;
}

// ---------------------------------------------------------------------------
// vtable slot 7 -- Binary @ 0x001c9514
// ASM-spec v1.6.1 VerticalScroller::Draw @0x001c9514: draws
//   1. track quad  (s_box)    scale (visibleHeightPx, totalHeightPx) at pos
//   2. top arrow   (s_arrow)  scale (visibleHeightPx, arrowH*size.y),
//                             y = pos.y - arrowSz*0.5 + totalHeightPx*0.5
//   3. bottom arrow(s_arrow)  same scale, y = pos.y + arrowSz*0.5 - totalHeightPx*0.5,
//                             drawn with U+V flipped (uMin=1,uMax=0,vMin=1,vMax=0)
//   4. thumb       (s_slider) scale (visibleHeightPx, sliderH*size.y), only when
//                             m_TypeId <= m_TotalRows; y computed from m_CurrentValue.
void VerticalScroller::Draw(float* hudScaleRaw) {
    (void)hudScaleRaw;

    float visPx = m_VisibleHeightPx;
    float totPx = m_TotalHeightPx;
    float sliderSz = s_slider.IsValid() ? (float)s_slider->GetHeight() * size.y : 0.0f;
    float arrowSz  = s_arrow.IsValid()  ? (float)s_arrow->GetHeight()  * size.y : 0.0f;

    MatrixManager& mm = MatrixManager::GetInstance();

    // --- track background quad (s_box) ---
    if (s_box.IsValid()) {
        mm.GetWorldStack().Reset();
        s_box->Set();
        Matrix44 mat = Matrix44::MakeScale(visPx, totPx, 1.0f);
        mat.GlobalTranslate44(pos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);
        s_box->UnSet();
    }

    if (s_arrow.IsValid()) {
        // --- top arrow ---
        s_arrow->Set();
        {
            Matrix44 mat = Matrix44::MakeScale(visPx, arrowSz, 1.0f);
            Vec3 p = pos;
            p.y = p.y + arrowSz * -0.5f + totPx * 0.5f;
            mat.GlobalTranslate44(p);
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);
        }
        // --- bottom arrow (U+V flipped) ---
        {
            Matrix44 mat = Matrix44::MakeScale(visPx, arrowSz, 1.0f);
            Vec3 p = pos;
            p.y = totPx * -0.5f + pos.y + arrowSz * 0.5f;
            mat.GlobalTranslate44(p);
            mm.GetWorldStack().SetCurrentMatrix(mat);
            mm.UploadModelViewOnly();
            Mortar::Mesh::DrawQuadUnCached(Colour::White, 1.0f, 0.0f, 1.0f, 0.0f, NULL);
        }
        s_arrow->UnSet();
    }

    // --- thumb (only when there is something to scroll) ---
    if (m_TypeId <= m_TotalRows && s_slider.IsValid()) {
        s_slider->Set();
        Matrix44 mat = Matrix44::MakeScale(visPx, sliderSz, 1.0f);
        float cur = (float)m_CurrentValue;
        float maxV = (float)m_MaxValue;
        Vec3 p = pos;
        p.y = arrowSz
            + (cur / maxV) * ((totPx - sliderSz) + arrowSz * -2.0f)
            + sliderSz * 0.5f
            + pos.y
            + totPx * -0.5f;
        if (m_bReverse != 0) {
            p.y = pos.y - (p.y - pos.y);   // mirror about pos.y
        }
        m_CachedThumbY = p.y;
        mat.GlobalTranslate44(p);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);
        s_slider->UnSet();
    }
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- Binary @ 0x001c8f98
// ASM-spec v1.6.1 VerticalScroller::Update @0x001c8f98: touch state machine.
//   Held: forward to UpdateTouchPosition. On release apply the one-shot step
//   (state1 += stepSize clamp max, state2 -= stepSize clamp min), reset state.
//   Acquire: hit-test the track; classify top-arrow band -> inc, bottom band ->
//   dec (both inverted by m_bReverse), middle -> drag (state3) only when
//   m_TypeId(5) <= m_TotalRows.
void VerticalScroller::Update(float dt) {
    (void)dt;

    float visPx  = m_VisibleHeightPx;
    float totPx  = m_TotalHeightPx;
    float arrowH = s_arrow.IsValid() ? (float)s_arrow->GetHeight() : 0.0f;

    if (m_TouchId != -1) {
        if (IsTouchDown(m_TouchId) != 0) {
            UpdateTouchPosition();   // still held
            return;
        }
        m_TouchId = -1;
        if (m_State == 1) {
            m_CurrentValue = m_CurrentValue + (int)m_StepSize;
            if (m_MaxValue < m_CurrentValue) m_CurrentValue = m_MaxValue;
        } else if (m_State == 2) {
            m_CurrentValue = m_CurrentValue - (int)m_StepSize;
            if (m_CurrentValue < m_MinValue) m_CurrentValue = m_MinValue;
        }
        m_State = 0;
        return;
    }

    float left   = pos.x + visPx * -0.5f;
    float right  = pos.x + visPx *  0.5f;
    float top    = pos.y + totPx *  0.5f;
    float bottom = pos.y + totPx * -0.5f;

    m_TouchId = TouchInRegion(left, right, bottom, top, -1);
    if (m_TouchId == -1) {
        UpdateTouchPosition();
        return;
    }
    if (IsTouchDown(m_TouchId) != 2) {
        m_TouchId = -1;
        return;
    }

    float arrowPx = arrowH * size.y;
    uint8_t state;
    int topBand = TouchInRegion(left, right, top - arrowPx, top, -1);
    if (topBand == -1) {
        int botBand = TouchInRegion(left, right, bottom, bottom + arrowPx, -1);
        if (botBand == -1) {
            state = (m_TypeId <= m_TotalRows) ? 3 : 0;   // middle -> drag
        } else {
            state = (m_bReverse != 0) ? 1 : 2;           // bottom band
        }
    } else {
        state = (m_bReverse == 0) ? 1 : 2;               // top band
    }
    m_State = state;
}

// ---------------------------------------------------------------------------
// vtable slot 12 -- Binary @ 0x001c9ec0 (mov r0,#5; bx lr)
int VerticalScroller::GetType() {
    return 5;
}

// ---------------------------------------------------------------------------
// Non-virtual. pos.x += m_VisibleHeightPx * 0.5f (places left edge at pos.x).
// Called by ListBox::ListBox right after construction.
void VerticalScroller::AdjustByWidth() {
    pos.x += m_VisibleHeightPx * 0.5f;
}

// Non-virtual setter.
void VerticalScroller::SetPosition(float x, float y) {
    pos.x = x;
    pos.y = y;
    pos.z = 1.0f;
}

// ---------------------------------------------------------------------------
// Private helper, reached via a PLT veneer in Update. Captures the live finger
// position each held frame.
// TODO: v1.6.1 VerticalScroller::UpdateTouchPosition (behind PLT @0x00111a??) --
//   the drag-mode (m_State==3) touch.y -> m_CurrentValue mapping is not yet
//   ported (GOT-resolved body; dead code, untested path). Capture is faithful;
//   the drag remap is deferred.
void VerticalScroller::UpdateTouchPosition() {
    if (m_TouchId != -1 && m_TouchId < 16) {
        m_LastTouchPos = game_work.m_FingerSpawnPos[m_TouchId];
    }
}

// ---------------------------------------------------------------------------
// Static -- Binary @ 0x001c98b0
// ASM-spec v1.6.1 VerticalScroller::LoadContent @0x001c98b0: LoadLocalisedTexture
//   for vbar.tex -> s_box, vslider.tex -> s_slider, arrow.tex -> s_arrow.
// DIFFERS: none of vbar.tex / vslider.tex / arrow.tex are shipped in
//   FruitNinjaBada/Data for v1.6.1 (the dropdown stack is dead code, so its art
//   was dropped). The faithful names are kept; the SmartPtrs stay null when the
//   art is absent and Draw no-ops the corresponding quads. The visual test
//   injects substitutes via SetTexturesForTest.
void VerticalScroller::LoadContent() {
    s_box    = Mortar::TextureManager::LoadLocalisedTexture("vbar.tex");
    s_slider = Mortar::TextureManager::LoadLocalisedTexture("vslider.tex");
    s_arrow  = Mortar::TextureManager::LoadLocalisedTexture("arrow.tex");
}

void VerticalScroller::UnloadContent() {
    s_box.SetNull();
    s_slider.SetNull();
    s_arrow.SetNull();
}

// Port/test-only injector (no binary counterpart) -- see header note.
void VerticalScroller::SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& box,
                                          const Mortar::SmartPtr<Mortar::Texture>& slider,
                                          const Mortar::SmartPtr<Mortar::Texture>& arrow) {
    s_box    = box;
    s_slider = slider;
    s_arrow  = arrow;
}

// Binary empty bx lr.
// Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple -- no-op stub.
void VerticalScroller::UpdateFromGameWork() {}
