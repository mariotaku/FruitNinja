<!-- Analysed: 2026-04-28T12:00 -->

# ShopScreen Buy/Equip Button Lifecycle

Full RE of the buy-button (`m_pBuyButton`, field `+0x84`) and equip-button
(`m_pEquipButton`, field `+0x8C`) lifecycle in `ShopScreen::Update`
(`0x0015e1f4`, 387 lines), `ShopScreen::ShrinkBuyButton` (`0x0015c4cc`),
`ShopScreen::DeletedMenuItem` (`0x0015d14c`), `ShopScreen::EquipCallback`
(`0x0015d630`), and `MenuButton::Update` (`0x0014e614`, 288 lines).

---

## 1. State Machine vs Button Creation

### Pre-amble (top of Update, every frame, before switch)

```
// 1. If no splats: m_LayerFlagsAlt = 0x40
// 2. Selection-change check (rate-limited, see §4):
if (m_pShopList && m_pShopList->GetItemClosestToZero() != m_pSelectedItem
    && g_ShopStaticBlock->m_SelCounter == 0) {
    SetSelected(m_pShopList->GetItemClosestToZero());
}
// 3. Increment mod-10 counter: g_ShopStaticBlock->m_SelCounter = (m_SelCounter+1)%10
// 4. m_LayerFlags = m_LayerFlagsAlt
```

`g_ShopStaticBlock` is the BSS block at `GOT_base + 0x451b4` (same base as
`g_bShopButtonShrinking`). The counter is at offset `+0x88` within that block.
SetSelected is called at most once every 10 Update frames.

### State 0 — Transition In

| Phase | m_pBuyButton | m_pEquipButton |
|-------|-------------|----------------|
| While alpha <= 0.999 | null | null |
| On alpha > 0.999 (single frame) | **created** | null |

