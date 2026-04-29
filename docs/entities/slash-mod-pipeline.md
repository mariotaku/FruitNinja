# SlashEntity Blade-Modifier Pipeline

This document specifies the blade-modifier (equipped blade) data pipeline that
runs from the shop equip callback through `SlashEntity::SetModColours` /
`SetModScales` / `InitModColours` and into the blade renderer.

**Source file**: `Slash.cpp` (per `_GLOBAL__I_Slash.cpp` at `0x0017e52c`).

---

## TL;DR -- storage strategy

All blade-mod state is **class-static / file-scope**. None of the three setters
take a `this` pointer; they write directly into Slash.cpp globals that live in
`.data` (with sane defaults baked in by the compiler) or `.bss` (zero-init).

The only per-`SlashEntity`-instance state touched by the equip pipeline is via
the **virtual `ColoursChanged()`** call: SetModColours walks every active
`SlashEntity` (entity type 3) and invokes `ColoursChanged()` on each so the
trail emitter can be torn down and re-created with the new particle hash.

Storage anchor: ARM PIC GOT base = **`0x001EC130`** (the `.got` section
start). All `iVar11 + DAT_xxx` arithmetic in Ghidra resolves through this GOT
unless the offset is `>= 0x6164c`, in which case the offset is direct
PC-relative and points at the (also class-static) `g_SlashState` struct
allocated in `.bss` at **`0x0024D77C`**.

---

## Class-static storage layout (Slash.cpp file-scope globals)

### Colour / palette state (touched by SetModColours / InitModColours)

| Address    | Section | Size | Type     | Default | Name (this doc) | Setter            | Reader(s)                                         |
|------------|---------|------|----------|---------|------------------|-------------------|---------------------------------------------------|
| `0x001F3E54` | .data   | 4    | float    | 1.0f    | `g_LifeScale`    | SetModColours p4  | `UpdateModColour @ 0x17b13c` (animation rate)     |
| `0x001F3E58` | .data   | 4    | int32    | 1       | `g_ColourCount`  | SetModColours p2  | `UpdateModColour @ 0x17b108`, `UpdatePoints`      |
| `0x0024D874` | .bss    | 4    | float    | 0.0f    | `g_PaletteProgress` | SetModColours (=lifeScale or rand%count) / InitModColours (=0) | `UpdateModColour` (mod-2 modulo bookkeeping), `UpdatePoints @ 0x17ba8a` |
| `0x0024D878` | .bss    | 64   | Colour[16] | (0,0,0,0xFF) per slot, default-ctor by `_GLOBAL__I_Slash.cpp` | `g_Palette` | SetModColours (memcpy of `count*4` bytes from p1) | `UpdateModColour` (lerps between palette[i] and palette[i+1]) |
| `0x0024D8B8` | .bss    | 4    | int32    | 0       | `g_ColourType`   | SetModColours p3  | TouchDown@`0x17d63c`, PreUpdate@`0x17c5cc`, ColoursChanged@`0x17c448`, UpdatePoints, Update@`0x17dd36` |
| `0x0024D8BC` | .bss    | 1    | byte     | 0       | `g_DirectionalFlag` | SetModColours (computed: 0 / 1 / 2) / InitModColours (=0) | UpdateTouchDown@`0x17d37a`,`0x17d546`, ColoursChanged@`0x17c45e` |

### Particle hash state (StringHash uints, all in .bss, default 0)

| Address    | Size | Name             | Set from         | Reader(s)                                                          |
|------------|------|------------------|------------------|--------------------------------------------------------------------|
| `0x0024D8C0` | 4  | `g_TrailHash`    | SetModColours p5 | UpdateTouchDown@`0x17d38a` (creates `m_TrailEmitter`), ColoursChanged@`0x17c46e` |
| `0x0024D8C4` | 4  | `g_ContactHash`  | SetModColours p8 | Update@`0x17d9dc`,`0x17d9f0` (spawned at fruit-slice contact)      |
| `0x0024D8C8` | 4  | `g_SecondHash`   | SetModColours p9 | DrawSlice@`0x17e450`,`0x17e458` (spawned on blade deactivation)    |

