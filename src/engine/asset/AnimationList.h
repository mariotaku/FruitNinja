#ifndef FN_ENGINE_ASSET_ANIMATIONLIST_H
#define FN_ENGINE_ASSET_ANIMATIONLIST_H

// Analysed: 2026-05-04T00:00

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include <map>
#include <cstdint>

namespace Mortar {

// Forward declarations -- full layout deferred to R3 RE pass
struct Animation;
struct AnimTrackGroup;

// Mortar::AnimationList -- refcounted resource holding named animations.
// Binary vtable @ 0x001ebc58; sizeof 0x24 (36).
// Layout:
//   +0x00  Mortar::ReferenceCounter (12B: vptr + strong count + weak count)
//   +0x0C  std::map<AsciiString, Animation> m_Anims (24B — std::map with cached _M_node_count)
// Total: 0x24 (36)
//
// Map value type confirmed BY VALUE (not pointer): the destructor
// Mortar::AnimationList::~AnimationList @ 0x001af1f4 destroys
//   std::map<Mortar::AsciiString, Mortar::Animation, std::less<AsciiString>, ...> at (this + 0xc).
// AnimationState::GetAnimation @ 0x001acdf8 likewise iterates a
//   std::map<AsciiString, Animation> reached via AnimationList* (state+0x28) -> (+0xc).
//
// NOTE: the resource loader entry point is Mortar::ResourceLoader::Load<AnimationList> @ 0x001b0600,
// which is a ResourceLoader template instantiation (Enter CS -> GetLoaders()[typeid] ->
// helper->Load() -> SmartPtr::Cast<AnimationList>), NOT a member of this class. It is owned
// by the ResourceLoader subsystem, not AnimationList.h.
//
// TODO: 0x000f6504 (ctor) / 0x001af1f4 (dtor) — full Mortar::Animation struct layout pending
//       R3 RE pass. Ctor/dtor are vtable thunks (PTR_Animation_001f09c4 / PTR__Animation_001ee0c4);
//       Animation carries a vtable + AnimTrackGroup data not yet RE'd. Until the complete type is
//       available the map value is held as Animation* (a std::map<K, Animation> value type needs the
//       complete type to instantiate). Switch to value-type AnimMap once Animation is fully ported.
class AnimationList : public Mortar::ReferenceCounter {
public:
    // std::map<AsciiString, Animation*> uses AsciiString::operator< -> Compare(),
    // which is non-lex (length-first, hash-second, memcmp-third). This matches the
    // binary's std::map ordering.
    // DIFFERS: binary map value is Mortar::Animation (value), confirmed by dtor @ 0x001af1f4.
    //   Port holds Animation* because the Animation struct is forward-declared only (incomplete);
    //   std::map<K, Animation> value-type requires the complete type. Corrected once Animation is
    //   fully RE'd (R3 pass).
    typedef std::map<AsciiString, Animation*> AnimMap;

    AnimationList() {}

    AnimMap m_Anims;  // +0x0C (24B std::map with cached _M_node_count)
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(AnimationList) == 0x24,
              "AnimationList sizeof mismatch (ReferenceCounter 12B + std::map 24B)");
#endif

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_ANIMATIONLIST_H
