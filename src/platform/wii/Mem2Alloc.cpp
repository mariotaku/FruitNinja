// Port specific: MEM2 free-list allocator. See Mem2Alloc.h for the "why" --
// this file is the "how".
//
// A compact first-fit, address-ordered free-list allocator (like a tiny
// dlmalloc): each free block is a node in a singly-linked list threaded
// through the block's own first bytes (address, size). Alloc walks the list
// for the first block that fits, splits off the remainder if it's big enough
// to be its own block, and returns the rest to the list unchanged. Free
// re-inserts the block in address order and coalesces with the immediate
// prev/next neighbours when they're adjacent -- this is what keeps
// repeated texture load/evict churn from fragmenting the arena into
// unusably-small slivers over a long play session.
//
// Deliberately NOT libogc's __lwp_heap_* -- that API is designed around
// lwp_heap_init() unconditionally installing its own thread-safety lock
// (LWP_MutexLock) and object bookkeeping sized for general-purpose IPC use,
// more machinery than a single-thread, alloc/free-only texture heap needs.
// A hand-rolled free-list is ~120 lines, has no hidden locking cost, and is
// trivially auditable against the "main-thread-only, no locking" contract
// this header documents.
#ifdef FRUIT_PLATFORM_WII

#include "platform/wii/Mem2Alloc.h"

#include <gccore.h>
#include <cstdint>
#include <cstring>

#include "debug/Logger.h"

namespace {

// Headroom left un-carved below SYS_GetArena2Hi(), in case libogc/IOS wants
// MEM2 for something else (e.g. USB Gecko / EXI over MEM2 on some setups).
const u32 kHeadroomBytes = 512 * 1024;

// Minimum split remainder: a free block smaller than this is left attached
// to the allocation rather than split off as its own (tiny, likely
// never-reusable) free node.
const u32 kMinSplit = 64;

struct FreeBlock {
    FreeBlock* next;
    u32        size;   // total bytes of this block, including this header
};

u8*        s_ArenaBase = NULL;
u32        s_ArenaSize = 0;
FreeBlock* s_FreeList   = NULL;   // address-ordered

inline u32 AlignUp(u32 v, u32 align) {
    return (v + (align - 1)) & ~(align - 1);
}

} // namespace

void Wii_MEM2Init() {
    if (s_ArenaBase) return;   // already initialised

    u8* lo = (u8*)SYS_GetArena2Lo();
    u8* hi = (u8*)SYS_GetArena2Hi();
    if (hi <= lo || (u32)(hi - lo) <= kHeadroomBytes) {
        LOG_ERROR("Mem2Alloc", "Wii_MEM2Init: MEM2 arena too small (lo=%p hi=%p)",
                  (void*)lo, (void*)hi);
        return;
    }

    u32 carveSize = (u32)(hi - lo) - kHeadroomBytes;
    s_ArenaBase = lo;
    s_ArenaSize = carveSize;

    // Claim the carved region so libogc doesn't hand it out again.
    SYS_SetArena2Lo(lo + carveSize);

    FreeBlock* head = (FreeBlock*)s_ArenaBase;
    head->next = NULL;
    head->size = s_ArenaSize;
    s_FreeList = head;

    LOG_INFO("Mem2Alloc", "Wii_MEM2Init: carved %u KB at %p (headroom %u KB below Hi=%p)",
             (unsigned)(carveSize / 1024), (void*)s_ArenaBase,
             (unsigned)(kHeadroomBytes / 1024), (void*)hi);
}

void* Wii_MEM2Alloc(u32 size, u32 align) {
    if (!s_ArenaBase || size == 0) return NULL;
    if (align < 32) align = 32;   // GX texture / DSP minimum

    // Each live allocation is prefixed with its own block header (size +
    // padding-from-header-to-payload) so Free() can find it again without a
    // separate side table. Worst-case padding for alignment is (align - 1).
    const u32 hdrSize = sizeof(u32) * 2;   // {blockSize, payloadOffset}
    u32 need = hdrSize + align - 1 + size;

    FreeBlock** prevNext = &s_FreeList;
    FreeBlock* blk = s_FreeList;
    while (blk) {
        if (blk->size >= need) break;
        prevNext = &blk->next;
        blk = blk->next;
    }
    if (!blk) {
        LOG_WARN("Mem2Alloc", "Wii_MEM2Alloc: out of memory (want %u, need %u incl. hdr/align)",
                 (unsigned)size, (unsigned)need);
        return NULL;
    }

    // Unlink from the free list.
    *prevNext = blk->next;

    u32 blkSize = blk->size;
    u8* blkAddr = (u8*)blk;

    // Split off the remainder if it's big enough to be a useful free block.
    if (blkSize - need >= kMinSplit) {
        FreeBlock* rem = (FreeBlock*)(blkAddr + need);
        rem->size = blkSize - need;
        // Re-insert `rem` in address order.
        FreeBlock** ip = &s_FreeList;
        while (*ip && (u8*)*ip < (u8*)rem) ip = &(*ip)->next;
        rem->next = *ip;
        *ip = rem;
        blkSize = need;
    }

    // Compute the aligned payload address within [blkAddr+hdrSize, blkAddr+blkSize).
    u8* payload = (u8*)AlignUp((u32)(blkAddr + hdrSize), align);

    // Stash {blockSize, offset-from-blkAddr-to-payload} immediately before
    // the payload so Free() can recover both without a side table.
    u32* tag = (u32*)(payload - hdrSize);
    tag[0] = blkSize;
    tag[1] = (u32)(payload - blkAddr);

    return payload;
}

void Wii_MEM2Free(void* p) {
    if (!p || !s_ArenaBase) return;

    u32* tag = (u32*)((u8*)p - sizeof(u32) * 2);
    u32 blkSize = tag[0];
    u32 payloadOff = tag[1];
    u8* blkAddr = (u8*)p - payloadOff;

    FreeBlock* fb = (FreeBlock*)blkAddr;
    fb->size = blkSize;

    // Re-insert in address order, coalescing with adjacent neighbours.
    FreeBlock** ip = &s_FreeList;
    while (*ip && (u8*)*ip < (u8*)fb) ip = &(*ip)->next;
    FreeBlock* nextBlk = *ip;

    fb->next = nextBlk;
    *ip = fb;

    // Coalesce with next if adjacent.
    if (nextBlk && (u8*)fb + fb->size == (u8*)nextBlk) {
        fb->size += nextBlk->size;
        fb->next = nextBlk->next;
    }

    // Coalesce with prev if adjacent (prev is whatever pointed at fb before
    // the insert -- walk again since `ip` may now be stale after the merge
    // above; cheap for this allocator's small free-list depth).
    FreeBlock* prev = NULL;
    FreeBlock* cur = s_FreeList;
    while (cur && cur != fb) { prev = cur; cur = cur->next; }
    if (prev && (u8*)prev + prev->size == (u8*)fb) {
        prev->size += fb->size;
        prev->next = fb->next;
    }
}

u32 Wii_MEM2FreeBytes() {
    u32 total = 0;
    for (FreeBlock* b = s_FreeList; b; b = b->next) total += b->size;
    return total;
}

#endif // FRUIT_PLATFORM_WII
