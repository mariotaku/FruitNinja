<!-- Analysed: 2026-04-25T10:30 -->

# MortarSound / MortarSoundMAM API Reference

Complete API surface for `Mortar::MortarSound` and `Mortar::MortarSoundMAM`, derived from
decompilation of the binary and cross-referenced against `BadaSound` (the known concrete
platform backend). Intended as a specification for an SDL2 audio backend implementer.

See also `docs/engine/sound-system.md` for the full four-layer architecture and
`docs/engine/audio-internals.md` for MAMAudioController / MAMAudioThread internals.

---

## Summary: accessible vs. GOT-thunk-blind

| Category | Count | Status |
|----------|-------|--------|
| MortarSound concrete methods (fully decompiled) | 9 | Accessible |
| MortarSound GOT thunks (resolved to concrete bodies) | 5 | Resolved -- same bodies |
| MortarSoundMAM methods (ctor + 2 dtors) | 3 | Accessible (no extra overrides) |
| MAMAudioController methods called by MortarSound | 6 | Accessible |
| MortarSound::Load (0x0018c6f4), InternalLoad (0x0018c8d0), IsReady (0x0018c7a8), SetPitch (0x0018c778) | 4 | Resolved; Load @ 0x0018c6f4 is a one-line wrapper calling InternalLoad |
| MAMAudioController::GetInstance, Find, RemoveListener | 3 | No matching symbol; called via GOT only |

**Total accessible:** 18 bodies fully decompiled.  
**GOT-thunk-blind:** 3 helper symbols (`GetInstance`, `Find`, `RemoveListener`) -- these
are either inlined or behind unresolvable thunks; their behaviour is inferred from call sites.

---

## MortarSound struct (0x10 = 16 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | vtable* | vtable | Points 8 bytes past RTTI block (ARM C++ ABI) |
| +0x04 | 4 | char* | m_Name | Heap-allocated sound name string; NULL when idle |
| +0x08 | 4 | uint | m_Handle | MAMAudioController sound ID; 0 = not active |
| +0x0c | 4 | int | m_State | 0 = idle, 1 = paused, 2 = playing |

`m_Handle` is the monotonic ID returned by `MAMAudioController::PlaySound` and used as the key
for all subsequent `StopSound` / `PauseSound` / `ResumeSound` / `SetSoundVolume` calls.

### Handle-validity guard

Every method that uses `m_Handle` first checks `if (m_Handle == 0) m_State = 0`. This resets
stale state when the controller has already freed the voice (e.g. one-shot sound finished).

---

## MortarSound vtable

The vtable stored in an instance is the pointer to the first function entry (the RTTI header
occupying the preceding 8 bytes is skipped). Binary address of vtable data: `0x001eb068`.

| Slot | Offset | Address | Name |
|------|--------|---------|------|
| 0 | +0x00 | 0x0018c704 | ~MortarSound() (non-deleting) |
| 1 | +0x04 | 0x0018c74c | ~MortarSound() (deleting, calls operator_delete) |

**MortarSound has only 2 virtual slots (both destructor variants).** All other methods
(IsPlaying, IsPaused, Stop, Pause, Resume, Play, SetVolume, Destroy, InternalDestroy) are
non-virtual concrete methods, called directly via GOT thunks from game code.

### MortarSoundMAM vtable (0x001eb080)

| Slot | Offset | Address | Name |
|------|--------|---------|------|
| 0 | +0x00 | 0x0018d090 | ~MortarSoundMAM() (non-deleting) |
| 1 | +0x04 | 0x0018d064 | ~MortarSoundMAM() (deleting) |

`MortarSoundMAM` overrides only the destructor. It does not add any new virtual methods and
does not override `Play`, `Stop`, `Pause`, `Resume`, or `SetVolume`.

---

## MortarSound method table

### Constructor / Destructor

