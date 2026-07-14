#ifndef FN_HUD_VERTICAL_SCROLLER_H
#define FN_HUD_VERTICAL_SCROLLER_H

//
// VerticalScroller : HUDControl3d (sizeof 0xB4 on ARM32)
//
// The scrollbar half of the dead-code dropdown widget stack
// (ComboBox -> ListBox -> VerticalScroller). No live call site in v1.6.1
// instantiates a ComboBox, so the whole triple is dead code -- but it carries a
// complete, faithful implementation (same policy as CheckBox / SliderControl).
//
// A ListBox owns exactly one VerticalScroller, created in ListBox::ListBox only
// when items.size() > visibleRows, positioned at the list's right edge and
// AddControl'd to the HUD. The scroller's live scroll offset is
//   m_CurrentValue (+0x88)
// which ListBox::Draw reads (confirmed @0x001947d0: ldr r3,[m_pScroller,#0x88])
// to pick the top visible row = items.begin() + m_CurrentValue.
//
// Binary (v1.6.1):
//   ctor  @ 0x001c9380 (C1) / 0x001c9284 (C2)
//   Init                @ 0x001c8e0c (empty)
//   PreDraw             @ 0x001c8f90 (empty)
//   Release             @ 0x001c917c (tail-calls HUDControl3d::Release)
//   Update              @ 0x001c8f98 (touch state machine)
//   Draw                @ 0x001c9514 (track + 2 arrows + thumb)
//   GetType (-> 5)      @ 0x001c9ec0
//   LoadContent         @ 0x001c98b0 (vbar.tex / vslider.tex / arrow.tex)
//
// Field offsets were re-verified against the ctor instruction stream
// (0x001c9380); m_LayerFlags is 0x800 (top-most overlay), NOT 0x400 -- the
// earlier port carried a stale v1.5.x value.
//

#include "HUDControl3d.h"
#include "math/_Vector3.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include <cstdint>

class VerticalScroller : public HUDControl3d {
    friend struct VerticalScrollerLayoutAssert;

private:
    // +0x7C: minimum scroll value (ctor arg minValue).
    int32_t  m_MinValue;

    // +0x80: maximum scroll value (ctor arg maxValue; ListBox passes items-visibleRows).
    int32_t  m_MaxValue;

    // +0x84: per-arrow-tap step (ctor arg stepSize, uint16; ListBox passes 1).
    uint16_t m_StepSize;

    // +0x86: alignment pad (not written by ctor)
    uint16_t _pad86;

public:
    // +0x88: live scroll value. READ EXTERNALLY by ListBox::Draw @0x001947d0
    // (top visible row = items.begin() + m_CurrentValue). Public: genuine
    // cross-class access, not offsetof-only.
    int32_t  m_CurrentValue;

private:
    // +0x8C: total item count; gates thumb visibility and drag enable (must be >= m_TypeId=5).
    uint8_t  m_TotalRows;

    // +0x8D: padding
    uint8_t  _pad8D[3];

    // +0x90: last computed thumb Y in world coords (written by Draw, not read internally).
    float    m_CachedThumbY;

    // +0x94: visible-rows count; if zero on ctor, defaults to 21 (0x15).
    uint16_t m_VisibleHeight;

    // +0x96: total-rows count (paired with m_VisibleHeight).
    uint16_t m_TotalHeight;

    // +0x98: m_VisibleHeight * size.y (cached pixel size).
    float    m_VisibleHeightPx;

    // +0x9C: m_TotalHeight * size.y (cached pixel size, full track height).
    float    m_TotalHeightPx;

    // +0xA0: constant 5; doubles as min-rows-for-thumb threshold (== GetType() return).
    uint8_t  m_TypeId;

    // +0xA1: direction flag; flips arrow-band -> state mapping and draw thumb math.
    uint8_t  m_bReverse;

    // +0xA2: touch state machine state (0=idle, 1=inc, 2=dec, 3=drag).
    uint8_t  m_State;

    // +0xA3: padding
    uint8_t  _padA3;

    // +0xA4: active touch slot (-1 = none); set to 0xFFFFFFFF by ctor.
    int32_t  m_TouchId;

