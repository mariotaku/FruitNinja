#ifndef FN_STUBS_ACCELERATION_H
#define FN_STUBS_ACCELERATION_H

// TODO: Acceleration -- auto-generated symbol-coverage stub.
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

class Acceleration {
public:
    // TODO: Acceleration::Acceleration -- auto stub
    Acceleration();
    // TODO: Acceleration::Clear -- auto stub
    void Clear();
    // TODO: Acceleration::GetAccelAbs -- auto stub
    void GetAccelAbs(float&, float&, float&);
    // TODO: Acceleration::GetAccelDelta -- auto stub
    void GetAccelDelta(float&, float&, float&);
    // TODO: Acceleration::GetInstance -- auto stub
    void GetInstance();
    // TODO: Acceleration::__UpdateInternal -- auto stub
    void __UpdateInternal(float, float, float);
};

}  // namespace Mortar

#endif  // FN_STUBS_ACCELERATION_H
