// Analysed: 2026-05-04T00:00

#include "asset/AnimationState.h"
#include "asset/AnimationList.h"
#include "asset/Mesh.h"
#include "asset/Model.h"

namespace Mortar {

// Binary @ 0x001ad150
AnimationState::AnimationState(Mortar::SmartPtr<AnimationList> list) {
    if (list.IsValid()) {
        m_AnimList = list;
    } else {
        m_AnimList = GetDummyAnimList();
    }
    m_CurrentIter = m_AnimList->m_Anims.end();
    m_Pad_0x30 = 0;
    m_Time  = 0.0f;
    m_Speed = 1.0f;
    m_Loop  = false;
}

// Binary @ 0x001acecc / 0x001acf34
AnimationState::~AnimationState() {}

// Binary @ 0x001ace8c
AnimationState::AnimIter AnimationState::GetAnimIter(const AsciiString& name) {
    return m_AnimList->m_Anims.find(name);
}

// Binary @ 0x001ace74
AnimationState::AnimConstIter AnimationState::GetAnimIter(const AsciiString& name) const {
    return m_AnimList->m_Anims.find(name);
}

// Binary @ 0x001ace2c -- iterate map.begin() forward idx times, bounds-checked
AnimationState::AnimIter AnimationState::GetAnimIter(unsigned long idx) {
    AnimIter it = m_AnimList->m_Anims.begin();
    AnimIter end = m_AnimList->m_Anims.end();
    for (unsigned long i = 0; i < idx && it != end; ++i) {
        ++it;
    }
    return it;
}

// Binary @ 0x001acd8c -- const overload
AnimationState::AnimConstIter AnimationState::GetAnimIter(unsigned long idx) const {
    AnimConstIter it = m_AnimList->m_Anims.begin();
    AnimConstIter end = m_AnimList->m_Anims.end();
    for (unsigned long i = 0; i < idx && it != end; ++i) {
        ++it;
    }
    return it;
}

// Binary @ 0x001ace8c
Animation* AnimationState::GetAnimation(const AsciiString& name) const {
    AnimConstIter it = GetAnimIter(name);
    if (it == m_AnimList->m_Anims.end()) {
        return 0;
    }
    return it->second;
}

// Binary @ 0x001acdf8
Animation* AnimationState::GetAnimation(unsigned long idx) const {
    AnimConstIter it = GetAnimIter(idx);
    if (it == m_AnimList->m_Anims.end()) {
        return 0;
    }
    return it->second;
}

// Binary @ 0x001ad398
void AnimationState::LinkMesh(const Mortar::SmartPtr<Model>& m) {
    m_Mesh = m;
    RebindAnim();
}

// Binary @ 0x001ad370
void AnimationState::PlayAnim(const AsciiString& name, float time, bool loop) {
    m_CurrentIter = GetAnimIter(name);
    m_Loop  = loop;
    m_Time  = time;
    RebindAnim();
}

// Binary @ 0x001ad348
void AnimationState::PlayAnimIdx(unsigned long idx, float time, bool loop) {
    m_CurrentIter = GetAnimIter(idx);
    m_Loop  = loop;
    m_Time  = time;
    RebindAnim();
}

// Binary @ 0x001ad218
void AnimationState::RebindAnim() {
    m_Bindings.m_Bones.clear();
    m_Bindings.m_Vectors.clear();
    // TODO: 0x001ad218 -- track-group walk + Model::GenerateBindings dispatch (R3 dependency).
    //   Spec (from binary @ 0x001ad218):
    //     if (m_Mesh && IsPlaying()) {
    //       grp = &m_CurrentIter->second->m_trackGroups;  // Animation+0x40 (vector<AnimTrackGroup>)
    //       for (g = 0; g < grp->size(); ++g) {
    //         base = m_Vectors.size();
    //         tracks = &(*grp)[g].m_vectorTracks;          // AnimTrackGroup+0x28 (vector<VectorTrack>)
    //         m_Vectors.resize(base + tracks->size(), AnimBindings::Vector());
    //         for (t = 0; t < tracks->size(); ++t) {
    //           AnimBindings::Vector& v = m_Vectors[base + t];
    //           v.field_0x0 = (*tracks)[t].m_typeTag;       // VectorTrack+0x00 copied to Vector+0x00
    //           m_Mesh->GenerateBindings((*grp)[g].m_name,  // AnimTrackGroup+0x?? (AsciiString)
    //                                    (*tracks)[t].m_targetName,  // VectorTrack+0x14 (AsciiString)
    //                                    v.m_bindings);     // Vector+0x04 (vector<Vector::Binding>)
    //           if (v.m_bindings.empty()) {                // shrink back: drop the empty entry
    //             --base;
    //             m_Vectors.resize(base + tracks->size(), AnimBindings::Vector());
    //           }
    //         }
    //       }
    //     }
    //   Blocked on: AnimTrackGroup / AnimTrackGroup::VectorTrack layout (forward-decl only,
    //   AnimationList.h), full Animation struct layout (Animation+0x40 m_trackGroups), and
    //   Model::GenerateBindings(AsciiString const&, AsciiString const&,
    //   vector<AnimBindings::Vector::Binding>&) const (itself // TODO @ 0x00192f5c in Model.h,
    //   blocked on AnimBindings::Vector::Binding). All R3.
}

// Binary @ 0x001accd0
void AnimationState::SetTime(float t) {
    if (!IsPlaying()) {
        return;
    }
    m_Time = t;
    // TODO: 0x001accd0 -- duration check + loop/stop logic + UpdateBinding<N> dispatch (R3 dependency).
    //   Spec (from binary @ 0x001accd0), continuing after m_Time = t:
    //     float dur = m_CurrentIter->second->m_duration;  // Animation+0x38 (float)
    //     if (dur < t) {
    //       if (!m_Loop) {                                 // this+0x3c
    //         m_CurrentIter = m_AnimList->m_Anims.end();   // stop (this+0x2c = end iter)
    //       } else {
    //         while (dur < t) t -= dur;                    // wrap into [0,dur)
    //         m_Time = t;
    //       }
    //     }
    //     for (i = 0; i < m_Vectors.size(); ++i) {
    //       AnimBindings::Vector& v = m_Vectors[i];
    //       // dispatch on a u16 type tag at *(v.m_bindings.data) + 0xc, i.e. v field +0x00
    //       // dereferenced (*(short*)(*(int*)&v + 0xc)); cases 0..5 -> UpdateBinding<N>(&v, m_Time)
    //       switch (tag) { case 0: UpdateBinding<0>(&v, m_Time); ... case 5: UpdateBinding<5>(&v, m_Time); }
    //     }
    //   Blocked on: full Animation struct layout (Animation+0x38 m_duration), the
    //   AnimBindings::Vector::Binding layout that carries the u16 type tag at +0xc, and the
    //   UpdateBinding<N> template family (not yet ported — interpolates a binding channel).
    //   All R3.
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
