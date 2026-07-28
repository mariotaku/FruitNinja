// Analysed: 2026-04-25T10:30
//
// ItemInfo + SlashModInfo — method implementations.
// Binary: ItemInfo::ctor 0x0013a714 (thunk 0x001145a0), ItemInfo::Parse 0x0013907c,
//         ItemInfo in-place dtor 0x0013adb8, deleting dtor 0x0013b098,
//         SlashModInfo::ctor 0x0013ae78, ParseSlashModInfo 0x00138d00,
//         SlashSoundMods::Parse 0x00138b0c, LoopingSound::Parse 0x00138ad0.

#include "ItemInfo.h"
#include "ItemParseUtil.h"
#include "GameWork.h"
#include "engine/util/StringHash.h"
#include "engine/util/StringTable.h"
#include "engine/audio/GameSound.h"
#include "engine/math/Random.h"
#include "entities/SlashEntity.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ParseItemType -- maps XML "type" attribute string to ItemType int.
// Binary: _Z13ParseItemTypePKc v1.6.1
int ParseItemType(const char* str) {
    if (str == nullptr) return 0;
    if (strcmp(str, "SLASH_MODIFIER") == 0) return 0;
    if (strcmp(str, "BACKGROUND")     == 0) return 1;
    if (strcmp(str, "UPSELL")         == 0) return 2;
    if (strcmp(str, "REMOVEADS")      == 0) return 3;
    return 0;
}

// -----------------------------------------------------------------------
// ItemInfo
// -----------------------------------------------------------------------

// ASM-verified: 2026-06-20T00:00Z v1.6.1 ItemInfo::ItemInfo @ 0x0013a714 (asm-inspector)
// ItemInfo::ItemInfo ctor @ 0x0013a714 (thunk 0x001145a0)
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
    , m_bSeen(true)           // 1 = owned/seen; ctor default per binary
    , m_Scale(0.125f)         // +0x40  ctor default 0.125f (v1.6.1 ItemInfo::ItemInfo @0x0013a714)
    , m_IsNew(false)          // +0x44  ctor default 0
{
    _pad11[0] = _pad11[1] = _pad11[2] = 0;
    _pad25[0] = _pad25[1] = _pad25[2] = 0;
    _pad3d[0] = _pad3d[1] = _pad3d[2] = 0;
    _pad45[0] = _pad45[1] = _pad45[2] = 0;
}

// ItemInfo dtor @ 0x0013adb8 (in-place) / 0x0013b098 (delete)
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
bool ItemInfo::IsLocked() {
    return m_Cost > 0;
}

