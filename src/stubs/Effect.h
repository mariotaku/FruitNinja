#ifndef FN_STUBS_EFFECT_H
#define FN_STUBS_EFFECT_H

// TODO: Effect -- auto-generated symbol-coverage stub.
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
  class DataStreamReader;
}

namespace Mortar {

class Effect {
public:
    // TODO: Effect::Load -- auto stub
    void Load(char const*);
    // TODO: Effect::LoadEffects -- auto stub
    void LoadEffects(void const*, unsigned int);
    // TODO: Effect::Properties -- auto stub
    void Properties() const;
    // TODO: Effect::_LoadPlatformData -- auto stub
    void _LoadPlatformData(unsigned int, Mortar::DataStreamReader&);
};

}  // namespace Mortar

#endif  // FN_STUBS_EFFECT_H
