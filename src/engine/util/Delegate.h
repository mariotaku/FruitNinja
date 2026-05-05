#ifndef MORTAR_DELEGATE_H
#define MORTAR_DELEGATE_H

//
// Mortar::Delegate<R(Args...)> — type-erased callable, layout-compatible
// with the binary's `Mortar::DelegateN<Ret, ...>` family. Fixed 36-byte
// size matching the binary regardless of arity.
//
// Binary spec: docs/engine/delegate-system.md
//
// Layout (36 bytes / 0x24, identical to binary regardless of arity):
//   +0x00..+0x1F  inline polymorphic subobject (vptr + bound state)
//   +0x20         m_bEmpty   1 = empty/no-callable, 0 = inline-stored
//   +0x21..+0x23  padding
//
// The inline subobject is a Concept (abstract base) whose first 4 bytes
// are its vptr — matching the binary's "ptr = vptr of inline subobject"
// at offset +0x00.
//
// Concrete subtypes:
//   FreeFn     — wraps a free function pointer. Sizeof: 4 vptr + 4 fnptr = 8.
//   MemFn<T>   — wraps obj* + member-fn-ptr.    Sizeof: 4 vptr + 4 obj + 8 ptmf = 16.
//   Functor<F> — wraps any callable (lambda).   Sizeof: 4 vptr + sizeof(F).
//
// Functor<F> requires sizeof(Functor<F>) <= 32. Larger callables fail
// static_assert. Binary's heap-fallback path is not taken in any
// FruitNinja callsite per RE.
//
// Empty-state: operator() is a no-op (returns R()). Matches binary's
// invoke flow at 0x0013134c which null-checks before vtable dispatch.
//
// Compatibility shims `Delegate0`..`Delegate4` are provided at the bottom
// for code that still uses the legacy API.
//

#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace Mortar {

template<typename Sig> class Delegate;

template<typename R, typename... Args>
class Delegate<R(Args...)> {
public:
    Delegate() noexcept : m_bEmpty(1) {}
    Delegate(decltype(nullptr)) noexcept : m_bEmpty(1) {}

    Delegate(const Delegate& other) {
        if (other.m_bEmpty) {
            m_bEmpty = 1;
        } else {
            other.Ptr()->CloneTo(&m_Storage);
            m_bEmpty = 0;
        }
    }

    Delegate(Delegate&& other) noexcept {
        if (other.m_bEmpty) {
            m_bEmpty = 1;
        } else {
            other.Ptr()->CloneTo(&m_Storage);
            other.Reset();
            m_bEmpty = 0;
        }
    }

    // Construct from any callable. sizeof(Functor<F>) must be <= 32 bytes.
    template<typename F,
             typename = typename std::enable_if<
                 !std::is_same<typename std::decay<F>::type, Delegate>::value
             >::type>
    Delegate(F&& fn) {
        typedef typename std::decay<F>::type Decayed;  // GCC 4.4: template alias unsupported pre-4.7
        static_assert(sizeof(Functor<Decayed>) <= kInlineSize,
            "Mortar::Delegate: callable too large for 32-byte inline storage. "
            "Reduce captures or use a member function via Make().");
        new (&m_Storage) Functor<Decayed>(std::forward<F>(fn));
        m_bEmpty = 0;
    }

    ~Delegate() { Reset(); }

    Delegate& operator=(const Delegate& other) {
        if (this != &other) {
            Reset();
            if (!other.m_bEmpty) {
                other.Ptr()->CloneTo(&m_Storage);
                m_bEmpty = 0;
            }
        }
        return *this;
    }

    Delegate& operator=(Delegate&& other) noexcept {
        if (this != &other) {
            Reset();
            if (!other.m_bEmpty) {
                other.Ptr()->CloneTo(&m_Storage);
                other.Reset();
                m_bEmpty = 0;
            }
        }
        return *this;
    }

    Delegate& operator=(decltype(nullptr)) noexcept {
        Reset();
        return *this;
    }

    template<typename F,
             typename = typename std::enable_if<
                 !std::is_same<typename std::decay<F>::type, Delegate>::value
             >::type>
    Delegate& operator=(F&& fn) {
        Reset();
        typedef typename std::decay<F>::type Decayed;  // GCC 4.4: template alias unsupported pre-4.7
        static_assert(sizeof(Functor<Decayed>) <= kInlineSize,
            "Mortar::Delegate: callable too large for 32-byte inline storage.");
        new (&m_Storage) Functor<Decayed>(std::forward<F>(fn));
        m_bEmpty = 0;
        return *this;
    }

