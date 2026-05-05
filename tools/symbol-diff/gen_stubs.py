#!/usr/bin/env python3
"""
Stub-generation tool for the 633 "real gameplay gap" methods.

For each missing binary method that's not Bada/tinyxml2/Defunct/Phantom:
  - If src/<somewhere>/<Class>.h exists and its class body can host a new
    method, append a `// STUB: <Class>::<method>` declaration before the
    closing `};` of the class, plus a no-op body in the matching .cpp.
  - Otherwise, create a fresh src/stubs/<Class>.h + .cpp pair tagged with
    `// TODO: <Class>` for every method.

Skips already-handled signatures (port already has the same demangled
form), methods whose args reference types unreachable from the target
header, and Singleton<>-inherited GetInstance.

Type translation (binary -> port), since the typedef refactors have
landed:
  _Vector3<float>      -> Vec3
  _Vector2<float>      -> Vec2
  _Matrix44<float>     -> Matrix44
  _Matrix43<float>     -> Matrix43
  Mortar:: prefix      -> kept verbatim (already aligned)

Usage: python tools/symbol-diff/gen_stubs.py
"""
import re, pathlib, io
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parents[2]

MISSING_FILE = ROOT / 'tmp/symbol-diff/missing_full_demangled.txt'
PORT_REAL    = ROOT / 'tmp/symbol-diff/port_full_demangled.txt'

SKIP_BADA = {
    'BadaSound','DisplayManagerBada','MAMAudioThread','MAMAudioController',
    'MortarAudioMixerBada','Texture2DFromFile_Bada','GraphicsBada','InputBada',
    'FileBada','Interlocked','StackHeap','LinkedHeap','MemoryPool',
    'PacketSerializer','BadaApplication','GlesForm','ComboBox','ListBox',
    'EditField','InputDeviceBada','GeometryBinding_Bada','IIndexStream_Bada',
    'IVertexStream_Bada','IndexStreamBasic_Bada','VertexStreamBasic_Bada',
    'BadaTextureData','VertexElement_Bada','FileSystem_Direct','IFile_Direct',
    'SoundManager',
}
SKIP_LIBRARY = {
    'TiXmlNode','TiXmlElement','TiXmlDocument','TiXmlAttribute','TiXmlBase',
    'TiXmlComment','TiXmlDeclaration','TiXmlText','TiXmlUnknown','TiXmlHandle',
    'TiXmlPrinter','TiXmlString','TiXmlAttributeSet','TiXmlDTDInfo',
    'TiXmlParsingData',
}
SKIP_DEFUNCT = {
    'NetworkManager','OpenFeintNewsRenderer','OpenFeintNewsRenderInfo',
    'LeaderboardScreen','LeaderboardManager','LeaderboardList','LeaderboardItem',
    'FNHighscoreList','FNHighscore','FriendLeaderboardItem',
    'P2PMessage','NetworkPacket','PacketDeserializer','PacketSerializer',
    'PacketFactory','FruitSlicedPacket','PointsPacket','StartGamePacket',
    'WaveSyncPacket','OpenFeint','GameCenter',
}
SKIP_PHANTOM = {
    'UpsellScreen','UpsellScreenElement','KeyboardControl',
}
SKIP_NAMESPACES = {'Mortar','std','FruitNinja','__gnu_cxx','tinyxml2'}

# Hard-skip these classes -- nested-class / template edge cases that aren't
# tractable for blanket stubbing.
SKIP_HARD = {
    'Mesh','Model',           # std::vector<...>::iterator nested-name issues
    'AnimationManager',       # nested AnimationLerp / AnimationState complexity
}

