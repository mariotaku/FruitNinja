// Analysed: 2026-05-03T00:00

#include "PowerUpManager.h"
#include "PowerUp.h"
#include "Game.h"
#include "entities/SlashEntity.h"
#include "network/NetworkManager.h"
#include "util/StringHash.h"
#include "asset/TextureManager.h"
#include "math/Vec3.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstring>
#include <string>

// @ 0x00117d20
PowerUpManager::PowerUpManager()
    : m_field60(0)
    , m_DtMod(1.0f)
    , m_field68(0.0f)
    , m_field6c(1.0f)
    , m_field70(1.0f)
    , m_field74(1.0f)
    , m_ScoreGainMult(1)
    , m_ScoreGainFactor(1)
    , m_ScoreLossMult(1)
    , m_ScoreLossFactor(1)
    , m_field88(0.0f)
    , _pad8c(0)
{
    // Containers default-constructed by member initialisation.
    // Binary explicitly writes m_field70 = m_field74 = 1.0f (above).
    // Scalar fields +0x60..+0x88 are technically uninitialised in the
    // binary (bss zero covers it); port explicitly zeroes for safety.
}

PowerUpManager::~PowerUpManager() {
    // Range-for replaced with iterator form so GCC 4.4 (asm-verify cross
    // toolchain) can parse. Same semantics in both compilers.
    for (std::map<uint32_t, PowerUp*>::iterator it = m_AllPowerUps.begin();
         it != m_AllPowerUps.end(); ++it) {
        delete it->second;
    }
    m_AllPowerUps.clear();
    for (std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        delete *it;
    }
    m_ActivePowerUps.clear();
}

// @ 0x00117a80
void PowerUpManager::SetDefaults() {
    m_field88   = 0.0f;
    m_field68   = 0.0f;
    m_field60   = 0;
    m_field70   = 1.0f;
    m_DtMod     = 1.0f;
    m_field6c   = 1.0f;
    ClearScoreMultipliers();
    SlashEntity::s_ModPowerMask = 0;
    // TODO: reset SlashEntityState blade-width/colour-mod fields (binary @ 0x00117a80
    //       writes 6 float fields to 1.0 via g_pFruitNinjaApp->m_pBladeState)
    //       when SlashEntityState is ported, wire here.
}

// @ 0x0011a218
void PowerUpManager::ClearScoreMultipliers() {
    m_ScoreLossMult   = 1;
    m_ScoreGainFactor = 1;
    m_ScoreLossFactor = 1;
    m_ScoreGainMult   = 1;
}

// @ 0x001189b4
void PowerUpManager::Update(float dt) {
    // (1) Capture previous-frame's composite WaveModifier dt-mod.
    float prevWaveDtMod = m_field74;
    int   specialIdx    = 0;

    // (2) Reset all per-frame composite multipliers.
    SetDefaults();

    // (3) Tick every active PowerUp clone.
    auto it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        float perPowerDt = pwr->IsPurchaseable() ? dt : (dt * prevWaveDtMod);
        int expired = pwr->Update(perPowerDt);

        if (expired == 0) {
            float p = pwr->GetCurrentTimeProgress();
            if (p > m_field88) {
                if (!pwr->IsPurchaseable()) {
                    m_field88 = p;
                    m_field60 = (int)(intptr_t)pwr;
                } else if (m_field88 < 0.001f) {    // DAT_00118b90
                    m_field88 = 0.001f;
                }
            }
            int numTimed = GetNumActiveTimedPowers();
            float& xpos  = pwr->m_BarXPos;
            float target = (specialIdx * 110.0f)    // DAT
                         + ((numTimed - 1) * -55.0f);  // DAT_00118b94
            xpos += (target - xpos) * 0.2f;            // DAT_00118b98

            if (pwr->IsSpecial()) ++specialIdx;
            ++it;
        } else {
            // Power expired — deactivate + free.
            uint32_t hash = pwr->m_NameHash;
            auto byHash = m_ActiveByHash.find(hash);
            if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
            pwr->Deactivate(false);
            pwr->Release();
            delete pwr;
            it = m_ActivePowerUps.erase(it);
        }
    }

    // (4) Tick all active screen-effects.
    auto eit = m_ActiveScreenEffects.begin();
    while (eit != m_ActiveScreenEffects.end()) {
        eit->Update(dt, 0.0f, 0.0f);   // DAT_00118b9c = 0.0f
        if (eit->m_RemainingTime <= 0.0f) {
            eit->Deactivate();
            eit = m_ActiveScreenEffects.erase(eit);
        } else {
            ++eit;
        }
    }

    // (5) Carry composite WaveModifier dt-mod forward one frame.
    m_field74 = m_field70;
}

