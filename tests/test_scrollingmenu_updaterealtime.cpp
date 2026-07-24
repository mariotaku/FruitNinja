// test_scrollingmenu_updaterealtime -- pins ScrollingMenu::UpdateRealtime's
// f==1.0f (dtSeconds == 1/60) behaviour against hand-derived expected values
// for Phases 4 (velocity integrate), 5 (layout), and 7 (spring-back) -- see
// ScrollingMenu.cpp/.h, v1.6.1 ScrollingMenu::Update @0x001b03b4 phase split.
//
// The port splits Update()'s 7 phases across two rates:
//   - Phases 1/2/3 (touch read) + Phase 6 (click-fire) stay in Update() at
//     the fixed 60Hz sim tick.
//   - Phases 4 (velocity integrate), 5 (layout), 7 (spring-back) move to
//     UpdateRealtime(dtSeconds), dt-scaled via SM_DECAY_F/SM_SPRING_F macros
//     (defined in ScrollingMenu.cpp) so that f = dtSeconds*60 == 1.0f
//     reproduces EXACTLY the per-tick literal forms the __bada__ build still
//     runs inline inside Update() (pv *= 0.9; vel += pv; spring *= 0.75/0.25/
//     0.1). This test hand-derives the expected values for a seeded
//     mid-drag-settle state and asserts UpdateRealtime(1/60) matches them
//     exactly, so the dt-scaled forms can never silently drift from the
//     original per-tick forms.
//
// No touches are ever registered with Mortar::Touch, so Update()'s Phase 2/3
// touch-acquire always no-ops (TouchInRegion/IsTouchDown see no active
// slots) -- irrelevant here since this test calls UpdateRealtime() directly,
// which never touches Phases 1/2/3/6 at all.
//
// Pure in-process: no GPU, no audio, no SDL.

#include "hud/ScrollingMenu.h"
#include "hud/ScrollingMenuItem.h"
#include "engine/input/Touch.h"
#include <cstdio>
#include <cmath>

static bool near_eq(float a, float b, float eps = 1e-3f) {
    return fabsf(a - b) < eps;
}

// Directly seeds Mortar::Touch::states1[slot] (bypassing SDL/InputTranslator
// entirely -- this test never registers a window or event loop) so a
// stationary or jittering finger can be driven across Update()/UpdateRealtime()
// calls. phase: -1=just-pressed (IsTouchDown==2), 0=held (==1), 1=released (==0).
static void SeedTouch(int slot, float x, float y, int phase) {
    Mortar::Touch& t = Mortar::Touch::GetInstance();
    Mortar::TouchState& s = t.states1[slot];
    s.prevX = s.currX;
    s.prevY = s.currY;
    s.currX = x;
    s.currY = y;
    s.liveX = x;
    s.liveY = y;
    s.extId = (uint32_t)(slot + 1);
    s.touchId = (uint32_t)(slot + 1);
    s.phase = phase;
}

