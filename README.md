# Fruit Ninja — Reverse-Engineered Port

Fruit Ninja v1.6.1 for Samsung Bada, reverse-engineered from the ARM binary and
rewritten from scratch in C++11. The goal is a close to 100% faithful
recreation, with the minimum of optional additions to fit modern platforms and
form factors.

Unofficial fan project, not affiliated with Halfbrick. You need your own copy of
the game data to build or run it.

## Screenshots

| Main menu | Arcade mode |
|---|---|
| ![Main menu](docs/screenshots/main-menu.png) | ![Arcade mode, frenzy banner](docs/screenshots/arcade-frenzy.png) |

Widescreen (16:9):

| Main menu | Arcade mode |
|---|---|
| ![Main menu in widescreen](docs/screenshots/main-menu-wide.png) | ![Arcade mode in widescreen, freeze powerup and a critical combo](docs/screenshots/arcade-wide.png) |

## What's new

- **Widescreen (16:9).** The original is a fixed 3:2 480x320. Opt in from
  Settings; applies on restart.
- **Motion mode.** Point to aim, flick to cut, instead of dragging a finger.
  On by default, and what makes an LG Magic Remote work.
- **Native refresh rate.** The sim keeps the original's fixed 60 Hz tick and
  frames interpolate on top, so a 120 Hz screen gets 120 fps. Desktop and web
  only -- the webOS UI layer does not appear to draw past 60.
- **A settings screen.** v1.6.1 has no options UI at all. This one saves your
  language, input and display choices.
- **webOS TV, Wii, and PS3 (scaffold).**

Smaller things: mouse-wheel scrolling, ESC/Back as a back key, F12 screenshots,
optional HD textures, and an offline-capable PWA build for the web.

## How to build

Everything is CMake.

| Target | Backend | Output | Build with |
|---|---|---|---|
| Desktop (Windows/Linux) | SDL2 + GL, ES2 shader path | executable | `cmake --preset host` |
| Web | Emscripten + WebGL | static site | `tools/web/build.sh` |
| LG webOS TV | SDL2 + GLES2 | `.ipk` | `tools/webos/build.sh` |
| Nintendo Wii | devkitPPC + libogc, native GX | homebrew `.zip` | `tools/wii/build.sh` |
| PlayStation 3 | PSL1GHT (scaffold) | `.elf` / `.pkg` | `tools/ps3/build.sh` |

Each target has its own README with the setup details. CI builds the webOS
`.ipk` and the Wii zip on every push, and attaches both to a GitHub release when
one is published.

Wii is playable but still rough; see `src/platform/wii/README.md`.

### PlayStation 3 (scaffold)

Opt-in only (`FRUIT_PLATFORM_PS3`). Needs [ps3toolchain](https://github.com/ps3dev/ps3toolchain) + [PSL1GHT](https://github.com/ps3dev/PSL1GHT). Details: `src/platform/ps3/README.md` and `src/platform/ps3/ENABLE.md`.

```bash
export PS3DEV=/usr/local/ps3dev
export PSL1GHT=$PS3DEV/psl1ght
export PATH=$PS3DEV/bin:$PS3DEV/ppu/bin:$PATH

# If CMakeLists is missing PS3 wiring on this branch:
bash tools/ps3/apply-cmake-ps3.sh

bash tools/ps3/build.sh          # ELF
bash tools/ps3/build.sh pkg      # TestBuildFruitNinja1.0.pkg
bash tools/ps3/package-assets.sh # FruitNinjaNECESARRYAssets.PKG (your Data/)
bash tools/ps3/package-dlc.sh skittles   # optional DLC packs
```

**Input (DualShock 3):** left stick = blade, Cross (X) = cut, right stick = UI.

**Status:** platform scaffolding + pad translator + packaging scripts. RSX
display / full game loop still TODO. Does not change host/web/webOS/Wii builds
when the option is OFF.

## How to develop

The RE record lives in the source, not in design docs. Every function carries
what is known about it as a comment: `// ASM-verified:` for anything checked
instruction-by-instruction against the binary, `// TODO: v1.6.1 0x...` for a gap
with the address to go read, `// DIFFERS:` for a deliberate deviation and why,
`// Defunct:` for dead online services kept as stubs. Grep for them.

`tools/asm-verify/` keeps that honest. It cross-compiles the port with the
binary's own toolchain (GCC 4.4.1) and diffs the ARM output against
`FruitNinja.exe` function by function, then ranks what looks like a real bug.
`bash tools/asm-verify/run.sh`, see `tools/asm-verify/README.md`.

Tests are `ctest`; headless GL setup is in `tests/README.md`.
`tools/README.md` indexes the rest.

## License

[MIT](LICENSE) covers only the code and tooling written for this project.
"Fruit Ninja", its name and logo, and all original game assets belong to
Halfbrick Studios.
