import os, re, glob
DATA='FruitNinjaBada/Data'
TEXDIRS=[DATA+'/textures', DATA+'/particles']
# present .tex basenames (lowercased) + their real paths
present={}
for d in TEXDIRS:
    for f in glob.glob(d+'/**/*.tex', recursive=True):
        b=os.path.basename(f)[:-4]
        present.setdefault(b.lower(), f)
# ---- collect LITERAL referenced names (src + xml) ----
lit=set()
def addlit(s):
    s=s.strip()
    if s.lower().endswith('.tex'): s=s[:-4]
    s=s.split('/')[-1]
    if s: lit.add(s.lower())
srcfiles=glob.glob('src/**/*.cpp',recursive=True)+glob.glob('src/**/*.h',recursive=True)
for f in srcfiles:
    t=open(f,encoding='utf-8',errors='replace').read()
    for m in re.finditer(r'"([A-Za-z0-9_./]+\.tex)"', t): addlit(m.group(1))
    for m in re.finditer(r'Load(?:Localised)?Texture\s*\(\s*"([A-Za-z0-9_./]+)"', t): addlit(m.group(1))
    for m in re.finditer(r'\bLoadTexture\s*\(\s*"([A-Za-z0-9_./]+)"', t): addlit(m.group(1))
for f in glob.glob(DATA+'/**/*.xml',recursive=True):
    t=open(f,encoding='utf-8',errors='replace').read()
    for m in re.finditer(r'(?:texture|factTexture|icon|image|iconTexture|backTexture)\s*=\s*"([A-Za-z0-9_./]+)"', t): addlit(m.group(1))
    # <texture name="..."> in particle xml
    for m in re.finditer(r'<texture[^>]*name\s*=\s*"([A-Za-z0-9_]+)"', t): addlit(m.group(1))
# ---- collect DYNAMIC prefixes (format strings that build tex names) ----
dyn_prefix=set()
FMT=re.compile(r'"([A-Za-z0-9_./]*?)%[sdiu]')   # literal part BEFORE first %s/%d
for f in srcfiles:
    t=open(f,encoding='utf-8',errors='replace').read()
    # only formats that look texture-related (contain a word char before %, or near .tex / Load / texture)
    for m in re.finditer(r'"([^"\n]*%[sdiu][^"\n]*)"', t):
        s=m.group(1)
        pm=FMT.match('"'+s)
        if pm:
            pre=pm.group(1).split('/')[-1]
            if len(pre)>=3:            # only meaningful prefixes (avoid "%s" catch-all)
                dyn_prefix.add(pre.lower())
# XML: sprinkle textures / model name templates sometimes use %; also fruitlist factTexture prefixes 'sml_'
# ---- classify ----
def is_used(b):
    bl=b.lower()
    if bl in lit: return 'literal'
    for p in dyn_prefix:
        if bl.startswith(p): return 'dynamic:'+p
    return None
unused={}; usedcount=0; dynhit={}
for b,path in present.items():
    u=is_used(b)
    if u is None: unused[b]=path
    else:
        usedcount+=1
        if u.startswith('dynamic'): dynhit.setdefault(u,[]).append(b)
print(f"PRESENT .tex: {len(present)} | literal-refs: {len(lit)} | dyn-prefixes: {sorted(dyn_prefix)}")
print(f"USED (literal or dynamic-prefix): {usedcount} | UNUSED candidates: {len(unused)}")
print("=== DYNAMIC-PREFIX groups (excluded from unused -- verify these are truly used) ===")
for k in sorted(dynhit): print(f"  {k:24} -> {len(dynhit[k])} files e.g. {dynhit[k][:4]}")
print("=== UNUSED candidates (present, no literal/dynamic ref) ===")
for b in sorted(unused):
    print("  ", os.path.relpath(unused[b],DATA))
