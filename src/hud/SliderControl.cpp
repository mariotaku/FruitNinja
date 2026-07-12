//
// SliderControl : HUDControl3d
// v1.6.1 SliderControl @ 0x001b7248..0x001b8148
//
// Dead code in v1.6.1 (OptionsScreen was repurposed to PauseScreen so nothing
// instantiates it), but the class has a complete real implementation, ported
// faithfully here. Field offsets/names re-derived from the ctor +
// UpdateTouchPosition instruction stream (Ghidra's struct names were swapped).
//

#include "SliderControl.h"
#include "hud/HUDLayer.h"
#include "asset/TextureManager.h"
#include "asset/Mesh.h"
#include "render/MatrixManager.h"
#include "render/Font.h"
#include "render/Utf8StringIterator.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "engine/input/Touch.h"
#include "engine/util/StringTable.h"
#include "game/GameWork.h"
#include <cstdint>

// ---------------------------------------------------------------------------
// Module-scope static texture handles (binary: Mortar::SmartPtr<Texture> GOT slots).
// LoadContent loads "box.tex" (track -- the same shared texture ComboBox/
// ListBox load for their own bar/row art) and "slider_will.tex" (thumb).
static Mortar::SmartPtr<Mortar::Texture> s_box;      // track background
static Mortar::SmartPtr<Mortar::Texture> s_slider;   // thumb

// ---------------------------------------------------------------------------
// Constructor -- Binary @ 0x001b7474
// ASM-verified: 2026-07-11T00:00Z v1.6.1 SliderControl::SliderControl @ 0x001b7474 (re-analyst)
//   Utf8StringIterator ctor on m_Label(+0x9C); Delegate0 ctor on m_OnValueChanged(+0xB8);
//   size(+0x20); m_FontSize(+0x84); pos(+0x8);
//   m_TrackWidth (+0x8C) = s_box.width  * size.x;   m_TrackHeight(+0x90) = s_box.height  * size.y;
//   m_ThumbWidth (+0x94) = s_slider.width * size.x; m_ThumbHeight(+0x98) = s_slider.height * size.y;
//   m_MinValue(+0x7C); m_MaxValue(+0x80); m_CurrentValue(+0x88); m_TouchId(+0xA8)=-1;
//   m_LayerFlags(+0x34)=0x400.
SliderControl::SliderControl(Vec3 inPos, Vec3 inSize,
                             const char* label,
                             int32_t minValue, int32_t maxValue,
                             uint16_t fontSize, int32_t initialValue)
    : HUDControl3d()
    , m_MinValue(minValue)
    , m_MaxValue(maxValue)
    , m_FontSize(fontSize)
    , _pad86(0)
    , m_CurrentValue(initialValue)
    , m_TrackWidth(0.0f)
    , m_TrackHeight(0.0f)
    , m_ThumbWidth(0.0f)
    , m_ThumbHeight(0.0f)
    , m_Label(label)
    , m_TouchId(-1)
    , m_TouchPos(0.0f, 0.0f, 0.0f)
    , m_OnValueChanged()
{
    _labelPad[0] = _labelPad[1] = 0;
    pos  = inPos;
    size = inSize;

    // Track/thumb extents come from the loaded texture dims (binary reads
    // s_box/s_slider apparentWidth/Height at +0x24/+0x28) scaled by size.
    if (s_box.IsValid()) {
        m_TrackWidth  = (float)s_box->GetWidth()  * size.x;
        m_TrackHeight = (float)s_box->GetHeight() * size.y;
    }
    if (s_slider.IsValid()) {
        m_ThumbWidth  = (float)s_slider->GetWidth()  * size.x;
        m_ThumbHeight = (float)s_slider->GetHeight() * size.y;
    }

    m_LayerFlags = Mortar::HUD_LAYER_FADE_MODAL;  // +0x34 = 0x400
}

