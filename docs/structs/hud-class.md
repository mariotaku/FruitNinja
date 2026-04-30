# HUD class — full RE spec

ASM-verified: 2026-04-30. Binary: `FruitNinja.exe` (Bada ARM32).
Companion to `docs/structs/hud.md` (which covers the wider HUDControl taxonomy).

This doc is the authoritative spec for the `HUD` container class itself —
its struct layout, every method, the per-frame ordering, and (critically)
the correct semantics of `HUD::Release` and `HUD::OnPause` that the port
currently has wrong.

---

## 1. HUD class identity

| Property                        | Value                                            |
|---------------------------------|--------------------------------------------------|
| Mangled symbol (likely)         | `HUD` (no namespace; bare class)                 |
| Vtable                          | **None** — pure data + non-virtual member fns    |
| Heap allocation size            | **0x24 bytes (36)** — `movs r0,#0x24` @ `0x0016c674` immediately before `HUD::HUD` call |
| Constructed at                  | `GameInit` step 2, stored at `g_GameData + 0x3C` |
| Accessed throughout via         | `*(HUD**)(g_GameData + 0x3C)`                    |

The 0x24 size matches Ghidra's struct (list head 8 + scales[6] 24 + 4 = 36)
and confirms HUD has at least one field beyond the 6-float scales array:
`HUD::Update` writes `1.0f` to `[this+0x20]` (`vstr.32 s15,[r4,#0x20]` @
`0x00144d38`). Semantics of that 7th word are still TBD — see Section 5.

---

## 2. Method address map

| Method                                       | Address     | Body size | Notes |
|----------------------------------------------|-------------|-----------|-------|
| `HUD::HUD()` (real)                          | `0x00144bb0` | 40 B     | thunk @ `0x000fd710` calls it via PTR `0x001ef700` |
| `HUD::~HUD()`                                | `0x00144cd0` / `0x00144cf4` | small | `Release(this); ~list();` (two thunks for in-place / deleting variants) |
| `HUD::AddControl(HUDControl*, bool)` (real)  | `0x00144db0` | 26 B     | PLT thunk @ `0x00105b40` calls it via PTR `0x001f2310` |
| `HUD::RemoveControl(HUDControl*)`            | `0x00144c40` | 26 B     | |
| `HUD::BeginDraw(float dt)`                   | `0x00144b28` | 78 B     | |
| `HUD::Draw(int layerMask)`                   | `0x00144a90` | 144 B    | |
| `HUD::Update(float dt)`                      | `0x00144d20` | 144 B    | port comment says `0x00144d40` — that's wrong, it's `0x00144d20` |
| `HUD::OnPause()`                             | `0x00144c00` | 62 B     | |
| `HUD::Release()`                             | `0x00144c5c` | 108 B    | |
| `HUD::ResetControls()`                       | `0x00144ba0` | small    | iterate, vtable+0x10 |
| `HUD::Save()`                                | `0x00144a40` | small    | iterate, vtable+0x38 |
| `HUD::SetToMultiplayerState()`               | `0x00144e00` | larger   | filter via vtable+0x2c, RemoveControl unmatched |

There is **no `HUD::GetControl`** — control lookup is done by callers via direct
`controls.front()` / iteration.

---

## 3. HUD struct layout (size = 0x24)

| Offset | Size | Type                  | Name (suggested)         | Default | Notes |
|--------|------|-----------------------|--------------------------|---------|-------|
| +0x00  | 4    | `_List_node*`         | controls.head_prev       | (sentinel) | std::list head, prev pointer |
| +0x04  | 4    | `_List_node*`         | controls.head_next       | (sentinel) | std::list head, next pointer (8-byte sentinel total) |
| +0x08  | 4    | `float`               | scale1 (worldTint.x)     | 1.0     | **gameplay-mutable**, saved/restored each frame by GameDraw |
| +0x0C  | 4    | `float`               | scale2 (worldTint.y)     | 1.0     | **gameplay-mutable** |
| +0x10  | 4    | `float`               | scale3 (worldTint.z)     | 1.0     | **gameplay-mutable** |
| +0x14  | 4    | `float`               | scale4 (uiTint.x?)       | 1.0     | written by ctor only; readers not yet found |
| +0x18  | 4    | `float`               | scale5 (uiTint.y?)       | 1.0     | written by ctor only |
| +0x1C  | 4    | `float`               | scale6 (uiTint.z?)       | 1.0     | written by ctor only |
| +0x20  | 4    | `float`               | field_0x20               | written 1.0f by `HUD::Update` start | Semantics TBD; only Update writes it. Possibly a per-frame "Update tint accumulator" or unused padding the compiler still touched. **Do not assume it's part of `scales[6]`.** |

