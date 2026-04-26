# Sound System

Four-layer architecture: GameSound (game) -> SoundManager (engine base) -> SoundManagerMAM (engine MAM impl) -> BadaSound (platform)

**Music state machine**: see [systems/music-state.md](../systems/music-state.md) for the
full `UpdateMusic` spec: volume ramp, crossfade between "Music-menu" / "background" tracks,
preload-arm logic, and port-side gaps.

---

## Layer 1: Mortar::SoundManager (engine base, 40 bytes)

Abstract sound manager singleton. 40 bytes with vtable + sound list. Volume/mute state stored as **static globals** (not instance fields).

### Struct layout (40 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | SoundManagerFns* | vtable | Virtual function table |
| +0x04 | 36 | List\<MortarSound*\> | m_SoundList | All allocated MortarSound objects |

### Vtable (SoundManagerFns, 28 bytes)

| Offset | Type | Name |
|--------|------|------|
| +0x00 | ptr | PreLoadSound |
| +0x04 | ptr | PreLoadSoundEx |
| +0x08 | ptr | SFXPauseAll |
| +0x0c | ptr | SFXUnpauseAll |
| +0x10 | ptr | BeginInterruption |
| +0x14 | ptr | EndInterruption |
| +0x18 | ptr | IsInterrupted |

### Static globals (GOT-relative, not instance fields)

- `s_MusicVolume` (float) -- accessed by GetMusicVolume/SetMusicVolume
- `s_SFXVolume` (float) -- accessed by SetSFXVolume
- `s_Muted` (bool) -- accessed by SyncMutes
- `s_SongHandle` (uint) -- accessed by SongResume/SongPause/SongStop

### Methods

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x0018d304 | SoundManager() | __thiscall | Constructor: sets vtable, inits List |
| 0x0018d2dc | SoundManager() | __thiscall | Alternate constructor (identical) |
| 0x0018d3e0 | ~SoundManager() | __thiscall | Destructor: resets vtable, destroys List |
| 0x0018d408 | ~SoundManager() | __thiscall | Deleting destructor |
| 0x00105948 | GetInstance() | static | Singleton accessor; creates SoundManagerMAM if needed |
| 0x0018d388 | SFXPlay(char*, ulong, void*, uchar, long) | __thiscall | Delegates to SFXPlayInternal |
| 0x0018d39c | SFXPlay(long, ulong, MortarSound*, uchar, long) | __thiscall | Overload, delegates to SFXPlayInternal |
| 0x0018c950 | SFXPlayInternal(long, ...) | __thiscall | Base stub (returns param_1) |
| 0x0018d430 | SFXPauseAll() | __thiscall | Base stub (empty) |
| 0x0018d2d8 | PreLoadSound(char*) | __thiscall | Base stub (nop) |
| 0x0018ce78 | PreLoadSoundEx(char*, bool) | __thiscall | Base stub (nop) |
| 0x0018c930 | GetMusicVolume() | __thiscall | Returns static s_MusicVolume |
| 0x0018ca78 | SetMusicVolume(float) | __thiscall | Sets static + SyncMutes() |
| 0x0018ca98 | SetSFXVolume(float) | __thiscall | Sets static + SyncMutes() |
| 0x0018c964 | SongResume() | __thiscall | MAMAudioController::ResumeSound |
| 0x0018c988 | SongPause() | __thiscall | MAMAudioController::PauseSound |
| 0x0018c9ac | SongStop() | __thiscall | MAMAudioController::StopSound |
| 0x0018c960 | SongSetMemorySize(long) | __thiscall | Stub (nop) |
| 0x0018c9d4 | SyncMutes() | __thiscall | Sets music/sfx mute based on volume==0 or muted flag |
| 0x0018cab8 | CreateNewSound() | __thiscall | Allocates MortarSoundMAM (0x10 bytes) |

---

## Layer 2: Mortar::SoundManagerMAM (engine subclass, inherits SoundManager)

MAM audio implementation. Overrides vtable methods. Uses MAMAudioController for actual playback.

### Struct layout (same as SoundManager, 40 bytes)

Inherits SoundManager directly. No additional instance fields -- uses MAMAudioController singleton for state.

