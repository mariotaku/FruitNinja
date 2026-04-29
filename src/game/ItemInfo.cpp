// Analysed: 2026-04-25T10:30
//
// ItemInfo + SlashModInfo — method implementations.
// Binary: ItemInfo::ctor 0x00113910, ItemInfo::Parse 0x0011293c,
//         SlashModInfo::ctor 0x00113d58, ParseSlashModInfo 0x001126c0.
// See docs/structs/items.md for full RE notes.

#include "ItemInfo.h"
#include "ItemParseUtil.h"
#include "engine/util/StringHash.h"
#include "entities/SlashEntity.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// -----------------------------------------------------------------------
// ItemInfo
// -----------------------------------------------------------------------

// ItemInfo::ItemInfo ctor @ 0x00113910
// Defaults per ctor analysis (items.md §ItemInfo ctor defaults).
ItemInfo::ItemInfo()
    : m_pName(nullptr)
    , m_Hash(0)
    , m_Cost(0)
    , m_Type((int8_t)0xFF)   // 0xFF = unset marker (binary ctor default)
    , m_pTitle(nullptr)
    , m_pDescText(nullptr)
    , m_pLockedText(nullptr)
    , m_pProgressFmt(nullptr)
    , m_RequirementType(0)
    , m_pTotalStatKey(nullptr)
    , m_CountDownFrom(0)
    , m_pTextureName(nullptr)
    , m_Colour1()             // default-constructed Colour()
    , m_Colour2(m_Colour1)    // copy of Colour1 default (binary: copy-init)
    , m_bSeen(true)           // 1 = starts "seen" (not new) per ctor
{
    _pad11[0] = _pad11[1] = _pad11[2] = 0;
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
    _pad3d[0] = _pad3d[1] = _pad3d[2] = 0;
}

// ItemInfo dtor @ 0x00113c70 (in-place) / 0x00113ea8 (delete)
ItemInfo::~ItemInfo() {
    free(m_pName);
    free(m_pTitle);
    free(m_pDescText);
    free(m_pLockedText);
    free(m_pProgressFmt);
    free(m_pTotalStatKey);
    free(m_pTextureName);
}

// ItemInfo::UnEquip @ 0x00113974 — no-op per binary (single bx lr)
void ItemInfo::UnEquip() {}

// ItemInfo::SetEquipped @ 0x00113978 — no-op per binary (single bx lr)
void ItemInfo::SetEquipped() {}

// ItemInfo::IsLocked @ 0x0015fa60
// return this->m_Cost > 0;  (cost == -1 means purchased; cost > 0 = locked)
bool ItemInfo::IsLocked() const {
    return m_Cost > 0;
}

// ItemInfo::Parse @ 0x0011293c
// Virtual; called for all item types. SlashModInfo overrides to also parse
// <slashModInfo> children.
void ItemInfo::Parse(tinyxml2::XMLElement* e) {
    // --- Parse <requirements> child element (optional) ---
    tinyxml2::XMLElement* req = e->FirstChildElement("requirements");  // 0x1b9fc4
    if (req != nullptr) {
        m_Cost = 1;  // default if present but no coins attr
        req->QueryIntAttribute("coins", &m_Cost);  // 0x1b9e68

        // "description" attr reads into m_pLockedText (binary 0x1b92d1).
        // NOTE: attr name in binary is "description" not "lockedText".
        const char* descAttr = req->Attribute("description");  // 0x1b92d1
        CloneString(&m_pLockedText, descAttr);
        if (m_pLockedText == nullptr) {
            // Fall back to element text content
            const char* text = GETSTRING_CAST_0_STR(req->GetText());
            CloneString(&m_pLockedText, text);
        }

        const char* progressAttr = req->Attribute("singular");  // 0x1b9fd1 -> m_pProgressFmt
        CloneString(&m_pProgressFmt, progressAttr);
        if (m_pProgressFmt != nullptr) {
            const char* localised = GETSTRING_CAST_0_STR(m_pProgressFmt);
            // CloneString re-allocates so we need to free and re-clone
            free(m_pProgressFmt);
            m_pProgressFmt = nullptr;
            CloneString(&m_pProgressFmt, localised);
        }

        // Requirement type flags (all use CompareWords(attr, "true"))
        const char* trueStr = "true";  // 0x1b9ea0
        const char* upsideDown = req->Attribute("showIfUpsideDown");  // 0x1b9fda
        if (CompareWords(trueStr, upsideDown) != 0) {
            m_RequirementType = 1;
        } else {
            const char* playedToday = req->Attribute("showIfPlayedToday");  // 0x1b9feb
            if (CompareWords(trueStr, playedToday) != 0) {
                m_RequirementType = 2;
            } else {
                const char* joinButtons = req->Attribute("showJoinButtons");  // 0x1b9ffd
                if (CompareWords(trueStr, joinButtons) != 0) {
                    m_RequirementType = 3;
                }
            }
        }

        req->QueryIntAttribute("countDownFrom", &m_CountDownFrom);  // 0x1ba00d

        const char* totalAttr = req->Attribute("total");  // 0x1bd00d
        CloneString(&m_pTotalStatKey, totalAttr);
    }

    // --- Always parse from the outer <item> element ---

    const char* nameAttr = e->Attribute("name");  // 0x1c3173
    CloneString(&m_pName, nameAttr);
    m_Hash = StringHash(m_pName);

    const char* titleAttr = e->Attribute("title");  // 0x1ba01b
    const char* titleStr = GETSTRING_CAST_0_STR(titleAttr);
    CloneString(&m_pTitle, titleStr);

    tinyxml2::XMLElement* desc = e->FirstChildElement("description");  // 0x1b92d1
    if (desc != nullptr) {
        const char* descText = GETSTRING_CAST_0_STR(desc->GetText());
        CloneString(&m_pDescText, descText);
    }

    const char* texAttr = e->Attribute("texture");  // 0x1b92e8
    CloneString(&m_pTextureName, texAttr);

    const char* colourAttr = e->Attribute("colour");  // 0x1b9f98
    ParseColour(&m_Colour1, colourAttr);

    // m_Colour2: vestigial attr "titleolour" (0x1ba021 — typo/mangled in binary
    // string table, appears unused in shipped XML)
    ParseColour(&m_Colour2, e->Attribute("titleolour"));  // 0x1ba021
}

