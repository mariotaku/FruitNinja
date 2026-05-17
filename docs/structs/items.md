<!-- Analysed: 2026-04-25T14:45 -->

# ItemInfo + ItemManager Structs

Full RE of the item shop system (blade skins, backgrounds, upsells, ad-removal IAPs).
Loaded during `InitialiseData` — called from `GameInitialise` step 14.

---

## ItemInfo (0x40 bytes) @ vtable `0x001e8c50`

Base class for all shop items.

| Offset | Size | Type     | Name            | Notes                                                        |
|--------|------|----------|-----------------|--------------------------------------------------------------|
| +0x00  | 4    | void*    | vtable          | Points to vtable base+8 (see table below)                   |
| +0x04  | 4    | char*    | m_pName         | Internal XML `name` attr; `StringHash` → m_Hash             |
| +0x08  | 4    | uint32   | m_Hash          | `StringHash(m_pName)` — map key for m_ByHash maps           |
| +0x0c  | 4    | int32    | m_Cost          | Cost in coins. -1 = already purchased/free; 0 = default item|
| +0x10  | 1    | int8     | m_Type          | Item type: 0=SLASH_MODIFIER, 1=BACKGROUND, 2=UPSELL, 3=REMOVEADS; 0xFF before parsed |
| +0x11  | 3    | (pad)    | —               | Alignment padding                                            |
| +0x14  | 4    | char*    | m_pTitle        | Localised display title (via GETSTRING); XML `title` attr   |
| +0x18  | 4    | char*    | m_pDescText     | Localised description from `<description>` child text       |
| +0x1c  | 4    | char*    | m_pLockedText   | Locked/cost display text from `<requirements>` text node    |
| +0x20  | 4    | char*    | m_pProgressFmt  | Progress format string; NULL if no `showIfPlayedToday` attr |
| +0x24  | 1    | int8     | m_RequirementType | Requirement unlock type: 0=none/free, 1=?, 2=?, 3=?       |
| +0x25  | 3    | (pad)    | —               | Alignment padding                                            |
| +0x28  | 4    | char*    | m_pTotalStatKey | Name of achievement stat `total` attr (NULL if none)        |
| +0x2c  | 4    | int32    | m_CountDownFrom | countDownFrom value (0 if absent)                           |
| +0x30  | 4    | char*    | m_pTextureName  | Texture asset name (XML `texture` attr); used for thumbnail  |
| +0x34  | 4    | Colour   | m_Colour1       | Parsed from XML `colour` attr (RGBA bytes packed as uint32) |
| +0x38  | 4    | Colour   | m_Colour2       | Second colour slot; initialised same as m_Colour1; XML "titleolour" (vestigial attr, appears unused) |
| +0x3c  | 1    | bool     | m_bSeen         | 1 = item has been seen in shop; 0 = "new item" badge visible |

**Total: 0x40 bytes.**

### ItemInfo ctor defaults (0x001138ac / 0x00113910)

```c
vtable        = ItemInfo_vtable + 8   // 0x001e8c58
m_pName       = NULL  (+0x04)
m_Hash        = 0     (+0x08)
m_Cost        = 0     (+0x0c)
m_Type        = 0xFF  (+0x10)        // 0xFF = "unset"
m_pTitle      = NULL  (+0x14)
m_pDescText   = NULL  (+0x18)
m_pLockedText = NULL  (+0x1c)
m_pProgressFmt = NULL (+0x20)
m_RequirementType = 0 (+0x24)
m_pTotalStatKey = NULL (+0x28)
m_CountDownFrom = 0   (+0x2c)
m_pTextureName  = NULL (+0x30)
m_Colour1 = Colour()  (+0x34)       // default-constructed
m_Colour2 = m_Colour1 (+0x38)       // copy of Colour1 default
m_bSeen   = 1         (+0x3c)       // starts "seen" (not new)
```

### ItemInfo vtable (`0x001e8c50`)

| Vtable slot | Byte offset from vtable_ptr | Address    | Name                  |
|-------------|----------------------------|------------|-----------------------|
| [0]         | –8 (RTTI offset_to_top)    | 0x00000000 | (offset_to_top = 0)   |
| [1]         | –4 (RTTI typeinfo ptr)     | 0x001e8c78 | typeinfo              |
| [2]         | +0x00 (vtable_ptr base)    | 0x00113c70 | ~ItemInfo() in-place  |
| [3]         | +0x04                      | 0x00113ea8 | ~ItemInfo() delete    |
| [4]         | +0x08                      | 0x00113974 | UnEquip()             |
| [5]         | +0x0c                      | 0x00113978 | SetEquipped()         |
| [6]         | +0x10                      | 0x0011293c | Parse(TiXmlElement*)  |

`LoadItemData` dispatches via `(*vtable_ptr[+0x10])(item, xmlNode)` — always calls `Parse`.

**UnEquip() @ 0x00113974** — no-op.

**SetEquipped() @ 0x00113978** — no-op.

**IsLocked() @ 0x0015fa60** — `return this->m_Cost > 0;` (cost == -1 means purchased; cost > 0 means locked/purchaseable)

---

## SlashModInfo : ItemInfo (0x110 bytes) @ ctor `0x00113d58`

Extends `ItemInfo` for `SLASH_MODIFIER` items. Inherits all fields at +0x00..+0x3f.

