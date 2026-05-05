#ifndef FN_STUBS_EFFECTGROUP_H
#define FN_STUBS_EFFECTGROUP_H

// TODO: EffectGroup -- auto-generated symbol-coverage stub.
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
#include "util/ReferenceCounter.h"
#include <cstdint>

// Forward decls for binary-shape arg types not yet ported here.
namespace Mortar {
  class Effect;
}

namespace Mortar {

class EffectGroup : public ReferenceCounter {
public:
    // TODO: EffectGroup::AddEffect -- auto stub
    void AddEffect(Mortar::SmartPtr<Mortar::Effect> const&);
    // TODO: EffectGroup::GetEffectIndex -- auto stub
    void GetEffectIndex(char const*) const;
};

}  // namespace Mortar

#endif  // FN_STUBS_EFFECTGROUP_H
