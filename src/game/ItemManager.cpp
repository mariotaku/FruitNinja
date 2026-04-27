// Analysed: 2026-04-25T10:30
//
// ItemManager — binary-faithful implementation.
// Binary: ctor 0x001121d0, LoadItemData 0x00113200, GetInstance 0x00112c34,
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
#include "engine/MenuBackground.h"
#include "entities/SlashEntity.h"
#include "screens/ShopScreen.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// External: g_FruitSaveData singleton — binary GOT @ 0x1f3ac0.
// Port: exposed via FruitSaveData.h (the instance lives in Game::pSaveData).
// We access it through the Game singleton here.
#include "Game.h"
static FruitSaveData* GetSaveData() {
    Game* g = Game::GetInstance();
    return g ? g->pSaveData : nullptr;
}

// g_SetEquippedItemFuncCalls @ 0x1f3cec — static call-guard for SetEquippedItem.
static int g_SetEquippedItemFuncCalls = 0;

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

ItemManager::~ItemManager() {
    for (ItemInfo* item : m_Items) {
        delete item;
    }
    m_Items.clear();
    m_ByHash.clear();
    for (int i = 0; i < 4; i++) m_ByHashType[i].clear();
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

// -----------------------------------------------------------------------
// LoadItemData @ 0x00113200
// Parse itemlist.xml, then load ItemSave.xml for persistence.
// -----------------------------------------------------------------------
void ItemManager::LoadItemData() {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Phase 1: Parse itemlist.xml
    // Binary uses path string "xml/itemList.xml" @ 0x1ba064 (capital L), but the
    // shipped Bada asset is "itemlist.xml" (lowercase). Use the lowercase form so
    // case-sensitive filesystems (Linux + some MSYS2 configs) actually find the file.
    std::string xmlPath = game->data_dir + "/xml/itemlist.xml";
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.LoadFile(xmlPath.c_str());

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
                        int exists = am->AchievementExists();
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
        fprintf(stderr, "ItemManager::LoadItemData: failed to open '%s' (error %d)\n",
                xmlPath.c_str(), (int)err);
    }

    // Phase 2: Load save state from ItemSave.xml
    // Binary: GetItemSavePath() returns "ItemSave.xml" (flat, no subdir).
    // Port: prepend data_dir so the file lands next to FruitySave.xml.
    const char* savePath = GetItemSavePath();
    std::string saveFullPath = game->data_dir + "/" + savePath;
    tinyxml2::XMLDocument save;
    tinyxml2::XMLError saveErr = save.LoadFile(saveFullPath.c_str());

    if (saveErr == tinyxml2::XML_SUCCESS) {
        tinyxml2::XMLElement* root = save.FirstChildElement("item_save_file");  // 0x1b9e4d
        if (root != nullptr) {
            FruitSaveData* sd = GetSaveData();
            if (sd != nullptr) {
                root->QueryIntAttribute("coins",           &sd->m_Coins);            // +0x20
                root->QueryIntAttribute("coinsTotal",      &sd->m_CoinsTotal);       // +0x24
                root->QueryIntAttribute("levelStartCoins", &sd->m_LevelStartCoins);  // +0x28
            }

            // <boughtItems> section
            tinyxml2::XMLElement* bought = root->FirstChildElement("boughtItems");  // 0x1b9e89
            if (bought != nullptr) {
                for (tinyxml2::XMLElement* e = bought->FirstChildElement("item");  // 0x1b9e95
                     e != nullptr;
                     e = e->NextSiblingElement("item")) {
                    const char* nameVal = e->Attribute("name");  // 0x1c3173
                    if (nameVal && *nameVal) {
                        uint32_t hash = StringHash(nameVal);
                        auto it = m_ByHash.find(hash);
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
            // Binary: SmartPtr<Texture> curBG; GetCurrentBackground(&curBG);
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
    auto it = m_ByHash.find(hash);
    if (it == m_ByHash.end()) return 0;

    ItemInfo* item = it->second;
    int cost = item->m_Cost;  // +0x0c
    FruitSaveData* sd = GetSaveData();
    int coins = (sd != nullptr) ? sd->m_Coins : 0;  // +0x20

    // ARM idiom: "if (-1 < cost && cost <= coins)" = if (cost >= 0 && cost <= coins)
    if (cost >= 0 && cost <= coins) {
        // AddCoins(-cost) — deduct coins from FruitSaveData
        if (sd != nullptr) sd->AddCoins(-cost);
        item->m_Cost = -1;  // mark purchased
        return 1;
    }
    return 0;
}

// -----------------------------------------------------------------------
// GetNumNewItems @ 0x00112048
// -----------------------------------------------------------------------
int ItemManager::GetNumNewItems() const {
    int count = 0;
    for (ItemInfo* item : m_Items) {
        if (!item->m_bSeen) count++;
    }
    return count;
}

// -----------------------------------------------------------------------
// AreNewItems @ 0x0011200c
// -----------------------------------------------------------------------
bool ItemManager::AreNewItems() const {
    for (ItemInfo* item : m_Items) {
        if (!item->m_bSeen) return true;
    }
    return false;
}

// -----------------------------------------------------------------------
// GetItem @ 0x00112084 — lookup by hash in m_ByHash
// -----------------------------------------------------------------------
ItemInfo* ItemManager::GetItem(uint32_t hash) const {
    auto it = m_ByHash.find(hash);
    if (it == m_ByHash.end()) return nullptr;
    return it->second;
}

// -----------------------------------------------------------------------
// SaveItemInfo @ 0x00112210
// -----------------------------------------------------------------------
void ItemManager::SaveItemInfo() {
    Game* game = Game::GetInstance();
    if (!game) return;

    FruitSaveData* sd = GetSaveData();

    tinyxml2::XMLDocument doc;

    // Build root <item_save_file version="1.0" coins=N coinsTotal=N levelStartCoins=N>
    tinyxml2::XMLElement* root = doc.NewElement("item_save_file");  // 0x1b9e4d
    root->SetAttribute("version", "1.0");  // 0x1b9e5c / 0x1b9e64
    if (sd != nullptr) {
        root->SetAttribute("coins",           sd->m_Coins);            // +0x20
        root->SetAttribute("coinsTotal",      sd->m_CoinsTotal);       // +0x24
        root->SetAttribute("levelStartCoins", sd->m_LevelStartCoins);  // +0x28
    } else {
        root->SetAttribute("coins",           0);
        root->SetAttribute("coinsTotal",      0);
        root->SetAttribute("levelStartCoins", 0);
    }

    // <boughtItems> section: all items with m_Cost < 0 (purchased)
    tinyxml2::XMLElement* bought = doc.NewElement("boughtItems");  // 0x1b9e89
    for (ItemInfo* item : m_Items) {
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
    const char* savePath = GetItemSavePath();
    std::string saveFullPath = game->data_dir + "/" + savePath;
    doc.SaveFile(saveFullPath.c_str());  // tinyxml2::XMLDocument::SaveFile uses fopen directly
}

// -----------------------------------------------------------------------
// GetFirst @ 0x0015fbc8 — begin iteration over m_Items
// it = 0 on entry; returns item at m_Items[0] or nullptr
// -----------------------------------------------------------------------
ItemInfo* ItemManager::GetFirst(int& it) const {
    it = 0;
    if (m_Items.empty()) return nullptr;
    return m_Items[0];
}

// -----------------------------------------------------------------------
// GetNext @ 0x0015fbf4 — advance + return next item
// -----------------------------------------------------------------------
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
