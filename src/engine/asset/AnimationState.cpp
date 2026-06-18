// Analysed: 2026-05-04T00:00

#include "asset/AnimationState.h"
#include "asset/AnimationList.h"
#include "asset/Mesh.h"
#include "asset/Model.h"
#include <cstring>

namespace Mortar {

// ---------------------------------------------------------------------------
// UpdateBinding<N> bodies -- B-spline evaluator, 6 instantiations (N=0..5).
//
// All six share the same high-level shape; N selects per-instantiation DAT scale
// constants and (for N==0) the no-blend nearest-sample path.
//
// Shared body:
//   1. Cast param_2 as BSplineKnots* via v.m_track (VectorTrack is a superset).
//   2. k = BSplineKnots::FindKnot(time, knots)
//   3. dim = *(u16*)(knots+0x0e)   (VectorTrack::m_dim)
//   4. Accumulate sample[dim] from knot rows (N>=1: basis blend; N==0: single row)
//   5. For each Binding b in v.m_bindings:
//        n = min(b.m_count, dim)
//        if (b.m_normalized) -> write clamped u8[0..255] using per-N SCALE
//        else                -> memcpy n floats to b.m_target
//
// The exact DAT float pairs (scale / clamp) and the spline math bodies
// (BSplineKnots::FindKnot, ClampKnotIdx, BasisFunc<2>) are follow-up RE tasks.
// ---------------------------------------------------------------------------

// N=0 @ 0x0026ec5c -- nearest-knot sample (no basis blend)
template<>
void UpdateBinding<0>(float time, AnimBindings::Vector const& v) {
    // TODO: 0x0026ec5c -- nearest-knot sample: FindKnot, single row read, write loop.
    //   Per-N DAT scale pair @ DAT_0026ed4c / DAT_0026ed50 (exact float values TBD).
    //   N=0 skips basis weighting: sample = m_data + dim*4*FindKnot(time, knots).
    (void)time; (void)v;
}

// N=1 @ 0x00270010 -- B-spline BasisFunc<2> quadratic blend
template<>
void UpdateBinding<1>(float time, AnimBindings::Vector const& v) {
    // TODO: 0x00270010 -- B-spline quadratic blend: FindKnot + ClampKnotIdx + BasisFunc<2>,
    //   accumulate sample[] weighted sum, write loop.
    //   Per-N DAT scale pair @ DAT_0x002701a8 / DAT_0x002701ac (exact float values TBD).
    (void)time; (void)v;
}

// N=2 @ 0x002701b0 -- B-spline BasisFunc<2>
template<>
void UpdateBinding<2>(float time, AnimBindings::Vector const& v) {
    // TODO: 0x002701b0 -- B-spline BasisFunc<2>.
    //   Per-N DAT scale pair @ DAT_0x00270348 / DAT_0x0027034c (exact float values TBD).
    (void)time; (void)v;
}

// N=3 @ 0x00270350 -- B-spline BasisFunc<2>
template<>
void UpdateBinding<3>(float time, AnimBindings::Vector const& v) {
    // TODO: 0x00270350 -- B-spline BasisFunc<2>.
    //   Per-N DAT scale pair @ DAT_0x002704e8 / DAT_0x002704ec (exact float values TBD).
    (void)time; (void)v;
}

// N=4 @ 0x002704f0 -- B-spline BasisFunc<2>
template<>
void UpdateBinding<4>(float time, AnimBindings::Vector const& v) {
    // TODO: 0x002704f0 -- B-spline BasisFunc<2>.
    //   Per-N DAT scale pair @ DAT_0x00270688 / DAT_0x0027068c (exact float values TBD).
    (void)time; (void)v;
}

// N=5 @ 0x00270690 -- B-spline BasisFunc<2>
template<>
void UpdateBinding<5>(float time, AnimBindings::Vector const& v) {
    // TODO: 0x00270690 -- B-spline BasisFunc<2>.
    //   Per-N DAT scale pair @ DAT_0x00270828 / DAT_0x0027082c (exact float values TBD).
    (void)time; (void)v;
}

// ---------------------------------------------------------------------------
// AnimationState methods
// ---------------------------------------------------------------------------

// Binary @ 0x0026f0b4 ctor
AnimationState::AnimationState(Mortar::SmartPtr<AnimationList> list) {
    if (list.IsValid()) {
        m_AnimList = list;
    } else {
        m_AnimList = GetDummyAnimList();
    }
    m_CurrentIter = m_AnimList->m_Anims.end();
    m_Time  = 0.0f;
    m_Speed = 1.0f;
}

// Binary D1/D2 @ 0x0026f010, D0 @ 0x0026f098
// Empty body; C++ auto-generates reverse-order member destruction (m_AnimList, m_Bindings, m_Mesh).
// AnimBindings::Bone/Vector members are non-empty, so their vector dtors run as expected.
// DIFFERS: binary inlines SmartPtr::Clear + explicit vptr writes; port relies on compiler synthesis.
AnimationState::~AnimationState() {}

// Binary @ 0x001ace8c
AnimationState::AnimIter AnimationState::GetAnimIter(const AsciiString& name) {
    return m_AnimList->m_Anims.find(name);
}

// Binary @ 0x001ace74
AnimationState::AnimConstIter AnimationState::GetAnimIter(const AsciiString& name) const {
    return m_AnimList->m_Anims.find(name);
}

// Binary @ 0x0026eb80 -- upfront size check, then iterate
AnimationState::AnimIter AnimationState::GetAnimIter(unsigned long idx) {
    if (idx >= m_AnimList->m_Anims.size()) return m_AnimList->m_Anims.end();
    AnimIter it = m_AnimList->m_Anims.begin();
    for (unsigned long i = 0; i < idx; ++i) {
        ++it;
    }
    return it;
}

// Binary @ 0x0026ec3c -- const overload
AnimationState::AnimConstIter AnimationState::GetAnimIter(unsigned long idx) const {
    if (idx >= m_AnimList->m_Anims.size()) return m_AnimList->m_Anims.end();
    AnimConstIter it = m_AnimList->m_Anims.begin();
    for (unsigned long i = 0; i < idx; ++i) {
        ++it;
    }
    return it;
}

// Binary @ 0x001ace8c -- iter==end() ? null : &iter->second
Animation* AnimationState::GetAnimation(const AsciiString& name) const {
    AnimConstIter it = GetAnimIter(name);
    if (it == m_AnimList->m_Anims.end()) {
        return 0;
    }
    return const_cast<Animation*>(&it->second);
}

// Binary @ 0x001acdf8 -- by index
Animation* AnimationState::GetAnimation(unsigned long idx) const {
    AnimConstIter it = GetAnimIter(idx);
    if (it == m_AnimList->m_Anims.end()) {
        return 0;
    }
    return const_cast<Animation*>(&it->second);
}

// Binary @ 0x001ad398
void AnimationState::LinkMesh(const Mortar::SmartPtr<Model>& m) {
    m_Mesh = m;
    RebindAnim();
}

// v1.6.1 AnimationState::PlayAnim @ 0x0026f3a8
void AnimationState::PlayAnim(const AsciiString& name, float time, bool loop) {
    AnimIter it = GetAnimIter(name);
    m_Loop  = loop;
    m_Time  = time;
    m_CurrentIter = it;
    RebindAnim();
}

// Binary @ 0x001ad348
void AnimationState::PlayAnimIdx(unsigned long idx, float time, bool loop) {
    m_CurrentIter = GetAnimIter(idx);
    m_Loop  = loop;
    m_Time  = time;
    RebindAnim();
}

// Binary @ 0x0026f1ac
// Rebuilds m_Bindings.m_Vectors from the current animation's track groups.
// For each track group, resizes m_Vectors, fills v.m_track, calls GenerateBindings,
// and collapses empty entries back.
// Stride magic (confirmed by reciprocal-division constants in binary):
//   AnimTrackGroup stride: 0x34 (DAT_0026f370 = 0xC4EC4EC5)
//   VectorTrack stride:    0x3c (DAT_0026f368 = 0xEEEEEEEF)
void AnimationState::RebindAnim() {
    m_Bindings.m_Bones.clear();
    m_Bindings.m_Vectors.clear();
    if (!m_Mesh.IsValid() || m_CurrentIter == m_AnimList->m_Anims.end()) {
        return;
    }

    Animation& anim = m_CurrentIter->second;
    uint32_t numGroups = (uint32_t)anim.m_trackGroups.size();
    for (uint32_t g = 0; g < numGroups; ++g) {
        AnimTrackGroup& grp = anim.m_trackGroups[g];
        uint32_t base = (uint32_t)m_Bindings.m_Vectors.size();
        uint32_t numTracks = (uint32_t)grp.m_vectorTracks.size();
        m_Bindings.m_Vectors.resize(base + numTracks);
        for (uint32_t t = 0; t < numTracks; ++t) {
            VectorTrack& trk = grp.m_vectorTracks[t];
            AnimBindings::Vector& v = m_Bindings.m_Vectors[base + t];
            v.m_track = &trk;
            // arg1 = grp.m_name  (channel/group name, AsciiString at AnimTrackGroup+0x00)
            // arg2 = trk.m_targetName (mesh/node target name, AsciiString at VectorTrack+0x14)
            // arg3 = v.m_bindings (vector<Binding> at Vector+0x04)
            m_Mesh->GenerateBindings(grp.m_name, trk.m_targetName, v.m_bindings);
            if (v.m_bindings.empty()) {
                --base;
                m_Bindings.m_Vectors.resize(base + numTracks);
            }
        }
    }
}

// Binary @ 0x0026ee84
// Advances time, handles loop wrap / stop, then dispatches UpdateBinding<N>
// for each element of m_Bindings.m_Vectors (at this+0x1c, stride 0x10).
void AnimationState::SetTime(float t) {
    if (m_CurrentIter == m_AnimList->m_Anims.end()) {
        return;
    }

    float dur = m_CurrentIter->second.m_duration;
    m_Time = t;

    if (dur < t) {
        if (!m_Loop) {
            m_CurrentIter = m_AnimList->m_Anims.end();
        } else {
            while (dur < m_Time) {
                m_Time -= dur;
            }
        }
    }

    // Walk m_Bindings.m_Vectors (this+0x1c .. this+0x20), stride 0x10.
    uint32_t numVectors = (uint32_t)m_Bindings.m_Vectors.size();
    for (uint32_t i = 0; i < numVectors; ++i) {
        AnimBindings::Vector& v = m_Bindings.m_Vectors[i];
        uint16_t tag = v.m_track->m_channelType;  // *(u16*)(v.m_track + 0xc)
        switch (tag) {
            case 0: UpdateBinding<0>(m_Time, v); break;
            case 1: UpdateBinding<1>(m_Time, v); break;
            case 2: UpdateBinding<2>(m_Time, v); break;
            case 3: UpdateBinding<3>(m_Time, v); break;
            case 4: UpdateBinding<4>(m_Time, v); break;
            case 5: UpdateBinding<5>(m_Time, v); break;
            default: break;
        }
    }
}

// Binary @ 0x001accc0
void AnimationState::StopAnim() {
    m_CurrentIter = m_AnimList->m_Anims.end();
}

// Binary @ 0x001ad974
bool AnimationState::IsPlaying() const {
    return m_CurrentIter != m_AnimList->m_Anims.end();
}

// Binary @ 0x001acffc -- Meyers singleton; empty AnimationList shared across all default ctors
Mortar::SmartPtr<AnimationList> AnimationState::GetDummyAnimList() {
    static Mortar::SmartPtr<AnimationList> s_dummy(new AnimationList());
    return s_dummy;
}

}  // namespace Mortar
