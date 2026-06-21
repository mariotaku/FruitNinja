#!/usr/bin/env python3
"""Semantic assembly diff: normalize ARM/Thumb encodings then LCS comparison.

Normalizes away:
- Register allocation differences (s0-s31 -> V0-V31, r0-r12 -> G0-G12)
- ARM vs Thumb encoding (push=stmdb, pop=ldmia, .w/.n suffixes, IT blocks)
- VFP size suffixes (vldr.32 -> vldr)
- ARM condition codes (ldreq -> ldr, moveq -> mov)
- Immediates and addresses (0xDEAD -> IMM, -42 -> IMM)
- Flag-setting variants (adds=add, subs=sub for comparison)
- Comment stripping
"""

import re, sys

# ==== ARM <-> Thumb MNEMONIC NORMALIZATION ====

# ARM mnemonic -> canonical mnemonic
ARM_TO_CANON = {
    # Stack
    'stmdb':  'push',   # stmdb sp!, {regs} = push
    'stmfd':  'push',   # stmfd sp!, {regs} = push (ARM deprecation alias)
    'ldmia':  'pop',    # ldmia sp!, {regs} = pop
    'ldmfd':  'pop',    # ldmfd sp!, {regs} = pop (ARM deprecation alias)

    # VFP store/load multiple
    'fstmias': 'vpush',
    'fstmiad': 'vpush',
    'fldmias': 'vpop',
    'fldmiad': 'vpop',
    'vstmia':  'vstm',
    'vldmia':  'vldm',

    # Data processing (ARM condition-code-less forms)
    'mov':     'mov',
    'movw':    'movw',
    'movt':    'movt',
    'mvn':     'mvn',
    'add':     'add',
    'sub':     'sub',
    'mul':     'mul',
    'and':     'and',
    'orr':     'orr',
    'eor':     'eor',
    'bic':     'bic',
    'lsl':     'lsl',
    'lsr':     'lsr',
    'asr':     'asr',
    'ror':     'ror',
    'cmp':     'cmp',
    'cmn':     'cmn',
    'tst':     'tst',
    'teq':     'teq',
    'rsb':     'rsb',
    'adc':     'adc',
    'sbc':     'sbc',
    'rsc':     'rsc',
    'mla':     'mla',
    'umull':   'umull',
    'smull':   'smull',
    'sdiv':    'sdiv',
    'udiv':    'udiv',
    'clz':     'clz',
    'rbit':    'rbit',
    'rev':     'rev',

    # Branch
    'b':       'b',
    'bl':      'bl',
    'bx':      'bx',
    'blx':     'blx',
    'bxj':     'bxj',

    # Load/store
    'ldr':     'ldr',
    'str':     'str',
    'ldrb':    'ldrb',
    'strb':    'strb',
    'ldrh':    'ldrh',
    'strh':    'strh',
    'ldrsb':   'ldrsb',
    'ldrsh':   'ldrsh',
    'ldrd':    'ldrd',
    'strd':    'strd',
    'ldm':     'ldm',
    'stm':     'stm',

    # VFP (canonical: strip .size suffix)
    'vldr':    'vldr',
    'vstr':    'vstr',
    'vadd':    'vadd',
    'vsub':    'vsub',
    'vmul':    'vmul',
    'vdiv':    'vdiv',
    'vneg':    'vneg',
    'vabs':    'vabs',
    'vsqrt':   'vsqrt',
    'vcmp':    'vcmp',
    'vcmpe':   'vcmp',
    'vmov':    'vmov',
    'vcvt':    'vcvt',
    'vmla':    'vmla',
    'vmls':    'vmls',
    'vnmla':   'vnmla',
    'vnmls':   'vnmls',
    'vnmul':   'vnmul',
    'vpush':   'vpush',
    'vpop':    'vpop',

    # Misc
    'nop':     'nop',
    'bkpt':    'bkpt',
    'svc':     'svc',
    'mrs':     'mrs',
    'msr':     'msr',
    'cps':     'cps',
    'cpsid':   'cpsid',
    'cpsie':   'cpsie',
    'dsb':     'dsb',
    'dmb':     'dmb',
    'isb':     'isb',
    'udf':     'udf',

    # Non-instruction lines
    '.short':  '.short',
    '.word':   '.word',
    '.byte':   '.byte',
}

