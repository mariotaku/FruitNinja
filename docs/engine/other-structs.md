# Engine Structs & Singletons

## MAMAudioThread (size >= 0x154)

| Offset | Type | Name | Notes |
|--------|------|------|-------|
| +0x10 | float | masterVolume | -1.0f |
| +0x18 | int | sampleRate | 16000 = 16 kHz |
| +0x1c | int | bufferSize | 0xc80 = 3200 |
| +0x30 | int | voiceCount | = 16 |
| +0x34 | MAMVoice[16] | voices | Each 0x10 bytes = 0x100 total |
| +0x134 | NLFQueue | cmdInput | Thread-safe audio command input |
| +0x144 | NLFQueue | cmdOutput | Thread-safe audio command output |

## Engine Subsystem Singletons

All accessed via GOT-relative addressing (ARM32 position-independent code).

### Analysis Coverage

| Manager | Functions | Struct Size | Named Fields | Coverage | Doc |
|---------|-----------|-------------|--------------|----------|-----|
| **MatrixManager** | 35+ | 8500 bytes | 9 | **Full** | [matrix-manager.md](matrix-manager.md) |
| **DisplayManager** | 30+ | 148 bytes | 16 | **Full** | [display-manager.md](display-manager.md) |
| **SystemManager** | 7 | 212 bytes | 11 | **Good** | [system-manager.md](system-manager.md) |
| **ActorManager** | 11 | 4204 bytes | 7 | **Good** | [actor-manager.md](actor-manager.md) |
| **InputManager** | 11 | 9 bytes | 2 | **Good** | [input-manager.md](input-manager.md) |
| **PSPParticleManager** | 5 | 48 bytes | 8 | **Good** | [particles.md](particles.md) |
| **SoundManager** | 15 | 40 bytes | 2+4 statics | **Full** | [sound-system.md](sound-system.md) |
| **FileManager** | 5 | 8 bytes | 1 (list) | **Good** | [other-structs.md](#filemanager-8-bytes) |
| **TextureManager** | 16 | 24 bytes | 1 (full map) | **Full** | [texture-mesh-manager.md](texture-mesh-manager.md) |
| **MeshManager** | 12 | 20 bytes | 6 | **Good** | [texture-mesh-manager.md](texture-mesh-manager.md) |
| **AnimationManager** | 8 | 20 bytes | 6 | **Good** | [texture-mesh-manager.md](texture-mesh-manager.md) |

All managers have `__thiscall` properly applied to every function.

### Singleton Details

| Singleton | Struct Size | Notes |
|-----------|-------------|-------|
| SystemManager | 212 bytes | FPS ring buffer, quit lifecycle. See [system-manager.md](system-manager.md) |
| MatrixManager | 8500 bytes | 4 matrix stacks, dirty-tracking. See [matrix-manager.md](matrix-manager.md) |
| DisplayManager | 148 bytes | GL state, viewport, screen rotation. See [display-manager.md](display-manager.md) |
| FileManager | 8 bytes | VFS: `std::list<IFileSystem*>` with priority sorting. See below |
| TextureManager | 24 bytes | `std::map<ulong, WeakPtr<Texture>>` cache. See [texture-mesh-manager.md](texture-mesh-manager.md) |
| MeshManager | 20 bytes | `List<SmartPtr<Model>>`. See [texture-mesh-manager.md](texture-mesh-manager.md) |
| AnimationManager | 20 bytes | `List<Animation*>` singleton. See [texture-mesh-manager.md](texture-mesh-manager.md) |
| InputManager | 9 bytes | Action-hash callbacks, 16-touch. See [input-manager.md](input-manager.md) |
| SoundManager | 40 bytes | `List<MortarSound*>` + static volume globals. See [sound-system.md](sound-system.md) |
| MAMAudioController | — | Spawns MAMAudioThread (16 voices, 16kHz). See MAMAudioThread struct above |
| Mortar::Touch | — | Low-level touch ring buffer (TEvnt) |
| PSPParticleManager | 48 bytes | Template-based emitters (8 named fields). See [particles.md](particles.md) |
| ActorManager | 4204 bytes | Entity pool (7 named fields). See [actor-manager.md](actor-manager.md) |

---

## FileManager (8 bytes)

Priority-sorted VFS layer. Just a `std::list<IFileSystem*>` (8 bytes on ARM32 Bada ABI).

| Offset | Size | Type | Name |
|--------|------|------|------|
| +0x00 | 8 | std::list\<IFileSystem*\> | m_fileSystems |

### Methods

| Function | Address | Notes |
|----------|---------|-------|
| FileManager() | 0x0019b0f8 | Constructs list + clear |
| ~FileManager | 0x0019b09c | ClearSystems + ~list |
| AddSystem | 0x0019b170 | Insert IFileSystem sorted by priority |
| RemoveSystem | 0x0019afe4 | Remove + delete from list |
| ClearSystems | 0x0019b074 | Loop RemoveSystem until empty |

### IFileSystem / FileSystem_Direct

- **IFileSystem** — abstract base with vtable (Open, Exists, etc.)
- **FileSystem_Direct** (~17 bytes) — direct file I/O on Bada. Fields: vtable, priority (uint), sortOrder (int), flags

`MortarGame::CreateFileSystems()` is an empty stub on Bada. FileSystem_Direct is added during GameInitialise.

**Port relevance:** None — port uses direct SDL file I/O.
