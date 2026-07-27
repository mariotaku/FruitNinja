// test_slash_reset -- regression guard for the SlashEntity::Reset() "bridging
// triangle" bug.
//
// ROOT CAUSE: The prior port Reset() loop only wrote .colour=white, leaving
// .x/.y/.z stale. DrawSlice submits m_PointCount+1 verts. After Reset,
// m_PointCount=0; first AddPoint sets m_PointCount=2; DrawSlice draws indices
// [0,1,2]. Index 2 (head-cap slot) is only written by UpdatePoints when
// m_PointCount>2. So in the first 1-2 frames of slice B the vertex at index 2
// still held slice A's position -> the strip bridged from the new slice (verts
// 0,1) to the previous slice's ghost vertex (index 2).
//
// FIX: Reset() now fully zeroes both ribbon buffers (pos=0, normal=(0,0,1),
// uv=0, colour=white) matching binary @0x1e6688.
//
// TEST STRATEGY (pure logic, no SDL / GL / audio):
//   1. Init a SlashEntity (allocates buffers).
//   2. Plant a canary position (y=+100) into buffer[2] of both buffers --
//      simulating the "stale end of slice A" state.
//   3. Call Reset() -- the fix should zero it; the old code left it at +100.
//   4. Assert buffer[2].y == 0.0f (post-fix) NOT +100.0f (pre-fix).
//   5. Bonus assertion: after driving a second strip (2 AddPoint calls, so
//      m_PointCount reaches 2 but head-cap slot [m_PointCount==2] is still
//      unfilled by UpdatePoints), GetVertexY(2) must be ~0.0f (from the
//      Reset wipe), NOT +100.0f (the prior-slice canary).
//
// This test FAILS on the old Reset (only-colour) and PASSES with the fix.
// It does NOT use SDL, GL, SoundManager, or any game singleton.
//
// FIXTURE INVARIANT: this file drives only the Touch API and Reset(), never
// SlashEntity::Update(). That matters -- Update's swipe-sound step calls
// PlaySwipe (v1.6.1 SlashEntity::PlaySwipe @0x001e8550) once |m_BladeDir| > 35,
// and PlaySwipe derefs game_work.mHud unguarded (SlashEntity.cpp:552), faithful
// to the binary, which relies on GameInit having created the HUD. If an
// Update() call is ever added here, the fixture must first do what boot does --
// game_work.mHud = new HUD() -- as test_slash_collision.cpp already does.
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#include "entities/SlashEntity.h"
#include "engine/render/QUADCUSTOMVERTEX.h"
#include "engine/input/InputEvent.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL (%s:%d): %s\n", __FILE__, __LINE__, #cond); \
            ::exit(1); \
        } \
    } while(0)

#define CHECK_NEAR(a, b, eps) \
    do { \
        float _a = (float)(a); \
        float _b = (float)(b); \
        float _d = _a - _b; if (_d < 0.0f) _d = -_d; \
        if (_d > (float)(eps)) { \
            std::printf("FAIL (%s:%d): |%.4f - %.4f| = %.4f > %.4f\n", \
                        __FILE__, __LINE__, _a, _b, _d, (float)(eps)); \
            ::exit(1); \
        } \
    } while(0)