| Address | Thunk addr | Name | Notes |
|---------|------------|------|-------|
| 0x0018c6ac | 0x00106b3c | MortarSound() | Sets vtable, zeroes m_Name/m_Handle/m_State |
| 0x0018c6d0 | -- | MortarSound() (alt) | Identical to above, separate translation unit |
| 0x0018c704 | 0x00100020 | ~MortarSound() | Resets vtable, calls InternalDestroy |
| 0x0018c728 | -- | ~MortarSound() (variant) | Same, no operator_delete |
| 0x0018c74c | -- | ~MortarSound() (deleting) | ~MortarSound() + operator_delete |

### State / query methods

| Address | Thunk addr | Name | Signature | Behaviour |
|---------|------------|------|-----------|-----------|
| 0x0018c780 | 0x00102f84 | IsPlaying() | `bool(this)` | Guard: if m_Handle==0 -> m_State=0. Returns m_State==2 |
| 0x0018c794 | -- | IsPaused() | `bool(this)` | Guard: same. Returns m_State==1 |

### Playback control methods

| Address | Thunk addr | Name | Signature | Behaviour |
|---------|------------|------|-----------|-----------|
| 0x0018c850 | 0x001020cc | Play() | `void(this)` | Guard. If m_State==0: RemoveListener, register m_Handle ptr as listener slot, call SoundManager::SFXPlay(m_Name, 0, NULL, 0x40, -1). If m_Handle!=0 after that: m_State=2 |
| 0x0018c830 | 0x000f83dc | Pause() | `void(this)` | Guard. If m_State==2: MAMAudioController::PauseSound(m_Handle); m_State=1 |
| 0x0018c810 | 0x000f5c40 | Resume() | `void(this)` | Guard. If m_State==1: MAMAudioController::ResumeSound(m_Handle); m_State=2 |
| 0x0018c7f0 | 0x000fec40 | Stop(float fadeTime) | `void(this, float)` | Guard. If m_Handle!=0: MAMAudioController::StopSound(m_Handle); m_State=0; m_Handle=0. fadeTime=0.0f in all observed calls (DAT_0018c8cc=0.0f). Fade not implemented -- stop is immediate. |
| 0x0018c7b4 | 0x00101d78 | SetVolume(float vol) | `void(this, float)` | Guard. If m_Handle!=0: MAMAudioController::SetSoundVolume(m_Handle, clamp_byte(vol * 255.0f)) |

SetVolume clamp: `(0.0 < vol * 255.0f) * (char)(int)(vol * 255.0f)` -- multiplies the bool
`(0.0 < x)` by the byte cast. This produces 0 for negative inputs and clamps naturally at
255 via byte truncation. Equivalent to `(uint8_t)max(0, (int)(vol * 255.0f))`.  
Constant: DAT_0018c7ec = `0x437f0000` = **255.0f**.

### Lifecycle methods

| Address | Thunk addr | Name | Signature | Behaviour |
|---------|------------|------|-----------|-----------|
| 0x0018c6fc | -- | Destroy() | `void(this)` | Calls InternalDestroy(this). Non-virtual wrapper. |
| 0x0018c8a4 | -- | InternalDestroy() | `void(this)` | If m_Name!=NULL: operator_delete(m_Name); m_Name=NULL. Calls Stop(0.0f). Calls MAMAudioController::RemoveListener(m_Handle). |

### MortarSound::Load and InternalLoad (resolved)

`Load @ 0x0018c6f4` -- one-line wrapper that calls `InternalLoad`. (Previously believed to
have no binary symbol; resolved by re-analyst 2026-05-18.)

`InternalLoad @ 0x0018c8d0` -- heap-copies name, tears down existing `m_Name` first via
`InternalDestroy` (which also calls `Stop(0)` and zeros `m_Handle`). Calling `InternalLoad`
on an actively-playing sound silently stops it.

`IsReady @ 0x0018c7a8` -- no-op stub in binary: handle-validity guard + always returns true.
Loads complete synchronously so IsReady is trivially always true.

`SetPitch @ 0x0018c778` -- no-op stub in binary: handle-validity guard only; pitch argument
is discarded entirely. Pitch shifting was not supported by the MAMAudioController.

---

## MortarSoundMAM method table

`MortarSoundMAM` is the concrete subclass allocated by `SoundManager::CreateNewSound()`.
It adds no fields and overrides only the destructor.

