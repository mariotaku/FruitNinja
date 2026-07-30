#ifndef MORTAR_DELEGATE_H
#define MORTAR_DELEGATE_H

//
// Mortar::Delegate<R(Args...)> — type-erased callable, layout-compatible
// with the binary's `Mortar::DelegateN<Ret, ...>` family (implemented via
// StackAllocatedPointer<Concept,32>). Fixed 36-byte size matching the binary
// regardless of arity.
//
// ASM-verified layout (0x24 = 36 bytes, identical to binary):
//   +0x00..+0x1F  inline subobject placement-constructed here; its vptr is AT
//                 +0x00 (binary Resolve@0x15d298 returns &m_Storage directly
//                 when flag==0, so the vptr IS the returned pointer value).
//   +0x20         m_bInline (binary name) / m_bEmpty (port name): see note below.
//   +0x21..+0x23  3 bytes padding (zeroed by ctors)
//
// Flag at +0x20 — binary vs port semantics:
//   Binary (StackAllocatedPointer::m_bInline): 0 = inline-stored, 1 = heap/empty.
//   Port (m_bEmpty):                           1 = empty,          0 = inline-bound.
//   These are value-identical in every reachable state: every FN callable is
//   <=32B so the binary's heap path is never taken. empty=1, inline-bound=0.
//   v1.6.1 StackAllocatedPointer Resolve@0x15d298 / ctor@0x15d2a8 / CopyConstruct@0x15d604
//
// Dispatch empty-guard: binary operator() @0x15f490 null-checks the Resolve()
//   result (at 0x15f498) before vtable dispatch — it does NOT test the flag
//   byte directly. The port mirrors this via Ptr() returning nullptr when empty.
//   Where Resolve is inlined the flag read is visible: v1.6.1
//   Delegate1<bool,InputEvent*>::Call @0x002757d4 is
//     ldrb r3,[r0,#0x20]; cmp r3,#0; ldrne r0,[r0]; cmp r0,#0; popeq;
//     ldr r3,[r0]; ldr r3,[r3,#0xc]; blx r3
//   i.e. flag set -> follow the heap pointer at +0x00, else use the inline
//   address, then null-check. Empty delegates zero +0x00, so the port's
//   `if (m_bEmpty) return R()` reaches the same outcome in every state.
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
// Compatibility shims `Delegate0`..`Delegate4` are provided at the bottom
// for code that still uses the legacy API.
//

#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace Mortar {

template<typename Sig> class Delegate;

template<typename R, typename... Args>
class Delegate<R(Args...)> {
public:
    Delegate() noexcept : m_bEmpty(1) {
        // Zero inline storage and pad so memcmp-based delegate equality in
        // Event::operator-= compares reliably (unused bytes are not garbage).
        ::memset(&m_Storage, 0, kInlineSize);
        m_pad[0] = m_pad[1] = m_pad[2] = 0;
    }
    Delegate(decltype(nullptr)) noexcept : m_bEmpty(1) {
        ::memset(&m_Storage, 0, kInlineSize);
        m_pad[0] = m_pad[1] = m_pad[2] = 0;
    }

    Delegate(const Delegate& other) {
        ::memset(&m_Storage, 0, kInlineSize);
        m_pad[0] = m_pad[1] = m_pad[2] = 0;
        if (other.m_bEmpty) {
            m_bEmpty = 1;
        } else {
            other.Ptr()->CloneTo(&m_Storage);
            m_bEmpty = 0;
        }
    }

    Delegate(Delegate&& other) noexcept {
        ::memset(&m_Storage, 0, kInlineSize);
        m_pad[0] = m_pad[1] = m_pad[2] = 0;
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
        ::memset(&m_Storage, 0, kInlineSize);
        m_pad[0] = m_pad[1] = m_pad[2] = 0;
        new (&m_Storage) Functor<Decayed>(std::forward<F>(fn));
        m_bEmpty = 0;
    }

    ~Delegate() { Reset(); }

