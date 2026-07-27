#ifndef FN_ITEM_INFO_H
#define FN_ITEM_INFO_H

// Analysed: 2026-04-25T10:30
//
// ItemInfo — base class for shop items (blade skins, backgrounds, upsells,
// remove-ads IAPs).  Binary vtable @ 0x001e8c50; ctor @ 0x0013a714 (thunk 0x001145a0).
// In-place dtor @ 0x0013adb8; deleting dtor @ 0x0013b098.
// Size: 0x48 bytes (72 bytes).
//
// Derived class SlashModInfo (0x118 bytes) extends for SLASH_MODIFIER items.
// Ctor @ 0x0013ae78; ParseSlashModInfo @ 0x00138d00.
//

#include "engine/math/Colour.h"
#include "ItemParseUtil.h"
#include <cstdint>
#include "engine/xml/TiXmlElement.h"

namespace Mortar { class MortarSound; }

// ItemType — matches m_Type byte values documented in binary.
// 0=SLASH_MODIFIER, 1=BACKGROUND, 2=UPSELL, 3=REMOVEADS
enum ItemType {
    ITEM_TYPE_BLADE      = 0,    // SLASH_MODIFIER
    ITEM_TYPE_BACKGROUND = 1,
    ITEM_TYPE_UPSELL     = 2,
    ITEM_TYPE_REMOVEADS  = 3,
};

// ParseItemType -- maps XML "type" attribute string to ItemType int.
// Binary: _Z13ParseItemTypePKc
int ParseItemType(const char* str);

// -----------------------------------------------------------------------
// ItemInfo (0x48 bytes)
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
    // +0x3c  bool       m_bSeen          1=owned/seen; ctor default 1
    bool     m_bSeen;
    // +0x3d  (3 bytes padding)
    uint8_t  _pad3d[3];
    // +0x40  float      m_Scale          ctor default 0.125f (v1.6.1 ItemInfo::ItemInfo @0x0013a714)
    float    m_Scale;
    // +0x44  bool       m_IsNew          "new item" badge flag; ctor default 0; +3 pad -> sizeof 0x48
    bool     m_IsNew;
    // +0x45  (3 bytes padding to 0x48)
    uint8_t  _pad45[3];

    // --- vtable-equivalent virtuals (C++ vtable handles dispatch) -------

    // ctor @ 0x0013a714 (thunk 0x001145a0) — sets defaults per binary
    ItemInfo();

    // dtor (in-place) @ 0x0013adb8; (delete) @ 0x0013b098
    virtual ~ItemInfo();

    // vtable[+0x08] UnEquip @ 0x00113974 — no-op
    virtual void UnEquip();

    // vtable[+0x0c] SetEquipped @ 0x00113978 — no-op
    virtual void SetEquipped();

    // vtable[+0x10] Parse @ 0x0013907c (v1.6.1) — parse <item> TiXmlElement
    virtual void Parse(TiXmlElement* e);

    // IsLocked @ 0x0015fa60 — return m_Cost > 0
    bool IsLocked();
};

// -----------------------------------------------------------------------
// SlashSoundMods (0x2c bytes) — binary @ 0x00138b0c.
// Stores sound names/volumes for swipe/impact/combo events on a blade mod.
// -----------------------------------------------------------------------
struct SlashSoundMods {
    // +0x00  int32_t  m_SoundCount              number of <sound> children parsed
    int32_t  m_SoundCount;
    // +0x04  char**   m_SoundNames              heap array of strdup'd <sound> text
    char**   m_SoundNames;
    // +0x08  float*   m_SoundVolumes            heap array of per-sound vol (default 1.0f)
    float*   m_SoundVolumes;
    // +0x0c  float    m_TimePerSound            XML attr "time_per_sound", default 0.0f
    float    m_TimePerSound;
    // +0x10  int32_t  m_PlaySequentialy         "play_sequentialy"=="true" ? 0 : -1; default -1
    int32_t  m_PlaySequentialy;
    // +0x14  float    m_TimeUntilNextSound      fractional QUEUE DEPTH (not a timer).
    // PlaySound seeds it to 0.999f (integer part 0) after firing a sound, and each
    // further auto-pick request while it is > 0 adds 1.0f instead of playing. Update()
    // decays it by dt/m_TimePerSound and releases exactly one queued sound each time
    // the truncated integer part drops. 0 == nothing queued, free to play immediately.
    float    m_TimeUntilNextSound;
    // +0x18  float    m_LastVolume              stored by PlaySound for use by PlaySoundIdx
    float    m_LastVolume;
    // +0x1c  float    m_LastPitch               stored by PlaySound; passed as vol arg to SFXPlay
    float    m_LastPitch;
    // +0x20  uint8_t  m_bPlayOntop              "play_ontop"=="true" XOR'd with default 1; default 1
    uint8_t  m_bPlayOntop;
    // +0x21  (3 bytes padding)
    uint8_t  _pad21[3];
    // +0x24  int*     m_RecentRing              new int[m_PreviousSoundsToAvoid] scratch buffer
    int*     m_RecentRing;
    // +0x28  int32_t  m_PreviousSoundsToAvoid   XML attr "previous_sounds_to_avoid", clamped to m_SoundCount-1
    int32_t  m_PreviousSoundsToAvoid;

