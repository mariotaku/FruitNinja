// LinkedHeap — variable-size single-buffer block allocator.
// Binary @ 0x001942fc (adjacent FreeList/StackHeap pool omitted — out of scope).

#include "util/LinkedHeap.h"
#include <cstring>
#include <cstdio>
#include <new>

namespace Mortar {

// Binary @ 0x00194948 — ctor.
// size is aligned up to 4; buffer allocated; bump/end pointers set; heads zeroed.
LinkedHeap::LinkedHeap(unsigned int size)
{
    unsigned int aligned = (size + 3u) & ~3u;
    m_pBuffer       = new uint8_t[aligned];
    m_Size          = aligned;
    m_StartAddr     = reinterpret_cast<uintptr_t>(m_pBuffer);
    m_EndAddr       = m_StartAddr + aligned;
    m_GuardBandSize = 0;
    m_pFreeListHead = 0;
    m_pFreeListTail = 0;
    m_pFirstBlock   = 0;
    m_pLastBlock    = 0;
}

// Binary @ 0x00194930 — dtor: ReleaseAll then delete[] buffer.
LinkedHeap::~LinkedHeap()
{
    ReleaseAll();
    delete[] m_pBuffer;
    m_pBuffer = 0;
}

// Binary @ 0x001945dc — reset: zero free-list heads, rewind bump to buffer start.
// All blocks are implicitly gone. (= Entity::HeapClear)
void LinkedHeap::ReleaseAll()
{
    m_pFreeListHead = 0;  // payload ptr
    m_pFreeListTail = 0;  // payload ptr
    m_pFirstBlock   = 0;
    m_pLastBlock    = 0;
    m_StartAddr     = reinterpret_cast<uintptr_t>(m_pBuffer);
}

// Binary @ 0x0019490c
void* LinkedHeap::Allocate(unsigned int sz, const char* name)
{
    return AllocateMemory(sz, name, 2);
}

// Binary @ 0x001948f4
void* LinkedHeap::AllocateFixed(unsigned int sz, const char* name)
{
    return AllocateMemory(sz, name, 4);
}

// Binary @ 0x001947f0 — core allocator.
// sz aligned to 4; need = guard + kHeaderSize + sz.
// 1. Free-list first-fit: if hit, reuse (rewrite flag+name), return payload.
// 2. Bump-allocate at m_StartAddr if fits before m_EndAddr, chain all-blocks list.
// Returns payload ptr (header + kHeaderSize + guard/2).
void* LinkedHeap::AllocateMemory(unsigned int sz, const char* name, uint8_t flag)
{
    unsigned int aligned = (sz + 3u) & ~3u;
    unsigned int need    = (unsigned int)((m_GuardBandSize >> 1) + kHeaderSize + aligned);

    // Free-list search first.
    void* hit = 0;
    if (FreeListSearch(need, hit)) {
        Block* blk = PayloadToBlock(hit);
        blk->SetFlag(flag);
        blk->name = name;
        return hit;
    }

    // Bump-allocate.
    if (m_StartAddr + need > m_EndAddr) {
        return 0;
    }

    Block* blk = reinterpret_cast<Block*>(m_StartAddr);
    blk->prev  = m_pLastBlock;   // +0x00 back-link to previous tail
    blk->next  = 0;              // +0x04 forward, none yet
    blk->name  = name;
    blk->sizeFlags = 0;
    blk->SetSize(need);
    blk->SetFlag(flag);

    if (m_pLastBlock) {
        m_pLastBlock->next = blk;
    } else {
        m_pFirstBlock = blk;
    }
    m_pLastBlock = blk;

    m_StartAddr += need;
    return BlockToPayload(blk);
}

// Binary @ 0x0019469c — free a payload.
// Marks block FREE (flag=1), adds to free-list, then coalesces:
//   - If block is the last allocated block: pop trailing FREE blocks and rewind
//     m_StartAddr by subtracting each block's size (binary @ 0x001946dc-0x0019471e).
//   - Else: find anchor (last consecutive prev-free block), then absorb consecutive
//     forward free blocks into anchor (binary @ 0x00194720-0x00194758).
// Block prev is at +0x00, next at +0x04 (binary layout).
void LinkedHeap::Release(void* payload, bool /*coalesce*/)
{
    if (!payload) return;
    Block* blk = PayloadToBlock(payload);
    uint8_t f  = blk->GetFlag();
    if (f == 1 || f == 4) return;  // already free or locked

    blk->SetFlag(1);
    blk->name = "freed";
    FreeListAdd(payload);

    // If this block is at the tail of the bump region, rewind.
    if (blk == m_pLastBlock) {
        // Walk backwards while the tail is free; subtract each block's size from
        // m_StartAddr (binary: SUB m_StartAddr, cur->GetSize() -- NOT reset to cur).
        Block* cur = m_pLastBlock;
        while (cur && cur->GetFlag() == 1) {
            FreeListRemove(BlockToPayload(cur));
            m_StartAddr -= cur->GetSize();    // binary: subtract size, not = (uintptr_t)cur
            Block* pr = cur->prev;            // +0x00 back-link
            if (pr) {
                pr->next = 0;                 // +0x04 forward
            } else {
                m_pFirstBlock = 0;
            }
            m_pLastBlock = pr;
            cur = pr;
        }
        return;
    }

    // Non-tail path (binary @ 0x00194720-0x00194758):
    // 1. Walk the PREV chain to find the anchor = last consecutive FREE block going back
    //    (stop when prev is null or prev is NOT free).
    Block* anchor = blk;
    for (;;) {
        Block* p = anchor->prev;
        if (!p || p->GetFlag() != 1) break;
        anchor = p;
    }
    // 2. Absorb consecutive FREE forward (next) blocks into anchor.
    Block* fwd = anchor->next;
    while (fwd && fwd->GetFlag() == 1) {
        FreeListRemove(BlockToPayload(fwd));
        anchor->SetSize(anchor->GetSize() + fwd->GetSize());
        Block* fn = fwd->next;                // +0x04
        anchor->next = fn;
        if (fn) {
            fn->prev = anchor;                // +0x00 fix successor's back-link
        } else {
            m_pLastBlock = anchor;            // guard: prevents -1 deref on subsequent tail-rewind
        }
        fwd = fn;
    }
}

// Binary @ 0x001945f8 — shrink block to newSz in place.
// If there is leftover space, split it off as a free block or rewind bump.
void LinkedHeap::Resize(void* payload, unsigned int newSz)
{
    if (!payload) return;
    Block* blk   = PayloadToBlock(payload);
    unsigned int alignedNew = (newSz + 3u) & ~3u;
    unsigned int need       = (unsigned int)((m_GuardBandSize >> 1) + kHeaderSize + alignedNew);
    unsigned int oldSize    = blk->GetSize();
    if (need >= oldSize) return;

    unsigned int remainder = oldSize - need;
    blk->SetSize(need);

    uintptr_t splitAddr = reinterpret_cast<uintptr_t>(blk) + need;
    Block* split        = reinterpret_cast<Block*>(splitAddr);

    if (blk == m_pLastBlock) {
        // Just rewind bump.
        m_StartAddr = splitAddr;
        return;
    }

    // Insert split block into all-blocks list.
    Block* nxt  = blk->next;
    split->prev = blk;
    split->next = nxt;
    if (nxt) {
        nxt->prev = split;
    } else {
        m_pLastBlock = split;
    }
    blk->next = split;
    split->name     = 0;
    split->sizeFlags = 0;
    split->SetSize(remainder);
    split->SetFlag(1);
    FreeListAdd(BlockToPayload(split));
}

// Binary @ 0x001944c0 — first-fit free-list scan.
// Exact/near fit: remove from free-list, return true + out payload.
// Larger fit: split remainder back onto free-list, return true.
// No fit: return false.
// Split threshold: remainder > headerSize (guard=0), i.e. > sizeof(Block) on this build.
// DIFFERS: original threshold = guardBand + 0x10 (ARM32 sizeof Block = 0x10 when guard=0),
// port uses kHeaderSize = sizeof(Block) (0x20 on x64) to match pointer-size-agnostic layout.
bool LinkedHeap::FreeListSearch(unsigned int need, void*& out)
{
    void* curPayload = m_pFreeListHead;
    while (curPayload) {
        Block* cur       = PayloadToBlock(curPayload);
        unsigned int sz  = cur->GetSize();
        if (sz >= need) {
            unsigned int remainder = sz - need;
            // Split only when remainder is large enough to hold a header + at least 1 byte payload.
            unsigned int headerSize = (unsigned int)((m_GuardBandSize >> 1) + kHeaderSize);
            if (remainder > headerSize) {
                // Split: resize current block down, create remainder block.
                cur->SetSize(need);
                uintptr_t remAddr = reinterpret_cast<uintptr_t>(cur) + need;
                Block* remBlk     = reinterpret_cast<Block*>(remAddr);
                remBlk->name      = 0;
                remBlk->sizeFlags = 0;
                remBlk->SetSize(remainder);
                remBlk->SetFlag(1);
                // Insert into all-blocks list after cur.
                Block* nxt    = cur->next;
                remBlk->prev  = cur;
                remBlk->next  = nxt;
                if (nxt) {
                    nxt->prev = remBlk;
                } else {
                    m_pLastBlock = remBlk;
                }
                cur->next = remBlk;
                // Remove cur from free-list; add remainder.
                FreeListRemove(curPayload);
                FreeListAdd(BlockToPayload(remBlk));
            } else {
                FreeListRemove(curPayload);
            }
            out = curPayload;
            return true;
        }
        curPayload = GetFreeNext(curPayload);
    }
    return false;
}

// Binary @ 0x00194474 — append payload to free-list tail (singly-linked via payload word).
void LinkedHeap::FreeListAdd(void* payload)
{
    SetFreeNext(payload, 0);
    if (!m_pFreeListHead) {
        m_pFreeListHead = payload;
        m_pFreeListTail = payload;
    } else {
        SetFreeNext(m_pFreeListTail, payload);
        m_pFreeListTail = payload;
    }
}

// Binary @ 0x0019448c — remove payload from free-list (singly-linked, O(n)).
void LinkedHeap::FreeListRemove(void* payload)
{
    void* prev = 0;
    void* cur  = m_pFreeListHead;
    while (cur) {
        void* nxt = GetFreeNext(cur);
        if (cur == payload) {
            if (prev) {
                SetFreeNext(prev, nxt);
            } else {
                m_pFreeListHead = nxt;
            }
            if (m_pFreeListTail == payload) {
                m_pFreeListTail = prev;
            }
            SetFreeNext(payload, 0);
            return;
        }
        prev = cur;
        cur  = nxt;
    }
}

// Binary @ 0x00194444 — (= Entity::HeapGetFree)
unsigned int LinkedHeap::GetTotalFreeMemory() const
{
    return m_Size - GetSizeOfUsedBlocks();
}

// Binary @ 0x0019440c — sum sizes of non-free blocks.
unsigned int LinkedHeap::GetSizeOfUsedBlocks() const
{
    unsigned int total = 0;
    Block* cur = m_pFirstBlock;
    while (cur) {
        if (cur->GetFlag() != 1) {
            total += cur->GetSize();
        }
        cur = cur->next;
    }
    return total;
}

// Binary @ 0x00194428 — sum sizes of free blocks.
unsigned int LinkedHeap::GetSizeOfUnusedBlocks() const
{
    unsigned int total = 0;
    Block* cur = m_pFirstBlock;
    while (cur) {
        if (cur->GetFlag() == 1) {
            total += cur->GetSize();
        }
        cur = cur->next;
    }
    return total;
}

// Binary @ 0x001943e4 — max(largest free block in list, remaining bump space).
unsigned int LinkedHeap::GetLargestFreeBlock() const
{
    unsigned int best = (unsigned int)(m_EndAddr - m_StartAddr);
    void* cur = m_pFreeListHead;
    while (cur) {
        Block* blk      = PayloadToBlock(cur);
        unsigned int sz = blk->GetSize();
        if (sz > best) best = sz;
        cur = GetFreeNext(cur);
    }
    return best;
}

// Binary @ 0x001945bc — debug usage dump (= Entity::HeapDisplay).
// Debug print stripped in shipping build; port mirrors that.
void LinkedHeap::DisplayUsage(bool show)
{
    if (!show) return;
    Block* cur = m_pFirstBlock;
    while (cur) {
        (void)cur;
        cur = cur->next;
    }
}

}  // namespace Mortar
