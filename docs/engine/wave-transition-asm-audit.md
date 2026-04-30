# Wave-end Transition ASM Audit

ASM-level audit of the wave-end transition pipeline (`UpdateWave` /
`IsWaveProcessing` / `GetNextWave` writeback) against `FruitNinja.exe`,
prompted by the gameplay_classic ctest showing only ~1 transition per 3 s.

Binary entry points (Ghidra @ `FruitNinja.exe`):

- `WaveManager::UpdateWave` -- 0x00125390
- `WaveManager::IsWaveProcessing` -- 0x00122a40
- `WaveManager::GetNextWave` -- 0x00124f10

Date: 2026-04-30. All ASM lines below quoted directly from
`disassemble_function`; addresses are absolute file offsets.

---

## 1. Per-function status

| Function                                            | Status   |
|-----------------------------------------------------|----------|
| `UpdateWave` wave-end block (port lines 754-763)    | CRITICAL |
| `UpdateWave` wave-not-set early return (line 694)   | CRITICAL |
| `UpdateWave` WaveTimer countdown (lines 697-702)    | CRITICAL |
| `IsWaveProcessing` (port lines 928-955)             | DIVERGES |
| `GetNextWave` nextDelay/wait writeback (lines 863-882) | DIVERGES |
| `WAVE_INFO` field offsets (post-rename `debff12`)   | MATCHES  |

The "1 transition / 3 s" symptom is explained by issues 1 + 2 + 3 + 5
together: the port double-charges the per-wave delay (writes 0.6 s into
its own `m_NextWaveDelay[]` array that the binary doesn't have),
`IsWaveProcessing` returns true longer than the binary
(`Bomb::GetNumActiveForPlayer` arg flipped), and the wave-not-set
fast-return blocks the rescue path that would call `GetNextWave` when
`m_pCurrentWave` is null.

---

## 2. Field-slot map (binary truth)

The binary uses **two overlapping per-player float[2] arrays** anchored at
+0x234 and +0x238:

| Offset      | Player 0     | Player 1     | Role                                           |
|-------------|--------------|--------------|------------------------------------------------|
| +0x234      | delay[0]     | --           | "delay" XML attr -- pre-spawn timer AND end-of-wave timer (single slot) |
| +0x238      | wait[0]      | delay[1]     | "wait" XML attr (p0); "delay" (p1) -- aliased  |
| +0x23c      | flag[0]      | wait[1]      | wave-was-spawned flag byte (p0); "wait" (p1) -- aliased |
| +0x23d      | flag[1]      | --           | flag byte (p1) / PROBABILITY_OVERIDE flags     |

Critical consequence: in single-player Classic, the **wave-end timer reads
from +0x238 (the "wait" slot)**, not +0x234. The pre-spawn timer reads
from +0x234. They are **the same slot in storage** for player 0 only when
the wave-end is reached -- the binary's writeback to +0x234 in
`GetNextWave` then becomes the next wave's pre-spawn delay (so the same
"delay" XML attr serves both roles, sequentially).

Port-side: `m_WaveTimer[]` and `m_NextWaveDelay[]` are **two separate
arrays elsewhere in the class** (after `+0x2cc`). The port writes
`m_NextWaveDelay[p] = field_0x234` at line 882, then reads
`m_NextWaveDelay[p]` at line 756 -- which is a different slot than the
binary's +0x238. This double-counts the delay (pre-spawn + post-spawn use
the same value).

---

## 3. UpdateWave wave-end block diff

Binary @ `0x0012593a..0x0012597e`:

```
0012593a: ldr   r3, [0x001259c0]              ; r3 = DAT_GameInstance offset
0012593c: adds  r5, r5, r3                    ; r5 = &game
0012593e: ldrb  [r5, #0x470]                  ; load game->field_0x470 (spawn-this-frame sentinel)
00125942: cbnz  r3, 0x001259a4                ; if spawned this frame, return (no wave-end yet)
00125944: add   r3, r4, r6, lsl #2            ; &this->m_pCurrentWave[p]
00125948: ldr   r3, [r3, #0x22c]
0012594c: cbz   r3, 0x0012597a                ; if wave==null, jump straight to GetNextWave
0012594e: add   r3, r6, #0x8e                 ; r3 = playerIdx + 0x8e
00125952: add   r2, r4, r3, lsl #2            ; r2 = &this[+0x238 + playerIdx*4]
00125956: vldr  s15, [r2]                     ; load wait[p]  (NOT delay[p])
0012595a: vcmpe s15, #0
00125962: ble   0x0012597a                    ; if <=0 -> GetNextWave
00125964: vsub  s17, s15, s17                 ; -= dt
00125974: str   r12, [r4, r3, lsl #2]         ; write back
00125978: bgt   0x001259a4                    ; if still >0 -> return
0012597a: blx   WaveManager::GetNextWave
```

