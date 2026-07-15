#ifndef FN_ENGINE_ASSET_ANIMATIONSTATE_H
#define FN_ENGINE_ASSET_ANIMATIONSTATE_H

// Analysed: 2026-05-04T00:00

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include "asset/AnimationList.h"
#include <vector>
#include <cstdint>

namespace Mortar {
class Model;

// AnimBindings -- per-animation channel binding tables.
// Bones @ this+0x00 (12B vector), Vectors @ this+0x0c (12B vector).
// SetTime/RebindAnim access Vectors at AnimationState+0x1c (= AnimBindings+0x0c).
struct AnimBindings {
    // Bone::Binding -- empty shell; Mesh::GenerateBindings(Bone) @ 0x0027385c is BX LR.
    // Declare for type completeness and vtable slot 7 signature.
    struct Bone {
        // Defunct-ish: Mesh emits no bone bindings; v1.6.1 Mortar::Mesh::GenerateBindings @0x0027385c (empty BX LR).
        struct Binding {};
        std::vector<Binding> m_bindings;  // +0x00 (12B)
    };

    // AnimBindings::Vector -- sizeof 16.
    // +0x00  VectorTrack* m_track     (source spline; RebindAnim writes track ptr here)
    // +0x04  vector<Binding> m_bindings (12B)
    // RebindAnim @ 0x0026f1ac: *(int*)v = trackPtr; then GenerateBindings fills v+0x04.
    // SetTime @ 0x0026ee84: dispatches on *(u16*)(m_track + 0xc) = VectorTrack::m_channelType.
    struct Vector {
        // AnimBindings::Vector::Binding -- stride 0x0c.
        // Produced by Mesh::GenerateBindings(Vector) @ 0x0027350c via push_back.
        // UpdateBinding reads:
        //   +0x00 u8   m_normalized -- if 1: write clamped u8[0..255]; else raw float copy
        //   +0x04 void* m_target    -- destination ptr
        //   +0x08 u32  m_count      -- element count; min'd with VectorTrack::m_dim
        struct Binding {
            uint8_t  m_normalized; // +0x00
            uint8_t  pad_01[3];    // +0x01..+0x03
            void*    m_target;     // +0x04
            uint32_t m_count;      // +0x08
            Binding() : m_normalized(0), m_target(0), m_count(0) {
                pad_01[0] = pad_01[1] = pad_01[2] = 0;
            }
        };

        VectorTrack*        m_track;     // +0x00 (was field_0x0; now named per R3 RE)
        std::vector<Binding> m_bindings; // +0x04 (12B)

        Vector() : m_track(0) {}
    };

    std::vector<Bone>   m_Bones;    // +0x00 (12B)
    std::vector<Vector> m_Vectors;  // +0x0C (12B)
};

#if defined(__bada__)
static_assert(sizeof(AnimBindings::Vector::Binding) == 0x0c,
              "AnimBindings::Vector::Binding sizeof mismatch (expected 0x0c)");
static_assert(sizeof(AnimBindings::Vector) == 16,
              "AnimBindings::Vector sizeof mismatch (VectorTrack* + vector == 16)");
#endif

// UpdateBinding<N> -- B-spline evaluator template, 6 instantiations (N=0..5).
// Real bodies:
//   N=0 @ 0x0026ec5c  nearest-knot sample (no basis blend)
//   N=1 @ 0x00270010  B-spline BasisFunc<2> quadratic blend
//   N=2 @ 0x002701b0  B-spline BasisFunc<2>
//   N=3 @ 0x00270350  B-spline BasisFunc<2>
//   N=4 @ 0x002704f0  B-spline BasisFunc<2>
//   N=5 @ 0x00270690  B-spline BasisFunc<2>
// Declared here so SetTime can dispatch via switch(tag).
template<int N>
void UpdateBinding(float time, AnimBindings::Vector const& v);

// Mortar::AnimationState -- per-instance playback state for an animation.
// Binary @ 0x0026f0b4 ctor; vtable @ 0x001ebd00; sizeof 0x40.
// Field layout (verified against v1.6.1 SetTime @ 0x0026ee84 / RebindAnim @ 0x0026f1ac):
//   +0x00  ReferenceCounter (vptr + refcounts, 12B)
//   +0x0C  SmartPtr<Model> m_Mesh
//   +0x10  AnimBindings m_Bindings (24B: Bones@+0x00, Vectors@+0x0c)
//            -> m_Bindings.m_Bones   at this+0x10 (clear target in RebindAnim)
//            -> m_Bindings.m_Vectors at this+0x1c (walk target in SetTime)
//   +0x28  SmartPtr<AnimationList> m_AnimList
//   +0x2C  AnimIter m_CurrentIter (4B rb-tree node ptr)
//   +0x30  uint32_t m_Pad_0x30 (gap; binary writes nothing meaningful here in SetTime)
//   +0x34  float m_Time
//   +0x38  float m_Speed (dead field in this class; used by AnimationManager externally)
//   +0x3C  bool m_Loop
class AnimationState : public Mortar::ReferenceCounter {
public:
    typedef AnimationList::AnimMap::iterator       AnimIter;
    typedef AnimationList::AnimMap::const_iterator AnimConstIter;