    // Invoke. Empty delegates silently return R(); matches binary flow
    // at 0x0013134c (null-check before vtable dispatch).
    R operator()(Args... args) const {
        if (m_bEmpty) return R();
        return Ptr()->Invoke(std::forward<Args>(args)...);
    }

    operator bool() const noexcept { return m_bEmpty == 0; }  // GCC 4.4: explicit conversion operators unsupported pre-4.5
    bool operator==(decltype(nullptr)) const noexcept { return m_bEmpty != 0; }
    bool operator!=(decltype(nullptr)) const noexcept { return m_bEmpty == 0; }

    // --- Static factories ---

    // Bind a member function. Mirrors binary's Callee<T> subtype.
    template<typename T>
    static Delegate Make(T* obj, R (T::*method)(Args...)) {
        Delegate d;
        new (&d.m_Storage) MemFn<T>(obj, method);
        d.m_bEmpty = 0;
        return d;
    }

    // Bind a free function. Mirrors binary's Global subtype.
    static Delegate MakeFree(R (*fn)(Args...)) {
        Delegate d;
        new (&d.m_Storage) FreeFn(fn);
        d.m_bEmpty = 0;
        return d;
    }

    // Explicit no-op delegate. Binary's MakeDelegate_DrawUtil_HUD binds a
    // DefaultDeleteCallback free function that returns its argument; the
    // port's empty-state already no-ops on call, so this just builds an
    // empty delegate.
    static Delegate Noop() noexcept { return Delegate(); }

private:
    struct Concept {
        virtual ~Concept() {}
        virtual R    Invoke(Args... args) const = 0;
        virtual void CloneTo(void* dst) const = 0;
    };

    struct FreeFn : Concept {
        R (*m_pFn)(Args...);
        explicit FreeFn(R (*fn)(Args...)) : m_pFn(fn) {}
        R    Invoke(Args... args) const override {
            return m_pFn ? m_pFn(std::forward<Args>(args)...) : R();
        }
        void CloneTo(void* dst) const override { new (dst) FreeFn(*this); }
    };

    template<typename T>
    struct MemFn : Concept {
        T* m_pObj;
        R (T::*m_pMethod)(Args...);
        MemFn(T* obj, R (T::*method)(Args...))
            : m_pObj(obj), m_pMethod(method) {}
        R    Invoke(Args... args) const override {
            return (m_pObj && m_pMethod)
                ? (m_pObj->*m_pMethod)(std::forward<Args>(args)...)
                : R();
        }
        void CloneTo(void* dst) const override { new (dst) MemFn(*this); }
    };

    template<typename F>
    struct Functor : Concept {
        mutable F m_fn;  // mutable: GCC 4.4 std::bind result lacks const operator()
        explicit Functor(const F& fn) : m_fn(fn) {}
        explicit Functor(F&& fn) : m_fn(std::move(fn)) {}
        R    Invoke(Args... args) const override {
            // GCC 4.4 std::bind result operator() takes _Args& (lvalue ref), so
            // passing std::forward<> (rvalue) fails to match. Drop forwarding;
            // all delegate args in this codebase are cheap pointer/scalar types.
            return m_fn(args...);
        }
        void CloneTo(void* dst) const override { new (dst) Functor(*this); }
    };

    Concept*       Ptr()       noexcept { return m_bEmpty ? nullptr : reinterpret_cast<Concept*>(&m_Storage); }
    const Concept* Ptr() const noexcept { return m_bEmpty ? nullptr : reinterpret_cast<const Concept*>(&m_Storage); }

    void Reset() noexcept {
        if (!m_bEmpty) {
            Ptr()->~Concept();
            m_bEmpty = 1;
        }
    }

    static const std::size_t kInlineSize = 32;
    typename std::aligned_storage<kInlineSize, sizeof(void*)>::type m_Storage;  // +0x00
    uint8_t  m_bEmpty;                                                          // +0x20
    uint8_t  m_pad[3];                                                          // +0x21
};

} // namespace Mortar

