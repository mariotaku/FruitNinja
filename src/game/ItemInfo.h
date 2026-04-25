#ifndef FN_ITEM_INFO_H
#define FN_ITEM_INFO_H

//
// ItemInfo — describes a single unlockable shop item (blade skin, background, etc.)
// Binary address: ctor 0x001138ac, size ~0x40 bytes.
//
// Field layout (from ItemInfo::ItemInfo ctor, ShopListItem::Create, ShopScreen usage):
//   +0x00  vtable ptr
//   +0x04  char*        m_pID          internal XML id string
//   +0x08  char*        m_pName        display name (localised)
//   +0x0c  void*        vtable entry (SetEquipped ptr used at +0x0c in SetEquippedItem)
//   +0x10  char (ItemType) m_Type      item category: 0=blade,1=background,2=???,3=???
//   +0x14  int          m_Price        price in currency units (0 = free/default)
//   +0x18  char*        m_pNameStr     unlocked display string (shown when owned)
//   +0x1c  char*        m_pLockedStr   locked/price display string
//   +0x20  char*        m_pFmtStr      format string for progress text (NULL = none)
//   +0x24  float        m_WidthScale
//   +0x28  float        m_HeightScale
//   +0x2c  int          m_SortOrder
//   +0x30  char*        m_pTextureName  texture asset path (used by Create for thumbnail)
//   +0x34  Colour       m_Colour1
//   +0x38  Colour       m_Colour2
//   +0x3c  char         m_bEquipped    1 when this item is currently equipped
//
// Port status: STUB — public API stubs only. Bodies are no-ops / safe defaults.
// The full implementation requires parsing items.xml and persisting to FruitSaveData.
//
// Analysed: 2026-04-25T00:00
//

#include <cstdint>

// ItemType matches the m_Type byte used by ItemManager (0..3 seen in binary).
// Value 0=blade, 1=background; 2 and 3 appear in SetEquippedItem but are unknown.
enum ItemType {
    ITEM_TYPE_BLADE      = 0,
    ITEM_TYPE_BACKGROUND = 1,
    ITEM_TYPE_UNKNOWN2   = 2,
    ITEM_TYPE_UNKNOWN3   = 3,
};

struct Colour;

class ItemInfo {
public:
    // +0x04 internal id string
    const char* m_pID;
    // +0x08
    const char* m_pName;
    // +0x0c  (vtable slot in binary used by SetEquippedItem to dispatch)
    void* m_pfnSetEquipped;   // set to nullptr in stub
    // +0x10  item category
    char m_Type;              // cast to ItemType
    // +0x14
    int m_Price;
    // +0x18
    const char* m_pNameStr;
    // +0x1c
    const char* m_pLockedStr;
    // +0x20
    const char* m_pFmtStr;
    // +0x24
    float m_WidthScale;
    // +0x28
    float m_HeightScale;
    // +0x2c
    int m_SortOrder;
    // +0x30
    const char* m_pTextureName;
    // +0x34..+0x38 colours (Colour = 4 bytes each)
    uint32_t m_Colour1;
    uint32_t m_Colour2;
    // +0x3c
    char m_bEquipped;

    ItemInfo()
        : m_pID(nullptr)
        , m_pName(nullptr)
        , m_pfnSetEquipped(nullptr)
        , m_Type(0)
        , m_Price(0)
        , m_pNameStr(nullptr)
        , m_pLockedStr(nullptr)
        , m_pFmtStr(nullptr)
        , m_WidthScale(1.0f)
        , m_HeightScale(1.0f)
        , m_SortOrder(0)
        , m_pTextureName(nullptr)
        , m_Colour1(0xFFFFFFFF)
        , m_Colour2(0xFFFFFFFF)
        , m_bEquipped(1)
    {}

    // ItemInfo::IsLocked @ 0x0015fa6c (ShopScreen uses this to gate buy/equip UI)
    // Returns non-zero when this item has not yet been purchased.
    // TODO: implement against FruitSaveData persistence. Stub: always unlocked.
    int IsLocked() const { return 0; }
};

#endif // FN_ITEM_INFO_H
