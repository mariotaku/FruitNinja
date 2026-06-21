# asm-verify/layout/

Binary class-layout / RTTI **reference** generators — **stage 10** of [`docs/re-pipeline.md`](../../../docs/re-pipeline.md). Outputs (JSON) land in `tmp/`; these are reviewed references for struct-size asserts, not auto-gates (relates to tasks #76/#77).

- **`infer-class-sizes.py`** — binary class sizes from `operator new` call sites (ground truth, via capstone over BinExport2) → `tmp/binary-class-sizes.json`.
- **`layout-reference.py`** — join binary sizes with the port's `static_assert(sizeof)` + inheritance; flag mismatches and attribute base-class deltas as "cascade". Surfaces assert candidates / NO-ASSERT gaps.
- **`extract-typeinfo.py`** — Itanium RTTI (`_ZTI*`) class→bases tree → `tmp/typeinfo-tree.json` (LIEF only, no Ghidra).
