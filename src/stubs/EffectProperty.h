#ifndef FN_STUBS_EFFECTPROPERTY_H
#define FN_STUBS_EFFECTPROPERTY_H

// TODO: EffectProperty -- auto-generated symbol-coverage stub.
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

// Forward decls for binary-shape arg types not yet ported here.
namespace Mortar {
  class EffectPropertyDefinition;
  class EffectPropertyValues;
}

namespace Mortar {

class EffectProperty {
public:
    // TODO: EffectProperty::EffectProperty -- auto stub
    EffectProperty(Mortar::EffectPropertyDefinition const&, Mortar::EffectPropertyValues&, unsigned int);
};

}  // namespace Mortar

#endif  // FN_STUBS_EFFECTPROPERTY_H
