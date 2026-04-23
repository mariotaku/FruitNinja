#ifndef MORTAR_MEMORY_POOL_H
#define MORTAR_MEMORY_POOL_H

//
// Mortar::MemoryPool<T> — fixed-capacity free-list backed pool.
// Matches the binary's MemoryPool<T> template (see SliceEffect::Node
// instantiation at 0x0016d75c Pop / 0x0016d780 Push / 0x0016df44 Create).
//
// Layout (matches binary offsets used by Pop/Push/Create):
//   +0x04  T*   m_Backing    — preallocated T[Capacity] raw storage
//   +0x08  T**  m_FreeList   — stack of free slot pointers
//   +0x0C  int  m_FreeCount  — current free-list size
//   +0x10  int  m_MaxCount   — capacity
//
// Usage pattern (matches binary):
//   pool.Create(N);              // preallocate + default-construct N
//   T* slot = pool.Pop();        // borrow a slot
//   ... use slot ...
//   pool.Push(slot);             // return to pool
//   pool.Destroy();              // tear down
//
// The binary runs T's default constructor only in Create — Push/Pop
// do NOT reconstruct. Callers are responsible for resetting per-field
// state (typically an Init() method on T) after Pop. Same here.
//
// Analysed: 2026-04-14T00:00
//

#include <cstddef>
#include <cstdlib>

namespace Mortar {

template <typename T>
class MemoryPool {
public:
    MemoryPool()
        : m_Backing(nullptr), m_FreeList(nullptr), m_FreeCount(0), m_MaxCount(0) {}

    ~MemoryPool() { Destroy(); }

    // Matches MemoryPool<T>::Create(int). Allocates `capacity` backing
    // T's via new[] (which runs T's default ctor per slot) + a
    // T*[capacity] free-list array, then fills the free list with
    // pointers to every slot (all slots start free).
    void Create(int capacity) {
        if (capacity <= 0) return;
        Destroy();

        m_Backing  = new T[capacity];
        m_FreeList = (T**)std::malloc(sizeof(T*) * capacity);
        m_MaxCount = capacity;
        m_FreeCount = capacity;
        for (int i = 0; i < capacity; ++i) {
            m_FreeList[i] = &m_Backing[i];
        }
    }

    // Matches MemoryPool<T>::~MemoryPool / Destroy. Frees both arrays.
    // Calls T destructors via delete[].
    void Destroy() {
        if (m_Backing)  { delete[] m_Backing;  m_Backing  = nullptr; }
        if (m_FreeList) { std::free(m_FreeList); m_FreeList = nullptr; }
        m_FreeCount = 0;
        m_MaxCount  = 0;
    }

    // Matches MemoryPool<T>::Pop (0x0016d75c). LIFO stack pop of the
    // free list. Returns nullptr when the pool is empty. The popped slot
    // is NOT re-constructed — caller must reset its fields.
    T* Pop() {
        if (m_FreeCount < 1) return nullptr;
        --m_FreeCount;
        return m_FreeList[m_FreeCount];
    }

    // Matches MemoryPool<T>::Push (0x0016d780). Return `slot` to the
    // free list. No-op if the pool is already full (which would indicate
    // a double-free bug in the caller).
    void Push(T* slot) {
        if (m_FreeCount < m_MaxCount) {
            m_FreeList[m_FreeCount] = slot;
            ++m_FreeCount;
        }
    }

    // Port helpers — not in binary, but useful for the active-iteration
    // patterns ActorManager / DrawActive* use where the caller tracks
    // "slot is alive" via a field on T (e.g. SplatEntity::m_bActive).

    // Direct slot access by raw index [0, Capacity). Lets callers
    // iterate every slot in backing order and check a per-T active flag.
    T*       SlotAt(int i)       { return (i >= 0 && i < m_MaxCount) ? &m_Backing[i] : nullptr; }
    const T* SlotAt(int i) const { return (i >= 0 && i < m_MaxCount) ? &m_Backing[i] : nullptr; }

    int FreeCount() const { return m_FreeCount; }
    int Capacity()  const { return m_MaxCount; }
    int InUseCount() const { return m_MaxCount - m_FreeCount; }

private:
    // Non-copyable — owning the backing buffer.
    MemoryPool(const MemoryPool&);
    MemoryPool& operator=(const MemoryPool&);

    T*   m_Backing;     // +0x04
    T**  m_FreeList;    // +0x08
    int  m_FreeCount;   // +0x0C
    int  m_MaxCount;    // +0x10
};

} // namespace Mortar

#endif
