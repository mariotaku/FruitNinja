// test_slash_collision.cpp
//
// Regression guard for SlashEntity::CollideWithSphere blade-vs-sphere collision.
//
// Gap covered: the previous test suite had no test exercising the collision
// path itself, so a regression in the segment-clamped geometry (m_SegLenSq /
// trail-shift) could pass all tests undetected.
//
// Three assertions:
//   1. CROSSING HIT: a ribbon that spans the sphere center returns true.
//   2. BLANK MISS:   a far-corner stroke returns false (guards the pre-#306
//      infinite-line over-trigger: segment clamp must not extend the blade).
//   3. NEAR-MISS / NEAR-HIT boundary (optional but load-bearing): a stroke
//      whose closest point is just outside the radius misses; one whose
//      closest point is inside the radius hits. Confirms the clamp geometry.
//
// Approach: drive AddPoint directly via the public TouchMoveX/TouchMoveY/
// TouchDown API so the test exercises the same code path the game uses.
// No SDL_Init, no GL, no GameInit, no audio -- follows test_slash_reset.cpp
// bootstrap pattern.
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "entities/SlashEntity.h"
#include "collision/ColSphere.h"
#include "engine/input/InputEvent.h"
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
// Touch helpers (identical pattern to test_slash_reset.cpp).
// The binary Touch path is: TouchMoveX writes m_RawTouchPos.x, TouchMoveY
// writes m_RawTouchPos.y, then TouchDown -> UpdateTouchDown calls
// OnTouchActive(m_RawTouchPos.x, m_RawTouchPos.y) to build the ribbon.
// ---------------------------------------------------------------------------

static InputEvent MakeMove(float x, float y) {
    InputEvent ev;
    ev.actionHash  = 0;
    ev.actionFlags = INPUT_ACTION_MOVE;
    ev.fingerId    = 0;
    ev.x           = x;
    ev.y           = y;
    ev.deltaX      = 0.0f;
    ev.deltaY      = 0.0f;
    ev.keycode     = 0;
    ev.m_mapper    = 0;
    return ev;
}

static InputEvent MakeDown(float x, float y, bool pressEdge) {
    InputEvent ev;
    ev.actionHash  = 0;
    ev.actionFlags = INPUT_ACTION_DOWN | (pressEdge ? INPUT_ACTION_DOWN_EDGE : 0u);
    ev.fingerId    = 0;
    ev.x           = x;
    ev.y           = y;
    ev.deltaX      = 0.0f;
    ev.deltaY      = 0.0f;
    ev.keycode     = 0;
    ev.m_mapper    = 0;
    return ev;
}

// Feed one (x,y) touch point into the SlashEntity.
// pressEdge=true: fires Reset() + seeds a new stroke.
// pressEdge=false: extends the current stroke.
static void Touch(SlashEntity& se, float x, float y, bool pressEdge) {
    InputEvent move = MakeMove(x, y);
    se.TouchMoveX(&move);
    se.TouchMoveY(&move);
    InputEvent down = MakeDown(x, y, pressEdge);
    se.TouchDown(&down);
}

