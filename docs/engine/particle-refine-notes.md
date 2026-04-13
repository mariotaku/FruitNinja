# Particle Refinement Notes

<!-- Diagnostic: 2026-04-13T21:00 -->

RE findings from comparing the port's `dark_blade` trail against the binary.
Actionable list ordered by visible impact. See `touch-rewrite-plan.md` for
the process template.

## Priority 1 — OBVIOUS

### 1. Spawn velocity not halved

**Binary:** `PSPParticleEmitter::AddParticle` at `0x00115644` has an
**unconditional** `local_78.x/y/z *= 0.5f` on the randomized set-level
velocity before it's stored on the particle. Applies to all modes, not just
two-player.

**Port:** `src/engine/particle/PSPParticleManager.cpp` lines 143-145 stores
the raw `RandRange(velMin, velMax)` without halving.

**Effect:** Particles fire at 2× the intended speed. For `dark_blade` with
`<velocity min="-180 -110 0" max="180 110 0"/>` the port spreads particles
over a 2× wider area in a 2× shorter time.

**Fix:** After `SpawnParticle` computes the randomized set velocity,
multiply all three components by `0.5f`.

### 2. Spin unit scale (0.01 vs 182.0)

**Binary:** `AddParticle` multiplies the LERP'd spin int16 by
`DAT_00115b64 = 182.0f` (degrees → 16-bit-angle-index scale:
`65536 / 360 ≈ 182`), then stores as an int16 angle-table index. The
spin is integrated each tick by adding this int16 to `field_0x28`.

**Port:** Uses `* 0.01f` and stores as `float` radians/second.

**Effect:** Particles visually do not spin (scale is ~1820× too small).
`dark_blade`'s `darkstuff` template uses `<spin startMin="0" startMax="-3"
endMin="-3" endMax="0"/>` so the port shows effectively zero rotation.

**Fix:** Either (a) port the int16-angle-index system exactly, or
(b) convert the raw int16 to rad/sec at spawn time:
`rad_per_sec = int16_val * (182.0f / 65536.0f) * 2π * FRAME_HZ`. Since
the port uses rad and Euler integration, option (b) is simpler.

## Priority 2 — SUBTLE

### 3. Trail emitter position lags touch by up to 64 units

**Binary:** `SlashEntity::UpdateTouchDown` at `0x0017D2E4` writes
`m_TrailEmitter->m_Pos = this->base.pos` each frame — where `this->base.pos`
is the **raw touch input position**, not the last interpolated trail point.

**Port:** `src/entities/SlashEntity.cpp:321` writes
`m_TrailEmitter->m_Pos = m_Points[m_NumPoints-1].center`, which is the
last interpolated `AddPoint` position. Because `OnTouchActive` spaces
points at `POINT_SPACING = 64.0f` units, the emitter can lag the real
finger by up to 64 units on fast swipes.

**Effect:** Subtle — trail particles spawn behind the finger on fast
swipes. Noticeable side-by-side with the binary.

**Fix:** Store the raw touch position in `OnTouchActive` (e.g.
`m_RawTouchPos`), write that to `m_TrailEmitter->m_Pos` in the `Update`
trail-emitter block instead of `m_Points[m_NumPoints - 1].center`.

### 4. Gravity LERP damping factor missing from integration

**Binary:** `PSPParticleManager::Draw` at `0x00114c64` integrates as:
```c
velx = (old_velx + dt * gravity_x) * lerp(velMin.x, velMax.x, t);
posx = old_posx + velx * dt;
```
where the LERP factor is per-component and per-frame — an additional
damping/attraction applied every tick. Note that for `dt < 0.025` the
half-step branch at the top is skipped (sub-step only for slow frames).

**Port:** `src/engine/particle/PSPParticleManager.cpp:260-261` does plain
Euler:
```cpp
p.m_Vel += p.m_Gravity * dt;
p.m_Pos += p.m_Vel * dt;
```
No multiplicative damping.

**Effect:** Particles fall on slightly different arcs than the binary.
Visible but secondary to #1/#2.

**Fix:** Apply the velocity-lerp factor in `UpdateEmitter` particle
integration. Requires passing `t = age / life` and reading
`tmpl->m_VelocityMin/Max` as a per-component LERP factor (not min/max
of the initial velocity as currently interpreted — this is the most
confusing part of the binary, since `m_VelocityMin/Max` in the template
is actually a gravity-damping pair, not an initial-velocity range).

## Priority 3 — NONE (confirmed correct)

### 5. `<life>` frame→second conversion
Binary divides by `DAT_001161e8 / DAT_001170a0 = 60.0f`. Port matches.

### 6. Colour byte scale
Binary uses `DAT_001166c4 = 255.0 / 31.0 ≈ 8.226`. Port matches.

### 7. Blend mode plumbing
Binary stores DESTINATION factor in `m_BlendMode` (last-write-wins between
`<SourceBlend>` and `<DestinationBlend>` because both write the same field).
Port's `Draw` treats `m_BlendMode` as the destination factor and calls
`glBlendFunc(GL_SRC_ALPHA, dstFactor)`, which is structurally correct.
`Mortar::Texture::Set` does NOT set GL blend state in the binary — the
port's per-flush `glBlendFunc` call is more defensive but functionally
equivalent for the `dark_blade` additive path.

### 8. Gravity value units
Both binary and port store `<gravity>` text-parsed as-is (no `/60` or
scale). Confirmed via binary `ParseInt3` → `(float)(longlong)local_38`.

## Out of scope

- Grid-lock snap (only `rim_spark` uses it)
- Friction deflection (dead in binary Draw)
- Ghost trail (separate task, `SlashEntityGhost` is ~500 lines)
- 3-stop colour lerp (Priority 2 item; deferred as "Tier A Phase 3")
- Directional emitter rotation (only matters for `particles_directional`
  mods; `dark_blade` doesn't use it)

## Order of implementation

1. **#1 velocity halving** — one-liner, biggest visible win
2. **#2 spin scale** — one-liner, removes one more obvious mismatch
3. **#3 emitter position** — small, improves fast-swipe feel
4. **#4 gravity lerp damping** — nontrivial, defer until 1-3 done

Each should be its own commit so bisect is easy.

## See also

- `docs/engine/particles.md` — full struct layouts + Draw flow
- `src/engine/particle/PSPParticleManager.{h,cpp}` — port impl
- `src/entities/SlashEntity.cpp` — trail emitter owner
- Binary: `AddParticle @ 0x00115644`, `UpdatePoints @ 0x0017B92C`,
  `Draw @ 0x00114C64`, `LoadFile @ 0x00115F60`, `UpdateTouchDown @ 0x0017D2E4`