    SlashSoundMods();

    // Parse — mirrors binary @ 0x00138b0c
    void Parse(TiXmlElement* elem);

    // Reset — called from Parse and from SlashModInfo::SetEquipped
    void Reset();

    // Update @ v1.6.1 0x00139988 — per-frame decay of the m_TimeUntilNextSound queue
    // depth; releases one deferred sound each time the integer part crosses down.
    // Must be called every frame (via SlashModInfo::UpdateSounds <- ItemManager::Update)
    // or a mod with time_per_sound set plays exactly one sound and then stays queued
    // forever. Uses m_LastPitch / m_LastVolume captured by the suppressed PlaySound.
    void Update(float dt);

    // PlaySound @ v1.6.1 0x00139a44 — plays sound by idx (-1 = auto-pick); returns
    // m_bPlayOntop != 0 (false when m_SoundCount == 0 — empty set never suppresses).
    bool PlaySound(int idx, float volume, float pitch);

    // PlaySoundIdx @ v1.6.1 0x001398b0 — plays a specific sound slot via GameSound::SFXPlay
    void PlaySoundIdx(int i);

    // GetNextSound @ 0x0010a234 — sequential mode (m_PlaySequentialy >= 0)
    // or random with recent-ring avoidance. ASM-verified 2026-05-20.
    int GetNextSound();
};

// -----------------------------------------------------------------------
// LoopingSound (0x10 bytes) — binary ctor @ 0x0013a864, dtor @ 0x0013ac10.
// Tracks a looping ambient sound attached to the equipped blade mod: gameplay
// pushes a desired volume in via SetLoopDesiredVol, Update() chases the current
// volume toward it, and the live SFX handle exists only while that volume is
// above zero (started on the way up, released on the way down).
//
// NOTE: no shipped itemlist.xml/itemlistnfc.xml declares loop="", so m_pLoopName is
// always NULL in v1.6.1 and this path is inert -- layout fidelity only.
// -----------------------------------------------------------------------
struct LoopingSound {
    // +0x00  float    m_DesiredVol  target volume; only writer is SetLoopDesiredVol
    float    m_DesiredVol;
    // +0x04  float    m_CurrentVol  lerped toward m_DesiredVol at 2.5/sec; drives SetVolume
    float    m_CurrentVol;
    // +0x08  Mortar::MortarSound*  m_pSound  live SFXPlay handle; NULL when silent
    Mortar::MortarSound* m_pSound;
    // +0x0c  char*    m_pLoopName   CloneString of <swipeSounds loop="">; delete[]'d by dtor
    char*    m_pLoopName;

    LoopingSound();

    // Parse — mirrors binary @ 0x00138ad0
    // Reads "loop" attr from elem (the <swipeSounds> element).
    void Parse(TiXmlElement* elem);

    // Reset @ v1.6.1 0x001388e8 — called from SlashModInfo::UnEquip @ 0x0013893c.
    // Zeroes both volumes and releases any live handle. m_pLoopName is NOT touched,
    // so the mod can start looping again the next time it is equipped.
    void Reset();

    // Update @ v1.6.1 0x0013975c — chases m_CurrentVol toward m_DesiredVol at 2.5
    // units/sec (clamped at the target, so it never overshoots), then starts the loop
    // through GameSound::SFXPlay on the first frame the volume is positive, tracks it
    // with MortarSound::SetVolume, and releases it once the volume reaches zero.
    // Called unconditionally every frame from SlashModInfo::UpdateSounds — there is no
    // early-out and no NULL check on m_pLoopName, matching the binary.
    void Update(float dt);

    // SetLoopDesiredVol @ v1.6.1 0x001382fc — sets the target volume, but only for a
    // mod that actually declared a loop name. Called from ItemManager::SetSwipeLoodVol.
    void SetLoopDesiredVol(float vol);
};

