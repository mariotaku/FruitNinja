# ActorManager Factory & Hash Converter

Phase A2 of the entity-pipeline RE. Spec for the two delegates that
`GameInit` registers on `Mortar::ActorManager` immediately after
`Initialise(5, 0x2000)`:

- `m_FactoryDelegate` (`Delegate1<Entity*, long>`, struct +0x1024) -- the
  factory used by `Add(type)` when the free pool has no recycled entity.
- `m_HashDelegate` (`Delegate2<long, unsigned long, bool&>`, struct +0x1048)
  -- the StringHash -> entity-type lookup used only by `LoadEntity`.

Analysed: 2026-04-30. Cross-references `docs/engine/actor-manager.md`,
`docs/engine/delegate-system.md`.


## 1. GameInit call site (0x0016cb50, RegisterFactory + RegisterHashConverter)

Ghidra decompile of the registration block (steps 16b/16c of GameInit):

```c
// --- Factory registration ---
Delegate1<Entity*, long>::BaseDelegate::BaseDelegate(&local_58);  // sets vptr=BaseDelegate+8
local_54 = *(int*)(GOT + DAT_0016ccb4);                           // tmp.fnPtr  = &CreateEntity
local_58 = *(int*)(GOT + DAT_0016ccb8) + 8;                       // tmp.vptr   = Global_vtable + 8
Delegate1<Entity*, long>::Delegate1(aBStack_f8);                  // empty wrapper on stack
ActorManager::RegisterFactory(actorMgr, (Delegate1)aBStack_f8);   // copies into +0x1024
Delegate1<Entity*, long>::~Delegate1(aBStack_f8);
Delegate1<Entity*, long>::Global::~Global(&local_58);

// --- Hash converter registration ---
Delegate2<long, ulong, bool&>::BaseDelegate::BaseDelegate(&local_60);
local_5c = *(int*)(GOT + DAT_0016ccbc);                           // tmp.fnPtr  = &HashTypeConvert
local_60 = *(int*)(GOT + DAT_0016ccc0) + 8;                       // tmp.vptr   = Global_vtable + 8
Delegate2<long, ulong, bool&>::Delegate2(aBStack_11c);
ActorManager::RegisterHashConverter(actorMgr, aBStack_11c);       // copies into +0x1048
Delegate2<long, ulong, bool&>::~Delegate2(aBStack_11c);
Delegate2<long, ulong, bool&>::Global::~Global(&local_60);
```

The two `Delegate*` temporaries are constructed empty, then mutated to
`Global` (free-function) form by overwriting their first 8 bytes
(vtable+8, fnPtr) -- the classic `MakeDelegate_Global` recipe from
`docs/engine/delegate-system.md` Section 5.2. After
`RegisterFactory` / `RegisterHashConverter` clones them into the
`ActorManager`, the temporaries are torn down via the `Global` dtor.

GOT base = `DAT_0016c9d0 + 0x16c654` = `0x0007FADC + 0x0016C654` =
**`0x001EC130`**.


## 2. GOT slot resolution table

Each pair (function-pointer slot, vtable slot) lives in the GOT at
addresses computed from the four immediates `DAT_0016ccb4..0016ccc0`.
Bytes were read directly from the binary; little-endian 32-bit values.

| Immediate | Address | LE bytes | Value (GOT offset) | Resolved GOT entry | GOT entry contents | Resolves to |
|-----------|---------|----------|--------------------|--------------------|--------------------|-------------|
| `DAT_0016ccb4` | `0x0016ccb4` | `94 74 00 00` | `0x00007494` | `0x001F35C4` | `1c 42 17 00` | **`0x0017421C`** -- `CreateEntity(long)` |
| `DAT_0016ccb8` | `0x0016ccb8` | `70 78 00 00` | `0x00007870` | `0x001F39A0` | `50 a3 1e 00` | **`0x001EA350`** -- `Delegate1<Entity*,long>::Global` vtable |
| `DAT_0016ccbc` | `0x0016ccbc` | `28 7a 00 00` | `0x00007A28` | `0x001F3B58` | `4c 41 17 00` | **`0x0017414C`** -- `HashTypeConvert(ulong, bool&)` |
| `DAT_0016ccc0` | `0x0016ccc0` | `58 75 00 00` | `0x00007558` | `0x001F3688` | `a8 a3 1e 00` | **`0x001EA3A8`** -- `Delegate2<long,ulong,bool&>::Global` vtable |

