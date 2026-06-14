#ifndef FN_ENGINE_UTIL_LINKEDHEAP_H
#define FN_ENGINE_UTIL_LINKEDHEAP_H

#include <cstdint>
#include <cstddef>

// LinkedHeap — variable-size single-buffer block allocator.
// Binary @ 0x001942fc (adjacent FreeList/StackHeap pool is separate, not ported here).
//
// Layout (36 bytes = 0x24):
//   +0x00  m_pFreeListHead  void* (payload ptr, binary ABI)
//   +0x04  m_pFreeListTail  void* (payload ptr, binary ABI)
//   +0x08  m_pFirstBlock    Block*
//   +0x0C  m_pLastBlock     Block*
//   +0x10  m_pBuffer        uchar*
//   +0x14  m_Size           uint   (aligned buffer size)
//   +0x18  m_StartAddr      uintptr_t (bump pointer)
//   +0x1C  m_EndAddr        uintptr_t (start + size)
//   +0x20  m_GuardBandSize  uint   (= 0 this build; kept for payload offset math)
//
// Block header (16 bytes, prepended before payload):
//   +0x00  prev  (all-blocks linked list back-link)   -- binary @ Release 0x0019471e
//   +0x04  next  (all-blocks linked list forward-link)
//   +0x08  name  const char* (alloc tag, or null)
//   +0x0C  size:24  (block size including this header; top byte reserved)
//   +0x0F  flag  uint8_t  (1=FREE, 2=allocated, 4=locked/fixed)
//
// Payload starts at Block + 0x10 + (m_GuardBandSize >> 1).
// Free-list is singly-linked via payload's first word: *(void**)payload = next payload.
// m_pFreeListHead / m_pFreeListTail are payload pointers (null-terminated, no sentinel).
//
// NON-polymorphic; no vtable.
//
// Class size verified: Entity::HeapCreate calls operator new(0x24) before
// placement-new of LinkedHeap. Binary @ 0x0019d708.

namespace Mortar {

class LinkedHeap {
public:
    // Internal block header.
    // Binary layout verified vs. Release @ 0x0019469c-0x00194758:
    //   prev at +0x00 (back-link), next at +0x04 (forward-link).
    struct Block {
        Block*      prev;       // +0x00 all-blocks back-link
        Block*      next;       // +0x04 all-blocks forward-link
        const char* name;       // +0x08 alloc tag (may be null)
        unsigned int sizeFlags; // +0x0C bits[0..23] = size incl header; flag byte at +0x0F (LE MSB)

        unsigned int GetSize() const  { return sizeFlags & 0x00FFFFFFu; }
        void         SetSize(unsigned int s) { sizeFlags = (sizeFlags & 0xFF000000u) | (s & 0x00FFFFFFu); }
        uint8_t      GetFlag() const  { return (uint8_t)(sizeFlags >> 24); }
        void         SetFlag(uint8_t f){ sizeFlags = (sizeFlags & 0x00FFFFFFu) | ((unsigned int)f << 24); }
    };

    // DIFFERS: binary header = 0x10 (ARM32 sizeof Block); port uses sizeof(Block) for 64-bit pointers.
    static const size_t kHeaderSize = sizeof(Block);

    // Binary @ 0x00194948 — ctor: align size to 4, allocate buffer, init fields.
    explicit LinkedHeap(unsigned int size);

    // Binary @ 0x00194930 — dtor: ReleaseAll then delete[] buffer.
    ~LinkedHeap();

    // Binary @ 0x001945dc — reset bump pointer; all blocks lost (= Entity::HeapClear).
    void ReleaseAll();

    // Binary @ 0x0019490c — Allocate(sz, name) -> AllocateMemory(sz, name, 2)
    void* Allocate(unsigned int sz, const char* name);

    // Binary @ 0x001948f4 — AllocateFixed(sz, name) -> AllocateMemory(sz, name, 4)
    void* AllocateFixed(unsigned int sz, const char* name);

    // Binary @ 0x001947f0 — core allocator: free-list first-fit then bump.
    void* AllocateMemory(unsigned int sz, const char* name, uint8_t flag);

    // Binary @ 0x0019469c — mark block free, coalesce, possibly rewind bump.
    void Release(void* payload, bool coalesce);

    // Binary @ 0x001945f8 — shrink block in place; split remainder.
    void Resize(void* payload, unsigned int newSz);