### Methods

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x0018cbec | SoundManagerMAM() | __thiscall | Constructor: calls SoundManager(), overrides vtable |
| 0x0018cb58 | SoundManagerMAM() | __thiscall | Alternate constructor |
| 0x0018cae4 | ~SoundManagerMAM() | __thiscall | Destructor |
| 0x0018cb10 | ~SoundManagerMAM() | __thiscall | Destructor variant |
| 0x0018cb34 | ~SoundManagerMAM() | __thiscall | Deleting destructor |
| 0x0018cd34 | SFXPlayInternal(char*, ulong, void*, uchar, long) | __thiscall | Main impl: GetSound -> MAMAudioController::PlaySound; or MortarSound::Load+Play |
| 0x0018c900 | SFXPauseAll() | __thiscall | Empty override (nop) |
| 0x0018c908 | PreLoadSound(char*) | __thiscall | Stub override (nop) |
| 0x0018cc64 | GetSound(char*) | __thiscall | Lookup by StringHash in std::map, loads via MAMAudioController::LoadSound on miss |

### SFXPlayInternal (0x0018cd34) logic

```
if (sound == NULL):
    mamSound = GetSound(name)
    if mamSound == NULL:
        clear handle
    else:
        controller = MAMAudioController::GetInstance()
        handle = controller->PlaySound(mamSound, ...)
else:
    MortarSound::Load(sound, name)
    MortarSound::Play()
```

---

## Mortar::MortarSound (16 bytes)

Individual sound instance. MortarSoundMAM is the concrete subclass.

### Struct layout (0x10 = 16 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | ptr | vtable | Virtual function table |
| +0x04 | 4 | char* | m_Name | Sound name (filename string) |
| +0x08 | 4 | uint | m_Handle | MAMAudioController sound handle (0 = not loaded) |
| +0x0c | 4 | int | m_State | 0=idle, 1=paused, 2=playing |

### Methods

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x0018c6ac | MortarSound() | __thiscall | Constructor |
| 0x0018c6d0 | MortarSound() | __thiscall | Alternate constructor |
| 0x0018c704 | ~MortarSound() | __thiscall | Destructor variant |
| 0x0018c728 | ~MortarSound() | __thiscall | Destructor |
| 0x0018c74c | ~MortarSound() | __thiscall | Deleting destructor |
| 0x0018c6fc | Destroy() | __thiscall | Calls InternalDestroy |
| 0x0018c8a4 | InternalDestroy() | __thiscall | Free name, Stop, RemoveListener |
| 0x0018c780 | IsPlaying() | __thiscall | Returns m_State == 2 |
| 0x0018c794 | IsPaused() | __thiscall | Returns m_State == 1 |
| 0x0018c830 | Pause() | __thiscall | If playing (state 2): PauseSound, state=1 |
| 0x0018c850 | Play() | __thiscall | If idle (state 0): SFXPlay via SoundManager, state=2 |
| 0x0018c7f0 | Stop(float) | __thiscall | StopSound, state=0, handle=0 |
| 0x0018c810 | Resume() | __thiscall | If paused (state 1): ResumeSound, state=2 |
| 0x0018c7b4 | SetVolume(float) | __thiscall | Maps 0-1 float to 0-255, calls MAMAudioController::SetSoundVolume |

SetVolume multiplier constant: DAT_0018c7ec = **255.0f** (maps 0.0-1.0 to 0-255 byte range).

### MortarSoundMAM (inherits MortarSound, same 16 bytes)

| Address | Name | Signature |
|---------|------|-----------|
| 0x0018d040 | MortarSoundMAM() | __thiscall |
| 0x0018d064 | ~MortarSoundMAM() | __thiscall |
| 0x0018d090 | ~MortarSoundMAM() | __thiscall |

---

## Layer 3: BadaSound (platform layer)

Bada OS audio backend using Osp::Media::Player (music) and AudioOut (SFX via PCM mixing).

### Struct layout (0x87C = 2172 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | PlayerListener* | m_pPlayerListener | Wraps this for Player callbacks |
| +0x04 | 4 | Player* | m_pPlayer | Osp::Media::Player for music (NULL initially) |
| +0x08 | 4 | float | m_MusicVolume | Default 0.45 (DAT_0018b89c), clamped 0-1 |
| +0x0c | 4 | float | m_SFXVolume | Default 0.4 (DAT_0018b8a0), clamped 0-1 |
| +0x10 | 1024 | void*[256] | m_SoundEffects | SoundEffectBada pointers (index 0-255) |
| +0x410 | 1024 | uint[256] | m_SoundHashes | StringHash lookup table |
| +0x810 | 4 | int | m_SoundCount | Number of loaded sounds (max 256) |
| +0x814 | 96 | ActiveEffect[8] | m_ActiveSlots | 8 concurrent SFX playback slots (0x0c each) |
| +0x874 | 4 | AudioOut* | m_pAudioOut | Osp::Media::AudioOut for PCM output |
| +0x878 | 4 | AudioOutListener* | m_pAudioOutListener | AudioOut event listener |