// @ 0x00119b08
void PowerUpManager::Reset(bool fullReset) {
    m_field88   = 0.0f;
    m_field68   = 0.0f;
    m_field60   = 0;
    m_field70   = 1.0f;
    m_field74   = 1.0f;
    m_DtMod     = 1.0f;
    m_field6c   = 1.0f;
    ClearScoreMultipliers();
    SlashEntity::s_ModPowerMask = 0;
    // TODO: reset SlashEntityState 6 blade-width/colour-mod fields to 1.0
    //       (binary @ 0x00119b08 via g_pFruitNinjaApp->m_pBladeState) when
    //       SlashEntityState is ported.

    if (fullReset) {
        Mortar::NetworkManager::GetInstance()->SyncClear();
    }

    std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (pwr->IsPurchaseable()) {
            pwr->Deactivate(true);
            if (!fullReset) {
                // ASM-verified: 2026-05-02 binary @ 0x00119bb0..0x00119bba -- check remaining-uses count
                if (!pwr->m_pPurchaseInfo || pwr->m_pPurchaseInfo->m_RemainingUses <= 0) {
                    uint32_t hash = pwr->m_NameHash;
                    std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
                    if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
                    pwr->Release();
                    delete pwr;
                    it = m_ActivePowerUps.erase(it);
                    continue;
                }
            } else {
                // Full reset: purchaseable powers are freed (re-activated below if zen)
                uint32_t hash = pwr->m_NameHash;
                std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
                if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
                pwr->Release();
                delete pwr;
                it = m_ActivePowerUps.erase(it);
                continue;
            }
            ++it;
            continue;
        }
        {
            uint32_t hash = pwr->m_NameHash;
            std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
            if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
            pwr->Deactivate(false);
            pwr->Release();
            delete pwr;
            it = m_ActivePowerUps.erase(it);
        }
    }

    ClearScreenEffects();

    // Zen mode (gameMode==2): re-activate all m_bIsSpecial powers from m_AllPowerUps.
    if (fullReset) {
        Game* game = Game::GetInstance();
        if (game && game->gameMode == 2) {
            for (std::map<uint32_t, PowerUp*>::iterator it2 = m_AllPowerUps.begin();
                 it2 != m_AllPowerUps.end(); ++it2) {
                PowerUp* tpl = it2->second;
                if (tpl && tpl->m_bIsSpecial) {
                    ActivatePower(tpl->m_NameHash);
                }
            }
        }
    }
}

// @ 0x00118904
void PowerUpManager::ClearTimedPowers() {
    m_field88 = 0.001f;    // DAT_001189b0
    m_field60 = 0;

    auto it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (!pwr->IsPurchaseable() && pwr->IsTimed()) {
            uint32_t hash = pwr->m_NameHash;
            auto byHash = m_ActiveByHash.find(hash);
            if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
            pwr->Deactivate(false);
            pwr->Release();
            delete pwr;
            it = m_ActivePowerUps.erase(it);
        } else {
            ++it;
        }
    }
}

// @ 0x001197c4
void PowerUpManager::ActivatePower(uint32_t hash) {
    std::map<uint32_t, PowerUp*>::iterator it = m_AllPowerUps.find(hash);
    if (it == m_AllPowerUps.end()) return;
    PowerUp* tpl = it->second;
    if (!tpl) return;

    // If already active, don't double-activate
    if (m_ActiveByHash.find(hash) != m_ActiveByHash.end()) return;

    PowerUp* clone = tpl->Clone();
    if (!clone) return;

    Vec3 zeroPos(0.0f, 0.0f, 0.0f);
    clone->Activate(false, zeroPos, 0.0f);
    m_ActivePowerUps.push_back(clone);
    m_ActiveByHash[hash] = clone;
}

