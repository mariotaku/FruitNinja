#!/usr/bin/env python3
"""operand_resolve: recover CALL-target and DATA-symbol identity for asm-verify.

asm-verify's `_norm_instr` keeps immediates / struct displacements / condition
codes strict, but throws away three things:

    bl|blx  <target>        -> CALL          (call TARGET discarded)
    <sym> / .LANCHOR / .L   -> <SYM>         (symbol IDENTITY discarded)
    [pc, #0x1c]             -> [pc, #POOL]   (pool SLOT masked -- correct)

The first two are structural blind spots: a port that calls libc `rand()` where
the binary calls `Math::Random::Rand32` on `Math::g_random`, or that references
the wrong `_Vector3<float>::UnitX/Y/Z`, normalizes to a byte-identical
instruction stream. This module resolves those operands back to names.

THE RESOLUTION RULE
-------------------
  * operand resolves to a RELOCATED slot, or to writable .data / .bss
        -> compare by SYMBOL NAME
  * operand resolves to NON-relocated read-only bytes
        -> compare by VALUE (bit pattern)

Relocation presence, not section alone, because (a) a `.rodata` word can hold an
ADDRESS (vtable / jump table / string-pointer array) that legitimately differs
between builds, and (b) a logically-const object with a DYNAMIC initialiser
lives in `.bss` behind a guard variable -- `_Vector3<float>::UnitZ` is exactly
that -- so its at-rest zeros are meaningless. Under this rule the tool NEVER
reads a writable value, so neither the dead-.data-initialiser trap nor its
.bss-filled-at-runtime inverse is reachable.

The same distinction is visible on both sides:
  * port  (unlinked .o):  `objdump -dr` prints an explicit R_ARM_* relocation
                          against the pool word / call site. Reloc => name.
                          No reloc => the word IS the value.
  * binary (linked DYN):  the GOT idiom
                              ldr rB,[pc,#a] ; .word GOT-(pc)
                              add rB,pc,rB
                              ldr rX,[pc,#b] ; .word <GOT slot offset>
                              ldr rD,[rB,rX]
                          lands on a .got slot carrying an R_ARM_GLOB_DAT /
                          R_ARM_JUMP_SLOT relocation. Reloc => name.
                          A pool word with no such resolution is a constant.

RESOLVE-THEN-COMPARE, ELSE FALL BACK
------------------------------------
`.L3` / `.LANCHOR0` genuinely differ between builds, and so do compiler-local
outlines (`T.936`). A name is emitted ONLY when the resolver lands on a real
named symbol reachable the same way on both sides; anything else keeps the
existing `<SYM>` / `CALL` masking. A flood of false divergence is strictly worse
than the current blindness, because the current blindness is understood.

Annotation vocabulary (appended to the normalized instruction):
    CALL =<mangled>      call resolved to a named symbol (through PLT if needed)
    CALL =LOCAL[a,b]     call resolved to a compiler-local outline (T.NNN),
                         described by the named symbols its body touches
    CALL <SYM>           unresolved -- existing masking, no new signal
    ldr GREG, [pc, #POOL] {=<mangled>}   pool word is a relocated slot -> NAME
    ldr GREG, [pc, #POOL] {#<value>}     pool word is a read-only constant -> VALUE
    ldr GREG, [pc, #POOL]                unresolved -- existing masking

Deliberately NOT annotated, because the identity is not comparable across
builds and emitting it is pure one-sided noise:
  * the `_GLOBAL_OFFSET_TABLE_ - pc` delta word (reloc-model, rule 2);
  * `.LANCHOR<n>` / `.rodata.str1.1` / `.text.*` -- link-time anchors and
    SECTION relocations, not names.
`CALL =LOCAL` IS emitted, because "the binary calls a compiler-local outline
here" is a real, checkable statement about the binary -- it is what surfaces
the SplatEntity libc-rand() bug -- even though it cannot be matched by name.
"""
import json
import os
import pathlib
import re
import subprocess
import struct

# --- shared line grammar -----------------------------------------------------

