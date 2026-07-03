// Analysed: 2026-05-04T11:00
//
// VerticalScroller : HUDControl3d
// Binary CU range: 0x00167860..0x00168804
//
// Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple --
// no in-game instantiation found. All method bodies are no-op stubs per
// the stub-don't-skip policy. Class shape, vtable layout, and field offsets
// are preserved exactly to match binary layout.

#include "VerticalScroller.h"
#include "hud/HUDLayer.h"
#include "engine/input/Touch.h"
#include "render/gl_funcs.h"

// ---------------------------------------------------------------------------
// Module-scope texture handles (binary: Mortar::SmartPtr<Texture> GOT slots for
// vbar/vslider/arrow). Port uses GLuint since Draw/LoadContent are no-op stubs.
// Binary @ GOT[+0x7148], GOT[+0x7104], GOT[+0x7B04]
static GLuint s_vbar    = 0;
static GLuint s_vslider = 0;
static GLuint s_arrow   = 0;

// ---------------------------------------------------------------------------
// Constructor
// Binary @ 0x00168230 (C2) / 0x00168304 (C1)
// Args (after this): pos, size, minValue, maxValue, stepSize, currentValue,
//                   reverseDir, totalRows, visibleHeight, totalHeight
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

    // Binary: if visibleHeight arg is 0, default to 21 (0x15). Binary @ 0x00168230.
    m_VisibleHeight = (visibleHeight == 0) ? 21 : visibleHeight;

    // Cache pixel sizes: visibleHeight * size.y, totalHeight * size.y.
    m_VisibleHeightPx = (float)m_VisibleHeight * size.y;
    m_TotalHeightPx   = (float)totalHeight * size.y;

    // Binary sets m_LayerFlags = 0x400 in ctor.
    m_LayerFlags = Mortar::HUD_LAYER_FADE_MODAL;

    _pad8D[0] = 0;
    _pad8D[1] = 0;
    _pad8D[2] = 0;
}

// Destructor chain -- Binary @ 0x00168178 (D2) / 0x001681B4 (D1) / 0x001681F0 (D0)
// D0/D1 call Release() then HUDControl3d dtor.
VerticalScroller::~VerticalScroller() {
    Release();
}

// ---------------------------------------------------------------------------
// vtable slot 2 -- Binary @ 0x00167E6C (empty bx lr)
// Defunct: VerticalScroller -- no-op stub; v1.6.1 binary @ 0x00167E6C
void VerticalScroller::Init() {
}

// ---------------------------------------------------------------------------
// vtable slot 3 -- v1.6.1 VerticalScroller::Release @0x001c917c: tail-calls HUDControl3d::Release @0x0018b134 (no-op bx lr); binary does NOT write m_State.
// Defunct: VerticalScroller -- no-op stub; v1.6.1 VerticalScroller::Release @0x001c917c
void VerticalScroller::Release() {
    HUDControl3d::Release();
}

// ---------------------------------------------------------------------------
// vtable slot 6 -- Binary @ 0x00167FD0 (empty bx lr; returns void)
// Defunct: VerticalScroller -- no-op stub; v1.6.1 binary @ 0x00167FD0
void VerticalScroller::PreDraw(float* hudScale) {
    (void)hudScale;
}

// ---------------------------------------------------------------------------
// vtable slot 7 -- Binary @ 0x00168454 (~224 instructions)
// Renders scrollbar as 4 textured quads: vbar background, up-arrow, down-arrow, thumb.
// Defunct: VerticalScroller -- no-op stub; v1.6.1 binary @ 0x00168454
void VerticalScroller::Draw(float* hudScaleRaw) {
    (void)hudScaleRaw;
    // Stub: binary draws vbar.tex background, arrow.tex at top+bottom, vslider.tex
    // thumb (if m_TotalRows >= m_TypeId). Port renders nothing -- widget is never
    // instantiated in-game. Binary draw math preserved in RE report for future
    // activation: tmp/re-verticalscroller.md section 3.8.
}