| Offset | Size | Type            | Name               | Notes                                                 |
|--------|------|-----------------|--------------------|-------------------------------------------------------|
| +0x00  | 0x40 | ItemInfo        | base               | Full ItemInfo struct                                  |
| +0x40  | 4    | Colour*         | m_pColours         | Heap array of `<colour>` entries; NULL if count==0    |
| +0x44  | 4    | int             | m_ColourCount      | Number of `<colour>` child elements parsed            |
| +0x48  | 4    | int             | m_ColourType       | ParseSlashModColourType result (NONE=0, PER_SLASH=1?) |
| +0x4c  | 4    | float           | m_LifeScale        | XML `life` attr (float, via QueryFloatAttribute)      |
| +0x50  | 1    | bool            | m_bDirectionalParticles | `particles_directional` attr CompareWords "true" |
| +0x51  | 3    | (pad)           | —                  | Alignment                                             |
| +0x54  | 4    | char*           | m_pParticlePath    | Heap-alloc `"tex_%s"` snprintf from `particles` attr; **SetEquipped passes this as SetModColours param_5 (trail emitter name)** |
| +0x58  | 4    | char*           | m_pTextureName2    | `texture` attr in `<slashModInfo>` sub-element; **SetEquipped passes this as SetModColours param_6 (blade texture)** |
| +0x5c  | 4    | char*           | m_pContactParticle | `contact_particles` attr; SetModColours param_8       |
| +0x60  | 4    | char*           | m_pParticle2       | Second particle attr; SetModColours param_9           |
| +0x64  | 4    | float           | m_ScaleEndThickness| `<scales end_thickness=>`; SetModScales param_2       |
| +0x68  | 4    | float           | m_ScaleLength      | `<scales length=>`; SetModScales param_3              |
| +0x6c  | 4    | float           | m_ScaleStartThickness | `<scales start_thickness=>`; SetModScales param_1 |
| +0x70  | 4    | float           | m_ScaleUVLength    | `<scales UV_length=>` (default 1.0f); SetModScales param_4 |
| +0x74  | 1    | bool            | m_bFlipForUpsideDown| `flipForUpsideDown` CompareWords "true"; SetModScales param_5 |
| +0x75  | 1    | bool            | m_bLoop            | `loop` attr from `<slashModInfo>` CompareWords "true"; SetModScales param_6 |
| +0x76  | 2    | (pad)           | —                  | Alignment                                             |
| +0x78  | 4    | float           | m_LoopUVLength     | SetModScales param_7 (from `<slashModInfo loop=>` region); default 1.0f |
| +0x7c  | 0x2c | SlashSoundMods  | m_SwipeSounds      | Parsed from `<swipeSounds>` child                     |
| +0xa8  | 0x2c | SlashSoundMods  | m_ImpactSounds     | Parsed from `<impactSounds>` child                    |
| +0xd4  | 0x2c | SlashSoundMods  | m_ComboSounds      | Parsed from `<comboSounds>` child                     |
| +0x100 | 0x10 | LoopingSound    | m_LoopingSound     | From `<loop>` child (SlashSoundMods::Parse)           |

**Total: 0x110 bytes.**

### SlashModInfo vtable (`0x001e8c38`, at `0x001e8c30` - 8`)

| Vtable slot | Byte offset | Address    | Name                        |
|-------------|-------------|------------|-----------------------------|
| [2]         | +0x00       | 0x00113ddc | ~SlashModInfo() in-place    |
| [3]         | +0x04       | 0x00113f24 | ~SlashModInfo() delete      |
| [4]         | +0x08       | 0x00112424 | UnEquip() — calls LoopingSound::Reset() |
| [5]         | +0x0c       | 0x00112430 | SetEquipped() — calls SetModColours + SetModScales + 3x SlashSoundMods::Reset |
| [6]         | +0x10       | 0x00112b0c | Parse() → ParseSlashModInfo @ 0x001126c0 |

`SetEquippedItem` type==0 with non-NULL item calls `(**(vtable+0x0c))(item)` = `SlashModInfo::SetEquipped @ 0x00112430`.

---

## ItemManager (singleton, 0x94 bytes) @ ctor `0x001121d0`

Singleton accessed via `ItemManager::GetInstance() @ 0x00112c34`.

Static local at GOT+0x89f4+0x18 = approximately BSS address `0x1f4b3c` (the guard uint32 is at `0x1f4b38`).

| Offset | Size | Type                    | Name             | Notes                                                         |
|--------|------|-------------------------|------------------|---------------------------------------------------------------|
| +0x00  | 4    | ItemInfo*               | m_DefaultItems[0]| First SLASH_MODIFIER item seen (default blade)                |
| +0x04  | 4    | ItemInfo*               | m_DefaultItems[1]| First BACKGROUND item seen                                    |
| +0x08  | 4    | ItemInfo*               | m_DefaultItems[2]| First UPSELL item seen (rarely non-NULL in shipped XML)       |
| +0x0c  | 4    | ItemInfo*               | m_DefaultItems[3]| REMOVEADS default — always NULL (type==3 excluded)            |
| +0x10  | 12   | vector\<ItemInfo*\>     | m_Items          | All items in XML order                                        |
| +0x1c  | 24   | map\<uint32,ItemInfo*\> | m_ByHash         | All items keyed by m_Hash                                     |
| +0x34  | 24   | map\<uint32,ItemInfo*\> | m_ByHashType[0]  | Type-0 (SLASH_MODIFIER) items by hash                        |
| +0x4c  | 24   | map\<uint32,ItemInfo*\> | m_ByHashType[1]  | Type-1 (BACKGROUND) items by hash                            |
| +0x64  | 24   | map\<uint32,ItemInfo*\> | m_ByHashType[2]  | Type-2 (UPSELL) items by hash                                 |
| +0x7c  | 24   | map\<uint32,ItemInfo*\> | m_ByHashType[3]  | Type-3 (REMOVEADS) items by hash                             |

**Ctor also clears +0x0c** (4 bytes).

**Note on +0x0c**: The ctor sets `*(undefined4 *)&this->field_0xc = 0` separately *before* zeroing the array, which suggests +0x0c may serve a dual purpose — it overlaps with `m_DefaultItems[3]` in the zero-init pass. The CLAUDE.md stub header shows `m_EquippedSlashModCount` at +0x0c but the ctor only inits the 4-slot array then clears +0x0c again. The SetEquippedItem type==2 handler decrements `*(int*)(iVar3 + DAT_00113148)` = `funcCalls @ 0x1f3cec` — a *different* static, not +0x0c. The +0x0c field is just m_DefaultItems[3].

**Total: 0x94 bytes** (vector=12 + 5×map=120 + array=16 = 148 = 0x94).

### Public API

| Address    | Signature                                           | Notes                                            |
|------------|-----------------------------------------------------|--------------------------------------------------|
| 0x00112c34 | `ItemManager* GetInstance()`                        | Static singleton, lazy-init                      |
| 0x001121d0 | `ItemManager::ItemManager()`                        | Ctor (used by GetInstance)                       |
| 0x00113200 | `void LoadItemData()`                               | Parse itemlist.xml + ItemSave.xml                |
| 0x0015fa6c | `int IsEquipped(ItemInfo* item)`                    | Returns 1 if item == m_DefaultItems[item->m_Type]|
| 0x0011307c | `void SetEquippedItem(ItemType type, ItemInfo* item)`| Sets slot + apply side effects                  |
| 0x00112498 | `int BuyItem(ulong this_ptr)`                       | Struct-return; deducts coins, sets cost=-1       |
| 0x00112048 | `int GetNumNewItems()`                              | Count items with m_bSeen==0                      |
| 0x0011200c | `bool AreNewItems()`                                | Any item with m_bSeen==0                         |
| 0x00112084 | `ItemInfo* GetItem(ulong hash)`                     | Lookup by hash in m_ByHash                       |
| 0x0011fd8  | `char* GetItemSavePath()`                           | Returns "Data/xml/ItemSave.xml" (via GOT)        |
| 0x00112210 | `void SaveItemInfo()`                               | Writes ItemSave.xml                              |
| 0x0015fbc8 | `ItemInfo* GetFirst(iterator& it)`                  | Begin iteration over m_Items                     |
| 0x0015fbf4 | `ItemInfo* GetNext(iterator& it)`                   | Advance + return next item                       |

