#ifndef FN_STUBS_TIMEKEEPER_H
#define FN_STUBS_TIMEKEEPER_H

// TODO: TimeKeeper -- auto-generated symbol-coverage stub.
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

class TimeKeeper {
public:
    // TODO: TimeKeeper::ProcessTime -- auto stub
    void ProcessTime(float);
    // TODO: TimeKeeper::SetSpeedScale -- auto stub
    void SetSpeedScale(float);
    // TODO: TimeKeeper::TimeKeeper -- auto stub
    TimeKeeper();
    // TODO: TimeKeeper::~TimeKeeper -- auto stub
    ~TimeKeeper();
};

}  // namespace Mortar


// Hoist into global scope to match the binary's class location.
using Mortar::TimeKeeper;
#endif  // FN_STUBS_TIMEKEEPER_H