`std::list<T*>` on this libstdc++ build = `{prev, next}` only at the head
(8 bytes). The `controls.size()` is not stored — `std::list::size()` is O(N)
on this implementation.

### HUD::HUD() pseudocode (`0x00144bb0`)

ASM `0x00144bb0..0x00144bd6`: `list::list()` then six `vstr.32 s15,[r4,#0x?]` of `0x3f800000` to +0x08/+0x0C/+0x10/+0x14/+0x18/+0x1C. **+0x20 is NOT initialised by ctor** — relies on `HUD::Update` writing it before any read.

### HUD::~HUD() pseudocode

`HUD::Release(this); std::list::~list(&this->controls);` — see Section 4.7 for the non-destructive Release semantics.

---

## 4. Method pseudocode (verified)

### 4.1 `HUD::AddControl(HUDControl*, bool)` @ `0x00144db0`

```c
void HUD::AddControl(HUD* this, HUDControl* ctrl, bool pushFront) {
    if (pushFront == 0)
        std::list<HUDControl*>::push_back(&this->controls, &ctrl);
    else
        std::list<HUDControl*>::push_front(&this->controls, &ctrl);
}
```

**Bool semantic confirmed:** `true` → `push_front`, `false` → `push_back`.
Port's parameter name `pushFront` is correct.

### 4.2 `HUD::RemoveControl(HUDControl*)` @ `0x00144c40`

```c
void HUD::RemoveControl(HUD* this, HUDControl* ctrl) {
    if (ctrl == nullptr) return;
    Mortar::Delegate1<void,HUDControl*>::operator()(&ctrl->m_RemoveCallback, ctrl);
    std::list<HUDControl*>::remove(&this->controls, &ctrl);
}
```

**Does not call vtable `Release()` and does not delete the control.** It
fires `m_RemoveCallback` (the per-control delegate at HUDControl+0x38) and
removes from list. The caller owns the lifetime.

### 4.3 `HUD::BeginDraw(float dt)` @ `0x00144b28`

```c
void HUD::BeginDraw(HUD* this, float dt) {
    auto it = controls.begin();
    while (it != controls.end()) {
        HUDControl* ctrl = *it;
        if (ctrl->m_bActive)                      // [+0x30]
            (*ctrl->vtable->BeginDraw)(ctrl, dt); // vtable +0x14
        ++it;
    }
}
```

Port matches.

### 4.4 `HUD::Draw(int layerMask)` @ `0x00144a90` — **tint flag semantic CONFIRMED**

```c
void HUD::Draw(HUD* this, int layerMask) {
    // Load (1.0, 1.0, 1.0) from a GOT global into a local Vec3.
    // GOT pool: PC@0x144a98 + DAT_00144b20(0x000a7698) = 0x001ec130 (GOT base)
    //           + DAT_00144b24(-0x30790)              = 0x001bb9a0
    // read_memory(0x001bb9a0, 12) = 0x3f800000, 0x3f800000, 0x3f800000  → (1,1,1)
    Vec3 identity = *(Vec3*)0x001bb9a0;     // local on stack at sp+4
    Vec3* worldTint = &this->scale1;        // = this+0x08

    for (auto it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (!ctrl->m_bActive) continue;                        // [+0x30]
        if ((layerMask & ctrl->m_LayerFlags) == 0) continue;   // [+0x34]

        // CRITICAL: HUDControl[+0x60] selects which Vec3 to pass.
        Vec3* tintArg;
        if (ctrl->field_0x60 != 0) {
            // Default path (ctor sets field_0x60 = 1).
            // Control's PreDraw/Draw receives HUD's gameplay-mutable tint window.
            tintArg = worldTint;             // &this->scale1
        } else {
            // Control opted out of gameplay tint — receives pure white (1,1,1).
            tintArg = &identity;
        }

        // vtable +0x20: PreDrawOrder(this, Vec3* tint, int layerMask)
        (*ctrl->vtable->PreDrawOrder)(ctrl, tintArg, layerMask);
        // vtable +0x24: DrawOrder(this, Vec3* tint, int layerMask)
        (*ctrl->vtable->DrawOrder)(ctrl, tintArg, layerMask);
    }
}
```

