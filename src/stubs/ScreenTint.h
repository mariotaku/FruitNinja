#ifndef FN_STUBS_SCREENTINT_H
#define FN_STUBS_SCREENTINT_H

// TODO: ScreenTint -- auto-generated symbol-coverage stub.
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
  class TiXmlElement;
}

namespace Mortar {

class ScreenTint {
public:
    // TODO: ScreenTint::Parse -- auto stub
    void Parse(TiXmlElement*);
};

}  // namespace Mortar

#endif  // FN_STUBS_SCREENTINT_H
