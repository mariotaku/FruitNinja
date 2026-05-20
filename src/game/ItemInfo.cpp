// Analysed: 2026-04-25T10:30
//
// ItemInfo + SlashModInfo — method implementations.
// Binary: ItemInfo::ctor 0x00113910, ItemInfo::Parse 0x0011293c,
//         SlashModInfo::ctor 0x00113d58, ParseSlashModInfo 0x001126c0,
//         SlashSoundMods::Parse 0x00112568, LoopingSound::Parse 0x0011253c.

#include "ItemInfo.h"
#include "ItemParseUtil.h"
#include "GameWork.h"
#include "engine/util/StringHash.h"
#include "engine/audio/GameSound.h"
#include "engine/math/Random.h"
#include "entities/SlashEntity.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// -----------------------------------------------------------------------
// ItemInfo
// -----------------------------------------------------------------------

// ItemInfo::ItemInfo ctor @ 0x00113910
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
bool ItemInfo::IsLocked() const {
    return m_Cost > 0;
}

// ItemInfo::Parse @ 0x0011293c
void ItemInfo::Parse(tinyxml2::XMLElement* e) {
    // --- Parse <requirements> child element (optional) ---
    tinyxml2::XMLElement* req = e->FirstChildElement("requirements");  // 0x1b9fc4
    if (req != nullptr) {
        m_Cost = 1;  // default if present but no coins attr
        req->QueryIntAttribute("coins", &m_Cost);  // 0x1b9e68

        const char* descAttr = req->Attribute("description");  // 0x1b92d1
        CloneString(&m_pLockedText, descAttr);
        if (m_pLockedText == nullptr) {
            const char* text = GETSTRING_CAST_0_STR(req->GetText());
            CloneString(&m_pLockedText, text);
        }

        const char* progressAttr = req->Attribute("singular");  // 0x1b9fd1
        CloneString(&m_pProgressFmt, progressAttr);
        if (m_pProgressFmt != nullptr) {
            const char* localised = GETSTRING_CAST_0_STR(m_pProgressFmt);
            free(m_pProgressFmt);
            m_pProgressFmt = nullptr;
            CloneString(&m_pProgressFmt, localised);
        }

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

    // m_Colour2: vestigial attr "titleolour" (0x1ba021 — typo/mangled in binary string table)
    ParseColour(&m_Colour2, e->Attribute("titleolour"));  // 0x1ba021
}

// -----------------------------------------------------------------------
// SlashSoundMods
// -----------------------------------------------------------------------

SlashSoundMods::SlashSoundMods()
    : m_SoundCount(0)
    , m_SoundNames(nullptr)
    , m_SoundVolumes(nullptr)
    , m_TimePerSound(0.0f)
    , m_PlaySequentialy(-1)
    , m_TimeUntilNextSound(0.0f)
    , m_LastVolume(0.0f)
    , m_LastPitch(0.0f)
    , m_bPlayOntop(1)
    , m_RecentRing(nullptr)
    , m_PreviousSoundsToAvoid(0)
{
    _pad21[0] = _pad21[1] = _pad21[2] = 0;
}

// SlashSoundMods::Parse @ 0x00112568
void SlashSoundMods::Parse(tinyxml2::XMLElement* elem) {
    if (elem == nullptr) return;

    // Count <sound> children
    int count = 0;
    tinyxml2::XMLElement* snd = elem->FirstChildElement("sound");
    while (snd != nullptr) {
        count++;
        snd = snd->NextSiblingElement("sound");
    }
    m_SoundCount = count;

    if (count > 0) {
        m_SoundNames   = (char**)malloc(count * sizeof(char*));
        m_SoundVolumes = (float*)malloc(count * sizeof(float));

        // element-level "vol" attr is the default for all sounds
        float defaultVol = 1.0f;
        elem->QueryFloatAttribute("vol", &defaultVol);

        int idx = 0;
        snd = elem->FirstChildElement("sound");
        while (snd != nullptr) {
            const char* text = snd->GetText();
            m_SoundNames[idx] = (text != nullptr) ? strdup(text) : strdup("");

            float vol = defaultVol;
            snd->QueryFloatAttribute("vol", &vol);
            m_SoundVolumes[idx] = vol;

            idx++;
            snd = snd->NextSiblingElement("sound");
        }
    }

    elem->QueryFloatAttribute("time_per_sound", &m_TimePerSound);

    const char* seqAttr = elem->Attribute("play_sequentialy");
    if (seqAttr != nullptr && strcmp(seqAttr, "true") == 0) {
        m_PlaySequentialy = 0;
    } else {
        m_PlaySequentialy = -1;
    }

    const char* ontopAttr = elem->Attribute("play_ontop");
    if (ontopAttr != nullptr && strcmp(ontopAttr, "true") == 0) {
        m_bPlayOntop = (uint8_t)(1 ^ 1);  // XOR with default 1 -> 0
    } else {
        m_bPlayOntop = 1;
    }

    int avoid = 0;
    elem->QueryIntAttribute("previous_sounds_to_avoid", &avoid);
    if (avoid > m_SoundCount - 1) avoid = m_SoundCount - 1;
    if (avoid < 0) avoid = 0;
    m_PreviousSoundsToAvoid = avoid;

    if (m_PreviousSoundsToAvoid > 0) {
        m_RecentRing = new int[m_PreviousSoundsToAvoid];
    }

    Reset();
}

// SlashSoundMods::Reset — called at end of Parse and from SlashModInfo::SetEquipped
void SlashSoundMods::Reset() {
    m_TimeUntilNextSound = 0.0f;
    m_LastVolume         = 0.0f;
    m_LastPitch          = 0.0f;
    if (m_RecentRing != nullptr) {
        for (int i = 0; i < m_PreviousSoundsToAvoid; i++) m_RecentRing[i] = -1;
    }
}

// SlashSoundMods::GetNextSound @ 0x00112cf0
// ASM-verified: 2026-05-20 binary @ 0x00112cf0 (asm-inspector)
int SlashSoundMods::GetNextSound() {
    if (!m_SoundNames || m_SoundCount <= 0) return -1;
    if (m_SoundCount == 1) return 0;
    if (m_PlaySequentialy >= 0) {
        int pick = m_PlaySequentialy % m_SoundCount;
        if (++m_PlaySequentialy >= m_SoundCount) m_PlaySequentialy = 0;
        return pick;
    }
    int pick = (int)Math::g_Random.Rand32((uint32_t)m_SoundCount);
    if (m_PreviousSoundsToAvoid <= 0) return pick;
    for (int i = 0; i < m_PreviousSoundsToAvoid; ++i) {
        if (m_RecentRing[i] == pick) pick = (int)Math::g_Random.Rand32((uint32_t)m_SoundCount);
    }
    for (int k = m_PreviousSoundsToAvoid - 1; k > 0; --k) {
        m_RecentRing[k] = m_RecentRing[k - 1];
    }
    m_RecentRing[0] = pick;
    return pick;
}

// SlashSoundMods::PlaySoundIdx @ 0x00112e94
void SlashSoundMods::PlaySoundIdx(int i) {
    if (i < 0) return;
    if (i > m_SoundCount - 1) i = m_SoundCount - 1;
    const char* name = m_SoundNames[i];
    float vol = m_SoundVolumes[i];
    GameSound* gs = game_work.mGameSound;
    Mortar::Delegate1<bool, Mortar::MortarSound*> empty;
    // NOTE: binary passes m_LastPitch in the vol position and per-sound vol in the pitch position
    gs->SFXPlay(name, m_LastPitch, vol, empty);
}

// SlashSoundMods::PlaySound @ 0x00112fd4
bool SlashSoundMods::PlaySound(int idx, float volume, float pitch) {
    if (m_SoundCount == 0) return false;
    m_LastPitch  = pitch;
    m_LastVolume = volume;
    if (idx >= 0) {
        PlaySoundIdx(idx);
    } else {
        if (m_TimeUntilNextSound > 0.0f) {
            m_TimeUntilNextSound += 1.0f;
        } else {
            int picked = GetNextSound();
            PlaySoundIdx(picked);
            if (picked >= 0 && m_TimePerSound > 0.0f) {
                m_TimeUntilNextSound = 0.99898f;  // DAT_00113038 = 0x3F7FBE77
            }
        }
    }
    return m_bPlayOntop != 0;
}

// -----------------------------------------------------------------------
// LoopingSound
// -----------------------------------------------------------------------

LoopingSound::LoopingSound()
    : m_SoundId(0)
    , m_Phase(0)
    , m_State(0)
    , m_pLoopName(nullptr)
{
}

// LoopingSound::Parse @ 0x0011253c
void LoopingSound::Parse(tinyxml2::XMLElement* elem) {
    if (elem != nullptr) {
        CloneString(&m_pLoopName, elem->Attribute("loop"));
    }
}

// LoopingSound::Reset — called from SlashModInfo::UnEquip @ 0x00112424
void LoopingSound::Reset() {
    // Runtime audio state reset; no-op until audio system is ported.
    m_SoundId = 0;
    m_Phase   = 0;
    m_State   = 0;
}

// -----------------------------------------------------------------------
// SlashModInfo
// -----------------------------------------------------------------------

// SlashModInfo ctor @ 0x00113d58
SlashModInfo::SlashModInfo()
    : ItemInfo()
    , m_pColours(nullptr)
    , m_ColourCount(0)
    , m_ColourType(0)
    , m_Speed(1.0f)
    , m_bDirectionalParticles(false)
    , m_pParticlePath(nullptr)
    , m_pTextureName2(nullptr)
    , m_pContactParticle(nullptr)
    , m_pReleaseParticle(nullptr)
    , m_ScaleStartThickness(1.0f)   // +0x64  default 1.0f
    , m_ScaleEndThickness(0.0f)     // +0x68  default 0.0f
    , m_ScaleLength(1.0f)           // +0x6c  default 1.0f
    , m_ScalePointScale(1.0f)       // +0x70  default 1.0f
    , m_bSlashFlash(false)          // +0x74
    , m_bFlipForUpsideDown(false)   // +0x75
    , m_ScaleUVLength(0.0f)         // +0x78  default 0.0f
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
    free(m_pReleaseParticle);
}

