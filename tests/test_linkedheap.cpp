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
    unsigned int two_blocks_min = (64 + 0x10) * 2;
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
    // Used includes headers (0x10 each) + payloads.
    CHECK(used >= total_payload + N * 0x10);

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

    std::printf("test_linkedheap: ALL PASS\n");
    return 0;
}