Creation at alpha-threshold:
1. `SplatEntity::RemoveAllSplats()`
2. `m_TransitionAlpha = 1.0f`, `m_BuyDelay = 0.0f`, `m_State = 1`
3. If `m_pBuyButton == null`:
   - Create `MenuButton` at `(185, -105, 0)` with `QuitShopCallback`
   - Texture: `*(GameTask + 0x17c)` (GameTask's back-icon SmartPtr)
   - FruitType: `*(GameTask + GOT_DAT_0015e578)` (pre-stored int in task; gives a bomb for the back button)
   - `HUD::AddControl(game->hud, button, false)`
   - `TutorialControl::ResetTutePos(tutCtrl, button)`
   - Falls through to **LAB_0015e874** (scale step):
     - `button->m_TargetSize *= 0.825` (DAT_0015e920)
     - `button->m_pFruitPiece->scale *= 0.825`

The `m_pBuyButton` creation is **single-shot**: guarded by `if (m_pBuyButton == null)`.
No `m_bEnabled` write in state 0 — the button starts with whatever `MenuButton::Init`
sets (enabled = 1).

### State 1 — Active

| Sub-path | m_pBuyButton | m_pEquipButton |
|----------|-------------|----------------|
| `m_BuyDelay > 0` | exists (stable) | unchanged |
| `m_BuyDelay <= 0`, `!m_bTouchProcessed` | exists (stable) | **ShrinkBuyButton fires** (see §3) |
| `m_BuyDelay <= 0`, `m_bTouchProcessed && IsEquipped` | exists (stable) | not created |
| `m_BuyDelay <= 0`, `m_bTouchProcessed && !IsEquipped && m_pEquipButton==null` | exists (stable) | **created** |
| `m_BuyDelay <= 0`, `m_bTouchProcessed && !IsEquipped && m_pEquipButton!=null` | exists (stable) | unchanged (already exists) |

Full state 1 pseudocode (binary-faithful, from decompile lines 130–260):

```c
// State 1
float negDt = -dt;
if (m_BuyDelay > 0.0f) {
    m_BuyDelay -= dt;   // decrements toward zero
} else {
    // m_BuyDelay <= 0: active phase
    if (m_pShopList->m_bTouchProcessed == 0) {
        // List is settled (not being scrolled)
        ShrinkBuyButton();   // §3
    } else {
        // List is being touched/scrolled
        // Check if item is equipped-or-locked, reset tutorial arrow if so
        if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
            ItemManager* im = ItemManager::GetInstance();
            bool equipped = im->IsEquipped(m_pSelectedItem->m_pItemInfo) != 0;
            bool locked   = m_pSelectedItem->m_pItemInfo->IsLocked() != 0;
            if (equipped || locked) {
                TutorialControl::ResetTutePos(tutCtrl, null);  // hide arrow
            }
        }
        // Create equip button if item is not equipped and button doesn't exist
        if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
            ItemManager* im = ItemManager::GetInstance();
            bool equipped = im->IsEquipped(m_pSelectedItem->m_pItemInfo) != 0;
            if (!equipped) {
                if (m_pEquipButton == null) {
                    // CREATE EQUIP BUTTON (§4 for full creation spec)
                }
                // (if m_pEquipButton already exists, do nothing — no destroy+recreate)
            }
            // If equipped: goto LAB_0015e68a (skip equip button creation, go to anim frame update)
        }
    }
}
// LAB_0015e68a: update animation frame counter (float arithmetic, clamp to [0, 0x3ffc])
m_AnimFrame = clamp(m_AnimFrame + dt * ANIM_FRAME_RATE, 0.0, 16380.0);
```

Key findings:
- `m_BuyDelay` is decremented with `m_BuyDelay -= dt` ONLY when `m_BuyDelay > 0`, not
  when it is already <= 0. The `else` branch (shrink/create) only runs when already <= 0.
- **The shrink fires every frame that m_BuyDelay <= 0 and m_bTouchProcessed == 0.**
  This is not a one-shot edge trigger.
- The equip button is NOT destroyed when `IsEquipped` is detected. The binary only
  creates it when missing; if it already exists and the item becomes equipped, the
  equip button remains until ShrinkBuyButton fires (or DeletedMenuItem clears it).

### State 2 / 7 — Transition Out

- `m_TransitionAlpha *= 0.85`
- Neither button is created or destroyed here (only the alpha decays).
- When alpha < 0.01 AND state==2 AND parent exists:
  - `parent->m_bPendingRemoval = 1`; `this->m_bPendingRemoval = 1`
  - `mainScreen->SetState(8)` (only state 2, not state 7)

### State 3 — Buy Animation

- `m_TransitionAlpha *= 0.75`
- While alpha >= 0.001: if `m_pBuyButton` and its fruit piece exist, fling the fruit with
  random velocity, `TutorialControl::ResetTutePos(tute, null)`, then `m_pBuyButton = null`
  (not SetPendingRemoval — just null the pointer; the entity is already flying).
- When alpha < 0.001: state=4, alpha=0.
  - Create NEW `m_pBuyButton` at `(185,-105,0)` with QuitShopCallback.
  - Falls through **LAB_0015e874** (same 0.825 scale step as state 0).

### State 4

- `m_LayerFlagsAlt = 0x80`
- No button creation/destruction.

### States 5 / 6

- If `m_pBuyButton` and its fruit piece exist: fling, set `m_pBuyButton = null`.
- Wait for ActorManager to have zero entities of both types.
- Then `ItemManager::SetEquippedItem(type, prevSlotItem)`, state=0.

---

## 2. The m_bTouchProcessed Gate

### What is m_bTouchProcessed?

`ScrollingMenu::m_bTouchProcessed` at field `+0xc9` (1 byte).

From `ScrollingMenu::Update` (`0x0015b747`):
- Cleared to `0` at the TOP of Update every frame.
- Set to `1` when a touch TAP fires (finger lifted inside a list item that was
  tapped without dragging past the drag threshold). Specifically: when the
  active touch is released AND the scroll did not become a drag, the menu
  fires the item's click callback AND sets `m_bTouchProcessed = 1`.

So `m_bTouchProcessed` is effectively a **one-frame flag**: it is `1` exactly on
the frame the user tapped an item, and `0` on all other frames.

### The exact binary condition in Update state 1

```c
// Binary at 0x0015e438..0x0015e442:
ldr.w r3,[r4,#0x94]       // r3 = m_pShopList
ldrb.w r3,[r3,#0xc9]      // r3 = m_bTouchProcessed
cmp r3,#0x0
beq.w 0x0015e684          // if zero (not processed): jump to ShrinkBuyButton
```

`beq` to ShrinkBuyButton means: **ShrinkBuyButton fires when `m_bTouchProcessed == 0`**.

When `m_bTouchProcessed != 0` (the one-frame tap-event): the equip-button
creation branch runs instead.

### Is this a per-frame gate or an edge trigger?

It is **per-frame**: every frame where `m_BuyDelay <= 0` AND
`m_bTouchProcessed == 0`, ShrinkBuyButton is called. This fires every frame
during normal idle (no touch), and also every frame while the list is being
dragged (drag sets `m_bDragging = 1` but does NOT set `m_bTouchProcessed = 1`).

There is **no equipped→not-equipped transition detection** in the binary.
ShrinkBuyButton is called every idle frame unconditionally (subject only to
`m_BuyDelay <= 0`). ShrinkBuyButton itself is idempotent once `m_bSliced = 1`
on the fruit (it early-returns when `Fruit::Sliced()` is true — §3).

### Does it fire during scroll?

Yes. While the list is being dragged, `m_bTouchProcessed == 0` (dragging doesn't
set it; only a clean tap-release sets it). Therefore ShrinkBuyButton fires every
frame while the list is scrolling. This is correct because ShrinkBuyButton's guard
(`if (Fruit::Sliced()) return`) prevents re-slicing an already-shrinking fruit.

---

## 3. ShrinkBuyButton Semantics (0x0015c4cc)

### Full binary pseudocode

```c
void ShopScreen::ShrinkBuyButton() {
    // Guard 1: equip button must exist
    if (m_pEquipButton == null) return;

    // Guard 2: equip button's fruit piece must exist
    Fruit* fruit = m_pEquipButton->m_pFruitPiece;  // *(m_pEquipButton + 0x134)
    if (fruit == null) return;

    // Guard 3: fruit must not already be sliced (idempotent)
    if (Fruit::Sliced(fruit)) return;  // blx 0x000f954c — checks fruit->m_bSliced

    // Action: programmatic slice
    fruit->m_bSliced       = 1;          // *(fruit+0xb4) = 1
    g_bShopButtonShrinking = 1;          // *(GOT_base + 0x451b4) = 1  [static BSS byte]
    m_pEquipButton->m_bEnabled = 0;      // *(m_pEquipButton + 0x123) = 0
    fruit->m_SecondVel = *g_ShrinkVec;   // *(fruit+0xc4..0xcc) = (1.0, 1.0, 1.0)
}
```

Assembly reference: `0x0015c4da..0x0015c510`.

### What fruit does it operate on?

**The equip button's fruit piece** (`m_pEquipButton->m_pFruitPiece` at
`m_pEquipButton + 0x134`), NOT the buy button. The buy button (`m_pBuyButton`)
is untouched by ShrinkBuyButton.

