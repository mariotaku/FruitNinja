#ifndef FN_ITEM_MANAGER_H
#define FN_ITEM_MANAGER_H

// Analysed: 2026-04-25T10:30
//
// ItemManager — singleton that owns all shop items.
// Binary: ctor 0x001121d0; GetInstance 0x00112c34; LoadItemData 0x001131f4.
//
// Struct layout (0x94 bytes) per docs/structs/items.md:
//   +0x00..+0x0f  ItemInfo*               m_DefaultItems[4]   equipped per type
//   +0x10         std::vector<ItemInfo*>  m_Items              all items in XML order
//   +0x1c         std::map<uint32,ItemInfo*> m_ByHash          all items keyed by hash
//   +0x34         std::map<uint32,ItemInfo*> m_ByHashType[0]   SLASH_MODIFIER
//   +0x4c         std::map<uint32,ItemInfo*> m_ByHashType[1]   BACKGROUND
//   +0x64         std::map<uint32,ItemInfo*> m_ByHashType[2]   UPSELL
//   +0x7c         std::map<uint32,ItemInfo*> m_ByHashType[3]   REMOVEADS
//
// NOTE: m_DefaultItems[3] (REMOVEADS) is always NULL (type==3 excluded from
//       default-item assignment in LoadItemData).
//

#include "ItemInfo.h"
#include <cstdint>
#include <vector>
#include <map>

class ItemManager {
public:
    // GetInstance @ 0x00112c34 — C++ static-local singleton
    static ItemManager* GetInstance();

    // EquippedSlashModCount @ .bss 0x0022ece4 — count of active SlashModifiers
    // that have called ApplyModifier. Decremented by RemoveModifier; when it
    // hits 0 the default blade is restored via SetEquippedItem(0, default).
    static int EquippedSlashModCount;

    // LoadItemData @ 0x001131f4 — parse itemlist.xml, load ItemSave.xml
    void LoadItemData();

    // UnLoadItemData @ 0x001124fc — frees all ItemInfo objects; called from GameDestroy
    void UnLoadItemData();

    // UnlockItem @ 0x001120b4 — achievement-driven unlock by hash
    bool UnlockItem(uint32_t hash);

    // IsEquipped @ 0x0015fa6c — returns 1 if item == m_DefaultItems[item->m_Type]
    int IsEquipped(ItemInfo* item) const;

    // SetEquippedItem @ 0x0011307c — sets slot + side effects
    void SetEquippedItem(int type, ItemInfo* item);

    // BuyItem @ 0x00112498 — deducts coins, marks item purchased
    // Returns 1 on success, 0 if item not found or insufficient coins
    int BuyItem(uint32_t hash);

    // GetNumNewItems @ 0x00112048
    int GetNumNewItems();

    // AreNewItems @ 0x0011200c
    bool AreNewItems();

    // GetItem @ 0x00112084 — lookup by hash in m_ByHash
    ItemInfo* GetItem(uint32_t hash);

    // GetItemSavePath — returns "Data/xml/ItemSave.xml"
    const char* GetItemSavePath() const;

    // SaveItemInfo @ 0x00112210 — write ItemSave.xml
    void SaveItemInfo();

    // GetFirst / GetNext @ 0x0015fbc8 / 0x0015fbf4 — iteration over m_Items
    ItemInfo* GetFirst(int& it) const;
    ItemInfo* GetNext(int& it) const;

    // Access equipped item slot (helper for ShopScreen)
    ItemInfo* GetEquipped(int type) const;

    // --- Port helpers for ShopScreen (not in binary public API) --------
    // Returns total item count.
    int GetNumItems() const { return (int)m_Items.size(); }
    // Returns item at index (by position in m_Items vector).
    ItemInfo* GetItemAt(int index) const {
        if (index < 0 || (size_t)index >= m_Items.size()) return nullptr;
        return m_Items[index];
    }

    // +0x00..+0x0f: default (equipped) item per type (binary: m_DefaultItems[4])
    ItemInfo* m_DefaultItems[4];

    // +0x10: all items in XML order
    std::vector<ItemInfo*> m_Items;

    // +0x1c: all items keyed by m_Hash
    std::map<uint32_t, ItemInfo*> m_ByHash;

    // +0x34..+0x7c: per-type maps (4 types × 0x18 bytes each)
    std::map<uint32_t, ItemInfo*> m_ByHashType[4];

private:
    // Binary ctor @ 0x001121d0
    ItemManager();

    // Binary dtor @ 0x001120f0 / 0x00112140
    ~ItemManager();

public:

public:

public:

public:

public:

public:
    // ---- AUTO-STUB MERGE: STUB -- gen_stubs.py ----
    // STUB: ItemManager::EquipItem -- auto stub from binary missing-symbol set
    void EquipItem(unsigned int);
    // STUB: ItemManager::PlayAlternateComboSound -- auto stub from binary missing-symbol set
    void PlayAlternateComboSound(int);
    // STUB: ItemManager::PlayAlternateImpactSound -- auto stub from binary missing-symbol set
    void PlayAlternateImpactSound(float, float);
    // STUB: ItemManager::PlayAlternateSwipeSound -- auto stub from binary missing-symbol set
    void PlayAlternateSwipeSound(float, float);
    // STUB: ItemManager::SetSwipeLoodVol -- auto stub from binary missing-symbol set
    void SetSwipeLoodVol(float);
    // STUB: ItemManager::UnequipItem -- auto stub from binary missing-symbol set
    void UnequipItem(unsigned int);
    // STUB: ItemManager::Update -- auto stub from binary missing-symbol set
    void Update(float);
    // ---- end AUTO-STUB MERGE ----
};

#endif // FN_ITEM_MANAGER_H
