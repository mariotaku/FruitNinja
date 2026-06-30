#ifndef FN_ENGINE_UTIL_LIST_H
#define FN_ENGINE_UTIL_LIST_H

#include <cstddef>
#include <new>

// Mortar::FreeList -- v1.6.1 @ multiple sites; exercised by List<SliceEffect> etc.
// 12 bytes: m_pHeap @0 (StackHeap*), m_BlockSize @4 (ulong), m_pHead @8 (void*).
// MeshManager always constructs List with m_pPool==0, so FreeList::Release
// is never reached in the MeshManager path. The branch is present for future
// consumers (List<SliceEffect>, List<MortarSound*>) that use the pool path.
namespace Mortar {
struct StackHeap;

struct FreeList {
    StackHeap* m_pHeap;       // @0x00
    unsigned long m_BlockSize; // @0x04
    void* m_pHead;             // @0x08

    // TODO: v1.6.1 FreeList::Release @unknown -- body not decompiled.
    // When implemented: return block to pool or StackHeap.
    void Release(void* block) { (void)block; }
};
} // namespace Mortar

#ifdef __bada__
static_assert(sizeof(Mortar::FreeList) == 12, "FreeList must be 12 bytes");
#endif

namespace Mortar {

// Mortar::List<T> -- binary-faithful doubly-linked pooled list.
// v1.6.1: ctor @0x002368cc, Clear @0x00236be0, Destroy @0x00236c5c.
// AddNodeToHead @0x001e3158 (List<SliceEffect> instantiation).
// Remove(Node*) @0x001e36c8 (List<SliceEffect> instantiation).
//
// Head layout (20 bytes, matches binary for all T):
//   m_pPool    @0x00 (FreeList* or MemoryPool<Node>*; 0 => operator new/delete)
//   m_pHead    @0x04 (Node*)
//   m_pTail    @0x08 (Node*)
//   m_Count    @0x0C (uint32_t)
//   m_OwnsPool @0x10 (short)
//   m_Active   @0x12 (short tri-state: 0=empty, 1=Add()-owned, 2=AddNodeToHead()-populated)
//
// Node layout: { T value; Node* m_pPrev; union { Node* m_pNext; Node* next; } }
//   prev BEFORE next (binary: m_pPrev at T+sizeof(T), m_pNext at T+sizeof(T)+4).
//   The 'next' alias keeps MeshManager's node->next forward-walk source-compatible.
//
// m_Active semantics:
//   0  = empty (no nodes ever added, or cleared)
//   1  = Add()-populated: list OWNS nodes, Clear()/Remove(Node*) frees via pool/delete
//   2  = AddNodeToHead()-populated: caller PRE-ALLOCATED nodes, list does NOT free them
//        on Remove(Node*); caller handles pool push. Count hits 0 -> m_Active=0.
template<typename T>
class List {
public:
    struct Node {
        T     value;    // @0x00 .. @sizeof(T)-1: payload
        Node* m_pPrev;  // @sizeof(T): backward link (prev BEFORE next)
        union {
            Node* m_pNext;  // @sizeof(T)+4: forward link (binary name)
            Node* next;     // same memory -- backward compat alias for MeshManager's node->next
        };
    };

    // Iterator for safe forward traversal. Captures next before the caller
    // may Remove the current node (use it.Next() before Remove).
    class Iterator {
    public:
        Node* m_pNode;

        explicit Iterator(Node* p) : m_pNode(p) {}

        bool Okay() const { return m_pNode != 0; }

        // Returns Iterator for the next node without advancing *this.
        Iterator Next() const {
            return Iterator(m_pNode ? m_pNode->m_pNext : 0);
        }

        Iterator& operator++() {
            if (m_pNode) m_pNode = m_pNode->m_pNext;
            return *this;
        }

        T& operator*() { return m_pNode->value; }

        // Raw node pointer access for Remove(Node*) call sites.
        Node* Get() const { return m_pNode; }
    };

    FreeList*    m_pPool;    // @0x00 (was m_pFreeList)
    Node*        m_pHead;    // @0x04
    Node*        m_pTail;    // @0x08
    unsigned int m_Count;    // @0x0C
    short        m_OwnsPool; // @0x10 (was m_OwnsFreeList)
    short        m_Active;   // @0x12

    // v1.6.1 List<T>::ctor @0x002368cc: zero all 6 fields, then call Clear().
    List() : m_pPool(0), m_pHead(0), m_pTail(0), m_Count(0),
             m_OwnsPool(0), m_Active(0) {
        Clear();
    }

    // v1.6.1 List<T>::Clear @0x00236be0:
    // Gate on m_Active==1 (Add()-owned). Walk via m_pNext; per node:
    //   call ~T() on value, then free node (FreeList::Release or operator delete).
    // After walk (or if m_Active!=1): zero m_Count/m_pHead/m_pTail/m_Active.
    // When m_Active==2 (AddNodeToHead-populated): just zero pointers; nodes are
    // caller-owned (pool-backed), do NOT double-free.
    void Clear() {
        if (m_Active == 1) {
            Node* node = m_pHead;
            while (node) {
                Node* nxt = node->m_pNext;
                node->value.~T();
                if (m_pPool) {
                    m_pPool->Release(node);
                } else {
                    ::operator delete(node);
                }
                node = nxt;
            }
        }
        m_Count  = 0;
        m_pHead  = 0;
        m_pTail  = 0;
        m_Active = 0;
    }

