# Session Handover (2026-04-28)

Comprehensive state of the project as of context-window save. Primary goal in flight: **base-class binary fidelity (Entity / HUDControl3d / ScrollingMenuItem)** -- foundational layouts that ripple into every leaf class already verified.

## Repo state

- **Branch**: `main` (single active branch). Obsolete `debug/equip-create-trace` exists but its work is already squashed-merged into main; delete with `git branch -D debug/equip-create-trace` when ready.
- **Stash**: `stash@{0}` from an earlier visual-test rollback. Likely stale -- inspect with `git stash show -p stash@{0}` and drop if not needed.
- **Build**: clean. Both MSYS2/MinGW and MSVC (VS Build Tools 2022) configurations work via the single `build/` dir.
  - MSYS2 configure: `cmake -G "MSYS Makefiles" -B build`
  - MSVC configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug` (in a Developer Cmd Prompt or after `vcvars64.bat`)
  - Build (any toolchain): `cmake --build build`
  - Run: `./build/fruit-ninja.exe` (MSYS2) or `./build/Debug/fruit-ninja.exe` (MSVC -- SDL2.dll auto-copied next to the exe via post-build hook).

## Recent commits (newest first, last ~10)

```
f26c91c CLAUDE.md: default analyser agents to background dispatch
cd8222f CLAUDE.md: prefer subdividing + parallel agent dispatches
7068d70 Fruit / Bomb / MenuButton: ASM-verified fixes from asm-inspector pass
1507af9 agents: add ASM-verified marker rule to asm-inspector + implementer
245cf8b Build: prebuilt SDL2 zip for MSVC + DLL-copy parity, fix SDL include
8b00049 docs: slim CLAUDE.md, move RE/implementation rules to sub-agents
dc8be11 GameTaskState: clarify port-only label, document binary origin
e5fa507 Mortar::Model: binary-faithful Skeleton::Swap + Draw per-mesh sort
71882b0 agents: add asm-inspector + redirect Ghidra scratch scripts to tmp/
f721ed0 docs: consolidate galleries under docs/gallery/, restore Y<->Z axis fix
```

## TOP-PRIORITY OPEN WORK

The latest asm-inspector pass (`a66c1359bd700da89` on 2026-04-28) verified base classes and surfaced **3 of 5 priority targets diverged**. These are the highest-impact items. Fix order matters because every leaf class inherits these layouts.

### 1. Entity base (`src/entities/Entity.{h,cpp}`) -- HIGH

Three divergences:

**(a) Vtable slot order is wrong.** Binary `Entity::*` vtable order (verified from `Bomb__vt @ 0x001ea478`):

```
slot 0  dtor1
slot 1  dtor2                    (deleting + non-deleting; C++ Itanium-ABI)
slot 2  Init                     binary @ Bomb 0x172504
slot 3  Release                  binary @ Bomb 0x171764  -- MISSING IN PORT
slot 4  Update                   binary @ Bomb 0x1729fc
slot 5  Draw                     binary @ Bomb 0x171be8
slot 6  PostUpdate               binary @ Bomb 0x1714e4 (`DrawUpdate`)
slot 7  PostLoad                 binary @ Entity 0x19d600 (no-op)  -- MISSING IN PORT
slot 8  InRect                   binary @ Entity 0x19d800 (col-sphere update)  -- MISSING IN PORT
slot 9  CollisionResponse        binary @ Bomb 0x17280c (port calls `OnSliced`)
```

Port currently has `Update` at slot 2 / `Draw` at slot 4 -- wrong slots. **`ActorManager::Update` walks vtable+0x10 (Update) and vtable+0x18 (PostUpdate) unconditionally**, so every actor type currently calls the wrong virtual. This historically caused the "bomb fuse-emitter-positioning bug that hid for weeks" (project memory: `feedback_preserve_functions.md`).

**(b) `m_Col` is a pointer, not inline.** Binary: `ColSphere* m_Col` at +0x38 (nullable, dereferenced by `Entity::InRect`). Port inlines `Mortar::ColSphere m_Col`, inflating Entity past 60 bytes. Subclasses (Bomb, Fruit) allocate or share a ColSphere and store its pointer at +0x38.

**(c) `Deactivate()` is NOT in the vtable.** Binary: `Deactivate` is a non-virtual helper -- `ActorManager::Deactivate (0x170184)` and the inline activate at `0x170b18` directly write `flags |= ENT_INACTIVE`. Port should drop `virtual void Deactivate()` from Entity. The deactivate-on-release path was already fixed in MenuButton (commit 7068d70); other call sites should also use direct flag manipulation.

**Fix sequence:**
1. Reorder Entity virtuals; add `Release`, `PostLoad`, `InRect` slots (PostLoad and InRect are likely no-ops or simple base bodies — verify via further RE if needed).
2. Change `m_Col` to `ColSphere*` and update Bomb/Fruit/SplatEntity ctor/dtor to allocate/free as needed (or share singletons).
3. Drop the `Deactivate()` virtual; convert all callers to direct `flags |= ENT_INACTIVE`.

Earn the marker `// ASM-verified: <new-timestamp> binary @ 0x0019d88c (asm-inspector)` (Entity ctor) and `... 0x00170b18 (asm-inspector)` (Activate) only after re-dispatching asm-inspector to confirm the new layout.

