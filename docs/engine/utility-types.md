# Mortar Engine Utility Types: ResourceLoader, SmartPtr, Delegate

## 1. ResourceLoader (68 bytes)

The HBR0 container parser used by MeshManager to load `.mad`/`.mmd` files. Part of the Mortar engine resource system.

### Struct Layout (68 bytes = 0x44)

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0x00 | 4 | uint32 | unknown0 | Initialized to 0 in constructor; read from DataReader in Initialize but not stored visibly |
| 0x04 | 40 (0x28) | AsciiString | basePath | Base path for resolving relative resource references |
| 0x2C | 12 | vector\<uint8\> | rawData | Raw byte data loaded from the resource chunk |
| 0x38 | 12 | vector\<ResourceLoader\> | children | Nested child ResourceLoaders (recursive container tree) |

### Key Methods

**Constructors:**
- `ResourceLoader(DataReader& reader, AsciiString const& basePath)` -- Primary. Sets basePath, then calls Initialize(reader).
- `ResourceLoader(AsciiString const& path)` -- From file path. Creates a FileDataReader, extracts parent path as basePath, calls Initialize.
- `ResourceLoader(ResourceLoader const& other)` -- Copy constructor. Copies all 4 fields.

**`Initialize(DataReader& reader)`** at `0x001b4708`:
1. Reads a uint32 (discarded/stored at offset 0)
2. Reads `childCount` (uint32), reserves children vector
3. For each child: reads size, creates VectorDataReader sub-stream, recursively constructs a child ResourceLoader, pushes to children vector
4. Reads a count of uint32 values (skipped -- possibly chunk type IDs)
5. Reads `dataSize` (uint32), if nonzero reads raw bytes into `rawData` vector

**`BasePathSet(AsciiString const&)`** at `0x001b534c`: Sets `this+4` (basePath)
**`BasePathGet() const`** at `0x001aa9e8`: Returns copy of `this+4` (basePath)

**`RegisterLoader<T>(Delegate1<SmartPtr<T>, ResourceLoader&>)`**: Static. Registers a typed loader callback keyed by a type hash (uint32). Uses a global `map<ulong, ConstFreeAutoPtr<LoaderHelperBase>>` behind a CriticalSection.

### Template Instantiations Found

- `RegisterLoader<Model>`, `RegisterLoader<Mesh>`, `RegisterLoader<IVertexStream>`, `RegisterLoader<IIndexStream>`, `RegisterLoader<AnimationList>`

### LoaderHelper (0x28 = 40 bytes)

- `LoaderHelperBase` at offset 0: vtable pointer
- `LoaderHelper<T>` at offset 4: contains a `Delegate1<SmartPtr<T>, ResourceLoader&>` (36 bytes)
- Total: 4 + 36 = 40 = 0x28 (matches `operator_new(0x28)` in RegisterLoader)

---

## 2. SmartPtr\<T\> (4 bytes)

An intrusive reference-counted smart pointer. Contains only a single raw pointer to the managed object. The reference count lives *inside* the pointed-to object (via `ReferenceCounter` / `__ReferenceCounterData` base class), not in the SmartPtr itself.

### Struct Layout (4 bytes)

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0x00 | 4 | T* | ptr | Raw pointer to the managed object (or nullptr) |

### __ReferenceCounterData (12 bytes)

The base class that all ref-counted objects inherit from:

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0x00 | 4 | vtable* | fns | Virtual function table (includes destructor, GetRefCounter) |
| 0x04 | 4 | uint32 | (refcount?) | Unnamed field |
| 0x08 | 4 | uint32 | (weak ref?) | Unnamed field |

`ReferenceCounter` inherits from `__ReferenceCounterData` and sets the vtable.

### Key Operations

**SetPtrCast\<T\>(T* newPtr)** -- The core assignment operation (e.g., at `0x001aaf68`):
```
if (newPtr != NULL) {
    refData = GetRefCounter(newPtr);    // vtable call at fns[2]
    refData->AddRef();
}
oldPtr = InterlockedPointer<T>::Swap(this, newPtr);  // atomic swap
if (oldPtr != NULL) {
    refData = GetRefCounter(oldPtr);
    refData->Release();
}
```

