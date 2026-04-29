# `FN::ClearMenuItems` (0x0016ac7c)

<!-- RE'd: 2026-04-29 from binary @ 0x0016ac7c..0x0016ad9a -->

Programmatic "release every menu fruit/bomb" routine. Fired by
`MenuButton::Update`'s released branch when the user slices any one
menu fruit; it cascade-releases all sibling menu items so that the
remaining buttons can shrink and disappear together.

## Caller

Called from exactly one site:
- `MenuButton::Update` @ `0x0014e7da` (gated on `m_bEnabled != 0`).

After the call, `MainScreen::OnMenuItemsCleared` is also fired (gated
on a non-null callback at `Game.field+0x160`).

## Structure

Two back-to-back iterator loops over `ActorManager::GetEntityFirst /
GetEntityNext`:
- **Pass 1**: type 0 = Fruit
- **Pass 2**: type 1 = Bomb

```c
void FN::ClearMenuItems(void) {
    list_iterator<Entity*> it;
    ActorManager* am;
    Entity*  e;
    Bomb*    b;
    Vec3     v;

    // --- Pass 1: fruits (type 0) ---
    am = ActorManager::GetInstance();
    e  = am->GetEntityFirst(0, &it);
    while (e) {
        if (e->m_bSliced == 0) {                     // +0xb4
            e->m_bSliced = 1;                         // SET FIRST (binary order)
            float vx = RandFloat_Scaled(10.0f);       // [0, 10)
            float vy = RandFloat_Scaled(5.0f);        // [0, 5)
            v = Vec3(vx - 5.0f, vy, 0.0f);            // 0.0f = DAT_0016ad9c
            e->vel = v;                               // +0x1c (3 floats)
            float ax = Math::Abs(e->vel.x);
            e->m_bDrawWhole = 1;                      // +0x114 (AFTER vel write)
            float sign = (e->pos.x < 0.0f) ? -1.0f : +1.0f;
            e->vel.x = ax * sign;                     // ONLY x; y/z unchanged
            e->m_HalfB_vel = e->vel;                  // +0xc4 = +0x1c (full copy)
        }
        e = ActorManager::GetEntityNext(0, &it);
    }

    // --- Pass 2: bombs (type 1) ---
    b = (Bomb*)ActorManager::GetEntityFirst(1, &it);
    while (b) {
        if (Bomb::Enabled(b)) {                       // !m_bCollisionGuard
            Bomb::Disable(b);                         // m_bCollisionGuard = 1
            float vx = RandFloat_Scaled(10.0f);
            float vy = RandFloat_Scaled(5.0f);
            v = Vec3(vx - 5.0f, vy, 0.0f);
            b->base.vel = v;                          // +0x1c
        }
        b->m_bMovement = 1;                           // +0x80 (UNCONDITIONAL)
        b = (Bomb*)ActorManager::GetEntityNext(1, &it);
    }
}
```

## Field write order (Pass 1 -- exact ARM trace)

| asm | Action |
|-----|--------|
| `0016ac9a` | `r3 = entity->m_bSliced` (load +0xb4 into r3) |
| `0016ac9e` | `cbnz r3, skip-this-entity` |
| `0016aca0` | `r6 = 1` |
| `0016aca6` | `entity->m_bSliced = 1` (store +0xb4) -- **SET FIRST** |
| `0016acac` | `s17 = RandFloat_Scaled(10.0)` |
| `0016acc0` | `s0  = RandFloat_Scaled(5.0)` |
| `0016acce` | `s0(=x) = s17 - 5.0` |
| `0016acd2` | `Vec3::Vec3(&local, x, y, 0.0)` |
| `0016acda` | `entity->vel = local` (3 floats to +0x1c) |
| `0016acde` | `s0 = entity->vel.x` |
| `0016ace2` | `s0 = Math::Abs(s0)` |
| `0016ace6` | `s15 = entity->pos.x` (+0x10) |
| `0016acea` | `r3 = &entity->m_HalfB_vel` (+0xc4) |
| `0016acee` | `entity->m_bDrawWhole = 1` (store +0x114) |
| `0016acf2..acfe` | `s15 = (pos.x < 0) ? -1.0 : 1.0` |
| `0016ad08` | `s15 = abs(vel.x) * sign` |
| `0016ad0c` | `entity->vel.x = s15` (only x; y/z left as random) |
| `0016ad10..ad14` | `entity->m_HalfB_vel = entity->vel` (3-float copy) |

## Important consequences

1. **`vel == m_HalfB_vel` after this routine.** The two fields are
   bitwise-identical at exit. The downstream
   `MenuButton::Update` released-branch gate
   `|vel - m_HalfB_vel|^2 > 0.001f` therefore **fails** for every
   fruit released by `ClearMenuItems`. That is intentional: the gate
   protects against re-entrant `ClearMenuItems` calls. Sibling buttons
   still detach via the unconditional `m_pEntity = nullptr` write at
   `0x0014e7ec` (see `docs/structs/gameplay-misc.md` MenuButton
   "Released-branch / cascade detach" section).

2. **Pass 1 guards on `m_bSliced == 0`** -- a fruit already sliced
   (by user or by an earlier ClearMenuItems on the same frame) is
   left alone. No double-write.

3. **Pass 2 has no `m_bSliced` guard** -- bombs always get
   `m_bMovement = 1` written, even if `Bomb::Enabled() == 0` (i.e.
   the bomb has already exploded / been disabled). Only the
   velocity write is gated on `Enabled()`.

4. **Bombs do NOT get the sign-flip treatment, no `m_HalfB_vel` write,
   no `m_bDrawWhole` write.** Their menu-button detach happens via the
   bomb branch in `MenuButton::Update` (`0x0014e7f4..0x0014e81e`)
   which polls `Bomb::Enabled()`.

## DAT constants

| Address | Value | Use |
|---------|-------|-----|
| `DAT_0016ad9c` | `0.0f` | Vec3 ctor's z component |
| immediate `0x41200000` | `10.0f` | Pass-1 / Pass-2 vx scale |
| immediate `0x40a00000` | `5.0f`  | Pass-1 / Pass-2 vy scale, AND vx -= 5 offset |
| immediate `0x3f800000` | `+1.0f` | sign-flip "positive" lit |
| immediate `0xbf800000` | `-1.0f` | sign-flip "negative" lit |

## RandFloat_Scaled (0x0016a960)

Helper used by ClearMenuItems and elsewhere. Returns
`Rand32(0x7FFFF) * (s / 524287.875f)` -- i.e. a uniform `[0, s)` float.
The port substitutes `((float)rand() / RAND_MAX) * s` which is
visually equivalent.

## Cross-reference

- `MenuButton::Update` released-branch gate analysis:
  `docs/structs/gameplay-misc.md` -> "Released-branch / cascade detach"
- `Bomb::Enabled` / `Bomb::Disable`:
  `docs/structs/gameplay-misc.md` (Bomb collision-guard byte)
