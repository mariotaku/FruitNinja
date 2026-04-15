# Session Notes — 2026-04-15

Port/RE work from a single long session. Covers GameModeScreen port,
sound stubs, DojoScreen back-bomb fixes, Bomb rotation lock, Bomb
fuse particle offset, debug tooling, particle draw order RE, and an
unresolved bomb atlas / texture rendering issue. Written as an
end-of-session snapshot so the next session can pick up.

---

## 1. Shipped this session

### 1.1 GameModeScreen port (`src/screens/GameModeScreen.{h,cpp}`)

Full port of the mode-select child screen (Classic / Zen / Arcade).
Binary refs:

- `GameModeScreen::GameModeScreen(bool) @ 0x0013e524`
- `GameModeScreen::CreateControls @ 0x0013e764`
- `GameModeScreen::Update @ 0x0013f10c`
- `ClassicModeCallback @ 0x0013dfb4`
- `ZenModeCallback @ 0x0013dffc`
- `ArcadeModeCallback @ 0x0013e19c`
- Instantiation site in `MainScreen::Update` at case 0x0e/0x0f,
  `~0x0014bf40` (spawn when `m_Timer2` crosses 0.25 downward)

Three `MenuButton` sub-buttons with offline positions from
`read_memory`:

| Button  | Position              | Fruit type | Callback state |
|---------|-----------------------|------------|----------------|
| Classic | `(195, -110, 0)`      | watermelon | state = 3      |
| Zen     | `(-70,  71,  0)`      | (port: 5)  | state = 6      |
| Arcade  | `(88,   48,  0)`      | (port: 8)  | state = 5      |

States implemented: 0 (lerp in), 2 (idle), 3-6 (mode-picked fade out),
0xe (back-out). States 1, 7, 8, 9 (network/pause paths) skipped.

MainScreen STATE_MODE_SELECT (0xe/0xf) now matches binary: decay
`m_Timer2 * 0.85` continuously and spawn `GameModeScreen` on the ONE
frame `m_Timer2` crosses 0.25. Child's `m_RemoveCallback` nulls the
parent's `m_pGameModeScreen` weak ref to avoid UAF.

### 1.2 Sound stubs wired (`src/Game.{h,cpp}`, several call sites)

`Game::pGameSound` is now owned, constructed in `GameInitialise` and
destroyed in `GameDestroy`. Replaced `// TODO: SFXPlay(...)` comments
with real calls at:

- `MainScreen::QuitGamesCallback` — `"swoosh_sound"`
- `SliceEffect_Add` — `"Clean-Slice-{1,2,3}"` (gated on
  `impulse > 2.5 && rand()%3 == 0` matching binary)
- `Bomb.cpp` menu-bomb hit — `"menu-bomb"`
- `Bomb.cpp` game-bomb explode — `"Bomb-explode"`
- `MainScreen::SoundCallback` — `SoundManager::SetSFXVolume`

Backend is still a no-op — `SoundManager` itself is stubbed. Wiring
an SDL audio backend later is a drop-in change.

### 1.3 DojoScreen back-bomb `PlayCallback` fix

Ported `DojoScreen::QuitCallback @ 0x001389F4` more faithfully:

1. **Play "menu-bomb" SFX** via `GameSound`. The earlier TODO comment
   and `docs/port-next-steps.md` both incorrectly said
   `"swoosh_sound"` — `"swoosh_sound"` is `MainScreen::NewGameCallback`
   (Play/watermelon). The back-bomb callback's actual SFX name is
   `"menu-bomb"` (string at `0x001B96C9`).

2. **Fling `m_pPlayButton->m_pFruitPiece`** after setting `m_State = 6`:

       vel = (RandFloat5() + 5.0, -RandFloat5(), 0.0)

   Binary guards on `piece != NULL`, so for the bomb-typed back
   button (no fruit piece) this is a no-op. Replicates the binary
   literally; `AboutCallback` uses the same pattern.

### 1.4 `MainScreen::ButtonDeleted` (`0x0014acc0`)

Binary has a single `MainScreen::ButtonDeleted(HUDControl*)` delegate
method that dispatches by identity across `pPlayButton`, `pDojoButton`,
`pQuitBtn`, `pMoreGamesBtn` and nulls whichever matches. It's
installed on each button's `m_RemoveCallback` at creation time
(confirmed from disassembly at `0x001f37e4`).

**Why it matters:** when the user slices the Dojo back-bomb,
`MenuButton::Update` fires `FN_ClearMenuItems`, which releases every
sibling menu fruit — including MainScreen's Play/Dojo fruits that
persist through the Dojo trip. Their `MenuButton`s then enter the
`FadeCounter` shrink-disappear path and self-delete. Without the
`ButtonDeleted` callback nulling the parent pointer, the next
`STATE_CAMERA_ZOOM` sees a non-null (dangling) `pPlayButton` and
skips `CreatePlayDojo` via its `if (!pPlayButton)` guard — leaving
the user on a MainScreen with only the logo and Quit bomb visible.