Port @ `WaveManager.cpp:754-763`:

```cpp
if (!IsWaveProcessing(playerIdx)) {
    float nextDelay = m_NextWaveDelay[playerIdx];   // reads PORT-OWNED slot
    if (nextDelay > 0.0f) {
        nextDelay -= dt;
        m_NextWaveDelay[playerIdx] = nextDelay;
        if (nextDelay > 0.0f) return;
    }
    GetNextWave(playerIdx);
}
```

| Binary @ | Port line | Difference                                                                 |
|----------|-----------|----------------------------------------------------------------------------|
| 0x0012593e..42 | (missing) | Binary gates the entire wave-end block on `game->field_0x470 == 0` (spawn-this-frame sentinel). Port lacks this. |
| 0x0012594c | 754-755 (precondition) | Binary: `if (wave==null) GetNextWave()`. Port early-returns at line 694 (`if (!wave) return;`) -- **prevents recovery when wave is null**. |
| 0x00125956 | 756       | Binary reads `field_0x238 + p*4` (the **wait** slot). Port reads `m_NextWaveDelay[p]` (own array). |
| 0x00125974 | 759       | Binary writes back to same +0x238+p*4 slot. Port writes back to `m_NextWaveDelay[p]`. |

**Effect:** in single-player with `<NextWaveDelay delay="0.6" wait="0"/>`,
the binary's wave-end timer is `wait=0` (default) -> 0 s end delay,
transition fires immediately when `IsWaveProcessing` returns false. The
port reads `delay=0.6` from its own slot -> 0.6 s end delay, plus the
0.6 s also applied at next wave's pre-spawn (line 698). Total: ~1.2 s
extra per transition vs binary, on top of `IsWaveProcessing`
divergences.

---

## 4. UpdateWave WaveTimer / wave-not-set diff

Binary @ `0x001253ee..0x00125984`:

```
001253ee: add   r3, r4, r6, lsl #2
001253f2: ldr   r3, [r3, #0x22c]              ; r3 = m_pCurrentWave[p]
001253f6: cmp   r3, #0
001253f8: bne.w 0x00125984                    ; non-null -> WaveTimer check
001253fc: b     0x00125930                    ; null -> wave-end logic (FALL THROUGH to IsWaveProcessing)
[...]
00125984: add   r3, r6, #0x8c
00125988: add   r3, r4, r3, lsl #2
0012598c: vldr  s15, [r3, #0x4]               ; reads +0x234+p*4 (delay slot)
00125990: vcmpe s15, #0
00125998: bls.w 0x001253fe                    ; if <=0 -> spawning loop
0012599c: b     0x0012591e                    ; if >0 -> WaveTimer -= dt; return
```

Then at `0x0012591e`:
```
0012591e: vsub  s15, s15, s17                 ; WaveTimer -= dt
00125922: ldr   r2, [0x001259c0]
00125928: strb  [r2, #0x470], 1               ; game->field_0x470 = 1 (sentinel)
0012592c: vstr  s15, [r3, #0x4]               ; write back +0x234+p*4
00125930: <fall through to IsWaveProcessing>
```

Port @ `WaveManager.cpp:693-702`:

```cpp
WAVE_INFO* wave = m_pCurrentWave[playerIdx];
if (!wave) return;                          // PORT: hard return when null

float waveTimer = m_WaveTimer[playerIdx];   // PORT: reads own m_WaveTimer slot
if (waveTimer > 0.0f) {
    m_WaveTimer[playerIdx] = waveTimer - dt;
    return;                                  // PORT: hard return on positive timer
}
m_WaveTimer[playerIdx] = 0.0f;
```

