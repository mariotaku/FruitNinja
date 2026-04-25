<!-- Analysed: 2026-04-25T16:30 -->

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

## See Also

- [Menu flow system](../systems/menu-flow.md) -- screen navigation graph
- [Screens & effects functions](../functions/screens-effects.md) -- screen callbacks
- [HUD structs](../structs/hud.md) -- base class for screen controls
