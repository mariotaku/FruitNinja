#ifndef FN_ENGINE_ASSET_DATAREADER_H
#define FN_ENGINE_ASSET_DATAREADER_H

#include <cstddef>
#include <cstring>
#include <vector>

// Mortar::DataReader -- abstract interface (pure-virtual base).
// Binary sizeof == 4 (vptr-only: one vtable pointer, no data members, no base class).
// v1.6.1 vtable @ 0x2D0298: [+0]=offset-to-top=0, [+4]=typeinfo @ 0x2D031C,
//   [+8]=slot0 == __cxa_pure_virtual (the only slot).
// Exactly ONE virtual slot (the pure-virtual Read). typeinfo @ 0x2D031C is plain
// __class_type_info (N6Mortar10DataReaderE) -- no base class, no virtual destructor in
// the vtable (the single slot IS Read, not a dtor).
// Ctor writes only the vptr (vtable_base + 8).
// Concrete subclasses: FileDataReader (v1.6.1 ctor @ 0x23B7EC),
//   VectorDataReader (v1.6.1 inline in Initialize @ 0x255578).
// Both override the single slot to supply the byte-reading primitive.

namespace Mortar {

class DataReader {
public:
    // Binary: ctor writes vptr only.
    DataReader();

    // Binary: NO virtual destructor in the vtable (the single slot is Read, not a dtor;
    // typeinfo is plain __class_type_info and subclass dtors are called explicitly, not
    // through the vtable). Kept non-virtual to preserve the binary's single-slot vtable.
    ~DataReader();

    // Binary vtable slot 0 (pure-virtual). Signature confirmed from
    // DataReader::ReadNative<unsigned long> @ 0x00255840, which invokes it as
    //   vtable[0](&retbuf, this, sizeof(T));    // retbuf == std::vector<unsigned char>
    // then reinterprets the first sizeof(T) bytes. The single virtual primitive reads
    // `count` bytes and returns them by value as a byte vector.
    // Concrete impls: FileDataReader (reads from File @ this+0x04),
    //   VectorDataReader (slices m_buf @ this+0x04 advancing m_pos @ this+0x10).
    // Binary vtable slot 0.
    virtual std::vector<unsigned char> Read(size_t count) = 0;

    // ReadNative<T> @ 0x00255840: calls Read(sizeof(T)) via vtable slot 0, then
    // reinterprets the first sizeof(T) bytes as T (raw LE memcpy).
    // ReadLE<T> @ 0x002558A0: branches on Endian::IsBigEndian(); on ARM-LE (and all
    // SDL targets) the branch is never taken and it reduces to ReadNative<T>.
    // DIFFERS: original branches on Endian::IsBigEndian(); LE-only port omits the
    //   big-endian path (ReadReversed<T>) since it is dead on all supported targets.
    template<typename T>
    T ReadLE() {
        std::vector<unsigned char> v = Read(sizeof(T));
        T val = T();
        if (v.size() >= sizeof(T)) memcpy(&val, v.data(), sizeof(T));
        return val;
    }
};

#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(DataReader) == 4, "DataReader sizeof mismatch (vptr-only abstract base)");
#endif

} // namespace Mortar

#endif
