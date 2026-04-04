#ifndef MORTAR_LIST_H
#define MORTAR_LIST_H

#include <vector>

// Mortar List<T> template matching original 20-byte layout
// Port uses std::vector internally
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
