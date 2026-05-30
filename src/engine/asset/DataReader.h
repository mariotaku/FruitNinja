#ifndef FN_ENGINE_ASSET_DATAREADER_H
#define FN_ENGINE_ASSET_DATAREADER_H

#include <cstddef>

// Mortar::DataReader -- abstract interface (pure-virtual base).
// Binary sizeof == 4 (vptr-only: one vtable pointer, no data members, no base class).
// Vtable @ 0x001eba50; typeinfo @ 0x001eba3c (N6Mortar10DataReaderE).
// One pure-virtual slot (-> __cxa_pure_virtual @ 0x002773d0).
// Ctor @ 0x001a8860 writes only the vptr. No inheritance (typeinfo is __class_type_info).
// Concrete subclasses override the single virtual slot to provide the actual Read impl.

namespace Mortar {

class DataReader {
public:
    // Binary @ 0x001a8860 (ctor writes vptr; only thing the base ctor does)
    DataReader();
    // Binary: virtual dtor at slot 0
    virtual ~DataReader();

    // Binary vtable slot 1: pure-virtual; concrete subclasses provide the implementation.
    // TODO: 0x001eba50 -- resolve pure-virtual slot signature (Read or Advance or similar).
    virtual void Read() = 0;
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(DataReader) == 4, "DataReader sizeof mismatch (vptr-only abstract base)");
#endif

} // namespace Mortar

#endif
