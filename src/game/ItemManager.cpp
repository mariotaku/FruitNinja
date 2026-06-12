// Analysed: 2026-04-25T10:30
//
// ItemManager — binary-faithful implementation.
// Binary: ctor 0x001121d0, LoadItemData 0x001131f4, GetInstance 0x00112c34,
//         IsEquipped 0x0015fa6c, SetEquippedItem 0x0011307c,
//         BuyItem 0x00112498, GetNumNewItems 0x00112048,
//         AreNewItems 0x0011200c, SaveItemInfo 0x00112210.
// See docs/structs/items.md for full pseudocode.
//

#include "ItemManager.h"
#include "ItemParseUtil.h"
#include "FruitSaveData.h"
#include "AchievementManager.h"
#include "engine/util/StringHash.h"
#include "engine/util/PathCI.h"
#include "engine/MenuBackground.h"
#include "entities/SlashEntity.h"
#include "screens/ShopScreen.h"
#include "debug/Logger.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

// External: g_FruitSaveData singleton — binary GOT @ 0x1f3ac0.
// Port: exposed via FruitSaveData.h (the instance lives in Game::pSaveData).
// We access it through the Game singleton here.
#include "Game.h"
#include "game/GameWork.h"
static FruitSaveData* GetSaveData() {
    Game* g = Game::GetInstance();
    return g ? game_work.m_SaveData : nullptr;
}

// g_SetEquippedItemFuncCalls @ 0x1f3cec — static call-guard for SetEquippedItem.
static int g_SetEquippedItemFuncCalls = 0;

// ItemManager::EquippedSlashModCount @ .bss 0x0022ece4
int ItemManager::EquippedSlashModCount = 0;

// -----------------------------------------------------------------------
// ItemManager ctor @ 0x001121d0
// -----------------------------------------------------------------------
ItemManager::ItemManager() {
    m_DefaultItems[0] = nullptr;
    m_DefaultItems[1] = nullptr;
    m_DefaultItems[2] = nullptr;
    m_DefaultItems[3] = nullptr;
    // Binary: also clears +0x0c separately (overlaps m_DefaultItems[3])
    m_DefaultItems[3] = nullptr;
}

// DIFFERS: original = no dtor (binary frees items via UnLoadItemData @ 0x001124fc).
//          Port keeps the dtor as a defensive no-op so static-destructor order
//          can't double-free.
ItemManager::~ItemManager() {
}

// -----------------------------------------------------------------------
// GetInstance @ 0x00112c34 — C++ static-local singleton
// -----------------------------------------------------------------------
ItemManager* ItemManager::GetInstance() {
    static ItemManager s_instance;
    return &s_instance;
}

// -----------------------------------------------------------------------
// GetItemSavePath @ 0x00111fd8 — binary: returns "ItemSave.xml" (rodata 0x001b9e40)
// DIFFERS from original: port had "Data/xml/ItemSave.xml" — fixed to match binary.
// -----------------------------------------------------------------------
const char* ItemManager::GetItemSavePath() const {
    return "ItemSave.xml";
}

// Port specific: build the full on-disk path for ItemSave.xml.
// On Emscripten, routes to the IDBFS-backed /save mount rather than the
// read-only MEMFS asset bundle.  On all other platforms, prepends data_dir.
static std::string BuildItemSaveFullPath() {
#if defined(__EMSCRIPTEN__)
    return std::string("/save/ItemSave.xml");
#else
    Game* game = Game::GetInstance();
    if (!game) return std::string("ItemSave.xml");
    return game->data_dir + "/ItemSave.xml";
#endif
}