# Types that aren't reachable from many port headers; methods that reference
# these are skipped at merge time but kept for new-file emission.
UNSAFE_FOR_MERGE = {
    # Defunct online types (no port header)
    'P2PMessage','NetworkPacket','NetworkProvider',
    'NetworkManagerStatusMessageID','NotificationCategory',
    # Engine types not always #include'd
    'GenericHUDControl','InputEvent','MortarSound',
    'EntityChunk','Skeleton','SharedEffectProperties',
    'MortarRectangleDec','AnimBindings',
    'SharedPropsInfo','PassBinding','BoneBinding',
    'Utf8StringProxy',  # paired with Utf8StringIterator
    # Nested enums whose parent class isn't visible at every merge site
    'ALIGNMENT_TYPE','PERSPECIVE_TYPE','NotificationType','ItemType',
    'WAVE_INFO','GAME_MODE','Endianness',
    # Misc engine types not commonly visible
    'QUADCUSTOMVERTEX','LIGHTSTRUCT','ScrollingMenuItem','ShopListItem',
    'LightStruct',
    # Templates the .h often doesn't include the headers for
    'Delegate0','Delegate1','Delegate2','Delegate3','Delegate4','Delegate5',
}

SIG_RE = re.compile(
    r'^(?:[\w:<>,\s\*&]+?\s+)?'
    r'((?:[\w~]+::)*[\w~]+)\s*\((.*)\)\s*(?:const)?\s*$')

def parse(line):
    line = line.strip()
    if not line: return None
    if line.startswith(('vtable','typeinfo','VTT','construction','guard',
                        'non-virtual','virtual','_GLOBAL','global constructors')):
        return None
    is_const = bool(re.search(r'\)\s*const\s*$', line))
    line2 = re.sub(r'\)\s*const\s*$', ')', line)
    m = SIG_RE.match(line2)
    if not m: return None
    qual, args = m.group(1), m.group(2).strip()
    parts = qual.split('::')
    if len(parts) < 2: return None
    cls = parts[-2]
    mth = parts[-1]
    ns  = '::'.join(parts[:-2]) if len(parts) > 2 else ''
    return ns, cls, mth, args, is_const

def normalize_args(args):
    a = args
    a = re.sub(r'\bunsigned long\b', 'unsigned int', a)
    a = re.sub(r'\blong\b(?!\s+long)', 'int', a)
    a = re.sub(r'\s+([*&])', r'\1', a)
    a = re.sub(r'([*&])\s+', r'\1 ', a)
    a = re.sub(r'\s+', ' ', a).strip()
    return a

def to_port_form(args):
    a = args
    a = re.sub(r'\b_Vector3<float>', 'Vec3', a)
    a = re.sub(r'\b_Vector2<float>', 'Vec2', a)
    a = re.sub(r'\b_Matrix44<float>', 'Matrix44', a)
    a = re.sub(r'\b_Matrix43<float>', 'Matrix43', a)
    return a

def referenced_types(args):
    return set(re.findall(r'\b(_*[A-Z][A-Za-z0-9_]*)\b', args))

def find_existing_header(cls):
    candidates = [h for h in ROOT.glob(f'src/**/{cls}.h')
                  if 'stubs' not in h.parts]
    for h in candidates:
        try:
            text = h.read_text(encoding='utf-8', errors='ignore')
        except Exception:
            continue
        if find_class_brace_range(text, cls) is not None:
            return h
    return None

def find_class_brace_range(text, cls):
    pat = re.compile(rf'(?<!friend\s)(?<!enum\s)\b(?:class|struct)\s+{re.escape(cls)}\b(?!\s*;)')
    for m in pat.finditer(text):
        head = text[m.start():m.start()+200]
        if re.search(r'\b(?:class|struct)\s+' + re.escape(cls) + r'(?:\s+final)?\s*[:{]', head):
            ob = text.find('{', m.start())
            if ob < 0: continue
            depth = 1
            i = ob + 1
            while i < len(text) and depth:
                c = text[i]
                if c == '{': depth += 1
                elif c == '}': depth -= 1
                i += 1
            if depth == 0:
                end = i
                while end < len(text) and text[end] in ' \t': end += 1
                if end < len(text) and text[end] == ';': end += 1
                return (m.start(), end, ob, i - 1)
    return None

