// Analysed: 2026-05-03T00:00

#include "WaveModifier.h"
#include "PowerUpManager.h"
#include "WaveManager.h"
#include "WaveStructs.h"
#include "ItemParseUtil.h"
#include "entities/Fruit.h"
#include "util/StringHash.h"
#include "math/Random.h"
#include <tinyxml2.h>
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
void PROBABILITY_OVERIDE::Parse(tinyxml2::XMLElement* xml) {
    xml->QueryIntAttribute("percentageChance",     &m_PercentChance);
    xml->QueryIntAttribute("waveCount",            &m_PerWaveCount);
    const char* typesAttr = xml->Attribute("types");
    if (typesAttr) {
        m_field68 = WaveManager::SplitWords(typesAttr, m_Types);
    }
    xml->QueryIntAttribute("perWave",              &m_PerWave);
    xml->QueryFloatAttribute("disableWhenPowered", &m_DisableWhenPowered);
    xml->QueryIntAttribute("numWaves",             &m_SelectedType);
}

// PROBABILITY_OVERIDE::GetType — binary @ 0x001217e4
// Picks a random entry from m_TypeQueue[0..m_field68).
// SelectType() is called once at Reset/NewGame; GetType() is called per-spawn.
// ASM-verified: 2026-05-18 binary @ 0x001217e4 (re-analyst)
int PROBABILITY_OVERIDE::GetType() {
    if (m_field68 <= 0) return -1;
    uint32_t idx = WaveManager::GetInstance()->GetRandom().Rand32((uint32_t)m_field68);
    return m_TypeQueue[idx];
}

// ----------------------------------------------------------------------------
// WaveQue methods — binary @ 0x00124334 / 0x00123258 / 0x00124464 / 0x00121b20
// Only used in gameMode==2 (Survival/Combo). Classic/Arcade never call these.
// ----------------------------------------------------------------------------

// WaveQue::AddWave — binary @ 0x00124334
void WaveQue::AddWave(WAVE_INFO* wi, bool isLast, Math::Random& rng) {
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
        item.m_SpawnerOps.push_back(op);
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
            for (std::vector<int>::iterator oit = it->m_SpawnerOps.begin();
                 oit != it->m_SpawnerOps.end(); ++oit) {
                if (*oit == 1)      *oit = 2;
                else if (*oit == 2) *oit = 1;
            }
        }
    }
}

// WaveQue::AddSpecials — binary @ 0x00121b20
// For each spawner-op in each item: with Rand32(100)<5 (or specials counter>=4),
// and if item.specialsCount<2, set op=3 (special).
void WaveQue::AddSpecials(Math::Random& rng) {
    for (std::list<WaveQueItem>::iterator it = m_Items.begin();
         it != m_Items.end(); ++it) {
        int specialsCount = 0;
        for (std::vector<int>::iterator oit = it->m_SpawnerOps.begin();
             oit != it->m_SpawnerOps.end(); ++oit) {
            uint32_t r = rng.Rand32(100);
            if ((r < 5u || specialsCount >= 4) && specialsCount < 2) {
                *oit = 3;
                ++specialsCount;
            }
        }
    }
}

WaveModifier::WaveModifier()
    : GameModifier()
    , m_BombMult(1.0f)
    , m_BombScale(1.0f)
    , m_FruitMult(1.0f)
    , m_DtMod(1.0f)
    , m_OverideProbabilityPool(0)
    , m_CritChanceMod(0.0f)
{}

WaveModifier::~WaveModifier() {}

// STUB: WaveModifier::ApplyModifier -- binary @ 0x???? (TODO RE)
void WaveModifier::ApplyModifier(bool isPurchased, float* extra) {
    GameModifier::ApplyModifier(isPurchased, extra);
}

// STUB: WaveModifier::RemoveModifier -- binary @ 0x???? (TODO RE)
void WaveModifier::RemoveModifier() {}

// STUB: WaveModifier::ResetSpecific -- binary @ 0x???? (TODO RE)
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

// @ 0x0012836c
// ASM-verified: 2026-05-03T00:00 binary @ 0x0012836c..0x00128424 (asm-inspector)
void WaveModifier::ParseSpecific(TiXmlElement* xml) {
    xml->QueryFloatAttribute("fruitMultiplyer", &m_FruitMult);
    xml->QueryFloatAttribute("bombMultiplyer",  &m_BombMult);
    xml->QueryFloatAttribute("bombScale",       &m_BombScale);
    xml->QueryFloatAttribute("criticalChance",  &m_CritChanceMod);
    xml->QueryFloatAttribute("powerUpDtMod",    &m_DtMod);
    xml->QueryIntAttribute  ("waveOveride",     &m_OverideProbabilityPool);

    for (TiXmlElement* c = xml->FirstChildElement("OverideProbability"); c;
         c = c->NextSiblingElement("OverideProbability")) {
        PROBABILITY_OVERIDE tmp;
        tmp.Parse(c);
        m_OverideEntries.push_back(tmp);
    }
}

GameModifier* WaveModifier::Clone() {
    WaveModifier* c = new WaveModifier();
    *c = *this;
    c->m_OverideEntries.clear();
    return c;
}
