# Mortar Engine Implementation TODO

Comprehensive task list for implementing the `mortar_engine` static library.
Reference docs are in `docs/engine/` (19 files). All struct layouts, method signatures,
and behavior are documented from RE of the original ARM32 binary.

## Status

Phases 1-3 are implemented. Phases 4-8 are TODO.

### Phase 1 — Foundation ✅ DONE

Files in `core/`, `math/`, `util/`:

- [x] `util/StringHash.h/.cpp` — Jenkins lookup3 hash
- [x] `util/ReferenceCounter.h` — Intrusive refcount base (12B)
- [x] `util/SmartPtr.h` — 4-byte intrusive smart pointer
- [x] `util/Delegate.h` — Delegate0-4 via std::function
- [x] `util/NLFQueue.h` — SPSC ring buffer (16B)
- [x] `util/AsciiString.h` — String wrapper
- [x] `util/List.h` — Mortar List<T> template
- [x] `math/Vec3.h` — _Vector3<float> (12B)
- [x] `math/Matrix44.h` — Column-major 4x4 + OrthoW, GlobalTranslate44, LocalTranslate44, Scale44
- [x] `math/Quaternion.h` — Quaternion with FromAxisAngle, ToMatrix44
- [x] `math/Colour.h` — BGRA packed + TintWhite/TintColour
- [x] `math/Random.h/.cpp` — 64-bit LCG (Knuth MMIX)
- [x] `math/MathUtil.h` — Clamp, Lerp, SinIdx, CosIdx
- [x] `math/math3d.h` — Standalone float[16] helpers
- [x] `core/Singleton.h` — Meyers singleton template
- [x] `core/MortarTypes.h` — FN_SCREEN_W/H, MortarRectangle
- [x] `core/SystemManager.h/.cpp` — FPS ring buffer, quit lifecycle

**Build note:** Phase 1 has NOT been build-tested yet (MSYS2 env issue). Test on Linux first.

---

### Phase 2 — Core Singletons ✅ DONE

Ref: `docs/engine/matrix-manager.md`, `docs/engine/display-manager.md`

- [x] `render/gl_funcs.h/.cpp` — Migrated + extended with glScissor, glGetError, glPixelStorei, glCompressedTexImage2D
- [x] `render/MatrixStack.h` — Full 32-deep stack (2120 bytes), version-tracked, Push/Pop/Scale/Translate
- [x] `render/MatrixManager.h/.cpp` — 4 MatrixStacks + dirty upload, singleton, SetupOrtho/LookAt/GetMVP
- [x] `render/DisplayManager.h/.cpp` — GL state singleton, BeginFrame/EndFrame/SwapBuffers/SetDrawColour
- [x] `asset/FileManager.h` — Stub header-only, no-op AddSystem/RemoveSystem

---

### Phase 3 — Rendering ✅ DONE

Ref: `docs/engine/rendering-pipeline.md`, `docs/engine/rendering-detail.md`

- [x] `render/QUADCUSTOMVERTEX.h` — QUADCUSTOMVERTEX (36B) + QuadVertex (20B) with static_assert
- [x] `render/Renderer.h/.cpp` — Migrated + expanded: 3 shaders (2D tint, 3D mesh, 2D vertex-color), DrawTriList/DrawTriStrip, MatrixManager singleton (no longer owned by Renderer)
- [x] `render/MortarCamera.h/.cpp` — Camera with SetupOrtho/SetupLookAt/SetupPerspective via MatrixManager
- [x] `collision/ColAABB.h` — AABB with AABB-AABB, AABB-Sphere, AABB-Line tests
- [x] `collision/ColLine.h` — Line segment with closest-point helpers
- [x] `collision/ColSphere.h` — Sphere with Sphere-Sphere, Sphere-Line, Contains tests

---

### Phase 4 — Asset Pipeline (depends on Phase 2+3)

Ref: `docs/engine/texture-mesh-manager.md`, `docs/engine/utility-types.md`, `docs/engine/formats/`

- [ ] `asset/tex_loader.h/.cpp` — Migrate from existing `src/engine/tex_loader.*`. Parses .tex files (12-byte header: format, width, height), converts RGBA4444/RGB565 to GL texture.
- [ ] `asset/Texture.h/.cpp` — Texture2D class inheriting ReferenceCounter
  - GL texture handle, width, height
  - `Set()` / `UnSet()` — bind/unbind
  - `static SmartPtr<Texture> Load(const char* path)` — factory via tex_loader
- [ ] `asset/TextureManager.h/.cpp` — Singleton, `std::map<uint32_t, WeakPtr<Texture>>` cache (24 bytes)
  - `Load(const char* path)` — StringHash → Find → if miss: Texture::Load → Add → return SmartPtr
  - `Find(uint32_t hash)` / `Find(const char* name)`
  - `Add(uint32_t hash, SmartPtr<Texture>)`
  - Ref: Loading flow in `docs/engine/texture-mesh-manager.md`