// SlashModInfo::UnEquip @ 0x00112424
void SlashModInfo::UnEquip() {
    m_LoopingSound.Reset();
}

// SlashModInfo::SetEquipped @ 0x00112430 (vtable slot +0x0c)
void SlashModInfo::SetEquipped() {
    SlashEntity::SetModColours(
        m_pColours,               // +0x40
        m_ColourCount,            // +0x44
        m_ColourType,             // +0x48
        m_Speed,                  // +0x4c
        m_pParticlePath,          // +0x54 -- trail emitter name
        m_pTextureName2,          // +0x58 -- blade overlay texture
        m_bDirectionalParticles,  // +0x50
        m_pContactParticle,       // +0x5c
        m_pReleaseParticle        // +0x60
    );
    SlashEntity::SetModScales(
        m_ScaleStartThickness,   // +0x64  param_1
        m_ScaleEndThickness,     // +0x68  param_2
        m_ScaleLength,           // +0x6c  param_3
        m_ScalePointScale,       // +0x70  param_4
        m_bFlipForUpsideDown,    // +0x75  param_5
        m_bSlashFlash,           // +0x74  param_6
        m_ScaleUVLength          // +0x78  param_7
    );
    m_SwipeSounds.Reset();   // +0x7c
    m_ImpactSounds.Reset();  // +0xa8
    m_ComboSounds.Reset();   // +0xd4
}

