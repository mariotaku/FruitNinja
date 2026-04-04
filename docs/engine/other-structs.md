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
| **SoundManager** | 13 | 40 bytes | 2 | **Partial** | [sound-system.md](sound-system.md) |
| **FileManager** | 5 | 8 bytes | 0 | **Partial** | — |
| **TextureManager** | 10+ | 24 bytes | 1 (full map) | **Good** | [texture-mesh-manager.md](texture-mesh-manager.md) |
| **MeshManager** | 8+ | 20 bytes | 6 | **Good** | [texture-mesh-manager.md](texture-mesh-manager.md) |
| **AnimationManager** | 6 | 1 byte stub | 0 | **Stub** | — |

All managers have `__thiscall` properly applied to every function.

### Singleton Details

| Singleton | Struct Size | Notes |
|-----------|-------------|-------|
| SystemManager | 212 bytes | Engine lifecycle, QuitGame(). 2 named fields (m_deviceId, vtable) |
| MatrixManager | 8500 bytes | 4 matrix stacks (Projection/View/World/Texture), dirty-tracking. See [matrix-manager.md](matrix-manager.md) |
| DisplayManager | 148 bytes | GL state, viewport, BeginFrame/EndFrame/SwapBuffers. Subclass: DisplayManagerBada (7 functions) |
| FileManager | 8 bytes | VFS abstraction (FileSystem_Direct on Bada) |
| TextureManager | 24 bytes | .tex loading + caching. 1 named field (m_textures map) |
| MeshManager | 1 byte stub | .mad/.mmd model loading + caching. Needs RE |
| AnimationManager | 1 byte stub | Skeletal/property animation. Needs RE |
| InputManager | 9 bytes | Action-hash callbacks, 16-touch. See [input-manager.md](input-manager.md) |
| Mortar::SoundManager | 40 bytes | Platform audio abstraction. Subclass: SoundManagerMAM (6 functions). See [sound-system.md](sound-system.md) |
| MAMAudioController | — | Spawns MAMAudioThread (16 voices, 16kHz). See MAMAudioThread struct above |
| Mortar::Touch | — | Low-level touch ring buffer (TEvnt) |
| PSPParticleManager | 48 bytes | Template-based particle emitters (8 named fields). See [particles.md](particles.md) |
| ActorManager | 4204 bytes | Entity pool manager. Full pseudocode documented but 0 struct fields named in Ghidra. See [actor-manager.md](actor-manager.md) |

### Biggest Gaps for Porting

1. **ActorManager** — 4204-byte struct with 0 named fields in Ghidra (docs have full pseudocode)
2. **DisplayManager** — need GL state fields for ES 2.0 port (148 bytes, only 4 named)
3. **MeshManager / AnimationManager** — 1-byte stubs, need real layouts if mesh caching is ported
4. **TextureManager** — only 24 bytes / 1 field, but texture loading is critical path
