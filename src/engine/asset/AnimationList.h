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
// Binary @ 0x001ebc50 vtable; sizeof 0x24 with std::map<AsciiString, Animation>
// at +0x0C (Bada Sourcery libstdc++ Rb_tree).
//
// TODO: 0x001b0600 -- AnimationList loader; full Animation struct layout
//                     pending R3 RE pass.
class AnimationList : public ReferenceCounter {
public:
    typedef std::map<AsciiString, Animation*> AnimMap;   // ptr-valued so fwd-decl is enough

    AnimationList() : m_AnimCount(0) {}

    AnimMap   m_Anims;        // +0x0C (12B map; counts at +0x20)
    uint32_t  m_AnimCount;    // +0x20 -- exposed by GetAnimIter(idx)
};

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_ANIMATIONLIST_H
