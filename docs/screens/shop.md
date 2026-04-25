<!-- Analysed: 2026-04-25T18:15 -->

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
QCallee<ShopScreen>(auStack_104, QuitShopCallback)  // confirmed via xref DATA at 0x0015e2fc
Delegate0::Delegate0(&DStack_178, auStack_104)       // wraps as press delegate
SmartPtr<Texture>::SmartPtr(&SStack_2c, GameTask + 0x17c)  // texture from GameTask
_Vector3<float>::_Vector3(&Stack_88, 185.0, -105.0, 0.0)   // DAT_0015e55c/560/558
MenuButton::MenuButton(pMVar7, &SStack_2c, &Stack_88, &DStack_178, fruitType, ...)
// fruitType = *(GameTask + 0x78EC) — pre-stored int in GameTask
HUD::AddControl(GameTask->hud, pMVar7, false)
TutorialControl::ResetTutePos(GameTask->tutorialCtrl, pMVar7)
// LAB_0015e874:
pMVar7->m_TargetSize *= 0.825  // DAT_0015e920
pMVar7->m_pFruitPiece->scale *= 0.825
```

Port uses `FruitInfo_GetCount()` as the fruit type (forces bomb spawn, matching the
DojoScreen back-button pattern). The actual fruit type from `*(GameTask + 0x78EC)` is
not resolved, but the port works because MenuButton treats out-of-range types as bombs.

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

## See Also

- [Menu flow system](../systems/menu-flow.md) -- screen navigation graph
- [Screens & effects functions](../functions/screens-effects.md) -- screen callbacks
- [HUD structs](../structs/hud.md) -- base class for screen controls
