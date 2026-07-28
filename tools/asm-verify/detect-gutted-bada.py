#!/usr/bin/env python3
"""detect-gutted-bada.py -- find bodies the asm-verify cross-build silently guts.

The asm-verify cross-build compiles src/ with -D__bada__ (toolchain.cmake:72),
which flips ~470 `#if(n)def __bada__` regions.  The lethal shape is
WRITE-REMOVED / READ-KEPT: a guard strips a STORE while the LOAD stays
unguarded, so the cross-build diffs a function against a value frozen at its
constructor.  Nothing errors -- the verification number just silently becomes
meaningless, in EITHER direction (a gutted body scores falsely CLEAN as easily
as falsely divergent), so bad scores alone cannot find these.

Two complementary detectors, both run by default:

  SOURCE  -- preprocessor-aware scan of src/.  Rules:
             R1 write-removed-read-kept : symbol stored ONLY inside blocks the
                                          cross-build excludes, loaded outside
             R2 gutted-body             : whole function body vanishes under
                                          __bada__ while the port arm has code
             R3 noop-else-arm           : `#else` (__bada__) arm is only
                                          `(void)x;` / a trivial return
             R4 definition-removed      : the DEFINITION is excluded, so the
                                          symbol never pairs -- green by silence
             R5 binary-object-not-built : a guard drops `new <binary class>` out
                                          of a live binary function
             parse anomalies            : #elif chains touching __bada__, orphan
                                          / unterminated directives, and
                                          brace-spanning guards -- everything the
                                          line-based scan could NOT model is
                                          reported rather than dropped silently

             Ranking is gated on the binary's own symbol table, so a port-only
             helper that legitimately vanishes ranks LOW while a real binary
             symbol losing its body ranks HIGH.

  REPORT  -- mines tmp/asm-verify/report.json for symbols whose ported body is
             a tiny fraction of the binary's instruction count, cross-referenced
             against files that actually contain a code-removing guard and
             against classify-divergences.py's `cause` field (without the cause
             cross-reference the real signal drowns in port-stub rows).

Machine-readable JSON lands in tmp/gutted-bada/ ; stdout is a short ranked
summary.  ASCII only.

Usage:
  tools/asm-verify/detect-gutted-bada.py                 # both detectors
  tools/asm-verify/detect-gutted-bada.py --mode source
  tools/asm-verify/detect-gutted-bada.py --git-rev HEAD~1   # scan a past tree
  tools/asm-verify/detect-gutted-bada.py --top 40 --min-rank LOW
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import shutil
from collections import Counter, defaultdict

# ---------------------------------------------------------------------------
# configuration
# ---------------------------------------------------------------------------

SRC_EXTS = ('.h', '.hpp', '.inl', '.cpp', '.c')

# Files whose __bada__ guards are platform plumbing by construction.  These are
# the *SDL / *Posix / *Win32 / platform-subtree / debug-OSD conventions from
# CLAUDE.md -- excluded from the asm-verify sweep anyway, so a guard there can
# never gut a verified body.
FILE_SKIP_RE = re.compile(
    r'(?:^|/)(?:'
    r'platform/'
    r'|debug/'
    r'|main\.cpp$'
    r')'
    r'|SDL\.(?:h|cpp)$'
    r'|Posix\.(?:h|cpp)$'
    r'|Win32\.(?:h|cpp)$'
    r'|WebOS\.(?:h|cpp)$'
    r'|Wii\.(?:h|cpp)$'
)

# Content that legitimately disappears under __bada__.  A guarded block whose
# every effective statement matches one of these is never a finding.
BENIGN_STMT_RES = [
    re.compile(r'\bLOG_[A-Z]+\s*\('),                 # expands to ((void)0) by design
    re.compile(r'\bDebug::Log'),
    re.compile(r'\b(?:printf|fprintf|snprintf|puts|fputs|fflush|perror)\s*\('),
    re.compile(r'\bassert\s*\(|\bstatic_assert\b|\bSTATIC_ASSERT\b|__builtin_offsetof|\boffsetof\s*\('),
    re.compile(r'\bgl[A-Z]\w*\s*\(|\bGL_[A-Z]|\bSDL_[A-Z]\w*\s*\(|\begl[A-Z]|\bglew'),
    re.compile(r'\b(?:std::)?(?:cout|cerr|clog)\b'),
    re.compile(r'#\s*(?:include|error|warning|pragma|define|undef)'),
]

# Names that are never a binary field worth tracking.
NAME_SKIP_RE = re.compile(
    r'^(?:'
    r'[A-Z][A-Z0-9_]*'                        # SCREAMING constants / macros
    r'|s_warned|s_once|s_logged|s_dbg\w*|s_Debug\w*'
    r'|g_Debug\w*|g_Verbose\w*|g_Log\w*'
    r'|[a-z]'                                 # x/y/z/w, r/g/b/a -- vector and
    r'|[a-z]\d'                               # colour components collide across
    r')$'                                     # every unrelated object
)

TRIVIAL_STMT_RE = re.compile(
    r'^\s*(?:'
    r'\(\s*void\s*\)\s*[\w\.\->\[\]]+\s*;'    # (void)event;
    r'|[\w\.\->\[\]]+\s*;'                    # bare `event;`
    r'|return\s*(?:true|false|0|0\.0f?|nullptr|NULL|-1)?\s*;'
    r'|break\s*;|continue\s*;'
    r'|[{}]'
    r')\s*$'
)

DECL_KEYWORDS = (
    'float', 'double', 'int', 'bool', 'char', 'void', 'short', 'long',
    'unsigned', 'signed', 'size_t', 'auto', 'const', 'static',
)

# ---------------------------------------------------------------------------
# lexing helpers
# ---------------------------------------------------------------------------

_STR_RE = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')


def read_lines(path):
    """Read a source file, stripping a UTF-8 BOM.  A BOM hides the file's first
    directive from a column-anchored scan and fakes an unbalanced guard
    (src/game/GameWork.h was reported as having an orphan #endif for exactly
    this reason)."""
    with open(path, encoding='utf-8', errors='replace') as fh:
        text = fh.read()
    if text and text[0] == '\ufeff':
        text = text[1:]
    return text.split('\n')


def strip_code(line, in_block_comment):
    """Blank out strings and comments.  Returns (clean_line, in_block_comment)."""
    out = []
    i = 0
    n = len(line)
    while i < n:
        if in_block_comment:
            j = line.find('*/', i)
            if j < 0:
                return ''.join(out), True
            i = j + 2
            in_block_comment = False
            continue
        if line.startswith('//', i):
            break
        if line.startswith('/*', i):
            in_block_comment = True
            i += 2
            continue
        ch = line[i]
        if ch in '"\'':
            m = _STR_RE.match(line, i)
            if m:
                out.append(' ' * (m.end() - i))
                i = m.end()
                continue
        out.append(ch)
        i += 1
    return ''.join(out), in_block_comment


# ---------------------------------------------------------------------------
# preprocessor state machine
# ---------------------------------------------------------------------------

# per-line cross-build state
ST_NEUTRAL = 'neutral'    # compiled in both builds
ST_BADA = 'bada'          # compiled ONLY with -D__bada__  (the cross-build)
ST_NOBADA = 'nobada'      # compiled ONLY WITHOUT __bada__ -> REMOVED from cross-build
ST_DEAD = 'dead'          # both bada and !bada required -- unreachable

PP_RE = re.compile(r'^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$')


def _bada_truth(kw, expr):
    """Return True if this arm is compiled under -D__bada__, False if it is
    compiled only WITHOUT it, or None if the condition does not mention
    __bada__ at all."""
    if '__bada__' not in expr:
        return None
    if kw == 'ifdef':
        return True
    if kw == 'ifndef':
        return False
    # #if / #elif expression
    e = expr.strip()
    neg = re.search(r'!\s*defined\s*\(?\s*__bada__', e) is not None
    if neg:
        return False
    if re.search(r'\bdefined\s*\(?\s*__bada__', e) is not None:
        return True
    return None


def scan_preprocessor(lines, path):
    """Compute per-line cross-build state plus guard blocks and anomalies."""
    states = [ST_NEUTRAL] * len(lines)
    stack = []          # list of dicts: {'truth': T/F/None, 'kw', 'start', 'expr', 'else_seen'}
    blocks = []         # bada-relevant regions
    anomalies = []
    in_comment = False
    clean = []

    for i, raw in enumerate(lines):
        c, in_comment = strip_code(raw, in_comment)
        clean.append(c)
        m = PP_RE.match(c)
        if m:
            kw, expr = m.group(1), m.group(2)
            if kw in ('if', 'ifdef', 'ifndef'):
                t = _bada_truth(kw, expr)
                stack.append({'truth': t, 'truth0': t, 'kw': kw, 'start': i + 1,
                              'expr': expr.strip(), 'arms': [], 'bada_rel': t is not None})
            elif kw in ('elif', 'else'):
                if not stack:
                    anomalies.append({'file': path, 'line': i + 1,
                                      'kind': 'orphan-else',
                                      'detail': '#%s with no open #if' % kw})
                    continue
                fr = stack[-1]
                fr['arms'].append((i + 1, kw))
                if kw == 'else':
                    if fr['truth'] is None:
                        pass
                    else:
                        fr['truth'] = not fr['truth']
                else:
                    t = _bada_truth('elif', expr)
                    if fr['bada_rel'] or t is not None:
                        # an #elif chain touching __bada__ is beyond this scan
                        anomalies.append({'file': path, 'line': i + 1,
                                          'kind': 'elif-chain',
                                          'detail': 'bada guard with #elif -- not modelled: %s'
                                                    % expr.strip()[:60]})
                    fr['truth'] = t
                    fr['bada_rel'] = fr['bada_rel'] or (t is not None)
            else:  # endif
                if not stack:
                    anomalies.append({'file': path, 'line': i + 1,
                                      'kind': 'orphan-endif',
                                      'detail': '#endif with no open #if'})
                    continue
                fr = stack.pop()
                if fr['bada_rel']:
                    fr['end'] = i + 1
                    blocks.append(fr)
            states[i] = ST_NEUTRAL   # the directive line itself
            continue

        # ordinary line: combine open frames
        st = ST_NEUTRAL
        for fr in stack:
            if fr['truth'] is True:
                st = ST_DEAD if st == ST_NOBADA else ST_BADA
            elif fr['truth'] is False:
                st = ST_DEAD if st == ST_BADA else ST_NOBADA
        states[i] = st

    for fr in stack:
        anomalies.append({'file': path, 'line': fr['start'], 'kind': 'unterminated-if',
                          'detail': '#%s %s never closed' % (fr['kw'], fr['expr'][:60])})

    # brace/paren balance inside every bada-relevant block -- an imbalanced one
    # spans a statement (e.g. an if/else), which this line-based scan cannot
    # model.  Report it rather than skip it silently.
    for b in blocks:
        seg = clean[b['start']:b.get('end', len(clean)) - 1]
        depth = 0
        par = 0
        for l in seg:
            if PP_RE.match(l):
                continue
            depth += l.count('{') - l.count('}')
            par += l.count('(') - l.count(')')
        if depth != 0 or par != 0:
            anomalies.append({
                'file': path, 'line': b['start'], 'kind': 'brace-spanning-guard',
                'detail': '#%s %s spans lines %d-%d with brace delta %+d, paren delta %+d'
                          % (b['kw'], b['expr'][:40], b['start'], b.get('end', -1), depth, par),
            })
    return states, clean, blocks, anomalies


# ---------------------------------------------------------------------------
# function attribution
# ---------------------------------------------------------------------------


def is_ctor_or_dtor(fn):
    """A write inside a constructor/destructor is exactly the 'value frozen at
    its constructor' that makes the bug invisible -- it must NOT count as a
    write that keeps the field live in the cross-build."""
    if not fn:
        return False
    parts = fn.split('::')
    last = parts[-1]
    if last.startswith('~'):
        return True
    if len(parts) >= 2 and last == parts[-2]:
        return True
    return False


CTRL_KEYWORDS = {'if', 'for', 'while', 'switch', 'catch', 'return', 'else',
                 'do', 'sizeof', 'case', 'new', 'delete', 'throw'}


def attribute_functions(clean):
    """Map line index -> enclosing function, best effort and brace based.

    Returns (owner, funcs) where funcs maps a key to
    {'name', 'def_line', 'first', 'last'}.  `def_line` is the SIGNATURE line, not
    the first body line: in a fully-guarded function the first body line is
    itself excluded, and testing that line would make the function look absent.
    Repeated names (overloads) get a `#2`, `#3` suffix so they do not merge.
    Namespace / class / extern-"C" braces are tracked so a definition nested in
    a namespace is still seen (Mortar's are).
    """
    owner = [None] * len(clean)
    funcs = {}
    depth = 0
    pending = None
    pending_line = None
    cur = None
    cur_open_depth = 0
    skip_head = re.compile(
        r'^(?:namespace|class|struct|union|enum|template|typedef|using|extern'
        r'|public|private|protected|friend|return|else|do)(?![A-Za-z_0-9])')
    for i, l in enumerate(clean):
        if PP_RE.match(l):
            continue
        st = l.strip()
        opens = l.count('{')
        closes = l.count('}')
        if cur is None:
            if st and not skip_head.match(st):
                m = re.search(r'((?:[A-Za-z_]\w*\s*::\s*)*~?[A-Za-z_]\w*)\s*\(', l)
                if m and m.group(1).split('::')[-1] not in CTRL_KEYWORDS:
                    pending = m.group(1).replace(' ', '')
                    pending_line = i
            if st.endswith(';'):
                pending = None
            if opens > 0 and pending is not None:
                key = pending
                n = 2
                while key in funcs:
                    key = '%s#%d' % (pending, n)
                    n += 1
                funcs[key] = {'name': pending, 'def_line': pending_line,
                              'first': i, 'last': i}
                owner[i] = key
                cur = key
                cur_open_depth = depth
                pending = None
            depth += opens - closes
            if depth < 0:
                depth = 0
            continue
        owner[i] = cur
        funcs[cur]['last'] = i
        depth += opens - closes
        if depth <= cur_open_depth:
            cur = None
            pending = None
    return owner, funcs


# ---------------------------------------------------------------------------
# symbol events
# ---------------------------------------------------------------------------

IDENT_RE = re.compile(r'(?:(->|\.)\s*)?\b([A-Za-z_]\w*)\b')


TYPE_NAMES = set()


def is_member_like(name, prefixed, member_names):
    if NAME_SKIP_RE.match(name):
        return False
    if name in TYPE_NAMES:
        return False
    if prefixed:
        return True
    if re.match(r'^(?:m_|s_|g_)\w+$', name):
        return True
    return name in member_names


PARAM_RE = re.compile(r'([A-Za-z_]\w*)\s*(?:\[\s*\])?\s*(?:,|\)|=)')


def collect_events(clean, states, owner, path, member_names, port_only_members,
                   funcs=None):
    """Yield (name, kind, line, func, state) for member-like reads and writes."""
    events = []
    locals_by_func = defaultdict(set)

    # crude local-declaration harvest so a guard-local temp is not mistaken for
    # a member.
    decl_re = re.compile(
        r'^\s*(?:const\s+|static\s+|volatile\s+)*'
        r'(?:[A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)\s*(?:<[^;{}]*>)?\s*[\*&]*\s*'
        r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^=]|;|\()')
    for key, info in (funcs or {}).items():
        d = info['def_line']
        sig = ' '.join(clean[d:min(d + 4, len(clean))])
        sig = sig[sig.find('('):] if '(' in sig else ''
        for mm in PARAM_RE.finditer(sig.split(')')[0] + ')'):
            locals_by_func[key].add(mm.group(1))

    for i, l in enumerate(clean):
        if PP_RE.match(l):
            continue
        m = decl_re.match(l)
        if m:
            nm = m.group(1)
            head = l.strip().split()[0] if l.strip() else ''
            if head in DECL_KEYWORDS or re.match(r'^[A-Z]', head) or head.endswith('_t'):
                locals_by_func[owner[i]].add(nm)

    for i, l in enumerate(clean):
        if PP_RE.match(l) or not l.strip():
            continue
        st = states[i]
        fn = owner[i]
        loc = locals_by_func.get(fn, ())
        for m in IDENT_RE.finditer(l):
            arrow, name = m.group(1), m.group(2)
            prefixed = arrow is not None
            if not is_member_like(name, prefixed, member_names):
                continue
            if name in port_only_members:
                continue
            if not prefixed and name in loc:
                continue
            end = m.end()
            rest = l[end:]
            if re.match(r'\s*\(', rest):          # a call, or a ctor-init entry
                continue
            kind = 'read'
            # `&m_TexId` handed to glGenTextures etc IS a store site.
            if re.search(r'&\s*$', l[:m.start(2)]) and not re.search(r'&&\s*$', l[:m.start(2)]):
                kind = 'write'
            else:
                # allow a field/subscript chain before the operator, so
                # `pos.x = v` and `a->b[i] = v` count as writes to `pos` / `a`.
                tail = re.match(
                    r'\s*(?:\.\s*[A-Za-z_]\w*|\[[^\]]*\])*\s*'
                    r'(\+\+|--|(?:\+|-|\*|/|%|\||&|\^|<<|>>)?=)(?!=)', rest)
                if tail:
                    op = tail.group(1)
                    kind = 'write'
                    if op not in ('++', '--', '=') and len(op) >= 2:
                        kind = 'rmw'      # += etc: both read and write
            # a declaration line writing an unprefixed name is a local
            if kind != 'read' and not prefixed and name in loc:
                continue
            events.append({'name': name, 'kind': kind, 'line': i + 1,
                           'func': fn, 'state': st, 'text': l.strip()[:160]})
    return events


# ---------------------------------------------------------------------------
# header harvesting
# ---------------------------------------------------------------------------

MEMBER_DECL_RE = re.compile(
    r'^\s*(?:mutable\s+|static\s+|const\s+|volatile\s+)*'
    r'[A-Za-z_][\w:<>,\s\*&]*?[\s\*&]'
    r'([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^=;]*)?;\s*$')


TYPE_DECL_RE = re.compile(r'\b(?:class|struct|enum|union)\s+([A-Za-z_]\w*)')
SCOPE_RE = re.compile(r'\b([A-Za-z_]\w*)\s*::')


def harvest_types(root, files):
    """Class / struct / enum / namespace names, so a forward declaration is not
    mistaken for a data member."""
    for rel in files:
        try:
            lines = read_lines(src_path(root, rel))
        except Exception:
            continue
        for l in lines:
            for m in TYPE_DECL_RE.finditer(l):
                TYPE_NAMES.add(m.group(1))
            for m in SCOPE_RE.finditer(l):
                TYPE_NAMES.add(m.group(1))


def harvest_members(root, files):
    """Return (all member names, members declared ONLY inside a !__bada__ guard)."""
    members = set()
    port_only = set()
    asserted = set()
    for rel in files:
        if not rel.endswith(('.h', '.hpp', '.inl')):
            continue
        path = src_path(root, rel)
        try:
            lines = read_lines(path)
        except Exception:
            continue
        states, clean, _blocks, _anom = scan_preprocessor(lines, rel)
        for i, l in enumerate(clean):
            if PP_RE.match(l):
                continue
            if 'offsetof' in l or 'static_assert' in l:
                for m in re.finditer(r',\s*([A-Za-z_]\w*)\s*\)', l):
                    asserted.add(m.group(1))
                continue
            if '(' in l:
                continue
            head = l.strip().split(' ')[0]
            if head in ('class', 'struct', 'enum', 'union', 'typedef', 'using',
                        'friend', 'return', 'template', 'namespace'):
                continue
            m = MEMBER_DECL_RE.match(l)
            if not m:
                continue
            nm = m.group(1)
            if NAME_SKIP_RE.match(nm):
                continue
            members.add(nm)
            if states[i] == ST_NOBADA:
                port_only.add(nm)
    port_only -= asserted
    return members, port_only, asserted


# ---------------------------------------------------------------------------
# block benignity
# ---------------------------------------------------------------------------

def effective_statements(clean, states, lo, hi, want_state):
    """Non-trivial statement lines in [lo,hi) whose state == want_state."""
    out = []
    for i in range(lo, min(hi, len(clean))):
        if PP_RE.match(clean[i]):
            continue
        if states[i] != want_state:
            continue
        s = clean[i].strip()
        if not s or TRIVIAL_STMT_RE.match(s):
            continue
        out.append((i + 1, s))
    return out


def is_benign(stmts):
    if not stmts:
        return True
    for _ln, s in stmts:
        if not any(r.search(s) for r in BENIGN_STMT_RES):
            return False
    return True


# ---------------------------------------------------------------------------
# binary symbol table
# ---------------------------------------------------------------------------

BIN_NAMES = set()


def load_binary_symbol_names(path):
    """Short (unqualified) names of every function symbol in the binary, from
    tools/asm-verify/export-binary-symbols.py's output.  This is the only
    principled way to tell 'the cross-build lost a REAL binary symbol' from
    'a port-only helper is legitimately absent'."""
    if not path or not os.path.exists(path):
        return False
    try:
        with open(path, encoding='utf-8', errors='replace') as fh:
            data = json.load(fh)
    except Exception:
        return False
    for e in data:
        m = e.get('mangled') or ''
        # walk the length-prefixed components; re.finditer would swallow the
        # whole tail in one match and lose every nested name after the first.
        i = 0
        while i < len(m):
            if not m[i].isdigit():
                i += 1
                continue
            j = i
            while j < len(m) and m[j].isdigit():
                j += 1
            n = int(m[i:j])
            if 0 < n <= len(m) - j:
                BIN_NAMES.add(m[j:j + n])
                i = j + n
            else:
                i = j
    return True


def in_binary(func_name):
    """True if the binary exports this function.  For a qualified name the CLASS
    must be a binary class too -- `GetInstance` alone matches dozens of unrelated
    singletons, so `SettingsScreen::GetInstance` (a port-only screen) would score
    as real on the method name alone."""
    if not BIN_NAMES:
        return None                     # unknown -- no symbol table loaded
    parts = func_name.split('::')
    if parts[-1].lstrip('~') not in BIN_NAMES:
        return False
    if len(parts) >= 2 and parts[-2] not in BIN_NAMES:
        return False
    return True


def rank_removal(real, residue, func_name):
    """Rank a removed/gutted body.  A body only matters if the BINARY has the
    symbol; and it matters most when the removed code touches state the binary
    also has (residue), rather than port-only bookkeeping.  Returns None to drop
    the finding entirely."""
    if real is False:
        return 'LOW'                       # port-only helper: absence is by design
    if residue:
        return 'HIGH'
    if is_ctor_or_dtor(func_name):
        return None                        # port-only registry in a ctor/dtor
    return 'MED'


# ---------------------------------------------------------------------------
# source detector
# ---------------------------------------------------------------------------

def list_sources(root):
    """Relative paths, prefixed with the root's own directory name so reported
    paths read as `src/screens/MainScreen.cpp` and can be pasted straight into
    an editor."""
    prefix = os.path.basename(os.path.normpath(root))
    out = []
    for dp, dn, fn in os.walk(root):
        dn[:] = [d for d in dn if d not in ('.git', 'build', 'tmp')]
        for f in fn:
            if f.endswith(SRC_EXTS):
                rel = os.path.relpath(os.path.join(dp, f), root).replace(os.sep, '/')
                out.append(prefix + '/' + rel if prefix else rel)
    out.sort()
    return out


def src_path(root, rel):
    prefix = os.path.basename(os.path.normpath(root))
    if prefix and rel.startswith(prefix + '/'):
        rel = rel[len(prefix) + 1:]
    return os.path.join(root, rel.replace('/', os.sep))


def scan_source(root, binary_symbols=None):
    files = list_sources(root)
    have_syms = load_binary_symbol_names(binary_symbols)
    harvest_types(root, files)
    member_names, port_only, asserted = harvest_members(root, files)
    member_names -= TYPE_NAMES

    findings = []
    anomalies = []
    blocks_total = 0
    blocks_benign = 0
    file_states = {}
    writes_elsewhere = defaultdict(list)   # name -> [(file, line)] kept writes

    per_file = {}
    for rel in files:
        if FILE_SKIP_RE.search(rel):
            continue
        path = src_path(root, rel)
        try:
            lines = read_lines(path)
        except Exception:
            continue
        if '__bada__' not in '\n'.join(lines):
            continue
        states, clean, blocks, anom = scan_preprocessor(lines, rel)
        owner, funcs = attribute_functions(clean)
        events = collect_events(clean, states, owner, rel, member_names, port_only,
                                funcs)
        per_file[rel] = (lines, states, clean, blocks, owner, events, funcs)
        anomalies.extend(anom)
        blocks_total += len(blocks)
        for ev in events:
            if (ev['kind'] in ('write', 'rmw') and ev['state'] in (ST_NEUTRAL, ST_BADA)
                    and not is_ctor_or_dtor(ev['func'])):
                writes_elsewhere[ev['name']].append((rel, ev['line']))

    for rel, (lines, states, clean, blocks, owner, events, funcs) in per_file.items():
        # ---- benign-block masking -------------------------------------------
        masked = set()
        for b in blocks:
            lo, hi = b['start'], b.get('end', len(clean))
            # BOTH arms matter: `#if !defined(__bada__) ... #else ... #endif`
            # has a removed arm and a kept arm inside the same block.
            stmts = (effective_statements(clean, states, lo, hi, ST_NOBADA)
                     + effective_statements(clean, states, lo, hi, ST_BADA))
            if is_benign(stmts):
                blocks_benign += 1
                for i in range(lo, min(hi, len(clean))):
                    masked.add(i + 1)
        live = [e for e in events if e['line'] not in masked]

        by_name = defaultdict(list)
        for e in live:
            by_name[e['name']].append(e)

        # A symbol never touched by any cross-build-compiled line is a purely
        # port-side facility (a debug registry, an SDL cache).  Its removal is
        # by design, so it must not seed R2/R3.
        port_only_local = set(
            n for n, evs in by_name.items()
            if all(e['state'] == ST_NOBADA for e in evs))

        # ---- R1: write removed, read kept -----------------------------------
        for name, evs in by_name.items():
            w_removed = [e for e in evs if e['kind'] in ('write', 'rmw') and e['state'] == ST_NOBADA]
            # A store inside a ctor/dtor does NOT keep the field live: "frozen at
            # its constructor" IS the bug, so those stores are not kept-writes.
            w_kept = [e for e in evs if e['kind'] in ('write', 'rmw')
                      and e['state'] in (ST_NEUTRAL, ST_BADA)
                      and not is_ctor_or_dtor(e['func'])]
            r_kept = [e for e in evs if e['kind'] in ('read', 'rmw')
                      and e['state'] in (ST_NEUTRAL, ST_BADA)
                      and not is_ctor_or_dtor(e['func'])]
            if not w_removed or w_kept or not r_kept:
                continue
            other = [x for x in writes_elsewhere.get(name, []) if x[0] != rel]
            rank = 'MED'
            if name in asserted:
                rank = 'HIGH'          # a real binary field with an offset assert
            if other:
                rank = 'LOW' if rank == 'MED' else 'MED'
            findings.append({
                'rule': 'R1-write-removed-read-kept',
                'rank': rank,
                'file': rel,
                'symbol': name,
                'binary_field_asserted': name in asserted,
                'writes_removed': [{'line': e['line'], 'func': e['func'], 'text': e['text']}
                                   for e in w_removed[:6]],
                'reads_kept': [{'line': e['line'], 'func': e['func'], 'text': e['text']}
                               for e in r_kept[:6]],
                'kept_writes_in_other_files': [{'file': f, 'line': l} for f, l in other[:6]],
                'detail': '%s written only inside cross-build-excluded blocks, read outside'
                          % name,
            })

        # ---- R2 / R3: gutted bodies and no-op else arms ----------------------
        def drop_port_only(stmts):
            """Discard statements that only ever touch port-side-only symbols."""
            keep = []
            for ln, s in stmts:
                if any(r.search(s) for r in BENIGN_STMT_RES):
                    continue
                names = set()
                for mm in IDENT_RE.finditer(s):
                    if re.match(r'\s*\(', s[mm.end():]):
                        continue          # a call, not state
                    names.add(mm.group(2))
                tracked = set(n for n in names
                              if is_member_like(n, False, member_names))
                if tracked and tracked <= (port_only_local | port_only):
                    continue
                keep.append((ln, s))
            return keep

        for fn, info in funcs.items():
            # start AFTER the signature line: it is neutral by construction and
            # would otherwise count as surviving body code.
            lo, hi = info['def_line'] + 1, info['last'] + 1
            if states[info['def_line']] == ST_NOBADA:
                # R4: the DEFINITION itself is excluded, so the symbol is absent
                # from the cross-build.  asm-verify only diffs symbols present on
                # BOTH sides, so an absent one is never diffed -- it goes green by
                # silence.  A nearby v1.6.1/@0x marker means it IS a binary symbol.
                head = ' '.join(lines[max(0, info['def_line'] - 8):info['def_line'] + 1])
                real = in_binary(info['name'])
                marked = (real is True) if real is not None else (
                    re.search(r'@\s*0x[0-9a-fA-F]{6,8}|v1\.6\.1', head) is not None)
                body = effective_statements(clean, states, lo, hi, ST_NOBADA)
                body = [x for x in body if x[0] not in masked]
                if not body or is_benign(body):
                    continue
                findings.append({
                    'rule': 'R4-definition-removed',
                    'rank': 'HIGH' if marked else 'LOW',
                    'file': rel,
                    'symbol': info['name'],
                    'binary_field_asserted': False,
                    'writes_removed': [{'line': l, 'func': info['name'], 'text': t}
                                       for l, t in body[:6]],
                    'reads_kept': [],
                    'kept_writes_in_other_files': [],
                    'in_binary': real,
                    'detail': '%s is not compiled under __bada__ at all (line %d); '
                              'the symbol cannot pair in asm-verify%s'
                              % (info['name'], info['def_line'] + 1,
                                 ' -- and the binary DOES export this name'
                                 if marked else ' (no binary symbol of this name'
                                 ' -- probably a port-only helper)'),
                })
                continue
            kept = effective_statements(clean, states, lo, hi, ST_NEUTRAL)
            kept += effective_statements(clean, states, lo, hi, ST_BADA)
            removed = effective_statements(clean, states, lo, hi, ST_NOBADA)
            removed = [(l, s) for l, s in removed if l not in masked]
            residue = drop_port_only(removed)
            if removed and not kept and not is_benign(removed):
                real2 = in_binary(info['name'])
                r2rank = rank_removal(real2, residue, info['name'])
                if r2rank is None:
                    continue
                findings.append({
                    'rule': 'R2-gutted-body',
                    'rank': r2rank,
                    'in_binary': real2,
                    'file': rel,
                    'symbol': info['name'],
                    'binary_field_asserted': False,
                    'writes_removed': [{'line': l, 'func': info['name'], 'text': t}
                                       for l, t in removed[:8]],
                    'reads_kept': [],
                    'kept_writes_in_other_files': [],
                    'port_only_residue_empty': not residue,
                    'detail': '%s compiles to an empty body under __bada__ (%d statements '
                              'removed)%s' % (info['name'], len(removed),
                                              '; every removed statement touches port-only '
                                              'state, so the binary implements this some '
                                              'other way -- verify' if not residue else ''),
                })

        r2_funcs = set(f['symbol'] for f in findings
                       if f['rule'] == 'R2-gutted-body' and f['file'] == rel)

        # ---- R5: a guard removes the construction of a REAL binary object ----
        # `#ifndef __bada__ { new GameModeScreen(...); mHud->AddControl(...) }`
        # inside a live binary function: the cross-build then verifies a Update()
        # that never spawns the child screen the binary spawns.
        for b in blocks:
            if b.get('truth0') is not False:
                continue
            lo, hi = b['start'], b.get('end', len(clean))
            stmts = effective_statements(clean, states, lo, hi, ST_NOBADA)
            stmts = [x for x in stmts if x[0] not in masked]
            news = []
            for ln, t in stmts:
                for mm in re.finditer(r'(?<![A-Za-z_])new\s+([A-Za-z_]\w*)', t):
                    if in_binary(mm.group(1)) and mm.group(1) not in ('char', 'float'):
                        news.append((ln, t, mm.group(1)))
            if not news:
                continue
            key = owner[b['start']] if b['start'] < len(owner) else None
            fname = funcs[key]['name'] if key in funcs else (key or '')
            if not fname or fname in r2_funcs or in_binary(fname) is False:
                continue
            findings.append({
                'rule': 'R5-binary-object-not-constructed',
                'rank': 'HIGH',
                'file': rel,
                'symbol': fname,
                'binary_field_asserted': False,
                'in_binary': in_binary(fname),
                'writes_removed': [{'line': ln, 'func': fname, 'text': t}
                                   for ln, t, _c in news[:6]],
                'reads_kept': [],
                'kept_writes_in_other_files': [],
                'detail': '%s: lines %d-%d drop `new %s` -- the cross-build verifies a '
                          'body that never constructs an object the binary does'
                          % (fname, b['start'], hi, news[0][2]),
            })
        for b in blocks:
            # truth0, not truth: an #else flips `truth` in place, so a
            # `#if !defined(__bada__) ... #else` block ends up recorded as True.
            if b.get('truth0') is not False or not b['arms']:
                continue
            else_ln = None
            for ln, kw in b['arms']:
                if kw == 'else':
                    else_ln = ln
            if else_ln is None:
                continue
            end = b.get('end', len(clean))
            then_stmts = effective_statements(clean, states, b['start'], else_ln, ST_NOBADA)
            else_stmts = effective_statements(clean, states, else_ln, end, ST_BADA)
            if not then_stmts or is_benign(then_stmts):
                continue
            residue3 = drop_port_only(then_stmts)
            if else_stmts:
                continue
            fn = owner[b['start']] if b['start'] < len(owner) else None
            fname = funcs[fn]['name'] if fn in funcs else (fn or '')
            r3real = in_binary(fname) if fname else None
            r3rank = rank_removal(r3real, residue3, fname)
            if r3rank is None or fname in r2_funcs:
                continue                   # R2 already reports the whole body
            findings.append({
                'rule': 'R3-noop-else-arm',
                'rank': r3rank,
                'in_binary': r3real,
                'port_only_residue_empty': not residue3,
                'file': rel,
                'symbol': fname or ('%s:%d' % (rel, b['start'])),
                'binary_field_asserted': False,
                'writes_removed': [{'line': l, 'func': fn, 'text': s} for l, s in then_stmts[:8]],
                'reads_kept': [],
                'kept_writes_in_other_files': [],
                'detail': 'lines %d-%d: the __bada__ arm is a no-op while the port arm '
                          'has %d statement(s)' % (b['start'], end, len(then_stmts)),
            })

    rank_order = {'HIGH': 0, 'MED': 1, 'LOW': 2}
    findings.sort(key=lambda f: (rank_order[f['rank']], f['rule'], f['file'], f['symbol']))
    stats = {
        'files_scanned': len(per_file),
        'bada_blocks': blocks_total,
        'bada_blocks_benign_masked': blocks_benign,
        'members_harvested': len(member_names),
        'port_only_members': len(port_only),
        'offset_asserted_fields': len(asserted),
        'binary_symbol_names': len(BIN_NAMES),
        'binary_symbols_loaded': bool(have_syms),
    }
    return findings, anomalies, stats


# ---------------------------------------------------------------------------
# report detector  (promoted from tmp/shim-audit/detect.py)
# ---------------------------------------------------------------------------

# causes that legitimately explain a tiny port body -- keeping them out of the
# ranking is what stops the real signal drowning (98 raw -> ~10 reviewable).
BENIGN_CAUSES = ('port-stub', 'port-stub-defunct')


def cls_of(mangled):
    m = re.match(r'^_ZN(?:K)?(\d+)([A-Za-z_0-9]+)', mangled)
    if m:
        return m.group(2)[:int(m.group(1))]
    m = re.match(r'^_ZN6Mortar(\d+)([A-Za-z_0-9]+)', mangled)
    if m:
        return m.group(2)[:int(m.group(1))]
    m = re.match(r'^_GLOBAL__I_(.+)\.cpp$', mangled)
    if m:
        return m.group(1)
    return None


def scan_report(report_path, root, source_findings, min_bin=20, max_ratio=0.10):
    if not os.path.exists(report_path):
        return [], {'error': 'report not found: %s' % report_path}
    with open(report_path, encoding='utf-8', errors='replace') as fh:
        data = json.load(fh)
    syms = data.get('symbols', [])

    # class -> candidate source files, from the whole tree (the prototype only
    # knew files that already had a bada block, which hid classes).
    cls2file = defaultdict(set)
    for rel in list_sources(root):
        base = rel.rsplit('/', 1)[-1].rsplit('.', 1)[0]
        cls2file[base].add(rel)

    flagged_files = set(f['file'] for f in source_findings)

    out = []
    for s in syms:
        diff = s.get('diff') or []
        if not diff:
            continue
        common = sum(1 for l in diff if l.startswith('  '))
        plus = sum(1 for l in diff if l.startswith('+'))
        minus = sum(1 for l in diff if l.startswith('-'))
        port = common + plus
        binn = s.get('max_score')
        try:
            binn = int(binn)
        except (TypeError, ValueError):
            binn = common + minus
        if binn < min_bin or port > max(6, binn * max_ratio):
            continue
        c = cls_of(s['mangled'])
        files = sorted(cls2file.get(c, set())) if c else []
        cause = s.get('cause')
        corroborated = any(f in flagged_files for f in files)
        # The cause cross-reference comes FIRST: a port-stub row stays noise even
        # when its file also has a guard finding, otherwise the 57 stub rows drown
        # the handful that matter.
        if cause in BENIGN_CAUSES:
            rank = 'NOISE'
        elif corroborated:
            rank = 'HIGH'
        else:
            rank = 'MED'
        out.append({
            'mangled': s['mangled'], 'addr': s.get('addr'), 'cls': c,
            'files': files, 'bin_instrs': binn, 'port_instrs': port,
            'ratio': round(port / float(binn), 4),
            'verdict': s.get('verdict'), 'cause': cause,
            'likelihood': s.get('likelihood'),
            'source_detector_corroborates': corroborated,
            'rank': rank,
        })
    rank_order = {'HIGH': 0, 'MED': 1, 'NOISE': 2}
    out.sort(key=lambda r: (rank_order[r['rank']], -r['bin_instrs']))
    stats = {
        'symbols_in_report': len(syms),
        'raw_candidates': len(out),
        'high': sum(1 for r in out if r['rank'] == 'HIGH'),
        'med': sum(1 for r in out if r['rank'] == 'MED'),
        'noise_benign_cause': sum(1 for r in out if r['rank'] == 'NOISE'),
        'cause_histogram': dict(Counter(r['cause'] for r in out).most_common()),
    }
    return out, stats


# ---------------------------------------------------------------------------
# rendering
# ---------------------------------------------------------------------------

def render(findings, anomalies, src_stats, rep, rep_stats, top, min_rank):
    order = {'HIGH': 0, 'MED': 1, 'LOW': 2, 'NOISE': 3}
    cut = order.get(min_rank.upper(), 2)

    print('')
    print('# gutted __bada__ body detector')
    print('')
    print('## (1) source scan -- %d files, %d bada blocks (%d masked as benign)'
          % (src_stats.get('files_scanned', 0), src_stats.get('bada_blocks', 0),
             src_stats.get('bada_blocks_benign_masked', 0)))
    print('')
    shown = [f for f in findings if order[f['rank']] <= cut]
    if not shown:
        print('  (no findings at rank >= %s)' % min_rank.upper())
    counts = Counter(f['rank'] for f in findings)
    print('  findings: HIGH=%d MED=%d LOW=%d   (showing %d)'
          % (counts['HIGH'], counts['MED'], counts['LOW'], min(len(shown), top)))
    print('')
    for f in shown[:top]:
        star = ' [binary field, offset-asserted]' if f.get('binary_field_asserted') else ''
        print('  %-4s %-28s %s :: %s%s' % (f['rank'], f['rule'].split('-', 1)[0] + ' ' + f['rule'].split('-', 1)[1][:22],
                                           f['file'], f['symbol'], star))
        print('       %s' % f['detail'])
        for w in f['writes_removed'][:2]:
            print('       - removed  %s:%s  %s' % (f['file'], w['line'], w['text'][:90]))
        for r in f['reads_kept'][:2]:
            print('       + kept     %s:%s  %s' % (f['file'], r['line'], r['text'][:90]))
        if f['kept_writes_in_other_files']:
            o = f['kept_writes_in_other_files'][0]
            print('       ? also written at %s:%s (cross-TU -- may be benign)' % (o['file'], o['line']))
        print('')

    print('## parse anomalies -- %d (guards this scan could NOT model; review by hand)'
          % len(anomalies))
    print('')
    for a in anomalies[:top]:
        print('  %-22s %s:%s  %s' % (a['kind'], a['file'], a['line'], a['detail'][:90]))
    if len(anomalies) > top:
        print('  ... %d more (see JSON)' % (len(anomalies) - top))
    print('')

    if not rep_stats:
        return
    if rep_stats.get('error'):
        print('## (2) report scan -- SKIPPED (%s)' % rep_stats['error'])
        print('')
        return
    print('## (2) report scan -- %d raw candidates from %d symbols'
          % (rep_stats.get('raw_candidates', 0), rep_stats.get('symbols_in_report', 0)))
    print('   HIGH=%d (corroborated by the source scan)  MED=%d  NOISE=%d (port-stub causes)'
          % (rep_stats.get('high', 0), rep_stats.get('med', 0), rep_stats.get('noise_benign_cause', 0)))
    print('')
    for r in rep[:top]:
        if r['rank'] == 'NOISE':
            continue
        print('  %-4s %-52s bin=%4d port=%3d ratio=%.3f cause=%s'
              % (r['rank'], r['mangled'][:52], r['bin_instrs'], r['port_instrs'],
                 r['ratio'], r['cause']))
        if r['files']:
            print('       %s' % ', '.join(r['files'][:3]))
    print('')


# ---------------------------------------------------------------------------

def export_rev(rev, root):
    """git-archive src/ from a revision into a temp dir; returns (dir, cleanup)."""
    tmpd = tempfile.mkdtemp(prefix='gutted-bada-')
    tar = subprocess.check_output(['git', 'archive', rev, 'src'], cwd=root)
    p = subprocess.Popen(['tar', '-x', '-C', tmpd], stdin=subprocess.PIPE)
    p.communicate(tar)
    if p.returncode != 0:
        raise RuntimeError('tar extract failed for %s' % rev)
    return os.path.join(tmpd, 'src'), tmpd


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    proj = os.path.abspath(os.path.join(here, '..', '..'))

    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--mode', choices=('source', 'report', 'both'), default='both')
    ap.add_argument('--src', default=os.path.join(proj, 'src'))
    ap.add_argument('--git-rev', default=None,
                    help='scan src/ as of this git revision instead of the worktree')
    ap.add_argument('--binary-symbols',
                    default=os.path.join(proj, 'tmp', 'asm-verify', 'binary-func-symbols.json'),
                    help='export-binary-symbols.py output; used to tell a lost BINARY '
                         'symbol from a legitimately absent port-only helper')
    ap.add_argument('--report-json', default=os.path.join(proj, 'tmp', 'asm-verify', 'report.json'))
    ap.add_argument('--out-dir', default=os.path.join(proj, 'tmp', 'gutted-bada'))
    ap.add_argument('--out-name', default=None,
                    help='basename for the JSON (default findings.json, or findings-<rev>.json)')
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('--min-rank', default='MED', choices=('HIGH', 'MED', 'LOW'))
    ap.add_argument('--quiet', action='store_true', help='JSON only, no stdout summary')
    args = ap.parse_args()

    cleanup = None
    src_root = args.src
    if args.git_rev:
        src_root, cleanup = export_rev(args.git_rev, proj)

    try:
        findings, anomalies, src_stats = ([], [], {})
        if args.mode in ('source', 'both'):
            findings, anomalies, src_stats = scan_source(src_root, args.binary_symbols)
        rep, rep_stats = ([], {})
        if args.mode in ('report', 'both'):
            rep, rep_stats = scan_report(args.report_json, src_root, findings)

        os.makedirs(args.out_dir, exist_ok=True)
        name = args.out_name or ('findings.json' if not args.git_rev
                                 else 'findings-%s.json' % re.sub(r'\W+', '_', args.git_rev))
        out_path = os.path.join(args.out_dir, name)
        with open(out_path, 'w', encoding='utf-8') as fh:
            json.dump({
                'src_root': src_root if not args.git_rev else 'git:%s:src' % args.git_rev,
                'mode': args.mode,
                'source_stats': src_stats,
                'source_findings': findings,
                'parse_anomalies': anomalies,
                'report_stats': rep_stats,
                'report_candidates': rep,
            }, fh, indent=1)

        if not args.quiet:
            render(findings, anomalies, src_stats, rep, rep_stats, args.top, args.min_rank)
            print('json: %s' % out_path.replace('\\', '/'))
    finally:
        if cleanup:
            shutil.rmtree(cleanup, ignore_errors=True)

    return 0


if __name__ == '__main__':
    sys.exit(main())
