# AboutScreen

## AboutScreen

**Constructor**: `0x0012ecb8` -- `AboutScreen::AboutScreen(DojoScreen*)`
**Update**: `0x0012f020` -- `AboutScreen::Update(float)`
**Draw**: `0x0012f394` -- `AboutScreen::Draw(float*)`

**Base class**: `HUDControl3d` (size 0x74 for base fields)

**Struct size**: ~0xA0 (highest field = `field_0x9c` + 4)

### Fields

| Offset | Type | Name (inferred) | Notes |
|--------|------|-----------------|-------|
| 0x00 | vtable* | vtable | Set to AboutScreen vtable |
| 0x00-0x73 | | (HUDControl3d base) | Includes pos, size, layer flags at 0x34 |
| 0x74 | SmartPtr\<Texture\> | m_BackgroundTex | Texture assigned from content; dimensions queried for sizing |
| 0x7C | float | m_TransitionAlpha | Lerped 0->1 in state 0, multiplied by 0.75 in state 2 |
| 0x8C | MenuButton* | m_BackButton | Created lazily in Update when state==0 completes (QCallee\<AboutScreen\>) |
| 0x90 | DojoScreen* | m_ParentDojo | Pointer to parent DojoScreen passed in constructor |
| 0x94 | MenuButton* | m_CreditsButton | Created first in Update; text/credits button |
| 0x98 | SmartPtr\<Texture\> | m_Texture2 | Additional texture SmartPtr |
| 0x9C | int | m_State | 0 = transition-in, 1 = idle, 2 = transition-out |

### State Machine (Update)

| State | Behavior |
|-------|----------|
| 0 | Lerps `m_TransitionAlpha` toward 1.0 (step 0.125). On first call, creates m_CreditsButton (MenuButton at 0x94). When alpha reaches threshold, creates m_BackButton (0x8C), sets state=1. |
| 1 | Idle -- buttons are interactive |
| 2 | Fades out: `m_TransitionAlpha *= 0.75`. When below threshold, marks self for removal (`m_bNoDestructor = 1`). |

---


## See Also

- [Menu flow system](../systems/menu-flow.md) -- screen navigation graph
- [Screens & effects functions](../functions/screens-effects.md) -- screen callbacks
- [HUD structs](../structs/hud.md) -- base class for screen controls
