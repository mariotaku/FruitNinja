# `ActorManager::Add` — Phase A3 deep-dive

Focused RE pass on `Mortar::ActorManager::Add(int entityType, bool ignored)`,
the entry point used by `WaveManager::SpawnFruit/SpawnBomb` and the
`GameInit` pre-spawn loop.

This file complements [`actor-manager.md`](actor-manager.md) (the broad
overview). Where the two disagree, treat **this file** as authoritative
for `Add` and the second-`bool` parameter — it is checked against the
disassembly at `0x0017068c..0x00170724`.

Analysed: 2026-04-30.

---

## 1. Address resolution chain

```
PLT thunk     0x00108084   (LDR-via-GOT trampoline)
  -> GOT slot 0x001f2f7c   = 0x0017068c   ; little-endian: 8c 06 17 00
        body  0x0017068c..0x00170724  (154 bytes, Thumb-2)
```

`read_memory(0x001f2f7c, 8)` returns `8c 06 17 00 68 70 19 00`. The
second pointer `0x00197068` is the next GOT entry (`Add(Entity*, long)`
overload at `0x00170654` after PLT/GOT bias) and is unrelated to this
spec.

`get_xrefs_to(0x0017068c)` confirms the only direct reference is the
GOT slot (`0x001f2f7c [DATA]`); the PLT stub at `0x00108084` is the
single computed-call site every caller actually targets.

---

## 2. `Add(int, bool)` full pseudocode

Reconstructed from disassembly. Stack slot `[sp,#0x4]` holds the second
argument on entry but is **immediately overwritten** at `0x001706fc` /
`0x00170702` (and read back at `0x00170722`) — proving the `bool` param
is dead. The slot is reused as a scratch local for the result pointer.

```c
Entity*
Mortar::ActorManager::Add(ActorManager* this, int entityType, bool /*ignored*/)
{
    // r6 = m_FreeCount; r5 = entityType; r4 = this
    int freeIdx = this->m_FreeCount - 1;
    Entity** poolEnd = &this->m_FreePool[this->m_FreeCount];   // pre-computed scan ptr
    Entity* result = nullptr;

    // ---- Recycle path: reverse-scan free pool for matching type ----
    while (freeIdx >= 0) {
        Entity* candidate = *(--poolEnd);                       // ldr r3,[r2,#-0x4]!
        if (candidate->type /* +0x35 */ == (uint8_t)entityType) {

            // Push BEFORE shrinking the free pool. Argument to push_back is a
            // pointer-to-slot on stack; binary stores `candidate` at [sp,#0x4]
            // here (str.w r3,[r1,#-0x4]!) then loads the type-list pointer
            // from this->m_pTypeLists (offset 0x1010).
            std::list<Entity*>* lists = this->m_pTypeLists;
            std::list<Entity*>::push_back(&lists[entityType], &candidate);

            // Shrink + compact: shift slots [freeIdx+1 .. m_FreeCount) one
            // step left so the pool stays contiguous. The binary uses a
            // pre-incremented r4 walker; algorithmically equivalent to:
            int newCount = this->m_FreeCount - 1;
            this->m_FreeCount = newCount;
            for (int j = freeIdx; j < newCount; j++)
                this->m_FreePool[j] = this->m_FreePool[j + 1];

            // Mortar::Entity::Activate (PLT 0x000f8544 -> 0x00170b18):
            //     this->flags &= 0xfe;          // clears ENT_INACTIVE
            Mortar::Entity::Activate(candidate);

            return candidate;                                    // [sp,#4] holds it
        }
        freeIdx--;
    }

    // ---- Factory path: pool empty or no matching type ----
    // PLT 0x000fd734 -> Delegate1::operator() at 0x001efxxx.
    //   GOT-resolved field at this+0x1024 + 0x24 = function pointer slot.
    // Binary: blx 0x000fd734 with r0 = &this->field_0x1024+0x24, r1 = entityType.
    Entity* fresh = this->m_FactoryDelegate(entityType);         // Delegate1<Entity*,long>
    if (fresh == nullptr) return nullptr;

    std::list<Entity*>* lists = this->m_pTypeLists;
    std::list<Entity*>::push_back(&lists[entityType], &fresh);

    fresh->type        = (uint8_t)entityType;                    // strb r5,[r3,#0x35]
    fresh->field_0x34  = 0;                                      // strb r2,[r3,#0x34]
    // Note: factory path does NOT call Activate(). Brand-new entities
    // come out of their ctor with flags=0, so (flags & 0x11) == 0 is
    // already satisfied for the first Update tick.
    return fresh;
}
```

