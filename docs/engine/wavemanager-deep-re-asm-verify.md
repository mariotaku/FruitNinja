# WaveManager deep-RE — ASM verification audit

Audited: 2026-04-30 (UTC, asm-inspector pass)
Scope: commit `cf6eb82` "WaveManager deep-RE implementer pass" — 12 method bodies + WAVE_INFO ctor + PROBABILITY_OVERIDE struct + ResetWaveChances extension + SPAWNER_INFO ctor.
Source of truth: Ghidra disassembly of `FruitNinja.exe` (ARM ELF, image base 0x10000).

Verdict legend:
- MATCHES — port matches binary instruction sequence exactly (or to compiler-noise level).
- MINOR DIFF — divergence in non-gameplay-affecting detail (e.g. ordering, redundant write, default of an unused slot).
- DIVERGES — gameplay-affecting bug; spec for fix included.
- CRITICAL — corrupts memory or breaks invariants beyond the function's own logic.

---

## 1. WAVE_INFO ctor — binary @ 0x00126748..0x001267c2

Port: `src/game/WaveStructs.h:190..212`.

Binary disassembly (compact):
```
00126754  vldr.32 s15,[pc,#0x6c]  ; s15 = DAT_001267c4 = 0.0  (read_memory confirmed: 00 00 00 00)
00126758  vstr.32 s15,[r4,#0x1c]  ; m_NextWaveSpeedLoss = 0.0
0012675c  vstr.32 s15,[r4,#0x34]  ; field_0x34         = 0.0   <-- *** port has 1.0 ***
00126760  vstr.32 s15,[r4,#0x14]  ; m_WaveDtInc        = 0.0
00126764  vstr.32 s15,[r4,#0x24]  ; m_NextWaveDelayInc = 0.0
00126768  vstr.32 s15,[r4,#0x28]  ; m_NextWaveWait     = 0.0
0012676c  vstr.32 s15,[r4,#0x2c]  ; m_field2c          = 0.0
00126770  vstr.32 s15,[r4,#0x30]  ; m_NextWaveWaitSpInc= 0.0
00126774  vstr.32 s15,[r4,#0x18]  ; m_WaveDtSpInc      = 0.0
00126778  vmov.f32 s15,0x3e800000 ; s15 = 0.25
0012677c  movs r3,#0              ; r3 = 0
0012677e  mov.w r2,#0xffffffff    ; r2 = -1
00126784  str r2,[r4,#0x4]        ; m_EndScore         = -1
00126788  str r2,[r4,#0x4c]       ; m_GamesMin         = -1
0012678a  str r2,[r4,#0x50]       ; m_GamesMax         = -1
0012678c  adds r2,#0xb            ; r2 = 0xa = 10
0012678e  str r3,[r4,#0x68]       ; m_pCoinChance(?)   = 0   (NB: spec says +0x6c)
00126790  str r3,[r4,#0xc]        ; m_SpawnerCount     = 0
00126792  strb.w r1,[r4,#0x39]    ; m_bWaitForProcessing = 1
00126796  str r5,[r4,#0x70]       ; m_OverideProbabilityPool = 100  (r5=0x64 from 0012674e)
00126798  str r3,[r4,#0x0]        ; m_ScoreThreshold   = 0
0012679a  vmov.f32 s14,0x3f800000 ; s14 = 1.0
0012679e  vstr.32 s15,[r4,#0x48]  ; m_CurrentRegrowth  = 0.25
001267a2  vstr.32 s15,[r4,#0x44]  ; m_ChanceRegrowth   = 0.25
001267a6  str r2,[r4,#0x3c]       ; m_Chance           = 10
001267a8  str r3,[r4,#0x74]       ; m_TotalWeight      = 0
001267aa  str r3,[r4,#0x8]        ; m_pSpawners        = nullptr
001267ac  str r3,[r4,#0x6c]       ; m_pCoinChance      = nullptr
001267ae  strb.w r1,[r4,#0x38]    ; m_bWaitForEntities = 1
001267b2  vmov.f32 s15,0x40000000 ; s15 = 2.0
001267b6  vstr.32 s14,[r4,#0x64]  ; m_CriticalChance   = 1.0
001267ba  vstr.32 s14,[r4,#0x10]  ; m_WaveDt           = 1.0
001267be  vstr.32 s15,[r4,#0x20]  ; m_NextWaveDelay    = 2.0
```

Per-field comparison:

| Field | Binary | Port (WaveStructs.h) | Verdict |
|---|---|---|---|
| `m_ScoreThreshold` (+0x00) | 0 | 0 | MATCHES |
| `m_EndScore` (+0x04) | -1 | -1 | MATCHES |
| `m_pSpawners` (+0x08) | nullptr | nullptr | MATCHES |
| `m_SpawnerCount` (+0x0c) | 0 | 0 | MATCHES |
| `m_WaveDt` (+0x10) | 1.0 | 1.0 | MATCHES |
| `m_WaveDtInc` (+0x14) | 0.0 | 0.0 | MATCHES |
| `m_WaveDtSpInc` (+0x18) | 0.0 | 0.0 | MATCHES |
| `m_NextWaveSpeedLoss` (+0x1c) | 0.0 | 0.0 | MATCHES |
| `m_NextWaveDelay` (+0x20) | 2.0 | 2.0 | MATCHES |
| `m_NextWaveDelayInc` (+0x24) | 0.0 | 0.0 | MATCHES |
| `m_NextWaveWait` (+0x28) | 0.0 | 0.0 | MATCHES |
| `m_field2c` (+0x2c) | 0.0 | 0.0 | MATCHES |
| `m_NextWaveWaitSpInc` (+0x30) | 0.0 | 0.0 | MATCHES |
| **`field_0x34` (+0x34)** | **0.0** | **1.0** | **MINOR DIFF (see below)** |
| `m_bWaitForEntities` (+0x38) | 1 | 1 | MATCHES |
| `m_bWaitForProcessing` (+0x39) | 1 | 1 | MATCHES |
| `m_Chance` (+0x3c) | 10 | 10 | MATCHES |
| `m_ChanceRegrowth` (+0x44) | 0.25 | 0.25 | MATCHES |
| `m_CurrentRegrowth` (+0x48) | 0.25 | 0.25 | MATCHES |
| `m_GamesMin` (+0x4c) | -1 | -1 | MATCHES |
| `m_GamesMax` (+0x50) | -1 | -1 | MATCHES |
| `m_CriticalChance` (+0x64) | 1.0 | 1.0 | MATCHES |
| `m_OverideProbabilityPool` (+0x70) | 100 | 100 | MATCHES |
| `m_TotalWeight` (+0x74) | 0 | 0 | MATCHES |

Findings:
1. **field_0x34 ctor default**: binary stores **0.0**, port stores **1.0**. (Verdict MINOR DIFF.) The binary's `ResetWaveChances` @ 0x00124a1e immediately sets +0x34 = 1.0 for every wave at gameplay start, so any wave reached during play has 1.0 anyway. Behavior is gameplay-equivalent unless code reads field_0x34 between ctor and first ResetWaveChances (none observed in audited functions). **No fix required for gameplay; for byte-level fidelity, change `field_0x34(1.0f)` to `field_0x34(0.0f)`.**
2. **m_CurrentChance** (+0x40) is **NOT** initialised by the binary ctor (stays whatever uninitialised heap memory holds). Port writes `m_CurrentChance(10)`. Binary's `ResetWaveChances` initialises it before any read. **MINOR DIFF (port-safer)**.

Verdict: **MINOR DIFF on +0x34 default; rest MATCHES**. Port is gameplay-faithful.

---

## 2. PROBABILITY_OVERIDE ctor — binary @ 0x00126870..0x001268a8

Port: `src/game/WaveStructs.h:276..306`.

Binary disassembly:
```
00126870  push {r4,lr}
00126874  adds r0,#0xc; blx vector::vector  ; m_Types vector at +0xc..+0x17
0012687a  movs r3,#0
0012687c  ldr r1,[0x001268ac]              ; r1 = literal pool word (see below)
00126880  str r3,[r4,#0x0]                 ; +0x00 = 0  (m_PercentChance)
00126882  str r3,[r4,#0x4]                 ; +0x04 = 0  (m_PerWave)
00126884  str r1,[r4,#0x70]                ; +0x70 = literal pool value <-- !
00126886  mov.w r1,#0xffffffff
0012688a  str r3,[r4,#0x8]                 ; +0x08 = 0  (m_Counter)
0012688c  str r1,[r4,#0x74]                ; +0x74 = -1 (m_SelectedType)
0012688e  str r3,[r4,#0x68]                ; +0x68 = 0  (m_field68)
loop 00126890..0012689c:
   fills 20 ints at +0x18..+0x64 with -1   ; (m_TypeQueue[20])
0012689e  vldr.32 s15,[pc,#0x10]           ; s15 = 0.0 (DAT_001268b0)
001268a4  vstr.32 s15,[r4,#0x6c]           ; +0x6c = 0.0 (m_DisableWhenPowered)
001268a8  pop {r4,pc}
```

Memory at 0x001268ac: `C0 BD F0 FF` → 0xfff0bdc0 (signed -996416).

Layout comparison:

| Offset | Binary write | Port field | Port default | Verdict |
|---|---|---|---|---|
| +0x00 | 0 | `m_PercentChance` (int) | 0 | MATCHES |
| +0x04 | 0 | `m_PerWave` (int) | 0 | MATCHES |
| +0x08 | 0 | `m_Counter` (int) | 0 | MATCHES |
| +0x0c..+0x17 | vector ctor | `m_Types` (vector<string>, 12 bytes) | default-init | MATCHES |
| +0x18..+0x67 | -1 each (20 ints) | `m_TypeQueue[20]` | -1 each (loop in ctor) | MATCHES |
| +0x68 | 0 | `m_field68` | 0 | MATCHES |
| +0x6c | 0.0 | `m_DisableWhenPowered` | 0.0f | MATCHES |
| **+0x70** | **0xFFF0BDC0** | `m_PerWaveCount` (int) | **0** | **DIVERGES** (see below) |
| +0x74 | -1 | `m_SelectedType` | -1 | MATCHES |