// Port convenience overload -- NOT a distinct binary ctor. 0x001b7474/0x001b7594
// are the Itanium C1/C2 pair for the SINGLE char* ctor above (confirmed identical
// bodies via decompile; neither calls GETSTRING_CAST_0). Resolves via
// GETSTRING_CAST_0 for call-site symmetry with CheckBox's genuine
// ctor(LocalizedString) (0x00166ab8), matching every other GETSTRING_CAST_0 site.
SliderControl::SliderControl(Vec3 inPos, Vec3 inSize,
                             LocalizedString locLabel,
                             int32_t minValue, int32_t maxValue,
                             uint16_t fontSize, int32_t initialValue)
    : HUDControl3d()
    , m_MinValue(minValue)
    , m_MaxValue(maxValue)
    , m_FontSize(fontSize)
    , _pad86(0)
    , m_CurrentValue(initialValue)
    , m_TrackWidth(0.0f)
    , m_TrackHeight(0.0f)
    , m_ThumbWidth(0.0f)
    , m_ThumbHeight(0.0f)
    , m_Label(GETSTRING_CAST_0(locLabel))
    , m_TouchId(-1)
    , m_TouchPos(0.0f, 0.0f, 0.0f)
    , m_OnValueChanged()
{
    _labelPad[0] = _labelPad[1] = 0;
    pos  = inPos;
    size = inSize;

    if (s_box.IsValid()) {
        m_TrackWidth  = (float)s_box->GetWidth()  * size.x;
        m_TrackHeight = (float)s_box->GetHeight() * size.y;
    }
    if (s_slider.IsValid()) {
        m_ThumbWidth  = (float)s_slider->GetWidth()  * size.x;
        m_ThumbHeight = (float)s_slider->GetHeight() * size.y;
    }

    m_LayerFlags = Mortar::HUD_LAYER_FADE_MODAL;  // +0x34 = 0x400
}

SliderControl::~SliderControl() {
    Release();
}

// vtable slot 2 -- bx lr; no-op.
void SliderControl::Init() {
}

// vtable slot 3 -- bx lr; no-op.
void SliderControl::Release() {
}

// vtable slot 6 -- bx lr; no-op.
void SliderControl::PreDraw(float* hudScale) {
    (void)hudScale;
}

// vtable slot 12 -- Binary @ 0x001b8148 (returns 5).
int SliderControl::GetType() {
    return 5;
}

// Non-virtual -- reassigns the label pointer (binary rebuilds the Utf8StringIterator).
void SliderControl::SetText(const char* str) {
    if (str) {
        m_Label = str;
    }
}