# objdump line: "  1db91c:\te59f5328 \tldr\tr5, [pc, #808]\t; 1dbc4c <sym+0x33c>"
_ADDR_RE   = re.compile(r"^\s*([0-9a-f]+):\s")
# The "; <hex> <...>" trailer objdump appends to a PC-relative load names the
# literal-pool slot's address exactly -- no pc+8 / Thumb-alignment maths needed,
# and it is identical in form on both sides.
_POOLREF_RE = re.compile(r";\s*([0-9a-f]+)\s+<")
_WORD_RE   = re.compile(r"^\s*([0-9a-f]+):\s+[0-9a-f ]+\s+\.word\s+(0x[0-9a-f]+|\d+)")
_PCLOAD_RE = re.compile(r"\b(?:v?ldr[a-z]*)\b[^;]*\[pc,\s*#-?(?:0x[0-9a-f]+|\d+)\]")
_CALL_RE   = re.compile(r"\b(blx?)\s+([0-9a-f]+)\s")
# objdump -r emits relocations on their own line, indented under the insn:
#             "\t\t\t324: R_ARM_GOTPC\t_GLOBAL_OFFSET_TABLE_"
_RELOC_RE  = re.compile(r"^\s+([0-9a-f]+):\s+(R_ARM_\S+)\s+(\S+)")

# Compiler-local outlines / anchors: real symbols in the symtab, but NOT stable
# names -- GCC numbers them per-TU. Treated as "resolved, but local".
_LOCAL_SYM_RE = re.compile(r"^(T\.\d+|\.L|__gnu_|\$[atd]$)")

# Relocation types whose addend names a symbol we can compare by NAME.
_RELOC_SYMBOLIC = (
    "R_ARM_GOT32", "R_ARM_GOT_BREL", "R_ARM_GOT_PREL",
    "R_ARM_ABS32", "R_ARM_REL32", "R_ARM_GOTOFF32", "R_ARM_GOTOFF",
    "R_ARM_TARGET1", "R_ARM_TARGET2", "R_ARM_PREL31",
)
_RELOC_CALL = ("R_ARM_CALL", "R_ARM_PLT32", "R_ARM_JUMP24",
               "R_ARM_THM_CALL", "R_ARM_THM_JUMP24")
_RELOC_GOTPC = ("R_ARM_GOTPC", "R_ARM_BASE_PREL")


# --- canonical name form -----------------------------------------------------
# Comparison is done on the DEMANGLED name. Mangled equality is NOT name
# equality: the two sides pick different Itanium SUBSTITUTION encodings for the
# same entity, e.g.
#     binary  _ZN6Mortar16DataStreamReader9SetSourceEPKvmNS_6Endian10EndiannessE
#     port    _ZN6Mortar16DataStreamReader9SetSourceEPKvmN6Mortar6Endian10Endian...
# -- same function, `NS_` vs the spelled-out `N6Mortar`. Comparing mangled would
# report that as a divergence (measured: it was, twice, in the first sweep).
# Demangling collapses it. If c++filt is unavailable the mangled name is used --
# symmetrically on both sides, since one memo serves the whole process.
_DEMANGLE_MEMO = {}
_CXXFILT = [None]


def _cxxfilt_path():
    if _CXXFILT[0] is not None:
        return _CXXFILT[0] or None
    cand = os.environ.get("ASM_VERIFY_CXXFILT")
    if not cand:
        objdump = os.environ.get("ASM_VERIFY_OBJDUMP", "")
        if objdump:
            cand = re.sub(r"objdump$", "c++filt", objdump)
    _CXXFILT[0] = cand if (cand and pathlib.Path(cand).exists()) else ""
    return _CXXFILT[0] or None


def canonical_names(names):
    """Batch-demangle into the memo. One c++filt per annotator, not per name."""
    todo = sorted(set(n for n in names
                      if n not in _DEMANGLE_MEMO and n.startswith("_Z")))
    if todo:
        tool = _cxxfilt_path()
        if tool:
            try:
                res = subprocess.run([tool], input="\n".join(todo),
                                     capture_output=True, text=True, check=True)
                out = res.stdout.splitlines()
                if len(out) == len(todo):
                    for a, b in zip(todo, out):
                        _DEMANGLE_MEMO[a] = b.strip() or a
            except Exception:
                pass
        for n in todo:
            _DEMANGLE_MEMO.setdefault(n, n)