// ---------------------------------------------------------------------------
// TEST 1 -- crossing_hit
//
// Blade: seed at (-60, 0), then (0, 0), then (60, 0).
// Buffer layout after three Touch calls (60-unit steps, each < POINT_SPACING=64,
// so no interpolation): spine positions at even indices:
//   [0] = (-60, 0, 0)  -- seed AddPoint
//   [2] = (  0, 0, 0)  -- second AddPoint (distance 60 > MOVE_THRESH=5, < 64)
//   [4] = ( 60, 0, 0)  -- third AddPoint
//
// Sphere at (0, 0, 0), radius 40.
// CollideWithSphere loop at i=0: a=(-60,0), b=(0,0), c=(0,0).
//   t = dot(c-a, b-a) / |b-a|^2 = dot(60,0, 60,0) / 3600 = 1.0 (clamped).
//   Closest = b = (0,0). dist^2 = 0 < 40^2 = 1600.  HIT.
// ---------------------------------------------------------------------------
static void test_crossing_hit() {
    std::printf("  test_crossing_hit...\n");

    SlashEntity se;
    se.Init(static_cast<void*>(0), 0L, static_cast<Vec3*>(0));

    Touch(se, -60.0f, 0.0f, true);   // seed; m_PointCount=2, spine[0]=(-60,0,0)
    Touch(se, 0.0f,   0.0f, false);  // head reaches sphere centre; m_PointCount=4
    Touch(se, 60.0f,  0.0f, false);  // extends past centre;        m_PointCount=6

    int pc = se.GetPointCount();
    std::printf("    m_PointCount=%d (expect >= 4)\n", pc);
    CHECK(pc >= 4);
    CHECK(se.IsBladeActive());

    ColSphere sphere(Vec3(0.0f, 0.0f, 0.0f), 40.0f);
    Vec3 bladeVel;
    bool hit = se.CollideWithSphere(sphere, bladeVel);
    std::printf("    hit=%s (expect true)\n", hit ? "true" : "false");
    CHECK(hit);

    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// TEST 2 -- blank_miss (#306 segment-clamp guard)
//
// Blade: seed at (-220, 150), then (-200, 150).
// Stroke is a short 20-unit horizontal segment at y=150, far from (0,0).
//   delta=20 > MOVE_THRESH=5 so the second point is accepted;
//   dist=20 < POINT_SPACING=64 so no intermediate points.
//   Spine [0]=(-220,150,0), [2]=(-200,150,0).
//
// Sphere at (0, 0, 0), radius 40.
// CollideWithSphere i=0: a=(-220,150), b=(-200,150), c=(0,0).
//   t = dot(220,-150, 20,0)/400 = 11.0 (clamped to 1.0).
//   Closest = b = (-200,150). dist^2 = 200^2+150^2 = 62500 >> 1600.  MISS.
//
// A naive infinite-line test would compute the perpendicular distance from
// (0,0) to the line y=150, giving 150 -- still a miss. This case primarily
// guards against accidentally reversing the clamp direction (t>1 -> closest
// must be b, not a point beyond b).
// ---------------------------------------------------------------------------
static void test_blank_miss() {
    std::printf("  test_blank_miss...\n");

    SlashEntity se;
    se.Init(static_cast<void*>(0), 0L, static_cast<Vec3*>(0));

    Touch(se, -220.0f, 150.0f, true);   // seed at far corner
    Touch(se, -200.0f, 150.0f, false);  // 20-unit step; m_PointCount=4

    int pc = se.GetPointCount();
    std::printf("    m_PointCount=%d (expect >= 4)\n", pc);
    CHECK(pc >= 4);
    CHECK(se.IsBladeActive());

    ColSphere sphere(Vec3(0.0f, 0.0f, 0.0f), 40.0f);
    Vec3 bladeVel;
    bool hit = se.CollideWithSphere(sphere, bladeVel);
    std::printf("    hit=%s (expect false)\n", hit ? "true" : "false");
    CHECK(!hit);

    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// TEST 3 -- near_boundary
//
// Both strokes start to the right of the sphere and extend further right.
// The clamp ensures only the actual closest point on the segment is tested.
//
// Near-miss: seed at (45,0), end at (100,0). dist=55 < 64 -> no interpolation.
//   Spine [0]=(45,0,0), [2]=(100,0,0).
//   i=0: a=(45,0), b=(100,0), c=(0,0).
//   t = dot(-45,0, 55,0)/55^2 = -2475/3025 = -0.818 -> clamped to 0.
//   Closest = a = (45,0). dist=45 > radius=40.  MISS.
//
// Near-hit: seed at (35,0), end at (100,0). dist=65 > 64 -> one intermediate
//   point at (35+64,0)=(99,0). Spine [0]=(35,0), [2]=(99,0), [4]=(100,0).
//   i=0: a=(35,0), b=(99,0), c=(0,0).
//   t = dot(-35,0, 64,0)/64^2 = -2240/4096 = -0.547 -> clamped to 0.
//   Closest = a = (35,0). dist=35 < radius=40.  HIT.
// ---------------------------------------------------------------------------
static void test_near_boundary() {
    std::printf("  test_near_boundary (near-miss)...\n");
    {
        SlashEntity se;
        se.Init(static_cast<void*>(0), 0L, static_cast<Vec3*>(0));

        Touch(se, 45.0f,  0.0f, true);
        Touch(se, 100.0f, 0.0f, false);

        CHECK(se.GetPointCount() >= 4);
        CHECK(se.IsBladeActive());

        ColSphere sphere(Vec3(0.0f, 0.0f, 0.0f), 40.0f);
        Vec3 bladeVel;
        bool hit = se.CollideWithSphere(sphere, bladeVel);
        std::printf("    near-miss hit=%s (expect false, dist=45 > r=40)\n",
                    hit ? "true" : "false");
        CHECK(!hit);
    }
    std::printf("  PASS\n");

    std::printf("  test_near_boundary (near-hit)...\n");
    {
        SlashEntity se;
        se.Init(static_cast<void*>(0), 0L, static_cast<Vec3*>(0));

        // dist=65 just exceeds POINT_SPACING=64 -> one interpolated point
        // at (35+64,0)=(99,0) before the head point at (100,0).
        Touch(se, 35.0f,  0.0f, true);
        Touch(se, 100.0f, 0.0f, false);

        CHECK(se.GetPointCount() >= 4);
        CHECK(se.IsBladeActive());

        ColSphere sphere(Vec3(0.0f, 0.0f, 0.0f), 40.0f);
        Vec3 bladeVel;
        bool hit = se.CollideWithSphere(sphere, bladeVel);
        std::printf("    near-hit  hit=%s (expect true,  dist=35 < r=40)\n",
                    hit ? "true" : "false");
        CHECK(hit);
    }
    std::printf("  PASS\n");
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::printf("test_slash_collision: start\n");

    test_crossing_hit();
    test_blank_miss();
    test_near_boundary();

    std::printf("test_slash_collision: PASS\n");
    return 0;
}
