// Analysed: 2026-05-04T11:30
//
// SliderControl : HUDControl3d
// Binary CU range: ~0x0015ff90..0x00160c90
//
// Defunct: SliderControl -- orphaned in binary; zero internal call sites.
// OptionsScreen was repurposed to PauseScreen, leaving the slider library
// code unused. All method bodies are no-op stubs per the stub-don't-skip
// policy. Class shape, vtable layout, and field offsets are preserved exactly
// to match binary layout.

#include "SliderControl.h"
#include "hud/HUDLayer.h"
#include "render/gl_funcs.h"
#include <cstring>

// ---------------------------------------------------------------------------
// Module-scope static texture handles (binary: Mortar::SmartPtr<Texture> GOT slots).
// LoadContent loads "box.tex" (track) and "slider_will.tex" (thumb).
// Port uses GLuint since LoadContent/UnloadContent are no-op stubs.
static GLuint s_TrackTexture = 0;
static GLuint s_ThumbTexture = 0;

// ---------------------------------------------------------------------------
// Constructor
// Binary @ 0x00160268 (master C2) / 0x00160398 (duplicate C2 variant)
// Args: pos, size, label, minValue, maxValue, fontSize, initialValue
//
// Binary ctor body:
//   1. HUDControl3d base init
//   2. Mortar::Utf8StringIterator(&m_Label, label)
//   3. Mortar::Delegate0<void>(&m_OnValueChanged)
//   4. Assigns pos, size
//   5. Samples two fonts via GOT[DAT_00160390] / GOT[DAT_00160394] to compute
//      m_TrackWidth/Height and m_ThumbWidth/Height from font glyph metrics
//   6. m_FontSize = fontSize, m_MinValue/MaxValue/CurrentValue
//   7. m_TouchId = -1
//   8. m_LayerFlags = 0x200 (layer bit 9)
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
    , m_TouchId(-1)
    , m_TouchPos(0.0f, 0.0f, 0.0f)
{
    pos  = inPos;
    size = inSize;

    // Binary @ 0x00160268: label copied via Mortar::Utf8StringIterator ctor.
    // Port: plain strncpy into char[28] placeholder.
    if (label) {
        strncpy(m_Label, label, sizeof(m_Label) - 1);
        m_Label[sizeof(m_Label) - 1] = '\0';
    } else {
        m_Label[0] = '\0';
    }

    // Binary computes track/thumb sizes from two font glyph metrics multiplied
    // by size.x and size.y. Fonts are unresolved in the port (GOT[0x00160390]
    // and GOT[0x00160394]). Fields stay zero -- widget is never rendered.

    // Binary @ 0x00160268: m_LayerFlags = 0x200
    m_LayerFlags = Mortar::HUD_LAYER_SLIDER;
}

// Destructor chain -- Binary @ 0x001601a8 (D2) / 0x00160140 (D1/deleting)
// D1/D2 call Release(), ~Mortar::Delegate0, ~Mortar::Utf8StringIterator, ~HUDControl3d.
SliderControl::~SliderControl() {
    Release();
}

// ---------------------------------------------------------------------------
// vtable slot 2 -- Binary @ 0x0015ffa0 (single bx lr; explicit SliderControl override)
// Defunct: SliderControl -- no-op stub; binary @ 0x0015ffa0
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::Init() {
}

// ---------------------------------------------------------------------------
// vtable slot 3 -- Binary @ 0x0015ffa4 (single bx lr; explicit SliderControl override)
// Defunct: SliderControl -- no-op stub; binary @ 0x0015ffa4
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::Release() {
}

// ---------------------------------------------------------------------------
// vtable slot 6 -- Binary @ 0x0015ffa8 (single bx lr; explicit SliderControl override)
// Defunct: SliderControl -- no-op stub; binary @ 0x0015ffa8
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::PreDraw(const Vec3& hudScale) {
    (void)hudScale;
}

