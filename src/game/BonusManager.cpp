// Analysed: 2026-05-03T00:00
// BonusManager -- post-game bonus award tracker singleton.
// sizeof 0x20 binary, Init @ 0x0010e8fc.

#include "BonusManager.h"
#include "FruitSaveData.h"
#include "Game.h"
#include "engine/asset/TextureManager.h"
#include "engine/util/StringHash.h"
#include "engine/util/PathCI.h"
#include "screens/BonusScreen.h"
#include "debug/Logger.h"
#include <tinyxml2.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include "game/GameWork.h"

using Mortar::TextureManager;

static bool BonusTierDescending(const Bonus* a, const Bonus* b) {
    return a->m_Tier > b->m_Tier;
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

// Binary @ BSS (file-scope global, not heap). Port uses local static.
BonusManager* BonusManager::GetInstance() {
    static BonusManager s_instance;
    return &s_instance;
}

BonusManager::BonusManager() {
    // Containers default-constructed.
}

BonusManager::~BonusManager() {
}

// ---------------------------------------------------------------------------
// Init -- Binary @ 0x0010e8fc
//
// Parses xml/bonusAwards.xml:
//   Root element: <bonusAwardsFile>
//   <bonusType> children -> BonusType::Parse -> m_AllBonuses
//   <l N="value"> children -> m_ComboTotalsByLevel
// ---------------------------------------------------------------------------
void BonusManager::Init() {
    m_AllBonuses.clear();
    m_ComboTotalsByLevel.clear();
    m_BestBonuses.clear();

    std::string path;
    const char* dataDir = TextureManager::GetDataDir();
    if (dataDir && dataDir[0]) {
        path = dataDir;
        path += "/xml/bonusAwards.xml";
    } else {
        path = "xml/bonusAwards.xml";
    }

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.LoadFile(path.c_str());
    if (err != tinyxml2::XML_SUCCESS) {
        std::string ci = Mortar::ResolvePathCI(path.c_str());
        if (!ci.empty()) err = doc.LoadFile(ci.c_str());
    }
    if (err != tinyxml2::XML_SUCCESS) {
        LOG_ERROR("BONUS/Init", "failed to open '%s' (error %d)",
               path.c_str(), (int)err);
        return;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("bonusAwardsFile");
    if (!root) {
        LOG_WARN("BONUS/Init", "no <bonusAwardsFile> root in '%s'",
               path.c_str());
        return;
    }

    for (tinyxml2::XMLElement* child = root->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        const char* tag = child->Name();
        if (!tag) continue;

        if (strcmp(tag, "bonusType") == 0) {
            BonusType bt;
            bt.Parse(child);
            m_AllBonuses.push_back(bt);
        } else if (strcmp(tag, "combo") == 0) {
            // <combo length="N" bonus="B" /> -- bonus value for combos of that length.
            int bonus = 0;
            child->QueryIntAttribute("bonus", &bonus);
            m_ComboTotalsByLevel.push_back(bonus);
        }
    }
}

// ---------------------------------------------------------------------------
// ClearBestBonuses -- Binary @ 0x000feb20
// ---------------------------------------------------------------------------
void BonusManager::ClearBestBonuses() {
    m_BestBonuses.clear();
}

// ---------------------------------------------------------------------------
// SetUpBonusScreen -- Binary @ 0x0010e404
//
// For each BonusType, calls GetBest() to get the best matching Bonus, sorts
// the results descending by tier, trims to top 3, then calls
// screen->AddAward(colour, texture, name, tier) for each.
//
// Hardcoded award colours (BGRA):
//   tier 0 (gold):   BGRA(0xAD, 0x7E, 0x00, 0x00)
//   tier 1 (red):    BGRA(0xA0, 0x05, 0x05, 0x00)
//   tier 2 (blue):   BGRA(0x01, 0x5C, 0x95, 0x00)
// ASM-verified: 2026-05-18 binary @ 0x0010e404 (re-analyst) -- screen null-guard
// wraps ONLY the AddAward loop; the cache-rebuild prefix runs unconditionally.
// ---------------------------------------------------------------------------
void BonusManager::SetUpBonusScreen(BonusScreen* screen) {
    // ASM-verified: 2026-05-22 binary @ 0x0010e1f0 (MakeColour_BGRA 3-arg
    // overload) hardcodes alpha = 0xFF (`strb 0xff,[r0,#0x3]`). Port was
    // packing 0x00 in the high byte, making every per-award entry.m_Colour
    // alpha=0 -- which made the star quad, award name text, and award score
    // text all invisible. (re-analyst)
    static const uint32_t k_TierColours[3] = {
        // BGRA packed: B=0xAD, G=0x7E, R=0x00, A=0xFF
        (0xAD) | (0x7E << 8) | (0x00 << 16) | (0xFFu << 24),  // gold
        // B=0xA0, G=0x05, R=0x05, A=0xFF
        (0xA0) | (0x05 << 8) | (0x05 << 16) | (0xFFu << 24),  // red
        // B=0x01, G=0x5C, R=0x95, A=0xFF
        (0x01) | (0x5C << 8) | (0x95 << 16) | (0xFFu << 24),  // blue
    };

    // Gather best bonus from each type.
    std::vector<Bonus*> candidates;
    for (size_t i = 0; i < m_AllBonuses.size(); ++i) {
        Bonus* b = m_AllBonuses[i].GetBest();
        if (b) candidates.push_back(b);
    }

    // Sort descending by tier (higher tier = better award).
    std::sort(candidates.begin(), candidates.end(), BonusTierDescending);

    // Trim to top 3.
    if (candidates.size() > 3) candidates.resize(3);

    // Store in m_BestBonuses.
    m_BestBonuses.clear();
    for (size_t i = 0; i < candidates.size(); ++i) {
        m_BestBonuses.push_back(*candidates[i]);
    }

    // Call AddAward on BonusScreen for each -- only when screen is non-null.
    if (screen) {
        int idx = 0;
        for (std::list<Bonus>::iterator it = m_BestBonuses.begin();
             it != m_BestBonuses.end() && idx < 3; ++it, ++idx) {
            uint32_t colour = k_TierColours[idx];
            screen->AddAward(colour, it->m_StarTexture, it->m_DisplayName, it->m_Tier);
        }
    }
}

// ASM-verified: 2026-05-22 binary @ 0x0010de24 (re-analyst).
// param: comboLen (>= 3 from caller gate).
// Two persistent counters: combo_bonus (payout sum) + best_combo (max length).
void BonusManager::AddCombo(int comboLen) {
    Game* game = Game::GetInstance();
    if (!game || !game_work.m_SaveData) return;

    static const uint32_t hComboBonus = StringHash("combo_bonus");
    static const uint32_t hBestCombo  = StringHash("best_combo");

    // ASM-verified: 2026-05-22 binary @ 0x0010de24 (re-analyst).
    // Indexed lookup into m_ComboTotalsByLevel (parsed from <combo bonus="N"/>).
    int payout = 0;
    if (!m_ComboTotalsByLevel.empty()) {
        int idx = (comboLen < 4) ? 0 : (comboLen - 3);
        int last = (int)m_ComboTotalsByLevel.size() - 1;
        if (idx > last) idx = last;
        if (idx < 0) idx = 0;
        payout = m_ComboTotalsByLevel[idx];
    }

    game_work.m_SaveData->AddToTotal("combo_bonus", hComboBonus,
                                     payout, false, false);

    int currentBest = game_work.m_SaveData->GetTotal(hBestCombo);
    int delta = comboLen - currentBest;
    if (delta < 0) delta = 0;
    game_work.m_SaveData->AddToTotal("best_combo", hBestCombo,
                                     delta, false, false);
}

// ---------------------------------------------------------------------------
// UnlockPostGameAchievements -- Binary @ 0x0010e1cc
// ---------------------------------------------------------------------------
bool BonusManager::UnlockPostGameAchievements() {
    bool any = false;
    for (size_t i = 0; i < m_AllBonuses.size(); ++i) {
        if (m_AllBonuses[i].UnlockAchievements()) any = true;
    }
    return any;
}

// ---------------------------------------------------------------------------
// Iterator helpers
// ---------------------------------------------------------------------------

Bonus* BonusManager::GetFirstBestBonus(std::list<Bonus>::iterator& it) {
    it = m_BestBonuses.begin();
    if (it == m_BestBonuses.end()) return nullptr;
    return &(*it);
}

Bonus* BonusManager::GetNextBestBonus(std::list<Bonus>::iterator& it) {
    if (it == m_BestBonuses.end()) return nullptr;
    ++it;
    if (it == m_BestBonuses.end()) return nullptr;
    return &(*it);
}
