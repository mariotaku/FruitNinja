# Mortar::Delegate Type Family

Spec for the `Mortar::Delegate0..4<RetType, Args...>` family used throughout the
HUD/screen system. Every arity has the **same 36-byte (0x24) struct layout** —
they all wrap a `Mortar::StackAllocatedPointer<BaseDelegate, 32>` that
small-buffer-stores either a `Global` (free function, 8 B) or a `Callee<T>`
(member function, 16 B) inline.

This doc supersedes the brief Delegate notes in
`docs/engine/utility-types.md`. In particular: the byte at `+0x20` is **not**
`isInline = 1`, it's the inverse — `0` means the object is in the inline
buffer.


## 1. Top-level layout — `Delegate<N>` / `StackAllocatedPointer<Base, 32>`

`Delegate<N><Ret, Args...>` is just a thin name-mangled wrapper around
`Mortar::StackAllocatedPointer<DelegateN<...>::BaseDelegate, 32>`. The
template's two parameters are `(BaseDelegate type, inline byte budget = 32)`.
**Total size: 36 bytes (0x24)** — 32 B inline storage + 1 B flag + 3 B trailing
padding.

```
offset  size  field            description
+0x00   4     ptr              inline-mode: vptr of the inline subobject
                               heap-mode:   pointer to heap-allocated subobject
+0x04   28    inlineRest[28]   remainder of the 32 B inline buffer
+0x20   1     usingHeap        0 = subobject lives in inline buffer (offsets 0..0x1F)
                               1 = subobject lives on heap (or empty); pointer at +0x00
+0x21   3     pad              alignment to 4
total   36
```

Empty/null state is `usingHeap = 1, ptr = 0`.

The `BaseDelegate` subobject placed in the inline buffer is **always** one of
two concrete types — `Global` (8 B) or `Callee<T>` (16 B). Both fit easily
inside the 32 B budget, so in practice the heap path is never taken in this
binary; the 36-byte storage is monomorphic and self-contained.

Verified at:
- `StackAllocatedPointer::StackAllocatedPointer()` @ `0x00109ae8`
  ```asm
  movs r2, #1
  strb.w r2, [r0, #0x20]   ; usingHeap = 1
  subs  r2, #1
  str   r2, [r0, #0]       ; ptr = 0
  bx    lr
  ```
- `StackAllocatedPointer<...>::Resolve()` @ `0x001312b0`
  ```asm
  ldrb.w r3, [r0, #0x20]
  cbz    r3, +2            ; if usingHeap == 0 -> return r0 (this == &inline)
  ldr    r0, [r0, #0]      ; else                return *(void**)this (heap ptr)
  bx     lr
  ```


## 2. `BaseDelegate` vtable layout

After construction, `[+0x00]` holds the subtype's **vtable pointer + 8** (gcc
ARM ABI: vptr skips the leading `(top-of-object, typeinfo)` pair). The shared
slot order across all `BaseDelegate` subtypes:

| Slot | Byte offset (from vptr) | Method | Purpose |
|------|------------------------|--------|---------|
| 0 | +0x00 | `~BaseDelegate()` (non-deleting) | Used when destroying inline subobject |
| 1 | +0x04 | `~BaseDelegate()` (deleting)     | Used when destroying heap subobject (`operator delete` after) |
| 2 | +0x08 | `CopyConstruct(this, dst)`       | Clone `*this` into `dst` (a `StackAllocatedPointer*`) |
| 3 | +0x0C | `Call(args...)`                  | Invoke — the actual dispatch |
| 4 | +0x10 | `GetTypeID() const`              | Returns RTTI/typeinfo pointer (used for type-safe equals) |
| 5 | +0x14 | `Compare(BaseDelegate const&)`   | Equality test — compares stored fnptr/PTMF/object |

`BaseDelegate` itself is **abstract** — its noop vtable at `0x001e8df8` has
`__cxa_pure_virtual` (`0x002773d0`) for slots 2..5. Concrete subtypes are
`Global` and `Callee<T>`, each with its own vtable.

### Verified addresses (Delegate1<void, HUDControl*> family)

| Subtype | Vtable | First useful byte (vptr stored = vtable+8) |
|---------|--------|------------------------------------------|
| `BaseDelegate` (abstract) | `0x001e8df8` | `0x001e8e00` |
| `Global`                  | `0x001e8fd8` | `0x001e8fe0` |
| `Callee<ScreenButton>`    | `0x001e9008` | `0x001e9010` |

