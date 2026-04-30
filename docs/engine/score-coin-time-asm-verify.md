# ScoreControl / CoinCounter / TimeControl — ASM-level fidelity audit

Audit date: 2026-04-30T04:20Z (asm-inspector pass)

Source of truth: GhidraMCP decompile + raw disasm of FruitNinja.exe.
Compared against:
- `src/hud/ScoreControl.{h,cpp}`
- `src/hud/CoinCounter.{h,cpp}`
- `src/hud/TimeControl.{h,cpp}`

Status legend: **MATCHES** (binary-faithful) / **MINOR DIFF** (TODO/stub
flagged in port; runtime-equivalent today) / **DIVERGES** (wrong logic,
visible behavior delta) / **CRITICAL** (game-breaking).

The "verified comment" line below each MATCHES section is the
`// ASM-verified: ...` line for `implementer` to paste above the
verified function. Lines are emitted only for MATCHES / functions
whose body equals the binary instruction-for-instruction (modulo TODO
stubs that are clearly flagged with `// TODO`).

---

## 1. ScoreControl  (size 0x100, vtable @ 0x001E9D48)

### 1.1 ctor @ 0x00158C7C — **MATCHES** (modulo TODO)

Binary:
- `HUDControl3d::HUDControl3d(this)`
- vtable = `vtable_table[GOT+...] + 8`  (skips first 2 dtor slots)
- `SmartPtr` ctors at `+0xa0`, `+0xa4`, `+0xf8`
- `+0x80 = 0` (m_ScoreSmoothed), `+0x2c = 0` (m_Timer base), `+0xfc = 0` (m_PlayerIdx)
- `+0x4 = 0x01` (m_bActive — set redundantly by base ctor)
- `+0x88 = 0` (m_HighscoreToShow), `+0x8c = -1.0f` (m_BannerStartTimer)
- `+0x90 = 1.0f` (m_ScalePulse), `+0xa8 = -2.0f` (m_BannerScaleTime)
- `LoadLocalisedTexture("hud_fruit.tex")` → assigned to `+0xf8` (m_FruitDigitTex)
- final `Reset()` call

Port (`ScoreControl.cpp:43`–`70`) sets all listed fields with matching values
and calls `Reset()` last. The default-init for `m_DigitAlpha[16]` is
explicit (vs binary which inits in `Reset()`); both reach the same state
because Reset zeroes them anyway.

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x00158c7c (asm-inspector)`

### 1.2 Init @ 0x00158190 — **MATCHES**

Binary calls `vtable[+0x10]` (= `Reset` slot). Port forwards to `Reset()`.

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x00158190 (asm-inspector)`

### 1.3 Release @ 0x00158370 — **MATCHES**

Binary nulls four SmartPtrs in this order: `+0xf8` (FruitDigitTex), `+0x74`
(m_Texture), `+0xa0` (ScoreIconTex), `+0xa4` (HighscoreBannerTex). Port
follows the same 4-step null sequence (`ScoreControl.cpp:81`–`86`).

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x00158370 (asm-inspector)`

### 1.4 Reset @ 0x001582E4 — **MINOR DIFF**

Binary:
1. `SmartPtr::operator=(this+0x74, this+0xf8)` — copy m_FruitDigitTex into m_Texture (the spritesheet HUDControl3d will render).
2. `m_PulseAngle = 0`
3. `m_bDirty = 1`
4. `size = Vec3(40.0, 40.0, 0.0) * globalHudScale` (DAT_00158354=40.0, DAT_00158358=0.0; pfVar1 = `*GOT[hud_scale_vec_ptr]`).
5. `m_DigitAlpha[16] = {0}` (loop zero +0xb8..+0xf7).
6. `m_DigitCount = 0; m_LastDigitCount = 0`.
7. `m_LayerFlags = 1 << m_PlayerIdx`.

Port (`ScoreControl.cpp:89`–`118`):
- DIFFERS-A: `m_Texture = 0` instead of `m_Texture = m_FruitDigitTex.Get()`. The port comment explains this is intentional — HUDControl3d::Draw would otherwise render the whole spritesheet as a 40x40 quad. Until PreDraw section B is fully wired with per-digit UV crop, leaving 0 is correct. **Mark: TODO, not a bug at this time.**
- DIFFERS-B: `size = (40,40,0)` directly; missing `* globalHudScale`. With current scale=1.0 this is identical. TODO is flagged in the port comment.
- All other steps match.

No `// ASM-verified` line yet (DIFFERS-A is intentional but logical, DIFFERS-B is a TODO multiplier).

### 1.5 Skip @ 0x001581A0 — **DIVERGES** (gate field)

Binary:
```
m_DisplayedScore = GetCurrentScore(m_PlayerIdx)
if (pSaveData[300] != 0) m_BannerScaleTime = 1.0f
```

Port (`ScoreControl.cpp:121`–`128`):
```
m_DisplayedScore = GetCurrentScore(m_PlayerIdx)
if (game->pauseFlag) m_BannerScaleTime = 1.0f
```

