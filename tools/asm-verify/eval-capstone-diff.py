#!/usr/bin/env python3
"""Compare ARM binary vs Thumb port with register normalization + LCS.
No external dependencies — pure regex normalization."""
import re, sys

def normalize(txt):
    """Normalize assembly text: strip addresses, canonicalize registers & mnemonics."""
    result = []
    for line in txt.strip().split('\n'):
        line = line.strip()
        if not line or ':' not in line or 'File format' in line:
            continue
        # objdump format: "  addr:\thexbytes\tmnemonic operands"
        # After addr:, take everything after the last tab (mnemonic + operands)
        # If no tab, split on whitespace and take last 2+ tokens
        if '\t' in line:
            # Get everything after the first tab (skip address)
            after_addr = line.split('\t', 1)[1] if line.count('\t') >= 2 else ''
            if not after_addr:
                continue
            # Now after_addr is either "hexbytes\tmnemonic ops" or "mnemonic ops"
            if '\t' in after_addr:
                instr = after_addr.split('\t', 1)[1].strip()  # skip hex bytes
            else:
                instr = after_addr.strip()  # no hex bytes column
        else:
            # No tabs — take tokens after address
            parts = line.split()
            if len(parts) < 3:
                continue
            # Skip address (parts[0] ends with ':'), skip hex bytes if present
            # Take the last 1-2 tokens as mnemonic + operands
            instr = ' '.join(parts[-2:])

        if not instr or instr.startswith('.'):
            continue

        # Normalize registers: sN -> VN, rN -> RN
        instr = re.sub(r'\bs(\d+)\b', r'V\1', instr)
        instr = re.sub(r'\br(\d+)\b', r'R\1', instr)
        # Normalize immediates
        instr = re.sub(r'#-?\d+', '#N', instr)
        instr = re.sub(r'\b0x[0-9a-f]+\b', 'ADDR', instr)
        # Normalize ARM vs Thumb VFP mnemonics
        instr = instr.replace('vldr.32', 'vldr').replace('vstr.32', 'vstr')
        instr = instr.replace('vadd.f32', 'vadd').replace('vsub.f32', 'vsub')
        instr = instr.replace('vmul.f32', 'vmul').replace('vdiv.f32', 'vdiv')
        instr = instr.replace('vcmp.f32', 'vcmp').replace('vmov.f32', 'vmov')
        # Normalize ARM push/pop <-> Thumb stmdb/ldmia
        instr = re.sub(r'^stmdb\s+sp!,', 'push', instr)
        instr = re.sub(r'^ldmia\s+sp!,', 'pop', instr)
        # Normalize Thumb add/sub sp, #N <-> no exact ARM equiv but common pattern
        instr = re.sub(r'\bsub\s+sp,\s+#N\b', 'sub sp, #N', instr)
        instr = re.sub(r'\badd\s+sp,\s+#N\b', 'add sp, #N', instr)
        # Strip ARM condition codes
        instr = re.sub(
            r'^(b|bl|bx|ldr|str|add|sub|cmp|mov|push|pop)'
            r'(eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le|al)\b',
            r'\1', instr)
        result.append(instr)
    return result

def lcs_similarity(a, b):
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

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: python3 eval-capstone-diff.py <port.s> <binary.s>")
        sys.exit(1)
    with open(sys.argv[1]) as f:
        port = normalize(f.read())
    with open(sys.argv[2]) as f:
        bin_asm = normalize(f.read())
    sim = lcs_similarity(port, bin_asm)
    print(f"Port (Thumb):  {len(port)} ops")
    print(f"Binary (ARM):  {len(bin_asm)} ops")
    print(f"LCS similarity: {sim:.1f}%")
    print()
    print("Sample normalized port:")
    for p in port[:3]:
        print(f"  {p}")
    print("Sample normalized binary:")
    for b in bin_asm[:3]:
        print(f"  {b}")
