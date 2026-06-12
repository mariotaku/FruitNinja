// LinkedHeap correctness test.
//
// Exercises the two crash-path scenarios fixed in the 2026-06-12 pass:
//   A) Tail-rewind: freeing the last block (and then the next-to-last)
//      must rewind m_StartAddr by subtracting each block's size.
//      Binary: Release @ 0x001946dc-0x0019471e.
//   B) Forward-merge: freeing a middle block whose NEXT neighbor is
//      already free must absorb the neighbor.
//      Binary: Release @ 0x00194720-0x00194758.
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "engine/util/LinkedHeap.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// Heap buffer large enough for all subtests.
static const unsigned int HEAP_SIZE = 4096;

static void test_basic_alloc_free()
{
    Mortar::LinkedHeap heap(HEAP_SIZE);

    // Allocate three blocks of different sizes.
    void* a = heap.Allocate(64, "a");
    void* b = heap.Allocate(128, "b");
    void* c = heap.Allocate(256, "c");
    CHECK(a != 0);
    CHECK(b != 0);
    CHECK(c != 0);
    CHECK(a != b && b != c && a != c);

    // Used bytes = three block headers + payloads (each aligned to 4, header = 0x10).
    unsigned int used_before = heap.GetSizeOfUsedBlocks();
    // At least the payload data is used.
    CHECK(used_before >= 64 + 128 + 256);

    // Free all -- used blocks should return to 0.
    heap.Release(a, true);
    heap.Release(b, true);
    heap.Release(c, true);
    unsigned int used_after = heap.GetSizeOfUsedBlocks();
    CHECK(used_after == 0);
}

static void test_tail_rewind_two_blocks()
{
    // This is the exact sequence that crashed: allocate two blocks,
    // free the MOST recently allocated one first, then the next-newest.
    // Both frees trigger the tail-rewind path.
    // After both frees the bump pointer must be at the buffer start
    // (all blocks gone), so a fresh allocation of the full-size block
    // must succeed.
    Mortar::LinkedHeap heap(HEAP_SIZE);

    void* a = heap.Allocate(100, "tail-a");
    void* b = heap.Allocate(200, "tail-b");
    CHECK(a != 0 && b != 0);

    unsigned int used_both = heap.GetSizeOfUsedBlocks();
    CHECK(used_both >= 100 + 200);

    // Free b first (last block) -- triggers tail-rewind for b.
    heap.Release(b, true);
    unsigned int used_a_only = heap.GetSizeOfUsedBlocks();
    CHECK(used_a_only < used_both);
    CHECK(used_a_only >= 100);

    // Free a (now last block) -- triggers tail-rewind for a.
    heap.Release(a, true);
    unsigned int used_none = heap.GetSizeOfUsedBlocks();
    CHECK(used_none == 0);

    // Bump pointer rewound fully: a large re-allocation must succeed.
    void* big = heap.Allocate(HEAP_SIZE - 0x20, "big");
    CHECK(big != 0);
}

static void test_forward_merge()
{
    // Allocate three blocks: left, mid, right.
    // Free right first (tail-rewind -- removes right entirely).
    // Then free left (goes onto free-list as non-tail since mid is still live).
    // Then free mid -- mid is now the tail, but left is its prev-neighbor on
    // the free-list. Release(mid) should trigger tail-rewind for mid+left.
    //
    // Alternative forward-merge path: allocate four blocks A,B,C,D.
    // Free C (onto free-list, not tail). Free B (not tail, neighbor C is free
    // => forward-merge absorbs C). After merge the B-region is one big free
    // block. Then free D (tail-rewind) and A (tail-rewind). End: used=0.
    Mortar::LinkedHeap heap(HEAP_SIZE);

    void* pa = heap.Allocate(64, "fwd-a");
    void* pb = heap.Allocate(64, "fwd-b");
    void* pc = heap.Allocate(64, "fwd-c");
    void* pd = heap.Allocate(64, "fwd-d");
    CHECK(pa != 0 && pb != 0 && pc != 0 && pd != 0);

    // Free C -- goes onto free-list; C is not the tail (D is tail).
    heap.Release(pc, true);
    // Free B -- B is not the tail, but its forward neighbor C is free.
    // Forward-merge must absorb C into B.
    heap.Release(pb, true);

    // Largest free block must be at least B+C combined (2*64 + 2 headers).
    unsigned int largest = heap.GetLargestFreeBlock();
    unsigned int two_blocks_min = (unsigned int)((64 + sizeof(Mortar::LinkedHeap::Block)) * 2);
    CHECK(largest >= two_blocks_min);

    // Free D (tail-rewind), then A (tail-rewind after merge winds back).
    heap.Release(pd, true);
    heap.Release(pa, true);
    CHECK(heap.GetSizeOfUsedBlocks() == 0);

    // Re-allocate after full rewind -- must succeed.
    void* reuse = heap.Allocate(128, "reuse");
    CHECK(reuse != 0);
}

