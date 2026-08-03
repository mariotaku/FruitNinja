#!/usr/bin/env python3
"""Check '// +0xNN' offset labels in src/ against the static_assert(offsetof(...))
ground truth.

Why this exists
---------------
stale-marker-lint.py validates the '@0x<addr>' in ASM markers against the symbol
table, but it is blind to the OTHER kind of binary address baked into this port:
the '// +0xNN' field-offset labels on struct members and field writes. Those
carry the v1.5.1 -> v1.6.1 drift.

That drift is not hypothetical. GameWork gained `rawDt` at +0x3C in v1.6.1, which
shifted every later field by 4; SetupGameWork.cpp's labels still read the v1.5.1
values. The code was CORRECT (it writes named fields, not raw offsets), but the
stale labels read as a divergence list and nearly triggered a "fix" to working
code. A label that lies is worse than no label: it survives review because it
looks like evidence.

Ground truth
------------
`static_assert(offsetof(Class, field) == 0xNN)` is enforced by the __bada__
32-bit build, so it cannot silently rot the way a comment can. A '// +0xNN'
label contradicting one is stale by definition.

False positives are the whole design problem
--------------------------------------------
A noisy version of this lint costs exactly what the original bug cost: someone
"fixing" correct code. So every rule below is deliberately conservative --

 * A line may carry SEVERAL labels ("// this+0xf0 -> mgr+0x08"). Each is checked
   against every field named on the line; the line is clean if each label matches
   something. Reporting the first label against the first identifier is wrong.
 * `m_pHostFruit->m_SliceTimer = 1.0f;  // Fruit+0xBC` labels the POINTEE's field,
   not the pointer's. Binding to the first identifier scanned gets this backwards.
 * `fruit->m_Gravity.z  // [fruit+0xa8]` is m_Gravity(+0xA0) plus the .z component.
   Component suffixes are resolved exactly (x=+0, y=+4, z=+8, w=+12) rather than
   waved through with a tolerance window, which would hide a real +4 drift.
 * Two classes may both have `m_Items` while only one is pinned by an assert. In
   headers the enclosing class is tracked by brace depth and only an exact
   (class, field) hit is compared. In .cpp the pointee type is unknown, so the
   rule is "matches SOME pinned field" -- weaker, but it never invents a verdict.

Anything not provable is skipped and counted, never guessed at.

Exit 1 if any mismatch is found; 0 otherwise.
"""

import os
import re
import sys
from collections import defaultdict

SRC = os.path.normpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "src"))

RE_ASSERT = re.compile(
    r"offsetof\s*\(\s*([A-Za-z_][A-Za-z0-9_:]*)\s*,\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)"
    r"\s*==\s*(0[xX][0-9A-Fa-f]+|\d+)")

# Every '+0xNN' inside the trailing comment, not just the first.
RE_LABEL = re.compile(r"\+\s*(0[xX][0-9A-Fa-f]+)")

RE_DECL = re.compile(
    r"^\s*(?:(?:static|mutable|const|volatile|unsigned|signed|struct|class)\s+)*"
    r"[A-Za-z_][A-Za-z0-9_:<>,\s\*&]*?"
    r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*;")

RE_CLASS_OPEN = re.compile(r"^\s*(?:class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)\b[^;]*$")
RE_IDENT = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)\b")

COMPONENT = {"x": 0, "y": 4, "z": 8, "w": 12}


def parse_int(tok):
    return int(tok, 16) if tok.lower().startswith("0x") else int(tok)


def ascii_only(s):
    return s.encode("ascii", "replace").decode("ascii")


def walk_sources():
    for root, _dirs, files in os.walk(SRC):
        for fn in sorted(files):
            if fn.endswith((".h", ".hpp", ".cpp", ".cc")):
                yield os.path.join(root, fn)


RE_IF_BADA = re.compile(r"^\s*#\s*if(?:def)?\s+.*\b__bada__")
RE_IFN_BADA = re.compile(r"^\s*#\s*(?:ifndef\s+__bada__|if\s+!\s*defined\s*\(\s*__bada__)")
RE_IF_ANY = re.compile(r"^\s*#\s*if")
RE_ELSE = re.compile(r"^\s*#\s*el(?:se|if)\b")
RE_ENDIF = re.compile(r"^\s*#\s*endif\b")


