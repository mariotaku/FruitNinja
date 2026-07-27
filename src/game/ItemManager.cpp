// Analysed: 2026-04-25T10:30
//
// ItemManager — binary-faithful implementation.
// v1.6.1: ctor 0x00138568, LoadItemData 0x00139d68, GetInstance 0x00139534,
//         IsEquipped 0x0015fa6c, SetEquippedItem 0x00139b1c,
//         BuyItem 0x001389c4, GetNumNewItems 0x00138380,
//         AreNewItems 0x00138320, SaveItemInfo 0x00138610,
//         UnLoadItemData 0x00138a64, GetItemSavePath 0x001382e0,
//         UnlockItem 0x0013842c.
//

#include "ItemManager.h"
#include "debug/Logger.h"
#include "ItemParseUtil.h"
#include "FruitSaveData.h"
#include "AchievementManager.h"
#include "engine/util/StringHash.h"
#include "engine/MenuBackground.h"
#include "engine/xml/TiXml.h"
#include "entities/SlashEntity.h"
#include "screens/ShopScreen.h"
#include <cstring>
#include <string>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

#include "Game.h"
#include "game/GameWork.h"

// ItemManager::EquippedSlashModCount @ .bss 0x0022ece4
int ItemManager::EquippedSlashModCount = 0;

// -----------------------------------------------------------------------
// ItemManager ctor @ v1.6.1 0x00138568
// -----------------------------------------------------------------------
ItemManager::ItemManager() {
    m_DefaultItems[0] = nullptr;
    m_DefaultItems[1] = nullptr;
    m_DefaultItems[2] = nullptr;
    m_DefaultItems[3] = nullptr;
}

// DIFFERS: original = no dtor (binary frees items via v1.6.1 UnLoadItemData @0x00138a64).
//          Port keeps the dtor as a defensive no-op so static-destructor order
//          can't double-free.
ItemManager::~ItemManager() {
}

// -----------------------------------------------------------------------
// GetInstance @ v1.6.1 0x00139534 — C++ static-local singleton
// -----------------------------------------------------------------------
ItemManager* ItemManager::GetInstance() {
    static ItemManager s_instance;
    return &s_instance;
}

// -----------------------------------------------------------------------
// GetItemSavePath @ v1.6.1 0x001382e0 — binary: returns "ItemSave.xml" (rodata 0x001b9e40)
// -----------------------------------------------------------------------
const char* ItemManager::GetItemSavePath() const {
    return "ItemSave.xml";
}

// Port specific: build the full on-disk path for ItemSave.xml, under
// <save_dir>/ItemSave.xml on every platform -- save_dir is resolved
// per-platform in exactly one place per backend (Mortar_ResolveSaveDir for
// host/webOS/Emscripten -- src/platform/SaveDirSDL.h; FN_SAVE_DIR on Wii --
// GameWii.cpp), so this function carries no platform branches.
static std::string BuildItemSaveFullPath() {
    return Game::GetInstance()->save_dir + "/" + "ItemSave.xml";
}

