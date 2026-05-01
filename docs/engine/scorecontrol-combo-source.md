# ScoreControl combo-count source

RE'd from `FruitNinja.exe` (Bada ARM32 ELF, GhidraMCP). Resolves the global
that drives `ScoreControl::Update`'s digit-count state and the per-digit
"x2/x4/x8/..." overlay (`docs/structs/hud.md` §ScoreControl PreDraw §B).

The combo count is **not** at GOT[0x7478] as the prior handover doc claimed
— the handover conflated two adjacent globals. Corrected layout below.

## Two related globals (both pointer-via-GOT)

ScoreControl GOT base resolves at `0x001ec130` (PC+0x00093be4 from
`0x0015854c`).

| GOT offset | Pointer target (BSS) | Meaning                              | Init / sentinel |
|-----------:|----------------------|--------------------------------------|-----------------|
| `0x78f8`   | `0x0024d764` (BSS)   | **combo count int** (the one we want)| `0`              |
| `0x7478`   | `0x001f3e4c`         | last-slasher player index (MP arb.)  | `-1` = none      |

The "combo count" is what `ScoreControl::Update` reads to compute
`m_DigitCount = clamp(comboCount - 1, 0, 15)`.

## Resolved decompilation snippet (`ScoreControl::Update` @ 0x0015853c)

```c
// Read combo count via GOT[0x78f8] (DAT_00158c78 = 0x78f8)
int* g_pComboCount = *(int**)(GOT_BASE + 0x78f8);   // -> 0x0024d764
int comboCount     = *g_pComboCount;
int digitsActive   = (comboCount < 16) ? comboCount - 1 : 15;
this->m_DigitCount = digitsActive;

// Same-player guard via GOT[0x7478] (DAT_001588c4 = 0x7478, last-slasher idx)
if (digitsActive >= 1 && game->gameMode == 1 /* Classic */) {
    int lastSlasher = *(int*)*(int**)(GOT_BASE + 0x7478);
    if (this->m_LastDigitCount == lastSlasher) { /* ramp digit alphas up */ }
    else                                        { /* ramp digit alphas down,
                                                     then m_LastDigitCount = lastSlasher */ }
}
```

So the count itself is at `GOT[0x78f8]`; `GOT[0x7478]` is only consulted to
detect "same player kept the combo going" in split-screen MP and is irrelevant
single-player (always 0 once P1 slashes).

## Writers (xrefs to `0x0024d764`)

| Address                  | Function                       | Operation         | Trigger                          |
|--------------------------|--------------------------------|-------------------|----------------------------------|
| `0x001787a8..0x001787b0` | `Fruit::CollisionResponse`     | `*p = *p + 1`     | every successful fruit slice     |
| `0x0017873a`             | `Fruit::CollisionResponse`     | `*p = 0`          | different-player-slashed branch (MP) |
| `0x00176c84`             | `Fruit::KillFruit`             | `*p = 0`          | miss / lifecycle expiry          |
| `0x001625dc`             | `TimeControl::Update`          | `*p = 0`          | Arcade timer hits zero (game-over) |
| `0x00125cdc`             | `WaveManager::Reset`           | `*p = 0`          | new game / wave restart          |
| `0x00124b68`             | `WaveManager::Resume`          | `*p = save[+0x78]`| resume from suspend (save data)  |
| `0x0016cd34`             | `SaveCurrentData` (read only)  | `save[+0x78] = *p`| pause / app-suspend              |

The companion `last-slasher index` (GOT[0x7478]) is written next to most of
these (e.g. TimeControl game-over: `*p = 0xFFFFFFFF`, WaveManager::Reset:
`*p = 1`, Fruit::CollisionResponse: `*p = this->m_PlayerIdx`).

## Lifetime semantics

- **No timer-based decay.** The combo holds indefinitely; it is reset only by
  the discrete events in the table above.
- **Reset on miss** — `Fruit::KillFruit(this, doMissPenalty)` zeros the count
  when a fruit drops off-screen un-sliced.
- **Reset on Arcade timeout** — `TimeControl::Update` zeros it on Game Over.
- **Reset on wave restart** — `WaveManager::Reset` zeros it.
- **Persisted across pause/resume** via `FruitSaveData` slot at offset `+0x78`
  (see `Resume`/`SaveCurrentData`).