// ---------------------------------------------------------------------------
// vtable slot 7 -- Binary @ 0x0016069c (~224 instructions)
// Draws: label text (Font::DrawString), track quad (box.tex, black tint),
//        thumb quad (slider_will.tex, black tint, x-offset per current value).
// Defunct: SliderControl -- no-op stub; binary @ 0x0016069c
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::Draw(const Vec3& hudScale, int layerMask) {
    (void)hudScale;
    (void)layerMask;
    // Stub: binary draws label via Font::DrawString at
    //   (pos.x + 15 + m_TrackWidth*0.5, pos.y + 14),
    // then renders track background (box.tex) and thumb (slider_will.tex)
    // as textured quads tinted with s_BgColour=(0,0,0,255).
    // Widget is never instantiated in-game; port renders nothing.
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- Binary @ 0x00160090 (touch state machine)
// Acquires a touch slot via TouchInRegion centered on pos with half-extents
// m_TrackWidth*0.5 (X) and m_TrackHeight*0.5 (Y). Releases on touch-up.
// On each held frame calls UpdateTouchPosition() to update m_CurrentValue.
// Defunct: SliderControl -- no-op stub; binary @ 0x00160090
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::Update(float dt) {
    (void)dt;
    // Stub: binary polls TouchInRegion / IsTouchDown and calls
    // UpdateTouchPosition() while m_TouchId != -1.
    // Port does nothing -- widget is never instantiated in-game.
}

// ---------------------------------------------------------------------------
// vtable slot 12 -- Binary @ 0x00160c8c (mov r0,#5; bx lr)
// Returns the HUDControl type-id for SliderControl. Not defunct -- live data.
int SliderControl::GetType() {
    return 5;
}

// ---------------------------------------------------------------------------
// Non-virtual. Binary @ 0x0016010c
// Updates m_Label: constructs Mortar::Utf8StringIterator from str, assigns to m_Label.
// Defunct: SliderControl -- no-op stub; binary @ 0x0016010c
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::SetText(const char* str) {
    if (str) {
        strncpy(m_Label, str, sizeof(m_Label) - 1);
        m_Label[sizeof(m_Label) - 1] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Private non-virtual helper called by Update while touch is held.
// Binary @ 0x0015ffb0 (~80 instructions)
// Maps live touch position (from g_TouchTable[m_TouchId].pos) into
// m_CurrentValue in [m_MinValue, m_MaxValue]. Fires m_OnValueChanged
// when m_CurrentValue changes. Quirk: value mapping is ratio*(min+max)/2
// which is only correct when min==0.
// Defunct: SliderControl -- no-op stub; binary @ 0x0015ffb0
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::UpdateTouchPosition() {
    // Stub: binary reads g_TouchTable[m_TouchId].pos into m_TouchPos,
    // then maps touch x-offset to m_CurrentValue, then fires m_OnValueChanged
    // if value changed. Port does nothing -- widget is never instantiated.
}

// ---------------------------------------------------------------------------
// Static -- Binary @ 0x00160890
// Loads "box.tex" (track background) and "slider_will.tex" (thumb) via
// TextureManager::LoadLocalisedTexture into static Mortar::SmartPtr<Texture> slots.
// Defunct: SliderControl -- no-op stub; binary @ 0x00160890
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::LoadContent() {
    // Stub: binary calls TextureManager::LoadLocalisedTexture for:
    //   s_TrackTexture <- "box.tex"
    //   s_ThumbTexture <- "slider_will.tex"
    // Port does nothing -- textures never loaded since widget is never instantiated.
}

// ---------------------------------------------------------------------------
// Static -- Binary @ 0x0016090c
// Defunct: SliderControl -- no-op stub; binary @ 0x0016090c
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::UnloadContent() {
    // Stub: binary calls Mortar::SmartPtr<Texture>::SetNull on s_TrackTexture, s_ThumbTexture.
    s_TrackTexture = 0;
    s_ThumbTexture = 0;
}

// ---------------------------------------------------------------------------
// HUDControl override -- Binary @ 0x0015ffac (single bx lr; no-op)
// Defunct: SliderControl -- no-op stub; binary @ 0x0015ffac
//          (no internal call sites; OptionsScreen was repurposed to
//          PauseScreen, leaving the slider library code unused).
void SliderControl::UpdateFromGameWork() {}
