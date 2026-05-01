# Fruit Shadows + Bomb Flashes — Stubbed-but-wired draw paths

Analysed: 2026-05-02. Both functions are called from the GameDraw post-actor pass (right before HUD layer 0x80). Both are currently no-op stubs in the port.

The two have very different statuses in the binary:

| Function | Binary addr | Status in binary | Action for port |
|---|---|---|---|
| `Fruit::DrawShadows()` | `0x00178f28` | **Real, fully functional**. ~30 lines. | Implement (see below). |
| `BombFlash::DrawActiveFlashes()` | `0x0017102c` | **Stripped/empty (`bx lr` only).** Confirmed via `disassemble_function`. | **Leave stubbed.** Doc the discovery; do not invent geometry. |

Both `0x0016ba6e` and `0x0016baf0` (the addresses GameDraw branches to) are PLT thunks resolving via the GOT. Real bodies are at the addresses above.

---

## 1. `Fruit::DrawShadows()` — implement

### 1a. Body (binary `0x00178f28`)

```c
void Fruit::DrawShadows() {
    // Header gate: FRUIT_INFO[+0xc0] = g_FruitShadowTex. Skip the whole pass
    // when the localised shadow texture wasn't loaded (slow hardware path).
    if (FRUIT_INFO_HEADER->shadowTex == nullptr) return;

    QUADCUSTOMVERTEX quadBuf[18432];   // ~3072 quads worth of stack scratch
    QUADCUSTOMVERTEX* writePtr = quadBuf;
    int  quadCount = 0;

    auto* am = ActorManager::GetInstance();
    Fruit* f = (Fruit*)am->GetEntityFirst(/*type=*/0, it);
    while (f) {
        // Gate: scale.x > 0 (skips entities mid-respawn / dead).
        if (f->scale.x > 0.0f) f->AddShadow(&writePtr, &quadCount);
        f = (Fruit*)am->GetEntityNext(0, it);
    }

    // World matrix → identity (shadows are pre-projected screen-space quads).
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorld().Reset();
    mm.UploadCurrentMatrices(/*uploadView=*/true);

    Texture::Set(FRUIT_INFO_HEADER->shadowTex);
    Mesh::DrawTriStrip(quadBuf, quadCount * 6 - 1, /*useIndexed=*/false, nullptr);
    Texture::UnSet(FRUIT_INFO_HEADER->shadowTex);
}
```

The `quadCount * 6 - 1` is the binary's tri-strip primitive count. AddQuad emits 6 verts per quad as two separate triangles in the buffer; the `-1` is the binary's literal arg pattern (likely a quirk of `Mesh::DrawTriStrip` counting strip primitives).

### 1b. `Fruit::AddShadow(QUADCUSTOMVERTEX**, int*)` — binary `0x00175ea0`

Per-fruit shadow geometry. Writes 1 quad for unsliced fruit, 3 quads (whole + two halves) when `m_ScaleAnim > 0` and the fruit has been split. Resolved DATs:

| DAT | Value | Meaning |
|---|---|---|
| `DAT_00175e98` | `0.0f` | UV `u` written into every shadow vert |
| `DAT_00175e9c` | `-5000.0f` | Shadow Z (deep behind playfield) |
| `DAT_00176160` | `0.0f` | Default `mirrorY` |
| `DAT_00176164` | `230.0f` | Spawn-fade alpha mult `(1 − scaleAnim) × 230` |
| `DAT_00176168` | `82.0f` | Whole-fruit shadow half-size base |
| `DAT_0017616c` | `-0.65f` | Whole-shadow X/Y offset mult |
| `DAT_00176170` | `100.0f` | Post-spawn alpha mult `scaleAnim × 100` |
| `DAT_00176174` | `50.0f` | Sliced-half shadow half-size base |
| `DAT_00176178` | `-0.45f` | Sliced-half X/Y offset mult |
| `DAT_00176180` | `→ Vec3` | Slice-plane axis (BSS singleton, `(0,0,1)`) |