static void ClearAllTouches() {
    Mortar::Touch& t = Mortar::Touch::GetInstance();
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

// Builds a ScrollingMenu with 3 rows (heights 80/80/80, matching the shop
// list's ShopListItem::SetHeight(80)) seeded into a mid-drag-settle state:
// a nonzero m_Velocity.y (scrolled partway) and a still-decaying
// m_PendingVelocity.y (as left behind by a flick a few frames after the
// finger lifts). m_Height=80 (SetHeight), m_TotalHeight=240 (3x80 from
// AddItem), so totalScrollH = m_Height - m_TotalHeight = -160.
static ScrollingMenu* MakeSeededMenu(float velY, float pendingVelY) {
    ScrollingMenu* menu = new ScrollingMenu();
    menu->SetWidth(290.0f);
    menu->SetHeight(80.0f);
    menu->SetItemHeight(80.0f);
    menu->pos = _Vector3<float>(0.0f, 0.0f, 0.0f);

    for (int i = 0; i < 3; ++i) {
        ScrollingMenuItem* item = new ScrollingMenuItem();
        item->SetHeight(80.0f);
        menu->AddItem(item);
    }

    menu->m_Velocity        = _Vector3<float>(0.0f, velY, 0.0f);
    menu->m_PendingVelocity = _Vector3<float>(0.0f, pendingVelY, 0.0f);
    menu->m_DragTargetIdx   = -1;
    return menu;
}

int main() {
    int failures = 0;

    // --- Case 1: mid-flick, IN-RANGE, settle-gate FAILS (pv too large) ---
    // Seeded: m_Velocity.y=-37.5, m_PendingVelocity.y=4.2, m_DragTargetIdx=-1.
    // Phase 4 (f=1.0): pv' = 4.2 * powf(0.9,1) = 3.78; vel' = -37.5+3.78 = -33.72.
    // Phase 7: offset=-33.72 is neither >0 (past-top, since m_DragTargetIdx<0
    // gates that branch) nor < totalScrollH=-160 (past-bottom) -> IN-RANGE
    // branch. m_TouchId==-1 so no early return there. Settle gate needs
    // |pv'| <= VEL_NEAR_ZERO_HI(0.1); pv'=3.78 fails -> Phase 7 takes the
    // `if (!gate) return;` EARLY RETURN, so m_Velocity.y stays at Phase-4's
    // -33.72 and pv stays at Phase-4's 3.78 (Phase 7's own end-of-phase
    // friction re-apply is never reached).
    {
        ScrollingMenu* menu = MakeSeededMenu(-37.5f, 4.2f);
        menu->UpdateRealtime(1.0f / 60.0f);

        float expectedPv  = 4.2f * 0.9f;           // 3.78
        float expectedVel = -37.5f + expectedPv;   // -33.72

        if (!near_eq(menu->m_PendingVelocity.y, expectedPv, 0.01f)) {
            fprintf(stderr, "FAIL [case1 pv]: pv.y=%.4f expected=%.4f\n",
                menu->m_PendingVelocity.y, expectedPv);
            ++failures;
        } else {
            printf("[PASS] case1 pv: pv.y=%.4f matches 4.2*0.9=%.4f\n",
                menu->m_PendingVelocity.y, expectedPv);
        }

        if (!near_eq(menu->m_Velocity.y, expectedVel, 0.01f)) {
            fprintf(stderr, "FAIL [case1 vel]: vel.y=%.4f expected=%.4f\n",
                menu->m_Velocity.y, expectedVel);
            ++failures;
        } else {
            printf("[PASS] case1 vel: vel.y=%.4f matches -37.5+3.78=%.4f\n",
                menu->m_Velocity.y, expectedVel);
        }

        // Phase 5 layout: item[0] (closest to i=0, m_DragTargetIdx<0 so the
        // globally-closest-to-origin branch runs) top-of-list row lands at
        // curY = pos.y - m_Velocity.y - halfH = 0 - (-33.72) - 40 = -6.28.
        float expectedItem0Y = 0.0f - expectedVel - 40.0f;
        if (!near_eq(menu->m_Items[0]->pos.y, expectedItem0Y, 0.01f)) {
            fprintf(stderr, "FAIL [case1 layout]: item[0].pos.y=%.4f expected=%.4f\n",
                menu->m_Items[0]->pos.y, expectedItem0Y);
            ++failures;
        } else {
            printf("[PASS] case1 layout: item[0].pos.y=%.4f matches %.4f\n",
                menu->m_Items[0]->pos.y, expectedItem0Y);
        }

        delete menu;
    }

    // --- Case 2: settled (pv near zero), IN-RANGE snap-step + end friction ---
    // Seeded: m_Velocity.y=-40.0 (mid-way between item snap points),
    // m_PendingVelocity.y=0.02 (already decayed near zero, as after many
    // settled frames). Phase 4 (f=1): pv' = 0.02*0.9 = 0.018;
    // vel' = -40.0+0.018 = -39.982.
    // Phase 5 recomputes m_SnapDist (closest-item signed delta) from the new
    // vel' before Phase 7 reads it as `snapDist`.
    // Phase 7: offset=-39.982 is IN-RANGE (same reasoning as case 1). Settle
    // gate: |pv'|=0.018 <= 0.1 -> PASSES. Snap-step (f=1):
    //   vel.y = offset + snapDist * (1 - powf(1-0.1, 1)) = offset + snapDist*0.1
    // matching the __bada__ literal `offset + snapDist * VEL_NEAR_ZERO_HI`.
    // CORRECTION: the IN-RANGE `else` branch's snap-step assignment is
    // immediately followed by its own `return;` (see ScrollingMenu.cpp Phase 7,
    // both the __bada__ and port forms) -- it never falls through to the
    // post-if `Vec3Scale_ScrollMenu(&m_PendingVelocity, f)` end-of-phase
    // friction line. That line is reached ONLY by the two spring branches
    // (past-top / past-bottom), which fall through past the if/else-if/else.
    // So for the IN-RANGE case (both gate-pass and gate-fail sub-paths) pv
    // gets decayed exactly ONCE per Update/UpdateRealtime call -- by Phase 4
    // only. (An earlier draft of this test wrongly assumed the end-friction
    // line always re-applies, expecting a double-decayed 0.0162; hand-tracing
    // the canonical __bada__ Update's actual control flow for this seed
    // confirms only the single Phase-4 decay -- 0.018 -- runs.)
    {
        ScrollingMenu* menu = MakeSeededMenu(-40.0f, 0.02f);

        // Hand-derive Phase 5's m_SnapDist the same way UpdateRealtime will:
        // curY = pos.y - vel' = 0 - (-39.982) = 39.982; closest item (i=0,
        // only entry considered before halfH offset) -> distToCenter =
        // curY - pos.y = 39.982; m_SnapDist = curY - pos.y = 39.982 (since
        // i=0 is the only iteration seen before any closer one, it always
        // wins the first comparison against CLOSEST_SENTINEL=10000).
        float velAfterPhase4 = -40.0f + (0.02f * 0.9f);
        float expectedSnapDist = (0.0f - velAfterPhase4) - 0.0f;

        menu->UpdateRealtime(1.0f / 60.0f);

        float expectedVel = velAfterPhase4 + expectedSnapDist * 0.1f;
        float expectedPv  = 0.02f * 0.9f;  // Phase 4 decay ONLY -- Phase 7's
                                            // snap-step `return;` skips the
                                            // end-of-phase friction re-apply.

        if (!near_eq(menu->m_Velocity.y, expectedVel, 0.01f)) {
            fprintf(stderr, "FAIL [case2 vel]: vel.y=%.4f expected=%.4f\n",
                menu->m_Velocity.y, expectedVel);
            ++failures;
        } else {
            printf("[PASS] case2 vel: vel.y=%.4f matches snap-stepped %.4f\n",
                menu->m_Velocity.y, expectedVel);
        }

        if (!near_eq(menu->m_PendingVelocity.y, expectedPv, 0.001f)) {
            fprintf(stderr, "FAIL [case2 pv]: pv.y=%.6f expected=%.6f\n",
                menu->m_PendingVelocity.y, expectedPv);
            ++failures;
        } else {
            printf("[PASS] case2 pv: pv.y=%.6f matches single-decayed %.6f\n",
                menu->m_PendingVelocity.y, expectedPv);
        }

        delete menu;
    }

    // --- Case 3: TAP-ON-MOVING-LIST -- stationary touch must halt a fling,
    // never accelerate it. Regression guard for the momentum-runaway bug: a
    // small fling followed by a tap/tap-hold made the list START scrolling
    // and ACCELERATE instead of halting (v1.6.1 ScrollingMenu::Update
    // @0x001b03b4 pairs the Phase-3B recompute + Phase-4 integrate in ONE
    // 60Hz pass; the port had split them across Update()/UpdateRealtime()
    // cadences, letting Phase 4 integrate a NOT-recomputed pending velocity
    // on presents where the port's recompute gate didn't run).
    //
    // Seed a fling in flight (m_Velocity.y=-30, m_PendingVelocity.y=6.0, as
    // left behind mid-flick), then register a STATIONARY touch at Y=0
    // (comfortably inside both the outer [-320,320] and inner [-181.25,
    // 181.25]x[-320,320]... wait, region bounds are on X via SetWidth(290)
    // -> outer X=[-145,145], inner X=[-181.25,181.25]; Y stays the ctor
    // default [-320,320] on both regions). Drive Phase-2 acquire via a
    // press-edge Update() tick, then several UpdateRealtime() presents
    // (including presents with NO interleaved Update() -- the steps==0 case
    // a 120Hz display produces every other frame) and assert |m_Velocity.y|
    // never grows and converges toward 0 (the tap halts the list).
    {
        printf("[case3] tap-on-moving-list: fling must halt, not accelerate\n");
        ClearAllTouches();
        ScrollingMenu* menu = MakeSeededMenu(-30.0f, 6.0f);

        const int slot = 0;
        const float touchX = 0.0f, touchY = 0.0f;

        // Press-edge: Phase 2 acquires (IsTouchDown==2 for phase==-1).
        SeedTouch(slot, touchX, touchY, -1);
        menu->Update(1.0f / 60.0f);
        if (menu->m_TouchId != slot) {
            fprintf(stderr, "FAIL [case3 acquire]: m_TouchId=%d expected=%d\n",
                menu->m_TouchId, slot);
            ++failures;
        }

        // Promote to held (phase 0) for all subsequent frames -- matches
        // Touch::StateUpdate's -1 -> 0 promotion after one sim tick.
        SeedTouch(slot, touchX, touchY, 0);

        float prevAbsVel = fabsf(menu->m_Velocity.y);
        bool monotonic = true;
        // Drive a mix of presents WITH and WITHOUT an interleaved 60Hz Update()
        // tick -- a real 120Hz display produces steps==0 on roughly every
        // other present (driver.advance() cadence), which is exactly the
        // scenario that let stale pending velocity integrate unopposed.
        for (int i = 0; i < 20; ++i) {
            if ((i % 2) == 0) {
                menu->Update(1.0f / 60.0f);       // steps==1 present
            }
            // else: steps==0 present -- UpdateRealtime with no Update() this
            // iteration, exactly like Game::run()'s loop when driver.advance()
            // returns 0.
            menu->UpdateRealtime(1.0f / 60.0f);

            float absVel = fabsf(menu->m_Velocity.y);
            if (absVel > prevAbsVel + 1e-4f) {
                fprintf(stderr,
                    "FAIL [case3 monotonic]: iter=%d |vel.y|=%.5f grew from %.5f "
                    "(fling accelerated instead of halting)\n",
                    i, absVel, prevAbsVel);
                monotonic = false;
                ++failures;
                break;
            }
            prevAbsVel = absVel;
        }
        if (monotonic) {
            printf("[PASS] case3: |m_Velocity.y| monotonically decreased/held "
                   "each present, final=%.5f\n", prevAbsVel);
        }

        // A stationary tap HALTS the list wherever it currently sits -- it
        // does not spring back toward 0 (Phase 7's spring-back is gated
        // `if (m_TouchId != -1) return;`, so it never runs while held). The
        // regression this guards against is GROWTH past the seeded -30.0,
        // not failure to reach 0; assert the halt held near the seed value
        // (paired recompute+integrate converges pending -> ~0 immediately
        // for a stationary anchor, freezing m_Velocity.y, per the hand-derived
        // simulation in this test's design notes).
        if (fabsf(menu->m_Velocity.y) > 30.5f) {
            fprintf(stderr,
                "FAIL [case3 halt]: |m_Velocity.y|=%.5f grew past the seeded "
                "30.0 (fling accelerated instead of halting)\n",
                fabsf(menu->m_Velocity.y));
            ++failures;
        } else {
            printf("[PASS] case3 halt: |m_Velocity.y|=%.5f held near seeded 30.0, "
                "did not accelerate\n", fabsf(menu->m_Velocity.y));
        }

        delete menu;
        ClearAllTouches();
    }

    // --- Case 4: boundary-jitter churn must not grow velocity. ---
    // Toggle the touch's position across the inner-region Y boundary each
    // tick (simulating a finger sitting right at the hysteresis edge) and
    // assert no acquire/release churn causes m_Velocity.y to grow. The inner
    // region's Y bounds are the ctor default [-320, 320] (SetWidth only
    // affects X), so toggle X across the inner-region edge (+/-181.25)
    // instead -- that's the actual hysteresis boundary SetWidth establishes.
    {
        printf("[case4] boundary-jitter churn must not grow velocity\n");
        ClearAllTouches();
        ScrollingMenu* menu = MakeSeededMenu(-20.0f, 3.0f);

        const int slot = 0;
        const float insideX  = 0.0f;      // well inside outer+inner X bounds
        const float outsideX = 200.0f;    // outside inner X bound (+/-181.25)

        SeedTouch(slot, insideX, 0.0f, -1);   // press-edge acquire
        menu->Update(1.0f / 60.0f);
        if (menu->m_TouchId != slot) {
            fprintf(stderr, "FAIL [case4 acquire]: m_TouchId=%d expected=%d\n",
                menu->m_TouchId, slot);
            ++failures;
        }

        float prevAbsVel = fabsf(menu->m_Velocity.y);
        bool grew = false;
        for (int i = 0; i < 16; ++i) {
            // Toggle in/out of the inner region's X bound every other tick --
            // boundary jitter. Held phase (0) throughout (no real re-press).
            float x = ((i % 2) == 0) ? insideX : outsideX;
            SeedTouch(slot, x, 0.0f, 0);
            menu->Update(1.0f / 60.0f);
            menu->UpdateRealtime(1.0f / 60.0f);

            float absVel = fabsf(menu->m_Velocity.y);
            if (absVel > prevAbsVel + 1e-4f) {
                fprintf(stderr,
                    "FAIL [case4 churn]: iter=%d |vel.y|=%.5f grew from %.5f\n",
                    i, absVel, prevAbsVel);
                grew = true;
                ++failures;
                break;
            }
            prevAbsVel = absVel;
        }
        if (!grew) {
            printf("[PASS] case4: boundary-jitter churn never grew |m_Velocity.y|, "
                   "final=%.5f\n", prevAbsVel);
        }

        delete menu;
        ClearAllTouches();
    }

    // --- Case 5: GetLivePos-failure present must SKIP integration, not
    // integrate stale pending. Directly targets the exact gap the fix closes
    // (ScrollingMenu.cpp UpdateRealtime, `recomputedThisPresent` gate): a
    // present where m_TouchId is still owned (no interleaved Update() ran to
    // clear it) but GetLivePos() fails (touch slot's phase reports released)
    // must not integrate the untouched m_PendingVelocity into m_Velocity --
    // before the fix, integration ran unconditionally and would have carried
    // the large seeded pending straight into m_Velocity that present.
    {
        printf("[case5] GetLivePos-failure present must skip integration\n");
        ClearAllTouches();
        ScrollingMenu* menu = MakeSeededMenu(-25.0f, 8.0f);

        const int slot = 0;
        SeedTouch(slot, 0.0f, 0.0f, -1);      // press-edge acquire
        menu->Update(1.0f / 60.0f);
        if (menu->m_TouchId != slot) {
            fprintf(stderr, "FAIL [case5 acquire]: m_TouchId=%d expected=%d\n",
                menu->m_TouchId, slot);
            ++failures;
        }
        SeedTouch(slot, 0.0f, 0.0f, 0);        // held
        menu->UpdateRealtime(1.0f / 60.0f);     // one normal paired present (PV -> ~0)

        // Now force the slot's phase to "released" WITHOUT calling Update()
        // (so m_TouchId stays == slot, exactly as it would on a present with
        // no interleaved 60Hz sim tick) and manually re-inflate
        // m_PendingVelocity to simulate a present where a fresh recompute
        // would have been skipped for some other transient reason. The fix
        // must leave m_Velocity.y untouched this call since GetLivePos()
        // fails (phase>=1) while m_TouchId != -1.
        Mortar::Touch::GetInstance().states1[slot].phase = 1;  // released
        float velBefore = menu->m_Velocity.y;
        menu->m_PendingVelocity.y = 50.0f;   // large stale value that must NOT integrate
        menu->UpdateRealtime(1.0f / 60.0f);

        if (!near_eq(menu->m_Velocity.y, velBefore, 1e-4f)) {
            fprintf(stderr,
                "FAIL [case5 skip]: m_Velocity.y changed from %.5f to %.5f "
                "(integrated stale pending despite GetLivePos failure)\n",
                velBefore, menu->m_Velocity.y);
            ++failures;
        } else {
            printf("[PASS] case5: m_Velocity.y held at %.5f, stale pending=50.0 "
                   "was NOT integrated while GetLivePos failed\n", menu->m_Velocity.y);
        }

        delete menu;
        ClearAllTouches();
    }

    if (failures == 0) {
        printf("[PASS] All scrollingmenu_updaterealtime cases passed.\n");
        return 0;
    }
    return 1;
}
