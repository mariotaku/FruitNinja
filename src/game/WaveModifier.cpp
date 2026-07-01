// Analysed: 2026-05-03T00:00

#include "WaveModifier.h"
#include "PowerUpManager.h"
#include "WaveManager.h"
#include "WaveStructs.h"
#include "GameWork.h"
#include "ItemParseUtil.h"
#include "entities/Fruit.h"
#include "util/StringHash.h"
#include "math/Random.h"
#include <cstdlib>

// Binary @ 0x00122c64. Re-rolls fruit-type indices from string-name vector.
// m_FruitTypeNames holds one name per fruit slot (from XML "type" attr, split by SplitWords).
// BOMB / BOMB_PINEAPPLE -> -2 (bomb sentinel); RANDOM -> RandomFruit(false); else FruitType lookup.
void SPAWNER_INFO::SelectTypes() {
    static const uint32_t kHashBomb        = StringHash("BOMB");
    static const uint32_t kHashBombPineapp = StringHash("BOMB_PINEAPPLE");
    static const uint32_t kHashRandom      = StringHash("RANDOM");
    for (int i = 0; i < m_FruitTypeCount; ++i) {
        m_pFruitTypeHashes[i] = -1;
        uint32_t h = StringHash(m_FruitTypeNames[i].c_str());
        if (h == kHashBomb || h == kHashBombPineapp)
            m_pFruitTypeHashes[i] = -2;
        else if (h == kHashRandom)
            m_pFruitTypeHashes[i] = Fruit::RandomFruit(false);
        else
            m_pFruitTypeHashes[i] = Fruit::FruitType(m_FruitTypeNames[i].c_str(), false);
    }
}

// PROBABILITY_OVERIDE::SelectType — binary @ 0x00122b44.
// Populates m_TypeQueue[] from m_Types string names.
// Three lazy-init guarded statics (Bomb / BombPineapple / Random hashes).
void PROBABILITY_OVERIDE::SelectType() {
    static const uint32_t kHashBomb        = StringHash("Bomb");
    static const uint32_t kHashBombPineapp = StringHash("BombPineapple");
    static const uint32_t kHashRandom      = StringHash("Random");
    int n = (int)m_Types.size();
    for (int i = 0; i < n && i < 20; ++i) {
        uint32_t h = StringHash(m_Types[i].c_str());
        if (h == kHashBomb || h == kHashBombPineapp)
            m_TypeQueue[i] = -2;
        else if (h == kHashRandom)
            m_TypeQueue[i] = Fruit::RandomFruit(false);
        else
            m_TypeQueue[i] = Fruit::FruitType(m_Types[i].c_str(), false);
    }
}

// PROBABILITY_OVERIDE::Parse — binary @ 0x001231d8
void PROBABILITY_OVERIDE::Parse(TiXmlElement* xml) {
    xml->QueryIntAttribute("percentageChance",     &m_PercentChance);
    xml->QueryIntAttribute("waveCount",            &m_PerWaveCount);
    const char* typesAttr = xml->Attribute("types");
    if (typesAttr) {
        m_TypeCount = WaveManager::SplitWords(typesAttr, m_Types);
    }
    xml->QueryIntAttribute("perWave",              &m_PerWave);
    xml->QueryFloatAttribute("disableWhenPowered", &m_DisableWhenPowered);
    xml->QueryIntAttribute("numWaves",             &m_SelectedType);
}

// PROBABILITY_OVERIDE::GetType — binary @ 0x001217e4
// Picks a random entry from m_TypeQueue[0..m_TypeCount).
// SelectType() is called once at Reset/NewGame; GetType() is called per-spawn.
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x001217e4 (re-analyst)
int PROBABILITY_OVERIDE::GetType() {
    if (m_TypeCount <= 0) return -1;
    uint32_t idx = WaveManager::GetInstance()->GetRandom().Rand32((uint32_t)m_TypeCount);
    return m_TypeQueue[idx];
}

// ----------------------------------------------------------------------------
// WaveQue methods — binary @ 0x00124334 / 0x00123258 / 0x00124464 / 0x00121b20
// Only used in gameMode==2 (Survival/Combo). Classic/Arcade never call these.
// ----------------------------------------------------------------------------

