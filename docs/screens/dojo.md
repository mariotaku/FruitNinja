# DojoScreen

## DojoScreen

**Constructor**: `0x00137b90` -- `DojoScreen::DojoScreen()`
**Update**: `0x00138414` -- `DojoScreen::Update(float)` (247 lines)
**Draw**: `0x0013822c` -- `DojoScreen::Draw(float*)`

**Base class**: `BaseScreen -> HUDControl3d`

**Struct size**: ~0xA4 (highest field = `field4_0xa0` + 4)

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00-0x93 | | (BaseScreen base) | BaseScreen adds fields at 0x8C (transition alpha) and 0x90 (state) |
| 0x8C | float | m_TransitionAlpha | (in BaseScreen) Transition interpolation factor |
| 0x90 | int | m_State | (in BaseScreen) State machine |
| 0x94 | MenuButton* | m_PlayButton | Main play/dojo button; created lazily with QCallee\<DojoScreen\> |
| 0x98 | MenuButton* | m_ShopButton | Shop button; checks `ItemManager::AreNewItems()` for "new" badge |
| 0x9C | MenuButton* | m_AboutButton | About/credits button |
| 0xA0 | int | m_Unknown | Set to 0 in constructor |

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Transition in: calls `BaseScreen::UpdateButtons`. Lerps `m_TransitionAlpha` toward 1.0 (step 0.25). Lazily creates all three buttons: m_PlayButton (0x94), m_ShopButton (0x98 -- with `ItemManager::AreNewItems` new-symbol check), m_AboutButton (0x9C). When alpha threshold reached, sets state=1. |
| 1 | Idle: checks `ItemManager::AreNewItems()` each frame to update shop button new-symbol. |
| 2-3 | Transition out: `alpha *= 0.75`. When below threshold, nulls button pointers. State 3 creates `AboutScreen` child. State 2 creates `ShopScreen` child. |
| 4 | Network/dashboard: waits for no active entities, then calls `NetworkManager::LaunchDashboard`, resets to state 0. |
| 6 | Fade out for game start: `alpha *= 0.75`. When below threshold, marks pending removal and sets GameState = 8. |

---