// -----------------------------------------------------------------------
// SlashModInfo
// -----------------------------------------------------------------------

// SlashModInfo ctor @ 0x00113d58
// Calls ItemInfo ctor (base), then sets SlashModInfo-specific defaults.
// Binary: DAT_00113dd0 = 1.0f for m_ScaleStartThickness.
SlashModInfo::SlashModInfo()
    : ItemInfo()
    , m_pColours(nullptr)
    , m_ColourCount(0)
    , m_ColourType(0)
    , m_LifeScale(1.0f)
    , m_bDirectionalParticles(false)
    , m_pParticlePath(nullptr)   // +0x54 -- trail emitter (was m_pTextureName2 -- SWAPPED per spec)
    , m_pTextureName2(nullptr)   // +0x58 -- blade overlay texture (was m_pParticlePath -- SWAPPED per spec)
    , m_pContactParticle(nullptr)
    , m_pParticle2(nullptr)
    , m_ScaleEndThickness(0.0f)    // +0x64
    , m_ScaleLength(0.0f)          // +0x68
    , m_ScaleStartThickness(1.0f)  // +0x6c  DAT_00113dd0 = 1.0f
    , m_ScaleUVLength(1.0f)        // +0x70  default 1.0f per spec
    , m_bFlipForUpsideDown(false)
    , m_bLoop(false)
    , m_LoopUVLength(1.0f)   // +0x78  default 1.0f per spec
    , m_SwipeSounds()
    , m_ImpactSounds()
    , m_ComboSounds()
    , m_LoopingSound()
{
    _pad51[0] = _pad51[1] = _pad51[2] = 0;
    _pad76[0] = _pad76[1] = 0;
}

// SlashModInfo dtor
SlashModInfo::~SlashModInfo() {
    delete[] m_pColours;
    free(m_pTextureName2);
    free(m_pParticlePath);
    free(m_pContactParticle);
    free(m_pParticle2);
}

// SlashModInfo::UnEquip @ 0x00112424
// Binary: calls LoopingSound::Reset() on m_LoopingSound (+0x100).
// Called when a blade skin is de-equipped.
void SlashModInfo::UnEquip() {
    m_LoopingSound.Reset();  // LoopingSound::Reset @ 0x00112424 (via vtable/thunk)
}

// SlashModInfo::SetEquipped @ 0x00112430 (vtable slot +0x0c)
// Forwards all blade-skin fields to SlashEntity state.
// Calls SetModColours + SetModScales + 3x SlashSoundMods::Reset.
void SlashModInfo::SetEquipped() {
    // Binary call sequence @ 0x00112430:
    //   SetModColours(colours, colourCount, colourType, lifeScale,
    //                 m_pParticlePath@+0x54, m_pTextureName2@+0x58,
    //                 directional, contactParticle, particle2)
    //   SetModScales(startThick@+0x6c, endThick@+0x64, scaleLen@+0x68,
    //                uvLen@+0x70, flipUD@+0x74, loop@+0x75, loopUVLen@+0x78)
    //   m_SwipeSounds.Reset() @+0x7c
    //   m_ImpactSounds.Reset() @+0xa8
    //   m_ComboSounds.Reset() @+0xd4
    SlashEntity::SetModColours(
        m_pColours,               // +0x40
        m_ColourCount,            // +0x44
        m_ColourType,             // +0x48
        m_LifeScale,              // +0x4c
        m_pParticlePath,          // +0x54 -- trail emitter name (SetModColours param_5)
        m_pTextureName2,          // +0x58 -- blade overlay texture (SetModColours param_6)
        m_bDirectionalParticles,  // +0x50
        m_pContactParticle,       // +0x5c
        m_pParticle2              // +0x60
    );
    SlashEntity::SetModScales(
        m_ScaleStartThickness,   // +0x6c  param_1
        m_ScaleEndThickness,     // +0x64  param_2
        m_ScaleLength,           // +0x68  param_3
        m_ScaleUVLength,         // +0x70  param_4
        m_bFlipForUpsideDown,    // +0x74  param_5
        m_bLoop,                 // +0x75  param_6
        m_LoopUVLength           // +0x78  param_7
    );
    m_SwipeSounds.Reset();   // +0x7c
    m_ImpactSounds.Reset();  // +0xa8
    m_ComboSounds.Reset();   // +0xd4
}

