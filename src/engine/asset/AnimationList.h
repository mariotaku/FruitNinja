#ifndef FN_ENGINE_ASSET_ANIMATIONLIST_H
#define FN_ENGINE_ASSET_ANIMATIONLIST_H

// Analysed: 2026-05-04T00:00

#include "util/ReferenceCounter.h"
#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include <map>
#include <vector>
#include <cstdint>

namespace Mortar {

// BSplineKnots -- embedded within VectorTrack at +0x00..+0x13. (sizeof 0x14)
// This is the struct UpdateBinding<N> receives as *param_2.
// Addresses: ctor zeroes +0x00/+0x04/+0x08/+0x10, sets *(u16*)(this+0x0e)=1.
struct BSplineKnots {
    // std::vector<float> knots -- knot-vector time positions
    float* m_knots_begin;  // +0x00
    float* m_knots_end;    // +0x04
    float* m_knots_cap;    // +0x08
    // NOTE: +0x0c and +0x0e overlap the switch tag / dim stored in VectorTrack;
    // BSplineKnots as a standalone struct is not instantiated separately; it is
    // always the embedded prefix of a VectorTrack.
    uint16_t m_channelType; // +0x0c -- switch tag; cases 0..5 -> UpdateBinding<N>
    uint16_t m_dim;         // +0x0e -- components per sample; default 1
    float*   m_data;        // +0x10 -- sample matrix: m_dim floats per knot row
};
// sizeof BSplineKnots == 0x14

// AnimTrackGroup::VectorTrack -- sizeof 0x3c.
// ctor @ 0x0026fab8, dtor @ 0x00109e6c.
// First 0x14 bytes form an embedded BSplineKnots (knot vector + channelType + dim + data ptr).
// m_targetName at +0x14 is passed as arg2 to Model/Mesh::GenerateBindings.
struct VectorTrack {
    // Embedded BSplineKnots prefix (UpdateBinding receives ptr to this struct
    // as its BSplineKnots* parameter):
    float*   m_knots_begin;  // +0x00  knots._begin
    float*   m_knots_end;    // +0x04  knots._end
    float*   m_knots_cap;    // +0x08  knots._cap
    uint16_t m_channelType;  // +0x0c  switch tag -> UpdateBinding<N>
    uint16_t m_dim;          // +0x0e  components per sample; ctor default = 1
    float*   m_data;         // +0x10  sample matrix: dim * sizeof(float) per knot

    AsciiString m_targetName; // +0x14  (0x28 bytes) -- mesh/node target name

    VectorTrack()
        : m_knots_begin(0), m_knots_end(0), m_knots_cap(0)
        , m_channelType(0), m_dim(1), m_data(0)
    {}
};
// sizeof VectorTrack == 0x14 + 0x28 == 0x3c

// AnimTrackGroup -- sizeof 0x34.
// ctor @ 0x0026fa90, dtor @ 0x002367e8.
struct AnimTrackGroup {
    AsciiString m_name;                    // +0x00 (0x28 bytes)
    std::vector<VectorTrack> m_vectorTracks; // +0x28 (12 bytes)

    AnimTrackGroup() {}
};
// sizeof AnimTrackGroup == 0x28 + 0x0c == 0x34

// Animation -- plain aggregate, NO vtable.
// Stored BY VALUE in AnimationList::m_Anims (confirmed by ~AnimationList @ 0x00270c44).
// rb-tree node layout: node+0x10 = pair.first (AsciiString key, 0x28B),
//                      node+0x38 = pair.second (Animation value, 0x14B).
// sizeof 0x14.
struct Animation {
    float                     m_duration;    // +0x00  SetTime reads node+0x38 = Animation+0x00
    uint32_t                  pad_04;        // +0x04  unused gap
    std::vector<AnimTrackGroup> m_trackGroups; // +0x08..+0x14 (12B vector)

    Animation() : m_duration(0.0f), pad_04(0) {}
};
// sizeof Animation == 4 + 4 + 12 == 0x14

// Mortar::AnimationList -- refcounted resource holding named animations.
// Binary vtable @ 0x001ebc58; sizeof 0x24 (36).
// Layout:
//   +0x00  Mortar::ReferenceCounter (12B: vptr + strong count + weak count)
//   +0x0C  std::map<AsciiString, Animation> m_Anims (24B -- std::map with cached _M_node_count)
// Total: 0x24 (36)
//
// Map value type is BY VALUE (Animation aggregate, no vtable):
// confirmed by ~AnimationList @ 0x00270c44 which destructs pair<AsciiString const, Animation>
// (pair dtor @ 0x00270bd8 destroys vector<AnimTrackGroup> at pair+0x30 and AsciiString at pair+0x00).
//
// GetAnimation @ 0x0026ec28 returns node+0x10 (start of pair); AnimationState reads
// m_duration at node+0x38 (= Animation+0x00) and m_trackGroups at node+0x40/+0x44.
//
// NOTE: the resource loader entry point is Mortar::ResourceLoader::Load<AnimationList> @ 0x001b0600,
// which is a ResourceLoader template instantiation (Enter CS -> GetLoaders()[typeid] ->
// helper->Load() -> SmartPtr::Cast<AnimationList>), NOT a member of this class.
class AnimationList : public Mortar::ReferenceCounter {
public:
    // std::map<AsciiString, Animation> uses AsciiString::operator< -> Compare(),
    // which is non-lex (length-first, hash-second, memcmp-third). Matches binary ordering.
    typedef std::map<AsciiString, Animation> AnimMap;

    AnimationList() {}

    AnimMap m_Anims;  // +0x0C (24B std::map with cached _M_node_count)
};

#if defined(__bada__)
static_assert(sizeof(BSplineKnots)   == 0x14, "BSplineKnots sizeof mismatch");
static_assert(sizeof(VectorTrack)    == 0x3c, "VectorTrack sizeof mismatch");
static_assert(sizeof(AnimTrackGroup) == 0x34, "AnimTrackGroup sizeof mismatch");
static_assert(sizeof(Animation)      == 0x14, "Animation sizeof mismatch");
static_assert(sizeof(AnimationList)  == 0x24,
              "AnimationList sizeof mismatch (ReferenceCounter 12B + std::map 24B)");
#endif

}  // namespace Mortar

#endif // FN_ENGINE_ASSET_ANIMATIONLIST_H
