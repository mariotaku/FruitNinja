#ifndef MORTAR_SMART_PTR_H
#define MORTAR_SMART_PTR_H

#include <cstddef>

// Binary @ 0x00188d84 -- SmartPtr<T> is a single embedded T* (intrusive refcount via
// T's ReferenceCounter base). sizeof 4 on ARM32.
// Lifecycle: ctor+raw-ptr always AddRef; assignment is AddRef(new)->Release(old)->store
// via the shared SetPtrCast primitive; no self-assign guard needed (AddRef-before-Release
// is net-zero-safe on self). assign() is NOINLINE so each set site emits a `bl` to the
// shared instantiation rather than inlining AddRef/Release/store (matches binary codegen).

#if defined(__GNUC__)
#define MORTAR_NOINLINE __attribute__((noinline))
#else
#define MORTAR_NOINLINE
#endif

namespace Mortar {

// Intrusive smart pointer (4 bytes). Matches binary `Mortar::SmartPtr<T>`.
template<typename T>
class SmartPtr {
    T* m_ptr;

    // ASM-spec v1.6.1 SmartPtr<T>::SetPtrCast @0x0019a4ac: AddRef(new) -> store -> Release(old),
    // no self-assign short-circuit. NOINLINE so each set site emits `bl SmartPtr<T>::assign`
    // (the binary tail-calls a single shared SetPtrCast) instead of inlining AddRef/Release/store.
    MORTAR_NOINLINE void assign(T* p) {
        if (p) p->AddRef();
        if (m_ptr) m_ptr->Release();
        m_ptr = p;
    }

    MORTAR_NOINLINE void release() {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

public:
    SmartPtr() : m_ptr(nullptr) {}
    // m_ptr must be nullptr before assign() so the Release-old inside assign is a no-op.
    SmartPtr(T* p) : m_ptr(nullptr) { assign(p); }
    SmartPtr(const SmartPtr& o) : m_ptr(nullptr) { assign(o.m_ptr); }
    ~SmartPtr() { release(); }

    SmartPtr& operator=(const SmartPtr& o) { assign(o.m_ptr); return *this; }
    SmartPtr& operator=(T* p)              { assign(p);       return *this; }

    T* operator->() const { return m_ptr; }
    T& operator*() const { return *m_ptr; }
    T* Get() const { return m_ptr; }
    bool IsValid() const { return m_ptr != nullptr; }
    operator bool() const { return m_ptr != nullptr; }

    bool operator==(const SmartPtr& o) const { return m_ptr == o.m_ptr; }
    bool operator!=(const SmartPtr& o) const { return m_ptr != o.m_ptr; }

    // Binary @ 0x001020a8 (SetPtr<AnimationList>) -- thin wrapper over SetPtrCast<ReferenceCounter>.
    void SetPtr(T* raw) { assign(raw); }

    // Binary @ 0x0012ee80 (SetNull<Texture>) -- delegates to SetPtr(nullptr) -> Release-old.
    void SetNull() { assign(nullptr); }

    // Test/debug-only accessor: exposes the pointee's ReferenceCounter strong count
    // (Mortar::ReferenceCounter::GetRefCount(), +0x04). Returns -1 when null. No
    // production call sites -- used by refcount-imbalance regression tests
    // (e.g. tests/test_ring_texture_lifecycle.cpp) to detect AddRef/Release drift
    // across repeated screen create/teardown cycles on a shared texture.
    int DebugRefCount() const { return m_ptr ? m_ptr->GetRefCount() : -1; }
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

}  // namespace Mortar

// Binary @ 0x00188d84 -- SmartPtr<T> is a single embedded T* (intrusive refcount via T's ReferenceCounter base).
#ifdef __bada__
#include "util/ReferenceCounter.h"
namespace { struct _SmartPtrSizeProbe : public Mortar::ReferenceCounter {}; }
static_assert(sizeof(Mortar::SmartPtr<_SmartPtrSizeProbe>) == 4,
              "SmartPtr<T> must be exactly 4 bytes (sizeof one T* slot)");
#endif

#endif // MORTAR_SMART_PTR_H
