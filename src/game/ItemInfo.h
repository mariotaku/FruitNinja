#ifndef FN_ITEM_INFO_H
#define FN_ITEM_INFO_H

// Analysed: 2026-04-25T10:30
//
// ItemInfo — base class for shop items (blade skins, backgrounds, upsells,
// remove-ads IAPs).  Binary vtable @ 0x001e8c50; ctor @ 0x00113910.
// Size: 0x40 bytes.
//
// Derived class SlashModInfo (0x110 bytes) extends for SLASH_MODIFIER items.
// Ctor @ 0x00113d58; ParseSlashModInfo @ 0x001126c0.
//
// See docs/structs/items.md for full field-offset table.
//

#include "engine/math/Colour.h"
#include "ItemParseUtil.h"
#include <cstdint>
#include <tinyxml2.h>

// ItemType — matches m_Type byte values documented in binary.
// 0=SLASH_MODIFIER, 1=BACKGROUND, 2=UPSELL, 3=REMOVEADS
enum ItemType {
    ITEM_TYPE_BLADE      = 0,    // SLASH_MODIFIER
    ITEM_TYPE_BACKGROUND = 1,
    ITEM_TYPE_UPSELL     = 2,
    ITEM_TYPE_REMOVEADS  = 3,
};

// ParseItemType — maps XML "type" attribute string to ItemType int.
// Binary: strcmp chain used in LoadItemData.
inline int ParseItemType(const char* str) {
    if (str == nullptr) return 0;
    if (strcmp(str, "SLASH_MODIFIER") == 0) return 0;
    if (strcmp(str, "BACKGROUND")     == 0) return 1;
    if (strcmp(str, "UPSELL")         == 0) return 2;
    if (strcmp(str, "REMOVEADS")      == 0) return 3;
    return 0;
}

// -----------------------------------------------------------------------
// ItemInfo (0x40 bytes)
// -----------------------------------------------------------------------
class ItemInfo {
public:
    // +0x04  char*      m_pName        internal XML `name` attr; hashed to m_Hash
    char*    m_pName;
    // +0x08  uint32_t   m_Hash         StringHash(m_pName) — map key
    uint32_t m_Hash;
    // +0x0c  int32_t    m_Cost         cost in coins; -1=purchased/free; 0=default
    int32_t  m_Cost;
    // +0x10  int8_t     m_Type         0xFF before parsed; 0=blade,1=bg,2=upsell,3=removeads
    int8_t   m_Type;
    // +0x11  (3 bytes padding)
    uint8_t  _pad11[3];
    // +0x14  char*      m_pTitle       localised display title (via GETSTRING_CAST_0_STR)
    char*    m_pTitle;
    // +0x18  char*      m_pDescText    localised description from <description> child text
    char*    m_pDescText;
    // +0x1c  char*      m_pLockedText  locked/cost display text
    char*    m_pLockedText;
    // +0x20  char*      m_pProgressFmt progress format string; NULL if no showIfPlayedToday
    char*    m_pProgressFmt;
    // +0x24  int8_t     m_RequirementType  0=none, 1=upsideDown, 2=playedToday, 3=joinButtons
    int8_t   m_RequirementType;
    // +0x25  (3 bytes padding)
    uint8_t  _pad25[3];
    // +0x28  char*      m_pTotalStatKey  achievement stat `total` attr (NULL if none)
    char*    m_pTotalStatKey;
    // +0x2c  int32_t    m_CountDownFrom  countDownFrom value (0 if absent)
    int32_t  m_CountDownFrom;
    // +0x30  char*      m_pTextureName   texture asset name; used for shop thumbnail
    char*    m_pTextureName;
    // +0x34  Colour     m_Colour1        parsed from `colour` attr
    Colour   m_Colour1;
    // +0x38  Colour     m_Colour2        second colour slot; init copy of Colour1
    Colour   m_Colour2;
    // +0x3c  bool       m_bSeen          1=seen in shop; 0="new item" badge visible
    bool     m_bSeen;
    // +0x3d  (3 bytes padding to 0x40)
    uint8_t  _pad3d[3];

    // --- vtable-equivalent virtuals (C++ vtable handles dispatch) -------

    // ctor @ 0x00113910 — sets defaults documented in items.md
    ItemInfo();

    // dtor (in-place) @ 0x00113c70; (delete) @ 0x00113ea8
    virtual ~ItemInfo();

    // vtable[+0x08] UnEquip @ 0x00113974 — no-op
    virtual void UnEquip();

    // vtable[+0x0c] SetEquipped @ 0x00113978 — no-op
    virtual void SetEquipped();

    // vtable[+0x10] Parse @ 0x0011293c — parse <item> TiXmlElement
    virtual void Parse(tinyxml2::XMLElement* e);

