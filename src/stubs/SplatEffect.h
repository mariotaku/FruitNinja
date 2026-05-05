#ifndef FN_STUBS_SPLATEFFECT_H
#define FN_STUBS_SPLATEFFECT_H

// TODO: SplatEffect -- auto-generated symbol-coverage stub.
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

class SplatEffect {
public:
    // TODO: SplatEffect::Draw -- auto stub
    void Draw();
    // TODO: SplatEffect::SplatEffect -- auto stub
    SplatEffect();
    // TODO: SplatEffect::Update -- auto stub
    void Update(float);
    // TODO: SplatEffect::~SplatEffect -- auto stub
    ~SplatEffect();
};

}  // namespace Mortar


// Hoist into global scope to match the binary's class location.
using Mortar::SplatEffect;
#endif  // FN_STUBS_SPLATEFFECT_H
