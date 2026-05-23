// Analysed: 2026-04-25T21:45
//
// ScrollingMenu full Update implementation.
// Binary: ctor 0x0015b3b0, Update 0x0015b747 (377 lines), AddItem 0x0015be54.
// Touch physics: drag detection, velocity integration, spring-back, per-item layout.

#include "ScrollingMenu.h"
#include "ScrollingMenuItem.h"
#include "entities/SlashEntity.h"
#include "engine/input/Touch.h"
#include "render/MatrixManager.h"
#include "render/MatrixStack.h"
#include <cstddef>
#include <cmath>
#include <cstdio>

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
// Vec3Scale_ScrollMenu @ 0x0015b714
// Multiplies all 3 components of a Vec3 by SCROLL_FRICTION (0.9).
// Matches the binary helper called twice in Update.
// ---------------------------------------------------------------------------
static void Vec3Scale_ScrollMenu(Vec3* v) {
    v->x *= SCROLL_FRICTION;
    v->y *= SCROLL_FRICTION;
    v->z *= SCROLL_FRICTION;
}

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
    , m_fieldCA(1)
    , m_fieldCB(0)
    , m_pCollidedItem(nullptr)
    , m_bConstrainedView(0)
    , m_pad_d1{0, 0, 0}
    , m_Velocity(0.0f, 0.0f, 0.0f)
{
    // Binary ctor helper @ 0x0015b2bc writes { -120, +320, +120, -320 } for both
    // outer and inner: [0]=LEFT, [1]=TOP, [2]=RIGHT, [3]=BOTTOM.
    // ASM-verified: 2026-05-24 binary @ 0x0015b3b0 ctor (asm-inspector)
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
// ASM-verified: 2026-05-24 binary @ 0x001479a0 (asm-inspector)
// ---------------------------------------------------------------------------
void ScrollingMenu::SetWidth(float w) {
    m_ItemHeight = w;
    const float HALF       = w * 0.5f;
    const float INNER_HALF = w * 0.625f;
    m_OuterRegion[0] = -HALF;        // LEFT
    m_OuterRegion[2] =  HALF;        // RIGHT
    m_InnerRegion[0] = -INNER_HALF;  // LEFT
    m_InnerRegion[2] =  INNER_HALF;  // RIGHT
    // Indices [1] (TOP) and [3] (BOTTOM) keep ctor defaults +320 / -320.
}

// ---------------------------------------------------------------------------
// ScrollingMenu::Collide @ 0x0015af4c
// Walks m_Items calling vtable[+0x34] (Slot13) on each.
// Returns the first item that returns non-null, or nullptr.
// Only walks when m_fieldCA != 0.
// ---------------------------------------------------------------------------
ScrollingMenuItem* ScrollingMenu::Collide(int touchSlot) {
    if (!m_fieldCA) return nullptr;
    for (std::vector<ScrollingMenuItem*>::iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        ScrollingMenuItem* item = *it;
        ScrollingMenuItem* hit = item->Slot13(touchSlot);
        if (hit) return hit;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// ScrollingMenu::Update @ 0x0015b747
//
// 7-phase touch-based scrolling physics:
//   Phase 1: clear m_bTouchProcessed
//   Phase 2: touch acquire (when m_TouchId == -1)
//   Phase 3: active touch tracking (drag / release)
//   Phase 4: velocity integration + friction
//   Phase 5: per-item layout + SetOnscreen + closest-item tracking
//   Phase 6: click callback (tap release without drag)
//   Phase 7: scroll bounds + spring-back
// ---------------------------------------------------------------------------
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
                m_pCollidedItem->Slot14();   // vtable[+0x38] Slot14 = touch-release
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
                    m_PendingVelocity = Vec3(0.0f, 0.0f, 0.0f);
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
                float anchorScrollY = m_AnchorOffset.y;

                m_PendingVelocity.y = (m_Velocity.y -
                                       (anchorScrollY - (currentY - anchorY)))
                                      * DRAG_DELTA_FACTOR;

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

    // --- Phase 4: velocity integration + friction ---
    // Vec3Scale_ScrollMenu(&field_0x90)  -- apply friction to pending velocity
    Vec3Scale_ScrollMenu(&m_PendingVelocity);

    // field_0xd4 += field_0x90  (m_Velocity += friction-scaled pending)
    m_Velocity += m_PendingVelocity;

    // RE confirmed: binary does NOT zero m_PendingVelocity here; it relies on
    // the 0.9 friction (Vec3Scale_ScrollMenu) at end of phase to decay it
    // naturally over ~20 frames. Earlier clear suppressed scroll motion.

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

    // Running layout cursor (binary: _Stack_68 = pos - velocity).
    // Earlier port had `m_Velocity.y - pos.y` (sign flipped); the binary's
    // `_Vector3<float>::operator-(out, pos)` call carries m_Velocity in
    // r2 as a hidden third register and computes `out = pos - velocity`.
    // See docs/systems/y-axis-convention.md Section 1.
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
            closestDist = curY - pos.y;
            if (closestDist < 0.0f) closestDist = -closestDist;
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
        item->Move(pos.x, curY, pos.z);

        // Advance cursor by another halfH for the next iteration (binary
        // subtracts halfH twice per item: once pre-Move, once post-Move).
        curY -= halfH;
    }

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
                // (no drag target active at frame start). Port uses GetItemClosestToZero()
                // in place of binary's latched this_00 pointer (equivalent when closest
                // search correctly updates m_ClosestIdx, which it does).
                ScrollingMenuItem* closest = GetItemClosestToZero();
                if (iVar2 == -1 && closest) {
                    closest->CallClickedMenuItemCallback();
                }
            }
        }
    }

    // --- Phase 7: scroll bounds + spring-back (binary-faithful) ---
    // Binary convention (RE-confirmed via docs/systems/y-axis-convention.md):
    //   offset > 0                   -> past TOP, spring to 0
    //   offset in [totalScrollH, 0]  -> valid scroll range
    //   offset < totalScrollH        -> past BOTTOM, spring to totalScrollH
    //   totalScrollH = m_Height - m_TotalHeight (NEGATIVE when content > viewport)
    float offset = m_Velocity.y;
    // Binary field38_0x9c (port: m_Width — name-swap per SetWidth comment) minus
    // field41_0xa8 (m_TotalHeight after the +0xa8/+0xac rename). Binary uses +0x9c,
    // NOT +0xa0 (m_Height). No clamp — binary lets totalScrollH>0 fall through naturally.
    float totalScrollH = m_Width - m_TotalHeight;

    if (offset > 0.0f && m_DragTargetIdx < 0) {
        // PAST TOP: spring back toward 0
        offset *= SPRING_BACK_COEF;
    } else if (offset < totalScrollH && m_DragTargetIdx < 0) {
        // PAST BOTTOM: spring forward toward totalScrollH
        offset = offset + (totalScrollH - offset) * SPRING_FWD_COEF;
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
    // ASM-verified: 2026-05-24 binary @ 0x0015b744 Phase 7 (re-analyst)
    Vec3Scale_ScrollMenu(&m_PendingVelocity);
}

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
    // ASM-verified: 2026-05-24 binary @ 0x0015be54 (re-analyst)
}

