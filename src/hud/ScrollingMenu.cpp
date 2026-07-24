// Analysed: 2026-04-25T21:45
//
// ScrollingMenu full Update implementation.
// Binary: ctor 0x0015b3b0 (TODO: re-verify v1.6.1), v1.6.1 ScrollingMenu::Update @0x001b03b4,
// AddItem 0x0015be54 (TODO: re-verify v1.6.1).
// Touch physics: drag detection, velocity integration, spring-back, per-item layout.

#include "ScrollingMenu.h"
#include "ScrollingMenuItem.h"
#include "entities/SlashEntity.h"
#include "engine/input/Touch.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <cstddef>
#include <cmath>

// ---------------------------------------------------------------------------
// DAT constants from binary (via docs/screens/shop.md)
// ---------------------------------------------------------------------------

// DAT_0015b740: Vec3Scale_ScrollMenu friction multiplier
static const float SCROLL_FRICTION   = 0.9f;    // 0x3f666666

// DAT_0015be00: drag detection threshold (abs y-delta must exceed this)
static const float DRAG_THRESHOLD    = 0.001f;  // 0x3a83126f

// DAT_0015be04 / DAT_0015be08: default visible range [top, bottom]
static const float RANGE_TOP         = -160.0f; // 0xc3200000
static const float RANGE_BOT         =  160.0f; // 0x43200000

// DAT_0015be10: initial sentinel distance for per-item closest search
static const float CLOSEST_SENTINEL  = 10000.0f; // 0x461c4000

// DAT_0015be14 / DAT_0015be20: velocity near-zero gate for spring hold
static const float VEL_NEAR_ZERO_LO  = -0.1f;  // 0xbdcccccd
static const float VEL_NEAR_ZERO_HI  =  0.1f;  // 0x3dcccccd (also snap step factor)

// Literals from decompile
static const float DRAG_CANCEL_DIST  = 5.0f;   // 0x40a00000 — finger-move to cancel tap
static const float SPRING_BACK_COEF  = 0.75f;  // 0x3f400000 — spring when past top
static const float SPRING_FWD_COEF   = 0.25f;  // 0x3e800000 — spring when past bottom
static const float CLICK_SNAP_GATE   = 2.0f;   // 0x40000000 — snap near-zero gate
static const float CLICK_VEL_GATE    = 0.5f;   // 0x3f000000 — velocity near-zero gate
static const float DRAG_DELTA_FACTOR = -0.5f;  // binary DAT

// ---------------------------------------------------------------------------
// Rate-independence macros for Phase 4/5/7 (Update @0x001b03b4 physics).
//
// Under __bada__, SM_DECAY_F/SM_SPRING_F expand to the ORIGINAL per-60Hz-tick
// scalar forms (`v *= k`, `v += (to - v) * k`) -- byte-identical to the
// binary, no powf, no extra locals. Under the port, the SAME call sites
// expand to dt-scaled forms using a local `float f` in scope at each use site
// (f = 1.0f in the 60Hz Update() phases, f = clamp(dtSeconds,0,0.1)*60 in
// UpdateRealtime()) so f==1 exactly reproduces the 60Hz tick.
//
// This keeps the Phase 4/5/7 arithmetic written ONCE per operation (as a
// macro body) so the __bada__ verbatim branch and the port's dt-scaled
// UpdateRealtime() can't drift apart. Do NOT extract these into functions --
// a real function call adds a `bl` under __bada__ that the binary does not
// have, which asm-verify would flag as a divergence.
// ---------------------------------------------------------------------------
#ifdef __bada__
    // v *= k  (decay towards zero by factor k each call)
    #define SM_DECAY_F(v, k)        ((v) *= (k))
    // v += (to - v) * k  (spring towards `to` by factor k each call)
    #define SM_SPRING_F(v, to, k)   ((v) += ((to) - (v)) * (k))
#else
    #define SM_DECAY_F(v, k)        ((v) *= powf((k), f))
    #define SM_SPRING_F(v, to, k)   ((v) += ((to) - (v)) * (1.0f - powf(1.0f - (k), f)))
#endif

// ---------------------------------------------------------------------------
// Vec3Scale_ScrollMenu @ 0x0015b714
// Multiplies all 3 components of a Vec3 by SCROLL_FRICTION (0.9), rate-scaled
// under the port (see SM_DECAY_F above). `f` must be in scope at the call site
// (f=1.0f in Update()'s __bada__-only Phase 4/7 uses; f=frame-equivalent in
// UpdateRealtime()). Matches the binary helper called twice in Update.
// ---------------------------------------------------------------------------
#ifdef __bada__
static void Vec3Scale_ScrollMenu(_Vector3<float>* v) {
    v->x *= SCROLL_FRICTION;
    v->y *= SCROLL_FRICTION;
    v->z *= SCROLL_FRICTION;
}
#else
static void Vec3Scale_ScrollMenu(_Vector3<float>* v, float f) {
    v->x *= powf(SCROLL_FRICTION, f);
    v->y *= powf(SCROLL_FRICTION, f);
    v->z *= powf(SCROLL_FRICTION, f);
}
#endif

// ---------------------------------------------------------------------------
// ScrollingMenu ctor @ 0x0015b3b0
// ---------------------------------------------------------------------------
ScrollingMenu::ScrollingMenu()
    : m_TouchId(-1)
    , m_TouchAnchorPos(0.0f, 0.0f, 0.0f)
    , m_AnchorOffset(0.0f, 0.0f, 0.0f)
    , m_PendingVelocity(0.0f, 0.0f, 0.0f)
    , m_Width(320.0f)        // DAT_0015b468 = 0x43a00000
    , m_Height(320.0f)       // DAT_0015b468 (binary uses SAME word for width AND height)
    , m_ItemHeight(240.0f)   // DAT_0015b46c = 0x43700000
    , m_TotalHeight(0.0f)
    , m_TotalWidth(0.0f)
    , m_ClosestIdx(0)
    , m_DragTargetIdx(-1)
    , m_SnapDist(1.0f)
    , m_bDragging(0)
    , m_bTouchProcessed(0)
    , m_bCollideEnabled(1)
    , m_padCB(0)
    , m_pCollidedItem(nullptr)
    , m_bConstrainedView(0)
    , m_pad_d1{0, 0, 0}
    , m_Velocity(0.0f, 0.0f, 0.0f)