### Key disassembly anchors

| Addr | Instr | Meaning |
|------|-------|---------|
| `0x0017068e` | `ldr.w r6,[r0,#0x808]` | r6 = m_FreeCount |
| `0x0017069c` | `add.w r2,r0,r2,lsl #0x2` | r2 = &m_FreePool[m_FreeCount+2] (pre-decrement target) |
| `0x001706a2` | `ldr.w r3,[r2,#-0x4]!` | walk pool descending |
| `0x001706a6` | `ldrb.w r1,[r3,#0x35]` | load `entity->type` |
| `0x001706ae` | `movw r2,#0x1010` | offsetof(m_pTypeLists) |
| `0x001706be` | `blx 0x000f3c30` | push_back(typeList, &candidate) |
| `0x001706e8` | `blx 0x000f8544` | Mortar::Entity::Activate(candidate) |
| `0x001706f4` | `add.w r0,r4,#0x1000; adds r0,#0x24` | r0 = &m_FactoryDelegate (this+0x1024) |
| `0x001706fe` | `blx 0x000fd734` | Delegate1::operator()(factory, entityType) |
| `0x0017071a` | `strb.w r5,[r3,#0x35]` | fresh->type = entityType |
| `0x0017071e` | `strb.w r2,[r3,#0x34]` | fresh->field_0x34 = 0 |

---

## 3. Pool struct layout (verified against `get_struct_layout ActorManager`)

```
Offset   Size   Field
------   ----   -----
0x000      4    LinkedHeap*       m_pHeap
0x004      4    int               m_HeapSize
0x008   2048    Entity*[512]      m_FreePool          // flat array, used as a stack
0x808      4    int               m_FreeCount         // 0..512, top-of-stack index
0x80c    512    (ColAABB scratch / deactivation queue used by Update — not read by Add)
0x100c     4    int               field_0x100c       (ctor zeroes this; otherwise unused)
0x1010     4    std::list<Entity*>* m_pTypeLists     // heap-allocated list array, size m_NumTypes
0x1014     8    std::list<MessageListener*>  (ignored by Add)
0x101c     4    int               m_NumTypes
0x1020     1    bool              m_DebugDraw
0x1024    36    Delegate1<Entity*,long>      m_FactoryDelegate
0x1048    36    Delegate2<long,ulong,bool&>  m_HashDelegate
                                                 (total: 4204 bytes)
```

`Add` only touches `m_FreePool / m_FreeCount / m_pTypeLists / m_FactoryDelegate`.
Notably: it does **not** check `m_pHeap` for null — that guard lives in
`Update`/`Draw`. Calling `Add` before `Initialise` would crash on the
null `m_pTypeLists` deref at `0x001706b4`/`0x0017070c`.

### Free-slot lookup performance

Linear reverse-scan of `m_FreePool[0..m_FreeCount-1]` matching on
`entity->type` (byte at +0x35). With `m_FreeCount` capped at 512 and a
realistic steady-state of ~30 entities (the GameInit pre-spawn count),
this is ~30 byte-compares per `Add`. No bucket-per-type structure;
recycling is genuinely O(N) over the whole pool.

The compaction loop after a hit is also O(N) (shift-left). Bounded by
the same N. Fine for FN's 60 fps × ~1 spawn/frame budget.

---

## 4. `Initialise(numTypes, heapSize)` parameter semantics