`Global` vtable @ `0x001e8fd8`:
- `[+0]` = 0 (top-offset)
- `[+4]` = `0x001e8ff8` (typeinfo)
- `[+8]`  slot 0 = `0x00130ffc` ~Global non-deleting
- `[+0xC]` slot 1 = `0x001915a4` ~Global deleting
- `[+0x10]` slot 2 = `0x0019151c` `Global::CopyConstruct`
- `[+0x14]` slot 3 = `0x0013144c` `Global::Call`
- `[+0x18]` slot 4 = `0x00131458` `Global::GetTypeID`
- `[+0x1C]` slot 5 = `0x00131470` `Global::Compare`

`Callee<ScreenButton>` vtable @ `0x001e9008`:
- `[+8]`  slot 0 = `0x00131020` ~Callee non-deleting
- `[+0xC]` slot 1 = `0x001915d0` ~Callee deleting
- `[+0x10]` slot 2 = `0x001914c8` `Callee::CopyConstruct`
- `[+0x14]` slot 3 = `0x001913dc` `Callee::Call`
- `[+0x18]` slot 4 = `0x00131400` `Callee::GetTypeID`
- `[+0x1C]` slot 5 = `0x00131418` `Callee::Compare`


## 3. `Global` subtype — free-function callback (8 B)

```
offset  size  field        description
+0x00   4     vptr         &Global_vtable + 8
+0x04   4     fnPtr        free function pointer (with thumb LSB)
total   8
```

`Global::Call(args...)` @ `0x0013144c` (Delegate1<void,HUDControl*>):
```asm
push  {r3, lr}
ldr   r3, [r0, #4]   ; r3 = fnPtr
mov   r0, r1         ; first arg (HUDControl*) into r0 — drops the bound `this`
blx   r3             ; tail-style call (note: only 1 arg here — adjust per arity)
pop   {r3, pc}
```

For higher arities, `r0..r3` shift right and the rest spill on stack normally;
the bound `this` (`r0` on entry) is *replaced* by the first user argument.

`Global::Compare` @ `0x00131470`:
```c
return this->fnPtr == other->fnPtr;
```

`Global::GetTypeID` @ `0x00131458`: returns `&typeinfo_for<Global>` (used by the
templated `Delegate::operator==` to short-circuit cross-type comparisons).


## 4. `Callee<T>` subtype — bound member function (16 B)

```
offset  size  field        description
+0x00   4     vptr         &Callee_vtable + 8
+0x04   4     obj          T* (bound this)
+0x08   4     ptmf_lo      gcc 4.5 PTMF first word: function address OR vtable byte-offset
+0x0C   4     ptmf_hi      gcc 4.5 PTMF second word: this-adjustment
                              LSB == 1 -> ptmf_lo is a vtable byte-offset (virtual call)
                              LSB == 0 -> ptmf_lo is the absolute function address
total   16
```

`Callee<T>::Callee(T* obj, RetType (T::*method)(Args...))` @ `0x0012fbbc` (a
Delegate0<void> instance, but the layout is identical for all arities):
```asm
push  {r0, r1, r4..r8, lr}
ldr   r4, [GOT_offset]
mov   r12, sp
mov   r6, r0           ; r6 = this
stm   r12, {r2, r3}    ; spill PTMF (r2,r3) to stack
adr   r3, GOT_anchor
adds  r4, r4, r3       ; r4 = GOT base
mov   r5, r1           ; r5 = obj
mov   r7, r2           ; r7 = ptmf_lo
ldr   r8, [sp, #4]     ; r8 = ptmf_hi (reload via spilled location)
blx   BaseDelegate::BaseDelegate(this)
ldr   r3, [GOT_vtable_offset]
str   r5, [r6, #4]     ; this->obj      = obj
str   r8, [r6, #0xc]   ; this->ptmf_hi  = r8
ldr   r3, [r4, r3]     ; r3 = &Callee_vtable
str   r7, [r6, #8]     ; this->ptmf_lo  = r7
adds  r3, #8           ; skip top-offset & typeinfo
str   r3, [r6, #0]     ; this->vptr     = vtable + 8
pop   {r2, r3, r4..r8, pc}
```

`Callee<T>::Compare` @ `0x00131418`:
```c
if (this->ptmf_lo == other->ptmf_lo) {
    if (this->ptmf_hi == other->ptmf_hi
        || (this->ptmf_lo == 0
            && (other->ptmf_hi & 1) == 0
            && (this->ptmf_hi & 1) == 0))
    {
        return this->obj == other->obj;
    }
}
return false;
```
The compare logic explicitly handles both PTMF flavours (virtual vs direct).

