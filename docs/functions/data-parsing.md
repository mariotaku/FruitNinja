# Data Parsing Functions (XML Loaders)

All XML loading uses TinyXML (`TiXmlDocument::LoadFile` → `FirstChildElement` → iterate `NextSiblingElement`).

---

## PowerUpManager::Load (0x00119cb0, 80 lines)

Parses `poweruplist.xml` into PowerUp objects.

```c
void PowerUpManager::Load() {
    TiXmlDocument* doc = new TiXmlDocument("Data/xml/poweruplist.xml");
    m_PowerUpMap.clear();       // map<ulong, PowerUp*>
    m_PurchaseList.clear();     // list<PowerUp*>
    
    if (doc->LoadFile()) {
        TiXmlElement* root = doc->FirstChildElement("powerInfoFile");
        
        // Phase 1: Parse <power> elements
        for (TiXmlElement* e = root->FirstChildElement("power");
             e != NULL; e = e->NextSiblingElement("power")) {
            PowerUp* pu = new PowerUp();   // 0xCC bytes
            PowerUp::Parse(pu, e);
            m_PowerUpMap[pu->m_NameHash] = pu;
            if (pu->Purchaseable())
                m_PurchaseList.push_back(pu);
        }
        
        // Phase 2: Parse <effect> elements (screen effects)
        for (TiXmlElement* e = root->FirstChildElement("effect");
             e != NULL; e = e->NextSiblingElement("effect")) {
            if (e->Attribute("name")) {
                ScreenEffect se;
                ScreenEffect::Parse(&se, e);
                m_EffectMap[se.m_NameHash] = se;   // map<ulong, ScreenEffect>
            }
        }
    }
    delete doc;
}
```

**Struct**: PowerUp = 0xCC bytes. ScreenEffect map at PowerUpManager+0x38, purchase list at +0x58.

---

## BonusManager::Init (0x0010e8fc, 49 lines)

Parses `bonusawards.xml` into BonusType entries.

```c
void BonusManager::Init() {
    TiXmlDocument* doc = new TiXmlDocument("Data/xml/bonusawards.xml");
    
    if (doc->LoadFile()) {
        TiXmlElement* root = doc->FirstChildElement("bonusAwardFile");
        
        // Phase 1: Parse <bonusType> elements
        for (TiXmlElement* e = root->FirstChildElement("bonusType");
             e != NULL; e = e->NextSiblingElement("bonusType")) {
            BonusType bt;
            BonusType::Parse(&bt, e);
            m_BonusTypes.push_back(bt);    // vector<BonusType>
        }
        
        // Phase 2: Parse <order> elements (bonus display order)
        for (TiXmlElement* e = root->FirstChildElement("order");
             e != NULL; e = e->NextSiblingElement("order")) {
            int val;
            e->QueryIntAttribute("id", &val);
            m_DisplayOrder.push_back(val);  // vector<int> at +0x14
        }
    }
    delete doc;
}
```

**Struct**: BonusType parsed via `BonusType::Parse`. Display order is a separate `vector<int>`.

---

<!-- Analysed: 2026-04-25T10:30 -->

## ItemManager::LoadItemData (0x00113200, 190 lines)

Parses `itemlist.xml` + `ItemSave.xml` into ItemInfo/SlashModInfo objects.
Full analysis in `docs/structs/items.md`.

```
XML:  xml/itemList.xml  (root = <itemManagerFile>)
Save: ItemSave.xml      (root = <item_save_file version coins coinsTotal levelStartCoins>)
```

**Phase 1 — Parse `<item>` elements:**
- `e->Attribute("type")` → `ParseItemType()` → 0=SLASH_MODIFIER, 1=BACKGROUND, 2=UPSELL, 3=REMOVEADS
- type==0 → `new SlashModInfo()` (0x110 bytes); else `new ItemInfo()` (0x40 bytes)
- Virtual dispatch `vtable[+0x10](item, e)` → `ItemInfo::Parse @ 0x0011293c` or `ParseSlashModInfo @ 0x001126c0`
- Achievement-gate: if `FruitSaveData::IsAchievementUnlocked(hash)==0` and `AchievementManager::AchievementExists()==0` and `item->m_Cost > 0` → `item->m_bSeen=0` (new badge) + `item->m_Cost=-1` (free) + `ShopScreen::NewItem()`
- If already unlocked: `item->m_Cost = -1` (auto-unlock)
- Push to `m_Items` (+0x10), insert into `m_ByHash` (+0x1c), and `m_ByHashType[type]` (+0x34+type×0x18)
- First item of each type (except type==3) → `m_DefaultItems[type]` (+0x00..+0x0f)