    Delegate& operator=(const Delegate& other) {
        if (this != &other) {
            Reset();
            ::memset(&m_Storage, 0, kInlineSize);
            m_pad[0] = m_pad[1] = m_pad[2] = 0;
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
            ::memset(&m_Storage, 0, kInlineSize);
            m_pad[0] = m_pad[1] = m_pad[2] = 0;
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
        ::memset(&m_Storage, 0, kInlineSize);
        m_pad[0] = m_pad[1] = m_pad[2] = 0;
        typedef typename std::decay<F>::type Decayed;  // GCC 4.4: template alias unsupported pre-4.7
        static_assert(sizeof(Functor<Decayed>) <= kInlineSize,
            "Mortar::Delegate: callable too large for 32-byte inline storage.");
        new (&m_Storage) Functor<Decayed>(std::forward<F>(fn));
        m_bEmpty = 0;
        return *this;
    }

    // Invoke. Empty delegates silently return R(); matches binary flow
    // v1.6.1 Mortar::Delegate0<void>::operator() @0x0015f4b0 is a 4-byte veneer that tail-calls
    // v1.6.1 Mortar::Delegate0<void>::Call @0x0015f490 -- null-check at 0x0015f498 before vtable dispatch.
    R operator()(Args... args) const {
        if (m_bEmpty) return R();
        return Ptr()->Invoke(std::forward<Args>(args)...);
    }

    operator bool() const noexcept { return m_bEmpty == 0; }  // GCC 4.4: explicit conversion operators unsupported pre-4.5
    bool operator==(decltype(nullptr)) const noexcept { return m_bEmpty != 0; }
    bool operator!=(decltype(nullptr)) const noexcept { return m_bEmpty == 0; }

    // Delegate equality — used by EventN::operator-= / UnRegister via DelegateEqual().
    // ASM-verified: 2026-06-24 v1.6.1 Mortar::Delegate0<void>::operator== @ 0x00167530
    //   (wrapper: a==b->true / b-null->false guards) + BaseDelegate::operator== @ 0x001674e0
    //   (GetTypeID@vt+0x10, Compare@vt+0x14) (asm-inspector):
    //   if (a->GetTypeID() != b->GetTypeID()) return false;  // vtable+0x10
    //   return a->Compare(*b);                               // vtable+0x14
    //   (port's Ptr() == binary StackAllocatedPointer::Resolve; the a==b/null guards live in the
    //    binary's Delegate0<void>::operator== wrapper @0x00167530, which Resolve()s both operands,
    //    does a==b->true and b-null->false, then tail-calls the bare BaseDelegate body @0x001674e0.
    //    Port's extra !a check is dead under the Event::operator-= invariant a=*it!=empty.)
    bool operator==(const Delegate& other) const {
        const Concept* a = Ptr();
        const Concept* b = other.Ptr();
        if (a == b) return true;
        if (!a || !b) return false;
        if (a->GetTypeID() != b->GetTypeID()) return false;
        return a->Compare(*b);
    }
    bool operator!=(const Delegate& other) const { return !(*this == other); }

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
    // ASM-verified: 2026-07-31T00:00Z v1.6.1 Mortar::Delegate1<bool,InputEvent*>::Call @ 0x002757d4 (asm-inspector)
    //   With this order the port's out-of-line Delegate1<bool,InputEvent*>::operator()
    //   compiles to `ldrb [r0,#32]; cmp #0; ...; ldr ip,[r3]; ldr r3,[ip,#12]; blx r3`
    //   — the `ldr r3,[r3,#0xc]` now matches the binary instruction for instruction.
    //   Before the swap it emitted `ldr fn,[vptr,#8]`, a slot-order divergence on
    //   EVERY delegate call site in the port.
    //
    // Declaration order IS the vtable slot order — keep it matching the binary's
    // BaseDelegate table. Read out of v1.6.1 _ZTVN6Mortar9Delegate1IbP10InputEventE6GlobalE
    // @0x002ce6d8 (slots start at +0x08 of the label, after offset-to-top + typeinfo):
    //   +0x00 ~BaseDelegate      +0x04 ~BaseDelegate (deleting)
    //   +0x08 Clone   @0x001d0864   -> CloneTo
    //   +0x0c Call    @0x001d0714   -> Invoke   (Delegate1<bool,InputEvent*>::Call
    //                                  @0x002757d4 dispatches `ldr r3,[r3,#0xc]`)
    //   +0x10 GetTypeID @0x001d0728
    //   +0x14 Compare   (@0x0015d4ec Callee<T>, @0x0015d574 Global)
    struct Concept {
        virtual ~Concept() {}
        virtual void CloneTo(void* dst) const = 0;
        virtual R    Invoke(Args... args) const = 0;
        virtual int  GetTypeID() const = 0;
        virtual bool Compare(const Concept& o) const = 0;
    };

    struct FreeFn : Concept {
        R (*m_pFn)(Args...);
        explicit FreeFn(R (*fn)(Args...)) : m_pFn(fn) {}
        R    Invoke(Args... args) const override {
            return m_pFn ? m_pFn(std::forward<Args>(args)...) : R();
        }
        void CloneTo(void* dst) const override { new (dst) FreeFn(*this); }
        // v1.6.1 Global::GetTypeID / Global::Compare @0x0015d574
        int  GetTypeID() const override {
            static const char s_id = 0;
            return (int)(intptr_t)&s_id;
        }
        bool Compare(const Concept& o) const override {
            return m_pFn == static_cast<const FreeFn&>(o).m_pFn;
        }
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
        // v1.6.1 Callee<T>::GetTypeID / Callee<T>::Compare @0x0015d4ec
        // One static-address per MemFn<T> instantiation -> unique id per bound type.
        int  GetTypeID() const override {
            static const char s_id = 0;
            return (int)(intptr_t)&s_id;
        }
        bool Compare(const Concept& o) const override {
            const MemFn<T>& other = static_cast<const MemFn<T>&>(o);
            // Compare bound object pointer AND full pmf (ptr word + adj word).
            return m_pObj == other.m_pObj && m_pMethod == other.m_pMethod;
        }
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
        int  GetTypeID() const override {
            static const char s_id = 0;
            return (int)(intptr_t)&s_id;
        }
        // DIFFERS: no binary Functor in any UnRegister path (v1.6.1); identity compare only.
        bool Compare(const Concept& o) const override { return this == &o; }
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
    // +0x20: binary field is StackAllocatedPointer::m_bInline (0=inline, 1=heap/empty). Port uses m_bEmpty
    // (1=empty, 0=bound). Every FN callable is <=32B so the heap path is never taken -> binary-flag and
    // m_bEmpty are value-identical in every reachable state (empty=1, inline-bound=0).
    // v1.6.1 StackAllocatedPointer Resolve@0x15d298 / ctor@0x15d2a8 / CopyConstruct@0x15d604.
    uint8_t  m_bEmpty;                                                          // +0x20
    uint8_t  m_pad[3];                                                          // +0x21 (zeroed by ctors)
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

// ASM-verified: 2026-06-21T00:00Z v1.6.1 Mortar::Delegate0<void> {ctor@0x15d2a8, Resolve@0x15d298, Delete@0x15d2bc, operator()@0x15f490, CopyConstruct@0x15d604} -- inline object at +0x00 (vptr), m_bInline@0x20 (0=inline/1=heap-empty), size 0x24 (asm-inspector)
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
    bool operator==(const Delegate0& other) const { return m_d == other.m_d; }
    bool operator!=(const Delegate0& other) const { return m_d != other.m_d; }
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
    bool operator==(const Delegate1& other) const { return m_d == other.m_d; }
    bool operator!=(const Delegate1& other) const { return m_d != other.m_d; }
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
    bool operator==(const Delegate2& other) const { return m_d == other.m_d; }
    bool operator!=(const Delegate2& other) const { return m_d != other.m_d; }
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
    bool operator==(const Delegate3& other) const { return m_d == other.m_d; }
    bool operator!=(const Delegate3& other) const { return m_d != other.m_d; }
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
    bool operator==(const Delegate4& other) const { return m_d == other.m_d; }
    bool operator!=(const Delegate4& other) const { return m_d != other.m_d; }
};

}  // namespace Mortar

#endif // MORTAR_DELEGATE_H
