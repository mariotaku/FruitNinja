# Sound Functions

## Sound

### GameSound::SFXPlay (0x00129270, 39 lines)

```c
MortarSound* GameSound::SFXPlay(char* name, float volume, float pitch, Delegate1 callback) {
    int slot = FindFree();
    if (slot == -1) return NULL;
    
    SoundManager::SFXPlay(name, &slots[slot].sound, flags);
    slots[slot].hash = StringHash(name);
    slots[slot].volume = volume;
    slots[slot].pitch = pitch;
    slots[slot].callback = callback;
    
    float finalVol = (1.0 - (1.0 - masterVolume) * volume) * pitch;
    MortarSound::SetVolume(slots[slot].sound, finalVol);
    
    return slots[slot].sound;
}
```

---

## Screen Callbacks

| Function | Address | Action |
|----------|---------|--------|
| MainScreen::GameModeCallback | 0x0014b068 | Open mode selection |
| MainScreen::NewGameCallback | 0x0014c384 | Direct game start |
| GameModeScreen::ClassicModeCallback | 0x0013dfb4 | Game.gameMode=0 |
| GameModeScreen::ArcadeModeCallback | 0x0013e19c | Game.gameMode=1 |
| GameModeScreen::ZenModeCallback | 0x0013dffc | Game.gameMode=3 |
| GameModeScreen::SetupLevel | 0x0013e21c | PrepareForLevelStart() |
| GameOverScreen::QuitCallback | 0x00140620 | HitMenuBomb → menu |
| GameOverScreen::RetryCallback | 0x0014105c | Restart game |
| PauseScreen::ContinueGameCallback | 0x00153fe8 | Unpause |
| PauseScreen::QuitGameCallback | 0x00153ebc | Quit to menu |

---


---

## Mortar::SoundManager (engine layer)

### SoundManager::SFXPlay (0x0018d388, 12 lines)

```c
// Thin wrapper — delegates to SFXPlayInternal
void SoundManager::SFXPlay(char* name, ulong hash, MortarSound* out, uchar flags, long param5) {
    SFXPlayInternal(name, hash, out, flags, param5 & 0xFF);
}
```

### SoundManager::SFXPlayInternal (0x0018cd34, 50 lines)

```c
void SoundManager::SFXPlayInternal(char* name, ulong hash, MortarSound* out, uchar flags, long p5) {
    if (flags == 0) {
        // Normal play: look up loaded sound by hash
        MAMSound* sound = GetSound(hash);
        MortarSound** outPtr = GetOutputSlot();
        if (sound == NULL) {
            state->playingId = 0;
            if (*outPtr) *(*outPtr) = 0;  // clear caller's handle
        } else {
            // Play through MAMAudioController
            uint playId = MAMAudioController::PlaySound(sound, *outPtr, state->looping);
            state->playingId = playId;
            if (*outPtr) *(*outPtr) = playId;
            state->looping = false;
        }
    } else {
        // Stream mode: load + play inline
        MortarSound::Load(flags, hash);
        MortarSound::Play();
    }
}
```

### SoundManager::SetSFXVolume (0x0018ca98, 11 lines)

| Address | Signature |
|---------|-----------|
| 0x0018ca98 | `void SetSFXVolume(float vol)` — stores volume, calls SyncMutes() |

### SoundManager::SetMusicVolume (0x0018ca78, 11 lines)

| Address | Signature |
|---------|-----------|
| 0x0018ca78 | `void SetMusicVolume(float vol)` — stores volume, calls SyncMutes() |

### SoundManager::CreateNewSound (0x0018cab8, 15 lines)

| Address | Signature |
|---------|-----------|
| 0x0018cab8 | `MortarSoundMAM* CreateNewSound()` — allocates 0x10-byte MortarSoundMAM |

### Sound Pipeline Summary

```
GameSound::SFXPlay(name, vol, pitch, callback)
  → finds free slot in 32-slot pool
  → SoundManager::SFXPlay(name, hash, &slot.sound, 0, 0x40)
    → SFXPlayInternal
      → GetSound(hash)  // lookup loaded MAMSound by StringHash
      → MAMAudioController::PlaySound(mamSound, outHandle, loop)
        → pushes command to NLFQueue → MAMAudioThread
          → mixes into 16kHz PCM output buffer
```

For SDL2 port: replace everything below SoundManager with:
- `SoundManager::SFXPlayInternal` → SDL_QueueAudio or custom mixer callback
- `GetSound` → hash table lookup of pre-loaded PCM buffers
- Music → SDL_LoadWAV + SDL audio stream