// Sanity-check: 36 bytes on any 32-bit ABI; 40 bytes on 64-bit (storage
// alignment forces 8-byte boundaries). Port does not need ARM-byte-exact
// fidelity — only that all signatures share the same size so struct
// offsets match across screens.
static_assert(sizeof(Mortar::Delegate<void()>) ==
              sizeof(Mortar::Delegate<bool(float)>),
              "Mortar::Delegate must have a uniform size across signatures");

// =====================================================================
// Legacy compatibility shims — Mortar::Delegate0..Delegate4
// Match the binary's `Mortar::DelegateN<Ret, ...>` template-class names so
// signatures using these shims mangle identically to the binary symbols.
// Internally each just wraps the variadic Mortar::Delegate.
// =====================================================================

namespace Mortar {

// Each shim wraps the variadic Mortar::Delegate so it inherits all the
// templated callable-construction. Same 36-byte ABI as Delegate.

template<typename Ret>
class Delegate0 {
    Delegate<Ret()> m_d;
public:
    Delegate0() {}
    Delegate0(decltype(nullptr)) {}
    Delegate0(Ret (*fn)()) : m_d(Delegate<Ret()>::MakeFree(fn)) {}
    Delegate0(const Delegate<Ret()>& d) : m_d(d) {}
    template<typename F,
             typename = typename std::enable_if<
                 !std::is_same<typename std::decay<F>::type, Delegate0>::value &&
                 !std::is_same<typename std::decay<F>::type, Delegate<Ret()>>::value
             >::type>
    Delegate0(F&& fn) : m_d(std::forward<F>(fn)) {}

    template<typename T>
    static Delegate0 QCallee(T* obj, Ret (T::*method)()) {
        return Delegate0(Delegate<Ret()>::Make(obj, method));
    }
    template<typename T>
    static Delegate0 Make(T* obj, Ret (T::*method)()) {
        return Delegate0(Delegate<Ret()>::Make(obj, method));
    }
    static Delegate0 MakeFree(Ret (*fn)()) {
        return Delegate0(Delegate<Ret()>::MakeFree(fn));
    }
    Ret operator()() const { return m_d(); }
    operator bool() const { return static_cast<bool>(m_d); }
};

template<typename Ret, typename A1>
class Delegate1 {
    Delegate<Ret(A1)> m_d;
public:
    Delegate1() {}
    Delegate1(decltype(nullptr)) {}
    Delegate1(Ret (*fn)(A1)) : m_d(Delegate<Ret(A1)>::MakeFree(fn)) {}
    Delegate1(const Delegate<Ret(A1)>& d) : m_d(d) {}
    template<typename F,
             typename = typename std::enable_if<
                 !std::is_same<typename std::decay<F>::type, Delegate1>::value &&
                 !std::is_same<typename std::decay<F>::type, Delegate<Ret(A1)>>::value
             >::type>
    Delegate1(F&& fn) : m_d(std::forward<F>(fn)) {}

    template<typename T>
    static Delegate1 QCallee(T* obj, Ret (T::*method)(A1)) {
        return Delegate1(Delegate<Ret(A1)>::Make(obj, method));
    }
    template<typename T>
    static Delegate1 Make(T* obj, Ret (T::*method)(A1)) {
        return Delegate1(Delegate<Ret(A1)>::Make(obj, method));
    }
    static Delegate1 MakeFree(Ret (*fn)(A1)) {
        return Delegate1(Delegate<Ret(A1)>::MakeFree(fn));
    }
    Ret operator()(A1 a1) const { return m_d(a1); }
    operator bool() const { return static_cast<bool>(m_d); }
};

template<typename Ret, typename A1, typename A2>
class Delegate2 {
    Delegate<Ret(A1, A2)> m_d;
public:
    Delegate2() {}
    Delegate2(decltype(nullptr)) {}
    Delegate2(Ret (*fn)(A1, A2)) : m_d(Delegate<Ret(A1, A2)>::MakeFree(fn)) {}
    Delegate2(const Delegate<Ret(A1, A2)>& d) : m_d(d) {}
    template<typename F,
             typename = typename std::enable_if<
                 !std::is_same<typename std::decay<F>::type, Delegate2>::value &&
                 !std::is_same<typename std::decay<F>::type, Delegate<Ret(A1, A2)>>::value
             >::type>
    Delegate2(F&& fn) : m_d(std::forward<F>(fn)) {}

