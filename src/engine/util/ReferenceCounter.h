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

// TODO: 0x00109e78 -- binary uses InterlockedUNumber::Increment / atomic decrement on
// both counters. Port uses plain ++/--. Functional risk only if SmartPtr is touched
// off-thread (Job system); upgrade if/when the threading model lands.

// Intrusive reference counting base class (12 bytes: vptr +0x00, strong count +0x04,
// weak count +0x08). Original: Mortar::__ReferenceCounterData / Mortar::ReferenceCounter
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

// Binary @ 0x00188c6c -- __ReferenceCounterData layout: vptr + strong count + weak count.
#ifdef __bada__
static_assert(sizeof(ReferenceCounter) == 12,
              "ReferenceCounter sizeof mismatch (vptr + strong count + weak count)");
#endif

#endif // MORTAR_REFERENCE_COUNTER_H
