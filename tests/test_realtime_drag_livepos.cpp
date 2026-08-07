// test_realtime_drag_livepos -- regression guard for task #13's per-present
// (native-refresh-rate) UI SCROLL finger tracking.
//
// DESIGN: touch EDGE/dispatch + slicing stay on the fixed 60Hz sim tick
// (Mortar::Touch::Update / InputTranslatorSDL::DispatchForSimTick) -- this
// task only adds a SEPARATE per-present shadow position (Mortar::Touch::
// liveX/liveY, refreshed read-only from the ring buffer by RefreshLivePos())
// that widgets can read via GetLivePos() for smoother drag tracking between
// sim ticks. RefreshLivePos() must NEVER advance the ring's m_eventHead/
// m_eventTail, never call ___UpdateInternal, and never mutate states1/
// states2/phase -- Touch::Update (the sim-tick drain) remains the ONLY
// writer of ring indices and phase state.
//
// INVARIANTS PINNED HERE:
//  (1) Queuing several sub-tick FINGERMOTIONs WITHOUT a DispatchForSimTick
//      leaves states1[slot].currY stale (unchanged from the last drain).
//  (2) RefreshLivePos() + GetLivePos() surfaces the NEWEST queued sample
//      (not the oldest, not an average) even though the ring was never
//      drained.
//  (3) The ring's m_eventHead/m_eventTail are unchanged by RefreshLivePos()
//      (read-only scan -- proves it cannot double-consume or desync the
//      sim-tick drain).
//  (4) A following DispatchForSimTick() still applies ALL queued samples in
//      order (determinism / no dropped events) -- states1[slot].currY ends
//      at the last queued position, and the ring is fully drained.
//
// Cross-build safe: no lambdas, no auto, no range-for, no enum class.

#define FN_TEST 1
#include "platform/InputTranslatorSDL.h"
#include "input/Touch.h"
#include <SDL.h>
#include <cstdio>
#include <cstring>
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
        float _a = (float)(a); float _b = (float)(b); \
        float _d = _a - _b; if (_d < 0.0f) _d = -_d; \
        if (_d > (float)(eps)) { \
            std::printf("FAIL (%s:%d): |%.4f - %.4f| = %.4f > %.4f\n", \
                        __FILE__, __LINE__, _a, _b, _d, (float)(eps)); \
            ::exit(1); \
        } \
    } while(0)

static SDL_Event MakeFingerDown(SDL_FingerID fid, float nx, float ny) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERDOWN;
    ev.tfinger.fingerId = fid;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    ev.tfinger.pressure = 1.0f;
    return ev;
}

static SDL_Event MakeFingerMotion(SDL_FingerID fid, float nx, float ny) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERMOTION;
    ev.tfinger.fingerId = fid;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    ev.tfinger.dx       = 0.01f;
    ev.tfinger.dy       = 0.01f;
    return ev;
}

static SDL_Event MakeFingerUp(SDL_FingerID fid, float nx, float ny) {
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type             = SDL_FINGERUP;
    ev.tfinger.fingerId = fid;
    ev.tfinger.x        = nx;
    ev.tfinger.y        = ny;
    return ev;
}

static void ResetTouch() {
    Mortar::Touch& t = Mortar::Touch::GetInstance();
    t.Update(0.0f);
    t.Clear();
    for (int i = 0; i < Mortar::Touch::MAX_SLOTS; ++i) {
        t.states1[i].extId   = 0;
        t.states1[i].touchId = 0;
        t.states1[i].phase   = 1;
        t.states1[i].currX   = 0;
        t.states1[i].currY   = 0;
        t.states1[i].prevX   = 0;
        t.states1[i].prevY   = 0;
        t.states1[i].liveX   = 0;
        t.states1[i].liveY   = 0;
    }
}