### Texture state (in `g_SlashState` struct, NOT a separate global)

| Address    | Size | Type                  | Setter            | Reader                              |
|------------|------|-----------------------|-------------------|-------------------------------------|
| `0x0024D854` | 8    | `SmartPtr<Texture>`   | SetModColours p6 (`LoadLocalisedTexture` -> assign) | DrawSlice@`0x17e49e` (replaces default texture if valid) |

This is `g_SlashState->modTexture` -- offset `+0xd8` inside the existing
`g_SlashState` struct documented in `slash-entity.md`. Not a new global.

### Scale state (touched by SetModScales)

| Address    | Section | Size | Type  | Default | Name (this doc)  | Reader(s)                                                                         |
|------------|---------|------|-------|---------|-------------------|-----------------------------------------------------------------------------------|
| `0x001F3E5C` | .data   | 4    | float | 1.0f    | `g_Scale1`        | UpdatePoints@`0x17bb82`,`0x17c2d2` (fade lifetime divisor)                        |
| `0x001F3E60` | .data   | 4    | float | 1.0f    | `g_Scale2`        | GetHeadThicknessScale@`0x17b8ac` (max width = `g_Scale2 * 9.0`), UpdatePoints, AddPoint@`0x17d066` |
| `0x0024D8D0` | .bss    | 4    | float | 0.0f    | `g_Scale3`        | UpdatePoints@`0x17bb32`,`0x17bb72`,`0x17bbaa`,`0x17c2ca`, AddPoint@`0x17d070` (min thickness) |
| `0x001F3E64` | .data   | 4    | float | 1.0f    | `g_Scale4`        | UpdatePoints@`0x17c1d8` (passed by ref to a sub-fn -- segment width / spacing arg) |
| `0x0024D8D4` | .bss    | 4    | float | 0.0f    | `g_Scale5`        | UpdatePoints@`0x17c082`                                                          |
| `0x0024D8D8` | .bss    | 1    | byte  | 0       | `g_ScaleFlag1`    | DrawSlice@`0x17e444` (gates `CreateGhost()` on deactivation), Update@`0x17d7e2`   |
| `0x001F3E69` | .data   | 1    | byte  | 1       | `g_ScaleFlag2`    | UpdatePoints@`0x17bcba`,`0x17bf3e`, AddPoint@`0x17d1a0` (gates split-screen UV mirroring branch) |

The `.data` defaults are baked in at link time -- the values listed in the
"Default" column are the initial values present in the ELF before any setter
runs. Calling **`InitModColours` does NOT reset these scale fields**; only the
colour-side state above is reset. To restore scale defaults, the port must
either (a) re-initialise from the same baked values in `.data`, or (b) have
the equip code always call `SetModScales(1.0, 1.0, 0.0, 1.0, 0, 1, 0.0)` for
the "no scale mod" case. The original binary uses (b): every blade ItemInfo
(including the default blade) carries an explicit scale tuple.

### Default colour palette source (used by InitModColours)

| Address    | Size | Type   | Default | Notes                                                                             |
|------------|------|--------|---------|-----------------------------------------------------------------------------------|
| `0x00268F64` | 4  | Colour | (0xFF, 0xFF, 0xFF, 0xFF) | The engine-wide global white Colour, ctor'd by `_GLOBAL__I_Colour.cpp` at startup |

`InitModColours` runs a 16-iteration loop that calls
`Colour::operator=(stack_temp, &g_Palette[i], src=&g_DefaultWhite)`. The
operator= signature is `(this=r0, dst_ref=r1, src=r2)` (verified at
`0x0010C488`) -- it writes `*r1 = *r2` despite Ghidra's misleading
decompilation. So **InitModColours overwrites all 16 palette slots with
opaque white**.

---

## SetModColours -- pseudocode

**Address**: `0x0017CA0C`
**Signature**: `void SlashEntity::SetModColours(Colour* colours, int colourCount, int colourType, float lifeScale, char const* particlePath, char const* textureName2, bool directional, char const* contactParticle, char const* particle2)`
**Calling convention**: AAPCS (this is a *static-like* function -- has no `this`).
**Stack args**: param_5..param_9 come in via `[sp,#0x40 .. #0x4c]`.

