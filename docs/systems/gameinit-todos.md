# GameInit unported step specs

`GameInit` @ `0x0016c644` (274 lines decompiled). The binary runs 23
sequential init steps; the port (src/game/GameInit.cpp) implements 12
of them and leaves 11 as `// TODO`. This doc gives one spec block per
TODO so the implementer can add a stub call site plus a method stub on
the relevant class.

Conventions used below:

- "GOT base" = `0x001EC130` (`DAT_0016c9d0=0x0007FADC` + PC `0x0016c654`).
- `g_TaskState` = `GOT + DAT_0016c9d4 (0x000452D4)` = the GameTaskState
  global; binary references it as `tmp` in the decompile and uses
  offsets like `+0x1c` (`mainScreen` slot), `+0xbc` (`SliceFx` model
  slot), `+0xc8` (`SliceEffect` pool slot), `+0x112`, `+0x114`.
- `g_GameData` = `GOT + DAT_0016c9d8` = the Game singleton; offsets
  `+0x3c` (HUD), `+0x44` (`m_bSoundOn`), `+0x160` (mainScreen),
  `+0x168` (tutorial), `+0x178` (coin), `+0x180` (time).
- All `blx 0x0010xxxx` calls in the disassembly are PLT thunks that
  re-dispatch through `PTR_*` GOT entries; the spec lists the thunk
  address (what GameInit actually calls) and, where useful, the real
  function body in this image.

## step 8: MeshManager loads (slice FX models)

```
### step 8: MeshManager loads
- Binary block: 0x0016c97c .. 0x0016c9a8 (MeshManager::Load x2 + SmartPtr assigns)
- Function(s) called:
  - Mortar::MeshManager::GetInstance() @ 0x00102678 (PLT thunk)
  - Mortar::MeshManager::Load(retval, this, &AsciiString) @ 0x000fe490 (PLT thunk -> PTR_Load_001efb80)
  - Mortar::AsciiString::AsciiString(this, const char*) @ 0x00107730
  - Mortar::SmartPtr<Mortar::Model>::operator= @ 0x001062c0
  - Mortar::SmartPtr<Mortar::Model>::~SmartPtr (cleanup)
  - Mortar::AsciiString::~AsciiString (cleanup)
- Strings: DAT_0016c9f8 -> "models/fruit/slice_fx.mmd" (0x001BC93F)
           DAT_0016cca4 -> "models/fruit/slice_fx_crit.mmd" (0x001BC959)
- Where the result goes:
  - first  Load -> g_TaskState +0xbc  (SmartPtr<Model>, "slice fx" mesh)
  - second Load -> g_TaskState +0xc0  (SmartPtr<Model>, "slice fx crit" mesh)
- Side effects: none beyond loading the two .mmd meshes through
  MeshManager (uses asset cache); no dependency on prior step 7.
- Stub-readiness: `MeshManager::Load(const char*) -> SmartPtr<Model>`
  EXISTS in port (`src/engine/asset/MeshManager.h:23`). The two slot
  fields don't yet exist on the port's `GameTaskState`; add
  `SmartPtr<Mortar::Model> sliceFxMesh;` and `sliceFxCritMesh;` to
  `src/game/GameTaskState.h` (or a new SliceEffect-owned slot) and
  call `MeshManager::Load("models/fruit/slice_fx.mmd")` /
  `..._crit.mmd` here. NEEDS new GameTaskState fields, no new method.
```

## step 9: SliceEffect list/pool init

