#ifndef MORTAR_REFERENCE_COUNTER_H
#define MORTAR_REFERENCE_COUNTER_H

// Intrusive reference counting base class (12 bytes)
// Original: __ReferenceCounterData / ReferenceCounter
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

#endif // MORTAR_REFERENCE_COUNTER_H