// -----------------------------------------------------------------------
// LoadItemData @ 0x001131f4
// Parse itemlist.xml, then load ItemSave.xml for persistence.
// -----------------------------------------------------------------------
void ItemManager::LoadItemData() {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Phase 1: Parse itemlist.xml
    // Binary literal is "xml/itemList.xml" @ 0x1ba064 (capital L), shipped
    // asset is "itemlist.xml". Bada's tinyxml resolves CI; the SDL port
    // matches via Mortar::ResolvePathCI fallback below.
    std::string xmlPath = game->data_dir + "/xml/itemList.xml";
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.LoadFile(xmlPath.c_str());
    if (err != tinyxml2::XML_SUCCESS) {
        std::string ci = Mortar::ResolvePathCI(xmlPath.c_str());
        if (!ci.empty()) err = doc.LoadFile(ci.c_str());
    }

    m_ByHash.clear();
    m_Items.clear();
    for (int i = 0; i < 4; i++) m_ByHashType[i].clear();
    m_DefaultItems[0] = m_DefaultItems[1] = m_DefaultItems[2] = m_DefaultItems[3] = nullptr;

    if (err == tinyxml2::XML_SUCCESS) {
        tinyxml2::XMLElement* root = doc.FirstChildElement("itemManagerFile");  // 0x1ba075
        if (root != nullptr) {
            for (tinyxml2::XMLElement* e = root->FirstChildElement("item");  // 0x1b9e95
                 e != nullptr;
                 e = e->NextSiblingElement("item")) {

                // Allocate typed item
                const char* typeStr = e->Attribute("type");  // 0x1b9372
                int type = ParseItemType(typeStr);            // 0=SLASH_MODIFIER..3=REMOVEADS

                ItemInfo* item;
                if (type == 0) {
                    item = new SlashModInfo();  // 0x110 bytes
                } else {
                    item = new ItemInfo();      // 0x40 bytes
                }
                item->m_Type = (int8_t)type;
                // Virtual dispatch to ItemInfo::Parse or ParseSlashModInfo
                item->Parse(e);

                // Achievement-gate check (binary: LoadItemData inner block)
                FruitSaveData* sd = GetSaveData();
                if (sd != nullptr) {
                    int unlocked = sd->IsAchievementUnlocked(item->m_Hash);
                    if (unlocked == 0) {
                        AchievementManager* am = AchievementManager::GetInstance();
                        int exists = am->AchievementExists(item->m_Hash);
                        if (exists == 0 && item->m_Cost > 0) {
                            // Not found in achievement system + positive cost
                            // → mark as new/free
                            item->m_bSeen = false;   // "new item" badge
                            item->m_Cost = -1;       // make it free
                            // ShopScreen::NewItem() — sets ShopScreen new-item float flag.
                            ShopScreen::s_NewItemAlpha = 1.0f;
                        }
                    } else {
                        // Achievement already unlocked → auto-unlock item
                        item->m_Cost = -1;
                    }
                }

                m_Items.push_back(item);
                m_ByHash[item->m_Hash] = item;
                m_ByHashType[type][item->m_Hash] = item;

                // Set default item for this type (first seen wins; type==3 excluded)
                if (m_DefaultItems[type] == nullptr && type != 3) {
                    m_DefaultItems[type] = item;
                }
            }
        }
    } else {
        LOG_ERROR("ITEM/LoadItemData", "failed to open '%s' (error %d)",
                  xmlPath.c_str(), (int)err);
    }

    // Phase 2: Load save state from ItemSave.xml
    // Binary: GetItemSavePath() returns "ItemSave.xml" (flat, no subdir).
    // Port: use BuildItemSaveFullPath() which routes to /save on Emscripten.
    std::string saveFullPath = BuildItemSaveFullPath();
    tinyxml2::XMLDocument save;
    tinyxml2::XMLError saveErr = save.LoadFile(saveFullPath.c_str());
    if (saveErr != tinyxml2::XML_SUCCESS) {
        std::string ci = Mortar::ResolvePathCI(saveFullPath.c_str());
        if (!ci.empty()) saveErr = save.LoadFile(ci.c_str());
    }

    if (saveErr == tinyxml2::XML_SUCCESS) {
        tinyxml2::XMLElement* root = save.FirstChildElement("item_save_file");  // 0x1b9e4d
        if (root != nullptr) {
            // Coin balance lives in game_work (+0x20/+0x24/+0x28), not FruitSaveData.
            root->QueryIntAttribute("coins",           &game_work.m_CoinsBalance);
            root->QueryIntAttribute("coinsTotal",      &game_work.m_CoinsTotalEarned);
            root->QueryIntAttribute("levelStartCoins", &game_work.m_CoinsAtGameStart);

            // <boughtItems> section
            tinyxml2::XMLElement* bought = root->FirstChildElement("boughtItems");  // 0x1b9e89
            if (bought != nullptr) {
                for (tinyxml2::XMLElement* e = bought->FirstChildElement("item");  // 0x1b9e95
                     e != nullptr;
                     e = e->NextSiblingElement("item")) {
                    const char* nameVal = e->Attribute("name");  // 0x1c3173
                    if (nameVal && *nameVal) {
                        uint32_t hash = StringHash(nameVal);
                        std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find(hash);
                        if (it != m_ByHash.end()) {
                            ItemInfo* itm = it->second;
                            itm->m_Cost = -1;  // mark purchased

                            const char* seenVal = e->Attribute("seen");  // 0x1b9ea5
                            bool isSeen;
                            if (!seenVal || !*seenVal) {
                                isSeen = false;
                            } else {
                                // ARM idiom: cmp = strcmp(seenVal, "true"); isSeen = (cmp == 0)
                                int cmp = strcmp(seenVal, "true");  // 0x1b9ea0
                                isSeen = (cmp == 0);
                            }
                            itm->m_bSeen = isSeen ? true : false;
                        }
                    }
                }
            }

            // <equippedItems> section
            tinyxml2::XMLElement* equipped = root->FirstChildElement("equippedItems");  // 0x1b9eaa
            if (equipped != nullptr) {
                for (tinyxml2::XMLElement* e = equipped->FirstChildElement("item");  // 0x1b9e95
                     e != nullptr;
                     e = e->NextSiblingElement("item")) {
                    const char* nameVal = e->Attribute("name");  // 0x1c3173
                    if (nameVal && *nameVal) {
                        uint32_t hash = StringHash(nameVal);
                        ItemInfo* itm = GetItem(hash);
                        if (itm != nullptr) {
                            m_DefaultItems[(int)itm->m_Type] = itm;
                        }
                    }
                }
            }
        }
    }
    // No error log on missing save — expected on first run.

    // Phase 3: Apply equipped items for all 4 types
    // Binary: field_0x8 = 0 (reset counter); then loop SetEquippedItem for i=0..3
    // Note: field_0x8 in the binary overlaps m_DefaultItems (see spec note on +0x0c).
    // The "reset counter" is g_SetEquippedItemFuncCalls, not a struct field.
    g_SetEquippedItemFuncCalls = 0;
    for (int i = 0; i < 4; i++) {
        SetEquippedItem(i, m_DefaultItems[i]);
    }
}