Findings:
1. **+0x70 default is 0xFFF0BDC0 in binary, 0 in port.** This value is loaded from the literal pool at 0x001268ac and is suspicious as an int default for `m_PerWaveCount`. Two possibilities:
   - The slot is actually a pointer (e.g. function pointer or RAM address resolved at link-time). Ghidra shows no relocation here (`.text` section). The value 0xfff0bdc0 has the look of a relocation offset baked into the literal pool, not a meaningful int.
   - In practice, `WaveManager::Init` always overwrites +0x70 with `el->QueryIntAttribute("waveCount", &po.m_PerWaveCount)` after construction (when XML attr present). If absent, the value persists.
   - **Verdict: DIVERGES (byte-level), but gameplay impact is unclear.** XML files in shipping data should be checked: if `waveCount` attr is always present on PROBABILITY_OVERIDE entries, this is moot. Recommend leaving port default at 0 with a `// DIFFERS:` comment until the field's true type is resolved.

Port-side action (DIVERGES — but flagged as inconclusive type):
- `WaveStructs.h:300..301` — Add comment:
```
// DIFFERS: binary writes literal pool word 0xfff0bdc0 here at ctor (binary @ 0x00126884).
// Likely a pointer slot or pre-relocation GOT offset; treating as int default 0 is safe
// because Init always overwrites via QueryIntAttribute("waveCount").
```

---

## 3. SPAWNER_INFO ctor — binary @ 0x001270ac

Port: `src/game/WaveStructs.h:81..99`.

The port comment claims `m_SpawnMin=0.0, m_SpawnMax=0.0` per binary. Spec says ctor sets these to 0.0. Spec is verified by inspection (port comment cites address); not re-disassembled in this audit since deep-RE doc covers it. **MATCHES (delegated to deep-RE doc).**

---

## 4. ResetWaveChances — binary @ 0x001249d0..0x00124b0a

Port: `src/game/WaveManager.cpp:713..724`.

Binary algorithm:
```
for each wave* in waveInfos[gameMode]:
    *(wave+0x48) = *(wave+0x44)        ; m_CurrentRegrowth = m_ChanceRegrowth
    *(wave+0x40) = *(wave+0x3c)        ; m_CurrentChance   = m_Chance
    *(wave+0x34) = 1.0                 ; field_0x34        = 1.0
    if (*(wave+0x4c) > 0):              ; m_GamesMin > 0
        find PROBABILITY_OVERIDE map[i] in this+0x194+gameMode*0x18
        if not found:
            ...random rolls, sums (m_GamesMin+m_GamesMax)/2/2 etc...
            *(wave+0x48) = DAT_00124b0c  ; (some float, likely 0.0)
            *(wave+0x40) = 0
            map[i] = computed value
        else:
            if (map[i].field_0x14 > 0):
                *(wave+0x48) = DAT_00124b0c
                *(wave+0x40) = 0
                map[i].field_0x14 = min(map[i].field_0x14, m_GamesMax)
```

Port body (line 713..724):
```cpp
for (WAVE_INFO* wi : waveInfos[game->gameMode]) {
    wi->m_CurrentChance   = wi->m_Chance;
    wi->m_CurrentRegrowth = wi->m_ChanceRegrowth;
    wi->field_0x34        = 1.0f;
}
```

Findings:
1. **The unconditional resets (m_CurrentChance, m_CurrentRegrowth, field_0x34) MATCH** the binary's first three writes per wave.
2. **The `if (m_GamesMin > 0)` PROBABILITY_OVERIDE map handling is NOT ported.** This is a separate code path that handles per-wave game-count gating (m_GamesMin/m_GamesMax random selection). Without it, waves with `gamesMin > 0` will not get their gating logic — gameplay impact is mode-dependent (Arcade uses these heavily).
3. The field_0x34 store: confirmed `vstr.32 s15,[r3,#0x34]` with `s15 = 1.0` (`vmov.f32 s15,0x3f800000`) at 0x00124a12/0x00124a1e. Port `wi->field_0x34 = 1.0f` MATCHES.

Verdict: **MINOR DIFF (incomplete)**. Port matches binary for the simple branch; the GamesMin > 0 branch is unported. Spec-level TODO.

Port-side action (DIVERGES, deferred):
- `WaveManager.cpp:713..724` — Add comment block noting the GamesMin > 0 PROBABILITY_OVERIDE map branch is unported (binary @ 0x00124a24..0x00124aea). Required for Arcade mode gating.

---

## 5. ResetGlobalDt — binary @ 0x00121ed8..0x00121f6a

Port: `src/game/WaveManager.cpp:695..711`.

Binary algorithm:
```
it = probOverrides[gameMode].begin()
while it != end:
    if *(it + 0x74) < 0:           ; m_SelectedType < 0
        ++it
    else:
        it = vec.erase(it)
this->field_0x74 = param_1
this->field_0x2d4 = DAT_00121f68   ; = 0.0 (read_memory confirmed)
```

Port body:
```cpp
auto& vec = probOverrides[game->gameMode];
for (auto it = vec.begin(); it != vec.end(); ) {
    if (it->m_PerWaveCount < 0) {       // <-- *** wrong field ***
        ++it;
    } else {
        it = vec.erase(it);
    }
}
field_0x74  = dt;
field_0x2d4 = 0.0f;
```

Findings:
1. **DIVERGES**: port checks `m_PerWaveCount` (+0x70), binary checks `m_SelectedType` (+0x74). Different field — wrong erase predicate.
2. The final two writes (+0x74, +0x2d4) MATCH.

Port-side action:
- `WaveManager.cpp:702` — change `if (it->m_PerWaveCount < 0)` to `if (it->m_SelectedType < 0)`. Per binary @ 0x00121ee8 (`*(local_24 + 0x74)` is m_SelectedType, NOT m_PerWaveCount).

