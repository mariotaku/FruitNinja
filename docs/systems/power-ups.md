# Power-Up System

## PowerUp Struct (estimated ~0xB8 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x04 | list\<GameModifier*\> | m_Modifiers | 12 bytes; all attached modifiers |
| +0x0c | uint | m_NameHash | StringHash of name |
| +0x10 | char[64] | m_Name | From XML "name" attribute |
| +0x50 | char[64] | m_DisplayName | Uppercase-first copy of name |
| +0x90 | byte | m_bIsPurchasable | From XML; or set if has PurchaseInfo |
| +0x91 | byte | m_bIsSpecial | From XML attribute |
| +0x94 | PurchaseInfo* | m_pPurchaseInfo | Non-null if purchasable (0xC4 bytes) |
| +0xa0 | float | m_TotalTime | Max duration of all modifiers |
| +0xa4 | Colour | m_Colour | Display colour from XML |
| +0xac | SmartPtr\<Texture\> | m_Texture1 | Power-up icon texture |
| +0xb0 | SmartPtr\<Texture\> | m_Texture2 | MissControl popup texture |
| +0xb4 | ScreenEffect* | m_pScreenEffect | Visual screen effect (0x50 bytes) |

## PowerUpManager Struct (partial)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | map\<ulong,PowerUp*\> | m_AllPowerUps | 24 bytes; all loaded power-ups by hash |
| +0x18 | list\<PowerUp*\> | m_ActivePowerUps | 12 bytes; currently active |
| +0x20 | map\<ulong,PowerUp*\> | m_ActiveByHash | 24 bytes; quick lookup for active |
| +0x60 | int | m_field60 | |
| +0x64 | float | m_DtMod | Time-scale multiplier (Frenzy = slow-mo) |
| +0x6c | float | m_field6c | = 1.0 |
| +0x70 | float | m_field70 | = 1.0 |
| +0x78 | int | m_ScoreGainMult | |
| +0x7c | int | m_ScoreGainFactor | |
| +0x88 | float | m_field88 | |

## GameModifier Types

Parsed from XML child elements of `<powerup>`. Each type inherits GameModifier base.

| Type | Size | XML Tag | Purpose |
|------|------|---------|---------|
| **ScoreModifier** | 0x3c | `"score"` | Modifies score gain/loss multipliers |
| **TimeModifier** | 0x3c | `"time"` | Modifies game time scale (Frenzy slow-mo) |
| **SlashModifier** | 0x40 | `"slash"` | Modifies blade behavior (blade type changes) |
| **WaveModifier** | 0x44 | `"wave"` | Modifies wave spawn parameters |

### GameModifier Base

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | void* | vtable | Virtual: Activate, UpdateSpecific, etc. |
| +0x04 | float | m_Duration | Time this modifier lasts |
| +0x18 | bool | m_bApplied | Set after first application |
| +0x1c | PowerUp* | m_pOwner | Back-pointer to parent PowerUp |

## PowerUp::Parse (0x1194f0, 128 lines)

Loads power-up definition from XML:

```xml
<powerup name="frenzy" special="true" purchasable="false"
         colour="R,G,B,A" icon="tex_name" popup="tex_name">
  <purchase cost="100" ... />
  <screeneffect ... />
  <time duration="5.0" scale="0.5" />
  <score gainfactor="2" duration="5.0" />
  <slash ... />
  <wave ... />
</powerup>
```

## ActivatePower Flow (0x1197c4, 118 lines)

```
PowerUpManager::ActivatePower(hash, position, extraParam):
  1. Look up PowerUp by hash in m_AllPowerUps
  2. Check m_ActiveByHash — skip if already active
  3. Clone() the PowerUp template → push to m_ActivePowerUps
  4. If no other timed powers active, OR is special, OR is purchased:
     → Activate immediately
  5. Else (another timed power already running):
     → Find shortest-remaining active power
     → Deactivate it, activate new one
```

## PowerUp::Activate (0x119134, 48 lines)

```
PowerUp::Activate(showPopup, isPurchased, position, extraParam):
  1. If not purchased:
     a. Show MissControl popup with m_Texture2
     b. Deduct coins: AddCoins(-purchaseInfo.cost)
  2. Iterate all GameModifiers:
     → Call modifier.vtable.Activate(isPurchased, extraParam)
  3. If has ScreenEffect:
     → ScreenEffect::Activate()
```

## Key Functions

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| PowerUp::Parse | 0x001194f0 | 128 | Load from XML, create modifiers |
| PowerUp::Activate | 0x00119134 | 48 | Activate modifiers + screen effect |
| PowerUp::Update | 0x00117f90 | — | Tick modifier timers |
| PowerUp::Deactivate | 0x00117f18 | — | Remove active modifiers |
| PowerUpManager::ActivatePower | 0x001197c4 | 118 | Clone + activate by hash |
| PowerUpManager::Update | 0x001189b4 | — | Tick all active powers |
| PowerUpManager::ClearTimedPowers | 0x00118904 | — | Remove all (on bomb hit) |
| PowerUpManager::Load | 0x00119cb0 | — | Load all power-up XML data |
| PowerUpManager::ApplyDtMod | 0x001204dc | — | m_DtMod *= param (time scale) |
| PowerUpManager::SlowClock | 0x001204cc | — | Slow time effect |