```c
void SlashEntity::SetModColours(
    Colour* colours, int colourCount, int colourType, float lifeScale,
    const char* particlePath,    // p5 -- trail particle path
    const char* textureName2,    // p6 -- modifier blade texture
    bool        directional,     // p7
    const char* contactParticle, // p8 -- particle on fruit-impact
    const char* particle2)       // p9 -- particle on blade deactivation
{
    // Phase 1 -- write scalar mod state ----------------------------------
    g_LifeScale     = lifeScale;
    g_ColourType    = colourType;
    g_ColourCount   = colourCount;

    // Phase 2 -- copy palette (count entries, 4 bytes each) --------------
    Colour stackTmp;
    for (int i = 0; i < colourCount; i++) {
        // Note: this loop calls Colour::operator=(&stackTmp, &g_Palette[i],
        // src=&colours[i]) which writes *(&g_Palette[i]) = colours[i].
        // (Verified via Colour::operator= at 0x0010C488 -- AAPCS r0=this,
        //  r1=dst, r2=src; the decompiler labels r1 as "param_1" but it's
        //  actually the destination parameter.)
        Colour_op_assign(&stackTmp, &g_Palette[i], &colours[i]);
    }
    Colour_op_assign(&stackTmp, &g_SlashState.bladeColour /* +0xf4 */, ...);
        // (cleanup write to slashState bladeColour; semantically resets
        //  the displayed cached blade colour to whatever the previous
        //  iteration's source pointed to. Functionally a state reset.)

    // Phase 3 -- progress reset -------------------------------------------
    g_PaletteProgress = 1.0f;                        // float-imm at PC+0x134
    if (g_ColourType == 2) {
        // Random palette index for "single-shot per swipe" mode.
        g_PaletteProgress = (float)Random::Rand32(g_Random, g_ColourCount);
    }

    // Phase 4 -- zero out particle hashes / directional flag -------------
    g_DirectionalFlag = 0;
    g_TrailHash       = 0;
    g_ContactHash     = 0;
    g_SecondHash      = 0;

    // Phase 5 -- modifier texture (param_6) -------------------------------
    if (textureName2 && *textureName2 != '\0') {
        SmartPtr<Texture> tmp;
        Mortar::TextureManager::LoadLocalisedTexture(&tmp, textureName2);
        g_SlashState.modTexture /* +0xd8 */ = tmp;        // SmartPtr::operator=
        ~tmp;
    } else {
        SmartPtrNull_Tex(&g_SlashState.modTexture);       // helper @ 0x0017CA00
    }

    // Phase 6 -- trail particle (param_5) ---------------------------------
    if (particlePath && *particlePath != '\0') {
        g_TrailHash = StringHash(particlePath);
        if (PSPParticleManager::EmitterExists(g_TrailHash)) {
            // Only NOW does directional get its non-zero value, and only
            // if the trail particle template was actually registered.
            g_DirectionalFlag = directional ? 2 : 1;
        }
    }

    // Phase 7 -- contact particle (param_8) ------------------------------
    if (contactParticle && *contactParticle != '\0') {
        g_ContactHash = StringHash(contactParticle);
        if (!PSPParticleManager::EmitterExists(g_ContactHash))
            g_ContactHash = 0;       // template missing -- zero so reader skips
    }

    // Phase 8 -- second particle (param_9) -------------------------------
    if (particle2 && *particle2 != '\0') {
        g_SecondHash = StringHash(particle2);
        if (!PSPParticleManager::EmitterExists(g_SecondHash))
            g_SecondHash = 0;
    }

    // Phase 9 -- live-update walker -------------------------------------
    // Only walks if game->something@+0x160 != 0 (i.e. game/engine is up).
    if (g_GamePtr->field_0x160 != 0) {
        EntityIterator it;
        Mortar::ActorManager* am = ActorManager::GetInstance();
        Entity* e = am->GetEntityFirst(/*type=*/3, &it);  // type 3 = SlashEntity
        while (e) {
            ((SlashEntity*)e)->ColoursChanged();           // virtual? See note.
            e = am->GetEntityNext(/*type=*/3, &it);
        }
    }
}
```

