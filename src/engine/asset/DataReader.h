#ifndef FN_ENGINE_ASSET_DATAREADER_H
#define FN_ENGINE_ASSET_DATAREADER_H

#include <cstddef>
#include <vector>

// Mortar::DataReader -- abstract interface (pure-virtual base).
// Binary sizeof == 4 (vptr-only: one vtable pointer, no data members, no base class).
// Vtable @ 0x001eba50 (offset-to-top=0 @ 0x001eba48, typeinfo @ 0x001eba4c->0x001eba3c,
//   slot 0 @ 0x001eba50 == __cxa_pure_virtual @ 0x002773d0).
// Exactly ONE virtual slot (the pure-virtual Read). typeinfo @ 0x001eba3c is plain
// __class_type_info (N6Mortar10DataReaderE) -- no base class, no virtual destructor in
// the vtable (the single slot IS Read, not a dtor).
// Ctor @ 0x001a8860 writes only the vptr (vtable_base + 8).
// Concrete subclasses: FileDataReader (ctor @ 0x001aa630), VectorDataReader (ctor @ 0x001b5008).
// Both override the single slot to supply the byte-reading primitive.

namespace Mortar {

class DataReader {
public:
    // Binary @ 0x001a8860 (ctor writes vptr; only thing the base ctor does)
    DataReader();

    // Binary: NO virtual destructor in the vtable (the single slot is Read, not a dtor;
    // typeinfo is plain __class_type_info and subclass dtors are called explicitly, not
    // through the vtable). Kept non-virtual to preserve the binary's single-slot vtable.
    ~DataReader();

    // Binary vtable slot 0 @ 0x001eba50 (pure-virtual). Signature recovered from
    // Mortar::DataReader::ReadNative<T> @ 0x001b4c80, which invokes it as
    //   vtable[0](&retbuf, this, sizeof(T));    // retbuf == std::vector<unsigned char>
    // then reads the requested bytes back out of the returned vector. The single virtual
    // primitive therefore reads `count` bytes and returns them by value as a byte vector.
    // Concrete impls: FileDataReader (reads from File @ this+4), VectorDataReader
    // (slices the backing std::vector @ this+4 advancing the position cursor @ this+0x10).
    // Binary @ 0x001eba50
    virtual std::vector<unsigned char> Read(size_t count) = 0;
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(DataReader) == 4, "DataReader sizeof mismatch (vptr-only abstract base)");
#endif

} // namespace Mortar

#endif
