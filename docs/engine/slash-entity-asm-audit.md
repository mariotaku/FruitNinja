# SlashEntity ASM audit — blade-trail "stuck after release" bug

**Symptom:** the white blade trail (`blade.tex`) sometimes lingers on screen at
full alpha for noticeably longer than the intended ~0.25 s after the user
finishes a slash gesture. Should fade out cleanly.

**Files:** `src/entities/SlashEntity.cpp`, `src/entities/SlashEntity.h`.

**Binary entry points referenced:** `SlashEntity::Update` @ 0x17D664,
`UpdateTouchDown` @ 0x17D2E4, `UpdatePoints` @ 0x17B92C, `DrawSlice` @ 0x17E424,
`TouchDown` @ 0x17D61C.

---

## 1. Lifecycle in the binary (Ghidra + objdump verified)

The binary tracks blade activity in `SlashEntity::field_0x144` (1-byte state).
It is **not** a stable "touch is down" boolean; it is a 2-frame countdown.

* `UpdateTouchDown` @ 0x17D2E4 ends with `field_0x144 |= 1` (LAB_0017d5d4).
* `DrawSlice` @ 0x17E424 begins with the state shift below (verified via
  `objdump -d FruitNinja.exe` at runtime address `0x16e428`):

```
16e428:  ldrb.w  r3, [r0, #0x144]   ; r3 = state
16e432:  cbz     r3, ...            ; if 0 -> skip clear
16e434:  lsls    r3, r3, #1         ; r3 <<= 1
16e436:  and.w   r3, r3, #2         ; r3 &= 2
16e43a:  strb.w  r3, [r0, #0x144]   ; write back
```

So each Draw frame the state evolves `1 -> 2 -> 0`. While the finger is held,
`UpdateTouchDown` re-OR's bit 0 every frame, keeping the value oscillating
between 1 and 3 (never reaching 0). On release, no more `|= 1`, so `DrawSlice`
shifts it down to 0 within at most 2 frames.

`UpdatePoints` @ 0x17B92C reads `field_0x144` to decide whether to render the
trail. Independently, every frame it grows each pair's perpendicular length by
a `dt`-scaled step and **drops pairs whose grown length exceeds
`m_Scale * 9.0`**. After release, `m_Scale` decays at `-2 * dt` per frame
(0x16e3a4 region), so the threshold collapses and **all surviving pairs fall
out together** within ~0.5 s.

## 2. Port lifecycle (current)

`SlashEntity::Update` polls `Mortar::Touch` slot 0:

```
phase <= 0  -> OnTouchActive(currX, currY)
phase >= 1  AND m_bHasHead  -> OnTouchReleased()  (m_State 1 -> 2)
```

* `OnTouchActive` calls `AddPoint(newPos, dir)` whose `p.age = 0.0f` sets the
  newest point fresh.
* Aging happens unconditionally: `m_Points[i].age += dt`.
* Drop loop expires points whose `age >= TRAIL_LIFETIME` (0.25 s) from the
  oldest end.
* State collapse: `m_State == 2 && m_NumPoints == 0 -> m_State = 0`.

The touch-release transition itself **is detected correctly** (the `else if
(m_bHasHead)` branch). The state machine is structurally sound.

## 3. Diagnosis — why the trail "sticks"

The structure is right, but the per-point age model produces a
**non-uniform fade** that the binary never produces.

At touch release:
* Tail (oldest) point has age close to `TRAIL_LIFETIME` (0.25 s) -> drops
  almost immediately, as expected.
* **Head (newest) point has `age = dt` (about 0.017 s)** because it was added
  this frame in `OnTouchActive`.

Result: the head, which is the **brightest part of the trail** (full alpha,
widest miter, on top of the texture), takes the full `TRAIL_LIFETIME` to fade
out. Visually this is a bright white blade tip frozen in place for 0.25 s
after the finger lifts. The user perceives it as "stuck" because:

1. Tail has already vanished (so the "sweeping" motion stopped),
2. but the head sits there at alpha=255 until its own age catches up.

The binary doesn't show this because `m_Scale` decays globally, dropping the
pair-survival threshold uniformly. All pairs vanish together, not tail-first.

This also matches the user's "sometimes" -- it is most visible when the swipe
ends abruptly with high recent point density (fast flick that stops dead).
A swipe that decelerates naturally already has older head-age and fades fine.

## 4. Fix

Lay a graded "minimum age" gradient across the whole trail at the moment of
release. Tail expires next frame; head expires `TRAIL_LIFETIME` later;
intermediate points expire linearly between. Existing per-point ages are kept
if they are already older (don't go backwards). Net effect: the entire trail
drains uniformly within `TRAIL_LIFETIME` of release, matching the binary's
collapse-via-`m_Scale` behaviour close enough that the visual is correct.

This is contained to `OnTouchReleased`; no struct changes, no Update
re-architecture, no impact on the active-swipe path.

```cpp
void SlashEntity::OnTouchReleased() {
    if (m_State == 1) m_State = 2;
    m_bHasHead = false;

    // Force a uniform drain: the head's age is ~0 at this point because it
    // was added this frame, so without this push it stays at full alpha
    // for the entire TRAIL_LIFETIME after release -- visible as a stuck
    // bright tip. Matches binary's m_Scale collapse @ 0x16e3a4 which
    // shrinks the per-pair drop threshold so all pairs fall out together.
    if (m_NumPoints > 1) {
        const float invN = 1.0f / (float)(m_NumPoints - 1);
        for (int i = 0; i < m_NumPoints; ++i) {
            // t = 1 at tail (oldest), 0 at head (newest)
            const float t = (float)((m_NumPoints - 1) - i) * invN;
            const float minAge = TRAIL_LIFETIME * t;
            if (m_Points[i].age < minAge) m_Points[i].age = minAge;
        }
    }
}
```

## 5. Regression check (active-swipe behaviour)

The fix only writes ages inside `OnTouchReleased`, which is called exactly
once on the `phase>=1 AND m_bHasHead` edge. During a continuous swipe
(`phase<=0`, OnTouchActive path) nothing in the trail-aging behaviour
changes -- new points still get `age = 0` from AddPoint, the per-frame loop
ages everyone uniformly by dt, and the drop loop trims the tail. So a live
swipe still appears at full intensity tip-to-tail.

The state collapse `m_State == 2 && m_NumPoints == 0 -> 0` already runs
each frame and is unaffected.

## 6. Verdict

**Diverges** at the post-release fade shape. The binary collapses the
geometry uniformly via a global scale; the port collapses it per-point via
age, which leaves the youngest (head) point lingering. Three-line fix in
`OnTouchReleased` brings the visual in line.

No new struct fields, no Update reordering. No comment stamp on
`OnTouchReleased` -- the verdict is *Diverges*, so the implementer applies
the fix and earns the comment line on the next clean ASM diff.