The gate is the **save-data byte at offset 300** (new-best flag). Port
substitutes `Game::pauseFlag` which is a different concept. Both default
0 so today no observable delta, but on a save-resume after a new-best
the binary triggers the banner; port does not.

### 1.6 Update @ 0x0015853C — **DIVERGES** (multiple)

Critical-path differences (each numbered; see binary @ 0x0015853C and
decompile in this audit's transcript):

1. **Stage-1 cascade gate** (binary @ 0x001585A8):
   - Binary outer test: `(digitsActive >= 1) AND (game->gameMode == 1)` → run cascade. Else: static-timer path.
   - Port (`ScoreControl.cpp:153`): only checks `gameMode == 1`. When `digitsActive == 0` and `gameMode == 1`, port runs the equality-cascade arm with both inputs zero (vacuously OK), but on the first `digitsActive` rise from 0 within mode 1, the port jumps directly into the `else` decay branch with `digitsActive==0 != m_LastDigitCount`, identical to binary. **Functionally close but the gate logic is different.**

2. **`s_StaticTimer` reset on cascade re-entry** (binary @ 0x00158600):
   - When the gameMode==1 cascade branch runs and `digitsActive == m_LastDigitCount`, binary sets `s_StaticTimer = 0.0` (`*(iVar12 + DAT_001588c8) = DAT_001588a0`). Port does not reset the static timer in this branch.

3. **Score-easing `min(catchup, maxStep)` selection** (binary @ 0x001587B4):
   - Binary computes `catchup = (currentScore + signCorrection - smoothed) * 0.1` and `maxStep = mult * 0.3 * baseRate`. The compare `catchup < maxStep` selects which to add. Port uses `std::min(catchup, maxStep)` — equivalent for finite floats. **MATCHES** (verified semantically identical).

4. **Stage-3 SFX gate is fully stubbed**:
   - Binary @ 0x00158848 plays `"Bonus-count-up"` when `(s_SfxCooldown <= 0) AND (gameMode == 2) AND (g_GameData.pCurrentWave[+0x80] > 0) AND (g_GameData.pCurrentWave[+0x84] > 0.0)`. Port has `if (false)` placeholder; no SFX ever plays. **MINOR DIFF — TODO in port already.**

5. **Stage-4 PulseAngle decay overflow handling** (binary @ 0x00158888):
   - Binary distinguishes two regimes: `pulseAngle <= DAT_001588b8` (some threshold) vs `>` and applies decay differently with overflow check `if (DAT_001588d4 < newAngle) angle = 0`. Port uses a single linear int decay clamped to [0, ushort]. **DIVERGES** in edge handling near 0/0xFFFF boundary, but the visible effect on `SinIdx(angle)` is small.

6. **Stage-6 wave-mode position recentering** (binary @ 0x00158B00..0x00158B70):
   - In wave (someTimer > 0) the binary measures the current score string at scale `m_ScalePulse * 48` and shifts m_DrawPos by `(-160 - measuredWidth*0.5, +80, 0)` to centre the score banner.
   - Port (`ScoreControl.cpp:260`–`265`) leaves a TODO and does not perform the centring at all. **DIVERGES** — score banner position will be off-centre during wave transitions. The score uses `currentScore` (not displayed!) for the measurement.

7. **Stage-6 multiplayer position offset**:
   - Binary: `pos -= Vec3(200, 0, 0) * |someTimer|` (animation-driven slide, both players use the magnitude of someTimer).
   - Port: `posX -= 200.0 * playerScale` where `playerScale = (IsMP && idx==1) ? -1 : 1`. With IsMP=false always, playerScale=1, so `posX -= 200`. **DIVERGES** — this isn't even close to the binary's slide formula. Today only P1 single-player works, so visible delta is bounded.

8. **Stage-7 banner activation gate**:
   - Binary: `wantBanner = (someTimer > DAT_00158c60 = 0.99999...) AND (pSaveData[300] != 0)`.
   - Port: `(waveTimer > 0.99f) AND false` (TODO). Constant 0.99 vs 0.99999 — minor; gate currently false. **MINOR DIFF — TODO.**

### 1.7 Draw @ 0x001581D4 — **MINOR DIFF**

Binary:
```
if ((m_PlayerIdx != 0 || !IsMultiplayer()) && (someTimer > -1.0f)) {
    a = clamp_byte(255.0 * cameraIntensity)   // DAT_00158248 = 255.0
    m_DrawColour.a = a
    HUDControl3d::Draw(this, layerMask)
}
```

Port (`ScoreControl.cpp:296`–`311`):
- `cameraIntensity` is hardcoded 1.0 (TODO comment); 255 * 1 = 255, identical at runtime. **MINOR DIFF.**
- All gates match (the De Morgan rewrite is equivalent).

### 1.8 PreDraw @ 0x00158E1C — **DIVERGES** (multiple)

Constants resolved from binary:
- DAT_00159090 = **96.0**, NOT 0.75 as the port comment claims.
- DAT_00159094 = 48.0 (font size).
- DAT_00159098 = 0.0.
- DAT_0015908c = 255.0 (alpha multiplier).
- DAT_001593c0 = 135.0, DAT_001593c4 = 182.0, DAT_001593c8 = 45.0, DAT_001593cc = 0.0, DAT_001593d0 = 155.0 (hardcoded y), DAT_001593d4 = 52.0, DAT_001593d8 = 48.0.

Findings:

1. **Section A baseline-width comment is wrong**:
   `ScoreControl.cpp:331` says `DAT_00159090 = 0.75` and uses `0.75` as the multiplier. The actual binary constant is **96.0**. The binary computes `baseline = MeasureString("000") * 96.0` (cached via cxa-guard) where MeasureString returns a normalized width. Port multiplies by `0.75` → produces a much smaller baseline → adaptive scale clamp triggers far earlier. **DIVERGES — wrong constant, wrong threshold.**

2. **Section A FontDrawString alignment**:
   - Binary alignment = **0x0d** (CENTER | MIDDLE | BOTTOM).
   - Port (`ScoreControl.cpp:349`): `Mortar::FONT_ALIGN_CENTER` = **0x01** (CENTER only).
   - Port uses centered horizontal but top-baseline vertical; binary baselines middle+bottom. **DIVERGES — wrong vertical anchoring.**

3. **Section A alpha**:
   - Port hardcodes `alpha = 255`. Binary computes `alpha = clamp_byte(255 * cameraIntensity)`. With cameraIntensity stub=1.0, identical at runtime. **MINOR DIFF — TODO.**

4. **Section B per-digit cursor advance**:
   - Binary @ 0x001591B4: `cursorX += MeasureString(label) * scale + 5.0` (multiplies measured width by per-digit `scale = 45 + i*6`).
   - Port (`ScoreControl.cpp:403`): `cursorX += MeasureWidth(scale, label) + 5.0f`. Port's `MeasureWidth` ignores the scale param (returns normalized width), and the port does NOT then multiply by `scale`. **DIVERGES — cursor advance is too small by a factor of `scale` (45..135).** Combo digits will overlap.

5. **Section B per-digit DrawString y-coordinate**:
   - Binary call shape (s0=x, s1=155.0 hardcoded, s2=0.0 z, s3=scale, s4..6=0.0).
   - Port passes `Vec3(drawX, 155.0f, 0.0f)` — y=155 ✓ matches.
   - **MATCHES** for y; the alignment value 0x01 (binary) also matches port's `FONT_ALIGN_CENTER`.

6. **Section B Texture rebind**:
   - Binary copies the FRUIT_INFO texture into `m_Texture` (+0x74) via SmartPtr::operator= (proper refcount).
   - Port (`ScoreControl.cpp:369`): `m_Texture = hudTex->m_TexId` (raw GLuint). Acceptable in port representation, but watch for refcount. **MATCHES** as a port concession.

7. **Section C highscore-banner text colour pulse**:
   - Binary lerps `local_60` (180,128,5,200) toward `(100,150,25,200)` via `CosIdx(s_BannerSinIdx * 0xb6) * -0.5 + 0.5` factor.
   - Port (`ScoreControl.cpp:419`): writes static base `Colour(0xB4, 0x80, 0x05, 200)` only, no lerp. **DIVERGES — no colour pulse.**

8. **Section C drawY**:
   - Binary uses a localised label `GETSTRING(0xB5, 0)` ("NEW BEST" or similar) drawn ahead of the number, with text laid out at `m_DrawPosX + cachedLabelWidth*20.0 - DAT_00159798`. Port draws only the bare number at `m_DrawPosY - 30`. **DIVERGES — missing the localised label and its measured offset.**

9. **Section D anchor logic**:
   - Binary: `anchorX = IsMultiplayer ? (DAT_001597c4 * someTimer - DAT_001597a0) : DAT_001597a4`. DAT_001597c4 = 160.0, DAT_001597a4 = `0xc3200000` = -160.0 (single-player anchor).
   - Port (`ScoreControl.cpp:442`): `Vec3(m_DrawPosX, m_DrawPosY + 5.5f, 0.0f)` — uses m_DrawPosX (= -218 + 24 = -194), NOT -160. **DIVERGES — score-icon position is off.**

10. **Section E banner scale formula**:
    - Binary @ 0x0015975C: `bannerScale = SinIdx(m_BannerScaleTime * DAT_001597b8)` where DAT_001597b8 = **21840.0**, then divided by `SinIdx(0x5550)` for normalisation.
    - Port (`ScoreControl.cpp:463`): `bannerScale = m_BannerScaleTime` (linear). **DIVERGES — banner has no overshoot/easing curve.**

11. **Section E translate offset**:
    - Binary translates by `(texW * 0.5 - DAT_001597c4 = texW*0.5 - 160.0, m_DrawPosY + DAT_001597c8, 0)`. DAT_001597c8 not yet decoded but is added not subtracted.
    - Port: `Vec3(texW * 0.5f - 64.0f, m_DrawPosY + 0.5f, 0.0f)`. **DIVERGES** — wrong x-offset (-64 vs -160) and wrong y-offset (+0.5 vs +DAT_001597c8).

12. **Arcade (mode==2) `xN` multiplier overlay**:
    - Binary @ 0x0015921C draws "x%d" when `PowerUpManager::GetScoreGainMultiplier() > 1`. Port has comment-only, no body. **MINOR DIFF — port stub returns 1, never triggers.**

### Summary of ScoreControl

| Method | Status |
|--------|--------|
| ctor | MATCHES |
| Init | MATCHES |
| Release | MATCHES |
| Reset | MINOR DIFF (intentional m_Texture stub + globalHudScale TODO) |
| Skip | DIVERGES (wrong gate field) |
| Update | DIVERGES (wave-mode centring, MP slide, several TODOs) |
| Draw | MINOR DIFF (cameraIntensity stub) |
| PreDraw | DIVERGES (Section A constant + alignment + Section B advance + Section C colour-pulse + Section D/E anchors and scale formula) |

---

## 2. CoinCounter  (size 0xD4, vtable @ 0x001E91B0)

### 2.1 ctor @ 0x00135600 — **MATCHES**

Binary:
- `HUDControl3d::HUDControl3d(this)`
- `vtable = *GOT[...] + 8`
- `+0x80 = 0`, `+0x84 = 0`, `+0x88 = 0`, `+0x90 = 0`
- `+0x4 = 1` (m_bActive)
- `+0x7c = 0` (uint16, m_CoinCount)

Port (`CoinCounter.cpp:7`–`10`): `m_CoinCount(0)` + memset of remainder.
HUDControl base ctor sets m_bActive=1. **MATCHES.**

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x00135600 (asm-inspector)`

### 2.2 Init @ 0x00135544 — **MINOR DIFF**

Binary @ 0x00135544 is a stub that loads a coin-icon texture into m_Texture
(referenced from GameInit). Port has empty `Init() override {}`. The
texture load is currently absent, so Draw cannot show a coin icon even
if it were ported. **MINOR DIFF — header notes "coin texture load (called from GameInit via vtable[2])".**

### 2.3 Release @ 0x0013557C — Not implemented

Port has no override for Release. Binary equivalent likely nulls the
SmartPtr at `+0x74`. With Init not loading anything, Release is a no-op
in both binary and port at runtime. **MINOR DIFF.**

### 2.4 Reset @ 0x00135548 — **MATCHES**

Binary:
```
fVar1 = field_0x8c;
field_0x90 = 1.0f;
fVar2 = 0.0;
if (0.0 < fVar1 && fVar1 >= 1.0) fVar2 = 1.0;
else if (0.0 < fVar1) fVar2 = fVar1;
field_0x8c = fVar2;
```
Effectively `clamp(field_0x8c, 0.0, 1.0)` then `field_0x90 = 1.0`.

Port (`CoinCounter.cpp:20`–`26`):
```
if (*f8c < 0.0f) *f8c = 0.0f;
if (*f8c > 1.0f) *f8c = 1.0f;
*f90 = 1.0f;
```

Boundary equivalence: binary maps `[<=0]→0`, `[>=1]→1`, mid stays. Port
maps `[<0]→0`, `[>1]→1`, mid stays. The `==0.0` and `==1.0` cases produce
identical values either way. **MATCHES.**

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x00135548 (asm-inspector)`

### 2.5 Update @ 0x00135580 — **MATCHES** (no-op)

Binary is a single `bx lr`. Port `Update` body is `(void)dt;`. **MATCHES.**

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x00135580 (asm-inspector)`

### 2.6 Draw @ 0x0013569C — **DIVERGES** (port body is empty)

Binary draws a textured quad + a font string (the coin number from
`(char*)(this+0x94)`) when `field_0x8c > 0.0`.

Port: `Draw() override { (void)hudScale; (void)layerMask; }` — empty.

Runtime impact today: `field_0x8c` is initialized to 0 by ctor and never
written outside `Reset()` (which clamps it to [0,1] but does not assign
non-zero). No external caller writes `+0x8c` in current port code. So
the binary's `if (0.0 < field_0x8c)` gate is also false at runtime; the
port's empty Draw matches **observable behavior**. But the port would
not draw the coin counter even when CoinCounter eventually starts being
animated externally. **DIVERGES** structurally; no live behavior delta.

### 2.7 GetType @ 0x00135AF4 — **MATCHES**

Binary: `mov r0, #3; bx lr`. Port: `int GetType() override { return 3; }`. **MATCHES.**

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x00135af4 (asm-inspector)`

### Summary of CoinCounter

| Method | Status |
|--------|--------|
| ctor | MATCHES |
| Init | MINOR DIFF (texture load missing) |
| Release | MINOR DIFF (no override; benign while Init is stub) |
| Reset | MATCHES |
| Update | MATCHES |
| Draw | DIVERGES structurally; no runtime delta today |
| GetType | MATCHES |

m_CoinCount confirmed at `+0x7C` (uint16). Field layout matches
documented spec.

---

## 3. TimeControl  (size 0x108, vtable @ 0x001EA158)

Constants resolved:
- DAT_001621e8 = **0.0** (used as default fallback for negative m_CountdownStart, m_TickFrame init)
- DAT_001621ec = **60.9** (Arcade override AND save-resume seed)
- DAT_0016215c = 60.9 (GetCountDown fallback)
- DAT_001623a0 = **0.0** (size.x init = 0)
- DAT_001623a4 = 320.0 (screen height for pos.y)
- DAT_001623a8 = 480.0 (screen width for pos.x)
- DAT_001627a0 = **0.0** (timer reset on game-over)
- DAT_001628c4 = **60.0** (seconds-per-minute)
- DAT_001628c8 = ?? (multiplayer-mode pos.x slide)
- DAT_001628cc = ?? (size.y multiplier in pos.y formula)
- DAT_00162b04 = **-0.6** (text x-multiplier of size.x)
- DAT_00162b08 = **0.0**
- DAT_00162b0c = **32.0** (font size — countdown text size!  AND power-up overlay y-offset)
  - In binary, DAT_00162b0c is loaded into `s2` (font size) for the countdown DrawString call AND used as the y-offset in the powerup-overlay path. Same constant, two roles.
- DAT_00162b10 = `0x80000003` (mask used in tick-tock UV; obscure; not used by port).
- DAT_00162b1c = **GOT-relative pointer** to the powerup overlay tint colour. The pointer at `[GOT + 0x73c8]` resolves to a Colour struct elsewhere — not yet de-referenced. Not a bare RGBA literal.

### 3.1 ctor @ 0x001622E8 — **MATCHES**

Binary:
- HUDControl3d base ctor + vtable+8
- `SmartPtr::operator=` sets `m_SecondaryTex` to null (the port reads `m_SecondaryTex` as a GLuint = 0 from base ctor, equivalent)
- `m_CountdownStart = -1.0f`
- `size = (0.0, 18.0, 0.0)`
- `pos = ((480 - size.x)*0.5 - 5, (320 + size.y)*0.5 - 5, 0)`
- `m_PowerupOverlay[0] = 0`
- `m_bNoDestructor = 0`
- final `Reset()` call

Port (`TimeControl.cpp:25`–`38`) sets all in identical order. **MATCHES.**

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x001622e8 (asm-inspector)`

### 3.2 Init @ 0x001620E4 — **MATCHES**

Forwards to `vtable[+0x10]` = `Reset()`. Port matches.

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x001620e4 (asm-inspector)`

### 3.3 Release @ 0x001623B4 — **MATCHES**

Binary: `SmartPtrNull(this+0x74)` (null m_Texture). Port comment notes the
port has no GLuint ref-count to release. **MATCHES** in port representation.

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x001623b4 (asm-inspector)`

### 3.4 Reset @ 0x00162168 — **MINOR DIFF**

Binary:
1. `m_PowerupOverlay[0] = 0`
2. `t = m_CountdownStart`; if `t < 0.0` then `t = 0.0`. `m_TimeRemaining = t`.
3. If `gameMode == 2 || IsMultiplayer()`:
   - `m_TimeRemaining = 60.9` (DAT_001621ec)
   - if `pSaveData[+0x10c] == 0.0 && g_GameData.someTimer < 0.0`:
     `pSaveData[+0x10c] = 60.9`
4. `m_TickFrame = 0.0`
5. `m_DrawColour = (255, 255, 255, 255)` (white default; literal copy from `+0x5c` field which holds default)

Port (`TimeControl.cpp:54`–`70`):
- Steps 1–2: ✓ (port sets `m_TimeRemaining = max(m_CountdownStart, 0)`).
- Step 3 mode test: `gameMode == 2` only — IsMultiplayer TODO. The pSaveData write is **missing entirely**. **MINOR DIFF — TODO.**
- Step 4: ✓
- Step 5: ✓

Zen 90.9: NOT set inside Reset (binary doesn't either). The 90.9 value
is set externally via `CountDown(90.9)` from GameInit before Reset
runs; Reset's `t = m_CountdownStart` then yields 90.9 in Zen.
Port matches this flow.

### 3.5 Skip @ 0x001620FC — **MINOR DIFF**

Binary:
```
m_TimeRemaining = pSaveData[+0x10c]
m_TickFrame = 0.0
```

Port (`TimeControl.cpp:96`–`100`): only sets `m_TickFrame = 0.0`; does
not restore `m_TimeRemaining` from save. TODO already flagged. **MINOR DIFF.**

### 3.6 CountDown @ 0x001620F0 — **MATCHES**

Binary: `this->m_CountdownStart = param`. Port matches.

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x001620f0 (asm-inspector)`

### 3.7 GetCountDown @ 0x00162134 — **MATCHES**

Binary: returns 60.9 if `gameMode != 2 && !IsMultiplayer()`, else
`m_CountdownStart`. Port (`TimeControl.cpp:77`–`84`) matches with the
IsMultiplayer TODO commented out.

Note: this looks counter-intuitive (Arcade mode = 2 returns the *user*
m_CountdownStart, non-Arcade returns hardcoded 60.9). That **is** what
the binary does. Re-confirmed by reading the ARM disasm.

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x00162134 (asm-inspector)`

### 3.8 AddTime @ 0x001204F0 — **MATCHES**

Binary: `m_TimeRemaining += delta`. Port matches.

Verified-comment:
`// ASM-verified: 2026-04-30T04:20 binary @ 0x001204f0 (asm-inspector)`

### 3.9 SetToMultiplayerState — **MATCHES** (calls Reset)

Binary vtable[+0x2c] forwards to Reset; port matches.

### 3.10 Update @ 0x001624A4 — **DIVERGES** (multiple — major)

Binary structure (stripped):
```
if (!IsTimedGame()) {
    m_LayerFlags = 0
    pSaveData[+0x10c] = -1.0f          // sentinel
    return
}
m_LayerFlags = 1
pos.x = (480 - size.x)*0.5 - 5       // recompute pos.x every frame
guard A = field_0x2 (game)
guard B = field_0x5 (game)
guard C = (field_0x170 != 0 && field_0x199 == 0)
if (guard A || guard B || guard C) goto WRITE_SAVE_AND_FORMAT

if (m_CountdownStart <= 0.0) {
    m_TimeRemaining += dt          // count UP
} else {
    cVar1 = m_DrawColour            // remember prev colour byte
    if (PowersEnabled()) {
        pwr = PowerUpManager::GetInstance()
        if (pwr->m_field68 > 0.0) {
            m_DrawColour = (255, 100, 100)
            sprintf(m_PowerupOverlay, "+%i", (int)pwr->m_field68 + 1)
            goto WRITE_SAVE_AND_FORMAT
        }
    }
    m_PowerupOverlay[0] = 0

    if (PowersEnabled()) m_TimeRemaining -= dt * pwr->m_field6c
    else                 m_TimeRemaining -= dt

    if (m_TimeRemaining < 0.5) {
        GameOver(-1, -1.0, -1)
        m_TimeRemaining = 0.0
        // also reset some globals to 0/-1
        m_DrawColour = (255, 100, 100)
        SFXPlay("time-up")
    } else if (m_TimeRemaining < 3.0) {
        m_DrawColour = ((int)(t*8.0) & 1) ? (255,100,100) : white
    } else if (m_TimeRemaining < 6.0) {
        m_DrawColour = ((int)(t*4.0) & 1) ? (255,100,100) : white
    } else if (m_TimeRemaining < 11.0) {
        m_DrawColour = ((int)(t*2.0) & 1) ? (255,100,100) : white
        // i.e. (int)(t+t)&1
    }

    // Tick/Tock SFX: when colour byte CHANGES from cVar1 (one-shot per flash edge)
    if (0.0 < t < 11.0 && m_DrawColour.r != cVar1) {
        toggle global parity bit
        SFXPlay(parity == 0 ? "Time-tick" : "Time-tock")
    }

    elapsed = m_CountdownStart - m_TimeRemaining
    m_TickFrame = (int)elapsed % 6 + 0.5
}
WRITE_SAVE_AND_FORMAT:
    pSaveData[+0x10c] = m_TimeRemaining
    sprintf(m_TextBuffer, "%i:%02i", (int)(t/60), (int)t%60)

if (IsSameScreenMultiplayer())
    pos.x += |someTimer| * DAT_001628c8
fVar10 = IsSameScreenMultiplayer() ? 1.0 : (1.0 - |someTimer|)
pos.y = size.y * -2.0 * fVar10 + (2*size.y + DAT_001628cc) * 0.5
```

Port (`TimeControl.cpp:102`–`150`) findings:

1. **m_LayerFlags=0 path also writes pSaveData[+0x10c] = -1.0f** — port misses the save-data sentinel write.
2. **pos.x is NOT recomputed each frame** — binary always recomputes `pos.x = (480 - size.x)*0.5 - 5`. Port leaves pos at ctor-time value. (Effect: if size.x changes during play, pos.x stays stale; today size.x = 0 always so no visible delta.)
3. **The triple guard** (`field_0x2 || field_0x5 || (field_0x170 != 0 && field_0x199 == 0)`) is missing entirely. Port only checks `pauseFlag` (= field_0x5 only). **DIVERGES** on `field_0x2`/`field_0x170` paths (other modal screens / non-pause halts).
4. **count-UP branch** when `m_CountdownStart <= 0` is missing. Port unconditionally subtracts dt. **DIVERGES.**
5. **PowerUpManager overlay** branch is missing (port comment says TODO). **MINOR DIFF — TODO.**
6. **Power-up time-multiplier** (`dt * pwr->m_field6c`) on countdown is missing.
7. **Flash thresholds wrong**:
   - Binary: `< 3` (8 Hz), `< 6` (4 Hz), `< 11` (2 Hz).
   - Port: `< 2` (8 Hz), `< 5` (4 Hz), `< 10` (2 Hz).
   - **DIVERGES — bands shifted by 1 second in two cases.**
8. **Flash function is wrong**:
   - Binary: `((int)(t*N) & 1) ? red : white` — sharp toggle (boolean alternation).
   - Port: `sinf(phase * pi)` — smooth sine. Visually different.
   - **DIVERGES.**
9. **Tick/Tock SFX** is entirely missing (port has TODO comment). The
   one-shot-per-edge gate uses a global parity bit toggled when the
   colour byte changes between frames — non-trivial. **DIVERGES.**
10. **pSaveData[+0x10c] = m_TimeRemaining** not written (TODO). **MINOR DIFF.**
11. **pos.y reposition** based on `IsSameScreenMultiplayer ? 1.0 : (1.0 - |someTimer|)` is missing. Port pos.y is fixed from ctor. **DIVERGES** for transition-time animation.
12. **`m_TextBuffer` is formatted in Update**, not Draw. Port formats in Draw. Same display result, slightly different state semantics.
13. **GameOver path**: port calls `GameOver(-1, -1.0, -1)` then `m_TimeRemaining = 0` then SFXPlay. Order matches binary. ✓ For the SFX, the binary plays `"time-up"` once via the SFX delegate machinery; port plays `"time-up"` directly. **MATCHES** in spirit.

### 3.11 Draw @ 0x001628D8 — **CRITICAL** (font size = 1.0 instead of 32.0)

Binary call shape resolved by hand-disassembly @ 0x00162982:
```
Mortar::Font::DrawString(font_ptr,
                         s0 = pos.x + (-0.6 * size.x),     // x
                         s1 = pos.y,                        // y
                         s2 = 0.0,                          // z
                         s3 = 32.0,                         // FONT SIZE  ← DAT_00162b0c
                         s4 = s5 = s6 = 0.0,
                         iter, tinted_drawColour, align = 0xe, 0)
```

Port (`TimeControl.cpp:182`–`184`):
```cpp
font->DrawString(1.0f, 1.0f, 0.0f,
                 m_TextBuffer, drawPos,
                 m_DrawColour, 0xe);
```

Wrapper expands to `(scale=1.0, maxWidth=1.0, z=0.0)`; **scale is the
font size in port semantics**. So the countdown text is being drawn at
1 pixel tall instead of 32. **CRITICAL — text is invisible.**

Other Draw findings:

- alignment 0xe = RIGHT | MIDDLE | BOTTOM ✓ (port matches).
- Tint: binary applies `TintColour(m_DrawColour)` (HUD-scale tint). Port passes `m_DrawColour` directly (no global HUD-scale modulation). **MINOR DIFF.**
- Powerup overlay branch:
  - `font->DrawString(1.0f, 1.0f, 0.0f, m_PowerupOverlay, ...)` — port size 1.0, binary uses **24.0** (s3 = 0x41c00000 = 24.0). **CRITICAL — overlay text invisible.**
  - drawY: binary uses `pos.y - 32.0` (DAT_00162b0c). Port also uses `pos.y - 32.0` (POWERUP_Y_OFFSET). ✓
  - Tint: binary loads from `*GOT[0x73c8]` (a `Colour*`). Port uses placeholder white. **DIVERGES — TODO already flagged.**
- Tick-tock UV-quad branch is dead code in shipped binary (`m_SecondaryTex`
  never assigned in any code path that reaches Draw); port omitted. **MATCHES** in observable behavior.

### Summary of TimeControl

| Method | Status |
|--------|--------|
| ctor | MATCHES |
| Init | MATCHES |
| Release | MATCHES |
| Reset | MINOR DIFF (pSaveData seed missing) |
| Skip | MINOR DIFF (save restore missing) |
| CountDown | MATCHES |
| GetCountDown | MATCHES |
| AddTime | MATCHES |
| SetToMultiplayerState | MATCHES |
| Update | DIVERGES (12 findings; flash thresholds, tick SFX, count-up, powerup, save-data, position recompute) |
| Draw | CRITICAL (font size = 1.0 instead of 32.0; text invisible) |

---

## Static caches (ScoreControl GOT+0x45180 block)

Three cxa-guard-protected statics in the binary, addressed via
`iVar12 + DAT_001588c8` and friends. Port (`ScoreControl.cpp:19`–`21`)
declares three file-scope statics:

| Port name | Binary slot | Used at |
|-----------|-------------|---------|
| `s_SfxCooldown`     | DAT_001588c8 + 4   | Stage-3 bonus SFX rate-limit (decremented Stage-2) |
| `s_StaticTimer`     | DAT_001588c8 + 0   | Stage-1 non-Classic 0.25s gate |
| `s_BannerSinIdx`    | DAT_001597a8 + 0x18 (separate guard slot) | Section C colour-pulse phase |

Layout question: the binary's three slots live in a single 12-byte block
guarded by one cxa-guard. The port's three independent file-statics
serialise differently if multiple ScoreControl instances ever exist (MP).
Today only one P1 instance runs at a time, so the singleton-static
mapping is acceptable. **MINOR DIFF — single-instance OK.**

The "GOT+0x45180" slot is a heuristic — actual address derived per
`*(int *)(iVar12 + DAT_001588c8)` chain. Treat the three port statics as
correct in spirit; verify offsets stay independent if multiplayer is
ever ported.

---

## Port-side action items, ranked

1. **CRITICAL — TimeControl::Draw font size.** Replace `1.0f` with
   `32.0f` in both DrawString calls (`TimeControl.cpp:182` and
   `TimeControl.cpp:192`). Without this the countdown is invisible.
2. **DIVERGES — ScoreControl::PreDraw Section A baseline constant.** The
   port comment cites `DAT_00159090 = 0.75` but the binary value is
   `0x42c00000 = 96.0`. Replace `0.75f` at `ScoreControl.cpp:336` with
   `96.0f` and update comment.
3. **DIVERGES — ScoreControl::PreDraw Section A alignment.** Change
   `Mortar::FONT_ALIGN_CENTER` (0x01) to `0x0d`
   (CENTER|MIDDLE|BOTTOM) at `ScoreControl.cpp:349`.
4. **DIVERGES — ScoreControl::PreDraw Section B cursor advance.** At
   `ScoreControl.cpp:403`, replace
   `cursorX += MeasureWidth(scale, label) + 5.0f` with
   `cursorX += MeasureWidth(scale, label) * scale + 5.0f`.
5. **DIVERGES — TimeControl::Update flash thresholds.** Change
   `< 2.0f / < 5.0f / < 10.0f` to `< 3.0f / < 6.0f / < 11.0f`.
6. **DIVERGES — TimeControl::Update flash function.** Replace
   `sinf(phase*pi)` smooth pulse with binary's
   `((int)(t*N)) & 1 ? red : white` boolean toggle (8/4/2 Hz from
   `t*8`, `t*4`, `t+t` respectively).
7. **DIVERGES — TimeControl::Update count-up branch.** Add the
   `if (m_CountdownStart <= 0.0f) m_TimeRemaining += dt` early arm
   before the countdown logic.
8. **DIVERGES — ScoreControl::PreDraw Section E banner scale.** Replace
   linear `bannerScale = m_BannerScaleTime` with
   `bannerScale = SinIdx((uint16_t)(m_BannerScaleTime * 21840.0f))`.
9. **DIVERGES — ScoreControl::Update Stage-6 wave-mode centring.**
   Implement the `pos -= Vec3(160 + measuredWidth*0.5, -80, 0)` shift
   when `someTimer > 0`.

Items below are MINOR DIFF / TODO — the port comments already flag
them and they currently produce identical or harmless output. Defer
until the underlying systems (PowerUpManager, FruitSaveData,
IsMultiplayer, cameraIntensity) are ported:

- ScoreControl::Skip pSaveData[300] gate
- ScoreControl::Update bonus-count-up SFX, banner pSaveData[300] gate
- ScoreControl::Update PulseAngle decay overflow regime
- ScoreControl::Update wave-mode MP slide formula
- ScoreControl::Draw cameraIntensity multiplier
- ScoreControl::PreDraw Section A alpha multiplier
- ScoreControl::PreDraw Section C colour pulse + localised label
- ScoreControl::PreDraw Section D anchor calc
- ScoreControl::PreDraw Section E translate offsets
- CoinCounter::Init coin-icon texture load
- CoinCounter::Draw entire body (latent — only matters once
  field_0x8c starts being driven by the coin-pickup code path)
- TimeControl::Update triple-guard `field_0x2 / 0x170 / 0x199`
- TimeControl::Update powerup overlay + time-multiplier
- TimeControl::Update tick/tock SFX one-shot
- TimeControl::Update pos.x recompute, pos.y reposition
- TimeControl::Reset pSaveData[+0x10c] save-resume seed
- TimeControl::Skip pSaveData[+0x10c] restore
- TimeControl::Draw HUD-scale TintColour modulation
- TimeControl::Draw powerup overlay GOT colour at `[GOT+0x73c8]`

---

## Verified-comment lines for `implementer` to paste

Apply only above functions whose body matches the binary today (modulo
TODO stubs that the port already explicitly flags). DO NOT paste
above DIVERGES / CRITICAL functions until they are fixed and re-audited.

```
// ScoreControl
// ASM-verified: 2026-04-30T04:20 binary @ 0x00158c7c (asm-inspector)   // ctor
// ASM-verified: 2026-04-30T04:20 binary @ 0x00158190 (asm-inspector)   // Init
// ASM-verified: 2026-04-30T04:20 binary @ 0x00158370 (asm-inspector)   // Release

// CoinCounter
// ASM-verified: 2026-04-30T04:20 binary @ 0x00135600 (asm-inspector)   // ctor
// ASM-verified: 2026-04-30T04:20 binary @ 0x00135548 (asm-inspector)   // Reset
// ASM-verified: 2026-04-30T04:20 binary @ 0x00135580 (asm-inspector)   // Update
// ASM-verified: 2026-04-30T04:20 binary @ 0x00135af4 (asm-inspector)   // GetType

// TimeControl
// ASM-verified: 2026-04-30T04:20 binary @ 0x001622e8 (asm-inspector)   // ctor
// ASM-verified: 2026-04-30T04:20 binary @ 0x001620e4 (asm-inspector)   // Init
// ASM-verified: 2026-04-30T04:20 binary @ 0x001623b4 (asm-inspector)   // Release
// ASM-verified: 2026-04-30T04:20 binary @ 0x001620f0 (asm-inspector)   // CountDown
// ASM-verified: 2026-04-30T04:20 binary @ 0x00162134 (asm-inspector)   // GetCountDown
// ASM-verified: 2026-04-30T04:20 binary @ 0x001204f0 (asm-inspector)   // AddTime
```