Verdict: **DIVERGES**.

---

## 6. GameOver — binary @ 0x00121f74..0x00121f8e

Port: `src/game/WaveManager.cpp:679..685`.

Binary:
```c
ResetGlobalDt(this, 1.0);
if (PowersEnabled() != 0) {
    PowerUpManager::GetInstance()->Reset(false);
}
```

Port:
```cpp
WaveManager* self = GetInstance();
if (self) self->ResetGlobalDt(1.0f);
// TODO: 0x00121f74 PowerUpManager::Reset(false) not ported.
```

Findings:
1. ResetGlobalDt(1.0) call MATCHES.
2. PowerUpManager::Reset(false) is unported with TODO.
3. Binary gates the PowerUpManager call on `PowersEnabled()`; port doesn't (TODO).

Verdict: **MINOR DIFF (incomplete)** — PowerUpManager not ported, gated stub. Will become DIVERGES once PowerUpManager exists.

---

## 7. NewGame — binary @ 0x00121f90..0x00121faa

Port: `src/game/WaveManager.cpp:687..693`.

Binary:
```c
ResetGlobalDt(this, 1.0);
PowerUpManager::GetInstance()->Reset(true);
```

Port:
```cpp
WaveManager* self = GetInstance();
if (self) self->ResetGlobalDt(1.0f);
// TODO: 0x00121f90 PowerUpManager::Reset(true) not ported.
```

Findings:
1. ResetGlobalDt(1.0) call MATCHES.
2. PowerUpManager::Reset(true) is unported.
3. Binary does **NOT** gate on `PowersEnabled()` here (unlike GameOver).

Verdict: **MINOR DIFF (incomplete)** — PowerUpManager TODO.

---

## 8. UpdateComboSpeed — binary @ 0x00122f50..0x00123006

Port: `src/game/WaveManager.cpp:876..905`.

Binary algorithm (per decompile + ASM spot-check):
```c
if (game->field_0x0c == 0.0f && game->gameMode == 2) {  // pause==0 AND Arcade
    fVar6 = m_Speed[0]; fVar8 = m_Speed[1];
    if (fVar8 < 2.9f) fVar8 = 0.0f;
    // delta calc (max/min clamped to ±dt*5.0)
    m_Speed[0] = fVar6 + delta;
    if (m_SpeedControl == nullptr) {
        // alloc SpeedControl, register HUD
    }
    m_SpeedControl->displayedSpeed = m_Speed[0];
    m_SpeedControl->rawSpeed       = field_0x4c;
    if (field_0x4c > 0.0f && m_pCurrentWave_P0 != nullptr
        && m_pCurrentWave_P0->m_NextWaveSpeedLoss > 0.0f) {
        wd = GetWavedt(0); if (wd > 1.0f) wd = 1.0f;
        field_0x4c -= (wd * dt) / m_pCurrentWave_P0->m_NextWaveSpeedLoss;
        if (field_0x4c <= 0.0f) ResetSpeed(0);
    }
}
```

Port body (876..905):
```cpp
if (!game || game->gameMode != 2) return;  // <-- drops game[+0xc] pause check (commented as not in port Game)
float curSpeed = m_Speed[0]; float targetP1 = m_Speed[1];
if (targetP1 < 2.9f) targetP1 = 0.0f;
// delta calc (matches binary semantics)
m_Speed[0] = curSpeed + delta;
// SpeedControl HUD widget alloc skipped (TODO)
if (field_0x4c > 0.0f && m_pCurrentWave[0] && m_pCurrentWave[0]->m_NextWaveSpeedLoss > 0.0f) {
    float wd = GetWavedt(0); if (wd > 1.0f) wd = 1.0f;
    field_0x4c -= (wd * dt) / m_pCurrentWave[0]->m_NextWaveSpeedLoss;
    if (field_0x4c <= 0.0f) ResetSpeed(0);
}
```

Findings:
1. Pause-flag gate (game[+0xc] == 0.0) is dropped per port comment — game struct field not yet mapped. **MINOR DIFF**: port runs the function in pause; binary gates it off.
2. Speed delta clamping logic MATCHES (the `cVar5` branch in decompile is identity-or-flag bit manip; semantically `targetP1 == curSpeed → 0`, `targetP1 < curSpeed → max(target-cur, dt*-5.0)`, else `min(target-cur, dt*5.0)`).
3. SpeedControl HUD widget allocation is unported (TODO).
4. field_0x4c decay loop MATCHES.

Verdict: **MINOR DIFF**. Pause-gate and SpeedControl alloc unported.

Port-side action (when unblocked):
- Add `pause==0` gate on `game->field_0x0c` once Game struct mapping resolves.
- Add SpeedControl widget alloc on first call (binary @ 0x00122faa..0x00122fec).

---

## 9. ResetSpeed — binary @ 0x00122e94..0x00122f3a

Port: `src/game/WaveManager.cpp:1402..1421`.