def canonical_name(name):
    """Demangled form of a symbol name, memoised. Identity for C symbols."""
    if name not in _DEMANGLE_MEMO:
        canonical_names([name])
        _DEMANGLE_MEMO.setdefault(name, name)
    return _DEMANGLE_MEMO[name]


def _ror32(v, n):
    n &= 31
    return ((v >> n) | (v << (32 - n))) & 0xFFFFFFFF


def _arm_dp_imm(word):
    """Decode an ARM data-processing rotated immediate."""
    return _ror32(word & 0xFF, ((word >> 8) & 0xF) * 2)


# ============================================================================
# Binary side -- linked ELF: sections, symbols, dynamic relocations, PLT.
# ============================================================================

class BinaryIndex(object):
    """Resolution index for the linked FruitNinja.exe.

    Built once per run (a few objdump/nm invocations + a raw .plt scan) and
    cached as JSON keyed by the binary's size+mtime, because every worker
    process in the ProcessPoolExecutor needs it.
    """

    def __init__(self, binary, objdump, nm, cache_path=None):
        self.binary = pathlib.Path(binary)
        self.objdump = str(objdump)
        self.nm = str(nm)
        self.sections = []        # (name, vma, size, fileoff, flags)
        self.relocs = {}          # vma -> symbol name (dynamic reloc table)
        self.symbols = {}         # vma -> symbol name
        self.got_base = None
        self.got_size = 0
        self.plt = {}             # plt entry vma -> got slot vma
        self.sizes = {}           # vma -> symbol byte size
        self.text_range = (0, 0)
        self._outline_cache = {}
        self._load(cache_path)

    # -- construction --------------------------------------------------------

    def _cache_key(self):
        st = self.binary.stat()
        return "%d-%d" % (st.st_size, int(st.st_mtime))

    def _load(self, cache_path):
        if cache_path:
            cache_path = pathlib.Path(cache_path)
            if cache_path.exists():
                try:
                    blob = json.loads(cache_path.read_text())
                    if blob.get("key") == self._cache_key():
                        self._from_blob(blob)
                        return
                except Exception:
                    pass
        self._build()
        if cache_path:
            try:
                cache_path.parent.mkdir(parents=True, exist_ok=True)
                cache_path.write_text(json.dumps(self._to_blob()))
            except Exception:
                pass

    def _to_blob(self):
        return {
            "key": self._cache_key(),
            "sections": self.sections,
            "relocs": dict(("%x" % k, v) for k, v in self.relocs.items()),
            "symbols": dict(("%x" % k, v) for k, v in self.symbols.items()),
            "got_base": self.got_base, "got_size": self.got_size,
            "plt": dict(("%x" % k, "%x" % v) for k, v in self.plt.items()),
            "sizes": dict(("%x" % k, v) for k, v in self.sizes.items()),
            "text_range": list(self.text_range),
        }

    def _from_blob(self, b):
        self.sections = [tuple(s) for s in b["sections"]]
        self.relocs = dict((int(k, 16), v) for k, v in b["relocs"].items())
        self.symbols = dict((int(k, 16), v) for k, v in b["symbols"].items())
        self.got_base = b["got_base"]
        self.got_size = b["got_size"]
        self.plt = dict((int(k, 16), int(v, 16)) for k, v in b["plt"].items())
        self.sizes = dict((int(k, 16), v) for k, v in b.get("sizes", {}).items())
        self.text_range = tuple(b["text_range"])

    def _run(self, args):
        return subprocess.run(args, capture_output=True, text=True,
                              check=True).stdout

    def _build(self):
        # -- sections ---------------------------------------------------------
        out = self._run([self.objdump, "-h", str(self.binary)])
        pend = None
        for line in out.splitlines():
            m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+"
                         r"([0-9a-f]+)\s+([0-9a-f]+)", line)
            if m:
                pend = [m.group(1), int(m.group(3), 16), int(m.group(2), 16),
                        int(m.group(5), 16), ""]
                continue
            if pend is not None and "ALLOC" in line:
                pend[4] = line.strip()
                self.sections.append(tuple(pend))
                pend = None
        for name, vma, size, _off, _fl in self.sections:
            if name == ".got":
                self.got_size = size
            if name == ".text":
                self.text_range = (vma, vma + size)

        # -- dynamic relocations ---------------------------------------------
        out = self._run([self.objdump, "-R", str(self.binary)])
        for line in out.splitlines():
            m = re.match(r"^([0-9a-f]{8})\s+(R_ARM_\S+)\s+(\S+)", line)
            if m and m.group(3) != "*ABS*":
                self.relocs[int(m.group(1), 16)] = m.group(3)

        # -- symbols ----------------------------------------------------------
        out = self._run([self.nm, "--print-size", str(self.binary)])
        for line in out.splitlines():
            m = re.match(r"^([0-9a-f]{8})\s+(?:([0-9a-f]+)\s+)?(\S)\s+(\S+)$", line)
            if not m:
                continue
            addr, name = int(m.group(1), 16), m.group(4)
            if name == "_GLOBAL_OFFSET_TABLE_":
                self.got_base = addr
                continue
            # First definition wins -- nm lists in address order and aliases
            # (e.g. C1/C2 ctor pairs) are equivalent for identity purposes.
            if addr not in self.symbols:
                self.symbols[addr] = name
                if m.group(2):
                    self.sizes[addr] = int(m.group(2), 16)

        # -- PLT: entry address -> GOT slot ----------------------------------
        self._build_plt()

    def _build_plt(self):
        sec = None
        for s in self.sections:
            if s[0] == ".plt":
                sec = s
                break
        if not sec:
            return
        _name, vma, size, fileoff, _fl = sec
        with open(self.binary, "rb") as fh:
            fh.seek(fileoff)
            data = fh.read(size)
        n = len(data) // 4
        words = struct.unpack("<%dI" % n, data[:n * 4])
        for i in range(n - 2):
            w0, w1, w2 = words[i], words[i + 1], words[i + 2]
            # add ip, pc, #imm ; add ip, ip, #imm ; ldr pc, [ip, #off]!
            if (w0 & 0xFFFFF000) != 0xE28FC000:
                continue
            if (w1 & 0xFFFFF000) != 0xE28CC000:
                continue
            if (w2 & 0xFFFFF000) != 0xE5BCF000:
                continue
            addr = vma + i * 4
            got = ((addr + 8) + _arm_dp_imm(w0) + _arm_dp_imm(w1)
                   + (w2 & 0xFFF)) & 0xFFFFFFFF
            self.plt[addr] = got

    # -- resolution ----------------------------------------------------------

    def section_of(self, vma):
        for name, svma, size, _off, flags in self.sections:
            if svma <= vma < svma + size:
                return name, flags
        return None, ""

    def resolve_call(self, target, follow=3):
        """Resolve a `bl`/`blx` target to (kind, value).

        kind 'sym'   -> value is the real mangled symbol name.
        kind 'local' -> value is the ADDRESS of a compiler-local outline; its
                        `T.<n>` name is not comparable across builds, so pass
                        the address to outline_label() instead.
        kind None    -> unresolved. PLT entries resolve THROUGH the GOT slot's
        R_ARM_JUMP_SLOT relocation; 4-byte `b` veneers that hop back into the
        PLT are followed (the TiXml case needs two hops).
        """
        seen = set()
        for _ in range(follow):
            if target in seen:
                break
            seen.add(target)
            if target in self.plt:
                name = self.relocs.get(self.plt[target])
                if name:
                    return "sym", name
                return None, None
            name = self.symbols.get(target)
            if name:
                if _LOCAL_SYM_RE.match(name):
                    return "local", target
                return "sym", name
            nxt = self._veneer_target(target)
            if nxt is None:
                return None, None
            target = nxt
        return None, None

    def outline_label(self, vma):
        """Describe a compiler-local outline (`T.936`) by what it TOUCHES.

        GCC -O2 outlines / IPA clones get per-TU names (`T.<n>`) that exist on
        one side only, so they can never be matched by name -- and masking them
        back to a bare `CALL` throws away the only comparable fact about them.
        One level of expansion over the outline's own body recovers that: the
        named functions it calls and the globals it loads through the GOT.

        `T.936` -> "LOCAL[_ZN4Math8g_randomE]", which is precisely the fact that
        makes a port-side libc `rand()` visible at the CALLER.

        Deliberately ONE level deep and label-only: no recursion, no dataflow.
        A label is never compared against a real symbol name -- it just carries
        the binary's side of the story into the diff body.
        """
        if vma in self._outline_cache:
            return self._outline_cache[vma]
        label = "LOCAL"
        size = self.sizes.get(vma, 0)
        if 0 < size <= 4096:
            try:
                text = self._run([self.objdump, "-d",
                                  "--start-address=0x%x" % vma,
                                  "--stop-address=0x%x" % (vma + size),
                                  str(self.binary)])
                names = []
                for line in text.splitlines():
                    wm = _WORD_RE.match(line)
                    if wm:
                        kind, nm_ = self.resolve_got_slot(int(wm.group(2), 0))
                        if kind == "sym" and nm_ not in names:
                            names.append(nm_)
                        continue
                    cm = _CALL_RE.search(line)
                    if cm:
                        kind, nm_ = self.resolve_call(int(cm.group(2), 16))
                        if kind == "sym" and nm_ not in names:
                            names.append(nm_)
                if names:
                    label = "LOCAL[%s]" % ",".join(sorted(names)[:2])
            except Exception:
                pass
        self._outline_cache[vma] = label
        return label

    def _veneer_target(self, vma):
        """If `vma` holds a lone ARM `b <imm>`, return its destination."""
        name, _flags = self.section_of(vma)
        if name not in (".text", ".plt"):
            return None
        w = self._word_at(vma)
        if w is None:
            return None
        if (w & 0x0F000000) != 0x0A000000:      # B (not BL: bit24 clear)
            return None
        if (w >> 28) != 0xE:                    # unconditional only
            return None
        off = w & 0x00FFFFFF
        if off & 0x00800000:
            off -= 0x01000000
        return (vma + 8 + off * 4) & 0xFFFFFFFF

    def _word_at(self, vma):
        for name, svma, size, fileoff, flags in self.sections:
            if svma <= vma < svma + size:
                if "CONTENTS" not in flags:
                    return None
                with open(self.binary, "rb") as fh:
                    fh.seek(fileoff + (vma - svma))
                    b = fh.read(4)
                if len(b) < 4:
                    return None
                return struct.unpack("<I", b)[0]
        return None

    def resolve_got_slot(self, offset):
        """GOT-relative offset -> (kind, name). Only a slot that actually
        carries a dynamic relocation, or that names a defined symbol, resolves.
        """
        if self.got_base is None:
            return None, None
        if not (0 < offset < max(self.got_size, 1)):
            return None, None
        slot = self.got_base + offset
        name = self.relocs.get(slot)
        if name:
            return ("local", name) if _LOCAL_SYM_RE.match(name) else ("sym", name)
        return None, None

    def resolve_gotoff(self, offset):
        """GOT-base-relative DATA offset (`add rD, rBase, rOff`) -> symbol."""
        if self.got_base is None:
            return None, None
        addr = (self.got_base + offset) & 0xFFFFFFFF
        name = self.symbols.get(addr)
        if name:
            return ("local", name) if _LOCAL_SYM_RE.match(name) else ("sym", name)
        # An offset into .got that is relocated is still a symbol reference.
        return self.resolve_got_slot(offset)

    def is_gotpc_delta(self, word, insn_vma):
        """True when `word` is the `_GLOBAL_OFFSET_TABLE_ - (pc)` delta."""
        if self.got_base is None:
            return False
        base = (insn_vma + 8 + word) & 0xFFFFFFFF
        return base == self.got_base


