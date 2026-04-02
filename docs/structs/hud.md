# HUD & UI Structs

## HUD & UI

### HUD (size ~0x20)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | list\<HUDControl*\> | controls | std::list = 8 bytes on this ABI |
| +0x08 | float[6] | scales | All init = 1.0f |

### HUDControl (base class)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | HUDControlFns* | vtable | |
| +0x08 | Vec3 | pos | |
| +0x20 | Vec3 | size | Half-extents, clamped to screen |
| +0x2c | float | field_0x2c | Timer or alpha |
| +0x30 | int | m_bActive | Non-zero = active |
| +0x32 | char | m_bNoDestructor | = 0 → call dtor on removal |
| +0x33 | char | m_bPendingRemoval | Set → remove next HUD::Update |
| +0x34 | int | field49_0x34 | = 1 after init |
| +0x38 | Delegate1 | m_RemoveCallback | Called before removal |
| +0x5f | byte | m_Alpha | = 0xff = fully opaque |

### MissControl : HUDControl3d : HUDControl (combo text display)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x7c | byte | m_AnimState | 0=idle, 3=animating |
| +0x7d | byte | m_bVisible | = 1 after MakeCombo |
| +0x80 | float | field5_0x80 | Fade alpha scale |
| +0x84 | byte | m_bActive | = 1 = combo display active |
| +0x85 | byte | m_bFlag | = 1 in MakeCombo |
| +0x88 | int | m_ComboCount | Number of fruits in combo |
| +0x8c | byte | m_bFlag8c | = 1 in Init |
| +0x90 | float | field15_0x90 | = 1.0f |

Pool: up to 9 combo text sprites (digit textures 1..9). `GetFree` (0x00150da4) scans pool for inactive instance.

---
