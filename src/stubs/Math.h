#ifndef FN_STUBS_MATH_H
#define FN_STUBS_MATH_H

// TODO: Math -- auto-generated symbol-coverage stub.
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

class Math {
public:
    // TODO: Math::ClosestPointOnLine -- auto stub
    void ClosestPointOnLine(Vec3&, Vec3&, Vec3&, Vec3&);
    // TODO: Math::LineIntersect -- auto stub
    void LineIntersect(Vec3&, Vec3&, Vec3&, Vec3&, Vec3&);
};

}  // namespace Mortar

#endif  // FN_STUBS_MATH_H
