#!/usr/bin/env python3
"""
Per-method signature surgery against the binary.

Reads tmp/symbol-diff/*.txt, finds (class, method) pairs where the port's
demangled args differ from the binary's, and applies CONSERVATIVE in-place
edits to the port code:

  - Drop `Vec3 const&` / `const Vec3&` -> `Vec3` (and Vec2, Matrix44, Matrix43)
    when the binary takes the type by value.
  - Drop `SmartPtr<T> const&` -> `SmartPtr<T>` ditto.
  - Drop `Delegate0<X>/Delegate1<X,Y>... const&` -> by-value ditto.
  - Drop top-level extra `const` on by-value primitives
    (`int const`, `unsigned int const`, etc.).
  - Drop `T& const` malformed qualifier (`int& const` -> `int&`).

Edits target only the method's argument list (matched by `<method-name>(...)`)
in both the .h declaration and the .cpp out-of-line definition. Parameter
names are preserved so .cpp bodies keep compiling.

Skipped intentional deviations:
  - tinyxml2: TiXmlElement* / tinyxml2::XMLElement*  (per user policy)
  - Game& ctor arg                                    (per user policy)
  - Renderer& Draw arg                                (per user policy)
"""
import re, pathlib
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parents[2]
BIN_FILE  = ROOT / 'tmp/symbol-diff/binary_symbols_demangled.txt'
PORT_FILE = ROOT / 'tmp/symbol-diff/port_full_demangled.txt'

SIG_RE = re.compile(r'^(?:[\w:<>,\s\*&]+?\s+)?'
                    r'((?:[\w~]+::)*[\w~]+)\s*\((.*)\)\s*(?:const)?\s*$')

def parse(line):
    line = line.strip()
    if not line: return None
    if line.startswith(('vtable','typeinfo','VTT','construction','guard',
                        'non-virtual','virtual','_GLOBAL','global constructors')):
        return None
    line2 = re.sub(r'\)\s*const\s*$', ')', line)
    m = SIG_RE.match(line2)
    if not m: return None
    qual, args = m.group(1), m.group(2).strip()
    parts = qual.split('::')
    if len(parts) < 2: return None
    cls, mth = parts[-2], parts[-1]
    return cls, mth, args

# Index by (cls, mth) -> list of arg-strings
def index(lines):
    out = defaultdict(list)
    for ln in lines:
        p = parse(ln)
        if not p: continue
        cls, mth, args = p
        out[(cls, mth)].append(args)
    return out

bin_idx  = index(BIN_FILE.read_text(encoding='utf-8', errors='ignore').splitlines())
port_idx = index(PORT_FILE.read_text(encoding='utf-8', errors='ignore').splitlines())

# ---------------- Compute per-method mismatch diff ----------------

# Tokenize args at the top-comma level.
def split_args(args):
    parts, cur, depth = [], '', 0
    for ch in args:
        if ch == '<': depth += 1; cur += ch
        elif ch == '>': depth -= 1; cur += ch
        elif ch == ',' and depth == 0:
            parts.append(cur.strip()); cur = ''
        else:
            cur += ch
    if cur.strip(): parts.append(cur.strip())
    return parts

def normalize_arg(a):
    a = re.sub(r'\bunsigned long\b', 'unsigned int', a)
    a = re.sub(r'\blong\b(?!\s+long)', 'int', a)
    a = re.sub(r'\s+([*&])', r'\1', a)
    a = re.sub(r'([*&])\s+', r'\1 ', a)
    a = re.sub(r'\s+', ' ', a).strip()
    return a

def to_port_form(args):
    a = args
    a = re.sub(r'_Vector3<float>', 'Vec3', a)
    a = re.sub(r'_Vector2<float>', 'Vec2', a)
    a = re.sub(r'_Matrix44<float>', 'Matrix44', a)
    a = re.sub(r'_Matrix43<float>', 'Matrix43', a)
    return a

# Identify (cls, mth) pairs with at least one differing signature.
auto_fix_targets = set()
for key, bargs_list in bin_idx.items():
    if key not in port_idx: continue
    bset = set(normalize_arg(a) for a in bargs_list)
    pset = set(normalize_arg(a) for a in port_idx[key])
    if bset == pset: continue

    cls, mth = key
    # Find at least one binary signature whose port-form args list differs
    # only by qualifier-strip (const& -> by-value) or const-strip.
    for b in bset - pset:
        for p in pset - bset:
            bp = to_port_form(b)
            pp = to_port_form(p)
            # Strip per-arg decorators for shape comparison.
            def core(s):
                ts = split_args(s)
                core_parts = []
                for t in ts:
                    t = re.sub(r'\bconst\b', '', t)
                    t = re.sub(r'[*&]+', '', t)
                    t = re.sub(r'\s+', ' ', t).strip()
                    core_parts.append(t)
                return ', '.join(core_parts)
            if core(pp) == core(bp):
                # Same arg-shape, only qualifiers differ -- safe to fix.
                auto_fix_targets.add((cls, mth))
                break