// @ 0x00119760
bool PowerUpManager::ActivateScreenEffect(uint32_t hash) {
    auto it = m_ScreenEffectPool.find(hash);
    if (it == m_ScreenEffectPool.end()) return false;
    ScreenEffect copy(it->second);
    copy.Activate();
    m_ActiveScreenEffects.push_back(copy);
    return true;
}

// @ 0x00117ed8
void PowerUpManager::ClearScreenEffects() {
    for (std::list<ScreenEffect>::iterator it = m_ActiveScreenEffects.begin();
         it != m_ActiveScreenEffects.end(); ++it) it->Deactivate();
    m_ActiveScreenEffects.clear();
}

// @ 0x00119cb0
void PowerUpManager::Load() {
    // Build path: Data/xml/powerUpList.xml
    std::string path;
    const char* dataDir = Mortar::TextureManager::GetDataDir();
    if (dataDir && dataDir[0]) {
        path = dataDir;
        path += "/xml/powerUpList.xml";
    } else {
        path = "xml/powerUpList.xml";
    }

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.LoadFile(path.c_str());
    if (err != tinyxml2::XML_SUCCESS) {
        printf("PowerUpManager::Load -- failed to open '%s' (error %d)\n", path.c_str(), (int)err);
        return;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("powers");
    if (!root) {
        printf("PowerUpManager::Load -- no <powers> root in '%s'\n", path.c_str());
        return;
    }

    m_AllPowerUps.clear();
    m_PurchasablePowers.clear();

    for (tinyxml2::XMLElement* child = root->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        const char* tag = child->Name();
        if (!tag) continue;

        if (strcmp(tag, "powerup") == 0) {
            PowerUp* pu = new PowerUp();
            pu->Parse(child);
            if (pu->m_NameHash == 0) {
                delete pu;
                continue;
            }
            m_AllPowerUps[pu->m_NameHash] = pu;
            if (pu->m_bIsPurchasable) {
                m_PurchasablePowers.push_back(pu);
            }
        } else if (strcmp(tag, "effect") == 0) {
            const char* nameAttr = child->Attribute("name");
            if (!nameAttr) continue;
            uint32_t hash = StringHash(nameAttr);
            ScreenEffect& se = m_ScreenEffectPool[hash];
            se.m_NameHash = hash;
            se.Parse(child);
        }
    }
}

// @ 0x0011840c
void PowerUpManager::LoadTextures() {
    for (std::map<uint32_t, PowerUp*>::iterator it = m_AllPowerUps.begin();
         it != m_AllPowerUps.end(); ++it) it->second->LoadTextures();
    for (std::map<uint32_t, ScreenEffect>::iterator it = m_ScreenEffectPool.begin();
         it != m_ScreenEffectPool.end(); ++it) it->second.LoadTextures();
}

// @ 0x00119384
void PowerUpManager::Draw() {
    for (std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        (*it)->DrawBar();
    }
}

// @ 0x00117b38
float PowerUpManager::GetActiveProgression(float t) const {
    PowerUp* active = nullptr;
    for (std::list<PowerUp*>::const_iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        if ((*it)->IsSpecial()) active = *it;
    }
    if (!active) return 2.0f;
    if (t > 0.0f && active->m_TotalTime > 0.0f) {
        return (active->m_LongestRemaining - t) / active->m_TotalTime;
    }
    return active->GetCurrentTimeProgress();
}

// @ 0x00117cac
PowerUp* PowerUpManager::GetActiveSingle(uint32_t hash) {
    auto it = m_ActiveByHash.find(hash);
    return (it == m_ActiveByHash.end()) ? nullptr : it->second;
}

// @ 0x00117bb8
int PowerUpManager::GetNumActiveTimedPowers() const {
    int n = 0;
    for (std::list<PowerUp*>::const_iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        if ((*it)->IsSpecial()) ++n;
    }
    return n;
}