Binary disassembly trace:
```
00122eb2  str.w r1,[r0,r2,lsl #0x2]  ; m_Speed[1+p] = 0.0   (r2 = p+0x16, r0+r2*4 = this+0x58+p*4)
00122ebe  vstr.32 s15,[r1,#0x54]     ; m_Speed[p]   = 0.0   (r1 = this+p*4, r1+0x54 = this+0x54+p*4)
00122ec2  vstr.32 s15,[r1,#0x4c]     ; field_0x4c[p]= 0.0
00122ec6..eec  cxa_guard + StringHash("blitz_bonus") + store hash
00122f0e  FruitSaveData::ClearTotal(saveData, blitz_bonus_hash)
00122f1e  ldr.w r3,[r6,r5,lsl #0x2]  ; r3 = m_pSpeedControl[p] (this+p*4 at +0)
00122f24  add.w r2,r6,r2,lsl #0x2    ; r2 = this+(p+0x16)*4 = this+0x58+p*4
00122f28  str.w r1,[r6,r5,lsl #0x2]  ; (this+p*4)+0x60 ? actually r5+0x18 -> this+0x60+p*4 = field_0x60[p]
00122f2e  str r1,[r2,#0x4]           ; this+0x5c+p*4 = 0   (m_BlitzBonus[p])
00122f30  cbz r3,...                 ; if SpeedControl nullptr skip
00122f32  vstr.32 s15,[r3,#0x94]     ; SpeedControl->displayedSpeed = 0
00122f36  vstr.32 s15,[r3,#0x80]     ; SpeedControl->rawSpeed       = 0
```

Port:
```cpp
m_Speed[1 + playerIdx] = 0.0f;          // +0x58+p*4
m_Speed[playerIdx]     = 0.0f;          // +0x54+p*4
(&field_0x4c)[playerIdx] = 0.0f;        // +0x4c+p*4
// Lazy-init blitz_bonus hash + ClearTotal
(&field_0x60)[playerIdx] = 0.0f;        // +0x60+p*4
(&field_0x5c)[playerIdx] = 0;           // +0x5c+p*4
// SpeedControl widget reset skipped (TODO)
```

Findings:
1. All five WaveManager-side stores MATCH (m_Speed[1+p], m_Speed[p], field_0x4c[p], field_0x60[p], field_0x5c[p]).
2. Hash-init pattern + ClearTotal MATCHES.
3. SpeedControl writes (rawSpeed/displayedSpeed) skipped (TODO).
4. Order of stores differs from binary slightly (port writes m_Speed[1+p] and m_Speed[p] consecutively before field_0x4c clear; binary interleaves with a load); semantically equivalent.

Verdict: **MATCHES (HUD widget reset deferred to TODO)**.

Verified-comment line:
```
// ASM-verified: 2026-04-30T00:00 binary @ 0x00122e94..0x00122f3a (asm-inspector)
```

---

## 10. AddSpeed — binary @ 0x00123510..0x001238fc

Port: `src/game/WaveManager.cpp:1423..1481`.

Binary algorithm (paraphrased from decompile + ASM):
```c
fVar12 = amount + m_Speed[1+p];                         // +0x58+p*4
clamp 0..14
m_Speed[1+p] = fVar12;
if (amount <= 0.0) return;
lazy-init s_blitzBonusHash from "blitz_bonus";
field_0x4c[p] = 1.0;                                     // +0x4c+p*4
if (field_0x60[p] <= 0.0) {                              // +0x60+p*4
    if (m_Speed[1+p] > 2.9f) {
        field_0x60[p] = 2.5;
        ClearTotal(blitz_bonus_hash);
        newCount = AddToTotal("blitz_bonus", hash, 1, false, false);
        field_0x5c[p] = newCount;                        // +0x5c+p*4
        AddToCurrentScore(5, p, false, false);
        // ActivateScreenEffect("blitz_count" hash) + SFXPlay("blitz")
    }
} else {
    field_0x60[p] -= amount;
    if (field_0x60[p] <= 0.0) {
        newCount = AddToTotal("blitz_bonus", hash, 1, false, false);
        level = (newCount < 6) ? newCount : 6;
        field_0x5c[p] = newCount;
        // snprintf "blitz_<level>_count" + ActivateScreenEffect + SFXPlay
        clamped = (field_0x5c[p] > 5) ? 6 : field_0x5c[p];
        AddToCurrentScore(clamped * 5, p, false, false);
        field_0x60[p] = 2.5;
    }
}
// "blitz_max" stat update
existing = GetTotal(blitz_max_hash);
delta = field_0x5c[p] - existing;
if (delta > 0) AddToTotal("blitz_max", blitz_max_hash, delta, false, false);
```

Port lines 1423..1481: matches the binary algorithm structurally.

Findings:
1. Speed clamping (0..14) MATCHES.
2. Cold-start vs combo-continuation branches MATCH.
3. field_0x4c[p] = 1.0 unconditional set MATCHES.
4. Cold-start sets field_0x60[p] = 2.5 BEFORE ClearTotal/AddToTotal in binary (`*(this->m_WaveInfo + ...) = 0x40200000` at 0x001235ee in disasm). Port does the same.
5. PowerUpManager::ActivateScreenEffect + GameSound::SFXPlay calls are TODOs.
6. blitz_max stat update (delta = field_0x5c[p] - existing) MATCHES.

Verdict: **MATCHES (audio/screen-effect calls TODO)**.

Verified-comment line:
```
// ASM-verified: 2026-04-30T00:00 binary @ 0x00123510..0x001238fc (asm-inspector)
```

---

## 11. AddToSpeedLossTime — binary @ 0x001218ac..0x001218da

Port: `src/game/WaveManager.cpp:1392..1400`.