**Note on the live-update call**: in the disassembly the `bl` to
`ColoursChanged` is a direct call to `0x0017c41c`, not a vtable dispatch.
There is no `ColoursChanged` slot in the SlashEntity vtable (slots 0..16 are
all accounted for in `slash-entity.md`). It's a non-virtual member function.
So the walker calls a fixed address, not via vtable. Implementer should mirror
this: a plain call, not a virtual dispatch.

---

## InitModColours -- pseudocode

**Address**: `0x0017CC38`
**Signature**: `void SlashEntity::InitModColours(SlashEntity* this)`

The `this` parameter is unused (the function operates entirely on class-static
state). It's a static-like function decorated as a member.

```c
void SlashEntity::InitModColours(SlashEntity* /* unused */) {
    g_DirectionalFlag = 0;
    g_TrailHash       = 0;
    g_ContactHash     = 0;
    g_SecondHash      = 0;
    g_ColourCount     = 1;        // back to "single colour"
    g_ColourType      = 0;        // back to "no animation"
    g_PaletteProgress = 0.0f;

    SmartPtrNull_Tex(&g_SlashState.modTexture);  // null the overlay tex

    // Overwrite all 16 palette slots with the engine's default white colour
    // (g_DefaultWhite at 0x00268F64). Despite Ghidra's misleading decompile,
    // Colour::operator= at 0x0010C488 takes (this=r0, dst=r1, src=r2) and
    // performs *dst = *src.
    Colour stackTmp;
    for (int i = 0; i < 16; i++) {
        Colour_op_assign(&stackTmp, &g_Palette[i], &g_DefaultWhite);
    }
}
```

**Notes / caveats**:
- Does **NOT** reset the scale state (`g_Scale1..g_Scale5`,
  `g_ScaleFlag1`/`g_ScaleFlag2`). Call `SetModScales(1.0,1.0,0.0,1.0,0,1,0.0)`
  separately if a full reset is needed.
- Does **NOT** walk active entities to invoke `ColoursChanged`. The "default
  blade" path doesn't need to clear trail emitters because it leaves
  `g_DirectionalFlag = 0`, which causes UpdateTouchDown to clear the emitter
  on the next touch-up.
- Does **NOT** trigger the live-update walker. So if the player swaps to "no
  blade mod" mid-game while a swipe is active, the active emitter persists
  until the next touch-up. (Original behaviour -- preserve it.)

---

## SetModScales -- pseudocode

**Address**: `0x0017B328`
**Signature**: `void SlashEntity::SetModScales(float scale1, float scale2, float scale3, float scale4, bool flag1, bool flag2, float scale5)`

```c
void SlashEntity::SetModScales(
    float scale1,    // p1 -> g_Scale1     (lifetime divisor;     default 1.0)
    float scale2,    // p2 -> g_Scale2     (max thickness coeff;   default 1.0)
    float scale3,    // p3 -> g_Scale3     (min thickness coeff;   default 0.0)
    float scale4,    // p4 -> g_Scale4     (segment width arg;     default 1.0)
    bool  flag1,     // p5 -> g_ScaleFlag1 (ghost-on-deactivate;   default 0)
    bool  flag2,     // p6 -> g_ScaleFlag2 (UV-mirror branch;      default 1)
    float scale5)    // p7 -> g_Scale5     (some fade target;      default 0.0)
{
    g_ScaleFlag1 = flag1;
    g_ScaleFlag2 = flag2;
    g_Scale1     = scale1;
    g_Scale2     = scale2;
    g_Scale3     = scale3;
    g_Scale4     = scale4;
    g_Scale5     = scale5;
}
```

Pure global-write function -- no side effects, no live-update walker.

**ABI note**: ARM hard-float (`Tag_ABI_VFP_args: VFP registers`). Float args
arrive in `s0`-`s4` (`scale1..scale4`) and `s4` again or stack for `scale7`;
bool args (`flag1`, `flag2`) arrive in `r0`,`r1`. The disassembly confirms
this exactly:

