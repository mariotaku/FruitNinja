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
//   +0x0C  std::map<AsciiString, Animation*> m_Anims (24B — std::map with cached _M_node_count)
// Total: 0x24 (36)
//
// Binary map value type: Mortar::Animation (value per demangled name), but since
// Animation struct is not fully RE'd and the binary's GetAnimation methods return
// Animation*, the port uses Animation* (pointer) to allow forward-declaration.
// TODO: 0x001b0600 -- AnimationList loader; full Animation struct layout
//                     pending R3 RE pass. Confirm map value type (value vs ptr).
class AnimationList : public Mortar::ReferenceCounter {
public:
    // std::map<AsciiString, Animation*> uses AsciiString::operator< -> Compare(),
    // which is non-lex (length-first, hash-second, memcmp-third). This matches the
    // binary's std::map ordering.
    // DIFFERS: binary map value may be Mortar::Animation (value) not Mortar::Animation*.
    //   Port uses pointer to allow forward-declaration; will be corrected once Animation
    //   struct is fully RE'd.
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