    // v1.6.1 List<T>::Destroy @0x00236c5c:
    // Calls Clear(), then if (m_OwnsPool && m_pPool): ~FreeList + delete + null.
    void Destroy() {
        Clear();
        if (m_OwnsPool && m_pPool) {
            m_pPool->~FreeList();
            ::operator delete(m_pPool);
            m_pPool = 0;
        }
    }

    ~List() {
        Destroy();
    }

    // Add(const T&) -- v1.6.1 non-intrusive path (MeshManager).
    // Allocates a new Node via operator new, copy-constructs T, appends to tail.
    // Sets m_Active=1 (list owns the node; Clear() will free it).
    void Add(const T& item) {
        Node* node = static_cast<Node*>(::operator new(sizeof(Node)));
        new (&node->value) T(item);
        node->m_pPrev = m_pTail;
        node->m_pNext = 0;
        if (m_pTail) {
            m_pTail->m_pNext = node;
        } else {
            m_pHead = node;
        }
        m_pTail = node;
        m_Count++;
        m_Active = 1;
    }

    // AddNodeToHead(Node*) -- v1.6.1 intrusive path (SliceEffect / pool-backed).
    // v1.6.1 List<SliceEffect>::AddNodeToHead @0x001e3158.
    // Takes a pre-allocated Node (caller popped it from the pool), splices at head.
    // NO allocator call -- pure pointer writes only.
    // Transitions m_Active: 0->2 (populated-but-not-owned).
    void AddNodeToHead(Node* node) {
        node->m_pPrev = 0;
        node->m_pNext = m_pHead;
        if (m_pHead) {
            m_pHead->m_pPrev = node;
        } else {
            m_pTail = node;
        }
        m_pHead = node;
        m_Count++;
        if (m_Active == 0) {
            m_Active = 2;
        }
    }

    // Remove(const T& item) -- by-value scan; for MeshManager's Release(SmartPtr<Model>).
    // Unlinks and frees the first node whose value equals item (operator==).
    // Gates on m_Active==1 path: frees via m_pPool->Release or operator delete.
    void Remove(const T& item) {
        Node* node = m_pHead;
        while (node) {
            if (node->value == item) {
                _Unlink(node);
                node->value.~T();
                if (m_pPool) {
                    m_pPool->Release(node);
                } else {
                    ::operator delete(node);
                }
                if (m_Count > 0) m_Count--;
                return;
            }
            node = node->m_pNext;
        }
    }

    // Remove(Node*) -- by-pointer; v1.6.1 List<SliceEffect>::Remove @0x001e36c8.
    // Unlinks the node from the doubly-linked chain.
    // m_Active==1: frees the node (via pool/delete); m_Active==2: just unlinks
    // (caller is responsible for returning the node to the pool).
    // When count reaches 0, resets m_Active to 0.
    void Remove(Node* node) {
        _Unlink(node);
        if (m_Count > 0) m_Count--;
        if (m_Active == 1) {
            node->value.~T();
            if (m_pPool) {
                m_pPool->Release(node);
            } else {
                ::operator delete(node);
            }
        }
        if (m_Count == 0) {
            m_Active = 0;
        }
    }

    // Head() -- returns first node pointer (forward walk entry point for MeshManager).
    Node* Head() const { return m_pHead; }

    // Begin() / begin() -- returns Iterator positioned at m_pHead.
    // Use it.Next() before Remove(it.Get()) to capture next safely.
    Iterator Begin() const { return Iterator(m_pHead); }
    Iterator begin() const { return Iterator(m_pHead); }

    unsigned int Count()  const { return m_Count; }
    bool         IsEmpty() const { return m_Count == 0; }

private:
    // Unlinks node from the doubly-linked chain without freeing it.
    void _Unlink(Node* node) {
        if (node->m_pPrev) {
            node->m_pPrev->m_pNext = node->m_pNext;
        } else {
            m_pHead = node->m_pNext;
        }
        if (node->m_pNext) {
            node->m_pNext->m_pPrev = node->m_pPrev;
        } else {
            m_pTail = node->m_pPrev;
        }
        node->m_pPrev = 0;
        node->m_pNext = 0;
    }
};

} // namespace Mortar

#ifdef __bada__
namespace { struct _ListHeadProbe {}; }
static_assert(sizeof(Mortar::List<_ListHeadProbe>) == 20,
              "Mortar::List<T> head must be exactly 20 bytes");
static_assert(offsetof(Mortar::List<_ListHeadProbe>, m_pPool)    == 0x00, "List::m_pPool offset");
static_assert(offsetof(Mortar::List<_ListHeadProbe>, m_pHead)    == 0x04, "List::m_pHead offset");
static_assert(offsetof(Mortar::List<_ListHeadProbe>, m_pTail)    == 0x08, "List::m_pTail offset");
static_assert(offsetof(Mortar::List<_ListHeadProbe>, m_Count)    == 0x0C, "List::m_Count offset");
static_assert(offsetof(Mortar::List<_ListHeadProbe>, m_OwnsPool) == 0x10, "List::m_OwnsPool offset");
static_assert(offsetof(Mortar::List<_ListHeadProbe>, m_Active)   == 0x12, "List::m_Active offset");
#endif

#endif // FN_ENGINE_UTIL_LIST_H