def class_namespace_in_h(h_text):
    return 'Mortar' if re.search(r'^\s*namespace\s+Mortar\b', h_text, re.M) else ''

# -------------- main pass --------------

def main():
    if not MISSING_FILE.exists():
        raise SystemExit('missing_full_demangled.txt absent; run symbol-diff first')

    # Load port-real symbols so we don't restub anything already present.
    port_sigs = set()
    if PORT_REAL.exists():
        port_sigs = set(PORT_REAL.read_text(encoding='utf-8', errors='ignore').splitlines())

    # Build per-class plan.
    by_cls = defaultdict(list)   # (ns, cls) -> [(mth, args, is_const, raw)]
    for ln in MISSING_FILE.read_text(encoding='utf-8', errors='ignore').splitlines():
        p = parse(ln)
        if not p: continue
        ns, cls, mth, args, is_const = p
        if cls in SKIP_BADA:    continue
        if cls in SKIP_LIBRARY: continue
        if cls in SKIP_DEFUNCT: continue
        if cls in SKIP_PHANTOM: continue
        if cls in SKIP_HARD:    continue
        if cls in SKIP_NAMESPACES: continue
        # Ignore nested-in-class symbols (Touch::State::Update etc.).
        if ns and ns != 'Mortar': continue
        # Already in port?
        if ln.strip() in port_sigs: continue
        args = normalize_args(args)
        by_cls[(ns, cls)].append((mth, to_port_form(args), is_const, ln.strip()))

    # Stats
    n_merged = 0
    n_methods_merged = 0
    n_new_pairs = 0
    n_methods_new = 0
    n_skipped_unsafe = 0
    n_skipped_existing = 0

    OUTDIR_NEW = ROOT / 'src/stubs'
    OUTDIR_NEW.mkdir(parents=True, exist_ok=True)

    for (ns, cls), method_list in sorted(by_cls.items()):
        target_h = find_existing_header(cls)
        if target_h is not None:
            # MERGE-INTO-EXISTING path
            ok = merge_into(target_h, ns, cls, method_list)
            if ok > 0:
                n_merged += 1
                n_methods_merged += ok
        else:
            # NEW-PAIR path
            n = emit_new(cls, ns, method_list)
            if n > 0:
                n_new_pairs += 1
                n_methods_new += n

    print(f'Merged into existing headers: {n_merged} classes / {n_methods_merged} methods')
    print(f'New stub pairs in src/stubs/: {n_new_pairs} classes / {n_methods_new} methods')


# -------------- merge-into-existing --------------

MERGE_START = '// ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----'
MERGE_END   = '// ---- end AUTO-STUB MERGE ----'