### What is the static BSS bool at GOT+0x451b4?

Name: `g_bShopButtonShrinking` (a 1-byte BSS flag at `GOT_base + 0x451b4`).

- **Written to 1** by `ShrinkBuyButton` (at `0x0015c504`).
- **Written to 0** by `Update` state 1 equip-button creation (at `0x0015e60a`):
  `strb.w r8,[r5,r12,lsl#0] // r8=0, r12=0x451b4` — cleared each time the equip
  button is freshly created.
- **Read** by `EquipCallback` at `0x0015d630` (as `DAT_0015d778`): determines which
  code path EquipCallback takes (see §5).
- **Read** by `DeletedMenuItem` at `0x0015d14c` (as `DAT_0015d1f4`): gates the
  "kick the fruit off-screen" path (see §6).

This bool is NOT a per-instance field. It is a **static process-global** in the
binary's BSS. The port models it as `ShopScreen::m_bShrinking` (a per-instance
member), which is semantically equivalent since ShopScreen is a singleton.

### All early-return conditions

1. `m_pEquipButton == null` → return (no equip button to shrink)
2. `m_pEquipButton->m_pFruitPiece == null` → return (fruit piece not present)
3. `Fruit::Sliced(fruit) != 0` → return (already sliced — idempotent guard)

### What happens after ShrinkBuyButton fires?

ShrinkBuyButton does NOT call `SetPendingRemoval` or destroy the equip button.
Instead:
- `m_bSliced = 1` causes `MenuButton::Update` to detect the fruit as sliced.
- The fruit's `m_SecondVel = (1,1,1)` gives non-zero velocity so
  `|vel - m_SecondVel|^2 > 0.001` (see §5 for how MenuButton::Update detects this).
- `MenuButton::Update` then fires `EquipCallback` (via `field7_0x88` delegate).
- `m_bEnabled = 0` prevents `ClearMenuItems` from firing in MenuButton::Update.
- Eventually `m_FadeCounter` reaches 0 → `m_bPendingRemoval = 1` → HUD removes
  the equip button → `m_RemoveCallback` fires → `DeletedMenuItem` is called.

The equip button is removed **asynchronously** through the HUD's pending-removal
mechanism — not immediately on the ShrinkBuyButton call frame.

---

## 4. Equip Button Creation Gate

### Conditions for creation

From Update state 1 disassembly (`0x0015e480..0x0015e5be`):

```c
// All conditions must hold:
// 1. m_pSelectedItem != null
if (m_pSelectedItem == null) goto LAB_0015e68a;

// 2. m_pSelectedItem->m_pItemInfo != null  (field +0x278 of ShopListItem)
if (m_pSelectedItem->m_pItemInfo == null) goto LAB_0015e68a;

// 3. ItemManager::IsEquipped(selectedItem->m_pItemInfo) == 0  (not equipped)
ItemManager* im = ItemManager::GetInstance();
if (im->IsEquipped(selectedItem->m_pItemInfo) != 0) goto LAB_0015e68a;

// 4. m_pEquipButton == null  (single-shot guard)
if (m_pEquipButton != null) {
    // button already exists — no action (fall through to anim frame update)
    goto LAB_0015e68a;
}

// CREATE:
SmartPtr<Texture> tex = static_block + 0x14;  // select_item.tex (slot +0x14)
Vec3 pos(DAT_0015e564=145.0f, DAT_0015e568=104.0f, DAT_0015e558=0.0f);
int fruitType = Fruit::FruitType(static_block + DAT_0015e58c, false);
// DAT_0015e58c resolves to the string "watermelon" (GOT offset to BSS string)
m_pEquipButton = new MenuButton(tex, pos, EquipCallback, fruitType, g_MenuVec, DeletedItemDelegate);
*(m_pEquipButton + 0x123) = 0;               // m_bEnabled = 0
SetSelected(m_pSelectedItem);                 // update fruit type from item info
HUD::AddControl(game->hud, m_pEquipButton, false);
TutorialControl::ResetTutePos(tutCtrl, m_pEquipButton);
g_bShopButtonShrinking = 0;                  // *** CLEAR the static bool ***
m_pEquipButton->m_TargetSize *= 0.75;        // literal 0.75f in decompile
m_pEquipButton->m_pFruitPiece->scale *= 0.75;
Fruit::RotateFacingUp(fruit, false, (0,1,0));
```

### Is creation single-shot or per-frame?