// ParseSlashModInfo @ 0x001126c0
void SlashModInfo::Parse(tinyxml2::XMLElement* e) {
    ItemInfo::Parse(e);

    tinyxml2::XMLElement* smi = e->FirstChildElement("slashModInfo");
    if (smi == nullptr) return;

    const char* trueStr = "true";

    // `type` attr -> m_ColourType
    const char* typeStr = smi->Attribute("type");
    if (typeStr) {
        if (CompareWords(typeStr, "PER_SLASH") != 0)      m_ColourType = 1;
        else if (CompareWords(typeStr, "PER_SWIPE") != 0) m_ColourType = 2;
        else                                               m_ColourType = 0;
    }

    // `texture` attr in <slashModInfo>
    const char* tex2 = smi->Attribute("texture");
    CloneString(&m_pTextureName2, tex2);

    // `speed` attr (float); default 1.0f already set in ctor
    smi->QueryFloatAttribute("speed", &m_Speed);

    // `particles_directional` attr
    const char* dirPart = smi->Attribute("particles_directional");
    m_bDirectionalParticles = (CompareWords(trueStr, dirPart) != 0);

    // `particles` attr — stored verbatim; SetModColours looks up by StringHash
    const char* particles = smi->Attribute("particles");
    if (particles != nullptr && particles[0] != '\0') {
        free(m_pParticlePath);
        m_pParticlePath = strdup(particles);
    }

    // `contact_particles` attr
    const char* contactPart = smi->Attribute("contact_particles");
    CloneString(&m_pContactParticle, contactPart);

    // `release_particles` attr
    const char* releasePart = smi->Attribute("release_particles");
    CloneString(&m_pReleaseParticle, releasePart);

    // `slash_flash` attr
    const char* slashFlash = smi->Attribute("slash_flash");
    m_bSlashFlash = (CompareWords(trueStr, slashFlash) != 0);

    // `flipForUpsideDown` attr
    const char* flip = smi->Attribute("flipForUpsideDown");
    m_bFlipForUpsideDown = (CompareWords(trueStr, flip) != 0);

    // <scales> child element
    tinyxml2::XMLElement* scales = smi->FirstChildElement("scales");
    if (scales != nullptr) {
        scales->QueryFloatAttribute("start_thickness", &m_ScaleStartThickness);
        scales->QueryFloatAttribute("end_thickness",   &m_ScaleEndThickness);
        scales->QueryFloatAttribute("length",          &m_ScaleLength);
        scales->QueryFloatAttribute("point_scale",     &m_ScalePointScale);
        scales->QueryFloatAttribute("UV_length",       &m_ScaleUVLength);
    }

    // <colour>R,G,B</colour> children -> m_pColours array.
    // The XML uses element TEXT, not a value attribute -- e.g.
    //   <colour>255,43,78</colour>     (disco palette entry)
    // not
    //   <colour value="255,43,78"/>
    int count = 0;
    tinyxml2::XMLElement* c = smi->FirstChildElement("colour");
    while (c != nullptr) {
        count++;
        c = c->NextSiblingElement("colour");
    }
    m_ColourCount = count;
    if (count > 0) {
        delete[] m_pColours;
        m_pColours = new Colour[count];
        int idx = 0;
        c = smi->FirstChildElement("colour");
        while (c != nullptr) {
            const char* cval = c->GetText();
            if (cval) ParseColour(&m_pColours[idx], cval);
            idx++;
            c = c->NextSiblingElement("colour");
        }
    }

    // Sound sections — binary calls SlashSoundMods::Parse for each child.
    tinyxml2::XMLElement* swipeElem = smi->FirstChildElement("swipeSounds");
    if (swipeElem != nullptr) {
        m_SwipeSounds.Parse(swipeElem);
        m_LoopingSound.Parse(swipeElem);  // "loop" attr lives on the same element
    }

    tinyxml2::XMLElement* impactElem = smi->FirstChildElement("impactSounds");
    if (impactElem != nullptr) {
        m_ImpactSounds.Parse(impactElem);
    }

    tinyxml2::XMLElement* comboElem = smi->FirstChildElement("comboSounds");
    if (comboElem != nullptr) {
        m_ComboSounds.Parse(comboElem);
    }
}