def merge_into(target_h, ns, cls, method_list):
    text = target_h.read_text(encoding='utf-8', errors='ignore')
    block = find_class_brace_range(text, cls)
    if not block:
        return 0
    _, _, brace_open, brace_close = block

    # Strip prior AUTO-STUB MERGE block if present so re-runs are idempotent.
    # Always write back the stripped form -- even if no new methods qualify,
    # we want the prior block (which may now reference unsafe types) to be
    # gone.
    stripped = re.sub(
        r'\n?\s*' + re.escape(MERGE_START) + r'.*?' + re.escape(MERGE_END) + r'\s*\n',
        '\n', text, flags=re.DOTALL)
    if stripped != text:
        target_h.write_text(stripped, encoding='utf-8')
        text = stripped
    block = find_class_brace_range(text, cls)
    _, _, brace_open, brace_close = block

    # Same for .cpp
    target_cpp_pre = target_h.with_suffix('.cpp')
    if target_cpp_pre.exists():
        ctext = target_cpp_pre.read_text(encoding='utf-8', errors='ignore')
        cstripped = re.sub(
            r'\n?\s*' + re.escape(MERGE_START) + r'.*?' + re.escape(MERGE_END) + r'\s*\n',
            '\n', ctext, flags=re.DOTALL)
        if cstripped != ctext:
            target_cpp_pre.write_text(cstripped, encoding='utf-8')

    h_ns = class_namespace_in_h(text)
    # If binary's ns differs from port's, bail (bad mangling target).
    if ns != h_ns:
        return 0

    # Existing method names in class body (and parent class via inheritance).
    class_body = text[brace_open:brace_close]
    existing_names = set(re.findall(r'(?:^|\s|;|}|:|\*|&)([~A-Za-z_]\w*)\s*\(', class_body))
    existing_names |= {cls, '~' + cls}
    # Singleton GetInstance from Singleton<X> base
    class_decl_line = text[block[0]:brace_open]
    if 'Singleton<' in class_decl_line:
        existing_names.add('GetInstance')
    # Walk up the single-inheritance chain collecting method names so that
    # virtual overrides with mismatched return types don't get re-declared.
    seen_parents = set()
    parent_decl = class_decl_line
    while True:
        pm = re.search(r':\s*(?:public|protected|private)\s+(?:Mortar::)?([A-Za-z_]\w*)',
                       parent_decl)
        if not pm: break
        parent = pm.group(1)
        if parent in seen_parents: break
        seen_parents.add(parent)
        ph = find_existing_header(parent)
        if not ph: break
        ptext = ph.read_text(encoding='utf-8', errors='ignore')
        pblock = find_class_brace_range(ptext, parent)
        if not pblock: break
        pbody = ptext[pblock[2]:pblock[3]]
        existing_names |= set(re.findall(r'(?:^|\s|;|}|:|\*|&)([~A-Za-z_]\w*)\s*\(', pbody))
        parent_decl = ptext[pblock[0]:pblock[2]]

    # If port's namespace is Mortar, drop redundant Mortar:: from arg types
    # so we don't need extra includes.
    if h_ns == 'Mortar':
        method_list = [(m, re.sub(r'\bMortar::', '', a), c, r) for (m, a, c, r) in method_list]

    # Filter unsafe-for-merge methods.
    safe = []
    for mth, args, is_const, raw in method_list:
        if mth in existing_names:
            continue
        used = referenced_types(args)
        if used & UNSAFE_FOR_MERGE:
            continue
        safe.append((mth, args, is_const, raw))
    if not safe:
        return 0

    # Build decl block.
    decl_buf = io.StringIO()
    decl_buf.write('\n')
    decl_buf.write('public:\n')
    decl_buf.write('    ' + MERGE_START + '\n')
    for mth, args, is_const, raw in safe:
        decl_buf.write(f'    // STUB: {cls}::{mth} -- auto stub from binary missing-symbol set\n')
        if mth == cls:
            decl_buf.write(f'    {cls}({args});\n')
        elif mth == '~' + cls:
            decl_buf.write(f'    ~{cls}();\n')
        elif mth.startswith('operator'):
            decl_buf.write(f'    int {mth}({args}){" const" if is_const else ""};\n')
        else:
            decl_buf.write(f'    void {mth}({args}){" const" if is_const else ""};\n')
    decl_buf.write('    ' + MERGE_END + '\n')
    decls = decl_buf.getvalue()

    # Insert before closing `};`
    new_text = text[:brace_close] + decls + text[brace_close:]
    target_h.write_text(new_text, encoding='utf-8')

    # Append bodies to .cpp (creating one if absent).
    target_cpp = target_h.with_suffix('.cpp')
    body_buf = io.StringIO()
    if not target_cpp.exists():
        body_buf.write(f'#include "{target_h.name}"\n\n')
    body_buf.write('\n')
    body_buf.write(MERGE_START + '\n')
    if h_ns == 'Mortar':
        body_buf.write('namespace Mortar {\n')
        for mth, args, is_const, raw in safe:
            body_buf.write(f'// STUB: {cls}::{mth} -- auto stub\n')
            if mth == cls:
                body_buf.write(f'{cls}::{cls}({args}) {{}}\n')
            elif mth == '~' + cls:
                body_buf.write(f'{cls}::~{cls}() {{}}\n')
            elif mth.startswith('operator'):
                body_buf.write(f'int {cls}::{mth}({args}){" const" if is_const else ""} {{ return 0; }}\n')
            else:
                body_buf.write(f'void {cls}::{mth}({args}){" const" if is_const else ""} {{}}\n')
        body_buf.write('}  // namespace Mortar\n')
    else:
        for mth, args, is_const, raw in safe:
            body_buf.write(f'// STUB: {cls}::{mth} -- auto stub\n')
            if mth == cls:
                body_buf.write(f'{cls}::{cls}({args}) {{}}\n')
            elif mth == '~' + cls:
                body_buf.write(f'{cls}::~{cls}() {{}}\n')
            elif mth.startswith('operator'):
                body_buf.write(f'int {cls}::{mth}({args}){" const" if is_const else ""} {{ return 0; }}\n')
            else:
                body_buf.write(f'void {cls}::{mth}({args}){" const" if is_const else ""} {{}}\n')
    body_buf.write(MERGE_END + '\n')
    if target_cpp.exists():
        existing = target_cpp.read_text(encoding='utf-8', errors='ignore')
        # Strip prior AUTO-STUB MERGE block first.
        existing = re.sub(
            r'\n?\s*' + re.escape(MERGE_START) + r'.*?' + re.escape(MERGE_END) + r'\s*\n',
            '\n', existing, flags=re.DOTALL)
        with target_cpp.open('w', encoding='utf-8') as f:
            f.write(existing.rstrip() + '\n')
            f.write(body_buf.getvalue())
    else:
        target_cpp.write_text(body_buf.getvalue(), encoding='utf-8')

    return len(safe)


