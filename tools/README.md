# tools/

Build, asset, and binary-fidelity tooling for the Fruit Ninja port.

- **`assets/`** — `.tex`/`.mad` asset conversion to PNG/etc. (CLI + standalone C++ converters).
- **`web/`** — Emscripten web build assembly, content-hash asset naming, and a LAN dev server for GitHub Pages.
- **`asm-verify/`** — binary-fidelity verification pipeline (cross-build the port with the binary's toolchain, diff operand-by-operand, rank real bugs). Entry: `bash tools/asm-verify/run.sh [--class Foo]`.
- **`symbol-diff/`** — binary-vs-port symbol coverage diff + un-wrap plan utilities. Entry: `/symbol-diff` skill.
- **`ghidra/`** — committed Ghidra scripts for recurring RE tasks (annotated disasm, undef-return sweep, std-container size fixer, GOT-string resolver). Run via GhidraMCP.

For the RE + port verification pipeline, **start at [`asm-verify/README.md`](asm-verify/README.md#pipeline)** — the canonical end-to-end stage map. Each subdir has its own README index.