Both `Global` vtables follow the standard 8-slot layout from
`delegate-system.md` Section 2 (top-offset, typeinfo, then 6 method
slots). vtable+8 is what the constructed delegate stores as its `vptr`.


## 3. `RegisterFactory` -- 0x0016d870 (real body, behind PLT 0x00107c34)

```asm
0016d870  add.w r0, r0, #0x1000   ; r0 = this + 0x1000
0016d874  push  {r3, lr}
0016d876  adds  r0, #0x24         ; r0 = this + 0x1024 (m_FactoryDelegate)
0016d878  blx   Delegate1<Entity*,long>::operator=
0016d87c  pop   {r3, pc}
```

```c
void ActorManager::RegisterFactory(Delegate1<Entity*,long> src) {
    Delegate1<Entity*,long>::operator=(&this->m_FactoryDelegate, &src);
    // operator= clones src into this->m_FactoryDelegate via the
    // Global::CopyConstruct vtable slot (delegate-system.md Sec 5.4).
}
```

**Storage offset**: `+0x1024` (matches port `ActorManager::m_FactoryDelegate`).
**Signature**: takes a delegate by value (36 B / 0x24 stack slot, passed
by reference per AAPCS). Returns void.


## 4. Factory body -- `CreateEntity(long)` @ 0x0017421C

```c
// 0x0017421C, signature: Entity* __stdcall CreateEntity(long entityType)
Entity* CreateEntity(long entityType) {
    void* obj;
    switch (entityType) {
    case 0:  obj = Entity::operator_new(0x118); Fruit::Fruit(obj);          break;  // Fruit
    case 1:  obj = Entity::operator_new(0x0B0); Bomb::Bomb(obj);            break;  // Bomb
    case 2:  obj = Entity::operator_new(0x094); Coin::Coin(obj);            break;  // Coin
    case 3:  obj = Entity::operator_new(0x184); SlashEntity::SlashEntity(obj); break; // SlashEntity
    case 4:  obj = Entity::operator_new(0x070); BombBlast::BombBlast(obj);  break;  // BombBlast
    default: obj = nullptr;
    }
    return (Entity*)obj;
}
```

Sizes match the per-class struct sizes documented elsewhere
(`docs/entities/*`). The default branch returns `nullptr` -- `Add(type)`
detects this and bails (port: `ActorManager::Add` stderr log).

The free function ABI is `__stdcall` (Ghidra-confirmed); on ARM hard-float
this is r0 = entityType, r0 = return.

The port already implements this 1:1 in `src/entities/EntityFactory.cpp`
(`CreateEntity`), with the documented divergence that case 3 returns
`nullptr` because the port owns a single `g_pSlashEntity` rather than
pooling SlashEntity through ActorManager.


## 5. `RegisterHashConverter` -- 0x0016d900 (real body, behind PLT 0x001069f8)

```asm
0016d900  add.w r0, r0, #0x1040   ; r0 = this + 0x1040
0016d904  push  {r3, lr}
0016d906  adds  r0, #0x8          ; r0 = this + 0x1048 (m_HashDelegate)
0016d908  blx   Delegate2<long,ulong,bool&>::operator=
0016d90c  pop   {r3, pc}
```

```c
void ActorManager::RegisterHashConverter(Delegate2<long, ulong, bool&> src) {
    Delegate2<long, ulong, bool&>::operator=(&this->m_HashDelegate, &src);
}
```

**Storage offset**: `+0x1048` (matches port `ActorManager::m_HashDelegate`).
**Signature**: takes a delegate by value. Returns void.


## 6. Hash converter body -- `HashTypeConvert(ulong, bool&)` @ 0x0017414C