```
### step 9: SliceEffect list/pool init
- Binary block: 0x0016c9a8 .. 0x0016ca90 (List<SliceEffect> ctor + clear,
  MemoryPool ctor + Create(100), state flag bookkeeping at end of block)
- Function(s) called:
  - operator new(0x14)                       (List<SliceEffect>)
  - List<SliceEffect>::List(this) @ 0x000f6510 (PLT thunk)
  - List<SliceEffect>::clear(this) @ 0x000f3ad4 (PLT thunk)
  - operator new(0x14)                       (MemoryPool<List<SliceEffect>::Node>)
  - MemoryPool<List<SliceEffect>::Node>::Create(this, 100) @ 0x000ff048 (PLT thunk)
  - direct field write *(int*)pool = GOT + DAT_0016ccac (0x0018xxxx vtable
    constant for the pool's "owner-list" backref)
- Where the result goes:
  - List<SliceEffect>* -> g_TaskState +0x64
  - MemoryPool<...>*    -> g_TaskState +0xc8
- Side effects: pool capacity = 100 nodes (matches port's existing
  SliceEffect convention). Block depends on step 8 only insofar as
  it follows MeshManager::Load; the slice-fx meshes from step 8 are
  what individual SliceEffect entries render.
- Stub-readiness: SliceEffect EXISTS in port (`src/hud/SliceEffect.h`)
  but is currently a free function (`FN::SliceEffect_Draw`), not a
  pool-backed entity list. NEEDS new methods:
    - `static void SliceEffect::CreateList(int capacity = 100);`
      (allocates port-side pool/list, replacing the C-style array
      currently in src/hud/SliceEffect.cpp)
    - per port convention, also export ownership through
      `GameTaskState` so step 9 owns the alloc and step at GameExit
      releases it.
  Natural home: `src/hud/SliceEffect.{h,cpp}`.
```

## step 10: GameTaskState flag init at +0x111/+0x112/+0x114/+0xc

```
### step 10: GameTaskState flag init
- Binary block: 0x0016ca8e .. 0x0016caa8 (4 stores immediately after
  the SliceEffect pool is wired into g_TaskState +0xc8)
- Function(s) called: NONE — pure field stores.
- Stores (with the decompile mapping):
    *(uint32_t*)(g_TaskState + 0x114) = *(uint32_t*)(g_GameData + 0x54)
        // copy of FruitNinjaApp / Game-side seed/state pointer
    *(uint8_t)(g_TaskState + 0x112) = 1   // "GameInit-complete" guard
                                          // — this is the same byte
                                          // step 1 reads at function
                                          // entry to skip re-init.
    *(uint8_t)(g_TaskState + 0x111) = 0
    *(uint8_t)(g_TaskState + 0x0c)  = 0   // "first frame" flag
- Where the result goes: GameTaskState struct fields (currently absent
  on the port; see GameTaskState.h which only models 4 fields out of
  the binary's 19).
- Side effects: +0x112 is the single most important flag — it gates
  GameInit at line 1 and is checked again by GameUpdate's "first
  frame after init" branch. Without this, re-entering State 2 would
  re-run the entire 274-line init.
- Stub-readiness: NEEDS new GameTaskState fields:
    bool initComplete;        // +0x112  (renames port's existing
                              // `initialized` to bind to the right
                              // offset, OR add as a parallel field)
    bool field_0x111;         // +0x111  (semantics TBD — RE gap)
    bool firstFrame;          // +0x0c   (suspected; cleared here,
                              // possibly read at game start; RE gap)
    void* pAppState_x54;      // +0x114  (copy of g_GameData+0x54;
                              // semantics TBD — RE gap)
  Natural home: `src/game/GameTaskState.h` (extend the struct).
  RE-gap: confirm what +0x114 (g_GameData+0x54) and +0x111 carry by
  searching xrefs to those offsets.
```

## step 12: PauseScreen allocation + ctor

