#ifndef FN_STUBS_GENERICHUDCONTROL_H
#define FN_STUBS_GENERICHUDCONTROL_H

// TODO: GenericHUDControl -- auto-generated symbol-coverage stub.
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

class GenericHUDControl {
public:
    // TODO: GenericHUDControl::PreDraw -- auto stub
    void PreDraw(float*);
    // TODO: GenericHUDControl::SetAngle -- auto stub
    void SetAngle(float, float);
    // TODO: GenericHUDControl::Update -- auto stub
    void Update(float);
};

}  // namespace Mortar


// Hoist into global scope to match the binary's class location.
using Mortar::GenericHUDControl;
#endif  // FN_STUBS_GENERICHUDCONTROL_H