**Disassembly proof** (key branch at `0x00144aca..0x00144af2`): `ldrb.w lr,[r3,#0x60]; cmp.w lr,#0x0; beq 0x144aea`. Fallthrough (non-zero) sets `r1 = r6 (&scale1)`; taken (zero) sets `r1 = r7 (&identity)`. Both paths then call `vtable[0x20]` (PreDrawOrder) and later `vtable[0x24]` (DrawOrder) with the same `r1`.

#### Semantics of `field_0x60`

`HUDControl::HUDControl` initialises `[+0x60] = 1` (`strb.w r8,[r4,#0x60]` @
`0x00144162`, with `r8 = 1`). So **default = receives gameplay tint**.

Setting `field_0x60 = 0` opts the control out of HUD's mutable tint and
forces it to draw at full brightness (identity Vec3). This is the inverse of
"is tinted by gameplay events".

### 4.5 `HUD::Update(float dt)` @ `0x00144d20` — **deletion guard confirmed**

```c
void HUD::Update(HUD* this, float dt) {
    MissControl::PreUpdate(dt);             // global pre-tick (combo decay)
    *(float*)((char*)this + 0x20) = 1.0f;   // reset field_0x20 (semantics TBD)

    auto it = controls.begin();
    while (it != controls.end()) {
        HUDControl* ctrl = *it;
        if (ctrl->m_bActive)                                // [+0x30]
            (*ctrl->vtable->Update)(ctrl, dt);              // vtable +0x28

        // Re-read ctrl in case Update() did something funky
        ctrl = *it;
        if (ctrl->m_bPendingRemoval == 0) {                 // [+0x33]
            ++it;
        } else {
            // Fire m_RemoveCallback BEFORE potentially deleting
            Mortar::Delegate1<void,HUDControl*>::operator()(&ctrl->m_RemoveCallback, ctrl);
            ctrl = *it;
            if (ctrl->m_bNoDestructor == 0) {               // [+0x32]
                // Call deleting-dtor (vtable +0x04) — frees memory.
                (*(*(void(***)(HUDControl*))ctrl + 1))(ctrl);
                *it = nullptr;                              // overwrite slot before erase
            }
            it = std::list<HUDControl*>::erase(&this->controls, it);
        }
    }
}
```

**Verified:** the deleting-dtor at vtable `+0x04` is only called when
`m_bNoDestructor == 0`. The port's logic matches this — it's correct.

### 4.6 `HUD::OnPause()` @ `0x00144c00` — **port wrong, dispatches to GetType not Init**

```c
void HUD::OnPause(HUD* this) {
    auto it = controls.begin();
    while (it != controls.end()) {
        HUDControl* ctrl = *it;
        // vtable +0x30 = GetType()  → returns int
        int t = (*ctrl->vtable->GetType)(ctrl);
        if (t == 8) {
            ScrollingMenu::ClearTouch(ctrl);   // cast ctrl to ScrollingMenu*
        }
        ++it;
    }
}
```

**Disassembly proof** (`0x00144c18..0x00144c26`): `ldr r3,[r0,#0x0]; ldr r3,[r3,#0x30]; blx r3` (call vtable+0x30 = `GetType`), then `cmp r0,#0x8; bne skip; blx 0x00107c94` (= `ScrollingMenu::ClearTouch`).

**Port bug** (`src/hud/HUD.h:97-102`): the port's `OnPause` calls
`(*it)->Init()` on every active control. That's **completely wrong** —
the binary doesn't call any vtable method on most controls; it only checks
each one's `GetType()` return value and branches to
`ScrollingMenu::ClearTouch` for the one specific type (TYPE_SCROLLING_MENU = 8).

The unconditional `Init()` call in the port could re-initialise screens,
buttons, and other controls every time the menu pauses, which would clobber
their state.

### 4.7 `HUD::Release()` @ `0x00144c5c` — **CRITICAL: port bug**

