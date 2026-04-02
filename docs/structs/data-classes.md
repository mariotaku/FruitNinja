# Data Classes (Highscores, Bonuses)

## FNHighscore (size ~0x54)

Single highscore entry.

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | char[32] | m_Name | Player name (SafeStrncpy31) |
| +0x1f | byte | m_field1f | = 0 |
| +0x20 | char[32] | m_DisplayName | Formatted display name |
| +0x3f | byte | m_field3f | = 0 |
| +0x40 | ulong | m_NameHash | StringHash(name) |
| +0x44 | int | m_Rank | Rank/position |
| +0x48 | int | m_Score | Score value |
| +0x4c | void* | m_UserData | Platform-specific user handle |
| +0x50 | byte | m_bIsCurrentUser | Set by IsCurrentUser() check |

Constructor (0x111f4c): `FNHighscore(name, hash, score, rank, userData, displayName)`

## FNHighscoreList

Manages a `list<FNHighscore>` for a single leaderboard.

### Key Methods

| Function | Address | Purpose |
|----------|---------|---------|
| AddScore | 0x1116e4 | Add entry: validates week, creates FNHighscore, inserts sorted |
| AddPlayerScore | 0x111874 | Add current player's score |
| GetHighscoreForUser | 0x1114fc | Look up by user handle |
| GetFirst / GetNext | 0x13cecc / 0x13cef4 | Iterator for display |
| PrepareForDataRetrieval | 0x11139c | Reset state before fetch |

### AddScore Flow (103 lines)

1. Validate week number (discard old weekly scores)
2. Format display name: uppercase, truncate to 10 chars with "..."
3. Create `FNHighscore` with StringHash of name
4. Insert into sorted `list<FNHighscore>` by score (descending)
5. Set flags: `[9]=loaded, [8]=hasData, [10]=valid`

### Struct Fields (from list management)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | list\<FNHighscore\> | m_Scores | Sorted by score descending |
| +0x08 | byte | m_bHasData | = 1 after data received |
| +0x09 | byte | m_bLoaded | = 1 after AddScore |
| +0x0a | byte | m_bValid | = 1 if data is current |
| +0x0b | byte | m_bWeekly | Weekly vs all-time flag |
| +0x0c | byte | m_bField0c | |

---

## Bonus (size ~0xD4)

Single bonus award definition. Loaded from `bonusawards.xml`.

### Struct Layout (from Parse, 109 lines)

| Offset | Type | Name | XML Source |
|--------|------|------|------------|
| +0x00 | int | m_MinScore | `min` attribute |
| +0x04 | int | m_MaxScore | `max` attribute (or same as min if `score` set) |
| +0x38 | int | m_field38 | `coins` attribute |
| +0x3c | int | m_field3c | `value` attribute |
| +0x40 | char[128] | m_Text | Element text content (localised string key) |
| +0xc0 | vector\<ulong\> | m_FruitHashes | StringHash of each fruit in `types` word list |
| +0xcc | uint | m_AchievementHash | StringHash of `achievement` attribute (0 if none) |
| +0xd0 | SmartPtr\<Texture\> | m_Texture | From `texture` attribute |

### Bonus::Parse XML

```xml
<bonus min="10" max="20" coins="5" value="1" texture="tex_name"
       types="apple,banana" achievement="achieve_name">
  BONUS_TEXT_KEY
</bonus>
```

### Bonus::IsAchieved (0x10df38)

Checks if the player's stats meet the bonus criteria by looking up fruit totals in FruitSaveData.

---

## BonusType

Container for a category of bonuses. Loaded from `bonusawards.xml`.

### Struct Layout (from Parse, 71 lines)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | map\<ulong, int\> | m_FruitCounts | Fruit hash → count needed |
| +0x18 | SmartPtr\<Texture\> | m_Texture | Category texture |
| +0x24 | byte | m_bField24 | = 0 initially |
| +0x28 | list\<Bonus\> | m_Bonuses | Child bonus entries |

### BonusType::Parse XML

```xml
<bonustype types="apple,banana,orange" texture="tex_name">
  <bonus min="10" ...>TEXT_KEY</bonus>
  <bonus min="50" ...>TEXT_KEY</bonus>
</bonustype>
```

Iterates `<bonus>` children, creates Bonus for each via `Bonus::Parse`.

### BonusType::GetBest (0x10e094)

Returns the highest-achieved bonus in this category.

### BonusType::UnlockAchievements (0x10e12c)

Checks all bonuses and unlocks associated achievements.

---

## BonusAwardHud (size ~0x48)

Visual display data for a single bonus award in BonusScreen. Plain value type (no vtable), stored in `vector<BonusAwardHud>`.

### Struct Layout (from copy constructor, 55 lines)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | float[4] | m_field00 | Position/size data |
| +0x10 | float[4] | m_field10 | Animation parameters |
| +0x20 | float[4] | m_field20 | More animation data |
| +0x30 | float[4] | m_field30 | |
| +0x40 | int | m_field40 | Copied separately in ctor |

Total = 0x44 bytes of plain data, bulk-copied in 4×16-byte chunks.

---

## bonusawards.xml Structure

```xml
<bonusInfoFile version="1.0.0">
  <bonustype types="apple,banana,orange" texture="bonus_icon">
    <bonus min="10" coins="1" texture="bonus_tex">BONUS_10_FRUITS</bonus>
    <bonus min="50" coins="5" achievement="bonus_50">BONUS_50_FRUITS</bonus>
    <bonus score="100" coins="10">BONUS_100_SCORE</bonus>
  </bonustype>
  <!-- more bonustype entries... -->
</bonusInfoFile>
```

---

## See Also

- [Game Over screen](../screens/game-over.md) -- FNHighscore display
- [Resources](../resources.md) -- bonusawards.xml format
