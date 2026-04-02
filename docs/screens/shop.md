# ShopScreen

## ShopScreen

**Constructor**: `0x0015cdac` -- `ShopScreen::ShopScreen(DojoScreen*)`
**Update**: `0x0015e1f4` -- `ShopScreen::Update(float)` (381 lines)
**Draw**: `0x0015dd50` -- `ShopScreen::Draw(float*)`

**Base class**: `HUDControl3d`

**Struct size**: ~0xBC (highest field = `field_0xb8` + 4)

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00-0x73 | | (HUDControl3d base) | |
| 0x74 | SmartPtr\<Texture\> | m_Texture | SetNull in constructor |
| 0x7C | float | m_TransitionAlpha | Initialized to 0; lerped toward 1.0 |
| 0x80 | int | m_LayerFlagsAlt | Set to 0x40 when no active splats |
| 0x84 | MenuButton* | m_BuyButton | Created lazily; has OnPress + OnHighlight delegates |
| 0x88 | float | m_BuyDelay | Timer decremented by dt; controls when buy button appears |
| 0x8C | MenuButton* | m_EquipButton | Created lazily for equip action |
| 0x90 | DojoScreen* | m_ParentDojo | Parent screen |
| 0x94 | ShopListControl* | m_ShopList | Scrollable list; selection tracked |
| 0x98 | int | m_SelectedIndex | Compared against ShopList current index |
| 0xAC | float | m_ScrollOffset | Computed from list item count + 0.5 |
| 0xB4 | int | m_AnimFrame | Animation counter for scroll |
| 0xB8 | int | m_State | State machine index |

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Transition in: lerps alpha to 1.0. On completion, creates m_BuyButton (0x84) with QCallee\<ShopScreen\>, removes all splats, sets state=1. |
| 1 | Active: manages buy delay timer, creates equip button (0x8C) if needed. Calls `SetSelected()` on list changes. Manages `ItemManager::IsEquipped` checks. |
| 2 | Transition out (to dojo): `alpha *= 0.75`. When below threshold and parent exists, marks parent `m_bNoDestructor=1`, self pending removal, changes GameState to 8. |
| 3 | Buy animation: `alpha *= 0.75`. On completion, creates a new buy button with fruit animation. Flings old button fruit offscreen with random velocity. |
| 4 | Resets layer flags to 0x80. |
| 5-6 | Similar to 3 -- fruit fling animation for purchased item. |
| 7 | Same as 2 -- transition out variant (goes back to dojo). |

---