def build_truth():
    """(class, field) -> BINARY offset, and field -> {class, ...}.

    Only harvests asserts that describe the BINARY layout. Headers here carry two
    assert blocks per class:

        #ifdef __bada__
        static_assert(offsetof(Entity, pos) == 0x10);   // binary
        #else
        static_assert(offsetof(Entity, pos) == 0x14);   // desktop x64: 8-byte
        #endif                                          // vtable ptr widens it

    Harvesting both let the #else win by dict-overwrite, so the tool compared
    correct binary labels against x64 host offsets and reported a tidy, entirely
    bogus "-4 drift" across Entity's core fields -- the exact false-positive class
    this lint exists to avoid, and indistinguishable at a glance from the real
    v1.5.1 drift. Only __bada__-true regions are ground truth for a '// +0xNN'.
    """
    exact = {}
    owners = defaultdict(set)
    sites = 0
    for path in walk_sources():
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                lines = fh.readlines()
        except IOError:
            continue
        stack = []   # True = __bada__ region, False = !__bada__ region, None = other
        for line in lines:
            if RE_ENDIF.match(line):
                if stack:
                    stack.pop()
                continue
            if RE_ELSE.match(line):
                if stack and stack[-1] is not None:
                    stack[-1] = not stack[-1]
                continue
            if RE_IF_ANY.match(line):
                if RE_IFN_BADA.match(line):
                    stack.append(False)
                elif RE_IF_BADA.match(line):
                    stack.append(True)
                else:
                    stack.append(None)
                continue
            if False in stack:      # host-only layout; never binary ground truth
                continue
            for m in RE_ASSERT.finditer(line):
                cls, field, off = m.group(1), m.group(2), parse_int(m.group(3))
                cls = cls.split("::")[-1]
                exact[(cls, field)] = off
                owners[field].add(cls)
                sites += 1
    return exact, owners, sites


def split_code_comment(line):
    i = line.find("//")
    if i < 0:
        return None, None
    return line[:i], line[i + 2:]


def allowed_offsets(code, field, base):
    """Offsets a label may legitimately carry for `field` based at `base`."""
    out = set([base])
    # field.x / field.y / field.z / field.w -> exact component offset
    for m in re.finditer(re.escape(field) + r"\s*\.\s*([xyzw])\b", code):
        out.add(base + COMPONENT[m.group(1)])
    return out


def candidates(code, comment, is_header, enclosing, exact, owners):
    """Fields on this line whose offset the label can be ATTRIBUTED to.

    Conservative on purpose. A label names a field that is often not the first
    identifier on the line, and sometimes not on the line at all --
    `m_pLinkedSlasher->ClearHeadPosX();  // SlashEntity+0x7c` labels SlashEntity's
    m_HeadPos, which appears nowhere in the statement. Guessing there produced a
    confident, wrong "stale label" report. So a field qualifies only when the
    label is genuinely about it: it is the member being declared, it is the
    assignment target, or the comment names it outright.
    """
    out = {}

    def pin(cls, f):
        if cls is not None:
            if (cls, f) in exact:
                out[f] = exact[(cls, f)]
            return
        # Unqualified: trust the name only when exactly one class pins it.
        if f in owners and len(owners[f]) == 1:
            only = list(owners[f])[0]
            out[f] = exact[(only, f)]

    if is_header:
        dm = RE_DECL.match(code)
        if dm and enclosing is not None:
            # Header member decl: bind strictly to the enclosing class. No
            # by-name fallback -- WaveQue::m_Items and ScrollingMenu::m_Items are
            # different fields, and the fallback happily compared one to the other.
            f = dm.group(1)
            if (enclosing, f) in exact:
                out[f] = exact[(enclosing, f)]
        return out

    # A comment may bind its label to a field explicitly -- "m_Texture=+0x74",
    # "scale (Entity+0x28)", "pos = HUDControl::pos (binary this+0x8)". When it
    # does, that field owns the label even if the statement assigns another one:
    # `m_RestScale.x = m_Texture->GetWidth();  // m_Texture=+0x74` is a fact about
    # m_Texture, and reading it as a claim about m_RestScale is how this lint
    # invented a "-200 drift".
    named = re.search(r"\b([A-Za-z_][A-Za-z0-9_]*)\s*(?:=|@)?\s*[\(]?"
                      r"(?:[A-Za-z_][A-Za-z0-9_]*\s*)?\+\s*0[xX]", comment)
    # The word before the label is only a binding when the code actually mentions
    # it. "// explosion world pos (+0xf0)" ends in `pos`, which names no field of
    # the object being written -- binding to it pinned an unrelated class's `pos`
    # and reported a stale label on a correct line. Same guard the fall-through
    # path below already applies.
    if named:
        f = named.group(1)
        if f in owners and re.search(r"\b" + re.escape(f) + r"\b", code):
            pin(None, f)
            return out
        # Label attributed to a field we cannot pin -> not our business.
        if re.search(r"\b" + re.escape(f) + r"\b", code):
            return out

    # A class hint in the comment ("Fruit+0xBC", "HUDControl::pos") must agree
    # with whichever class the by-name rule would resolve to.
    hint = re.search(r"\b([A-Z][A-Za-z0-9_]*)\s*(?:\+\s*0[xX]|::)", comment)
    hint_cls = hint.group(1) if hint else None

    def pin_checked(cls, f):
        if hint_cls and cls is None and f in owners:
            if (hint_cls, f) in exact:
                pin(hint_cls, f)    # comment named the class outright
                return
            if hint_cls not in owners[f]:
                return          # comment says another class; refuse to guess
        pin(cls, f)

    # Assignment target, incl. `Class::field = ...` and `p->field.z = ...`
    lhs = code.split("=")[0] if "=" in code else ""
    am = re.search(r"(?:([A-Za-z_][A-Za-z0-9_]*)\s*::\s*)?"
                   r"([A-Za-z_][A-Za-z0-9_]*)\s*(?:\.\s*[xyzw]\b)?\s*$", lhs.strip())
    if am:
        pin(am.group(1), am.group(2))

    # ...or the comment names the field explicitly. Goes through pin_checked so an
    # explicit "HUDControl::pos" hint still overrules the by-name resolution --
    # calling pin() here ignored the hint and pinned Entity::pos instead.
    for m in RE_IDENT.finditer(comment):
        f = m.group(1)
        if f in owners and re.search(r"\b" + re.escape(f) + r"\b", code):
            pin_checked(None, f)

    return out