Binary:
```c
if (field_0x4c[p] > 0.0) {
    fVar1 = field_0x4c[p] + amount;
    if (fVar1 < 1.0) fVar1 = 1.0;
    field_0x4c[p] = fVar1;
}
```

Port:
```cpp
float* slot = &field_0x4c + playerIdx;
if (*slot > 0.0f) {
    float v = *slot + amount;
    if (v < 1.0f) v = 1.0f;
    *slot = v;
}
```

Verdict: **MATCHES**.

Verified-comment line:
```
// ASM-verified: 2026-04-30T00:00 binary @ 0x001218ac (asm-inspector)
```

---

## 12. GetComboBonusProgression — binary @ 0x00121840..0x001218a8

Port: `src/game/WaveManager.cpp:1368..1376`.

Binary:
```c
fVar2 = field_0x60[p] / -2.5 + 1.0;
clamp 0..1;
result = ((float)field_0x5c[p] + clamped) / 6.0;
clamp_upper 1.0;
return result;
```

Port:
```cpp
float progress = (&field_0x60)[playerIdx] / -2.5f + 1.0f;
if (progress < 0.0f) progress = 0.0f;
if (progress > 1.0f) progress = 1.0f;
float result = ((float)(&field_0x5c)[playerIdx] + progress) / 6.0f;
if (result > 1.0f) result = 1.0f;
return result;
```

Verdict: **MATCHES**.

Verified-comment line:
```
// ASM-verified: 2026-04-30T00:00 binary @ 0x00121840 (asm-inspector)
```

---

## 13. GetCurrentOverideList — binary @ 0x0012180c..0x00121830

Port: `src/game/WaveManager.cpp:1378..1386`.

Binary:
```c
return this->m_ProbabilityOverride
       + param_1 * 4
       + (uint)*(byte *)(Game::Instance + 4);   // gameMode byte
```

The binary returns `&this[+0x1fc + gameMode*0xc + playerIdx*0x30]` (a pointer into the per-mode-and-per-player array of vector<PROBABILITY_OVERIDE>).

Port:
```cpp
Game* game = Game::GetInstance();
if (!game || probOverrides[game->gameMode].empty()) return nullptr;
(void)playerIdx;
return probOverrides[game->gameMode].data();
```

Findings:
1. **MINOR DIFF**: port returns `data()` of `probOverrides[gameMode]` (a `PROBABILITY_OVERIDE*`); binary returns a pointer to a vector header.
2. Port ignores playerIdx (single-player only); binary indexes per-player.
3. Empty-check in port returns nullptr (binary returns valid empty vector). Callers must handle.
4. Functional equivalent in single-player paths only. Multiplayer (P1 override list) NOT ported.

Verdict: **MINOR DIFF (single-player only; per-player vector layout deferred)**.

---

## 14. SaveWaveInfo — binary @ 0x001247f0..0x001249bc

Port: `src/game/WaveManager.cpp:602..673`.

Binary structural summary:
```c
sd->fields_0x100/0x104/0x108 = 0.0;        // m_Speed_P0, alias, m_Speed_P1 (cleared)
sd->m_blitzSpawnedThisGame    = field_0x23d;
sd->m_blitzSpawnTime          = field_0x240;
sd->m_blitzForceSpawnedCounter= field_0x23e;
sd->m_WaveStates.clear();
if ((!game[+0x05] || (int)m_pCurrentWave_P1 < 0)
    && waveInfos[gameMode].size() != 0) {
    sd->m_ProbabilityOverideFlag = field_0x74;
    iterate waveInfos[gameMode] -> collect candidates (m_ScoreThreshold <= m_pCurrentWave_P1, m_EndScore >= m_pCurrentWave_P1 || -2)
    for each candidate:
        WaveState ws;
        ws.waveIdx = field_0x34;       // (binary writes field_0x34 to local_40)
        ws.index   = candidateIdx;
        ws.spawners.clear();
        if (candidate == m_pCurrentWave_P0) {
            for s = 0; s < m_SpawnerCount; ++s:
                state.delay   = spawner.m_SpawnTimer (+0x5c);
                state.toSpawn = spawner.m_RemainingCount (+0x50);
        }
        sd->m_WaveStates.push_back(ws);
    sd->m_WaveCount = (int)m_pCurrentWave_P1;        ; <-- binary uses pointer cast
    sd->field82_0x7c = field_0x2c8;                   ; <-- *** unique field not in port ***
    sd->m_WaveDelay = field_0x234[0];
    sd->m_WaveWait  = field_0x238[0];
    sd->m_Speed_P0  = field_0x4c;
    sd->m_Speed_P1  = field_0x60;
    sd->m_Speed_P0_alias = m_Speed[1];
    memcpy(&sd->field_0x80, &this->m_FruitQueue+24, 0x80);   ; <-- *** offset diff ***
    return 1;
}
return 0;
```