// HUDControl override -- single bx lr; no-op.
void SliderControl::UpdateFromGameWork() {
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- Binary @ 0x001b7248
// Touch state machine. Hit-region centered on pos: X half-extent = m_TrackWidth*0.5,
// Y half-extent = size.y*60*0.5. Acquire a slot via TouchInRegion; hold while
// IsTouchDown; call UpdateTouchPosition each held frame.
void SliderControl::Update(float dt) {
    (void)dt;

    float halfX = m_TrackWidth * 0.5f;
    float halfY = (size.y * 60.0f) * 0.5f;

    if (m_TouchId == -1) {
        float minX = pos.x - halfX;
        float maxX = pos.x + halfX;
        float minY = pos.y - halfY;
        float maxY = pos.y + halfY;
        m_TouchId = TouchInRegion(minX, maxX, minY, maxY, -1);
        if (m_TouchId != -1) {
            if (IsTouchDown(m_TouchId) != 2) {
                m_TouchId = -1;
            }
            return;  // acquire frame: no position update
        }
        // No slot acquired -- falls through to UpdateTouchPosition (no-op, id == -1).
    } else {
        if (IsTouchDown(m_TouchId) == 0) {
            m_TouchId = -1;
            return;
        }
        // Still held -- falls through to UpdateTouchPosition.
    }
    UpdateTouchPosition();
}

// ---------------------------------------------------------------------------
// Binary @ 0x001b713c
// Maps (pos.x - finger.x) across the track into [m_MinValue, m_MaxValue], writes the
// result to m_CurrentValue, and fires m_OnValueChanged when the value changes.
// The mapping is ratio*(min+max)/2 -- only exactly correct when min==0 (binary quirk).
void SliderControl::UpdateTouchPosition() {
    int   id       = m_TouchId;
    float extent   = m_TrackWidth * 0.5f;
    int   oldValue = m_CurrentValue;

    if (id != -1) {
        m_TouchPos = game_work.m_FingerSpawnPos[id];
        float d = pos.x - m_TouchPos.x;

        int value;
        if (d > extent) {
            value = m_MinValue;
        } else {
            int   di   = (int)d;
            float absd = (float)(di < 0 ? -di : di);
            if (absd > extent) {
                value = m_MaxValue;
            } else {
                int dm = (int)(d - extent);
                if (dm < 0) dm = -dm;
                float ratio    = (float)dm / extent;
                int   computed = (int)(ratio * (float)(m_MaxValue + m_MinValue) * 0.5f + 0.5f);
                value = computed;
                if (m_MaxValue < value) value = m_MaxValue;   // min(max, computed)
                if (m_MinValue > value) value = m_MinValue;   // max(min, ...)
            }
        }
        m_CurrentValue = value;
    }

    if (oldValue == m_CurrentValue) {
        return;
    }
    m_OnValueChanged();
}

// ---------------------------------------------------------------------------
// vtable slot 7 -- Binary @ 0x001b792c
// Label (Yellow, tinted by hudScale) at (pos.x + 15 + trackW*0.5, pos.y + 14),
// font game_work.m_Fonts[1] (= pFontMain), size 24. Track quad (s_box, White)
// scaled (trackW, trackH) at pos. Thumb quad (s_slider, White) scaled
// (thumbW, thumbH), translated in X by the current-value ratio across the track.
void SliderControl::Draw(float* hudScaleRaw) {
    const Vec3& hudScale = *reinterpret_cast<const Vec3*>(hudScaleRaw);

    float trackW = m_TrackWidth;
    float trackH = m_TrackHeight;
    float thumbW = m_ThumbWidth;
    float thumbH = m_ThumbHeight;
    // Positioning uses a normalized thumb width (thumbW * 74/128); the quad scale
    // uses thumbW directly. Both are the binary's exact arithmetic.
    float thumbPosW = thumbW * 74.0f * 0.0078125f;

    const float tintRGB[3] = { hudScale.x, hudScale.y, hudScale.z };
    // Colour::Yellow (not defined in the port's Colour) = (255,255,0,255).
    Colour labelCol = Colour::TintColour(Colour(255, 255, 0, 255), tintRGB);

    if (m_Label && game_work.pFontMain.IsValid()) {
        Mortar::Utf8StringIterator iter(m_Label);
        game_work.pFontMain->DrawString(iter,
                                        pos.x + 15.0f + trackW * 0.5f, pos.y + 14.0f, 0.0f,
                                        labelCol, 24.0f, 0.0f, 0.0f, 1, NULL, 0.0f);
    }

    MatrixManager& mm = MatrixManager::GetInstance();

    // --- track quad (s_box) ---
    mm.GetWorldStack().Reset();
    if (s_box.IsValid()) {
        s_box->Set();
        Matrix44 mat = Matrix44::MakeScale(trackW, trackH, 1.0f);
        mat.GlobalTranslate44(pos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);
        s_box->UnSet();
    }

    // --- thumb quad (s_slider) ---
    mm.GetWorldStack().Reset();
    if (s_slider.IsValid()) {
        s_slider->Set();
        Matrix44 mat = Matrix44::MakeScale(thumbW, thumbH, 1.0f);
        float value = (float)m_CurrentValue;
        float maxV  = (float)m_MaxValue;
        Vec3 thumbPos = pos;
        thumbPos.x = thumbPosW * 0.5f
                   + (value / maxV) * (trackW - thumbPosW)
                   + pos.x
                   + trackW * -0.5f;
        mat.GlobalTranslate44(thumbPos);
        mm.GetWorldStack().SetCurrentMatrix(mat);
        mm.UploadModelViewOnly();
        Mortar::Mesh::DrawQuadUnCached(Colour::White, NULL);
        s_slider->UnSet();
    }
}

// ---------------------------------------------------------------------------
// Static -- Binary @ 0x001b7bc0
// ASM-spec v1.6.1 SliderControl::LoadContent @0x001b7bc0: LoadLocalisedTexture
//   box.tex -> s_box (track), slider_will.tex -> s_slider (thumb).
// box.tex is the SAME shared texture ComboBox (@0x00168b3c) and ListBox
//   (@0x00194fdc) load for their own bar/row art (Ghidra-confirmed string
//   reference at each LoadContent call site) -- not a SliderControl-only asset.
void SliderControl::LoadContent() {
    s_box    = Mortar::TextureManager::LoadLocalisedTexture("box.tex");
    s_slider = Mortar::TextureManager::LoadLocalisedTexture("slider_will.tex");
}

void SliderControl::UnloadContent() {
    s_box.SetNull();
    s_slider.SetNull();
}

// Port/test-only injector (no binary counterpart) -- see header note.
void SliderControl::SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& track,
                                       const Mortar::SmartPtr<Mortar::Texture>& thumb) {
    s_box    = track;
    s_slider = thumb;
}