### 2. HUDControl3d base (`src/hud/HUDControl3d.{h,cpp}`) -- HIGH

Field offsets all wrong:

| Field | Binary offset | Port placement |
|-------|--------------|----------------|
| primary tex (`m_Texture`) | **+0x74** | port has it at +0x60 (wrong) |
| secondary tex (`m_SecondaryTex`) | **+0x78** | port has `field_0x78 int` (wrong type) |
| `Draw()` activity gate | byte at **+0x5f != 0** | port gates on `m_DrawColour.a == 0` (wrong field) |
| UV floats | **belong in HUDControl base, not HUDControl3d** | port has them in HUDControl3d |

Port draws the wrong texture (or no texture) because it reads from a non-texture slot; current visual works only because port code is internally consistent but diverged from binary-derived offsets.

**Fix sequence:**
1. Move `m_Texture` field to +0x74 in HUDControl3d.
2. Move `m_SecondaryTex` (replace `field_0x78 int`) to +0x78.
3. Replace the `m_DrawColour.a == 0` gate in `Draw()` with the byte-at-+0x5f gate (rename the port field that maps to +0x5f -- likely `m_bShouldDraw` or similar set to 1 by HUDControl ctor).
4. Move UV floats out of HUDControl3d into HUDControl base struct.

Marker after fix: `// ASM-verified: <ts> binary @ 0x001443f4 (asm-inspector)` for the ctor; rerun asm-inspector for `Draw()`.

### 3. ScrollingMenuItem base (`src/hud/ScrollingMenuItem.{h,cpp}`) -- MEDIUM

- **Delegate1 at +0x30 (port has it at +0x2C)** — off-by-4. Binary Delegate1 is 36 bytes (ends +0x53).
- **`m_field58` and `m_DescText[128]` belong in `ShopListItem`, not the base.** Total binary size of ScrollingMenuItem is 88 bytes (`+0x58`); port currently is ~220 bytes due to subclass-field leak.

Fix:
1. Move Delegate1 to +0x30 in the port struct.
2. Move `m_field58` and `m_DescText[128]` to ShopListItem.
3. Update offsetof-based static_asserts in `ShopListItem.h` accordingly (already gated on `__GLIBCXX__`).

Marker after fix: `// ASM-verified: <ts> binary @ 0x001e9f00 (asm-inspector)` (vtable) and `0x0015b5dc` (ctor).

## OTHER OPEN GAPS (LOWER PRIORITY)

- ~~**`Bomb::Init +0xa4` literal-pool value**~~ — RESOLVED 2026-04-28 by re-analyst pass. Pool `DAT_001726A8 = 0.0f`. Port's existing `m_Countdown = 0.0f` already matches the binary. The `may need 1.0` speculation was wrong (1.0 is loaded into s15 elsewhere for the radius default and +0xa8 speedMult, not +0xa4). See `docs/entities/bomb.md:30` (m_Countdown).
- **`Fruit::Update` sliced ramp special-case** — binary has 6.5x grav ramp instead of 4.5x when `m_bSpecial` field is set (`+0x10c`); port doesn't model the field. Defer until special-fruit category needs it.
- **`MenuButton::Init` field gaps** — binary writes `+0x10c = 1` (m_bSpecialGravity?), `+0x108 = MenuButton*` (slash-entity backref); port doesn't model these fields.
- **`Mesh::GetBounds`** — port returns raw bone-binding bounds without transforming through bone world matrices. Pre-existing approximation; correct for unanimated models, may have slightly off sort keys for animated.
- ~~**`BombHit.cpp:335` C4551 warning**~~ — RESOLVED 2026-04-28. The `(void)SplatEntity::RemoveAllSplats;` was an intentional symbol-unreference idiom, not a missing call. Binary calls it only inside `if (IsSameScreenMultiplayer())` (`ResetGameEntities @ 0x0016a058`). Port skips multiplayer so the call is correctly omitted; the symbol-reference replaced with a comment-only doc note.
- **`SetSelected` fruit-type strings** (`DAT_0015c970/c974`) — still stubbed in ShopScreen.cpp.
- **`GameTaskState`** — port models only 4 of 19 fields. Expand as more callers are ported (full layout in `docs/structs/game.md`).