**Phase 2 — Load `ItemSave.xml`:**
- Root attrs: `QueryIntAttribute("coins", &sd->m_Coins)`, `"coinsTotal"`, `"levelStartCoins"` (FruitSaveData +0x20/+0x24/+0x28)
- `<boughtItems>/<item name=N seen=true|false>` → hash lookup in m_ByHash → `m_Cost=-1`, `m_bSeen=0|1`
- `<equippedItems>/<item name=N>` → hash lookup → `m_DefaultItems[item->m_Type] = item`

**Phase 3 — Apply equipped items:**
- Loop `i=0..3`: `SetEquippedItem((ItemType)i, m_DefaultItems[i])`

**Key string constants (all GOT-relative from iVar6=0x1ec130):**

| DAT offset | String       | Use                            |
|------------|--------------|--------------------------------|
| 0x1ba064   | `xml/itemList.xml`  | TiXmlDocument path      |
| 0x1ba075   | `itemManagerFile`   | Root element tag        |
| 0x1b9e95   | `item`              | Child element tag       |
| 0x1b9372   | `type`              | Item type attribute     |
| 0x1c3173   | `name`              | Item name attribute     |
| 0x1b9e4d   | `item_save_file`    | Save root element       |
| 0x1b9e68   | `coins`             | Save root attribute     |
| 0x1b9e6e   | `coinsTotal`        | Save root attribute     |
| 0x1b9e79   | `levelStartCoins`   | Save root attribute     |
| 0x1b9e89   | `boughtItems`       | Save child element      |
| 0x1b9ea5   | `seen`              | Bought item attribute   |
| 0x1b9ea0   | `true`              | Boolean compare string  |
| 0x1b9eaa   | `equippedItems`     | Save child element      |

**Item types**: 0=SLASH_MODIFIER (SlashModInfo, 0x110 bytes), 1=BACKGROUND (ItemInfo, 0x40 bytes), 2=UPSELL, 3=REMOVEADS.
**5 maps** total: 1 global + 4 per-type.
**Call site**: `InitialiseData @ 0x0010b7ca`, step 14 (after AchievementManager::LoadAchievementInfo, before BonusManager::Init).
**Full struct + method pseudocode**: see `docs/structs/items.md`.

---

## AchievementManager::LoadAchievementInfo (0x00109200, 279 lines)

Parses `achievementlist.xml` into AchievementInfo objects.

```c
void AchievementManager::LoadAchievementInfo() {
    m_AchievementMap.clear();   // map<ulong, AchievementInfo*>
    
    // Load 2 UI textures for achievement display
    TextureManager::LoadLocalisedTexture("achievement_icon_1.tex");
    TextureManager::LoadLocalisedTexture("achievement_icon_2.tex");
    
    TiXmlDocument* doc = new TiXmlDocument("Data/xml/achievementlist.xml");
    
    if (doc->LoadFile()) {
        TiXmlElement* root = doc->FirstChildElement("achievementFile");
        
        for (TiXmlElement* e = root->FirstChildElement("achievement");
             e != NULL; e = e->NextSiblingElement("achievement")) {
            
            // Only load achievements that are NOT yet unlocked
            if (FruitSaveData::IsAchievementUnlocked(e))
                continue;
            
            AchievementInfo* ai = new AchievementInfo();  // 0x1A0 bytes
            
            // Parse attributes
            ai->displayString = e->Attribute("display");  // +0x00
            ai->name = e->Attribute("name");               // +0x40
            ai->nameHash = StringHash(ai->name);           // +0x80
            ai->description = e->FirstChildElement("desc")->GetText();  // +0x88
            e->QueryIntAttribute("target", &ai->targetCount);   // +0x18C
            e->QueryIntAttribute("threshold", &ai->threshold);  // +0x188
            e->QueryIntAttribute("hidden", &ai->hidden);        // +0x198
            
            // Load icon texture: "tex_%s" format
            ai->icon = TextureManager::Load(sprintf("tex_%s", iconAttr));
            
            // Parse game mode bitmask (comma-separated)
            ai->modeBitmask = ParseGameModeBitmask(e->Attribute("modes"));
            
            // Categorize by type hash (11 known types)
            ulong typeHash = StringHash(e->Attribute("type"));
            switch (typeHash) {
                case TYPE_4: case TYPE_5: case TYPE_8:
                    key = StringHash(e->Attribute("name"));
                    m_TypeMaps[type][key] = ai;
                    break;
                case TYPE_9:
                    ai->specificOrder = new SpecificOrder(name);  // 0x1C0
                    break;
                case TYPE_10:
                    m_TypeMap10[nameHash] = ai;
                    break;
                case TYPE_1: case TYPE_2:
                    key = m_Counter++;  // at +0x154
                    m_TypeMaps[type][key] = ai;
                    break;
                default:
                    key = ai->threshold;
                    m_TypeMaps[type][key] = ai;
                    break;
            }
        }
    }
    delete doc;
}
```

**Key detail**: Only *unlocked* achievements are skipped — the game loads all unearned achievements for tracking.
**AchievementInfo** = 0x1A0 bytes. 11 achievement type categories, stored in per-type sub-maps.

