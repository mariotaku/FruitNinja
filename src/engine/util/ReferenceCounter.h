#ifndef MORTAR_REFERENCE_COUNTER_H
#define MORTAR_REFERENCE_COUNTER_H

// Binary @ 0x00188c6c -- __ReferenceCounterData layout: vptr + strong count + weak count.
// Binary vtable layout: slot 0 = ~D2, slot 1 = ~D0, slot 2 = GetRefCounter() returning
// __ReferenceCounterData*. Port uses a single virtual dtor; no caller currently depends
// on the indirect `obj->vtable[+8]()` GetRefCounter() form, so no port-side virtual
// needed.
// DIFFERS: binary vtable slot 2 = GetRefCounter() returning __ReferenceCounterData*.
//   Port omits this virtual; no call site currently dispatches via vtable[+8]().
//   asm-verify has run (R4 W4) and not flagged the indirect form -- deliberate omission.

// Binary @ 0x00109e78 -- __ReferenceCounterData::AddRef(): `adds r0,#0x4; blx
//   InterlockedUNumber::Increment` -- an atomic increment of the strong count at +0x4.
//   The matching Release path performs the symmetric atomic decrement. Both route
//   through InterlockedUNumber, whose vtable thunk (PTR_Increment_001eed24 @ 0x000fb97c)
//   calls the Bada OSAL interlocked primitive.
// DIFFERS: original = InterlockedUNumber::Increment/Decrement (Bada OSAL atomics),
//   using plain ++/-- because (a) the cross-toolchain target is GCC 4.4.1 pre-C++11 with
//   no portable <atomic>, and the Bada interlocked HAL has no SDL2/GLES2 counterpart, and
//   (b) the only path that touches counters off-thread is the Job system, which is not yet
//   ported. The observable increment/decrement-then-free behaviour is identical
//   single-threaded; re-introduce a portable atomic here once the threading model lands.

namespace Mortar {

// Intrusive reference counting base class (12 bytes: vptr +0x00, strong count +0x04,
// weak count +0x08). Matches binary `Mortar::__ReferenceCounterData` /
// `Mortar::ReferenceCounter`.
class ReferenceCounter {
    int m_StrongCount; // offset 0x04
    int m_WeakCount;   // offset 0x08
public:
    ReferenceCounter() : m_StrongCount(0), m_WeakCount(0) {}
    virtual ~ReferenceCounter() {}

    void AddRef() { m_StrongCount++; }
    void Release() { if (--m_StrongCount <= 0) delete this; }
    int GetRefCount() const { return m_StrongCount; }
};

}  // namespace Mortar

// Binary @ 0x00188c6c -- __ReferenceCounterData layout: vptr + strong count + weak count.
#ifdef __bada__
static_assert(sizeof(Mortar::ReferenceCounter) == 12,
              "ReferenceCounter sizeof mismatch (vptr + strong count + weak count)");
#endif

#endif // MORTAR_REFERENCE_COUNTER_H