From `0x0017046c`:

```c
void ActorManager::Initialise(int numTypes, int heapSize) {
    m_pHeap     = new LinkedHeap(heapSize);
    m_HeapSize  = heapSize;
    m_NumTypes  = numTypes;
    void* lists = m_pHeap->Allocate((numTypes + 1) * 8, ...);
    if (lists) {
        for (int i = numTypes - 1; i >= 0; i--)
            std::list<Entity*>::list();             // construct in place
    }
    m_pTypeLists = lists;
    Entity::HeapExist();   // bumps a global ref-count (see entity-base.md)
}
```

GameInit (0x0016cc04) calls `Initialise(5, 0x2000)`:

| Param | Value | Meaning |
|-------|-------|---------|
| `numTypes` | `5` | number of entity-type buckets (Fruit=0, Bomb=1, type 2 unused, type 3 unused, BombBlast=4). One `std::list<Entity*>` is constructed per bucket. |
| `heapSize` | `0x2000` (8192 bytes) | argument forwarded to `LinkedHeap` ctor — the size of the heap chunk that backs `m_pTypeLists` and any future `LinkedHeap::Allocate` calls inside ActorManager. **Not** related to the entity-payload heap; entity bodies come from `Entity::HeapCreate(0x20000)` invoked separately at `0x0016cbb0`. See [entity-base.md](../entities/entity-base.md). |

Allocates `(numTypes + 1) * 8 = 48` bytes from the LinkedHeap — five
8-byte `std::list<Entity*>` headers plus an 8-byte sentinel. The +1 is
defensive against off-by-one indexing; `Add` only reaches `[0..numTypes-1]`.

`numTypes=5` does **not** mean the free pool is `5 * 0x2000 = 40960`
slots. The free pool is a fixed-size 512-pointer array baked into the
class layout (`+0x008..+0x808`), independent of `Initialise` arguments.
Heap size and pool capacity are unrelated dials.

---

## 5. Pre-spawn vs runtime-spawn behavior

The `GameInit` pre-spawn loop (`0x0016cc0e..0x0016cc4e`):

```c
for (int i = 0; i < 30; i++) {
    pE = ActorManager::Add(am, 0, true); pE->flags |= 0x11;   // Fruit
    pE = ActorManager::Add(am, 1, true); pE->flags |= 0x11;   // Bomb
    pE = ActorManager::Add(am, 4, true); pE->flags |= 0x11;   // BombBlast
}
```

Why the explicit `flags |= 0x11` after each call? `Add`'s factory path
hands back a freshly-constructed entity with `flags = 0`. The pre-spawn
goal is to populate the free pool, not the active list — but `Add`
unconditionally pushes onto `m_pTypeLists[type]`. The pattern is:

1. `Add(type, true)` -> factory mints entity, push to `m_pTypeLists[type]`.
2. Caller sets `flags |= 0x11` -> bits `0x10` (`ENT_KILLED`) and
   `0x01` (`ENT_INACTIVE`).
3. Next `ActorManager::Update` tick scans all type lists, finds these
   entities flagged `0x10`, calls `Deactivate(e)` which moves them
   from `m_pTypeLists[type]` to `m_FreePool[]`.

After 30 iterations × 3 types and one Update, the free pool holds 90
ready-to-recycle entities (`m_FreeCount = 90`). Subsequent live spawns
during gameplay go through the recycle path of `Add` and never hit
`operator new` again.

`Add` itself does NOT set `flags |= 0x11`. The caller does, because the
flag combination is the seed value the deactivation pass needs to
recycle the entity — and that's only desired for pre-spawn, not for
the in-game `WaveManager::SpawnFruit/SpawnBomb` callers which want the
entity active immediately.

---

## 6. Second `bool` parameter — confirmed dead

| Caller | bool value passed |
|--------|-------------------|
| `GameInit` pre-spawn (×3 per loop iter, ×30 iters) | `true` |
| `WaveManager::SpawnFruit` @ 0x001225a0 | `true` |
| `WaveManager::SpawnBomb` @ 0x00121fa8 (3 call sites) | `true` |
| `Coin::Spawn` (and other minor spawners) | `true` (no exception found in xref scan) |