---

## PSPParticleManager::LoadFile (0x00115f60, 722 lines)

Parses `particles_fast.xml` / `particles_slow.xml` into emitter templates. This is the largest XML parser in the codebase.

```c
void PSPParticleManager::LoadFile(char* basePath, char* xmlPath, char** nameBuffer) {
    // 1. Initialize particle pool (if first call)
    if (m_pParticleArray == NULL) {
        // Allocate 0x400 (1024) PSPParticle objects, each 0xA4 bytes
        m_pParticleArray = new PSPParticle[1024];
        for (int i = 1; i < 1024; i++) {
            memset(&particles[i], 0, 0xA4);
            particles[i].nextFreeIdx = i + 1;  // free list chain
        }
        particles[1023].nextFreeIdx = 0;        // end of free list
        m_FirstFree = 1;
    }
    
    // 2. Initialize emitter pool (if first call)
    if (m_pEmitterPool == NULL) {
        m_pEmitterPool = new MemoryPool<PSPParticleEmitter>(0x78);
    }
    
    // 3. Parse XML
    TiXmlDocument doc(xmlPath);
    if (!doc.LoadFile()) {
        PowerManager::Update();  // fallback?
        return;
    }
    
    TiXmlElement* root = doc.FirstChildElement("particle_file");
    TiXmlElement* body = root->FirstChildElement("body");
    TiXmlElement* emitterElem = body->FirstChildElement("emitter");
    
    // 4. Allocate template array
    float* templates = new float[0xa0a0 / 4];  // ~0x2828 templates
    m_TemplateCount = 0;
    
    // 5. Parse each <emitter> element
    while (emitterElem != NULL) {
        char* name = emitterElem->Attribute("name");
        uint nameHash = StringHash(name);
        
        // Store name to nameBuffer if provided
        if (nameBuffer) strcpy(nameBuffer[m_TemplateCount], name);
        
        // Zero-init template (0xB8 bytes per PSPEmitterTemplate)
        memset(template, 0, 0xB8);
        
        // Parse template fields:
        //   life, shape (Point/Line/etc), size (start/end)
        //   colour (start RGBA, end RGBA) — ParseInt4 for each
        //   texture reference — load via TextureManager
        //   aspect ratio from texture dimensions
        
        // Parse <particleSet> children:
        for each <particleSet>:
            // time (start/stop), particleNumber (init/perSec)
            // velocity (min xyz, max xyz)
            // acceleration, gravity, rotation, scale
            // fadeIn/fadeOut, drag, etc.
        
        // Hash lookup table: local_11b0[m_TemplateCount] = nameHash
        
        m_TemplateCount++;
        emitterElem = emitterElem->NextSiblingElement("emitter");
    }
    
    // 6. Build hash→index lookup
    // ... (remaining 400+ lines handle particleSet sub-parsing)
}
```

### PSPEmitterTemplate Layout (0xB8 bytes per template)

Key fields parsed from XML:
- `+0x00`: float life (seconds)
- `+0x08-0x0D`: shape type and dimensions
- `+0x20-0x37`: start colour (RGBA as floats)
- `+0x38-0x3B`: end colour
- `+0x94-0x9B`: colour bytes (packed from RGBA parse)
- `+0xAC`: SmartPtr\<Texture\> texture reference
- `+0xB0`: float aspect ratio (width/height)

### Particle Pool

- **1024 particles** max (`PSPParticle`, 0xA4 bytes each)
- Free list via `nextFreeIdx` at particle+0x40
- Emitter pool uses `MemoryPool<PSPParticleEmitter>` (0x78 bytes each)

---

## XML File Summary

| XML File | Loader | Address | Lines | Output |
|----------|--------|---------|-------|--------|
| poweruplist.xml | PowerUpManager::Load | 0x119cb0 | 80 | map\<hash, PowerUp*\> + list\<PowerUp*\> |
| bonusawards.xml | BonusManager::Init | 0x10e8fc | 49 | vector\<BonusType\> + vector\<int\> |
| itemlist.xml | ItemManager::LoadItemData | 0x113200 | 190 | vector\<ItemInfo*\> + per-type maps |
| achievementlist.xml | AchievementManager::LoadAchievementInfo | 0x109200 | 279 | map\<hash, AchievementInfo*\> + type sub-maps |
| particles_fast/slow.xml | PSPParticleManager::LoadFile | 0x115f60 | 722 | Template array + hash→index lookup |

---

## See Also

- [Power-ups system](../systems/power-ups.md) — PowerUp struct, modifiers
- [Particles system](../engine/particles.md) — PSPEmitterTemplate, AddEmitter
- [Wave system](../systems/wave-system.md) — WAVE_INFO XML parsing (in WaveManager::Init)
- [Resources](../resources.md) — XML file schemas and examples
- [Data structs](../structs/data.md) — FRUIT_INFO (loaded by Fruit::LoadInfo, separate from these)
