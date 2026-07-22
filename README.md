# Fruit Ninja — Reverse-Engineered Port

A faithful, from-scratch reimplementation of **Fruit Ninja v1.6.1** (the Samsung
Bada release, built on Halfbrick's *Mortar* engine) in portable C++11, running on
modern desktops, the web, the Nintendo Wii, and LG webOS TVs.

The goal is **fidelity**: match the original's gameplay, physics, scoring, timing,
and visual behaviour as closely as possible, reconstructed by reverse-engineering
the original ARM binary. The RE record lives in the source itself — as
`// ASM-verified:`, `// TODO:`, and `// DIFFERS:` comments next to the code they
describe — not in separate design docs.

> [!IMPORTANT]
> **Unofficial fan project — for preservation and educational purposes.**
> This project is **not affiliated with, authorized, or endorsed by Halfbrick
> Studios**. *Fruit Ninja*, its artwork, audio, models, and the name/logo are
> © Halfbrick Studios and remain their property. **No game assets are included in
> this repository.** To build and run, you must provide your own copy of the
> original game's data — the build fetches/reads it from a local dump; it is never
> committed or redistributed here. This repo contains only original
> reverse-engineered source code and tooling.

## Platforms

| Target | Backend | Package | Build entry |
|--------|---------|---------|-------------|
| Desktop (Windows/Linux) | SDL2 + desktop OpenGL (ES2 shader path) | executable | CMake presets (`cmake --preset host`) |
| Web | Emscripten / WebAssembly + WebGL (ES2) | static site | `tools/web/` |
| Nintendo Wii | devkitPPC / libogc / **GX** (no SDL) | Homebrew `.zip` | `tools/wii/build.sh` |
| LG webOS TV | SDL2 + GLES2 (openlgtv buildroot NDK) | `.ipk` | `tools/webos/build.sh` |

The renderer is a hand-written **OpenGL ES 2.0** shader pipeline (2D quads, 3D
meshes, fonts, particles) — the original fixed-function ES1 path was fully
migrated; the Wii target uses native GX.

## Building

All targets are CMake-based and need the original game data present locally (see
the disclaimer above — assets are **not** shipped). Each backend's directory
README has the details:

- **Desktop:** configure with the committed CMake presets (`CMakePresets.json`);
  vcpkg supplies SDL2 / SDL2_image / FreeType / tinyxml2.
- **Web:** `tools/web/` — native Emscripten SDK; `tools/web/README.md`.
- **Wii:** `tools/wii/build.sh` — devkitPPC/libogc; produces `fruit-ninja-wii.zip`.
- **webOS:** `tools/webos/build.sh` — the openlgtv buildroot NDK (Linux/WSL);
  produces a `.ipk`, verified with `webosbrew-ipk-verify`.

## Asset gallery

The web deploy publishes a browsable **asset gallery** at `/gallery` — textures
(streamed at runtime as byte-range slices straight out of the game's WebAssembly
`.data` bundle, so nothing is duplicated) and the extracted 3D models. It is
generated from the local game dump at build time (`tools/web/build-gallery.sh`);
like everything else, no art is committed.

## Documentation

- `docs/HANDOVER.md` — contributor onboarding (start here).
- `docs/port-plan.md` — the port's intent and scope.
- `docs/engine/` — the load-bearing reference set: file formats, static-init
  order, coordinate convention, the intentionally-skipped online-services list,
  and toolchain/ABI provenance.
- Per-directory `README.md`s document each tool/pipeline.

## Reverse-engineering & toolchain

The original is an ARM32 little-endian Bada ELF (Halfbrick Mortar engine, built
with Sourcery G++ 4.4.1). An asm-verification pipeline (`tools/asm-verify/`)
cross-compiles the port with the original toolchain and diffs it against the
binary to keep the reimplementation faithful. Ghidra scripts used during RE live
in `tools/ghidra/`.

## License

The reverse-engineered source code and tooling in this repository are released
under the [MIT License](LICENSE) — © 2026 Mariotaku.

Original *Fruit Ninja* game content, assets, and trademarks are © Halfbrick
Studios, are **not** covered by the MIT License, and are not included or
distributed here. See [NOTICE](NOTICE).

## Credits

Reverse-engineering and port by Mariotaku. *Fruit Ninja* is a trademark of
Halfbrick Studios.