print(f'Methods to auto-fix: {len(auto_fix_targets)}')

# ---------------- Patterns to apply per arg list ----------------

# Patterns are tuples (regex, replacement). Applied to the args portion of
# every targeted method declaration. Param names preserved by group capture.
ARG_PATTERNS = [
    # `Vec3 const&` / `const Vec3&` -> `Vec3`
    (re.compile(r'\bconst\s+Vec3\s*&'),     'Vec3'),
    (re.compile(r'\bVec3\s+const\s*&'),     'Vec3'),
    (re.compile(r'\bconst\s+Vec2\s*&'),     'Vec2'),
    (re.compile(r'\bVec2\s+const\s*&'),     'Vec2'),
    (re.compile(r'\bconst\s+Matrix44\s*&'), 'Matrix44'),
    (re.compile(r'\bMatrix44\s+const\s*&'), 'Matrix44'),
    (re.compile(r'\bconst\s+Matrix43\s*&'), 'Matrix43'),
    (re.compile(r'\bMatrix43\s+const\s*&'), 'Matrix43'),
    (re.compile(r'\bconst\s+Colour\s*&'),   'Colour'),
    (re.compile(r'\bColour\s+const\s*&'),   'Colour'),
    # `SmartPtr<X> const&` -> `SmartPtr<X>` (likewise leading const)
    (re.compile(r'\bconst\s+SmartPtr<([^>]+)>\s*&'), r'SmartPtr<\1>'),
    (re.compile(r'\bSmartPtr<([^>]+)>\s+const\s*&'), r'SmartPtr<\1>'),
    # `T& const` -- malformed; the const is meaningless. Just strip.
    (re.compile(r'(&|\*)\s+const(\s|,|\)|;|=|$)'), r'\1\2'),
    # Top-level `const` on by-value primitives at end of arg
    # (e.g. `unsigned int const`) -- drop.
    (re.compile(r'\b(unsigned\s+(?:char|short|int|long))\s+const\b(?!\s*[*&])'), r'\1'),
    (re.compile(r'\b(char|short|int|long|float|double)\s+const\b(?!\s*[*&])'), r'\1'),
]

def fix_args(args_text):
    a = args_text
    for pat, rep in ARG_PATTERNS:
        a = pat.sub(rep, a)
    return a

def find_files(cls):
    out = []
    for ext in ('.h', '.cpp'):
        for p in ROOT.glob(f'src/**/{cls}{ext}'):
            if 'stubs' in p.parts: continue
            out.append(p)
    return out

n_files_changed = 0
n_method_edits = 0

# Match a single declaration/definition: optional return type + class scope +
# method name + balanced parens. We use a simple `<name>\(<arg-text>\)` regex
# with greedy matching for arg-text but balanced manually.
for cls, mth in sorted(auto_fix_targets):
    files = find_files(cls)
    for f in files:
        text = f.read_text(encoding='utf-8', errors='ignore')
        # Find each `<mth>(...)` in the file; we rely on `(...)` being
        # paren-balanced at depth 1 (which is true for normal C++ code).
        new_text = text
        method_re = re.compile(rf'\b({re.escape(mth)})\s*\(', re.M)
        cursor = 0
        edited = False
        while True:
            m = method_re.search(new_text, cursor)
            if not m: break
            # Walk from the open paren forward to find the matching close.
            i = m.end()
            depth = 1
            while i < len(new_text) and depth:
                c = new_text[i]
                if c == '(': depth += 1
                elif c == ')': depth -= 1
                i += 1
            if depth: break
            args_start = m.end()
            args_end = i - 1
            args = new_text[args_start:args_end]
            new_args = fix_args(args)
            if new_args != args:
                new_text = new_text[:args_start] + new_args + new_text[args_end:]
                edited = True
                n_method_edits += 1
                cursor = args_start + len(new_args) + 1
            else:
                cursor = i
        # Strip trailing `const` on method declarations / definitions when
        # binary's signature drops it. We detect by: if any binary sig for
        # this (cls, mth) is non-const but every port sig is const, strip.
        b_consts = [normalize_arg(a) for a in bin_idx[(cls, mth)]]
        p_consts = [normalize_arg(a) for a in port_idx[(cls, mth)]]
        # NOTE: our parse() already stripped trailing `const` from args, so
        # we can't tell from those alone. Keeping this fix out of scope here;
        # earlier `tools/symbol-diff/fix_const.py` handled the bulk.
        if edited:
            f.write_text(new_text, encoding='utf-8')
            n_files_changed += 1
            print(f'  edited: {f.relative_to(ROOT)} -- {mth}')

print(f'\nTotal: {n_method_edits} method edits across {n_files_changed} file writes')
