# Scoring Pipeline

## Scoring Pipeline

1. **SlashEntity::Update** → iterates Fruit/Bomb entities via ActorManager
2. For each: **SlashEntity::CollideWithEntity** (line–circle segment test)
3. On hit: `entity→vtable→CollisionResponse(entity, slash, 0, 0, &bladeVel)`
4. **Fruit::CollisionResponse:**
   - Check critical probability (`Game.m_ScoreThreshold / WaveManager::GetCriticalChance`)
   - Play SFX, spawn juice particles, AddSlice() visual
   - Compute points: base = `FRUIT_INFO.m_BaseScore`; critical += bonus
   - `AddToCurrentScore(points, playerIdx, true, false)`
   - `WaveManager::AddSpeed` in Zen mode
   - FruitSaveData tracking (per-fruit stats, achievements)
5. **AddToCurrentScore:**
   - Apply `GetScoreMultiplyer × delegate modifier`
   - Add to `Game.currentScore` (+0x18)
   - Decrement `Game.m_ScoreThreshold` (+0x30) on tier crossing → play SFX
   - `FruitSaveData::AddToTotal` for stat tracking
6. **SlashEntity combo scoring** (triggered on timer expiry):
   - Decrement `Game.m_ScoreThreshold`
   - `AddToCurrentScore(comboCount, entityType, true, ...)`
   - `BonusManager::AddCombo` in Zen mode
   - `Coin::MakeCoins` for coin rewards

---

## Asset Loading Order (GameInitialise)

1. MenuButton::LoadContent
2. Fruit::LoadInfo
3. SplatEntity::LoadContent
4. SlashEntity::LoadContent
5. Bomb::LoadContent
6. GameOverScreen::LoadContent
7. PowerUpShop::LoadContent
8. PreloadSounds

---

## See Also

- [Scoring functions](../functions/scoring.md) -- score calculation pseudocode
- [SlashEntity](../entities/slash-entity.md) -- combo detection