- [ ] `asset/ResourceLoader.h/.cpp` — HBR0 container parser (68 bytes)
  - Recursive: reads child count, constructs child ResourceLoaders
  - `RegisterLoader<T>(Delegate1<SmartPtr<T>, ResourceLoader&>)` — typed callback in global map
  - `Load<T>(name)` — opens file, initializes, calls registered loader
  - Ref: `docs/engine/utility-types.md`
- [ ] `asset/Model.h` — Model with vector<SmartPtr<Mesh>>, depth-sorted Draw
- [ ] `asset/Mesh.h` — Mesh with GeometryBinding, PassBinding, EffectProperties
  - Simplified for port: store vertex/index data + texture reference directly
  - Skip full Effect/GeometryBinding system; use Renderer's 3D shader
- [ ] `asset/MeshManager.h/.cpp` — List<SmartPtr<Model>> cache (20 bytes)
  - `Load(name)` → ResourceLoader chain
- [ ] `asset/AnimationManager.h/.cpp` — List<Animation*> cache (20 bytes), low priority

---

### Phase 5 — Input (depends on Phase 1+2)

Ref: `docs/engine/input-manager.md`, `docs/engine/touch-system.md`

- [ ] `input/InputEvent.h` — Event struct
  - `uint32_t actionHash` — StringHash of action name
  - `uint32_t actionFlags` — DOWN=1, MOVE=2, UP=4
  - `int fingerId` — touch finger index
  - `float x, y` — position in game coordinates
- [ ] `input/InputManager.h/.cpp` — Action-hash callback dispatch singleton
  - `RegisterInputCallback(uint32_t actionHash, uint32_t flags, Delegate1<bool, InputEvent*> callback)`
  - `DispatchEvent(InputEvent* event)` — iterates registered callbacks matching hash+flags
  - `LoadConfigFile(const char* path)` — parses action config XML
  - `ClearActions()`
  - Migrate from existing `src/platform/InputManager.*`, expand with full API
- [ ] `input/Touch.h/.cpp` — Double-buffered 8-slot multitouch (468 bytes)
  - State (28B): startX/Y, currentX/Y, pointerId, touchId, phase
  - TEvnt (20B): pointerId, isDown, x, y, timestamp
  - Ring buffer of 10 TEvnts
  - `__UpdateInternal()` — push TEvnt to ring buffer
  - `Update(dt)` — drain ring buffer, update back buffer, swap to front
  - `SendIndividualTouchCallbacks()` — emit InputDevice events
- [ ] `input/SDLInputTranslator.h/.cpp` — SDL events → Mortar touch events
  - Migrate from existing `src/platform/SDLInputTranslator.*`
  - SDL_FINGERDOWN/MOVE/UP → Touch::__UpdateInternal
  - Mouse fallback for desktop

---

### Phase 6 — Audio (depends on Phase 1+2)

Ref: `docs/engine/sound-system.md`, `docs/engine/audio-internals.md`

- [ ] `audio/MortarSound.h/.cpp` — Sound instance (16 bytes)
  - vtable, m_Name (char*), m_Handle (uint), m_State (0=idle, 1=paused, 2=playing)
  - `SetVolume(float vol)` — maps 0.0-1.0 to 0-255
- [ ] `audio/SoundManager.h/.cpp` — Abstract base singleton (40 bytes)
  - `std::list<MortarSound*>` m_Sounds
  - Static volume globals (m_SFXVolume, m_MusicVolume, m_SFXMuted, m_MusicMuted)
  - Virtual: `SFXPlay()`, `SFXStop()`, `SFXPauseAll()`, `SFXUnpauseAll()`
  - Virtual: `PreLoadSound()`, `PreLoadSoundEx()`
- [ ] `audio/SoundManagerSDL.h/.cpp` — SDL_AudioCallback backend
  - Replaces the MAM/Bada 4-layer stack with SDL2 raw audio
  - 16 voices, 16kHz, 16-bit mono PCM mixing
  - Uses NLFQueue for thread-safe command passing (matching original pattern)
  - `SFXPlay()` → find free voice, assign sound, start mixing
  - `SFXStop()` → clear voice
  - Audio callback: drain command queue, mix active voices, output
- [ ] `audio/GameSound.h/.cpp` — Game-level 32-slot pool (1800 bytes)
  - 32 SoundSlots (56B each): pSound, nameHash, isFree, volume, pitch, callback
  - `SFXPlay(nameHash, volume, pitch)` — find free slot, delegate to SoundManager
  - Volume formula: `(1 - (1 - masterVol) * vol) * pitch`
  - `FindFree()`, `IsPlaying()`, `Release()`, `KillAll()`

---

### Phase 7 — Entities & Particles (depends on Phase 1+2)

Ref: `docs/engine/actor-manager.md`, `docs/engine/particles.md`

- [ ] `entity/Entity.h` — Base entity class (0x3C bytes)
  - Vtable must match original order: ~dtor, ~dtor, OnActivate, OnDeactivate, Update, Draw, PostUpdate
  - Fields: pos (Vec3), vel (Vec3), angle, scale, flags (byte at +0x0C byte 3), m_EntityType (byte at +0x35)
  - Flag bits: 0x01=inactive, 0x04=updating, 0x08=post-updating, 0x10=pending deactivate, 0x20=no destruct
