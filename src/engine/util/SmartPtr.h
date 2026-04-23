#ifndef MORTAR_SMART_PTR_H
#define MORTAR_SMART_PTR_H

#include <cstddef>

// Intrusive smart pointer (4 bytes)
// Original: SmartPtr<T> with ref counting via ReferenceCounter base
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

    // Matches Mortar::SmartPtr<T>::SetNull (binary pattern)
    void SetNull() {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }
};

#endif // MORTAR_SMART_PTR_H
