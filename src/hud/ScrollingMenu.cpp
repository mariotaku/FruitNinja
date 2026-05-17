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
    , m_TotalWidth(0.0f)
    , m_TotalHeight(0.0f)
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
    // Touch region bounds. Binary helper SetVec3_ScrollMenu(p1, p2, out) writes
    //   out = { p1, 320.0f, p2, -320.0f }   (DAT_0015b2d8/dc)
    // Outer = SetVec3_ScrollMenu(-120, +120); Inner = same call -> identical region.
    // Bada touch coords are 90-degree-rotated, so binary's "all pos.x" encoding
    // corresponds to (xMin/xMax, yMin/yMax) split in unrotated desktop coords.
    // The port stores axes already separated in [xMin, yMin, yMax, xMax] form.
    // Touch region. Binary @ 0x0015b3b0 (asm-inspector) calls
    // SetVec3_ScrollMenu(-120, +120) for both outer and inner, writing
    // `{ -120, +320, +120, -320 }` in Bada-rotated coords. Port reads
    // indices as [xMin, yMin, yMax, xMax] in unrotated landscape coords;
    // the magnitudes below preserve the original port mapping that
    // worked for the list strip.
    //
    // Earlier port commit widened m_OuterRegion[3] to +400 ("Port
    // specific: cover dialog-box / equip-button column"), which let
    // touches on the right panel trigger list scroll. Reverted: the
    // outer region must NOT extend into the dialog-box / buy-equip
    // column. Right panel taps now route to their own buttons.
    const float HALF_ITEM = m_ItemHeight * 0.5f;   // 120.0f
    m_OuterRegion[0] = -m_Width  * 0.5f;  // xMin = -160
    m_OuterRegion[1] = -HALF_ITEM;        // yMin = -120
    m_OuterRegion[2] =  HALF_ITEM;        // yMax = +120
    m_OuterRegion[3] =  m_Width  * 0.5f;  // xMax = +160 (was +400 — DROPPED widen)

    m_InnerRegion[0] = -m_Width  * 0.5f;  // -160
    m_InnerRegion[1] = -HALF_ITEM * 0.5f; // -60
    m_InnerRegion[2] =  HALF_ITEM * 0.5f; // +60
    m_InnerRegion[3] =  m_Width  * 0.5f;  // +160
}

