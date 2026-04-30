# Entity Heap (Mortar::LinkedHeap)

Phase A1 RE for the entity arena. Companion to
[`actor-manager.md`](actor-manager.md). All addresses are little-endian
ARM32 (Bada FruitNinja.exe).

## Verdict

The "Entity heap" is a single process-global instance of
`Mortar::LinkedHeap` — a doubly-linked-block bump allocator with a
free-list for reuse. It is **not** a fixed-size pool, **not**
per-Entity-type, and **not** an array of pre-built slots. Per-block
metadata + free-list links live inline in the same backing buffer as
the entity bytes (16-byte header per block).

Two LinkedHeap instances coexist:

| Heap | Size | Owner | Used for |
|------|------|-------|----------|
| Entity heap | 0x20000 (128 KB) | global, set by `Entity::HeapCreate` | every `Entity*` returned by the factory (Fruit/Bomb/Coin/SlashEntity/BombBlast). Also reads through `Entity::operator new`/`operator delete`. |
| ActorManager bookkeeping heap | 0x2000 (8 KB) | `ActorManager::m_pHeap` | the `(numTypes+1) * 8`-byte `std::list<Entity*>` array. (One block; never resized.) |

`ActorManager::Add` recycles entities through `m_FreePool` (a 512-slot
`Entity*` array on the `ActorManager` itself, not on the heap), so in
the steady state the LinkedHeap is hit only on first spawn of each
type. Per-type sub-pools do **not** exist.

## LinkedHeap struct (0x24 bytes)

Recovered from `LinkedHeap::LinkedHeap` (0x00194988) and
`LinkedHeap::AllocateMemory` (0x001947f0):

| Off | Type | Name (proposed) | Notes |
|-----|------|-----------------|-------|
| +0x00 | `Block*` | `freeListHead` | head of free-list (NULL until first Release). Field 0 walked by `FreeListSearch`. |
| +0x04 | `u32` | (unused / reserved) | zeroed in ctor. |
| +0x08 | `Block*` | `firstBlock` | head of allocated-block list. Used by `Release` for left-merge guard. |
| +0x0c | `Block*` | `lastBlock` | tail of allocated-block list. Used to append on bump path. |
| +0x10 | `u8*` | `bufferBase` | `operator new[]( (size+3)&~3 )`. Freed in dtor. |
| +0x14 | `u32` | `bufferCapacityAligned` | `(size + 3) & ~3` — aligned cap. |
| +0x18 | `u8*` | `bumpCursor` | next free byte; advances by header+payload on each new alloc. |
| +0x1c | `u8*` | `bufferEnd` | `bufferBase + bufferCapacityAligned`. |
| +0x20 | `u32` | `guardSize` | per-block trailing-guard size (0 in this build — no `DAT_001948f0` guard cookie written when zero). |

Per-block header (16 bytes, lives at `bumpCursor` before the payload):

| Off in block | Type | Name | Notes |
|--------------|------|------|-------|
| +0x00 | `Block*` | `next` | next block in alloc-list (or free-list when free). |
| +0x04 | `Block*` | `prev` | prev block in alloc-list. NULL for first. |
| +0x08 | `const char*` | `tag` | optional `param_2` tag string from `Allocate`. Used for diagnostics. |
| +0x0c | `u24 + u8` | `sizeAndFlags` | low 24 bits = total block size including header; high byte = state byte. State 1 = free, 4 = locked-allocated, 2 = "Allocate" path, anything else = "AllocateFixed/Other". |

The payload pointer returned to the caller is `&header + 0x10` (i.e.
header sits immediately before the user data). `LinkedHeap::Release`
recovers the header from the user pointer via
`payload + (-0x10 - (guardSize >> 1))` (with guardSize=0 that's just
`payload - 0x10`).

## HeapCreate (0x0019d708, 0x40 bytes)

```cpp
// Mortar::Entity::HeapCreate(unsigned long bytes)
static LinkedHeap*  s_pEntityHeap;   // GOT slot, see "Globals" below
static unsigned int s_EntityHeapSize;

void Entity::HeapCreate(unsigned long bytes) {
    LinkedHeap* h = static_cast<LinkedHeap*>( ::operator new(0x24) );
    new (h) LinkedHeap(bytes);     // calls 0x00194988 — allocates aligned buffer
    s_pEntityHeap     = h;
    s_EntityHeapSize  = bytes;
    // Note: HeapExist() flag is implicit — `s_pEntityHeap != NULL` is the test.
    //   See ActorManager::Initialise, which calls Entity::HeapExist() right
    //   after its own LinkedHeap is up; that sets a separate "actor-mgr ready"
    //   global, NOT this one.
}
```