**Single-shot**: guarded by `if (m_pEquipButton == null)`. Once created, the
creation block is never re-entered while the button exists.

### When selection changes, destroy+recreate or update in-place?

**Neither** — the binary does NOT destroy the old equip button when scrolling
to a different item. It calls `SetSelected` in the top-level pre-amble (every
10th frame) to update the fruit type displayed on the existing button.

Only when the selection change check fires (`m_pShopList->GetItemClosestToZero() !=
m_pSelectedItem`) does the binary call `SetSelected` — and even then, only in the
top-level preamble, not in the state 1 branch. In state 1, there is no per-scroll
`SetSelected` call. The button is updated lazily via the top-level rate-limited path.

The button is destroyed only when:
- `ShrinkBuyButton` fires (equip button sliced → fades out → HUD removes it → `DeletedMenuItem` nulls `m_pEquipButton`)
- ShopScreen destructor calls `Release()` which calls `SetPendingRemoval` on both buttons

**Crucially**: scrolling from a non-equipped item to another non-equipped item does
NOT destroy/recreate the equip button. The binary keeps the existing equip button
alive and updates its displayed fruit type (and texture) via `SetSelected`.

However, when `m_bTouchProcessed == 0` (idle frame), ShrinkBuyButton fires every
frame, which slices the equip button's fruit. This means the equip button is
continually shrunk when the list is idle. So the lifecycle is:

1. Item is settled (not-equipped) → ShrinkBuyButton fires every frame BUT:
   - First call: slices fruit (m_bSliced=1), sets m_SecondVel=(1,1,1), sets
     g_bShopButtonShrinking=1, disables button
   - Subsequent calls: guard `Fruit::Sliced()` returns early (idempotent)
2. Fruit fades via MenuButton::Update (FadeCounter shrinks) → HUD removes it
3. DeletedMenuItem fires → m_pEquipButton = null, m_BuyDelay += 0.05f
4. Next frame: m_bTouchProcessed==0 and m_pEquipButton==null → ShrinkBuyButton
   fires → guard `m_pEquipButton==null` → early return (no-op)
5. User taps (m_bTouchProcessed==1 for one frame): equip button is created if
   item is not equipped

---

## 5. MenuButton "User Sliced It" Auto-Equip Path

### Binary logic in MenuButton::Update (0x0014e614)

When entity is a fruit (`m_FruitType < FruitInfo_GetCount()`), the binary does:

```c
// Binary at 0x0014e74a..0x0014e7ec (fruit entity path)
byte bSliced = m_pEntity->m_bSliced;  // entity + 0xb4
if (bSliced == 0) goto GROW_IN;       // not sliced: go to grow-in animation

// Sliced path: compute relative velocity
Vec3 relVel = m_pEntity->vel - m_pEntity->m_SecondVel;  // entity[+0x1c] - entity[+0xc4]
float relVelSqMag = MagnitudeSqr(relVel);

if (relVelSqMag <= DAT_0014e978) goto AFTER_CALLBACK;  // <= 0.001: no callback yet
// relVelSqMag > 0.001: fire callback
Delegate0::operator()(&this->field7_0x88);   // fire EquipCallback / QuitShopCallback
TutorialControl::ResetTutePos(tute, 0);

// Restore entity scale from m_HitBoundsScale
m_pEntity->scale = m_HitBoundsScale;

// Post-callback: if fruit type is 0 AND main vel is zero → set m_bDrawWhole=1
// (This sets flag on pEVar5 = m_pFruitPiece after entity detach)
if (m_pFruitPiece && m_pFruitPiece->type == 0 &&
    m_pFruitPiece->vel.x == 0 && m_pFruitPiece->vel.y == 0) {
    m_pFruitPiece->m_bDrawWhole = 1;   // fruit+0x114
}

// Gate: ClearMenuItems and MainScreen::OnMenuItemsCleared only if m_bEnabled != 0
if (m_bEnabled != 0) {
    ClearMenuItems();
    if (mainScreen != null) MainScreen::OnMenuItemsCleared();
}

m_pEntity = null;  // detach entity → next frame enters shrink path

AFTER_CALLBACK:
// FadeCounter shrink (run whether or not callback fired):
```

### The binary's differentiator: m_bEnabled, not m_bDrawWhole

The binary uses **`m_bEnabled`** (at `MenuButton + 0x123`) to distinguish
"shop programmatically sliced it" from "user user-sliced it":

- **ShrinkBuyButton path** (programmatic): sets `m_bEnabled = 0` before slicing.
  MenuButton::Update fires `EquipCallback` (because `relVelSqMag > 0.001`),
  but does NOT call `ClearMenuItems` or `OnMenuItemsCleared` (because `m_bEnabled == 0`).
- **User-sliced path**: `m_bEnabled` remains 1 (set by Init). MenuButton::Update
  fires `EquipCallback` AND calls `ClearMenuItems` + `OnMenuItemsCleared`.

The binary therefore fires `EquipCallback` in BOTH cases. `ClearMenuItems` is the
action that fires ONLY for user-initiated slices.

The `m_bDrawWhole = 1` set at `0x0014e7d0` is a POST-callback action for a
specific edge case: after callback fires, if the fruit piece is type-0 fruit AND
its main vel is zero (stationary, not yet flung by physics), set `m_bDrawWhole`.
This is NOT the mechanism that distinguishes user vs programmatic — it is an
incidental write after the callback path.

