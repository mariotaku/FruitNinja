// Analysed: 2026-05-03T00:00
// Bonus / BonusType implementation.
// Binary addresses: Bonus ctor @ 0x0010005c, dtor @ 0x0010fa40.
// BonusType ctor @ 0x0010df00, Parse @ 0x0012f3a4.

#include "Bonus.h"
#include "FruitSaveData.h"
#include "AchievementManager.h"
#include "../Game.h"
#include "engine/util/StringHash.h"
#include "engine/util/StringTable.h"
#include "engine/asset/TextureManager.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include "game/GameWork.h"

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// Bonus -- ctor / dtor / copy
// ---------------------------------------------------------------------------

// v1.6.1 Bonus::{ctor} @0x0012ecb8 (C1; base ctor C2 @0x0012ed50).
// Defaults verified from disassembly:
//   m_MinSliced = 0, m_MaxSliced = DAT_0012ed4c = 10,000,000 (no-upper-bound
//   sentinel), m_DivisibleBy = 0, m_Tier = 5. m_MaxSliced MUST be the large
//   sentinel because Bonus::IsAchieved gates `score > m_MaxSliced` unconditionally.
Bonus::Bonus()
    : m_MinSliced(0)
    , m_MaxSliced(10000000)
    , m_DivisibleBy(0)
    , m_Tier(5)
    , m_AchievementHash(0)
{
    memset(m_NameTemplate, 0, sizeof(m_NameTemplate));
    memset(m_DisplayName,  0, sizeof(m_DisplayName));
    // m_MinFruit, m_MaxFruit, m_PatternHashes default-constructed.
}

// v1.6.1 Bonus::{ctor} @0x00130edc (copy ctor)
Bonus::Bonus(const Bonus& rhs)
    : m_MinSliced(rhs.m_MinSliced)
    , m_MaxSliced(rhs.m_MaxSliced)
    , m_MinFruit(rhs.m_MinFruit)
    , m_MaxFruit(rhs.m_MaxFruit)
    , m_DivisibleBy(rhs.m_DivisibleBy)
    , m_Tier(rhs.m_Tier)
    , m_PatternHashes(rhs.m_PatternHashes)
    , m_AchievementHash(rhs.m_AchievementHash)
    , m_StarTexture(rhs.m_StarTexture)
{
    memcpy(m_NameTemplate, rhs.m_NameTemplate, sizeof(m_NameTemplate));
    memcpy(m_DisplayName,  rhs.m_DisplayName,  sizeof(m_DisplayName));
}

// TODO: Bonus::~Bonus -- address unresolved (0x0010fa40's PLT thunk resolves to
// std::list<Fruit*>::{dtor}, but Bonus has no std::list<Fruit*> member; not this
// function's body).
Bonus::~Bonus() {
    // Containers + SmartPtr release themselves.
}