// ItemInfo::Parse @ 0x0013907c (v1.6.1)
void ItemInfo::Parse(TiXmlElement* e) {
    // --- Parse <requirements> child element (optional) ---
    TiXmlElement req = e->FirstChildElement("requirements");  // 0x1b9fc4
    if (req) {
        m_Cost = 1;  // default if present but no coins attr
        req.QueryIntAttribute("coins", &m_Cost);  // 0x1b9e68

        const char* descAttr = req.Attribute("description");  // 0x1b92d1
        CloneString(&m_pLockedText, descAttr);
        if (m_pLockedText == nullptr) {
            const char* text = GETSTRING_CAST_0_STR(req.GetText());
            CloneString(&m_pLockedText, text);
        }

        const char* progressAttr = req.Attribute("singular");  // 0x1b9fd1
        CloneString(&m_pProgressFmt, progressAttr);
        if (m_pProgressFmt != nullptr) {
            const char* localised = GETSTRING_CAST_0_STR(m_pProgressFmt);
            free(m_pProgressFmt);
            m_pProgressFmt = nullptr;
            CloneString(&m_pProgressFmt, localised);
        }

        const char* trueStr = "true";  // 0x1b9ea0
        const char* upsideDown = req.Attribute("showIfUpsideDown");  // 0x1b9fda
        if (CompareWords(trueStr, upsideDown) != 0) {
            m_RequirementType = 1;
        } else {
            const char* playedToday = req.Attribute("showIfPlayedToday");  // 0x1b9feb
            if (CompareWords(trueStr, playedToday) != 0) {
                m_RequirementType = 2;
            } else {
                const char* joinButtons = req.Attribute("showJoinButtons");  // 0x1b9ffd
                if (CompareWords(trueStr, joinButtons) != 0) {
                    m_RequirementType = 3;
                }
            }
        }

        req.QueryIntAttribute("countDownFrom", &m_CountDownFrom);  // 0x1ba00d

        const char* totalAttr = req.Attribute("total");  // 0x1bd00d
        CloneString(&m_pTotalStatKey, totalAttr);
    }

    const char* nameAttr = e->Attribute("name");  // 0x1c3173
    CloneString(&m_pName, nameAttr);
    m_Hash = StringHash(m_pName);

    const char* titleAttr = e->Attribute("title");  // 0x1ba01b
    const char* titleStr = GETSTRING_CAST_0_STR(titleAttr);
    CloneString(&m_pTitle, titleStr);

    TiXmlElement desc = e->FirstChildElement("description");  // 0x1b92d1
    if (desc) {
        const char* descText = GETSTRING_CAST_0_STR(desc.GetText());
        CloneString(&m_pDescText, descText);
    }

    const char* texAttr = e->Attribute("texture");  // 0x1b92e8
    CloneString(&m_pTextureName, texAttr);

    const char* colourAttr = e->Attribute("colour");  // 0x1b9f98
    ParseColour(m_Colour1, colourAttr);

    // m_Colour2: vestigial attr "titleolour" (0x1ba021 -- typo/mangled in binary string table)
    ParseColour(m_Colour2, e->Attribute("titleolour"));  // 0x1ba021
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

// SlashSoundMods::Parse @ 0x00138b0c
void SlashSoundMods::Parse(TiXmlElement* elem) {
    if (!elem) return;

    // Count <sound> children
    int count = 0;
    TiXmlElement snd = elem->FirstChildElement("sound");
    while (snd) {
        count++;
        snd = snd.NextSiblingElement("sound");
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
        while (snd) {
            const char* text = snd.GetText();
            m_SoundNames[idx] = (text != nullptr) ? strdup(text) : strdup("");

            float vol = defaultVol;
            snd.QueryFloatAttribute("vol", &vol);
            m_SoundVolumes[idx] = vol;

            idx++;
            snd = snd.NextSiblingElement("sound");
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

// ASM-spec v1.6.1 SlashSoundMods::Update @ 0x00139988:
//  - m_TimeUntilNextSound is a fractional QUEUE DEPTH, not a timer.
//  - if <= 0 return; n = t - dt / m_TimePerSound; clamp n to 0;
//    store; if (int)t != (int)n -> PlaySoundIdx(GetNextSound()) (releases one queued sound).
// The division is safe unguarded exactly as in the binary: PlaySound only ever seeds
// m_TimeUntilNextSound when m_TimePerSound > 0, and Reset()/the ctor leave it at 0, so
// the early-out fires before the vdiv whenever m_TimePerSound is 0.
void SlashSoundMods::Update(float dt) {
    float t = m_TimeUntilNextSound;
    if (t <= 0.0f) return;

    float n = t - dt / m_TimePerSound;
    if (n <= 0.0f) n = 0.0f;
    m_TimeUntilNextSound = n;

    if ((int)t == (int)n) return;

    PlaySoundIdx(GetNextSound());
}

// SlashSoundMods::GetNextSound @ 0x00139630
// ASM-verified: 2026-05-20 v1.6.1 SlashSoundMods::GetNextSound @ 0x00139630 (asm-inspector)
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

// SlashSoundMods::PlaySoundIdx @ v1.6.1 0x001398b0
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

// SlashSoundMods::PlaySound @ v1.6.1 0x00139a44 -- returns the m_bPlayOntop byte
// (+0x20) whenever m_SoundCount != 0, and 0 when the set is empty. Callers
// (PlayAlternateSwipeSound @0x00139b04 / ImpactSound @0x00139aec) forward the
// result verbatim as their "suppress the default SFX" flag.
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
                m_TimeUntilNextSound = 0.999f;  // DAT_00113038 = 0x3F7FBE77
            }
        }
    }
    return m_bPlayOntop != 0;
}

// -----------------------------------------------------------------------
// LoopingSound
// -----------------------------------------------------------------------

// NOTE: no shipped itemlist.xml/itemlistnfc.xml declares loop="", so m_pLoopName is
// always NULL in v1.6.1 and this path is inert -- layout fidelity only.

