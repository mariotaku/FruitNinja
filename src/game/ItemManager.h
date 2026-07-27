#ifndef FN_ITEM_MANAGER_H
#define FN_ITEM_MANAGER_H

// Analysed: 2026-04-25T10:30
//
// ItemManager — singleton that owns all shop items.
// v1.6.1 ItemManager ctor @ 0x00138568; GetInstance @ 0x00139534; LoadItemData @ 0x00139d68.
//
// Struct layout (0x94 bytes):
//   +0x00..+0x0f  ItemInfo*               m_DefaultItems[4]   equipped per type
//   +0x10         std::vector<ItemInfo*>  m_Items              all items in XML order
//   +0x1c         std::map<uint32,ItemInfo*> m_ByHash          all items keyed by hash
//   +0x34         std::map<uint32,ItemInfo*> m_ByHashType[0]   SLASH_MODIFIER
//   +0x4c         std::map<uint32,ItemInfo*> m_ByHashType[1]   BACKGROUND
//   +0x64         std::map<uint32,ItemInfo*> m_ByHashType[2]   UPSELL
//   +0x7c         std::map<uint32,ItemInfo*> m_ByHashType[3]   REMOVEADS
//
// ASM-spec v1.6.1 ItemManager @0x00138568: storage faithful (std::vector<ItemInfo*> +0x10
//   + 5x std::map; size 0x94; .bss singleton)
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
    // GetInstance @ v1.6.1 0x00139534 — C++ static-local singleton
    static ItemManager* GetInstance();

    // EquippedSlashModCount @ .bss 0x0022ece4 — count of active SlashModifiers
    // that have called ApplyModifier. Decremented by RemoveModifier; when it
    // hits 0 the default blade is restored via SetEquippedItem(0, default).
    static int EquippedSlashModCount;

    // LoadItemData @ v1.6.1 0x00139d68 — parse itemlist.xml, load ItemSave.xml
    void LoadItemData();

    // UnLoadItemData @ v1.6.1 0x00138a64 — frees all ItemInfo objects; called from GameDestroy
    void UnLoadItemData();

    // UnlockItem @ v1.6.1 0x0013842c — achievement-driven unlock by hash
    bool UnlockItem(uint32_t hash);

    // IsEquipped @ 0x0015fa6c — returns 1 if item == m_DefaultItems[item->m_Type]
    int IsEquipped(ItemInfo* item);

    // SetEquippedItem @ v1.6.1 0x00139b1c — sets slot + side effects
    // ASM-spec v1.6.1 ItemManager::SetEquippedItem @0x00139b1c: 1st param is ItemType (enum), not int.
    void SetEquippedItem(ItemType type, ItemInfo* item);

    // BuyItem @ v1.6.1 0x001389c4 — deducts coins, marks item purchased
    // Returns 1 on success, 0 if item not found or insufficient coins
    int BuyItem(uint32_t hash);

    // GetNumNewItems @ v1.6.1 0x00138380
    int GetNumNewItems();

    // AreNewItems @ v1.6.1 0x00138320
    bool AreNewItems();

    // GetItem @ 0x00112084 — lookup by hash in m_ByHash
    ItemInfo* GetItem(uint32_t hash);

    // GetItemSavePath @ v1.6.1 0x001382e0 — returns "ItemSave.xml"
    const char* GetItemSavePath() const;

    // SaveItemInfo @ v1.6.1 0x00138610 — write ItemSave.xml
    void SaveItemInfo();

    // GetFirst / GetNext @ v1.6.1 ItemManager::GetFirst @0x001b6bc8 / GetNext @0x001b6c08
    // — iteration over m_Items via vector iterator (caller owns the iterator storage).
    ItemInfo* GetFirst(std::vector<ItemInfo*>::iterator& it);
    ItemInfo* GetNext(std::vector<ItemInfo*>::iterator& it);

    // Access equipped item slot (helper for ShopScreen)
    ItemInfo* GetEquipped(int type) const;

    // +0x00..+0x0f: default (equipped) item per type (binary: m_DefaultItems[4])
    // [0]=BLADE [1]=BACKGROUND [2]=UPSELL [3]=REMOVEADS (always null)
    // ASM-spec v1.6.1 ItemManager @0x00138568: storage faithful (std::vector<ItemInfo*> +0x10
    //   + 5x std::map; size 0x94; .bss singleton)
    ItemInfo* m_DefaultItems[4];

    // +0x10: all items in XML order
    std::vector<ItemInfo*> m_Items;

    // +0x1c: all items keyed by m_Hash
    std::map<uint32_t, ItemInfo*> m_ByHash;

    // +0x34..+0x7c: per-type maps (4 types x 0x18 bytes each)
    std::map<uint32_t, ItemInfo*> m_ByHashType[4];

private:
    // Binary ctor @ v1.6.1 0x00138568
    ItemManager();

    // Binary dtor @ v1.6.1 0x001383e0 / 0x00138428
    ~ItemManager();

public:
    // ---- Additional ItemManager public API (binary missing-symbol set) ----
    // EquipItem @ 0x00103198 — equip item by hash; returns 1 on success.
    int EquipItem(unsigned long hash);
    // PlayAlternateComboSound @ v1.6.1 0x00139ad0 — plays combo sound from equipped blade mod.
    // Returns true if an alternate sound was played (suppresses default combo SFX).
    bool PlayAlternateComboSound(int);
    // PlayAlternateImpactSound @ v1.6.1 0x00139aec — delegates to m_pCurrentSlashMod->m_ImpactSounds.PlaySound.
    // Returns true iff an alternate sound played (suppresses per-fruit impact SFX iteration).
    // ASM-verified: 2026-05-23 v1.6.1 ItemManager::PlayAlternateImpactSound @ 0x00139aec (re-analyst)
    bool PlayAlternateImpactSound(float volume, float pitch);
    // PlayAlternateSwipeSound @ v1.6.1 0x00139b04 — delegates to m_DefaultItems[0]->m_SwipeSounds.PlaySound.
    // Returns PlaySound's m_bPlayOntop result: true suppresses the default Sword-swipe SFX;
    // false (no blade mod, no swipe sounds, or play_ontop="true") lets it play.
    bool PlayAlternateSwipeSound(float volume, float pitch);
    // SetSwipeLoodVol @ v1.6.1 0x0013830c — set looping-swipe desired volume on the
    // equipped blade mod: if m_DefaultItems[0], call its
    // m_LoopingSound.SetLoopDesiredVol(vol).
    void SetSwipeLoodVol(float);
    // UnequipItem @ 0x0010314c — unequip item by hash; returns true if found.
    bool UnequipItem(unsigned long hash);
    // Update @ v1.6.1 0x00139a34 — per-frame update of equipped blade mod sounds:
    // if m_DefaultItems[0], call SlashModInfo::UpdateSounds(dt).
    void Update(float);
    // ---- end additional API ----
};

#endif // FN_ITEM_MANAGER_H