**~SmartPtr()**: Calls `Clear()`, which calls `Release()` on the current pointer's ref counter.

**SetPtr\<U\>(SmartPtr\<U\> const&)**: Extracts raw pointer from source SmartPtr, calls `SetPtrCast`.

**SetPtr\<U\>(U*)**: Directly calls `SetPtrCast` with the pointer.

### Thread Safety

Uses `InterlockedPointer<T>::Swap` for atomic pointer exchange, making the pointer swap thread-safe. `AddRef`/`Release` on `__ReferenceCounterData` are also likely atomic.

### Template Instantiations Found

SmartPtr\<Model\>, SmartPtr\<Mesh\>, SmartPtr\<Texture2D\>, SmartPtr\<Texture3D\>, SmartPtr\<TextureCube\>, SmartPtr\<Effect\>, SmartPtr\<EffectGroup\>, SmartPtr\<Geometry\>, SmartPtr\<GeometryBinding\>, SmartPtr\<IVertexStream\>, SmartPtr\<IVertexSource\>, SmartPtr\<IIndexStream\>, SmartPtr\<IIndexSource\>, SmartPtr\<AnimationList\>, SmartPtr\<AnimationState\>, SmartPtr\<SharedEffectProperties\>, SmartPtr\<ReferenceCounter\>, SmartPtr\<Job\>, SmartPtr\<__WeakReferenceData\>, SmartPtr\<Bada::Texture2DFromFile_Bada\>

---

## 3. Delegate0, Delegate1, Delegate2, Delegate3, Delegate4 (36 bytes each)

A type-safe callback/event system. All DelegateN variants have the **same size** because they all contain exactly one `StackAllocatedPointer<BaseDelegate, 32>`.

### Struct Layout (36 bytes = 0x24)

Every `DelegateN<ReturnType, Args...>` is just a thin wrapper around:

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0x00 | 36 | StackAllocatedPointer\<BaseDelegate, 32\> | impl | Polymorphic callable storage |

### StackAllocatedPointer\<BaseDelegate, 32\> (36 bytes = 0x24)

Small-buffer-optimized polymorphic pointer. Stores the BaseDelegate inline if it fits in 32 bytes, otherwise allocates on heap.

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0x00 | 4 | BaseDelegate* or inline data | ptrOrData | If isInline=true: vtable of inline object. If isInline=false: heap pointer |
| 0x04 | 28 | byte[28] | inlineStorage | Remaining inline storage (total 32 bytes from offset 0x00) |
| 0x20 | 1 | bool | isInline | 1 = object is stored inline in bytes 0x00..0x1F, 0 = heap-allocated |

**Constructor**: Sets `isInline = 1`, `ptr = 0` (empty delegate).

**Delete()**:
- If `isInline == false` (heap): calls `Resolve()` to get heap pointer, invokes virtual destructor via vtable[0], resets to inline/empty
- If `isInline == true` (inline): if vtable pointer != null, calls vtable[1] (destructor for inline object), resets pointer to null

**CopyConstruct\<T\>(T const&)**: Calls Delete() first, then sets `isInline = false` (0), constructs T in place at offset 0x00 (inline storage).

### BaseDelegate (vtable-based, polymorphic)

The vtable provides:
- `vtable[0]`: destructor (for heap objects)
- `vtable[1]`: destructor (for inline objects)
- `vtable[2]`: CopyConstruct / clone
- `vtable[3]`: Call -- the actual invocation

### Two Subtypes of BaseDelegate

**1. Global** (8 bytes) -- Free function callback:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0x00 | 4 | vtable* | vtable |
| 0x04 | 4 | FuncPtr | functionPointer |

`Global::Call(args...)`: Invokes `(*functionPointer)(args...)` -- decompiled as `(**(code**)(this + 4))(args...)`

**2. Callee\<T\>** (16 bytes) -- Member function callback:

| Offset | Size | Type | Field |
|--------|------|------|-------|
| 0x00 | 4 | vtable* | vtable |
| 0x04 | 4 | MemberFuncPtr (low word) | methodPtr part 1 |
| 0x08 | 4 | MemberFuncPtr (high word) or adjustment | methodPtr part 2 |
| 0x0C | 4 | T* | objectPtr |

ARM32 member function pointers are 8 bytes (two words), stored at offsets 0x04 and 0x08. The target object pointer is at 0x0C.

Both Global (8 bytes) and Callee (16 bytes) fit within the 32-byte inline buffer of StackAllocatedPointer.

### Delegate Call Chain

`DelegateN::operator()(args...)` -> `DelegateN::Call(args...)` -> `StackAllocatedPointer::Resolve()` -> `vtable[3](args...)` on the BaseDelegate subtype

From `Delegate0<void>::Call()` at `0x00147a10`:
```c
BaseDelegate* p = StackAllocatedPointer::Resolve();
if (p != NULL) {
    (*(p->vtable[3]))();  // vtable offset 0x0C
}
```

### Delegate Variants Found

| Type | Example Instantiations |
|------|----------------------|
| Delegate0\<void\> | Button callbacks on AboutScreen, MainScreen, PauseScreen, ScreenButton, etc. |
| Delegate1\<void, HUDControl*\> | UI control callbacks on most screens |
| Delegate1\<void, int\> | Integer parameter callbacks |
| Delegate1\<bool, float\> | Float parameter with bool return |
| Delegate1\<int, int\> | ScoreModifier callback |
| Delegate1\<bool, InputEvent*\> | Input event handler |
| Delegate1\<bool, MortarSound*\> | SpeedControl sound callback |
| Delegate1\<SmartPtr\<T\>, ResourceLoader&\> | Resource loading callbacks (Model, Mesh, IVertexStream, IIndexStream, AnimationList) |
| Delegate1\<void, Coin*\> | Coin collection callback |
| Delegate1\<void, ScrollingMenuItem*\> | ShopScreen menu item callback |
| Delegate1\<Entity*, long\> | Entity creation callback |
| Delegate2\<void, bool, bool\> | Two-bool callbacks |
| Delegate2\<long, ulong, bool&\> | Entity ID lookup |
| Delegate2\<void, P2PMessage, NetworkPacket*\> | Network message handler |
| Delegate3\<void, char const*, int, int\> | String+two-int callbacks |
| Delegate3\<void, char const*, void*, int\> | Data loading callback |
| Delegate3\<bool, MenuButton*, float, ScreenButton&\> | Menu button interaction |
| Delegate4\<bool, char const*, int, int, void*\> | Extended data callback |
| Delegate4\<bool, char const*, long long, int, void*\> | Extended data callback (64-bit) |

### QCallee Factory

`DelegateN::QCallee<T>(T* obj, RetType (T::*method)(Args...))` is a convenience factory that constructs a `Callee<T>` from an object pointer and member function pointer:
```cpp
Delegate0<void>::QCallee<MainScreen>(mainScreen, &MainScreen::SomeCallback);
```

---

## Summary Table

| Type | Size | Key Insight |
|------|------|-------------|
| ResourceLoader | 68 bytes | Recursive HBR0 container: uint32 + AsciiString(40) + vector\<uint8\>(12) + vector\<ResourceLoader\>(12) |
| SmartPtr\<T\> | 4 bytes | Single pointer; intrusive ref counting via __ReferenceCounterData in the pointed-to object |
| Delegate0..4 | 36 bytes | StackAllocatedPointer\<BaseDelegate, 32\> with inline Global(8) or Callee(16) storage |
| StackAllocatedPointer | 36 bytes | 32 bytes inline storage + 4-byte aligned bool at offset 0x20 (padded to 36) |
| BaseDelegate::Global | 8 bytes | vtable + function pointer |
| BaseDelegate::Callee\<T\> | 16 bytes | vtable + 8-byte member func ptr + object pointer |
| __ReferenceCounterData | 12 bytes | vtable + 2 counters (strong + weak?) |
| LoaderHelper\<T\> | 40 bytes | vtable(4) + Delegate1(36) |