The function takes a 32-bit StringHash and produces an `entityType` long
plus an `ok` out-parameter. Lazily initialises a 5-slot lookup table on
first call via `__cxa_guard`.

### 6.1 Static table at `g_HashTypeTable` (0x001F3DE0, 5 x 12 = 60 bytes)

Each entry is 12 bytes (`{ uint8 ok; pad[3]; uint32 hash; long entityType; }`):

| Slot | Offset | `ok` byte | `hash` (filled at first call) | `entityType` | StringHash input | Resolved string |
|------|--------|-----------|------------------------------|--------------|-------------------|-----------------|
| 0 | +0x00 | 0x01 | StringHash("fruit") | 0 | `0x001B96A9` | `"fruit"` |
| 1 | +0x0C | 0x01 | StringHash("bomb")  | 1 | `0x001B96CE` | `"bomb"`  |
| 2 | +0x18 | 0x01 | StringHash("slash") | 3 | `0x001BCC56` | `"slash"` |
| 3 | +0x24 | 0x01 | StringHash("blast") | 4 | `0x001BCC5C` | `"blast"` |
| 4 | +0x30 | 0x01 | StringHash("coin")  | 2 | `0x001BCC62` | `"coin"`  |

All `ok` bytes are pre-set to `1` in `.data`. The `entityType` longs are
also baked in. Only the `hash` field is mutated at runtime (once, under
`__cxa_guard`). Note ordering: lookup table is sorted by string-spelling
proximity to coin-counter strings, **not** by entityType -- type 2 (Coin)
is in the last slot.

Guard variable: `g_HashTypeTable_guard` @ `0x0024D638`.

### 6.2 Function body

```c
// 0x0017414C. __stdcall: r0 = hash, r1 = bool* outOk, returns long entityType in r0.
long HashTypeConvert(ulong inputHash, bool* outOk) {
    // Lazy init -- StringHash all 5 strings into the table on first call.
    if ((g_HashTypeTable_guard & 1) == 0 && __cxa_guard_acquire(&g_HashTypeTable_guard)) {
        g_HashTypeTable[0].hash = StringHash("fruit");   // entry 0 -> type 0
        g_HashTypeTable[1].hash = StringHash("bomb");    // entry 1 -> type 1
        g_HashTypeTable[2].hash = StringHash("slash");   // entry 2 -> type 3
        g_HashTypeTable[3].hash = StringHash("blast");   // entry 3 -> type 4
        g_HashTypeTable[4].hash = StringHash("coin");    // entry 4 -> type 2
        __cxa_guard_release(&g_HashTypeTable_guard);
    }

    for (int i = 0; i < 5; i++) {
        if (g_HashTypeTable[i].hash == inputHash) {
            *outOk = g_HashTypeTable[i].ok;       // always 1 in baked data
            return g_HashTypeTable[i].entityType; // 0/1/3/4/2 in table order
        }
    }

    *outOk = false;     // not-found path
    return -1;          // 0xFFFFFFFFL
}
```

The disassembly shows the ASM-level layout precisely matches: stride
0xC, hash field at +4, type field at +8, ok byte at +0. The not-found
return is `mov.w r0, #0xffffffff` (= -1 long).

### 6.3 Where it is called

- **Only consumer**: `ActorManager::LoadEntity` (0x00170728) -- reads the
  hash from `*(uint32*)(EntityChunk + 0x48)`, calls
  `Delegate2::operator()(this->m_HashDelegate, hash, &localOk)`,
  bails on `entityType == -1`, otherwise calls `Add(entityType, ok)`
  and finishes deserialising the chunk.
- `LoadEntity` is part of the level-serialisation path that the port
  does not implement. **The hash converter is dead code in the live
  port**; a stub binding is fine for parity but never invoked.


## 7. Delegate type-system mapping

Both delegates use the standard 36-byte (0x24) inline-Global form
documented in `docs/engine/delegate-system.md`:

```
+0x00  vptr      = 0x001EA350 + 8   (factory)        / 0x001EA3A8 + 8   (hash)
+0x04  fnPtr     = 0x0017421C       (factory)        / 0x0017414C       (hash)
+0x08..0x1F      pad / unused inline buffer
+0x20  usingHeap = 0  (inline)
+0x21..0x23      pad
```

The construction sequence in GameInit is:

1. `BaseDelegate::BaseDelegate(&tmp)` -- sets `vptr = abstract+8`,
   `fnPtr = 0`.
2. Two raw stores overwrite `vptr` and `fnPtr` with the `Global` vtable
   pointer and the actual function pointer (mirrors
   `MakeDelegate_DrawUtil_HUD` recipe at 0x00130DAC).
3. The wrapping `Delegate1` / `Delegate2` ctor is invoked on a separate
   stack-buffer that begins as the empty state (`usingHeap=1, ptr=0`).
4. `RegisterFactory` / `RegisterHashConverter` calls
   `Delegate*::operator=(&dst, &src)` which:
   - tears down whatever was in `dst.m_FactoryDelegate / m_HashDelegate`
     (initially empty),
   - calls `Global::CopyConstruct` (slot 2) to placement-new the source
     `Global` into the destination's inline buffer,
   - sets `dst.usingHeap = 0`.

After that, the temporaries are destroyed -- but the function pointer
has already been **copied** into the ActorManager's slot, not aliased.

`Compare(other)` for these `Global` delegates returns `this->fnPtr ==
other->fnPtr` (delegate-system.md Section 3) -- equality is purely by
function-pointer identity, not by call site or registration order.


## 8. Port-side implementation plan

### 8.1 Current state of port

`src/entities/ActorManager.h` currently models the two delegate slots as
**raw C function pointers** (no 36-byte struct):

```cpp
typedef Entity* (*FactoryFn)(int entityType);
FactoryFn m_FactoryDelegate;                       // +0x1024 in binary
typedef void (*HashFn)(long, unsigned long&, bool&);  // wrong return type, see below
HashFn   m_HashDelegate;                           // +0x1048 in binary
```

`RegisterFactory(FactoryFn)` is implemented inline as
`m_FactoryDelegate = factory`. `RegisterHashConverter(HashFn)` does the
same. `GameInit` in the port is currently expected to call
`actorMgr->RegisterFactory(nullptr)` / `RegisterHashConverter(nullptr)`
(see HEADER comment in `ActorManager.h`).

`src/entities/EntityFactory.cpp` already provides
`Entity* CreateEntity(int entityType)` matching the binary's
`CreateEntity` 1:1 (with the SlashEntity divergence). This is the
correct host for the factory function.

### 8.2 What the implementer should do

1. **Factory binding.** The port already has the right function in
   `EntityFactory.cpp`. GameInit (the port equivalent of step 16b)
   should simply call:
   ```cpp
   ActorManager::GetInstance()->RegisterFactory(&CreateEntity);
   ```
   No struct-allocation needed -- the port deliberately collapsed the
   36-byte `Delegate1<Entity*,long>` to a raw `FactoryFn` pointer
   because:
   - `m_FactoryDelegate` is read by exactly one site (`Add`'s factory
     path) which the port already implements as a direct call;
   - no other binary code probes the 36 bytes at `+0x1024`, so layout
     parity does not matter unless the rest of `ActorManager` becomes
     binary-layout-faithful (which it is not -- `m_pTypeLists` is a
     `std::list<Entity*>*`, not a flat 0x18-byte std::list array, so
     the struct already diverges).

2. **Hash-converter binding.** The port should add a free function in
   `src/entities/EntityFactory.cpp` (or a sibling
   `EntityHashConverter.cpp`) whose body matches Section 6.2:
   ```cpp
   // 0x0017414C
   long HashTypeConvert(unsigned long inputHash, bool& outOk);
   ```
   The five baked entries can be a `static const` array initialised at
   first call (no `__cxa_guard` needed -- C++11 magic statics suffice).
   GameInit would then:
   ```cpp
   ActorManager::GetInstance()->RegisterHashConverter(&HashTypeConvert);
   ```

   **However**, since `LoadEntity` is not ported, this is purely for
   parity -- a `nullptr` registration is functionally equivalent in the
   live port, and the cost of writing the table is only justified when
   `LoadEntity` lands. Acceptable for now: leave `RegisterHashConverter`
   as `nullptr` and add a `// RE-gap: port LoadEntity to wire this`
   comment.