| Address | Thunk addr | Name | Notes |
|---------|------------|------|-------|
| 0x0018d040 | 0x000ff87c | MortarSoundMAM() | Calls MortarSound(), overrides vtable to MortarSoundMAM vtable |
| 0x0018d064 | -- | ~MortarSoundMAM() (deleting) | Resets vtable, calls ~MortarSound(), operator_delete |
| 0x0018d090 | -- | ~MortarSoundMAM() (non-deleting) | Resets vtable, calls ~MortarSound() |

**MortarSoundMAM is a trivial subclass.** Its only purpose is to have a distinct vtable so that
the runtime destructor dispatch correctly reaches `~MortarSoundMAM` before `~MortarSound`.
All playback behaviour lives in the base `MortarSound` methods.

---

## MAMAudioController -- methods called by MortarSound

These are the backend methods that MortarSound delegates to. All are fully decompiled.

| Address | Name | Signature | Behaviour |
|---------|------|-----------|-----------|
| 0x0018c350 | PlaySound | `uint(this, MAMSound*, uint* listenerSlot, bool loop)` | Increments m_NextSoundId, pushes Play cmd (type 0) to NLFQueue at thread+0x134. If listenerSlot!=0: Find/create ListenPair in m_pListeners linked list. Returns new sound ID. |
| 0x0018c3f0 | StopSound | `void(this, uint id)` | Pushes Stop cmd (type 1). Calls RemoveListener(id). |
| 0x0018c328 | PauseSound | `void(this, uint id)` | Pushes Pause cmd (type 2). |
| 0x0018c300 | ResumeSound | `void(this, uint id)` | Pushes Resume cmd (type 3). |
| 0x0018c2dc | SetSoundVolume | `void(this, uint id, uint8 vol)` | Pushes Volume cmd (type 4) with vol 0-255. |
| 0x0018c468 | LoadSound | `MAMSound*(this, char* path)` | Opens .mad file, allocates MAMSound (12 bytes), reads PCM header (5 ints), reads sample data, right-shifts all samples >>4. Returns NULL on open failure. |

### RemoveListener / Find (GOT-thunk-blind)

`MAMAudioController::RemoveListener` and `MAMAudioController::Find` are called from
`InternalDestroy`, `Stop`, `StopSound`, and `Play` but have no resolvable symbol. Their
behaviour is:
- `Find(id)`: walks the `m_pListeners` linked list (12-byte nodes: next+callback_ptr+id), returns
  node whose `id` field matches, or NULL.
- `RemoveListener(id)`: equivalent to `Find(id)` + unlink + delete node. Cleans up the
  completion callback for a sound that is being stopped/destroyed.

---

## MAMAudioController struct (12 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | MAMAudioThread* | m_pThread | Audio mixing thread; NLFQueue at thread+0x134 |
| +0x04 | 4 | uint | m_NextSoundId | Monotonic counter; incremented on each PlaySound |
| +0x08 | 4 | ListenPair* | m_pListeners | Singly-linked list of completion callbacks (12 bytes each: next*, callback_ptr*, id) |

---

## GameSound additional methods (newly decompiled)

These were not in `sound-system.md` and are needed to complete the port.

### GameSound::Update (0x00129380)

```c
void GameSound::Update() {
    // If paused-for-interruption flag (slot +0x04) set, check if interruption ended
    if (m_field04 != 0) {
        SoundManager* mgr = SoundManager::GetInstance();
        if (mgr->vtable->IsInterrupted()) return;  // still interrupted
        m_field04 = 0;
        Unpause();
    }

    for (int i = 0; i < 32; i++) {
        SoundSlot* s = &m_Slots[i];
        if (s->isFree != 0) continue;

        if (!s->pSound->IsPlaying() && !s->pSound->IsPaused()) {
            // Sound finished -- fire completion callback
            bool handled = s->callback(s->pSound);
            if (handled) return;
            // Mark slot free
            DestroySoundInternals(this);  // calls MortarSound::Destroy on pSound
            s->nameHash = 0;
            s->isFree = 1;
            continue;
        }

        // Volume update: (1 - (1-masterVol)*slotVol) * pitch
        if (s->volume > 0.0f) {
            s->pSound->SetVolume(
                (1.0f - (1.0f - m_MasterVolume) * s->volume) * s->pitch
            );
        }
    }
}
```