### Velocity check detail

The magnitude check is on `vel - m_SecondVel` (relative velocity), not `vel`
alone. At ShrinkBuyButton time, `m_SecondVel = (1,1,1)`. The main `vel` of
the fruit is whatever gravity/physics have given it (nonzero for a live menu
fruit). `|vel - (1,1,1)|^2 > 0.001` is nearly always true. So EquipCallback
fires on the very first frame after ShrinkBuyButton — no delay.

### What EquipCallback does when g_bShopButtonShrinking == 1

```c
// EquipCallback @ 0x0015d630
if (m_pEquipButton == null) return;
if (g_bShopButtonShrinking != 0) {
    // Programmatic shrink path
    Fruit* fruit = m_pEquipButton->m_pFruitPiece;  // *(m_pEquipButton+0x134)
    if (fruit) {
        // Copy entity's current pos to m_HalfB_pos (fruit+0xb8..0xc0):
        fruit->m_HalfB_pos = fruit->pos;        // entity+0x10..0x18 → fruit+0xb8
        // Set main vel from a global "fling" Vec3 (GOT_DAT_0015d788):
        fruit->vel       = *g_ShopFlingVec;     // fruit+0x1c..0x24
        fruit->m_HalfB_vel = *g_ShopFlingVec;   // fruit+0xc4..0xcc (m_SecondVel)
        fruit->m_Gravity = *g_ShopFlingVec;     // fruit+0x9c..0xa4  <-- m_Gravity, NOT m_HalfB_vel
        // CORRECTED: fruit+0x9c is m_Gravity (Vec3), not m_HalfB_vel.
        // Writing (0,1,0) to m_Gravity inverts the downward gravity pull
        // so the sliced fruit accelerates upward/rightward off-screen
        // instead of dropping. This is what makes the watermelon fly off
        // in sync with the ring shrink in the original.
    }
    // Does NOT call ItemManager::SetEquippedItem — equip does not happen
    return;
} else {
    // User-pressed path
    m_BuyDelay = 0x3e800000 = 0.25f;   // delay before next ShrinkBuyButton can fire
    if (m_pSelectedItem && m_pSelectedItem->m_pItemInfo) {
        ItemManager::SetEquippedItem(im, itemType, itemInfo);
        // Play equip SFX based on itemType
    }
}
```