Total: 0x87C bytes (2172)

### ActiveEffect (0x0c = 12 bytes)

| Offset | Size | Type | Name |
|--------|------|------|------|
| +0x00 | 4 | int | handle | 0 = free slot |
| +0x04 | 4 | void* | data | SoundEffectBada reference |
| +0x08 | 4 | int | sampleCount | Remaining samples |

### SoundEffectBada (0x10 = 16 bytes)

| Offset | Size | Type | Name |
|--------|------|------|------|
| +0x00 | 4 | int | field0 | |
| +0x04 | 4 | void* | buffers | ByteBuffer list |
| +0x08 | 4 | int | dataSize | PCM data size in bytes |
| +0x0c | 1 | bool | loop | Loop flag |

### Methods

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x0018b7a0 | BadaSound() | __thiscall | Constructor: init 8 ActiveEffects, create PlayerListener, AudioOut, start PCM output |
| 0x0018b69c | BadaSound() | __thiscall | Alternate constructor |
| 0x0018b4b8 | ~BadaSound() | __thiscall | MusicStop, destroy Player, destroy all SoundEffectBada |
| 0x0018b4f8 | ~BadaSound() | __thiscall | Deleting destructor |
| 0x0018b0ec | MusicGetVolume() | __thiscall | Returns this->m_MusicVolume (+0x08) |
| 0x0018b3e8 | MusicSetVolume(float) | __thiscall | Clamp 0-1, store +0x08, Player::SetVolume |
| 0x0018b538 | MusicPlay(char*) | __thiscall | MusicStop, create Player if needed, OpenFile(basePath+name+ext) |
| 0x0018b494 | MusicStop() | __thiscall | If Player state 5(paused) or 6(playing): Stop() |
| 0x0018b47c | MusicPause() | __thiscall | If state 6(playing): Pause() |
| 0x0018b464 | MusicResume() | __thiscall | If state 5(paused): Play() |
| 0x0018b458 | MusicMute(bool) | __thiscall | Player::SetMute |
| 0x0018b190 | SFXSetVolume(float) | __thiscall | Clamp 0-1, store at +0x0c |
| 0x0018b1f4 | SFXLoad(char*, SoundEffectBada**) | __thiscall | Load .wav.pcm file, hash table + SoundEffectBada |
| 0x0018b3c8 | SFXLoad(char*, bool) | __thiscall | Wrapper: calls SFXLoad then SetLoop |
| 0x0018b1c0 | SFXPlay(char*) | __thiscall | StringHash lookup, calls SFXPlay(int) |
| 0x0018b130 | SFXPlay(int) | __thiscall | Find free ActiveSlot (8 max), Prepare + play |
| 0x0018b11c | SFXPause(int) | __thiscall | Stub (nop, returns param) |
| 0x0018b124 | SFXStop(int) | __thiscall | Stub (nop, returns param) |
| 0x0018b0f4 | FindSound(uint) | __thiscall | Linear search m_SoundHashes for hash match |

