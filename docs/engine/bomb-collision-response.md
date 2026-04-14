<!-- Analysed: 2026-04-13T00:00 -->

# Bomb::CollisionResponse — else branch (menu bomb re-hit)

Address: `0x0017280c`

---

## Overview of the full dispatch

```c
int Bomb::CollisionResponse(Bomb* this, Entity* slash, ulong p2, ulong p3, Vec3* bladeVel) {
    if (this->field_0x78 != 0) return 0;   // collision guard — already processing

    if (this->m_bMenuBombHit == 0) {
        // --- game-bomb path (Classic / Zen) ---
        // [covered by existing port OnSliced]
    } else {
        // --- menu-bomb re-hit path ---  <-- THIS DOCUMENT
        if (this->m_SomeGameStateBackref == 0 ||
            *(byte*)(this->m_SomeGameStateBackref + 0x123) != 0)
        {
            ClearMenuItems();
        }
        Delegate0<void>::operator()(&this->m_HitCallback);
    }
    this->m_bHit = 1;
    return 0;
}
```

---

## `m_bMenuBombHit` — how it is set

`Bomb_Init` (`0x00172504`) always sets `m_bMenuBombHit = 0`.

`Bomb::SetCallback` (`0x0017121c`) sets it to `1`:

```c
void Bomb::SetCallback(Bomb* this, Delegate0<void>* cb, MenuButton* owner) {
    this->m_bMenuBombHit = 1;
    Delegate0<void>::operator=(&this->m_HitCallback, cb);
    this->m_SomeGameStateBackref = (int)owner;  // raw MenuButton* stored as int
    this->m_RotY    = 0x2D;   // fixed tilt
    this->m_RotVelX = 2;
    this->m_RotX    = 0;
    this->m_RotVelY = 0;
}
```

`SetCallback` is called from `MenuButton::Init` (`0x0014ee40`) only when
`m_FruitType >= numFruitTypes` (i.e., the entity slot is a Bomb, not a Fruit):

```c
// inside MenuButton::Init, bomb branch:
Bomb::SetCallback((Bomb*)pEntity, param_2 /*Delegate0 from ctor*/, this /*MenuButton*/);
```

`param_2` is the `Delegate0<void>` passed as the third argument to `MenuButton::MenuButton(...)` —
the tap/click callback (e.g., `MainScreen::MainScreen_QuitGamesCallback` bound via
`Delegate0::QCallee<MainScreen>`).

---

## `m_SomeGameStateBackref` / offset `+0x123`

`m_SomeGameStateBackref` (Bomb+0x84) stores the raw `MenuButton*` that owns the bomb.

`*(byte*)(menuButton + 0x123)` = `MenuButton::m_bEnabled` (offset 291 decimal = 0x123, type
`byte`, confirmed from Ghidra struct size 348 / layout). The condition reads:

```
if (m_SomeGameStateBackref == NULL  ||  menuButton->m_bEnabled != 0)
    ClearMenuItems();
```

- If there is no owning MenuButton (NULL), always clear.
- If the owning MenuButton is enabled (`m_bEnabled != 0`), clear.
- Only skip `ClearMenuItems` if the owning MenuButton exists AND is currently disabled.

---

## `ClearMenuItems` (`0x0016ac7c`)

Iterates both entity pools. Does **not** deactivate entities via ActorManager; instead it applies a
random off-screen impulse so they fly away visually.

