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

### BadaSound (platform backend — replace for port)

**Struct layout:**

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | void* | m_pOverlayCtrl | Bada overlay control (passed to Player::Construct) |
| +0x04 | Player* | m_pPlayer | Osp::Media::Player for music playback |
| +0x08 | float | m_MusicVolume | 0.0–1.0, clamped |
| +0x0c | float | m_SFXVolume | 0.0–1.0, clamped |
| +0x10..+0x410 | uint[256] | m_SoundHashes | StringHash lookup table (hash → index) |
| +0x410..+0x810 | SoundEffectBada*[256] | m_SoundEffects | Loaded PCM data per sound |
| +0x810 | int | m_SoundCount | Number of loaded sounds (max 256) |
| +0x814..+0x874 | ActiveEffect[8] | m_ActiveSlots | 8 concurrent SFX playback slots, 0x0c each |

**Functions (all decompiled):**

| Function | Address | Lines | Purpose |
|----------|---------|-------|---------|
| SFXLoad | 0x0018b1f4 | 72 | Load .wav.pcm file → hash table + SoundEffectBada |
| SFXPlay | 0x0018b130 | 20 | Find free slot (8 max), prepare + play sound |
| SFXSetVolume | 0x0018b190 | 9 | Clamp 0–1, store at +0x0c |
| MusicPlay | 0x0018b538 | 42 | Create Player, OpenFile(path), play music track |
| MusicStop | 0x0018b494 | 10 | Stop if playing (state 5 or 6) |
| MusicPause | 0x0018b47c | 6 | Pause if playing (state 6) |
| MusicResume | 0x0018b464 | 6 | Resume if paused (state 5) |
| MusicMute | 0x0018b458 | 5 | Player::SetMute |
| MusicSetVolume | 0x0018b3e8 | 12 | Clamp 0–1, store at +0x08, Player::SetVolume |

#### SFXLoad (0x0018b1f4) — fully decompiled

```c
void BadaSound::SFXLoad(const char* name, SoundEffectBada** outEffect) {
    if (!name || !*name) return;
    
    uint hash = StringHash(name);
    if (FindSound(this, hash)) return;  // already loaded
    
    // Build path: basePath + name + extension
    char path[260];
    sprintf(path, "%s%s%s", DATA_SFX_PATH, name, ".wav.pcm");
    
    // Read entire file into ByteBuffer
    File file;
    file.Construct(path, "r");
    FileAttributes attrs;
    File::GetAttributes(path, attrs);
    int fileSize = attrs.GetFileSize();
    
    if (fileSize > 0) {
        ByteBuffer* buf = new ByteBuffer();
        buf->Construct(fileSize);
        file.Read(buf);
        
        if (m_SoundCount >= 256) { delete buf; return; }
        
        // Store hash at index (m_SoundCount + 0x104) in hash table
        m_SoundHashes[m_SoundCount + 0x104] = hash;
        
        // Create SoundEffectBada and copy PCM data
        SoundEffectBada* effect = new SoundEffectBada();  // 0x10 bytes
        if (outEffect) *outEffect = effect;
        
        ByteBuffer* pcmBuf = new ByteBuffer();
        pcmBuf->Construct(fileSize);
        // Byte-by-byte copy (inefficient but matches original)
        for (int i = 0; i < fileSize; i++) {
            byte b; buf->GetByte(&b); pcmBuf->SetByte(b);
        }
        effect->Add(pcmBuf, fileSize);
        
        m_SoundEffects[m_SoundCount + 4] = effect;
        m_SoundCount++;
    }
}
```

**Key for porting:** SFXLoad reads the entire .wav.pcm file into a ByteBuffer, wraps it in a SoundEffectBada. For SDL2, replace with `SDL_LoadWAV` after prepending a WAV header (see [audio format](../formats/audio.md)).

#### SFXPlay (0x0018b130) — fully decompiled

```c
int BadaSound::SFXPlay(int soundIndex) {
    // Find free slot in 8-slot active array
    for (int i = 0; i < 8; i++) {
        if (m_ActiveSlots[i].handle == 0) {
            ActiveEffect::Prepare(&m_ActiveSlots[i], m_SoundEffects[soundIndex + 4]);
            m_ActiveSlots[i].sampleCount = m_SoundEffects[soundIndex + 4]->dataSize;
            break;
        }
    }
    // Reset global flag
    return 0;
}
```

**Limits:** 8 concurrent SFX at a time. If all slots busy, sound is dropped.

#### Music (MusicPlay/Stop/Pause/Resume)

Music uses `Osp::Media::Player` (Bada media API):
- **MusicPlay**: Stops current music → creates Player if needed → `OpenFile(basePath + name + ext)`
- **MusicStop**: Checks Player state (5=paused, 6=playing) → `Stop()`
- **MusicPause**: Only if state==6 (playing) → `Pause()`
- **MusicResume**: Only if state==5 (paused) → `Play()`

Player states: 5=paused, 6=playing (Bada `Osp::Media::Player::PlayerState` enum).

**For SDL2 port:** Replace with SDL2 audio stream or SDL_mixer for music. The game has ~5 music tracks as .wav.pcm files.

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

SystemManager, MatrixManager, FileManager, DisplayManager (480×320), TextureManager, MeshManager, AnimationManager, InputManager, PSPParticleManager, PowerUpManager, LeaderboardManager, NetworkManager (P2P + OpenFeint + GameCenter), MAMAudioController → MAMAudioThread (16 voices, 16kHz), WaveManager, ItemManager, AchievementManager, BonusManager, Mortar::Touch, Mortar::SoundManager, FruitCamera

---

## See Also

- [Sound functions](../functions/sound.md) -- SFXLoad, PlaySound pseudocode
- [Audio format](../formats/audio.md) -- .wav.pcm file format
- [Other structs](../structs/other.md) -- MAMAudioThread layout