```c
void Fruit::AddShadow(QUADCUSTOMVERTEX** outVerts, int* outCount) {
    // Same-screen multiplayer mirror flag (for player2's lower half).
    float mirrorX = 1.0f, mirrorY = 0.0f;
    if (m_PlayerIdx >= 1 && IsSameScreenMultiplayer()) {
        mirrorX = 0.0f;
        mirrorY = (pos.x < 0.0f) ? 1.0f : -1.0f;
    }

    // --- Quad 1: spawn-fade whole-fruit shadow (m_ScaleAnim < 1) ---
    if (m_ScaleAnim < 1.0f) {
        int   a = (int)((1.0f - m_ScaleAnim) * 230.0f);
        uint8_t alpha = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        Colour col(255, 255, 255, alpha);

        float halfSize = 82.0f * scale.x;             // m_ScaleAnim NOT applied
        float ox = mirrorY * halfSize * -0.65f;
        float oy = mirrorX * halfSize * -0.65f;
        AddQuad(outVerts, pos.x + ox, pos.y + oy, halfSize, halfSize, col);
        ++(*outCount);
    }

    // --- Quad 2 + 3: per-half shadows (m_ScaleAnim > 0) ---
    if (m_ScaleAnim > 0.0f) {
        int   a = (int)(m_ScaleAnim * 100.0f);
        uint8_t alpha = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        Colour col(255, 255, 255, alpha);

        float halfSize = scale.x * 50.0f;
        Vec3 axis = *g_SlicePlaneAxis;                // BSS singleton, (0,0,1)
        Matrix33 m;

        // Half A — rotated by m_Rot1, anchored at pos.
        m_Rot1.ToMatrix33(m);
        Vec3 dirA = m * axis;                         // axis transformed by Rot1
        Vec3 anchorA = pos + (dirA * 0.5f);           // local_54 = 0.5
        float ox = mirrorY * halfSize * -0.45f;
        float oy = mirrorX * halfSize * -0.45f;
        AddQuad(outVerts, anchorA.x + ox, anchorA.y + oy, halfSize, halfSize, col);
        ++(*outCount);

        // Half B — rotated by m_Rot2, anchored at m_SecondPos.
        m_Rot2.ToMatrix33(m);
        Vec3 dirB = m * axis;
        Vec3 anchorB = m_SecondPos + (dirB * 0.5f);
        AddQuad(outVerts, anchorB.x + ox, anchorB.y + oy, halfSize, halfSize, col);
        ++(*outCount);
    }
}
```

Both quads are emitted **regardless of `m_bSliced`** — the unsliced case still runs the half-quad branch, which produces two heavily-overlapped shadows under the whole fruit. That overlap is intentional; it's the cross-fade as a fruit transitions from spawn (whole, big shadow) to active (two near-coincident half shadows).

### 1c. `AddQuad` (binary `0x00175db0`) — geometry helper

Writes 6 `QUADCUSTOMVERTEX`s as two triangles forming a centered axis-aligned quad:

```
v0: (cx-w, cy-h)    v3: (cx+w, cy-h)
v1: (cx-w, cy+h)    v4: (cx-w, cy+h)
v2: (cx+w, cy-h)    v5: (cx+w, cy+h)
```

All 6 verts share `z = -5000`, normal `(0, 0, 1)`, packed `colour`, and per-vert UVs picked from the corner pattern (u, v ∈ {0.0, 1.0}). Advances `*outVerts` by 6 entries.

### 1d. Port-side action list