```
### step 12: PauseScreen allocation
- Binary block: 0x0016cad8 .. 0x0016caf8 (operator new(0xd8) +
  PauseScreen::PauseScreen + virtual Init via vtable[2])
- Function(s) called:
  - operator new(0xd8)
  - PauseScreen::PauseScreen(this) @ 0x00101778 (PLT thunk)
  - vtable[2] (Init/LoadContent) — called via
      pcVar5 = ((pauseScreen->super).super.vtable)->Init;
      (*pcVar5)(pauseScreen);
- Object size: 0xd8 bytes
- Where the result goes: g_TaskState +0x04 (note: NOT in g_GameData).
  Stored as `*(PauseScreen**)(g_TaskState + 4) = pauseScreen`.
- Side effects: allocated AFTER MainScreen (step 11) and BEFORE
  TutorialControl (step 13). The HUD AddControl batch in step 14
  registers MainScreen, PauseScreen, TutorialControl in that order.
- Stub-readiness: NEEDS new class. Create
  `src/screens/PauseScreen.{h,cpp}` modeled after MainScreen
  (`HUDControl` subclass, `Init()` / `Update()` / `Draw(layer)`
  virtuals, layer mask 0x40 likely — confirm from PauseScreen
  decompile in a follow-up RE pass). Also add
  `class PauseScreen* pPauseScreen;` slot to `GameTaskState` at +0x04
  (or just hold ownership in GameInit and register via HUD).
  RE-gap: full PauseScreen struct + LoadContent body not yet REd.
```

## step 13: TutorialControl allocation

```
### step 13: TutorialControl allocation
- Binary block: 0x0016caf8 .. 0x0016cb1e (operator new(0xa0) +
  TutorialControl::TutorialControl + virtual Init via vtable[2])
- Function(s) called:
  - operator new(0xa0)
  - TutorialControl::TutorialControl(this) @ 0x000f52e0 (PLT thunk)
  - vtable[2] (Init / LoadContent)
- Object size: 0xa0 bytes
- Where the result goes:
  - tutorialCtrl ptr -> g_GameData +0x168 (`Game::pTutorialCtrl` —
    same field the port already uses)
  - flag *(uint8_t)(g_GameData + 0x05) = 1  (port: Game::field_0x05)
  - float *(float)(g_GameData + 0x0c) = -1.0f (0xbf800000; semantics
    "tutorial timer disabled / not yet running")
- Side effects: the binary re-allocates and overwrites Game+0x168
  unconditionally. The port currently allocates TutorialControl in
  GameInitialise (the global one-time init); here in GameInit per
  session it would leak the previous ptr. Two viable port choices:
    A. Match binary: free the previous TutorialControl (if any) and
       re-alloc. Cleanest from a fidelity standpoint.
    B. Port-specific: keep the GameInitialise-side allocation and
       JUST run the equivalent of `LoadContent` + the two field
       writes here (game->field_0x05 = 1, game->field_0x0c = -1.0f).
       Document with `// DIFFERS:` comment.
  Recommendation: option A — fidelity first per CLAUDE.md.
- Stub-readiness: TutorialControl class EXISTS in port
  (`src/hud/TutorialControl.{h,cpp}`). NEEDS:
    - confirm `LoadContent()` exists (vtable[2]); if not, add stub.
    - add the two Game fields if missing (`field_0x05`, `field_0x0c`).
```

## step 15: Entity::HeapCreate

```
### step 15: Entity::HeapCreate
- Binary block: 0x0016cb48 .. 0x0016cb4e (single call, no operands
  beyond the size constant)
- Function(s) called:
  - Mortar::Entity::HeapCreate(0x20000) @ 0x000fd500 (PLT thunk
    -> PTR_HeapCreate_001ef650; real body 0x0019d708, 0x40 bytes)
- Argument: 0x20000 (131072 bytes = 128 KB) — total Entity heap.
- Where the result goes: process-global Entity arena (no field
  store; the body 0x0019d708 stores the heap into Mortar::Entity's
  static slot).
- Side effects: MUST run BEFORE step 16 ActorManager::Initialise,
  which carves slots out of this heap. Counterpart cleanup is
  Mortar::Entity::HeapDestroy in GameExit (also currently TODO).
- Stub-readiness: NEEDS new method
  `static void Entity::HeapCreate(size_t bytes);` on
  `src/entities/Entity.h`. Port may stub it as a no-op since C++
  `new` already heap-allocates per-Entity; the binary ran a custom
  bump allocator out of LinkedHeap to avoid Bada's heap. Document
  with `// DIFFERS: original = LinkedHeap arena 0x20000, port uses
  std new (no fixed cap)`. Also add `Entity::HeapDestroy()` for the
  GameExit counterpart.
  Natural home: `src/entities/Entity.{h,cpp}`.
