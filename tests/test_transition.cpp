// test_transition.cpp -- unit tests for GROUP B easing curves (Transition.h/cpp).
// v1.6.1 @0x0014e8c4..@0x0014eaa0.
//
// Pure CPU: no GPU, no audio, no file I/O.
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "util/Transition.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

// Tolerance for float comparisons
static bool near(float a, float b, float tol = 1e-4f) {
    return fabsf(a - b) <= tol;
}

static void test_square_transition()
{
    // t*t; s ignored
    CHECK(near(SquareTransition(0.0f, 99.0f), 0.0f));
    CHECK(near(SquareTransition(0.5f, 99.0f), 0.25f));
    CHECK(near(SquareTransition(1.0f, 99.0f), 1.0f));
    CHECK(near(SquareTransition(2.0f, 99.0f), 4.0f));
}

static void test_full_transition()
{
    // Always 1.0
    CHECK(near(FullTransition(0.0f, 0.0f), 1.0f));
    CHECK(near(FullTransition(0.5f, 99.0f), 1.0f));
    CHECK(near(FullTransition(1.0f, 1.0f), 1.0f));
}

static void test_straight_transition()
{
    // clamp(t*s, 0, 1)
    CHECK(near(StraightTransition(0.5f, 1.0f), 0.5f));
    CHECK(near(StraightTransition(0.0f, 1.0f), 0.0f));
    CHECK(near(StraightTransition(1.0f, 1.0f), 1.0f));
    // Clamp at 0
    CHECK(near(StraightTransition(-1.0f, 1.0f), 0.0f));
    // Clamp at 1
    CHECK(near(StraightTransition(2.0f, 1.0f), 1.0f));
    // Scaled: s=2, t=0.4 -> v=0.8
    CHECK(near(StraightTransition(0.4f, 2.0f), 0.8f));
    // Clamped by scale: s=2, t=0.6 -> v=1.2 -> 1.0
    CHECK(near(StraightTransition(0.6f, 2.0f), 1.0f));
}

static void test_in_and_out()
{
    // Plateau region: s=0.2, t=0.5 -> 1.0
    CHECK(near(InAndOut(0.5f, 0.2f), 1.0f));
    // Ramp up: t=0.1, s=0.2 -> t/s = 0.5
    CHECK(near(InAndOut(0.1f, 0.2f), 0.5f));
    // Ramp down: t=0.9, s=0.2 -> (1-t)/s = 0.5
    CHECK(near(InAndOut(0.9f, 0.2f), 0.5f));
    // Endpoints
    CHECK(near(InAndOut(0.0f, 0.2f), 0.0f));
    CHECK(near(InAndOut(1.0f, 0.2f), 0.0f));
}

static void test_sin_pulse()
{
    // SinPulse(0.5, 1.0) == sinf(pi*0.5) == 1.0
    CHECK(near(SinPulse(0.5f, 1.0f), 1.0f, 1e-3f));
    // SinPulse(0, _) == sinf(0) == 0
    CHECK(near(SinPulse(0.0f, 1.0f), 0.0f, 1e-3f));
    // SinPulse(1, 1) == sinf(pi) == 0
    CHECK(near(SinPulse(1.0f, 1.0f), 0.0f, 1e-3f));
}

static void test_sin_transition()
{
    // SinTransition(1, deg) == 1.0 for any deg (sin(deg)/sin(deg))
    CHECK(near(SinTransition(1.0f, 90.0f), 1.0f, 1e-3f));
    CHECK(near(SinTransition(1.0f, 45.0f), 1.0f, 1e-3f));
    CHECK(near(SinTransition(1.0f, 115.0f), 1.0f, 1e-3f));
    // SinTransition(0, deg) == 0
    CHECK(near(SinTransition(0.0f, 90.0f), 0.0f, 1e-3f));
}

static void test_sin_in_and_out()
{
    // SinInAndOut(t,s) = SinTransition(InAndOut(t,s), 115.0)
    // At plateau (t=0.5, s=0.2): InAndOut=1.0 -> SinTransition(1,115)=1.0
    CHECK(near(SinInAndOut(0.5f, 0.2f), 1.0f, 1e-3f));
    // At endpoints: InAndOut=0.0 -> SinTransition(0,115)=0.0
    CHECK(near(SinInAndOut(0.0f, 0.2f), 0.0f, 1e-3f));
    CHECK(near(SinInAndOut(1.0f, 0.2f), 0.0f, 1e-3f));
}

static void test_jumpy_pulse()
{
    // JumpyPulse(0, s): t=0 <= s (any positive s), v=0 -> idx=0 -> sin(0)=0
    CHECK(near(JumpyPulse(0.0f, 0.5f), 0.0f, 1e-3f));
    // At peak of phase 1: t=s/2, so t/s=0.5 -> sin(pi/2)=1
    float s = 0.5f;
    CHECK(near(JumpyPulse(s * 0.5f, s), 1.0f, 1e-3f));
    // Phase 2 returns negative: JumpyPulse(1.0, 0.5) -> (t-s)/(1-s)=1 -> sin(pi)=0 * -0.2 = 0
    CHECK(near(JumpyPulse(1.0f, 0.5f), 0.0f, 1e-3f));
}

static void test_jumpy_sin_pulse()
{
    // JumpySinPulse(0, s): tp1=1, a=0.5*s*s*(1-1)/7=0, idx=0, sin(0)*(1-0)=0
    CHECK(near(JumpySinPulse(0.0f, 1.0f), 0.0f, 1e-3f));
}

int main()
{
    printf("test_transition: start\n");

    test_square_transition();
    printf("  SquareTransition: OK\n");

    test_full_transition();
    printf("  FullTransition: OK\n");

    test_straight_transition();
    printf("  StraightTransition: OK\n");

    test_in_and_out();
    printf("  InAndOut: OK\n");

    test_sin_pulse();
    printf("  SinPulse: OK\n");

    test_sin_transition();
    printf("  SinTransition: OK\n");

    test_sin_in_and_out();
    printf("  SinInAndOut: OK\n");

    test_jumpy_pulse();
    printf("  JumpyPulse: OK\n");

    test_jumpy_sin_pulse();
    printf("  JumpySinPulse: OK\n");

    printf("test_transition: PASS\n");
    return 0;
}
