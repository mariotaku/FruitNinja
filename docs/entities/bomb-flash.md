# BombFlash System

BombFlash is a managed array of flash effects (0x44 / 68 bytes each). Not an Entity -- a standalone system with static Update/Draw/Cleanup functions.

## BombFlash Struct Layout (0x44 bytes)

| Offset | Size | Type | Name |
|--------|------|------|------|
| 0x00 | 4 | vtable* | vtable |
| 0x04 | 4 | float | m_Timer |
| 0x08 | 4 | Colour | field_0x8 (start colour) |
| 0x0C | 4 | Colour | field_0xc (end colour) |
| 0x0B | 1 | byte | m_MaxAlpha |
| 0x0F | 1 | byte | m_CurrentAlpha |
| 0x10 | 4 | float | m_SinAngle |
| 0x14 | 4 | float | m_CosAngle |
| 0x18 | 4 | SmartPtr<Texture> | texture |
| 0x1C | 12 | Vec3 | m_Pos (position) |
| 0x28 | 12 | Vec3 | m_Dir (direction) |
| 0x34 | 12 | Vec3 | m_Scale |
| 0x40 | 1 | byte | m_bActive |
| 0x42 | 2 | ushort | m_Angle (16-bit angle) |

---

## Update Logic

### BombFlash::Update (0x171038, 61 lines)

Visual flash overlay when a bomb is hit. Animates size and alpha:

```c
float t = (timer + dt) / DURATION;
timer += dt;

// Quadratic scale animation
scale.x = BASE_X + t * ACCEL_X * t;
scale.y = BASE_Y + t * ACCEL_Y * t;

// Alpha fade: ramp up -> hold -> ramp down
if (timer < FADE_IN_TIME) {
    alpha = maxAlpha * (timer / FADE_IN_TIME);
} else if (timer > HOLD_END_TIME) {
    float fadeProgress = (DURATION - timer) / FADE_OUT_DURATION;
    alpha = maxAlpha * fadeProgress * fadeProgress;
}

if (timer > DURATION) {
    active = false;
    timer = 0;
}
```

---

## Instance Methods

### BombFlash::BombFlash (0x00171a14, 0x00171a50) -- constructors

```c
BombFlash::BombFlash() {
    vtable = &BombFlash_vtable;
    Colour::Colour(&field_0x8);    // init start colour
    Colour::Colour(&field_0xc);    // init end colour
    SmartPtr<Texture>::SmartPtr(&texture);
    m_bActive = 0;
}
```

Pseudocode summary:
- Initializes vtable, two colours, texture smart pointer
- Starts inactive

### ~BombFlash (0x00171f38, 0x00171fb8) -- destructors

```c
~BombFlash() {
    vtable = &BombFlash_vtable;
    SmartPtr<Texture>::~SmartPtr(&texture);  // release texture ref
}
```

### BombFlash::MakeFlash (0x001723f4)

`void __thiscall BombFlash::MakeFlash(void *this, Colour colour, Vec3 *pos, Vec3 *dir, SmartPtr<Texture> *tex)`

Creates a flash with position, colour, alpha, and animation parameters (11 params):

```c
void BombFlash::MakeFlash(Colour colour, Vec3 *pos, Vec3 *dir, SmartPtr<Texture> *tex) {
    texture = *tex;                    // copy texture ref
    m_Pos = *pos;
    m_Pos.z = DAT_z;                  // override Z
    m_Dir = *dir;

    // Store start/end colours
    field_0xc = colour;    // end colour
    field_0x8 = colour;    // start colour

    // Offset position along direction * scale
    m_Pos += *dir * 30.0f;    // 0x40F00000 = 30.0

    // Clamp X position to screen bounds
    if (m_Pos.x < DAT_threshold)
        m_Pos.x = DAT_left;
    else
        m_Pos.x = DAT_right;

    // Initial scale
    m_Scale = Vec3(DAT_s, DAT_s, DAT_s);

    // Calculate angle from direction
    m_Angle = Atan2Idx(dir->x, -dir->y);
    m_Timer = DAT_threshold;   // start timer
    m_bActive = 1;

    // Precompute sin/cos for rendering
    m_SinAngle = Sin(m_Angle);
    m_CosAngle = Cos(m_Angle);

    // Call virtual Update to process first frame
    vtable->Update(this);
}
```

Pseudocode summary:
- Configures a flash effect with position, direction, colour, and texture
- Offsets position along direction by 30 units
- Clamps X to screen bounds
- Computes rotation angle from direction vector
- Activates the flash and calls initial update

---

## Static/System Functions

### BombFlash::CleanUp (0x00171f64)

`void BombFlash::CleanUp(void)`

```c
void BombFlash::CleanUp() {
    int *arrayPtr = *g_flashArrayPtr;
    if (arrayPtr != NULL) {
        int count = *(arrayPtr - 1);   // count stored before array
        // Destruct all BombFlash objects in reverse order
        for (int i = count - 1; i >= 0; i--) {
            ~BombFlash(&array[i]);
        }
        operator_delete(arrayPtr - 1);  // free the allocation
        *g_flashArrayPtr = NULL;
    }
    *g_flashCountPtr = 0;
}
```

Pseudocode summary:
- Destructs all BombFlash objects in the managed array
- Frees the array allocation
- Resets count to 0

### BombFlash::RemoveAllFlashes (0x00170fe4)

`void BombFlash::RemoveAllFlashes(void)`

```c
void BombFlash::RemoveAllFlashes() {
    int count = *g_flashCount;
    for (int i = 0; i < count; i++) {
        Destroy();   // destroys flash at index i
    }
}
```

Pseudocode summary:
- Iterates all active flashes and destroys them
- Does not free the array itself (unlike CleanUp)

### BombFlash::UpdateActiveFlashes (0x00171028)

`void BombFlash::UpdateActiveFlashes(float dt)`

```c
void BombFlash::UpdateActiveFlashes(float dt) {
    return;   // Empty/stubbed
}
```

### BombFlash::DrawActiveFlashes (0x0017102c)

`void BombFlash::DrawActiveFlashes(void)`

```c
void BombFlash::DrawActiveFlashes() {
    return;   // Empty/stubbed
}
```

Note: Both UpdateActiveFlashes and DrawActiveFlashes are empty stubs. The flash visual is likely handled entirely by DrawBombHit or was disabled in this build.

---

## Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| BombFlash::Update | 0x00171038 | 61 | Flash scale + alpha animation |
| BombFlash::MakeFlash | 0x001723f4 | -- | Create new flash (11 params) |
| BombFlash::DrawActiveFlashes | 0x0017102c | -- | Render flashes (stubbed) |
| BombFlash::UpdateActiveFlashes | 0x00171028 | -- | Update flashes (stubbed) |
| BombFlash::CleanUp | 0x00171f64 | -- | Destroy array and free memory |
| BombFlash::RemoveAllFlashes | 0x00170fe4 | -- | Deactivate all flashes |

---

## See Also

- [Bomb entity](bomb.md) -- triggers BombFlash via HitBomb/CleanupBomb
- [BombBlast entity](bomb-blast.md) -- explosion shockwave ring
- [Effects overview](../systems/effects.md) -- SplatEntity, BombFlash, BombBlast systems
