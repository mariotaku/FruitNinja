// Mortar::Delegate basic correctness + size-fidelity test.
//
// Binary spec: docs/engine/delegate-system.md
//   Total size 36 bytes / 0x24 (uniform across all signatures).

#include "engine/util/Delegate.h"

#include <cstdio>
#include <cstdlib>

namespace {

int g_freeFnCalls = 0;
int g_freeFnSum = 0;

void FreeAdd(int a, int b) { g_freeFnCalls++; g_freeFnSum += a + b; }

class Counter {
public:
    int hits = 0;
    int payload = 0;

    void Bump() { hits++; }
    void Bump1(int n) { hits++; payload = n; }
    int  Sum(int a, int b) { hits++; return a + b; }
};

#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL: %s\n", #cond); ::exit(1); } } while(0)

} // namespace

int main() {
    using D0 = Mortar::Delegate<void()>;
    using D2 = Mortar::Delegate<void(int, int)>;

    // Signature uniformity (per docs/engine/delegate-system.md).
    CHECK(sizeof(D0) == sizeof(Mortar::Delegate<bool(float)>));
    CHECK(sizeof(D0) == sizeof(Mortar::Delegate<int(int, int, int, int)>));

    // Empty-state behaviour: silently no-op.
    D0 empty;
    CHECK(!empty);
    empty();  // must not crash

    // Free-function bind.
    g_freeFnCalls = 0;
    g_freeFnSum = 0;
    D2 free_d = D2::MakeFree(&FreeAdd);
    CHECK((bool)free_d);
    free_d(2, 3);
    CHECK(g_freeFnCalls == 1 && g_freeFnSum == 5);

    // Member-function bind via Make().
    Counter c;
    D0 mem_d = D0::Make(&c, &Counter::Bump);
    mem_d(); mem_d();
    CHECK(c.hits == 2);

    // Non-void return + multi-arg member (uses a separate counter so we
    // don't conflate hit-counts with the copy/move test below).
    Counter sumCounter;
    Mortar::Delegate<int(int, int)> sum_d =
        Mortar::Delegate<int(int, int)>::Make(&sumCounter, &Counter::Sum);
    CHECK(sum_d(7, 8) == 15);
    CHECK(sumCounter.hits == 1);

    // Lambda capture.
    int captured = 0;
    Mortar::Delegate<void(int)> lam_d = [&captured](int n) { captured += n; };
    lam_d(10); lam_d(20);
    CHECK(captured == 30);

    // Copy + move semantics preserve binding.
    D0 mem_d_copy = mem_d;
    mem_d_copy();
    CHECK(c.hits == 3);
    D0 mem_d_moved = std::move(mem_d_copy);
    mem_d_moved();
    CHECK(c.hits == 4);

    // Reset to empty.
    mem_d = nullptr;
    CHECK(!mem_d);
    mem_d();  // no-op, must not crash

    // Reassignment.
    Counter c2;
    mem_d = D0::Make(&c2, &Counter::Bump);
    mem_d();
    CHECK(c2.hits == 1);

    // Noop factory.
    D0 noop = D0::Noop();
    CHECK(!noop);

    std::printf("all delegate tests passed (sizeof=%zu)\n", sizeof(D0));
    return 0;
}
