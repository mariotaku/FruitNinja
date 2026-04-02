# Reverse Engineering TODO

## Unfinished Analysis

### Power-Up System — MOSTLY DONE

See `docs/systems/power-ups.md`. Core architecture recovered:
- PowerUp struct (~0xB8 bytes): name, hash, modifiers list, textures, screen effect
- 4 modifier types: ScoreModifier, TimeModifier, SlashModifier, WaveModifier
- ActivatePower + Activate flow fully traced
- PowerUpManager maps and active list documented

**Still undecompiled** (lower priority):
- `PowerUpManager::Update` — tick active power timers
- `PowerUpManager::Load` — load all power-up XML definitions
- `PowerUp::Deactivate` — cleanup on expiry
- Individual modifier Parse/UpdateSpecific methods
- `ScreenEffect::Activate/Parse` — visual effect details

### Touch → Slash Input Pipeline — DONE

See `docs/systems/touch-input.md`. Full pipeline recovered:
- GlesForm::TransformTouchPos: raw → game coords (320×480, Y-flipped)
- Touch::__UpdateInternal: ring buffer with TEvnt structs
- SlashEntity::TouchDown/MoveX/MoveY: maps input to entity position
- Multi-touch: 8 simultaneous touches, unique IDs

### Particle System — DONE

See `docs/systems/particles.md`. Full architecture recovered:
- PSPParticleManager: pool-based emitter management, template search by hash
- PSPParticleEmitter: 0x4C bytes, linked list, position/velocity/scale/timeScale
- PSPEmitterTemplate: loaded from data file, contains ParticleSets with spawn timing
- PSPParticle: 0xA4 bytes per particle
- AddEmitter, Update, Draw all decompiled
- 3 draw layers: -1 (background), 0 (mid), 1 (foreground)

**Still undecompiled:** AddParticle full (313 lines, only first 80 read), LoadFile, Draw full (382 lines)

### SplatEntity System — DONE

See `docs/systems/effects.md`. DrawActiveSplats fully decompiled:
- Batched triangle list rendering with QUADCUSTOMVERTEX
- 6 splat variants, pool-based

### BombFlash / BombBlast — DONE

See `docs/systems/effects.md`. Both Update functions fully decompiled:
- BombFlash: quadratic scale + alpha fade animation (61 lines)
- BombBlast: expanding radius, dual velocity, 3s lifetime (32 lines)

### Coin System (low priority, 16 functions)

- `Coin::MakeCoins` — seen in scoring pipeline
- `Coin::ClearCoins` — seen in GameExit
- Simple bouncing coin entity for visual reward feedback

## Recovered but not fully documented

### StringHash Algorithm — DONE

Full C implementation saved to `docs/systems/string-hash.md`. Jenkins lookup3 with case-folding, initial `c = 0x805 + len`.

### Math::SinIdx / CosIdx

16-bit fixed-point angle lookup. Scale factor: `angle_u16 = degrees * 0xb6`.
Can be replaced with standard `sin(angle * 2π / 65536)` and `cos()`.

### Bomb::Update — DONE

Fully decompiled (195 lines). Chain-bomb spawning, fuse SFX, rotation, physics all documented in `docs/structs/entities.md`.
