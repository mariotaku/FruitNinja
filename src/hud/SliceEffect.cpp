// SliceEffect.cpp -- keyframe table constant only.
//
// AddSlice, DrawSlices, and the slice-state fields (s_slices, s_pool, s_sliceModel[4])
// all live in Fruit.cpp as part of FruitGlobalData g_fruitData, matching the binary
// layout @ 0x332910 where the pool and list are owned by the fruit subsystem.
//
// v1.6.1 binary refs:
//   AddSlice   @0x001dc990  (Fruit TU)
//   DrawSlices @0x001dae7c  (Fruit TU)

#include "SliceEffect.h"
#include "math/_Vector3.h"

// SLICE_KEYFRAMES -- 7 scale keyframes for the slash-line animation.
// Initialised by _GLOBAL__I_GameTask.cpp static ctor @0x0016d0dc.
// Verified raw values from binary:
//   DAT_0016d3ec = 0x3fd9999a = 1.700
//   DAT_0016d3f0 = 0x3e99999a = 0.300
//   DAT_0016d3f4 = 0x3dcccccd = 0.100
const _Vector3<float> SLICE_KEYFRAMES[7] = {
    _Vector3<float>(1.0f, 1.0f, 1.0f), // frame 0 -- circle blob
    _Vector3<float>(1.7f, 0.3f, 1.0f), // frame 1 -- beginning to stretch
    _Vector3<float>(8.0f, 0.1f, 1.0f), // frame 2 -- thin line
    _Vector3<float>(20.0f, 0.1f, 1.0f), // frame 3 -- max stretch
    _Vector3<float>(4.0f, 0.1f, 1.0f), // frame 4 -- snapping back
    _Vector3<float>(0.1f, 0.1f, 0.1f), // frame 5 -- near-invisible
    _Vector3<float>(0.1f, 0.1f, 0.1f), // frame 6 -- fully collapsed
};
