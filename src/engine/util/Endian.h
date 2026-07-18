#ifndef FN_ENGINE_UTIL_ENDIAN_H
#define FN_ENGINE_UTIL_ENDIAN_H

#include <stdint.h>
#include <cstring>

// Endian -- platform endianness query namespace.
//
// Binary: Endian::GetEndian(), Endian::IsBigEndian() referenced in
//   DataReader.h (ReadLE @ 0x002558A0 branches on IsBigEndian()) and
//   DataStreamReader (ReadBasicType compares GetEndian() vs m_Endian).
//   Endian::ConvertFromLittle<T> referenced in ResourceLoader.cpp comment
//   @ 0x002553cc.
//
// TODO: v1.6.1 -- RE full Endian namespace: exact enum value constants for
//   Endianness (LITTLE/BIG), ConvertFromLittle<T> body, any other members.
//   All SDL2/x86/wasm32 targets are little-endian; the BE path is dead there
//   but must be modelled to match the binary's conditional. It is LIVE on the
//   Wii port target (PowerPC, big-endian) -- see FN_BIG_ENDIAN below.
//
// Port note: every on-disk asset format (.tex/.mad/.mmd/string tables/save
// numeric blobs) and all RE'd struct layouts are little-endian, matching the
// original ARM Bada device. Host/web targets (x86, x86_64, wasm32) are also
// little-endian, so GetEndian() returns LITTLE there and the swap paths below
// are dead code (compiled out). On Wii (PowerPC, big-endian) GetEndian()
// returns BIG, which activates DataStreamReader::ReadBasicType's existing
// swap and the FN_BIG_ENDIAN-gated swaps added to the native-load readers.
//
// Port specific: FN_BIG_ENDIAN gate. Not from the binary -- the original
// ARM Bada target is always little-endian, so this macro has no binary
// counterpart. It exists solely so the Wii (big-endian PowerPC) port can
// byteswap little-endian on-disk data at the file-read boundary while
// leaving every little-endian target (host/web/asm-verify) untouched.
#if defined(FRUIT_PLATFORM_WII) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define FN_BIG_ENDIAN 1
#endif