```c
void HUD::Release(HUD* this) {
    // Get GameData* from GOT and set its "in-Release" flag at offset +0x34.
    // This blocks reentry / certain async hooks during teardown.
    GameData* g = *(GameData**)(GOT_BASE + DAT_00144ccc);   // = *(GameData**) (g_GameData)
    g->field_0x34 = 1;                                      // ENTER guard

    auto it = controls.begin();
    while (it != controls.end()) {
        HUDControl* ctrl = *it;
        if (ctrl->m_bNoDestructor == 0) {                   // [+0x32]
            // Fire removal callback first
            Mortar::Delegate1<void,HUDControl*>::operator()(&ctrl->m_RemoveCallback, ctrl);
            ctrl = *it;
            if (ctrl != nullptr) {
                // vtable +0x04 = deleting-dtor (frees memory)
                (*(*(void(***)(HUDControl*))ctrl + 1))(ctrl);
                *it = nullptr;
            }
        }
        // NOTE: when m_bNoDestructor != 0, the control is left alone — no callback,
        //       no dtor. The HUD does not own those controls.
        ++it;
    }

    std::list<HUDControl*>::clear(&this->controls);

    g->field_0x34 = 0;                                      // EXIT guard
}
```

**Disassembly proof** (`0x00144c7c..0x00144c98`): `ldrb.w r6,[r0,#0x32]; cbnz r6,skip` — when `m_bNoDestructor != 0`, jumps over the entire callback + dtor + nullify block. The "do work" path: `adds r0,#0x38; blx Delegate1::op()`; null-check `cbz r0`; `ldr r3,[r0,#0x0]; ldr r3,[r3,#0x4]; blx r3` (deleting-dtor at vtable+0x04); `str r6,[r3,#0x8]` (r6=0 → NULL out the iter).

#### Port bug analysis (`src/hud/HUD.h:104-111`)

```cpp
void Release() {
    for (auto it = controls.begin(); it != controls.end(); ++it) {
        (*it)->Release();    // BUG: binary doesn't call vtable Release()
        delete *it;          // BUG: binary skips delete when m_bNoDestructor != 0
    }
    controls.clear();
}
```

Three discrepancies:

1. **Port calls `ctrl->Release()` (vtable +0x0C).** Binary does not.
   Binary fires `m_RemoveCallback` (delegate at +0x38), not the vtable
   Release method. These are different things.
2. **Port deletes every control unconditionally.** Binary only deletes when
   `m_bNoDestructor == 0`. Static / pool / stack-allocated / shared controls
   set `m_bNoDestructor = 1` and survive `HUD::Release` untouched.
3. **Port does not set the GameData guard flag at +0x34.** Whatever code
   reads that flag is currently unprotected from re-entry during
   destruction. (Guard semantics are out of scope for this doc; flag-clear
   is performed by the port-side teardown owner of the screen.)

#### What the port should do

```cpp
void Release() {
    // Optional: if/when GameData re-entry guard is ported, set it here.
    for (auto it = controls.begin(); it != controls.end(); ++it) {
        HUDControl* ctrl = *it;
        if (!ctrl->m_bNoDestructor) {
            if (ctrl->m_RemoveCallback)
                ctrl->m_RemoveCallback(ctrl);
            delete ctrl;       // delete only when binary would
            *it = nullptr;
        }
    }
    controls.clear();
}
```

This is non-destructive for any control flagged `m_bNoDestructor = 1`,
matching the binary, and explains why removing the destructive Release()
call (commit `b0bfa51`) "fixed" gameplay.

### 4.8 `HUD::ResetControls()` @ `0x00144ba0`

```c
void HUD::ResetControls(HUD* this) {
    for (HUDControl* c : controls)
        (*c->vtable->Reset)(c);   // vtable +0x10
}
```

### 4.9 `HUD::Save()` @ `0x00144a40`

```c
void HUD::Save(HUD* this) {
    for (HUDControl* c : controls)
        if (c != nullptr)
            (*c->vtable->Save)(c);  // vtable +0x38
}
```

### 4.10 `HUD::SetToMultiplayerState()` @ `0x00144e00`