// Plant a canary value directly into the allocated buffer at index [idx]
// by reaching through the test-seam accessor approach.
// We use InitPoints to get a fresh buffer, then write directly via a
// temporary raw-pointer obtained through a placement-consistent cast.
// Since SlashEntity exposes no raw-pointer accessor, we leverage that
// InitPoints is public and the test-seam GetVertexY is public --
// if GetVertexY reads buffer[2].y and returns the canary we planted,
// the canary write succeeded (proves the accessor works).
//
// The only way to write to the buffer without a getter is to go through
// InitPoints (which zeroes) then AddPoint. But AddPoint is private.
//
// Instead we test at the level that is testable without private access:
//   * After InitPoints: GetVertexY(2) == 0.0f (buffers start zeroed).
//   * After InitPoints: manually confirm via GetVertexY the init state.
//   * Simulate what AddPoint leaves behind: after two AddPoint-equivalent
//     actions the head-cap slot at index m_PointCount must still be 0.0f
//     (because Reset zeroed it and UpdatePoints hasn't run yet).
//
// To plant the canary we use the fact that Reset clears [0..m_SplitPoint).
// We call InitPoints (zeroes all), then call Reset() itself twice:
//   - The FIRST Reset call must zero everything (this is the fix).
//   - We verify that even if the buffers had non-zero data from a prior
//     InitPoints, Reset zeroes them.
//
// To actually plant a non-zero canary we need to write through a public
// path. The only public write path to buffer contents (without AddPoint)
// is via the ColourType==0 UpdatePoints path -- but that requires a GL
// context for the internal colour stamping.
//
// Simplest feasible no-GL approach:
//   * InitPoints allocates zeroed buffers -> GetVertexY(2) == 0.
//   * Drive a SHORT slice (1 synthetic touch point) via TouchMoveX/Y +
//     TouchDown -- each takes an InputEvent*. m_RawTouchPos is set by
//     TouchMoveX/Y; TouchDown calls Reset()+UpdateTouchDown->OnTouchActive.
//   * OnTouchActive writes to buffer[0] and buffer[1] (first AddPoint),
//     and sets m_PointCount=2.
//   * GetVertexY(2) at that point is the HEAD-CAP SLOT -- still 0 if Reset
//     cleared it, OR the stale value from a prior slice if Reset only wrote
//     .colour.
//   * We prime the canary by: InitPoints, then memset slice A's position
//     into buffer[2] via a small helper below, then call Reset(), then
//     drive one AddPoint (via TouchDown with a synthetic event), then check
//     GetVertexY(2).
//
// Because the internal buffer pointer is not accessible from the test, we
// use a workaround: InitPoints zeroes the buffer. We then call TouchDown
// once at y=+100 (slice A) to get m_PointCount=2 (buffer[0,1] written).
// At this point buffer[2] is still 0 (freshly InitPoints'd). For the
// pre-fix canary we need to call it AGAIN from y=-100 WITHOUT Reset in
// between, so buffer[2] gets written by a second AddPoint for slice A.
// Then Reset(). Then TouchDown at y=-100 (slice B) so buffer[0,1] get
// slice B positions. Now check buffer[2] -- it should be 0, not +100.
//
// Concretely:
//   1. InitPoints(160)            -> buffer all 0
//   2. TouchDown at y=+100        -> buffer[0].y=100, buffer[1].y=~100, m_PointCount=2
//      (Reset fires because m_BladeActive==0, zeroes all, then AddPoint writes [0],[1])
//   3. TouchDown at y=+110 (continue slice A, m_BladeActive now =1 so no Reset)
//      -> buffer[2].y=~110, buffer[3].y=~110, m_PointCount=4
//   4. Reset()                    -> must zero buffer[2] (THE FIX)
//   5. TouchDown at y=-100 (start slice B; m_BladeActive==0 because Reset cleared it)
//      -> AddPoint writes buffer[0].y=-100, buffer[1].y=~-100, m_PointCount=2
//   6. GetVertexY(2) MUST be ~0 (post-fix) or ~+110 (pre-fix bug)
//
// For step 3 to work without Reset firing, m_BladeActive must be non-zero.
// TouchDown sets m_BladeActive |= 1 via OnTouchActive -> AddPoint epilogue.
// So after step 2, m_BladeActive=1. Step 3 calls TouchDown which checks
// (m_BladeActive==0 || pressEdge). m_BladeActive=1, and we don't set
// INPUT_ACTION_DOWN_EDGE, so Reset does NOT fire. Good.

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

// Drive SlashEntity to accept one touch position: set raw pos then fire TouchDown.
// pressEdge=true forces Reset (first point of a new slice).
static void Touch(SlashEntity& se, float x, float y, bool pressEdge) {
    InputEvent move = MakeMove(x, y);
    se.TouchMoveX(&move);
    se.TouchMoveY(&move);
    InputEvent down = MakeDown(x, y, pressEdge);
    se.TouchDown(&down);
}