// ---------------------------------------------------------------------------
// vtable slot 10 -- Binary @ 0x00167FD8 (~120 instructions)
// Touch state machine: hit-test bbox -> classify top-arrow/bottom-arrow/drag ->
// on release apply one-shot step to m_CurrentValue clamped to [min,max].
// Defunct: VerticalScroller -- no-op stub; v1.6.1 binary @ 0x00167FD8
void VerticalScroller::Update(float dt) {
    (void)dt;
    // Stub: binary polls TouchInRegion / IsTouchDown and updates m_CurrentValue.
    // Port does nothing -- widget is never instantiated in-game.
    // Binary logic preserved in RE report: tmp/re-verticalscroller.md section 3.6.
}

// ---------------------------------------------------------------------------
// vtable slot 12 -- Binary @ 0x00168B7C (mov r0,#5; bx lr)
int VerticalScroller::GetType() {
    return 5;
}

// ---------------------------------------------------------------------------
// Non-virtual. Binary @ 0x0014A908 (also via thunk @ 0x00107754 -> GOT 0x001F2C6C)
// Effect: pos.x += m_VisibleHeightPx * 0.5f
// Places scrollbar left edge at original pos.x (right side of parent listbox).
// Defunct: VerticalScroller -- no-op stub; v1.6.1 binary @ 0x0014A908
void VerticalScroller::AdjustByWidth() {
    pos.x += m_VisibleHeightPx * 0.5f;
}

// ---------------------------------------------------------------------------
// Non-virtual setter. Binary @ 0x0014A920 (in ListBox.cpp CU)
// Defunct: VerticalScroller -- no-op stub; v1.6.1 binary @ 0x0014A920
void VerticalScroller::SetPosition(float x, float y) {
    pos.x = x;
    pos.y = y;
    pos.z = 1.0f;   // hard-coded in binary
}

// ---------------------------------------------------------------------------
// Private non-virtual helper called by Update while touch is held.
// v1.6.1 VerticalScroller::UpdateTouchPosition @0x001c8e10 (~80 instructions)
// Drag-mode: reads live touch position -> maps touch.y into [m_MinValue, m_MaxValue].
// Defunct: VerticalScroller -- no-op stub; v1.6.1 VerticalScroller::UpdateTouchPosition @0x001c8e10
void VerticalScroller::UpdateTouchPosition() {
    // Stub: binary reads touch slot via GOT[+0x7990] (touch table, 12-byte stride),
    // stores into m_LastTouchPos, then (if m_State==3) maps touch.y into m_CurrentValue.
    // Full math preserved in RE report: tmp/re-verticalscroller.md section 3.7.
}

// ---------------------------------------------------------------------------
// Static -- Binary @ 0x0016872C
// Loads vbar.tex, vslider.tex, arrow.tex via TextureManager::LoadLocalisedTexture.
// Defunct: VerticalScroller -- no-op stub; v1.6.1 binary @ 0x0016872C
void VerticalScroller::LoadContent() {
    // Stub: binary calls Mortar::TextureManager::LoadLocalisedTexture for each of:
    //   GOT[+0x7148] = "vbar.tex"    (@ 0x001BC570)
    //   GOT[+0x7104] = "vslider.tex" (@ 0x001BC579)
    //   GOT[+0x7B04] = "arrow.tex"   (@ 0x001BB0C3)
    // Port does nothing -- textures never loaded since widget is never instantiated.
}

// ---------------------------------------------------------------------------
// Static -- Binary @ 0x001687D0
// Defunct: VerticalScroller -- no-op stub; v1.6.1 binary @ 0x001687D0
void VerticalScroller::UnloadContent() {
    // Stub: binary calls Mortar::SmartPtr<Texture>::SetNull on s_vbar, s_vslider, s_arrow.
    s_vbar    = 0;
    s_vslider = 0;
    s_arrow   = 0;
}

// ---------------------------------------------------------------------------
// Binary @ 0x00167fd4 (empty bx lr; body is genuinely empty in the binary)
// Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple --
// no-op stub; binary @ 0x00167fd4
void VerticalScroller::UpdateFromGameWork() {}