### GameSound::Pause (0x00129256)

```c
void GameSound::Pause() {
    for (int i = 0; i < 32; i++) {
        SoundSlot* s = &m_Slots[i];
        if (s->isFree == 0 && s->pSound->IsPlaying()) {
            s->pSound->Pause();
            s->field12 = 1;   // +0x12: paused-by-system flag
        }
    }
}
```

### GameSound::Unpause (0x00129218)

```c
void GameSound::Unpause() {
    SoundManager* mgr = SoundManager::GetInstance();
    if (mgr->vtable->IsInterrupted()) {
        m_field04 = 1;  // defer until interruption clears
        return;
    }
    for (int i = 0; i < 32; i++) {
        SoundSlot* s = &m_Slots[i];
        if (s->field12 != 0) {   // +0x12: was paused-by-system
            s->pSound->Resume();
            s->field12 = 0;
        }
    }
}
```

### SoundSlot: revised field layout

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | MortarSound* | pSound | |
| +0x04 | 4 | uint | nameHash | |
| +0x08 | 8 | -- | padding | |
| +0x10 | 1 | byte | isFree | 1=free |
| +0x11 | 1 | byte | field11 | (unused, set 0 at init) |
| +0x12 | 1 | byte | field12 | 1 = paused-by-system (GameSound::Pause/Unpause) |
| +0x14 | 4 | float | volume | |
| +0x18 | 4 | float | pitch | |
| +0x1c | 28 | Delegate1\<bool,MortarSound*\> | callback | Completion callback |

---

## BadaSound reference summary

BadaSound is the Bada OS concrete audio backend. It is NOT a subclass of SoundManager or
MortarSound -- it is a separate platform layer used by `SoundManagerMAM::SFXPlayInternal`
via `MAMAudioController::LoadSound`. The SDL2 backend replaces BadaSound + MAMAudioThread
entirely.

| Aspect | Detail |
|--------|--------|
| SFX storage | Hash table (256 slots): `m_SoundHashes[256]` + `m_SoundEffects[256]` |
| Concurrent playback | 8 `ActiveEffect` slots (12 bytes each: handle, data, sampleCount) |
| SFX format | Raw 16-bit PCM, little-endian, mono, loaded from `.wav.pcm` files |
| Music | Osp::Media::Player (streaming, supports pause/resume/volume) |
| SFX volume | `m_SFXVolume` float, default 0.4 |
| Music volume | `m_MusicVolume` float, default 0.45 |
| PCM output | Osp::Media::AudioOut, PCM_S16_LE, 2520-byte initial silence buffer |

For full layout and decompiled methods, see `docs/engine/sound-system.md`.

---

## Port path: SDL2 backend implementation

The SDL2 backend must replace the bottom two layers of the architecture:

```
GameSound (keep as-is)
  v
SoundManager / SoundManagerMAM (keep; SFXPlayInternal needs minor changes -- see below)
  v
[SDL2 backend -- replaces MAMAudioController + MAMAudioThread + BadaSound]
  v
SDL_AudioCallback mixing thread
```

### What each MortarSound method needs from the SDL2 backend

| MortarSound method | SDL2 backend action |
|--------------------|---------------------|
| `Play()` | `SoundManager::SFXPlay` path: `MAMAudioController::PlaySound` -> push Play cmd. SDL2 equivalent: atomically assign a free voice in SDL_AudioCallback's voice table; return a new monotonic ID as m_Handle. |
| `Stop(0.0f)` | `MAMAudioController::StopSound` -> push Stop cmd. SDL2: mark voice inactive by ID. Immediate stop (fadeTime=0 always). |
| `Pause()` | `MAMAudioController::PauseSound` -> push Pause cmd. SDL2: set voice isActive=false, preserve playOffset. |
| `Resume()` | `MAMAudioController::ResumeSound` -> push Resume cmd. SDL2: set voice isActive=true. |
| `SetVolume(v)` | `MAMAudioController::SetSoundVolume(id, (uint8)(v*255))` -> push Volume cmd. SDL2: update voice volume. |
| `IsPlaying()` | Guard on m_Handle==0 (automatic state reset). No backend call needed -- m_State tracks it. |
| `IsPaused()` | Same -- no backend call. |
| `Destroy()` / `InternalDestroy()` | Calls Stop + RemoveListener. SDL2: mark voice inactive, remove completion callback entry. |
| `Load(name)` | Set m_Name (heap copy). No backend call. |

