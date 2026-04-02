# FruitNinja.exe — Port Readiness Report

## Symbol Coverage

**Total non-thunk .text functions: 9,624**

| Category | Count | % | Description |
|----------|-------|---|-------------|
| Named class methods | 8,799 | 91.4% | Have `Class::Method` names from ELF symbols |
| Named free functions | 335 | 3.5% | Global functions like `AddToCurrentScore`, `GameTaskUpdate` |
| `_GLOBAL__I_*` | 143 | 1.5% | C++ static initializers (one per .cpp TU) |
| `T.NNN` (compiler locals) | 323 | 3.4% | GCC local static helper thunks — unnamed |
| `FUN_*` (Ghidra auto) | 7 | 0.1% | Truly unnamed; Ghidra couldn't resolve |

### Named class distribution (top 15)

| Class | Functions |
|-------|-----------|
| std:: | 3,076 |
| Mortar:: | 2,875 |
| __gnu_cxx:: | 468 |
| TiXmlNode:: | 45 |
| WaveManager:: | 44 |
| Fruit:: | 42 |
| PowerUpManager:: | 37 |
| GameOverScreen:: | 37 |
| MenuButton:: | 35 |
| SlashEntity:: | 34 |
| GameModeScreen:: | 34 |
| LeaderboardScreen:: | 32 |
| MainScreen:: | 31 |
| GlesForm:: | 30 |
| TiXmlElement:: | 29 |

### Truly unnamed: FUN_* (7 functions)

| Address | Context |
|---------|---------|
| 0x120bfe | Near WaveManager code |
| 0x120c1a | Near WaveManager code |
| 0x1738e4 | Inside Fruit/Bomb region (4 adjacent funcs) |
| 0x1738e8 | " |
| 0x17390c | " |
| 0x173944 | " |
| 0x183474 | Near PSPParticle/FruitCamera region; 2 params |

### T.NNN helpers (323 functions)

GCC-generated thunks for local statics, vtable adjustors, and template helpers. Key ones identified:

| Symbol | Purpose |
|--------|---------|
| T_573, T_575 | Texture / SmartPtr helpers in MissControl |
| T_615 | Random probability check in SplatEntity |
| T_713 | SmartPtr constructor helper |
| T_1021 | Float math helper (combo score calculation) |
| T_1337, T_1338, T_1339 | Delegate construction helpers in GameTask |
| T_1353 | Delegate1<bool,MortarSound*> helper for SFX playback |

---

## What's Recovered

- **Struct layouts** for 20+ core classes (see `structs.md`)
  - Fruit, Bomb, SlashEntity, Game, FruitCamera, MortarCamera
  - HUD, HUDControl, MissControl, WaveManager, FruitSaveData
  - PowerUpManager, BonusManager, ScoreModifier, MAMAudioThread
  - ItemManager, FRUIT_INFO, ColLine/ColCircle, GlesForm, FruitNinja
- **Scoring pipeline** — end-to-end from SlashEntity collision through AddToCurrentScore
- **Collision system** — line segment vs circle intersection (ColLine vs ColCircle)
- **Game loop** — OspMain → Timer(10ms) → GameTaskUpdate state machine
- **Class hierarchy** — 96.5% symbol coverage (see `classes.md`)
- **Audio architecture** — MAMAudioThread with 16 voices, 16kHz, NLFQueue

---

## What's Missing for a Port

### Priority 1: Game State Machine

The `GameTaskUpdate` dispatches through a function pointer table indexed by `Game[0]` byte. Need to recover:
- All task states (menu, playing, game-over, pause, etc.)
- Transition logic between states
- The function pointer table entries

### Priority 2: T.NNN Helper Identification — DONE

Of the 323 addresses listed as `T.NNN` in FindTextFunctions output:
- **~120 are actual thunk functions** — GCC local-static helpers with internal linkage
- **~200 are ARM literal pool data** — not code; PC-relative constants embedded in larger functions

The ~120 real thunks fall into 10 categories (see `tmp/t_taxonomy.md` for full table):

| Category | Count | Replacement |
|----------|-------|-------------|
| SmartPtrNull | ~25 | `SmartPtr<T>::SetNull()` |
| ZeroInit | ~20 | `*ptr = 0` |
| DelegateCtor | ~15 | `Delegate1<bool,MortarSound*>::BaseDelegate()` |
| Random | ~12 | `Math::Random::Rand32()` or `RandF()` |
| MatrixOp | ~15 | `MatrixStack::Reset/Translate/Scale/Upload` |
| DrawOp | ~8 | `Mesh::DrawQuadUnCached()` |
| Colour | ~5 | `Colour(r,g,b,a)` |
| Math | ~5 | `Vec3 *= scalar`, `max(0,x)` |
| Callback | ~5 | Input/touch event handlers |
| Other | ~10 | strncpy, accessors, misc |

**For porting:** Replace all T.NNN calls with their named equivalents. Most are trivial one-liners duplicated per compilation unit.

### Priority 3: Rendering Pipeline — DONE

See `structs.md` "Rendering Pipeline" section. Full GameDraw (211 lines) decompiled:
- `Game::Draw()` / `GameTaskDraw` flow
- MatrixManager / MatrixStack setup (ortho projection)
- Fruit mesh/quad drawing (Mortar::Mesh::DrawQuad)
- GeometryBinding_Bada / shader passes
- SplatEntity rendering (background splats)
- SlashEntity blade geometry rendering

### Priority 4: Physics & Trajectory — DONE

See `structs.md` "Fruit Physics" section. Fruit::Update (412 lines) and SpawnFruit (248 lines) fully decompiled.

### Priority 5: Wave/Spawn System — DONE

See `structs.md` "Wave System" section. WaveManager::Init (470 lines) fully decompiled with complete XML schema.

### Priority 6: Asset Pipeline — MOSTLY DONE

- FRUIT_INFO fully mapped (816 bytes, ~85% typed) — see `structs.md`
- Sound system documented (GameSound pool + BadaSound backend)
- Texture loading via SmartPtr\<Texture\> + TextureManager (pattern understood)
- Font loading: not yet decompiled in detail

### Priority 7: Menu/UI Flow — DONE

See `structs.md` "Menu/UI Flow" section. Full screen hierarchy and callbacks documented.

### Priority 8: Save System — DONE

See `structs.md` "Save System" section. SaveCurrentData (108 lines) decompiled, persistence model documented.

### Priority 9: Platform Abstraction (port-time)

- Replace Bada OSP calls (Timer, Form, Touch, Player)
- Replace EGL/sgl with target platform GL context
- Replace BadaSound with cross-platform audio
- Strip OpenFeint/GameCenter (defunct services)
- Strip NetworkManager P2P (or reimplement)

---

## Suggested Approach

| Step | Task | Method |
|------|------|--------|
| 1 | Identify T.NNN helpers | Batch-decompile 323 T. functions, categorize |
| 2 | Game state machine | Decompile GameTaskUpdate function table, map all states |
| 3 | Rendering pipeline | Decompile GameTaskDraw, Fruit::Draw, SlashEntity::DrawSlice |
| 4 | Fruit physics | Decompile Fruit::Chuck + Fruit::Update fully |
| 5 | Wave spawning | Decompile WaveManager::SpawnFruit and XML parsing |
| 6 | FRUIT_INFO complete | Decompile Fruit::LoadInfo to map all 816 bytes |
| 7 | Menu flow | Decompile DojoScreen, screen transitions |
| 8 | Save/load | Decompile FruitSaveData read/write |
| 9 | Platform shim | Design abstraction layer for target platform |
| 10 | Source reconstruction | Convert decompiled C to clean C++ with recovered types |
