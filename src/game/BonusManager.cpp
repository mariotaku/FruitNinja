// BonusManager -- post-game bonus award tracker singleton.
// sizeof 0x20 binary, Init @ v1.6.1 0x0012f53c.

#include "BonusManager.h"
#include "FruitSaveData.h"
#include "Game.h"
#include "engine/math/Random.h"
#include "engine/util/StringHash.h"
#include "engine/xml/TiXml.h"
#include "screens/BonusScreen.h"
#include "game/GameWork.h"
#include "debug/Logger.h"
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
// Init -- v1.6.1 BonusManager::Init @0x0012f53c
// (thunk @0x001043e4 tail-calls this body; header's old 0x0010e8fc was
// unversioned v1.5.x residue.)
//
// Parses xml/bonusAwards.xml:
//   Root element: <bonusAwardsFile>
//   <bonusType> children -> BonusType::Parse -> m_AllBonuses
//   <combo bonus="N"/> children -> m_ComboTotalsByLevel (push_back by
//   document order; the "length" attribute is unread by the binary --
//   AddCombo indexes this vector positionally by comboLen-3, clamped)
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
        LOG_WARN("BonusManager", "Init -- no <bonusAwardsFile> root");
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
        } else if (strcmp(tag, "combo") == 0) {
            // <combo length="N" bonus="X"/> combo level entry
            int val = 0;
            child.QueryIntAttribute("bonus", &val);
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
// SetUpBonusScreen -- v1.6.1 BonusManager::SetUpBonusScreen @0x0012ede8
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
//   tier 0: BGRA(0xAD, 0x7E, 0x00, 0xFF)  gold
//   tier 1: BGRA(0xA0, 0x05, 0x05, 0xFF)  red
//   tier 2: BGRA(0x01, 0x5C, 0x95, 0xFF)  blue
// ---------------------------------------------------------------------------
void BonusManager::SetUpBonusScreen(BonusScreen* screen) {
    ClearBestBonuses();

    // Hardcoded tier colours matching binary constants.
    // ASM-spec v1.6.1 BonusManager::SetUpBonusScreen @0x0012ede8: award colours
    // built via 3-arg Colour ctor @0x0012eb1c, which hardwires byte[3]=0xFF
    // (alpha). The alpha=0x00 previously here made every award row invisible.
    static const Colour k_TierColours[3] = {
        // BGRA: B=0xAD, G=0x7E, R=0x00, A=0xFF
        Colour(0x00, 0x7E, 0xAD, 0xFF),  // gold
        // BGRA: B=0xA0, G=0x05, R=0x05, A=0xFF
        Colour(0x05, 0x05, 0xA0, 0xFF),  // red
        // BGRA: B=0x01, G=0x5C, R=0x95, A=0xFF
        Colour(0x95, 0x5C, 0x01, 0xFF),  // blue
    };

    // Get best from each BonusType and push directly into m_BestBonuses.
    // TODO: v1.6.1 0x0012ede8 (BonusManager::SetUpBonusScreen) — shuffle indices via
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
// ASM-spec v1.6.1 BonusManager::AddCombo @0x0012e570:
//   combo_bonus += m_ComboTotalsByLevel[clamp(comboLen - 3, 0, size-1)];
//   best_combo   = max(best_combo, comboLen);
// Key strings are the exact literals "combo_bonus" / "best_combo" -- NO
// per-mode suffix (BonusType::Parse hashes these same literals out of
// bonusAwards.xml's <bonusType total="..."> attribute; a mode-suffixed key
// here would hash differently and the bonus could never be read back by
// Bonus::GetBest()/GetBonusTotal()).
// ---------------------------------------------------------------------------
void BonusManager::AddCombo(int comboLen) {
    if (comboLen < 3) return;

    Game* game = Game::GetInstance();
    if (!game || !game_work.m_SaveData) return;

    FruitSaveData* sd = game_work.m_SaveData;

    static const uint32_t hComboBonus = StringHash("combo_bonus");
    static const uint32_t hBestCombo  = StringHash("best_combo");

    int amt = 0;
    if (!m_ComboTotalsByLevel.empty()) {
        int idx = comboLen - 3;
        if (idx < 0) idx = 0;
        int maxIdx = (int)m_ComboTotalsByLevel.size() - 1;
        if (idx > maxIdx) idx = maxIdx;
        amt = m_ComboTotalsByLevel[idx];
    }
    sd->AddToTotal("combo_bonus", hComboBonus, amt, false, false);

    int existing = sd->GetTotal(hBestCombo);
    int delta = comboLen - existing;
    if (delta < 0) delta = 0;
    sd->AddToTotal("best_combo", hBestCombo, delta, false, false);
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