static void test_freelist_reuse()
{
    // Allocate A and B. Free A (A is non-tail; goes onto free-list).
    // Allocate C with size <= A's payload -- must get recycled from free-list.
    // Then free B (tail-rewind) and C (tail-rewind or free-list).
    Mortar::LinkedHeap heap(HEAP_SIZE);

    void* pa = heap.Allocate(128, "reuse-a");
    void* pb = heap.Allocate(128, "reuse-b");
    CHECK(pa != 0 && pb != 0);

    heap.Release(pa, true);  // A goes onto free-list

    // Allocate something smaller that fits in A's slot.
    void* pc = heap.Allocate(64, "reuse-c");
    CHECK(pc != 0);
    // pc must recycle a's region (address should be within old a's range).
    // (We don't know the exact address, just that alloc succeeded.)

    heap.Release(pb, true);
    heap.Release(pc, true);
    CHECK(heap.GetSizeOfUsedBlocks() == 0);
}

static void test_used_bytes_accounting()
{
    // Allocate many blocks, verify accounting throughout.
    Mortar::LinkedHeap heap(HEAP_SIZE);

    static const int N = 8;
    static const unsigned int sizes[N] = {16, 32, 48, 64, 80, 96, 112, 128};
    void* ptrs[N];
    int i;

    for (i = 0; i < N; ++i) {
        ptrs[i] = heap.Allocate(sizes[i], "acct");
        CHECK(ptrs[i] != 0);
    }

    unsigned int used = heap.GetSizeOfUsedBlocks();
    unsigned int total_payload = 0;
    for (i = 0; i < N; ++i) {
        total_payload += (sizes[i] + 3u) & ~3u;
    }
    // Used includes headers (sizeof(Block) each) + payloads.
    CHECK(used >= total_payload + N * (unsigned int)sizeof(Mortar::LinkedHeap::Block));

    // Free all in reverse order (tail-rewind each time).
    for (i = N - 1; i >= 0; --i) {
        heap.Release(ptrs[i], true);
    }
    CHECK(heap.GetSizeOfUsedBlocks() == 0);
}

// Reproduce the v1.6.1 shutdown crash: ActorManager::Clear drains the heap
// by freeing all N live entities in ALLOCATION ORDER (head-first, not tail-first).
// Each free hits the non-tail forward-merge path. After all frees, the bump
// pointer must have rewound to buffer start (no leaked regions, no crash).
static void test_drain_alloc_order()
{
    static const int N    = 8;
    static const unsigned int BLOCK_SZ = 64;
    Mortar::LinkedHeap heap(4096);

    void* ptrs[N];
    int i;
    for (i = 0; i < N; ++i) {
        ptrs[i] = heap.Allocate(BLOCK_SZ, "drain");
        CHECK(ptrs[i] != 0);
    }

    // Free in allocation order (head-first). First 7 frees hit the non-tail
    // forward-merge path; the last free hits tail-rewind on the merged region.
    for (i = 0; i < N; ++i) {
        heap.Release(ptrs[i], true);
    }

    CHECK(heap.GetSizeOfUsedBlocks() == 0);
    // After full drain the bump pointer must have rewound so a large alloc works.
    void* big = heap.Allocate(3800, "refill");
    CHECK(big != 0);
}