`Callee<T>::Call(args...)` @ `0x001913dc` is a small thunk that:
- Reads `obj` (`[this+4]`).
- Reads PTMF (`[this+8], [this+0xC]`).
- Tests `ptmf_hi & 1`:
  - direct: jump to `ptmf_lo` with `r0 = obj + (ptmf_hi >> 1)` and the user
    args shifted left by one register (`r0` was the delegate, now becomes
    `obj`; user args were in `r1..` and get repacked starting at `r1`).
  - virtual: load `vtable = *(void**)obj`; call `vtable[ptmf_lo / 4]` with
    the same `r0 = obj + (ptmf_hi >> 1)` adjustment.

(Ghidra has merged this thunk into a neighbouring function and won't carve
it as a discrete symbol; the bytes at `0x001913dc..0x001913fb` are the
actual thunk.)


## 5. Construction recipes

### 5.1 Empty default

`Delegate1<void, HUDControl*>::Delegate1()` ⇒ `StackAllocatedPointer()` @
`0x00109ae8`:
```
this[0..3]   = 0
this[+0x20]  = 1   (usingHeap, but ptr is null, so it's the empty state)
```

`Resolve()` returns `null`, so `Call()` is a no-op.

### 5.2 No-op delegate that *does* something (binds DefaultDeleteCallback)

The "DrawUtil HUD" no-op is **not** the empty state — it binds a real free
function (`DefaultDeleteCallback @ 0x00143f94`, which returns its arg
unmodified). Pattern from `MakeDelegate_DrawUtil_HUD` @ `0x00130dac`:

```c
// Caller stack-allocates an 8-byte Global, then passes its address.
Global tmp;
MakeDelegate_DrawUtil_HUD(&tmp);                   // fills tmp
DelegateN<...>::operator=(&target, &tmp);          // clone into target
Global::~Global(&tmp);
```

`MakeDelegate_DrawUtil_HUD` body:
```asm
push  {r3, r4, r5, lr}
ldr   r4, [pc, ...]              ; r4 = GOT base offset
adr   r3, anchor
mov   r5, r0                     ; r5 = &tmp
adds  r4, r4, r3
blx   BaseDelegate::BaseDelegate ; sets tmp.vptr to BaseDelegate vtable +8
ldr   r2, [pc, ...]              ; GOT[fnPtr_to_DefaultDeleteCallback]
mov   r0, r5
ldr   r3, [pc, ...]              ; GOT[Global_vtable]
ldr   r2, [r4, r2]               ; r2 = &DefaultDeleteCallback
ldr   r3, [r4, r3]               ; r3 = &Global_vtable
str   r2, [r5, #4]               ; tmp.fnPtr = &DefaultDeleteCallback
adds  r3, #8                     ; skip {top-offset, typeinfo}
str   r3, [r5, #0]               ; tmp.vptr  = Global_vtable + 8 (overrides BaseDelegate)
pop   {r3, r4, r5, pc}
```

Final `tmp` bytes (at offsets 0..7):
```
+0: 0x001E8FE0   (Global vtable + 8 for Delegate1<void, HUDControl*>)
+4: 0x00143F95   (&DefaultDeleteCallback | thumb bit)
```

### 5.3 Bind member function (`QCallee<T>`)

```cpp
Callee<T> tmp;
DelegateN<...>::QCallee<T>(&tmp, obj, &T::method);
DelegateN<...>::operator=(&target, &tmp);
Callee<T>::~Callee(&tmp);
```

`QCallee` is a one-line static helper that just forwards to the `Callee` ctor.
The actual ctor packs `(obj, method)` per Section 4. The `method` argument is
an 8-byte gcc-4.5 PTMF passed in `(r2, r3)` per the hard-float ABI.

### 5.4 `operator=` (assign-from-temporary)

`StackAllocatedPointer::operator=` @ `0x001a9080`:
```c
auto* p = src->Resolve();
if (p == nullptr) {
    *(void**)this = nullptr;
    this->usingHeap = 1;            // become empty
} else {
    // Source is non-null. Use *its* CopyConstruct vtable slot (slot 2)
    // to clone into `this`. This sets this->vptr, copies the fields,
    // and writes this->usingHeap = 0.
    p->vtable[2](p, this);
}
```

The matching `CopyConstruct<Concrete>` thunk @ e.g. `0x001314ac`:
```asm
push  {r3, r4, r5, lr}
mov   r4, r0
mov   r5, r1
blx   StackAllocatedPointer::Delete   ; tear down whatever's currently in `this`
movs  r3, #0
mov   r0, r4
mov   r1, r5
strb.w r3, [r4, #0x20]                ; this->usingHeap = 0 (now inline)
blx   Concrete::Concrete              ; placement-new the concrete subtype
                                      ; into the inline buffer at this+0
pop   {r3, r4, r5, pc}
```