The two globals (resolved by GOT walk on the literal pool at
0x0019d740/44/48):

| Symbol | GOT slot | Resolves via |
|--------|----------|--------------|
| `s_pEntityHeap`     (`LinkedHeap**`) | 0x001eb130 + 0x7924 | -> `0x001f2a54` (data ptr to global) |
| `s_EntityHeapSize`  (`u32*`)         | 0x001eb130 + 0x742c | -> `0x001f255c` (data ptr to global) |

(These are GOT entries that point at writable globals. The single-byte
"HeapExist" flag is **not** a separate global in this build — code
checks `*s_pEntityHeap != 0`, see `Entity::HeapExist` at 0x0019d658.)

## HeapDestroy (0x0019d6d0, 0x38 bytes)

```cpp
void Entity::HeapDestroy() {
    LinkedHeap** pp = &s_pEntityHeap;
    LinkedHeap*  h  = *pp;
    if (h) {
        h->~LinkedHeap();          // ReleaseAll() then delete bufferBase
        ::operator delete(h);
        *pp = nullptr;
    }
    s_EntityHeapSize = 0;          // also zero the size global
}
```

`LinkedHeap::~LinkedHeap` (0x00194918) calls `ReleaseAll()` (resets
header pointers and bumpCursor to start) and then
`::operator delete[](bufferBase)`. **It does not run any per-entity
destructors** — those must already have been drained by
`ActorManager::Clear` (called from GameExit before `HeapDestroy`).

## Allocation flow

GameInit (0x0016c644) order is:

1. `Entity::HeapCreate(0x20000)`            — global heap up.
2. `ActorManager::Initialise(5, 0x2000)`    — second LinkedHeap, type-list array.
3. `WaveManager::Init` / `GameTaskInitInput`.
4. Pre-spawn loop: 30x `(Add(0); Add(1); Add(4))` then `flags |= 0x11`.

Per-spawn through `ActorManager::Add(type, activate)` (0x0017068c):

```
ActorManager::Add(type, true)
 |
 |-- search m_FreePool[m_FreeCount-1 .. 0] for entity with same type
 |   |
 |   if found:
 |     - shift array down to remove slot
 |     - push_back into m_pTypeLists[type]      (std::list, allocator = std::new — NOT the LinkedHeap)
 |     - Entity::Activate(e)   { e->flags &= 0xfe; }   // clear ENT_INACTIVE only
 |     - return e
 |
 |-- (free pool empty for this type)
 |     - e = m_FactoryDelegate(type)             // -> CreateEntity(type) @ 0x0017421c
 |         CreateEntity calls one of:
 |           Mortar::Entity::operator new(0x118) ; Fruit::Fruit(...)
 |           Mortar::Entity::operator new(0xb0)  ; Bomb::Bomb(...)
 |           Mortar::Entity::operator new(0x94)  ; Coin::Coin(...)
 |           Mortar::Entity::operator new(0x184) ; SlashEntity::SlashEntity(...)
 |           Mortar::Entity::operator new(0x70)  ; BombBlast::BombBlast(...)
 |     - push_back into m_pTypeLists[type]
 |     - e->type = (u8) type ; e->m_RecycleFlag(field_0x34) = 0
 |     - return e
```

`Mortar::Entity::operator new(size)` body @ 0x0019d7dc:

```cpp
void* Mortar::Entity::operator new(size_t n) {
    return LinkedHeap::Allocate(s_pEntityHeap, n, /*tag*/ nullptr);
}
```

`LinkedHeap::Allocate(size, tag)` (thin wrapper @ 0x0019490c):

```cpp
void* LinkedHeap::Allocate(unsigned int size, const char* tag) {
    return AllocateMemory(size, tag, /*state=*/2);   // state=2 == "Allocate"
}
```