Key conclusion: when `g_bShopButtonShrinking == 1` (ShrinkBuyButton triggered the
equip button's shrink), EquipCallback **does not equip the item**. It flings the
fruit piece with `g_ShopFlingVec` and returns. This is the correct binary behavior.

---

## 6. DeletedMenuItem Hook

### Function signature and address

`ShopScreen::DeletedMenuItem(HUDControl* param_1)` @ `0x0015d14c`

This is the `m_RemoveCallback` (delegate at HUDControl `+0x38`) set on both
`m_pBuyButton` and `m_pEquipButton`. It fires when HUD removes a button from its
list (after `m_bPendingRemoval` causes the HUD to erase the control).

### Binary pseudocode

```c
void ShopScreen::DeletedMenuItem(HUDControl* param_1) {
    // Path for equip button
    if (param_1 == m_pEquipButton) {
        if (g_bShopButtonShrinking != 0) {
            // Only kick the fruit off-screen if shrink was programmatic
            Fruit* fruit = param_1->m_pFruitPiece;  // *(param_1 + 0x134)
            if (fruit) {
                fruit->m_HalfB_vel.y = -480.0f;     // *(fruit+0xbc) = 0xC3F00000 = -480.0
                fruit->m_HalfB_pos.y = -480.0f;     // *(fruit+0x14) = 0xC3F00000 = -480.0
                // Set fruit vel from g_ShopFlingVec (negated):
                fruit->m_HalfB_vel2 = -(*g_FlingVec);   // *(fruit+0x9c) = negated
                // Kick downward: second y component = -10.0 (0xC1200000), main y = -10.0
                *(fruit + 0xc8) = 0xC1200000;  // m_SecondVel.y = -10.0 (downward kick)
                *(fruit + 0x20) = 0xC1200000;  // vel.y = -10.0
            }
        }
        // Always: clear equip button pointer + add delay
        m_pEquipButton = null;
        m_BuyDelay += 0.05f;   // DAT_0015d1ec = 0x3D4CCCCD = 0.05f
    }

    // Path for buy button
    if (param_1 == m_pBuyButton) {
        m_pBuyButton = null;
        // No delay added for buy button removal
    }
}
```

Constants:
- `DAT_0015d1e8 = 0xC3F00000 = -480.0f` — the "kick below screen" Y value
- `DAT_0015d1ec = 0x3D4CCCCD = 0.05f` — the delay added back to m_BuyDelay

### Xrefs to DeletedMenuItem

`DeletedMenuItem` is registered as `m_RemoveCallback` on **both buttons** via
`Delegate1<void,HUDControl*>::operator=` set immediately after `HUD::AddControl`:

- State 0 buy button: `0x0015e3e2..0x0015e3f0` (registers via QCallee wrapper)
- State 1 equip button: `0x0015e60e..0x0015e616`
- State 3 replacement buy button: `0x0015e848..0x0015e84c`

The same callback handles both buttons by comparing `param_1` to each pointer.

### Connection to ShrinkBuyButton

The `g_bShopButtonShrinking` flag set by `ShrinkBuyButton` is read by
`DeletedMenuItem` to decide whether to kick the fruit piece off-screen. If the
fruit was removed without ShrinkBuyButton (e.g., user actually sliced the equip
button), the fruit piece is not kicked — it just disappears normally through
MenuButton's FadeCounter-to-zero path.

---

## 7. Full Lifecycle Diagram

```
ShopScreen::ctor()
    m_pBuyButton = null
    m_pEquipButton = null
    m_BuyDelay = 0.0
    m_State = 0
    g_bShopButtonShrinking = not reset (its previous value persists)

State 0 (alpha lerp):
    Every frame: alpha += (1-alpha)*0.125
    When alpha > 0.999:
        CREATE m_pBuyButton (QuitShopCallback, bomb fruit, pos 185,-105,0)
        HUD::AddControl(buyButton)
        RegisterCallback(buyButton, DeletedMenuItem)
        TutorialControl::ResetTutePos(buyButton)
        scale by 0.825
        State -> 1

State 1 (active):
    Every frame where m_BuyDelay <= 0 AND m_bTouchProcessed == 0:
        ShrinkBuyButton():
            If m_pEquipButton && fruit && !Fruit::Sliced:
                fruit->m_bSliced = 1
                g_bShopButtonShrinking = 1
                equip->m_bEnabled = 0
                fruit->m_SecondVel = (1,1,1)
                -> MenuButton::Update fires EquipCallback next frame (relVelSqMag > 0.001)
                -> EquipCallback: g_bShopButtonShrinking==1 -> fling fruit, no equip
                -> FadeCounter reaches 0 -> m_bPendingRemoval = 1
                -> HUD removes equip button
                -> DeletedMenuItem fires:
                     g_bShopButtonShrinking==1 -> kick fruit to y=-480, vel.y=-10
                     m_pEquipButton = null
                     m_BuyDelay += 0.05

    Every frame where m_BuyDelay <= 0 AND m_bTouchProcessed == 1 (one-frame tap):
        If !IsEquipped && m_pEquipButton == null:
            CREATE m_pEquipButton (EquipCallback, watermelon fruit, pos 145,104,0)
            HUD::AddControl(equipButton)
            RegisterCallback(equipButton, DeletedMenuItem)
            TutorialControl::ResetTutePos(equipButton)
            g_bShopButtonShrinking = 0   <- CLEARED HERE
            SetSelected(m_pSelectedItem) <- update fruit type
            equip->m_bEnabled = 0
            scale by 0.75

    When user slices m_pEquipButton's fruit:
        MenuButton::Update detects m_bSliced=1, relVelSqMag > 0.001
        Fires EquipCallback:
            g_bShopButtonShrinking == 0 (user path)
            m_BuyDelay = 0.25f
            ItemManager::SetEquippedItem(type, item)
            Play equip SFX
        m_bEnabled==1 -> ClearMenuItems() -> all menu fruits flung
        MainScreen::OnMenuItemsCleared()
        m_pEntity = null -> FadeCounter shrinks -> HUD removes equip button
        DeletedMenuItem:
            g_bShopButtonShrinking==0 -> NO kick
            m_pEquipButton = null
            m_BuyDelay += 0.05

QuitShopCallback (user presses buy/back button):
    m_State = 2
    Fling m_pBuyButton's fruit piece
    TutorialControl::ResetTutePos(null)

State 2 / 7 (fade-out):
    m_TransitionAlpha *= 0.85
    When < 0.01: parent->m_bPendingRemoval=1, self->m_bPendingRemoval=1 (state 2 only)

Destructor:
    Release():
        m_pBuyButton->SetPendingRemoval()
        m_pEquipButton->SetPendingRemoval()
        m_pShopList->SetPendingRemoval()
```

---

## 8. Port-Side Gaps

The following deviations from the binary exist in the current port. Each entry
gives the binary semantics, the port file/location, and what should replace it.

---

### Gap 1: m_bDrawWhole used as user-vs-programmatic sentinel

**Binary**: `m_bEnabled = 0` (set by ShrinkBuyButton) is the gate.
`MenuButton::Update` fires EquipCallback in both cases; `m_bEnabled` only gates
`ClearMenuItems`. `m_bDrawWhole` is written POST-callback for a static-fruit
edge case.

**Port (ShopScreen.cpp:434)**:
```cpp
// Port specific: ... fruit->m_bDrawWhole = true;
```
**Port (MenuButton.cpp:431-433)**:
```cpp
bool userSliced = (m_pEntity->entityType == 0) &&
                  m_pFruitPiece &&
                  !m_pFruitPiece->m_bDrawWhole;
if (!m_bRemovalPending && m_ClickCallback && userSliced) {
```

**Should be (binary-faithful)**:
- ShrinkBuyButton must NOT set `m_bDrawWhole`.
- MenuButton::Update callback gate: fire the click callback whenever
  `m_bSliced=1 AND relVelSqMag > 0.001` (no m_bDrawWhole check).
  `ClearMenuItems` and `OnMenuItemsCleared` are gated by `m_bEnabled != 0`.
- Port MenuButton field `m_bEnabled` (currently at `+0x123` in Ghidra struct)
  must be the gate for ClearMenuItems in MenuButton::Update.

---

### Gap 2: EquipCallback does not implement the g_bShopButtonShrinking branch

**Binary** (EquipCallback, 0x0015d630): when `g_bShopButtonShrinking != 0`,
copies the equip-button fruit's current pos to `m_HalfB_pos`, then sets
`vel`, `m_SecondVel`, and `m_HalfB_vel` all to `*g_ShopFlingVec`. Does NOT
call `SetEquippedItem`.

**Port (ShopScreen.cpp:545-586)**:
```cpp
// DIFFERS: flag not resolved; always take the "non-upsell" path
// TODO: resolve DAT_0015d778 flag
```
The port always equips the item.

**Should be**: implement the `g_bShopButtonShrinking` branch in EquipCallback:
- Check `m_bShrinking` (the port's per-instance mirror of the static bool).
- If true: copy fruit pos to m_HalfB_pos, set all three vel fields from a
  global kick vector, return without calling SetEquippedItem.
- If false: set m_BuyDelay = 0.25f, call SetEquippedItem, play SFX.

The `g_ShopFlingVec` (DAT_0015d788) must be resolved.

---

### Gap 3: DeletedMenuItem not implemented

**Binary**: `ShopScreen::DeletedMenuItem` @ `0x0015d14c` is the `m_RemoveCallback`
registered on BOTH buy button and equip button (via `Delegate1::operator=` after
`HUD::AddControl`). It:
- Nulls `m_pEquipButton` when equip button is removed.
- Adds `m_BuyDelay += 0.05f` when equip button is removed.
- Kicks fruit off-screen when `g_bShopButtonShrinking != 0`.
- Nulls `m_pBuyButton` when buy button is removed.

**Port**: Uses `RemoveBuyButton()` / `RemoveEquipButton()` helpers that call
`SetPendingRemoval()` on the buttons. The deletion callback is registered in
`MenuButton::Init` but the port passes a no-op lambda (or nullptr?) where
the binary would register `DeletedMenuItem`.

**Should be**: implement `ShopScreen::DeletedMenuItem(HUDControl*)` and register
it as `m_RemoveCallback` (via the `Delegate1` parameter of `MenuButton::MenuButton`
ctor, the 7th param `param_6` of the 6-arg ctor, i.e. the `deletedCb` argument in
`MenuButton::Init`) for BOTH buttons.

---

### Gap 4: ShrinkBuyButton wrongly sets m_bDrawWhole

**Binary**: ShrinkBuyButton at `0x0015c4cc` sets exactly:
- `fruit->m_bSliced = 1`
- `g_bShopButtonShrinking = 1`
- `m_pEquipButton->m_bEnabled = 0`
- `fruit->m_SecondVel = *g_ShrinkVec (= (1,1,1))`

It does NOT touch `m_bDrawWhole`.

**Port (ShopScreen.cpp:434)**:
```cpp
fruit->m_bDrawWhole = true;  // Port specific: ...
```

**Should be**: remove the `m_bDrawWhole = true` line entirely. The binary does
not write it. (After Gap 1 is fixed, the `m_bDrawWhole` sentinel is no longer
needed anywhere in ShrinkBuyButton.)

---

### Gap 5: State 1 ShrinkBuyButton gate adds port-specific conditions

**Binary** (0x0015e438..0x0015e442): the ONLY condition before calling
ShrinkBuyButton (after `m_BuyDelay <= 0`) is:
```
m_pShopList->m_bTouchProcessed == 0
```

**Port (ShopScreen.cpp:721-724)**:
```cpp
const bool shouldShrink =
    m_pShopList && !m_pShopList->m_bTouchProcessed &&
    !needCreate && centeredEquipped;
```
This adds two port-specific conditions (`!needCreate` and `centeredEquipped`)
that don't exist in the binary.

**Should be**: call ShrinkBuyButton when `m_BuyDelay <= 0 && !m_bTouchProcessed`.
No other gate. ShrinkBuyButton's own internal guards (m_pEquipButton check,
Fruit::Sliced check) handle idempotency.

---

### Gap 6: Selection change check top-level has wrong structure

**Binary** (pre-amble of Update, before switch):
1. `m_pShopList != null`
2. `m_pShopList->GetItemClosestToZero() != m_pSelectedItem` (pointer compare)
3. `g_ShopStaticBlock->m_SelCounter == 0` (mod-10 rate limiter)

All three must hold to call `SetSelected`.

Then `g_ShopStaticBlock->m_SelCounter = (m_SelCounter + 1) % 10` is computed
unconditionally (the `__aeabi_idivmod` call with modulus 10).

**Port (ShopScreen.cpp:600-609)**:
```cpp
if (m_pShopList && m_pShopList->GetItemClosestToZeroIdx() != (int)(intptr_t)m_pSelectedItem) {
    if (!m_pShopList->m_bTouchProcessed) {
        ShopListItem* newSel = ...;
        SetSelected(newSel);
    }
}
```
Port checks `m_bTouchProcessed` instead of the mod-10 counter. These have
opposite semantics (`bTouchProcessed` is 1 on tap frames, counter is 0 every
10th frame regardless of touch).

**Should be**: port the mod-10 counter (add `int m_SelCounter` to the static
block or as an instance field), increment unconditionally every frame, gate
`SetSelected` on counter==0. This mirrors the binary's rate-limiting.

NOTE: The state 1 inner selection check (`0x0015e438`) was already separated
from the top-level preamble check. The port duplicated part of it in the state 1
branch. The binary only has the preamble check for top-level `SetSelected`.

---

### Gap 7: Velocity magnitude check in MenuButton::Update uses wrong formula

**Binary (0x0014e75c-0014e76c)**:
```
relVel = entity->vel - entity->m_SecondVel  // vel[+0x1c] minus SecondVel[+0xc4]
fVar = MagnitudeSqr(relVel)
if (fVar > DAT_0014e978) → fire callback
```
Uses the **relative** velocity `vel - m_SecondVel`, not `|vel|` alone.

**Port (MenuButton.cpp:415-417)**:
```cpp
const Vec3& v = m_pEntity->vel;
const float velSq = v.x * v.x + v.y * v.y + v.z * v.z;
if (velSq > 0.001f) {
```
Uses absolute velocity magnitude.

**Should be**:
```cpp
Vec3 relVel = m_pEntity->vel - m_pFruitPiece->m_SecondVel;
float relVelSqMag = relVel.x*relVel.x + relVel.y*relVel.y + relVel.z*relVel.z;
if (relVelSqMag > 0.001f) {
```
For fruits whose `m_SecondVel` is `(0,0,0)` (normal gameplay), this is
functionally identical to the port. But for ShrinkBuyButton (which sets
`m_SecondVel = (1,1,1)`), the relative velocity is what matters.

---

### Summary table

| # | Port file | Location | Binary semantics | Port deviation |
|---|-----------|----------|-----------------|----------------|
| 1 | MenuButton.cpp | lines 431-433 | `m_bEnabled` gates ClearMenuItems (not m_bDrawWhole) | Port uses m_bDrawWhole as sentinel |
| 1b | ShopScreen.cpp | line 434 | ShrinkBuyButton does NOT write m_bDrawWhole | Port writes m_bDrawWhole in ShrinkBuyButton |
| 2 | ShopScreen.cpp | lines 545-586 | EquipCallback takes g_bShopButtonShrinking branch | Port always equips, no branch |
| 3 | ShopScreen.cpp | lines 349-366 | Both buttons have DeletedMenuItem as m_RemoveCallback | Port uses helper methods, not binary callback |
| 4 | ShopScreen.cpp | lines 721-724 | Only guard: `!m_bTouchProcessed` | Port adds needCreate + centeredEquipped guards |
| 5 | ShopScreen.cpp | lines 600-609 | SetSelected gated on mod-10 counter==0 | Port checks m_bTouchProcessed instead |
| 6 | MenuButton.cpp | lines 415-417 | Velocity check on `|vel - m_SecondVel|^2` | Port checks `|vel|^2` |

---

## Constants Referenced

| Address | Value | Name | Usage |
|---------|-------|------|-------|
| GOT+0x451b4 | BSS byte | g_bShopButtonShrinking | Set by ShrinkBuyButton, read by EquipCallback + DeletedMenuItem |
| DAT_0015c518 = 0x77cc | GOT offset | g_ShrinkVecPtr | Pointer to the (1,1,1) Vec3 used in ShrinkBuyButton |
| DAT_0015d1ec = 0x3D4CCCCD | 0.05f | EQUIP_BTN_DELETED_DELAY | m_BuyDelay += 0.05 in DeletedMenuItem |
| DAT_0015d1e8 = 0xC3F00000 | -480.0f | KICK_OFFSCREEN_Y | Y value for off-screen kick in DeletedMenuItem |
| DAT_0015d778 = 0x451b4 | (same as g_bShopButtonShrinking) | same | EquipCallback reads the same static bool |
| DAT_0015d788 | GOT offset | g_ShopFlingVec | Fling velocity Vec3 in EquipCallback's programmatic path |
| DAT_0014e978 = 0x3a83126f | 0.001f | VEL_CALLBACK_THRESHOLD | MenuButton::Update velocity threshold |
| 0.3e800000 | 0.25f | EQUIP_DELAY | m_BuyDelay = 0.25f in EquipCallback user path |

---

## Function Addresses for Reference

| Function | Address |
|----------|---------|
| ShopScreen::Update | 0x0015e1f4 |
| ShopScreen::ShrinkBuyButton | 0x0015c4cc |
| ShopScreen::SetSelected | 0x0015c870 |
| ShopScreen::DeletedMenuItem | 0x0015d14c |
| ShopScreen::EquipCallback | 0x0015d630 |
| ShopScreen::QuitShopCallback | 0x0015d55c |
| ShopScreen::ClickedOnShopItem | 0x0015d4b4 |
| MenuButton::Update | 0x0014e614 |
| MenuButton::Init | 0x0014ee40 |
| MenuButton ctor (SmartPtr, Vec3, ...) | 0x0014f24c |
