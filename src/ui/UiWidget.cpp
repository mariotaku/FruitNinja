//
// UiWidget -- Port specific: see header for rationale. No binary counterpart;
// this whole file is port-only glue code, so no // ASM-verified markers apply.
//

#include "UiWidget.h"
#include "render/NineSlice.h"
#include "render/MatrixManager.h"
#include "render/Utf8StringIterator.h"
#include "asset/Mesh.h"
#include "math/Matrix44.h"
#include "engine/input/Touch.h"
#include "game/GameWork.h"

// Logical (post-HD-halving) box.tex corner size. box.svg's rounded end spans
// ~7.5 logical px (2px transparent margin + 5.5 corner radius), so the fixed
// 9-patch corner must be >= that or the tip of the arc lands in the STRETCHED
// edge and the ends smear when the box is drawn wide/thin (e.g. the slider
// track). Src 9 texels fully contains the arc + a hair of the straight rim;
// dest 8 world units draws every widget's corner at the same crisp size.
const float UiWidget::kBoxSrcBorderX  = 9.0f;
const float UiWidget::kBoxSrcBorderY  = 9.0f;
const float UiWidget::kBoxDestBorderX = 8.0f;
const float UiWidget::kBoxDestBorderY = 8.0f;

UiWidget::UiWidget()
    : HUDControl3d()
    , m_HalfW(0.0f)
    , m_HalfH(0.0f)
    , m_TouchId(-1)
    , m_TouchCapture(0.0f, 0.0f, 0.0f)
    , m_OnChange()
    , m_Tint(Colour::White)
    , m_TextColour(Colour::White)
    , m_pFont(NULL)
    , m_BoxTex()
{
}

UiWidget::~UiWidget() {
}

bool UiWidget::HitTest(float x, float y) const {
    return x >= pos.x - m_HalfW && x <= pos.x + m_HalfW &&
           y >= pos.y - m_HalfH && y <= pos.y + m_HalfH;
}

UiWidget::PressResult UiWidget::PollTouch() {
    if (m_TouchId == -1) {
        int slot = TouchInRegion(pos.x - m_HalfW, pos.x + m_HalfW,
                                  pos.y - m_HalfH, pos.y + m_HalfH, -1);
        if (slot == -1) {
            return kNone;
        }
        if (IsTouchDown(slot) != 2) {
            return kNone;
        }
        m_TouchId = slot;
        m_TouchCapture = game_work.m_FingerSpawnPos[slot];
        return kPressed;
    }

    if (IsTouchDown(m_TouchId) == 0) {
        m_TouchId = -1;
        return HitTest(m_TouchCapture.x, m_TouchCapture.y) ? kReleasedInside : kReleasedOutside;
    }

    m_TouchCapture = game_work.m_FingerSpawnPos[m_TouchId];
    return kHeld;
}

void UiWidget::CancelTouch(int slot) {
    if (m_TouchId == -1) return;
    if (slot != -1 && m_TouchId != slot) return;
    m_TouchId = -1;
}

void UiWidget::DrawBox(float cx, float cy, float w, float h, Colour tint) {
    if (!m_BoxTex.IsValid()) return;
    Mortar::NineSlice::Draw(m_BoxTex.Get(), cx, cy, w, h,
                            kBoxSrcBorderX, kBoxSrcBorderY,
                            kBoxDestBorderX, kBoxDestBorderY, tint);
}

void UiWidget::DrawGlyphQuad(Mortar::Texture* tex, float cx, float cy, float w, float h, Colour c) {
    if (!tex) return;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    tex->Set();

    Matrix44 mat = Matrix44::MakeScale(w, h, 1.0f);
    mat.GlobalTranslate44(_Vector3<float>(cx, cy, 0.0f));
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    Mortar::Mesh::DrawQuadUnCached(c, NULL);

    tex->UnSet();
}

void UiWidget::DrawText(const char* s, float x, float yTop, float scale, Colour c,
                        const Mortar::MortarRectangleT<float>* clip) {
    if (!s) return;

    Mortar::Font* font = m_pFont ? m_pFont : game_work.pFontMain.Get();
    if (!font) return;

    Mortar::Utf8StringIterator iter(s);
    // Font::DrawString mutates *clip in place (entry/exit transform) then
    // restores it -- const_cast is safe here, the caller's rect is unchanged
    // by the time this call returns.
    font->DrawString(iter, x, yTop, 0.0f, c, scale, 0.0f, 0.0f, 1,
                     const_cast<Mortar::MortarRectangleT<float>*>(clip), 0.0f);
}
