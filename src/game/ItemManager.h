#ifndef FN_ITEM_MANAGER_H
#define FN_ITEM_MANAGER_H

//
// ItemManager — singleton that owns the list of unlockable items and tracks which
// item is currently equipped per category (blade, background, etc.).
//
// Binary addresses:
//   ctor         0x001121d0
//   dtor         0x001120f0 / 0x00112140
//   GetInstance  (static singleton, GOT-relative pattern)
//   IsEquipped   0x0015fa6c  returns 1 if param_1 == s_equippedItems[param_1->m_Type]
//   SetEquippedItem 0x0011307c  sets slot + calls Apply/ChangeBackground
//   BuyItem      0x00112498  (struct-return via r0; ulong param_1 = ptr)
//   GetNumNewItems 0x00112048
//   AreNewItems  0x0011200c
//
// Struct layout (from ctor 0x001121d0):
//   +0x00..+0x0f  ItemInfo* s_equippedItems[4] — one per ItemType
//   +0x10         std::vector<ItemInfo*> m_Items      (all items)
//   +0x1c         std::map<ulong, ItemInfo*> m_ByHash0
//   +0x34         std::map<ulong, ItemInfo*> m_ByHash1
//   +0x4c         std::map<ulong, ItemInfo*> m_ByHash2
//   +0x64         std::map<ulong, ItemInfo*> m_ByHash3
//   +0x7c         std::map<ulong, ItemInfo*> m_ByHash4
//   +0x0c         int m_EquippedSlashModCount (decremented by SlashModifier::RemoveModifier)
//
// Port status: STUB — singleton returns empty manager. IsEquipped always returns 0.
// SetEquippedItem and BuyItem are no-ops. Sufficient for ShopScreen to compile and
// display the UI without crashing. Full impl requires items.xml parsing + FruitSaveData.
//
// Analysed: 2026-04-25T00:00
//

#include "ItemInfo.h"
#include <cstdint>

class ItemManager {
public:
    // Matches singleton pattern in binary. Returns a valid (empty) instance.
    static ItemManager* GetInstance();

    // ItemManager::IsEquipped @ 0x0015fa6c
    // Returns 1 if param_1 == s_equippedItems[param_1->m_Type], else 0.
    // Stub: returns 0 (nothing is equipped).
    int IsEquipped(ItemInfo* item) const;

    // ItemManager::SetEquippedItem @ 0x0011307c
    // Sets s_equippedItems[type] = item and applies side effects
    // (ChangeBackground for type==1, SlashEntity apply for type==0).
    // Stub: no-op.
    void SetEquippedItem(int type, ItemInfo* item);

    // ItemManager::BuyItem @ 0x00112498 (struct-return, ptr passed via r0)
    // Marks item as purchased in FruitSaveData and plays purchase SFX.
    // Stub: no-op.
    void BuyItem(ItemInfo* item);

    // ItemManager::GetNumNewItems @ 0x00112048
    // Returns count of items that are newly available (unlocked but not seen).
    // Stub: 0.
    int GetNumNewItems() const;

    // ItemManager::AreNewItems @ 0x0011200c
    // Returns non-zero if any item is newly available.
    // Stub: 0.
    int AreNewItems() const;

    // Access the equipped item slot for a given ItemType.
    // Binary: *(s_equippedItems + type*4).
    ItemInfo* GetEquipped(int type) const;

    // --- Not in binary's public interface but needed by ShopScreen ---
    // Returns total item count. Stub: 0.
    int GetNumItems() const;

    // Returns item at index. Stub: nullptr.
    ItemInfo* GetItem(int index) const;

private:
    ItemManager() {}

    // +0x00..+0x0f: 4-slot equipped array (one per ItemType, 0-3)
    ItemInfo* m_equippedItems[4];

    // Singleton instance
    static ItemManager s_instance;
};

#endif // FN_ITEM_MANAGER_H