    // Binary @ 0x001944c0 — first-fit free-list search (out param receives payload).
    bool FreeListSearch(unsigned int need, void*& out);

    // Binary @ 0x00194474 — append payload to free-list tail.
    void FreeListAdd(void* payload);

    // Binary @ 0x0019448c — remove payload from free-list (singly-linked, O(n)).
    void FreeListRemove(void* payload);

    // Binary @ 0x00194444 — m_Size - GetSizeOfUsedBlocks() (= Entity::HeapGetFree).
    unsigned int GetTotalFreeMemory() const;

    // Binary @ 0x0019440c — sum of sizes of allocated/fixed blocks.
    unsigned int GetSizeOfUsedBlocks() const;

    // Binary @ 0x00194428 — sum of sizes of free blocks.
    unsigned int GetSizeOfUnusedBlocks() const;

    // Binary @ 0x001943e4 — max(largest-free-block-size, end - startAddr).
    unsigned int GetLargestFreeBlock() const;

    // Returns true if payload was allocated from this heap's buffer.
    // Used by Entity::operator delete to guard against releasing system-heap
    // pointers through the entity heap when the heap ran out of space and
    // operator new fell back to ::operator new.
    bool Contains(void* payload) const {
        uintptr_t p = reinterpret_cast<uintptr_t>(payload);
        uintptr_t bufStart = reinterpret_cast<uintptr_t>(m_pBuffer);
        return p >= bufStart && p < m_EndAddr;
    }

    // Binary @ 0x001945bc — debug usage dump (= Entity::HeapDisplay).
    void DisplayUsage(bool show);

private:
    void*       m_pFreeListHead;    // +0x00 payload ptr (binary ABI)
    void*       m_pFreeListTail;    // +0x04 payload ptr (binary ABI)
    Block*      m_pFirstBlock;      // +0x08
    Block*       m_pLastBlock;       // +0x0C
    uint8_t*    m_pBuffer;          // +0x10
    unsigned int m_Size;            // +0x14
    uintptr_t   m_StartAddr;        // +0x18 (bump pointer; advances on alloc)
    uintptr_t   m_EndAddr;          // +0x1C (= m_StartAddr_initial + m_Size)
    unsigned int m_GuardBandSize;   // +0x20 (= 0 this build)

    // Payload <-> block header conversions.
    Block* PayloadToBlock(void* payload) const {
        return reinterpret_cast<Block*>(
            reinterpret_cast<uint8_t*>(payload) - kHeaderSize - (m_GuardBandSize >> 1));
    }
    void* BlockToPayload(Block* blk) const {
        return reinterpret_cast<uint8_t*>(blk) + kHeaderSize + (m_GuardBandSize >> 1);
    }

    // Free-list next-link: stored in the payload's first word as a payload pointer.
    static void* GetFreeNext(void* payload) {
        return *reinterpret_cast<void**>(payload);
    }
    static void SetFreeNext(void* payload, void* next) {
        *reinterpret_cast<void**>(payload) = next;
    }

#ifdef FN_TEST
public:
    // Test-only accessors for all-blocks list integrity checks.
    Block*    TestGetFirstBlock() const { return m_pFirstBlock; }
    uint8_t*  TestGetBuffer()     const { return m_pBuffer; }
    unsigned int TestGetSize()    const { return m_Size; }
private:
#endif

    // Per-op integrity walker gated on FN_HEAPCHECK env var.
    // Walks the all-blocks list and verifies structural invariants.
    // On the first violation: logs to stderr and FN_HEAPLOG file (if set),
    // then calls abort(). opLabel is e.g. "A 320" or "R".
    bool CheckIntegrity(const char* opLabel);
};

} // namespace Mortar

// Layout lock: mirrors binary's 0x24-byte LinkedHeap.
// Skipped in asm-verify cross-build because GCC 4.4 rejects offsetof/
// __builtin_offsetof on private members from outside the class body, and
// in-class static_assert can't use sizeof(incomplete_type) in C++0x mode.
// The real Bada toolchain sees complete private-member access and passes.
#if defined(__bada__) && !defined(FN_ASM_VERIFY_CROSS)
static_assert(sizeof(Mortar::LinkedHeap) == 0x24, "LinkedHeap size mismatch (expected 0x24)");
#endif

#endif  // FN_ENGINE_UTIL_LINKEDHEAP_H