| Binary @ | Port line | Difference                                                                 |
|----------|-----------|----------------------------------------------------------------------------|
| 0x001253fc | 694      | Binary falls through to `IsWaveProcessing`+`GetNextWave` when `wave==null`. Port returns -- **no recovery**. |
| 0x0012598c | 697      | Binary reads `[+0x234+p*4]`. Port reads own `m_WaveTimer[]` array. |
| 0x00125928 | (missing) | Binary sets `game->field_0x470 = 1` whenever the WaveTimer is positive. Port doesn't track. |
| 0x00125930 | 700-701  | Binary falls through to `IsWaveProcessing` after WaveTimer decrement (does not return). Port `return;` at line 700 skips the wave-end check entirely. |

**Effect:** port does not run `IsWaveProcessing` while `m_WaveTimer > 0`,
which is correct for the spawn-skip path (bug-tolerant) but masks the
above wave-end aliasing bug (the port never reaches its wave-end block
during the pre-spawn delay; the binary does, but +0x234 == WaveTimer slot
itself, so the same value drives the wait countdown after the wave
processes -- aliasing is by design, not a bug).

---

## 5. IsWaveProcessing diff

Binary @ `0x00122a40..0x00122ad6`:

```
00122a48: ldrb  r3, [r0+r1, #0x23c]           ; flag = (&field_0x23c)[p]
00122a4e: beq   0x00122acc                    ; flag==0 -> return 0
00122a50: cbnz  r1, 0x00122aac                ; p!=0 -> p1 path
[p0 path]
00122a52: ldr   r3, [r0, #0x22c]              ; wave = m_pCurrentWave[0]
00122a56: cbz   r3, 0x00122a78                ; wave==null -> ActorManager check
00122a58: ldrb  r2, [r3, #0x39]               ; m_bWaitForProcessing
00122a5c: cbz   r2, 0x00122ac4                ; ==0 -> clear flag, return 0
00122a5e: ldrb  r3, [r3, #0x38]               ; m_bWaitForEntities
00122a62: cbnz  r3, 0x00122a78                ; !=0 -> ActorManager check
00122a64: mov   r0, #-1                       ; arg0 = -1
00122a68: blx   Fruit::GetNumActiveForPlayer  ; (arg1 = r1 = p = 0; not set, leftover)
00122a6c: cmp   r0, #0
00122a6e: bgt   0x00122ad4                    ; >0 -> return 1
00122a70: mov   r0, #-1
00122a74: mov   r1, r4                        ; arg1 = playerIdx (= 0)
00122a76: b     0x00122abc
00122abc: blx   Bomb::GetNumActiveForPlayer   ; arg0=-1, arg1=0
00122ac0: bgt   0x00122ad4                    ; >0 -> return 1
[clear flag, return 0]
[p1 path: arg = (p, true) for Fruit then Bomb]
```

Port @ `WaveManager.cpp:928-955`:

```cpp
if (Fruit::GetNumActiveForPlayer(-1, false) >= 1) return true;       // p=0 path
if (Bomb::GetNumActiveForPlayer(-1, true) >= 1) return true;         // <-- 'true'
[...]
if (Fruit::GetNumActiveForPlayer(playerIdx, true) >= 1) return true; // p1 path
if (Bomb::GetNumActiveForPlayer(playerIdx, true) >= 1) return true;
```

| Binary @ | Port line | Difference                                                                |
|----------|-----------|---------------------------------------------------------------------------|
| 0x00122a76..bc | 940 | p=0 calls `Bomb::GetNumActiveForPlayer(-1, **0**)` (arg2=false). Port passes `**true**`. **DIVERGES.** |
| 0x00122a48 | 930 | Binary loads byte stride: `(&field_0x23c)[p]`. Port matches.            |
| 0x00122a58 vs a5e order | 937-938 | Binary: check `+0x39` (`m_bWaitForProcessing`) FIRST, then `+0x38` (`m_bWaitForEntities`). Port matches. |
| 0x00122a78..aa | (missing precise gating) | Binary: when wave==null OR `bWaitForEntities!=0`, calls `ActorManager::GetNumEntities(am, 0)`, then conditionally `GetNumEntities(am, 1)` only if `!IsMultiplayer()`. Port checks both unconditionally. Single-player only -> equivalent in practice. |