// Find the slot claimed for extId. NOTE: extId is NOT the raw SDL_FingerID
// passed to MakeFingerDown/Motion -- InputTranslatorSDL::DrainSDLEvent calls
// Touch::OnPressed(ch + 1, ...) where ch is the MAPPED CHANNEL (0-based,
// from MapFingerId), not the SDL_FingerID (see InputTranslatorSDL.cpp ~299:
// "Mortar::Touch::GetInstance().OnPressed(ch + 1, gx, gy)"). A fresh
// InputTranslatorSDL always maps its first real touch finger to channel 0
// (MOUSE_CHANNEL=15 and HOVER_CHANNEL=14 are reserved, see
// InputTranslatorSDL.h), so the
// first finger pressed against a fresh `tr` always lands at extId==1 --
// mirroring test_input_tick_invariant's own assertions (states1[s].extId==1
// for the first finger). Callers here pass channel+1, not the SDL_FingerID.
static int FindSlotByExtId(uint32_t extId) {
    Mortar::Touch& t = Mortar::Touch::GetInstance();
    for (int i = 0; i < Mortar::Touch::MAX_SLOTS; ++i) {
        if (t.states1[i].extId == extId) return i;
    }
    return -1;
}

// Invariant (1)+(2)+(3): sub-tick motions without a dispatch leave states1
// stale; RefreshLivePos+GetLivePos tracks the newest sample; ring indices
// are unchanged by the scan.
static void test_livepos_tracks_newest_without_draining_ring() {
    printf("  test_livepos_tracks_newest_without_draining_ring...\n");
    ResetTouch();

    InputTranslatorSDL tr;
    tr.Init();

    // Press a finger and dispatch once so it's a live slot with a known
    // extId/currY baseline. The SDL_FingerID here (1) is arbitrary -- the
    // first real touch pressed against a fresh `tr` always maps to channel 0,
    // so the resulting Touch extId is 1 (channel + 1; see FindSlotByExtId's
    // comment), independent of the SDL_FingerID value.
    SDL_Event down = MakeFingerDown((SDL_FingerID)1, 0.5f, 0.5f);
    tr.DrainSDLEvent(down, NULL);
    tr.DispatchForSimTick();

    int slot = FindSlotByExtId(1);
    CHECK(slot >= 0);

    Mortar::Touch& touch = Mortar::Touch::GetInstance();
    float baselineY = touch.states1[slot].currY;
    int headBefore = touch.eventBuffer.m_eventHead;
    int tailBefore = touch.eventBuffer.m_eventTail;

    // Queue several sub-tick FINGERMOTIONs WITHOUT DispatchForSimTick.
    // DrainSDLEvent pushes straight to the ring for channels 0-7 -- see
    // InputTranslatorSDL.h's header doc -- but nothing drains it here.
    SDL_Event m1 = MakeFingerMotion((SDL_FingerID)1, 0.5f, 0.60f);
    SDL_Event m2 = MakeFingerMotion((SDL_FingerID)1, 0.5f, 0.70f);
    SDL_Event m3 = MakeFingerMotion((SDL_FingerID)1, 0.5f, 0.80f);
    tr.DrainSDLEvent(m1, NULL);
    tr.DrainSDLEvent(m2, NULL);
    tr.DrainSDLEvent(m3, NULL);

    // (1) states1[slot].currY must still be the stale pre-motion baseline --
    // no dispatch/drain has happened yet.
    CHECK_NEAR(touch.states1[slot].currY, baselineY, 0.001f);

    // (3) Ring indices must be unaffected by the drains pushing into it
    // (tail advances by 3 -- that's ___only___ DrainSDLEvent's own push, not
    // a RefreshLivePos side effect, verified below the RefreshLivePos call).
    int tailAfterDrains = touch.eventBuffer.m_eventTail;
    CHECK(tailAfterDrains == tailBefore + 3);
    CHECK(touch.eventBuffer.m_eventHead == headBefore);

    // (2) RefreshLivePos + GetLivePos must surface the NEWEST queued sample
    // (0.80 normalized -> gy = 160 - 0.80*320 = -96.0), not the oldest
    // (0.60 -> gy=32) nor the stale baseline.
    touch.RefreshLivePos();

    float lx = 0.0f, ly = 0.0f;
    bool active = touch.GetLivePos(slot, lx, ly);
    CHECK(active);
    CHECK_NEAR(ly, -96.0f, 1.0f);
    printf("    baseline currY=%.2f, newest liveY=%.2f (expect ~-96.0)\n", baselineY, ly);

    // (3) RefreshLivePos itself must be read-only: ring indices unchanged
    // from right after the drains (no draining, no double-consumption).
    CHECK(touch.eventBuffer.m_eventHead == headBefore);
    CHECK(touch.eventBuffer.m_eventTail == tailAfterDrains);

    // states1[slot].currY (the sim-tick-authoritative value) must STILL be
    // untouched by RefreshLivePos -- only the separate liveX/liveY shadow
    // fields changed.
    CHECK_NEAR(touch.states1[slot].currY, baselineY, 0.001f);

    printf("  PASS\n");
}