#ifndef __bada__
    , m_pClickTarget(nullptr)
    , m_ClosestSnapDelta(0.0f)
#endif
{
    // Binary ctor helper @ 0x0015b2bc writes { -120, +320, +120, -320 } for both
    // outer and inner: [0]=LEFT, [1]=TOP, [2]=RIGHT, [3]=BOTTOM.
    // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x0015b3b0 ctor (asm-inspector)
    m_OuterRegion[0] = -120.0f;  // LEFT
    m_OuterRegion[1] =  320.0f;  // TOP
    m_OuterRegion[2] =  120.0f;  // RIGHT
    m_OuterRegion[3] = -320.0f;  // BOTTOM

    m_InnerRegion[0] = -120.0f;  // LEFT
    m_InnerRegion[1] =  320.0f;  // TOP
    m_InnerRegion[2] =  120.0f;  // RIGHT
    m_InnerRegion[3] = -320.0f;  // BOTTOM
}

ScrollingMenu::~ScrollingMenu() {
    DestroyList();
}

// ---------------------------------------------------------------------------
// ScrollingMenu::SetWidth @ 0x001479a0
// Writes field40_0xa4 (port: m_ItemHeight — names swapped vs binary semantics,
// preserved to avoid mangled-symbol drift) and recomputes LEFT/RIGHT bounds
// of both touch regions. TOP/BOTTOM indices [1]/[3] keep ctor defaults.
// Binary @ 0x001479a0: vstr.32 s0,[r0,#0xa4] then derives 4 fields:
//   +0xe0 = w*-0.5, +0xe8 = w*0.5, +0xf0 = w*-0.5*1.25, +0xf8 = w*0.5*1.25
// [0] must be negative (xMin) so Phase-2 TouchInRegion(xMin<=x<=xMax) accepts.
// ASM-verified: 2026-06-06 v1.6.1 binary @ 0x001479a0 (re-analyst) -- SetWidth: +0xe0=w*-0.5(xMin) +0xe8=w*0.5(xMax) +0xf0=w*-0.5*1.25 +0xf8=w*0.5*1.25; [1]/[3] keep ctor +-320. Region[0]=xMin must be negative so Phase-2 TouchInRegion(xMin<=x<=xMax) accepts.
// Note: port "outer"/"inner" names are functionally swapped vs binary semantics
// (+0xe0 group is the tighter ACQUIRE region; +0xf0 group is the 1.25x-wider HOLD region).
// ---------------------------------------------------------------------------
void ScrollingMenu::SetWidth(float w) {
    m_ItemHeight = w;                   // binary +0xa4
    const float HALF       = w * 0.5f;
    const float INNER_HALF = HALF * 1.25f;
    m_OuterRegion[0] = -HALF;           // binary +0xe0 = w*-0.5  (xMin)
    m_OuterRegion[2] =  HALF;           // binary +0xe8 = w*0.5   (xMax)
    m_InnerRegion[0] = -INNER_HALF;     // binary +0xf0 = w*-0.5*1.25  (xMin, hold)
    m_InnerRegion[2] =  INNER_HALF;     // binary +0xf8 = w*0.5*1.25   (xMax, hold)
    // Indices [1] (TOP) and [3] (BOTTOM) keep ctor defaults +320 / -320.
}