int ScrollingMenu::GetNumItems() const {
    return (int)m_Items.size();
}

int ScrollingMenu::GetItemClosestToZeroIdx() const {
    return m_ClosestIdx;
}

ScrollingMenuItem* ScrollingMenu::GetItemClosestToZero() const {
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
// ASM-verified: 2026-05-24 binary @ 0x0015af98 (re-analyst)
// ---------------------------------------------------------------------------
void ScrollingMenu::Draw(const Vec3& /*hudScale*/, int /*layerMask*/) {
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
    // ASM-verified: 2026-05-24 binary @ 0x0015afd0 (re-analyst)
}

// ---- Lifecycle methods ported from binary (re-analyst 2026-05-18) ----

// Binary @ 0x0015af3c -- ClearTouch. Drops the tracked touch slot and
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
    m_Velocity          = Vec3(0.0f, 0.0f, 0.0f);
    m_TouchAnchorPos    = Vec3(0.0f, 0.0f, 0.0f);
    m_AnchorOffset      = Vec3(0.0f, 0.0f, 0.0f);
    m_SnapDist          = 1.0f;
    m_PendingVelocity   = Vec3(0.0f, 0.0f, 0.0f);
    // Note: binary does NOT clear m_bDragging (+0xc8) in Reset.
    // ASM-verified: 2026-05-24 binary @ 0x0015aeb8 (re-analyst)
}

// Binary @ 0x0015af38 -- no-op stub (single bx lr).
void ScrollingMenu::Skip() {}