## 6. Invoke flow — `DelegateN::operator()(args...)`

Tail of the chain (Delegate1<void, HUDControl*>):

```
operator()  @  0x00131360  -->  Call  @  0x0013134c
                                         push   {r4, lr}
                                         mov    r4, r1            ; save user arg
                                         blx    Resolve            ; r0 = inline ptr or heap ptr
                                         cbz    r0, ret            ; null  -> no-op
                                         ldr    r3, [r0, #0]      ; r3 = vptr
                                         mov    r1, r4             ; restore user arg
                                         ldr    r3, [r3, #0xc]    ; r3 = vtable[3] (Call)
                                         blx    r3                 ; (this=r0, args... =r1..)
                                  ret:   pop    {r4, pc}
```

For arity N>1 the same logic applies — Resolve, `cbz` null check, vptr-load,
`vtable[3]` indirect call. **Empty delegates are silently ignored** (no
exception, no crash, no return-value adjustment — caller sees zero-init
return for non-void delegates because `r0` retains whatever Resolve
left there which is 0).


## 7. Destruction flow

`DelegateN::~DelegateN()` ⇒ `StackAllocatedPointer::~StackAllocatedPointer()`
⇒ `StackAllocatedPointer::Delete()` @ `0x00126ea0`:
```c
if (this->usingHeap == 0) {                 // inline subobject
    void* p = this->Resolve();              // == this
    p->vtable[0]();                         // non-deleting dtor (slot 0)
    *(void**)this = nullptr;
    this->usingHeap = 1;                    // mark empty
} else if (*(void**)this != nullptr) {      // heap subobject
    auto* p = *(void**)this;
    p->vtable[1]();                         // deleting dtor (slot 1, runs operator delete)
    *(void**)this = nullptr;
}
```

Note slot 0 vs slot 1: the **inline** path uses slot 0 (the non-deleting
destructor — no `operator delete` because the bytes belong to the parent
object). The **heap** path uses slot 1 (deleting), which runs the
destructor and then `operator delete(this)`.

`~ScreenButton()` @ `0x00131628` is a clean example — five tail-calls in
order:
```asm
push   {r4, r5, r6, lr}
mov    r4, r0
adds   r0, #0x78         ; this+0x78
add.w  r5, r4, #0x54
blx    ~Delegate1<void,HUDControl*>(this+0x78)   ; deletedCb
mov    r0, r5
blx    ~Delegate0<void>(this+0x54)               ; clickCb
add.w  r0, r4, #0x30
blx    ~Delegate3<bool,MenuButton*,float,...>(this+0x30) ; updateCb
add.w  r0, r4, #0xc
blx    ~Delegate1<bool,float>(this+0xc)          ; visCheck
add.w  r0, r4, #0x8
blx    ~SmartPtr<Texture>(this+0x8)
mov    r0, r4
pop    {r4, r5, r6, pc}
```
ScreenButton field offsets: 0x0C, 0x30, 0x54, 0x78 — exactly 0x24 (36 B)
apart, confirming each delegate is 36 B.


## 8. Known instantiations and their slot offsets

| Owner | Delegate field | Offset | Type |
|-------|---------------|--------|------|
| `ScreenButton` | `m_VisibilityCheck` | +0x0C | `Delegate1<bool, float>` |
| `ScreenButton` | `m_UpdateCallback`  | +0x30 | `Delegate3<bool, MenuButton*, float, ScreenButton&>` |
| `ScreenButton` | `m_ClickCallback`   | +0x54 | `Delegate0<void>` |
| `ScreenButton` | `m_DeletedCallback` | +0x78 | `Delegate1<void, HUDControl*>` |
| `HUDControl`   | `m_RemoveCallback`  | +0x38 | `Delegate1<void, HUDControl*>` |
| `MenuButton`   | `m_ClickCallback`   | +0x88 | `Delegate0<void>` (assumed — confirms via ~MenuButton) |
| `MenuButton`   | `m_DeletedCallback` | +0xAC | `Delegate1<void, HUDControl*>` |

All 36 B / 0x24 stride.

Other arities seen in the binary (sizes are identical, only the `Call` thunk
in slot 3 differs):