Ported 1:1 in `MainScreen::ButtonDeleted` and bound to each button's
`m_RemoveCallback` via a small thunk lambda.

### 1.5 F7 debug time scale

`FN::g_DebugTimeScale` (float, default `1.0`) is multiplied into `dt`
in `Game::run` right after `SystemManager::Update(&dt)` writes the
fixed `1/60`. F7 key in the SDL event loop toggles between `1.0` and
`0.1` (10x slowdown). Draw still runs every real frame so animation
stays smooth at slow speed.

**Subtle point:** many port animations use *per-tick* lerp rates
(e.g. `alpha += (1-alpha) * 0.25` in DojoScreen) that ignore `dt`
entirely, so scaling `dt` alone doesn't slow them. At each per-tick
lerp site, multiply the rate by `FN::g_DebugTimeScale`. At 1.0x the
multiplier is 1.0 (no-op, binary-exact). At 0.1x the lerp advances
10x less per frame — smooth because it still updates every tick.

Applied so far: `DojoScreen::Update` state-0 alpha lerp. Extend to
other sites when slowdown reveals them.

### 1.6 Rename `debug/DebugHitbox` → `debug/DebugFlags`

File now holds both `g_DebugHitboxes` (F1) and `g_DebugTimeScale`
(F7), so the name needed to broaden. Kept all other naming the same.
Updated includes in `Game.cpp`, `DojoScreen.cpp`, `GameInit.cpp`,
`debug/CMakeLists.txt`. Header guard is `FN_DEBUG_FLAGS_H`.

### 1.7 Menu-bomb rotation lock (binary `Bomb::SetCallback @ 0x0017121c`)

Binary writes four fields to any bomb attached to a `MenuButton`:

    +0x70 = 2           (vel, one axis spins slowly)
    +0x72 = 0           (vel, other axis LOCKED)
    +0x74 = 0           (angle)
    +0x76 = 0x2d        (angle = 45)

This overwrites the `1..7 random` velocities `Bomb::Init` wrote, so
menu bombs barely rotate instead of tumbling. The port now applies
these after `Bomb::Init` in `MenuButton::Init`'s bomb branch.

**Field naming caveat.** Our `FN01_ApplyStructs.java` labeling has
`m_RotVelX` / `m_RotVelY` / `m_RotX` / `m_RotY` at +0x70/+0x72/+0x74/
+0x76, but in `Bomb::Draw` the "m_RotX" field actually feeds the
RotY matrix step, and "m_RotY" feeds the RotZ matrix step — so the
name-to-axis mapping is reversed. Raw binary writes are `+0x70 = 2`
and `+0x72 = 0`; to make the visual match what the user sees on
device, the port's `MenuButton::Init` assigns the locked value
(`0`) to `m_RotVelX` and the spinning value (`2`) to `m_RotVelY` —
which diverges from the raw offsets but produces the correct visual.
A follow-up to rename the struct fields in `FN01` + `Bomb.h` to
match what-drives-what would remove this confusion.

### 1.8 Menu bomb z-layer override (binary `0x0014f144`)

Binary `MenuButton::Init` bomb branch writes `float 150.0f` to
`bomb+0x6c` (`m_ZPosition`) — confirmed from the `vstr.32` at
`0x0014f144`. Overrides the depth allocator value
`Bomb::Init` set via `GetBombZPosition()` (which cycles `-10..-400`),
so menu bombs share the same `+150` layer as menu fruits and render
in front of the ring via ActorManager's 3D depth-write pass.

Ported 1:1 in `MenuButton::Init` bomb branch: `bomb->m_ZPosition = FRUIT_ZPOS`.

### 1.9 Remove `tex_loader` module

`src/engine/asset/tex_loader.{h,cpp}` and the legacy forwarding
`src/engine/tex_loader.{h,cpp}` deleted. `Renderer::upload_texture`
and the `TexImage` forward were dead code; removed. `Texture::Load`
reverted to the native GPU RGB565 / RGBA4444 / etc upload via
`UploadNative` using `GL_UNSIGNED_SHORT_5_6_5` etc. `CMakeLists.txt`
entry removed.

### 1.10 Bomb fuse particle offset (port-specific)

Binary `Bomb::Update` sets `emitter->m_Pos = bomb.pos` **once** at
creation (`0x00172f12`) and never updates it. No fuse-tip offset
vector anywhere in the binary. The fuse-tip visual in the original
is explained by particle drift over lifetime combined with the mesh
vertex layout.