LoopingSound::LoopingSound()
    : m_DesiredVol(0.0f)
    , m_CurrentVol(0.0f)
    , m_pSound(nullptr)
    , m_pLoopName(nullptr)
{
}

// LoopingSound::Parse @ 0x00138ad0
void LoopingSound::Parse(TiXmlElement* elem) {
    if (elem) {
        CloneString(&m_pLoopName, elem->Attribute("loop"));
    }
}

// ASM-spec v1.6.1 LoopingSound::Reset @ 0x001388e8 — called from
// SlashModInfo::UnEquip @ 0x0013893c. Both volumes go to zero and a live handle is
// released; m_pLoopName is deliberately left intact so a re-equip can loop again.
void LoopingSound::Reset() {
    m_DesiredVol = 0.0f;
    m_CurrentVol = 0.0f;
    if (m_pSound != nullptr) {
        game_work.mGameSound->Release(m_pSound, m_pLoopName);
        m_pSound = nullptr;
    }
}

// ASM-spec v1.6.1 SlashModInfo::LoopingSound::Update @ 0x0013975c:
//  - m_CurrentVol steps toward m_DesiredVol by dt * 2.5 per second (0x40200000 up,
//    0xc0200000 down) and is clamped at the target so it never overshoots; already
//    equal snaps outright. Stored back @ 0x001397c4.
//  - a positive volume with no live handle starts the loop @ 0x001397d4, and any live
//    handle is retracked through MortarSound::SetVolume @ 0x0013986c.
//  - a zero volume with a live handle releases it @ 0x00139890.
// The binary has no early-out and no NULL check on m_pLoopName here -- SFXPlay is
// called with whatever the name is. Matched deliberately.
void LoopingSound::Update(float dt) {
    float cur = m_CurrentVol;
    float tgt = m_DesiredVol;
    if (cur > tgt) {
        cur += dt * -2.5f;
        if (tgt > cur) cur = tgt;
    } else if (cur < tgt) {
        cur += dt * 2.5f;
        if (tgt < cur) cur = tgt;
    } else {
        cur = tgt;
    }
    m_CurrentVol = cur;

    if (cur > 0.0f) {
        if (m_pSound == nullptr) {
            Mortar::Delegate1<bool, Mortar::MortarSound*> empty;
            m_pSound = game_work.mGameSound->SFXPlay(m_pLoopName, 1.0f, 1.0f, empty, 0.0f);
        }
        if (m_pSound != nullptr) {
            m_pSound->SetVolume(m_CurrentVol);
        }
    } else if (m_pSound != nullptr) {
        game_work.mGameSound->Release(m_pSound, m_pLoopName);
        m_pSound = nullptr;
    }
}

// ASM-spec v1.6.1 LoopingSound::SetLoopDesiredVol @ 0x001382fc — the guard reads
// +0x0c (m_pLoopName) and the store lands on +0x00, so a mod with no loop name can
// never be given a non-zero target. Called from ItemManager::SetSwipeLoodVol.
void LoopingSound::SetLoopDesiredVol(float vol) {
    if (m_pLoopName != nullptr) {
        m_DesiredVol = vol;
    }
}

// External fn defined in SlashModifier.cpp; v1.6.1 ParseSlashModColourType @ 0x001e62e4.
extern int ParseSlashModColourType(const char* str);

// -----------------------------------------------------------------------
// SlashModInfo
// -----------------------------------------------------------------------

// ASM-verified: 2026-06-20T00:00Z v1.6.1 SlashModInfo::SlashModInfo @ 0x0013ae78 (asm-inspector)
// SlashModInfo ctor @ 0x0013ae78
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
    , m_ScaleStartThickness(1.0f)   // +0x6c  default 1.0f
    , m_ScaleEndThickness(0.0f)     // +0x70  default 0.0f
    , m_ScaleLength(1.0f)           // +0x74  default 1.0f
    , m_ScalePointScale(1.0f)       // +0x78  default 1.0f
    , m_bSlashFlash(false)          // +0x7c
    , m_bFlipForUpsideDown(false)   // +0x7d
    , m_ScaleUVLength(0.0f)         // +0x80  default 0.0f
    , m_SwipeSounds()
    , m_ImpactSounds()
    , m_ComboSounds()
    , m_LoopingSound()
{
    _pad51[0] = _pad51[1] = _pad51[2] = 0;
    _pad76[0] = _pad76[1] = 0;
}