- **Multiplayer arbitration**: if a *different* player slashes, the count
  resets to 0 before the increment runs (so each slash by the new player
  re-establishes their combo from 1).

## Equivalent in the port

**There is currently no equivalent.** Search confirms:
- `Game.h` has no combo field (closest is `missCount` at +0x14, unrelated).
- `WaveManager` exposes `field_0x4c` / `field_0x60` ("combo timers", floats —
  these are the per-player **time** trackers in the doc, *not* the count).
- `Fruit::CollisionResponse` is a visual-only stub and does not increment any
  counter (`src/entities/Fruit.cpp:648..`).
- `ScoreControl::Update` line 150 reads its own placeholder
  `static int s_ComboCount = 0`.

A new global `int g_ComboCount` (single-player) — or, to anticipate MP, a
two-element array `int g_ComboCount[2]` indexed by `Fruit::m_PlayerIdx` — must
be added. Recommended home: a free function/static in the same translation
unit as the eventual `Fruit::CollisionResponse` score path, exported via a
small header (e.g. `src/game/ScoreState.h`) so both `Fruit` and `ScoreControl`
can see it. Match the binary by also adding a sibling `int g_LastSlasher`
initialised to `-1` (matches GOT[0x7478] sentinel from TimeControl GameOver).

## Single-line wire-up for ScoreControl.cpp

The current line 150–151 in `src/hud/ScoreControl.cpp`:

```cpp
static int s_ComboCount = 0;  // TODO: wire to real combo source from WaveManager / Score path
int digitsActive = s_ComboCount;
```

becomes (assuming a global `extern int g_ComboCount;` in the new header):

```cpp
int digitsActive = g_ComboCount - 1;     // matches binary @ 0x00158580: count - 1
```

Or, inlining the off-by-one to keep the existing clamp:

```cpp
int digitsActive = g_ComboCount - 1;     // binary computes count-1 BEFORE clamp
if (digitsActive < 0)  digitsActive = 0; // already in current code
if (digitsActive > 15) digitsActive = 15;
```

Note the **`-1`** — the binary does `comboCount - 1` *before* the 15-clamp,
not after. With combo=1 (first slash) digitsActive is 0 (no overlay yet),
combo=2 lights digit 0 ("x2"), combo=3 → digits 0–1 ("x2","x4"), etc. The
current placeholder's `digitsActive = s_ComboCount` is missing this `-1`.

## Cross-references and outstanding gaps

- The port-side increment is the larger missing piece. Implementer would need
  to extend `Fruit::CollisionResponse` to:
  1. Read `g_LastSlasher`; if `!= m_PlayerIdx`, reset `g_ComboCount = 0`,
     then store `g_LastSlasher = m_PlayerIdx`.
  2. `g_ComboCount += 1`.
  3. (Outside scope here) drive `BonusManager` / score multiplier off the new
     count.
- `Fruit::KillFruit` and `WaveManager::Reset`/`Resume` zero-writes also need
  porting once the global exists.
- `FruitSaveData` slot `+0x78` (combo) and `+0x74` (last-slasher) are not yet
  in the port's `FruitSaveData` struct.

## Binary references

- `ScoreControl::Update` @ `0x0015853c` (read site `0x00158c2e`, literal
  pool `0x00158c78` = `0x000078f8`)
- `Fruit::CollisionResponse` @ `0x001780b0` (increment block
  `0x001787a8..0x001787b0`)
- `Fruit::KillFruit` @ `0x00176abc` (zero write `0x00176c84`)
- `TimeControl::Update` @ `0x001624a4` (zero write `0x001625dc`)
- `WaveManager::Reset` @ `0x00125be4` (zero write `0x00125cdc`)
- `WaveManager::Resume` @ `0x00124b1c` (restore write `0x00124b68`)
- `SaveCurrentData` @ `0x0016ccc8` (save read `0x0016cd34`)
- BSS combo-count int @ `0x0024d764` (GOT[`0x78f8`] @ `0x001f3a28`)
- BSS last-slasher int @ `0x001f3e4c` (GOT[`0x7478`] @ `0x001f35a8`)
