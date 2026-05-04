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
    // Match real port-side Delegate.h layout: 36 bytes on ARM32 (this is what
    // every other layout-asserting class -- HUDControl, MenuButton,
    // FruitFactControl, etc. -- is calibrated to). NOTE: GameSound RE doc
    // says binary is 32 bytes; the 4B discrepancy is a known port-vs-binary
    // drift that GameSound's sizeof(Slot)==0x38 asserts have been disabled
    // for. See src/engine/audio/GameSound.h TODO.
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
    unsigned char                                          _tag;
    unsigned char                                          _pad[3];
};

}  // namespace Mortar

// Legacy compatibility aliases used by GameSound, MenuButton, HUDControl etc.
// These match the real port's Delegate.h placement at GLOBAL scope (not in
// namespace Mortar).
template<typename Ret>
class Delegate0 {
public:
    Delegate0() {}
    Delegate0(decltype(nullptr)) {}
    Ret operator()() const { return Ret(); }
    operator bool() const { return false; }
private:
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
};

template<typename Ret, typename A1>
class Delegate1 {
public:
    Delegate1() {}
    Delegate1(decltype(nullptr)) {}
    Ret operator()(A1) const { return Ret(); }
    operator bool() const { return false; }
private:
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
};

template<typename Ret, typename A1, typename A2>
class Delegate2 {
public:
    Delegate2() {}
    Delegate2(decltype(nullptr)) {}
    Ret operator()(A1, A2) const { return Ret(); }
    operator bool() const { return false; }
private:
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
};

#endif