---

## Function Pseudocode

### ItemManager::LoadItemData (0x00113200, ~190 lines)

Called from: `InitialiseData @ 0x0010b7ca` (step 14 of 15).

```c
void ItemManager::LoadItemData() {
    // Phase 1: Parse itemlist.xml
    TiXmlDocument* doc = new TiXmlDocument("xml/itemList.xml");   // str @ 0x1ba064
    m_ByHash.clear();
    m_Items.clear();
    m_DefaultItems[0] = m_DefaultItems[1] = m_DefaultItems[2] = m_DefaultItems[3] = NULL;

    if (doc->LoadFile()) {
        TiXmlElement* root = doc->FirstChildElement("itemManagerFile");  // 0x1ba075
        for (TiXmlElement* e = root->FirstChildElement("item");          // 0x1b9e95
             e != NULL;
             e = e->NextSiblingElement("item")) {
            // Allocate typed item
            char* typeStr = e->Attribute("type");                        // 0x1b9372
            int type = ParseItemType(typeStr);                           // 0=SLASH_MODIFIER..3=REMOVEADS
            ItemInfo* item;
            if (type == 0) {
                item = new SlashModInfo();    // 0x110 bytes
            } else {
                item = new ItemInfo();        // 0x40 bytes
            }
            item->m_Type = type;
            item->vtable->Parse(item, e);    // virtual dispatch to ItemInfo::Parse or ParseSlashModInfo

            // Achievement-gate check: if achievement not yet unlocked
            //   AND AchievementManager doesn't know about it (doesn't exist)
            //   AND item has a cost → mark as "new" (unseen, free)
            FruitSaveData* sd = g_FruitSaveData;   // GOT ptr @ 0x1f3ac0
            int unlocked = FruitSaveData::IsAchievementUnlocked(sd->m_AchievementHash);
            if (unlocked == 0) {
                AchievementManager* am = AchievementManager::GetInstance();
                int exists = AchievementManager::AchievementExists(am);
                if (exists == 0 && item->m_Cost > 0) {
                    item->m_bSeen = 0;        // mark "new item" badge
                    item->m_Cost = -1;        // make it free
                    ShopScreen::NewItem();    // set ShopScreen new-item flag (float = 1.0f)
                }
            } else {
                item->m_Cost = -1;   // achievement already unlocked → auto-unlock item
            }

            m_Items.push_back(item);                          // +0x10
            m_ByHash[item->m_Hash] = item;                   // +0x1c keyed by m_Hash (+0x08)
            m_ByHashType[type][item->m_Hash] = item;         // +0x34 + type*0x18

            // Set default item for this type (first seen wins, type==3 excluded)
            if (m_DefaultItems[type] == NULL && type != 3) {
                m_DefaultItems[type] = item;
            }
        }
    }
    delete doc;   // (virtual dtor call)

    // Phase 2: Load save state from ItemSave.xml
    char* savePath = GetItemSavePath();   // "Data/xml/ItemSave.xml"
    TiXmlDocument* save = new TiXmlDocument(savePath);
    if (save->LoadFile()) {
        // Root element: <item_save_file version="1.0" coins="..." coinsTotal="..." levelStartCoins="...">
        TiXmlElement* root = save->FirstChildElement("item_save_file");  // 0x1b9e4d
        FruitSaveData* sd = g_FruitSaveData;
        root->QueryIntAttribute("coins",           &sd->m_Coins);           // +0x20
        root->QueryIntAttribute("coinsTotal",      &sd->m_CoinsTotal);      // +0x24
        root->QueryIntAttribute("levelStartCoins", &sd->m_LevelStartCoins); // +0x28

        // <boughtItems> section: mark purchased items
        TiXmlElement* bought = root->FirstChildElement("boughtItems");   // 0x1b9e89
        if (bought != NULL) {
            for (TiXmlElement* e = bought->FirstChildElement("item");    // 0x1b9e95
                 e != NULL;
                 e = e->NextSiblingElement("item")) {
                char* nameVal = e->Attribute("name");                    // 0x1c3173
                if (nameVal && *nameVal) {
                    uint32 hash = StringHash(nameVal);
                    auto it = m_ByHash.find(hash);
                    if (it != m_ByHash.end()) {
                        ItemInfo* item = it->second;
                        item->m_Cost = -1;       // mark purchased
                        char* seenVal = e->Attribute("seen");            // 0x1b9ea5
                        bool isSeen;
                        if (!seenVal || !*seenVal)
                            isSeen = false;
                        else {
                            int cmp = strcmp(seenVal, "true");           // 0x1b9ea0
                            isSeen = (cmp == 0);    // ARM idiom: '\\x01' - (char)cmp, clamped
                        }
                        item->m_bSeen = isSeen ? 1 : 0;
                    }
                }
            }
        }

        // <equippedItems> section: set default (equipped) item per type
        TiXmlElement* equipped = root->FirstChildElement("equippedItems"); // 0x1b9eaa
        if (equipped != NULL) {
            for (TiXmlElement* e = equipped->FirstChildElement("item");    // 0x1b9e95
                 e != NULL;
                 e = e->NextSiblingElement("item")) {
                char* nameVal = e->Attribute("name");                      // 0x1c3173
                if (nameVal && *nameVal) {
                    uint32 hash = StringHash(nameVal);
                    ItemInfo* item = ItemManager::GetItem(hash);
                    if (item != NULL) {
                        m_DefaultItems[item->m_Type] = item;
                    }
                }
            }
        }
    }
    delete save;

    // Phase 3: Apply equipped items for all 4 types
    field_0x8 = 0;    // reset some counter
    for (int i = 0; i < 4; i++) {
        SetEquippedItem((ItemType)i, m_DefaultItems[i]);
    }
}
```

### ItemInfo::Parse (0x0011293c)

Virtual function, called for all item types. SlashModInfo overrides at the same vtable slot — `ParseSlashModInfo @ 0x001126c0` handles `<slashModInfo>` children.

