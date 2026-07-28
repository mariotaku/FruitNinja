// Analysed: 2026-05-03T00:00

#include "WaveModifier.h"
#include "PowerUpManager.h"
#include "WaveManager.h"
#include "WaveStructs.h"
#include "GameWork.h"
#include "ItemParseUtil.h"
#include "engine/network/P2PMessageHandling.h"
#include "entities/Fruit.h"
#include "util/StringHash.h"
#include "math/Random.h"
#include <algorithm>
#include <cstdlib>

// ASM-spec v1.6.1 SPAWNER_INFO::SelectTypes @0x0012dcc8 (thunk veneer @0x00114654).
// Re-rolls fruit-type indices from string-name vector.
// m_FruitTypeNames holds one name per fruit slot (from XML "type" attr, split by SplitWords).
// "bomb"/"Bomb" (case-insensitive) -> -2 (bomb sentinel); "1fruit" (case-insensitive) ->
// Fruit::RandomFruit(false) resolved once here (fixed for the spawner until next Reset);
// else Fruit::FruitType(name,false) lookup (returns -1 for names not in fruitlist.xml,
// e.g. "random", which is the deliberate sentinel routed into WaveManager::UpdateWave's
// per-spawn override/blitz-selection path).
void SPAWNER_INFO::SelectTypes() {
    static const uint32_t kHashBomb     = StringHash("BOMB");
    static const uint32_t kHashOneFruit = StringHash("1fruit");
    for (int i = 0; i < m_FruitTypeCount; ++i) {
        m_pFruitTypeHashes[i] = -1;
        uint32_t h = StringHash(m_FruitTypeNames[i].c_str());
        if (h == kHashBomb)
            m_pFruitTypeHashes[i] = -2;
        else if (h == kHashOneFruit)
            m_pFruitTypeHashes[i] = Fruit::RandomFruit(false);
        else
            m_pFruitTypeHashes[i] = Fruit::FruitType(m_FruitTypeNames[i].c_str(), false);
    }
}

// ASM-verified: 2026-07-06 v1.6.1 SPAWNER_INFO::GetRandCount @0x0012df30 (re-analyst).
// Random spawn count in [lo,hi]. lo/hi grow with the wave revisit counter; the low
// bound uses m_MinGrowthInc (+0x3c, unwritten by Init -> 0), the high bound m_GrowthInc (+0x44).
int SPAWNER_INFO::GetRandCount(float waveRevisitCounter) {
    int lo = (int)(m_SpawnMin + waveRevisitCounter * m_MinGrowthInc);
    int hi = (int)(m_SpawnMax + waveRevisitCounter * m_GrowthInc);
    int range = hi - lo;
    if (range < 1) return lo;
    return lo + (int)WaveManager::GetInstance()->GetRandom().Rand32((uint32_t)range);
}

// ASM-verified: 2026-07-06 v1.6.1 SPAWNER_INFO::ResetDelay @0x0012dfa0 (re-analyst).
// m_SpawnTimer = max(0, m_Delay + revisit*m_DelayInc). The revisit term is ADDED (vmla).
void SPAWNER_INFO::ResetDelay(float waveRevisitCounter) {
    float t = m_Delay + waveRevisitCounter * m_DelayInc;
    if (t < 0.0f) t = 0.0f;
    m_SpawnTimer = t;
}

// ASM-verified: 2026-07-06 v1.6.1 SPAWNER_INFO::Reset @0x0012dfc8 (re-analyst).
void SPAWNER_INFO::Reset(float waveRevisitCounter) {
    float count = (float)GetRandCount(waveRevisitCounter);
    m_reserved58     = 0;
    m_RemainingCount = (int)count;
    m_SpawnCountF    = count;
    SelectTypes();
    ResetDelay(waveRevisitCounter);
}

