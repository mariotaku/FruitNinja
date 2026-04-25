// Analysed: 2026-04-25T14:00
//
// ShopListItem stub implementation.
// Binary: ctor (0-param) 0x0015f9e8, ctor (5-param) 0x0015f734.
// Item rendering not yet ported.

#include "ShopListItem.h"
#include "game/ItemInfo.h"
#include <cstring>

ShopListItem::ShopListItem()
    : ScrollingMenuItem()
    , m_field25c(0.0f)
    , m_field260(0.0f)
    , m_Alpha(0.0f)
    , m_pItemInfo(nullptr)
    , m_fieldMenu(1)
    , m_field27d(0)
    , m_field27e(0)
    , _pad3(0)
    , m_field280(0.0f)
{
    // Matches binary ctor 0x0015f9e8:
    //   SmartPtr<Texture> ctor at +0x274   (SetNull)
    //   *(+0x264) = DAT_0015fa54   (float constant — not yet resolved; 0 assumed)
    //   *(+0x278) = 0              (ItemInfo* = null)
    //   SetNull(+0x274)
    //   *(+0x260) = *(+0x25c) = *(+0x280) = DAT_0015fa54
    //   *(+0x27e) = 0
    //   *(+0x27c) = 1
    //   *(+0x27d) = 0
    memset(_pad, 0, sizeof(_pad));
    memset(_pad2, 0, sizeof(_pad2));
    m_TexThumb.SetNull();
}

ShopListItem::~ShopListItem() {}