# Thumb-specific patterns that normalize away
THUMB_CLEANUP = [
    # Strip .w (wide) and .n (narrow) suffixes
    (r'\.w\b', ''),
    (r'\.n\b', ''),
]

# ARM condition codes to strip
ARM_COND_CODES = r'(eq|ne|cs|hs|cc|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)'

# VFP size suffixes to strip
VFP_SIZE_SUFFIXES = [
    r'\.32\b', r'\.64\b', r'\.f32\b', r'\.f64\b', r'\.s32\b', r'\.s64\b',
    r'\.u32\b', r'\.u64\b',
]


def parse_objdump_line(line):
    """Extract mnemonic+operands from an objdump line.
    Handles both ARM format:  '  addr:\thexbytes\tmnemonic ops'
    and Thumb format:        '  addr:\thexbytes\tmnemonic ops  ; comment'
    Returns None for non-instruction lines (labels, directives, blank).
    """
    line = line.strip()
    if not line or 'Disassembly' in line or 'file format' in line:
        return None

    # Strip comment
    if ';' in line:
        line = line.split(';')[0].strip()

    # Split on tabs: addr \t hexbytes \t mnemonic ops
    parts = line.split('\t')
    if len(parts) < 2:
        return None

    # Check if this is a function label line (ends with ':')
    if len(parts) == 2 and ':' in parts[0] and not parts[1].strip():
        return None

    # Tab format
    if len(parts) >= 3:
        instr = parts[2].strip()
    else:
        instr = parts[1].strip()

    # Skip label-only lines (contain ':' but no instruction)
    if ':' in instr and not any(c.isalpha() or c in '#[' for c in instr.replace(':', '')):
        return None

    return instr


def normalize_instr(instr):
    """Normalize a single instruction (mnemonic + operands) to canonical form."""
    if not instr:
        return None

    # Split mnemonic from operands
    parts = instr.split(None, 1)
    mnemonic = parts[0].lower() if parts else ''
    operands = parts[1] if len(parts) > 1 else ''

    # Strip VFP size suffixes from mnemonic
    mnemonic = re.sub(r'vldr\.32', 'vldr', mnemonic)
    mnemonic = re.sub(r'vstr\.32', 'vstr', mnemonic)
    mnemonic = re.sub(r'vadd\.f32', 'vadd', mnemonic)
    mnemonic = re.sub(r'vsub\.f32', 'vsub', mnemonic)
    mnemonic = re.sub(r'vmul\.f32', 'vmul', mnemonic)
    mnemonic = re.sub(r'vdiv\.f32', 'vdiv', mnemonic)
    mnemonic = re.sub(r'vcmp\.f32', 'vcmp', mnemonic)
    mnemonic = re.sub(r'vmov\.f32', 'vmov', mnemonic)
    mnemonic = re.sub(r'vneg\.f32', 'vneg', mnemonic)
    mnemonic = re.sub(r'vabs\.f32', 'vabs', mnemonic)
    mnemonic = re.sub(r'vsqrt\.f32', 'vsqrt', mnemonic)
    mnemonic = re.sub(r'vcvt\.f32\.f64', 'vcvt', mnemonic)
    mnemonic = re.sub(r'vcvt\.f64\.f32', 'vcvt', mnemonic)
    # Strip any remaining .f32/.f64/.32/.64/.s32/.s64/.u32/.u64 suffixes
    for pat in VFP_SIZE_SUFFIXES:
        mnemonic = re.sub(pat, '', mnemonic)

    # Strip ARM condition codes from mnemonic
    mnemonic = re.sub(ARM_COND_CODES + r'$', '', mnemonic)

    # Strip Thumb .w and .n suffixes
    mnemonic = re.sub(r'\.w$', '', mnemonic)
    mnemonic = re.sub(r'\.n$', '', mnemonic)

    # ARM->canonical mnemonic mapping
    mnemonic = ARM_TO_CANON.get(mnemonic, mnemonic)

    # Normalize flag-setting variants (adds=add, subs=sub, movs=mov) for comparison
    if mnemonic.endswith('s') and len(mnemonic) > 2:
        base = mnemonic[:-1]
        if base in ARM_TO_CANON:
            mnemonic = ARM_TO_CANON[base]

    # Normalize registers in operands
    # VFP single-precision: s0-s31 -> V0-V31
    operands = re.sub(r'\bs(\d+)\b', r'V\1', operands)
    # VFP double-precision: d0-d31 -> D0-D31
    operands = re.sub(r'\bd(\d+)\b', r'D\1', operands)
    # General-purpose: r0-r12 -> G0-G12 (but keep sp, lr, pc special)
    operands = re.sub(r'\br(1[3-5])\b', r'G\1', operands)  # r13-r15 first
    operands = re.sub(r'\br([0-9]|1[0-2])\b', r'G\1', operands)
    # Normalize sp, lr, pc in operands
    operands = operands.replace('sp', 'SP').replace('lr', 'LR').replace('pc', 'PC')

    # Also normalize sp/lr/pc in mnemonic (shouldn't happen, but defensive)
    mnemonic = mnemonic.replace('sp', 'SP').replace('lr', 'LR').replace('pc', 'PC')

    # Normalize immediates
    operands = re.sub(r'#-?\d+', '#N', operands)
    operands = re.sub(r'#0x[0-9a-f]+', '#N', operands)
    # Normalize addresses
    operands = re.sub(r'\b0x[0-9a-f]+\b', 'ADDR', operands)

    # Normalize register lists {r0, r1, r2} -> {G0, G1, G2}
    # (already handled by rN -> GN substitution)

    # Collapse whitespace
    operands = re.sub(r'\s+', ' ', operands).strip()

    return f"{mnemonic} {operands}".strip()