#### SFXLoad (0x0018b1f4) -- fully decompiled

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
        
        m_SoundHashes[m_SoundCount] = hash;     // +0x410 array
        
        SoundEffectBada* effect = new SoundEffectBada();  // 0x10 bytes
        if (outEffect) *outEffect = effect;
        
        ByteBuffer* pcmBuf = new ByteBuffer();
        pcmBuf->Construct(fileSize);
        for (int i = 0; i < fileSize; i++) {
            byte b; buf->GetByte(&b); pcmBuf->SetByte(b);
        }
        effect->Add(pcmBuf, fileSize);
        
        m_SoundEffects[m_SoundCount] = effect;  // +0x10 array
        m_SoundCount++;
    }
}
```

#### SFXPlay (0x0018b130) -- fully decompiled

```c
int BadaSound::SFXPlay(int soundIndex) {
    for (int i = 0; i < 8; i++) {
        if (m_ActiveSlots[i].handle == 0) {  // free slot
            ActiveEffect::Prepare(&m_ActiveSlots[i], m_SoundEffects[soundIndex]);
            m_ActiveSlots[i].sampleCount = m_SoundEffects[soundIndex]->dataSize;
            break;
        }
    }
    return 0;
}
```

**Limits:** 8 concurrent SFX at a time. If all slots busy, sound is dropped.

#### Constructor (0x0018b7a0) -- AudioOut initialization

```c
BadaSound::BadaSound() {
    // Init 8 ActiveEffect slots
    for (int i = 0; i < 8; i++)
        ActiveEffect::ActiveEffect(&m_ActiveSlots[i]);
    
    m_pPlayerListener = new PlayerListener(this);  // 0x0c bytes
    m_MusicVolume = 0.45f;
    m_pPlayer = NULL;
    m_SFXVolume = 0.4f;
    
    // Clear hash + effect arrays
    for (int i = 0; i < 256; i++) {
        m_SoundHashes[i] = 0;
        m_SoundEffects[i] = NULL;
    }
    
    // Setup AudioOut for PCM mixing
    m_pAudioOut = new AudioOut();
    m_pAudioOutListener = new AudioOutListener(this);
    AudioOut::Construct(m_pAudioOut);
    AudioOut::Prepare(m_pAudioOut, AUDIO_TYPE_PCM_S16_LE, 5/*channels?*/, 2/*buffers?*/);
    
    // Write initial silence buffer (0x9d8 = 2520 bytes)
    ByteBuffer* silence = new ByteBuffer();
    silence->Construct(2520);
    for (int i = 0; i < 2520; i++) silence->SetByte(0);
    AudioOut::WriteBuffer(m_pAudioOut, silence);
    AudioOut::WriteBuffer(m_pAudioOut, silence);
    AudioOut::Start();
    delete silence;
}
```

---

## Layer 4: GameSound (game layer, 0x708 = 1800 bytes)

Pool-based sound manager with 32 slots (each 0x38 = 56 bytes). Game code uses this layer exclusively.

### Struct layout (0x708 = 1800 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | float | m_MasterVolume | Default 1.0 |
| +0x04 | 4 | int | m_field04 | Unknown, set to 0 in constructor |
| +0x08 | 1792 | SoundSlot[32] | m_Slots | 32 sound slots, 0x38 each |

### SoundSlot (0x38 = 56 bytes per slot)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | MortarSound* | pSound | Allocated in constructor via CreateNewSound |
| +0x04 | 4 | uint | nameHash | StringHash of sound name |
| +0x08 | 8 | | (padding/unknown) | |
| +0x10 | 1 | byte | isFree | 1=free, 0=in use |
| +0x11 | 1 | byte | field11 | Set to 0 on init |
| +0x12 | 1 | byte | field12 | Set to 0 on init |
| +0x14 | 4 | float | volume | Default 1.0 (0x3f800000) |
| +0x18 | 4 | float | pitch | |
| +0x1c | 28 | Delegate1\<bool,MortarSound*\> | callback | Completion callback |

Slot array starts at +0x08 in the struct (first slot's pSound = m_Slot0_pSound in Ghidra at +0x08).

### Methods

| Address | Name | Signature | Notes |
|---------|------|-----------|-------|
| 0x001294bc | GameSound() | __thiscall | Constructor: ASound init x32, CreateNewSound x32, masterVol=1.0 |
| 0x00129450 | GameSound() | __thiscall | Alternate constructor |
| 0x001293a0 | ~GameSound() | __thiscall | Destroys all pSound objects |
| 0x001293f8 | ~GameSound() | __thiscall | Deleting destructor |
| 0x001290e8 | FindFree() | __thiscall | Linear scan for isFree==1, returns slot index or -1 |
| 0x00129270 | SFXPlay(char*, float, float, Delegate1) | __thiscall | Main play method (see below) |
| 0x00129100 | IsPlaying(int hash) | __thiscall | Scan slots for matching hash, check MortarSound::IsPlaying |
| 0x00129124 | IsPlaying(char*) | __thiscall | StringHash wrapper for IsPlaying(int) |
| 0x00129138 | IsValid(MortarSound*, char*) | __thiscall | Check if slot matches ptr+hash |
| 0x0012917c | Release(MortarSound*, char*) | __thiscall | Stop + Destroy + mark free |
| 0x001291e0 | KillAll() | __thiscall | Stop all playing slots, mark all free |
| 0x00129170 | DestroySoundInternals(MortarSound*) | static | Calls MortarSound::Destroy |
| 0x001695e8 | PreloadInGameSounds() | — | Preloads all game SFX at startup |

#### SFXPlay (0x00129270) logic

```c
MortarSound* GameSound::SFXPlay(const char* name, float vol, float pitch, Delegate1 callback) {
    int i = FindFree();
    if (i == -1) return NULL;
    
    SoundManager* mgr = SoundManager::GetInstance();
    mgr->SFXPlay(name, 0, m_Slots[i].pSound, 0x40, -1);
    
    m_Slots[i].isFree = 0;
    m_Slots[i].nameHash = StringHash(name);
    m_Slots[i].callback = callback;
    m_Slots[i].pitch = pitch;
    m_Slots[i].volume = vol;
    
    // Volume formula: (1 - (1 - masterVol) * vol) * pitch
    MortarSound::SetVolume(m_Slots[i].pSound, (1.0 - (1.0 - m_MasterVolume) * vol) * pitch);
    
    return m_Slots[i].pSound;
}
```

---

## Sound Architecture

```
GameSound (game-level pool, 32 slots)
  |-- SFXPlay/Release/KillAll
  v