## CONVENTIONS / RULES ADDED THIS SESSION

All in CLAUDE.md, `.claude/agents/*.md`. Don't restate, but be aware:

- **Subdivide multi-item asks** into multiple agent calls; **run independent items in parallel** (one message, multiple Agent tool uses).
- **Default analyser agents (`re-analyst`, `asm-inspector`) to background** (`run_in_background: true`). Foreground only when next step strictly needs the result.
- **`// ASM-verified:` markers**: paste only for Confirmed verdicts above the verified function/block. Format: `// ASM-verified: <ISO-8601 to-the-minute UTC> binary @ 0x<addr> (asm-inspector)`. Greppable: `grep -rn 'ASM-verified:' src/`.
- **Ghidra scratch scripts** go in `tmp/ghidra_scripts/`, NOT in the project's `ghidra_scripts/` (which used to hold persistent scripts but is now reserved-only). The project does not maintain reusable Ghidra scripts in version control.
- **CLAUDE.md is slim** (~70 lines). Detailed RE rules live in `re-analyst.md`; detailed implementation rules / coordinate system / fidelity policy live in `implementer.md`. Don't bloat CLAUDE.md with rules that belong to a single agent.

## RECOMMENDED NEXT STEPS (in order)

1. **Entity base fix** (vtable order + m_Col pointer + drop Deactivate virtual). Largest blast radius — every actor subclass touches Entity. Dispatch one implementer agent.
2. **Re-dispatch asm-inspector** on Entity to earn the marker. Background.
3. **HUDControl3d base fix** (texture offsets + Draw gate + UV move). Dispatch one implementer agent.
4. **Re-dispatch asm-inspector** on HUDControl3d to earn the marker.
5. **ScrollingMenuItem fix** (Delegate1 offset + subclass-field leak). Lower urgency; can wait.
6. After (1)-(4), every leaf class previously verified (Fruit/Bomb/MenuButton) should be **re-asm-inspected** to confirm their inherited layout still matches.

Optional / parallel:
- Resolve `Bomb::Init +0xa4` literal-pool value (one targeted re-analyst call).
- Investigate `BombHit.cpp:335` C4551 warning (one targeted re-analyst or asm-inspector call).

## EVIDENCE FILES (in `tmp/asm-compare/`)

ASM extracts saved per method during the verification passes. Useful as ground truth for re-comparison:

- `Entity_ctor_binary.s`, `Entity_activate_binary.s`, `Entity_vtable_binary.s`
- `HUDControl3d_ctor_binary.s`, `HUDControl3d_draw_binary.s`
- `ReferenceCounter_binary.s`
- `ScrollingMenuItem_ctor_binary.s`
- `BaseScreen_ctor_binary.s`
- `entity_activate_test.{cpp,s}` (Bada-toolchain compile unit + output)
- `fruit_init_binary.s`, `fruit_slice_critical_binary.s`, `fruit_slice_normal_binary.s`, `fruit_update_binary.s`
- `bomb_init_binary.s`, `bomb_setcallback_binary.s`
- `menubutton_update_binary.s`, `menubutton_init_binary.s`, `menubutton_release_binary.s`
- `model_swapskeleton_binary.s`, `model_updateboneLinks_binary.s`, `model_draw_binary.s`
- `fruit_slice_test.{cpp,s}` (older worked example for the spin-loop fix)

## KEY DOC POINTERS

- `docs/README.md` — documentation index
- `docs/TODO.md` — pre-existing TODO list (separate from this handover)
- `docs/structs/game.md` — GameTaskState full 19-field layout
- `docs/screens/shop-buttons.md` — recently-written shop equip-button RE
- `docs/gallery/textures/index.html`, `docs/gallery/models/index.html` — visual asset browsers
- `tools/tex2png.cpp`, `tools/convert_textures.cpp` — texture conversion utilities

## SESSION CONTEXT FOR HANDOVER

Recent decisions / debugging arcs in this session that the next session should know:
- Bisected and root-caused multiple shop-equip-button regressions, all traced to MSVC/MSYS2 layout differences and `Deactivate()` setting `ENT_INACTIVE` freezing fruits mid-flight (now fixed in commit 7068d70).
- Validated the asm-inspector workflow on Mortar::Model, Fruit, Bomb, MenuButton, and base classes; the pattern works.
- Established that the binary's compiler is **Samsung Sourcery G++ 4.4.1** (close to but not identical to the SDK's 4.5.3) with **hard-float ABI** (`Tag_ABI_VFP_args: VFP registers`). Asm-inspector compile flags pinned at `-O2 -mthumb -mcpu=cortex-a8 -mfpu=vfpv3 -mfloat-abi=hard`.
- The user prefers actionable error reports over speculative fixes — when something looks wrong, RE the binary first and apply changes only after evidence.
