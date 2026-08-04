#ifndef FN_SLICE_EFFECT_H
#define FN_SLICE_EFFECT_H

// SliceEffect subsystem -- slash-line visual effects.
//
// v1.6.1 binary refs:
//   AddSlice     @0x001dc990  (_Z8AddSlice8_Vector3IfEffiP5Fruitf)
//   DrawSlices   @0x001dae7c
//   Pool create  in Fruit::LoadInfo @0x001e10c4
//   Models load  in Fruit::LoadFruitModels @0x001e09b4
//
// Owner: Fruit TU statics (s_slices + s_pool).
// AddSlice / DrawSlices are top-level free functions (no namespace).
//
// The 0x30-byte binary node is Mortar::List<SliceEffect>::Node:
//   +0x00..+0x27  SliceEffect value   (payload -- 0x28 bytes)
//   +0x28         Node* m_pPrev       (doubly-linked prev, set by AddNodeToHead)
//   +0x2c         Node* m_pNext       (doubly-linked next, set by AddNodeToHead)
// The List<SliceEffect> head (20 bytes) lives at s_slices (heap pointer in FruitGlobalData).

#include "math/_Vector3.h"
#include "util/List.h"
#include <cstdint>

class Fruit;

// SliceEffect -- 0x28-byte payload for one slash-line visual effect instance.
// Used as T in Mortar::List<SliceEffect>; Mortar::List<SliceEffect>::Node wraps
// it with m_pPrev and m_pNext at +0x28 and +0x2c (prev BEFORE next).
//
// Binary layout verified against AddSlice @0x001dc990.
struct SliceEffect {
    float    m_Timer;      // +0x00: 0..6 lifetime clock; expire at >=6.0
    float    m_Impulse;    // +0x04: v.y (length-scale hint)
    float    m_AngleDeg;   // +0x08: v.x (degrees-offset angle)
    _Vector3<float> m_Pos;        // +0x0c: world position (+0x0c..+0x17)
    int      m_ModelIdx;   // +0x18: 0/1/3 -> s_sliceModel index
    Fruit*   m_pFruit;     // +0x1c: fruit link (dedup key + DrawSlices timer cap); real Fruit* or NULL
    float    m_RateMul;    // +0x20: v.z (per-frame timer-rate multiplier)
    // +0x24: dead reserved word -- AddSlice @0x001dc990 memset-0's it and never
    // writes a real value; DrawSlices @0x001dae7c never reads it. (Earlier RE
    // mis-labeled it 'm_pNext'; the list prev/next are at +0x28/+0x2c.)
    uint32_t m_Reserved24; // +0x24: unused, always 0

    SliceEffect()
        : m_Timer(0.f), m_Impulse(0.f), m_AngleDeg(0.f),
          m_Pos(0.f, 0.f, 0.f), m_ModelIdx(0), m_pFruit(0),
          m_RateMul(1.f), m_Reserved24(0) {}
};

#ifdef __bada__
#include <cstddef>
// SliceEffect payload must be exactly 0x28 bytes so that List<SliceEffect>::Node
// gets m_pPrev at +0x28 and m_pNext at +0x2c (binary: AddSlice @0x001dc990).
static_assert(sizeof(SliceEffect) == 0x28,
    "SliceEffect payload must be 0x28 bytes (v1.6.1 AddSlice @0x001dc990)");
static_assert(__builtin_offsetof(SliceEffect, m_Timer)    == 0x00, "SliceEffect::m_Timer");
static_assert(__builtin_offsetof(SliceEffect, m_Impulse)  == 0x04, "SliceEffect::m_Impulse");
static_assert(__builtin_offsetof(SliceEffect, m_AngleDeg) == 0x08, "SliceEffect::m_AngleDeg");
static_assert(__builtin_offsetof(SliceEffect, m_Pos)      == 0x0c, "SliceEffect::m_Pos");
static_assert(__builtin_offsetof(SliceEffect, m_ModelIdx) == 0x18, "SliceEffect::m_ModelIdx");
static_assert(__builtin_offsetof(SliceEffect, m_pFruit)   == 0x1c, "SliceEffect::m_pFruit");
static_assert(__builtin_offsetof(SliceEffect, m_RateMul)  == 0x20, "SliceEffect::m_RateMul");
static_assert(__builtin_offsetof(SliceEffect, m_Reserved24) == 0x24, "SliceEffect::m_Reserved24");

// List<SliceEffect>::Node probe: the 0x30-byte doubly-linked node that wraps
// SliceEffect payload. m_pPrev and m_pNext are provided by the List<T>::Node wrapper.
// v1.6.1: this is the node type that AddNodeToHead @0x001e3158 and Remove @0x001e36c8 manage.
namespace {
typedef Mortar::List<SliceEffect>::Node _SliceNode;
}
static_assert(sizeof(_SliceNode) == 0x30,
    "List<SliceEffect>::Node must be 0x30 bytes (v1.6.1 AddSlice @0x001dc990)");
static_assert(__builtin_offsetof(_SliceNode, m_pPrev) == 0x28,
    "List<SliceEffect>::Node::m_pPrev must be at +0x28");
static_assert(__builtin_offsetof(_SliceNode, m_pNext) == 0x2c,
    "List<SliceEffect>::Node::m_pNext must be at +0x2c");
#endif

// 7-frame keyframe scale table (frame index = int(m_Timer), frac = m_Timer - int).
// Binary: _GLOBAL__I_GameTask.cpp static ctor @0x0016d0dc.
extern const _Vector3<float> SLICE_KEYFRAMES[7];

// AddSlice -- spawn a new slice-line effect.
// Binary: _Z8AddSlice8_Vector3IfEffiP5Fruitf @0x001dc990
//   v.x = angleDeg, v.y = impulse, v.z = rateMul
//   posX/posY/posZ = world position of the slice effect
//   modelIdx: 0=slice_fx, 1=slice_fx_crit, 3=super-fruit slice model (drawn by
//     the DrawSlices pass==true second pass)
//   fruit: dedup key AND lifetime link. Either a real Fruit* (only the
//     super-fruit call B @0x001bbc70 passes one -- the host fruit) or NULL.
//     Fruit::Slice @0x001dcd84 and Fruit::CollisionResponse @0x001ddabc both
//     pass NULL; there are no (Fruit*)1 / (Fruit*)3 sentinels.
//     A non-NULL key runs the dedup walk: every node already keyed to the same
//     fruit EXCEPT THE FIRST is unlinked (m_pFruit = 0) and expired
//     (m_Timer = 6.0). A NULL key skips the walk entirely.
void AddSlice(_Vector3<float> v, float posX, float posY, int modelIdx, Fruit* fruit, float posZ);

// DrawSlices -- update timer + draw all active slice nodes.
// Binary: @0x001dae7c
//   pass==false: draw modelIdx!=3 nodes (normal + crit lines)
//   pass==true : draw modelIdx==3 nodes (super-fruit second pass)
// A node with a live m_pFruit has its timer CAPPED at 3.0 (@0x001daee0), so it
// holds at keyframe 3 until the linked fruit is sliced and the link clears.
// Nodes reaching 6.0 are removed and returned to the pool.
void DrawSlices(float dt, bool pass);

#endif // FN_SLICE_EFFECT_H