# -------------- new-pair emission --------------

def emit_new(cls, ns, method_list):
    # Filter methods that reference clearly-unreachable types so the new
    # stub file actually compiles.
    UNSAFE_NEW = {
        # Nested types we can't forward-decl cleanly
        'AnimBindings','Endian','Endianness','BoneBinding',
        'SharedPropsInfo','PassBinding','ValueBuffer','PulseInfo','State',
        'ALIGNMENT_TYPE','PERSPECIVE_TYPE','NotificationType','ItemType',
        'WAVE_INFO','GAME_MODE',
        'ThrowFlags','RoundingMode','DataType','TranisitionInfo',
        # SmartPtr<X> by-value requires complete X (calls AddRef in copy
        # ctor). The stub headers don't pull in Texture/Mesh full defs.
        'Texture','Mesh','Skeleton',
        'Utf8StringIterator','Utf8StringProxy',
        # Misc engine internals not always reachable
        'QUADCUSTOMVERTEX','LIGHTSTRUCT','GenericHUDControl','InputEvent',
    }
    # Hard-skip these classes from new-pair emission entirely (nested-enum
    # heavy or fundamentally incompatible with blanket stubbing).
    SKIP_NEW_HARD = {'FPU','VertexElement','WordWrap','PulseInfo'}
    if cls in SKIP_NEW_HARD:
        return 0
    method_list = [
        (m, a, c, r) for (m, a, c, r) in method_list
        if not (referenced_types(a) & UNSAFE_NEW)
    ]
    if not method_list:
        return 0

    OUT = ROOT / 'src/stubs'
    h_path = OUT / f'{cls}.h'
    cpp_path = OUT / f'{cls}.cpp'

    refs_used = set()
    for _, args, _, _ in method_list:
        refs_used |= referenced_types(args)

    # Translate args to drop redundant Mortar:: when the new class lives
    # in Mortar namespace.
    target_ns = ns or 'Mortar'

    PREDEFINED = {
        cls, target_ns, 'Mortar', 'std', 'FruitNinja', 'tinyxml2', '__gnu_cxx',
        'Vec3', 'Vec2', 'Matrix44', 'Matrix43',
        # Already covered by the standard #includes the new pair emits.
        'Delegate0','Delegate1','Delegate2','Delegate3','Delegate4','Delegate5',
        'SmartPtr','AsciiString','Colour',
        # Namespaces / templates (not classes) we shouldn't forward-declare.
        'Endian','AnimBindings',
        # Frequent stub-internal types -- emitted as a flat empty class.
        'Endianness',
    }

    BUILTIN = {'void','bool','char','short','int','long','float','double','signed',
               'unsigned','const','volatile','wchar_t','size_t','ptrdiff_t','nullptr_t'}

    fwd = sorted(refs_used - PREDEFINED - BUILTIN)

    guard = f'FN_STUBS_{cls.upper()}_H'
    h = io.StringIO()
    h.write(f'#ifndef {guard}\n#define {guard}\n\n')
    h.write(f'// TODO: {cls} -- auto-generated symbol-coverage stub.\n')
    h.write(f'//   Empty bodies; real binary implementations live at the\n')
    h.write(f'//   addresses listed in tmp/symbol-diff/missing_full_demangled.txt.\n')
    h.write(f'//   Replace each method with a real port over time.\n\n')
    h.write('#include "math/Vec3.h"\n')
    h.write('#include "math/Vec2.h"\n')
    h.write('#include "math/Matrix44.h"\n')
    h.write('#include "math/Colour.h"\n')
    h.write('#include "util/Delegate.h"\n')
    h.write('#include "util/SmartPtr.h"\n')
    h.write('#include "util/AsciiString.h"\n')
    h.write('#include <cstdint>\n\n')
    if fwd:
        h.write('// Forward decls for binary-shape arg types not yet ported here.\n')
        h.write('namespace Mortar {\n')
        for r in fwd:
            h.write(f'  class {r};\n')
        h.write('}\n\n')
    if target_ns:
        h.write(f'namespace {target_ns} {{\n\n')
    h.write(f'class {cls} {{\npublic:\n')
    for mth, args, is_const, raw in method_list:
        h.write(f'    // TODO: {cls}::{mth} -- auto stub\n')
        if mth == cls:
            h.write(f'    {cls}({args});\n')
        elif mth == '~' + cls:
            h.write(f'    ~{cls}();\n')
        elif mth.startswith('operator'):
            h.write(f'    int {mth}({args}){" const" if is_const else ""};\n')
        else:
            h.write(f'    void {mth}({args}){" const" if is_const else ""};\n')
    h.write('};\n\n')
    if target_ns:
        h.write(f'}}  // namespace {target_ns}\n\n')
    h.write(f'#endif  // {guard}\n')
    h_path.write_text(h.getvalue(), encoding='utf-8')

    c = io.StringIO()
    c.write(f'// TODO: {cls} -- auto-generated stub bodies. See {cls}.h.\n')
    c.write(f'#include "{cls}.h"\n\n')
    if target_ns:
        c.write(f'namespace {target_ns} {{\n\n')
    for mth, args, is_const, raw in method_list:
        c.write(f'// TODO: {cls}::{mth} -- auto stub\n')
        if mth == cls:
            c.write(f'{cls}::{cls}({args}) {{}}\n')
        elif mth == '~' + cls:
            c.write(f'{cls}::~{cls}() {{}}\n')
        elif mth.startswith('operator'):
            c.write(f'int {cls}::{mth}({args}){" const" if is_const else ""} {{ return 0; }}\n')
        else:
            c.write(f'void {cls}::{mth}({args}){" const" if is_const else ""} {{}}\n')
    if target_ns:
        c.write(f'\n}}  // namespace {target_ns}\n')
    cpp_path.write_text(c.getvalue(), encoding='utf-8')

    return len(method_list)


if __name__ == '__main__':
    main()