`LinkedHeap::AllocateMemory` (0x001947f0) — the real bump allocator.
Two paths: empty-free-list fast path (pure bump from `bumpCursor`,
appending the new block to the doubly-linked alloc list) and
free-list path (try `FreeListSearch` first; if that returns a reused
block, restamp `state`/`tag` and return; otherwise fall through to
the same bump). Headers carry `next`/`prev`/`tag`/`(size:24,state:8)`
so `Release` can walk and coalesce. Total alloc size is
`((size+3)&~3) + 0x10 + guardSize` (guardSize=0 in this build).

`Mortar::Entity::operator delete(void* p)` (0x0019d770) just forwards
to `LinkedHeap::Release(s_pEntityHeap, p)` (0x0019469c), which:

1. Recovers the header at `p - 0x10` (with `guardSize=0`).
2. Marks `header.state = 1` (free).
3. Calls `FreeListAdd(p)`.
4. **Coalesce-right** while the next block is free (collapses tail
   freelist into a single trailing region; if the just-freed block
   is the tail, it actually shrinks the bumpCursor — see the
   `lastBlock` branch).
5. **Coalesce-left** with adjacent free predecessors via the doubly
   linked list (the `else` branch).

So the heap is a fully general first-fit allocator with coalescing,
not a fixed-stride slab.

## Free-list / slot reuse semantics

There are TWO levels of free-list:

1. **`ActorManager::m_FreePool`** (Entity-level recycling). Type-aware,
   LIFO scan. This is what gameplay uses.
2. **`LinkedHeap` free-list** (raw-byte recycling). Type-agnostic,
   first-fit, coalescing. This is only hit when an Entity is actually
   destroyed (`Mortar::Entity::operator delete`) — i.e. via
   `ActorManager::Remove` / `ActorManager::Clear`, not via
   `Deactivate`.

Per-frame entity lifecycle (verified against ActorManager docs):

```
Update(dt):
  for type in 0..numTypes:
    for e in m_pTypeLists[type]:
      if (e->flags & 0x11) == 0:      ; ENT_SKIP_MASK clear
          e->flags |= 0x0c            ; ENT_UPDATING|ENT_POST_UPDATING
          e->vt->Update(dt)
          e->vt->PostUpdate(dt)
      if (e->flags & 0x10) != 0:      ; ENT_KILLED
          push e onto local deactivation queue
  for e in deactivation queue:
    Deactivate(e)                     ; -> erase from type-list, push into m_FreePool
                                        ; entity stays allocated in LinkedHeap
```

Entity bytes are returned to the LinkedHeap **only** in two paths:

- `ActorManager::Remove(e)` — ad-hoc removal that runs vt[0xC]
  (`OnDeactivate`) and vt[4] (destructor, which after the chain ends
  in `Mortar::Entity::operator delete`).
- `ActorManager::Clear()` — sweep through every type-list AND the
  free pool, destruct each, releasing back to the heap.

Steady-state gameplay never destructs entities; it just toggles them
between "in type-list" and "in m_FreePool". This is why the 128 KB
Entity heap can be small: 30+30+30 pre-spawned Fruit/Bomb/BombBlast
plus ad-hoc spawns peak well below capacity.

## Pre-spawn `flags |= 0x11` (binary @ 0x0016cc0e..0x0016cc4e)

Setting `flags |= 0x11` after each pre-spawned `Add` puts the entity
into the "fully dormant" state without un-allocating it:

| Bit | Symbol (port) | Effect |
|-----|---------------|--------|
| 0x01 | `ENT_INACTIVE` | `Update`/`Draw` gates fail (`(flags & 0x11) == 0`). Cleared by `Entity::Activate(e)` during `Add`'s recycle path. |
| 0x10 | `ENT_KILLED` | At end of `Update`, this entity is added to the deactivation queue. |

So the pre-spawn loop's intent is: **allocate 30 of each type into the
type-lists, then immediately schedule them for deactivation on the
first frame.** After frame 1's `Update`/`Deactivate` sweep, those 90
entities sit in `m_FreePool`, ready for fast `Add` recycling without
hitting the LinkedHeap. This guarantees that the first 30 Fruit/Bomb
spawns of actual gameplay are pool-recycle hits, not factory hits —
predictable startup latency.

Setting both `0x01` and `0x10` (rather than just `0x10`) keeps the
entity skipped on the same frame (in case `Add(true)` already cleared
0x01 via Activate during the recycle path — note that pre-spawn always
hits the factory path here because the pool is empty, so 0x01 was
never cleared and the OR is technically idempotent).

## Flag enum (Entity+0x0c byte)

Verified by xref-search:

| Bit | Symbol | Set by | Cleared by | Tested by |
|-----|--------|--------|------------|-----------|
| 0x01 | `ENT_INACTIVE` | pre-spawn `\|= 0x11`; never set after first `Activate` clears it | `Entity::Activate` (`flags &= 0xfe`) | `(flags & 0x11) == 0` gate in `Update`/`Draw` |
| 0x04 | `ENT_UPDATING` | `Update` (`flags \|= 0x0c`) | (overwritten next frame) | (debug only) |
| 0x08 | `ENT_POST_UPDATING` | `Update` (`flags \|= 0x0c`) | (overwritten next frame) | (debug only) |
| 0x10 | `ENT_KILLED` | gameplay code (`Bomb::CollisionResponse`, `Fruit::CollisionResponse`, BombBlast lifetime expiry, etc.) and pre-spawn | `Deactivate` flow (entity moves to free-pool, flag NOT cleared on the entity itself — the recycle path's `Entity::Activate` only touches 0x01; the next `Add` path never re-sets 0x10 on a fresh recycle) | `Update` post-tick "is-killed" check; `(flags & 0x11) == 0` gate |
| 0x20 | `ENT_NO_DESTRUCT` | (pre-spawned long-lived entities; cleared by ctor — `Entity::Entity` does `flags &= 0xdf`) | `Entity::Entity` ctor | `ActorManager::Remove` (`if ((flags & 0x20) == 0) ...`) and `ActorManager::Clear` |

Note: bit 0x10 not being explicitly cleared on the recycle path may
look like a bug, but it isn't — the reuse path in `Add` does **not**
go through any code that re-sets it, and entity-side gameplay clears
the kill bit explicitly when the entity comes alive (e.g. `Fruit::Init`
and `Bomb::Init` zero the whole flag byte via the inherited
`Entity::Entity` body called as part of the placement-new ctor… wait
— that only runs on the factory path). The actual clearing for recycled
entities is done by individual subclass `Init` methods that reset the
flag byte; **port should mirror this**: when re-Activating from the
pool, the subclass's Init should explicitly do
`flags &= ~(ENT_KILLED | ENT_INACTIVE)` (the binary does this
implicitly via per-field stores in Init bodies).

## Globals

| Name | GOT slot offset | Resolved address (this build) |
|------|-----------------|-------------------------------|
| `s_pEntityHeap` | `+0x7924` from GOT base 0x001eb130 | `0x001f2a54` (writable global, initially 0) |
| `s_EntityHeapSize` | `+0x742c` from GOT base 0x001eb130 | `0x001f255c` (writable global, initially 0) |

`Entity::HeapExist()` (0x0019d658):
```cpp
return *s_pEntityHeap != 0;   // not a separate flag — the pointer IS the flag
```

## Binary references (summary)

| Function | Address | Bytes |
|----------|---------|-------|
| `Mortar::Entity::HeapCreate(ulong)` | 0x0019d708 | 0x40 |
| `Mortar::Entity::HeapDestroy()` | 0x0019d6d0 | 0x38 |
| `Mortar::Entity::HeapExist()` | 0x0019d658 | ~0x10 |
| `Mortar::Entity::operator new(uint)` | 0x0019d7dc | 0x1a |
| `Mortar::Entity::operator delete(void*)` | 0x0019d770 | 0x1c |
| `Mortar::Entity::Activate(Entity*)` | 0x00170b18 | 0x06 (`flags &= 0xfe`) |
| `Mortar::Entity::Entity()` (full body) | 0x0019d88c | 0x5c |
| `LinkedHeap::LinkedHeap(uint)` (full body) | 0x00194988 | ~0x80 |
| `LinkedHeap::~LinkedHeap()` (full body) | 0x00194918 | ~0x18 |
| `LinkedHeap::Allocate(uint, char const*)` | 0x0019490c | 0x08 (wrap of AllocateMemory state=2) |
| `LinkedHeap::AllocateFixed(uint, char const*)` | 0x001948f4 | 0x08 (wrap, state=4) |
| `LinkedHeap::AllocateMemory(uint, char*, ulong)` | 0x001947f0 | ~0x100 |
| `LinkedHeap::Release(void*, bool)` (full body) | 0x0019469c | ~0xc8 |
| `LinkedHeap::ReleaseAll()` (full body) | 0x001945dc | ~0x40 |
| `LinkedHeap::FreeListSearch(ulong, void**)` | 0x001944c0 | ~0xb8 |
| `CreateEntity(long)` (factory delegate) | 0x0017421c | 0x78 |
| `ActorManager::Add(int, bool)` | 0x0017068c | 0x9a |
| `ActorManager::Initialise(int, int)` | 0x001704ac | 0x6e |
| `ActorManager::Destroy()` | 0x0017037c | ~0x38 |
| `ActorManager::Clear()` | 0x00170064 | ~0x120 |
| `ActorManager::Deactivate(Entity*)` | 0x00170184 | ~0x50 |
| `ActorManager::Remove(Entity*)` | 0x001702d8 | ~0x60 |
| GameInit pre-spawn loop | 0x0016cc0e .. 0x0016cc4e | — |
| GameInit `HeapCreate(0x20000)` call site | 0x0016cb48 .. 0x0016cb4e | — |
| GameExit `HeapDestroy()` call site | inside 0x0016cf74 | — |

## Port-side implementation plan

The fidelity-first answer for the port is: **do not actually replicate
LinkedHeap** (it's an arena tuned to Bada's bad system allocator and
serves no behavioural purpose on desktop/SDL), but **do** replicate
the `HeapExist` global pointer, the symmetric Create/Destroy
lifecycle, and the recycle semantics of `ActorManager::m_FreePool`.

### `src/entities/Entity.{h,cpp}` changes

Add to `Entity.h`:

- `static void HeapCreate(unsigned int bytes);` (already declared,
  needs body).
- `static void HeapDestroy();` (already declared, needs body).
- `static bool HeapExist();` -- `return s_pEntityHeap != nullptr;`.
- `static void* operator new(std::size_t n);` -- forwards to global
  `::operator new`.
- `static void operator delete(void* p);` -- forwards to global
  `::operator delete`.
- `static void* s_pEntityHeap;` and `static unsigned int s_EntityHeapSize;`
  private statics. Type can be `void*` since the port doesn't carve
  bytes; we only need the non-null sentinel.

In `Entity.cpp` define those statics (default null/0). `HeapCreate`
sets `s_pEntityHeap` to a `&dummy_singleton` (any non-null address)
and stores `bytes` for diagnostics. `HeapDestroy` zeros both.
`operator new`/`operator delete` route to global new/delete.

Comment each body with a one-line `// DIFFERS:` note pointing at the
binary address (0x0019d708, 0x0019d6d0, 0x0019d7dc, 0x0019d770) and
explaining the port uses the system heap because desktop malloc is
already coalescing.

### Wiring (no other src/ changes required)

- `EntityFactory.cpp` already calls `new Fruit()` etc. via the C++
  factory delegate — once `Entity::operator new` is overridden, every
  Fruit/Bomb/Coin/SlashEntity/BombBlast `new` automatically routes
  through it. Confirm the factory delegate is the
  `Mortar::Entity::operator_new(size); Subclass::Subclass(this);`
  two-step pattern from binary 0x0017421c — it should be (placement-new
  semantics over `Entity::operator new`).
- `ActorManager::Initialise` should be left calling its OWN allocator
  for the type-list array (0x2000 budget). The port can safely back
  this with `new std::list<Entity*>[numTypes]` without any LinkedHeap.
- `ActorManager::Add`'s recycle path (search `m_FreePool` for matching
  type, swap-down) is the load-bearing semantic for spawn determinism.
  See `actor-manager.md` for the existing port spec.
- The pre-spawn loop in GameInit (30x of types 0/1/4 + `flags |= 0x11`)
  should be ported verbatim — its only purpose is to populate
  `m_FreePool` for fast steady-state recycling.

### Optional follow-up (not load-bearing for fidelity)

If diagnostic tooling around "how many entity bytes are live" is ever
wanted, the port could add an atomic counter inside the new/delete
overrides. The binary doesn't expose one (the LinkedHeap has
`DisplayUsage` that goes to a debug-only log), so this is opt-in.

## See also

- [`actor-manager.md`](actor-manager.md) -- pool/free-list at the
  Entity level; this doc covers the byte-level allocator beneath.
- [`../entities/entity-base.md`](../entities/entity-base.md) -- if
  it grows -- for the `Entity` struct itself.
- `docs/systems/gameinit-todos.md` step 15 -- original TODO entry
  this RE resolves.