```c
void ItemInfo::Parse(TiXmlElement* e) {
    // Parse <requirements> child element (optional)
    TiXmlElement* req = e->FirstChildElement("requirements");   // 0x1b9fc4
    if (req != NULL) {
        this->m_Cost = 1;    // default if present but no coins attr
        req->QueryIntAttribute("coins", &this->m_Cost);         // 0x1b9e68
        
        char* descAttr = req->Attribute("description");          // 0x1b92d1 — NOTE: appears to be
                                                                 // "description" but reads into m_pLockedText
        CloneString(&this->m_pLockedText, descAttr);
        if (this->m_pLockedText == NULL) {
            // Fall back to element text content
            char* text = GETSTRING_CAST_0_STR(req->GetText());
            CloneString(&this->m_pLockedText, text);
        }
        
        char* progressAttr = req->Attribute("singular");         // 0x1b9fd1 → m_pProgressFmt
        CloneString(&this->m_pProgressFmt, progressAttr);
        if (this->m_pProgressFmt != NULL) {
            char* localised = GETSTRING_CAST_0_STR(this->m_pProgressFmt);
            CloneString(&this->m_pProgressFmt, localised);
        }
        
        // Requirement type flags (all use CompareWords(attr, "true"))
        char* trueStr = "true";                                  // 0x1b9ea0
        char* upsideDown = req->Attribute("showIfUpsideDown");   // 0x1b9fda
        if (CompareWords(trueStr, upsideDown) != 0) {
            this->m_RequirementType = 1;
        } else {
            char* playedToday = req->Attribute("showIfPlayedToday"); // 0x1b9feb
            if (CompareWords(trueStr, playedToday) != 0)
                this->m_RequirementType = 2;
            else {
                char* joinButtons = req->Attribute("showJoinButtons"); // 0x1b9ffd
                if (CompareWords(trueStr, joinButtons) != 0)
                    this->m_RequirementType = 3;
            }
        }
        
        req->QueryIntAttribute("countDownFrom", &this->m_CountDownFrom); // 0x1ba00d
        
        char* totalAttr = req->Attribute("total");                // 0x1bd00d
        CloneString(&this->m_pTotalStatKey, totalAttr);
    }

    // Always parse from the outer <item> element:
    char* nameAttr = e->Attribute("name");                        // 0x1c3173
    CloneString(&this->m_pName, nameAttr);
    this->m_Hash = StringHash(this->m_pName);

    char* titleAttr = e->Attribute("title");                      // 0x1ba01b
    char* titleStr = GETSTRING_CAST_0_STR(titleAttr);
    CloneString(&this->m_pTitle, titleStr);

    TiXmlElement* desc = e->FirstChildElement("description");    // 0x1b92d1
    if (desc != NULL) {
        char* descText = GETSTRING_CAST_0_STR(desc->GetText());
        CloneString(&this->m_pDescText, descText);
    }

    char* texAttr = e->Attribute("texture");                     // 0x1b92e8
    CloneString(&this->m_pTextureName, texAttr);

    char* colourAttr = e->Attribute("colour");                   // 0x1b9f98
    ParseColour(&this->m_Colour1, colourAttr);
    // m_Colour2 is initialised to a default copy; the attr "titleolour" (0x1ba021)
    // is a vestigial/mangled second colour attr that appears in the binary string table
    // but reads from a zero-init copy of Colour1. In shipped XML only one colour= exists.
    ParseColour(&this->m_Colour2, e->Attribute("titleolour"));   // 0x1ba021 — likely a
                                                                  // typo/vestigial attr
}
```

### ItemManager::IsEquipped (0x0015fa6c)

```c
int IsEquipped(ItemInfo* item) {
    if (item == NULL) return 0;
    return (m_DefaultItems[item->m_Type] == item) ? 1 : 0;
}
```

### ItemManager::SetEquippedItem (0x0011307c)

```c
// Note: ARM comparison idiom — Ghidra shows "if (0 < count)" which fires when count >= 1.
// The static `funcCalls @ 0x1f3cec` tracks concurrent SetEquippedItem invocations.
void SetEquippedItem(ItemType type, ItemInfo* item) {
    int* funcCalls = &g_SetEquippedItemFuncCalls;  // 0x1f3cec

    if (type == ITEM_TYPE_BACKGROUND) {             // 1
        if (*funcCalls > 0) {
            SmartPtr<Texture> curBG;
            GetCurrentBackground(&curBG);
            bool equal = SmartPtr::operator_cast_to_bool(&curBG);
            // "1 - (uint)equal; if (1 < equal) iVar4 = 0"
            // ARM idiom: equal != 0 means they are equal → no change needed
            SmartPtr<Texture>::~SmartPtr(&curBG);
            if (equal != 0) goto DONE;
        }
        char* texName = (item != NULL) ? item->m_pTextureName : NULL;  // +0x30
        ChangeBackground(texName);
    }
    else if (type == ITEM_TYPE_UPSELL) {            // 2
        if (*funcCalls >= 1) {
            (*funcCalls)--;
            return;
        }
    }
    else if (type == ITEM_TYPE_BLADE) {             // 0
        SlashEntity* slash = NULL;
        if (*(SlashEntity**)this != NULL)
            slash = (**(SlashEntity**)this)->GetSlashEntity();
        if (item == NULL) {
            SlashEntity::InitModColours(slash);
            SlashEntity::SetModScales(1.0f, 1.0f, 0.0f, 1.0f, false, false, 0.0f);
        } else {
            (*item->vtable->SetEquipped)(item);    // virtual call to item's SetEquipped
        }
    }
DONE:
    if (*funcCalls > 0) (*funcCalls)--;
    m_DefaultItems[type] = item;
    if (item != NULL) item->m_bSeen = 1;            // mark as seen when equipped
}
```

### ItemManager::BuyItem (0x00112498)

The function has a struct-return ABI: `r0` = hidden return ptr, but Ghidra shows `param_1` as the ItemManager `this` ptr (the struct-return storage is implicit). Effective call: `BuyItem(ItemManager* this, uint32 hash)`.

```c
// param_1 = pointer to ItemManager (contains m_ByHash at +0x1c)
// Returns 1 on success, 0 on failure
int BuyItem(ulong param_1_itemmanager) {
    auto it = m_ByHash.find(hash);    // lookup in m_ByHash (+0x1c)
    if (it == m_ByHash.end()) return 0;

    ItemInfo* item = it->second;
    int cost = item->m_Cost;           // +0x0c
    FruitSaveData* sd = g_FruitSaveData;  // 0x1f3ac0
    int coins = sd->m_Coins;           // +0x20

    // ARM idiom: "if (-1 < cost && cost <= coins)" = if (cost >= 0 && cost <= coins)
    if (cost >= 0 && cost <= coins) {
        AddCoins(-cost);               // deduct coins
        item->m_Cost = -1;            // mark purchased
        return 1;
    }
    return 0;
}
```

