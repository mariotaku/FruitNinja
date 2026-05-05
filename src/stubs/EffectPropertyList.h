#ifndef FN_STUBS_EFFECTPROPERTYLIST_H
#define FN_STUBS_EFFECTPROPERTYLIST_H

// TODO: EffectPropertyList -- auto-generated symbol-coverage stub.
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
}

namespace Mortar {

class EffectPropertyList {
public:
    // TODO: EffectPropertyList::Contains -- auto stub
    void Contains(Mortar::EffectPropertyDefinition const&) const;
    // TODO: EffectPropertyList::GetProperty -- auto stub
    void GetProperty(char const*);
    // TODO: EffectPropertyList::GetProperty -- auto stub
    void GetProperty(char const*) const;
    // TODO: EffectPropertyList::SortProperties -- auto stub
    void SortProperties();
};

}  // namespace Mortar

#endif  // FN_STUBS_EFFECTPROPERTYLIST_H