namespace Endian {

// Endianness enum stored in DataStreamReader::m_Endian (+0x0c, uint32_t field).
// TODO: v1.6.1 -- confirm binary enum values via binary symbol table / disassembly.
//   LITTLE = 0 and BIG = 1 are assumed consistent with common convention.
enum Endianness {
    LITTLE = 0,
    BIG    = 1
};

// GetEndian() -- returns the native host byte order.
// Binary @ 0x002558A0 (ReadLE) calls this indirectly via IsBigEndian().
// On little-endian port targets (host/web/asm-verify) this always returns
// LITTLE. On FN_BIG_ENDIAN targets (Wii) it returns BIG, which is what
// activates DataStreamReader::ReadBasicType's existing (binary-modelled)
// swap path.
inline Endianness GetEndian() {
#if defined(FN_BIG_ENDIAN)
    return BIG;
#else
    return LITTLE;
#endif
}

// IsBigEndian() -- convenience wrapper around GetEndian().
// Binary @ 0x002558A0 branches on this; branch is never taken on LE targets,
// live on FN_BIG_ENDIAN targets (Wii).
inline bool IsBigEndian() {
    return GetEndian() == BIG;
}

// ---------------------------------------------------------------------
// Port specific: byteswap helpers. No binary counterpart (the ARM Bada
// target never needed them) -- added so the native-load primitive readers
// (DataReader::ReadLE, StringTable::LoadHeader/LoadLanguage, MeshManager's
// PSP stream header reads, TextureFileFormat's raw header memcpy reads) can
// byteswap little-endian on-disk values at the file-read boundary on
// FN_BIG_ENDIAN targets. Unused (and left undefined) on little-endian
// targets to keep those builds byte-for-byte unchanged.
// ---------------------------------------------------------------------
#if defined(FN_BIG_ENDIAN)

inline uint16_t fnByteSwap16(uint16_t v) {
    return (uint16_t)((v >> 8) | (v << 8));
}

inline uint32_t fnByteSwap32(uint32_t v) {
    return ((v >> 24) & 0x000000FFu)
         | ((v >>  8) & 0x0000FF00u)
         | ((v <<  8) & 0x00FF0000u)
         | ((v << 24) & 0xFF000000u);
}

inline uint64_t fnByteSwap64(uint64_t v) {
    return ((v >> 56) & 0x00000000000000FFull)
         | ((v >> 40) & 0x000000000000FF00ull)
         | ((v >> 24) & 0x0000000000FF0000ull)
         | ((v >>  8) & 0x00000000FF000000ull)
         | ((v <<  8) & 0x000000FF00000000ull)
         | ((v << 24) & 0x0000FF0000000000ull)
         | ((v << 40) & 0x00FF000000000000ull)
         | ((v << 56) & 0xFF00000000000000ull);
}

// Float swap: reinterpret the 4-byte pattern, swap as uint32_t, reinterpret back.
inline float fnByteSwapFloat(float v) {
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    bits = fnByteSwap32(bits);
    float out;
    memcpy(&out, &bits, sizeof(out));
    return out;
}

// fnByteSwap(T) overload set -- scalar dispatch used by CopyArrayLE<T> below.
// Mirrors Mortar::ResourceLoader's IntToType-tag ByteSwapInPlace dispatch,
// but as a return-by-value overload set (no tag type needed since these are
// free functions, not template-dispatched on sizeof at a single call site).
inline uint16_t fnByteSwap(uint16_t v) { return fnByteSwap16(v); }
inline int16_t  fnByteSwap(int16_t v)  { return (int16_t)fnByteSwap16((uint16_t)v); }
inline uint32_t fnByteSwap(uint32_t v) { return fnByteSwap32(v); }
inline int32_t  fnByteSwap(int32_t v)  { return (int32_t)fnByteSwap32((uint32_t)v); }
inline float    fnByteSwap(float v)    { return fnByteSwapFloat(v); }
inline uint64_t fnByteSwap(uint64_t v) { return fnByteSwap64(v); }
inline int64_t  fnByteSwap(int64_t v)  { return (int64_t)fnByteSwap64((uint64_t)v); }

#endif // FN_BIG_ENDIAN

// ---------------------------------------------------------------------
// Port specific: canonical asm-verify-facing typed-read macros.
//
// These are THE choke point for "read a little-endian scalar/array off disk"
// and are deliberately macros, not inline functions/templates: the asm-verify
// cross-build compiles the LE path (Bada target has no FN_BIG_ENDIAN) and its
// generated ASM must byte-match the original binary's plain raw read with
// NO extra call/branch/loop overhead. A macro guarantees the LE expansion is
// textually the bare operation the binary does -- an aligned pointer read
// (FN_READ_U16/U32/F32) or a straight memcpy (FN_READ_ARRAY) -- with zero risk
// of the compiler leaving behind a stray call frame or failing to fully
// inline a template. Under FN_BIG_ENDIAN (Wii) the same macros add the
// fnByteSwap* call; LE callers (Bada/host/web) never see that branch at all
// (the swap arm is not just dead-code-eliminated, it isn't even *emitted*
// as source on the LE expansion -- see the #else arm of each macro).
//
// dst/src are expected to already be the correct pointer type; casts are
// left to the call site (mirrors the binary's raw pointer arithmetic).
// ---------------------------------------------------------------------
#if defined(FN_BIG_ENDIAN)
#define FN_READ_U16(ptr)   (::Endian::fnByteSwap16(*(const uint16_t*)(ptr)))
#define FN_READ_U32(ptr)   (::Endian::fnByteSwap32(*(const uint32_t*)(ptr)))
#define FN_READ_F32(ptr)   (::Endian::fnByteSwapFloat(*(const float*)(ptr)))
#define FN_READ_U64(ptr)   (::Endian::fnByteSwap64(*(const uint64_t*)(ptr)))
#else
// Byte-faithful: identical to the binary's plain aligned read. No swap, no
// function call -- this is what the asm-verify cross-build compiles.
#define FN_READ_U16(ptr)   (*(const uint16_t*)(ptr))
#define FN_READ_U32(ptr)   (*(const uint32_t*)(ptr))
#define FN_READ_F32(ptr)   (*(const float*)(ptr))
#define FN_READ_U64(ptr)   (*(const uint64_t*)(ptr))
#endif

// FN_READ_ARRAY(dst, src, type, count) -- copy `count` elements of `type`
// from `src` into `dst`. LE expansion is the bare memcpy the binary's
// ReadBytes-style raw copy already does. BE expansion additionally
// byteswaps each element in place after the copy.
#if defined(FN_BIG_ENDIAN)
#define FN_READ_ARRAY(dst, src, type, count)                                 \
    do {                                                                     \
        size_t fn_read_array_n_ = (size_t)(count);                           \
        memcpy((dst), (src), fn_read_array_n_ * sizeof(type));               \
        for (size_t fn_read_array_i_ = 0; fn_read_array_i_ < fn_read_array_n_; ++fn_read_array_i_) { \
            (dst)[fn_read_array_i_] = ::Endian::fnByteSwap((dst)[fn_read_array_i_]);                 \
        }                                                                     \
    } while (0)
#else
// Byte-faithful: identical to the binary's plain memcpy. No swap loop.
#define FN_READ_ARRAY(dst, src, type, count)                                 \
    memcpy((dst), (src), (size_t)(count) * sizeof(type))
#endif

// ---------------------------------------------------------------------
// Port specific: CopyArrayLE<T> -- typed-array copy convenience wrapper
// around FN_READ_ARRAY, for callers that prefer a function call over the
// macro (e.g. passing a template-deduced T without spelling it twice).
// This wrapper is ergonomics-only; it is NOT the asm-verify-facing surface
// -- FN_READ_ARRAY above is. On LE, CopyArrayLE's body reduces to exactly
// the same single memcpy FN_READ_ARRAY expands to (no swap code emitted at
// all, since the #if FN_BIG_ENDIAN arm is absent from the LE translation
// unit), so using either at a call site produces identical LE codegen; the
// macro is preferred at any call site the asm-verify cross-build measures.
// ---------------------------------------------------------------------
template<typename T>
inline void CopyArrayLE(T* dst, const void* src, size_t count) {
    FN_READ_ARRAY(dst, src, T, count);
}

} // namespace Endian