**Side note on `AddCoins(int delta) @ 0x0010a3bc`:**
```c
void AddCoins(int delta) {
    FruitSaveData* sd = g_FruitSaveData;
    sd->m_Coins += delta;              // +0x20
    if (delta > 0) sd->m_CoinsTotal += delta;  // +0x24
}
```

### ItemManager::GetNumNewItems (0x00112048)

```c
int GetNumNewItems() {
    int count = 0;
    for (ItemInfo* item : m_Items) {   // vector<ItemInfo*> at +0x10
        if (item->m_bSeen == 0) count++;  // +0x3c
    }
    return count;
}
```

### ItemManager::AreNewItems (0x0011200c)

```c
bool AreNewItems() {
    for (ItemInfo* item : m_Items) {
        if (item->m_bSeen == 0) return true;
    }
    return false;
}
```

### ItemManager::SaveItemInfo (0x00112210)

Writes `ItemSave.xml` with purchased items and equipped slots.

```c
void SaveItemInfo() {
    TiXmlDocument doc;  // stack-allocated
    FruitSaveData* sd = g_FruitSaveData;

    // Build root <item_save_file version="1.0" coins=N coinsTotal=N levelStartCoins=N>
    TiXmlElement* root = new TiXmlElement("item_save_file");  // 0x1b9e4d
    root->SetAttribute("version", "1.0");                     // 0x1b9e5c / 0x1b9e64
    root->SetAttribute("coins",           sd->m_Coins);       // +0x20
    root->SetAttribute("coinsTotal",      sd->m_CoinsTotal);  // +0x24
    root->SetAttribute("levelStartCoins", sd->m_LevelStartCoins); // +0x28

    // <boughtItems> section: all items with m_Cost < 0 (purchased)
    TiXmlElement* bought = new TiXmlElement("boughtItems");    // 0x1b9e89
    for (ItemInfo* item : m_Items) {
        if (item->m_Cost < 0) {    // cost == -1 → purchased
            TiXmlElement* e = new TiXmlElement("item");        // 0x1b9e95
            e->SetAttribute("name", item->m_pName);           // 0x1c3173 / +0x04
            char* seenVal = (item->m_bSeen == 0) ? "false" : "true"; // 0x1b9e9a / 0x1b9ea0
            e->SetAttribute("seen", seenVal);                  // 0x1b9ea5
            bought->LinkEndChild(e);
        }
    }
    root->LinkEndChild(bought);

    // <equippedItems> section: m_DefaultItems[0..3] with m_Cost <= 0 (free or purchased)
    TiXmlElement* equip = new TiXmlElement("equippedItems");   // 0x1b9eaa
    for (int i = 0; i < 4; i++) {                             // loop: iVar7 += 4 while iVar7 != 0x10
        ItemInfo* item = m_DefaultItems[i];
        if (item != NULL && item->m_Cost < 1) {               // cost <= 0 = owned
            TiXmlElement* e = new TiXmlElement("item");        // 0x1b9e95
            e->SetAttribute("name", item->m_pName);           // 0x1c3173
            equip->LinkEndChild(e);
        }
    }
    root->LinkEndChild(equip);

    doc.LinkEndChild(root);
    char* path = GetItemSavePath();
    doc.SaveFile(path);
    // TiXmlDocument::~TiXmlDocument (stack dtor)
}
```

---

## itemlist.xml Schema

Root element: `<itemManagerFile version="1.0.0">`.

### `<item>` element

| Attribute   | Type   | Maps to field       | Notes                                          |
|-------------|--------|---------------------|------------------------------------------------|
| `type`      | string | ParseItemType()     | "SLASH_MODIFIER"=0, "BACKGROUND"=1, "UPSELL"=2, "REMOVEADS"=3 |
| `name`      | string | m_pName, m_Hash     | Internal name (hashed to m_Hash via StringHash) |
| `title`     | string | m_pTitle            | Localisation key → GETSTRING_CAST_0_STR        |
| `colour`    | string | m_Colour1           | "R,G,B" or "R,G,B,A" passed to ParseColour     |
| `texture`   | string | m_pTextureName      | Asset name (no extension); used for shop thumbnail |

### `<item>` child elements

| Element        | Attribute       | Type   | Maps to field       | Notes                                |
|----------------|-----------------|--------|---------------------|--------------------------------------|
| `<requirements>`| `coins`        | int    | m_Cost              | Purchase price; if absent, item is free |
| `<requirements>`| `total`        | string | m_pTotalStatKey     | Achievement stat key (FruitSaveData::GetTotal key) |
| `<requirements>`| `countDownFrom`| int    | m_CountDownFrom     | Progress "N remaining" display        |
| `<requirements>`| `singular`     | string | m_pProgressFmt      | Localisation key for singular form    |
| `<requirements>`| `showIfUpsideDown`| bool | m_RequirementType=1 | CompareWords with "true"              |
| `<requirements>`| `showIfPlayedToday`| bool| m_RequirementType=2 | CompareWords with "true"              |
| `<requirements>`| `showJoinButtons`| bool | m_RequirementType=3 | CompareWords with "true"              |
| `<requirements>`| (text content) | string | m_pLockedText     | Displayed when item is locked         |
| `<description>` | (text content) | string | m_pDescText      | Item description text                 |
| `<slashModInfo>`| multiple       | —      | SlashModInfo fields | Only on SLASH_MODIFIER items; parsed by ParseSlashModInfo @ 0x1126c0 |

### SLASH_MODIFIER items (as of shipped XML)

| name              | texture                | Has requirements |
|-------------------|------------------------|------------------|
| ORIGINAL_SLASH    | item_originalblade     | No (free default)|
| SHINY_RED_SLASH   | item_shiney_red_blade  | Yes              |
| DISCO_SLASH       | item_discoblade        | Yes (banana_total)|
| SPARKLE_SLASH     | item_mr_sparkle        | Yes              |
| AMERICAN_SLASH    | item_american_blade    | Yes              |
| BUTTERFLY_KNIFE   | item_butterfly_knife   | Yes (strawberry_combo_total)|
| FLAME_BLADE       | item_flame_blade       | Yes              |
| ICE_BLADE         | item_ice_blade         | Yes (freeze_total)|
| PIXEL_BLADE       | item_pixel_blade       | Yes (CLASSIC_combos)|
| MUSICAL_BLADE     | item_piano_blade       | Yes (crits_total)|
| RAINBOW_BLADE     | item_party_knife       | Yes              |
| BAMBOO_BLADE      | item_bamboo_shoot      | Yes (ZEN_days)   |

