// Analysed: 2026-05-03T00:00
// BonusManager -- post-game bonus award tracker singleton.
// sizeof 0x20 binary, Init @ 0x0010e8fc.

#include "BonusManager.h"
#include "FruitSaveData.h"
#include "Game.h"
#include "engine/math/Random.h"
#include "engine/util/StringHash.h"
#include "engine/xml/TiXml.h"
#include "screens/BonusScreen.h"
#include "game/GameWork.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    TiXmlDocument doc;
    if (!doc.LoadFile("xml/bonusAwards.xml")) {
        return;
    }

    TiXmlElement root = doc.FirstChildElement("bonusAwardsFile");
    if (!root) {
        printf("BonusManager::Init -- no <bonusAwardsFile> root\n");
        return;
    }

    for (TiXmlElement child = root.FirstChildElement();
         child; child = child.NextSiblingElement()) {
        const char* tag = child.Name();
        if (!tag) continue;

        if (strcmp(tag, "bonusType") == 0) {
            BonusType bt;
            bt.Parse(&child);
            m_AllBonuses.push_back(bt);
        } else if (strcmp(tag, "l") == 0) {
            // <l N="value"> combo level entry
            int val = 0;
            child.QueryIntAttribute("N", &val);
            m_ComboTotalsByLevel.push_back(val);
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
// Binary flow:
//   1. ClearBestBonuses()
//   2. Build shuffled index vector [0, 1, ..., N-1] using Math::g_random
//   3. For each shuffled index: GetBest() -> push_back into m_BestBonuses
//   4. m_BestBonuses.sort() (ascending by tier per operator<)
//   5. Trim from front until size <= 3 (keeps highest-tier items)
//   6. Iterate, call BonusScreen::AddAward for each with tier colour
//
// Port: step 2 shuffle not yet implemented; iteration is in-order.
// Hardcoded award colours (BGRA):
//   tier 0: BGRA(0xAD, 0x7E, 0x00, 0x00)  gold
//   tier 1: BGRA(0xA0, 0x05, 0x05, 0x00)  red
//   tier 2: BGRA(0x01, 0x5C, 0x95, 0x00)  blue
// ---------------------------------------------------------------------------
void BonusManager::SetUpBonusScreen(BonusScreen* screen) {
    ClearBestBonuses();

    // Hardcoded tier colours matching binary constants.
    static const Colour k_TierColours[3] = {
        // BGRA: B=0xAD, G=0x7E, R=0x00, A=0x00
        Colour(0x00, 0x7E, 0xAD, 0x00),  // gold
        // BGRA: B=0xA0, G=0x05, R=0x05, A=0x00
        Colour(0x05, 0x05, 0xA0, 0x00),  // red
        // BGRA: B=0x01, G=0x5C, R=0x95, A=0x00
        Colour(0x95, 0x5C, 0x01, 0x00),  // blue
    };

    // Get best from each BonusType and push directly into m_BestBonuses.
    // TODO: v1.6.1 0x0011096c (BonusManager::SetUpBonusScreen) — shuffle indices via
    // Math::g_random before iterating (Fisher-Yates), matching binary.
    for (size_t i = 0; i < m_AllBonuses.size(); ++i) {
        Bonus* b = m_AllBonuses[i].GetBest();
        if (b) m_BestBonuses.push_back(*b);
    }

    // Sort ascending by tier (lowest tier first, matching binary operator<).
    m_BestBonuses.sort();

    // Trim to top 3: erase lowest-tier items from front.
    while (m_BestBonuses.size() > 3) {
        m_BestBonuses.erase(m_BestBonuses.begin());
    }

    // Call AddAward for each (binary null-checks screen around the loop only).
    if (screen) {
        int idx = 0;
        for (std::list<Bonus>::iterator it = m_BestBonuses.begin();
             it != m_BestBonuses.end() && idx < 3; ++it, ++idx) {
            screen->AddAward(k_TierColours[idx], it->m_StarTexture,
                             it->m_DisplayName, it->m_Tier);
        }
    }
}

// ---------------------------------------------------------------------------
// AddCombo -- v1.6.1 BonusManager::AddCombo @0x0012e570
//
// Records a combo event (comboLen >= 3) into per-mode save totals.
// Key strings are inline StringHash literals in the binary:
//   "combo_bonus" (per-level combo total) and "best_combo" (best combo).
// ---------------------------------------------------------------------------
void BonusManager::AddCombo(int comboLen) {
    if (comboLen < 3) return;

    Game* game = Game::GetInstance();
    if (!game || !game_work.m_SaveData) return;

    // Mode name table per binary GetModeName @ 0x0010b15c.
    static const char* k_ModeNames[4] = { "Classic", "Casino", "Arcade", "Zen" };
    int mode = (int)game_work.gameMode;
    if (mode < 0 || mode > 3) mode = 0;
    const char* modeName = k_ModeNames[mode];

    // TODO: resolve exact format strings from binary DAT_0010def4 / DAT_0010defc.
    // Likely "CombosTotal-%s" and "BestCombo-%s" with GetModeName() suffix.
    char keyTotal[64];
    char keyBest[64];
    snprintf(keyTotal, sizeof(keyTotal), "CombosTotal-%s", modeName);
    snprintf(keyBest,  sizeof(keyBest),  "BestCombo-%s",   modeName);

    FruitSaveData* sd = game_work.m_SaveData;
    sd->AddToTotal(keyTotal, StringHash(keyTotal), 1, false, false);

    int existing = sd->GetTotal(keyBest);
    if (comboLen > existing) {
        int delta = comboLen - existing;
        sd->AddToTotal(keyBest, StringHash(keyBest), delta, false, false);
    }
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
