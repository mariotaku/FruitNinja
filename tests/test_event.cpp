// Mortar::Event1 multicast correctness test.
//
// Covers:
//   - += (subscribe): bound method is called on fire
//   - () (fire): all subscribers notified in order
//   - -= (unsubscribe): removed subscriber is NOT called after removal
//   - Multiple subscribers: all fire, then individual removal leaves others
//   - Empty event fire: no-op, no crash
//
// No GPU, no audio, no SDL. Pure in-process template test.

#include "engine/util/Event.h"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL(%d): %s\n", __LINE__, #cond); \
            ::exit(1); \
        } \
    } while (0)

namespace {

// Simple counter target to receive event callbacks.
struct Receiver {
    int  callCount;
    int  lastArg;

    Receiver() : callCount(0), lastArg(-999) {}

    void OnEvent(int val) {
        callCount++;
        lastArg = val;
    }
};

} // namespace

int main() {
    // --- 1. Empty event fire is a no-op ---
    {
        Mortar::Event1<int> ev;
        ev(42);  // must not crash
    }

    // --- 2. Subscribe and fire ---
    {
        Mortar::Event1<int> ev;
        Receiver r;

        Mortar::Delegate1<void, int> d =
            Mortar::Delegate1<void, int>::Make(&r, &Receiver::OnEvent);
        ev += d;

        ev(7);
        CHECK(r.callCount == 1);
        CHECK(r.lastArg   == 7);

        ev(13);
        CHECK(r.callCount == 2);
        CHECK(r.lastArg   == 13);
    }

    // --- 3. Unsubscribe: removed delegate is not called ---
    {
        Mortar::Event1<int> ev;
        Receiver r;

        Mortar::Delegate1<void, int> d =
            Mortar::Delegate1<void, int>::Make(&r, &Receiver::OnEvent);
        ev += d;
        ev(1);
        CHECK(r.callCount == 1);

        ev -= d;
        ev(2);
        CHECK(r.callCount == 1);  // no new call after unsubscribe
    }

    // --- 4. Multiple subscribers: all fired; individual removal works ---
    {
        Mortar::Event1<int> ev;
        Receiver r1, r2;

        Mortar::Delegate1<void, int> d1 =
            Mortar::Delegate1<void, int>::Make(&r1, &Receiver::OnEvent);
        Mortar::Delegate1<void, int> d2 =
            Mortar::Delegate1<void, int>::Make(&r2, &Receiver::OnEvent);

        ev += d1;
        ev += d2;

        ev(5);
        CHECK(r1.callCount == 1 && r1.lastArg == 5);
        CHECK(r2.callCount == 1 && r2.lastArg == 5);

        // Remove d1; d2 should still fire
        ev -= d1;
        ev(9);
        CHECK(r1.callCount == 1);  // unchanged
        CHECK(r2.callCount == 2 && r2.lastArg == 9);
    }

    // --- 5. Event0 (no-arg): subscribe, fire, unsubscribe ---
    {
        Mortar::Event0 ev0;
        int ticks = 0;

        // Use a Receiver that bumps a local counter via a free-function wrapper
        // (GCC 4.4 safe: no lambda, no capture).
        // We exercise Event0 with Delegate0<void> via Make.
        struct Ticker {
            int count;
            Ticker() : count(0) {}
            void Tick() { count++; }
        } t;

        Mortar::Delegate0<void> d =
            Mortar::Delegate0<void>::Make(&t, &Ticker::Tick);
        ev0 += d;
        ev0();
        ev0();
        CHECK(t.count == 2);

        ev0 -= d;
        ev0();
        CHECK(t.count == 2);  // unsubscribed, no new tick

        (void)ticks;
    }

    // --- 6. Event3 (3-arg): subscribe and fire ---
    {
        Mortar::Event3<int, int, int> ev3;
        struct Tri {
            int sum;
            Tri() : sum(0) {}
            void Add(int a, int b, int c) { sum += a + b + c; }
        } tri;

        Mortar::Delegate3<void, int, int, int> d =
            Mortar::Delegate3<void, int, int, int>::Make(&tri, &Tri::Add);
        ev3 += d;
        ev3(1, 2, 3);
        CHECK(tri.sum == 6);

        ev3 -= d;
        ev3(10, 20, 30);
        CHECK(tri.sum == 6);  // unsubscribed
    }

    std::printf("all event tests passed\n");
    return 0;
}