```

## step 16: ActorManager::Initialise + RegisterFactory + RegisterHashConverter

```
### step 16: ActorManager full init
- Binary block: 0x0016cb50 .. 0x0016cc06 (three sub-blocks chained
  through GetInstance + Delegate construction):
    16a. Initialise(5, 0x2000)     0x0016cb50..0x0016cb5e
    16b. RegisterFactory(...)      0x0016cb5e..0x0016cb98
    16c. RegisterHashConverter(...) 0x0016cb9e..0x0016cc04
- Function(s) called:
  - Mortar::ActorManager::GetInstance() @ 0x000f4e30 (PLT thunk)
  - Mortar::ActorManager::Initialise(this, numTypes=5, heapSize=0x2000)
    @ 0x000f7d04 (PLT thunk -> PTR_Initialise_001ed8fc)
  - Delegate1<Entity*, long>::BaseDelegate::BaseDelegate
    @ 0x00100dac (PLT thunk) — sets up the factory delegate's vtable
    field (loaded from GOT entries DAT_0016ccb4 / DAT_0016ccb8)
  - Delegate1<Entity*, long>::Delegate1 @ 0x000f9d74 (PLT thunk)
  - Mortar::ActorManager::RegisterFactory(this, delegate)
    @ 0x00107c34 (PLT thunk) — body is a one-line assign
    `m_FactoryDelegate = factory` per port header
  - Delegate2<long, ulong, bool&>::BaseDelegate::BaseDelegate
    @ 0x001008e4 (PLT thunk)
  - Delegate2<long, ulong, bool&>::Delegate2 @ 0x000fa584 (PLT thunk)
  - Mortar::ActorManager::RegisterHashConverter(this, delegate)
    @ 0x001069f8 (PLT thunk -> PTR_RegisterHashConverter_001f27f8)
  - Delegate destructors / Global::~Global cleanup (stack tidy)
- Delegate targets (resolved through GOT slots used in GameInit):
    Factory delegate:
      vtable: `*(GOT + DAT_0016ccb4)` (function pointer table)
      thisptr-equivalent base: `*(GOT + DAT_0016ccb8) + 8`
    HashConverter delegate:
      vtable: `*(GOT + DAT_0016ccbc)`
      thisptr-equivalent base: `*(GOT + DAT_0016ccc0) + 8`
- Where the result goes: ActorManager singleton state (port already
  has `ActorManager::GetInstance()` and `Initialise(int, int)`).
  Both delegates feed `m_FactoryDelegate` and `m_HashDelegate` slots.
- Side effects:
    - `numTypes=5` means the type-list array is sized for entity
      types {0=Fruit, 1=Bomb, 2=?, 3=Slash (per GameTaskInitInput
      use of Add(3,true)), 4=Splat or "fragment/slice"}. Confirm the
      enum table in a follow-up.
    - Heap size 0x2000 is per-type chunk size, NOT the same as the
      0x20000 Entity::HeapCreate above.
- Stub-readiness:
    - `ActorManager::Initialise(int, int)` EXISTS in port
      (`src/entities/ActorManager.h:88`).
    - `RegisterFactory(FactoryFn)` EXISTS (`ActorManager.h:95`,
      inline assign) — port already takes a function pointer rather
      than a Delegate1; safe to keep that shape.
    - `RegisterHashConverter` does NOT exist in the port. NEEDS new
      method on `ActorManager`:
        `void RegisterHashConverter(HashFn fn);`
      where `typedef void (*HashFn)(long /*entityType*/,
                                    unsigned long& /*outHash*/,
                                    bool& /*outOk*/);`
      and a `m_HashDelegate` member to store it.
    - Natural home: `src/entities/ActorManager.{h,cpp}`.
    - The factory + hash bodies themselves live in
      `src/entities/EntityFactory.{h,cpp}`; port should expose them
      as plain function pointers and pass them here.
    RE-gap: confirm the exact factory and hash-converter function
    addresses by reading the GOT slots
    `[0x0016ccb4 .. 0x0016ccc0]` and dereferencing once.