// ASM-spec v1.6.1 PROBABILITY_OVERIDE::SelectType @0x00121000.
// Populates m_TypeQueue[] from m_Types string names.
// Same two lazy-init guarded statics as SelectTypes above (Bomb / 1fruit hashes).
void PROBABILITY_OVERIDE::SelectType() {
    static const uint32_t kHashBomb     = StringHash("Bomb");
    static const uint32_t kHashOneFruit = StringHash("1fruit");
    int n = (int)m_Types.size();
    for (int i = 0; i < n && i < 20; ++i) {
        uint32_t h = StringHash(m_Types[i].c_str());
        if (h == kHashBomb)
            m_TypeQueue[i] = -2;
        else if (h == kHashOneFruit)
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

    // v1.6.1 PROBABILITY_OVERIDE::Parse @0x00121500: one <PowerAllowance
    // allowPercentage="N"> child per timed-power-count bucket.
    for (TiXmlElement child = xml->FirstChildElement("PowerAllowance");
         child; child = child.NextSiblingElement("PowerAllowance")) {
        int allowPercentage = 0;
        child.QueryIntAttribute("allowPercentage", &allowPercentage);
        m_PowerAllowance.push_back(allowPercentage);
    }
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
// WaveQue methods — v1.6.1 @0x0012d014 / 0x0012cfbc / 0x0012d1d0 / 0x0012ce8c
// Only used in gameMode==2 (Survival/Combo). Classic/Arcade never call these.
//
// REACHABILITY (v1.6.1): WaveManager::SetupWaveQue @0x00123458 is the sole caller
// of AddWave (6 call sites) and itself has ZERO xrefs in the image — only an
// exported-symbol entry, no bl and no data reference. So AddWave cannot execute in
// v1.6.1 and burns no Math::g_random draws today. Do NOT "revive" it as a live bug;
// it only matters if a caller is restored (the Combo/Survival wave-que mode —
// combowavelist.xml / survivalwavelist.xml do ship). Kept binary-faithful anyway
// because Rand32 draws off a shared global sequence: a wrong DRAW COUNT here would
// shift every later draw in the game the moment a caller returns.
// ----------------------------------------------------------------------------

// ASM-spec v1.6.1 WaveQue::AddWave @0x0012d014.
// The policy roll (Rand32(100) @0x0012d030..) is UNCONDITIONAL — one draw always.
// Per-slot draw counts, N = wi->m_TotalWeight, W = wi->m_WaveIndex:
//   isLast (policy -3/-4, alternating)  -> exactly 1 draw
//   policy in {5,95,35,65}              -> 1 + N
//   policy == 50 (the balance path)     -> 1 + max(0, N - max(0, W-1))
// Rand32's state advance is independent of its range argument, so the range only
// changes the value, never the count.
void WaveQue::AddWave(WaveInfo* wi, bool isLast) {
    Math::Random& rng = WaveManager::GetInstance()->m_Random;
    WaveQueItem item;
    item.m_Count0 = 0;
    item.m_Count1 = 0;
    item.m_Count2 = wi->m_WaveIndex;  // +0x68 seed; non-zero for every wave but #0
    item.m_Fraction = 0.5f;

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

    for (int i = 0; i < wi->m_TotalWeight; ++i) {
        int op;
        if (policy == -4) {
            policy = -3;
            op = 2;
        } else if (policy == -3) {
            policy = -4;
            op = 1;
        } else {
            // Balance path: while the two counters are more than one apart the side
            // that is behind is chosen outright — NO RNG draw. m_Count2 carries the
            // wave-index seed, so early waves skip up to W-1 draws.
            int diff = item.m_Count1 - item.m_Count2;
            if (diff < 0) diff = -diff;
            if (policy == 0x32 && diff > 1) {
                op = (item.m_Count1 < item.m_Count2) ? 1 : 2;
            } else {
                // Shared tail @0x0012d124 — also the tie case; there is no
                // separate tie handler and no Rand32(2) in the binary.
                uint32_t r2 = rng.Rand32(100);
                op = (r2 > (uint32_t)policy) ? 1 : 2;
            }
        }
        // Binary increments (&item.m_Count0)[op] BEFORE the push_back (0x0012d154-64).
        switch (op) {
            case 0: ++item.m_Count0; break;
            case 1: ++item.m_Count1; break;
            case 2: ++item.m_Count2; break;
        }
        item.m_SlotList.push_back(op);
    }

    // Only the non-alternating policies overwrite the 0.5f init.
    if (policy >= -1)
        item.m_Fraction = (float)item.m_Count1 / (float)wi->m_TotalWeight;

    m_Items.push_back(item);
}

// WaveQue::PopWave — v1.6.1 @0x0012cfbc
bool WaveQue::PopWave(WaveQueItem* out) {
    if (m_Items.begin() == m_Items.end())
        return false;
    *out = *m_Items.begin();
    m_Items.erase(m_Items.begin());
    return true;
}

// ASM-spec v1.6.1 WaveQue::RandomiseOrder @0x0012d1d0:
//  iterates a COPY of m_Items; for each item builds a mirrored duplicate via
//  copy-ctor (@0x0012da08: SlotList+Fraction+Count0/1/2, 0x1c bytes), flips every
//  slot op 1<->2 (0/3 untouched), swaps m_Count1<->m_Count2 (+0x14<->+0x18), and
//  inserts it into the REAL queue at index 0,2,4,... -> queue doubles to
//  [mirror0, orig0, mirror1, orig1, ...]. bool gates the whole thing; sole caller
//  SetupWaveQue @0x00123458 passes true. Originals are never mutated.
void WaveQue::RandomiseOrder(bool mirror) {
    if (!mirror) return;
    std::list<WaveQueItem> copy(m_Items);
    int insertPos = 0;
    for (std::list<WaveQueItem>::iterator it = copy.begin();
         it != copy.end(); ++it, insertPos += 2) {
        WaveQueItem m(*it);
        for (size_t i = 0; i < m.m_SlotList.size(); ++i) {
            if      (m.m_SlotList[i] == 1) m.m_SlotList[i] = 2;
            else if (m.m_SlotList[i] == 2) m.m_SlotList[i] = 1;
        }
        std::swap(m.m_Count1, m.m_Count2);
        // Binary walks insertPos nodes from begin(); the end-reached-first branch
        // @0x0012d2c8 is unreachable (index 2k < size n+k for k<n).
        std::list<WaveQueItem>::iterator ins = m_Items.begin();
        for (int step = 0; step < insertPos; ++step) ++ins;
        m_Items.insert(ins, m);
    }
}

// ASM-spec v1.6.1 WaveQue::AddSpecials @ 0x0012ce8c
// idleItems (binary r6) is a CROSS-ITEM counter declared outside the outer loop: it
// persists across all m_Items, is bumped once per item after the inner slot loop, and
// is reset to 0 only when a special is placed (anywhere). Per-item cap is
// it->m_SpecialsCount (WaveQueItem +0x1c, distinct from idleItems). On placement, the
// slot-selected counter (m_Count0/m_Count1/m_Count2) is decremented.
void WaveQue::AddSpecials() {
    Math::Random& rng = WaveManager::GetInstance()->m_Random;
    int idleItems = 0;
    for (std::list<WaveQueItem>::iterator it = m_Items.begin();
         it != m_Items.end(); ++it) {
        for (std::vector<int>::iterator oit = it->m_SlotList.begin();
             oit != it->m_SlotList.end(); ++oit) {
            uint32_t r = rng.Rand32(100);
            if ((r < 5u || idleItems > 4) && it->m_SpecialsCount < 2) {
                int slot = *oit;
                idleItems = 0;
                switch (slot) {
                    case 0: it->m_Count0--;    break;
                    case 1: it->m_Count1--;    break;
                    case 2: it->m_Count2--;    break;
                }
                *oit = 3;
                ++it->m_SpecialsCount;
            }
        }
        ++idleItems;
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

// v1.6.1 WaveModifier::ApplyModifier @0x0015068c is the ONE compiled function
// behind both of this class's virtual slots: it overrides GameModifier's slot 5
// (base body @0x00140890) and slot 8. That mirrors the base class, where
// 0x00140890 is likewise the single ICF-merged body serving OnDeferComplete and
// ApplyModifier (see GameModifier.cpp). There is no separate OnDeferComplete
// symbol in the binary; the port keeps the C++ method only so slot-5 virtual
// dispatch from GameModifier::Update reaches this body, and forwards.
void WaveModifier::OnDeferComplete(bool unused, float* pExtra) {
    ApplyModifier(unused, pExtra);
}

// v1.6.1 WaveModifier::ApplyModifier @0x0015068c. After chaining the base body:
//   (1) if m_OverideProbabilityPool < 10000 && !isPurchased &&
//       m_OverideProbabilityPool < WaveManager current wave, rewind via
//       SetCurrentWave(m_OverideProbabilityPool, -1.0f, 0).
//       Binary reads the counter at WaveManager+0x238 (confirmed via
//       SetCurrentWave @ 0x00125d1c writing (playerIdx+0x8e)*4 = 0x238 for P0);
//       the port maps that slot as m_WaveCount[0] per WaveManager.h's 64-bit
//       DIFFERS block, matching the rest of this file's convention.
//   (2) call SelectType() on every m_OverideEntries entry, then PREPEND them all
//       (dst.insert(dst.begin(), ...)) into the WaveManager's current override
//       list (GetCurrentOverideList(0) == m_ProbabilityOverride[gameMode]), then
//       clear m_OverideEntries. Front-insertion matters: UpdateWave's override
//       picker walks front-to-back and stops at the first cumulative-probability
//       match, so insert order picks the winner when multiple overrides are active.
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

    // Prepend the (now type-selected) override entries into the WaveManager's
    // current override list. The binary's GetCurrentOverideList(0) returns the
    // vector header at m_ProbabilityOverride[gameMode] (playerIdx 0 = primary
    // slot); dst.insert(dst.begin(), ...) -- binary-verified instruction-by-
    // instruction @0x0015068c -- inserts the whole m_OverideEntries range at the
    // FRONT of dst, then clears it. Front-insertion matters: WaveManager::UpdateWave's
    // override picker walks dst front-to-back and stops at the first cumulative-
    // probability match, so insertion order determines which override wins when
    // multiple are active.
    std::vector<PROBABILITY_OVERIDE>& dst =
        WaveManager::GetInstance()->m_ProbabilityOverride[game_work.gameMode];
    dst.insert(dst.begin(), m_OverideEntries.begin(), m_OverideEntries.end());
    m_OverideEntries.clear();
}

// ASM-verified: 2026-07-04T00:00:00Z v1.6.1 WaveModifier::RemoveModifier @ 0x00150590
// If the WaveManager current wave counter (m_WaveCount[0]) is < 0 AND
// m_OverideProbabilityPool <= that counter and we're not in online MP, reset the
// wave to SetCurrentWave(5, 0.25f, 0). Then, regardless of that gate, erase the
// FRONT m_OverrideCount entries from m_ProbabilityOverride[gameMode] -- undoing
// the PREPEND that ApplyModifier performed when the modifier
// was applied.
void WaveModifier::RemoveModifier() {
    WaveManager* w = WaveManager::GetInstance();
    if (w->m_WaveCount[0] < 0 &&
        m_OverideProbabilityPool <= WaveManager::GetInstance()->m_WaveCount[0] &&
        !IsOnlineMultiplayer()) {
        WaveManager::GetInstance()->SetCurrentWave(5, 0.25f, 0);
    }

    std::vector<PROBABILITY_OVERIDE>& dst =
        WaveManager::GetInstance()->m_ProbabilityOverride[game_work.gameMode];
    std::vector<PROBABILITY_OVERIDE>::iterator it = dst.begin();
    int count = 0;
    while (it != dst.end() && count < m_OverrideCount) {
        it = dst.erase(it);
        ++count;
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
