import os, re, glob
DATA = 'FruitNinjaBada/Data'
# 1. Available .tex basenames (case-insensitive) across textures/ + particles/ (+ any subdir)
avail = set()
for root,_,files in os.walk(DATA):
    for f in files:
        if f.lower().endswith('.tex'):
            avail.add(f[:-4].lower())
def have(name):
    n = name.lower()
    if n.endswith('.tex'): n = n[:-4]
    # strip any lang-dir prefix like "zh/foo"
    n = n.split('/')[-1]
    return n in avail
refs = {}  # name -> set of source locations
def add(name, src):
    name = name.strip()
    if not name or '%' in name or '{' in name: return   # skip format/dynamic
    refs.setdefault(name, set()).add(src)
# 2a. src/ code: LoadTexture / LoadLocalisedTexture / LoadContent string literals
for f in glob.glob('src/**/*.cpp', recursive=True)+glob.glob('src/**/*.h', recursive=True):
    try: txt=open(f,encoding='utf-8',errors='replace').read()
    except: continue
    for m in re.finditer(r'Load(?:Localised)?Texture\s*\(\s*"([^"]+\.?t?e?x?)"', txt):
        add(m.group(1), f)
    # also generic LoadTexture("name") without .tex
    for m in re.finditer(r'\bLoadTexture\s*\(\s*"([^"]+)"', txt):
        add(m.group(1), f)
# 2b. Data XML: texture=, factTexture=, icon=, <texture name=>, *Texture=
for f in glob.glob(DATA+'/**/*.xml', recursive=True):
    try: txt=open(f,encoding='utf-8',errors='replace').read()
    except: continue
    for attr in ['texture','factTexture','icon','iconTexture','backTexture','buttonTexture','image']:
        for m in re.finditer(attr+r'\s*=\s*"([^"]+)"', txt):
            add(m.group(1), os.path.basename(f))
# 3. Report missing
missing = {}
for name, srcs in refs.items():
    # bonusawards/particle textures: Bonus::Parse appends .tex; particle <texture name> resolves in particles/
    if not have(name):
        missing[name] = srcs
print(f"Available .tex files: {len(avail)}")
print(f"Distinct texture refs scanned: {len(refs)}")
print(f"=== MISSING ({len(missing)}) ===")
for name in sorted(missing):
    srcs = ', '.join(sorted(missing[name]))[:90]
    print(f"  {name:34}  <- {srcs}")
