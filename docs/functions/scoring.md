# Scoring Functions

## Scoring

### AddToCurrentScore (0x0010a7ac, 86 lines)

```c
void AddToCurrentScore(int points, int playerIdx, bool trackFruit, bool sendNetPacket) {
    int prevScore = game->currentScore;
    int multiplier = GetScoreMultiplyer(0);
    int modified = scoreDelegate.Call(points * multiplier);
    int newScore = prevScore + modified;
    game->currentScore = max(0, newScore);
    
    // Score tier SFX
    int prevTier = prevScore / TIER_SIZE;
    int newTier = newScore / TIER_SIZE;
    if (prevTier < newTier && game->comboCounter > 0) {
        game->comboCounter--;
        GameSound::SFXPlay(game->pGameSound, "score_tier", 1.0, 1.0, delegate);
    }
    
    // Stat tracking
    if (points > 0 && trackFruit && playerIdx < 2) {
        FruitSaveData::AddToTotal(game->pSaveData, "fruit_sliced", hash, points, true, false);
        game->fruitTotal = result;
    }
    
    // Network sync
    if (playerIdx == 1 && sendNetPacket)
        PointsPacket::Send(waveManager->syncToken, points);
}
```

### StringHash (0x0019c5d4, 90 lines)

See `docs/engine/string-hash.md` for full C implementation.

---

