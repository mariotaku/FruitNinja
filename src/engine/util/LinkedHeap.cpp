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
    m_pFreeListHead = 0;
    m_pFreeListTail = 0;
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
// sz aligned to 4; need = guard + 0x10 + sz.
// 1. Free-list first-fit: if hit, reuse (rewrite flag+name), return payload.
// 2. Bump-allocate at m_StartAddr if fits before m_EndAddr, chain all-blocks list.
// Returns payload ptr (header + 0x10 + guard/2).
void* LinkedHeap::AllocateMemory(unsigned int sz, const char* name, uint8_t flag)
{
    unsigned int aligned = (sz + 3u) & ~3u;
    unsigned int need    = (m_GuardBandSize >> 1) + 0x10u + aligned;

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
    blk->next  = 0;
    blk->prev  = m_pLastBlock;
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
//     m_StartAddr to the earliest contiguous trailing free run.
//   - Else: merge forward adjacent FREE blocks into this one.
void LinkedHeap::Release(void* payload, bool /*coalesce*/)
{
    if (!payload) return;
    Block* blk = PayloadToBlock(payload);
    uint8_t f  = blk->GetFlag();
    if (f == 1 || f == 4) return;  // already free or locked

    blk->SetFlag(1);
    blk->name = "freed";
    FreeListAdd(blk);

    // If this block is at the tail of the bump region, rewind.
    if (blk == m_pLastBlock) {
        // Walk backwards while the tail is free.
        Block* cur = m_pLastBlock;
        while (cur && cur->GetFlag() == 1) {
            FreeListRemove(cur);
            Block* pr = cur->prev;
            if (pr) {
                pr->next = 0;
            } else {
                m_pFirstBlock = 0;
            }
            m_pLastBlock = pr;
            m_StartAddr  = reinterpret_cast<uintptr_t>(cur);
            cur = pr;
        }
        return;
    }

    // Merge forward adjacent free blocks into blk.
    Block* fwd = blk->next;
    while (fwd && fwd->GetFlag() == 1) {
        unsigned int merged = blk->GetSize() + fwd->GetSize();
        FreeListRemove(fwd);
        Block* fn = fwd->next;
        if (fn) {
            fn->prev = blk;
        } else {
            m_pLastBlock = blk;
        }
        blk->next = fn;
        blk->SetSize(merged);
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
    unsigned int need       = (m_GuardBandSize >> 1) + 0x10u + alignedNew;
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
    FreeListAdd(split);
}

// Binary @ 0x001944c0 — first-fit free-list scan.
// Exact/near fit: remove from free-list, return true + out payload.
// Larger fit: split remainder back onto free-list, return true.
// No fit: return false.
bool LinkedHeap::FreeListSearch(unsigned int need, void*& out)
{
    Block* cur = m_pFreeListHead;
    while (cur) {
        unsigned int sz = cur->GetSize();
        if (sz >= need) {
            unsigned int remainder = sz - need;
            if (remainder > 0x10u + 4u) {
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
                FreeListRemove(cur);
                FreeListAdd(remBlk);
            } else {
                FreeListRemove(cur);
            }
            out = BlockToPayload(cur);
            return true;
        }
        cur = GetFreeNext(cur);
    }
    return false;
}

// Binary @ 0x00194474 — append blk to free-list tail (singly-linked via payload word).
void LinkedHeap::FreeListAdd(Block* blk)
{
    SetFreeNext(blk, 0);
    if (!m_pFreeListTail) {
        m_pFreeListHead = blk;
        m_pFreeListTail = blk;
    } else {
        SetFreeNext(m_pFreeListTail, blk);
        m_pFreeListTail = blk;
    }
}

// Binary @ 0x0019448c — remove blk from free-list (singly-linked, O(n)).
void LinkedHeap::FreeListRemove(Block* blk)
{
    Block* prev = 0;
    Block* cur  = m_pFreeListHead;
    while (cur) {
        Block* nxt = GetFreeNext(cur);
        if (cur == blk) {
            if (prev) {
                SetFreeNext(prev, nxt);
            } else {
                m_pFreeListHead = nxt;
            }
            if (m_pFreeListTail == blk) {
                m_pFreeListTail = prev;
            }
            SetFreeNext(blk, 0);
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
    Block* cur = m_pFreeListHead;
    while (cur) {
        unsigned int sz = cur->GetSize();
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
