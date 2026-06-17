#ifndef FN_ENGINE_UTIL_LIST_H
#define FN_ENGINE_UTIL_LIST_H

#include <cstddef>
#include <new>

// Mortar::FreeList -- v1.6.1 @ multiple sites; exercised by List<SliceEffect> etc.
// 12 bytes: m_pHeap @0 (StackHeap*), m_BlockSize @4 (ulong), m_pHead @8 (void*).
// MeshManager always constructs List with m_pFreeList==0, so FreeList::Release
// is never reached in the MeshManager path. The branch is present for future
// consumers (List<SliceEffect>, List<MortarSound*>) that use the pool path.
namespace Mortar {
struct StackHeap;

struct FreeList {
    StackHeap* m_pHeap;     // @0x00
    unsigned long m_BlockSize; // @0x04
    void* m_pHead;          // @0x08

    // TODO: v1.6.1 FreeList::Release @unknown -- body not decompiled.
    // When implemented: return block to pool or StackHeap.
    void Release(void* block) { (void)block; }
};
} // namespace Mortar

#ifdef __bada__
static_assert(sizeof(Mortar::FreeList) == 12, "FreeList must be 12 bytes");
#endif

namespace Mortar {

// Mortar::List<T> -- 20-byte intrusive singly-linked list.
// v1.6.1: ctor @0x002368cc, Clear @0x00236be0, Destroy @0x00236c5c
// Node is singly-linked, 12 bytes in the binary (T=SmartPtr<Model>=8B on ARM32 + 4B next).
// In this port SmartPtr<T> is 4 bytes (intrusive refcount), so Node = sizeof(T)+4.
//
// Layout (20 bytes, matches binary MeshManager::m_Models field):
//   m_pFreeList @0x00 (FreeList*)
//   m_pHead     @0x04 (Node*)
//   m_pTail     @0x08 (Node*)
//   m_Count     @0x0C (uint32_t)
//   m_OwnsFreeList @0x10 (short)
//   m_Active    @0x12 (short)
template<typename T>
class List {
public:
    struct Node {
        T     value;  // @0x00
        Node* next;   // @sizeof(T)
    };

    FreeList* m_pFreeList;    // @0x00
    Node*     m_pHead;        // @0x04
    Node*     m_pTail;        // @0x08
    unsigned int m_Count;     // @0x0C
    short     m_OwnsFreeList; // @0x10
    short     m_Active;       // @0x12

    // v1.6.1 List<T>::ctor @0x002368cc: zero all 6 fields, then call Clear().
    List() : m_pFreeList(0), m_pHead(0), m_pTail(0), m_Count(0),
             m_OwnsFreeList(0), m_Active(0) {
        Clear();
    }

    // v1.6.1 List<T>::Clear @0x00236be0:
    // Gate on m_Active==1. Walk node=m_pHead via ->next; per node:
    //   call ~T() on value, then free node (FreeList::Release or operator delete).
    // After walk: zero m_Count/m_pHead/m_pTail/m_Active.
    void Clear() {
        if (m_Active == 1) {
            Node* node = m_pHead;
            while (node) {
                Node* next = node->next;
                node->value.~T();
                if (m_pFreeList) {
                    m_pFreeList->Release(node);
                } else {
                    // operator delete without calling dtor again (dtor called above)
                    ::operator delete(node);
                }
                node = next;
            }
            m_Count  = 0;
            m_pHead  = 0;
            m_pTail  = 0;
            m_Active = 0;
        }
    }

    // v1.6.1 List<T>::Destroy @0x00236c5c:
    // Calls Clear(), then if (m_OwnsFreeList && m_pFreeList): ~FreeList + delete + null.
    void Destroy() {
        Clear();
        if (m_OwnsFreeList && m_pFreeList) {
            m_pFreeList->~FreeList();
            ::operator delete(m_pFreeList);
            m_pFreeList = 0;
        }
    }

    ~List() {
        Destroy();
    }

    // Append a new node at tail; set value; bump m_Count; set m_Active=1.
    void Add(const T& item) {
        Node* node = static_cast<Node*>(::operator new(sizeof(Node)));
        new (&node->value) T(item);
        node->next = 0;
        if (m_pTail) {
            m_pTail->next = node;
        } else {
            m_pHead = node;
        }
        m_pTail = node;
        m_Count++;
        m_Active = 1;
    }

    // Faithful forward-scan remove by value identity (operator== comparison).
    // TODO: v1.6.1 List<T>::Remove GOT @0x00107b20 -- exact head/tail fixup order unconfirmed.
    void Remove(const T& item) {
        Node* prev = 0;
        Node* node = m_pHead;
        while (node) {
            if (node->value == item) {
                if (prev) {
                    prev->next = node->next;
                } else {
                    m_pHead = node->next;
                }
                if (node == m_pTail) {
                    m_pTail = prev;
                }
                node->value.~T();
                if (m_pFreeList) {
                    m_pFreeList->Release(node);
                } else {
                    ::operator delete(node);
                }
                m_Count--;
                return;
            }
            prev = node;
            node = node->next;
        }
    }

    // Forward iteration helpers used by MeshManager::Find/ReleaseAll/Release.
    Node* Head() const { return m_pHead; }

    unsigned int Count() const { return m_Count; }

    bool IsEmpty() const { return m_Count == 0; }
};

} // namespace Mortar

#ifdef __bada__
namespace { struct _ListSizeProbe {}; }
static_assert(sizeof(Mortar::List<_ListSizeProbe>) == 20,
              "Mortar::List<T> must be exactly 20 bytes");
static_assert(offsetof(Mortar::List<_ListSizeProbe>, m_pFreeList)    == 0x00, "List::m_pFreeList offset");
static_assert(offsetof(Mortar::List<_ListSizeProbe>, m_pHead)        == 0x04, "List::m_pHead offset");
static_assert(offsetof(Mortar::List<_ListSizeProbe>, m_pTail)        == 0x08, "List::m_pTail offset");
static_assert(offsetof(Mortar::List<_ListSizeProbe>, m_Count)        == 0x0C, "List::m_Count offset");
static_assert(offsetof(Mortar::List<_ListSizeProbe>, m_OwnsFreeList) == 0x10, "List::m_OwnsFreeList offset");
static_assert(offsetof(Mortar::List<_ListSizeProbe>, m_Active)       == 0x12, "List::m_Active offset");
#endif

#endif // FN_ENGINE_UTIL_LIST_H