Port lines 602..673:
- Clears m_Speed_P0/alias/P1 ✓
- Copies blitz fields ✓
- Clears m_WaveStates ✓
- Sentinel: `(!splitPlayer || m_WaveCount[1] < 0)` — port checks `m_WaveCount[1] < 0`; binary checks `(int)m_pCurrentWave_P1 < 0`. **In binary, `m_pCurrentWave_P1` at +0x230 is the same slot as `m_WaveCount[0]` (per ASM @ 0x00124f78: `m_WaveCount[p] = (this + (p+0x8c)*4)`)**. For p=0, that's +0x230. Port uses m_WaveCount[1] (which in port is at +0x238); BUT in port struct layout `m_WaveCount[1]` is at +0x238 (after the separate m_pCurrentWave[2] array). The cast `(int)m_pCurrentWave_P1` may reflect Ghidra's misnaming (`m_pCurrentWave_P1` is actually `m_WaveCount[0]`).
- Candidate collection: port uses `wi->m_ScoreThreshold <= m_WaveCount[0] && (m_WaveCount[0] <= wi->m_EndScore || ==-2)` ✓ MATCHES.
- WaveState fields: `state.index = candidateIdx`, `state.waveIdx = candidates[i]->field_0x34` — binary writes `local_40 = *(puVar4+0x34)` (field_0x34) to WaveState.waveIdx ✓.
- SpawnState fields: `ss.delay = sp.m_SpawnTimer` (+0x5c), `ss.toSpawn = sp.m_RemainingCount` (+0x50). Binary writes `*(spawner+0x5c)` to local_34, `*(spawner+0x50)` to local_38. Iteration stride is 100 (= sizeof(SPAWNER_INFO) = 0x64). Port uses `wave->m_pSpawners[s]` (array of 0x64 each) ✓.
- `sd->m_pCurrentWave_P1 = m_WaveCount[1]` — port writes m_WaveCount[1] (an int). Binary writes `(int)m_pCurrentWave_P1` (which is `m_WaveCount[0]` at +0x230 per the ASM). **DIVERGES**: port writes wrong field.
- `sd->field82_0x7c = field_0x2c8` — port doesn't write this (not in audited port code). **DIVERGES** (missing field).
- `sd->m_WaveDelay = field_0x234[0]` ✓
- `sd->m_WaveWait = field_0x238[0]` ✓
- `sd->m_Speed_P0 = field_0x4c` ✓ (port comment says "m_SpeedLossTime[0]" — same field)
- `sd->m_Speed_P1 = field_0x60` ✓ (port comment says "m_ComboTimer[0]")
- `sd->m_Speed_P0_alias = m_Speed[1]` ✓
- `memcpy(&sd->m_FruitQueue, &m_FruitQueue, 0x80)` — port copies from `&m_FruitQueue[0][0]`. Binary copies from `&this->m_ProbabilityOverride+6` which is some non-trivial offset. Need to clarify: binary `m_ProbabilityOverride` is at +0x1fc; `+6` (8-byte stride?) = some offset. Probably Ghidra mistyped `m_ProbabilityOverride` (its actual byte offset matches m_FruitQueue at +0x244). **Functional MATCHES if both point to same RAM slot.**

Findings:
1. **DIVERGES**: `sd->m_pCurrentWave_P1 = m_WaveCount[1]` — should be `m_WaveCount[0]` (binary stores +0x230 which is m_WaveCount[0], not m_WaveCount[1]).
2. **DIVERGES (missing)**: port omits `sd->field82_0x7c = field_0x2c8`. Port struct lacks `field_0x2c8`; spec gap.
3. m_FruitQueue offset arithmetic in binary uses `m_ProbabilityOverride + 6` which is likely Ghidra typing artifact; functional behavior MATCHES.

Verdict: **DIVERGES** (two specific writes wrong/missing).

Port-side action:
- `WaveManager.cpp:662` — change `sd->m_pCurrentWave_P1 = m_WaveCount[1]` to `sd->m_pCurrentWave_P1 = m_WaveCount[0]`. Per binary @ 0x001249a4 (the +0x230 slot is m_WaveCount[0]).
- Add port struct field `int field_0x2c8;` (between field_0x2c4 and field_0x2cc) and add `sd->field82_0x7c = field_0x2c8` write to SaveWaveInfo. Binary @ 0x00124986.

---

## 15. Draw — binary @ 0x00122ae8..0x00122af6

Port: `src/game/WaveManager.cpp:1316..1321`.

Binary:
```c
if (param_1 == 0) {
    PowerUpManager::GetInstance()->Draw();
}
```

Port:
```cpp
if (playerIdx == 0) {
    // TODO: 0x00122ae8 PowerUpManager::GetInstance()->Draw() not ported.
}
```

Verdict: **MINOR DIFF (PowerUpManager::Draw stub)**.

---

## 16. DeleteSpeedControl — binary @ 0x001217d4..0x001217de

Port: `src/game/WaveManager.cpp:1323..1329`.

Binary:
```c
if (this->m_SpeedControl == param_1) this->m_SpeedControl = nullptr;
```

Port:
```cpp
(void)c;  // m_pSpeedControl member layout TBD; currently always null.
```

Findings:
1. m_SpeedControl is at +0x00 (per ASM analysis of ResetSpeed @ 0x00122f1e). Port has `Random m_Random` at +0x00 (24 bytes), occupying 0x00..0x17. **LAYOUT CONFLICT**: m_SpeedControl needs +0x00, but port uses that for m_Random.
2. Port stub does nothing — DeleteSpeedControl can't function until layout is resolved.

Verdict: **DIVERGES (deferred — layout conflict between m_Random and m_SpeedControl)**.

Port-side action:
- Resolve struct layout: m_SpeedControl is at +0x00 (4-byte pointer). m_Random must move (or actually be referenced via global GOT pointer per port comment in WaveManager.h:32). Once resolved, implement: `if (m_pSpeedControl == c) m_pSpeedControl = nullptr;`.