### BACKGROUND items

| name        | texture             | Has requirements |
|-------------|---------------------|------------------|
| background1 | GB_game             | No (free default)|
| background9 | BG_fruit_ninja      | Yes              |
| background3 | BG_i_heart_sensei   | Yes (strawberry_facts)|
| background4 | BG_greatwave        | Yes (watermelon_total)|
| background5 | BG_YinYang          | Yes (passionfruit_total)|

---

## FruitSaveData Integration

ItemManager calls the following FruitSaveData methods/fields:

| Method / Field                  | Address      | Used by             | Notes                                               |
|---------------------------------|--------------|---------------------|-----------------------------------------------------|
| `FruitSaveData::IsAchievementUnlocked(ulong hash)` | 0x00129c50 | LoadItemData | Checks `m_AchievementMap @ +0x158` and `+0x170`; returns 0=not found, 1=found in one map, 2=found in both |
| `FruitSaveData->m_Coins` (+0x20) | —          | LoadItemData, BuyItem, SaveItemInfo | Current coin balance |
| `FruitSaveData->m_CoinsTotal` (+0x24) | —       | LoadItemData, BuyItem, SaveItemInfo | All-time earned coins |
| `FruitSaveData->m_LevelStartCoins` (+0x28) | —   | LoadItemData, SaveItemInfo | Coins at level start (for refund) |
| `AddCoins(int delta) @ 0x0010a3bc` | 0x0010a3bc | BuyItem | Free function; adds to both +0x20 and +0x24 if positive |

**FruitSaveData singleton GOT ptr**: `*(FruitSaveData**)(GOT + 0x7990)` = `*(int*)(0x1f3ac0)`.
Created in `InitialiseData`, stored at `g_GameData + 0x4c` (GameTaskState+0x4c).

### Minimum FruitSaveData stub for ItemManager

The stub must expose:
1. `int IsAchievementUnlocked(ulong hash)` — return 0 to keep achievement-locked items locked; return non-zero to auto-unlock
2. `int m_Coins` at +0x20 — coin balance (init from save or 0)
3. `int m_CoinsTotal` at +0x24 — total earned
4. `int m_LevelStartCoins` at +0x28 — coins at level start

`FruitSaveData::GetTotal` (used by GameInitialise for sound/music prefs) is a separate concern not needed for ItemManager.

---

## GetInstance / Singleton Location

`ItemManager::GetInstance() @ 0x00112c34` uses the C++ static-local `__cxa_guard_acquire` / `__cxa_guard_release` pattern:

```c
ItemManager* ItemManager::GetInstance() {
    static ItemManager s_instance;   // guard at BSS ~0x1f4b38, object at BSS ~0x1f4b3c
    return &s_instance;
}
```

**Not a member of `Game` or `GameTaskState`** — it is a free static singleton.

---

## Call Site

`LoadItemData` is called from `InitialiseData @ 0x0010b7ca` (the function has existing plate comments in Ghidra). Sequence:

```
InitialiseData (0x0010b7ca)
  → AchievementManager::GetInstance() + LoadAchievementInfo()   // step 13
  → ItemManager::GetInstance() + LoadItemData()                  // step 14
  → BonusManager::GetInstance() + Init()                         // step 15
```

`InitialiseData` is called from `GameInitialise @ 0x0010bdfc` via `InitialiseData()`.

---

## Blockers for Implementation

1. **`AchievementManager::AchievementExists`** — called in the achievement-gate check inside LoadItemData. If stubbed to return 0 (not exists), all items with a positive cost but no achievement will be treated as "new/free" (badge + m_Cost=-1). A no-op stub returning 0 is safe for now.

2. **`FruitSaveData`** — the port stub needs to expose `m_Coins` (+0x20), `m_CoinsTotal` (+0x24), `m_LevelStartCoins` (+0x28), and `IsAchievementUnlocked(hash)`. These are the only fields ItemManager touches.

3. **`GETSTRING_CAST_0_STR`** — used in ItemInfo::Parse for localisation key → string resolution. All `title` and description strings go through this. If unimplemented, it should return its input unchanged (pass-through stub).

4. **`ChangeBackground(char* texName)`** — called by SetEquippedItem when type==1. Already partially analysed at 0x0016ae6c; stores a SmartPtr\<Texture\> into a global slot.

5. **`SlashEntity::SetModScales` / `InitModColours`** — called by SetEquippedItem when type==0 and item==NULL (revert to default blade). Already documented elsewhere.

6. **`ParseColour`** — parses "R,G,B" or "R,G,B,A" string into a Colour struct (4 bytes). Used by ItemInfo::Parse for m_Colour1/m_Colour2. Should already exist from other loaders.

7. **`CompareWords`** — used in ParseSlashModInfo and ItemInfo::Parse for attribute boolean checks. Returns 0 if strings match (strcmp==0). If not yet ported, stub as `strcmp`.

8. **`CloneString`** — duplicates a string onto the heap. Used throughout Parse. `CloneString(char** dst, char* src)` = `*dst = strdup(src)` effectively. Already exists at 0x001141c0.

---

<!-- Analysed: 2026-04-25T14:45 -->

## ChangeBackground — Gap 1

**Address**: `0x0016ae8c` (real implementation); `0x000f9708` = thunk via GOT.

**Signature**: `void ChangeBackground(const char* texName)`

**GOT base**: `0x001EC130` (confirmed shared by `InitModColours`, `SetModColours`, `SetModScales`, `GetCurrentBackground`).

### Behaviour

```c
void ChangeBackground(const char* texName) {
    bool fast = IsFastHardware();
    if (texName == NULL)
        texName = "gb_game";           // 0x001bc79d — default background
    const char* suffix = fast ? "" : "_sml";  // fast: 0x001bda4c (empty); slow: 0x001bc7a5
    char buf[64];
    OS_SPrintf(buf, 64, "%s%s.tex", texName, suffix);   // fmt @ 0x001bc7aa
    SmartPtr<Texture> tmp;
    TextureManager::LoadLocalisedTexture(&tmp, buf);
    // Store into global backgroundTexture SmartPtr (file-static _ZL17backgroundTexture)
    // at BSS 0x231500 = GOT(0x1EC130) + 0x000452d4 + 0xfc
    g_backgroundTexture = tmp;          // SmartPtr<Texture>::operator=
    // ~SmartPtr tmp
}
```