```
0017b338: strb r0,[r4,#0x0]    ; *g_ScaleFlag1 = r0  (flag1)
0017b33c: strb r1,[r2,#0x0]    ; *g_ScaleFlag2 = r1  (flag2)
0017b342: vstr.32 s0,[r2]      ; *g_Scale1 = s0
0017b34a: vstr.32 s1,[r2]      ; *g_Scale2 = s1
0017b352: vstr.32 s2,[r2]      ; *g_Scale3 = s2
0017b35a: vstr.32 s3,[r2]      ; *g_Scale4 = s3
0017b362: vstr.32 s4,[r3]      ; *g_Scale5 = s4
```

(Ghidra mis-orders these as `param_5,param_6,param_1..param_4,param_7` in the
decompile because the AAPCS has bools-first-then-floats for int/float
arg-mixing -- the order shown above is *register* order, not *source* order.)

---

## ColoursChanged -- pseudocode (the per-instance live-update fn)

**Address**: `0x0017C41C`
**Signature**: `void SlashEntity::ColoursChanged()` (member fn, `this` in r0)
**NOT in vtable** -- called directly by SetModColours.

```c
void SlashEntity::ColoursChanged() {
    // 1. Tear down any existing trail emitter on this instance.
    if (this->m_TrailEmitter /* +0x3c */) {
        PSPParticleManager::ClearEmitter(GetInstance(), this->m_TrailEmitter);
        this->m_TrailEmitter = NULL;
    }

    // 2. If blade is currently active, re-prime per-instance state.
    if (this->m_bBladeActive /* +0x144 */) {
        this->m_PointCount /* +0x58 */ = 0;        // truncate trail geometry

        // ColourType==2 means the highlight colour is "snapshot at swipe
        // start". Re-snapshot now so the new palette takes effect immediately
        // for the in-progress swipe.
        if (g_ColourType == 2) {
            UpdateModColour(&this->m_HighlightColour /* +0x48 */, 1.0f);
        }

        // 3. Re-create trail emitter with the new hash, if directional flag
        //    indicates one is wanted.
        if (g_DirectionalFlag != 0) {
            this->m_TrailEmitter =
                PSPParticleManager::AddEmitter(g_TrailHash, NULL, true);
            if (this->m_TrailEmitter)
                this->m_TrailEmitter->followEntity /* +0x48 */ = 1;
        }
    }
}
```

**Why `m_PointCount = 0`**: existing vertex buffers were filled with the old
blade's colours/UVs. Truncating to 0 forces UpdatePoints to rebuild from
scratch with the new palette / textures.

---

## UpdateModColour reader semantics (consumes palette state)

**Address**: `0x0017B0F4`
**Signature**: `void SlashEntity::UpdateModColour(Colour* outDst, float dt)`

Called from:
- `PreUpdate @ 0x17c5cc` with `outDst=NULL, dt=frame_dt` -- when ColourType==1
- `TouchDown @ 0x17d646` with `outDst=&this->m_HighlightColour, dt=1.0` -- when ColourType==2
- `ColoursChanged @ 0x17c454` with `outDst=&this->m_HighlightColour, dt=1.0` -- when ColourType==2