**Effect:** the `(-1, true)` Bomb call in port aggregates all bombs,
including those that were never assigned a player and bombs flagged for
"don't count toward processing" (binary uses `false` to filter those
out). Result: port keeps `IsWaveProcessing == true` longer than binary.
Combined with the wave-end double-delay above, this is the dominant
contributor to the ~3 s observed transition cadence.

---

## 6. GetNextWave nextDelay/wait writeback diff

Binary @ `0x001251cc..0x00125258`:

```
001251cc: add   r3, r4, r5, lsl #2
001251d0: add   r7, r5, #0x8c                 ; r7 = playerIdx + 0x8c
001251d4: ldr   r3, [r3, #0x22c]              ; wave = m_pCurrentWave[p]
001251d8: vldr  s1, [r3, #0x20]               ; wave->m_NextWaveDelay
001251dc: vcmpe s1, #0
001251e4: bhi   0x001251f4                    ; >0 -> compute scaled delay
001251e6: add   r7, r4, r7, lsl #2            ; r7 = &this[+0x230+p*4]
001251ea: vldr  s15, [pc, #0x13c]             ; s15 = 0.0
001251ee: vstr  s15, [r7, #0x4]               ; *(+0x234+p*4) = 0
001251f2: b     0x00125210
001251f4: vldr  s15, [r3, #0x34]              ; field_0x34 (revisit counter)
001251f8: vldr  s14, [r3, #0x24]              ; m_NextWaveDelayInc
001251fc: vldr  s0,  [pc, #0x12c]             ; s0  = 0.05f (clamp floor)
00125200: vmla  s1, s14, s15                  ; s1 = m_NextWaveDelay + inc * revisit
00125204: blx   Math::Max<float>              ; max(s1, 0.05)
00125208: add   r7, r4, r7, lsl #2
0012520c: vstr  s0, [r7, #0x4]                ; *(+0x234+p*4) = result
00125210: add   r2, r4, r5, lsl #2
00125214: add   r3, r5, #0x8e                 ; r3 = playerIdx + 0x8e
00125218: ldr   r2, [r2, #0x22c]              ; wave
0012521c: vldr  s15, [r2, #0x28]              ; wave->m_NextWaveWait
00125220: vmov  r1, s15
00125224: str   r1, [r4, r3, lsl #2]          ; *(+0x238+p*4) = wait
00125228: vldr  s14, [r2, #0x30]              ; wave->m_NextWaveWaitSpInc
0012522c: vcmpe s14, #0
00125234: beq   0x0012525c                    ; ==0 -> done
00125236: add   r2, r4, r5, lsl #2
0012523a: vldr  s13, [r2, #0x54]              ; m_Speed[p]
0012523e: vmla  s15, s14, s13                 ; s15 = wait + spinc * speed
00125242: vldr  s14, [pc, #0xe8]              ; s14 = 0.05
00125246: vcmpe s15, s14
0012524e: it    le
00125250: vmov.le.f32 s15, s14                ; clamp >=0.05
00125254: vmov  r2, s15
00125258: str   r2, [r4, r3, lsl #2]          ; *(+0x238+p*4) = clamped
```

Port @ `WaveManager.cpp:863-882`:

```cpp
if (wave->m_NextWaveDelay > 0.0f) {
    float delay = wave->m_NextWaveDelay + wave->m_NextWaveDelayInc * wave->field_0x34;
    if (delay < 0.05f) delay = 0.05f;
    field_0x234 = delay;                       // p=0 only; ignores playerIdx
} else {
    field_0x234 = 0.0f;                        // p=0 only
}
{
    float wait  = wave->m_NextWaveWait;
    float spinc = wave->m_NextWaveWaitSpInc;
    if (spinc != 0.0f) {
        float w2 = wait + spinc * m_Speed[playerIdx];
        if (w2 <= 0.05f) w2 = 0.05f;
        wait = w2;
    }
    field_0x238 = wait;                        // p=0 only
}
m_NextWaveDelay[playerIdx] = field_0x234;       // <-- writes own per-player array
```