// SlashModInfo dtor @ v1.6.1 0x00113ddc / 0x00113f24
// Binary frees m_pColours via operator_delete(m_pColours - 8) because the
// allocation header (element_size=4, count) sits at the allocation start,
// with m_pColours pointing 8 bytes past it.
SlashModInfo::~SlashModInfo() {
    // BC: binary frees with operator_delete((void*)(m_pColours - 8)), sets to NULL.
    // Use free() which corresponds to operator_delete under the port's toolchain.
    if (m_pColours != nullptr) {
        free((char*)m_pColours - 8);
        m_pColours = nullptr;
    }
    free(m_pTextureName2);
    free(m_pParticlePath);
    free(m_pContactParticle);
    free(m_pReleaseParticle);
}

// SlashModInfo::UnEquip @ 0x0013893c
void SlashModInfo::UnEquip() {
    m_LoopingSound.Reset();
}

// SlashModInfo::SetEquipped @ 0x00138944 (vtable slot +0x0c)
void SlashModInfo::SetEquipped() {
    SlashEntity::SetModColours(
        m_pColours,               // +0x48
        m_ColourCount,            // +0x4c
        m_ColourType,             // +0x50
        m_Speed,                  // +0x54
        m_pParticlePath,          // +0x5c -- trail emitter name
        m_pTextureName2,          // +0x60 -- blade overlay texture
        m_bDirectionalParticles,  // +0x58
        m_pContactParticle,       // +0x64
        m_pReleaseParticle        // +0x68
    );
    SlashEntity::SetModScales(
        m_ScaleLength,           // +0x74  length      -> g_Scale3
        m_ScaleStartThickness,   // +0x6c  thickness   -> g_Scale1
        m_ScaleEndThickness,     // +0x70  endThick    -> g_Scale2
        m_ScalePointScale,       // +0x78  pointScale  -> g_Scale4
        m_bFlipForUpsideDown,    // +0x7d  flipUD      -> g_ScaleFlag1
        m_bSlashFlash,           // +0x7c  loop        -> g_ScaleFlag2
        m_ScaleUVLength          // +0x80  uvNormalLen -> g_Scale5
    );
    m_SwipeSounds.Reset();   // +0x84
    m_ImpactSounds.Reset();  // +0xb0
    m_ComboSounds.Reset();   // +0xdc
}

