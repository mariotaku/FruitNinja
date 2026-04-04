# Resource Files (FruitNinjaBada/)

## Directory Structure

```
FruitNinjaBada/
  Bin/FruitNinja.exe          — ARM32 ELF binary
  Data/
    fonts/                    — 12 .fnt bitmap font definitions
    input/                    — 2 .txt input mapping configs
    models/
      fruit/                  — fruit 3D models (.mad + .mmd)
      effects/                — slice/splat effect models
      hud/                    — HUD element models
    particles/                — particle textures (.tex) + XML configs
    sfx/                      — 133 raw PCM sound effects (.wav.pcm)
    sound/                    — music files
    stringtables/             — 10 .str localisation files
    textures/                 — 452 .tex texture files
    xml/                      — 17 XML data files
    ItemSave.xml              — save data template
  Info/                       — Bada app manifest
  Res/                        — Bada resources
  Share/                      — Bada shared data
  Signature.xml               — Bada app signature
```

## File Type Summary

| Extension | Count | Format |
|-----------|-------|--------|
| .tex | 452 | Halfbrick texture (header + pixel data) |
| .pcm | 133 | Raw PCM audio (16-bit, 16kHz) |
| .mmd | 122 | Halfbrick mesh model data |
| .mad | 92 | Halfbrick mesh animation data |
| .xml | 17 | TinyXML data files |
| .fnt | 12 | Bitmap font definitions |
| .str | 10 | String table (localisation) |
| .txt | 3 | Input config / text |

## Binary File Formats

### .tex — Halfbrick Texture

Header (4 bytes):
```
+0x00: byte  format (0x06 = RGBA4444?)
+0x01: byte  flags  (0x06)
+0x02: short width  (e.g. 0x0040 = 64px, little-endian)
+0x04: short height (e.g. 0x0040 = 64px)
```
Followed by raw pixel data. Format byte likely indicates colour depth/encoding.

### .mad — Halfbrick Model Animation Data

Magic: `HBR0` (Halfbrick Resource v0)
```
+0x00: char[4]  magic ("HBR0")
+0x04: int      version? (0)
+0x08: int      flags? (0)
+0x0c: int      data_offset
+0x10: int      count (1)
+0x12: short    path_length
+0x14: char[]   original_path (e.g. "D:\Projects\iPhoneDev\FruitNinja\Asset_wor...")
```
Contains original Windows build paths — confirms iPhone origin cross-compiled for Bada.

### .mmd — Halfbrick Mesh Model Data

Magic: `HBR0` (nested)
```
+0x00: "HBR0"  outer container
+0x04: version, sub-count
+0x08: "HBR0"  inner mesh data
...
Contains texture references (e.g. "textures\fruit_big_sheet_1"), map names ("Map #1")
```

### .wav.pcm — Raw PCM Audio

```
+0x00: int      unknown (1)
+0x04: int      sample_rate? (0x3E80 = 16000)
+0x08: int      channels? (16)
+0x0c: int      data_size
+0x10: int      unknown (0)
+0x14: raw PCM  sample data (16-bit signed, little-endian)
```

### .fnt — Bitmap Font

Binary format. References .tex texture atlas files. Each font has a corresponding `*_0.tex` atlas.

### .str — String Table

Binary format for localised strings. 10 files suggest 10 language variants.

## XML Data Files

All in `Data/xml/`, loaded by TinyXML:

| File | Loaded by | Purpose |
|------|-----------|---------|
| **fruitlist.xml** | Fruit::LoadInfo | All fruit types with properties, colours, facts, sounds |
| **wavelist.xml** | WaveManager::Init | Wave definitions (simple mode?) |
| **originalwavelist.xml** | WaveManager::Init | Classic mode waves |
| **arcadewavelist.xml** | WaveManager::Init | Arcade mode waves |
| **zenwavelist.xml** | WaveManager::Init | Zen mode waves |
| **zenvswavelist.xml** | WaveManager::Init | Zen vs multiplayer waves |
| **combowavelist.xml** | WaveManager::Init | Combo challenge waves |
| **intensewavelist.xml** | WaveManager::Init | Intense difficulty waves |
| **survivalwavelist.xml** | WaveManager::Init | Survival mode waves |
| **poweruplist.xml** | PowerUpManager::Load | Power-up definitions with modifiers and effects |
| **itemlist.xml** | ItemManager | Blade types and shop items |
| **achievementlist.xml** | AchievementManager | Achievement definitions |
| **bonusawards.xml** | BonusManager::Init | Bonus award criteria |
| **musicdesc.xml** | Sound system | Music track descriptions |
| **ItemSave.xml** | FruitSaveData | Save data template |

## XML Format Details

### fruitlist.xml