```

## step 18: GameTaskInitInput

```
### step 18: GameTaskInitInput
- Binary block: 0x0016cc0a .. 0x0016cc0e (single call, no args)
- Function(s) called:
  - GameTaskInitInput() @ 0x00169670 (357 lines decompiled)
- What it does (summary from 0x00169670 decompile):
    1. InputManager::LoadConfigFile(<path string>) — reads the
       per-task input bindings.
    2. Loop x16 (one slot per "input region" / on-screen action):
         - copies a Vec3 into a per-slot table at offset +0xa0 of
           a g_TaskState-adjacent struct (rotated touch zones).
         - ActorManager::Add(3, true) — adds a Slash (entityType=3)
           per region; the returned Entity pointer is stored at
           `g_TaskState + 0x24 + i*4`.
         - calls Entity vtable[2] (Init) with a Vec3 from a GOT
           entry (default position).
         - sprintf a per-slot input-event name ("touchN", "swipeN",
           "moveN") and calls
           InputManager::RegisterInputCallback(StringHash(name), cb)
           three times per slot (down/up/move handlers).
    3. After the loop, registers 7 more global input callbacks
       (key bindings, accelerometer, etc.) using the same
       Delegate1<bool, InputEvent*> pattern.
- Where the result goes:
    - per-slot Entity ptrs -> g_TaskState +0x24 ... +0x60
    - per-slot Vec3 zones  -> separate g_TaskState-adjacent struct
      (the InputZones array, +0xa0 stride 0xc, see DAT_00169a44/48).
    - input callback table -> InputManager singleton.
- Side effects: must run AFTER step 17 (WaveManager::Init) per the
  binary call order; in practice it depends on ActorManager being
  initialised (step 16) so Add(3, true) can succeed.