**Side effects**:
- Calls `IsFastHardware()` to select texture resolution suffix: fast hardware → no suffix (full-res), slow → `"_sml"` (low-res).
- Appends `.tex` extension and loads via `TextureManager::LoadLocalisedTexture`.
- Stores the result into the global `SmartPtr<Texture> backgroundTexture` at BSS `0x231500`.
- `GetCurrentBackground() @ 0x0016af28` reads back from the same slot (`[GOT + 0x000452d4 + 0xfc]`), confirming it is a file-scope static (symbol `_ZL17backgroundTexture`).

**Related**: `ChangeBackground(SmartPtr<Texture>*) @ 0x0016ae6c` is a thin overload that does `g_backgroundTexture = *param_1` directly (no path building).

**Port-side change required** (`src/game/ItemManager.cpp`, `SetEquippedItem` type==1 branch):

Replace the `// TODO: ChangeBackground(texName)` stub with the real call once `ChangeBackground` is implemented:

```cpp
// was: (void)texName;
ChangeBackground(texName);   // defined in MenuBackground.cpp or globals.cpp
```

`ChangeBackground` itself needs: `IsFastHardware()`, `TextureManager::LoadLocalisedTexture`, `SmartPtr<Texture>`, and a global `backgroundTexture` slot. The port can implement this as a free function in `src/engine/MenuBackground.cpp` (matches the binary source file `MenuBackground.cpp` identified in `_ZN14MenuBackground*` symbols). No `Osp::` API needed — `TextureManager::LoadLocalisedTexture` maps to the existing texture loader.

---

## SlashModInfo::SetEquipped / SlashEntity::SetModColours / InitModColours / SetModScales — Gap 2

<!-- Analysed: 2026-04-25T14:45 -->

### SlashModInfo::SetEquipped (vtable +0x0c @ 0x00112430)

Called by `SetEquippedItem` when `type == ITEM_TYPE_BLADE` and `item != NULL`, via `(**(vtable+0x0c))(item)`.

```c
void SlashModInfo::SetEquipped(SlashModInfo* this) {
    // Forwards all SlashModInfo blade-skin fields to SlashEntity state
    SlashEntity::SetModColours(
        this->m_pColours,             // +0x40 — Colour array (NULL if none)
        this->m_ColourCount,          // +0x44
        this->m_ColourType,           // +0x48
        this->m_LifeScale,            // +0x4c
        this->m_pParticlePath,        // +0x54 — trail emitter name (e.g. "tex_sparkle")
        this->m_pTextureName2,        // +0x58 — blade overlay texture name
        this->m_bDirectionalParticles,// +0x50
        this->m_pContactParticle,     // +0x5c
        this->m_pParticle2            // +0x60
    );
    SlashEntity::SetModScales(
        this->m_ScaleStartThickness,  // +0x6c  param_1
        this->m_ScaleEndThickness,    // +0x64  param_2
        this->m_ScaleLength,          // +0x68  param_3
        this->m_ScaleUVLength,        // +0x70  param_4
        this->m_bFlipForUpsideDown,   // +0x74  param_5
        this->m_bLoop,                // +0x75  param_6
        this->m_LoopUVLength          // +0x78  param_7
    );
    this->m_SwipeSounds.Reset();    // +0x7c
    this->m_ImpactSounds.Reset();   // +0xa8
    this->m_ComboSounds.Reset();    // +0xd4
}
```

**SlashModInfo::UnEquip @ 0x00112424**: calls `LoopingSound::Reset()` on `m_LoopingSound` (+0x100). Called when a blade skin is de-equipped.

### SlashEntity::SetModColours (0x0017ca0c; thunk at 0x000f870c)

**Signature**: `void SetModColours(Colour* colours, int colourCount, int colourType, float lifeScale, const char* particlePath, const char* textureName2, bool directional, const char* contactParticle, const char* particle2)`

**What it mutates** (all via the global `g_slashEntity` singleton, double-dereferenced through GOT slots):

| Parameter    | Field written in SlashEntity | Notes |
|--------------|------------------------------|-------|
| colourCount  | *(GOT+0x7980) = ptr to count field | int |
| colourType   | *(GOT+0x7980-4) = ptr to type field | int; if type==2 → pick random start index |
| lifeScale    | *(GOT+0x7ab8) | float |
| colours[]    | 16-entry palette copied from param_1 | loop 0..15, `Colour::operator=` |
| textureName2 | SmartPtr at GOT+0x7ab8+0xd8 | if non-NULL/non-empty: `LoadLocalisedTexture`; else null |
| particlePath | StringHash → *(GOT+0x70e8) uint32 | then `PSPParticleManager::EmitterExists`: if exists set emitter-type flag (1 or 2) |
| directional  | flag byte — 1=normal, 2=directional | |
| contactParticle | StringHash → *(GOT+0x7330) | zeroed if emitter doesn't exist |
| particle2    | StringHash → *(GOT+0x70a4) | zeroed if emitter doesn't exist |
| post-apply   | Iterates ActorManager type-3 entities → `ColoursChanged()` | only if `*(iVar11 + 0x160) != 0` |

After writing, if the game is active (`*(*(GOT+0x7be8)+0x160) != 0`), walks all type-3 `ActorManager` entities and calls `ColoursChanged()` on each — this re-tints already-spawned blade ghosts/trails.

### SlashEntity::InitModColours (0x0017cc38; thunk at 0x000f77b8)

**Signature**: `void InitModColours(SlashEntity* this)` (this is ignored; accesses global singleton).

Resets all mod-colour state to defaults:
- Sets `colourCount` = 0, `colourType` = 0
- Nulls the overlay texture SmartPtr
- Resets particle/contact hashes to 0
- Resets emitter-type flags
- Copies default colour palette (16 entries from the base-colour array in GOT)
- `uVar2` (init value for colour type) = `DAT_0017ccac = 0x00000000` (zero = NONE)

### SlashEntity::SetModScales (0x0017b328; thunk at 0x000fada0)

**Signature**: `void SetModScales(float startThick, float endThick, float scaleLen, float uvLen, bool flipUD, bool loop, float loopUVLen)`

Writes directly to fields of the `g_slashEntity` singleton (via GOT double-indirection):