# ============================================================================
# Per-side annotators
# ============================================================================

class _Annotator(object):
    """Common shell: parse a disassembly once, expose annotate(raw_line)."""

    def __init__(self):
        self.pool_annot = {}      # pool slot addr -> annotation string
        self.call_annot = {}      # instruction addr -> annotation string

    def _canonicalise(self):
        """Rewrite every emitted MANGLED name to its demangled canonical form.

        Runs once per side, after annotation, so it costs one c++filt call --
        and, because the memo is process-wide, both sides of a symbol are
        canonicalised the same way even if c++filt is missing."""
        raw = []
        for d in (self.pool_annot, self.call_annot):
            for v in d.values():
                raw.extend(re.findall(r"_Z[\w.]+", v))
        if not raw:
            return
        canonical_names(raw)
        def _sub(v):
            return re.sub(r"_Z[\w.]+", lambda m: _DEMANGLE_MEMO.get(
                m.group(0), m.group(0)), v)
        for d in (self.pool_annot, self.call_annot):
            for k in list(d):
                d[k] = _sub(d[k])

    def annotate(self, raw_line):
        m = _ADDR_RE.match(raw_line)
        addr = int(m.group(1), 16) if m else None
        if addr is not None and addr in self.call_annot:
            return self.call_annot[addr]
        if _PCLOAD_RE.search(raw_line):
            pm = _POOLREF_RE.search(raw_line)
            if pm:
                return self.pool_annot.get(int(pm.group(1), 16))
        return None


