<!-- Analysed: 2026-04-13T00:00 -->

# ResetGameEntities

## Addresses

| Symbol | Address |
|--------|---------|
| `ResetGameEntities` (impl) | `0x0016a058` |
| thunk (via PTR `0x001ee0e4`) | `0x000f94bc` |

Total lines (impl): 40 (including loop boilerplate). Called via the thunk from all callers.

---

## Signature

```c
void ResetGameEntities(bool killAll);
```

**`killAll`** gates whether unsliced fruits are force-marked sliced before being launched off-screen:

- `false` — only fruits in a multiplayer game (`IsSameScreenMultiplayer() == true`) OR fruit whose
  flag byte `*(game + 6) != 0` (Zen mode flag) are auto-marked sliced. Others get a proper
  `CollisionResponse + Slice` call so score/effects fire.
- `true` — every fruit gets `m_bSliced = 1` unconditionally (no score, no juice). Used by
  `InstantLevelDestroy` (PowerUp nuke).

---

## Constants

| DAT | Value | Meaning |
|-----|-------|---------|
| `DAT_0016a190` | `-480.0f` (`0xC3F00000`) | Off-screen Y to fling all entities to |
| `DAT_0016a194` | `0.0f` | dt passed to entity vtable Update after repositioning |
| `DAT_0016a198` | `400.0f` (`0x43C80000`) | Distance-squared threshold: if fruit is > 20 units from camera origin, normalise impulse vector and scale to 20 before passing to CollisionResponse |

---

## Pseudocode

```c
void ResetGameEntities(bool killAll) {
    // 1. Reset all 16 SlashEntities (4-byte step, 16 iterations = 0x40 bytes)
    for (int i = 0; i < 16; i++)
        SlashEntity::Reset();

    // 2. Flush all active Bombs — fling off-screen
    ActorManager* am = ActorManager::GetInstance();
    Bomb* bomb = (Bomb*)am->GetEntityFirst(1 /*type=Bomb*/, iter);
    while (bomb) {
        bomb->base.pos.y  = -480.0f;   // DAT_0016a190
        bomb->base.vel.y  = -1.5f;
        Bomb::Chuck(bomb, 0.0f);        // DAT_0016a194 as chuck param
        bomb->base.vtable->Update(0.0f, bomb);
        bomb = (Bomb*)am->GetEntityNext(1, iter);
    }

    // 3. Process all active Fruits
    Fruit* fruit = (Fruit*)am->GetEntityFirst(0 /*type=Fruit*/, iter);
    while (fruit) {
        Fruit::Chuck(fruit, 0.0f);

        // Determine if fruit should be auto-sliced without effects
        bool zenMode = (*(game + 6) != 0);
        if (zenMode || killAll) {
            fruit->m_bSliced = 1;
        }

        if (!fruit->m_bSliced) {
            // Compute impulse from camera origin
            Vec3 impulse = fruit->base.pos - cameraOrigin;  // cameraOrigin at (game+0xcc)
            float distSq = impulse.MagnitudeSqr();
            if (distSq > 400.0f) {                          // DAT_0016a198
                impulse.Normalise();
                impulse *= 20.0f;
            }
            fruit->base.vtable->CollisionResponse(fruit, 0, 0, 0, &impulse);
            Fruit::Slice(fruit);
        }

        // Fling both halves off-screen
        fruit->base.pos.y   = -480.0f;  // DAT_0016a190
        fruit->m_HalfB_pos.y = -480.0f;
        fruit->base.vel.y   = -1.5f;
        fruit->m_HalfB_vel.y = -1.5f;
        fruit->base.vtable->Update(0.0f, fruit);  // DAT_0016a194

        fruit = (Fruit*)am->GetEntityNext(0, iter);
    }

    // 4. Multiplayer: scrub juice splats
    if (IsSameScreenMultiplayer())
        SplatEntity::RemoveAllSplats();
}
```

---

## Field Writes Summary

| Entity | Field | Offset | Value | Notes |
|--------|-------|--------|-------|-------|
| Bomb | `base.pos.y` | Entity+0x14 | -480.0f | Off-screen |
| Bomb | `base.vel.y` | Entity+0x20 | -1.5f | Downward |
| Fruit | `base.pos.y` | Entity+0x14 | -480.0f | Off-screen |
| Fruit | `m_HalfB_pos.y` | Fruit+0xBC (off 184+4) | -480.0f | Second half |
| Fruit | `base.vel.y` | Entity+0x20 | -1.5f | |
| Fruit | `m_HalfB_vel.y` | Fruit+0xC8 (off 196+4) | -1.5f | |
| Fruit | `m_bSliced` | Fruit+0xB4 (off 180) | 1 | Conditional |

---

## Cross-References

| Caller | Address | `killAll` | Context |
|--------|---------|-----------|---------|
| `UpdateBombHit` | `0x0016a1d2` | `false` | Bomb hit timer crosses 1.5 s threshold downward |
| `EndRetryLevel` | `0x0016a24e` | `false` | Player retries after game over |
| `InstantLevelDestroy` | `0x0016a2d0` | `true` | PowerUp nuke / instant clear |