// Invariant (4): a following DispatchForSimTick still applies every queued
// sample in order (determinism unaffected by the read-only RefreshLivePos
// scans that happened in between).
static void test_dispatch_after_liveposcans_applies_all_samples() {
    printf("  test_dispatch_after_liveposcans_applies_all_samples...\n");
    ResetTouch();

    InputTranslatorSDL tr;
    tr.Init();

    // SDL_FingerID (2) is arbitrary and NOT the Touch extId to look up by --
    // see FindSlotByExtId's comment: the first real touch pressed against a
    // fresh `tr` always maps to channel 0, i.e. Touch extId 1.
    SDL_Event down = MakeFingerDown((SDL_FingerID)2, 0.5f, 0.2f);
    tr.DrainSDLEvent(down, NULL);
    tr.DispatchForSimTick();

    int slot = FindSlotByExtId(1);
    CHECK(slot >= 0);

    Mortar::Touch& touch = Mortar::Touch::GetInstance();

    SDL_Event m1 = MakeFingerMotion((SDL_FingerID)2, 0.5f, 0.30f);
    SDL_Event m2 = MakeFingerMotion((SDL_FingerID)2, 0.5f, 0.40f);
    SDL_Event m3 = MakeFingerMotion((SDL_FingerID)2, 0.5f, 0.50f);
    tr.DrainSDLEvent(m1, NULL);
    // Interleave RefreshLivePos scans between drains -- as Game::tickRealtimeUi
    // would do once per present, i.e. possibly multiple times before the next
    // sim tick's DispatchForSimTick.
    touch.RefreshLivePos();
    tr.DrainSDLEvent(m2, NULL);
    touch.RefreshLivePos();
    tr.DrainSDLEvent(m3, NULL);
    touch.RefreshLivePos();

    // DispatchForSimTick drains the ENTIRE ring (all 3 motions), applying
    // them in order; the final states1 must reflect the LAST queued sample.
    tr.DispatchForSimTick();

    // gy = 160 - 0.50*320 = 0.0
    CHECK_NEAR(touch.states1[slot].currY, 0.0f, 1.0f);
    // Ring fully drained.
    CHECK(touch.eventBuffer.m_eventHead == touch.eventBuffer.m_eventTail);

    printf("    after dispatch: currY=%.2f (expect ~0.0), ring drained (head==tail)\n",
           touch.states1[slot].currY);

    printf("  PASS\n");
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_EVENTS) < 0) {
        fprintf(stderr, "SDL_Init(EVENTS) failed: %s\n", SDL_GetError());
        return 1;
    }

    printf("test_realtime_drag_livepos: start\n");

    test_livepos_tracks_newest_without_draining_ring();
    test_dispatch_after_liveposcans_applies_all_samples();

    printf("test_realtime_drag_livepos: PASS\n");

    SDL_Quit();
    return 0;
}
