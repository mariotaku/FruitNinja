#ifndef FN_ENGINE_UTIL_ENDIAN_H
#define FN_ENGINE_UTIL_ENDIAN_H

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
//   All SDL2 targets are little-endian; the BE path is dead but must be
//   modelled to match the binary's conditional.
//
// Port note: ARM Bada device is little-endian; all supported SDL2 targets
// (x86, x86_64, Emscripten/wasm32) are also little-endian. GetEndian()
// always returns LITTLE and IsBigEndian() always returns false. The endian-
// swap path in ReadBasicType is compiled but never executed.

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
// On all port targets (LE only) this always returns LITTLE.
inline Endianness GetEndian() {
    return LITTLE;
}

// IsBigEndian() -- convenience wrapper around GetEndian().
// Binary @ 0x002558A0 branches on this; branch is never taken on LE targets.
inline bool IsBigEndian() {
    return GetEndian() == BIG;
}

} // namespace Endian

#endif  // FN_ENGINE_UTIL_ENDIAN_H