// -----------------------------------------------------------------------
// SlashModInfo : ItemInfo (0x118 bytes)
// Extends ItemInfo for SLASH_MODIFIER items.
// Binary vtable overrides Parse at slot +0x10 with ParseSlashModInfo.
// ctor @ 0x0013ae78; outer parse @ 0x0013935c wraps inner parse @ 0x00138d00.
// -----------------------------------------------------------------------
class SlashModInfo : public ItemInfo {
public:
    // +0x48  Colour*    m_pColours          heap array of <colour> entries.
    // Parse() (v1.6.1 SlashModInfo::Parse @0x0013935c) guarantees this is never
    // left NULL: if no <colour> children were found, it falls back to a single
    // default-constructed (opaque black) Colour with m_ColourCount=1.
    Colour*  m_pColours;
    // +0x4c  int        m_ColourCount        number of <colour> child elements parsed
    int      m_ColourCount;
    // +0x50  int        m_ColourType         ParseSlashModColourType result (0=NONE, 1=PER_SLASH, 2=PER_SWIPE)
    int      m_ColourType;
    // +0x54  float      m_Speed              XML `speed` attr (float); default 1.0f
    float    m_Speed;
    // +0x58  bool       m_bDirectionalParticles  `particles_directional` CompareWords "true"
    bool     m_bDirectionalParticles;
    // +0x59  (3 bytes padding)
    uint8_t  _pad51[3];
    // +0x5c  char*      m_pParticlePath      XML `particles` verbatim (trail emitter key)
    char*    m_pParticlePath;
    // +0x60  char*      m_pTextureName2      `texture` in <slashModInfo>; blade overlay texture
    char*    m_pTextureName2;
    // +0x64  char*      m_pContactParticle   `contact_particles` attr; verbatim CloneString
    char*    m_pContactParticle;
    // +0x68  char*      m_pReleaseParticle   `release_particles` attr; verbatim CloneString
    char*    m_pReleaseParticle;
    // +0x6c  float      m_ScaleStartThickness  <scales start_thickness=>; SetModScales p2 (thickness); default 1.0f
    float    m_ScaleStartThickness;
    // +0x70  float      m_ScaleEndThickness    <scales end_thickness=>; SetModScales p3 (endThickness); default 0.0f
    float    m_ScaleEndThickness;
    // +0x74  float      m_ScaleLength          <scales length=>; SetModScales p1 (length); default 1.0f
    float    m_ScaleLength;
    // +0x78  float      m_ScalePointScale      <scales point_scale=>; SetModScales p4 (pointScale); default 1.0f
    float    m_ScalePointScale;
    // +0x7c  bool       m_bSlashFlash          `slash_flash` CompareWords "true"; SetModScales p6 (loop)
    bool     m_bSlashFlash;
    // +0x7d  bool       m_bFlipForUpsideDown   `flipForUpsideDown` CompareWords "true"; SetModScales p5 (flipUD)
    bool     m_bFlipForUpsideDown;
    // +0x7e  (2 bytes padding)
    uint8_t  _pad76[2];
    // +0x80  float      m_ScaleUVLength        <scales UV_length=>; SetModScales param_7; default 0.0f
    float    m_ScaleUVLength;
    // +0x84  SlashSoundMods  m_SwipeSounds    from <swipeSounds>
    SlashSoundMods m_SwipeSounds;
    // +0xb0  SlashSoundMods  m_ImpactSounds   from <impactSounds>
    SlashSoundMods m_ImpactSounds;
    // +0xdc  SlashSoundMods  m_ComboSounds    from <comboSounds>
    SlashSoundMods m_ComboSounds;
    // +0x108 LoopingSound    m_LoopingSound   "loop" attr from <swipeSounds> element
    LoopingSound   m_LoopingSound;

    SlashModInfo();
    virtual ~SlashModInfo() override;

    // vtable[+0x08] UnEquip @ 0x0013893c — calls LoopingSound::Reset() on m_LoopingSound (+0x108)
    virtual void UnEquip() override;
    // vtable[+0x0c] SetEquipped @ 0x00138944 — calls SetModColours + SetModScales + 3x SlashSoundMods::Reset
    virtual void SetEquipped() override;
    // vtable[+0x10] Parse override — ParseSlashModInfo @ 0x00138d00
    virtual void Parse(TiXmlElement* e) override;

    // UpdateSounds @ v1.6.1 0x001399f0 — per-frame tick of all four sound members.
    // Driven by ItemManager::Update(dt) from GameUpdate with the RAW frame dt (no
    // quickener / slow-mo scaling). Not calling it every frame leaves any mod with a
    // time_per_sound attribute permanently queued after its first sound.
    void UpdateSounds(float dt);
};

#ifdef __bada__
#include <cstddef>
static_assert(offsetof(SlashSoundMods, m_TimeUntilNextSound) == 0x14, "SlashSoundMods::m_TimeUntilNextSound");
static_assert(offsetof(SlashSoundMods, m_LastVolume)         == 0x18, "SlashSoundMods::m_LastVolume");
static_assert(offsetof(SlashSoundMods, m_LastPitch)          == 0x1c, "SlashSoundMods::m_LastPitch");
static_assert(sizeof(LoopingSound)                           == 0x10, "LoopingSound size");
static_assert(offsetof(LoopingSound, m_DesiredVol)           == 0x00, "LoopingSound::m_DesiredVol");
static_assert(offsetof(LoopingSound, m_CurrentVol)           == 0x04, "LoopingSound::m_CurrentVol");
static_assert(offsetof(LoopingSound, m_pSound)               == 0x08, "LoopingSound::m_pSound");
static_assert(offsetof(LoopingSound, m_pLoopName)            == 0x0c, "LoopingSound::m_pLoopName");
static_assert(sizeof(ItemInfo)                               == 0x48, "ItemInfo size");
static_assert(offsetof(ItemInfo, m_Scale)                    == 0x40, "ItemInfo::m_Scale");
static_assert(offsetof(ItemInfo, m_IsNew)                    == 0x44, "ItemInfo::m_IsNew");
static_assert(sizeof(SlashModInfo)                           == 0x118, "SlashModInfo size");
#endif

#endif // FN_ITEM_INFO_H