Two-pass: (1) build a temp list of controls whose `vtable+0x2c` returns
non-zero, (2) iterate main list and `RemoveControl` for each match.
Effectively removes controls that flag themselves "not multiplayer-safe".

---

## 5. HUD field_0x20 — **the unknown 7th word**

`HUD::Update`'s prologue sets `[this+0x20] = 1.0f` (`vstr.32 s15,[r4,#0x20]`).
`HUD::HUD()` does NOT initialise this field. No other writer or reader located
in this pass. Likely a per-frame "reset" of a tint/dt accumulator that some
HUDControl::Update mutates. **Port should reserve a `float` slot there** —
don't fold it into `scales[6]`. Bring forward as RE gap if symptoms appear.

---

## 6. HUDControl base class — relevant fields

Refer to `docs/structs/hud.md` Section "HUDControl" for the full layout.
Re-confirmed here for the fields HUD itself accesses:

| Offset | Type      | Name              | HUD reads / writes via                                  |
|--------|-----------|-------------------|---------------------------------------------------------|
| +0x00  | vtable*   | vtable            | every dispatch                                          |
| +0x30  | uint8_t   | m_bActive         | guard in BeginDraw/Draw/Update                          |
| +0x32  | uint8_t   | m_bNoDestructor   | guard in Update + **Release**                           |
| +0x33  | uint8_t   | m_bPendingRemoval | trigger in Update                                       |
| +0x34  | int       | m_LayerFlags      | mask in Draw                                            |
| +0x38  | Delegate1 | m_RemoveCallback  | fired in RemoveControl, Update (on pending removal), Release |
| +0x60  | uint8_t   | field_0x60        | **selects identity-vs-HUD-scales tint** in Draw         |

Vtable slots HUD invokes:

| vt offset | Method            | HUD caller |
|-----------|-------------------|------------|
| +0x04     | deleting dtor     | Update (when pending+!noDtor), Release (when !noDtor) |
| +0x10     | Reset             | ResetControls |
| +0x14     | BeginDraw         | BeginDraw |
| +0x20     | PreDrawOrder      | Draw |
| +0x24     | DrawOrder         | Draw |
| +0x28     | Update            | Update |
| +0x2c     | IsMultiplayerSafe (returns int) | SetToMultiplayerState |
| +0x30     | GetType           | OnPause (compares against 8 = ScrollingMenu) |
| +0x38     | Save              | Save |

---

## 7. Layer flag conventions (from GameDraw `0x0016b888`)

GameDraw issues exactly seven `HUD::Draw(hud, mask)` calls, in this fixed order:

| Call # | layerMask | Position in frame                                             | Likely purpose                       |
|--------|-----------|---------------------------------------------------------------|--------------------------------------|
| 1      | `0x40`    | After Actors, before Splats                                   | Behind-fruits HUD (shadow layer)     |
| 2      | `0x80`    | After SlashEntity::PreDraw + BombBlast + BombFlash            | Mid-effects HUD                      |
| 3      | `0x01`    | After particles + slices (depth buffer off, world-tinted)     | **Default layer** — most controls    |
| 4      | `0x08`    | After scales reset to 1.0, after WaveManager::Draw            | UI layer (untinted)                  |
| 5      | `0x100`   | After CritHit                                                 | Post-effects layer                   |
| 6      | `0x200`   | After BombHit                                                 | Late-overlay layer                   |
| 7      | `0x400`   | Final draw of frame, after NetworkManager modal dialogs       | Topmost layer (fades / news)         |

### Scale-window discipline in GameDraw

Around layers 0x40, 0x80, 0x01, GameDraw does:
```c
// Frame start: snapshot HUD's scale1/scale2/scale3 (gameplay window)
HUD* hud = g_GameData->hud;
float saved1 = hud->scale1, saved2 = hud->scale2, saved3 = hud->scale3;
...
HUD::Draw(hud, 0x40);
... world drawing using HUD's mutable tint ...
HUD::Draw(hud, 0x80);
... particle/slice draws ...
HUD::Draw(hud, 0x01);
... waveMgr / postFX ...

// Reset gameplay window to identity before UI layers
hud->scale1 = hud->scale2 = hud->scale3 = 1.0f;

HUD::Draw(hud, 0x08);
HUD::Draw(hud, 0x100);
HUD::Draw(hud, 0x200);

// Frame end: restore originals
hud->scale1 = saved1; hud->scale2 = saved2; hud->scale3 = saved3;

HUD::Draw(hud, 0x400);
```

