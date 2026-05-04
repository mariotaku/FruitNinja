#!/usr/bin/env python
# Classify the symbol-diff missing list into categories and write
# tmp/symbol-diff/missing_organized.md.
#
# Categories (kept in sync with .claude/skills/symbol-diff.md):
#   falsepos -- detection false positives (namespace mis-detected as class)
#   bada     -- Bada platform classes (replaced by SDL2 / port equivalents)
#   library  -- third-party tinyxml2 (linked separately)
#   defunct  -- // Defunct: stubs (online services, P2P, GameCenter, etc.)
#   phantom  -- only _GLOBAL__I_ exists in binary (orphan TUs)
#   gameplay -- REAL gaps -- next-priority targets

import re
import pathlib
from collections import defaultdict

ROOT = pathlib.Path(__file__).resolve().parents[2]
DIFF = ROOT / 'tmp' / 'symbol-diff'

binary_demangled = (DIFF / 'binary_symbols_demangled.txt').read_text(
    encoding='utf-8', errors='ignore').splitlines()
port_demangled = set((DIFF / 'port_full_demangled.txt').read_text(
    encoding='utf-8', errors='ignore').splitlines())


def parse_sym(s):
    s = s.strip()
    s = re.sub(r'\s+const\s*&?$', '', s)
    m = re.match(r'^(?:[\w:<>,\s*&]+?\s+)?([\w]+(?:::[\w~]+)*)\s*\(', s)
    if not m:
        return None
    full = m.group(1)
    if '::' in full:
        cls, mth = full.rsplit('::', 1)
        return (cls.split('::')[-1], mth)
    return (None, full)


missing_demangled = [s for s in binary_demangled if s not in port_demangled]
missing_by_class = defaultdict(list)
for s in missing_demangled:
    p = parse_sym(s)
    if not p:
        continue
    cls, mth = p
    if cls:
        missing_by_class[cls].append(mth)

# Classification rules -- keep in sync with project policy + RE classification.
BADA = {
    'BadaSound', 'DisplayManagerBada', 'MAMAudioThread', 'MAMAudioController',
    'MortarAudioMixerBada', 'Texture2DFromFile_Bada', 'GraphicsBada',
    'InputBada', 'FileBada', 'Interlocked', 'StackHeap', 'LinkedHeap',
    'MemoryPool', 'PacketSerializer', 'BadaApplication', 'GlesForm', 'ComboBox',
    'ListBox', 'EditField', 'InputDeviceBada', 'Geometry', 'Geometry_Bada',
    'GeometryBinding', 'GeometryBinding_Bada',
    'IIndexStream_Bada', 'IVertexStream_Bada', 'IndexStreamBasic_Bada',
    'VertexStreamBasic_Bada', 'BadaTextureData', 'VertexElement_Bada',
    'FileSystem_Direct', 'IFile_Direct',
    # SoundManager: portable in binary but port impl lives in SoundManagerSDL.cpp
    # (excluded by *SDL.cpp filter) with simplified signatures. Classified
    # Bada-platform-replaced until split + signature audit lands.
    'SoundManager',
}
LIBRARY = {
    'TiXmlNode', 'TiXmlElement', 'TiXmlDocument', 'TiXmlAttribute', 'TiXmlBase',
    'TiXmlComment', 'TiXmlDeclaration', 'TiXmlText', 'TiXmlUnknown', 'TiXmlHandle',
    'TiXmlPrinter', 'TiXmlString', 'TiXmlAttributeSet', 'TiXmlDTDInfo',
    'TiXmlParsingData',
}
DEFUNCT = {
    'NetworkManager', 'OpenFeintNewsRenderer', 'LeaderboardScreen',
    'FriendLeaderboardItem', 'LeaderboardItem', 'LeaderboardManager',
    'OpenFeint', 'P2PMessage', 'FNHighscoreList', 'FNHighscore', 'GameCenter',
    'OpenFeintNewsRenderInfo', 'FruitSlicedPacket', 'PointsPacket',
    'WaveSyncPacket', 'NetworkPacket', 'LeaderboardList', 'PacketDeserializer',
    'StartGamePacket', 'PacketFactory',
}
PHANTOM = {
    'AttractScreen', 'BladeScreen', 'LocalScoreEntryScreen', 'VSGameOverScreen',
    'OptionsScreen', 'ChallengeScreenSL', 'ChallengeHistoryScreenSL',
    'CreateChallengeScreenSL', 'BuyStarfruitScreen', 'OperatorAlertControl',
    'CreditCounterControl', 'MultiplayerTutorialControl', 'ZenVersusControl',
    'MainScreenArcade', 'UpsellScreen', 'UpsellScreenElement', 'KeyboardControl',
}
FALSE_POS = {'Mortar', 'std', 'FruitNinja'}


def classify(cls):
    if cls in FALSE_POS: return 'falsepos'
    if cls in BADA:      return 'bada'
    if cls in LIBRARY:   return 'library'
    if cls in DEFUNCT:   return 'defunct'
    if cls in PHANTOM:   return 'phantom'
    return 'gameplay'


cat_buckets = defaultdict(list)
for cls, mlist in missing_by_class.items():
    cat_buckets[classify(cls)].append((cls, len(mlist)))
for k in cat_buckets:
    cat_buckets[k].sort(key=lambda x: -x[1])

out = DIFF / 'missing_organized.md'
with out.open('w', encoding='utf-8') as f:
    f.write('# Binary vs Port Symbol Diff (Cross-Toolchain) -- Organized\n\n')
    f.write('- Binary text symbols: **{}**\n'.format(len(binary_demangled)))
    f.write('- Port symbols (cross-compiled): **{}**\n'.format(len(port_demangled)))
    f.write('- Missing classes: **{}**\n'.format(len(missing_by_class)))
    f.write('- Missing methods: **{}**\n\n'.format(
        sum(len(v) for v in missing_by_class.values())))
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
        if not items:
            continue
        f.write('## {}\n\n'.format(title))
        f.write('**{} classes**, {} methods.\n\n'.format(
            len(items), sum(n for _, n in items)))
        f.write('| Methods | Class |\n|---:|---|\n')
        for cls, n in items[:50]:
            f.write('| {} | `{}` |\n'.format(n, cls))
        f.write('\n')
print('Wrote', out)
