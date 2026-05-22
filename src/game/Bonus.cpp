// Analysed: 2026-05-03T00:00
// Bonus / BonusType implementation.
// Binary addresses: Bonus ctor @ 0x0010005c, dtor @ 0x0010fa40.
// BonusType ctor @ 0x0010df00, Parse @ 0x0010e7ec.

#include "Bonus.h"
#include "FruitSaveData.h"
#include "AchievementManager.h"
#include "../Game.h"
#include "engine/util/StringHash.h"
#include "engine/asset/TextureManager.h"
#include <tinyxml2.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include "game/GameWork.h"

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// Bonus -- ctor / dtor / copy
// ---------------------------------------------------------------------------

// Binary @ 0x0010005c
Bonus::Bonus()
    : m_MinSliced(0)
    , m_MaxSliced(0)
    , m_DivisibleBy(0)
    , m_Tier(-1)
    , m_AchievementHash(0)
{
    memset(m_NameTemplate, 0, sizeof(m_NameTemplate));
    memset(m_DisplayName,  0, sizeof(m_DisplayName));
    // m_MinFruit, m_MaxFruit, m_PatternHashes default-constructed.
}

// Binary @ 0x00110090
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

// Binary @ 0x0010fa40
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
// Bonus::Parse -- Binary @ 0x0010e61c
//
// Reads attributes from a <bonus> XML element:
//   requires-min = int          -> m_MinSliced
//   requires-max = int          -> m_MaxSliced
//   single       = int          -> m_DivisibleBy
//   modulo       = int          -> m_DivisibleBy (alias)
//   priority     = int          -> m_Tier
//   texture      = string       -> m_StarTexture (loads "<name>.tex")
//   pattern      = csv hashes   -> m_PatternHashes
//   achievement  = string       -> m_AchievementHash (StringHash)
//   min-<fruit>  = int          -> m_MinFruit[StringHash(fruit)]
//   max-<fruit>  = int          -> m_MaxFruit[StringHash(fruit)]
// Inner text -> m_NameTemplate (stripped)
// ---------------------------------------------------------------------------
void Bonus::Parse(tinyxml2::XMLElement* e) {
    if (!e) return;

    e->QueryIntAttribute("requires-min", &m_MinSliced);
    e->QueryIntAttribute("requires-max", &m_MaxSliced);
    e->QueryIntAttribute("single",       &m_DivisibleBy);
    e->QueryIntAttribute("modulo",       &m_DivisibleBy);
    e->QueryIntAttribute("priority",     &m_Tier);

    const char* texName = e->Attribute("texture");
    if (texName && texName[0]) {
        char texPath[128];
        snprintf(texPath, sizeof(texPath), "%s.tex", texName);
        m_StarTexture = TextureManager::LoadLocalisedTexture(texPath);
    }

    const char* achievement = e->Attribute("achievement");
    if (achievement && achievement[0]) {
        m_AchievementHash = StringHash(achievement);
    }

    // pattern attr: comma-separated list of strings -> hash each token
    const char* pattern = e->Attribute("pattern");
    if (pattern && pattern[0]) {
        char buf[256];
        strncpy(buf, pattern, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* tok = strtok(buf, ",");
        while (tok) {
            // trim leading/trailing whitespace
            while (*tok == ' ') ++tok;
            char* end = tok + strlen(tok) - 1;
            while (end > tok && *end == ' ') { *end = '\0'; --end; }
            if (*tok) {
                m_PatternHashes.push_back((uint64_t)StringHash(tok));
            }
            tok = strtok(nullptr, ",");
        }
    }

    // Walk all attributes to pick up "min-<fruit>" and "max-<fruit>" prefixes.
    for (const tinyxml2::XMLAttribute* attr = e->FirstAttribute();
         attr; attr = attr->Next()) {
        const char* aname = attr->Name();
        if (!aname) continue;
        if (strncmp(aname, "min-", 4) == 0) {
            const char* fruitName = aname + 4;
            if (fruitName[0]) {
                uint64_t key = (uint64_t)StringHash(fruitName);
                m_MinFruit[key] = atoi(attr->Value());
            }
        } else if (strncmp(aname, "max-", 4) == 0) {
            const char* fruitName = aname + 4;
            if (fruitName[0]) {
                uint64_t key = (uint64_t)StringHash(fruitName);
                m_MaxFruit[key] = atoi(attr->Value());
            }
        }
    }

    // Inner text -> m_NameTemplate
    const char* text = e->GetText();
    if (text) {
        // Strip leading whitespace
        while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') ++text;
        strncpy(m_NameTemplate, text, sizeof(m_NameTemplate) - 1);
        m_NameTemplate[sizeof(m_NameTemplate) - 1] = '\0';
        // Strip trailing whitespace
        int len = (int)strlen(m_NameTemplate);
        while (len > 0 && (m_NameTemplate[len-1] == ' ' || m_NameTemplate[len-1] == '\t'
               || m_NameTemplate[len-1] == '\r' || m_NameTemplate[len-1] == '\n')) {
            m_NameTemplate[--len] = '\0';
        }
    }

    // m_DisplayName: same as m_NameTemplate by default (XML may override separately).
    strncpy(m_DisplayName, m_NameTemplate, sizeof(m_DisplayName) - 1);
    m_DisplayName[sizeof(m_DisplayName) - 1] = '\0';
}

// ---------------------------------------------------------------------------
// Bonus::IsAchieved -- Binary @ 0x0010df38
// ASM-verified: 2026-05-18 binary @ 0x0010df38 (re-analyst Claude #49)
//
// Returns non-zero tier value when the bonus conditions are met, 0 otherwise.
// Checks:
//   1. totalAcrossFruits within [m_MinSliced, m_MaxSliced] (0 = no bound)
//   2. iterate fruitCounts; for each entry look up min (default 0) and max
//      (default 1,000,000 = DAT_0010e090); fail if count outside bounds.
//   3. m_DivisibleBy != 0: totalAcrossFruits must be divisible by m_DivisibleBy
//   4. m_PatternHashes non-empty: at least one pattern hash must be found
//      in fruitCounts (non-zero count)
// Binary iterates fruitCounts (param_2), not m_MinFruit/m_MaxFruit, so a fruit
// hash present in m_MinFruit but absent from fruitCounts is silently skipped.
// ---------------------------------------------------------------------------
// DIFFERS: original param name was `score` — renamed to `totalAcrossFruits`
//   to match call-site semantics (UnlockAchievements passes the fruit-slice sum,
//   not the game score). Cosmetic only; no ABI change.
int Bonus::IsAchieved(int totalAcrossFruits, std::map<uint64_t, int>& fruitCounts) {
    if (m_Tier < 0) return 0;

    // Min/max sliced bounds (0 = unconstrained)
    if (m_MinSliced > 0 && totalAcrossFruits < m_MinSliced) return 0;
    if (m_MaxSliced > 0 && totalAcrossFruits > m_MaxSliced) return 0;

    // Per-fruit bounds: iterate fruitCounts (binary's param_2 iteration direction).
    // Missing key in m_MinFruit defaults to 0; missing key in m_MaxFruit defaults
    // to 1,000,000 (DAT_0010e090 = 0x000f4240).
    static const int kNoMaxSentinel = 1000000;
    for (std::map<uint64_t, int>::iterator fc = fruitCounts.begin();
         fc != fruitCounts.end(); ++fc) {
        int count = fc->second;

        std::map<uint64_t, int>::const_iterator minIt = m_MinFruit.find(fc->first);
        int minVal = (minIt != m_MinFruit.end()) ? minIt->second : 0;
        if (count < minVal) return 0;

        std::map<uint64_t, int>::const_iterator maxIt = m_MaxFruit.find(fc->first);
        int maxVal = (maxIt != m_MaxFruit.end()) ? maxIt->second : kNoMaxSentinel;
        if (count > maxVal) return 0;
    }

    // Divisible-by check
    if (m_DivisibleBy > 0 && (totalAcrossFruits % m_DivisibleBy) != 0) return 0;

    // Pattern check: at least one pattern hash present in fruitCounts
    if (!m_PatternHashes.empty()) {
        bool found = false;
        for (size_t i = 0; i < m_PatternHashes.size(); ++i) {
            std::map<uint64_t, int>::iterator fc = fruitCounts.find(m_PatternHashes[i]);
            if (fc != fruitCounts.end() && fc->second > 0) {
                found = true;
                break;
            }
        }
        if (!found) return 0;
    }

    return m_Tier;
}

// ---------------------------------------------------------------------------
// BonusType -- ctor / dtor / copy
// ---------------------------------------------------------------------------

// Binary @ 0x0010df00
BonusType::BonusType()
    : m_HasAchievement(false)
{
}

// Binary @ 0x0010df1c
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
// BonusType::Parse -- Binary @ 0x0010e7ec
//
// Reads a <bonusType> element:
//   "requires" attr (CSV of fruit names) -> m_RequiredHashes
//   <bonus> children                     -> m_Bonuses
// ---------------------------------------------------------------------------
void BonusType::Parse(tinyxml2::XMLElement* e) {
    if (!e) return;

    const char* requires_ = e->Attribute("requires");
    if (requires_ && requires_[0]) {
        char buf[256];
        strncpy(buf, requires_, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* tok = strtok(buf, ",");
        while (tok) {
            while (*tok == ' ') ++tok;
            if (*tok) {
                uint64_t key = (uint64_t)StringHash(tok);
                m_RequiredHashes[key] = 1;
            }
            tok = strtok(nullptr, ",");
        }
    }

    for (tinyxml2::XMLElement* child = e->FirstChildElement("bonus");
         child; child = child->NextSiblingElement("bonus")) {
        Bonus b;
        b.Parse(child);
        if (b.m_Tier >= 0) {
            if (b.m_AchievementHash != 0) m_HasAchievement = true;
            m_Bonuses.push_back(b);
        }
    }
}

static int GetBonusTotal(uint64_t hash);

// ASM-verified: 2026-05-22 binary @ 0x0010e094 (re-analyst).
Bonus* BonusType::GetBest() {
    int totalAcrossFruits = 0;
    for (std::map<uint64_t, int>::iterator it = m_RequiredHashes.begin();
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
// GetBonusTotal -- Binary @ 0x0010ddb4 (file-scope helper)
//
// If hash == StringHash("score"), return the current game score for player 0.
// Otherwise return FruitSaveData::GetTotal(sd, hash).
// The "score" string hash is cached on first call (function-local static).
// ---------------------------------------------------------------------------
static int GetBonusTotal(uint64_t hash) {
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
// BonusType::UnlockAchievements -- Binary @ 0x0010e12c
// ASM-verified: 2026-05-18 binary @ 0x0010e12c (re-analyst Claude #49)
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
    for (std::map<uint64_t, int>::iterator it = m_RequiredHashes.begin();
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
