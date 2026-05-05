#ifndef FN_ENGINE_ASSET_ANIMATIONSTATE_H
#define FN_ENGINE_ASSET_ANIMATIONSTATE_H

// Analysed: 2026-05-04T00:00

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include "asset/AnimationList.h"
#include <vector>

namespace Mortar {
class Model;

// AnimBindings -- stub for now; full layout in R3
// Binary @ 0x001ad218 RebindAnim -- Bone/Vector field details pending R3 RE pass.
struct AnimBindings {
    struct Bone {
        // TODO: R3 -- Bone binding fields
    };
    struct Vector {
        // TODO: R3 -- Vector binding fields
    };

    std::vector<Bone>   m_Bones;     // +0x00
    std::vector<Vector> m_Vectors;   // +0x0C
};

// Mortar::AnimationState -- per-instance playback state for an animation.
// Binary @ 0x001ad150 ctor; vtable @ 0x001ebd00; sizeof 0x40.
// 14 missing methods landed via this shell.
class AnimationState : public Mortar::ReferenceCounter {
public:
    // Iterator types opaque to callers
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

    // Binary @ 0x001ace8c -- iter==end() ? null : iter->second
    Animation* GetAnimation(const AsciiString& name) const;
    // Binary @ 0x001acdf8 -- by index
    Animation* GetAnimation(unsigned long idx) const;

    // Binary @ 0x001ad398 -- assign mesh pointer, then RebindAnim
    void LinkMesh(const Mortar::SmartPtr<Model>& m);

    // Binary @ 0x001ad370 -- set iter+loop+time, then RebindAnim
    void PlayAnim(const AsciiString& name, float time, bool loop);
    // Binary @ 0x001ad348 -- same body using index-based GetAnimIter
    void PlayAnimIdx(unsigned long idx, float time, bool loop);

    // Binary @ 0x001ad218 -- rebuild AnimBindings from (m_Mesh, current track group);
    //                       called from LinkMesh/PlayAnim/PlayAnimIdx
    void RebindAnim();

    // Binary @ 0x001accd0 -- advance time, handle loop/stop, dispatch UpdateBinding<N>
    void SetTime(float t);

    // Binary @ 0x001accc0 -- m_CurrentIter = m_Anims.end()
    void StopAnim();

    // Binary @ 0x001ad974 -- inline; m_CurrentIter != m_Anims.end()
    bool IsPlaying() const;

    // Binary @ 0x001acffc -- Meyers singleton, empty AnimationList shared across all default ctors
    static Mortar::SmartPtr<AnimationList> GetDummyAnimList();

private:
    Mortar::SmartPtr<Model>         m_Mesh;         // +0x0C
    AnimBindings            m_Bindings;     // +0x10 (24B)
    Mortar::SmartPtr<AnimationList> m_AnimList;     // +0x28
    AnimIter                m_CurrentIter;  // +0x2C
    // padding                             // +0x30
    float                   m_Time;         // +0x34
    float                   m_Speed;        // +0x38 (dead field in this class; AnimationManager uses)
    bool                    m_Loop;         // +0x3C
};

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_ANIMATIONSTATE_H
