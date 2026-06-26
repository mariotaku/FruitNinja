// MemoryPool<T> unit test.
//
// Exercises the fixed-capacity free-list pool used by SliceEffect::Node and
// particle subsystems. Assertions cover the full lifecycle contract:
//   Create -> Pop (N times) -> Pop-empty -> Push -> Reset -> Destroy.
//
// A static live-counter on the element type proves Create runs T ctors and
// Destroy (via ~MemoryPool / dtor) runs T dtors.
//
// LIFO contract (verified from header): Pop does --m_FreeCount then returns
// m_FreeList[m_FreeCount], so the last-filled slot is the first returned.
// Push appends at m_FreeList[m_FreeCount] then increments -- mirror LIFO push.
// After Create(N) the free list is [&Backing[0], &Backing[1], ..., &Backing[N-1]].
// The first Pop therefore returns &Backing[N-1], the second &Backing[N-2], etc.
//
// Pure in-process: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "util/MemoryPool.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// ---------------------------------------------------------------------------
// Element type with ctor/dtor instrumentation.
// ---------------------------------------------------------------------------

static int g_LiveCount = 0;

struct Elem {
    int value;
    Elem()  : value(0) { ++g_LiveCount; }
    ~Elem()             { --g_LiveCount; }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void test_create_counts()
{
    g_LiveCount = 0;
    {
        Mortar::MemoryPool<Elem> pool;
        pool.Create(5);

        // Create must have default-constructed 5 Elem objects.
        CHECK(g_LiveCount == 5);

        CHECK(pool.Capacity()   == 5);
        CHECK(pool.FreeCount()  == 5);
        CHECK(pool.InUseCount() == 0);
    }
    // Dtor calls Destroy() which calls delete[] -> 5 Elem dtors.
    CHECK(g_LiveCount == 0);
}

static void test_pop_distinct_nonnull()
{
    Mortar::MemoryPool<Elem> pool;
    pool.Create(5);

    Elem* slots[5];
    for (int i = 0; i < 5; ++i) {
        slots[i] = pool.Pop();
        CHECK(slots[i] != 0);
        CHECK(pool.FreeCount()  == 5 - (i + 1));
        CHECK(pool.InUseCount() == i + 1);
    }

    // All pointers must be distinct.
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            CHECK(slots[i] != slots[j]);
        }
    }

    // All pointers must lie inside the backing array.
    for (int i = 0; i < 5; ++i) {
        CHECK(pool.SlotAt(0) != 0);
        bool found = false;
        for (int k = 0; k < 5; ++k) {
            if (pool.SlotAt(k) == slots[i]) {
                found = true;
                break;
            }
        }
        CHECK(found);
    }
}

static void test_pop_empty_returns_null()
{
    Mortar::MemoryPool<Elem> pool;
    pool.Create(3);

    pool.Pop();
    pool.Pop();
    pool.Pop();

    CHECK(pool.FreeCount() == 0);
    // Pool exhausted: next Pop must return nullptr.
    Elem* over = pool.Pop();
    CHECK(over == 0);
    // Counts must be unchanged after a failed Pop.
    CHECK(pool.FreeCount()  == 0);
    CHECK(pool.InUseCount() == 3);
}

static void test_push_returns_to_freelist()
{
    Mortar::MemoryPool<Elem> pool;
    pool.Create(3);

    Elem* a = pool.Pop();
    Elem* b = pool.Pop();
    CHECK(pool.FreeCount() == 1);

    pool.Push(b);
    CHECK(pool.FreeCount()  == 2);
    CHECK(pool.InUseCount() == 1);

    // LIFO: the slot most recently Pushed is the one Pop returns next.
    Elem* c = pool.Pop();
    CHECK(c == b);
    CHECK(pool.FreeCount() == 1);

    (void)a;
}

static void test_lifo_order()
{
    // Verify LIFO ordering for a full create-pop-push-pop round-trip.
    // Create(4): free list filled [&Backing[0], [1], [2], [3]].
    // Pop order: [3], [2], [1], [0]  (LIFO: last in = first out).
    Mortar::MemoryPool<Elem> pool;
    pool.Create(4);

    Elem* base = pool.SlotAt(0);

    Elem* p0 = pool.Pop();  // &Backing[3]
    Elem* p1 = pool.Pop();  // &Backing[2]
    Elem* p2 = pool.Pop();  // &Backing[1]
    Elem* p3 = pool.Pop();  // &Backing[0]

    CHECK(p0 == base + 3);
    CHECK(p1 == base + 2);
    CHECK(p2 == base + 1);
    CHECK(p3 == base + 0);

    // Push p2 (&Backing[1]) -- next Pop should return it.
    pool.Push(p2);
    Elem* reused = pool.Pop();
    CHECK(reused == p2);
}

static void test_reset()
{
    Mortar::MemoryPool<Elem> pool;
    pool.Create(5);

    // Exhaust pool.
    for (int i = 0; i < 5; ++i) {
        pool.Pop();
    }
    CHECK(pool.FreeCount() == 0);

    // Reset restores all slots to free without calling ctors/dtors.
    int liveBefore = g_LiveCount;
    pool.Reset();
    CHECK(g_LiveCount == liveBefore);  // no ctor/dtor calls during Reset

    CHECK(pool.FreeCount()  == 5);
    CHECK(pool.InUseCount() == 0);
    CHECK(pool.Capacity()   == 5);

    // Can Pop again after Reset.
    Elem* s = pool.Pop();
    CHECK(s != 0);
    CHECK(pool.FreeCount() == 4);
}

static void test_destroy_runs_dtors()
{
    g_LiveCount = 0;
    Mortar::MemoryPool<Elem> pool;
    pool.Create(7);
    CHECK(g_LiveCount == 7);

    // Pop a few so not all slots are in the free list.
    pool.Pop();
    pool.Pop();
    pool.Pop();

    // Destroy must run all 7 T dtors regardless of in-use state.
    pool.Destroy();
    CHECK(g_LiveCount == 0);
    CHECK(pool.Capacity()  == 0);
    CHECK(pool.FreeCount() == 0);

    // Second Destroy must be a safe no-op (nullptr guard).
    pool.Destroy();
    CHECK(g_LiveCount == 0);
}

static void test_create_zero_is_noop()
{
    Mortar::MemoryPool<Elem> pool;
    pool.Create(0);
    CHECK(pool.Capacity()  == 0);
    CHECK(pool.FreeCount() == 0);
    Elem* s = pool.Pop();
    CHECK(s == 0);
}

int main()
{
    std::printf("test_memorypool: start\n");

    test_create_counts();
    std::printf("  create_counts: OK\n");

    test_pop_distinct_nonnull();
    std::printf("  pop_distinct_nonnull: OK\n");

    test_pop_empty_returns_null();
    std::printf("  pop_empty_returns_null: OK\n");

    test_push_returns_to_freelist();
    std::printf("  push_returns_to_freelist: OK\n");

    test_lifo_order();
    std::printf("  lifo_order: OK\n");

    test_reset();
    std::printf("  reset: OK\n");

    test_destroy_runs_dtors();
    std::printf("  destroy_runs_dtors: OK\n");

    test_create_zero_is_noop();
    std::printf("  create_zero_is_noop: OK\n");

    std::printf("test_memorypool: PASS\n");
    return 0;
}
