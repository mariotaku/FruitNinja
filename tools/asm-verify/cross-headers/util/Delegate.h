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
    // Callable-functor ctor: accept any compatible callable (lambdas,
    // small named functors). Stub doesn't invoke; type-check only.
    template<typename F>
    Delegate(const F&) {}
    R operator()(Args...) const { return R(); }
    operator bool() const { return false; }
    template<typename T>
    static Delegate Make(T*, R (T::*)(Args...)) { return Delegate(); }
    static Delegate MakeFree(R (*)(Args...)) { return Delegate(); }
private:
    // Layout matches port-side Delegate.h exactly: 32B inline buffer + 1B flag@+0x20 + 3B pad = 0x24 (36B).
    // This also matches the binary (StackAllocatedPointer<Concept,32>): size=0x24 was ASM-verified via
    // asm-inspector. The "32" is the inline-buffer CAPACITY (kInlineSize), not the struct size.
    // There is NO port-vs-binary drift on Delegate layout.
    // v1.6.1 StackAllocatedPointer ctor@0x15d2a8 / Resolve@0x15d298 (asm-inspector 2026-06-21).
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
    unsigned char                                          _tag;
    unsigned char                                          _pad[3];
};

// Legacy compatibility aliases used by GameSound, MenuButton, HUDControl etc.
// These match the real port's Delegate.h placement INSIDE namespace Mortar
// (port-side Delegate0..Delegate4 live in `namespace Mortar`, see
// src/engine/util/Delegate.h:241). The earlier stub placed them at global
// scope, which broke `Mortar::Delegate1<bool, InputEvent*>` typedefs in
// InputDevice.h, GameSound.h, HUDControl.h and friends. Sized 36B to match
// the primary template + every layout-asserting class (HUDControl,
// MenuButton, FruitFactControl, GameSound::Slot).
template<typename Ret>
class Delegate0 {
public:
    Delegate0() {}
    Delegate0(decltype(nullptr)) {}
    template<typename F>
    Delegate0(const F&) {}
    Ret operator()() const { return Ret(); }
    operator bool() const { return false; }
    template<typename T>
    static Delegate0 QCallee(T*, Ret (T::*)()) { return Delegate0(); }
    template<typename T>
    static Delegate0 Make(T*, Ret (T::*)()) { return Delegate0(); }
    static Delegate0 MakeFree(Ret (*)()) { return Delegate0(); }
private:
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
    unsigned char                                          _tag;
    unsigned char                                          _pad[3];
};

template<typename Ret, typename A1>
class Delegate1 {
public:
    Delegate1() {}
    Delegate1(decltype(nullptr)) {}
    template<typename F>
    Delegate1(const F&) {}
    Ret operator()(A1) const { return Ret(); }
    operator bool() const { return false; }
    template<typename T>
    static Delegate1 QCallee(T*, Ret (T::*)(A1)) { return Delegate1(); }
    template<typename T>
    static Delegate1 Make(T*, Ret (T::*)(A1)) { return Delegate1(); }
    static Delegate1 MakeFree(Ret (*)(A1)) { return Delegate1(); }
private:
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
    unsigned char                                          _tag;
    unsigned char                                          _pad[3];
};

template<typename Ret, typename A1, typename A2>
class Delegate2 {
public:
    Delegate2() {}
    Delegate2(decltype(nullptr)) {}
    template<typename F>
    Delegate2(const F&) {}
    Ret operator()(A1, A2) const { return Ret(); }
    operator bool() const { return false; }
    template<typename T>
    static Delegate2 QCallee(T*, Ret (T::*)(A1, A2)) { return Delegate2(); }
    template<typename T>
    static Delegate2 Make(T*, Ret (T::*)(A1, A2)) { return Delegate2(); }
    static Delegate2 MakeFree(Ret (*)(A1, A2)) { return Delegate2(); }
private:
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
    unsigned char                                          _tag;
    unsigned char                                          _pad[3];
};

template<typename Ret, typename A1, typename A2, typename A3>
class Delegate3 {
public:
    Delegate3() {}
    Delegate3(decltype(nullptr)) {}
    template<typename F>
    Delegate3(const F&) {}
    Ret operator()(A1, A2, A3) const { return Ret(); }
    operator bool() const { return false; }
    template<typename T>
    static Delegate3 QCallee(T*, Ret (T::*)(A1, A2, A3)) { return Delegate3(); }
    template<typename T>
    static Delegate3 Make(T*, Ret (T::*)(A1, A2, A3)) { return Delegate3(); }
    static Delegate3 MakeFree(Ret (*)(A1, A2, A3)) { return Delegate3(); }
private:
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
    unsigned char                                          _tag;
    unsigned char                                          _pad[3];
};

template<typename Ret, typename A1, typename A2, typename A3, typename A4>
class Delegate4 {
public:
    Delegate4() {}
    Delegate4(decltype(nullptr)) {}
    template<typename F>
    Delegate4(const F&) {}
    Ret operator()(A1, A2, A3, A4) const { return Ret(); }
    operator bool() const { return false; }
    template<typename T>
    static Delegate4 QCallee(T*, Ret (T::*)(A1, A2, A3, A4)) { return Delegate4(); }
    template<typename T>
    static Delegate4 Make(T*, Ret (T::*)(A1, A2, A3, A4)) { return Delegate4(); }
    static Delegate4 MakeFree(Ret (*)(A1, A2, A3, A4)) { return Delegate4(); }
private:
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
    unsigned char                                          _tag;
    unsigned char                                          _pad[3];
};

}  // namespace Mortar

#endif
