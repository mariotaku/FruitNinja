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
    // TODO: 0x001ad218 -- track-group walk + Mesh::GenerateBindings dispatch (R3 dependency)
}

// Binary @ 0x001accd0
void AnimationState::SetTime(float t) {
    if (!IsPlaying()) {
        return;
    }
    m_Time = t;
    // TODO: 0x001accd0 -- duration check + loop/stop logic + UpdateBinding<N> dispatch (R3 dependency)
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
