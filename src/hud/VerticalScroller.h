#ifndef FN_HUD_VERTICAL_SCROLLER_H
#define FN_HUD_VERTICAL_SCROLLER_H

// Defunct: ComboBox/ListBox/VerticalScroller dropdown widget triple --
// no in-game instantiation found; binary @ 0x00168230 (VerticalScroller),
// 0x0014A178 (ListBox), 0x00136164 (ComboBox).
// Class shape preserved per stub-don't-skip policy.

#include "HUDControl3d.h"
#include "math/Vec3.h"
#include <cstdint>

class VerticalScroller : public HUDControl3d {
public:
    // +0x7C: minimum scroll value
    // Binary: ctor @ 0x00168230 stores r3 here (minValue arg).
    int32_t  m_MinValue;

    // +0x80: maximum scroll value
    // Binary: ctor stores sp[0x30] here (maxValue arg).
    int32_t  m_MaxValue;

    // +0x84: per-arrow-tap step
    // Binary: ctor stores sp[0x34] here (stepSize arg, uint16).
    uint16_t m_StepSize;

    // +0x86: alignment pad (not written by ctor)
    uint16_t _pad86;

    // +0x88: live scroll value; read externally by ListBox::Update @ 0x00149C84.
    int32_t  m_CurrentValue;

    // +0x8C: total item count; gates thumb visibility and drag enable (must be >= m_TypeId=5).
    uint8_t  m_TotalRows;

    // +0x8D: padding
    uint8_t  _pad8D[3];

    // +0x90: last computed thumb Y in world coords (written by Draw, not read internally).
    float    m_CachedThumbY;

    // +0x94: visible-rows count; if zero on ctor, defaults to 21.
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
    Vec3     m_LastTouchPos;

    // Binary @ 0x00168230 (C2) / 0x00168304 (C1)
    VerticalScroller(const Vec3& pos, const Vec3& size,
                     int32_t minValue, int32_t maxValue, uint16_t stepSize,
                     int32_t currentValue, bool reverseDir,
                     uint8_t totalRows, uint16_t visibleHeight, uint16_t totalHeight);

    // Binary @ 0x00168178 (D2) / 0x001681B4 (D1) / 0x001681F0 (D0)
    virtual ~VerticalScroller();

    // vtable slot 2 -- Binary @ 0x00167E6C (empty bx lr)
    void Init() override;

    // vtable slot 3 -- Binary @ 0x00168170 (calls HUDControl3d::Release)
    void Release() override;

    // vtable slot 6 -- Binary @ 0x00167FD0 (empty bx lr)
    void PreDraw(const Vec3& hudScale) override;

    // vtable slot 7 -- Binary @ 0x00168454 (~224 instructions; renders bar/thumb/arrows)
    void Draw(const Vec3& hudScale, int layerMask) override;

    // vtable slot 10 -- Binary @ 0x00167FD8 (~120 instructions; touch state machine)
    void Update(float dt) override;

    // vtable slot 12 -- Binary @ 0x00168B7C (mov r0,#5; bx lr)
    int GetType() override;

    // Non-virtual. Binary @ 0x0014A908 (in ListBox.cpp CU; also via thunk @ 0x00107754)
    // Effect: pos.x += m_VisibleHeightPx * 0.5f; (places left edge at original pos.x)
    void AdjustByWidth();

    // Non-virtual. Binary @ 0x0014A920 (in ListBox.cpp CU)
    void SetPosition(float x, float y);

    // Non-virtual private helper; called by Update each frame while touch is held.
    // Binary @ 0x00167E70 (~80 instructions; drag-mode touch-to-value mapping)
    void UpdateTouchPosition();

    // Static texture lifecycle. Binary @ 0x0016872C / 0x001687D0.
    // Loads / unloads vbar.tex, vslider.tex, arrow.tex via TextureManager.
    static void LoadContent();
    static void UnloadContent();

#ifdef __bada__
    // Layout assertions -- only valid under Bada/ARM cross-toolchain.
    // sizeof check done as static_assert outside the class body below.
#endif
};

#ifdef __bada__
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
#endif

#endif // FN_HUD_VERTICAL_SCROLLER_H
