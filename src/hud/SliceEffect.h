#ifndef FN_SLICE_EFFECT_H
#define FN_SLICE_EFFECT_H

// SliceEffect subsystem — slash-line visual effects.
//
// v1.6.1 binary refs:
//   AddSlice     @0x001dc990  (_Z8AddSlice8_Vector3IfEffiP5Fruitf)
//   DrawSlices   @0x001dae7c
//   Pool create  in Fruit::LoadInfo @0x001e10c4
//   Models load  in Fruit::LoadFruitModels @0x001e09b4
//
// Owner: Fruit TU statics (s_slices + s_pool).
// AddSlice / DrawSlices are top-level free functions (no namespace).

#include "math/Vec3.h"
#include <cstdint>

class Fruit;

// SliceEffect::Node — 0x30 (48) bytes.
// Binary layout verified against AddSlice @0x001dc990.
struct SliceEffect {
    // Intrusive doubly-linked list node for s_slices.
    // Pool-managed: MemoryPool<Node> owns the backing storage.
    struct Node {
        float  m_Timer;      // +0x00: 0..6 lifetime clock; expire at >=6.0
        float  m_Impulse;    // +0x04: v.y (length-scale hint)
        float  m_AngleDeg;   // +0x08: v.x (degrees-offset angle)
        Vec3   m_Pos;        // +0x0c: world position (+0x0c..+0x17)
        int    m_ModelIdx;   // +0x18: 0/1/3 -> s_sliceModel index
        Fruit* m_pFruit;     // +0x1c: fruit link (dedup/clamp); sentinels 0/1/3
        float  m_RateMul;    // +0x20: v.z (per-frame timer-rate multiplier)
        Node*  m_pNext;      // +0x24: intrusive list next
        Node*  m_pPrev;      // +0x28: intrusive list prev
        uint32_t _pad;       // +0x2c: padding to 0x30

        Node() : m_Timer(0.0f), m_Impulse(0.0f), m_AngleDeg(0.0f),
                 m_Pos(0.0f, 0.0f, 0.0f), m_ModelIdx(0),
                 m_pFruit(0), m_RateMul(1.0f),
                 m_pNext(0), m_pPrev(0), _pad(0) {}
    };
};

#ifdef __bada__
#include <cstddef>
static_assert(sizeof(SliceEffect::Node) == 0x30,
              "SliceEffect::Node must be 0x30 bytes (v1.6.1 AddSlice @0x001dc990)");
static_assert(offsetof(SliceEffect::Node, m_Timer)    == 0x00, "");
static_assert(offsetof(SliceEffect::Node, m_Impulse)  == 0x04, "");
static_assert(offsetof(SliceEffect::Node, m_AngleDeg) == 0x08, "");
static_assert(offsetof(SliceEffect::Node, m_Pos)      == 0x0c, "");
static_assert(offsetof(SliceEffect::Node, m_ModelIdx) == 0x18, "");
static_assert(offsetof(SliceEffect::Node, m_pFruit)   == 0x1c, "");
static_assert(offsetof(SliceEffect::Node, m_RateMul)  == 0x20, "");
static_assert(offsetof(SliceEffect::Node, m_pNext)    == 0x24, "");
static_assert(offsetof(SliceEffect::Node, m_pPrev)    == 0x28, "");
#endif

// 7-frame keyframe scale table (frame index = int(m_Timer), frac = m_Timer - int).
// Binary: _GLOBAL__I_GameTask.cpp static ctor @0x0016d0dc.
extern const Vec3 SLICE_KEYFRAMES[7];

// AddSlice -- spawn a new slice-line effect.
// Binary: _Z8AddSlice8_Vector3IfEffiP5Fruitf @0x001dc990
//   v.x = angleDeg, v.y = impulse, v.z = rateMul
//   posX/posY/posZ = world position of the slice effect
//   modelIdx: 0=slice_fx, 1=slice_fx_crit, 3=slice_fx (super-fruit pass)
//   fruit: dedup/clamp link; sentinel values 0, 1, 3 accepted
void AddSlice(Vec3 v, float posX, float posY, int modelIdx, Fruit* fruit, float posZ);

// DrawSlices -- update timer + draw all active slice nodes.
// Binary: @0x001dae7c
//   pass==false: draw modelIdx!=3 nodes (normal + crit lines)
//   pass==true : draw modelIdx==3 nodes (super-fruit second pass)
void DrawSlices(float dt, bool pass);

#endif // FN_SLICE_EFFECT_H