def normalize(txt):
    """Parse objdump output and normalize to canonical instruction list."""
    result = []
    for line in txt.strip().split('\n'):
        instr = parse_objdump_line(line)
        if instr is None:
            continue
        norm = normalize_instr(instr)
        if norm:
            result.append(norm)
    return result


def lcs_similarity(a, b):
    """Longest common subsequence as percentage of max length."""
    m, n = len(a), len(b)
    if max(m, n) == 0:
        return 100.0
    dp = [[0] * (n + 1) for _ in range(m + 1)]
    for i in range(m):
        for j in range(n):
            if a[i] == b[j]:
                dp[i+1][j+1] = dp[i][j] + 1
            else:
                dp[i+1][j+1] = max(dp[i+1][j], dp[i][j+1])
    return dp[m][n] / max(m, n) * 100


def levenshtein_similarity(a, b):
    """Levenshtein distance as similarity percentage."""
    m, n = len(a), len(b)
    if max(m, n) == 0:
        return 100.0
    prev = list(range(n + 1))
    curr = [0] * (n + 1)
    for i in range(1, m + 1):
        curr[0] = i
        for j in range(1, n + 1):
            cost = 0 if a[i-1] == b[j-1] else 1
            curr[j] = min(prev[j] + 1, curr[j-1] + 1, prev[j-1] + cost)
        prev, curr = curr, prev
    dist = prev[n]
    return (1.0 - dist / max(m, n)) * 100


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python3 eval-capstone-diff.py <port.s> <binary.s>")
        sys.exit(1)

    with open(sys.argv[1]) as f:
        port = normalize(f.read())
    with open(sys.argv[2]) as f:
        bin_asm = normalize(f.read())

    lcs_sim = lcs_similarity(port, bin_asm)
    lev_sim = levenshtein_similarity(port, bin_asm)

    print(f"Port (Thumb):  {len(port)} ops")
    print(f"Binary (ARM):  {len(bin_asm)} ops")
    print(f"LCS similarity:       {lcs_sim:.1f}%")
    print(f"Levenshtein similarity: {lev_sim:.1f}%")
    print()
    print("Sample normalized port:")
    for p in port[:5]:
        print(f"  {p}")
    print("Sample normalized binary:")
    for b in bin_asm[:5]:
        print(f"  {b}")