The port applies a port-specific emitter offset each frame: start
with local `(0, 0, 1)` (mesh's fuse axis, verified via bone bounds
`z ∈ [-47.4, +78.3]`), apply the same `Scale × RotX(-83°) × RotY ×
RotZ` transform chain as `Bomb::Draw`, scale by `FUSE_LOCAL_Z *
scale.x`, add to `bomb.pos`. Because `RotZ` leaves the Z axis
invariant, the fuse stays static while the bomb rolls around it.

Sign convention note: the port's inline RotY in `Bomb::Draw`
modifies `col0 = cos*col0 + sin*col2` — for a vertex transformed by
`mat * v`, this is equivalent to `new_vx = vx*cos − vz*sin,
new_vz = vx*sin + vz*cos` (opposite sign to a standard right-handed
RotY; matches the binary `_Matrix44::RotY44`). My first port-side
transform had the signs flipped, mirroring the fuse to the wrong
side; fixed.

### 1.11 Particle `useDepth` parser verified (RE agent)

Background RE agent investigated the port's XML parser bug where
`useDepth` is read as an *attribute* but the atlas XML uses
`<drawOrder>` *child element*. Finding: the binary reads `useDepth`
as an attribute too — and `<drawOrder>` child elements are
*irrelevant* to the particle layer system, used only by an unrelated
`EffectImage::Parse` @ `0x0011dda4`. The `bomb_smoke` / `smoke` /
`sparks` templates have NO `useDepth` attribute, so both binary and
port default to `0` → `pm.Draw(0)` pass. **Port parser is correct.**

### 1.12 Particle pass order + depth state (RE agent)

Full ordered sequence inside binary `GameDraw @ 0x0016b888`:

| Step | Call | Depth Test | Depth Write |
|------|------|------------|-------------|
| 1 | `SetDepthBuffer(1)` | ON | — |
| 2 | `SetDepthBufferWrite(0)` | ON | OFF |
| 3 | `HUD::Draw(0x40)` | ON | OFF |
| 4 | SplatEntity, Shadows, PreDraw, BombBlast, BombFlash | ON | OFF |
| 5 | `HUD::Draw(0x80)` | ON | OFF |
| 6 | `pm.Draw(-1)` | ON | OFF |
| 7 | **`SetDepthBuffer(0)`** | **OFF** | OFF |
| 8 | Slash[0..15]->Draw() | OFF | OFF |
| 9 | `pm.Draw(0)` | OFF | OFF |
| 10 | SetGlobalAmbience, DrawSlices | OFF | OFF |
| 11 | `HUD::Draw(0x01)` | OFF | OFF |
| 12 | `pm.Draw(1)` | OFF | OFF |
| 13 | HUD pos reset, WaveManager::Draw(0) | OFF | OFF |
| 14 | `HUD::Draw(0x08)` | OFF | OFF |
| 15 | MainScreen::DrawPostEffects, DrawCritHit, `HUD::Draw(0x100)`, DrawBombHit, `HUD::Draw(0x200)` | OFF | OFF |
| 16 | restore HUD, DrawStartFade, `HUD::Draw(0x400)` | OFF | OFF |

Key finding: **no `pm.Draw` call after `HUD::Draw(0x08)`**. All three
particle passes precede it. `SetDepthBuffer(0)` fires before the
slash loop and stays off for the rest of the frame, so everything
after step 7 is painter-order.

Implication for MainScreen Quit bomb particles appearing behind the
ring: this is actually the **binary's behavior too**. MainScreen
Quit ring is in `HUD::Draw(0x08)` (step 14), after `pm.Draw(0)`
(step 9), so the ring draws on top of the fuse particles in both the
binary and port. DojoScreen back bomb has its ring on layer `0x40`
(step 3) which is BEFORE particles — so its particles draw on top.
Asymmetry is by layer, not a port bug.

---

## 2. Unresolved: bomb red-coloured render

Investigation in progress, **not fixed**. The bomb entity (and
possibly all entities per one user report) renders with the wrong
atlas region / colouring.

### What we checked

- **Atlas file is fine.** `fruit_atlas.tex` is 512×512 format `0x11`
  (RGB565), `TexFmtToGL @ 0x00189f78` confirms format byte 0x11 maps
  to `GL_RGB / GL_UNSIGNED_SHORT_5_6_5`. Port uses the same upload
  path via `Texture::UploadNative`.
- **Bomb mesh has one geometry, one material** (`fruit_atlas`),
  `diff = amb = (1,1,1)`, `selfIllum = (0,0,0)`, `isLit = 0`. Not a
  material tint issue.
- **Bomb mesh bone bind pose translation is zero** — verified via
  `Skeleton::Swap` dump. The vertex shader doesn't shift the mesh
  via bone bindings.
- **Bomb mesh bounds:** `x = [-48.7, +48.7]`, `y = [-48.7, +48.7]`,
  `z = [-47.4, +78.3]`. The +Z asymmetry is the fuse protrusion.