- `Delegate0<void>`
- `Delegate1<void, T*>` for T ∈ {HUDControl, Coin, ScrollingMenuItem, Entity, ...}
- `Delegate1<bool, float>`, `Delegate1<int, int>`, `Delegate1<bool, InputEvent*>`,
  `Delegate1<bool, MortarSound*>`, `Delegate1<Entity*, long>`,
  `Delegate1<SmartPtr<T>, ResourceLoader&>`
- `Delegate2<void, bool, bool>`, `Delegate2<long, ulong, bool&>`,
  `Delegate2<void, P2PMessage, NetworkPacket*>`
- `Delegate3<void, char const*, int, int>`,
  `Delegate3<void, char const*, void*, int>`,
  `Delegate3<bool, MenuButton*, float, ScreenButton&>`
- `Delegate4<bool, char const*, int, int, void*>`,
  `Delegate4<bool, char const*, long long, int, void*>`


## 9. Port-side note (informational, not a request to edit src/)

A layout-faithful port template that matches this struct without ABI compat:
```cpp
template <class Ret, class... Args>
class Delegate {
    // 32 B inline buffer
    union {
        struct {
            void* vptr;
            void* slot1;          // free-fn -> fnPtr (Global), member -> obj
            uint32_t slot2;       // member -> ptmf_lo (unused for Global)
            uint32_t slot3;       // member -> ptmf_hi (unused for Global)
            uint8_t  pad[16];     // unused trailing bytes
        };
        uint8_t buf[32];
    };
    uint8_t  usingHeap;            // 0 = inline, 1 = heap/empty
    uint8_t  pad[3];
};
static_assert(sizeof(Delegate<void>) == 36, "must match ARM binary");
```
The original binary never takes the heap path in practice — the inline buffer
is always large enough for both `Global` (8 B) and `Callee<T>` (16 B). A port
can implement this with `std::function`/lambdas internally as long as
**`sizeof == 36`** is preserved for any embedded-in-struct usage; otherwise
HUDControl/MenuButton/ScreenButton field offsets shift.


## 10. Reference function addresses

| Address | Name |
|---------|------|
| `0x00109ae8` | `StackAllocatedPointer::StackAllocatedPointer()` (default ctor) |
| `0x001a9080` | `StackAllocatedPointer::operator=(other)` |
| `0x001a90a0` | `StackAllocatedPointer::StackAllocatedPointer(other)` (copy ctor) |
| `0x00126ea0` | `StackAllocatedPointer::Delete()` |
| `0x001312b0` | `StackAllocatedPointer::Resolve() const` |
| `0x0013134c` | `Delegate1<void,HUDControl*>::Call(arg)` |
| `0x00131360` | `Delegate1<void,HUDControl*>::operator()(arg)` |
| `0x0012668c` | `Delegate1<void,HUDControl*>::BaseDelegate::BaseDelegate()` |
| `0x001264bc` | `BaseDelegate::~BaseDelegate()` (non-deleting) |
| `0x00126f94` | `BaseDelegate::~BaseDelegate()` (deleting) |
| `0x00130dac` | `MakeDelegate_DrawUtil_HUD` (binds `DefaultDeleteCallback`) |
| `0x00143f94` | `DefaultDeleteCallback(HUDControl*)` (returns arg unmodified) |
| `0x0012fbbc` | `Delegate0<void>::Callee<AboutScreen>::Callee(obj, method)` |
| `0x0012fbfc` | `Delegate0<void>::QCallee<AboutScreen>(obj, method)` |
| `0x00131044` | `Delegate1<void,HUDControl*>::Callee<ScreenButton>::Callee(other)` |
| `0x001314ac` | `StackAllocatedPointer::CopyConstruct<Callee<ScreenButton>>(src)` |
| `0x0013144c` | `Delegate1<void,HUDControl*>::Global::Call(arg)` |
| `0x00131470` | `Delegate1<void,HUDControl*>::Global::Compare(other)` |
| `0x00131418` | `Delegate1<void,HUDControl*>::Callee<ScreenButton>::Compare(other)` |
| `0x00131628` | `~ScreenButton` (canonical 5-subobject teardown) |
| `0x00130dd8` | `BaseScreen::Release` (canonical Make-and-assign caller) |
| `0x00144104` | `HUDControl::HUDControl` (canonical noop-Delegate-init caller) |

Vtables:
| Address | Class |
|---------|-------|
| `0x001e8df8` | `Delegate1<void,HUDControl*>::BaseDelegate` (abstract; pure-virtual slots) |
| `0x001e8fd8` | `Delegate1<void,HUDControl*>::Global` |
| `0x001e9008` | `Delegate1<void,HUDControl*>::Callee<ScreenButton>` |