def main():
    exact, owners, sites = build_truth()
    print("offset-label-lint: %d (class,field) pairs pinned by %d static_assert sites"
          % (len(exact), sites))

    mismatches = []
    checked = unprovable = 0

    for path in walk_sources():
        is_header = path.endswith((".h", ".hpp"))
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                lines = fh.readlines()
        except IOError:
            continue
        rel = os.path.relpath(path, os.path.join(SRC, ".."))

        # Track the enclosing class by brace depth (headers only).
        stack = []   # [(class_name, depth_at_open)]
        depth = 0

        for i, line in enumerate(lines, 1):
            code, comment = split_code_comment(line)

            if is_header:
                probe = line if code is None else code
                cm = RE_CLASS_OPEN.match(probe)
                pending = cm.group(1) if cm else None

            if code is None or not code.strip():
                if is_header:
                    d = (line.count("{") - line.count("}"))
                    if pending and "{" in line:
                        stack.append((pending, depth))
                    depth += d
                    while stack and depth <= stack[-1][1]:
                        stack.pop()
                continue

            labels = [parse_int(m.group(1)) for m in RE_LABEL.finditer(comment)]
            if labels:
                enclosing = stack[-1][0] if (is_header and stack) else None
                cands = candidates(code, comment, is_header, enclosing,
                                   exact, owners)

                if not cands:
                    unprovable += 1
                else:
                    ok = set()
                    for f, base in cands.items():
                        ok |= allowed_offsets(code, f, base)
                    bad = [L for L in labels if L not in ok]
                    checked += 1
                    # Only when NO label on the line is explainable. A line like
                    # "// this+0xf0 -> mgr+0x08" carries one label per object;
                    # the second naming a field we cannot see is not evidence of
                    # staleness in the first.
                    if bad and len(bad) == len(labels):
                        f = sorted(cands)[0]
                        mismatches.append(
                            (rel, i, enclosing, f, bad[0], cands[f],
                             line.rstrip()))

            if is_header:
                d = (line.count("{") - line.count("}"))
                if pending and "{" in line:
                    stack.append((pending, depth))
                depth += d
                while stack and depth <= stack[-1][1]:
                    stack.pop()

    print("  labels checked against ground truth : %d" % checked)
    print("  skipped, nothing on the line pinned : %d" % unprovable)
    print("  MISMATCHED                          : %d" % len(mismatches))

    if mismatches:
        print("")
        by_file = defaultdict(list)
        for m in mismatches:
            by_file[m[0]].append(m)
        # Many hits in one file is a carried-over-struct signature, not a typo.
        for rel in sorted(by_file, key=lambda k: -len(by_file[k])):
            rows = by_file[rel]
            print("%s  (%d)" % (rel, len(rows)))
            for (_r, ln, cls, field, label, off, src) in rows:
                print("  :%-5d %s%s  label +0x%X  pinned +0x%X  (delta %+d)"
                      % (ln, (cls + "::") if cls else "", field, label, off,
                         label - off))
                # Source comments may contain em-dashes etc; the Windows console
                # codepage (cp932 here) raises UnicodeEncodeError on them. Tool
                # output is ASCII-only per project convention.
                print("        %s" % ascii_only(src.strip()[:112]))
            print("")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