```c
void SlashEntity::UpdateModColour(Colour* outDst, float dt) {
    if (dt != 0.0f) {
        if (g_ColourCount == 1) {
            // Single-entry palette: copy palette[0] straight to slashState
            // bladeColour. No interpolation.
            g_SlashState.bladeColour = g_Palette[0];
        } else {
            // Advance progress, wrap modulo colourCount.
            float progress = g_PaletteProgress + dt * g_LifeScale;
            while (progress > (float)g_ColourCount) progress -= g_ColourCount;
            g_PaletteProgress = progress;

            if (progress < 0.0f) {
                // ColourType != 0: clamp to 0 (so animation only goes forward)
                // ColourType == 0: wrap around backwards
                if (g_ColourType == 0) {
                    while (progress < 0.0f) progress += g_ColourCount;
                    g_PaletteProgress = progress;
                } else {
                    g_PaletteProgress = 0.0f;
                }
            }

            // Decide whether progress is "exact integer" (no lerp needed) or
            // fractional (lerp between two adjacent palette entries).
            float frac = progress - (float)(int)(progress + 0.5);
            const float EPS_NEG = -0.001f;  // DAT_0017b304
            const float EPS_POS =  0.001f;  // DAT_0017b308
            bool nearInt = (frac < 0)
                ? (frac > EPS_NEG)
                : (frac < EPS_POS);

            if (!nearInt) {
                // Lerp branch.
                int   i0 = ((int)progress) % g_ColourCount;
                int   i1 = ((int)progress + 1) % g_ColourCount;
                Colour c0 = g_Palette[i0];
                Colour c1 = g_Palette[i1];
                float t  = progress - (float)(int)progress;
                // Per-channel lerp, clamp to [0, 255].
                g_SlashState.bladeColour.r = clamp_u8(c0.r + (c1.r - c0.r) * t);
                g_SlashState.bladeColour.g = clamp_u8(c0.g + (c1.g - c0.g) * t);
                g_SlashState.bladeColour.b = clamp_u8(c0.b + (c1.b - c0.b) * t);
                g_SlashState.bladeColour.a = clamp_u8(c0.a + (c1.a - c0.a) * t);
                goto WRITEBACK;
            }
            // Exact integer branch: copy palette[round(progress)] straight in.
            int idx = ((int)(progress + 0.5)) % g_ColourCount;
            g_SlashState.bladeColour = g_Palette[idx];
        }
    }

WRITEBACK:
    if (outDst != NULL) {
        // For ColourType==2 (per-swipe snapshot), also copy result to caller.
        Colour stackTmp = ...;  // (Ghidra-internal, not used)
        *outDst = g_SlashState.bladeColour;
    }
}
```

**Implementer note**: there is a clamp-to-zero step (`(0.0 < f) * f`) that
zeroes negative channel values; this matches a saturating cast to `uint8`.

---

## Consumer summary -- where each field is read

| Field             | Consumer             | Address      | What it does                                        |
|-------------------|-----------------------|--------------|------------------------------------------------------|
| `g_TrailHash`     | UpdateTouchDown       | `0x17d38a`   | `m_TrailEmitter = AddEmitter(g_TrailHash)` if `g_DirectionalFlag != 0` && touch active |
| `g_TrailHash`     | ColoursChanged        | `0x17c46e`   | Same as above, on equip while swipe in progress     |
| `g_ContactHash`   | Update (slice loop)   | `0x17d9dc`+  | `AddEmitter(g_ContactHash)` at slice point per fruit hit |
| `g_SecondHash`    | DrawSlice             | `0x17e450`+  | `AddEmitter(g_SecondHash)` at blade pos on deactivation |
| `g_DirectionalFlag` | UpdateTouchDown     | `0x17d37a`   | `if (== 0)` clear emitter; `if (== 2)` rotate trail emitter to match swipe dir (CosIdx/SinIdx of -angle into emitter+0x2c, +0x30) |
| `g_DirectionalFlag` | ColoursChanged      | `0x17c45e`   | Re-create trail emitter on equip                    |
| `g_Palette[16]`   | UpdateModColour       | `0x17b1cc`+  | Per-channel lerp source                             |
| `g_ColourCount`   | UpdateModColour       | `0x17b108`   | Single vs. multi entry branch; modulo divisor       |
| `g_ColourCount`   | UpdatePoints          | (not direct, indirect via `_idivmod`) |                                       |
| `g_LifeScale`     | UpdateModColour       | `0x17b13c`   | Animation rate: `progress += dt * g_LifeScale`      |
| `g_ColourType`    | TouchDown             | `0x17d63c`   | `if (== 2)` snapshot highlight via UpdateModColour  |
| `g_ColourType`    | PreUpdate             | `0x17c5cc`   | `if (== 1)` advance bladeColour each frame          |
| `g_ColourType`    | ColoursChanged        | `0x17c448`   | `if (== 2)` re-snapshot highlight                   |
| `g_ColourType`    | UpdateModColour       | `0x17b16a`   | branch for forward-only vs. wraparound progress     |
| `g_ColourType`    | UpdatePoints          | `0x17ba7e`,`0x17bdca` | (further branch on type for vertex colour writeback) |
| `g_PaletteProgress` | UpdateModColour     | `0x17b12c`+  | accumulator                                          |
| `g_PaletteProgress` | UpdatePoints        | `0x17ba8a`   | written (likely UV scrolling tied to progress)      |
| `g_SlashState.modTexture` (`+0xd8`) | DrawSlice | `0x17e49e` | Replaces `defaultTexture` in single-pass tristrip render |
| `g_Scale1`        | UpdatePoints          | `0x17bb82`,`0x17c2d2` | Lifetime divisor for fade rate                       |
| `g_Scale2`        | GetHeadThicknessScale | `0x17b8ac`   | `maxWidth = g_Scale2 * 9.0`                         |
| `g_Scale2`        | UpdatePoints          | `0x17bb7a`,`0x17bba2`,`0x17c2c2` | Fade range upper bound                          |
| `g_Scale2`        | AddPoint              | `0x17d066`   | Per-vertex thickness factor (lerped with Scale3)    |
| `g_Scale3`        | UpdatePoints          | `0x17bb32`,`0x17bb72`,`0x17bbaa`,`0x17c2ca` | Fade range lower bound, min thickness   |
| `g_Scale3`        | AddPoint              | `0x17d070`   | Per-vertex thickness floor                          |
| `g_Scale4`        | UpdatePoints          | `0x17c1d8`   | Passed by ref to subroutine                          |
| `g_Scale5`        | UpdatePoints          | `0x17c082`   | Fade rate variant (split-screen branch)             |
| `g_ScaleFlag1`    | DrawSlice             | `0x17e444`   | `if (!= 0)` call `CreateGhost()` on deactivation    |
| `g_ScaleFlag1`    | Update                | `0x17d7e2`   | Gates ghost-spawn timer                              |
| `g_ScaleFlag2`    | UpdatePoints          | `0x17bcba`,`0x17bf3e` | Gates UV-mirror branch (split-screen UV flip) |
| `g_ScaleFlag2`    | AddPoint              | `0x17d1a0`   | Same -- gates UV setup branch                        |