| Parameter  | BSS field addr | Meaning |
|------------|----------------|---------|
| param_5 (flipUD) | 0x1F3A68 | bool flip-for-upside-down |
| param_6 (loop)   | 0x1F38B8 | bool loop texture |
| param_1 (startThick) | 0x1F31B8 | float start thickness scale |
| param_2 (endThick)   | 0x1F36E8 | float end thickness scale |
| param_3 (scaleLen)   | 0x1F357C | float trail length scale |
| param_4 (uvLen)      | 0x1F3248 | float UV length scale |
| param_7 (loopUVLen)  | 0x1F3B18 | float loop UV length |

Default call (no blade skin): `SetModScales(1.0f, 1.0f, 0.0f, 1.0f, false, false, 0.0f)`.

**Port-side change required** (`src/game/ItemManager.cpp`, `SetEquippedItem` type==0 branch):

```cpp
// was: // TODO: SlashEntity::InitModColours(slash)
//      // TODO: SlashEntity::SetModScales(...)
SlashEntity::InitModColours(slash);
SlashEntity::SetModScales(1.0f, 1.0f, 0.0f, 1.0f, false, false, 0.0f);
```

For `item != NULL`, the `item->SetEquipped()` virtual call is already wired. `SlashModInfo::SetEquipped` must be overriding the `ItemInfo::SetEquipped` no-op — confirmed: the vtable `SetEquipped` slot for `SlashModInfo` is `0x00112430` (distinct from ItemInfo's no-op at `0x00113978`). Port's `ItemInfo::SetEquipped()` virtual + `SlashModInfo::SetEquipped()` override must be implemented.

**Blocker**: `SetModColours` and `SetModScales` need a live `SlashEntity*` pointer (obtained via `(*(*this))->GetSlashEntity()` in `SetEquippedItem`). Port needs `GetSlashEntity()` on whatever singleton holds it (probably `Game` or `ActorManager`). Stub `SetModColours`/`InitModColours`/`SetModScales` as no-ops until SlashEntity's singleton wiring is in place; the `SetEquipped()` virtual call path is already correct.

---

## SaveItemInfo Disk Write Path — Gap 3

<!-- Analysed: 2026-04-25T14:45 -->

**Address**: `SaveItemInfo @ 0x00112210`

**Save path**: `GetItemSavePath() @ 0x00111fd8` returns the literal string `"ItemSave.xml"` (at rodata `0x001b9e40`) — **no `"Data/"` prefix**. The port's current implementation incorrectly returns `"Data/xml/ItemSave.xml"` and then strips `"Data/"`.

**Actual binary path**: `"ItemSave.xml"` relative to the app's working directory (on Bada this is `/Home/` or similar per-app data dir).

**Write mechanism**: `TiXmlDocument::SaveFile(const char*) @ 0x00185ce8`:

```c
bool TiXmlDocument::SaveFile(const char* path) {
    Mortar::File file(path, 7, 0);    // mode 7 = write/create
    if (file.Open()) {
        SaveFile(&file);               // Print() to file
        file.Close();
        return true;
    }
    return false;
}
```

`Mortar::File` (`_ZN6Mortar4FileC1EPKcim`) wraps the platform file API. On Bada: `Osp::Io::File`. Port: standard `fopen`/`fwrite` via C stdio. The port currently has `Mortar::File` or equivalent porting needed; alternatively `tinyxml2::XMLDocument::SaveFile(const char*)` (the tinyxml2 version) does the right thing using `fopen` directly.

**Port-side change required** (`src/game/ItemManager.cpp`, `SaveItemInfo`):

1. Fix `GetItemSavePath()` to return just `"ItemSave.xml"` (or the port's writable data path directly).
2. Uncomment the `doc.SaveFile(saveFullPath.c_str())` call — tinyxml2's `SaveFile` uses C `fopen`, which works without any `Osp::` stub.
3. The `saveFullPath` computation should be simply `game->data_dir + "/ItemSave.xml"` (drop the `xml/` subdirectory since the binary uses a flat path).

No `Osp::Io::File` stub is needed for the port — tinyxml2 handles the write directly.

---

## FruitSaveData Coin Persistence — Gap 4

<!-- Analysed: 2026-04-25T14:45 -->

### Fields

| Field            | Offset | Read from          | Written to         |
|------------------|--------|--------------------|--------------------|
| `m_Coins`        | +0x20  | `ItemSave.xml` attr `coins` | `ItemSave.xml` attr `coins` |
| `m_CoinsTotal`   | +0x24  | `ItemSave.xml` attr `coinsTotal` | `ItemSave.xml` attr `coinsTotal` |
| `m_LevelStartCoins` | +0x28 | `ItemSave.xml` attr `levelStartCoins` | `ItemSave.xml` attr `levelStartCoins` |

**These three fields are exclusively persisted in `ItemSave.xml`** — not in the main game save file (`ParseSaveFile @ 0x0012b5e8` does not read or write them). The `FruitSaveData` ctor (`0x00129cb4`) and copy-ctor (`0x0016e2fc`) do NOT initialise or copy them, so they start as zero-initialised BSS (value = 0 on first run, set from file on subsequent runs).

**Read path**: `LoadItemData @ 0x00113200` (Phase 2) — `root->QueryIntAttribute("coins", &sd->m_Coins)` etc.

**Write path**: `SaveItemInfo @ 0x00112210` — `root->SetAttribute("coins", sd->m_Coins)` etc.

**`AddCoins(int delta) @ 0x0010a3bc`** (free function via GOT):
```c
void AddCoins(int delta) {
    FruitSaveData* sd = g_FruitSaveData;
    sd->m_Coins += delta;
    if (delta > 0) sd->m_CoinsTotal += delta;  // only positive deltas accumulate in total
}
```
`m_LevelStartCoins` is only modified directly (not through `AddCoins`), presumably snapshotted at level start to allow refund on retry.

**Port-side changes required**:

1. Add `int m_Coins`, `int m_CoinsTotal`, `int m_LevelStartCoins` at offsets `+0x20`, `+0x24`, `+0x28` of `FruitSaveData` in `src/game/FruitSaveData.h`.
2. `LoadItemData` already calls `root->QueryIntAttribute("coins", &sd->m_Coins)` etc. — no change needed once the fields exist.
3. Once `SaveItemInfo` disk write is unblocked (Gap 3), coin persistence is complete — no additional code needed.
4. Port's `AddCoins` free function should match: `m_Coins += delta; if (delta > 0) m_CoinsTotal += delta`. `m_LevelStartCoins` is snapshotted separately at game-start (search for callers when porting that flow).