class PortAnnotator(_Annotator):
    """Port side: an unlinked .o. Relocations name everything directly."""

    def __init__(self, text):
        _Annotator.__init__(self)
        relocs = {}
        words = {}
        refs = set()
        lo, hi = None, None
        for line in text.splitlines():
            # Relocation records are self-identifying (the R_ARM_* token sits
            # where an instruction's raw-bytes column would be), so no
            # positional guard is needed -- and adding one mis-fires, because
            # objdump indents them with TABS that `^\s*` happily eats.
            rm = _RELOC_RE.match(line)
            if rm:
                relocs[int(rm.group(1), 16)] = (rm.group(2), rm.group(3))
                continue
            m = _ADDR_RE.match(line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            lo = addr if lo is None else min(lo, addr)
            hi = addr if hi is None else max(hi, addr)
            wm = _WORD_RE.match(line)
            if wm:
                words[int(wm.group(1), 16)] = int(wm.group(2), 0)
            elif _PCLOAD_RE.search(line):
                pm = _POOLREF_RE.search(line)
                if pm:
                    refs.add(int(pm.group(1), 16))
        # objdump elides RUNS OF ZEROES as "...", so a pool slot that is
        # referenced, sits inside the disassembled range, and has neither a
        # `.word` line nor a relocation is a zero word -- not an unknown. Not
        # reconstructing it would make one side annotate a 0.0f constant that
        # the other side silently dropped (pure asymmetric noise).
        for slot in refs:
            if slot not in words and slot not in relocs \
                    and lo is not None and lo <= slot <= hi:
                words[slot] = 0
        # -- calls -----------------------------------------------------------
        for line in text.splitlines():
            m = _ADDR_RE.match(line)
            if not m or not re.search(r"\bblx?\s", line):
                continue
            addr = int(m.group(1), 16)
            rel = relocs.get(addr)
            if rel and rel[0] in _RELOC_CALL:
                name = rel[1]
                kind = "local" if _LOCAL_SYM_RE.match(name) else "sym"
                self.call_annot[addr] = "=LOCAL" if kind == "local" else "=" + name
        # -- literal pool ----------------------------------------------------
        for slot in set(list(words.keys()) + list(relocs.keys())):
            rel = relocs.get(slot)
            if rel:
                rtype, name = rel
                if rtype in _RELOC_GOTPC:
                    # The GOT-PC delta is pure reloc-model noise (rule 2) and
                    # its shape differs between a linked DYN and an unlinked .o.
                    # Carries no port-fidelity signal -- leave it masked.
                    continue
                if rtype in _RELOC_SYMBOLIC:
                    if _LOCAL_SYM_RE.match(name) or name.startswith("."):
                        # .LANCHOR0 / .rodata.str1.1 / .text.* -- a SECTION or a
                        # link-time anchor, not a name. Identity is meaningless
                        # across builds, so keep the historical masking.
                        continue
                    self.pool_annot[slot] = "{=%s}" % name
                # any other reloc type: leave unresolved (fall back)
            elif slot in words:
                # No relocation at all -> non-relocated read-only bytes.
                # Compare by VALUE (rule: value only where nothing can move it).
                self.pool_annot[slot] = "{#0x%08x}" % words[slot]
        self._canonicalise()


class BinaryAnnotator(_Annotator):
    """Binary side: a linked DYN. Recovers identity through GOT / PLT.

    Runs a small linear dataflow over the function so a pool word is only read
    as a GOT offset when it is actually CONSUMED as one -- `ldr rD,[rBase,rOff]`
    / `add rD,rBase,rOff` where rBase is the function's GOT-base register. That
    is what keeps a small integer constant from being mis-resolved into
    whatever GOT slot happens to sit at that offset.
    """

    def __init__(self, text, index):
        _Annotator.__init__(self)
        self.index = index
        words = {}
        insns = []       # (addr, mnem, ops, pool)
        lo, hi = None, None
        for line in text.splitlines():
            m = _ADDR_RE.match(line)
            if not m:
                continue
            addr = int(m.group(1), 16)
            lo = addr if lo is None else min(lo, addr)
            hi = addr if hi is None else max(hi, addr)
            wm = _WORD_RE.match(line)
            if wm:
                words[addr] = int(wm.group(2), 0)
                continue
            body = line.split("\t")
            if len(body) < 3:
                continue
            mn = body[2].strip()
            ops = body[3].split(";")[0].strip() if len(body) > 3 else ""
            pool = None
            if _PCLOAD_RE.search(line):
                pm = _POOLREF_RE.search(line)
                if pm:
                    pool = int(pm.group(1), 16)
            insns.append((addr, mn, ops, pool))

        # Same objdump zero-run elision as the port side -- see PortAnnotator.
        for _a, _m, _o, pool in insns:
            if pool is not None and pool not in words \
                    and lo is not None and lo <= pool <= hi:
                words[pool] = 0

        # -- calls -----------------------------------------------------------
        for addr, mn, ops, _pool in insns:
            if not re.match(r"blx?$", mn):
                continue
            tm = re.match(r"([0-9a-f]+)\b", ops)
            if not tm:
                continue
            kind, val = index.resolve_call(int(tm.group(1), 16))
            if kind == "sym":
                self.call_annot[addr] = "=" + val
            elif kind == "local":
                self.call_annot[addr] = "=" + index.outline_label(val)

        # -- literal pool: dataflow ------------------------------------------
        reg_pool = {}        # reg -> pool slot addr
        got_regs = set()     # regs currently holding the GOT base
        consumed = set()     # pool slots consumed as a GOT offset
        gotpc = set()        # pool slots that hold the GOT-PC delta
        for addr, mn, ops, pool in insns:
            base = re.match(r"^(?:v?ldr|add|mov|sub)", mn)
            dst = None
            dm = re.match(r"(r\d+|ip|fp|sl|sb|lr)\b", ops)
            if base and dm:
                dst = dm.group(1)
            if pool is not None and dst:
                reg_pool[dst] = pool
                got_regs.discard(dst)
                continue
            # add rD, pc, rS  -> rD is the GOT base (rS came from a GOTPC word).
            # The `_GLOBAL_OFFSET_TABLE_ - pc` delta is relative to THIS
            # instruction, not to the load that fetched it, so the test has to
            # happen here.
            am = re.match(r"add\s*$", mn)
            if am:
                pm = re.match(r"(\w+),\s*pc,\s*(\w+)", ops)
                if pm and pm.group(2) in reg_pool:
                    slot = reg_pool[pm.group(2)]
                    w = words.get(slot)
                    if w is not None and index.is_gotpc_delta(w, addr):
                        gotpc.add(slot)
                        got_regs.add(pm.group(1))
                        reg_pool.pop(pm.group(1), None)
                        continue
                pm = re.match(r"(\w+),\s*(\w+),\s*(\w+)$", ops)
                if pm and pm.group(2) in got_regs and pm.group(3) in reg_pool:
                    slot = reg_pool[pm.group(3)]
                    w = words.get(slot)
                    if w is not None:
                        kind, name = index.resolve_gotoff(w)
                        consumed.add(slot)
                        self._set_pool(slot, kind, name)
                    if pm.group(1) not in (pm.group(2), pm.group(3)):
                        reg_pool.pop(pm.group(1), None)
                        got_regs.discard(pm.group(1))
                    continue
            # ldr rD, [rBase, rOff]  -> GOT slot load
            lm = re.match(r"ldr\S*$", mn)
            if lm:
                pm = re.match(r"(\w+),\s*\[(\w+),\s*(\w+)\]", ops)
                if pm and pm.group(2) in got_regs and pm.group(3) in reg_pool:
                    slot = reg_pool[pm.group(3)]
                    w = words.get(slot)
                    if w is not None:
                        kind, name = index.resolve_got_slot(w)
                        consumed.add(slot)
                        self._set_pool(slot, kind, name)
                    reg_pool.pop(pm.group(1), None)
                    got_regs.discard(pm.group(1))
                    continue
            if dst:
                reg_pool.pop(dst, None)
                got_regs.discard(dst)

        # -- remaining pool words --------------------------------------------
        for slot, w in words.items():
            if slot in consumed:
                continue
            if slot in gotpc:
                # GOT-PC delta: reloc-model noise, no port-fidelity signal.
                continue
            # The linear dataflow above is a PREFERENCE, not a gate: it can miss
            # a consumer across a branch join or a re-materialised GOT base. A
            # word that lands on a RELOCATED .got slot is a symbol reference by
            # definition -- emitting the value there would be a lie, and it is
            # the exact shape that showed up as one-sided `{#0x000077f4}` noise.
            kind, name = index.resolve_got_slot(w)
            if kind == "sym":
                self.pool_annot[slot] = "{=%s}" % name
                continue
            if kind == "local":
                continue
            # Not a GOT offset -> either a direct address (relocated / named
            # symbol => NAME) or a plain constant (=> VALUE).
            name = index.symbols.get(w)
            if name and not _LOCAL_SYM_RE.match(name):
                self.pool_annot[slot] = "{=%s}" % name
                continue
            if w in index.relocs:
                self.pool_annot[slot] = "{=%s}" % index.relocs[w]
                continue
            sec, flags = index.section_of(w)
            if sec is not None and "READONLY" not in flags:
                # Writable target with no name we can pin: do NOT read its
                # value (dead-.data / runtime-filled-.bss trap). Fall back.
                continue
            self.pool_annot[slot] = "{#0x%08x}" % w
        self._canonicalise()

    def _set_pool(self, slot, kind, name):
        if kind == "sym":
            self.pool_annot[slot] = "{=%s}" % name
        # kind 'local' (a link-time anchor) or None -> leave unresolved and
        # fall back to the plain masked `[pc, #POOL]`.


# ============================================================================
# Entry points used by asm-verify.py
# ============================================================================

_BIN_INDEX = None


def get_binary_index():
    """Lazily build (or load from cache) the binary-side index."""
    global _BIN_INDEX
    if _BIN_INDEX is not None:
        return _BIN_INDEX
    root = pathlib.Path(__file__).resolve().parent.parent.parent
    binary = pathlib.Path(os.environ.get(
        "ASM_VERIFY_BINARY", root / "FruitNinjaBada" / "Bin" / "FruitNinja.exe"))
    objdump = os.environ.get(
        "ASM_VERIFY_OBJDUMP",
        str(root / "tools" / "toolchain" / "sourcery-2010q1" / "bin"
            / "arm-none-eabi-objdump"))
    nm = os.environ.get("ASM_VERIFY_NM", objdump.replace("objdump", "nm"))
    cache_dir = pathlib.Path(os.environ.get(
        "ASM_VERIFY_REPORT_DIR", root / "tmp" / "asm-verify"))
    _BIN_INDEX = BinaryIndex(binary, objdump, nm,
                             cache_path=cache_dir / "binary-index.json")
    return _BIN_INDEX


def demangle_all(names, cxxfilt=None):
    """Best-effort batch demangle for REPORT DISPLAY only.

    Comparison is done on MANGLED names: both sides come from ELF symbol
    tables / relocation entries in the same Itanium namespace, so mangled
    equality IS name equality, and demangling before comparing would only lose
    the signature. (If the binary side ever came from Ghidra labels instead,
    this is where the canonicalisation would go.)
    """
    if not names:
        return {}
    cxxfilt = cxxfilt or os.environ.get("ASM_VERIFY_CXXFILT")
    if not cxxfilt:
        objdump = os.environ.get("ASM_VERIFY_OBJDUMP", "")
        if objdump:
            cxxfilt = objdump.replace("objdump", "c++filt")
    if not cxxfilt or not pathlib.Path(cxxfilt).exists():
        return {}
    try:
        res = subprocess.run([cxxfilt], input="\n".join(names),
                             capture_output=True, text=True, check=True)
    except Exception:
        return {}
    out = res.stdout.splitlines()
    return dict(zip(names, out)) if len(out) == len(names) else {}