// WaveQue::AddWave — binary @ 0x00124334
void WaveQue::AddWave(WaveInfo* wi, bool isLast) {
    Math::Random& rng = WaveManager::GetInstance()->m_Random;
    WaveQueItem item;
    item.m_WaveIndex = wi->m_WaveIndex;

    // Roll a policy code that controls alternating normal/random spawner-op assignment.
    int policy;
    uint32_t roll = rng.Rand32(100);
    if (isLast) {
        policy = (roll < 0x33u) ? -4 : -3;
    } else {
        if      (roll < 0x14u) policy = 5;
        else if (roll < 0x28u) policy = 0x5f;
        else if (roll < 0x32u) policy = 0x23;
        else if (roll < 0x3cu) policy = 0x41;
        else                   policy = 0x32;
    }

    int counter1 = 0;  // count of op=1 assignments
    int counter2 = 0;  // count of op=2 assignments

    for (int i = 0; i < wi->m_TotalWeight; ++i) {
        int op;
        if (policy == -4) {
            policy = -3;
            op = 2;
        } else if (policy == -3) {
            policy = -4;
            op = 1;
        } else if (policy == 0x32) {
            int diff = counter1 - counter2;
            if (diff > 1)       op = 2;
            else if (diff < -1) op = 1;
            else                op = (rng.Rand32(2) == 0) ? 1 : 2;
        } else {
            uint32_t r2 = rng.Rand32(100);
            op = ((uint32_t)policy < r2) ? 1 : 2;
        }
        item.m_SlotList.push_back(op);
        if (op == 1) ++counter1; else ++counter2;
    }

    m_Items.push_back(item);
}

// WaveQue::PopWave — binary @ 0x00123258
bool WaveQue::PopWave(WaveQueItem* out) {
    if (m_Items.begin() == m_Items.end())
        return false;
    *out = *m_Items.begin();
    m_Items.erase(m_Items.begin());
    return true;
}

// WaveQue::RandomiseOrder — binary @ 0x00124464
// Alternately flips spawner-ops 1<->2 at every other list position (positions 0,2,4,...).
void WaveQue::RandomiseOrder(bool doSwap) {
    if (!doSwap) return;
    int pos = 0;
    for (std::list<WaveQueItem>::iterator it = m_Items.begin();
         it != m_Items.end(); ++it, ++pos) {
        if ((pos & 1) == 0) {
            for (std::vector<int>::iterator oit = it->m_SlotList.begin();
                 oit != it->m_SlotList.end(); ++oit) {
                if (*oit == 1)      *oit = 2;
                else if (*oit == 2) *oit = 1;
            }
        }
    }
}

// WaveQue::AddSpecials — binary @ 0x0012ce8c
// For each spawner-op in each item: with Rand32(100)<5 (or specials counter>=4),
// and if item.specialsCount<2, set op=3 (special).
// TODO: v1.6.1 0x0012ce8c (WaveQue::AddSpecials) — this is a port-invented
//   approximation; the binary has 3 extra CALL blocks. Faithful body is
//   blocked on the Combo/Survival wave-mode port (asm-verify HIGH, ACCEPT-deferred).
void WaveQue::AddSpecials() {
    Math::Random& rng = WaveManager::GetInstance()->m_Random;
    for (std::list<WaveQueItem>::iterator it = m_Items.begin();
         it != m_Items.end(); ++it) {
        int specialsCount = 0;
        for (std::vector<int>::iterator oit = it->m_SlotList.begin();
             oit != it->m_SlotList.end(); ++oit) {
            uint32_t r = rng.Rand32(100);
            if ((r < 5u || specialsCount >= 4) && specialsCount < 2) {
                *oit = 3;
                ++specialsCount;
            }
        }
    }
}

// WaveQueItem::PopPlayer — binary @ 0x0012cf6c.
// Pops front of m_SlotList into *out. Returns true if an item was available.
bool WaveQueItem::PopPlayer(int* out) {
    if (m_SlotList.empty()) return false;
    if (out) *out = m_SlotList.front();
    m_SlotList.erase(m_SlotList.begin());
    return true;
}

