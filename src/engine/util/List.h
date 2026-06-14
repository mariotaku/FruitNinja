#ifndef MORTAR_LIST_H
#define MORTAR_LIST_H

#include <vector>

// DIFFERS: original = Mortar::List<T> intrusive doubly-linked list (20B: FreeList*@0x0, head@0x4,
//   tail@0x8, count@0xC, m_OwnsFreeList@0x10, m_Active@0x12; 12B nodes, next@+8),
//   using std::vector because the model-cache semantics (drop-refcount-on-remove) are equivalent
//   and only the singleton path (m_pFreeList==0) is exercised in MeshManager.
template<typename T>
class List {
    std::vector<T> m_items;
public:
    void push_back(const T& item) { m_items.push_back(item); }
    void clear() { m_items.clear(); }
    int size() const { return (int)m_items.size(); }
    bool empty() const { return m_items.empty(); }

    T& operator[](int i) { return m_items[i]; }
    const T& operator[](int i) const { return m_items[i]; }

    void resize(int n) { m_items.resize(n); }
    void reserve(int n) { m_items.reserve(n); }

    void erase(int i) { m_items.erase(m_items.begin() + i); }

    // Iterator support
    typename std::vector<T>::iterator begin() { return m_items.begin(); }
    typename std::vector<T>::iterator end() { return m_items.end(); }
    typename std::vector<T>::const_iterator begin() const { return m_items.begin(); }
    typename std::vector<T>::const_iterator end() const { return m_items.end(); }
};

#endif // MORTAR_LIST_H