// -----------------------------------------------------------------------
// IsEquipped @ 0x0015fa6c
// -----------------------------------------------------------------------
int ItemManager::IsEquipped(ItemInfo* item) const {
    if (item == nullptr) return 0;
    return (m_DefaultItems[(int)item->m_Type] == item) ? 1 : 0;
}

// -----------------------------------------------------------------------
// SetEquippedItem @ 0x0011307c
// -----------------------------------------------------------------------
void ItemManager::SetEquippedItem(int type, ItemInfo* item) {
    int* funcCalls = &g_SetEquippedItemFuncCalls;

    if (type == ITEM_TYPE_BACKGROUND) {
        if (*funcCalls > 0) {
            // Binary: Mortar::SmartPtr<Texture> curBG; GetCurrentBackground(&curBG);
            // equal = SmartPtr::operator_cast_to_bool(&curBG);
            // if equal != 0 goto DONE;
            // Port stub: we can't check curBG without ChangeBackground ported,
            // so skip the equality check and always call ChangeBackground.
            // TODO: implement curBG equality check when ChangeBackground lands.
        }
        const char* texName = (item != nullptr) ? item->m_pTextureName : nullptr;  // +0x30
        ChangeBackground(texName);  // defined in MenuBackground.cpp; binary 0x0016ae8c
    } else if (type == ITEM_TYPE_UPSELL) {
        if (*funcCalls >= 1) {
            (*funcCalls)--;
            return;
        }
    } else if (type == ITEM_TYPE_BLADE) {
        // Binary: SlashEntity* slash = (*(*this))->GetSlashEntity()
        // if item == NULL: SlashEntity::InitModColours + SetModScales
        // else: virtual call item->SetEquipped()
        if (item == nullptr) {
            // Binary: SlashEntity::InitModColours(slash) + SetModScales(defaults)
            // slash is obtained from (*(*this))->GetSlashEntity() — port uses g_pSlashEntity directly.
            SlashEntity::InitModColours();
            SlashEntity::SetModScales(1.0f, 1.0f, 0.0f, 1.0f, false, false, 0.0f);
        } else {
            item->SetEquipped();  // virtual call -> SlashModInfo::SetEquipped @ 0x00112430
        }
    }
    // DONE:
    if (*funcCalls > 0) (*funcCalls)--;
    m_DefaultItems[type] = item;
    if (item != nullptr) item->m_bSeen = true;  // mark as seen when equipped
}

