# Port Intent

**Goal:** Faithful port of FruitNinja.exe v1.6.1 (Bada OS, Halfbrick Mortar engine) to modern platforms.

**Fidelity first** — match the original game's gameplay mechanics, physics, scoring, timing, and visual behavior as closely as possible.

## Scope

- **Preserve all gameplay** — fruit/bomb spawn, slicing collision, scoring, combos, power-ups, waves, all game modes.
- **Preserve simultaneous multi-finger slicing** — per-finger blades (up to 8 fingers) — this is the binary's "multiplayer" model, not same-screen split-screen.
- **Port all UI screens and widgets** — main menu, dojo, shop, game-over, pause, settings, achievements, leaderboards, etc.
- **Stub defunct features, never skip** — OpenFeint, GameCenter, P2P multiplayer, online news, online leaderboards, etc. have call shapes and vtable layouts preserved but method bodies are no-ops that return safe defaults. This keeps the call graph identical to the binary and isolates the "dead feature" decision to one place.
- **Defunct UI is still drawn when the binary draws it** — if v1.6.1 visibly renders it on screen, the port renders it too (as a visible stub). Unreferenced dead code is not instantiated.

## Platforms

- **Host** (Windows/Linux/macOS): SDL2 + OpenGL ES 2.0
- **Web** (Emscripten): WebGL ES 2.0 / wasm32
- **Wii**: devkitPPC + libogc + GX (native graphics)
- **webOS**: buildroot NDK + GLES2

## Non-Goals

- Multiplayer revival (P2P transport layer is defunct; transport seam not isolated yet)
- Online features (intentionally dead)
- Native Bada OS build (forward-port only)