| Binary @ | Port line | Difference                                                                 |
|----------|-----------|----------------------------------------------------------------------------|
| 0x001251ee, 0x0012520c | 867, 869 | Binary writes to `+0x234 + playerIdx*4` (per-player). Port writes to `field_0x234` (single global). For `playerIdx=0` only, these match by coincidence. For p=1 they diverge. |
| 0x00125224, 0x00125258 | 880     | Binary writes wait to `+0x238 + playerIdx*4`. Port writes to `field_0x238` (single global). Same per-player issue. |
| 0x00125200             | 865     | Binary: `s1 = m_NextWaveDelay + inc * revisit` then `Math::Max(s1, 0.05)`. Port matches the `if (<0.05) = 0.05` form. |
| (none)                 | 882     | Port has an EXTRA writeback: `m_NextWaveDelay[playerIdx] = field_0x234`. Binary does NOT have this. The port-owned `m_NextWaveDelay[]` array is a port-introduced field that the binary doesn't have. |

---

## 7. WAVE_INFO field offsets (post-rename `debff12`)

Verified against binary `WaveManager::Init` access patterns. All offsets
referenced in `GetNextWave` decompile match the port's `WaveStructs.h`:

| Field                   | Port offset | Binary offset | Status |
|-------------------------|-------------|---------------|--------|
| `m_NextWaveDelay`       | +0x20       | +0x20 (`vldr [r3, #0x20]` @ 0x001251d8) | MATCH |
| `m_NextWaveDelayInc`    | +0x24       | +0x24 (`vldr [r3, #0x24]` @ 0x001251f8) | MATCH |
| `m_NextWaveWait`        | +0x28       | +0x28 (`vldr [r2, #0x28]` @ 0x0012521c) | MATCH |
| `m_NextWaveWaitSpInc`   | +0x30       | +0x30 (`vldr [r2, #0x30]` @ 0x00125228) | MATCH |
| `m_bWaitForEntities`    | +0x38       | +0x38 (`ldrb [r3, #0x38]` @ 0x00122a5e) | MATCH |
| `m_bWaitForProcessing`  | +0x39       | +0x39 (`ldrb [r3, #0x39]` @ 0x00122a58) | MATCH |
| `field_0x34` (revisit)  | +0x34       | +0x34 (`vldr [r3, #0x34]` @ 0x001251f4) | MATCH |

Default values from `WAVE_INFO::WAVE_INFO()`: `m_bWaitForEntities=1`,
`m_bWaitForProcessing=0`. Need separate audit of binary ctor to confirm
these defaults; not in scope here.

---

## 8. Port-side action

### CRITICAL: align port to binary's overlapping per-player slot model

Pick ONE of two strategies. Strategy A is faithful; B is a minimal patch.

**Strategy A (faithful) -- delete `m_WaveTimer[]` and `m_NextWaveDelay[]` arrays**

1. `WaveManager.h`: remove the `float m_NextWaveDelay[2];` and
   `float m_WaveTimer[2];` members. The binary uses `field_0x234` /
   `field_0x238` as the per-player array bases.
2. `WaveManager.h`: change `field_0x234` / `field_0x238` to
   `float field_0x234[2];` so they form a `[+0x234..+0x23b]` interleaved
   pair (entries: delay[0]=+0x234, wait[0]=+0x238, delay[1]=+0x238,
   wait[1]=+0x23c). Move `field_0x23c` to start at +0x23c (kept as
   `uint8_t field_0x23c[2];`).
3. `WaveManager.cpp:697`: `float waveTimer = (&field_0x234)[playerIdx];`
   (or equivalent indexed access at +0x234+playerIdx*4).
4. `WaveManager.cpp:699`: `(&field_0x234)[playerIdx] = waveTimer - dt;`
5. `WaveManager.cpp:700`: REMOVE `return;` (binary falls through to
   wave-end check).
6. `WaveManager.cpp:756`: `float nextDelay = *(float*)((char*)this + 0x238 + playerIdx*4);`
   (or equivalent: read at `+0x238+p*4`, NOT `m_NextWaveDelay[p]`).