// Walk the all-blocks list and assert structural integrity:
//   - Every prev/next pointer is either null or within [buffer, buffer+size).
//   - Each forward link's back-link points back to us.
// Bounds the walk at max_blocks to catch infinite loops on corruption.
static void check_list_integrity(Mortar::LinkedHeap& heap, int max_blocks = 4096)
{
    uint8_t* buf_lo = heap.TestGetBuffer();
    uint8_t* buf_hi = buf_lo + heap.TestGetSize();

    Mortar::LinkedHeap::Block* cur = heap.TestGetFirstBlock();
    int count = 0;
    while (cur) {
        ++count;
        CHECK(count <= max_blocks);  // detects cycle / corruption

        uint8_t* p = reinterpret_cast<uint8_t*>(cur);
        CHECK(p >= buf_lo && p < buf_hi);

        if (cur->prev) {
            uint8_t* pp = reinterpret_cast<uint8_t*>(cur->prev);
            CHECK(pp >= buf_lo && pp < buf_hi);
            // back-link from prev must point to cur
            CHECK(cur->prev->next == cur);
        }
        if (cur->next) {
            uint8_t* np = reinterpret_cast<uint8_t*>(cur->next);
            CHECK(np >= buf_lo && np < buf_hi);
            // forward-link's back-link must point to cur
            CHECK(cur->next->prev == cur);
        }

        cur = cur->next;
    }
}

// Reproduce the v1.6.1 shutdown crash: ActorManager::Clear drains a split /
// fragmented heap in allocation order. The bug was that FreeListSearch's SPLIT
// path wrote blk->name (struct +0x10 on ARM32) and blk->sizeFlags (struct +0x0C)
// into addresses that, on x64 with sizeof(Block)==0x20, landed in the payload or
// the NEXT block's prev/next slots -- corrupting pointers that Release() then
// dereferenced as Block*.
//
// This test forces a split explicitly and validates the all-blocks list after
// every mutating operation so the corruption is caught at the split, not only
// at shutdown.
static void test_split_path()
{
    // Use a heap large enough for: big(2000) + guard(64) + small(64) + fragments.
    Mortar::LinkedHeap heap(8192);

    // Allocate a large block and a "guard" block behind it so that when we
    // free big, it is NOT the tail (guard is), forcing big onto the free-list
    // rather than triggering tail-rewind.
    void* big   = heap.Allocate(2000, "big");
    void* guard = heap.Allocate(64,   "guard");
    CHECK(big   != 0);
    CHECK(guard != 0);

    // Free big -- big goes onto the free-list (not tail, guard is tail).
    heap.Release(big, true);
    check_list_integrity(heap);

    // Allocate small (64 bytes) -- this MUST hit big's freed region via
    // FreeListSearch. Because 2000 >> 64, a SPLIT must occur.
    // On unfixed code this split writes sizeFlags at struct+0x18 (x64), which
    // stomps guard->prev, corrupting the all-blocks list.
    void* small = heap.Allocate(64, "small");
    CHECK(small != 0);
    check_list_integrity(heap);  // FAILS on unfixed code -- guard->prev clobbered

    // Fragment the remainder further.
    void* f1 = heap.Allocate(32,  "f1");
    check_list_integrity(heap);
    void* f2 = heap.Allocate(200, "f2");
    check_list_integrity(heap);
    void* f3 = heap.Allocate(16,  "f3");
    check_list_integrity(heap);

    // Release everything in allocation order (head-first); mirrors ActorManager::Clear.
    heap.Release(small, true);
    check_list_integrity(heap);
    heap.Release(guard, true);
    check_list_integrity(heap);
    heap.Release(f1, true);
    check_list_integrity(heap);
    heap.Release(f2, true);
    check_list_integrity(heap);
    heap.Release(f3, true);
    check_list_integrity(heap);

    CHECK(heap.GetSizeOfUsedBlocks() == 0);
}

// Same drain but in interleaved order to exercise the prev-walk anchor logic.
static void test_drain_interleaved()
{
    static const int N    = 8;
    static const unsigned int BLOCK_SZ = 64;
    Mortar::LinkedHeap heap(4096);

    void* ptrs[N];
    int i;
    for (i = 0; i < N; ++i) {
        ptrs[i] = heap.Allocate(BLOCK_SZ, "drain2");
        CHECK(ptrs[i] != 0);
    }

    // Interleaved order: p[3],p[0],p[7],p[1],p[5],p[2],p[6],p[4]
    static const int order[N] = { 3, 0, 7, 1, 5, 2, 6, 4 };
    for (i = 0; i < N; ++i) {
        heap.Release(ptrs[order[i]], true);
    }

    CHECK(heap.GetSizeOfUsedBlocks() == 0);
    void* big = heap.Allocate(3800, "refill2");
    CHECK(big != 0);
}

