// Analysed: 2026-04-25T00:00
//
// ItemManager — stub implementation.
// All methods are no-ops or return safe defaults until items.xml parsing
// and FruitSaveData persistence are ported.
// Binary: ctor 0x001121d0, dtor 0x001120f0, IsEquipped 0x0015fa6c,
//         SetEquippedItem 0x0011307c, BuyItem 0x00112498.
//

#include "ItemManager.h"
#include <cstring>

// Singleton instance
ItemManager ItemManager::s_instance;

ItemManager* ItemManager::GetInstance() {
    return &s_instance;
}

int ItemManager::IsEquipped(ItemInfo* item) const {
    // TODO: implement — check m_equippedItems[item->m_Type] == item
    return 0;
}

void ItemManager::SetEquippedItem(int type, ItemInfo* item) {
    // TODO: implement — set slot, call ChangeBackground / SlashEntity apply
    (void)type;
    (void)item;
}

void ItemManager::BuyItem(ItemInfo* item) {
    // TODO: implement — mark purchased in FruitSaveData, play SFX
    (void)item;
}

int ItemManager::GetNumNewItems() const {
    return 0;
}

int ItemManager::AreNewItems() const {
    return 0;
}

ItemInfo* ItemManager::GetEquipped(int type) const {
    if (type < 0 || type >= 4) return nullptr;
    return m_equippedItems[type];
}

int ItemManager::GetNumItems() const {
    return 0;
}

ItemInfo* ItemManager::GetItem(int /*index*/) const {
    return nullptr;
}
