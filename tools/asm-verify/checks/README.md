# asm-verify/checks/

Standalone auxiliary checks — run **manually**, not part of `run.sh`. Each catches a bug class the per-symbol asm diff misses.

- **`check-signatures.py`** — function signature mismatches binary-vs-port (symbols from the binary ELF + the cross-build manifest).
- **`check-sources-drift.py`** — drift in `verify-sources.cmake` (STALE listed-but-missing files = error; UNLISTED portable `.cpp` = info).
- **`compare-globals.py`** — diff `STT_OBJECT` DATA symbols binary-vs-port (PORT-ONLY / SIZE-MISMATCH / BINARY-ONLY); the function-level diff can't see data.
- **`eval-capstone-diff.py`** — semantic ARM/Thumb-normalized LCS assembly diff (reg-alloc, encoding, condition codes, immediates normalized away).
