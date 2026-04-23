#ifndef MORTAR_NLF_QUEUE_H
#define MORTAR_NLF_QUEUE_H

#include <cstddef>

// SPSC (single-producer single-consumer) lock-free ring buffer (16 bytes)
template<typename T>
class NLFQueue {
    T* m_Buffer;
    int m_Capacity;
    int m_ReadIdx;
    int m_WriteIdx;
public:
    NLFQueue() : m_Buffer(nullptr), m_Capacity(0), m_ReadIdx(0), m_WriteIdx(0) {}

    ~NLFQueue() {
        delete[] m_Buffer;
    }

    void Init(int capacity) {
        delete[] m_Buffer;
        m_Capacity = capacity + 1; // one extra slot to distinguish full from empty
        m_Buffer = new T[m_Capacity];
        m_ReadIdx = 0;
        m_WriteIdx = 0;
    }

    bool Push(const T& item) {
        int next = (m_WriteIdx + 1) % m_Capacity;
        if (next == m_ReadIdx) {
            return false; // full
        }
        m_Buffer[m_WriteIdx] = item;
        m_WriteIdx = next;
        return true;
    }

    bool Pop(T& item) {
        if (m_ReadIdx == m_WriteIdx) {
            return false; // empty
        }
        item = m_Buffer[m_ReadIdx];
        m_ReadIdx = (m_ReadIdx + 1) % m_Capacity;
        return true;
    }

    bool IsEmpty() const { return m_ReadIdx == m_WriteIdx; }
};

#endif // MORTAR_NLF_QUEUE_H
