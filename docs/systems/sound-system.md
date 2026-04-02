# Sound System

## Sound System

### GameSound (0x708 bytes)

Pool-based sound manager with ~32 slots (each 0x38 = 56 bytes).

**Sound slot layout** (0x38 bytes per slot):

| Offset | Type | Name |
|--------|------|------|
| +0x00 | MortarSound* | m_pSound |
| +0x04 | uint | m_NameHash |
| +0x10 | byte | m_field10 |
| +0x14 | float | m_Volume |
| +0x18 | float | m_Pitch |
| +0x1c | Delegate1 | m_Callback |

**SFXPlay** (0x129270): Finds free slot via `FindFree()`, calls `SoundManager::SFXPlay(name, sound*, flags)`, stores hash + volume + pitch + callback in slot. Volume = `(1 - (1 - masterVol) * vol) * pitch`.

### BadaSound (platform backend)

| Function | Address | Purpose |
|----------|---------|---------|
| SFXLoad | 0x0018b1f4 | Load sound file from disk |
| SFXPlay | 0x0018b130 | Play a loaded sound effect |
| MusicPlay | 0x0018b538 | Start background music |
| MusicStop | 0x0018b494 | Stop music |
| MusicPause/Resume | 0x0018b47c / 0x0018b464 | Pause/resume music |
| MusicMute | 0x0018b458 | Mute music |
| SFXSetVolume | 0x0018b190 | Set SFX master volume |
| MusicSetVolume | 0x0018b3e8 | Set music volume |

### Sound Architecture

```
GameSound (game-level pool, 32 slots)
  └─ Mortar::SoundManager (engine singleton)
       └─ BadaSound (platform: Bada OS audio)
            └─ Osp::Media::Player (Bada media API)

MAMAudioThread (separate thread, 16kHz, 16 voices)
  └─ NLFQueue<MAMSoundInputCmd> (lock-free command queue)
  └─ NLFQueue<MAMSoundOutputCmd> (lock-free response queue)
```

SFX names are string literals in the binary (e.g. "sfx_apple_hit", "sfx_bomb_fuse") — loaded via `BadaSound::SFXLoad` on init, referenced by string hash at play time.

---

## Subsystem Singletons

SystemManager, MatrixManager, FileManager, DisplayManager (320×480), TextureManager, MeshManager, AnimationManager, InputManager, PSPParticleManager, PowerUpManager, LeaderboardManager, NetworkManager (P2P + OpenFeint + GameCenter), MAMAudioController → MAMAudioThread (16 voices, 16kHz), WaveManager, ItemManager, AchievementManager, BonusManager, Mortar::Touch, Mortar::SoundManager, FruitCamera
