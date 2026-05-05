#ifndef FN_STUBS_INDEXSTREAMBASIC_H
#define FN_STUBS_INDEXSTREAMBASIC_H

// TODO: IndexStreamBasic -- auto-generated symbol-coverage stub.
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
  class IIndexSource;
}

namespace Mortar {

class IndexStreamBasic {
public:
    // TODO: IndexStreamBasic::IndexCount -- auto stub
    void IndexCount() const;
    // TODO: IndexStreamBasic::IndexSize -- auto stub
    void IndexSize() const;
    // TODO: IndexStreamBasic::IndexStreamBasic -- auto stub
    IndexStreamBasic(Mortar::SmartPtr<Mortar::IIndexSource> const&);
    // TODO: IndexStreamBasic::IsBigEndian -- auto stub
    void IsBigEndian() const;
    // TODO: IndexStreamBasic::PrepareStream -- auto stub
    void PrepareStream();
    // TODO: IndexStreamBasic::PrimitiveType -- auto stub
    void PrimitiveType() const;
    // TODO: IndexStreamBasic::SetSource -- auto stub
    void SetSource(Mortar::SmartPtr<Mortar::IIndexSource> const&);
    // TODO: IndexStreamBasic::~IndexStreamBasic -- auto stub
    ~IndexStreamBasic();
};

}  // namespace Mortar

#endif  // FN_STUBS_INDEXSTREAMBASIC_H