// Verify that a split whose remainder payload would be smaller than sizeof(void*)
// (the free-list link width) is suppressed -- the whole block is given to the
// caller rather than creating an un-linkable sub-minimum fragment.
//
// Construction:
//   We need remainder in the range (kHeaderSize, kHeaderSize + sizeof(void*)),
//   i.e. remainder payload in [1, sizeof(void*)-1].  Use remainder = kHeaderSize + 4
//   (payload = 4 < 8) which on x64 satisfies: 4 > 0 so the old "remainder > kHeaderSize"
//   guard passes and the split fires; 4 < sizeof(void*)=8 so SetFreeNext overruns.
//
//   free block total = need + kHeaderSize + 4
//   need = kHeaderSize + aligned_payload_of_request
//
//   Choose total = 128 (fixed).  Then:
//     need = 128 - (kHeaderSize + 4) = 92  => aligned_payload = 92 - kHeaderSize = 60
//   => request Allocate(60) from a free block of 128.
//
//   On ARM32: kHeaderSize=16, sizeof(void*)=4.  remainder=kHeaderSize+4=20.
//   remainder > kHeaderSize (20>16) TRUE, payload=4==sizeof(void*) so the link
//   write is exactly 4 bytes -- no overrun.  The fix makes this platform-safe by
//   raising the threshold to kHeaderSize + sizeof(void*).
// Verify that a split whose remainder payload would be smaller than sizeof(void*)
// (the free-list link width) is suppressed -- the whole block is given to the
// caller rather than creating an un-linkable sub-minimum fragment.
//
// Construction:
//   We need remainder R such that:
//     R > kHeaderSize   (the existing guard passes -- split would fire)
//     R < kHeaderSize + sizeof(void*)  (payload < link width -- overrun occurs)
//   Use R = kHeaderSize + 4:  payload = 4 bytes; SetFreeNext writes 8 => 4-byte overrun.
//
//   Pick BLOCK_TOTAL = 128 (4-byte aligned, large enough).
//   Target is allocated with payload = BLOCK_TOTAL - kHeaderSize (block fills 128 bytes).
//   Then freed (block size = 128 stays in free-list).
//   Then Allocate(REQUEST_SZ) where REQUEST_SZ = BLOCK_TOTAL - R - kHeaderSize:
//     need = kHeaderSize + REQUEST_SZ = BLOCK_TOTAL - R
//     remainder = BLOCK_TOTAL - need = R = kHeaderSize + 4  => bug.
//
//   On ARM32: kHeaderSize=16, sizeof(void*)=4.  R=20; payload=4==sizeof(void*)
//   so the link write is exactly in-bounds -- no overrun. The fix unifies both
//   platforms by raising the threshold to kHeaderSize + sizeof(void*).
static void test_tiny_remainder_split()
{
    static const size_t H  = Mortar::LinkedHeap::kHeaderSize;
    static const size_t LS = sizeof(void*);

    // Sub-minimum payload (< sizeof(void*)) to embed in the remainder block.
    static const unsigned int SUB_PAYLOAD = 4u;
    static const unsigned int R           = (unsigned int)(H + SUB_PAYLOAD); // remainder size
    static const unsigned int BLOCK_TOTAL = 128u;  // target free-block total size
    // Payload for the large allocation that will create a BLOCK_TOTAL block:
    static const unsigned int TARGET_PAYLOAD = BLOCK_TOTAL - (unsigned int)H;
    // Payload for the re-allocation that triggers the split:
    //   need = H + REQUEST_SZ = BLOCK_TOTAL - R  => REQUEST_SZ = BLOCK_TOTAL - R - H
    static const unsigned int REQUEST_SZ  = BLOCK_TOTAL - R - (unsigned int)H;

    // This scenario is only unsafe when R > H (existing guard passes) AND payload < LS.
    // On ARM32 (H=16, LS=4): R=20 > 16 BUT payload=4 == LS so no overrun.
    // The test is still valid post-fix on ARM32: no split, correct behaviour.
    // Skip if arithmetic degenerates (shouldn't happen on supported platforms).
    if (R <= (unsigned int)H || REQUEST_SZ == 0 || BLOCK_TOTAL <= R + (unsigned int)H) {
        std::printf("  [tiny_remainder_split: skipped -- degenerate platform config]\n");
        return;
    }

    // Heap: anchor + target (128 bytes) + sentinel + slack.
    Mortar::LinkedHeap heap(BLOCK_TOTAL * 5 + 512);

    // Anchor before target so that when target is freed it is not the tail block.
    void* anchor = heap.Allocate(TARGET_PAYLOAD, "trs-anchor");
    CHECK(anchor != 0);

    // Target: allocate exactly BLOCK_TOTAL bytes of total block space.
    void* target = heap.Allocate(TARGET_PAYLOAD, "trs-target");
    CHECK(target != 0);

    // Sentinel immediately after target.  Its prev pointer is what the overrun stomps.
    void* sentinel = heap.Allocate(TARGET_PAYLOAD, "trs-sentinel");
    CHECK(sentinel != 0);

    // Free target -> goes onto free-list (sentinel keeps it non-tail).
    heap.Release(target, true);
    check_list_integrity(heap);

    // Allocate REQUEST_SZ from free-list.  The free block is 128 bytes.
    //   need = H + REQUEST_SZ = BLOCK_TOTAL - R
    //   remainder = R = H + 4  (payload = 4 < sizeof(void*) = 8 on x64)
    // Pre-fix: remainder > H is TRUE => split fires => SetFreeNext writes 8 bytes
    //   into 4-byte payload => sentinel->prev upper 4 bytes clobbered.
    // Post-fix: remainder < H + LS => split suppressed.
    void* small = heap.Allocate(REQUEST_SZ, "trs-small");
    CHECK(small != 0);
    check_list_integrity(heap);  // FAILS pre-fix: sentinel->prev clobbered

    unsigned int used  = heap.GetSizeOfUsedBlocks();
    unsigned int freed = heap.GetSizeOfUnusedBlocks();
    CHECK(used > 0);
    CHECK(used + freed <= heap.TestGetSize());

    heap.Release(small, true);
    check_list_integrity(heap);
    heap.Release(sentinel, true);
    check_list_integrity(heap);
    heap.Release(anchor, true);
    check_list_integrity(heap);
    CHECK(heap.GetSizeOfUsedBlocks() == 0);
}