1. **Add `Fruit::AddShadow` member** (private static-callable helper that writes into the shared buffer). Signature: `void AddShadow(QUADCUSTOMVERTEX** outVerts, int* outCount)`.
2. **Already loaded:** `g_FruitShadowTex` exists in `src/entities/FruitInfo.cpp` line 52 as `static SmartPtr<Mortar::Texture> g_FruitShadowTex;` — already loaded by `FruitInfo_Load` step 0 (`fruit_shadow.tex`). Port code needs to expose it (extern or accessor) so `Fruit.cpp` can read it. Recommended: small accessor `Mortar::Texture* FruitInfo_GetShadowTex();` in `FruitInfo.h`.
3. **Slice plane axis:** binary reads `*g_SlicePlaneAxis` (a Vec3 BSS singleton initialised by `_GLOBAL__I_Fruit.cpp` to `(0, 0, 1)` — same provenance as `g_FruitTint1` already in `Fruit.cpp` lines 30-34). Just hard-code `Vec3 axis(0.0f, 0.0f, 1.0f)` locally; the BSS slot is never reassigned at runtime.
4. **`m_PlayerIdx` field is missing** from `src/entities/Entity.h` — Grep confirmed no match. The mp-mirror branch can be skipped (substitute `mirrorX = 1.0f, mirrorY = 0.0f` always) until same-screen MP wires the field. Add `// DIFFERS: m_PlayerIdx not ported, MP shadow mirror skipped`.
5. **Buffer sizing:** binary uses 18432 verts on the stack. Port can use a static module-local `QUADCUSTOMVERTEX s_ShadowVerts[3 * MAX_FRUIT * 6]` matching the BombBlast pattern in `BombBlast.cpp`. With `MAX_FRUIT ~= 64` that's 1152 verts, well under the binary's headroom.
6. **Wire into `GameDraw`'s post-actor pass:** the call site is already there (look for `Fruit::DrawShadows();` in the port's GameDraw equivalent). Replace the stub at `Fruit.cpp` line 1214.
7. **`Mesh::DrawTriStrip` count:** the binary literally passes `count * 6 - 1`. With `Renderer::DrawTriList` available (used by BombBlast), pass `count * 6` instead — same geometry interpreted as a tri-list, no `-1` quirk.

### 1e. Ready-to-paste snippet

```cpp
// Fruit.cpp — replace the 2-line stub at line 1213.
//
// Matches Fruit::DrawShadows (0x00178f28) + AddShadow (0x00175ea0).
// Texture: fruit_shadow.tex (loaded by FruitInfo_Load step 0).
// Geometry: 1 fade-out quad while spawning, 2 half-quads when active.
void Fruit::DrawShadows() {
    Mortar::Texture* shadowTex = FruitInfo_GetShadowTex();   // see action 2
    if (!shadowTex) return;

    static QUADCUSTOMVERTEX s_Buf[64 * 3 * 6];
    QUADCUSTOMVERTEX* w = s_Buf;
    int count = 0;

    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;
    std::list<Entity*>::iterator it;
    Entity* e = am->GetEntityFirst(0, it);
    while (e && count + 3 <= 64 * 3) {
        Fruit* f = static_cast<Fruit*>(e);
        if (f->scale.x > 0.0f) f->AddShadow(&w, &count);
        e = am->GetEntityNext(0, it);
    }
    if (count == 0) return;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    shadowTex->Set();
    if (Renderer* r = Renderer::GetInstance())
        r->DrawTriList(s_Buf, count * 6);
    shadowTex->UnSet();
}

// Helper — matches Fruit::AddShadow @ 0x00175ea0.
void Fruit::AddShadow(QUADCUSTOMVERTEX** out, int* outCount) {
    // DIFFERS: m_PlayerIdx not ported; same-screen MP mirror disabled.
    const float mirrorX = 1.0f;
    const float mirrorY = 0.0f;

    if (m_ScaleAnim < 1.0f) {
        int a = (int)((1.0f - m_ScaleAnim) * 230.0f);
        uint8_t al = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        float hs = 82.0f * scale.x;
        float ox = mirrorY * hs * -0.65f;
        float oy = mirrorX * hs * -0.65f;
        AddQuad(out, pos.x + ox, pos.y + oy, hs, hs, Colour(255, 255, 255, al));
        ++(*outCount);
    }

    if (m_ScaleAnim > 0.0f) {
        int a = (int)(m_ScaleAnim * 100.0f);
        uint8_t al = (a < 1) ? 0 : (a > 254 ? 255 : (uint8_t)a);
        float hs = scale.x * 50.0f;
        Vec3 axis(0.0f, 0.0f, 1.0f);            // g_SlicePlaneAxis singleton

        Matrix33 m;
        m_Rot1.ToMatrix33(m);
        Vec3 anchorA = pos + (m * axis) * 0.5f;
        float ox = mirrorY * hs * -0.45f;
        float oy = mirrorX * hs * -0.45f;
        AddQuad(out, anchorA.x + ox, anchorA.y + oy, hs, hs, Colour(255,255,255,al));
        ++(*outCount);

        m_Rot2.ToMatrix33(m);
        Vec3 anchorB = m_SecondPos + (m * axis) * 0.5f;
        AddQuad(out, anchorB.x + ox, anchorB.y + oy, hs, hs, Colour(255,255,255,al));
        ++(*outCount);
    }
}
```