- **Bomb mesh UV range (verified from vertex stream parsing):**
  `u = [0.008..0.247], v = [0.503..0.749]` → atlas pixels
  `(4..126, 257..383)` — this is a 123×127 tile in the left-middle
  of the atlas. Dumping that exact region via an inline RGB565
  decoder produces an image matching the user's reference PNG: a
  dark bomb body with a red X cross. **So the UVs ARE correct.**
- **Vertex color at v0:** bytes `8b 9e b5 ff` = bluish-gray. Not red.

### What's odd

With the shader reduced to `gl_FragColor = vec4(c.rgb, ...)` (no
vertex-color multiply), the bomb renders **pure white**, not the
dark-with-red-X atlas tile we know the UVs point at. Pure white
from `texture2D(u_tex, v_uv).rgb` can only happen if:

1. The sampled texel actually IS white — contradicted by the PPM
   dump of the same UVs.
2. No texture is bound — but GLES2 default for an unbound sampler
   is black, not white.
3. The bomb isn't actually being drawn and we're seeing the
   background or another pass — possible.

### User's key objection

"The fruit and the bomb are sharing the same atlas. There is no way
that the fruit draws right while the bomb doesn't." — i.e. if atlas
upload + shader path works for fruits, the bomb using the same path
can't independently be broken. The red bomb must either be:

- fruits also subtly wrong (user previously said "all entities are
  not having right texture")
- bomb using a different code path I haven't found
- "pure white bomb" was actually the background showing through an
  undrawn bomb

### Suggested next steps

1. **Screenshot of current render.** Visually confirm whether fruits
   ARE actually rendering with correct colours, or whether they're
   also subtly off. The user suspects all entities are wrong.
2. **Diff `Bomb::Draw` against `Fruit::Draw` line by line** to find
   any state setup that differs.
3. **Log the bound texture ID** inside `DrawGeometry` for the bomb
   vs fruit calls — confirm both are sampling the same GL texture
   handle.
4. **Read back one sampled pixel** via `glReadPixels` after drawing
   the bomb to see what colour the GPU actually produces at a known
   UV coordinate.

### Vertex layout divergence noted but not acted on

Binary `LoadVertexStreamPSP @ 0x001a7b0c` extracts a **weightFmt**
at bits 2-4 of the vertDecl (our port's parser ignores those bits
entirely). For `decl = 0x120001ff`, bits 2-4 = `111` = 7,
`FormatSize(7) = 4`. Binary `Stride()` formula:

    (normalFmt + colorFmt + field[13] + field[12]) * 3
    + posFmt * (field[7] + 1)
    + texFmt * 2
    + weightFmt

Both port (assumed layout: `tex(8) + col(4) + norm(12) + pos(12)`)
and binary stride compute to **36 bytes** for bomb.mmd — but the
field-by-field layouts differ:

- Port: tex(8) + col(4) + norm(12) + pos(12)
- Binary (from `GenerateElementListing @ 0x001a7718`): tex(8) +
  weight(4) + color(12, 3 floats) + normal(12, 3 floats). Position
  has a 0-byte contribution in this declaration — not in the stream.

If the port's layout is wrong, color/normal/pos bytes are being
mis-read for bomb.mmd. Tex (at offset 0) is still correct. This
**might** explain the bomb rendering mystery if fruits happen to
have a different vertDecl where the port's layout accidentally works.
Needs verification: dump vertDecl values for a fruit mesh and
compare.

---

## 3. File/state notes

Open/uncommitted code changes at end of session (not yet committed):

- `src/entities/Bomb.cpp` — bomb fuse-tip emitter offset via
  transform chain (section 1.10)
- `src/hud/MenuButton.cpp` — `bomb->m_ZPosition = FRUIT_ZPOS`
  override (section 1.8)
- `src/engine/asset/Texture.cpp` — reverted to native RGB565 upload
  (section 1.9)
- `src/engine/CMakeLists.txt` — removed `asset/tex_loader.cpp`
- `src/engine/render/Renderer.{h,cpp}` — removed dead
  `upload_texture` + `TexImage` forward
- Deleted: `src/engine/asset/tex_loader.{h,cpp}`,
  `src/engine/tex_loader.{h,cpp}`
- `src/entities/SlashEntity.cpp` — doc-only annotations (not mine,
  do not commit as this change)

Committed earlier this session:

- `954a78b` GameModeScreen port + sound call-site wiring
- `4218b28` `MainScreen::ButtonDeleted` null dangling button refs
- `dfe2380` F7 debug time scale
- `6663c8c` DojoScreen::PlayCallback: SFX + fling
- `54a9ad4` Rename debug/DebugHitbox → debug/DebugFlags
- `97f58bd` MenuButton: port `Bomb::SetCallback` rotation overrides
