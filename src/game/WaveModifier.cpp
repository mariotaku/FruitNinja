// Analysed: 2026-05-03T00:00

#include "WaveModifier.h"
#include "PowerUpManager.h"
#include "WaveManager.h"
#include "WaveStructs.h"
#include "ItemParseUtil.h"
#include "entities/Fruit.h"
#include "util/StringHash.h"
#include <tinyxml2.h>

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

// PROBABILITY_OVERIDE::Parse — binary @ 0x001231d8
void PROBABILITY_OVERIDE::Parse(tinyxml2::XMLElement* xml) {
    xml->QueryIntAttribute("percentageChance",     &m_PercentChance);
    xml->QueryIntAttribute("waveCount",            &m_PerWaveCount);
    const char* typesAttr = xml->Attribute("types");
    if (typesAttr) {
        WaveManager::SplitWords(typesAttr, m_Types);
    }
    xml->QueryIntAttribute("perWave",              &m_PerWave);
    xml->QueryFloatAttribute("disableWhenPowered", &m_DisableWhenPowered);
    xml->QueryIntAttribute("numWaves",             &m_SelectedType);
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