// ---------------------------------------------------------------------------
// ScrollingMenu::Collide @ 0x0015af4c
// Walks m_Items calling vtable[+0x38] (Slot13/hit-test, v1.6.1 slot 14) on each.
// Returns the first item that returns non-null, or nullptr.
// Only walks when m_bCollideEnabled != 0.
// ---------------------------------------------------------------------------
ScrollingMenuItem* ScrollingMenu::Collide(int touchSlot) {
    if (!m_bCollideEnabled) return nullptr;
    for (std::vector<ScrollingMenuItem*>::iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        ScrollingMenuItem* item = *it;
        ScrollingMenuItem* hit = item->Slot13(touchSlot);
        if (hit) return hit;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// v1.6.1 ScrollingMenu::Update @0x001b03b4
//
// 7-phase touch-based scrolling physics:
//   Phase 1: clear m_bTouchProcessed
//   Phase 2: touch acquire (when m_TouchId == -1)
//   Phase 3: active touch tracking (drag / release)
//   Phase 4: velocity integration + friction
//   Phase 5: per-item layout + SetOnscreen + closest-item tracking
//   Phase 6: click callback (tap release without drag)
//   Phase 7: scroll bounds + spring-back
//
// Port scroll-rate split (no binary counterpart -- see HUDControl::UpdateRealtime):
// Phases 1/2/3 (live touch read) and Phase 6 (discrete click-fire) MUST stay
// in this 60Hz Update() -- Phase 6 fires a one-shot click callback/SFX on
// settle with no debounce, and would re-fire up to 120x/s if run per-present.
// Phases 4 (velocity integrate), 5 (layout), 7 (spring-back) are pure physics
// with no discrete side effects, so under the port they move to
// UpdateRealtime() (dt-scaled via the SM_DECAY_F/SM_SPRING_F macros above),
// which runs once per PRESENTED frame instead of the fixed 60Hz sim tick.
// Phase 5 still runs here too (at f=1.0f) so Phase 6's click gate reads a
// freshly-published m_pClickTarget/m_ClosestSnapDelta for the CURRENT touch
// state every 60Hz tick; Phase 5 is idempotent (recomputes layout from
// m_Velocity, no accumulation) so running it in both places is safe.
//
// Under __bada__ this function compiles to the ORIGINAL single-pass 7-phase
// body verbatim (byte-identical ASM) -- no split, no UpdateRealtime, no new
// members exist there at all.
// ---------------------------------------------------------------------------
// ASM-verified: 2026-07-15T02:35Z v1.6.1 ScrollingMenu::Update @ 0x001b03b4 (asm-inspector)
void ScrollingMenu::Update(float /*dt*/) {
    using namespace Mortar;

    // --- Phase 1: clear tap-fired flag each frame ---
    // Binary: this->field_0xc9 = 0 at top of Update
    m_bTouchProcessed = 0;

    // --- Phase 2: touch acquire (only when no active touch) ---
    if (m_TouchId == -1) {
        // Scan outer region for a held finger.
        // Layout: [0]=LEFT, [1]=TOP, [2]=RIGHT, [3]=BOTTOM.
        // TouchInRegion param order: xMin, xMax, yMin, yMax, hint.
        const float xMin = pos.x + m_OuterRegion[0];  // LEFT
        const float yMax = pos.y + m_OuterRegion[1];  // TOP
        const float xMax = pos.x + m_OuterRegion[2];  // RIGHT
        const float yMin = pos.y + m_OuterRegion[3];  // BOTTOM
        int slot = TouchInRegion(xMin, xMax, yMin, yMax, -1);
        m_TouchId = slot;
        int touchState = IsTouchDown(slot);

        if (touchState == 2) {
            // PRESS-EDGE (state==2 = just-pressed, one frame) — valid acquire.
            // Binary: IsTouchDown(slot) == 2 fires acquire; state==1 (held) does NOT.
            // (Comment previously inverted "held"/"just-pressed" labels --
            //  re-verified 2026-05-17, binary @ 0x00169144 returns 2 for the
            //  press-edge phase float 2.0f and 1 for held phase float 1.0f.)
            ScrollingMenuItem* hitItem = Collide(slot);
            m_DragTargetIdx = -1;  // field77_0xc0 = -1

            // Latch touch anchor: copy current touch position
            const TouchState* ts = Touch::GetInstance().GetSlot(slot);
            if (ts) {
                m_TouchAnchorPos.x = (float)ts->currX;
                // Both binary and port use Y-up touch coords (binary's
                // GlesForm::TransformTouchPos at 0x0018327c bakes a Y-flip
                // into its 90 degree rotation so its result matches the
                // port's InputTranslatorSDL output direction). The drag
                // formula below works on touch deltas, so absolute units
                // (binary pixel vs port ortho) are irrelevant; only sign
                // matters and both are Y-up.
                m_TouchAnchorPos.y = (float)ts->currY;
                m_TouchAnchorPos.z = (float)ts->phase;
            }

            // Latch scroll offset at press time (copy m_Velocity into m_AnchorOffset)
            m_AnchorOffset = m_Velocity;

            m_pCollidedItem = hitItem;

            // Binary @ 0x0015b7cc: GOT[0x7740] |= 0x40 (re-analyst 2026-05-17).
            // GOT[0x7740] resolves to SlashEntity::s_ModPowerMask. Bit 0x40
            // = "ScrollingMenu drag active" -- SlashEntity::Update reads it
            // in its collision-gate to suppress fruit/bomb slicing while
            // the player is dragging this menu (prevents accidentally
            // slicing fruits behind a shop/score list).
            SlashEntity::s_ModPowerMask |= 0x40u;
        } else {
            // Not a valid held touch — discard
            m_TouchId = -1;
        }
    }

    // iVar2 = pre-Phase-3 snapshot of m_DragTargetIdx (binary saves r-reg before Phase 3 branch).
    // Phase 6 uses iVar2 == -1 as the "no drag target was active" gate for click callback.
    // DIFFERS: binary uses pos.x for Y bounds (Bada portrait-rotation); port uses pos.y.
    int iVar2 = m_DragTargetIdx;

    // --- Phase 3: active touch tracking ---
    if (m_TouchId != -1) {
        // Binary checks TouchInRegion on the inner region to decide if finger is still in.
        // DIFFERS: binary uses pos.x for Y bounds (Bada portrait-rotation); port uses pos.y.
        float px = pos.x;
        float py = pos.y;
        int stillIn = TouchInRegion(
            px + m_InnerRegion[0],   // LEFT
            px + m_InnerRegion[2],   // RIGHT
            py + m_InnerRegion[3],   // BOTTOM
            py + m_InnerRegion[1],   // TOP
            m_TouchId);              // hint

        if (stillIn != m_TouchId) {
            // --- Phase 3A: finger left inner region or was lifted ---
            if (m_pCollidedItem) {
                m_pCollidedItem->Slot14();   // vtable[+0x3C] Slot14 = touch-release (v1.6.1 slot 15)
            }

            if (!m_bDragging) {
                if (m_pCollidedItem == nullptr) {
                    // No item collided AND no drag -> closest-item search at press-time anchor
                    m_DragTargetIdx = -1;
                    float fVar20 = -m_Velocity.y;
                    int idx = 0;
                    float bestDist3A = 10000.0f;   // DAT_0015ba10
                    for (std::vector<ScrollingMenuItem*>::iterator it3 = m_Items.begin();
                         it3 != m_Items.end(); ++it3) {
                        float fVar17 = (fVar20 + pos.y + m_Height * -0.5f) - m_TouchAnchorPos.y;
                        if (fVar17 < 0.0f) fVar17 = -fVar17;
                        if (fVar17 < bestDist3A) {
                            bestDist3A = fVar17;
                            if (fVar20 + m_TotalHeight + m_Velocity.y < m_Height)
                                m_DragTargetIdx = -1;
                            else
                                m_DragTargetIdx = idx;
                        }
                        idx++;
                        float h = (*it3)->GetHeight();
                        fVar20 -= h;
                    }
                    m_PendingVelocity = _Vector3<float>(0.0f, 0.0f, 0.0f);
                }
            } else {
                // DRAGGING path: friction projection + snap distance computation
                float fVar21 = m_PendingVelocity.y;
                float fVar17 = 0.0f;
                while (fVar21 < -0.05f || fVar21 > 0.05f) {
                    fVar21 *= 0.9f;
                    fVar17 += fVar21;
                }
                bool nonTrivial = (fVar17 < -0.01f || fVar17 > 0.01f);
                if (nonTrivial) {
                    float fVar14 = m_Velocity.y;
                    float projected = fVar14 + fVar17;
                    if (projected <= 0.01f &&
                        (m_ItemHeight - m_TotalHeight) + 0.01f <= projected) {
                        float py5 = pos.y;
                        fVar14 = (py5 - fVar14) - fVar17;
                        float bestDist3Bd = 10000.0f;
                        float bestOff = 0.0f;
                        for (std::vector<ScrollingMenuItem*>::iterator it3b = m_Items.begin();
                             it3b != m_Items.end(); ++it3b) {
                            float fVar16 = fVar14 - py5;
                            float fVar19 = (fVar16 < 0.0f) ? -fVar16 : fVar16;
                            if (fVar19 < bestDist3Bd) {
                                bestDist3Bd = fVar19;
                                bestOff = fVar16;
                            }
                            float h = (*it3b)->GetHeight();
                            fVar21 -= h;
                            h = (*it3b)->GetHeight();
                            fVar14 -= h;
                        }
                        float ratio = (bestOff - m_Velocity.y) / fVar17;
                        if (ratio < 0.0f) ratio = -ratio;
                        m_SnapDist = ratio;
                    }
                }
            }

            // Release clears (run regardless of m_bDragging branch)
            m_TouchId       = -1;
            m_pCollidedItem = nullptr;
            m_bDragging     = 0;
            SlashEntity::s_ModPowerMask &= ~0x40u;

        } else {
            // --- Phase 3B: finger still in inner region -> drag velocity update ---
            m_SnapDist = 1.0f;

            const TouchState* ts = Touch::GetInstance().GetSlot(m_TouchId);
            if (ts) {
                float currentY = (float)ts->currY;
                float anchorY       = m_TouchAnchorPos.y;

#ifdef __bada__
                float anchorScrollY = m_AnchorOffset.y;
                m_PendingVelocity.y = (m_Velocity.y -
                                       (anchorScrollY - (currentY - anchorY)))
                                      * DRAG_DELTA_FACTOR;
#endif
                // Port specific: task #13 -- the m_PendingVelocity.y compute
                // above (drag-delta velocity formula) moved to UpdateRealtime
                // (below), reading GetLivePos(m_TouchId) at native present
                // rate instead of ts->currY at the fixed 60Hz tick. At THIS
                // sim-tick moment liveY == currY (the ring was just drained
                // by Touch::Update), so recomputing it here again would gain
                // nothing -- only the per-present recompute (on the extra
                // presents between sim ticks) delivers 120Hz tracking. Do
                // NOT double-compute: __bada__ keeps the original single-pass
                // compute verbatim (byte-identical ASM); the port build skips
                // it here and does it once, in UpdateRealtime.

                // Phase 3B: clear collided item if Slot13 (re-test) returns 0
                if (m_pCollidedItem) {
                    if (m_pCollidedItem->Slot13(m_TouchId) == nullptr) {
                        m_pCollidedItem = nullptr;
                    }
                }

                float delta = currentY - anchorY;
                if (delta < 0.0f) delta = -delta;
                if (delta > DRAG_THRESHOLD) {
                    if (delta > DRAG_CANCEL_DIST && m_pCollidedItem) {
                        m_pCollidedItem->Slot12();
                        m_pCollidedItem = nullptr;
                    }
                    m_bDragging = 1;
                }
            }
        }
    }

#ifdef __bada__
    // --- Phase 4: velocity integration + friction ---
    // Vec3Scale_ScrollMenu(&field_0x90)  -- apply friction to pending velocity
    Vec3Scale_ScrollMenu(&m_PendingVelocity);
    m_Velocity += m_PendingVelocity;

    // RE confirmed: binary does NOT zero m_PendingVelocity here; it relies on
    // the 0.9 friction (Vec3Scale_ScrollMenu) at end of phase to decay it
    // naturally over ~20 frames. Earlier clear suppressed scroll motion.
#endif

    // Determine visible range limits
    float rangeTop = RANGE_TOP;  // DAT_0015be04 = -160.0f
    float rangeBot = RANGE_BOT;  // DAT_0015be08 =  160.0f
    if (m_bConstrainedView) {
        rangeTop = pos.y - m_Height;  // pos.y - field60_0xa0
        rangeBot = pos.y;
    }

    // --- Phase 5: per-item position + SetOnscreen + closest-item tracking ---
    float closestDist = CLOSEST_SENTINEL;  // DAT_0015be10
    m_ClosestIdx = 0;                       // field76_0xbc reset

    // Binary local pSVar4 (mirrors the loop's per-frame drag-target snap
    // pointer). Set ONLY in the m_DragTargetIdx == i branch below -- stays
    // null on every other path (including the "no drag target" branch and
    // all non-matching iterations). Phase 6's click gate reads THIS, not
    // GetItemClosestToZero() (which always resolves once any item has been
    // focused, and is wrong for the click gate -- see Phase 6 below).
    ScrollingMenuItem* dragTargetItem = nullptr;

    // Running layout cursor (binary: _Stack_68 = pos - velocity).
    // Earlier port had `m_Velocity.y - pos.y` (sign flipped); the binary's
    // `_Vector3<float>::operator-(out, pos)` call carries m_Velocity in
    // r2 as a hidden third register and computes `out = pos - velocity`.
    // See docs/engine/coordinate-system.md.
    float curY = pos.y - m_Velocity.y;

    for (int i = 0; i < (int)m_Items.size(); i++) {
        ScrollingMenuItem* item = m_Items[(size_t)i];
        float halfH = item->GetHeight() * 0.5f;

        // Closest-item tracking (uses curY BEFORE the halfH adjustment).
        if (m_DragTargetIdx < 0) {
            // No specific drag target — find globally closest to scroll origin
            float distToCenter = curY - pos.y;
            if (distToCenter < 0.0f) distToCenter = -distToCenter;
            if (distToCenter < closestDist) {
                m_ClosestIdx = i;
                // Binary at 0x0015bcf6: vsub.f32 s16,s15,s14 where
                //   s15 = curY (_Stack_68.y), s14 = pos.y (m_pParent->pos.y).
                // Result is curY - pos.y -- the SIGNED delta needed to
                // bring this item to the focal point. Earlier port had
                // `curY - (pos.y - m_Velocity.y)` (extra velocity term),
                // which made the snap step diverge instead of converge.
                m_SnapDist = curY - pos.y;
                closestDist = distToCenter;
            }
        } else if (m_DragTargetIdx == i) {
            // This is the dragged item
            m_ClosestIdx = m_DragTargetIdx;
            // ASM-verified: 2026-07-25T00:00Z v1.6.1 ScrollingMenu::Update @0x001b098c (asm-inspector) -- drag-target arm refreshes the snap delta every frame (folds to curY-pos.y), same as the closest arm; omitting it froze the delta -> velocity ramp.
            m_SnapDist = curY - pos.y;
            closestDist = curY - pos.y;
            if (closestDist < 0.0f) closestDist = -closestDist;
            dragTargetItem = item;  // binary: pSVar4 = pSVar10
        }

        // Binary advances cursor by halfH BEFORE the Move call so the item
        // center sits halfH below the running cursor (item TOP edge aligned
        // to the cursor). Earlier port did both halfH subtractions AFTER
        // Move, which placed item centers at the cursor instead of one
        // half-height below it -- the user-visible 0.5-item-height
        // downward layout shift.
        curY -= halfH;

        // SetOnscreen: with rangeTop=-160 (small Y) and rangeBot=160 (large Y)
        // the menu uses a Y-down convention. Off-screen if item is fully
        // above the top edge (item-bottom-Y < rangeTop) or fully below the
        // bottom edge (item-top-Y > rangeBot). Item-bottom-Y = curY+halfH,
        // item-top-Y = curY-halfH.
        bool onscreen;
        if (curY + halfH < rangeTop || curY - halfH > rangeBot)
            onscreen = false;
        else
            onscreen = true;
        item->SetOnscreen(onscreen);

        // Move: assign world position from layout cursor
        item->Move(_Vector3<float>(pos.x, curY, pos.z));

        // Advance cursor by another halfH for the next iteration (binary
        // subtracts halfH twice per item: once pre-Move, once post-Move).
        curY -= halfH;
    }

#ifndef __bada__
    // Port specific: publish Phase-5 outputs for Phase 6 below (this same
    // 60Hz Update call) AND for Phase 7 in UpdateRealtime() (see bridge
    // members in ScrollingMenu.h). Copy the SIGNED values verbatim -- do not
    // re-derive or abs() them here.
    m_pClickTarget     = dragTargetItem;
    m_ClosestSnapDelta = m_SnapDist;
#endif

    // --- Phase 6: click callback (tap release without drag) ---
    // fVar18 = closest-item position (the target snap Y for closest item)
    // snapDist = fVar18 - field_0xd8 (how far we are from snapping to closest item)
    // Binary computes fVar18 from item positions; simplify: use m_SnapDist accumulated above
    float snapDist = m_SnapDist;

    if (!m_bDragging) {
        // Check if snap distance is near zero (|snapDist| < 2.0f)
        float absSnap = snapDist < 0.0f ? -snapDist : snapDist;
        if (absSnap < CLICK_SNAP_GATE) {
            // Check if pending velocity is near zero (|pending.y| < 0.5f).
            // Binary @ 0x0015bddc reads field_0x94 = m_PendingVelocity.y (the
            // per-frame drag delta), NOT field_0xd8 = m_Velocity.y (which is
            // the snapped scroll offset and can sit at -80 in equilibrium
            // for any non-top item). Matches Phase 7's snap-step gate which
            // also reads m_PendingVelocity.y.
            float vel = m_PendingVelocity.y;
            float absVel = vel < 0.0f ? -vel : vel;
            if (absVel < CLICK_VEL_GATE) {
                // Set "tap processed" flag
                m_bTouchProcessed = 1;

                // Binary gate: iVar2 == -1 means m_DragTargetIdx was -1 before Phase 3
                // (no drag target active at frame start). Binary's click target is
                // pSVar4, a LOCAL set only in Phase 5's `m_DragTargetIdx == i` branch
                // (see dragTargetItem above) -- NOT GetItemClosestToZero()/m_ClosestIdx,
                // which always resolves to a valid item once anything has been focused.
                // Using GetItemClosestToZero() here fired the click callback (and its
                // "equip-locked" SFX, which has no debounce) every settled frame instead
                // of only on the drag-target-snap edge.
                if (iVar2 == -1 && dragTargetItem) {
                    dragTargetItem->CallClickedMenuItemCallback();
                }
            }
        }
    }

#ifdef __bada__
    // --- Phase 7: scroll bounds + spring-back (binary-faithful) ---
    // Binary @ 0x0015bd7c reads +0xa0 at 0x0015bd9e (vldr.32 s13,[r4,#0xa0]):
    //   bottom = field_0xa0 - m_TotalHeight  (SetHeight field minus accumulated item heights)
    //   [bottom, 0] is the valid scroll range; bottom is NEGATIVE when content overflows.
    // ASM-verified: 2026-06-06 v1.6.1 binary @ 0x0015bd7c (asm-inspector) -- scroll clamp bottom = SetHeight-field(+0xa0) - m_TotalHeight(+0xa8); SetHeight=vtable+0x4c->+0xa0, SetWidth=vtable+0x50->+0xa4, SetItemHeight=vtable+0x54->+0x9c. Shop: SetWidth(290)/SetHeight(80)/SetItemHeight(80).
    float offset = m_Velocity.y;
    // m_Height = port name for binary +0xa0 (the SetHeight target).
    float totalScrollH = m_Height - m_TotalHeight;

    if (offset > 0.0f && m_DragTargetIdx < 0) {
        // PAST TOP: spring back toward 0
        SM_DECAY_F(offset, SPRING_BACK_COEF);
    } else if (offset < totalScrollH && m_DragTargetIdx < 0) {
        // PAST BOTTOM: spring forward toward totalScrollH
        SM_SPRING_F(offset, totalScrollH, SPRING_FWD_COEF);
    } else {
        // IN RANGE or being dragged
        if (m_TouchId != -1) return;
        float pv = m_PendingVelocity.y;
        bool gate = (pv < 0.0f) ? (pv >= VEL_NEAR_ZERO_LO) : (pv <= VEL_NEAR_ZERO_HI);
        if (!gate) return;
        m_Velocity.y = offset + snapDist * VEL_NEAR_ZERO_HI;
        return;
    }
    m_Velocity.y = offset;

    // Apply friction to pending velocity again (end-of-phase)
    // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x0015b744 Phase 7 (re-analyst)
    Vec3Scale_ScrollMenu(&m_PendingVelocity);
#endif
}

#ifndef __bada__
// ---------------------------------------------------------------------------
// Port specific: no binary counterpart -- see HUDControl::UpdateRealtime and
// the phase-split comment above Update(). Runs Phase 4 (integrate), Phase 5
// (layout, idempotent -- re-published bridge members are overwritten next
// 60Hz Update tick), and Phase 7 (spring-back) dt-scaled via SM_DECAY_F /
// SM_SPRING_F (defined above Vec3Scale_ScrollMenu) so f==1.0f exactly
// reproduces one 60Hz Update tick's worth of physics.
// ---------------------------------------------------------------------------
void ScrollingMenu::UpdateRealtime(float dtSeconds) {
    if (dtSeconds < 0.0f) dtSeconds = 0.0f;
    if (dtSeconds > 0.1f) dtSeconds = 0.1f;   // clamp across stalls/tab-switches
    const float f = dtSeconds * 60.0f;

    // --- Phase 3B (moved): per-present drag-delta velocity compute ---
    // Task #13 -- byte-copy of Update()'s Phase 3B m_PendingVelocity.y formula
    // (see the comment at that call site), but read at native present rate via
    // GetLivePos instead of once per 60Hz tick via ts->currY. Gated to
    // m_TouchId != -1 (a drag is confirmed/owns the touch) -- the SAME gate
    // Phase 7 below already uses to know a touch is live; when no touch is
    // active this no-ops and m_PendingVelocity is left as-is (a release fling
    // continues decaying via Phase 4 below, untouched).
    //
    // Bug fix (momentum runaway, small-fling-then-tap): in the binary's single-
    // pass Update @0x001b03b4, the recompute above and the Phase 4 integrate
    // below are the SAME function call, so a stationary tap's `pending =
    // (V - anchor) * -0.5` always converges against a V sampled the SAME
    // instant the anchor was latched (Phase 2, also that call). The port
    // splits recompute (here, gated on GetLivePos success) from integrate
    // (unconditional below) across presents; when the gate fails for a
    // present but a touch is still nominally owned (m_TouchId != -1), the old
    // code fell through to integrate anyway -- feeding a NOT-recomputed
    // (potentially stale, pre-tap-settle) m_PendingVelocity into m_Velocity
    // unopposed. Track whether the recompute actually ran THIS present and
    // skip the integrate when it didn't AND a touch is owned, so the pairing
    // the binary gets for free from single-pass execution is restored. This
    // does not affect the touch==-1 free-fling coast path (recompute is
    // correctly skipped there and integrate must still run every present to
    // decay the residual impulse).
    //
    // Kept after the Phase-5 m_SnapDist fix (drag-target arm refresh, see
    // above): this gate covers a DIFFERENT scenario -- GetLivePos() failing
    // (finger-lift TEvnt landed between sim ticks, flipping states1[slot].phase
    // to released) on a present where no interleaved 60Hz Update() has yet run
    // to clear m_TouchId. That is a genuine runtime race the single-pass
    // binary Update() can never hit (recompute and integrate are the same
    // function call, so GetLivePos-equivalent data is always fresh); the port's
    // cross-present split can. Verified this is still load-bearing by hand-
    // simulating test case 5 with the gate removed: GetLivePos correctly fails
    // (phase forced to 1) so recompute is skipped, but an unconditional
    // integrate would still fold the untouched (and in that test, deliberately
    // re-inflated) m_PendingVelocity into m_Velocity -- exactly the bug this
    // gate exists to prevent. Cases 3/4 never exercise the gate's skip path at
    // all (their touch stays held/phase==0 for the whole test, so GetLivePos
    // always succeeds and recomputedThisPresent is always true) -- they pass
    // with or without it; case 5 requires it.
    bool recomputedThisPresent = false;
    if (m_TouchId != -1) {
        float liveY, liveX;
        if (Mortar::Touch::GetInstance().GetLivePos(m_TouchId, liveX, liveY)) {
            float anchorY       = m_TouchAnchorPos.y;
            float anchorScrollY = m_AnchorOffset.y;
            m_PendingVelocity.y = (m_Velocity.y -
                                   (anchorScrollY - (liveY - anchorY)))
                                  * DRAG_DELTA_FACTOR;
            recomputedThisPresent = true;
        }
    }

    // --- Phase 4: velocity integration + friction (decaying IMPULSE) ---
    // pv *= powf(0.9,f); vel += pv ONCE -- do NOT multiply the add by f,
    // that would double-count the impulse already decayed above.
    bool integrateRan = (m_TouchId == -1 || recomputedThisPresent);
    if (integrateRan) {
        Vec3Scale_ScrollMenu(&m_PendingVelocity, f);
        m_Velocity += m_PendingVelocity;
    }

    // Determine visible range limits (mirrors Update()'s Phase 5 preamble)
    float rangeTop = RANGE_TOP;
    float rangeBot = RANGE_BOT;
    if (m_bConstrainedView) {
        rangeTop = pos.y - m_Height;
        rangeBot = pos.y;
    }

    // --- Phase 5: per-item position + SetOnscreen + closest-item tracking ---
    // Verbatim copy of Update()'s Phase 5 loop (idempotent -- recomputes
    // layout from m_Velocity every call, no accumulation) so it can run here
    // AND in Update() without drift. Do not re-derive the curY sign or the
    // snap delta -- see the sign-convention note in Update() above.
    float closestDist = CLOSEST_SENTINEL;
    m_ClosestIdx = 0;
    ScrollingMenuItem* dragTargetItem = nullptr;
    float curY = pos.y - m_Velocity.y;

    for (int i = 0; i < (int)m_Items.size(); i++) {
        ScrollingMenuItem* item = m_Items[(size_t)i];
        float halfH = item->GetHeight() * 0.5f;

        if (m_DragTargetIdx < 0) {
            float distToCenter = curY - pos.y;
            if (distToCenter < 0.0f) distToCenter = -distToCenter;
            if (distToCenter < closestDist) {
                m_ClosestIdx = i;
                m_SnapDist = curY - pos.y;
                closestDist = distToCenter;
            }
        } else if (m_DragTargetIdx == i) {
            m_ClosestIdx = m_DragTargetIdx;
            // ASM-verified: 2026-07-25T00:00Z v1.6.1 ScrollingMenu::Update @0x001b098c (asm-inspector) -- drag-target arm refreshes the snap delta every frame (folds to curY-pos.y), same as the closest arm; omitting it froze the delta -> velocity ramp.
            m_SnapDist = curY - pos.y;
            closestDist = curY - pos.y;
            if (closestDist < 0.0f) closestDist = -closestDist;
            dragTargetItem = item;
        }

        curY -= halfH;

        bool onscreen;
        if (curY + halfH < rangeTop || curY - halfH > rangeBot)
            onscreen = false;
        else
            onscreen = true;
        item->SetOnscreen(onscreen);

        item->Move(_Vector3<float>(pos.x, curY, pos.z));

        // Port specific: no binary counterpart. Advance item per-frame timers
        // (e.g. ShopListItem's NEW-badge bounce, selected/cost fades) exactly
        // ONCE per present with the real dtSeconds -- NOT in Update()'s Phase
        // 5 (which does not call this), so a 120Hz display doesn't double the
        // rate. See ScrollingMenuItem::AdvanceAnim / ShopListItem::AdvanceAnim.
        item->AdvanceAnim(dtSeconds);

        curY -= halfH;
    }

    // Publish bridge members (Phase 6 in the next 60Hz Update tick reads these).
    m_pClickTarget     = dragTargetItem;
    m_ClosestSnapDelta = m_SnapDist;

    float snapDist = m_SnapDist;

    // --- Phase 7: scroll bounds + spring-back (dt-scaled) ---
    float offset = m_Velocity.y;
    float totalScrollH = m_Height - m_TotalHeight;

    if (offset > 0.0f && m_DragTargetIdx < 0) {
        // PAST TOP: spring back toward 0
        SM_SPRING_F(offset, 0.0f, SPRING_BACK_COEF);
    } else if (offset < totalScrollH && m_DragTargetIdx < 0) {
        // PAST BOTTOM: spring forward toward totalScrollH
        SM_SPRING_F(offset, totalScrollH, SPRING_FWD_COEF);
    } else {
        // IN RANGE or being dragged
        if (m_TouchId != -1) return;
        float pv = m_PendingVelocity.y;
        // Threshold gate stays UNSCALED -- pv is a state member already
        // rate-consistent via the Phase-4 decay above.
        bool gate = (pv < 0.0f) ? (pv >= VEL_NEAR_ZERO_LO) : (pv <= VEL_NEAR_ZERO_HI);
        if (!gate) return;
        m_Velocity.y = offset + snapDist * (1.0f - powf(1.0f - VEL_NEAR_ZERO_HI, f));
        return;
    }
    m_Velocity.y = offset;

    // Apply friction to pending velocity again (end-of-phase)
    Vec3Scale_ScrollMenu(&m_PendingVelocity, f);
}
#endif

// ---------------------------------------------------------------------------
// ScrollingMenu::AddItem @ 0x0015be54
// ---------------------------------------------------------------------------
ScrollingMenuItem* ScrollingMenu::AddItem(ScrollingMenuItem* item) {
    if (!item) return nullptr;
    m_TotalWidth  += item->GetWidth();    // field42_0xac += vtable[+0x0c](item)
    m_TotalHeight += item->GetHeight();   // field41_0xa8 += vtable[+0x08](item)
    item->SetParent(this);
    m_Items.push_back(item);
    return item;
    // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x0015be54 (re-analyst)
}

// ---------------------------------------------------------------------------
// ScrollingMenu::RemoveItemImmediate @ 0x001af83c  (PLT thunk 0x0010e85c)
// Immediately tears down the item at `index`. Called by
// ScrollingMenuItemRemoveAnimate::Update when its shrink animation finishes.
//
// Binary field map:
//   +0xbc  m_ClosestIdx (int)
//   +0xd8  m_Velocity.y (scroll offset)
//   +0xa0  m_Height (SetHeight target / row-height field)
//   +0x94  m_PendingVelocity.y  set to DAT_001af964 = 0.1f (0x3dcccccd)
//   +0xa8  m_TotalHeight        reset to DAT_001af968 = 0.0f then re-summed
// Item teardown: scalar-deleting dtor == delete item.
// erase==false leaves the now-stale pointer in items[] (the binary only erase()s
// the vector slot when the bool arg is non-zero); the recompute loop skips
// `index`, so the dangling slot is never dereferenced.
// ---------------------------------------------------------------------------
void ScrollingMenu::RemoveItemImmediate(int index, bool erase) {
    // If we are removing the currently-focused last row, shift focus/scroll up one row.
    if (index == GetItemClosestToZeroIdx() &&
        index == (int)m_Items.size() - 1) {
        m_ClosestIdx -= 1;                  // +0xbc -= 1
        m_Velocity.y -= m_Height;           // +0xd8 -= +0xa0  (scroll offset -= row-height field)
        m_PendingVelocity.y = 0.1f;         // +0x94 = DAT_001af964 (0.1f)
    }

    // Destroy the item occupying this slot (vtable slot 1 = scalar-deleting dtor).
    ScrollingMenuItem* item = m_Items[(size_t)index];
    if (item) {
        delete item;
    }

    // Recompute total scroll height as the sum of GetHeight() over all OTHER items.
    m_TotalHeight = 0.0f;                    // +0xa8 = DAT_001af968 (0.0f)
    for (int i = 0; i < (int)m_Items.size(); i++) {
        if (i != index) {
            m_TotalHeight += m_Items[(size_t)i]->GetHeight();
        }
    }

    // The binary only shrinks the vector when erase != 0 (the RemoveAnimate
    // completion path passes 0, leaving the stale pointer in place).
    if (erase) {
        m_Items.erase(m_Items.begin() + index);
    }
}

int ScrollingMenu::GetNumItems() {
    return (int)m_Items.size();
}

int ScrollingMenu::GetItemClosestToZeroIdx() {
    return m_ClosestIdx;
}

ScrollingMenuItem* ScrollingMenu::GetItemClosestToZero() {
    int idx = GetItemClosestToZeroIdx();
    if (idx < 0 || idx >= (int)m_Items.size()) return nullptr;
    return m_Items[(size_t)idx];
}

// ---------------------------------------------------------------------------
// ScrollingMenu::Draw @ 0x0015af98
// Binary: pure iterator over m_Items calling vtable+0x2c (Draw) on each.
// No world.Reset() between items in binary.
// DIFFERS: original does not Reset world stack between items; port adds
// world.Reset() because ShopListItem's Draw leaks Scale44 into the next
// item's first draw call. Fix the leak in ShopListItem to match binary.
// ASM-verified: 2026-05-24 v1.6.1 binary @ 0x0015af98 (re-analyst)
// ---------------------------------------------------------------------------
void ScrollingMenu::Draw(float* /*hudScaleRaw*/) {
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    for (std::vector<ScrollingMenuItem*>::iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        world.Reset();
        // DIFFERS: original = no Reset here; using Reset because ShopListItem leaks Scale44
        (*it)->Draw();
    }
}

void ScrollingMenu::DestroyList() {
    SlashEntity::s_ModPowerMask &= ~0x40u;
    for (std::vector<ScrollingMenuItem*>::iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        ScrollingMenuItem* item = *it;
        item->Remove();   // vtable slot 7 (+0x1c)
        delete item;      // vtable slot 1 dtor+operator_delete
    }
    m_Items.clear();
    // Binary does NOT zero m_TotalWidth/m_TotalHeight here.
    // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x0015afd0 (re-analyst)
}

// ---- Lifecycle methods ported from binary (re-analyst 2026-05-18) ----

// Binary @ 0x001af6a8 -- ClearTouch. Drops the tracked touch slot and
// clears the drag flag. Used by callers that need to abort an in-flight
// drag without waiting for finger lift (e.g. when the menu is hidden).
void ScrollingMenu::ClearTouch() {
    m_TouchId   = -1;
    m_bDragging = 0;
}

// Binary @ 0x0015af50 -- Collide(long) is the same body as Collide(int);
// the binary exports both signatures because the type-resolver delegate
// uses long, and gameplay callers use int. Port forwards.
ScrollingMenuItem* ScrollingMenu::Collide(long touchSlot) {
    return Collide((int)touchSlot);
}

// Binary @ 0x0015af28 -- chains to vtable[+0x10] (HUDControl::Init).
// Port's HUDControl::Init is a no-op, so the body is effectively empty.
void ScrollingMenu::Init() {}

// Binary @ 0x0015af34 -- empty pass-through.
void ScrollingMenu::PreDraw(float* /*viewVec*/) {}

// Binary -- no standalone Release symbol; HUDControl3d::Release base
// runs. Port mirrors via DestroyList so item leaks don't survive the
// HUD control's removal from the HUD list.
void ScrollingMenu::Release() {
    DestroyList();
}

// Binary @ 0x0015aeb8
void ScrollingMenu::Reset() {
    m_DragTargetIdx     = -1;
    m_TouchId           = -1;
    m_pCollidedItem     = nullptr;
    // Binary writes byte 1 at HUDControl base +0x32 (m_bNoDestructor).
    m_bNoDestructor     = 1;
    m_bConstrainedView  = 0;
    m_Velocity          = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_TouchAnchorPos    = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_AnchorOffset      = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_SnapDist          = 1.0f;
    m_PendingVelocity   = _Vector3<float>(0.0f, 0.0f, 0.0f);
    // Note: binary does NOT clear m_bDragging (+0xc8) in Reset.
    // ASM-verified: 2026-05-24 v1.6.1 binary @ 0x0015aeb8 (re-analyst)
}

// Binary @ 0x0015af38 -- no-op stub (single bx lr).
void ScrollingMenu::Skip() {}

#ifndef __bada__
// Port specific: no binary counterpart -- see ScrollingMenu.h.
// Mirrors Update()'s Phase 2 touch-acquire rect test (pos + m_OuterRegion[4],
// [0]=LEFT [1]=TOP [2]=RIGHT [3]=BOTTOM) but against an arbitrary point
// instead of a live touch slot.
bool ScrollingMenu::ContainsPoint(float gx, float gy) const {
    const float xMin = pos.x + m_OuterRegion[0];
    const float yMax = pos.y + m_OuterRegion[1];
    const float xMax = pos.x + m_OuterRegion[2];
    const float yMin = pos.y + m_OuterRegion[3];
    return gx >= xMin && gx <= xMax && gy >= yMin && gy <= yMax;
}

// Port specific: no binary counterpart -- see ScrollingMenu.h.
// Phase 5's layout cursor is `curY = pos.y - m_Velocity.y`, decreasing as the
// item index increases (items are laid out downward). A LARGER m_Velocity.y
// therefore brings an EARLIER (smaller-index) item to the focal point, so
// scrolling toward LATER items (wheel-down) means DECREASING m_Velocity.y --
// hence the minus sign below. Nudging m_Velocity.y directly (not snapping it)
// lets the existing Phase 4/7 spring in Update()/UpdateRealtime() animate the
// list smoothly to the new closest item, exactly like a small fling.
void ScrollingMenu::ScrollByItems(int delta) {
    m_Velocity.y -= (float)delta * m_Width;   // m_Width = binary field_0x9c (item row height, see .h note)
}
#endif

// ASM-spec v1.6.1 DefaultClickedMenuItemCallback @0x1af5f4: identity pass-through.
ScrollingMenuItem* DefaultClickedMenuItemCallback(ScrollingMenuItem* item) {
    return item;
}
