#ifndef FN_STUBS_GEOMETRY_H
#define FN_STUBS_GEOMETRY_H

// TODO: Geometry -- auto-generated symbol-coverage stub.
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
  class EffectGroup;
  class GeometryBinding;
  class SharedEffectProperties;
}

namespace Mortar {

class Geometry {
public:
    // TODO: Geometry::BuildPropList -- auto stub
    void BuildPropList(Mortar::SmartPtr<Mortar::SharedEffectProperties> const&);
    // TODO: Geometry::EffectGroupSet -- auto stub
    void EffectGroupSet(Mortar::SmartPtr<Mortar::EffectGroup> const&);
    // TODO: Geometry::Geometry -- auto stub
    Geometry(Mortar::SmartPtr<Mortar::GeometryBinding> const&, Mortar::SmartPtr<Mortar::SharedEffectProperties> const&);
    // TODO: Geometry::GetEffectIndex -- auto stub
    void GetEffectIndex(char const*) const;
    // TODO: Geometry::GetEffectIndex -- auto stub
    void GetEffectIndex(std::basic_string<char, std::char_traits<char>, std::allocator<char> > const&) const;
    // TODO: Geometry::GetProperty -- auto stub
    void GetProperty(char const*);
    // TODO: Geometry::GetProperty -- auto stub
    void GetProperty(char const*) const;
    // TODO: Geometry::HasActiveEffect -- auto stub
    void HasActiveEffect() const;
    // TODO: Geometry::Render -- auto stub
    void Render();
    // TODO: Geometry::SetActiveEffect -- auto stub
    void SetActiveEffect(unsigned int);
};

}  // namespace Mortar

#endif  // FN_STUBS_GEOMETRY_H
