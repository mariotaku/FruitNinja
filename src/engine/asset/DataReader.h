#ifndef FN_ENGINE_ASSET_DATAREADER_H
#define FN_ENGINE_ASSET_DATAREADER_H

#include <cstddef>
#include <cstring>
#include <vector>

#include "util/Endian.h"

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
    // SDL/web targets) the branch is never taken and it reduces to ReadNative<T>.
    // On FN_BIG_ENDIAN targets (Wii) the branch IS taken: the bytes on disk are
    // still little-endian (DIFFERS note below), so the loaded value is byteswapped
    // to native (big-endian) order after the raw memcpy.
    // DIFFERS: original branches on Endian::IsBigEndian(); LE-only targets (host/web/
    //   asm-verify) omit the big-endian path (ReadReversed<T>) since it is dead there.
    //   Port specific: FN_BIG_ENDIAN (Wii) activates a byteswap-after-load instead of
    //   porting the binary's separate ReadReversed<T> byte-assembly, since the two are
    //   observably equivalent (swap-after-native-load == assemble-in-reverse-order) and
    //   ReadReversed<T>'s body was never RE'd (dead on every SKU the binary shipped on).
    template<typename T>
    T ReadLE() {
        std::vector<unsigned char> v = Read(sizeof(T));
        T val = T();
        if (v.size() >= sizeof(T)) memcpy(&val, v.data(), sizeof(T));
#if defined(FN_BIG_ENDIAN)
        if (sizeof(T) == 2) {
            uint16_t tmp; memcpy(&tmp, &val, 2);
            tmp = Endian::fnByteSwap16(tmp);
            memcpy(&val, &tmp, 2);
        } else if (sizeof(T) == 4) {
            uint32_t tmp; memcpy(&tmp, &val, 4);
            tmp = Endian::fnByteSwap32(tmp);
            memcpy(&val, &tmp, 4);
        } else if (sizeof(T) == 8) {
            uint64_t tmp; memcpy(&tmp, &val, 8);
            tmp = Endian::fnByteSwap64(tmp);
            memcpy(&val, &tmp, 8);
        }
#endif
        return val;
    }
};

#if defined(__bada__)
static_assert(sizeof(DataReader) == 4, "DataReader sizeof mismatch (vptr-only abstract base)");
#endif

} // namespace Mortar

#endif
