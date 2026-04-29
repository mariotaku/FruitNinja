<!-- Analysed: 2026-04-25T20:30 -->

# ShopScreen

## ShopScreen

**Constructor**: `0x0015cdac` -- `ShopScreen::ShopScreen(DojoScreen*)`
**Update**: `0x0015e1f4` -- `ShopScreen::Update(float)` (387 lines)
**Draw**: `0x0015dd50` -- `ShopScreen::Draw(float*)`

**Base class**: `HUDControl3d`

**Struct size**: `0xBC`

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00-0x73 | | (HUDControl3d base) | |
| 0x74 | SmartPtr\<Texture\> | m_TexInst | SetNull in ctor |
| 0x7C | float | m_TransitionAlpha | 0 in ctor; lerped toward 1 in state 0 |
| 0x80 | int | m_LayerFlagsAlt | 0x40 when no splats; 0x80 in state 4 |
| 0x84 | MenuButton* | m_pBuyButton | Back/quit button (QuitShopCallback). Created in state 0. |
| 0x88 | float | m_BuyDelay | Set to 0.0 (DAT_0015e558) in state 0 completion |
| 0x8C | MenuButton* | m_pEquipButton | Equip button (EquipCallback). Created in state 1. |
| 0x90 | DojoScreen* | m_pParent | Parent screen; used in states 2/7 to set m_bNoDestructor |
| 0x94 | ScrollingMenu* | m_pShopList | Scrollable item list |
| 0x98 | ShopListItem* | m_pSelectedItem | Currently selected list item |
| 0x9C-0xA8 | ShopListItem*[3] | m_pSlotItems | Cached per-type selection |
| 0xAC | float | m_ScrollOffset | (float)(numItems) + 0.5 |
| 0xB4 | int | m_AnimFrame | Sin-wave frame counter (0..0x3ffc) |
| 0xB8 | int | m_State | State machine index |

### Button Naming Clarification

The field named `m_pBuyButton` (field_0x84) is actually the **back/quit button** — its press
delegate calls `QuitShopCallback` which sets `m_State=2` and triggers fade-out back to the dojo.
The name "buy button" is a Ghidra artifact from early analysis; the binary stores it at field_0x84
and it sits in the same screen position (185, -105) as the dojo's own back button.

The field `m_pEquipButton` (field_0x8c) is the **equip/purchase button** — its press delegate
calls `EquipCallback` which calls `ItemManager::SetEquippedItem`.

### Resolved Constants

| Address | Value | Name | Usage |
|---------|-------|------|-------|
| DAT_0015e554 | `3f7fbe77` = 0.999f | ALPHA_IN_DONE | State 0 fade-in completion |
| DAT_0015e558 | `00000000` = 0.0f | BUY_DELAY_INIT | Also z for button positions |
| DAT_0015e55c | `43390000` = 185.0f | POS_BACK_BUTTON.x | Back/quit button X |
| DAT_0015e560 | `c2d20000` = -105.0f | POS_BACK_BUTTON.y | Back/quit button Y |
| DAT_0015e564 | `43110000` = 145.0f | POS_EQUIP_BUTTON.x | Equip button X |
| DAT_0015e568 | `42d00000` = 104.0f | POS_EQUIP_BUTTON.y | Equip button Y |
| DAT_0015e90c | `3f59999a` = 0.85f | ALPHA_DECAY_STATE27 | States 2/7 alpha decay (NOT 0.75) |
| DAT_0015e910 | `3c23d70a` = 0.01f | ALPHA_STATE27_DONE | States 2/7 trigger threshold |
| DAT_0015e914 | `3a83126f` = 0.001f | ALPHA_STATE3_DONE | State 3 completion threshold |
| DAT_0015e904 | `47d547ff` = ~109260.0f | ANIM_FRAME_RATE | Animation counter increment/sec |
| DAT_0015e908 | `467ff000` = 16380.0f | (float)ANIM_FRAME_MAX | Animation frame clamp as float |
| DAT_0015e918 | `43390000` = 185.0f | POS_BACK_BUTTON_NEW.x | State-3 replacement button X |
| DAT_0015e91c | `c2d20000` = -105.0f | POS_BACK_BUTTON_NEW.y | State-3 replacement button Y |
| DAT_0015e920 | `3f533333` = 0.825f | BUTTON_SCALE | Post-creation scale for both buttons |
| DAT_0015e93c | `00000000` = 0.0f | (z coord) | Z for state-3 button + fling vel Z |
| DAT_0015ead8 | `42200000` = 40.0f | LIST_POS_Y | Shop list Y position |
| DAT_0015eadc | `00000000` = 0.0f | LIST_POS_Z | Shop list Z position |
| DAT_0015eae0 | `42be0000` = 95.0f | LIST_SLIDE_OFF | Slide formula offset |
| DAT_0015eae4 | `43910000` = 290.0f | LIST_SLIDE_MUL | Slide formula multiplier |

State 3 fade uses literal `0.75f` in the decompile (not a DAT constant).

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Transition in: `alpha += (1-alpha)*0.125`. On alpha > 0.999: call RemoveAllSplats, set m_BuyDelay=0, state=1. If m_pBuyButton null: create back/quit MenuButton at (185,-105,0) with QuitShopCallback, add to HUD, ResetTutePos, scale by 0.825. |
| 1 | Active: decrement m_BuyDelay by dt. Check list selection changes -> SetSelected. If delay<=0 and !m_bTouchProcessed: ShrinkBuyButton. Else: check IsEquipped/IsLocked; if not equipped and no equip button: create equip MenuButton at (145,104,0) with EquipCallback, scale by 0.75. |
| 2 | Transition out (primary): `alpha *= 0.85`. When alpha < 0.01 AND parent exists: set parent m_bPendingRemoval=1 (field_0x33), self m_bPendingRemoval=1, mainScreen->SetState(STATE_SLIDE_IN). |
| 3 | Buy-anim fade: `alpha *= 0.75`. While alpha >= 0.001: fling old back-button fruit. On completion: state=4, alpha=0, create new back button, scale by 0.825. |
| 4 | Resets m_LayerFlagsAlt to 0x80. |
| 5 | Wait for ActorManager empty -> equip item via ItemManager -> state=0. |
| 6 | Same as 5. |
| 7 | Transition out (alternate): `alpha *= 0.85`. Does NOT trigger mainScreen transition. |

### Splat-Pool X-Shift on Alpha-Decrease (0x0015ea50 .. 0x0015eabe)

After the state switch and the `m_pListMenu` slide block, `Update` runs a tail loop
that nudges every active **SplatEntity** along its X axis whenever
`m_TransitionAlpha` decreased between this frame and last. **This is NOT a
`HUD::m_Controls` walk** -- the verification report hypothesis was wrong.
The list iterated is the global SplatEntity pool, the same one populated by
`SplatEntity::CreatePool` / `GetFree` / `RemoveAllSplats`.

#### Loop bounds (resolved DAT pool)

The loop pseudo-disasm at 0x0015ea54-0x0015ea60:

```
ldr r3, [0x0015eaf8]   ; r3 = 0x000079e0  (GOT offset to splat-pool ptr-of-ptr)
ldr r1, [0x0015eafc]   ; r1 = 0x000078a0  (GOT offset to splat-pool count ptr-of-ptr)
ldr r3, [r5, r3]       ; r3 = *(GOT+0x79e0) = 0x0026891C
ldr r1, [r5, r1]       ; r1 = *(GOT+0x78a0) = 0x00268920
ldr r3, [r3, #0x0]     ; r3 = *(0x0026891C) = base of SplatEntity[] array
ldr r1, [r1, #0x0]     ; r1 = *(0x00268920) = pool count
```

`0x0026891C` and `0x00268920` are exactly the `.bss` pair updated by
`SplatEntity::CreatePool` (xrefs include `RemoveAllSplats`, `DrawActiveSplats`,
`UpdateActiveSplats`, `GetFree`, `CleanUp`). Stride is **0x78 (120 bytes)** =
`sizeof(SplatEntity)`, matching `adds r3,#0x78` in the loop.

#### Per-entity filter

```
if (entity->m_bActive [+0x75] != 0  &&
    entity->m_SplatType [+0x70] >= 0)        // signed >= 0
    apply shift;
```