    // IsLocked @ 0x0015fa60 — return m_Cost > 0
    bool IsLocked() const;
};

// -----------------------------------------------------------------------
// SlashSoundMods (0x2c bytes) — stub; full layout not yet RE'd.
// Stores references to swipe/impact/combo sounds for a blade mod.
// Binary: parsed by SlashSoundMods::Parse (from <swipeSounds> etc.)
// TODO: flesh out when audio is fully ported.
// -----------------------------------------------------------------------
struct SlashSoundMods {
    // 0x2c opaque bytes matching binary size
    uint8_t _data[0x2c];

    SlashSoundMods() { for (int i = 0; i < 0x2c; i++) _data[i] = 0; }
};

// -----------------------------------------------------------------------
// LoopingSound (0x10 bytes) — stub; full layout not yet RE'd.
// Binary: part of SlashModInfo; parsed from <loop> child.
// TODO: flesh out when looping audio is fully ported.
// -----------------------------------------------------------------------
struct LoopingSound {
    uint8_t _data[0x10];

    LoopingSound() { for (int i = 0; i < 0x10; i++) _data[i] = 0; }
};

// -----------------------------------------------------------------------
// SlashModInfo : ItemInfo (0x110 bytes)
// Extends ItemInfo for SLASH_MODIFIER items.
// Binary vtable overrides Parse at slot +0x10 with ParseSlashModInfo.
// ctor @ 0x00113d58; parse @ 0x001126c0.
// -----------------------------------------------------------------------
class SlashModInfo : public ItemInfo {
public:
    // +0x40  Colour*    m_pColours          heap array of <colour> entries; NULL if count==0
    Colour*  m_pColours;
    // +0x44  int        m_ColourCount        number of <colour> child elements parsed
    int      m_ColourCount;
    // +0x48  int        m_ColourType         ParseSlashModColourType result (0=NONE, 1=PER_SLASH?)
    int      m_ColourType;
    // +0x4c  float      m_LifeScale          XML `life` attr (float)
    float    m_LifeScale;
    // +0x50  bool       m_bDirectionalParticles  `particles_directional` CompareWords "true"
    bool     m_bDirectionalParticles;
    // +0x51  (3 bytes padding)
    uint8_t  _pad51[3];
    // +0x54  char*      m_pTextureName2      `texture` in <slashModInfo> sub-element
    char*    m_pTextureName2;
    // +0x58  char*      m_pParticlePath      heap `"tex_%s"` snprintf from `particles` attr
    char*    m_pParticlePath;
    // +0x5c  char*      m_pContactParticle   `contact_particles` attr
    char*    m_pContactParticle;
    // +0x60  char*      m_pParticle2         second particle attr
    char*    m_pParticle2;
    // +0x64  (4 bytes padding / alignment gap per spec: 0x64..0x67)
    uint8_t  _pad64[4];
    // +0x68  float      m_ScaleEndThickness  <scales end_thickness=>
    float    m_ScaleEndThickness;
    // +0x6c  float      m_ScaleLength        <scales length=>
    float    m_ScaleLength;
    // +0x70  float      m_ScaleUVLength      <scales UV_length=> (default 1.0)
    float    m_ScaleUVLength;
    // +0x74  bool       m_bFlipForUpsideDown  `flipForUpsideDown` CompareWords "true"
    bool     m_bFlipForUpsideDown;
    // +0x75  bool       m_bLoop              `loop` attr from <slashModInfo> CompareWords "true"
    bool     m_bLoop;
    // +0x76  (2 bytes padding)
    uint8_t  _pad76[2];
    // +0x78  float      m_ScaleStartThickness  <scales start_thickness=> (default 1.0f @ DAT_00113dd0)
    float    m_ScaleStartThickness;
    // +0x7c  SlashSoundMods  m_SwipeSounds    from <swipeSounds>
    SlashSoundMods m_SwipeSounds;
    // +0xa8  SlashSoundMods  m_ImpactSounds   from <impactSounds>
    SlashSoundMods m_ImpactSounds;
    // +0xd4  SlashSoundMods  m_ComboSounds    from <comboSounds>
    SlashSoundMods m_ComboSounds;
    // +0x100 LoopingSound    m_LoopingSound   from <loop>
    LoopingSound   m_LoopingSound;

    SlashModInfo();
    virtual ~SlashModInfo() override;

    // vtable[+0x08] UnEquip override — no-op (binary: inherits base no-op)
    virtual void UnEquip() override;
    // vtable[+0x0c] SetEquipped override — no-op (binary: inherits base no-op)
    virtual void SetEquipped() override;
    // vtable[+0x10] Parse override — ParseSlashModInfo @ 0x001126c0
    virtual void Parse(tinyxml2::XMLElement* e) override;
};

#endif // FN_ITEM_INFO_H
