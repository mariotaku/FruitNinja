// ASM-spec v1.6.1 Mortar::LoadAnims @0x0026f3fc

#include "asset/AnimationList.h"
#include "asset/ResourceLoader.h"
#include "util/SmartPtr.h"
#include <cstring>
#include <vector>

namespace Mortar {

// VectorTrack dtor @ 0x00109e6c.
// Frees the flat sample array (m_data) and destroys the knots std::vector
// overlay (m_knots_begin/end/cap are the raw internals of a std::vector<float>
// on ARM32 libstdc++; reinterpret_cast lets its destructor release the memory).
VectorTrack::~VectorTrack() {
    delete[] m_data;
    reinterpret_cast<std::vector<float>*>(&m_knots_begin)->~vector<float>();
}

// Mortar::LoadAnims -- deserializes an AnimationList from a ResourceLoader stream.
// Called as a delegate from AnimationManager::LoadAnimInternal @0x0026efa8.
// Mangled: _ZN6Mortar9LoadAnimsERNS_14ResourceLoaderE
// ASM-spec v1.6.1 Mortar::LoadAnims @0x0026f3fc
SmartPtr<AnimationList> LoadAnims(ResourceLoader& rl) {
    AnimationList* list = new AnimationList();   // operator_new(0x24)

    uint32_t animCount = rl.Read<uint32_t>();
    for (uint32_t a = 0; a < animCount; ++a) {
        AsciiString animName = rl.Read<AsciiString>();
        Animation& anim = list->m_Anims[animName];  // std::map::operator[]

        anim.m_duration = rl.Read<float>();     // Animation+0x00
        rl.Read<float>();                       // Animation+0x04 (pad_04, read and discard)

        uint32_t groupCount = rl.Read<uint32_t>();
        anim.m_trackGroups.resize(groupCount);

        for (uint32_t g = 0; g < groupCount; ++g) {
            AnimTrackGroup& grp = anim.m_trackGroups[g];  // stride 0x34

            AsciiString groupName = rl.Read<AsciiString>();
            grp.m_name.Set(groupName);

            uint32_t trackCount = rl.Read<uint32_t>();
            grp.m_vectorTracks.resize(trackCount);

            for (uint32_t t = 0; t < trackCount; ++t) {
                VectorTrack& trk = grp.m_vectorTracks[t];  // stride 0x3c

                trk.m_targetName  = rl.Read<AsciiString>(); // VectorTrack+0x14
                trk.m_channelType = rl.Read<uint8_t>();      // VectorTrack+0x0c
                trk.m_dim         = rl.Read<uint8_t>();      // VectorTrack+0x0e

                if (trk.m_dim != 0) {
                    uint16_t numKnots = rl.Read<uint16_t>();

                    // Flat sample matrix: dim*numKnots floats, zero-filled.
                    uint32_t dataSize = (uint32_t)trk.m_dim * numKnots;
                    trk.m_data = new float[dataSize];
                    memset(trk.m_data, 0, dataSize * sizeof(float));
                    for (uint32_t i = 0; i < dataSize; ++i) {
                        trk.m_data[i] = rl.Read<float>();
                    }

                    // m_knots_begin/end/cap are the internal storage of a
                    // std::vector<float> (ARM32 libstdc++ 3-pointer layout).
                    // Binary: std::vector<float>::resize(VectorTrack+0x00, numKnots, 0.0f).
                    std::vector<float>* knotsVec =
                        reinterpret_cast<std::vector<float>*>(&trk.m_knots_begin);
                    knotsVec->resize(numKnots);
                    for (uint32_t k = 0; k < numKnots; ++k) {
                        (*knotsVec)[k] = rl.Read<float>();
                    }
                }
            }
        }
    }

    return WrapPtr(list);
}

} // namespace Mortar