// Verify that allocating fewer bytes than sizeof(void*) (the free-list link)
// and then freeing that block does not overrun into the physically adjacent
// block's prev pointer. Pre-fix: SetFreeNext writes 8 bytes into a 4-byte
// payload, overrunning the subsequent block. Post-fix: payload is clamped to
// at least sizeof(void*) so the write is always in-bounds.
static void test_tiny_alloc_free()
{
    // sizeof(void*) = 8 on x64; Allocate(4) yields a 4-byte payload pre-fix.
    static const unsigned int TINY = 4;
    Mortar::LinkedHeap heap(4096);

    // Allocate tiny block, then a neighbor so tiny is non-tail.
    void* tiny = heap.Allocate(TINY, "tiny");
    CHECK(tiny != 0);
    void* neighbor = heap.Allocate(64, "neighbor");
    CHECK(neighbor != 0);

    check_list_integrity(heap);

    // Free tiny: SetFreeNext writes sizeof(void*)=8 bytes into the tiny
    // payload. Pre-fix the payload is only 4 bytes -> neighbor->prev clobbered.
    heap.Release(tiny, true);
    check_list_integrity(heap);  // FAILS pre-fix

    heap.Release(neighbor, true);
    check_list_integrity(heap);
    CHECK(heap.GetSizeOfUsedBlocks() == 0);
}

int main()
{
    std::printf("test_linkedheap: running\n");

    test_basic_alloc_free();
    std::printf("  basic_alloc_free: PASS\n");

    test_tail_rewind_two_blocks();
    std::printf("  tail_rewind_two_blocks: PASS\n");

    test_forward_merge();
    std::printf("  forward_merge: PASS\n");

    test_freelist_reuse();
    std::printf("  freelist_reuse: PASS\n");

    test_used_bytes_accounting();
    std::printf("  used_bytes_accounting: PASS\n");

    test_drain_alloc_order();
    std::printf("  drain_alloc_order: PASS\n");

    test_drain_interleaved();
    std::printf("  drain_interleaved: PASS\n");

    test_split_path();
    std::printf("  split_path: PASS\n");

    test_tiny_remainder_split();
    std::printf("  tiny_remainder_split: PASS\n");

    test_tiny_alloc_free();
    std::printf("  tiny_alloc_free: PASS\n");

    std::printf("test_linkedheap: ALL PASS\n");
    return 0;
}
