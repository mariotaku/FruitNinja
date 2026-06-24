// Pure-logic unit test for fn::FixedStepDriver.
// No SDL, no GL, no audio -- compiles against the header only.
// Covers: steady 60/144/30 Hz streams, spike clamping, alpha bounds.

#include "platform/FixedStepDriver.h"
#include <cstdio>
#include <cmath>
#include <cassert>

static int failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); ++failures; } \
         else { fprintf(stdout, "PASS: %s\n", msg); } } while(0)

int main() {
    // (a) 60 Hz steady: ~16.667ms per tick for 60 ticks -> exactly 60 steps
    {
        fn::FixedStepDriver d;
        int total = 0;
        const double tick = 1000.0 / 60.0;
        for (int i = 0; i < 60; ++i) {
            total += d.advance(tick);
        }
        CHECK(total == 60, "(a) 60 steady ticks at 16.667ms -> 60 steps");
    }

    // (b) 144 Hz: ~6.944ms per tick for ~144 frames over 1s -> ~60 steps total
    {
        fn::FixedStepDriver d;
        int total = 0;
        const double tick = 1000.0 / 144.0;
        // 144 frames at 144 Hz = 1 second
        for (int i = 0; i < 144; ++i) {
            total += d.advance(tick);
        }
        // Should be 60 steps (within rounding: 59 or 60)
        CHECK(total >= 59 && total <= 61,
              "(b) 144Hz (6.94ms) over 1s => ~60 steps");
    }

    // (c) 30 fps: ~33.333ms per frame for 30 frames -> ~60 steps (2 per frame)
    {
        fn::FixedStepDriver d;
        int total = 0;
        const double tick = 1000.0 / 30.0;
        for (int i = 0; i < 30; ++i) {
            total += d.advance(tick);
        }
        CHECK(total == 60, "(c) 30fps (33.3ms) over 1s => 60 steps (2/frame)");
    }

    // (d) Single 500ms spike -> cap engages, at most maxSteps=5 returned
    // FP note: cap = 5*(1000/60) is computed as a double; draining in a loop
    // of -= frameMs can leave a sub-frameMs residual so the result is 4 or 5
    // (both are correct -- the invariant is "no more than maxSteps", not
    // "always exactly maxSteps when capped").
    {
        fn::FixedStepDriver d;
        int steps = d.advance(500.0);
        CHECK(steps >= 4 && steps <= 5, "(d) 500ms spike clamps to maxSteps (4 or 5, cap engaged)");
        double a = d.alpha();
        CHECK(a >= 0.0 && a < 1.0, "(d) alpha in [0,1) after spike");
    }

    // (e) alpha() is always in [0, 1) across a steady stream
    {
        fn::FixedStepDriver d;
        const double tick = 1000.0 / 60.0;
        bool alphaOk = true;
        for (int i = 0; i < 120; ++i) {
            d.advance(tick);
            double a = d.alpha();
            if (a < 0.0 || a >= 1.0) { alphaOk = false; break; }
        }
        CHECK(alphaOk, "(e) alpha() in [0,1) for 120 steady ticks");
    }

    // (f) Zero elapsed -> 0 steps, accumulator unchanged
    {
        fn::FixedStepDriver d;
        int steps = d.advance(0.0);
        CHECK(steps == 0, "(f) zero elapsed -> 0 steps");
        CHECK(d.alpha() == 0.0, "(f) zero elapsed -> alpha == 0");
    }

    // (g) Negative elapsed -> treated as 0 (no underflow)
    {
        fn::FixedStepDriver d;
        int steps = d.advance(-100.0);
        CHECK(steps == 0, "(g) negative elapsed -> 0 steps");
    }

    if (failures == 0) {
        fprintf(stdout, "All fixedstep tests passed.\n");
        return 0;
    }
    fprintf(stderr, "%d fixedstep test(s) FAILED.\n", failures);
    return 1;
}