### Key implementation constraints

1. **m_Handle is a monotonic uint**, not a buffer pointer. The SDL2 voice table must be
   searchable by this ID. Use a 16-entry array matching MAMAudioThread's 16-voice limit.

2. **NLFQueue command pattern**: The binary uses lock-free queues between game thread and audio
   callback. SDL2 can use `SDL_LockAudioDevice` / `SDL_UnlockAudioDevice` instead, or a simple
   ring buffer protected by atomic ops. All 5 command types (Play/Stop/Pause/Resume/Volume) must
   be implemented.

3. **PCM format**: MAMAudioController::LoadSound right-shifts all samples >>4 after loading.
   SDL2 backend should load `.wav.pcm` (raw 16-bit LE mono) and apply the same >>4 shift, or
   match the original 16kHz mixer sample rate (MAMAudioThread: `sampleRate=16000`).

4. **8 vs 16 concurrent voices**: BadaSound used 8 ActiveSlots; MAMAudioThread uses 16 voices.
   The port should use 16 to match MAMAudioThread (the actual playback layer).

5. **Music**: SoundManager::SongPlay/Stop/Pause/Resume delegate to MAMAudioController, which
   is a separate path from the PCM voice mixer. SDL2 backend can use SDL_mixer or a secondary
   SDL_AudioStream for music. The binary's music volume default is 0.45f; SFX volume default
   0.4f (from BadaSound constructor).

6. **Completion callbacks**: `MAMAudioController::PlaySound` accepts an optional listener ptr
   (`m_Handle` address in the MortarSound object) that is fired when playback completes. This
   feeds into `GameSound::Update`'s `IsPlaying()` check. The SDL2 voice must mark the ID as
   finished in the output queue (or simply zero the voice) so `m_Handle==0` triggers correctly.

### SoundManagerMAM::SFXPlayInternal call for the port

The existing `SoundManagerMAM::GetSound` / `MAMAudioController::PlaySound` path is correct.
The `else` branch (`sound != NULL` -> `Load + Play`) is used when `SoundManager::SFXPlay` is
called with a pre-allocated `MortarSound*` (the GameSound pool path). That path calls
`Load(sound, name)` then `Play(sound)`, which internally calls `SoundManager::SFXPlay` again
recursively, but with `sound==NULL`. The port's `MortarSound::Play` implementation must call
`SoundManager::SFXPlay(m_Name, 0, NULL, ...)` (the no-sound-ptr overload) to trigger
`GetSound -> MAMAudioController::PlaySound`.

---

## GOT-thunk dead ends (flag for implementer)

| Symbol | Reason | Impact |
|--------|--------|--------|
| `MAMAudioController::GetInstance` | Singleton behind GOT; no resolvable body found | Low -- standard singleton; port uses its own instance |
| `MAMAudioController::Find` | Called inside PlaySound/StopSound; no symbol | Low -- simple linked-list search; only needed if completion callbacks are implemented |
| `MAMAudioController::RemoveListener` | Same | Low -- remove ListenPair node by ID |

None of the dead ends block a functional SDL2 implementation. They only affect completion-
callback accuracy (`m_Handle` zeroing on voice finish), which matters for `IsPlaying()` correctness
in `GameSound::Update`.

---

## See also

- `docs/engine/sound-system.md` -- full four-layer architecture + BadaSound full decompile
- `docs/engine/audio-internals.md` -- MAMAudioController / MAMAudioThread / NLFQueue internals
- `src/engine/audio/MortarSound.{h,cpp}` -- current stub port
- `src/engine/audio/SoundManager.{h,cpp}` -- current stub port
- `src/engine/audio/GameSound.{h,cpp}` -- fully ported pool layer