// WaveQueItem::PerformCatchup — binary @ 0x0012cdb0.
// If |leftCount - rightCount| > 5 and Rand32(100) < 60:
//   scan m_SlotList for the majority side's entry and randomly flip one.
// Returns 1 if it flipped, 0 otherwise.
// ASM-spec v1.6.1 WaveQueItem::PerformCatchup @ 0x0012cdb0: binary uses a local
// int[50] scratch buffer of candidate slot INDICES (sub sp,#0xc8=200B) then
// Rand32(count) picks one; port collapses collect+pick into count + re-scan-to-pick
// -- bit-identical (same RNG draws, same flip, same return). int[50] is local
// codegen only. findOp=(left>right)?2:1; left==right unreachable.
int WaveQueItem::PerformCatchup(int leftCount, int rightCount) {
    int diff = leftCount - rightCount;
    if (diff < 0) diff = -diff;
    if (diff <= 5) return 0;

    WaveManager* wm = WaveManager::GetInstance();
    if (!wm) return 0;
    if (wm->GetRandom().Rand32(100) >= 60u) return 0;

    // The side with more spawns is reduced; the other grows.
    // If leftCount < rightCount, the list has too many 2s; flip a 2->1.
    // If leftCount > rightCount, the list has too many 1s; flip a 1->2.
    int findOp  = (leftCount < rightCount) ? 2 : 1;
    int flipTo  = (leftCount < rightCount) ? 1 : 2;

    // Count candidates
    int candidateCount = 0;
    for (std::vector<int>::iterator it = m_SlotList.begin();
         it != m_SlotList.end(); ++it) {
        if (*it == findOp) ++candidateCount;
    }
    if (candidateCount == 0) return 0;

    int pick = (int)wm->GetRandom().Rand32((uint32_t)candidateCount);
    int seen = 0;
    for (std::vector<int>::iterator it = m_SlotList.begin();
         it != m_SlotList.end(); ++it) {
        if (*it == findOp) {
            if (seen == pick) {
                *it = flipTo;
                return 1;
            }
            ++seen;
        }
    }
    return 0;
}

WaveModifier::WaveModifier()
    : GameModifier()
    , m_OverrideCount(0)
    , m_BombMult(1.0f)
    , m_BombScale(1.0f)
    , m_FruitMult(1.0f)
    , m_DtMod(1.0f)
    , m_OverideProbabilityPool(10000)
    , m_CritChanceMod(1.0f)
{}

WaveModifier::~WaveModifier() {}

// @ 0x0015068c -- WaveModifier overrides GameModifier vtable slot [5]
// (OnDeferComplete, base @ 0x00140890). Ghidra mislabels this "ApplyModifier"
// because the body chains the base slot-5 function; the vtable proves it is the
// OnDeferComplete slot (WaveModifier vtable @ 0x2cc8b0 slot[5]=0x0015068c,
// base GameModifier vtable @ 0x2cc6d8 slot[5]=0x00140890=OnDeferComplete).
// Body (verbatim from binary):
//   (1) chain GameModifier::OnDeferComplete(unused, pExtra).
//   (2) if m_OverideProbabilityPool < 10000 && !unused(=isPurchased) &&
//       m_OverideProbabilityPool < WaveManager current-wave-counter:
//       SetCurrentWave(m_OverideProbabilityPool, -1.0f, 0).
//       Binary reads the counter at WaveManager+0x238 (confirmed via
//       SetCurrentWave @ 0x00125d1c writing (playerIdx+0x8e)*4 = 0x238 for P0);
//       the port maps that slot as m_WaveCount[0] per WaveManager.h's 64-bit
//       DIFFERS block, matching the rest of this file's convention.
//   (3) if m_OverideEntries empty, return.
//   (4) SelectType() each of the first m_OverrideCount override entries.
//   (5) insert the whole m_OverideEntries range into the WaveManager current
//       override list (GetCurrentOverideList(0) == m_ProbabilityOverride[gameMode]),
//       then clear m_OverideEntries.
void WaveModifier::OnDeferComplete(bool unused, float* pExtra) {
    GameModifier::OnDeferComplete(unused, pExtra);

    if (m_OverideProbabilityPool < 10000 && !unused) {
        WaveManager* w = WaveManager::GetInstance();
        if (m_OverideProbabilityPool < w->m_WaveCount[0]) {
            WaveManager::GetInstance()->SetCurrentWave(m_OverideProbabilityPool, -1.0f, 0);
        }
    }

    if (m_OverideEntries.empty()) {
        return;
    }

    for (int i = 0; i < m_OverrideCount &&
                    i < (int)m_OverideEntries.size(); ++i) {
        m_OverideEntries[i].SelectType();
    }

    std::vector<PROBABILITY_OVERIDE>& dst =
        WaveManager::GetInstance()->m_ProbabilityOverride[game_work.gameMode];
    dst.insert(dst.end(), m_OverideEntries.begin(), m_OverideEntries.end());
    m_OverideEntries.clear();
}

