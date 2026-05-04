// Cross-build stub for util/Delegate.h.
// The real Mortar::Delegate template uses noexcept (C++0x feature added in
// GCC 4.6) and other modern C++ idioms that GCC 4.4/4.5 can't parse.
// For asm-verification of game-logic .cpp we don't need the real template,
// only the type names; provide opaque stubs.
#ifndef MORTAR_DELEGATE_H
#define MORTAR_DELEGATE_H

#include <cstddef>
#include <type_traits>

namespace Mortar {

template<typename Sig> class Delegate;

template<typename R, typename... Args>
class Delegate<R(Args...)> {
public:
    Delegate() {}
    Delegate(decltype(nullptr)) {}
    R operator()(Args...) const { return R(); }
    operator bool() const { return false; }
    template<typename T>
    static Delegate Make(T*, R (T::*)(Args...)) { return Delegate(); }
    static Delegate MakeFree(R (*)(Args...)) { return Delegate(); }
private:
    // Real Delegate layout: aligned_storage<32, sizeof(void*)> + uint8 tag + 3B pad = 36 bytes.
    // Match exactly so containing structs (HUDControl::m_RemoveCallback, MenuButton, etc.)
    // have the same sizeof under the cross-toolchain as on the host build.
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
    unsigned char                                          _tag;
    unsigned char                                          _pad[3];
};

}  // namespace Mortar

#endif