**Implication for the port:** controls that need to be tint-affected by
gameplay (e.g. flash-on-bomb fade in world layer) need `field_0x60 = 1`
(default) **and** their layer mask in {0x40, 0x80, 0x01}. Pure-UI
controls draw on layer 0x08+ and their scales are forced to 1.0 anyway,
so `field_0x60` doesn't matter for them visually — but setting
`field_0x60 = 0` is a clean "I am UI, ignore the gameplay window"
opt-out.

### Per-frame ordering (from GameUpdate `0x0016bed0`)

```c
// Per HUD substep (variable when game is paused-but-still-animating)
HUD::Update(hud, dt);    // or substepped HUD::Update(hud, GAME_HUD_SUBSTEP_DT)
                         // when game is paused (gameTaskState[+0x35] != 0)
```

Per-frame draw (after the world has been rendered):
```c
HUD::BeginDraw(hud, dt);
HUD::Draw(hud, 0x40);
HUD::Draw(hud, 0x80);
... gameplay overlays ...
HUD::Draw(hud, 0x01);
... reset HUD scales ...
HUD::Draw(hud, 0x08);
HUD::Draw(hud, 0x100);
HUD::Draw(hud, 0x200);
... restore HUD scales ...
HUD::Draw(hud, 0x400);
```

Layer 0x400 is the **only** layer drawn after `restore`, meaning anything
on 0x400 sees whatever scale1..3 the gameplay had at frame start
(typically 1.0 unless a fade is active).

---

## 8. Constants & DAT pool refs

| DAT                | Address       | Decoded value                | Use                                       |
|--------------------|---------------|------------------------------|-------------------------------------------|
| `DAT_00144b20`     | `0x00144b20`  | `0x000a7698`                 | HUD::Draw GOT base offset                 |
| `DAT_00144b24`     | `0x00144b24`  | `0xfffcf870` (= -0x30790)    | HUD::Draw identity-vec3 offset            |
| identity Vec3      | `0x001bb9a0`  | `{1.0f, 1.0f, 1.0f}`         | passed when HUDControl[+0x60] == 0        |
| `DAT_00144cc8`     | `0x00144cc8`  | `0x000a74cc`                 | HUD::Release GOT base offset              |
| `DAT_00144ccc`     | `0x00144ccc`  | `0x00007990`                 | HUD::Release GameData ptr offset          |
| GameData[+0x34]    | (runtime)     | uint8_t in-Release guard     | HUD::Release sets 1 / clears to 0         |
| operator_new size  | `0x0016c674`  | `0x24` (36)                  | HUD heap allocation in GameInit           |
| HUD pointer slot   | (runtime)     | `g_GameData[+0x3C]`          | location of `HUD*` accessed by GameDraw / GameUpdate |

---

## 9. Port action items (summary — see Sections 4 and 5 for pseudocode)

| Port file:line              | Bug                                                                | Fix per binary                                                                  |
|-----------------------------|--------------------------------------------------------------------|---------------------------------------------------------------------------------|
| `src/hud/HUD.h:104-111`     | `Release()` deletes every control & calls `vtable->Release`        | Skip when `m_bNoDestructor`; fire `m_RemoveCallback`; call deleting-dtor (= `delete`) only otherwise — see 4.7 |
| `src/hud/HUD.h:97-102`      | `OnPause()` calls `Init()` on each active control                  | Dispatch `GetType()`; only when == 8 call `ScrollingMenu::ClearTouch` — see 4.6 |
| `src/hud/HUD.h:62-75`       | `Draw()` always passes HUD scales (SPEC GAP)                       | Branch on `field_0x60`: non-zero → HUD scales, zero → identity (1,1,1) — see 4.4 |
| `src/hud/HUD.h:78-94`       | `Update()` missing `MissControl::PreUpdate(dt)` and `field_0x20 = 1.0f` reset | Add both at function start — see 4.5                                            |
| `src/hud/HUD.h:14-24`       | HUD struct is 32 B (`list + float[6]`)                             | Add `float field_0x20;` after `scales[6]` so size = 36 (matches binary 0x24)    |