ScrollingMenu::~ScrollingMenu() {
    DestroyList();
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
        // Scan outer region for a held finger
        // Binary: TouchInRegion(pos.x + field100, pos.x + field102,
        //                       pos.x + field103, pos.x + field101, -1)
        // Outer region: [0]=xMin_rel, [1]=yMin_rel, [2]=yMax_rel, [3]=xMax_rel
        // Param order for TouchInRegion: x0, x1, y0, y1, hint
        int slot = TouchInRegion(
            pos.x + m_OuterRegion[0],  // x0 = pos.x + xMin
            pos.x + m_OuterRegion[3],  // x1 = pos.x + xMax
            pos.y + m_OuterRegion[1],  // y0 = pos.y + yMin
            pos.y + m_OuterRegion[2],  // y1 = pos.y + yMax
            -1);
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

    // --- Phase 3: active touch tracking ---
    int iVar2 = -1; // tracks IsTouchDown result for release detection (used in Phase 6)
    if (m_TouchId != -1) {
        // Once a touch has been acquired, keep tracking until the finger
        // is RELEASED (IsTouchDown == 0). The inner-region check used to
        // gate this, but the binary's check is "still touched", and the
        // inner region is too small for the visible list area on this
        // ortho — drags outside ±60 vertical of the menu's pos.y were
        // releasing instantly, springing the scroll right back.
        int touchState = IsTouchDown(m_TouchId);
        bool stillTouched = (touchState != 0);
        // stillIn keeps the diagnostic name; "treat as in-region" iff still down.
        int stillIn = stillTouched ? m_TouchId : -1;

        if (stillIn != m_TouchId) {
            // --- Phase 3A: finger left inner region or was lifted ---
            if (m_pCollidedItem) {
                // Fire vtable[+0x38] (Slot14 = touch-release signal) on collided item
                m_pCollidedItem->Slot14();
            }

            if (!m_bDragging && m_pCollidedItem == nullptr) {
                // No drag occurred AND no item tracked -> snap to closest
                m_DragTargetIdx = -1;  // field77_0xc0 = -1

                // Binary: accumulate closest item by distance
                // (full closest-item search; simplified here to -1 as no-drag snap)
                // The actual closest-item tracking happens in Phase 5.
            }

            // Release the touch slot.
            // Binary @ 0x0015baa0..0x0015babe clears in this exact order:
            //   field_0x74 (m_TouchId)      = -1
            //   field_0xcc (m_pCollidedItem) = 0
            //   field_0xc8 (m_bDragging)    = 0
            //   GOT[0x7740] (s_ModPowerMask) &= ~0x40 -- re-enable slicing
            iVar2 = IsTouchDown(m_TouchId);
            m_TouchId       = -1;
            m_pCollidedItem = nullptr;
            m_bDragging     = 0;
            SlashEntity::s_ModPowerMask &= ~0x40u;

        } else {
            // --- Phase 3B: finger still in inner region -> drag velocity update ---

            // Reset snap-distance accumulator during active drag (field78_0xc4)
            m_SnapDist = 1.0f;

            // Compute new scroll offset from drag delta:
            // offset = (anchorScrollY - (currentY - anchorY)) * -0.5
            const TouchState* ts = Touch::GetInstance().GetSlot(m_TouchId);
            if (ts) {
                float currentY = (float)ts->currY;
                float anchorY  = m_TouchAnchorPos.y;   // y at finger-down
                float anchorScrollY = m_AnchorOffset.y; // scroll offset at finger-down

                // Binary formula (RE-confirmed via shop-scroll-debug.md, line ~263):
                //   new_offset = (m_Velocity.y - (anchorScrollY - (currentY - anchorY))) * -0.5
                float newOffset = (m_Velocity.y -
                                   (anchorScrollY - (currentY - anchorY)))
                                  * DRAG_DELTA_FACTOR;
                m_PendingVelocity.y = newOffset;

                // Handle collided item: check if it was lifted mid-drag
                if (m_pCollidedItem) {
                    // Binary: if IsTouchDown(item_tracked_slot) == 0: clear collided item
                    // Port: check if touch is still down on the tracked slot
                    // (m_TouchId is the slot; we already checked it's still in region)
                    // Just check if the global IsTouchDown is 0 for the slot
                    if (IsTouchDown(m_TouchId) == 0) {
                        m_pCollidedItem = nullptr;
                    }
                }

                // Drag threshold detection
                float delta = currentY - anchorY;
                if (delta < 0.0f) delta = -delta; // fabsf
                if (delta > DRAG_THRESHOLD) {
                    m_bDragging = 1;

                    // Cancel pending tap if drag is large
                    if (delta > DRAG_CANCEL_DIST && m_pCollidedItem) {
                        m_pCollidedItem->Slot12();  // vtable[+0x30]: cancel-tap signal
                        m_pCollidedItem = nullptr;
                    }
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

                // Fire callback only if touch was fully released (iVar2 == -1 from IsTouchDown)
                // Binary: iVar2 == -1 means touch was released during this frame's tracking
                // AND a collided item pointer exists (or closest item is tracked)
                ScrollingMenuItem* closest = GetItemClosestToZero();
                if (iVar2 == -1 && closest) {
                    // ScrollingMenuItem::CallClickedMenuItemCallback @ 0x0015c27c
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
    float totalScrollH = m_Height - m_TotalHeight;
    if (totalScrollH > 0.0f) totalScrollH = 0.0f;  // content shorter than viewport

    if (offset <= 0.0f || m_DragTargetIdx >= 0) {
        if (offset >= totalScrollH || m_DragTargetIdx >= 0) {
            if (m_TouchId != -1) return;
            // Binary at 0x0015bddc: vldr.32 s14,[r4,#0x94] -- gates the
            // snap step on m_PendingVelocity.y, NOT m_Velocity.y. After a
            // drag release, m_PendingVelocity decays to ~0.05 within a few
            // frames (0.9 friction), while m_Velocity may still be 50+
            // from accumulated drag input. Using m_Velocity here meant
            // the snap was almost never gated open. Reading m_PendingVelocity
            // matches the binary and lets snap fire on release.
            float vel = m_PendingVelocity.y;
            bool velSmall = (vel < 0.0f) ? (vel >= VEL_NEAR_ZERO_LO)
                                          : (vel <  VEL_NEAR_ZERO_HI);
            if (!velSmall) return;
            m_Velocity.y = offset + snapDist * VEL_NEAR_ZERO_HI;
            return;
        }
        // offset < totalScrollH -> past bottom, spring toward totalScrollH
        m_Velocity.y = offset + (totalScrollH - offset) * SPRING_FWD_COEF;
    } else {
        // offset > 0 -> past top, spring to 0
        m_Velocity.y = offset * SPRING_BACK_COEF;
    }

    // Apply friction to pending velocity again (end-of-phase)
    Vec3Scale_ScrollMenu(&m_PendingVelocity);
}

// ---------------------------------------------------------------------------
// ScrollingMenu::AddItem @ 0x0015be54
// ---------------------------------------------------------------------------
ScrollingMenuItem* ScrollingMenu::AddItem(ScrollingMenuItem* item) {
    if (!item) return nullptr;
    m_TotalHeight += item->GetHeight();
    m_TotalWidth  += item->GetWidth();
    item->SetParent(this);
    m_Items.push_back(item);
    return item;
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
// Pure iterator -- calls Draw() on every item in m_Items.
// No scissor, no clipping, no per-item position update.
//
// Per-item matrix discipline: reset the world matrix between items so
// each item's Draw inherits a clean identity. Mirrors HUD::Draw's
// per-control reset; without it, a previous item's dirty matrix
// (e.g. ShopListItem's Part 5 icon SetCurrentMatrix(Scale44(64,64,0)))
// bleeds into the next item's first draw call. Binary equivalent
// behaviour comes from the matrix-stack discipline shared with HUD.
// ---------------------------------------------------------------------------
void ScrollingMenu::Draw(const Vec3& /*hudScale*/, int /*layerMask*/) {
    MatrixStack& world = MatrixManager::GetInstance().GetWorldStack();
    for (std::vector<ScrollingMenuItem*>::iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        world.Reset();
        (*it)->Draw();
    }
}

void ScrollingMenu::DestroyList() {
    // Binary @ 0x0015afea -- safety clear of the slice-suppression bit so
    // a menu destroyed mid-drag doesn't leave slicing disabled forever.
    SlashEntity::s_ModPowerMask &= ~0x40u;
    for (std::vector<ScrollingMenuItem*>::iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        delete *it;
    }
    m_Items.clear();
    m_TotalHeight = 0.0f;
    m_TotalWidth  = 0.0f;
}

// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
// STUB: ScrollingMenu::ClearTouch -- auto stub
void ScrollingMenu::ClearTouch() {}
// ---- end AUTO-STUB MERGE ----

// ---- STUBS (binary) ----

// STUB: ScrollingMenu::Collide(long) -- binary @ 0x???? (TODO RE)
ScrollingMenuItem* ScrollingMenu::Collide(long touchSlot) {
    return Collide((int)touchSlot);
}

// STUB: ScrollingMenu::Init -- binary @ 0x???? (TODO RE)
void ScrollingMenu::Init() {}

// STUB: ScrollingMenu::PreDraw(float*) -- binary @ 0x???? (TODO RE)
void ScrollingMenu::PreDraw(float* /*viewVec*/) {}

// STUB: ScrollingMenu::Release -- binary @ 0x???? (TODO RE)
void ScrollingMenu::Release() {}

// STUB: ScrollingMenu::Reset -- binary @ 0x???? (TODO RE)
void ScrollingMenu::Reset() {}

// STUB: ScrollingMenu::Skip -- binary @ 0x???? (TODO RE)
void ScrollingMenu::Skip() {}

// ---- end STUBS ----
