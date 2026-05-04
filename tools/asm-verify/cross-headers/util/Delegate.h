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
    // Real Delegate layout on ARM32 (Bada): 32 bytes total.
    // Layout per binary RE: aligned_storage<28> + uint8 tag + 3B pad,
    // OR aligned_storage<32> with tag stored inside the storage block.
    // Empirically the binary's Delegate1<bool, MortarSound*> in GameSound::Slot
    // is exactly 32 bytes (sizeof Slot == 0x38 with finishCallback @ +0x14
    // and reserved @ +0x34 means finishCallback occupies +0x14..+0x33 = 32B).
    // 32 bytes total on ARM32; 36 bytes on 64-bit host (which is fine -- only
    // the cross-toolchain's offsetof asserts run under __bada__).
    typename std::aligned_storage<32, sizeof(void*)>::type _storage;
};

}  // namespace Mortar

#endif