// -----------------------------------------------------------------------
// BuyItem @ 0x00112498
// Returns 1 on success, 0 if not found or insufficient coins.
// -----------------------------------------------------------------------
int ItemManager::BuyItem(uint32_t hash) {
    std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find(hash);
    if (it == m_ByHash.end()) return 0;

    ItemInfo* item = it->second;
    int cost = item->m_Cost;  // +0x0c
    // Coin balance lives in game_work.m_CoinsBalance (game_work +0x20), not FruitSaveData.
    int coins = game_work.m_CoinsBalance;

    // ARM idiom: "if (-1 < cost && cost <= coins)" = if (cost >= 0 && cost <= coins)
    if (cost >= 0 && cost <= coins) {
        // AddCoins(-cost): deduct from game_work balance (binary @ 0x0010a3bc).
        game_work.m_CoinsBalance -= cost;
        item->m_Cost = -1;  // mark purchased
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------
// GetNumNewItems @ 0x00112048
// -----------------------------------------------------------------------
int ItemManager::GetNumNewItems() {
    int count = 0;
    for (std::vector<ItemInfo*>::const_iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        if (!(*it)->m_bSeen) count++;
    }
    return count;
}

// -----------------------------------------------------------------------
// AreNewItems @ 0x0011200c
// -----------------------------------------------------------------------
bool ItemManager::AreNewItems() {
    for (std::vector<ItemInfo*>::const_iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        if (!(*it)->m_bSeen) return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// GetItem @ 0x00112084 — lookup by hash in m_ByHash
// -----------------------------------------------------------------------
ItemInfo* ItemManager::GetItem(uint32_t hash) {
    std::map<uint32_t, ItemInfo*>::const_iterator it = m_ByHash.find(hash);
    if (it == m_ByHash.end()) return nullptr;
    return it->second;
}

// -----------------------------------------------------------------------
// SaveItemInfo @ 0x00112210
// -----------------------------------------------------------------------
void ItemManager::SaveItemInfo() {
    Game* game = Game::GetInstance();
    if (!game) return;

    tinyxml2::XMLDocument doc;

    // Build root <item_save_file version="1.0" coins=N coinsTotal=N levelStartCoins=N>
    // Coin balance lives in game_work (+0x20/+0x24/+0x28), not FruitSaveData.
    tinyxml2::XMLElement* root = doc.NewElement("item_save_file");  // 0x1b9e4d
    root->SetAttribute("version", "1.0");  // 0x1b9e5c / 0x1b9e64
    root->SetAttribute("coins",           (int)game_work.m_CoinsBalance);
    root->SetAttribute("coinsTotal",      (int)game_work.m_CoinsTotalEarned);
    root->SetAttribute("levelStartCoins", (int)game_work.m_CoinsAtGameStart);

    // <boughtItems> section: all items with m_Cost < 0 (purchased)
    tinyxml2::XMLElement* bought = doc.NewElement("boughtItems");  // 0x1b9e89
    for (std::vector<ItemInfo*>::iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        ItemInfo* item = *it;
        if (item->m_Cost < 0) {  // cost == -1 → purchased
            tinyxml2::XMLElement* e = doc.NewElement("item");  // 0x1b9e95
            e->SetAttribute("name", item->m_pName);            // 0x1c3173 / +0x04
            // Binary: "false" if m_bSeen==0, "true" otherwise
            const char* seenVal = (!item->m_bSeen) ? "false" : "true";  // 0x1b9e9a/0x1b9ea0
            e->SetAttribute("seen", seenVal);                  // 0x1b9ea5
            bought->InsertEndChild(e);
        }
    }
    root->InsertEndChild(bought);

    // <equippedItems> section: m_DefaultItems[0..3] with m_Cost <= 0 (free or purchased)
    tinyxml2::XMLElement* equip = doc.NewElement("equippedItems");  // 0x1b9eaa
    for (int i = 0; i < 4; i++) {
        ItemInfo* item = m_DefaultItems[i];
        if (item != nullptr && item->m_Cost < 1) {  // cost <= 0 = owned
            tinyxml2::XMLElement* e = doc.NewElement("item");  // 0x1b9e95
            e->SetAttribute("name", item->m_pName);            // 0x1c3173
            equip->InsertEndChild(e);
        }
    }
    root->InsertEndChild(equip);

    doc.InsertEndChild(root);

    // Build full save path — binary uses flat "ItemSave.xml" (no subdir).
    // Port: use BuildItemSaveFullPath() which routes to /save on Emscripten.
    std::string saveFullPath = BuildItemSaveFullPath();
    doc.SaveFile(saveFullPath.c_str());  // tinyxml2::XMLDocument::SaveFile uses fopen directly
#if defined(__EMSCRIPTEN__)
    // Port specific: flush the IDBFS /save mount to IndexedDB after each
    // write so data survives page reload/close.
    EM_ASM({ FS.syncfs(false, function(err) {}); });
#endif
}

// -----------------------------------------------------------------------
// UnLoadItemData @ 0x001124fc — frees ItemInfo objects via virtual deleting dtor.
// Maps are NOT cleared; LoadItemData re-clears on next load. Caller:
// GameDestroy @ 0x0010b7ec, between AchievementManager::UnLoadAchievementInfo
// and HUD teardown.
// -----------------------------------------------------------------------
// Binary @ 0x001124fc
void ItemManager::UnLoadItemData() {
    std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.begin();
    for (; it != m_ByHash.end(); ++it) {
        ItemInfo* item = it->second;
        if (item != NULL) {
            delete item;        // virtual dtor: ItemInfo's vtable[1] is the deleting dtor in binary
            it->second = NULL;
        }
    }
}

// -----------------------------------------------------------------------
// UnlockItem @ 0x001120b4 — achievement-driven unlock for items keyed by hash.
// Caller: FruitSaveData::Update @ 0x0012b3dc when a non-numeric achievement
// key fires.
// -----------------------------------------------------------------------
// Binary @ 0x001120b4
bool ItemManager::UnlockItem(uint32_t hash) {
    std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find(hash);
    if (it == m_ByHash.end()) return false;
    ItemInfo* item = it->second;
    item->m_bSeen = false;     // +0x3c — flag NEW for badge
    item->m_Cost  = -1;        // +0x0c — purchased/unlocked
    return true;
}

// -----------------------------------------------------------------------
// GetFirst @ 0x0015fbc8 — begin iteration over m_Items
// it = 0 on entry; returns item at m_Items[0] or nullptr
// -----------------------------------------------------------------------
// DIFFERS: original = vector::iterator& (4 bytes); port uses int& index (4 bytes,
//          functionally equivalent — both are 4-byte references walked over m_Items).
ItemInfo* ItemManager::GetFirst(int& it) const {
    it = 0;
    if (m_Items.empty()) return nullptr;
    return m_Items[0];
}

// -----------------------------------------------------------------------
// GetNext @ 0x0015fbf4 — advance + return next item
// -----------------------------------------------------------------------
// DIFFERS: original = vector::iterator& (4 bytes); port uses int& index (4 bytes,
//          functionally equivalent — both are 4-byte references walked over m_Items).
ItemInfo* ItemManager::GetNext(int& it) const {
    it++;
    if (it < 0 || (size_t)it >= m_Items.size()) return nullptr;
    return m_Items[it];
}

// -----------------------------------------------------------------------
// GetEquipped — helper for ShopScreen
// -----------------------------------------------------------------------
ItemInfo* ItemManager::GetEquipped(int type) const {
    if (type < 0 || type >= 4) return nullptr;
    return m_DefaultItems[type];
}

// ---- Additional ItemManager public API (binary missing-symbol set) ----

// -----------------------------------------------------------------------
// EquipItem @ 0x00103198 (ELF) / 0x00113198 (Ghidra)
// Equip item by hash. Returns 1 on success, 0 if not found or not purchased.
// Binary: lookup m_ByHash; gate on m_Cost < 0; UnEquip current slot; SetEquippedItem.
// ASM-verified: 2026-05-23 binary @ 0x00103198 (re-analyst)
// -----------------------------------------------------------------------
int ItemManager::EquipItem(unsigned int hash) {
    std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find((uint32_t)hash);
    if (it == m_ByHash.end()) return 0;
    ItemInfo* item = it->second;
    if (item->m_Cost >= 0) return 0;

    // Call UnEquip() on currently-equipped item in this slot (virtual slot +8)
    ItemInfo* current = m_DefaultItems[(int)(int8_t)item->m_Type];
    if (current != nullptr) {
        current->UnEquip();
    }
    SetEquippedItem((int)(int8_t)item->m_Type, item);
    return 1;
}

// -----------------------------------------------------------------------
// UnequipItem @ 0x0010314c (ELF) / 0x0011314c (Ghidra)
// Unequip item by hash. Returns true if item was found.
// Binary: lookup m_ByHash; if found, UnEquip() + SetEquippedItem(type, nullptr).
// ASM-verified: 2026-05-23 binary @ 0x0010314c (re-analyst)
// -----------------------------------------------------------------------
bool ItemManager::UnequipItem(unsigned int hash) {
    std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find((uint32_t)hash);
    if (it == m_ByHash.end()) return false;
    ItemInfo* item = it->second;
    item->UnEquip();
    SetEquippedItem((int)(int8_t)item->m_Type, nullptr);
    return true;
}

// PlayAlternateComboSound @ 0x0011303c
bool ItemManager::PlayAlternateComboSound(int comboIdx) {
    SlashModInfo* m = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (!m) return false;
    return m->m_ComboSounds.PlaySound(comboIdx, 1.0f, 1.0f);
}
// -----------------------------------------------------------------------
// PlayAlternateImpactSound @ 0x00113054
// Binary: *(int*)this == m_DefaultItems[0] (first field = blade slot pointer).
// Dereferences to SlashModInfo, calls m_ImpactSounds.PlaySound(-1, vol, pitch).
// ASM-verified: 2026-05-23 binary @ 0x00113054 / 0x00113068 (re-analyst)
// -----------------------------------------------------------------------
bool ItemManager::PlayAlternateImpactSound(float volume, float pitch) {
    SlashModInfo* mod = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (!mod) return false;
    return mod->m_ImpactSounds.PlaySound(-1, volume, pitch);
}

// -----------------------------------------------------------------------
// PlayAlternateSwipeSound @ 0x00113068
// Binary: *(int*)this == m_DefaultItems[0]; calls m_SwipeSounds.PlaySound(-1, vol, pitch).
// -----------------------------------------------------------------------
bool ItemManager::PlayAlternateSwipeSound(float volume, float pitch) {
    SlashModInfo* mod = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (!mod) return false;
    return mod->m_SwipeSounds.PlaySound(-1, volume, pitch);
}
// -----------------------------------------------------------------------
// SetSwipeLoodVol @ 0x00111ffc
// Binary: field0_0x0 is the equipped blade mod (m_DefaultItems[0], typed
// SlashModInfo*). If non-null, forward the volume to its looping-swipe sound.
//   if (m_DefaultItems[0]) m_DefaultItems[0]->m_LoopingSound.SetLoopDesiredVol(vol);
// Binary @ 0x00111ffc
// -----------------------------------------------------------------------
void ItemManager::SetSwipeLoodVol(float vol) {
    SlashModInfo* mod = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (mod != nullptr) {
        mod->m_LoopingSound.SetLoopDesiredVol(vol);
    }
}

// -----------------------------------------------------------------------
// Update @ 0x00112fc8 — per-frame update of equipped blade mod sounds.
// Binary: field0_0x0 is m_DefaultItems[0] (SlashModInfo*); if non-null,
// SlashModInfo::UpdateSounds(m_DefaultItems[0], dt).
// Binary @ 0x00112fc8
// -----------------------------------------------------------------------
void ItemManager::Update(float dt) {
    SlashModInfo* mod = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (mod != nullptr) {
        mod->UpdateSounds(dt);
    }
}
// ---- end additional API ----
