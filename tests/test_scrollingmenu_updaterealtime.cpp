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
#include <cstdio>
#include <cmath>

static bool near_eq(float a, float b, float eps = 1e-3f) {
    return fabsf(a - b) < eps;
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

    if (failures == 0) {
        printf("[PASS] All scrollingmenu_updaterealtime cases passed.\n");
        return 0;
    }
    return 1;
}