```c
void ClearMenuItems() {
    // --- Fruit entities (type 0) ---
    ActorManager* am = ActorManager::GetInstance();
    Entity* e = am->GetEntityFirst(0, iter);
    while (e) {
        if (*(byte*)(e + 0xB4) == 0) {       // m_bSliced == 0 (not yet sliced)
            *(byte*)(e + 0xB4) = 1;           // mark sliced — suppresses further collisions

            float vx = RandFloat_Scaled_GT(0x41200000) - 5.0f;  // rand [0, 10) - 5 = [-5, 5)
            float vy = RandFloat_Scaled_GT(0x40a00000);          // rand [0, 5.25)
            // Preserve X direction sign from current pos.x
            float sign = (e->pos.x < 0.0f) ? -1.0f : 1.0f;
            float absVx = fabsf(vx);

            e->vel.x = absVx * sign;          // offset 0x1c
            e->vel.y = vy;                    // offset 0x20
            e->vel.z = 0.0f;                  // DAT_0016ad9c = 0.0f

            *(byte*)(e + 0x114) = 1;          // m_bSliced flag mirror / half-B active flag

            // Copy vel into half-B vel (offsets 0xC4, 0xC8, 0xCC = m_HalfB_vel)
            *(float*)(e + 0xC4) = e->vel.x;
            *(float*)(e + 0xC8) = e->vel.y;   // offset 200 decimal = 0xC8
            *(float*)(e + 0xCC) = e->vel.z;
        }
        e = am->GetEntityNext(0, iter);
    }

    // --- Bomb entities (type 1) ---
    Bomb* bomb = (Bomb*)am->GetEntityFirst(1, iter);
    while (bomb) {
        if (Bomb::Enabled(bomb)) {
            Bomb::Disable(bomb);

            float vx = RandFloat_Scaled_GT(0x41200000) - 5.0f;
            float vy = RandFloat_Scaled_GT(0x40a00000);
            bomb->base.vel.x = vx;
            bomb->base.vel.y = vy;
            bomb->base.vel.z = 0.0f;          // DAT_0016ad9c = 0.0f
        }
        bomb->m_bMovement = 1;                // re-enable physics so they fly
        bomb = (Bomb*)am->GetEntityNext(1, iter);
    }
}
```

Constants used by `ClearMenuItems`:

| Value | Hex | Meaning |
|-------|-----|---------|
| `0x41200000` | `10.0f` | RandFloat upper bound for vx (result = rand [0,10) - 5) |
| `0x40a00000` | `5.0f` | RandFloat upper bound for vy (result is raw [0, ~5.25)) |
| `DAT_0016ad9c` | `0.0f` | Z velocity — always zero |

---

## `m_HitCallback` Delegate (`Bomb+0x40`, 36 bytes)

The field is `Delegate0<void> m_HitCallback` at Bomb offset 0x40 (64). It is a standard
`Mortar::Delegate0<void>` (36-byte BaseDelegate slot).

`Bomb::SetCallback` copies into it via `Delegate0::operator=`. The callee stored is whatever
`Delegate0<void>` was passed by `MenuButton::Init` from the MenuButton constructor's `param_2`.

Tracing `MainScreen_Update` (`0x0014b278`): when `pQuitBtn` is NULL (first frame of state 1), it
builds a callback with:

```c
Delegate0<void>::QCallee<MainScreen>(callee_buf, this /*MainScreen*/);
Delegate0<void>::Delegate0(&DStack_3fc, callee_buf);
// ... then passes DStack_3fc as param_2 to MenuButton::MenuButton(...)
```

The bound method resolves to `MainScreen::MainScreen_QuitGamesCallback`
(`0x0014b1a0`) — the function that triggers the quit-to-desktop flow. When
`Delegate0::operator()` fires on the second hit, it calls that callback.

---

## Port implementation checklist

- `m_bMenuBombHit` is set to `1` in `Bomb::SetCallback`, called from `MenuButton::Init` for bomb-
  type menu items. Game bombs always have `m_bMenuBombHit == 0` after `Bomb_Init`.
- The condition `m_SomeGameStateBackref + 0x123` is `MenuButton::m_bEnabled`. Do not confuse with
  `m_bInteractive` (+0x122) or `m_bVisible` (+0x121).
- `ClearMenuItems` does NOT call `ActorManager::Remove`. It only sets `m_bSliced` on fruits and
  calls `Bomb::Disable` + re-enables `m_bMovement` on bombs, letting the normal Update loop expire
  them naturally.
- The `m_bHit = 1` assignment at the very end of `CollisionResponse` is shared by both branches —
  it fires after the delegate call in the menu-bomb path.
