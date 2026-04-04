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

| Singleton | Notes |
|-----------|-------|
| SystemManager | Engine lifecycle, QuitGame() |
| MatrixManager | 4 matrix stacks, dirty-tracking. See [matrix-manager.md](matrix-manager.md) |
| DisplayManager | GL state, viewport, BeginFrame/EndFrame/SwapBuffers |
| FileManager | VFS abstraction (FileSystem_Direct on Bada) |
| TextureManager | .tex loading + caching |
| MeshManager | .mad/.mmd model loading + caching |
| AnimationManager | Skeletal/property animation |
| InputManager | Action-hash callbacks, 16-touch. See [input-manager.md](input-manager.md) |
| Mortar::SoundManager | Platform audio abstraction. See [sound-system.md](sound-system.md) |
| MAMAudioController | Spawns MAMAudioThread (16 voices, 16kHz) |
| Mortar::Touch | Low-level touch ring buffer (TEvnt) |
| PSPParticleManager | Template-based particle emitters. See [particles.md](particles.md) |