7. `WaveManager.cpp:759`: write back to same `+0x238+p*4` slot.
8. `WaveManager.cpp:867, 869, 880`: change all three writes from
   `field_0x234` / `field_0x238` to per-player indexed writes
   (`*(float*)((char*)this + 0x234 + playerIdx*4)` etc.).
9. `WaveManager.cpp:882`: DELETE `m_NextWaveDelay[playerIdx] = field_0x234;`
   (the binary has no such writeback).

**Strategy B (minimal) -- keep port arrays but fix the read site**

1. `WaveManager.cpp:756`: change
   `float nextDelay = m_NextWaveDelay[playerIdx];` ->
   `float nextDelay = (playerIdx == 0) ? field_0x238 : /* p1 wait slot */ ...;`
   (read from the wait slot, not the delay slot).
2. `WaveManager.cpp:759`: write back to `field_0x238`.
3. `WaveManager.cpp:882`: DELETE the `m_NextWaveDelay[playerIdx] =
   field_0x234;` line.
4. `WaveManager.cpp:697`: change to read from `field_0x234`.
5. `WaveManager.cpp:699`: write back to `field_0x234`.
6. `WaveManager.cpp:700`: REMOVE `return;`.

### CRITICAL: remove port's hard return when `wave == null`

`WaveManager.cpp:693-694`:

```cpp
WAVE_INFO* wave = m_pCurrentWave[playerIdx];
if (!wave) return;            // <-- DELETE
```

Replace with: skip the WaveTimer / spawner loop when null but **still
fall through to** the `IsWaveProcessing` + `GetNextWave` block. Binary
@ 0x001253fc unconditionally branches to the wave-end logic when the
wave is null (the wave-end may then call `GetNextWave` to assign one).

```cpp
WAVE_INFO* wave = m_pCurrentWave[playerIdx];
if (wave) {
    // ... existing WaveTimer + spawner loop ...
}
// Fall through to wave-end block.
```

### DIVERGES: fix `Bomb::GetNumActiveForPlayer` arg in IsWaveProcessing

`WaveManager.cpp:940`:

```cpp
if (Bomb::GetNumActiveForPlayer(-1, true) >= 1) return true;
                                  ^^^^
```

Change to:

```cpp
if (Bomb::GetNumActiveForPlayer(-1, false) >= 1) return true;
```

Binary @ 0x00122a76: `r1 = r4` (= playerIdx, which is 0 = false). The
second argument is the active-only filter; passing `true` over-counts
bombs and keeps `IsWaveProcessing` returning true longer than the binary.

### MINOR: missing `game->field_0x470` sentinel gate

Binary @ 0x0012593e gates the wave-end block on `game->field_0x470 == 0`
(set elsewhere when a spawn happened this frame, presumably to defer
GetNextWave by one frame after the last spawn). Port lacks this. Effect
is small (off-by-one frame on transition) but causes spurious early
GetNextWave calls when the last spawn of a wave coincides with
`IsWaveProcessing == false`. Track for future fix; not the dominant
cause of the symptom.

---

## 9. Summary

The dominant root causes of "1 transition / 3 s" are:

1. The port's `m_NextWaveDelay[]` array is a port-introduced field that
   doesn't exist in the binary. The wave-end transition reads from the
   wrong slot, and the value (0.6 s) is double-counted (used both as
   pre-spawn delay via `m_WaveTimer[]` and as wave-end delay via
   `m_NextWaveDelay[]`). Net: ~1.2 s extra per wave vs binary.
2. `IsWaveProcessing` passes `true` for the bomb arg in the p=0 path
   instead of `false`, keeping the function returning true while bombs
   still exist that should not gate wave processing. Adds variable
   delay (depends on bomb lifetime).
3. The port's `if (!wave) return;` early-return blocks the rescue path
   that would call `GetNextWave` when `m_pCurrentWave` is null; if any
   transition happens to clear the pointer briefly the port stalls
   until the next frame's redundant entry.
4. Cumulative effect: each transition takes ~600-3000 ms instead of
   ~600 ms (XML "delay" baseline) or 0 ms (binary baseline when "wait"
   attr is zero, which is the default).

Fix priority: Strategy A (or B) on the `m_NextWaveDelay[]` field first;
the `Bomb` arg fix and the wave-null fall-through next.