`AddQuad` already exists in port-equivalent form via `BombBlast.cpp`'s vertex layout — extract it into a small shared helper in `src/render/QuadBuffer.{h,cpp}` (writes 6 `QUADCUSTOMVERTEX`s to a buffer pointer with the corner pattern documented in §1c) and call from both BombBlast::DrawActiveBlasts and Fruit::AddShadow.

---

## 2. `BombFlash::DrawActiveFlashes()` — leave stubbed

### 2a. Binary verdict

```
disassemble_function 0x0017102c:
  0017102c: bx lr
```

Single-instruction empty body. The Bada port shipped this stripped. Likewise `UpdateActiveFlashes` (`0x00171028`) — also a single `bx lr` (cf. `docs/entities/bomb-flash.md` lines 180-198, where this was already noted).

### 2b. What this means for the port

- The flash visual that hits the screen on bomb-strike is **not** rendered by `BombFlash::DrawActiveFlashes`. It comes from `DrawBombHit()` (called later in GameDraw, gated on `Game.bombHitTimer > 0`) plus `BombBlast::DrawActiveBlasts` (the shockwave kite). Both already render in the port.
- `BombFlash::MakeFlash` (`0x001723f4`) **is** real — it activates a pool slot and runs `Update` once. `Update` (`0x00171038`) is also real, animating `m_Scale`/`m_CurrentAlpha`. So the pool ticks, but nothing reads `m_CurrentAlpha`/`m_Scale` for rendering.
- Conclusion: the BombFlash class is dead-render code in the shipped Bada binary. The port's current stub at `BombFlash.cpp` line 50 (`void BombFlash::DrawActiveFlashes() {}`) is byte-correct. **Do not add geometry.**

### 2c. Port-side action list

1. **No code change.** `BombFlash::DrawActiveFlashes` stays empty.
2. **Update the comment** at `BombFlash.cpp` line 49 from `(TODO: real draw)` to `(empty in binary — bx lr only, confirmed via Ghidra disassemble)`. Likewise the `TODO: real impl pending` comment on `BombFlash::Update` at line 19 should change to a verified note: `Update is real (0x00171038, ASM-decoded 2026-05-02), but its outputs (m_Scale, m_CurrentAlpha) are never rendered because DrawActiveFlashes is stripped.`
3. **Optional cleanup:** since `m_CurrentAlpha`/`m_Scale_x..z` are never read by anyone in the binary (or the port), the BombFlash struct's render fields could stay padded as-is. Do not delete them — `MakeFlash` writes them and the binary's struct layout depends on the offsets.
4. **If a future re-RE finds the visual elsewhere** (iOS/PSP version's `0x00105a20` thunk goes through `PTR_DrawActiveFlashes_001f22b0` — that pointer might land on a non-empty body in another platform binary), revisit. For Bada, this is settled.

### 2d. (No snippet — the function is intentionally empty.)

---

## Cross-references

- GameDraw call sites: `Fruit::Fruit_DrawShadows()` and `BombFlash::DrawActiveFlashes()` are called sequentially in the post-actor pass, between `SplatEntity::DrawActiveSplats()` and `HUD::Draw(0x80)`. See decompiled GameDraw at `0x0016b898` for the full sequence.
- Existing port stubs: `src/entities/Fruit.cpp:1213-1216`, `src/entities/BombFlash.cpp:49-50`.
- Shadow texture loader: `src/entities/FruitInfo.cpp:51-63` (`g_FruitShadowTex`, `fruit_shadow.tex`).
- Geometry helper to extract: `BombBlast.cpp` already builds `QUADCUSTOMVERTEX` strips identically — share the writer.
- Existing docs: `docs/entities/fruit.md:1122-1137` (DrawShadows summary), `docs/entities/bomb-flash.md:189-199` (already flagged as stubbed).