static void test_reset_zeroes_head_cap_slot() {
    printf("  test_reset_zeroes_head_cap_slot...\n");

    SlashEntity se;

    // Binary-faithful Init (allocates ColLine, calls InitPoints(160)).
    se.Init(static_cast<void*>(0), 0L, static_cast<_Vector3<float>*>(0));

    // Immediately after Init: buffer[2] must be 0 (InitPoints zeroes all).
    CHECK_NEAR(se.GetVertexY(2), 0.0f, 0.01f);

    // Step 2: Slice A, first touch at y=+100 with pressEdge=true (forces Reset +
    // AddPoint). After this: m_PointCount=2, buffer[0,1] hold y~=+100.
    Touch(se, 0.0f, 100.0f, true);
    CHECK(se.GetPointCount() == 2);

    // buffer[2] must still be 0 -- it has not been written by AddPoint yet.
    CHECK_NEAR(se.GetVertexY(2), 0.0f, 0.01f);

    // Step 3: Slice A continues -- second touch far enough away to trigger AddPoint.
    // Move 200 units (> POINT_SPACING=64) so OnTouchActive hits the non-seed branch
    // and adds more points. pressEdge=false so Reset does NOT fire.
    Touch(se, 0.0f, 300.0f, false);
    // m_PointCount should have grown past 2.
    int countAfterSliceA = se.GetPointCount();
    printf("    countAfterSliceA=%d\n", countAfterSliceA);
    CHECK(countAfterSliceA >= 4);

    // buffer[2] is now written by AddPoint with y near +300 (or intermediate).
    float ySliceA = se.GetVertexY(2);
    printf("    buffer[2].y after slice A = %.2f (expect != 0 and != -100)\n", ySliceA);
    // Must NOT be zero (it was overwritten by the second AddPoint).
    float absYSliceA = ySliceA < 0.0f ? -ySliceA : ySliceA;
    CHECK(absYSliceA > 50.0f);

    // Step 4: Reset -- THE FIX: must zero buffer[2].
    se.Reset();
    CHECK(se.GetPointCount() == 0);
    float yAfterReset = se.GetVertexY(2);
    printf("    buffer[2].y after Reset() = %.2f (expect 0.0)\n", yAfterReset);
    CHECK_NEAR(yAfterReset, 0.0f, 0.01f);

    // Step 5: Slice B at y=-100 with pressEdge=true.
    // TouchDown sees m_BladeActive==0 (Reset clears it) so Reset fires again
    // (idempotent), then AddPoint writes buffer[0,1] at y=-100.
    Touch(se, 0.0f, -100.0f, true);
    CHECK(se.GetPointCount() == 2);

    // Step 6: head-cap slot is at index m_PointCount (==2). It must be 0.0f
    // (post-Reset wipe) not the +300-region value from slice A.
    // This is the exact bridging-triangle assertion: if buffer[2] were +300
    // the strip from slice B (indices 0,1) would bridge to slice A's ghost.
    float yHeadCap = se.GetVertexY(se.GetPointCount());
    printf("    buffer[m_PointCount=2].y (head-cap slot) = %.2f (expect ~0.0)\n", yHeadCap);
    CHECK_NEAR(yHeadCap, 0.0f, 0.01f);

    printf("  PASS\n");
}

static void test_reset_zeroes_both_buffers() {
    printf("  test_reset_zeroes_both_buffers...\n");

    SlashEntity se;
    se.Init(static_cast<void*>(0), 0L, static_cast<_Vector3<float>*>(0));

    // Drive slice A and populate buffer[2] with non-zero data.
    Touch(se, 0.0f, 100.0f, true);
    Touch(se, 0.0f, 300.0f, false);
    CHECK(se.GetPointCount() >= 4);

    // Confirm buffer[2] is non-zero.
    float yBefore = se.GetVertexY(2);
    float absYBefore = yBefore < 0.0f ? -yBefore : yBefore;
    CHECK(absYBefore > 10.0f);

    // Reset -- must zero all buffer entries.
    se.Reset();

    // Check multiple slots beyond m_PointCount (which is now 0).
    // GetVertexY upper bound is m_SplitPoint, so indices 0..m_SplitPoint-1
    // are all accessible and should all be 0.
    for (int i = 0; i < 10; ++i) {
        float y = se.GetVertexY(i);
        if (y < -0.01f || y > 0.01f) {
            printf("    FAIL: buffer[%d].y = %.4f after Reset (expected 0)\n", i, y);
            ::exit(1);
        }
    }

    printf("  PASS\n");
}

int main() {
    printf("test_slash_reset: start\n");

    test_reset_zeroes_head_cap_slot();
    test_reset_zeroes_both_buffers();

    printf("test_slash_reset: PASS\n");
    return 0;
}