// -----------------------------------------------------------------------
// LoadItemData @ v1.6.1 0x00139d68
// Parse itemList.xml, then load ItemSave.xml for persistence.
// DIFFERS: original = Mortar TiXml (operator new(0x48)) (v1.6.1 LoadItemData @0x00139d68),
//   using tinyxml2 because the TiXml subsystem is unported -- container/iteration logic matches.
// DIFFERS: original = saves at GetItemSavePath() return ("ItemSave.xml", same directory);
//   port uses BuildItemSaveFullPath() which routes to <save_dir>/ItemSave.xml
//   (per-platform save_dir -- see src/platform/SaveDirSDL.h).
// -----------------------------------------------------------------------
void ItemManager::LoadItemData() {
    // Phase 1: Parse itemList.xml (binary path v1.6.1 LoadItemData @0x00139d68)
    m_ByHash.clear();
    m_Items.clear();
    for (int i = 0; i < 4; i++) m_ByHashType[i].clear();
    m_DefaultItems[0] = m_DefaultItems[1] = m_DefaultItems[2] = m_DefaultItems[3] = nullptr;

    {
        TiXmlDocument doc;
        if (doc.LoadFile("xml/itemList.xml")) {
            TiXmlElement root = doc.FirstChildElement("itemManagerFile");  // 0x1ba075
            if (root) {
                for (TiXmlElement e = root.FirstChildElement("item");  // 0x1b9e95
                     e;
                     e = e.NextSiblingElement("item")) {

                    const char* typeStr = e.Attribute("type");  // 0x1b9372
                    int type = ParseItemType(typeStr);

                    ItemInfo* item;
                    if (type == 0) {
                        item = new SlashModInfo();
                    } else {
                        item = new ItemInfo();
                    }
                    item->m_Type = (int8_t)type;
                    item->Parse(&e);

                    // Achievement-gate check (binary: game_work.pM_SaveData directly, no null guard)
                    FruitSaveData* sd = game_work.m_SaveData;
                    if (sd->IsAchievementUnlocked(item->m_Hash)) {
                        item->m_Cost = -1;
                    } else {
                        AchievementManager* am = AchievementManager::GetInstance();
                        if (!am->AchievementExists(item->m_Hash) && item->m_Cost > 0) {
                            item->m_bSeen = false;
                            item->m_Cost = -1;
                            ShopScreen::s_ScrollOffset = 1.0f;
                        }
                    }

                    m_Items.push_back(item);
                    m_ByHash[item->m_Hash] = item;
                    m_ByHashType[type][item->m_Hash] = item;

                    if (m_DefaultItems[type] == nullptr && type != 3) {
                        m_DefaultItems[type] = item;
                    }
                }
            }
        }
    }

    // Phase 2: Load save state from ItemSave.xml
    {
        std::string saveFullPath = BuildItemSaveFullPath();
        TiXmlDocument save;
        if (save.LoadFile(saveFullPath.c_str())) {
            TiXmlElement root = save.FirstChildElement("item_save_file");  // 0x1b9e4d
            if (root) {
                root.QueryIntAttribute("coins",           &game_work.m_CoinsBalance);
                root.QueryIntAttribute("coinsTotal",      &game_work.m_CoinsTotalEarned);
                root.QueryIntAttribute("levelStartCoins", &game_work.m_CoinsAtGameStart);

                TiXmlElement bought = root.FirstChildElement("boughtItems");  // 0x1b9e89
                if (bought) {
                    for (TiXmlElement e = bought.FirstChildElement("item");  // 0x1b9e95
                         e;
                         e = e.NextSiblingElement("item")) {
                        const char* nameVal = e.Attribute("name");  // 0x1c3173
                        if (nameVal && *nameVal) {
                            uint32_t hash = StringHash(nameVal);
                            std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find(hash);
                            if (it != m_ByHash.end()) {
                                ItemInfo* itm = it->second;
                                itm->m_Cost = -1;
                                const char* seenVal = e.Attribute("seen");  // 0x1b9ea5
                                itm->m_bSeen = (seenVal && *seenVal && strcmp(seenVal, "true") == 0);
                            }
                        }
                    }
                }

                TiXmlElement equipped = root.FirstChildElement("equippedItems");  // 0x1b9eaa
                if (equipped) {
                    for (TiXmlElement e = equipped.FirstChildElement("item");  // 0x1b9e95
                         e;
                         e = e.NextSiblingElement("item")) {
                        const char* nameVal = e.Attribute("name");  // 0x1c3173
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
    }

    // Phase 3: Apply equipped items for all 4 types
    // Binary: zeros m_DefaultItems[2] @ +8 before SetEquippedItem loop so the
    // UPSELL slot doesn't short-circuit via funcCalls guard.
    m_DefaultItems[2] = nullptr;
    for (int i = 0; i < 4; i++) {
        SetEquippedItem((ItemType)i, m_DefaultItems[i]);
    }
}

// -----------------------------------------------------------------------
// IsEquipped @ 0x0015fa6c (address not re-verified for v1.6.1)
// -----------------------------------------------------------------------
int ItemManager::IsEquipped(ItemInfo* item) {
    if (item == nullptr) return 0;
    return (m_DefaultItems[(int)item->m_Type] == item) ? 1 : 0;
}

// -----------------------------------------------------------------------
// SetEquippedItem @ v1.6.1 0x00139b1c
// Binary: funcCalls is a function-local static inside SetEquippedItem, NOT a
// struct field or file-scope global. It is never reset by LoadItemData;
// LoadItemData instead zeroes m_DefaultItems[2] before calling the loop.
// -----------------------------------------------------------------------
void ItemManager::SetEquippedItem(ItemType type, ItemInfo* item) {
    static int funcCalls = 0;

    if (type == ITEM_TYPE_BACKGROUND) {
        if (funcCalls > 0) {
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
        if (funcCalls >= 1) {
            funcCalls--;
            return;
        }
    } else if (type == ITEM_TYPE_BLADE) {
        // TODO: v1.6.1 0x00139b1c (SetEquippedItem) -- binary uses virtual GetSlashEntity
        // on the currently-equipped item ((*m_DefaultItems[0])->vtable[+8]() = GetSlashEntity),
        // then passes the result to SlashEntity::InitModColours(slash). Port uses static call
        // (g_pSlashEntity directly) pending GetSlashEntity virtual being ported to ItemInfo.
        if (item == nullptr) {
            // Binary: SlashEntity::InitModColours(slash) + SetModScales(defaults)
            // slash obtained from (*m_DefaultItems[0])->GetSlashEntity(); port uses global.
            SlashEntity::InitModColours();
            SlashEntity::SetModScales(1.0f, 1.0f, 0.0f, 1.0f, false, false, 0.0f);
        } else {
            item->SetEquipped();  // virtual call -> SlashModInfo::SetEquipped @ 0x00138944
        }
    }
    // DONE:
    if (funcCalls > 0) funcCalls--;
    m_DefaultItems[type] = item;
    if (item != nullptr) item->m_bSeen = true;  // mark as seen when equipped
}

// -----------------------------------------------------------------------
// BuyItem @ v1.6.1 0x001389c4
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
        AddCoins(-cost);  // v1.6.1 0x001389c4 (BuyItem) calls AddCoins(-cost) @0x00119f78
        item->m_Cost = -1;  // mark purchased
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------
// GetNumNewItems @ v1.6.1 0x00138380
// -----------------------------------------------------------------------
int ItemManager::GetNumNewItems() {
    int count = 0;
    for (std::vector<ItemInfo*>::const_iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        if (!(*it)->m_bSeen) count++;
    }
    return count;
}

// -----------------------------------------------------------------------
// AreNewItems @ v1.6.1 0x00138320
// -----------------------------------------------------------------------
bool ItemManager::AreNewItems() {
    for (std::vector<ItemInfo*>::const_iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        if (!(*it)->m_bSeen) return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// GetItem @ 0x00112084 (address not re-verified for v1.6.1) — lookup by hash in m_ByHash
// -----------------------------------------------------------------------
ItemInfo* ItemManager::GetItem(uint32_t hash) {
    std::map<uint32_t, ItemInfo*>::const_iterator it = m_ByHash.find(hash);
    if (it == m_ByHash.end()) return nullptr;
    return it->second;
}

// -----------------------------------------------------------------------
// SaveItemInfo @ v1.6.1 0x00138610
// -----------------------------------------------------------------------
void ItemManager::SaveItemInfo() {
    TiXmlDocument doc;

    // Build root <item_save_file version="1.0" coins=N coinsTotal=N levelStartCoins=N>
    // Coin balance lives in game_work (+0x20/+0x24/+0x28), not FruitSaveData.
    TiXmlElement root = doc.NewElement("item_save_file");  // 0x1b9e4d
    root.SetAttribute("version", "1.0");  // 0x1b9e5c / 0x1b9e64
    root.SetAttribute("coins",           (int)game_work.m_CoinsBalance);
    root.SetAttribute("coinsTotal",      (int)game_work.m_CoinsTotalEarned);
    root.SetAttribute("levelStartCoins", (int)game_work.m_CoinsAtGameStart);

    // <boughtItems> section: all items with m_Cost < 0 (purchased)
    TiXmlElement bought = doc.NewElement("boughtItems");  // 0x1b9e89
    for (std::vector<ItemInfo*>::iterator it = m_Items.begin(); it != m_Items.end(); ++it) {
        ItemInfo* item = *it;
        if (item->m_Cost < 0) {  // cost == -1 — purchased
            TiXmlElement e = doc.NewElement("item");  // 0x1b9e95
            e.SetAttribute("name", item->m_pName);    // 0x1c3173 / +0x04
            // Binary: "false" if m_bSeen==0, "true" otherwise
            const char* seenVal = (!item->m_bSeen) ? "false" : "true";  // 0x1b9e9a/0x1b9ea0
            e.SetAttribute("seen", seenVal);           // 0x1b9ea5
            bought.InsertEndChild(e);
        }
    }
    root.InsertEndChild(bought);

    // <equippedItems> section: m_DefaultItems[0..3] with m_Cost <= 0 (free or purchased)
    TiXmlElement equip = doc.NewElement("equippedItems");  // 0x1b9eaa
    for (int i = 0; i < 4; i++) {
        ItemInfo* item = m_DefaultItems[i];
        if (item != nullptr && item->m_Cost < 1) {  // cost <= 0 = owned
            TiXmlElement e = doc.NewElement("item");  // 0x1b9e95
            e.SetAttribute("name", item->m_pName);    // 0x1c3173
            equip.InsertEndChild(e);
        }
    }
    root.InsertEndChild(equip);

    doc.InsertEndChild(root);

    // Build full save path — binary uses flat "ItemSave.xml" (no subdir).
    // Port: use BuildItemSaveFullPath() which routes to <save_dir>/ItemSave.xml.
    std::string saveFullPath = BuildItemSaveFullPath();
    doc.SaveFile(saveFullPath.c_str());
#if defined(__EMSCRIPTEN__)
    // Port specific: flush the IDBFS /save mount to IndexedDB after each
    // write so data survives page reload/close.
    EM_ASM({ FS.syncfs(false, function(err) {}); });
#endif
}

// -----------------------------------------------------------------------
// UnLoadItemData @ v1.6.1 0x00138a64 — frees ItemInfo objects via virtual deleting dtor.
// Maps are NOT cleared; LoadItemData re-clears on next load. Caller:
// GameDestroy, between AchievementManager::UnLoadAchievementInfo and HUD teardown.
// -----------------------------------------------------------------------
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
// UnlockItem @ v1.6.1 0x0013842c — achievement-driven unlock for items keyed by hash.
// Caller: FruitSaveData::Update when a non-numeric achievement key fires.
// -----------------------------------------------------------------------
bool ItemManager::UnlockItem(uint32_t hash) {
    std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find(hash);
    if (it == m_ByHash.end()) return false;
    ItemInfo* item = it->second;
    item->m_bSeen = false;     // +0x3c — flag NEW for badge
    item->m_Cost  = -1;        // +0x0c — purchased/unlocked
    return true;
}

// -----------------------------------------------------------------------
// ASM-spec v1.6.1 ItemManager::GetFirst @ 0x001b6bc8 — begin iteration over m_Items
// it = m_Items.begin(); returns *it or nullptr if empty
// -----------------------------------------------------------------------
ItemInfo* ItemManager::GetFirst(std::vector<ItemInfo*>::iterator& it) {
    it = m_Items.begin();
    if (it == m_Items.end()) return nullptr;
    return *it;
}

// -----------------------------------------------------------------------
// ASM-spec v1.6.1 ItemManager::GetNext @ 0x001b6c08 — advance + return next item
// -----------------------------------------------------------------------
ItemInfo* ItemManager::GetNext(std::vector<ItemInfo*>::iterator& it) {
    ++it;
    if (it == m_Items.end()) return nullptr;
    return *it;
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
// EquipItem @ 0x00139ccc
// Equip item by hash. Returns 1 on success, 0 if not found or not purchased.
// Binary: lookup m_ByHash; gate on m_Cost < 0; UnEquip current slot; SetEquippedItem.
// ASM-verified: 2026-05-23 v1.6.1 ItemManager::EquipItem @ 0x00139ccc (re-analyst)
// -----------------------------------------------------------------------
int ItemManager::EquipItem(unsigned long hash) {
    std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find((uint32_t)hash);
    if (it == m_ByHash.end()) return 0;
    ItemInfo* item = it->second;
    if (item->m_Cost >= 0) return 0;

    // Call UnEquip() on currently-equipped item in this slot (virtual slot +8)
    ItemInfo* current = m_DefaultItems[(int)(int8_t)item->m_Type];
    if (current != nullptr) {
        current->UnEquip();
    }
    SetEquippedItem((ItemType)(int8_t)item->m_Type, item);
    return 1;
}

// -----------------------------------------------------------------------
// UnequipItem @ 0x00139c4c
// Unequip item by hash. Returns true if item was found.
// Binary: lookup m_ByHash; if found, UnEquip() + SetEquippedItem(type, nullptr).
// ASM-verified: 2026-05-23 v1.6.1 ItemManager::UnequipItem @ 0x00139c4c (re-analyst)
// -----------------------------------------------------------------------
bool ItemManager::UnequipItem(unsigned long hash) {
    std::map<uint32_t, ItemInfo*>::iterator it = m_ByHash.find((uint32_t)hash);
    if (it == m_ByHash.end()) return false;
    ItemInfo* item = it->second;
    item->UnEquip();
    SetEquippedItem((ItemType)(int8_t)item->m_Type, nullptr);
    return true;
}

// PlayAlternateComboSound @ v1.6.1 0x00139ad0
bool ItemManager::PlayAlternateComboSound(int comboIdx) {
    SlashModInfo* m = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (!m) return false;
    return m->m_ComboSounds.PlaySound(comboIdx, 1.0f, 1.0f);
}
// -----------------------------------------------------------------------
// PlayAlternateImpactSound @ v1.6.1 0x00139aec
// Binary: *(int*)this == m_DefaultItems[0] (first field = blade slot pointer).
// Dereferences to SlashModInfo, calls m_ImpactSounds.PlaySound(-1, vol, pitch).
// ASM-verified: 2026-05-23 v1.6.1 ItemManager::PlayAlternateImpactSound @ 0x00139aec (re-analyst)
// -----------------------------------------------------------------------
bool ItemManager::PlayAlternateImpactSound(float volume, float pitch) {
    SlashModInfo* mod = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (!mod) return false;
    return mod->m_ImpactSounds.PlaySound(-1, volume, pitch);
}

// -----------------------------------------------------------------------
// PlayAlternateSwipeSound @ v1.6.1 0x00139b04
// Binary: *(int*)this == m_DefaultItems[0]; calls m_SwipeSounds.PlaySound(-1, vol, pitch)
// on the mod's +0x84 SlashSoundMods. Returns PlaySound's result verbatim
// (m_bPlayOntop byte; 0 when the equipped blade has no swipe sounds), which
// SlashEntity::PlaySwipe @0x001e8550 uses to gate the stock "Sword-swipe-%d".
// -----------------------------------------------------------------------
bool ItemManager::PlayAlternateSwipeSound(float volume, float pitch) {
    SlashModInfo* mod = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (!mod) return false;
    return mod->m_SwipeSounds.PlaySound(-1, volume, pitch);
}
// -----------------------------------------------------------------------
// SetSwipeLoodVol @ v1.6.1 0x0013830c
// Binary: field0_0x0 is the equipped blade mod (m_DefaultItems[0], typed
// SlashModInfo*). If non-null, forward the volume to its looping-swipe sound.
//   if (m_DefaultItems[0]) m_DefaultItems[0]->m_LoopingSound.SetLoopDesiredVol(vol);
// -----------------------------------------------------------------------
void ItemManager::SetSwipeLoodVol(float vol) {
    SlashModInfo* mod = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (mod != nullptr) {
        mod->m_LoopingSound.SetLoopDesiredVol(vol);
    }
}

// -----------------------------------------------------------------------
// Update @ v1.6.1 0x00139a34 — per-frame update of equipped blade mod sounds.
// Binary: field0_0x0 is m_DefaultItems[0] (SlashModInfo*); if non-null,
// SlashModInfo::UpdateSounds(m_DefaultItems[0], dt).
// -----------------------------------------------------------------------
void ItemManager::Update(float dt) {
    SlashModInfo* mod = static_cast<SlashModInfo*>(m_DefaultItems[0]);
    if (mod != nullptr) {
        mod->UpdateSounds(dt);
    }
}
// ---- end additional API ----
