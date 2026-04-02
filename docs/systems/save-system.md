# Save System

## Save System (FruitSaveData)

### Data Storage

`FruitSaveData` (0x238 bytes) uses two `map<ulong, SliceTotal>` containers to store per-fruit-type stats. The key is a `StringHash` of the stat name (e.g. "apple_sliced").

**SliceTotal** (0x48 bytes): contains the stat name string (char[64]) + uint count + ulong hash.

### SaveCurrentData (0x16ccc8, 108 lines)

Called on game exit / pause / game-over. Copies Game state into a FruitSaveData snapshot:

```
saveData.field73_0x64 = GetCurrentScore(0)         // current score
saveData.field74_0x68 = GetCurrentMissCount(0)     // miss count
saveData.field75_0x6c = Game.gameMode               // mode (0-3)
saveData.m_CriticalChance = Game.m_ScoreThreshold   // score threshold
saveData.field80_0x74 = some_global                  // highscore reference
saveData.field81_0x78 = some_global2
saveData.field226_0x130 = Game.m_BombHitTimer        // (conditionally)
```

Also saves `GameOverScreen` state (field_0x114..0x128) if game-over screen is active.

### Key Save Functions

| Function | Address | Purpose |
|----------|---------|---------|
| SaveCurrentData | 0x0016ccc8 | Snapshot Game → FruitSaveData |
| FruitSaveData::AddToTotal | 0x0012b21c | Add stat value to map (or create entry) |
| FruitSaveData::GetTotal | 0x0012a110 | Look up stat by hash |
| FruitSaveData::Update | 0x0012b3dc | Tick achievement timers, process queued items |
| FruitSaveData::SaveGameState | 0x00129ca8 | Clear entity states list |
| FruitSaveData::ClearCombo | 0x00129b94 | Reset combo tracking |
| FruitSaveData::PublishUnlockedAchievements | 0x0012a194 | Push to network |
| GameTaskSaveOnExit | 0x0016cf40 | Called on app exit |

### Persistence Model

Stats are stored as key-value pairs in `map<ulong, SliceTotal>`:
- **Primary map** (+0x00): cumulative all-time stats
- **Secondary map** (+0x18): per-session stats

Achievement tracking uses `map<ulong, AchievementItem>` at +0x158 and +0x170, with countdown timers processed in `Update()`.

Score maps at +0x194 (4 × `map<int,int>`) store per-mode score history.

---

## See Also

- [Data structs](../structs/data.md) -- FruitSaveData layout
- [Game flow functions](../functions/game-flow.md) -- SaveCurrentData call sites