    // Binary @ 0x001ad150 -- store list (or empty dummy), iter=end, time=0, speed=1, loop=false
    explicit AnimationState(Mortar::SmartPtr<AnimationList> list);
    virtual ~AnimationState();

    // Binary @ 0x001ace8c -- forward to m_AnimList->m_Anims.find(name)
    AnimIter      GetAnimIter(const AsciiString& name);
    // Binary @ 0x001ace74 -- const overload
    AnimConstIter GetAnimIter(const AsciiString& name) const;
    // Binary @ 0x001ace2c -- iterate map.begin() forward idx times, bounds-checked
    AnimIter      GetAnimIter(unsigned long idx);
    // Binary @ 0x001acd8c -- const overload
    AnimConstIter GetAnimIter(unsigned long idx) const;

    // Binary @ 0x001ace8c -- iter==end() ? null : &iter->second
    Animation* GetAnimation(const AsciiString& name) const;
    // Binary @ 0x001acdf8 -- by index
    Animation* GetAnimation(unsigned long idx) const;

    // Binary @ 0x001ad398 -- assign mesh pointer, then RebindAnim
    void LinkMesh(const Mortar::SmartPtr<Model>& m);

    // Binary @ 0x001ad370 -- set iter+loop+time, then RebindAnim
    void PlayAnim(const AsciiString& name, float time, bool loop);
    // Binary @ 0x001ad348 -- same body using index-based GetAnimIter
    void PlayAnimIdx(unsigned long idx, float time, bool loop);

    // Binary @ 0x0026f1ac -- rebuild AnimBindings from (m_Mesh, current track group)
    void RebindAnim();

    // Binary @ 0x0026ee84 -- advance time, handle loop/stop, dispatch UpdateBinding<N>
    void SetTime(float t);

    // Binary @ 0x001accc0 -- m_CurrentIter = m_Anims.end()
    void StopAnim();

    // Binary @ 0x001ad974 -- inline; m_CurrentIter != m_Anims.end()
    bool IsPlaying() const;

private:
    Mortar::SmartPtr<Model>          m_Mesh;         // +0x0C
    AnimBindings                     m_Bindings;     // +0x10 (24B: Bones@+0x10, Vectors@+0x1c)
    Mortar::SmartPtr<AnimationList>  m_AnimList;     // +0x28
    AnimIter                         m_CurrentIter;  // +0x2C
    uint32_t                         m_Pad_0x30;     // +0x30 (gap; binary writes nothing here)
    float                            m_Time;         // +0x34
    float                            m_Speed;        // +0x38 (dead in this class)
    bool                             m_Loop;         // +0x3C
};

#ifdef __bada__
static_assert(sizeof(Mortar::AnimationState) == 0x40,
              "AnimationState sizeof mismatch (expected 0x40)");
#endif

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_ANIMATIONSTATE_H