namespace Mortar {
namespace Endian {

// Mortar::Endian::Endianness -- distinct nested duplicate of ::Endian::Endianness.
//
// The v1.6.1 binary has two different mangled spellings for the same enum
// depending on which TU/declaration a call site saw:
//   DataStreamReader::SetSource            @0x00250bdc -> N6Endian10EndiannessE       (global-scope Endian)
//   DataStreamReader::DataStreamReader(...) @0x00250bf4 -> NS_6Endian10EndiannessE     (Mortar::Endian, nested)
// This is a real ODR/mangling quirk in the original Mortar engine, not a port bug.
// To pair both symbols' mangled names exactly, the port models both spellings as
// distinct enum types: the global one (above) for SetSource, and this nested one
// for the ctor. Values are kept in lockstep with ::Endian::Endianness; convert with
// a C-style cast at the one call site (the ctor delegating to SetSource).
enum Endianness {
    LITTLE = ::Endian::LITTLE,
    BIG    = ::Endian::BIG
};

// Callers inside `namespace Mortar` write `Endian::fnByteSwap*`, which resolves
// to THIS nested namespace, not global `::Endian`. Re-export the global swap
// helpers here so those call sites resolve without every one qualifying `::`.
#if defined(FN_BIG_ENDIAN)
using ::Endian::fnByteSwap16;
using ::Endian::fnByteSwap32;
using ::Endian::fnByteSwap64;
using ::Endian::fnByteSwapFloat;
using ::Endian::fnByteSwap;
#endif
using ::Endian::CopyArrayLE;

} // namespace Endian
} // namespace Mortar

#endif  // FN_ENGINE_UTIL_ENDIAN_H