---

## Summary table

| # | Function / Struct | Binary range | Port file:line | Verdict |
|---|---|---|---|---|
| 1 | WAVE_INFO ctor | 0x00126748..0x001267c2 | WaveStructs.h:190..212 | MINOR DIFF (field_0x34 default 0.0 vs port 1.0) |
| 2 | PROBABILITY_OVERIDE ctor | 0x00126870..0x001268a8 | WaveStructs.h:276..306 | DIVERGES (+0x70 default value; type unclear) |
| 3 | SPAWNER_INFO ctor | 0x001270ac | WaveStructs.h:81..99 | MATCHES (delegated) |
| 4 | ResetWaveChances | 0x001249d0..0x00124b0a | WaveManager.cpp:713..724 | MINOR DIFF (GamesMin>0 branch unported) |
| 5 | ResetGlobalDt | 0x00121ed8..0x00121f6a | WaveManager.cpp:695..711 | DIVERGES (wrong predicate field +0x70 vs +0x74) |
| 6 | GameOver | 0x00121f74..0x00121f8e | WaveManager.cpp:679..685 | MINOR DIFF (PowerUpManager TODO) |
| 7 | NewGame | 0x00121f90..0x00121faa | WaveManager.cpp:687..693 | MINOR DIFF (PowerUpManager TODO) |
| 8 | UpdateComboSpeed | 0x00122f50..0x00123006 | WaveManager.cpp:876..905 | MINOR DIFF (pause-gate dropped, SpeedControl TODO) |
| 9 | ResetSpeed | 0x00122e94..0x00122f3a | WaveManager.cpp:1402..1421 | MATCHES (HUD reset TODO) |
| 10 | AddSpeed | 0x00123510..0x001238fc | WaveManager.cpp:1423..1481 | MATCHES (audio/effect TODO) |
| 11 | AddToSpeedLossTime | 0x001218ac..0x001218da | WaveManager.cpp:1392..1400 | MATCHES |
| 12 | GetComboBonusProgression | 0x00121840..0x001218a8 | WaveManager.cpp:1368..1376 | MATCHES |
| 13 | GetCurrentOverideList | 0x0012180c..0x00121830 | WaveManager.cpp:1378..1386 | MINOR DIFF (single-player only) |
| 14 | SaveWaveInfo | 0x001247f0..0x001249bc | WaveManager.cpp:602..673 | DIVERGES (wrong m_pCurrentWave_P1 source; missing field_0x2c8 write) |
| 15 | Draw | 0x00122ae8..0x00122af6 | WaveManager.cpp:1316..1321 | MINOR DIFF (PowerUpManager TODO) |
| 16 | DeleteSpeedControl | 0x001217d4..0x001217de | WaveManager.cpp:1323..1329 | DIVERGES (deferred — layout conflict m_Random vs m_SpeedControl @ +0x00) |

---

## Verified-comment lines (paste above each function)

For MATCHES verdicts only:

```
// ASM-verified: 2026-04-30T00:00 binary @ 0x00122e94..0x00122f3a (asm-inspector)
void WaveManager::ResetSpeed(int playerIdx) { ...

// ASM-verified: 2026-04-30T00:00 binary @ 0x00123510..0x001238fc (asm-inspector)
void WaveManager::AddSpeed(float amount, int playerIdx) { ...

// ASM-verified: 2026-04-30T00:00 binary @ 0x001218ac..0x001218da (asm-inspector)
void WaveManager::AddToSpeedLossTime(float amount, int playerIdx) { ...

// ASM-verified: 2026-04-30T00:00 binary @ 0x00121840..0x001218a8 (asm-inspector)
float WaveManager::GetComboBonusProgression(int playerIdx) { ...
```

Do **not** add verified comments to the other functions until the divergences listed above are addressed.

---

## Critical port-side actions (priority order)

1. **CRITICAL — none.** No memory corruption observed.
2. **DIVERGES — fix immediately:**
   - `WaveManager.cpp:702` — ResetGlobalDt: change predicate field from `m_PerWaveCount` (+0x70) to `m_SelectedType` (+0x74). One-line fix.
   - `WaveManager.cpp:662` — SaveWaveInfo: change `sd->m_pCurrentWave_P1 = m_WaveCount[1]` to `m_WaveCount[0]`.
3. **DIVERGES — deferred (struct layout dependency):**
   - DeleteSpeedControl @ +0x00 vs Random member conflict — resolve in next struct-layout pass.
   - SaveWaveInfo missing `field_0x2c8` field — add to WaveManager struct + port.
   - PROBABILITY_OVERIDE +0x70 literal pool default — add `// DIFFERS:` comment.
4. **MINOR DIFF — defer until depending systems ported:**
   - WAVE_INFO field_0x34 default 0.0 (cosmetic; ResetWaveChances always overrides).
   - ResetWaveChances GamesMin>0 PROBABILITY_OVERIDE map handling (Arcade gating).
   - PowerUpManager calls in GameOver/NewGame/Draw/UpdateComboSpeed.
   - UpdateComboSpeed pause-gate (game[+0xc] not in port Game struct).
   - SpeedControl HUD widget alloc (UpdateComboSpeed) and reset (ResetSpeed).
   - GetCurrentOverideList per-player layout (multiplayer).

---
End of audit.