The blade is rendered as a **single-pass two-tristrip** (`Mesh::DrawTriStrip`
of `m_pLeftBuffer` then `m_pRightBuffer`, both with the same texture).
Modifier blades **replace** the default texture, they do **not** overlay it.
Vertex colours come from per-vertex `+0x18` fields, which are populated from
`this->m_BaseColour` (`+0x44`) per-frame in UpdatePoints; m_BaseColour itself
is interpolated from `m_HighlightColour` (`+0x48`) by Update's blade-colour
animation block. The palette pipeline writes m_HighlightColour (or
g_SlashState.bladeColour, which UpdatePoints then folds in) with the
animated colour.

---

## DAT pool -- all literal addresses

### SetModColours literal pool (`0x0017CBB8` .. `0x0017CBE8`)

| Addr   | Value        | Meaning |
|--------|--------------|---------|
| 0017CBB4 | `0x3F800000` | float 1.0 (the "PaletteProgress reset" value loaded by `vldr.32 s15,[pc,#0x134]`) |
| 0017CBB8 | `0x0006F718` | GOT-PC anchor offset (anchor=`0x17ca18`, GOT=`0x1EC130`) |
| 0017CBBC | `0x00007330` | -> g_ColourType ptr in GOT |
| 0017CBC0 | `0x0000757C` | -> g_LifeScale ptr in GOT |
| 0017CBC4 | `0x00007998` | -> g_ColourCount ptr in GOT |
| 0017CBC8 | `0x0000763C` | -> g_Palette base ptr in GOT |
| 0017CBCC | `0x0006164C` | direct offset to g_SlashState (anchor + this = `0x24D77C`) |
| 0017CBD0 | `0x00007AB8` | -> g_PaletteProgress ptr in GOT |
| 0017CBD4 | `0x0000773C` | -> g_Random ptr in GOT |
| 0017CBD8 | `0x00007574` | -> g_DirectionalFlag ptr in GOT |
| 0017CBDC | `0x000070A4` | -> g_SecondHash ptr in GOT |
| 0017CBE0 | `0x00007980` | -> g_TrailHash ptr in GOT |
| 0017CBE4 | `0x000070E8` | -> g_ContactHash ptr in GOT |
| 0017CBE8 | `0x00007990` | -> g_GamePtr (game struct holding `+0x160` engine-up flag) |