- Stub-readiness: NEEDS a new free function (matches the binary —
  it is NOT a method on any class):
    `void GameTaskInitInput();`
  Port natural home: `src/game/GameInit.cpp` companion file
  `src/game/GameTaskInput.{h,cpp}` (mirroring binary's own TU).
  Initial port stub can omit the 16-slot loop and only set up the
  global callbacks needed for single-touch play (the port already
  uses one global SlashEntity, not a 16-region rotated grid). Mark
  the loop body with a `// TODO: multi-touch / multi-player input
  zones` and document as RE-gap if any non-trivial pieces emerge.
```

## step 19: 30x prespawn loop (Add(0)/Add(1)/Add(4))

```
### step 19: 30x prespawn loop
- Binary block: 0x0016cc0e .. 0x0016cc4e (do/while with counter
  reaching 0x1e = 30; three Add calls per iteration)
- Function(s) called per iteration:
  - Mortar::ActorManager::GetInstance() @ 0x000f4e30 (PLT thunk)
  - Mortar::ActorManager::Add(this, entityType, activate=true)
    @ 0x00108084 (PLT thunk -> PTR_Add_001f2f7c) — three calls with
    entityType = 0, 1, 4 in order.
  - direct OR: `pEntity->flags |= 0x11`
- Loop body in pseudocode:
    for (int i = 0; i < 30; ++i) {
        Entity* e0 = ActorManager::GetInstance()->Add(0, true);
        e0->flags |= 0x11;
        Entity* e1 = ActorManager::GetInstance()->Add(1, true);
        e1->flags |= 0x11;
        Entity* e4 = ActorManager::GetInstance()->Add(4, true);
        e4->flags |= 0x11;
    }
- What 0/1/4 are (cross-referenced with FN01_ApplyStructs and
  EntityFactory in the port):
    0 = Fruit       (registered fruit type — actual species selected
                    later when WaveManager picks one and respawns)
    1 = Bomb
    4 = Splat / SplatEntity (likely the splat trail; matches
        SplatEntity::CreatePool(0x80) in step 20 — the prespawn
        gives the ActorManager 30 splat slots, the pool gives splash
        textures another 0x80 slots)
  RE-gap: confirm the type-id table by reading EntityFactory's
  switch in the binary; the port's existing `EntityFactory::Create
  (entityType)` mirrors this and is the correct cross-reference.
- flags |= 0x11 = bit 0 (alive) | bit 4 (pre-spawned / inactive
  pool slot). Pre-spawned entities sit in the ActorManager type
  list with active=true but flags set so they're skipped by
  Update/Draw until WaveManager promotes them via a "respawn this
  slot as <species>" call.
- Where the result goes: ActorManager type-lists (3 lists × 30 = 90
  entities pre-allocated). No external pointer is kept; the entities
  are reachable through ActorManager::GetTypeList(type).
- Side effects: depends on step 16 (Initialise carved the type-
  lists) and step 18 (input zones must be ready since some Add
  paths read GameTaskState slots populated by GameTaskInitInput).
- Stub-readiness:
    - `ActorManager::Add(int type, bool activate)` EXISTS in port.
    - The flag set `e->flags |= 0x11` requires the `flags` field
      on `Entity`; that field already exists (port uses bit 0 alive
      mask). NEEDS:
        - confirm bit 4 (0x10) semantic in the port; if absent, add
          `kEntityFlagPrespawned = 0x10` to Entity flag enum.
    - The loop itself is just a 30-iter `for`; no new method needed.
    - Natural home: inline in `GameInit.cpp` step 19 block. If the
      implementer prefers a helper, put it on `ActorManager` as
      `void Prespawn(int type, int count, uint32_t flagMask)` —
      this composes cleanly and is reusable, but the binary
      inlines it.
```

## step 20: SplatEntity::CreatePool(0x80)

```
### step 20: SplatEntity::CreatePool(0x80)
- Binary block: 0x0016cc52 .. 0x0016cc56 (single call)
- Function(s) called:
  - SplatEntity::CreatePool(0x80) @ 0x001042a4 (PLT thunk
    -> PTR_CreatePool_001f1adc; real body @ 0x0017ef34, 48 lines)
- Argument: `0x80` (128) = pool capacity in SplatEntity slots.
  Real body allocates `(capacity * 0xf + 1) * 8` bytes — i.e. a
  packed array of (0x78 = 120 byte) SplatEntity records plus a
  4-byte length prefix. Each splat owns 0xf "drop" sub-records.
- Where the result goes: SplatEntity static pool slot
  (`PTR_CreatePool_001f1adc` resolves to the Splat singleton's
  `s_pool` member, accessed via GOT entry DAT_0017effc relative
  to the body's PC).
- Side effects: must run AFTER step 19 prespawn (binary order).
  Drains and reallocates if a pool already exists (the body's
  prologue calls each existing entry's vtable[0] dtor before
  `operator delete[]`).
- Stub-readiness: `static void SplatEntity::CreatePool(int)`
  EXISTS in port (`src/entities/SplatEntity.h:122`). Just wire
  the call. Verify the port-side `CreatePool` accepts capacity
  in SplatEntity units (not bytes); the binary takes capacity
  units. Also verify the port allocates 15 drops per splat —
  if not, that's an RE-gap to flag (the 0xf factor in the
  byte-count formula carries the per-splat drop count).
```

## step 23: SoundManager::Initialise + SetSFXVolume

```
### step 23: SoundManager init
- Binary block: 0x0016cc64 .. 0x0016cc94 (Initialise + SetSFXVolume,
  both via SoundManager::GetInstance(); branch on Game+0x44 selects
  the volume constant)
- Function(s) called:
  - Mortar::SoundManager::GetInstance() @ 0x000fd884 (PLT thunk)
  - Mortar::SoundManager::Initialise(this, const char* basePath)
    @ 0x0010557c (PLT thunk)
  - Mortar::SoundManager::SetSFXVolume(this, float volume)
    @ 0x00105f6c (PLT thunk)
- Argument to Initialise:
  basePath = `(char*)(GOT + DAT_0016ccc4)`
           = 0x001BC978
           = "Sound/Win32Project/Win/FruitNinja"
  (Bada-side asset root for SoundManager's per-cue file lookup.
   Port should rewrite this to "assets/Sound/" or whatever path
   convention the port chose; document as `// DIFFERS:`.)
- SetSFXVolume value:
    if (Game+0x44 (`m_bSoundOn`) == 0)  volume = DAT_0016cca0 (= 0.0f)
    else                                volume = 0.5f
  i.e. binary defaults SFX to half-volume when sound is enabled,
  zero otherwise.
- Where the result goes: SoundManager singleton internal state.
- Side effects: must run AFTER step 22 BombFlash::CreatePool — by
  binary order. No data dependency, but moving it earlier breaks
  fidelity. Has no dependencies upstream beyond Game being
  constructed (always true here).
- Stub-readiness: `SoundManager::GetInstance()` EXISTS
  (`src/engine/audio/SoundManager.h:43`). NEEDS new methods:
    - `void SoundManager::Initialise(const char* basePath);`
      port-side stub may store the base path and lazily defer
      actual cue-file scanning.
    - `void SoundManager::SetSFXVolume(float v);` — store into a
      member field (`m_sfxVolume`) consumed by the per-cue mixer.
  Natural home: `src/engine/audio/SoundManager.{h,cpp}`.
  RE-gap: SoundManager::Initialise body (real address — chase
  PTR_Initialise via the disassembly of 0x0010557c). The base path
  also needs a port-side translation (Bada Sound/Win32Project/...
  is meaningless on SDL2; map to assets/sound/ or similar).
```

## Cross-cutting notes

- Step 14 (HUD::AddControl x3) in the port currently registers
  MainScreen + (TODO PauseScreen) + (TODO TutorialControl). Once
  steps 12 and 13 produce real ptrs, the existing call site in
  GameInit.cpp lines 112 -- 115 just needs the two TODO
  AddControl calls re-enabled.
- Steps 8 -- 23 in this doc are intentionally listed in binary
  order, NOT GameInit.cpp source order. The port already groups them
  in the binary order; each TODO comment in src/game/GameInit.cpp
  carries the matching `step N:` label.
- `g_TaskState +0x112` (step 10's primary flag) is also the early-
  return guard at `GameInit` entry (`if (*(char*)(tmp + 0x112) ==
  '\0')`) — without setting it at the end of step 10, every
  subsequent invocation of GameInit would re-run the entire 274-
  line init, leaking HUD, ScoreControl, MainScreen, etc. The port
  currently has no equivalent guard; add it as part of step 10.
- All PLT thunks listed above are resolved at load time via the
  GOT and re-dispatch to the real bodies in the 0x0017xxxx ..
  0x001fxxxx range. Whether to inline-port the PLT call or skip
  straight to the real body is a port style choice; either is
  fidelity-correct.

## RE gaps flagged for follow-up

- `g_TaskState +0x111` semantics (step 10) — only known use is the
  reset to 0 here. Search xrefs to that offset.
- `g_TaskState +0x114` (step 10) — copies `g_GameData+0x54`.
  `g_GameData+0x54` itself is unmapped on the port; identify it.
- PauseScreen full struct + LoadContent (step 12).
- Entity heap layout — confirm whether 0x20000 is the sole arena
  or per-type (step 15 vs 16's 0x2000).
- ActorManager factory + hash-converter function bodies (step 16)
  via GOT slots `[0x0016ccb4 ... 0x0016ccc0]`.
- SplatEntity 15-drops-per-slot semantics (step 20) — the body's
  `(cap * 0xf + 1) * 8` formula shows the structure but the per-
  drop layout is not RE'd.
- SoundManager::Initialise body and the per-cue path scheme
  (step 23) — port needs a path-translation policy.
