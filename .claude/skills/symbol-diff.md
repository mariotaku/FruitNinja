---
name: symbol-diff
description: Compare binary's text symbols against the port's full src/ tree (cross-toolchain compile + nm), classify the diff (Bada platform / tinyxml2 / defunct / phantom / real gameplay), and save organized reports to tmp/symbol-diff/.
user_invocable: true
---

# Symbol Diff Skill

Produce a binary-vs-port symbol coverage report by cross-compiling every `src/**/*.cpp` with the Sourcery 2010q1 toolchain (matches Samsung Bada's mangling and ABI exactly), running `nm` on the resulting `.o` files, and diffing against the binary's text-symbol set.

## When to invoke
- After a large batch of subsystem ports lands, to measure coverage progression.
- To identify the next "biggest gap" classes to port.
- To validate that a Bada-platform/defunct/phantom class is correctly excluded.

## Pre-requisites
- `fnverify` Docker image built: `bash tools/asm-verify/setup.sh` (one-time).
- The port's MSVC `build/` exists with `tinyxml2.h` available at `build/_deps/tinyxml2-src/tinyxml2.h`.

## Steps

### 1. Extract binary symbols (mangled + demangled)

```bash
mkdir -p tmp/symbol-diff
docker run --rm -v "$(cygpath -m "$(pwd)"):/work" fnverify -c '
arm-none-eabi-nm /work/FruitNinjaBada/Bin/FruitNinja.exe \
  | awk "\$2 ~ /^[Tt]\$/ {print \$3}" \
  | sort -u > /work/tmp/symbol-diff/binary_symbols_mangled.txt

arm-none-eabi-nm --demangle /work/FruitNinjaBada/Bin/FruitNinja.exe \
  | awk "\$2 ~ /^[Tt]\$/ { \$1=\"\"; \$2=\"\"; sub(/^  */,\"\"); print }" \
  | sort -u > /work/tmp/symbol-diff/binary_symbols_demangled.txt
'
```

Expected: ~3305 mangled / ~2916 demangled.

### 2. Cross-compile every src/.cpp and aggregate port symbols

This is the load-bearing step. It stages source onto an ext4 path inside the container (drvfs's i386 inode-overflow blocks the toolchain from stating /work directly), applies known C++11→C++03 sed transforms, compiles with the same flags as the asm-verifier (`-fshort-enums -fshort-wchar` for ABI parity), and harvests text symbols.

```bash
docker run --rm -v "$(cygpath -m "$(pwd)"):/work" fnverify -c '
mkdir -p /tmp/portsrc/src /tmp/portsrc/cross-headers /tmp/portsrc/tinyxml2
rsync -aq /work/src/ /tmp/portsrc/src/
rsync -aq /work/tools/asm-verify/cross-headers/ /tmp/portsrc/cross-headers/
cp /work/build/_deps/tinyxml2-src/tinyxml2.h /tmp/portsrc/tinyxml2/

# C++11 -> C++03 patches:
# 1. `explicit operator bool` is C++11; strip explicit
# 2. `using Foo = Bar;` template-aliases are C++11; rewrite as typedef
find /tmp/portsrc/src -name "*.h" -o -name "*.cpp" | xargs sed -i \
    -e "s/explicit operator bool/operator bool/g" \
    -e "s|using \([A-Za-z_][A-Za-z_0-9]*\) = \(.*\);|typedef \2 \1;|g"

mkdir -p /tmp/portsyms
CXX=arm-none-eabi-g++
CXXFLAGS="-mthumb -mcpu=cortex-a8 -mfloat-abi=hard -mfpu=vfpv3 -fshort-enums -fshort-wchar"
CXXFLAGS="$CXXFLAGS -std=gnu++0x -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables"
CXXFLAGS="$CXXFLAGS -fpermissive -include /tmp/portsrc/cross-headers/fn-cxx11-shims.h"
INCS="-I/tmp/portsrc/src -I/tmp/portsrc/src/engine -I/tmp/portsrc/src/game -I/tmp/portsrc/src/screens -I/tmp/portsrc/src/hud -I/tmp/portsrc/src/entities -I/tmp/portsrc/src/platform -I/tmp/portsrc/src/debug -I/tmp/portsrc/cross-headers -I/tmp/portsrc/tinyxml2"

ok=0; fail=0
> /tmp/compile_failures.txt
cd /tmp/portsrc
# Skip platform-glue (no portable symbols to diff against the binary):
#   1. *SDL.cpp suffix convention (mirrors binary *Bada classifier).
#   2. Anything under src/platform/sdl/ (whole-directory SDL backend).
#   3. Explicit-name exclusions for entry points etc. that don't fit either rule:
#        - src/main.cpp (SDL_main entry; never has portable symbols)
# Add to the explicit list when a new file is portable-named but really platform-only.
for cpp in $(find src -name "*.cpp" \
                 ! -name "*SDL.cpp" \
                 ! -path "src/platform/sdl/*" \
                 ! -path "src/main.cpp"); do
    rel=${cpp#src/}
    obj=/tmp/portsyms/$(echo "$rel" | tr "/" "_").o
    if $CXX $CXXFLAGS $INCS -c "$cpp" -o "$obj" 2>/dev/null; then
        ok=$((ok+1))
    else
        fail=$((fail+1))
        echo "$cpp" >> /tmp/compile_failures.txt
    fi
done
echo "compiled OK: $ok, failed: $fail"

ls /tmp/portsyms/*.o | xargs arm-none-eabi-nm 2>/dev/null \
  | awk "\$2 ~ /^[Tt]\$/ {print \$3}" | grep -v "^\." | sort -u \
  > /work/tmp/symbol-diff/port_full_mangled.txt
arm-none-eabi-c++filt < /work/tmp/symbol-diff/port_full_mangled.txt | sort -u \
  > /work/tmp/symbol-diff/port_full_demangled.txt

comm -23 /work/tmp/symbol-diff/binary_symbols_mangled.txt /work/tmp/symbol-diff/port_full_mangled.txt \
  > /work/tmp/symbol-diff/missing_full_mangled.txt
arm-none-eabi-c++filt < /work/tmp/symbol-diff/missing_full_mangled.txt | sort -u \
  > /work/tmp/symbol-diff/missing_full_demangled.txt
cp /tmp/compile_failures.txt /work/tmp/symbol-diff/compile_failures.txt
'
```

Expected: ~67/99 TUs compile (the 32 failures use C++11 lambdas / range-for / shared_ptr-via-`<memory>` that GCC 4.4.1 cannot parse — fix `Delegate.h` template-aliases and replace lambdas with free functions to lift this).

### 3. Generate the organized report

Run the Python classifier (writes `tmp/symbol-diff/missing_organized.md`):

```python
python << 'PY'
import re, pathlib
from collections import defaultdict

binary_demangled = pathlib.Path('tmp/symbol-diff/binary_symbols_demangled.txt').read_text(encoding='utf-8', errors='ignore').splitlines()
port_demangled = set(pathlib.Path('tmp/symbol-diff/port_full_demangled.txt').read_text(encoding='utf-8', errors='ignore').splitlines())
port_mangled = set(pathlib.Path('tmp/symbol-diff/port_full_mangled.txt').read_text(encoding='utf-8', errors='ignore').splitlines())
binary_mangled = pathlib.Path('tmp/symbol-diff/binary_symbols_mangled.txt').read_text(encoding='utf-8', errors='ignore').splitlines()

def parse_sym(s):
    s = s.strip()
    s = re.sub(r'\s+const\s*&?$', '', s)
    m = re.match(r'^(?:[\w:<>,\s*&]+?\s+)?([\w]+(?:::[\w~]+)*)\s*\(', s)
    if not m: return None
    full = m.group(1)
    if '::' in full:
        cls, mth = full.rsplit('::', 1)
        return (cls.split('::')[-1], mth)
    return (None, full)

missing_demangled = [s for s in binary_demangled if s not in port_demangled]
missing_by_class = defaultdict(list)
for s in missing_demangled:
    p = parse_sym(s)
    if not p: continue
    cls, mth = p
    if cls: missing_by_class[cls].append(mth)

# Classification rules — keep in sync with project policy.
BADA = {'BadaSound','DisplayManagerBada','MAMAudioThread','MAMAudioController','MortarAudioMixerBada',
        'Texture2DFromFile_Bada','GraphicsBada','InputBada','FileBada','Interlocked','StackHeap',
        'LinkedHeap','MemoryPool','PacketSerializer','BadaApplication','GlesForm','ComboBox','ListBox',
        'EditField','InputDeviceBada','GeometryBinding_Bada','IIndexStream_Bada','IVertexStream_Bada',
        'IndexStreamBasic_Bada','VertexStreamBasic_Bada','BadaTextureData','VertexElement_Bada',
        'FileSystem_Direct','IFile_Direct'}
LIBRARY = {'TiXmlNode','TiXmlElement','TiXmlDocument','TiXmlAttribute','TiXmlBase','TiXmlComment',
           'TiXmlDeclaration','TiXmlText','TiXmlUnknown','TiXmlHandle','TiXmlPrinter','TiXmlString',
           'TiXmlAttributeSet','TiXmlDTDInfo','TiXmlParsingData'}
DEFUNCT = {'NetworkManager','OpenFeintNewsRenderer','LeaderboardScreen','FriendLeaderboardItem',
           'LeaderboardItem','LeaderboardManager','OpenFeint','P2PMessage','FNHighscoreList',
           'FNHighscore','GameCenter','OpenFeintNewsRenderInfo','FruitSlicedPacket','PointsPacket',
           'WaveSyncPacket','NetworkPacket','LeaderboardList','PacketDeserializer','StartGamePacket',
           'PacketFactory'}
PHANTOM = {'AttractScreen','BladeScreen','LocalScoreEntryScreen','VSGameOverScreen','OptionsScreen',
           'ChallengeScreenSL','ChallengeHistoryScreenSL','CreateChallengeScreenSL','BuyStarfruitScreen',
           'OperatorAlertControl','CreditCounterControl','MultiplayerTutorialControl','ZenVersusControl',
           'MainScreenArcade','UpsellScreen','UpsellScreenElement','KeyboardControl'}
FALSE_POS = {'Mortar','std','FruitNinja'}

def classify(cls):
    if cls in FALSE_POS: return 'falsepos'
    if cls in BADA: return 'bada'
    if cls in LIBRARY: return 'library'
    if cls in DEFUNCT: return 'defunct'
    if cls in PHANTOM: return 'phantom'
    return 'gameplay'

cat_buckets = defaultdict(list)
for cls, mlist in missing_by_class.items():
    cat_buckets[classify(cls)].append((cls, len(mlist)))
for k in cat_buckets: cat_buckets[k].sort(key=lambda x: -x[1])

out = pathlib.Path('tmp/symbol-diff/missing_organized.md')
with out.open('w', encoding='utf-8') as f:
    f.write('# Binary vs Port Symbol Diff (Cross-Toolchain) -- Organized\n\n')
    f.write(f'- Binary text symbols: **{len(binary_demangled)}**\n')
    f.write(f'- Port symbols (cross-compiled): **{len(port_demangled)}**\n')
    f.write(f'- Missing classes: **{len(missing_by_class)}**\n\n')
    headers = [
        ('falsepos', 'Detection False Positives (namespace mis-detected as class)'),
        ('bada',     'Bada Platform -- replaced by SDL2 / port equivalents'),
        ('library',  'Third-Party tinyxml2 (linked separately)'),
        ('defunct',  'Defunct Online -- // Defunct: stubs'),
        ('phantom',  'Phantom -- only _GLOBAL__I_ exists in binary'),
        ('gameplay', 'Gameplay / Engine -- REAL gaps'),
    ]
    for key, title in headers:
        items = cat_buckets.get(key, [])
        if not items: continue
        f.write(f'## {title}\n\n')
        f.write(f'**{len(items)} classes**, {sum(n for _,n in items)} methods.\n\n')
        f.write('| Methods | Class |\n|---:|---|\n')
        for cls, n in items[:50]:
            f.write(f'| {n} | `{cls}` |\n')
        f.write('\n')
print(f'Wrote {out}')
PY
```

### 4. Show the summary

Read `tmp/symbol-diff/missing_organized.md` and present:
- Per-category class+method counts (False positives / Bada / tinyxml2 / Defunct / Phantom / Real)
- Top 10-20 real gameplay gaps for next-priority planning
- Compile-failure caveat (32 TUs typically blocked on C++11 features)

## Output
- Files in `tmp/symbol-diff/`:
  - `binary_symbols_{mangled,demangled}.txt`
  - `port_full_{mangled,demangled}.txt`
  - `missing_full_{mangled,demangled}.txt`
  - `compile_failures.txt`
  - `missing_organized.md` (the headline deliverable)
- Summary printed to user

## Updating classification rules

The `BADA / LIBRARY / DEFUNCT / PHANTOM / FALSE_POS` sets in step 3 should be kept in sync with `docs/engine/online-services-audit.md` and `docs/TODO.md` "Phantom" lists. When a new class is identified as defunct/Bada/phantom, add it to the relevant set so the report classifies it correctly.
