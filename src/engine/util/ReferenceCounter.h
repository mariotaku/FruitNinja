#ifndef MORTAR_REFERENCE_COUNTER_H
#define MORTAR_REFERENCE_COUNTER_H

// Binary hierarchy (v1.6.1):
//   ReferenceCounter            abstract base, vtable @ 0x2cf750
//     __ReferenceCounterData    concrete 12-byte block, vtable @ 0x2cf768
//       __WeakReferenceData     weak-ref control block 16 bytes, vtable @ 0x2cf930
//
// Port collapses ReferenceCounter + __ReferenceCounterData into a single class.
// DIFFERS: binary separates the abstract ReferenceCounter interface (vptr only)
//   from __ReferenceCounterData (concrete storage). Port merges them because no
//   call site dispatches through the abstract ReferenceCounter vptr independently.
//
// DIFFERS: binary vtable slot 2 = GetRefCounter() returning __ReferenceCounterData*.
//   Port omits this virtual (no call site currently dispatches via vtable[+8]()).
//   asm-verify (R4 W4) did not flag the indirect form -- deliberate omission.
//
// DIFFERS: binary +0x08 = __WeakReferenceData* m_pWeakData (lazy-allocated weak-ref
//   control block, 16 bytes, CAS-installed by v1.6.1 __GetWeakRefData @0x2275d4).
//   Port models +0x08 as int m_WeakCount (inline count, strong-ref-only semantics).
//   sizeof(int) == sizeof(ptr) on ARM32 so the 12-byte binary layout is preserved
//   by coincidence. If/when WeakPtr support is needed, replace m_WeakCount with a
//   __WeakReferenceData* and add the 16-byte control block class.
//   __WeakReferenceData is currently UNPORTED (no port consumers).
//
// DIFFERS: binary v1.6.1 AddRef/Release @0x1194f4 use InterlockedUNumber::Increment/Decrement
//   (Bada OSAL atomics). Port uses plain ++/-- (single-threaded; Job system unported).
//   v1.6.1 AddRef @ 0x1194f4; __GetWeakRefData @ 0x2275d4.

namespace Mortar {

// Intrusive reference counting base class (12 bytes: vptr +0x00, strong count +0x04,
// weak-data-ptr +0x08 — port models +0x08 as inline int m_WeakCount per DIFFERS above).
// Matches binary `Mortar::__ReferenceCounterData` layout.
class ReferenceCounter {
    int m_StrongCount; // offset 0x04
    int m_WeakCount;   // offset 0x08  (DIFFERS: binary = __WeakReferenceData*, port = int)
public:
    ReferenceCounter() : m_StrongCount(0), m_WeakCount(0) {}
    virtual ~ReferenceCounter() {}

    void AddRef() { m_StrongCount++; }
    void Release() { if (--m_StrongCount <= 0) delete this; }
    int GetRefCount() const { return m_StrongCount; }
};

}  // namespace Mortar

#ifdef __bada__
static_assert(sizeof(Mortar::ReferenceCounter) == 12,
              "ReferenceCounter sizeof mismatch (vptr + strong count + weak-data field)");
#endif

#endif // MORTAR_REFERENCE_COUNTER_H