// SlashModInfo::Parse @ 0x0013935c wraps the inner ParseSlashModInfo @ 0x00138d00.
// DIFFERS: binary ParseSlashModInfo does NOT call ItemInfo::Parse and reads
// slash-specific attributes directly from the root element (no child wrapper).
// Port adaptation keeps ItemInfo::Parse + <slashModInfo> child navigation
// because the asset XML schema nests slash attributes in a child element
// (v1.6.1 ParseSlashModInfo @0x00138d00).
void SlashModInfo::Parse(TiXmlElement* e) {
    ItemInfo::Parse(e);

    TiXmlElement smi = e->FirstChildElement("slashModInfo");
    if (smi) {
        const char* trueStr = "true";

        // `type` attr -> m_ColourType via ParseSlashModColourType
        const char* typeStr = smi.Attribute("type");
        m_ColourType = ParseSlashModColourType(typeStr);

        // DIFFERS: binary allocates 64-byte buffer + snprintf("%s.tex") for m_pTextureName2.
        // Port stores the raw attribute value directly because the asset XML may or may not
        // include ".tex" in the attribute value (v1.6.1 ParseSlashModInfo @0x00138d00).
        const char* tex2 = smi.Attribute("texture");
        CloneString(&m_pTextureName2, tex2);

        // `speed` attr (float); default 1.0f already set in ctor
        smi.QueryFloatAttribute("speed", &m_Speed);

        // `particles_directional` attr
        const char* dirPart = smi.Attribute("particles_directional");
        m_bDirectionalParticles = (CompareWords(trueStr, dirPart) != 0);

        // `particles` attr — stored verbatim; SetModColours looks up by StringHash
        const char* particles = smi.Attribute("particles");
        if (particles != nullptr && particles[0] != '\0') {
            free(m_pParticlePath);
            m_pParticlePath = strdup(particles);
        }

        // `contact_particles` attr
        const char* contactPart = smi.Attribute("contact_particles");
        CloneString(&m_pContactParticle, contactPart);

        // `release_particles` attr
        const char* releasePart = smi.Attribute("release_particles");
        CloneString(&m_pReleaseParticle, releasePart);

        // `slash_flash` attr
        const char* slashFlash = smi.Attribute("slash_flash");
        m_bSlashFlash = (CompareWords(trueStr, slashFlash) != 0);

        // `flipForUpsideDown` attr
        const char* flip = smi.Attribute("flipForUpsideDown");
        m_bFlipForUpsideDown = (CompareWords(trueStr, flip) != 0);

        // <scales> child element
        TiXmlElement scales = smi.FirstChildElement("scales");
        if (scales) {
            scales.QueryFloatAttribute("start_thickness", &m_ScaleStartThickness);
            scales.QueryFloatAttribute("end_thickness",   &m_ScaleEndThickness);
            scales.QueryFloatAttribute("length",          &m_ScaleLength);
            scales.QueryFloatAttribute("point_scale",     &m_ScalePointScale);
            scales.QueryFloatAttribute("UV_length",       &m_ScaleUVLength);
        }

        // <colour>R,G,B</colour> children -> m_pColours array.
        // Binary @ 0x001127ee-0x0011287a: counts <colour> children (storing to
        // m_ColourCount each iteration), then allocates (count+2)*4 bytes with
        // a header: [element_size=4, count], storing the data pointer at offset +8.
        int count = 0;
        TiXmlElement c = smi.FirstChildElement("colour");
        while (c) {
            count++;
            m_ColourCount = count;
            c = c.NextSiblingElement("colour");
        }
        count = m_ColourCount;
        if (count > 0) {
            // Allocate header + data: two ints (element_size, count) then Colour array
            int* raw = (int*)malloc((count + 2) * 4);
            raw[0] = 4;               // element size = sizeof(Colour)
            raw[1] = count;           // element count
            Colour* colourPtr = (Colour*)(raw + 2);
            m_pColours = colourPtr;

            c = smi.FirstChildElement("colour");
            while (c) {
                const char* cval = c.GetText();
                if (cval) ParseColour(*colourPtr, cval);
                colourPtr++;
                c = c.NextSiblingElement("colour");
            }
        }

        // Sound sections — binary calls SlashSoundMods::Parse for each child.
        TiXmlElement swipeElem = smi.FirstChildElement("swipeSounds");
        if (swipeElem) {
            m_SwipeSounds.Parse(&swipeElem);
            m_LoopingSound.Parse(&swipeElem);  // "loop" attr lives on the same element
        }

        TiXmlElement impactElem = smi.FirstChildElement("impactSounds");
        if (impactElem) {
            m_ImpactSounds.Parse(&impactElem);
        }

        TiXmlElement comboElem = smi.FirstChildElement("comboSounds");
        if (comboElem) {
            m_ComboSounds.Parse(&comboElem);
        }
    }

    // ASM-spec v1.6.1 SlashModInfo::Parse @0x0013935c: default 1x opaque-black
    // colour when no <colour> children were parsed (m_pColours still null).
    // Binary allocates the same (count+2)*4-byte header+data block as the
    // count>0 path above, with count=1, then default-constructs one Colour
    // (opaque black: b=0,g=0,r=0,a=255) into the data slot.
    if (m_pColours == nullptr) {
        int* raw = (int*)malloc(3 * 4);
        raw[0] = 4;  // element size = sizeof(Colour)
        raw[1] = 1;  // element count
        m_pColours = (Colour*)(raw + 2);
        *m_pColours = Colour();  // opaque black (0,0,0,255)
        m_ColourCount = 1;
    }
}

// ASM-spec v1.6.1 SlashModInfo::UpdateSounds @0x001399f0:
//  - m_SwipeSounds/+0x84, m_ImpactSounds/+0xb0, m_ComboSounds/+0xdc .Update(dt), then
//    m_LoopingSound/+0x108 .Update(dt) (tail call). No gating.
void SlashModInfo::UpdateSounds(float dt) {
    m_SwipeSounds.Update(dt);
    m_ImpactSounds.Update(dt);
    m_ComboSounds.Update(dt);
    m_LoopingSound.Update(dt);
}
