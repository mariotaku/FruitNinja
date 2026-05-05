#ifndef FN_STUBS_SPAWNER_INFO_H
#define FN_STUBS_SPAWNER_INFO_H

// TODO: SPAWNER_INFO -- auto-generated symbol-coverage stub.
//   Empty bodies; real binary implementations live at the
//   addresses listed in tmp/symbol-diff/missing_full_demangled.txt.
//   Replace each method with a real port over time.

#include "math/Vec3.h"
#include "math/Vec2.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include "util/Delegate.h"
#include "util/SmartPtr.h"
#include "util/AsciiString.h"
#include <cstdint>

namespace Mortar {

class SPAWNER_INFO {
public:
    // TODO: SPAWNER_INFO::GetRandCount -- auto stub
    void GetRandCount(float);
    // TODO: SPAWNER_INFO::Reset -- auto stub
    void Reset(float);
    // TODO: SPAWNER_INFO::ResetDelay -- auto stub
    void ResetDelay(float);
};

}  // namespace Mortar


// Hoist into global scope to match the binary's class location.
using Mortar::SPAWNER_INFO;
#endif  // FN_STUBS_SPAWNER_INFO_H
