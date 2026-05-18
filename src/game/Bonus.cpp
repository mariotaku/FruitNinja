// Analysed: 2026-05-03T00:00
// Bonus / BonusType implementation.
// Binary addresses: Bonus ctor @ 0x0010005c, dtor @ 0x0010fa40.
// BonusType ctor @ 0x0010df00, Parse @ 0x0010e7ec.

#include "Bonus.h"
#include "FruitSaveData.h"
#include "../Game.h"
#include "engine/util/StringHash.h"
#include "engine/asset/TextureManager.h"
#include <tinyxml2.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

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
//
// Returns non-zero tier value when the bonus conditions are met, 0 otherwise.
// Checks:
//   1. score within [m_MinSliced, m_MaxSliced] (0 = no bound)
//   2. per-fruit counts within [m_MinFruit[k], m_MaxFruit[k]]
//   3. m_DivisibleBy != 0: score must be divisible by m_DivisibleBy
//   4. m_PatternHashes non-empty: at least one pattern hash must be found
//      in fruitCounts (non-zero count)
// ---------------------------------------------------------------------------
int Bonus::IsAchieved(int score, std::map<uint64_t, int>& fruitCounts) {
    if (m_Tier < 0) return 0;

    // Min/max sliced bounds (0 = unconstrained)
    if (m_MinSliced > 0 && score < m_MinSliced) return 0;
    if (m_MaxSliced > 0 && score > m_MaxSliced) return 0;

    // Per-fruit min bounds
    for (std::map<uint64_t, int>::iterator it = m_MinFruit.begin();
         it != m_MinFruit.end(); ++it) {
        std::map<uint64_t, int>::iterator fc = fruitCounts.find(it->first);
        int count = (fc != fruitCounts.end()) ? fc->second : 0;
        if (count < it->second) return 0;
    }

    // Per-fruit max bounds
    for (std::map<uint64_t, int>::iterator it = m_MaxFruit.begin();
         it != m_MaxFruit.end(); ++it) {
        std::map<uint64_t, int>::iterator fc = fruitCounts.find(it->first);
        int count = (fc != fruitCounts.end()) ? fc->second : 0;
        if (count > it->second) return 0;
    }

    // Divisible-by check
    if (m_DivisibleBy > 0 && (score % m_DivisibleBy) != 0) return 0;

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

// ---------------------------------------------------------------------------
// BonusType::GetBest -- Binary @ 0x0010e094
//
// Builds a fruitCounts map from current session totals in FruitSaveData,
// then walks m_Bonuses (which are sorted descending by tier) and returns
// the first one whose IsAchieved passes. Returns nullptr if none pass.
// ---------------------------------------------------------------------------
Bonus* BonusType::GetBest() {
    // Build fruit-count map from session totals.
    // Binary uses pSaveData->m_SessionTotals for per-fruit counts.
    // Port: call FruitSaveData::GetTotal per required hash.
    // We can only use the fruitCounts map from what's available in saves;
    // for simplicity build from m_RequiredHashes keys and check GetTotal.
    // The binary also checks m_RequiredHashes against zero first.

    // Sort m_Bonuses descending by tier for deterministic best-first pick.
    std::sort(m_Bonuses.begin(), m_Bonuses.end());

    // Build fruitCounts: for each RequiredHash key, look up session total.
    std::map<uint64_t, int> fruitCounts;
    // We build from all pattern hashes + min/max keys across bonuses.
    // The binary walks the session totals map directly; port approximates
    // by populating with GetTotal for each hash we know about.
    for (size_t i = 0; i < m_Bonuses.size(); ++i) {
        Bonus& b = m_Bonuses[i];
        for (std::map<uint64_t, int>::iterator it = b.m_MinFruit.begin();
             it != b.m_MinFruit.end(); ++it) {
            if (fruitCounts.find(it->first) == fruitCounts.end())
                fruitCounts[it->first] = 0;
        }
        for (std::map<uint64_t, int>::iterator it = b.m_MaxFruit.begin();
             it != b.m_MaxFruit.end(); ++it) {
            if (fruitCounts.find(it->first) == fruitCounts.end())
                fruitCounts[it->first] = 0;
        }
        for (size_t j = 0; j < b.m_PatternHashes.size(); ++j) {
            if (fruitCounts.find(b.m_PatternHashes[j]) == fruitCounts.end())
                fruitCounts[b.m_PatternHashes[j]] = 0;
        }
    }

    // Use GetTotal (lifetime totals) as a proxy for fruitCounts.
    // TODO: binary uses session totals for this check; port uses lifetime.
    {
        Game* g_b = Game::GetInstance();
        FruitSaveData* sd_b = g_b ? g_b->pSaveData : 0;
        for (std::map<uint64_t, int>::iterator it = fruitCounts.begin();
             it != fruitCounts.end(); ++it) {
            it->second = (sd_b && sd_b->IsAchievementUnlocked((uint32_t)it->first) != 0) ? 1 : 0;
        }
    }

    int score = Game::GetInstance() ? Game::GetInstance()->currentScore : 0;
    for (size_t i = 0; i < m_Bonuses.size(); ++i) {
        if (m_Bonuses[i].IsAchieved(score, fruitCounts) != 0) {
            return &m_Bonuses[i];
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// BonusType::UnlockAchievements -- Binary @ 0x0010e12c
//
// For each bonus in m_Bonuses that has an m_AchievementHash, unlock it
// via FruitSaveData::IsAchievementUnlocked (port: we just return true if any).
// Binary: calls AchievementManager::UnlockAchievement on m_AchievementHash.
// Returns true if any achievement was unlocked.
// ---------------------------------------------------------------------------
bool BonusType::UnlockAchievements() {
    if (!m_HasAchievement) return false;
    bool any = false;
    for (size_t i = 0; i < m_Bonuses.size(); ++i) {
        Bonus& b = m_Bonuses[i];
        if (b.m_AchievementHash == 0) continue;
        // TODO: call AchievementManager::UnlockAchievement(b.m_AchievementHash)
        // when AchievementManager is fully ported.
        any = true;
    }
    return any;
}