- [ ] `entity/ActorManager.h/.cpp` — Per-type entity pool (4204 bytes)
  - `Entity* m_FreePool[512]` at +0x008
  - `int m_FreeCount` at +0x808
  - `std::list<Entity*>* m_TypeLists` at +0x1010 (array of per-type lists)
  - `int m_NumTypes` at +0x101C
  - `Delegate1<Entity*, long> m_FactoryDelegate` at +0x1024
  - `Initialise(numTypes, heapSize)` — allocate type lists
  - `Add(entityType, activate)` — recycle from free pool or factory
  - `Update(dt)` — tick all, collect deactivation queue, process
  - `Draw()` — render all active
  - `Deactivate(entity)` — move to free pool
  - `Remove(entity)` — destroy (no recycle)
- [ ] `particle/PSPEmitterTemplate.h` — Loaded template data from XML
- [ ] `particle/PSPParticle.h` — Individual particle (164 bytes)
- [ ] `particle/PSPParticleEmitter.h` — Runtime emitter instance (~0x4C bytes)
- [ ] `particle/PSPParticleManager.h/.cpp` — Template-based emitter singleton (48 bytes)
  - `AddEmitter(hash, pos, ...)` — create emitter from template
  - `Update(dt)` — tick all emitters and particles
  - `Draw()` — render all particles

---

### Phase 8 — Font (depends on Phase 3+4)

Ref: `docs/engine/rendering-pipeline.md` (Font System section)

- [ ] `render/Font.h/.cpp` — BMFont .fnt loader + DrawString (~0x430 bytes)
  - 256 glyph entries, page count, atlas dimensions, scale factor
  - `static SmartPtr<Font> Load(const char* path)` — parse BMFont .fnt text format
  - `DrawString(scale, maxWidth, z, atlas, text, pos, colour, alignment, flags)`:
    - Inline color tags `[FFFFFF]text[/]`
    - Word wrapping at maxWidth
    - Per-glyph quad generation with kerning
    - Multi-page atlas support
    - Alignment flags (0x0F mask): left/center/right/top/bottom
    - Batches into per-page vertex arrays, flushes via DrawTriList
- [ ] `render/BakedString.h/.cpp` — Pre-baked text vertex cache (28 bytes)
  - `m_pTextures` — array of page textures
  - `m_PageCount`, `m_pVertexData`, `m_pVertexCounts`
  - `m_Width`, `m_Height`
  - `Bake(font, text, ...)` — render once via Font::DrawString, cache vertices
  - `Draw()` — iterate pages, bind texture, DrawTriList with cached verts

---

## CMakeLists.txt

Current `src/engine/CMakeLists.txt` defines `mortar_engine` with Phase 1-3 sources (8 .cpp files).
Add sources from each phase as implemented. The full target will include ~25 .cpp files.

Legacy `fn_engine` target is kept temporarily for `tex_loader.cpp` and `Mesh.cpp` (Phase 4 migration).
Old headers at `src/engine/` root (`gl_funcs.h`, `MatrixManager.h`, `Renderer.h`) are forwarding headers.
Old `src/math/` headers forward to `src/engine/math/` — canonical math types live in the engine.

## Migration Checklist

When the engine library is complete, update game code:

- [ ] Remove `add_subdirectory(src/math)` and `add_subdirectory(src/platform)` from root CMakeLists.txt
- [ ] Change all game libs to link `mortar_engine` instead of `fn_engine fn_math fn_platform`
- [ ] Update game includes: `#include "Vec3.h"` → `#include "math/Vec3.h"`
- [ ] Update game includes: `#include "Renderer.h"` → `#include "render/Renderer.h"`
- [ ] Update game includes: `#include "InputManager.h"` → `#include "input/InputManager.h"`
- [ ] Move singletons out of Game struct into engine GetInstance() calls
- [ ] Delete old `src/math/`, `src/platform/`, old files in `src/engine/`
- [ ] Remove legacy `fn_engine` target from CMakeLists.txt

## Key Design Notes

1. **SetupOrtho param order**: `(top, bottom, left, right, near, far)` — NOT standard GL `(left, right, bottom, top)`. The game calls `SetupOrtho(160, -160, -240, 240, 2000, -6000)`.

2. **GLES2 translation**: Original uses GL ES 1.x fixed-function (glMatrixMode, glVertexPointer, etc.). Port uses shaders. MatrixManager computes MVP and stores as uniform. Renderer manages shader programs and attribute binding.

3. **Two rendering paths**: Path A (3D) goes through Effect/Geometry/PassBinding. Path B (2D) is immediate-mode DrawQuad/DrawTriList. For the port, both use Renderer's GLES2 shaders.

4. **SmartPtr is intrusive**: Objects inherit ReferenceCounter. NOT std::shared_ptr.

5. **Delegate wraps std::function**: Match original API (Delegate0-4, QCallee<T> factory) but use std::function internally. Don't replicate binary layout.

6. **Original centered ortho**: All positions use centered coordinates. X: +160 (top) to -160 (bottom). Y: -240 (left) to +240 (right). No conversion needed.