Mortar::SoundManager (engine singleton, abstract)
  |-- SFXPlay -> SFXPlayInternal (virtual)
  |-- SetMusicVolume/SetSFXVolume (static globals)
  |-- SongPause/SongResume/SongStop
  v
Mortar::SoundManagerMAM (engine MAM impl)
  |-- SFXPlayInternal -> GetSound -> MAMAudioController::PlaySound
  |-- GetSound: hash-map cache, loads via MAMAudioController::LoadSound
  v
BadaSound (Bada OS platform layer, NOT subclass of SoundManager)
  |-- SFXLoad: reads .wav.pcm -> SoundEffectBada (PCM buffer)
  |-- SFXPlay: 8-slot ActiveEffect mixer via AudioOut
  |-- MusicPlay/Stop/Pause/Resume via Osp::Media::Player
  v
MAMAudioThread (separate thread, 16kHz, 16 voices)
  |-- NLFQueue<MAMSoundInputCmd> (lock-free command queue)
  |-- NLFQueue<MAMSoundOutputCmd> (lock-free response queue)
```

**Two audio paths coexist:**
1. **MAM path** (SoundManager -> SoundManagerMAM -> MAMAudioController -> MAMAudioThread): Software mixer with 16 voices, used by GameSound for SFX
2. **Bada path** (BadaSound -> Osp::Media): Direct platform audio for music + PCM SFX playback

SFX names are string literals in the binary (e.g. "sfx_apple_hit", "sfx_bomb_fuse") -- loaded via BadaSound::SFXLoad on init, referenced by string hash at play time.

---

## Calling Convention Fixes Applied

All functions below had "Unknown calling convention" or used `in_r0` and were fixed to `__thiscall`:

| Address | Function | Class |
|---------|----------|-------|
| 0x0018c850 | Play() | MortarSound |
| 0x0018c830 | Pause() | MortarSound |
| 0x0018c6fc | Destroy() | MortarSound |
| 0x0018c8a4 | InternalDestroy() | MortarSound |
| 0x0018c780 | IsPlaying() | MortarSound |
| 0x0018c794 | IsPaused() | MortarSound |
| 0x0018c7f0 | Stop(float) | MortarSound |
| 0x0018c900 | SFXPauseAll() | SoundManagerMAM |
| 0x0018c908 | PreLoadSound(char*) | SoundManagerMAM |
| 0x0018c930 | GetMusicVolume() | SoundManager |
| 0x0018c950 | SFXPlayInternal(...) | SoundManager |
| 0x0018c960 | SongSetMemorySize(long) | SoundManager |
| 0x0018c964 | SongResume() | SoundManager |
| 0x0018c988 | SongPause() | SoundManager |
| 0x0018c9ac | SongStop() | SoundManager |
| 0x0018c9d4 | SyncMutes() | SoundManager |
| 0x0018ca78 | SetMusicVolume(float) | SoundManager |
| 0x0018ca98 | SetSFXVolume(float) | SoundManager |
| 0x0018cc64 | GetSound(char*) | SoundManagerMAM |
| 0x0018ce78 | PreLoadSoundEx(char*, bool) | SoundManager |
| 0x0018d2d8 | PreLoadSound(char*) | SoundManager |
| 0x0018d39c | SFXPlay(long, ...) | SoundManager |
| 0x0018d430 | SFXPauseAll() | SoundManager |
| 0x0018b0ec | MusicGetVolume() | BadaSound |
| 0x0018b11c | SFXPause(int) | BadaSound |
| 0x0018b124 | SFXStop(int) | BadaSound |
| 0x0018b458 | MusicMute(bool) | BadaSound |
| 0x0018b464 | MusicResume() | BadaSound |
| 0x0018b47c | MusicPause() | BadaSound |
| 0x0018b494 | MusicStop() | BadaSound |
| 0x001290e8 | FindFree() | GameSound |
| 0x001291e0 | KillAll() | GameSound |

---

## See Also

- [Audio format](../formats/audio.md) -- .wav.pcm file format
- [Other structs](../structs/other.md) -- MAMAudioThread layout