// ParseSlashModInfo @ 0x001126c0
// Calls ItemInfo::Parse first, then parses <slashModInfo> child element.
// Many sub-attrs are stubbed (sound/particle paths) until those systems land.
void SlashModInfo::Parse(tinyxml2::XMLElement* e) {
    // Call base ItemInfo::Parse first (binary calls it)
    ItemInfo::Parse(e);

    tinyxml2::XMLElement* smi = e->FirstChildElement("slashModInfo");
    if (smi == nullptr) return;

    const char* trueStr = "true";  // 0x1b9ea0

    // `type` attr -> m_ColourType. Binary maps:
    //   "NONE"      -> 0 (static palette)
    //   "PER_SLASH" -> 1 (per-frame anim — UpdateModColour ticks each frame)
    //   "PER_SWIPE" -> 2 (random pick on each swipe; not used in shipped XML)
    const char* typeStr = smi->Attribute("type");
    if (typeStr) {
        if (CompareWords(typeStr, "PER_SLASH") != 0)      m_ColourType = 1;
        else if (CompareWords(typeStr, "PER_SWIPE") != 0) m_ColourType = 2;
        else                                               m_ColourType = 0;
    }

    // `texture` attr in <slashModInfo>
    const char* tex2 = smi->Attribute("texture");
    CloneString(&m_pTextureName2, tex2);

    // `life` attr (float)
    smi->QueryFloatAttribute("life", &m_LifeScale);

    // `particles_directional` attr
    const char* dirPart = smi->Attribute("particles_directional");
    m_bDirectionalParticles = (CompareWords(trueStr, dirPart) != 0);

    // `particles` attr -> heap "tex_%s" snprintf
    const char* particles = smi->Attribute("particles");
    if (particles != nullptr && particles[0] != '\0') {
        // Binary: snprintf("tex_%s", particles) into heap buffer
        char buf[256];
        snprintf(buf, sizeof(buf), "tex_%s", particles);
        free(m_pParticlePath);
        m_pParticlePath = strdup(buf);
    }

    // `contact_particles` attr
    const char* contactPart = smi->Attribute("contact_particles");
    CloneString(&m_pContactParticle, contactPart);

    // second particle (see ParseSlashModInfo — exact attr name TBD)
    // TODO: resolve exact attr name from binary string table when audio lands

    // `flipForUpsideDown` attr
    const char* flip = smi->Attribute("flipForUpsideDown");
    m_bFlipForUpsideDown = (CompareWords(trueStr, flip) != 0);

    // `loop` attr
    const char* loopAttr = smi->Attribute("loop");
    m_bLoop = (CompareWords(trueStr, loopAttr) != 0);

    // <scales> child element
    tinyxml2::XMLElement* scales = smi->FirstChildElement("scales");
    if (scales != nullptr) {
        scales->QueryFloatAttribute("start_thickness", &m_ScaleStartThickness);
        scales->QueryFloatAttribute("end_thickness",   &m_ScaleEndThickness);
        scales->QueryFloatAttribute("length",          &m_ScaleLength);
        scales->QueryFloatAttribute("UV_length",       &m_ScaleUVLength);
    }

    // <colour> children -> m_pColours array
    // Count first, then allocate
    int count = 0;
    for (tinyxml2::XMLElement* c = smi->FirstChildElement("colour");
         c != nullptr; c = c->NextSiblingElement("colour")) {
        count++;
    }
    m_ColourCount = count;
    if (count > 0) {
        delete[] m_pColours;
        m_pColours = new Colour[count];
        int idx = 0;
        for (tinyxml2::XMLElement* c = smi->FirstChildElement("colour");
             c != nullptr; c = c->NextSiblingElement("colour")) {
            const char* cval = c->Attribute("value");
            if (cval) ParseColour(&m_pColours[idx], cval);
            idx++;
        }
    }

    // Sound sections — stubbed: binary calls SlashSoundMods::Parse for each.
    // TODO: implement SlashSoundMods::Parse when audio is ported.
    // <swipeSounds>  -> m_SwipeSounds
    // <impactSounds> -> m_ImpactSounds
    // <comboSounds>  -> m_ComboSounds
    // <loop>         -> m_LoopingSound (via SlashSoundMods::Parse)
}