The disassembly never reads `[sp,#0x4]` between function entry and the
first `str ... ,[sp,#0x4]` at `0x001706fc` (which overwrites it with
the factory's return value). The argument is consumed nowhere.

Probable intent: a "force factory" / "skip recycle" flag that was
designed but never wired up; the engine pattern (Halfbrick Mortar) ships
with it as a vestige. Port-side: keep the parameter in the signature for
1:1 call-site fidelity, ignore the value.

---

## 7. Multiplayer / playerIdx routing

`Add` itself contains zero player-index logic. Player routing is done
*after* `Add` returns:

- `WaveManager::SpawnBomb` / `SpawnFruit` call `Bomb::SetForPlayer(b,1)`
  / `Bomb::SetForPlayer(b,2)` after each `Add`, which writes a
  per-player tracking field on the entity (Bomb-side).
- `Fruit::SetForPlayer(newFruit, 0)` is similarly invoked from
  `SpawnFruit`'s online-multiplayer branch.

No per-Entity `playerIdx` field is read or written by `Add`. The free
pool is not partitioned by player — recycled entities can cross player
boundaries because `SetForPlayer` overwrites whatever was left from the
previous tenancy.

---

## 8. Cross-reference with Phase A1 (heap) and A2 (factory)

### A1 heap interaction

`Add` reads two heap-related fields:

- `m_pHeap` (`+0x000`) — never null-checked here. Initialise must
  have run.
- `m_pTypeLists` (`+0x1010`) — points into the LinkedHeap chunk
  allocated by `Initialise`. `Add`'s `push_back` writes into this
  region.

Entity bodies themselves come from a **separate** heap created by
`Entity::HeapCreate(0x20000)` at GameInit step 15 (see
[`entity-base.md`](../entities/entity-base.md)). The factory delegate
(see A2) calls `operator new` which is overridden globally on Entity
to draw from that heap.

ActorManager's `m_pHeap` only sizes the type-list array — it is **not**
the entity payload heap. This is the most common confusion when
porting: the 8 KB heap behind `Initialise` is irrelevant to entity
storage capacity.

### A2 factory interaction

`Add`'s factory call site at `0x001706fe` reads
`this->field_0x1024 + 0x24` as the function-pointer slot inside the
36-byte `Delegate1<Entity*,long>` object embedded at `+0x1024`. The
factory body (see A2) is a hard-coded `switch (entityType)` over
`{0,1,4}` returning `new Fruit() / new Bomb() / new BombBlast()`.

Field used by `Add` after factory returns:

| Field | Offset on Entity | Set by Add | Set by factory |
|-------|------------------|------------|----------------|
| `vtable` | `+0x00` | no | yes (ctor) |
| `flags` | `+0x0c` (byte 3) | no | yes (=0) |
| `field_0x34` | `+0x34` | yes (=0) | yes (=0 in ctor, redundant) |
| `type` | `+0x35` (byte) | yes (=entityType) | no |

Recycle path skips `field_0x34` and `type` writes (both are already
correct on a recycled entity that came back through `Deactivate`).

---

## 9. Port-side implementation plan for `src/entities/ActorManager.{h,cpp}`

**Status: complete.** The current port (commit window ending
`a4c0591`) already implements the binary-faithful version of `Add`.
Verified line-by-line against the disassembly:

| Binary behavior | Port line in `ActorManager.cpp` |
|-----------------|---------------------------------|
| Reverse-scan free pool | `for (int i = m_FreeCount - 1; i >= 0; i--)` (L110) |
| Match on `type` byte at +0x35 | `candidate->entityType == entityType` (L112) |
| `push_back` BEFORE `m_FreeCount--` | L115 vs L116 (correct order) |
| Compaction loop | L117-119 |
| `Entity::Activate` clears flags & 0xfe | `candidate->flags &= ~(ENT_INACTIVE | ENT_KILLED)` (L125) |
| Factory path push, set type+0x34 | L137-139 |
| Factory path skips Activate | (no call) |
| `bool` param ignored | named `/*unused*/`, default `true` |

### Notable port divergences (intentional)

1. **`ENT_KILLED` cleared on recycle** (`& ~ENT_KILLED` in addition to
   `~ENT_INACTIVE`). Binary's `Entity::Activate` only clears bit 0
   (`flags &= 0xfe`), leaving bit 4 set. **Recommended action: revisit.**
   Pre-spawned entities reach the free pool with both bits set; if the
   binary really preserves bit 4 on Activate, recycled entities would
   immediately re-enter the kill list on the next Update tick and ping
   between active/free until something else clears the flag. That can't
   be right at runtime, so either (a) `Deactivate` clears the kill bit
   when it parks the entity (not visible in the decompile but worth
   ASM-checking at `0x00170184`), or (b) a Game-side helper clears it
   on the active path. Port's `~ENT_KILLED` may be papering over the
   missing clear elsewhere. **RE-gap noted, port-side bug unverified —
   no implementer action yet.**

2. **`m_pHeap` sentinel (set to `this`)**. Binary uses real `LinkedHeap*`.
   Port short-circuits the LinkedHeap entirely; `m_pHeap = this` only
   exists so `Update`/`Draw`'s null-check evaluates true. `Add` doesn't
   read it. Documented in `ActorManager.h`.

3. **`Renderer&` arg on `Draw`.** Unrelated to `Add`; mentioned only
   for completeness.

### No new code changes required for A3

`Add` is already binary-faithful. The two follow-ups for the
implementer are:

- ASM-inspector pass on `Deactivate` (`0x00170184`) to confirm whether
  `entity->flags &= ~ENT_KILLED` happens there in the binary. If yes,
  the port's `~ENT_KILLED` mask in `Add`'s recycle path is redundant
  (harmless) and the binary's `Activate` with only `&=0xfe` is correct.
  If no, document where the kill-bit clear actually happens.

- Confirm that `ENT_NO_DESTRUCT` (0x20) on Clear()/Remove() handling
  matches the binary's `flags & 0x20` test — this is in scope for
  `actor-manager.md` but not for `Add` per se.

---

## 10. Function addresses (one-stop)

| Symbol | Address | Bytes | Verified |
|--------|---------|-------|----------|
| `ActorManager::Add(int,bool)` | 0x0017068c | 154 | yes (this doc) |
| `ActorManager::Add(Entity*,long)` | 0x00170654 | 56 | yes |
| `ActorManager::Initialise` | 0x0017046c | 110 | yes |
| `ActorManager::Deactivate` | 0x00170184 | ~60 | yes |
| `ActorManager::ctor` | 0x00170578 | 94 | yes |
| `Entity::Activate` (PLT) | 0x000f8544 | thunk | yes |
| `Entity::Activate` (impl) | 0x00170b18 | 8 | yes (`flags &= 0xfe`) |
| `Delegate1::operator()` (PLT) | 0x000fd734 | thunk | yes |
| `std::list<Entity*>::push_back` (PLT) | 0x000f3c30 | thunk | yes |
| PLT thunk for `Add` | 0x00108084 | thunk | yes |
| GOT slot for `Add` | 0x001f2f7c | 4 | yes (LE: `8c 06 17 00`) |

---

## See also

- [`actor-manager.md`](actor-manager.md) — broad ActorManager coverage
  (Update, Draw, Deactivate, Remove, GetNumEntities*, lifecycle).
- [`../entities/entity-base.md`](../entities/entity-base.md) — Entity
  flags table, vtable layout, `HeapCreate(0x20000)`.
- [`../systems/gameinit-todos.md`](../systems/gameinit-todos.md) —
  step 16 (RegisterFactory + RegisterHashConverter), step 19
  (pre-spawn loop).