### InitModColours literal pool (`0x0017CCAC` .. `0x0017CCD8`)

| Addr   | Value        | Meaning |
|--------|--------------|---------|
| 0017CCAC | `0x00000000` | float 0.0 (PaletteProgress reset) |
| 0017CCB0 | `0x0006F4E8` | GOT-PC anchor (anchor=`0x17cc48`, GOT=`0x1EC130`) |
| 0017CCB4 | `0x00007574` | -> g_DirectionalFlag |
| 0017CCB8 | `0x0006164C` | direct offset to g_SlashState |
| 0017CCBC | `0x00007980` | -> g_TrailHash |
| 0017CCC0 | `0x000070E8` | -> g_ContactHash |
| 0017CCC4 | `0x000070A4` | -> g_SecondHash |
| 0017CCC8 | `0x00007998` | -> g_ColourCount |
| 0017CCCC | `0x00007330` | -> g_ColourType |
| 0017CCD0 | `0x00007AB8` | -> g_PaletteProgress |
| 0017CCD4 | `0x0000763C` | -> g_Palette base |
| 0017CCD8 | `0x000073A4` | -> g_DefaultWhite (engine-wide) |

### SetModScales literal pool (`0x0017B368` .. `0x0017B384`)

| Addr   | Value        | Meaning |
|--------|--------------|---------|
| 0017B368 | `0x00070E00` | GOT-PC anchor |
| 0017B36C | `0x00007938` | -> g_ScaleFlag1 |
| 0017B370 | `0x00007788` | -> g_ScaleFlag2 |
| 0017B374 | `0x00007088` | -> g_Scale1 |
| 0017B378 | `0x000075B8` | -> g_Scale2 |
| 0017B37C | `0x0000744C` | -> g_Scale3 |
| 0017B380 | `0x00007118` | -> g_Scale4 |
| 0017B384 | `0x000079E8` | -> g_Scale5 |

### Initial values block (`0x001F3E54` .. `0x001F3E70`, in `.data`)

```
0x001F3E54: 00 00 80 3F   -> g_LifeScale = 1.0f
0x001F3E58: 01 00 00 00   -> g_ColourCount = 1
0x001F3E5C: 00 00 80 3F   -> g_Scale1 = 1.0f
0x001F3E60: 00 00 80 3F   -> g_Scale2 = 1.0f
0x001F3E64: 00 00 80 3F   -> g_Scale4 = 1.0f
0x001F3E68: 01 01 00 00   -> g_ScaleFlag2 = 1 (and one more byte = 1; padding?)
0x001F3E6C: 00 00 00 3F   -> 0.5f (unrelated, in next bss-related block)
```

### Slash-related .bss block (`0x0024D874` .. `0x0024D8D8`)

All zero by default; populated by SetModColours / SetModScales.

```
0x0024D874: g_PaletteProgress  (float)
0x0024D878: g_Palette[0]       (Colour)
0x0024D87C: g_Palette[1]       (Colour)
...        (16 entries, 64 bytes total)
0x0024D8B8: g_ColourType       (int)
0x0024D8BC: g_DirectionalFlag  (byte)  [bytes BD..BF padding to align next field]
0x0024D8C0: g_TrailHash        (uint)
0x0024D8C4: g_ContactHash      (uint)
0x0024D8C8: g_SecondHash       (uint)
0x0024D8CC: ??                 (gap; 4 bytes)
0x0024D8D0: g_Scale3           (float)
0x0024D8D4: g_Scale5           (float)
0x0024D8D8: g_ScaleFlag1       (byte)
```

---

## See also

- [SlashEntity](slash-entity.md) -- main blade entity, including DrawSlice / UpdatePoints / AddPoint pseudocode
- [Particle system](../systems/particles.md) -- PSPParticleManager, EmitterExists, AddEmitter
- [String hash](../systems/string-hash.md) -- hash function used for particle template lookup