    template<typename T>
    static Delegate2 QCallee(T* obj, Ret (T::*method)(A1, A2)) {
        return Delegate2(Delegate<Ret(A1, A2)>::Make(obj, method));
    }
    template<typename T>
    static Delegate2 Make(T* obj, Ret (T::*method)(A1, A2)) {
        return Delegate2(Delegate<Ret(A1, A2)>::Make(obj, method));
    }
    static Delegate2 MakeFree(Ret (*fn)(A1, A2)) {
        return Delegate2(Delegate<Ret(A1, A2)>::MakeFree(fn));
    }
    Ret operator()(A1 a1, A2 a2) const { return m_d(a1, a2); }
    operator bool() const { return static_cast<bool>(m_d); }
};

template<typename Ret, typename A1, typename A2, typename A3>
class Delegate3 {
    Delegate<Ret(A1, A2, A3)> m_d;
public:
    Delegate3() {}
    Delegate3(decltype(nullptr)) {}
    Delegate3(Ret (*fn)(A1, A2, A3)) : m_d(Delegate<Ret(A1, A2, A3)>::MakeFree(fn)) {}
    Delegate3(const Delegate<Ret(A1, A2, A3)>& d) : m_d(d) {}
    template<typename F,
             typename = typename std::enable_if<
                 !std::is_same<typename std::decay<F>::type, Delegate3>::value &&
                 !std::is_same<typename std::decay<F>::type, Delegate<Ret(A1, A2, A3)>>::value
             >::type>
    Delegate3(F&& fn) : m_d(std::forward<F>(fn)) {}

    template<typename T>
    static Delegate3 QCallee(T* obj, Ret (T::*method)(A1, A2, A3)) {
        return Delegate3(Delegate<Ret(A1, A2, A3)>::Make(obj, method));
    }
    template<typename T>
    static Delegate3 Make(T* obj, Ret (T::*method)(A1, A2, A3)) {
        return Delegate3(Delegate<Ret(A1, A2, A3)>::Make(obj, method));
    }
    static Delegate3 MakeFree(Ret (*fn)(A1, A2, A3)) {
        return Delegate3(Delegate<Ret(A1, A2, A3)>::MakeFree(fn));
    }
    Ret operator()(A1 a1, A2 a2, A3 a3) const { return m_d(a1, a2, a3); }
    operator bool() const { return static_cast<bool>(m_d); }
};

template<typename Ret, typename A1, typename A2, typename A3, typename A4>
class Delegate4 {
    Delegate<Ret(A1, A2, A3, A4)> m_d;
public:
    Delegate4() {}
    Delegate4(decltype(nullptr)) {}
    Delegate4(Ret (*fn)(A1, A2, A3, A4)) : m_d(Delegate<Ret(A1, A2, A3, A4)>::MakeFree(fn)) {}
    Delegate4(const Delegate<Ret(A1, A2, A3, A4)>& d) : m_d(d) {}
    template<typename F,
             typename = typename std::enable_if<
                 !std::is_same<typename std::decay<F>::type, Delegate4>::value &&
                 !std::is_same<typename std::decay<F>::type, Delegate<Ret(A1, A2, A3, A4)>>::value
             >::type>
    Delegate4(F&& fn) : m_d(std::forward<F>(fn)) {}

    template<typename T>
    static Delegate4 QCallee(T* obj, Ret (T::*method)(A1, A2, A3, A4)) {
        return Delegate4(Delegate<Ret(A1, A2, A3, A4)>::Make(obj, method));
    }
    template<typename T>
    static Delegate4 Make(T* obj, Ret (T::*method)(A1, A2, A3, A4)) {
        return Delegate4(Delegate<Ret(A1, A2, A3, A4)>::Make(obj, method));
    }
    static Delegate4 MakeFree(Ret (*fn)(A1, A2, A3, A4)) {
        return Delegate4(Delegate<Ret(A1, A2, A3, A4)>::MakeFree(fn));
    }
    Ret operator()(A1 a1, A2 a2, A3 a3, A4 a4) const { return m_d(a1, a2, a3, a4); }
    operator bool() const { return static_cast<bool>(m_d); }
};

}  // namespace Mortar

#endif // MORTAR_DELEGATE_H