    // +0xA8..+0xB3: last sampled touch position (12 bytes, Vec3).
    _Vector3<float> m_LastTouchPos;

public:
    // Binary @ 0x001c9380 (C1) / 0x001c9284 (C2)
    VerticalScroller(_Vector3<float> pos, _Vector3<float> size,
                     int32_t minValue, int32_t maxValue, uint16_t stepSize,
                     int32_t currentValue, bool reverseDir,
                     uint8_t totalRows, uint16_t visibleHeight, uint16_t totalHeight);

    // Binary @ 0x001c9268 (D0) / 0x001c9250 (D1) region -- D0/D1 call Release() then base dtor.
    virtual ~VerticalScroller();

    // vtable slot 2 -- Binary @ 0x001c8e0c (empty bx lr)
    void Init() override;

    // vtable slot 3 -- Binary @ 0x001c917c (tail-calls HUDControl3d::Release)
    void Release() override;

    // vtable slot 6 -- Binary @ 0x001c8f90 (empty bx lr)
    void PreDraw(float* hudScale) override;

    // vtable slot 7 -- Binary @ 0x001c9514 (track quad + top/bottom arrows + thumb)
    void Draw(float* hudScaleRaw) override;

    // vtable slot 10 -- Binary @ 0x001c8f98 (touch state machine)
    void Update(float dt) override;

    // vtable slot 12 -- Binary @ 0x001c9ec0 (mov r0,#5; bx lr)
    int GetType() override;

    // Non-virtual. Effect: pos.x += m_VisibleHeightPx * 0.5f (places left edge at pos.x).
    // Called by ListBox::ListBox right after construction.
    void AdjustByWidth();

    // Non-virtual setter.
    void SetPosition(float x, float y);

    // Read-only accessors (test/caller convenience).
    int32_t MaxValue()  const { return m_MaxValue; }
    uint8_t TotalRows() const { return m_TotalRows; }

    // Private non-virtual helper called by Update while a touch is held.
    // Reached through a PLT veneer @0x00111a?? in Update; captures the finger
    // position and (in drag mode) maps it to m_CurrentValue.
    void UpdateTouchPosition();

    // Static texture lifecycle. Binary @ 0x001c98b0.
    // Loads vbar.tex -> s_box, vslider.tex -> s_slider, arrow.tex -> s_arrow.
    static void LoadContent();
    static void UnloadContent();

    // Port/test-only: inject substitute textures (the faithful vbar/vslider/arrow
    // art is NOT shipped in v1.6.1 -- see .cpp DIFFERS). Always compiled (the
    // static slots live in this TU). No binary counterpart.
    static void SetTexturesForTest(const Mortar::SmartPtr<Mortar::Texture>& box,
                                   const Mortar::SmartPtr<Mortar::Texture>& slider,
                                   const Mortar::SmartPtr<Mortar::Texture>& arrow);

    // vtable/per-frame hook -- Binary empty bx lr.
    // Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple -- no-op stub.
    void UpdateFromGameWork();
};

#ifdef __bada__
struct VerticalScrollerLayoutAssert {
    static_assert(sizeof(VerticalScroller) == 0xB4, "VerticalScroller size mismatch");
    static_assert(__builtin_offsetof(VerticalScroller, m_MinValue)       == 0x7C, "m_MinValue offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_MaxValue)       == 0x80, "m_MaxValue offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_StepSize)       == 0x84, "m_StepSize offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_CurrentValue)   == 0x88, "m_CurrentValue offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_TotalRows)      == 0x8C, "m_TotalRows offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_CachedThumbY)   == 0x90, "m_CachedThumbY offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_VisibleHeight)  == 0x94, "m_VisibleHeight offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_TotalHeight)    == 0x96, "m_TotalHeight offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_VisibleHeightPx)== 0x98, "m_VisibleHeightPx offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_TotalHeightPx)  == 0x9C, "m_TotalHeightPx offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_TypeId)         == 0xA0, "m_TypeId offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_bReverse)       == 0xA1, "m_bReverse offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_State)          == 0xA2, "m_State offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_TouchId)        == 0xA4, "m_TouchId offset");
    static_assert(__builtin_offsetof(VerticalScroller, m_LastTouchPos)   == 0xA8, "m_LastTouchPos offset");
};
#endif

#endif // FN_HUD_VERTICAL_SCROLLER_H
