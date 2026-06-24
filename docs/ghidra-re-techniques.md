# Ghidra RE script techniques (FruitNinja.exe)

Reusable Ghidra-scripting techniques salvaged from months of one-off RE scratch
scripts. The scripts themselves are **user-local, not in-repo** (Ghidra's script
dir `~/ghidra_scripts`); this doc preserves the *techniques* so they survive the
scratch-script churn. The canonical RE record is `src/` source comments — this is
a tooling index, not a findings store.

Run scripts via `run_script_inline` (Java GhidraScript). If inline scripts ever
fail with an OSGi `Unresolved requirements` error, a broken `.java` slipped into
`~/ghidra_scripts` and is breaking the whole source-bundle compile — quarantine it.
BinExport is intentionally NOT installed in Ghidra; use `binexport-cli` instead.

## Status after the 2026-06 cleanup
- **19 reusable scripts** kept live in `~/ghidra_scripts` (named below).
- **5 finding-dumps** parked in `~/ghidra_scripts_archive` (verify-before-reuse; see bottom).
- 89 spent one-shot probes deleted.

---

## Reusable techniques

### RTTI / vtable / class-hierarchy recovery ⭐
- **`recover_rtti.py` + `RecoverRtti.java`** — full GCC Itanium-ABI RTTI walker.
  Anchors on `_ZTVN10__cxxabiv1{17__class,20__si_class,21__vmi_class}_type_infoE`
  (base and base+8 vptr), classifies every `_ZTI*`, reads parents from si/vmi
  layout, walks each `_ZTV*` (skip offset-to-top + typeinfo slots, then fptrs until
  non-exec/0, **strip Thumb LSB `& ~1`**), writes plate comments + `<prog>_rtti.json`.
  The single most reusable script.
- **`FN06_LabelVtables.java`** — labels `_ZTV/_ZTI/_ZTS` → `Class::vtable/typeinfo`;
  carries a hand-rolled `N<len><comp>...E` nested-name demangler (useful when
  Ghidra's GNU demangler is unavailable).
- **`FN07_ListUnnamedVfuncs.java`** — per-class report of `FUN_*`/`thunk_*` vtable
  slots with param count + CC + pure-virtual detection. "What's still unnamed" triage.

### Bulk return-type / prototype application (the *180k* pipeline)
4-stage assembly line for undefined-return funcs in an address range:
1. **`FNListUndefRet180k.java`** — list non-thunk/non-std funcs in `[lo,hi)` with
   `undefined*` return → TSV.
2. **`FNDecompFirstLine180k.java`** — batch-decompile, dump first ~800 chars (feed a classifier).
3. external classifier → `addr<TAB>proto` TSV.
4. **`FNApplyProtos180kV3.java`** — apply. V3 resolves types via
   `DataTypeManager.findDataTypes()` (skips `/Demangler` dupes) + primitive map +
   trailing-`*` pointer parsing. (V1/V2 used the fragile `CParser` — deleted.)
- **`FN_CtorDtorReturnVoidPass.java`** — ranged ctor/dtor → `void` (Itanium ABI;
  detect ctor by de-templated name == namespace, dtor by `~`, skip std/__gnu_cxx/__cxxabiv1).
- **`ApplyReturnTypes_180_1b9_pass2.java`** — TSV-driven typed applier (richer kind
  set incl. `TiXmlNode*`/`TiXmlAttribute*`).

### Calling-convention / "is this really a method" audits
- **`FindNonThiscall.java`** — class-namespaced methods whose CC ≠ `__thiscall`.
- **`FindStaticMislabels.java`** — decompile every `__thiscall`, flag zero-`this`-use
  functions (static-mislabel candidates).

### Field-offset scanners
Find every `str`/`ldr`/`vstr`/`vldr` to `[reg,#0xNNN]`. Prefer the **scalar-operand**
match (`getOpObjects()` + `Scalar.getUnsignedValue()`) over `insn.toString()`
substring matching — no false hits.
- **`FindOff604.java`** — canonical single-offset scalar-operand scanner template.
- **`FindFruitInfoOffsets.java`** — immediate-**range** variant (scans `[0x270..0x330]`).

### GOT-relative DAT resolution (PIC globals) ⭐
Mortar/Bada is `-fPIC`: globals reached as `GOT_base + DAT_offset`, where `GOT_base`
= `*(literal) + literal_pc_anchor`:
```
gotBase = mem.getInt(literalAddr) + pcAnchorConst
target  = gotBase + mem.getInt(datSlot)
ptr     = mem.getInt(target); value = read(ptr)
```
- **`ResolveGotStrings.java`** — string variant.
- **`ResolveDats5.java`** — mixed string + Vec3 + float variant.

### Float / literal-pool constant decoding
- **`DecodeFloats_DojoAbout.java`** — read code addrs, `Float.intBitsToFloat` on 4 LE
  bytes. Minimal decoder.
- **`FN_ReadDojoDrawFloats.java`** — resolves Thumb2 `vldr` PC-relative addressing
  (PC align-4 + offset) to locate the literal first. Decodes floats *referenced by*
  a function, not a hand-listed set.

### Misc utilities
- **`DumpCompilerInfo.java`** — dump `.comment`/`.ARM.attributes`/notes (toolchain
  provenance; baked into `docs/engine/binary-build-evidence.md`).
- **`CountArmFuncs.java`** — ARM-vs-Thumb tally bucketed by `Symbol.getSource()`.

---

## Parked finding-dumps (archive — verify before reuse)
In `~/ghidra_scripts_archive`; their output mostly lives in `src/` already, kept only
because a few values may not be fully baked in. **Re-verify against `src/` first** —
some predate known struct drift.
- **`FN08_UpdateStructs.java`** — richest dump: `FRUIT_INFO` (0x330) full field map,
  `BadaSound` (0x878), all 5 power-up modifier structs.
- **`CreateEngineStructs.java`** — `SystemManager` (0xD4) / `ActorManager` (0x106C);
  may be stale (cf. GameWork 0x608→0x6a4 drift).
- **`FixDelegateSizes.java`** — `Delegate` = 36 bytes.
- **`DumpBombBounds.java`** — `Bomb::Update@0x1729fc` OOB bound floats.
- **`FN_FixStdContainerSizes.java`** — container-size resizer. **Embedded size table is
  partly STALE** (`std::vector=12`); current policy is `std::list`=8 / `std::map`=24 —
  re-verify the table before running.
