#ifndef FN_STUBS_GEOMETRYBINDING_H
#define FN_STUBS_GEOMETRYBINDING_H

// TODO: GeometryBinding -- auto-generated symbol-coverage stub.
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
  class IIndexStream;
  class IVertexStream;
}

namespace Mortar {

class GeometryBinding {
public:
    // TODO: GeometryBinding::EffectGroupSet -- auto stub
    void EffectGroupSet(Mortar::SmartPtr<Mortar::EffectGroup> const&);
    // TODO: GeometryBinding::GeometryBinding -- auto stub
    GeometryBinding();
    // TODO: GeometryBinding::IndexStreamSet -- auto stub
    void IndexStreamSet(Mortar::SmartPtr<Mortar::IIndexStream> const&, std::basic_string<char, std::char_traits<char>, std::allocator<char> > const&);
    // TODO: GeometryBinding::VertexStreamAdd -- auto stub
    void VertexStreamAdd(Mortar::SmartPtr<Mortar::IVertexStream> const&);
    // TODO: GeometryBinding::VertexStreamRemove -- auto stub
    void VertexStreamRemove(Mortar::SmartPtr<Mortar::IVertexStream> const&);
};

}  // namespace Mortar

#endif  // FN_STUBS_GEOMETRYBINDING_H