// Binary @ 0x001282d4. After chaining base ApplyModifier:
//   (1) if m_OverideProbabilityPool <= 9999 (i.e. < 10000) && !isPurchased &&
//       m_OverideProbabilityPool < WaveManager current wave, rewind via
//       SetCurrentWave(m_OverideProbabilityPool, -1.0f, 0).
//   (2) call SelectType() on every m_OverideEntries entry, append them all into
//       the WaveManager's current override list (GetCurrentOverideList(0)), then
//       clear m_OverideEntries.
// Binary reads WaveManager+0x230 as the current wave counter; in SP that slot is
// m_WaveCount[0] (see WaveManager.h +0x230 dual-purpose note).
void WaveModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);

    if (m_OverideProbabilityPool < 10000 && !isPurchased) {
        WaveManager* w = WaveManager::GetInstance();
        if (m_OverideProbabilityPool < w->m_WaveCount[0]) {
            WaveManager::GetInstance()->SetCurrentWave(m_OverideProbabilityPool, -1.0f, 0);
        }
    }

    for (std::vector<PROBABILITY_OVERIDE>::iterator it = m_OverideEntries.begin();
         it != m_OverideEntries.end(); ++it) {
        it->SelectType();
    }

    // Append the (now type-selected) override entries into the WaveManager's
    // current override list. The binary's GetCurrentOverideList(0) returns the
    // vector header at m_ProbabilityOverride[gameMode] (playerIdx 0 = primary
    // slot); insert the whole m_OverideEntries range, then clear it.
    std::vector<PROBABILITY_OVERIDE>& dst =
        WaveManager::GetInstance()->m_ProbabilityOverride[game_work.gameMode];
    dst.insert(dst.end(), m_OverideEntries.begin(), m_OverideEntries.end());
    m_OverideEntries.clear();
}

// Binary @ 0x00128128. If the WaveManager current wave counter (+0x230 = SP
// m_WaveCount[0]) is < 0 AND m_OverideProbabilityPool <= that counter, reset the
// wave to SetCurrentWave(5, 0.25f, 0).
void WaveModifier::RemoveModifier() {
    WaveManager* w = WaveManager::GetInstance();
    if (w->m_WaveCount[0] < 0 &&
        m_OverideProbabilityPool <= WaveManager::GetInstance()->m_WaveCount[0]) {
        WaveManager::GetInstance()->SetCurrentWave(5, 0.25f, 0);
    }
}

// @ 0x001280e0 -- empty override in binary (no specific reset work); no-op is faithful.
void WaveModifier::ResetSpecific() {}

// @ 0x001280e4
int WaveModifier::UpdateSpecific(float /*dt*/) {
    WaveManager*    w = WaveManager::GetInstance();
    PowerUpManager* p = PowerUpManager::GetInstance();
    w->FruitMultiplyer(m_FruitMult);
    w->BombMultiplyer(m_BombMult);
    w->BombScale(m_BombScale);
    w->CriticalChanceMod(m_CritChanceMod);
    p->PowerupDtModMultiply(m_DtMod);
    return 0;
}

// ASM-spec v1.6.1 WaveModifier::ParseSpecific @ 0x00150768 —
// (1) 6 QueryAttribute calls: fruitMultiplyer→m_Params[2], bombMultiplyer→m_Params[0],
//     bombScale→m_Params[1], criticalChance→m_Param5, powerUpDtMod→m_Params[3],
//     waveOveride→m_TargetWave
// (2) Loop: TiXmlNode::FirstChildElement("OverideProbability") on the base xml node,
//     then iterate via TiXmlNode::NextSiblingElement.
// (3) Per-iteration: construct PROBABILITY_OVERIDE on stack, Parse it,
//     m_OverrideCount+=1, push_back into m_Overrides, destruct.
void WaveModifier::ParseSpecific(TiXmlElement* xml) {
    xml->QueryFloatAttribute("fruitMultiplyer", &m_FruitMult);
    xml->QueryFloatAttribute("bombMultiplyer",  &m_BombMult);
    xml->QueryFloatAttribute("bombScale",       &m_BombScale);
    xml->QueryFloatAttribute("criticalChance",  &m_CritChanceMod);
    xml->QueryFloatAttribute("powerUpDtMod",    &m_DtMod);
    xml->QueryIntAttribute  ("waveOveride",     &m_OverideProbabilityPool);

    for (TiXmlElement child = xml->FirstChildElement("OverideProbability");
         child;
         child = child.NextSiblingElement("OverideProbability")) {
        PROBABILITY_OVERIDE tmp;
        tmp.Parse(&child);
        m_OverrideCount = m_OverrideCount + 1;
        m_OverideEntries.push_back(tmp);
    }
}

GameModifier* WaveModifier::Clone() {
    WaveModifier* c = new WaveModifier();
    *c = *this;
    c->m_bConfigured = false;
    return c;
}