Both fields are part of the `SplatEntity` struct (already documented at offsets
117 and 112 in Ghidra's `SplatEntity` layout).

#### Alpha delta (NOT stored on the screen)

`prevAlpha` is **not** a struct field. It is the value of `this->field_0x7c`
captured into `s18` at function entry (`vldr.32 s18,[r0,#0x7c]` at 0x0015e1fe)
and saved by the `vpush {d8,d9}` prologue. The loop is gated by:

```
if (m_TransitionAlpha [+0x7c] < prevAlpha_at_entry)   // s18
```

i.e. the loop only fires when alpha **decreased** between the value at function
entry and the value after the state-machine ran. That happens in:

- **State 2 / State 7**: `alpha *= 0.85` (always decreases while alpha > 0).
- **State 3**: `alpha *= 0.75` (always decreases). However, state 3 also
  re-creates the back button via `LAB_0015e874` and resets alpha to
  `DAT_0015e93c = 1.0` on the same frame -- in that frame, alpha increases,
  so the loop does not fire. Other state-3 frames do fire it.
- **State 0**: `alpha += (1-alpha)*0.125` -- monotonic increase, never fires.
- **States 1/4/5/6**: alpha unchanged -- never fires.

Net effect: the loop is the **fade-out splat-shift** for transitions 2/7 (and
the non-spawning frames of 3).

The delta used is computed in *complement-of-alpha* space:

```
fade_delta = (1 - m_TransitionAlpha_now) - (1 - prevAlpha_at_entry)
           = prevAlpha_at_entry - m_TransitionAlpha_now    // strictly positive when loop fires
```

(Confirmed by 0015ea62 `vsub s15, s14, s15` (1 - now), 0015ea66
`vsub s18, s14, s18` (1 - prev), 0015ea6a `vsub s18, s15, s18`
((1-now) - (1-prev)) = prev - now.)

#### X threshold + multipliers (resolved DAT pool 0x0015eae0..0x0015eaec)

Read of memory at those addresses (little-endian floats):

| Address | Bytes | Value | Used as |
|---|---|---|---|
| `DAT_0015eae0` | `00 00 be 42` | **95.0f** | Slide-block constant for `m_pListMenu` (x-base offset, see below) |
| `DAT_0015eae4` | `00 00 91 43` | **290.0f** | Slide-block + splat shift (down-mult) |
| `DAT_0015eae8` | `00 00 3e 43` | **190.0f** | Splat shift (up-mult) |
| `DAT_0015eaec` | `00 00 48 42` | **50.0f** | Splat-X threshold |
| `DAT_0015eaf0` | `90 79 00 00` | GOT off `0x7990` | Game-singleton (states 5/6) -- unrelated |
| `DAT_0015eaf4` | `b0 79 00 00` | GOT off `0x79b0` | Slide-block target ptr -- unrelated |
| `DAT_0015eaf8` | `e0 79 00 00` | GOT off `0x79e0` | **Splat-pool array base** ptr-of-ptr |
| `DAT_0015eafc` | `a0 78 00 00` | GOT off `0x78a0` | **Splat-pool count** ptr-of-ptr |

Note that 290 and 190 are the same constants used in the Block-A1 and
`m_pListMenu` slide formulas elsewhere in the file (DAT_0015e058, DAT_0015e054
pair). 290 is also `DAT_0015eae4` reused for both the m_pListMenu slide
(`(1-alpha)*290*-1.5 - 95` written into m_pListMenu->pos.x at 0x0015ea0e) and
the splat down-multiplier in this loop.

#### Sign / direction (axis is **m_Pos_x**, which is screen-vertical)

The loop reads/writes `*(float*)(splat + 0x38)`, which per the Ghidra
`SplatEntity` layout is **`m_Pos_x`** (offset 56 = 0x38). Recall the engine's
ortho frame: **X = +160 (top of screen) .. -160 (bottom)** (CLAUDE.md). So
shifting `m_Pos_x` is the on-screen vertical motion of the splat.

```
fade_delta = prevAlpha - alpha     // > 0 in this branch
mul_down   = fade_delta * 290.0f * 1.5f
mul_up     = fade_delta * 190.0f * 1.5f

if (splat.m_Pos_x > 50.0f)         // upper half of screen
    splat.m_Pos_x += mul_up        // move further toward +X (further up)
else
    splat.m_Pos_x -= mul_down      // move further toward -X (further down)
```

i.e. as the shop fades out, splats above x=50 drift up and splats at/below x=50
drift down -- the entire splat layer parts away from the centerline so the
underlying screen behind the shop becomes visible cleanly.

#### Exact pseudocode

```c
float prevAlpha = this->m_TransitionAlpha;   // captured at function entry
/* ... state machine runs and may mutate this->m_TransitionAlpha ... */
/* ... m_pListMenu slide block ... */

if (this->m_TransitionAlpha < prevAlpha) {
    SplatEntity* splat = SplatEntity::s_Pool;     // **(int*)(GOT+0x79e0)
    int          count = SplatEntity::s_PoolSize; // **(int*)(GOT+0x78a0)

    float delta = prevAlpha - this->m_TransitionAlpha; // > 0
    float down  = delta * 290.0f * 1.5f;
    float up    = delta * 190.0f * 1.5f;

    for (int i = 0; i < count; i++, splat = (SplatEntity*)((char*)splat + 0x78)) {
        if (!splat->m_bActive)        continue;   // +0x75
        if ( splat->m_SplatType < 0)  continue;   // +0x70  (signed)

        if (splat->m_Pos_x > 50.0f)
            splat->m_Pos_x += up;     // +0x38
        else
            splat->m_Pos_x -= down;
    }
}
```

#### Port implications

- The current port stub `(void)prevAlpha;` is fine *only* if the port has no
  splats remaining when the shop transitions out. State 0 calls
  `SplatEntity::RemoveAllSplats` on entry, so under normal flow the splat pool
  is empty during 2/7 and the visible effect is nil. **However** if the port
  ever enters Shop with leftover splats (e.g. quick re-entry, debug paths),
  this loop is what scatters them off-screen during fade-out. Implement against
  the SplatEntity pool, not against `game.hud->m_Controls`.
- Replicating this requires (a) a way to iterate the SplatEntity pool, (b)
  fields `m_Pos_x`, `m_bActive`, `m_SplatType` (all already exposed in the
  Ghidra struct).

### State=8 Write (GameTaskState Transition)

Binary at `0x0015e28c` (in state 2 branch):
```
*(undefined4*)(*(int*)(*(int*)(iVar14 + DAT_0015e924) + 0x160) + 0x10c) = 8;
```
Where:
- `DAT_0015e924` = `90 79 00 00` = 0x7990 — GOT offset to the GameTask/Game object
- `+0x160` = `mainScreen` field in `Game`
- `+0x10c` = `m_State` field in `MainScreen`
- `8` = `STATE_SLIDE_IN`

Port: `game.mainScreen->SetState(STATE_SLIDE_IN)` in state 2 only (not state 7).

Also note: the binary writes `*(parent + 0x33) = 1` which in HUDControl layout is
`m_bPendingRemoval` (NOT `m_bNoDestructor` which lives at +0x32). Both parent and
self get `m_bPendingRemoval = 1` — both are removed from the HUD in the next Update.

### Back/Quit Button Creation (State 0)

Binary in `ShopScreen::Update` at `~0x0015e2dc`:
```
QCallee<ShopScreen>(auStack_104, QuitShopCallback)  // QuitShopCallback fn-ptr via GOT[0x78EC]=0x0015D55C (DAT_0015e578)
Delegate0::Delegate0(&DStack_178, auStack_104)      // wraps as press delegate
SmartPtr<Texture>::SmartPtr(&SStack_2c, GameTask + 0x17c)  // texture from GameTask
_Vector3<float>::_Vector3(&Stack_88, 185.0, -105.0, 0.0)   // DAT_0015e55c/560/558
MenuButton::MenuButton(pMVar7, &SStack_2c, &Stack_88, &DStack_178,
                       **(int**)(GOT+0x7060),       // fruitType = s_FruitInfoCount (DAT_0015e580)
                       &Stack_94, &DStack_19c)
HUD::AddControl(GameTask->hud, pMVar7, false)
TutorialControl::ResetTutePos(GameTask->tutorialCtrl, pMVar7)
// LAB_0015e874:
pMVar7->m_TargetSize *= 0.825  // DAT_0015e920
pMVar7->m_pFruitPiece->scale *= 0.825
```

**fruitType resolution (verified 2026-04-29)**:

The disassembly at 0x0015e32e shows:
```
ldr.w r12, [0x0015e580]   ; r12 = 0x7060 (GOT offset)
ldr.w r7,  [r5, r12]      ; r7  = *(GOT_BASE + 0x7060) = pointer to int (s_FruitInfoCount)
ldr   r7,  [r7, #0x0]     ; r7  = *r7 = the fruit count value
str   r7,  [sp, #0x0]     ; pass as 5th arg to MenuButton ctor
```

- `DAT_0015e580` = `0x00007060` — GOT offset to the `s_FruitInfoCount` global pointer
- `*(GOT + 0x7060)` = `0x0024D754` (in `.bss`) — pointer to int holding fruit count
- `**(int**)(GOT+0x7060)` = current fruit count (incremented per `<fruit>` XML element in `Fruit::LoadInfo` at 0x00179a6e–0x00179a7e). This is exactly what the port's `FruitInfo_GetCount()` returns.
- `DAT_0015e578` = `0x000078EC` is NOT the fruit type — it's the GOT offset to the `QuitShopCallback` function pointer (`GOT[0x78EC]` = `0x0015D55C` = `ShopScreen::QuitShopCallback`), used by `QCallee<ShopScreen>` to wire the press delegate.

**Port correctness**: `FruitInfo_GetCount()` in `src/entities/FruitInfo.cpp` returns
`s_FruitInfoCount`, which matches the binary's `**(int**)(GOT+0x7060)` exactly. The
back/quit button is correctly the bomb-threshold sentinel — same pattern as Dojo and
About back-buttons. **No fix needed.**

### Dependency Audit

| Dependency | Exists in src/? | Status | Required for back-button flow? |
|---|---|---|---|
| `ItemManager::GetInstance` / `GetNumItems` / `IsEquipped` / `SetEquippedItem` | Yes (`game/ItemManager.cpp`) | Stubbed — returns 0 / no-op | Not required (stubs keep logic paths intact) |
| `ScrollingMenu` (`AddItem`, `GetNumItems`, `GetItemClosestToZero`) | Yes (`hud/ScrollingMenu.cpp`) | Functional stub — items addable, scroll not interactive | Not required for quit flow |
| `ScrollingMenuItem` (base of ShopListItem) | Yes (`hud/ScrollingMenuItem.cpp`) | Functional | No |
| `ShopListItem` (`m_pItemInfo`, `m_Alpha`) | Yes (`hud/ShopListItem.cpp`) | Functional stub | No |
| `FruitSaveData::CheckDatesHaveChanged` | Yes (`game/FruitSaveData.cpp`) | Stub (empty body) | No (called by DojoScreen before push) |
| `SplatEntity::NumActiveSplats` / `RemoveAllSplats` | Partial — SplatEntity.h/cpp exists, but these static wrappers not exposed | Missing stubs | No (always-0 fallback OK) |
| `TutorialControl::ResetTutePos(MenuButton*)` | Yes (`hud/TutorialControl.cpp`) | Functional | YES — called in state 0 creation and QuitShopCallback |
| `Fruit::FruitType(name, false)` | Yes (`entities/Fruit.cpp`) | Functional | YES — used in state 1 equip button |
| `Fruit::SetFruitType` | Yes (`entities/Fruit.cpp`) | Functional | No (needed for SetSelected display only) |
| `Fruit::RotateFacingUp` | Yes (`entities/Fruit.h`) | Functional | No |
| `Fruit::m_bSliced` / `Fruit::m_bDetached` | Yes (`entities/Fruit.h`) | Functional | YES — used in QuitShopCallback fling |
| `LowResBackgrounds` flag | Not a separate function — equivalent is LoadContent conditional | Stubbed as false | No |
| `Mortar::TextureManager::LoadLocalisedTexture` | Yes (`asset/TextureManager`) | Functional | YES — for static textures in LoadContent |
| `MenuButton` ctor + `m_pFruitPiece` / `m_TargetSize` | Yes (`hud/MenuButton.cpp`) | Functional | YES — back button uses it |
| `GameSound::SFXPlay` | Yes (`engine/audio/GameSound.h`) | Functional (audio backend stubbed) | No (graceful no-op if null) |
| `HUD::AddControl` | Yes (`hud/HUD.h`) | Functional | YES — back button added to HUD |
| `ActorManager::GetNumEntities` | Yes (`entities/ActorManager`) | Functional | No (only states 5/6) |
| `ItemInfo::IsLocked` | Yes (`game/ItemInfo.h`) | Functional | No |
| `MainScreen::SetState(STATE_SLIDE_IN)` | Yes (`screens/MainScreen.h`) | Functional | YES — the state=8 transition |

**User-visible flow (transition in + back button + transition out) requires**:
`MenuButton`, `HUD::AddControl`, `TutorialControl::ResetTutePos`, `Fruit::FruitType`,
`FruitInfo_GetCount`, `MainScreen::SetState`. All are present and functional.

---

---

## Draw Function (0x0015dd50)

<!-- Analysed: 2026-04-25T18:15 -->

### Function Header

| Field | Value |
|-------|-------|
| Address | `0x0015dd50` |
| End | `0x0015e1d8` (pop.w at `0x0015e1d6`) |
| Size | ~0x488 bytes |
| Instruction count | ~190 instructions |
| Signature | `void ShopScreen::Draw(ShopScreen* this, float* layers)` |
| Calling convention | `__thiscall` (r0 = this, r1 = layers; `layers` param is NEVER USED in the body) |
| Stack frame | `sub sp,#0x198` — 408 bytes of locals (9 Matrix44s + 9 Vec3s + 6 Colours + 2 SmartPtrs) |

The `layers` parameter (r1) is loaded but discarded. The binary does NOT perform the standard `(*hudScale_int & m_LayerFlags) == 0` early-return. Instead, it uses a different guard (see top-level control flow below).

---

### Static Block Layout (GOT-relative)

Draw navigates to the ShopScreen static texture block via:

```
GOT_base = DAT_0015e07c + 0x15dd60
         = 0x0008e3d0 + 0x0015dd60 = 0x001ec130

static_block_base = GOT_base + DAT_0015e080
                  = 0x001ec130 + 0x000451b4   (runtime BSS)
```

`DAT_0015e080 = DAT_0015e1ec = 0x000451b4` — both decompile aliases point to the same block.

**Corrected static slot assignments** (verified from LoadContent `0x0015cb08` disasm + string search):

| Slot offset | DAT_string | String address | Texture file |
|-------------|-----------|----------------|--------------|
| `+0x14` | DAT_0015ccb8 = `0xfffd002e` | `0x001bc15e` | `locked.tex` |
| `+0x18` | DAT_0015ccbc = `0xfffd0039` | `0x001bc169` | `select_item.tex` |
| `+0x2c` | DAT_0015cca8 = `0xfffcf054` | `0x001bb184` | `loading.tex` (not a shop tex; slot unused by Draw) |
| `+0x30` | DAT_0015ccb0 = `0xfffd0005` | `0x001bc135` | `scratch_deviders.tex` |
| `+0x34` | DAT_0015ccb4 = `0xfffd001a` | `0x001bc14a` | `dialog_box_shop.tex` |
| `+0x38` | DAT_0015ccc0 = `0xfffd0049` | `0x001bc179` | `selected.tex` |
| `+0x3c` | DAT_0015ccc4 = `0xfffd0056` | `0x001bc186` | `selected_sml.tex` |
| `+0x40` | DAT_0015ccc8 = `0xfffd0067` | `0x001bc197` | `locked_stroke.tex` |
| `+0x44` | DAT_0015cccc = `0xfffd0079` | `0x001bc1a9` | `new_item_sml.tex` |
| `+0x48` | DAT_0015ccd0 = `0xfffd008a` | `0x001bc1ba` | `BG_store.tex` (non-low-res) |
| `+0x48` | DAT_0015ccd4 = `0xfffd0097` | `0x001bc1c7` | `BG_store_sml.tex` (low-res) |
| `+0x4c` | — | — | `s_bContentLoaded` flag (1 byte, set last in LoadContent) |
| `+0x74` | — | — | `__cxa_guard` flag for one-time Vec3 cache (Block B) |
| `+0x78` | — | — | Cached `Vec3(selected.tex_w+1, selected.tex_h+1, 1.0f)` (Block B) |
| `+0x84` | — | — | `float dial_alpha` — dialog box grayscale alpha (Block A) |

**Note for port maintainers**: The header file `ShopScreen.h` and the port source `ShopScreen.cpp` have the slot names wrong:
- Port says `+0x2c = dialog_box_shop.tex`, `+0x34 = locked_stroke.tex`, `+0x38 = scratch_deviders.tex`. **These are incorrect.**
- Correct per binary: `+0x34 = dialog_box_shop.tex`, `+0x38 = selected.tex`, `+0x30 = scratch_deviders.tex`. The `static SmartPtr` members in the port need to be re-ordered to match the binary's actual slot addresses.

**Textures used by Draw** (only 3 of the 10 slots are accessed):

| Slot | Texture | Used in |
|------|---------|---------|
| `+0x34` | `dialog_box_shop.tex` | Block A: info dialog panel render |
| `+0x38` | `selected.tex` | Block B: animated selection ring |
| `+0x48` | `BG_store.tex` | Block A: background panel (both A1 and A2) |

Slots `+0x14`, `+0x18`, `+0x2c`, `+0x30`, `+0x3c`, `+0x40`, `+0x44` are NOT accessed by Draw.

---

### Colour Constant

Both Block A and Block B read their render colour from:

```
colour_ptr = *(Colour**)(GOT_base + DAT_0015e08c)
           = *(Colour**)(0x001ec130 + 0x000073a4)
           = *(Colour**)(0x001f34d4)   [runtime GOT entry]
```

This resolves at runtime to a `Colour` object (4 bytes RGBA). It is populated by the engine and assumed to be `{0xFF, 0xFF, 0xFF, 0xFF}` (white full-alpha). The implementer should pass white/full-alpha to all quads and verify visually.

---

### Helper Functions Called by Draw

All three are file-local stubs that wrap the global `MatrixManager` and `Mesh`:

| Function | Address | What it does |
|----------|---------|--------------|
| `ResetMatrix_GameTask` | `0x0015c6d4` | `MatrixStack::Reset(game->matrixStack)` |
| `SetMatrix_GameTask(Matrix44*)` | `0x0015c6b0` | `MatrixStack::SetCurrentMatrix(game->matrixStack, mat)` |
| `UploadMatrices_GameTask` | `0x0015c694` | `MatrixManager::UploadCurrentMatrices(game->matrixManager, true)` |
| `DrawQuadSized_GameTask(float u0, float u1, Colour*)` | `0x0015c710` | Copies colour; calls `Mesh::DrawQuadUnCached(colour, u0, u1, 0.1875f, 0.8125f, nullptr)` |
| `DrawQuad_GameTask(Colour*)` | `0x0015c6f4` | Copies colour; calls `Mesh::DrawQuadUnCached(colour, (Colour)0, nullptr)` — full quad, default UVs |

`DrawQuadSized_GameTask` passes hardcoded `v0=0.1875f, v1=0.8125f` regardless of caller. The `u0`/`u1` parameters crop the horizontal UV range.

---

### Top-Level Control Flow

```
Draw(this, layers):
    iVar3 = DAT_0015e080          // = 0x000451b4 (static block GOT offset)
    GOT_base = DAT_0015e07c + 0x15dd60
    
    if (this->m_LayerFlags == this->m_LayerFlagsAlt):    // field_0x34 == field_0x80
        goto Block_A
    elif (this->m_AnimFrame > 0):                        // field_0xb4 > 0
        goto Block_B
    else:
        return
```

This is NOT the standard `(layers & m_LayerFlags) == 0` HUD mask check. The guard is a structural flag check:
- `m_LayerFlags` (offset `0x34`) is initialized to `0x80` in the ctor.
- `m_LayerFlagsAlt` (offset `0x80`) starts at `0`, becomes `0x40` after state 0, becomes `0x80` in state 4.
- `Block A` fires only once when both equal `0x80` (state 4). Immediately upon entry, Block A sets `m_LayerFlags = 1`, so subsequent frames fall through to Block B.
- `Block B` fires every frame as long as `m_AnimFrame > 0`.

So the render lifecycle is:
1. State 4 entry: `m_LayerFlagsAlt = 0x80`, `m_LayerFlags = 0x80` → Block A fires once (draws static BG + dialog box), resets `m_LayerFlags = 1`.
2. Thereafter: `1 != 0x80` → check `m_AnimFrame > 0` → Block B fires (draws animated ring) each frame.
3. When `m_AnimFrame == 0`: both blocks skip → nothing drawn.

---

### Block A — One-Shot BG + Dialog Box (0x0015ded8 .. 0x0015e1cd)

**Trigger**: `m_LayerFlags (0x34) == m_LayerFlagsAlt (0x80)` — fires exactly once per shop visit.

**First action** (before any render):
```c
this->m_LayerFlags = 1;   // at 0x0015dee6 — marks "Block A already ran"
```

Block A then branches on `m_TransitionAlpha`:

#### Sub-Block A1 — Sliding BG + ring (when m_TransitionAlpha < 1.0)

Addresses: `0x0015def6 .. 0x0015dff9`

```
1. Texture::Set(static_block[+0x48])          // BG_store.tex
2. Scale Vec3 = (291.0f, 321.0f, 0.0f)        // DAT_0015e064, DAT_0015e068, DAT_0015e05c
   Matrix44 scale = Scale44(291, 321, 0)
3. Translate Vec3 = (m_pShopList->field_0x8, 0.0f, 0.0f)
   // m_pShopList is at this->field_0x94; field_0x8 of ScrollingMenu = scroll X pos
   GlobalTranslate44(scroll_x, 0, 0) onto scale matrix
4. ResetMatrix / SetMatrix(scale+translate) / UploadMatrices
5. Colour = copy of *(colour_ptr from GOT + 0x000073a4)
6. DrawQuadSized_GameTask(0.03125f, 0.597656f, colour)
   // UV: u=[1/32, ~19/32], v=[3/16, 13/16] — LEFT half of BG texture

7. Compute slide_X:
   slide_X = DAT_0015e054 + (1.0f - m_TransitionAlpha) * DAT_0015e058 * 1.5f
           = 145.0f + (1.0f - alpha) * 190.0f * 1.5f
   // At alpha=0: slide_X = 145 + 285 = 430.  At alpha=1: slide_X = 145.
8. Scale Vec3 = (191.0f, 321.0f, 0.0f)        // DAT_0015e074, DAT_0015e068, DAT_0015e05c
   Matrix44 scale = Scale44(191, 321, 0)
9. Translate Vec3 = (slide_X, 0.0f, 0.0f)
   GlobalTranslate44(slide_X, 0, 0) onto matrix
10. ResetMatrix / SetMatrix / UploadMatrices
11. Colour = copy of *(colour_ptr from GOT + 0x000073a4)
12. DrawQuadSized_GameTask(0.597656f, 0.9375f, colour)
    // UV: u=[~19/32, 31/32], v=[3/16, 13/16] — RIGHT half of BG texture

13. Texture::UnSet(static_block[+0x48])       // UnSet BG_store.tex
```

NOTE: The BG is drawn in **two separate quads** cropping U=[0.03125..0.597656] and U=[0.597656..0.9375] respectively. The two draws use different translation X values — the first is anchored to the scroll position, the second slides in from the right. The V range [0.1875, 0.8125] is hardcoded inside `DrawQuadSized_GameTask`.

#### Sub-Block A2 — Static full BG (when m_TransitionAlpha >= 1.0)

Addresses: `0x0015dffe .. 0x0015e08f`

```
1. Scale Vec3 = (481.0f, 321.0f, 0.0f)        // DAT_0015e078, DAT_0015e068, DAT_0015e05c
   Matrix44 = Scale44(481, 321, 0)
   (no Translation — pure scale, quad centered at origin)
2. ResetMatrix / SetMatrix(scale_only) / UploadMatrices
3. Texture::Set(static_block[+0x48])           // BG_store.tex
4. Colour = copy of *(colour_ptr from GOT + 0x000073a4)
5. DrawQuadSized_GameTask(0.03125f, 0.9375f, colour)
   // UV: u=[1/32, 31/32], v=[3/16, 13/16] — full BG in one draw
   // slide_X stored as 145.0f for use by Block A3 below

6. Texture::UnSet(static_block[+0x48])         // UnSet BG_store.tex
```

#### Sub-Block A3 — Dialog Box (runs after BOTH A1 and A2)

Addresses: `0x0015e09e .. 0x0015e1cd`

This sub-block runs regardless of which sub-block (A1 or A2) preceded it. `fVar5` holds the final slide_X (from A1) or `145.0f` (from A2).

```
1. Get dialog box texture dimensions:
   tex_w = static_block[+0x34]->vtable->GetWidth()   // dialog_box_shop.tex width
   tex_h = static_block[+0x34]->vtable->GetHeight()  // dialog_box_shop.tex height

2. Scale Vec3 = (float(tex_w + 1), float(tex_h + 1), 0.0f)
   // DAT_0015e1e0 = 0.0f for z
   local_44 = 1.0f (pushed to stack before Vec3::operator* call)
   Vec3 = Vec3(tex_w+1, tex_h+1, 0) * local_44    // multiply by 1.0 = no change
   Matrix44 = Scale44(from_that_Vec3)

3. Translate:
   GlobalTranslate44(fVar5 - 4.0f, -3.0f, 0.0f)
   // 4.0f hardcoded (0x40800000); -3.0f hardcoded (0xc0400000)

4. ResetMatrix / SetMatrix(scale+translate) / UploadMatrices

5. Compute dialog_alpha:
   is_locked = m_pSelectedItem->field_0x278->ItemInfo::IsLocked()
   // m_pSelectedItem = this->field_0x98 (ShopListItem*)
   // field_0x278 = ItemInfo* within ShopListItem
   
   dt = *(float*)(*(Game_ptr_from_GOT_0x7990) + 0x38)
   // DAT_0015e1f0 = 0x00007990 — GOT offset to Game* pointer
   // Game + 0x38 = dt field (same offset as in Update's DAT_0015e924 = 0x7990)
   
   if is_locked:
       dial_alpha = static_block[+0x84] + dt * 5.0f   // fade IN toward 1.0
       if dial_alpha > 1.0:  dial_alpha = 1.0
   else:
       dial_alpha = static_block[+0x84] + dt * (-5.0f) // fade OUT toward 0.0
       if dial_alpha < 0.0:  dial_alpha = 0.0          // clamp (ARM idiom: fVar7 = DAT_0015e1e0 = 0.0f)
   static_block[+0x84] = dial_alpha

6. Compute grayscale:
   r_float = DAT_0015e1e8 + DAT_0015e1e4 * dial_alpha
           = 255.0f     + (-120.0f)    * dial_alpha
   // dial_alpha=0 (unlocked): r = 255 (white)
   // dial_alpha=1 (locked):   r = 255 - 120 = 135 (medium gray)
   r = (r_float > 0.0f) ? (uint8_t)(int)r_float : 0
   colour = Colour(r, r, r, 0xFF)

7. Texture::Set(static_block[+0x34])           // dialog_box_shop.tex
8. DrawQuad_GameTask(colour)
   // Full-quad draw (no UV crop), all channels = same r value, alpha = 0xFF
9. Texture::UnSet(static_block[+0x34])         // UnSet dialog_box_shop.tex
```

---

### Block B — Animated Selection Ring (0x0015dd78 .. 0x0015ded4)

**Trigger**: `m_LayerFlags (0x34) != m_LayerFlagsAlt (0x80)` AND `m_AnimFrame (0xb4) > 0`.

This block renders every frame when the shop is in normal operating state (states 0, 1, 2, 3, 7).

#### One-Time Cache Init (guarded by `__cxa_guard`)

```
if (static_block[+0x74] not yet initialized):
    __cxa_guard_acquire(static_block + 0x74)
    tex_w = static_block[+0x38]->vtable->GetWidth()     // selected.tex width
    tex_h = static_block[+0x38]->vtable->GetHeight()    // selected.tex height
    static_block[+0x78] = Vec3(float(tex_w+1), float(tex_h+1), 1.0f)
    // 1.0f comes from local_44 = 0x3f800000 pushed to stack
    __cxa_guard_release(static_block + 0x74)
    __aeabi_atexit(static_block+0x78, dtor, dso_handle)  // register cleanup
```

This runs exactly once per process. The Vec3 at `static_block[+0x78]` caches the selected.tex dimension-based scale.

#### Slide X Computation

```
if m_TransitionAlpha < 1.0:
    slide_X = DAT_0015e054 + (1.0f - m_TransitionAlpha) * DAT_0015e058 * 1.5f
            = 145.0f + (1.0f - alpha) * 190.0f * 1.5f
else:
    slide_X = DAT_0015e054 = 145.0f
```

Identical formula to Block A sub-block A1.

#### Ring Draw

```
1. Copy Vec3 scale from static_block[+0x78]  (= {tex_w+1, tex_h+1, 1.0f})

2. if m_AnimFrame < 0x3ffc (= 16380):
       sin_ratio = Math::SinIdx((uint16_t)m_AnimFrame) / Math::SinIdx(0x3ffc)
       Vec3 *= sin_ratio
   // This creates a grow-in pulse: sin_ratio goes 0→1 as m_AnimFrame 0→0x3ffc.
   // SinIdx(0x3ffc) ≈ sin(π/2) ≈ 1.0, so denominator ≈ 1.0.

3. Matrix44 = Scale44(from_Vec3)

4. GlobalTranslate44(slide_X, DAT_0015e060, 0.0f)
   //                                        = 104.0f
   // Positions the ring at (slide_X, 104, 0)

5. ResetMatrix / SetMatrix(scale+translate) / UploadMatrices

6. SmartPtr<Texture> temp_ptr = static_block[+0x38]  // selected.tex SmartPtr copy
   Texture::Set(temp_ptr.ptr)

7. Colour = copy of *(colour_ptr from GOT + 0x000073a4)
8. DrawQuad_GameTask(colour)
   // Full-quad, same colour as Block A quads

9. Texture::UnSet(temp_ptr.ptr)
10. SmartPtr dtor (releases temp_ptr ref)
```

---

### DAT Constants Table (Draw Function)

All values resolved via `read_memory` from the binary.

| Address | Raw bytes (LE) | Float/int value | Role in Draw |
|---------|---------------|-----------------|--------------|
| DAT_0015e054 | `00 00 11 43` | **145.0f** | Base slide X (rest position, also equip-button Y from Update) |
| DAT_0015e058 | `00 00 3e 43` | **190.0f** | Slide X multiplier: `(1-alpha)*190*1.5` |
| DAT_0015e05c | `00 00 00 00` | **0.0f** | Z coordinate / Vec3 padding |
| DAT_0015e060 | `00 00 d0 42` | **104.0f** | Ring translate Y (Block B) |
| DAT_0015e064 | `00 80 91 43` | **291.0f** | BG scale X in A1 (tex_w+1 for BG_store.tex) |
| DAT_0015e068 | `00 80 a0 43` | **321.0f** | BG scale Y (tex_h+1 for BG_store.tex, shared by A1/A2/B) |
| DAT_0015e06c | `00 00 00 3d` | **0.03125f** | u0 for DrawQuadSized left-quad (= 1/32) |
| DAT_0015e070 | `00 00 19 3f` | **0.597656f** | u1/u0 split point between left and right BG quads |
| DAT_0015e074 | `00 00 3f 43` | **191.0f** | BG right-quad scale X in A1 |
| DAT_0015e078 | `00 80 f0 43` | **481.0f** | BG full-quad scale X in A2 (entire width+1) |
| DAT_0015e07c | `d0 e3 08 00` | **0x0008e3d0** (GOT delta) | PC-relative offset to compute GOT base |
| DAT_0015e080 | `b4 51 04 00` | **0x000451b4** (GOT offset) | Offset from GOT base to static texture block |
| DAT_0015e08c | `a4 73 00 00` | **0x000073a4** (GOT offset) | Offset to Colour* for all quads |
| DAT_0015e1dc | `00 00 11 43` | **145.0f** | Slide X value stored in A2 (same as DAT_0015e054; separate read) |
| DAT_0015e1e0 | `00 00 00 00` | **0.0f** | Z for dialog translate; also 0-clamp for dial_alpha |
| DAT_0015e1e4 | `00 00 f0 c2` | **-120.0f** | dial_alpha multiplier in grayscale formula |
| DAT_0015e1e8 | `00 00 7f 43` | **255.0f** | dial_alpha base in grayscale formula |
| DAT_0015e1ec | `b4 51 04 00` | **0x000451b4** (GOT offset) | Same as DAT_0015e080; second alias for static block |
| DAT_0015e1f0 | `90 79 00 00` | **0x00007990** (GOT offset) | Offset to Game* pointer (for dt at Game+0x38) |
| `0x40800000` (literal) | — | **4.0f** | Dialog translate X subtractor (`fVar5 - 4.0`) |
| `0xc0400000` (literal) | — | **-3.0f** | Dialog translate Y |
| `0x3f780000` (literal) | — | **0.96875f** | u1 for A2 full-BG DrawQuadSized and A1 right-quad |
| `0x3f800000` (literal) | — | **1.0f** | Vec3 z-component for cached ring scale; alpha clamp upper |

---

### Blend Mode / Texture Unit State

No explicit `Mortar::ChangeBlendMode` or `EnableTextureUnit` calls are made by Draw. The binary uses only `Texture::Set` / `Texture::UnSet` (single texture unit). No multi-texturing, no blend mode changes, no scissor regions.

The port can use a single GL_TEXTURE_2D bind for each quad as in AboutScreen::Draw. Alpha blending (GL_BLEND) should already be enabled by the engine's frame init before Draw is called.

---

### Cross-References / Shared Helpers

- `Math::SinIdx(uint16_t)` at `0x000fc858` — returns `float` sin approximation from a 14-bit angle (0x0000..0x3fff = 0..2π). Used only in Block B when `m_AnimFrame < 0x3ffc`.
- `_Vector3::operator*=(Vec3*, float)` at `0x000fb6ac` — scale Vec3 in-place. Used in Block B to apply sin_ratio to cached scale.
- `_Vector3::operator*(Vec3_out*, float_ptr)` at `0x001028b8` — multiply Vec3 by scalar from pointer. Used in A3 for the `local_44=1.0f` multiply (identity operation).
- `_Matrix44::GlobalTranslate44(Matrix44*, float tx, float ty, float tz)` at `0x00107b80` — computes T*M (translate then existing scale).
- `_Matrix44::Scale44(Matrix44*, float sx, float sy, float sz)` at `0x000f3ac8` — builds pure scale matrix.
- `__cxa_guard_acquire` / `__cxa_guard_release` — C++ one-time-init guards (Block B cache).
- `__aeabi_atexit` at `0x000fa3b0` — registers Vec3 destructor for cleanup at process exit. The implementer does NOT need to call this; in the port, the static Vec3 is a member of `ShopScreen`.

---

### Implementer Checklist

Execute in this exact order to achieve a binary-faithful Draw:

**Setup (every Draw call):**
1. Compute `GOT_base` equivalent as `ShopScreen::s_static_block_ptr` (already handled by C++ static members in port).
2. Read `m_LayerFlags (field_0x34)` and `m_LayerFlagsAlt (field_0x80)`. Check `m_LayerFlags == m_LayerFlagsAlt`.

**Block A path (`m_LayerFlags == m_LayerFlagsAlt`):**
3. Immediately set `m_LayerFlags = 1` (to prevent re-entry next frame).
4. Read `m_TransitionAlpha`.
5. **If `m_TransitionAlpha < 1.0f` (Sub-block A1):**
   a. `Texture::Set(s_TexBGStore)`
   b. Build Scale44 with Vec3(291, 321, 0)
   c. Read `m_pShopList->pos.x` (or the field at `m_pShopList + 0x8`), build Translate with `(scroll_x, 0, 0)`
   d. `Reset / Set / Upload` matrices
   e. `DrawQuadSized(u0=0.03125f, u1=0.597656f, colour_white)`
   f. Compute `slide_X = 145.0f + (1.0f - alpha) * 190.0f * 1.5f`
   g. Build Scale44 with Vec3(191, 321, 0)
   h. Build Translate with `(slide_X, 0, 0)`
   i. `Reset / Set / Upload` matrices
   j. `DrawQuadSized(u0=0.597656f, u1=0.96875f, colour_white)`
   k. `Texture::UnSet(s_TexBGStore)`
   l. Save `slide_X` for A3.
6. **Else `m_TransitionAlpha >= 1.0f` (Sub-block A2):**
   a. Build Scale44 with Vec3(481, 321, 0) — pure scale, no translate
   b. `Reset / Set / Upload` matrices
   c. `Texture::Set(s_TexBGStore)`
   d. `DrawQuadSized(u0=0.03125f, u1=0.96875f, colour_white)`
   e. `Texture::UnSet(s_TexBGStore)`
   f. Set `slide_X = 145.0f` for A3.
7. **Sub-block A3 (always, after A1 or A2):**
   a. Read `s_TexDialogBox` width and height via SmartPtr vtable calls.
   b. Build Scale44 with Vec3(w+1, h+1, 0) multiplied by 1.0f (identity, keep as-is).
   c. Build Translate44 with `(slide_X - 4.0f, -3.0f, 0.0f)`.
   d. `Reset / Set / Upload` matrices.
   e. Compute `dt = game.dt` (from `Game + 0x38`).
   f. Compute `is_locked = m_pSelectedItem->m_pItemInfo->IsLocked()`.
      (If `m_pSelectedItem` is null, treat as not-locked / dial_alpha = 0.)
   g. Update `s_static_dial_alpha`:
      - If locked: `dial_alpha = min(dial_alpha + dt * 5.0f, 1.0f)`
      - If not locked: `dial_alpha = max(dial_alpha + dt * (-5.0f), 0.0f)`
   h. Compute `r_float = 255.0f + (-120.0f) * dial_alpha`. Clamp to `[0, 255]`.
   i. `r = (uint8_t)(int)r_float`
   j. Set colour = `Colour(r, r, r, 0xFF)`.
   k. `Texture::Set(s_TexDialogBox)`
   l. `DrawQuad(colour)` — full quad, no UV crop.
   m. `Texture::UnSet(s_TexDialogBox)`

**Block B path (`m_LayerFlags != m_LayerFlagsAlt` AND `m_AnimFrame > 0`):**
8. Compute `slide_X` using same formula as step 5f (or 145.0f if alpha >= 1.0).
9. If `s_cachedRingVec3` not yet initialized:
   - Read `s_TexSelected` width and height.
   - Set `s_cachedRingVec3 = Vec3(w+1, h+1, 1.0f)`.
10. Copy `scale_vec = s_cachedRingVec3`.
11. If `m_AnimFrame < 0x3ffc`:
    - `sin_ratio = Math::SinIdx((uint16_t)m_AnimFrame) / Math::SinIdx(0x3ffc)`
    - `scale_vec *= sin_ratio`
12. Build Scale44 from `scale_vec`.
13. Build Translate44 with `(slide_X, 104.0f, 0.0f)`.
14. `Reset / Set / Upload` matrices.
15. `Texture::Set(s_TexSelected)` (via SmartPtr copy for ref-counting safety).
16. `DrawQuad(colour_white)` — full quad.
17. `Texture::UnSet(s_TexSelected)`.

**Both blocks — return.**

---

### Known Ambiguities / Flags for Implementer

1. **`m_pShopList->field_0x8` identity**: The translate X in A1 uses `*(m_pShopList + 0x8)`. In the port's `ScrollingMenu`, `pos.x` is set in Update to `(1-alpha)*190*-1.5 - 95`. This is field `+0x8` of the ScrollingMenu object, which should be `pos.x` (assuming `ScrollingMenu` starts with `{vtable, x, y, z}` = `{+0x0, +0x4, +0x8, +0xc}`). Verify the field offset against the actual ScrollingMenu struct layout.

2. **`s_TexDialogBox->vtable` GetWidth/GetHeight**: The decompile calls `vtable[+0x14]` and `vtable[+0x18]` on `*(int**)(static_block + 0x34)` — that is, derefs the SmartPtr to get the `Texture*`, then calls vtable slots 5 and 6 (0-indexed). The port should call `s_TexDialogBox->GetWidth()` and `s_TexDialogBox->GetHeight()`.

3. **Colour constant**: The `*(Colour**)(GOT + 0x73a4)` runtime value is assumed white `{255,255,255,255}` but is NOT confirmed. If quads appear tinted unexpectedly, investigate this GOT slot.

4. **DrawQuadSized UV meaning**: `DrawQuadSized_GameTask(u0, u1, colour)` calls `Mesh::DrawQuadUnCached(colour, u0, u1, v0=0.1875f, v1=0.8125f, effects=0)`. Verify the parameter order in the port's `Mesh::DrawQuadUnCached` matches — the engine may pass these as `(u0, u1, v0, v1)` or as UVs mapped onto the quad corners. If BG looks cropped wrong, check the UV→vertex mapping.

5. **`Vec3::operator*` in A3**: The decompile calls `_Vector3::operator*(&_Stack_b8, &_Stack_ac.x)` with a float scalar pushed as `local_44 = 1.0f`. This multiplies the dimension Vec3 by exactly 1.0f — it is effectively a copy. The port can skip this and use the Vec3 directly.

6. **`static_block + 0x84` (dial_alpha)**: This float persists across frames (static). In the port it must be a `static float` inside `ShopScreen::Draw` or a dedicated static member. It is SEPARATE from `m_TransitionAlpha`.

7. **Sub-block A2 translate**: Sub-block A2 passes a pure scale matrix (no translate) to `SetMatrix_GameTask`. Looking at the disasm carefully, after `Scale44` in A2 the matrix register (`r10 = sp+0x48`) is passed to `SetMatrix_GameTask` — NOT a GlobalTranslate+Scale combo. So the BG quad in A2 renders centered at origin. However the `auStack_178` name in the Ghidra decompile may cause confusion; the disasm is authoritative.

---

## Known Runtime Issues (2026-04-25)

Observed after wiring back-icon + select_item textures and confirming the screen renders:

1. **Back-icon spins** — `back_icon.tex` rotates inside the back-button ring. `HUDControl3d::Draw` applies `RotZ44(SinIdx, CosIdx)` when `m_Timer != 0`. DojoScreen's back button doesn't visibly spin; ShopScreen's does. Likely cause: `MenuButton::Update` ticks `m_Timer` for the new instance, and ShopScreen never zeroes it. Fix candidates: (a) zero `m_Timer` post-Init, (b) RE the binary's per-screen suppression of timer ticking.
2. **Bomb mesh missing inside back ring** — only the fuse particle emits; the 3D bomb itself isn't visible. Indicates a `Bomb::Draw` guard tied to `m_bMenuBombHit`, `m_bMovement`, or a layer mask. Dojo bombs render fine, so the regression is ShopScreen-specific (possibly an interaction with the 0x80→0x40 layer toggle in `ShopScreen::Update`).
3. **"SELECTED" stamp floats in empty space** — Block B draws the pulsing ring at `(slide_X, 104, 0)` but with no list items it has nothing to overlap. Resolves once `ScrollingMenu::Draw` + `ShopListItem::Draw` populate the list.
4. **Empty info dialog box** — `dialog_box_shop.tex` panel renders, but no item name/description because the Font system isn't wired into the game struct (`game->field_0x54` font slots).
5. **Empty item list** — `ScrollingMenu::Draw` and `ShopListItem::Draw` are stubs; `ItemManager::GetNumItems()` returns 0.

---

## ScrollingMenuItem Class Layout

<!-- Analysed: 2026-04-25T23:00 -->

ScrollingMenuItem does NOT inherit from HUDControl. It has its own independent vtable.

**Vtable** (raw ptr `0x001e9ef8`, object vtable = `0x001e9f00`):

| Slot | Offset | Function | Address |
|------|--------|----------|---------|
| 0 | +0x00 | ~ScrollingMenuItem (dtor1) | `0x0015c3ac` |
| 1 | +0x04 | ~ScrollingMenuItem (dtor2) | `0x0015c3e8` |
| 2 | +0x08 | GetHeight | `0x0013cdf0` |
| 3 | +0x0C | GetWidth | `0x0013cdf8` |
| 4 | +0x10 | SetHeight | `0x0013ce00` |
| 5 | +0x14 | SetWidth | `0x0013ce08` |
| 6 | +0x18 | Move(_Vector3) | `0x0015aea8` |
| 7 | +0x1C | Remove | `0x0013d14c` |
| 8 | +0x20 | SetParent(ScrollingMenu*) | `0x0015aeb4` |
| 9 | +0x24 | SetOnscreen(bool) | `0x0013ce10` |
| 10 | +0x28 | SetText(char*) | `0x0015b124` |
| 11 | +0x2C | **Draw()** | `0x0015b480` |
| 12 | +0x30 | ? | `0x00147970` |
| 13 | +0x34 | ? | `0x00147974` |
| 14 | +0x38 | ? | `0x00147978` |

**ScrollingMenuItem struct layout** (deduced from ctor `0x0015b228`):

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | vtable* | vtable | |
| +0x04 | float | pos.x | Set by Move() |
| +0x08 | float | pos.y | Set by Move() |
| +0x0C | float | pos.z | Set by Move() |
| +0x10 | ScrollingMenu* | m_pParent | Set by SetParent() |
| +0x14 | Colour | m_Colour | 4 bytes RGBA, from MakeColourFromGlobal_ScrollMenu |
| +0x18 | float | m_Width | From GOT-cached Vec3[0] |
| +0x1C | float | m_Height | From GOT-cached Vec3[1] |
| +0x20 | float | m_Depth | From GOT-cached Vec3[2] |
| +0x24 | float | m_ParamWidth | **GetHeight() reads this field** (not +0x1C). No-arg ctor sets 25.0f; `ShopListItem::Create` overrides to 80.0f. SetHeight also writes here. |
| +0x28 | float | m_ParamHeight | ctor param (4-param ctor writes height arg here; no-arg ctor writes 0.0f; `ShopListItem::Create` writes 290.0f). NOT what GetHeight() reads. |
| +0x2C | Delegate1 | m_Callback | 40 bytes (Delegate1<void,ScrollingMenuItem*>) |
| +0x54 | char* | m_pText | Label string ptr; SetNull then SetText called in ctor |
| +0x58 | (unknown) | | |
| +0x5C | char[...] | m_DescText | Inline description text buffer (used by ShopListItem::Draw as `in_r0+0x5c`) |

**ShopListItem vtable** (raw ptr `0x001ea028`, object vtable = `0x001ea030`) — overrides:

| Slot | Offset | Function | Address |
|------|--------|----------|---------|
| 0 | +0x00 | ~ShopListItem (dtor1) | `0x0015cfb4` |
| 1 | +0x04 | ~ShopListItem (dtor2) | `0x0015d018` |
| 6 | +0x18 | ShopListItem::Move (override) | `0x0015d9fc`* |
| 11 | +0x2C | **ShopListItem::Draw** | `0x0015eb00` |

*Ghidra merged `0x0015d9fc` into `_GLOBAL__I_ShopScreen.cpp`. The address is a valid ARM Thumb-2 entry point for ShopListItem's Move override; the merger is a Ghidra analysis artifact.

**ShopListItem additional fields** (appended after ScrollingMenuItem base):

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x25C | float | m_NewItemAlpha | `>0` → draw new_item_sml badge; fades from ShopListItem init |
| +0x260 | float | m_SelectedAlpha | `>0` → draw selected_sml highlight ring |
| +0x264 | float | (unknown float) | Initialised to `DAT_0015f7a0 = 0.0f` |
| +0x268 | Vec3? | (position) | Added to in_r0+4 (pos) for icon translate |
| +0x274 | SmartPtr\<Texture\> | m_pIconTex | Item icon texture (loaded externally); SetNull in ctor |
| +0x278 | ItemInfo* | m_pItemInfo | Pointer to item info; 0 in ctor |
| +0x27C | byte | m_bOnscreen | 1 in ctor; 0 means item is off-screen (Draw early-exits) |
| +0x27D | byte | m_bSelected | 0 in ctor; when 1, resets static colour cache |
| +0x27E | byte | m_bIsNew | 0 in ctor; when non-zero, draws `loading.tex` overlay badge |
| +0x280 | float | m_CostAlpha | Used by cost text: `(m_CostAlpha * 255.0f)` clamped → byte alpha |

---

## ScrollingMenu::Draw

<!-- Analysed: 2026-04-25T20:30 -->

**Address**: `0x0015af98`
**Signature**: `void ScrollingMenu::Draw(float* hudScale)`
**Size**: 12 instructions (~50 bytes)

ScrollingMenu::Draw is a minimal iterator. It has NO scissor/clipping setup, NO scroll-offset application, and NO per-item positioning logic. All of that is in `ScrollingMenu::Update`.

```c
void ScrollingMenu::Draw(float* hudScale) {
    for (auto it = m_Items.begin(); it != m_Items.end(); ++it) {
        (*it)->vtable[+0x2C]();   // calls Draw() on each item
    }
}
```

The vtable dispatch at offset `+0x2C` resolves to:
- `ScrollingMenuItem::Draw` (`0x0015b480`) for plain items
- `ShopListItem::Draw` (`0x0015eb00`) for shop items (ShopListItem overrides slot 11)

**Positioning**: each item's world position is set by `ScrollingMenu::Update` via `(*item)->vtable[+0x18](item, vec3)` (the `Move` call). Draw just iterates and renders; no per-frame position is set during Draw.

**Companion function**: `ScrollingMenu::PreDraw` at `0x0015af34` is a no-op (`return param_1`).

**No scissor/clip**: the binary does not call any GL scissor or clip-plane function inside Draw. The ShopScreen's list stays within the dialog_box bounds naturally due to position math, not GL clipping.

---

## ScrollingMenuItem::Draw

<!-- Analysed: 2026-04-25T20:30 -->

**Address**: `0x0015b480`
**Signature**: `void ScrollingMenuItem::Draw()` (unknown calling convention — `r0 = this`)
**Size**: ~150 bytes
**Used by**: all ScrollingMenu items that are NOT ShopListItem (i.e., plain text items)

### Pseudocode

```c
void ScrollingMenuItem::Draw() {
    if (this->m_pText == NULL) return;  // +0x54 null check

    // Compute clip region from parent ScrollingMenu bounds
    float* clipRect = NULL;
    if (this->m_pParent != NULL) {   // +0x10
        float parentY = m_pParent->pos.y;   // +0x08 of parent
        float half_h  = (float)m_pParent->vtable->GetHeight() * 0.5f;
        // clipRect points to local_5c[4] = { top, bottom, left, right }
        // top    = parentY + half_h
        // bottom = parentY - half_h
        // left   = parentX - half_w   (similar for X)
        // right  = parentX + half_w
        clipRect = &local_5c;
    }

    Font* font = *(Font**)(game->pData + 0x54);  // font_fruit_ninja.fnt
    Utf8StringIterator iter(this->m_pText);       // +0x54
    Vec3 drawPos = this->pos;                     // +0x04

    // Shadow pass (black, 50% alpha)
    // (none in ScrollingMenuItem base; ShopListItem adds shadows)

    // Draw with colour from m_Colour (+0x14)
    Vec2 globalVec2 = CopyGlobalVec2_GameTask();
    Font::Font_DrawString(
        /*scale=*/ 31.0f,        // 0x41f00000
        /*spacing=*/ 1.0f,       // 0x3f800000
        /*lineHeight=*/ DAT_0015b5a0,  // 0.0f
        font,
        &iter,
        &drawPos,
        &m_Colour,
        &globalVec2,
        /*flags=*/ 0xF,          // right-aligned + centred?
        clipRect                 // NULL if no parent
    );
}
```

**Font slot**: `Game + 0x54` = `font_fruit_ninja.fnt` (SD) / `font_fruit_ninja_HD.fnt` (HD).
**Scale**: hardcoded `31.0f`.
**Flags**: `0xF`.
**Clip**: populated from `m_pParent` field `+0x08/+0x0C`(pos) and `+0x44/+0x48` vtable calls (GetHeight/GetWidth on the parent).

---

## ShopListItem::Draw

<!-- Analysed: 2026-04-25T20:30 -->

**Address**: `0x0015eb00`
**Body**: `0x0015eb00` – `0x0015f717` (~1816 bytes)
**Signature**: `void ShopListItem::Draw()` (unknown calling convention — `r0 = this`)
**Instruction count**: ~450 instructions
**Font::DrawString calls**: 5 (2 title, 2 cost-hint, 1 description)
**Textures drawn**: 5 distinct slots

All GOT accesses share the same `GOT_base = 0x001ec130` (same static block as ShopScreen::Draw).

### Guard

```c
if (in_r0 + 0x27D) {  // m_bSelected flag
    static_block[+0x8C] = 0xFFFFFFFF;  // reset colour cache
}
if (in_r0 + 0x2C == 0) return;  // scrollingmenuitem +0x2c = onscreen flag? Actually this is in HUDControl base... 
```

Actually the guard at `in_r0 + 0x2d` — looking at the decompile again: `if (*(char*)(in_r0 + 0x2d) != '\0')`. This is a different offset. Since `in_r0` is the ShopListItem pointer and `+0x2C` is the start of the Delegate1, `+0x2D` is the second byte of the delegate. This is confusing — it may actually be a custom onscreen flag stored in the delegate region. The effective meaning is: if this byte is non-zero, proceed with drawing; otherwise the whole item body is skipped.

### Part 1: Item Name (Title) Text — always drawn if onscreen

```c
Font* font = game->pData + 0x54;           // font_fruit_ninja.fnt
Colour itemColour = *(Colour*)(GOT_base + 0x73a4);  // white
if (ItemInfo::IsLocked(this->m_pItemInfo)) {
    itemColour = Colour(200, 200, 200, 255);  // greyed out
}
Vec3 basePos = this->pos;  // in_r0 + 0x04

// Check if HD font mode (Game + 0x03 == '\f')
uint scale = (Game.field_0x03 == '\f') ? 20 : 25;

// Measure title string to check if it needs scale-to-fit
float measured = Font::MeasureString(font, this->m_pItemInfo->m_pTitle);  // ItemInfo + 0x14
float textScale = 1.0f;
if (measured * scale > 175.0f  &&  Game.field_0x03 == '\f') {
    textScale = 175.0f / (measured * scale);
    scale = (uint)(0.0f < textScale * 20.0f) * (int)(textScale * 20.0f);
}

// Shadow draw (offset +4, -4, 0)
Vec3 shadowPos = basePos + Vec3(4.0f, -4.0f, 0.0f);
Font::Font_DrawString(scale, 1.0f, 0.0f, font, title_iter, shadowPos, Colour(0,0,0,64), globalVec2, 0xE, 0);

// Actual draw (no offset)
Vec3 drawPos = basePos;
Font::Font_DrawString(scale, 1.0f, 0.0f, font, title_iter, drawPos, itemColour, globalVec2, 0xE, 0);
```

`ItemInfo::m_pTitle` = `*(char**)(ItemInfo + 0x14)`.

### Part 2: Cost Hint Text — drawn when cost-strings cached

The binary caches 4 cost-string widths (in `static_block[+0x8C..+0x9C]`) scaled by `pFVar29` (cost scale). These correspond to 4 possible currency/cost strings stored in `static_block[+0x1C..+0x28]` (4 pointers × 4 bytes). The active one is picked by `*(char*)(ItemInfo + 0x10)` (the cost-type index, values 0–3).

```c
float costScale = (Game.field_0x03 == '\f') ? textScale * 16.0f : 20.0f;  // 0x41a00000 = 20.0

// Cache cost-string widths if not yet measured
if (static_block[+0x90] == 0.0f) {
    for (int i = 0; i < 4; i++) {
        float w = Font::MeasureString(font, static_block[+0x1C + i*4]);
        static_block[+0x90 + i*4] = w * costScale;
    }
}

char* costStr = static_block[+0x1C + costTypeIndex * 4];
if (costStr != NULL) {
    // Shadow
    Vec3 cShadowPos = basePos + Vec3(4.0f, -4.0f, 0.0f);
    Font::Font_DrawString(costScale, 1.0f, 0.0f, font, cost_iter, cShadowPos, Colour(0,0,0,64), globalVec2, 0xE, 0);
    // Actual
    Vec3 cPos = basePos;
    Font::Font_DrawString(costScale, 1.0f, 0.0f, font, cost_iter, cPos, itemColour, globalVec2, 0xE, 0);
}
```

### Part 3: new_item_sml badge — when `m_NewItemAlpha > 0`

```c
if (in_r0->m_NewItemAlpha > 0.0f) {   // +0x25C
    // Scale: Vec3(65.0f, 33.0f, 0.0f) * m_NewItemAlpha * m_NewItemAlpha (two multiplies)
    // Translate: (basePos.x - title_width*costScale - 4.0f,
    //              34.0f + basePos.y + static_block[+0x6C],
    //              0.0f)
    // Clamp tx: if parent and parent field +0xB8==1 then tx >= scale_x*0.25 - 240.0f
    Scale44 + GlobalTranslate44; Reset/Set/Upload;
    Texture::Set(static_block[+0x44]);   // new_item_sml.tex
    DrawQuad(itemColour);
    Texture::UnSet(static_block[+0x44]);
}
```

### Part 4: selected_sml highlight ring — when `m_SelectedAlpha > 0`

```c
if (in_r0->m_SelectedAlpha > 0.0f) {   // +0x260
    // Scale: Vec3(65.0f, 33.0f, 0.0f) * m_SelectedAlpha * m_SelectedAlpha
    // Translate: (basePos.x - static_block[+0x90 + costTypeIndex*4] - 32.0f,
    //             basePos.y,
    //             0.0f)
    Scale44 + GlobalTranslate44; Reset/Set/Upload;
    SmartPtr<Texture> tmp = static_block[+0x3C];  // selected_sml.tex
    Texture::Set(tmp);
    DrawQuad(itemColour);
    Texture::UnSet(tmp);
}
```

### Part 5: Item icon — when `m_pIconTex` is non-null

```c
if (SmartPtr::operator_cast_to_bool(&in_r0->m_pIconTex)) {  // +0x274
    // Scale: Vec3(DAT_0015f188, DAT_0015f188, 0.0f)
    //      = Vec3(64.0f, 64.0f, 0.0f)
    // Translate from cached Vec3 at static_block[+GOT_0x73ec] + (in_r0+0x268)
    Scale44 + GlobalTranslate44; Reset/Set/Upload;
    if (!ItemInfo::IsLocked(m_pItemInfo)) {
        Texture::Set(*(in_r0 + 0x274));  // m_pIconTex
        DrawQuad(itemColour);
        Texture::UnSet(*(in_r0 + 0x274));
    } else {
        Texture::Set(static_block[+0x40]);  // locked_stroke.tex (greyed-out icon overlay)
        DrawQuad(itemColour);
        Texture::UnSet(static_block[+0x40]);
    }
}
```

`DAT_0015f188 = 0x42800000 = 64.0f`.

### Part 6: scratch_deviders divider cell

```c
// Scale: Vec3(DAT_0015f198, 17.0f, 0.0f) * *(float**)(GOT+0x7214) / 2.0f
// Translate: based on pos, parent scroll
Scale44 + GlobalTranslate44; Reset/Set/Upload;
Texture::Set(static_block[+0x30]);  // scratch_deviders.tex

// Colour cache: if static_block[+0x8C] == costTypeIndex -> white (255,255,255,200)
//               else                                     -> grey  (128,128,128,255)
//               (and update cache)
DrawQuad(divider_colour);
Texture::UnSet(static_block[+0x30]);
```

A second divider draw follows (`if (m_bIsNew)`):
```c
if (in_r0->m_bIsNew) {  // +0x27E
    // Same Scale, translate RIGHT side (operator- instead of operator+)
    Texture::Set(static_block[+0x30]);  // scratch_deviders.tex again
    DrawQuad(Colour(128,128,128,255));
    Texture::UnSet(static_block[+0x30]);
}
```

### Part 7: Description / lock text — when `m_pText` set and alpha > 0

```c
uint alpha = clamp((uint)(m_CostAlpha * 255.0f), 0, 255);  // +0x280
if (alpha != 0) {
    char* descBuf = (char*)(in_r0 + 0x5C);  // inline description buffer
    Font* descFont = game->pData + 0x54;    // font_fruit_ninja.fnt (same slot)
    float descFontSize = 18.0f;
    // Shrink descFontSize until text fits within DAT_0015f524 height
    while (Font::GetStringHeight(descFont, descBuf, descFontSize, ...) > DAT_0015f524) {
        descFontSize -= 0.25f;
    }
    float xPos = ShopScreen::GetDescriptionTextXPos();
    
    if (!IsLocked || purchaseState == 0 || purchaseState == 3) {
        // Normal description text
        Colour textColour = IsLocked ? Colour(255,255,255,alpha) : Colour(0x74,0x5D,0x3B,alpha);
        Font::DrawString(xPos, 0.0f, 0.0f, descFontSize, ..., descFont, descBuf, textColour, 0xF, 0);
    } else if (purchaseState == 1) {
        // "Cost per play" mode: show currency label + price
        // costPriceScale = descFontSize * DAT_0015f538
        // ... additional Font::DrawString calls
    } else if (purchaseState == 2) {
        // FruitSaveData::PlayedModeToday check
        // ... additional Font::DrawString calls
    }
}
```

`purchaseState = *(char*)(ItemInfo + 0x24)`.
`DAT_0015f524 = 0x42820000 = 65.0f` (max description text height).
`ShopScreen::GetDescriptionTextXPos()` returns `145.0f - 80.0f = 65.0f` when fully transitioned.

### Part 8: loading.tex new-badge overlay — when `m_bIsNew`

```c
if (in_r0->m_bIsNew) {  // +0x27E
    Texture::Set(static_block[+0x2C]);  // loading.tex
    // Draw 1: shadow quad
    // Scale: Vec3(290.0f, 120.0f, 0.0f)
    // Translate: (*(parent->pos + 0x08) - 2.0f,  105.0f, 0.0f)
    Scale44 + GlobalTranslate44; Reset/Set/Upload;
    DrawQuad(Colour(0,0,0,128));
    // Draw 2: same translate, different Y offset
    // Translate: (*(parent->pos + 0x08) - 2.0f, -105.0f, 0.0f)
    Scale44 + GlobalTranslate44; Reset/Set/Upload;
    DrawQuad(Colour(0,0,0,128));
    Texture::UnSet(static_block[+0x2C]);
}
```

`DAT_0015f718 = 290.0f`, `DAT_0015f71c = 120.0f`, `DAT_0015f724 = 105.0f`, `DAT_0015f728 = -105.0f`.

Note: `loading.tex` is used here as a **banner/badge shape**, not as a loading indicator. The two semi-transparent black quads form a top/bottom stripe on the "new item" badge.

### Static Block Slot Usage Summary (ShopListItem::Draw)

All accesses via `GOT_base + 0x000451b4` (same static block as ShopScreen):

| Slot | Texture | Draw Part | Condition |
|------|---------|-----------|-----------|
| `+0x2C` | `loading.tex` | New-badge overlay (Part 8) | `m_bIsNew != 0` |
| `+0x30` | `scratch_deviders.tex` | Divider cell (Part 6) | Always |
| `+0x3C` | `selected_sml.tex` | Selected highlight (Part 4) | `m_SelectedAlpha > 0` |
| `+0x40` | `locked_stroke.tex` | Icon-locked overlay (Part 5) | Icon present AND locked |
| `+0x44` | `new_item_sml.tex` | New-item badge (Part 3) | `m_NewItemAlpha > 0` |

**Not accessed by ShopListItem::Draw**: `+0x14` (locked.tex), `+0x18` (select_item.tex), `+0x34` (dialog_box_shop.tex), `+0x38` (selected.tex), `+0x48` (BG_store.tex).

### Resolved Slot +0x44 Texture

Confirmed by reading LoadContent `0x0015cb08`:
- `DAT_0015cccc = 0xfffcf079` (`0x001ec130 - 0x30FB7 ... ` = `0x001bc1a9`)
- `*(char*)0x001bc1a9` = `"new_item_sml.tex"` ← **slot +0x44 = `new_item_sml.tex`**

This was previously listed in shop.md slot table as `+0x44` but the doc note said "Slot +0x44 (unidentified)". It is now confirmed.

### Corrected Static Block Slot Table

The corrected slot-to-texture mapping (verified from LoadContent disasm + string reads + Draw analysis):

| Slot | Texture | Used by |
|------|---------|---------|
| `+0x14` | `locked.tex` | ShopListItem (not in Draw — loaded for potential future use) |
| `+0x18` | `select_item.tex` | ShopListItem (not in Draw) |
| `+0x2C` | `loading.tex` | ShopListItem::Draw Part 8 |
| `+0x30` | `scratch_deviders.tex` | ShopListItem::Draw Part 6 |
| `+0x34` | `dialog_box_shop.tex` | ShopScreen::Draw Block A3 |
| `+0x38` | `selected.tex` | ShopScreen::Draw Block B |
| `+0x3C` | `selected_sml.tex` | ShopListItem::Draw Part 4 |
| `+0x40` | `locked_stroke.tex` | ShopListItem::Draw Part 5 |
| `+0x44` | `new_item_sml.tex` | ShopListItem::Draw Part 3 |
| `+0x48` | `BG_store.tex` / `BG_store_sml.tex` | ShopScreen::Draw Block A |

The header file `ShopScreen.h` and port source `ShopScreen.cpp` have **wrong slot names** for +0x2c and +0x34/+0x38/+0x40 — see existing note in "Static Block Layout" section above. The port maintainer must re-order the static SmartPtr members to match this corrected table.

---

## ShopListItem Row Height — GetHeight() and Create()

<!-- Analysed: 2026-04-25T23:00 -->

### Binary: GetHeight() returns *(this + 0x24) = m_ParamWidth

`ScrollingMenuItem::GetHeight` at `0x0013cdf0`:
```c
undefined4 ScrollingMenuItem::GetHeight(void) {
    int in_r0;
    return *(undefined4 *)(in_r0 + 0x24);   // +0x24 = m_ParamWidth field
}
```

`ScrollingMenuItem::SetHeight` at `0x0013ce00`:
```c
void ScrollingMenuItem::SetHeight(float param_1) {
    *(float *)(this + 0x24) = param_1;       // also writes +0x24
}
```

**Critical**: `GetHeight()` does NOT read `+0x1C` (`m_Height`). It reads `+0x24`, which is the field named `m_ParamWidth` in the port header (`ScrollingMenuItem.h` line 142). This field is what `ScrollingMenu::Update Phase 5` and `AddItem` use for the row pitch.

### No-arg ScrollingMenuItem ctor default

The no-arg ctor (`0x0015b5dc`, called by `ShopListItem::ShopListItem`) sets:
```c
*(float *)(this + 0x24) = 0x41c80000;   // = 25.0f  (hardcoded literal in ctor)
*(undefined4 *)(this + 0x28) = DAT_0015b668;  // = 0x00000000 = 0.0f
```

So straight after construction `GetHeight()` returns **25.0f**. This is the port's current default and IS correct for the ctor.

### ShopListItem::Create overrides +0x24 to 80.0f

`ShopListItem::Create` at `0x0015c988` (called from `ShopScreen::Init` after each `ShopListItem::ShopListItem()`):
```c
void ShopListItem::Create(ShopListItem *this, ItemInfo *param_1, ShopScreen *param_2) {
    *(undefined4 *)(this + 0x24) = DAT_0015cae8;  // = 0x42a00000 = 80.0f  <-- OVERRIDES GetHeight
    *(undefined4 *)(this + 0x28) = DAT_0015caec;  // = 0x43910000 = 290.0f
    // ... (also sets m_Width/m_Height/m_Depth, ShopScreen*, ItemInfo*, text, etc.)
    // Vec3(60.0f, 13.0f, 0.0f) written to +0x18/+0x1C/+0x20
}
```

**Resolved constants from binary:**
| DAT address | Hex value | Float value | Field written |
|---|---|---|---|
| `DAT_0015cae4` | `0x00000000` | 0.0f | `*(this+0x280)` (m_CostAlpha init) + Vec3 z |
| `DAT_0015cae8` | `0x42a00000` | **80.0f** | `*(this+0x24)` — **GetHeight() return value** |
| `DAT_0015caec` | `0x43910000` | 290.0f | `*(this+0x28)` — m_ParamHeight |
| `DAT_0015caf0` | `0x42700000` | 60.0f | Vec3.x for m_Width (+0x18) |

So every `ShopListItem` in the binary has `GetHeight() = 80.0f` after `Create()` runs.

### Row-pitch in ScrollingMenu::Update Phase 5

```c
float halfH = item->GetHeight() * 0.5f;   // = 80.0f * 0.5f = 40.0f per item
// ...
curY -= halfH;   // advance TWICE (before and after each item)
curY -= halfH;
```

Row pitch per item = `halfH * 2 = 80.0f`. Each item occupies **80 units** of Y space in the layout.

### ScrollingMenu m_TotalHeight accumulation in AddItem

`ScrollingMenu::AddItem` reads `GetHeight()` and adds to `field63_0xac`:
```c
this->field63_0xac += GetHeight(param_1);  // += 80.0f per item
```

With N items: `m_TotalHeight = N * 80.0f`. The scroll bounds use this for spring-back.

### ShopScreen::Init initialisation sequence

In `ShopScreen::Init` (`0x0015f7ac`):
1. Creates `ScrollingMenu` (new, 0x100 bytes)
2. Calls `ScrollingMenu::vtable[+0x04]` with `DAT_0015f9c4 = 290.0f` — sets some ScrollingMenu window size
3. Calls `vtable[+0x4C]` and `vtable[+0x54]` on the menu with `DAT_0015f9c8 = 80.0f`
4. Iterates `ItemManager::GetFirst/GetNext`:
   - `operator_new(0x284)` — alloc ShopListItem (0x284 bytes)
   - `ShopListItem::ShopListItem(this_00)` — ctor sets `+0x24 = 25.0f`
   - `ShopListItem::Create(this_00, pItemInfo, shopScreen)` — **overrides `+0x24 = 80.0f`**
   - `ScrollingMenu::AddItem(...)` — accumulates `+0x24` into `m_TotalHeight`
5. After loop: sets the ScrollingMenu's pos to `Vec3(DAT_0015f9d0, DAT_0015f9d4, ...)` = `Vec3(-530.0f, 0.0f, ...)`
6. Writes `*(scrollMenu + 0xd8)` = initial scroll offset from a global

### Visible region (m_ItemHeight = -120.0f)

`ScrollingMenu` ctor sets `field61_0xa4 = DAT_0015b39c = 240.0f`... wait — the no-arg ctor at `0x0015b2e0` (used by `ShopScreen::Init`) sets:
- `field59_0x9c = 320.0f`
- `field60_0xa0 = 320.0f` (visible window height for this ctor)
- `field61_0xa4 = 240.0f` (m_ItemHeight, outer touch half-height)

The outer touch region yMin/yMax = `±field61_0xa4 = ±240.0f`. The visible clip range for SetOnscreen uses the default `RANGE_TOP=-160.0f / RANGE_BOT=160.0f` unless `m_bConstrainedView` is set. **No GL scissor** is used — see ScrollingMenu::Draw section.

### Summary

| Parameter | Binary value | Port current value | Correct? |
|---|---|---|---|
| `GetHeight()` reads | `*(this + 0x24)` = `m_ParamWidth` | `m_Height` (`+0x1C`) | **WRONG** |
| Height after ctor | 25.0f | 25.0f | OK (ctor default) |
| Height after `Create()` | **80.0f** | never set (Create not ported) | **WRONG** |
| Row pitch in Update | `GetHeight()` * 2 = **160.0f** | `m_Height` * 2 = 50.0f | **WRONG** |
| SetHeight writes | `*(this + 0x24)` | `m_Height` (`+0x1C`) | **WRONG** |

---

## ScrollingMenu::Update (Input Handling)

<!-- Analysed: 2026-04-25T21:45 -->

**Address**: `0x0015b744`
**Size**: 376 decompiled lines
**Signature**: `void __thiscall ScrollingMenu::Update(ScrollingMenu* this, float dt)`

---

### Field Map (Binary vs Port)

The following table maps binary field names (from the Ghidra decompile) to their port equivalents.
Fields marked **WRONG** in the port header have the wrong name and must be corrected.

| Binary name | Offset | Type | Port name | Notes |
|-------------|--------|------|-----------|-------|
| `field22_0x74` | `+0x74` | int | `m_TouchId` | Touch slot index; -1 = none |
| `field77_0xc0` | `+0xc0` | int | `m_SelectedIdx` | **WRONG** — this is NOT the selected index. See below. |
| `field76_0xbc` | `+0xbc` | int | `m_ClosestIdx` | Closest-to-zero item index (what ShopScreen reads) |
| `field83_0xcc` | `+0xcc` | ptr | `m_pCollidedItem` | **MISSING in port** — collided ScrollingMenuItem* (for click) |
| `field78_0xc4` | `+0xc4` | float | (unnamed) | Closest-to-snap distance accumulator; init 1.0f |
| `field_0xc8` | `+0xc8` | bool | `m_bDragging` | 1 while finger has moved past drag threshold |
| `field_0xc9` | `+0xc9` | bool | `m_bTouchProcessed` | Set to 1 when tap released without drag; cleared each frame at entry |
| `field_0xca` | `+0xca` | bool | `m_fieldCA` | Enables Collide() walk; init 1 |
| `field_0xd0` | `+0xd0` | bool | (unnamed) | **MISSING in port** — when 1, constrains visible range to `[pos.y - field60_0xa0, pos.y]` |
| `field_0xd4..0xdc` | `+0xd4` | Vec3 | (unnamed) | **MISSING in port** — velocity Vec3 (x/y/z); y-component drives scroll |
| `field_0x7c..0x80` | `+0x7c` | float | (unnamed) | **MISSING in port** — touch anchor Y at finger-down; used to compute drag delta |
| `field_0x84..0x8c` | `+0x84` | Vec3 | (unnamed) | **MISSING in port** — anchor scroll offset at finger-down |
| `field_0x88` | `+0x88` | float | `m_ScrollOffset` | **WRONG** — real scroll offset (Y component) is at `field_0xd8` (`+0xd8`). `+0x88` is the anchor Y. |
| `field_0xd8` | `+0xd8` | float | (none) | **TRUE scroll offset** — Y offset applied to item layout. Port's `m_ScrollOffset` maps here. |
| `field_0x90..0x98` | `+0x90` | Vec3 | (unnamed) | **MISSING in port** — pending velocity Vec3 (integrated into field_0xd4 each tick) |
| `field59_0x9c` | `+0x9c` | float | `m_Width`? | **CHECK** — ctor uses `DAT_0015b468 = 320.0f`; this is the total scroll area width |
| `field60_0xa0` | `+0xa0` | float | `m_Height` | ctor uses `DAT_0015b46c = 240.0f`; this is the visible window height |
| `field61_0xa4` | `+0xa4` | float | `m_ItemHeight` | ctor uses `DAT_0015b470 = -120.0f`; sets the touch outer-region half-height (negative!) |
| `field62_0xa8` | `+0xa8` | float | `m_TotalWidth` | **WRONG** — used in scroll bounds; should be total scroll height |
| `field63_0xac` | `+0xac` | float | `m_TotalHeight` | **CHECK** — also init to 0 |
| `field100..107` | `+0xe0..0xfc` | 2×Vec4 | (unnamed) | Touch region bounds: outer (+0xe0..0xec) and inner (+0xf0..0xfc); set by ctor |

**Critical offset corrections for the port**:

1. `m_ScrollOffset` at `+0x88` is the **touch anchor Y**, not the scroll offset. The real scroll offset is `field_0xd8` at `+0xd8`.
2. `m_SelectedIdx` at `+0xc0` is actually the `field77_0xc0` used to track the **collided/hovered item index during a drag** — not a persistent selection. The persistent "closest to zero" index is `field76_0xbc` at `+0xbc` (port's `m_ClosestIdx`).
3. Several fields are entirely missing from the port header: `field83_0xcc` (collided item ptr), `field_0xd4` (velocity Vec3), `field_0xd0` (visibility mode), `field_0x90` (pending velocity Vec3).

---

### Touch Input Model

Update uses the **Mortar::Touch** global array (not HUDControl's touch system). Two global accessors:

| Function | Address | Behaviour |
|----------|---------|-----------|
| `TouchInRegion(x0,x1,y0,y1, hint_slot)` | `0x001691cc` | Searches the 16-slot touch array for a touch whose position lies in the rect `[x0..x1] x [y0..y1]`. If `hint_slot` is 0..15 AND that slot is already in the rect, returns it immediately; otherwise linear-scans from slot 0. Returns -1 if no match. Position check: `touch[slot].state > 0` (finger down). |
| `IsTouchDown(slot)` | `0x00169144` | Returns 0 if slot out-of-range or state==0, 1 if `state==1.0` (just pressed), 2 if `state==2.0` (held/moving). |

Each touch slot is a 12-byte struct at index `slot * 0xC` relative to the global touch array base:
- `+0xA0` : float x position
- `+0xA4` : float y position
- `+0xA8` : float state (0=up, 1=just-down, 2=held)

Touch region bounds stored in the ScrollingMenu struct as two 4-component groups:
- **Outer region** (`field100..103`, `+0xe0..0xec`): `{xMin_rel, yMin_rel, yMax_rel, xMax_rel}` relative to `pos.x`. Set by ctor from `DAT_0015b470/0x474` defaults. Used for initial touch-acquire scan.
- **Inner region** (`field104..107`, `+0xf0..0xfc`): `{xMin_rel, yMin_rel, yMax_rel, xMax_rel}` relative to `pos.x`. Set from `field61_0xa4 * -0.5 / +0.5`. Used during drag to detect if finger left the scroll area.

**`ScrollingMenu::Collide(touchSlot)`** at `0x0015af4c`: walks `m_Items` calling `vtable[+0x34](item, touchSlot)` on each. The first item that returns non-zero is returned as the collided item pointer. Enabled only when `field_0xca != 0`. Vtable +0x34 is slot 13 (currently named `Slot13` in the port).

---

### Phase 1 — Per-Frame Init

```c
// Clear "tap fired" flag at top of every frame
this->field_0xc9 = 0;   // m_bTouchProcessed = false
```

---

### Phase 2 — Touch Acquire (when field22_0x74 == -1, i.e. no active touch)

```c
if (m_TouchId == -1) {
    // Scan for a new finger anywhere in the OUTER region
    int slot = TouchInRegion(pos.x + field100_0xe0, pos.x + field102_0xe8,
                             pos.x + field103_0xec, pos.x + field101_0xe4, -1);
    m_TouchId = slot;

    if (IsTouchDown(slot) == 2) {
        // Finger is HELD (state 2) — valid acquire
        void* hitItem = Collide(this, slot);   // find which item was touched
        m_SelectedIdx = -1;                     // field77_0xc0 = -1 (no drag target yet)
        // Latch touch-point anchor into field_0x78..0x8c
        //   field_0x78 = touch[slot].x   (x anchor)
        //   field_0x7c = touch[slot].y   (y anchor)
        //   field_0x80 = touch[slot].state
        //   field_0x84 = field_0xd4 (copy current velocity Vec3 into anchor)
        //   field_0x88 = field_0xd8 (copy current scroll offset into anchor)
        //   field_0x8c = field_0xdc
        field83_0xcc = hitItem;               // remember which item was tapped
        // Mark touch bitmask (GOT[0x7740] |= 0x40)
    } else {
        m_TouchId = -1;   // IsTouchDown != 2 => ignore this finger
    }
}
```

**Note**: `IsTouchDown == 2` means the finger is already held (state 2.0), not just pressed (1.0). The acquire fires when a held finger enters the region, not on first contact.

---

### Phase 3 — Active Touch Tracking (when field22_0x74 != -1)

```c
if (m_TouchId != -1) {
    // Check if the finger is still within the INNER region
    int stillIn = TouchInRegion(pos.x + field104_0xf0, pos.x + field106_0xf8,
                                pos.x + field107_0xfc, pos.x + field105_0xf4,
                                m_TouchId);

    if (stillIn != m_TouchId) {
        // --- Finger left the inner region (or was lifted) ---
        // Phase 3A: Touch Release path
        if (field83_0xcc != nullptr) {
            // Fire vtable[+0x38] (slot 14 / "Slot14") on the collided item
            (*field83_0xcc->vtable[0x38])();
        }

        if (!m_bDragging && field83_0xcc == nullptr) {
            // No drag occurred AND no item was being tracked -> snap to closest
            m_SelectedIdx = -1;   // field77_0xc0

            // Find the item closest to the center: accumulate
            // dist = abs((scrollOffset_negated + pos.y + m_Height*-0.5) - touch_anchor_y)
            // for each item; track minimum. Also gates on:
            //   (scrollOffset + field62_0xa8 + field_0xd8 >= field60_0xa0)
            // If gate passes: m_SelectedIdx = item_index, else = -1
            // (i.e. only snap to an item if it fits within bounds)
        }

        // If dragging (m_bDragging set), skip the snap — carry velocity through
    } else {
        // --- Finger still in inner region ---
        // Phase 3B: Drag velocity update
        //
        // field78_0xc4 = 1.0f  (reset snap-distance accumulator during drag)
        //
        // New scroll offset = (field_0xd8 - (field_0x88 -
        //                      (touch[m_TouchId].y - field_0x7c))) * -0.5
        // This is: offset = (anchorOffset - (currentY - anchorY)) * -0.5
        // Factor -0.5 converts finger delta to scroll velocity direction.
        //
        // If field83_0xcc != nullptr:
        //   check IsTouchDown(field83_0xcc) == 0 -> clear field83_0xcc
        //
        // Drag threshold: if abs(touch[m_TouchId].y - field_0x7c) > DAT_0015be00 (0.001f):
        //   m_bDragging = 1  (field_0xc8)
        //   if (abs(delta) > 5.0 && field83_0xcc != nullptr):
        //     fire vtable[+0x30] (slot 12) on item to cancel pending tap
        //     field83_0xcc = nullptr
    }
```

---

### Phase 4 — Velocity Integration + Spring-back

```c
// Apply velocity Vec3 to scroll offset:
Vec3Scale_ScrollMenu(&field_0x90);    // scale by DAT_0015b740 = 0.9f (friction)
field_0xd4 += field_0x90;             // velocity += friction-damped pending
field_0x90 = pos - pos;              // (compute relative displacement _Stack_68 = field_0x90 - pos)

// Determine visible range limits
float rangeTop = DAT_0015be04;   // -160.0f  (scroll lower bound)
float rangeBot = DAT_0015be08;   //  160.0f  (scroll upper bound)
if (field_0xd0) {                // visibility mode flag
    rangeTop = pos.y - field60_0xa0;   // clamp to [pos.y - height, pos.y]
    rangeBot = pos.y;
}
```

---

### Phase 5 — Per-Item Position + SetOnscreen + Closest-Item Tracking

**Row pitch**: `item->GetHeight()` dispatches through `vtable[+0x08]` = `ScrollingMenuItem::GetHeight`
which returns `*(this + 0x24)` (`m_ParamWidth` in port naming). For ShopListItems this is **80.0f**
(set by `ShopListItem::Create`). Row pitch per item = `80.0f * 2 = 160.0f`.

See "ShopListItem Row Height" section above for full analysis.

```c
ScrollingMenuItem* closestItem = nullptr;
float closestDist = DAT_0015be10;   // 10000.0f initial max
this->m_ClosestIdx = 0;             // field76_0xbc

float curY = _Stack_68.y;    // start of item layout = scroll displacement + something

for (int i = 0; i < m_Items.size(); i++) {
    ScrollingMenuItem* item = m_Items[i];
    float halfH = item->GetHeight() * 0.5f;   // vtable[+0x08]

    // Closest-item tracking
    if (m_SelectedIdx < 0) {
        // No specific drag target — find globally closest to center
        float distToCenter = abs(_Stack_68.y - pos.y);
        if (distToCenter < closestDist) {
            m_ClosestIdx = i;
            // distToSnap = abs(curY - field_0xd8) / field_0x94
            this->field78_0xc4 = distToSnap;
            closestDist = distToCenter;
        }
    } else if (m_SelectedIdx == i) {
        // This is the dragged item
        m_ClosestIdx = m_SelectedIdx;
        // distToSnap calc same as above
        closestItem = item;
        closestDist = abs(...)
    }

    // SetOnscreen: item is visible if curY-halfH < rangeTop AND curY+halfH > rangeBot
    bool onscreen;
    if (curY - halfH > rangeTop || curY + halfH < rangeBot)
        onscreen = false;
    else
        onscreen = true;
    item->vtable[+0x24](item, onscreen ? 1 : 0);   // SetOnscreen

    // Move: assign world position from running layout cursor
    Vec3 movePos(_Stack_74);     // copy of current layout Y
    item->vtable[+0x18](item, &movePos);             // Move

    // Advance cursor by item height
    curY -= halfH;   // advance TWICE (before and after each item)
    curY -= halfH;

    i++;
}
```

`_Stack_68.y` decrements by each item's full height as the loop progresses. The first item sits at the top of the viewport and subsequent items stack downward.

---

### Phase 6 — Click Callback (tap release without drag)

```c
// After item loop:
float snapDist = fVar18 - field_0xd8;   // remaining snap distance

if (!m_bDragging) {   // field_0xc8 == 0
    // Check if snap distance is near zero (|snapDist| < 2.0f)
    if (abs(snapDist) < 2.0f) {
        // Check if velocity is near zero (|field_0x94| < 0.5f)
        float vel = field_0x94;
        if (abs(vel) < 0.5f) {
            // Set "tap processed" flag
            m_bTouchProcessed = 1;   // field_0xc9
            // Fire callback only if touch was fully released (iVar2 == -1 from IsTouchDown)
            // AND a collided item pointer exists
            if (iVar2 == -1 && closestItem != nullptr) {
                ScrollingMenuItem::CallClickedMenuItemCallback(closestItem);
                // -> fires item->m_Callback(item)
                // -> for ShopListItem: calls ShopScreen::ClickedOnShopItem
            }
        }
    }
}
```

The callback fires on the **item closest to center** at the moment of release, not on whichever item was initially tapped.

---

### Phase 7 — Scroll Bounds + Spring-back

```c
float offset = field_0xd8;   // current scroll offset

if (offset <= 0.0f || m_SelectedIdx >= 0) {
    // Lower half of range or actively dragging
    float totalScrollH = field60_0xa0 - field62_0xa8;   // m_Height - m_TotalWidth (sic)
    if (offset >= totalScrollH || m_SelectedIdx >= 0) {
        // Still in-bounds or dragging — apply spring
        if (m_TouchId != -1) return;   // finger still down, no spring

        float vel = field_0x94;
        bool velSmall;
        if (vel < 0.0f) velSmall = (vel >= DAT_0015be14);  // >= -0.1f
        else            velSmall = (vel <  DAT_0015be20);   // < 0.1f

        if (!velSmall) return;   // still moving fast, let it coast

        // Snap step: offset += snapDist * DAT_0015be20 (0.1f)
        field_0xd8 = offset + snapDist * 0.1f;
        return;
    }
    // offset < totalScrollH -> scrolled past bottom
    // Spring toward bottom: offset = offset + (totalScrollH - offset) * 0.25f
    field_0xd8 = offset + (totalScrollH - offset) * 0.25f;
} else {
    // offset > 0 -> scrolled past top
    // Spring toward 0: offset *= 0.75f
    field_0xd8 = offset * 0.75f;
}

Vec3Scale_ScrollMenu(&field_0x90);   // apply friction to velocity again
```

---

### Resolved DAT Constants

| Address | Bytes (LE) | Float value | Role |
|---------|------------|-------------|------|
| `DAT_0015b740` | `66 66 66 3f` | **0.9f** | `Vec3Scale_ScrollMenu` multiplier (velocity friction per tick) |
| `DAT_0015ba10` | `00 24 74 49` | **~2001100** | Large sentinel initial "min dist" for snap search |
| `DAT_0015ba14` | `00 00 00 00` | **0.0f** | Initial closest-dist accumulator (reset at touch-release snap) |
| `DAT_0015ba18` | `66 66 66 3f` | **0.9f** | Velocity friction (same as b740; used in spring-deadzone check) |
| `DAT_0015ba1c` | `cd cc 4c bd` | **-0.05f** | Spring deadzone lower bound (velocity near-zero low) |
| `DAT_0015ba20` | `cd cc 4c 3d` | **0.05f** | Spring deadzone upper bound (velocity near-zero high) |
| `DAT_0015ba24` | `0a d7 23 bc` | **-0.01f** | Scroll lower bound during in-bounds check |
| `DAT_0015ba28` | `0a d7 23 3c` | **0.01f** | Scroll upper bound / near-zero threshold |
| `DAT_0015ba2c` | `00 40 1c 46` | **10000.0f** | Initial closest-to-center sentinel distance |
| `DAT_0015be00` | `6f 12 83 3a` | **0.001f** | Drag detection threshold (abs delta must exceed this) |
| `DAT_0015be04` | `00 00 20 c3` | **-160.0f** | Scroll lower bound (default visible range top) |
| `DAT_0015be08` | `00 00 20 43` | **160.0f** | Scroll upper bound (default visible range bottom) |
| `DAT_0015be10` | `00 40 1c 46` | **10000.0f** | Initial per-item closest-dist sentinel in item loop |
| `DAT_0015be14` | `cd cc cc bd` | **-0.1f** | Velocity near-zero lower bound (spring hold gate) |
| `DAT_0015be20` | `cd cc cc 3d` | **0.1f** | Velocity near-zero upper bound AND snap step factor |
| `5.0f` (literal) | `00 00 a0 40` | **5.0f** | Finger-move threshold to cancel item tap callback |
| `0.75f` (literal) | `00 00 40 3f` | **0.75f** | Spring-back multiplier when scrolled past top |
| `0.25f` (literal) | `00 00 80 3e` | **0.25f** | Spring-forward multiplier when scrolled past bottom |
| `2.0f` (literal) | `00 00 00 40` | **2.0f** | Snap-distance "near-zero" gate for click-fire |
| `0.5f` (literal) | `00 00 00 3f` | **0.5f** | Velocity "near-zero" gate for click-fire |
| `-0.5f` (literal) | `00 00 00 bf` | **-0.5f** | Factor converting drag delta to scroll velocity |

---

### Helper Functions Called by Update

| Function | Address | What it does |
|----------|---------|--------------|
| `TouchInRegion(x0,x1,y0,y1,hint)` | `0x001691cc` | Find touch slot in rect; returns -1 if none |
| `IsTouchDown(slot)` | `0x00169144` | Returns 0/1/2 for state (up/just-down/held) |
| `ScrollingMenu::Collide(this, slot)` | `0x0015af4c` | Hit-test items against touch slot; calls `vtable[+0x34]` on each |
| `Vec3Scale_ScrollMenu(vec3*)` | `0x0015b714` | Multiplies all 3 components of a Vec3 by `DAT_0015b740 = 0.9f` |
| `ScrollingMenuItem::CallClickedMenuItemCallback()` | `0x0015c27c` | Fires `item->m_Callback(item)` at `+0x30` via Delegate1 |
| `item->vtable[+0x08](item)` (slot 2) | per subclass | `GetHeight()` — used for half-height in layout and SetOnscreen |
| `item->vtable[+0x18](item, Vec3*)` (slot 6) | `0x0015aea8` | `Move(Vec3)` — sets item world position |
| `item->vtable[+0x24](item, bool)` (slot 9) | `0x0013ce10` | `SetOnscreen(bool)` — marks item as visible or not |
| `item->vtable[+0x30](item)` (slot 12) | `0x00147970` | Cancel-tap signal (called when drag > 5 units, clears item highlight) |
| `item->vtable[+0x34](item, slot)` (slot 13) | `0x00147974` | Hit-test query (used by Collide; returns non-zero if touch is on item) |
| `item->vtable[+0x38](item)` (slot 14) | `0x00147978` | Touch-release signal (called when finger leaves inner region) |

---

### Field Offset Verification — Port vs Binary

The port's `ScrollingMenu.h` has several incorrect field assignments that must be fixed before implementing Update:

| Port field name | Port offset | Binary offset | Correct? | Fix |
|----------------|-------------|---------------|----------|-----|
| `m_TouchId` | stated `+0x74` | `+0x74` (field22_0x74) | YES | OK |
| `m_SelectedIdx` | stated `+0xc0` | `+0xc0` is `field77_0xc0` (drag target) | **WRONG label** | Rename to `m_DragTargetIdx`; actual "selected" = `m_ClosestIdx` at `+0xbc` |
| `m_ClosestIdx` | stated `+0xbc` | `+0xbc` (field76_0xbc) | YES (label OK) | OK |
| `m_ScrollOffset` | stated `+0x88` | `+0x88` is touch anchor Y; true scroll offset = `+0xd8` | **WRONG offset** | Move `m_ScrollOffset` to `+0xd8`; add `m_AnchorY` at `+0x88` |
| `m_bDragging` | stated `+0xc8` | `+0xc8` (field_0xc8) | YES | OK |
| `m_bTouchProcessed` | stated `+0xc9` | `+0xc9` (field_0xc9) | YES | OK |
| `m_fieldCA` | stated `+0xca` | `+0xca` (field_0xca) | YES | OK |
| `m_Width` | stated `+0x9c` | `+0x9c` (field59_0x9c) init 320.0f | YES (value) | OK |
| `m_Height` | stated `+0xa0` | `+0xa0` (field60_0xa0) init 240.0f | YES (value) | OK |
| `m_ItemHeight` | stated `+0xa4` | `+0xa4` (field61_0xa4) init -120.0f | **WRONG value** — init is -120.0f not 25.0f | Fix ctor default |
| `m_TotalWidth` | stated `+0xa8` | `+0xa8` (field62_0xa8) | **WRONG label** — used as total scroll height in bounds check | Rename to `m_TotalScrollHeight` |
| `m_TotalHeight` | stated `+0xac` | `+0xac` (field63_0xac) | Check usage | Verify |
| (missing) | — | `+0xcc` (field83_0xcc) | **MISSING** | Add `ScrollingMenuItem* m_pCollidedItem` at `+0xcc` |
| (missing) | — | `+0xd0` (field_0xd0) | **MISSING** | Add `bool m_bConstrainedView` at `+0xd0` |
| (missing) | — | `+0xd4` (field_0xd4) Vec3 | **MISSING** | Add `Vec3 m_Velocity` at `+0xd4` (scroll velocity Vec3) |
| (missing) | — | `+0x90` (field_0x90) Vec3 | **MISSING** | Add `Vec3 m_PendingVelocity` at `+0x90` |
| (missing) | — | `+0x78` (field_0x78) Vec3 | **MISSING** | Add `Vec3 m_TouchAnchorPos` at `+0x78` (finger-down touch position) |
| (missing) | — | `+0x84` (field_0x84) Vec3 | **MISSING** | Add `Vec3 m_AnchorOffset` at `+0x84` (scroll offset at finger-down) |

---

## See Also

- [Menu flow system](../systems/menu-flow.md) -- screen navigation graph
- [Screens & effects functions](../functions/screens-effects.md) -- screen callbacks
- [HUD structs](../structs/hud.md) -- base class for screen controls
- [Font slots](../engine/font.md) -- Game+0x54 = font_fruit_ninja.fnt