3. **Signature fix on `HashFn`.** The port's current typedef:
   ```cpp
   typedef void (*HashFn)(long, unsigned long&, bool&);
   ```
   has the **wrong shape** -- the binary's function returns the
   entityType (`long`) and writes only the `ok` flag through a
   reference. Correct signature:
   ```cpp
   typedef long (*HashFn)(unsigned long inputHash, bool& outOk);
   ```
   `LoadEntity` (0x00170728) decompile shows
   `entityType = Delegate2::operator()(hash, &localOk)` -- the second
   template parameter (`unsigned long`) is the input, and the return
   slot of `Delegate2<long, ulong, bool&>` is the entityType. The
   `bool&` template arg is the third parameter that becomes the
   reference parameter of the call. This wires up to the
   `Delegate2<Ret, Arg1, Arg2&>` form, not `Delegate2<void, ...>`.

   This is a port-side bug to flag, not a reverse-engineering question.

4. **No struct-layout alignment work.** The 36-byte Delegate inline
   form does not need to be modelled in the port unless and until the
   port commits to binary-faithful struct sizes for `ActorManager`.
   Port `ActorManager` is already not size-faithful, so a raw fn-ptr
   is the right level of abstraction here.

### 8.3 Acceptance criteria

- `ActorManager::GetInstance()->RegisterFactory(&CreateEntity)` is
  called during port GameInit, before the pre-spawn loop (step 19).
- `Add(0)` / `Add(1)` / `Add(2)` / `Add(4)` succeed when the free pool
  is empty (factory path); `Add(3)` returns `nullptr` and logs
  (port-specific divergence).
- `HashFn` typedef in `ActorManager.h` is corrected to
  `long (*)(unsigned long, bool&)`. Binding can stay `nullptr` until
  `LoadEntity` is ported.


## 9. Function & address quick-reference

| Address | Name | Notes |
|---------|------|-------|
| `0x00107c34` | PLT thunk -> `RegisterFactory` | Calls through `PTR_RegisterFactory_001f2e0c` |
| `0x001f2e0c` | GOT slot | Contains `0x0016d870` |
| `0x0016d870` | `Mortar::ActorManager::RegisterFactory` | Stores delegate at this+0x1024 |
| `0x001069f8` | PLT thunk -> `RegisterHashConverter` | Calls through `PTR_RegisterHashConverter_001f27f8` |
| `0x001f27f8` | GOT slot | Contains `0x0016d900` |
| `0x0016d900` | `Mortar::ActorManager::RegisterHashConverter` | Stores delegate at this+0x1048 |
| `0x0017421C` | `CreateEntity(long)` | 5-case switch -> Fruit/Bomb/Coin/SlashEntity/BombBlast |
| `0x0017414C` | `HashTypeConvert(ulong, bool&)` | 5-entry hash -> entityType lookup |
| `0x00170728` | `ActorManager::LoadEntity` | Only consumer of `m_HashDelegate` |
| `0x001EA350` | `Delegate1<Entity*,long>::Global` vtable | factory delegate |
| `0x001EA3A8` | `Delegate2<long,ulong,bool&>::Global` vtable | hash delegate |
| `0x001F3DE0` | `g_HashTypeTable` (60 B) | 5 x 12 entries, lazily-hashed |
| `0x0024D638` | `g_HashTypeTable_guard` | __cxa_guard for lazy init |
| `0x001B96A9` | string `"fruit"` | hash entry 0, type 0 |
| `0x001B96CE` | string `"bomb"` | hash entry 1, type 1 |
| `0x001BCC56` | string `"slash"` | hash entry 2, type 3 |
| `0x001BCC5C` | string `"blast"` | hash entry 3, type 4 |
| `0x001BCC62` | string `"coin"` | hash entry 4, type 2 |