```xml
<fruitInfoFile version="1.0.0">
  <critical chance="50" chance_inc="30" score="10" colour="R,G,B,A" 
           scale="1.25" splats="15" spread="1.25" disappear_speed="1"/>
  <bomb size="55" collision="35"/>
  <FruitInfo name="apple" singular="FRUITNAME_APPLE" plural="FRUITNAME_PLURAL_APPLE"
             singularEnglish="apple" pluralEnglish="apples"
             chance="100" scale="60" colour="R,G,B,A" collision="5"
             onSide="true" factColour="R,G,B" factTexture="sml_ap"
             onlySprinkle="true" hasSplatSeeds="0" hitInfluence="1.0"
             score="0">
    <fact>FRUIT_FACT_00</fact>
    <impact_sound>Impact-Apple</impact_sound>
  </FruitInfo>
</fruitInfoFile>
```

16 fruit types: apple, banana, orange, watermelon, strawberry, kiwifruit, pineapple, plum, pear, mango, apple_red, lime, dragon (score=50), coconut, passionfruit, lemon.

### wavelist.xml / originalwavelist.xml

```xml
<waveManagerFile version="1.0.0">
  <defaults waveChance="90" waveChanceGrowth="0.33" criticalChance="1.0"/>
  <WaveInfo waveNo="0">
    <Spawn type="mango,watermelon" min="1" max="1"/>
    <Wave_dt dt="0.90" inc="0"/>
    <NextWaveDelay delay="1.0"/>
  </WaveInfo>
  <WaveInfo waveNo="9" until="13">
    <Spawn type="random, random, bomb" min="4" max="6" mininc="1" maxinc="1" delay="0.6"/>
    <ChooseFrom types="apple, random"/>
    <Wave_dt dt="0.95"/>
  </WaveInfo>
</waveManagerFile>
```

### poweruplist.xml

```xml
<powerInfoFile version="1.0.0">
  <power name="freeze" single="true" colour="255,0,0" bar="tex" popup="tex">
    <time_mod length="7" slowClock="0">
      <dt_speed transitionTime="0.5" dt="0.5"/>
    </time_mod>
    <effect>
      <sound name="Bonus-Banana-Freeze"/>
      <image texture="ice_cover" pos="0,0,-5000" scaleToScreen="true" 
             transitionTime="0.75" transition="fade" drawOrder="before_splats"/>
    </effect>
    <wave_mod length="7" waveOveride="-100"/>
  </power>
</powerInfoFile>
```

Power-up types: ready_set_go, freeze, speed (frenzy), score_mult (double score).

### particles_fast.xml / particles_slow.xml

```xml
<particle_file version="1.0.0">
  <body>
    <emitter name="watermelon">
      <life>5</life>
      <shape>Point</shape>
      <size start="1" end="1"/>
      <particleSet name="w_big_splat">
        <time start="0" stop="5"/>
        <particleNumber init="15" perSec="0"/>
        <velocity min="-500 200 0" max="500 400 0"/>
      </particleSet>
    </emitter>
  </body>
</particle_file>
```

Two variants: `particles_fast.xml` (for fast hardware) and `particles_slow.xml` (reduced particles).

## Model Naming Convention

Each fruit has multiple model variants:
```
{name}.mad              — whole fruit animation
{name}_single.mad       — single mesh (no animation)
{name}_single.mmd       — single mesh data
{name}_a_piece_1.mmd    — sliced half A
{name}_a_piece_2.mmd    — sliced half B (or _b_piece_*)
{name}_outline.mad      — outline effect
{name}_center.mad       — center piece
{name}ice.mad           — frozen variant (for freeze power-up)
```

## Resource Loading Flow

```
GameInitialise()
  → Fruit::LoadInfo()           — parses fruitlist.xml, creates FRUIT_INFO array
  → Fruit::LoadFruitModels()    — loads .mad/.mmd per fruit type → FruitModelInfo array
  → SlashEntity::LoadContent()  — loads blade textures/models
  → Bomb::LoadContent()         — loads bomb model
  → SplatEntity::LoadContent()  — loads splat textures
  → PowerUpShop::LoadContent()  — loads shop UI textures
  → PreloadSounds()             — loads .wav.pcm files via BadaSound::SFXLoad
  → PSPParticleManager::LoadFile() — parses particles_fast/slow.xml
  → WaveManager::Init()         — parses mode-specific wavelist XML (4 modes)
```

---

## See Also

- [Texture format](engine/formats/textures.md) -- .tex file layout
- [Audio format](engine/formats/audio.md) -- .wav.pcm file format
- [Model format](engine/formats/models.md) -- HBR0 container, vertex streams
- [Font format](engine/formats/fonts.md) -- BMFont .fnt files
- [Wave system](systems/wave-system.md) -- wavelist XML parsing
- [Power-ups system](systems/power-ups.md) -- poweruplist.xml
