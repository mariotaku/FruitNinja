# MAMAudioController & Threading Internals

## Architecture

Two-thread producer/consumer audio system:

```
Game Thread                         Audio Callback Thread
    │                                       │
    ├─ SoundManagerMAM::SFXPlayInternal     │
    │   → MAMAudioController::PlaySound     │
    │     → push MAMSoundInputCmd           │
    │       to NLFQueue (game→audio)  ──────→ MAMAudioThread::FillBuffer
    │                                       │   → ProcessSoundCommands (drain queue)
    │                                       │   → mix active voices into output buffer
    │                                       │   → push MAMSoundOutputCmd
    │  ←────────────────────────────────────│     to NLFQueue (audio→game)
    ├─ MAMAudioController::Update           │
    │   → drain output queue                │
    │   → fire completion callbacks         │
```

## MAMAudioThread (340 bytes)

| Offset | Size | Type | Name | Notes |
|--------|------|------|------|-------|
| +0x00 | 4 | void* | vtable | |
| +0x10 | 4 | float | masterVolume | -1.0f (disabled) |
| +0x18 | 4 | int | sampleRate | 16000 = 16kHz |
| +0x1C | 4 | int | bufferSize | 0xC80 = 3200 bytes |
| +0x30 | 4 | int | voiceCount | 16 |
| +0x34 | 256 | MAMVoice[16] | voices | 16 bytes each |
| +0x134 | 16 | NLFQueue | cmdInput | Game→audio commands |
| +0x144 | 16 | NLFQueue | cmdOutput | Audio→game completions |

### MAMVoice (16 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | MAMSound* | pSound | PCM data reference |
| +0x04 | int | soundId | Monotonic ID from controller |
| +0x08 | int | playOffset | Current sample position |
| +0x0C | byte | isLooping | Loop flag |
| +0x0D | byte | isActive | Playing flag |
| +0x0E | byte | volume | 0-255 (0xFF = full) |

### MAMSound (12 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | int16* | pcmData | 16-bit PCM, right-shifted >>4 on load |
| +0x04 | int | sampleCount | Number of samples |
| +0x08 | int | loopStart | Sample offset for loop point |

## MAMAudioController (~12 bytes)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x00 | MAMAudioThread* | m_pThread | |
| +0x04 | int | m_NextSoundId | Monotonic counter |
| +0x08 | ListenPair* | m_pListeners | Completion callback linked list |

### Command Types (ProcessSoundCommands)

7 command types in MAMSoundInputCmd:
1. **Play** — assign voice, set sound/offset/loop/volume
2. **Stop** — clear voice by soundId
3. **Pause** — set voice inactive
4. **Resume** — set voice active
5. **Volume** — update voice volume
6. **Suspend** — pause all voices
7. **Unsuspend** — resume all voices

## NLFQueue (16 bytes)

Non-locking FIFO queue. Single-producer/single-consumer (no CAS needed). Fixed circular buffer.

| Offset | Type | Name |
|--------|------|------|
| +0x00 | void* | m_Buffer |
| +0x04 | int | m_Capacity |
| +0x08 | int | m_ReadIdx |
| +0x0C | int | m_WriteIdx |

## LFQueue (~8 bytes)

True lock-free queue using CAS (`SafeCompareAndSwapPointer`). Node-based (12-byte nodes: next + data). Used for multi-producer job dispatch in WorkGroup.

## CriticalSection

**Empty/no-op on Bada.** Constructor and destructor are both empty stubs — no mutex wrapper. The engine relies on single-producer/single-consumer NLFQueue guarantees instead.

## WorkGroup (24 bytes)

| Offset | Size | Type | Name |
|--------|------|------|------|
| +0x00 | 1 | bool | m_Initialized |
| +0x04 | 12 | LFQueue\<SmartPtr\<Job\>\> | m_JobQueue |
| +0x10 | 8 | std::list\<WorkerThread*\> | m_Workers |

`WakeWorkerThread` delegates to `NetworkManager::SpawnThreadController`.

## WorkerThread (~8 bytes)

| Offset | Type | Name |
|--------|------|------|
| +0x00 | SmartPtr\<Job\> | m_CurrentJob |
| +0x04 | byte | m_bRunning |

Entry point `workerThreadIphoneMain`: loop → set running, pop jobs from WorkGroup's LFQueue, run each, clear flag.

---

## Port Notes

- Replace MAMAudioThread with SDL_AudioCallback mixing loop
- NLFQueue pattern maps to SDL_AudioStream or a simple ring buffer
- CriticalSection is no-op — SDL_mutex where needed
- WorkGroup/WorkerThread can be ignored (single-threaded port) or replaced with SDL_Thread

---

## See Also

- [Sound system](sound-system.md) — SoundManager/BadaSound/GameSound high-level
- [Other structs](other-structs.md) — MAMAudioThread struct reference