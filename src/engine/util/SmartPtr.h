#ifndef MORTAR_SMART_PTR_H
#define MORTAR_SMART_PTR_H

#include <cstddef>

// Binary @ 0x00188d84 -- SmartPtr<T> is a single embedded T* (intrusive refcount via
// T's ReferenceCounter base). sizeof 4 on ARM32.
// Lifecycle: ctor+raw-ptr always AddRef; assignment is AddRef-then-Release; SetPtrCast
// is the assignment primitive in the binary (port uses operator= for the same effect).

// Intrusive smart pointer (4 bytes). Original: Mortar::SmartPtr<T>
template<typename T>
class SmartPtr {
    T* m_ptr;
public:
    SmartPtr() : m_ptr(nullptr) {}
    SmartPtr(T* p) : m_ptr(p) { if (m_ptr) m_ptr->AddRef(); }
    SmartPtr(const SmartPtr& o) : m_ptr(o.m_ptr) { if (m_ptr) m_ptr->AddRef(); }
    ~SmartPtr() { if (m_ptr) m_ptr->Release(); }

    SmartPtr& operator=(const SmartPtr& o) {
        if (this != &o) {
            if (o.m_ptr) o.m_ptr->AddRef();
            if (m_ptr) m_ptr->Release();
            m_ptr = o.m_ptr;
        }
        return *this;
    }

    SmartPtr& operator=(T* p) {
        if (p != m_ptr) {
            if (p) p->AddRef();
            if (m_ptr) m_ptr->Release();
            m_ptr = p;
        }
        return *this;
    }

    T* operator->() const { return m_ptr; }
    T& operator*() const { return *m_ptr; }
    T* Get() const { return m_ptr; }
    bool IsValid() const { return m_ptr != nullptr; }
    operator bool() const { return m_ptr != nullptr; }

    // Binary @ 0x001020a8 (SetPtr<AnimationList>) -- thin wrapper over SetPtrCast<ReferenceCounter>;
    // at the source level identical to operator=(raw).
    void SetPtr(T* raw) { *this = raw; }

    // Binary @ 0x0012ee80 (SetNull<Texture>) -- delegates to SetPtr(nullptr) -> Release-old.
    void SetNull() { *this = nullptr; }
};

// Binary @ 0x001ae748 (WrapPtr<AnimationList>), 0x001928e0 (WrapPtr<AnimationState>),
//        0x001aa4dc (WrapPtr<Model>), 0x0018a160 (WrapPtr<Texture2DFromFile_Bada>).
// Semantically identical to SmartPtr<T>(raw); kept as a separate symbol for naming
// clarity at "freshly-new'd raw -> SmartPtr" ownership-transfer sites. The new'd
// object starts with refcount 0; the WrapPtr ctor bumps it to 1.
template <typename T>
inline SmartPtr<T> WrapPtr(T* raw) {
    return SmartPtr<T>(raw);
}

// Binary @ 0x00188d84 -- SmartPtr<T> is a single embedded T* (intrusive refcount via T's ReferenceCounter base).
#ifdef __bada__
namespace { struct _SmartPtrSizeProbe : public ReferenceCounter {}; }
static_assert(sizeof(SmartPtr<_SmartPtrSizeProbe>) == 4,
              "SmartPtr<T> must be exactly 4 bytes (sizeof one T* slot)");
#endif

#endif // MORTAR_SMART_PTR_H
