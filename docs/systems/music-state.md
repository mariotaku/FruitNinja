# Music State Machine

<!-- Analysed: 2026-04-26T00:00 -->
<!-- Updated: 2026-04-26T00:00 — Correction: GameInit sets m_TransitionTimer=-1.0 at boot;
     "Music-menu" plays first, not "background". See "Initial Seed Call" section. -->

This document is the sole spec an implementer needs to port `UpdateMusic` 1:1 from
`FruitNinja.exe` (ARM32 LE, Halfbrick Mortar Engine). No further Ghidra work is needed.

---

## Table of Contents

1. [Overview](#overview)
2. [DAT_ Symbol Resolution](#dat-symbol-resolution)
3. [Static State Layout](#static-state-layout)
4. [Function Pseudocode](#function-pseudocode)
5. [Called Functions](#called-functions)
6. [State-Machine Prose Description](#state-machine-prose-description)
7. [Callers and Cadence](#callers-and-cadence)
8. [Initial Seed Call](#initial-seed-call)
9. [Port-Side Gaps](#port-side-gaps)

---

## Overview

`UpdateMusic(float dt)` at **0x0016a68c** (Ghidra also shows an inner-block label at
0x0016a808 and an entry-point alias at 0x0016a860 which is the shared branch tail;
the canonical function entry is 0x0016a68c). It is called every frame from
`GameUpdate` once `LoadingJob::IsLoaded()` returns non-zero.

The function:
- Maintains a signed float "current volume" in range `[-1.0, +0.55 * masterVolume]`.
  Negative = menu track; positive = gameplay track; 0 = silence / transition.
- Ramps that float by `dt * 4.0` per frame toward +cap (gameplay) or down toward
  -1.0 (menu), based on game state and ActorManager entity counts.
- Fires `SongPlay` once per direction-change when the track ID flips between -1 and +1.
- Arms one-shot preload timers for in-game and arcade-mode sounds when conditions are met.
- Every frame (after the volume float changes): calls `SoundManager::SetMusicVolume`
  with `abs(currentVol) * 0.4`.

Volume is NEVER passed directly to SongPlay; SongPlay just starts the track at the
engine's internal volume. The volume ramp only drives `SetMusicVolume`.

---

## DAT_ Symbol Resolution

All addresses are binary-section addresses in `FruitNinja.exe`.

GOT base for the function: `r4 = 0x00081a94 + 0x0016a69c = 0x001ec130`

### Float constants in literal pool

| Binary address | Renamed symbol | Value (hex) | Value (float) | Role in UpdateMusic |
|---|---|---|---|---|
| 0x0016a92c | `g_MusicRampFastMultiplier` | 0x3fcccccd | **1.6** | Multiplier on dt-delta during fast-ramp path (negative vol, no entities) |
| 0x0016a930 | `g_MusicVolMinCap` | 0xba83126f | **-0.001** | Lower bound for the fast-ramp cap (effectively near-zero negative) |
| 0x0016a934 | `g_MusicVolZero` | 0x00000000 | **0.0** | Clamp-to-zero used in music-disabled (m_bMusicOn == 0) ramp |
| 0x0016a938 | `g_MusicVolOutputScale` | 0x3ecccccd | **0.4** | Scales abs(currentVol) before SetMusicVolume |
| 0x0016a93c | `g_MusicVolMaxScale` | 0x3f0ccccd | **0.55** | Multiplied by pGameSound->m_MasterVolume to get the upper cap |

### GOT-relative pointer table

`iVar5` = GOT base = 0x001ec130 (computed as `DAT_0016a940 + 0x0016a69c` per ARM PC-relative lit pool).

| Literal pool addr | Stored offset | Computed BSS addr | Contents / interpretation |
|---|---|---|---|
| 0x0016a944 | 0x000452d4 | 0x00231404 (BSS) | Base of `g_MusicState` static struct (see below). Direct field access: no extra dereference. |
| 0x0016a948 | 0x000076c0 | 0x001f37f0 (BSS) | Pointer-to-pointer: `*(float**)(iVar5+0x76c0)` → `g_currentVolume` float in BSS |
| 0x0016a94c | 0x00007990 | 0x001f3ac0 (BSS) | Pointer-to-pointer: `*(int*)(iVar5+0x7990)` → `g_GameData` base pointer |
| 0x0016a950 | 0x00007c54 | 0x001f3d84 (BSS) | Base of preload timer block (direct field access: +0x14 = ingame timer, +0x18 = arcade timer) |
| 0x0016a954 | 0x000074e8 | 0x001f3618 (BSS) | Pointer-to-pointer: `*(int**)(iVar5+0x74e8)` → `g_trackId` int in BSS |
| 0x0016a958 | 0xfffd0657 (relative) | 0x001bc787 | String literal **"Music-menu"** (null-terminated) |
| 0x0016a95c | 0xfffd0662 (relative) | 0x001bc792 | String literal **"background"** (null-terminated) |

The string offsets 0xfffd0657 and 0xfffd0662 are PC-relative displacements stored in
the literal pool; added to `r4` at runtime via `adds r1,r4,r1`.

### g_GameData fields accessed

The pointer chain `*(int*)(iVar5 + DAT_0016a94c)` yields the `g_GameData` base address
(already documented in `docs/structs/game.md`). The fields used by UpdateMusic:

| g_GameData offset | Type | Name | Meaning in UpdateMusic |
|---|---|---|---|
| +0x04 | byte | `gameMode` | 0x02 = Zen/ZenBlitz — triggers the arcade preload arm gate |
| +0x0C | float | `m_TransitionTimer` | Must be >= 0.0 to allow preload arm (negative = transition active) |
| +0x44 | bool | (not used) | — |
| +0x45 | bool | `m_bMusicOn` | 0 = music toggle OFF → ramp toward 0.0; non-zero = music ON → ramp toward cap |
| +0x188 | GameSound* | `pGameSound` | `*(float*)pGameSound` = `GameSound::m_MasterVolume`; cap = 0.55 * m_MasterVolume |

Note: `m_bMusicOn` at +0x45 drives the entire volume-ramp direction. When it is 0,
UpdateMusic ramps currentVol toward 0.0 and never calls SongPlay. When it is non-zero,
UpdateMusic ramps toward the cap and calls SongPlay on track flips.

### GameSound first field

`GameSound::m_MasterVolume` is at offset +0x00 of the GameSound struct (0x708 bytes,
32 slots). It is initialised to 1.0 in `GameSound::GameSound()` (0x00129450).
UpdateMusic's volume cap is `0.55 * m_MasterVolume`, which at initialisation = 0.55.

---

## Static State Layout

UpdateMusic owns two static blocks in BSS. Neither has a C++ class; they are plain
arrays of bytes/floats initialised to zero at process start.

### g_MusicState (base address 0x00231404, size >= 0xe2 bytes)

Accessed via `r4 + DAT_0016a944 + field_offset`.

| Offset | Type | Name | Written by | Read by | Meaning |
|---|---|---|---|---|---|
| +0xe0 | bool (byte) | `armed_ingame` | UpdateMusic | UpdateMusic, PreloadInGameSounds | Set to 1 when preload-ingame countdown should tick. PreloadInGameSounds has its OWN one-shot guard at g_MusicState+0x21 (separate field, same struct). |
| +0xe1 | bool (byte) | `armed_arcade` | UpdateMusic | UpdateMusic, PreloadArcadeModeSounds | Set to 1 when preload-arcade countdown should tick. PreloadArcadeModeSounds guard at g_MusicState+0x20. |

Note: `PreloadInGameSounds` checks `g_MusicState+0x21` as its own "already-preloaded"
idempotency guard (set to 1 on first call, skips subsequent calls). `PreloadArcadeModeSounds`
checks `g_MusicState+0x20`. These are different fields from +0xe0/+0xe1 — the arm flags
gate the timer countdown, while +0x20/+0x21 gate the actual PreLoad vtable calls inside
the preload functions.

### Preload timer block (base address 0x001f3d84, size >= 0x1c bytes)

Accessed via `r4 + DAT_0016a950 + field_offset`.

| Offset | Type | Name | Init | Meaning |
|---|---|---|---|---|
| +0x14 | float | `timer_ingame` | 0.0 (BSS) | Counts down by `dt*4.0` each frame. When it crosses 0, fires `PreloadInGameSounds()`. |
| +0x18 | float | `timer_arcade` | 0.0 (BSS) | Counts down by `dt*4.0` each frame. When it crosses 0, fires `PreloadArcadeModeSounds()`. |

Both timers are armed (started) implicitly: when `armed_ingame` / `armed_arcade` become
1, the timer is already at 0 (BSS zero), so the countdown fires on the very next frame
unless the timer was separately set to a positive value elsewhere.

### g_currentVolume (float*, resolved at runtime to some BSS float)

A single float. Signed range used:
- Negative (towards -1.0) = menu-music "side"
- Positive (towards 0.55 * m_MasterVolume) = gameplay-music "side"
- Exactly 0.0 = silence (neither track active)

### g_trackId (int*, resolved at runtime to some BSS int)

Values:
- `-1` = menu track is active (SongPlay("Music-menu") has been called)
- `+1` = gameplay track is active (SongPlay("background") has been called)
- `0` = no track playing (initial state / between tracks)

---

## Function Pseudocode

The following C-like pseudocode is a direct translation of the binary at 0x0016a68c.
Variable names match the Ghidra decompile; all ARM idiom inversions are already applied
(see CLAUDE.md: "ARM comparison idioms"). Each comment cites the disassembly address.

```c
// UpdateMusic(float dt)
// Called every frame from GameUpdate when LoadingJob::IsLoaded().
// Address: 0x0016a68c
void UpdateMusic(float dt) {
    // 0x0016a6a6: per-frame delta = dt * 4.0 (volume ramp rate = 4.0 units/second)
    float delta = dt * 4.0f;

    // Save old volume for change-detection at end
    float oldVol = *g_currentVolume;   // 0x0016a6ac

    // -----------------------------------------------------------------------
    // BLOCK 1: Arm preload-ingame-sounds countdown
    // Condition: NOT already armed  AND  m_TransitionTimer >= 0.0
    // Sub-condition (to SKIP arming): currentVol < 0.0
    //   AND GetNumEntities(Fruit==0) != 0
    //   AND GetNumEntities(Bomb==1)  != 0
    // -----------------------------------------------------------------------
    if (!g_MusicState.armed_ingame) {           // 0x0016a6a0
        if (g_GameData->m_TransitionTimer >= 0.0f) {  // 0x0016a6ba: vcmpe / blt
            bool skip_arm = false;
            if (*g_currentVolume < 0.0f) {     // 0x0016a6c4: bpl
                // Only skip arming if fruits AND bombs are both present
                ActorManager* am = ActorManager::GetInstance();
                if (ActorManager::GetNumEntities(am, 0) != 0) {  // type 0 = Fruit
                    am = ActorManager::GetInstance();
                    if (ActorManager::GetNumEntities(am, 1) != 0) {  // type 1 = Bomb
                        skip_arm = true;        // 0x0016a6e4: cbnz r0 → LAB_0016a6f0
                    }
                }
            }
            if (!skip_arm) {
                g_MusicState.armed_ingame = 1;  // 0x0016a6ec
            }
        }
    }

    // -----------------------------------------------------------------------
    // BLOCK 2: Tick preload-ingame countdown; fire PreloadInGameSounds on expiry
    // -----------------------------------------------------------------------
    if (g_MusicState.armed_ingame) {            // 0x0016a6f4
        float t = preloadTimerBlock->timer_ingame;  // 0x0016a6fe
        if (t > 0.0f) {
            t -= delta;
            preloadTimerBlock->timer_ingame = t; // 0x0016a714
            if (t <= 0.0f) {
                PreloadInGameSounds();           // 0x0016a71e
            }
        }
    }

    // -----------------------------------------------------------------------
    // BLOCK 3: Arm preload-arcade-sounds countdown
    // Condition: gameMode == 0x02 (Zen/ZenBlitz)  AND  NOT already armed
    //            AND  m_TransitionTimer >= 0.0
    // Sub-condition (to SKIP arming): currentVol < 0.0
    //   AND GetNumEntities(Fruit) != 0  AND GetNumEntities(Bomb) != 0
    // -----------------------------------------------------------------------
    if (g_GameData->gameMode == 0x02) {         // 0x0016a726
        if (!g_MusicState.armed_arcade) {       // 0x0016a730
            if (g_GameData->m_TransitionTimer >= 0.0f) {  // 0x0016a742
                bool skip_arm = false;
                if (*g_currentVolume < 0.0f) {  // 0x0016a74c: bpl
                    ActorManager* am = ActorManager::GetInstance();
                    if (ActorManager::GetNumEntities(am, 0) != 0) {
                        am = ActorManager::GetInstance();
                        if (ActorManager::GetNumEntities(am, 1) != 0) {
                            skip_arm = true;
                        }
                    }
                }
                if (!skip_arm) {
                    g_MusicState.armed_arcade = 1;  // 0x0016a774
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // BLOCK 4: Tick preload-arcade countdown; fire PreloadArcadeModeSounds on expiry
    // -----------------------------------------------------------------------
    if (g_MusicState.armed_arcade) {            // 0x0016a77c
        float t = preloadTimerBlock->timer_arcade;  // 0x0016a786
        if (t > 0.0f) {
            t -= delta;
            preloadTimerBlock->timer_arcade = t;
            if (t <= 0.0f) {
                PreloadArcadeModeSounds();       // 0x0016a7a6
            }
        }
    }

    // -----------------------------------------------------------------------
    // BLOCK 5: Volume ramp — split on m_bMusicOn flag
    // -----------------------------------------------------------------------
    if (g_GameData->m_bMusicOn == 0) {
        // ---- Music DISABLED branch (0x0016a868) ----
        // Ramp currentVol toward 0.0 from either direction
        float v = *g_currentVolume;             // 0x0016a86c
        float newV;
        if (v > 0.0f) {
            // Positive side: decrease toward 0
            newV = v - delta;
            if (newV <= 0.0f) {
                newV = 0.0f;                    // g_MusicVolZero = DAT_0016a934
            }
        } else {
            // Zero or negative: do NOT go more negative
            // ARM: "bpl 0x0016a8ae" = skip if v >= 0 (i.e. only act if v < 0)
            if (v < 0.0f) {
                newV = v + delta;
                if (newV >= 0.0f) {             // 0x0016a89c: vcmpe s16,#0 + it pl
                    newV = 0.0f;
                }
            } else {
                goto LAB_end_ramp;              // v == 0.0, nothing to do
            }
        }
        *g_currentVolume = newV;                // 0x0016a8aa
    } else {
        // ---- Music ENABLED branch (0x0016a7b6) ----
        // Check if gameplay is in "transition" (m_TransitionTimer < 0)
        if (g_GameData->m_TransitionTimer < 0.0f) {  // 0x0016a7ba: bpl → 0x0016a80a
            // Transition active: ramp DOWN toward -1.0 (kill gameplay music)
            float v = *g_currentVolume - delta; // 0x0016a7d0: vsub s16,s14,s16
            if (v <= -1.0f) {                   // 0x0016a7d4: vcmpe s16,s15 where s15=-1.0
                v = -1.0f;                      // 0x0016a7de: vmov.le.f32 s16,s15
            }
            *g_currentVolume = v;               // 0x0016a7e2: vstr s16,[r3]
            // If still negative (ramping), or already on menu track: skip SongPlay
            if (v >= 0.0f) {                    // 0x0016a7e6: bpl → 0x0016a8ae
                goto LAB_end_ramp;
            }
            if (*g_trackId == -1) {             // 0x0016a7f6: cmp r2, #0xffffffff
                goto LAB_end_ramp;              // already on menu track, no re-play
            }
            // Flip track ID to -1 (menu) and call SongPlay("Music-menu")
            *g_trackId = -1;                    // 0x0016a800: str r2,[r3]
            SoundManager* sm = SoundManager::GetInstance();  // 0x0016a802
            SoundManager::SongPlay(sm, "Music-menu");        // 0x0016a860
        } else {
            // No transition: gameplay / menu determination by vol sign + entity counts
            float* volPtr = g_currentVolume;
            float v = *volPtr;
            if (v < 0.0f) {
                // Volume is on the menu side — check if we should ramp toward gameplay
                ActorManager* am = ActorManager::GetInstance();
                if (ActorManager::GetNumEntities(am, 0) == 0) {
                    // No fruits: skip fast-ramp, fall through to slow ramp below
                    goto slow_ramp;
                }
                // Fruits present: fast-ramp toward g_MusicVolMinCap (-0.001)
                float newV = v + delta * 1.6f;  // 0x0016a830: vmla s15,s16,s14 (s14=1.6)
                if (newV >= -0.001f) {          // 0x0016a838: vcmpe s15,s14 (s14=-0.001)
                    newV = -0.001f;             // cap at g_MusicVolMinCap
                }
                *volPtr = newV;                 // 0x0016a846: vstr s15,[r5]
                goto LAB_end_ramp;
            }

        slow_ramp:
            // Volume >= 0 (gameplay side) or no fruits: slow ramp upward
            // Cap = 0.55 * pGameSound->m_MasterVolume
            float cap = 0.55f * g_GameData->pGameSound->m_MasterVolume;  // 0x0016a8ee..0x0016a904
            float newV = *g_currentVolume + delta;   // 0x0016a8fc: vadd s16,s16,s15
            if (newV >= cap) {                        // 0x0016a908: vcmpe s16,s15
                newV = cap;
            }
            *g_currentVolume = newV;                  // 0x0016a916
            // If still at or below zero: no track flip needed
            if (newV <= 0.0f) {                       // 0x0016a91a: vcmpe s16,#0
                goto LAB_end_ramp;
            }
            if (*g_trackId == 1) {                    // 0x0016a852: cmp r2,#0x1
                goto LAB_end_ramp;                    // already on gameplay track
            }
            // Flip track ID to +1 (gameplay) and call SongPlay("background")
            *g_trackId = 1;                           // 0x0016a858: str r2,[r3]
            SoundManager* sm = SoundManager::GetInstance();  // 0x0016a85a
            SoundManager::SongPlay(sm, "background"); // 0x0016a860
        }
    }

LAB_end_ramp:
    // -----------------------------------------------------------------------
    // BLOCK 6: Drive SetMusicVolume every frame when vol changed
    // Condition: trackId != 0  AND  currentVol != oldVol
    // -----------------------------------------------------------------------
    if (*g_trackId != 0) {                          // 0x0016a8b4: cmp r3,#0
        float v = *g_currentVolume;
        if (oldVol != v) {                          // 0x0016a8c0: vcmpe s17,s15
            SoundManager* sm = SoundManager::GetInstance();  // 0x0016a8ca
            float absV = Math::Abs<float>(v);       // 0x0016a8d4: blx 0x00106200
            SoundManager::SetMusicVolume(sm, absV * 0.4f);  // 0x0016a8de..0x0016a8e2
        }
    }
}
```

### Pseudocode notes

- `Math::Abs<float>` at 0x00106200: a single-instruction thunk (`vabs.f32 s0,s0; bx lr`).
  Simply returns `fabsf(x)`.
- The `slow_ramp` label above corresponds to the `goto LAB_0016a8e8` branch in Ghidra's
  decompile. The ARM jump target is 0x0016a8e8.
- The `goto LAB_end_ramp` targets correspond to `goto LAB_0016a8ae` in the Ghidra listing.

---

## Called Functions

### PreloadInGameSounds (0x001695e8, 42 instructions)

Guards with `g_MusicState+0x21` (byte flag); returns immediately if already called.
Sets that flag, then calls `SoundManager::PreLoadSound` on four assets:
1. "Time-tock" (0x001bc29c) — timer countdown tock SFX
2. "Time-tick" (0x001bc2a6) — timer countdown tick SFX
3. "Critical" (0x001bceff) — critical-slice SFX
4. Loop for i=1..3: `sprintf(buf, "%s%d", "Combo-", i)` → "Combo-1", "Combo-2", "Combo-3"

Binary address of the guard flag: `g_MusicState + 0x21` (BSS).

### PreloadArcadeModeSounds (0x00169504, 52 instructions)

Guards with `g_MusicState+0x20`; returns immediately if already called.
Sets that flag, then calls `SoundManager::PreLoadSound` on twelve assets in fixed order:
1. "Combo-Blitz-Backing-Light" (0x001bc258) — blitz mode backing track light variant
2. "Combo-Blitz-Backing" (0x001bc272) — blitz mode backing track
3. Two more calls using same string as #2 (deliberate — preloads the same asset twice,
   binary-faithful; likely a copy-paste in the original source)
4. Eight more arcade-specific sound strings (addresses 0x001ba775, 0x001ba9fa and
   nearby, including "combo-blitz-1", "combo-blitz-2", and several "Bonus-Banana-*" strings
   at 0x001bc694: "Bonus-Banana-Freeze", "Bonus-Banana-Frenzy", "Bonus-Banana-X2")

Do NOT simplify the two identical consecutive calls — the binary calls PreLoadSound
on the same string twice; replicate this.

### Math::Abs<float> (0x00106200, 2 instructions)

```asm
vabs.f32 s0, s0
bx lr
```

Equivalent to `fabsf(x)`. No side effects.

### Mortar::ActorManager::GetInstance (0x001705f0, 68 bytes)

Lazy singleton with `__cxa_guard`. Returns the singleton `ActorManager*`.

### Mortar::ActorManager::GetNumEntities(ActorManager*, int type) (0x0016ff98, 18 bytes)

Returns the count of active entities of the given type from the per-type std::list.
Entity type indices used in UpdateMusic:
- `0` = **Fruit** (confirmed, docs/engine/actor-manager.md)
- `1` = **Bomb** (confirmed, docs/engine/actor-manager.md)

### Mortar::SoundManager::GetInstance (0x00105948)

Lazy singleton; creates SoundManagerMAM if needed.

### Mortar::SoundManager::SongPlay(SoundManager*, char* name) (0x0018c954 / thunk 0x000f8a90)

Starts music playback by name. The `name` parameter is a relative path suffix;
BadaSound::MusicPlay prepends the base path and appends an extension. UpdateMusic
calls through the GOT thunk at 0x000f8a90. The SoundManager object pointer is r0
(returned by GetInstance just before the call).

### Mortar::SoundManager::SetMusicVolume(SoundManager*, float vol) (0x0018ca78)

Sets the static `s_MusicVolume` and calls `SyncMutes()`. Input range expected 0.0..1.0.
UpdateMusic passes `abs(currentVol) * 0.4`, so the max sent to SetMusicVolume is
`0.55 * m_MasterVolume * 0.4 = 0.22 * m_MasterVolume`. At default m_MasterVolume=1.0
the effective max music volume is **0.22**.

---

## State-Machine Prose Description

### What `m_bMusicOn` (+0x45) means

`g_GameData+0x45` is the music-toggle flag loaded by `InitialiseData` step 10:
`m_bMusicOn = (GetTotal(saveData, "musicOff") == 0)`. True (1) = music enabled at last
session. The main-menu toggle button writes this byte. When 0, UpdateMusic ramps
currentVol toward 0.0 and never calls SongPlay or flips g_trackId.

### What `m_TransitionTimer` (+0x0c) means

`g_GameData+0x0C` is a countdown float used during gameplay transitions (bomb hit,
retry, game-over entry). When it is negative (counting down), UpdateMusic treats it as
"gameplay scene is transitioning out" and ramps currentVol DOWN toward -1.0
(i.e. forces a switch back to the menu track). When it is >= 0.0, no transition is
forced and the entity-count logic determines the ramp direction.

### When does the track flip?

The track flips from menu (-1) to gameplay (+1) when:
1. `m_bMusicOn == 1` (music enabled)
2. `m_TransitionTimer >= 0.0` (no forced transition)
3. `*g_currentVolume >= 0.0` (already crossed zero from the negative side)
4. `GetNumEntities(Fruit) > 0` OR `*g_currentVolume >= 0` without fast-ramp
5. The slow-ramp path pushes vol past 0 and `g_trackId != 1`

The fast-ramp path (fruits present + vol < 0) ramps to `-0.001` but never crosses 0
itself — it can only bring vol to near-zero. The crossover to positive happens via the
slow-ramp path once vol reaches 0.

Actually more precisely: the fast-ramp caps at `g_MusicVolMinCap = -0.001`. Once vol
reaches -0.001, the next frame takes the `slow_ramp` path (v = -0.001 < 0 but the
fast-ramp guard reuses vol, so with delta it may cross 0 in one frame). The track flip
fires the first frame `g_currentVolume > 0.0` AND `g_trackId != 1`.

### Ramp rate in units per second

`delta = dt * 4.0f`. With dt = 1/60 (fixed step), delta per frame = 0.0667.
Ramp rate = **4.0 units/second** for both directions unless the 1.6 fast-multiplier
applies. With 1.6 multiplier: **6.4 units/second**.

Time to ramp from 0 to full (cap ≈ 0.55): 0.55 / 4.0 ≈ **0.14 seconds** (8 frames).
Time to ramp from 0 to -1.0 (menu side): 1.0 / 4.0 = **0.25 seconds** (15 frames).
Fast-ramp from -1.0 to -0.001 (fruits present): 0.999 / 6.4 ≈ **0.16 seconds** (9 frames).

### What the preload arm logic does

Two "arm" flags gate one-shot countdown timers:

**Armed-ingame** (`g_MusicState+0xe0`):
- Armed when: flag is 0, m_TransitionTimer >= 0, and NOT (vol < 0 AND fruits exist AND bombs exist)
- i.e. arm the preload as soon as the scene is quiet (no entities) or transitioning upward
- Once armed, the timer at preloadTimerBlock+0x14 ticks down by delta each frame
- When timer <= 0: fires `PreloadInGameSounds()` once (inner guard at g_MusicState+0x21)

**Armed-arcade** (`g_MusicState+0xe1`):
- Same logic PLUS requires `gameMode == 0x02` (Zen/ZenBlitz mode only)
- Fires `PreloadArcadeModeSounds()` (inner guard at g_MusicState+0x20)

Since both timers start at 0.0 (BSS), arming them causes an immediate fire on the NEXT
frame's Block 2 / Block 4 check (the `t > 0.0f` guard fails, so nothing ticks; the fire
is gated on `t <= 0.0f` which is always true from the start). In practice the preload
fires on the first eligible frame.

### Volume output mapping

```
abs(currentVol) * 0.4 -> SoundManager::SetMusicVolume
```

Menu track playing: currentVol ramps from 0 to -1.0. abs = 0..1.0, output = 0..0.4.
Gameplay track playing: currentVol ramps from 0 to cap (0.55). abs = 0..0.55, output = 0..0.22.
The menu track thus plays louder (up to 0.4) than the gameplay track (up to 0.22).

This asymmetry is intentional — "Music-menu" is the main theme, played louder; "background"
is the in-game ambient, played softer.

---

## Callers and Cadence

### UpdateMusic callers

XRefs to 0x0016a68c:
- `From 001edc44 [DATA]` — function pointer table entry (GOT reference)
- `From 000f86e4 in UpdateMusic [COMPUTED_CALL]` — thunk at 0x000f86dc that delegates to 0x0016a68c

Direct call site: **`GameUpdate(float, bool)` at 0x0016bed0**, line:

```c
// Inside: if (LoadingJob::IsLoaded() != 0) { ... }
SoundManager::Update(sm, scaledDt);
GameSound::Update();
UpdateMusic(scaledDt);   // <-- the call
ItemManager::Update(im, scaledDt);
```

The `scaledDt` passed to UpdateMusic is `dt` after the loading-screen timer logic —
effectively the fixed `dt = 1/60` from `g_GameData+0x38` when not loading. The call is
gated by `LoadingJob::IsLoaded()`, so UpdateMusic never runs during the loading splash.

Cadence: **once per frame at ~60Hz**, dt ≈ 0.01667.

---

## Initial Seed Call

<!-- Analysed: 2026-04-26T00:00 -->

There is **NO separate boot-time SongPlay call** in the binary. UpdateMusic is the
sole issuer of `SongPlay`. On the first frame after loading completes:
- `g_currentVolume` starts at 0.0 (BSS zero)
- `g_trackId` starts at 0 (BSS zero)
- `m_bMusicOn` depends on save data (1 if music was on; 1 on first run with no save)

**Critical:** `GameInit` (0x0016c644) writes `-1.0f` (0xbf800000) to `g_GameData+0x0c`
(`m_TransitionTimer`) in step 13 — immediately after creating TutorialControl, before
the loading guard for UpdateMusic is ever satisfied. See binary address 0x0016cb2a:

```c
// GameInit step 13 — after TutorialControl is created:
*(undefined4 *)(iVar7 + 0xc) = 0xbf800000;  // m_TransitionTimer = -1.0f
```

With music enabled, Block 5 on the **first eligible frame** takes the music-enabled path:
- `m_TransitionTimer = -1.0f < 0.0f` — the TRANSITION branch fires (0x0016a7b6).
- `currentVol = 0.0 - delta < 0` (ramping down); v is negative so `v >= 0` is false.
- `g_trackId != -1` (starts at 0) so the track flip fires: `g_trackId = -1`,
  `SongPlay("Music-menu")` is called.

So the **first track to play at boot is "Music-menu"**, not "background". The menu
theme plays as soon as the loading screen exits and UpdateMusic runs.

The transition back to "background" happens when gameplay begins and something sets
`m_TransitionTimer` back to >= 0.0. The key reset sites are:
- `EndRetryLevel` (0x0016a270): writes `0.0f` to m_TransitionTimer — gameplay resumes
- `InstantLevelDestroy` (0x0016a2de): writes `0.0f` to m_TransitionTimer
- `SkipToGameOver` (0x0016adec): writes `param_3` (caller-supplied value, typically a
  positive countdown) to m_TransitionTimer — transitions BACK to menu at game-over
- Various `MainScreen_Update` writes (0x0014ba2e, 0x0014ba4c, 0x0014b5f4, 0x0014b640,
  0x0014bdee, 0x0014c1c0, 0x0014c1cc) — screen state transitions

The flow is:
1. Boot → GameInit sets m_TransitionTimer = -1.0 → UpdateMusic plays "Music-menu"
2. Player starts game → some path sets m_TransitionTimer >= 0 → UpdateMusic ramps
   currentVol up toward cap → SongPlay("background") when vol crosses 0
3. Game ends → SkipToGameOver sets m_TransitionTimer negative again → UpdateMusic
   ramps currentVol down to -1.0 → SongPlay("Music-menu")

**Implication for the port:**
The port's current `SongPlay("Music-menu")` in `GameInitialise.cpp:136` is approximately
correct in effect (menu music plays at boot), but it is a manual call rather than going
through UpdateMusic. Once UpdateMusic is ported with the correct initial state
(m_TransitionTimer = -1.0f from GameInit), the manual boot call should be REMOVED —
UpdateMusic will fire SongPlay("Music-menu") automatically on its first eligible frame.

---

## Port-Side Gaps

The implementer needs to:

1. **Remove `SongPlay("Music-menu")` from `src/game/GameInitialise.cpp:136`.**
   This manual call is a placeholder. Once UpdateMusic is running AND `GameInit` sets
   `m_TransitionTimer = -1.0f`, UpdateMusic will automatically call SongPlay("Music-menu")
   on its first eligible frame — no manual boot call needed.

   Also ensure `GameInit` in the port writes `-1.0f` to `g_GameData.m_TransitionTimer`
   (offset +0x0c) at step 13 (after TutorialControl is created). This is the key init
   that makes UpdateMusic pick "Music-menu" at boot instead of "background".

2. **Add new fields to the port's `g_GameData` (or equivalent) if missing:**
   - `g_GameData+0x45` = `m_bMusicOn` — already documented in `docs/structs/game.md`,
     should exist in the port's `GameData` struct. Verify it is wired to the menu
     music-toggle button callback.
   - `g_GameData+0x188` = `pGameSound` (GameSound*) — already in `docs/structs/game.md`.
     The port's `GameSound::m_MasterVolume` must be at offset +0x00 of the struct.

3. **Add `g_currentVolume` (float) and `g_trackId` (int) static globals** to the
   port-side music state. These are BSS globals in the binary, zero-initialised.
   Suggested placement: a `MusicState` struct in `src/game/UpdateMusic.cpp` (static file-scope):
   ```cpp
   static float g_currentVolume = 0.0f;
   static int   g_trackId       = 0;
   ```

4. **Add `g_MusicState` armed flags (+0xe0/+0xe1) and preload timer block (+0x14/+0x18)**
   as additional static file-scope variables in the same file:
   ```cpp
   static bool  g_armedIngame  = false;
   static bool  g_armedArcade  = false;
   static float g_timerIngame  = 0.0f;
   static float g_timerArcade  = 0.0f;
   ```

5. **Wire `UpdateMusic(float dt)` into `GameUpdate`** immediately after the
   `GameSound::Update()` call (and `SoundManager::Update()`), inside the
   `LoadingJob::IsLoaded()` guard:
   ```cpp
   // src/game/GameUpdate.cpp, inside if (LoadingJob::IsLoaded())
   Mortar::SoundManager::GetInstance().Update(scaledDt);
   GameSound::Update();
   UpdateMusic(scaledDt);    // ADD THIS
   ItemManager::GetInstance()->Update(scaledDt);
   ```

6. **Implement `PreloadInGameSounds()` and `PreloadArcadeModeSounds()`** as
   free functions with own idempotency guards. The port-side
   `SoundManager::PreLoadSound` call can be a no-op initially (SDL2 may not need
   explicit preloading if sounds are loaded on first SFXPlay), but the function
   boundaries must exist with binary-faithful guard flags.

7. **`SoundManager::SongPlay(name)`** must be implemented. It maps to
   `BadaSound::MusicPlay` in the binary. Port equivalent: stop the current music
   stream, open and start the new one. The name is passed as-is; the port should
   apply the asset-directory prefix and `.ogg` / `.mp3` extension.

8. **`SoundManager::SetMusicVolume(float)`** must drive the actual audio volume.
   In the port this maps to SDL2 music volume controls.

9. **`Math::Abs<float>`** is already a trivial `fabsf` — use the standard library.

10. **Check `GameInitialise.cpp` TODO comment on line 135**: the comment already notes
    this is a placeholder and UpdateMusic will replace it. Confirm the comment is
    removed alongside the SongPlay call.

---

## See Also

- `docs/engine/sound-system.md` — SoundManager, BadaSound, MortarSound structs
- `docs/functions/sound.md` — GameSound::SFXPlay pipeline
- `docs/structs/game.md` — g_GameData struct (m_bMusicOn +0x45, pGameSound +0x188, m_TransitionTimer +0x0C)
- `docs/engine/actor-manager.md` — ActorManager::GetNumEntities(type), entity types
- `docs/functions/game-update.md` — GameUpdate call tree (UpdateMusic location confirmed)