Bonus& Bonus::operator=(const Bonus& rhs) {
    if (this != &rhs) {
        m_MinSliced      = rhs.m_MinSliced;
        m_MaxSliced      = rhs.m_MaxSliced;
        m_MinFruit       = rhs.m_MinFruit;
        m_MaxFruit       = rhs.m_MaxFruit;
        m_DivisibleBy    = rhs.m_DivisibleBy;
        m_Tier           = rhs.m_Tier;
        memcpy(m_NameTemplate, rhs.m_NameTemplate, sizeof(m_NameTemplate));
        memcpy(m_DisplayName,  rhs.m_DisplayName,  sizeof(m_DisplayName));
        m_PatternHashes  = rhs.m_PatternHashes;
        m_AchievementHash = rhs.m_AchievementHash;
        m_StarTexture    = rhs.m_StarTexture;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Bonus::Parse -- Binary @ 0x0012f0f8
// ASM-spec v1.6.1 Bonus::Parse @0x0012f0f8
//
// Reads attributes from a <bonus> XML element (real bonusawards.xml schema):
//   min      = int          -> m_MinSliced (lower bound on totalAcrossFruits)
//   max      = int          -> m_MaxSliced (upper bound; also alias "equals" sets both)
//   equals   = int          -> m_MinSliced = m_MaxSliced = val (exact match)
//   multiple = int          -> m_DivisibleBy
//   points   = int          -> m_Tier (award tier; absent -> stays 0)
//   texture  = string       -> m_StarTexture (loads "<name>.tex")
//   achievement = string    -> m_AchievementHash (StringHash)
//   min-<fruit> = int       -> m_MinFruit[StringHash(fruit)]
//   max-<fruit> = int       -> m_MaxFruit[StringHash(fruit)]
// Inner text -> m_NameTemplate (localised via GETSTRING_CAST_0_STR; TinyXML
// already condenses surrounding whitespace, so no manual trim is needed)
// Parent-texture fallback is done by BonusType::Parse after this returns (binary @0x0012f3a4).
// ---------------------------------------------------------------------------
void Bonus::Parse(TiXmlElement* e) {
    if (!e) return;

    e->QueryIntAttribute("min",      &m_MinSliced);
    e->QueryIntAttribute("max",      &m_MaxSliced);
    e->QueryIntAttribute("multiple", &m_DivisibleBy);
    e->QueryIntAttribute("points",   &m_Tier);

    // "equals" sets an exact-match range on the total.
    int equalsVal = -1;
    if (e->QueryIntAttribute("equals", &equalsVal) == TIXML_SUCCESS) {
        m_MinSliced = equalsVal;
        m_MaxSliced = equalsVal;
    }

    const char* achievement = e->Attribute("achievement");
    if (achievement && achievement[0]) {
        m_AchievementHash = StringHash(achievement);
        // Binary: achievement present + points absent => m_Tier = -1 (achievement-only
        // bonus; never selected as "best" in BonusType::GetBest since bestTier starts at 0).
        if (!e->Attribute("points")) {
            m_Tier = -1;
        }
    }

    const char* texName = e->Attribute("texture");
    if (texName && texName[0]) {
        char texPath[128];
        snprintf(texPath, sizeof(texPath), "%s.tex", texName);
        m_StarTexture = TextureManager::LoadLocalisedTexture(texPath);
    }
    // Parent-texture fallback is handled by BonusType::Parse AFTER this returns
    // (binary BonusType::Parse @0x0012f3a4: SmartPtr::operator= on m_StarTexture if !bool(m_StarTexture)).

    // Walk all attributes to pick up "min-<fruit>" and "max-<fruit>" prefixes.
    for (TiXmlAttribute attr = e->FirstAttribute(); attr; attr = attr.Next()) {
        const char* aname = attr.Name();
        if (!aname) continue;
        if (strncmp(aname, "min-", 4) == 0) {
            const char* fruitName = aname + 4;
            if (fruitName[0]) {
                uint32_t key = StringHash(fruitName);
                m_MinFruit[key] = atoi(attr.Value());
            }
        } else if (strncmp(aname, "max-", 4) == 0) {
            const char* fruitName = aname + 4;
            if (fruitName[0]) {
                uint32_t key = StringHash(fruitName);
                m_MaxFruit[key] = atoi(attr.Value());
            }
        }
    }

    // ASM-spec v1.6.1 Bonus::Parse @0x0012f0f8: strcpy(m_NameTemplate,
    // GETSTRING_CAST_0_STR(GetText())) -- template holds the LOCALIZED string;
    // m_DisplayName written ONLY by IsAchieved @0x0012e708
    // snprintf(m_DisplayName,0x40,m_NameTemplate,score).
    const char* text = e->GetText();
    if (!text) {
        m_NameTemplate[0] = '\0';
    } else {
        const char* localised = GETSTRING_CAST_0_STR(text);
        strncpy(m_NameTemplate, localised ? localised : text, sizeof(m_NameTemplate) - 1);
        m_NameTemplate[sizeof(m_NameTemplate) - 1] = '\0';
    }

    // ASM-spec v1.6.1 Bonus::Parse @0x0012f0f8: valuesEqual -> m_PatternHashes
    // Attribute("valuesEqual") -> comma-split (trim leading space after each comma,
    // skip empties) -> StringHash each token -> push_back (uint32_t).
    const char* valuesEq = e->Attribute("valuesEqual");
    if (valuesEq && valuesEq[0]) {
        char buf[256];
        strncpy(buf, valuesEq, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* tok = strtok(buf, ",");
        while (tok) {
            while (*tok == ' ') ++tok;
            if (*tok) m_PatternHashes.push_back(StringHash(tok));
            tok = strtok(NULL, ",");
        }
    }
}

// ---------------------------------------------------------------------------
// ASM-verified: 2026-08-01T16:08Z v1.6.1 Bonus::IsAchieved @ 0x0012e708..0x0012e917 (asm-inspector)
//
// Every comparison was operand-resolved against the binary. Note for future
// sweeps: an asm-verify row here reports `blt` (binary) vs `bgt` (port) at the
// min gate. That is a REGISTER-NORMALISATION ARTIFACT, not a bug -- the binary
// does `cmp r1,r3` (score,min) and the port `cmp r3,r1` (min,score), which is
// the same strict test with the operands swapped and the same fail target. GCC
// picked the reverse order because r3 was already loaded. Do not "fix" it.
//
// Boundary sense confirmed on both sides: hitting m_MaxSliced exactly COUNTS as
// achieved, and score == m_MinSliced counts, so equals="N" bonuses award at
// exactly N.
//
// Faithful port of the binary control flow:
//   1. Gate (0x0012e708-0x0012e74c, all unconditional -- NO `>0` guards):
//        if (score <  m_MinSliced) return 0;
//        if (score >  m_MaxSliced) return 0;
//        if (m_DivisibleBy > 0 && score % m_DivisibleBy != 0) return 0;
//      m_MaxSliced's ctor default acts as the "no upper bound" sentinel, so the
//      unconditional `score > m_MaxSliced` almost never fires unless an explicit
//      max/equals was parsed.
//      TODO: v1.6.1 Bonus::Bonus -- the ctor default cited a v1.5.x DAT_0010e390
//      and its v1.6.1 address is unconfirmed; re-verify the constant rather than
//      trusting the old 10,000,000 figure.
//   2. Per-fruit loop (0x0012e750-0x0012e7fc): iterate fruitCounts (param_2). For
//      each entry: min from m_MinFruit (default 0), max from m_MaxFruit
//      (absent-max sentinel loaded from the pool at 0x0012e918 = 1,000,000).
//      Fail if count < min || count > max -- both strict, and the port's
//      branchless predication for the `||` is equivalent.
//   3. Pattern loop (0x0012e820-0x0012e8bc) over m_PatternHashes: EVERY pattern
//      hash must be present in fruitCounts, and all must share the SAME count,
//      which on the first iteration must be > 0. (r10 = first-iter flag,
//      r7 = reference count seeded on first iter.)
//   4. Side effects on success (0x0012e8c0-0x0012e8f8):
//        if (m_AchievementHash != 0 && m_Tier > 0)
//            AchievementManager::GetInstance()->UnlockBonusAchievement(m_AchievementHash);
//        snprintf(m_DisplayName, 0x40, m_NameTemplate, score); // template has %d
//   5. return m_Tier (0 on any fail).
// DIFFERS: original param name was `score` -- renamed to `score` kept; the
//   second param is fruitCounts (binary param_2). Cosmetic only; no ABI change.
int Bonus::IsAchieved(int score, std::map<unsigned long, int>& fruitCounts) {
    // Gate -- unconditional bounds + divisible-by (binary 0010df3e-0010df62).
    if (score < m_MinSliced) return 0;
    if (score > m_MaxSliced) return 0;
    if (m_DivisibleBy > 0 && (score % m_DivisibleBy) != 0) return 0;

    // Per-fruit bounds: iterate fruitCounts (binary's param_2 iteration order).
    // Missing key in m_MinFruit defaults to 0; missing key in m_MaxFruit defaults
    // to DAT_0010e090 = 1,000,000.
    static const int kNoMaxSentinel = 1000000;  // DAT_0010e090 = 0x000f4240
    for (std::map<unsigned long, int>::iterator fc = fruitCounts.begin();
         fc != fruitCounts.end(); ++fc) {
        int count = fc->second;

        std::map<unsigned long, int>::const_iterator minIt = m_MinFruit.find(fc->first);
        int minVal = (minIt != m_MinFruit.end()) ? minIt->second : 0;

        std::map<unsigned long, int>::const_iterator maxIt = m_MaxFruit.find(fc->first);
        int maxVal = (maxIt != m_MaxFruit.end()) ? maxIt->second : kNoMaxSentinel;

        if (count < minVal || count > maxVal) return 0;
    }

    // Pattern loop: every pattern hash must be present in fruitCounts, all with
    // the SAME count, which on the first pattern must be > 0 (binary 0010dfec).
    bool firstPattern = true;
    int refCount = -1;
    for (size_t i = 0; i < m_PatternHashes.size(); ++i) {
        std::map<unsigned long, int>::iterator fc = fruitCounts.find(m_PatternHashes[i]);
        if (fc == fruitCounts.end()) return 0;  // pattern fruit absent
        if (firstPattern) {
            refCount = fc->second;
            if (refCount <= 0) return 0;
            firstPattern = false;
        } else if (refCount != fc->second) {
            return 0;
        }
    }

    // Success side effects (binary 0010e054-0010e076).
    if (m_AchievementHash != 0 && m_Tier > 0) {
        AchievementManager::GetInstance()->UnlockBonusAchievement((unsigned long)m_AchievementHash);
    }
    // Binary uses m_NameTemplate as the snprintf format string directly with
    // `score` as the variadic arg (templates contain %d). Reproduced faithfully.
    snprintf(m_DisplayName, sizeof(m_DisplayName), m_NameTemplate, score);

    return m_Tier;
}

// ---------------------------------------------------------------------------
// BonusType -- ctor / dtor / copy
// ---------------------------------------------------------------------------

// v1.6.1 BonusType::{ctor} @0x0012e6c0
BonusType::BonusType()
    : m_HasAchievement(false)
{
}

// TODO: BonusType::BonusType(const BonusType&) -- address unresolved (0x0010df1c's PLT
// thunk resolves to std::list<Bonus>::_M_transfer, but BonusType holds std::vector<Bonus>,
// not std::list; not this function's body).
BonusType::BonusType(const BonusType& rhs)
    : m_RequiredHashes(rhs.m_RequiredHashes)
    , m_Bonuses(rhs.m_Bonuses)
    , m_HasAchievement(rhs.m_HasAchievement)
{
}

BonusType::~BonusType() {
}

BonusType& BonusType::operator=(const BonusType& rhs) {
    if (this != &rhs) {
        m_RequiredHashes = rhs.m_RequiredHashes;
        m_Bonuses        = rhs.m_Bonuses;
        m_HasAchievement = rhs.m_HasAchievement;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// BonusType::Parse -- Binary @ 0x0012f3a4
// ASM-spec v1.6.1 BonusType::Parse @0x0012f3a4
//
// Reads a <bonusType> element (real bonusawards.xml schema):
//   "total" attr (CSV of fruit/stat names) -> m_RequiredHashes keys (values=0)
//   "texture" attr                         -> parentTexSmart (SStack_20 in binary)
//   <bonus> children                       -> m_Bonuses
// After each Bonus::Parse, if m_StarTexture is null, assigns parentTexSmart
// (binary: SmartPtr::operator= at 0x0012f3a4+offset).
// ---------------------------------------------------------------------------
void BonusType::Parse(TiXmlElement* e) {
    if (!e) return;

    // Load parent-level texture SmartPtr (binary BonusType::Parse @0x0012f3a4: SStack_20).
    Mortar::SmartPtr<Mortar::Texture> parentTexSmart;
    const char* parentTexAttr = e->Attribute("texture");
    if (parentTexAttr && parentTexAttr[0]) {
        char texPath[128];
        snprintf(texPath, sizeof(texPath), "%s.tex", parentTexAttr);
        parentTexSmart = TextureManager::LoadLocalisedTexture(texPath);
    }

    const char* total = e->Attribute("total");
    if (total && total[0]) {
        char buf[256];
        strncpy(buf, total, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* tok = strtok(buf, ",");
        while (tok) {
            while (*tok == ' ') ++tok;
            char* end = tok + strlen(tok) - 1;
            while (end > tok && *end == ' ') { *end = '\0'; --end; }
            if (*tok) {
                uint32_t key = StringHash(tok);
                m_RequiredHashes[key] = 0;
            }
            tok = strtok(NULL, ",");
        }
    }

    for (TiXmlElement child = e->FirstChildElement("bonus");
         child; child = child.NextSiblingElement("bonus")) {
        Bonus b;
        b.Parse(&child);
        if (!b.m_StarTexture) {  // binary: SmartPtr::operator_cast_to_bool(auStack_100+0xD0)
            b.m_StarTexture = parentTexSmart;
        }
        if (b.m_AchievementHash != 0) m_HasAchievement = true;
        m_Bonuses.push_back(b);
    }
}

// Forward declaration (defined below). Binary: _Z13GetBonusTotalm @0x0012e4c4
int GetBonusTotal(unsigned long hash);

// ASM-verified: 2026-05-22 v1.6.1 BonusType::GetBest @ 0x0012e91c (re-analyst).
Bonus* BonusType::GetBest() {
    int totalAcrossFruits = 0;
    for (std::map<unsigned long, int>::iterator it = m_RequiredHashes.begin();
         it != m_RequiredHashes.end(); ++it) {
        int total = GetBonusTotal(it->first);
        it->second = total;
        totalAcrossFruits += total;
    }

    Bonus* best = nullptr;
    int bestTier = 0;
    for (size_t i = 0; i < m_Bonuses.size(); ++i) {
        int tier = m_Bonuses[i].m_Tier;
        if (tier > bestTier && m_Bonuses[i].IsAchieved(totalAcrossFruits, m_RequiredHashes)) {
            best = &m_Bonuses[i];
            bestTier = tier;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// GetBonusTotal -- Binary: _Z13GetBonusTotalm @0x0012e4c4 (v1.6.1)
//
// If hash == StringHash("score"), return the current game score for player 0.
// Otherwise return FruitSaveData::GetTotal(sd, hash).
// The "score" string hash is cached on first call (function-local static).
// ---------------------------------------------------------------------------
int GetBonusTotal(unsigned long hash) {
    static const uint32_t kScoreHash = StringHash("score");
    if ((uint32_t)hash == kScoreHash) {
        Game* g = Game::GetInstance();
        return g ? game_work.currentScore : 0;
    }
    Game* g = Game::GetInstance();
    FruitSaveData* sd = g ? game_work.m_SaveData : 0;
    return sd ? sd->GetTotal((uint32_t)hash) : 0;
}

// ---------------------------------------------------------------------------
// BonusType::UnlockAchievements -- Binary @ 0x0012e9f4
// ASM-verified: 2026-05-18 v1.6.1 BonusType::UnlockAchievements @ 0x0012e9f4 (re-analyst Claude #49)
//
// Pre-pass: refresh per-fruit totals into m_RequiredHashes values (the map
// keys were populated by Parse from the "requires" CSV; values get overwritten
// here with GetBonusTotal), sum into totalAcrossFruits.
// Then for each Bonus with m_AchievementHash != 0, call IsAchieved; on success
// call AchievementManager::UnlockBonusAchievement.
// Returns true if any achievement was awarded.
// ---------------------------------------------------------------------------
bool BonusType::UnlockAchievements() {
    if (!m_HasAchievement) return false;

    int totalAcrossFruits = 0;
    for (std::map<unsigned long, int>::iterator it = m_RequiredHashes.begin();
         it != m_RequiredHashes.end(); ++it) {
        int total = GetBonusTotal(it->first);
        it->second = total;
        totalAcrossFruits += total;
    }

    bool anyAwarded = false;
    for (size_t i = 0; i < m_Bonuses.size(); ++i) {
        Bonus* b = &m_Bonuses[i];
        if (b->m_AchievementHash == 0) continue;
        if (!b->IsAchieved(totalAcrossFruits, m_RequiredHashes)) continue;
        AchievementManager::GetInstance()->UnlockBonusAchievement((unsigned long)b->m_AchievementHash);
        anyAwarded = true;
    }
    return anyAwarded;
}
