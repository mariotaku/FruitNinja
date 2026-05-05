// Analysed: 2026-05-04T00:00

#include "PowerUpManager.h"
#include "PowerUp.h"
#include "GameModifier.h"
#include "Game.h"
#include "GameOver.h"
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
    : m_pActiveSpecial(0)
    , m_DtMod(1.0f)
    , m_StopClockAccum(0.0f)
    , m_SlowClockMult(1.0f)
    , m_WaveDtModCur(1.0f)
    , m_WaveDtModPrev(1.0f)
    , m_ScoreGainMult(1)
    , m_ScoreGainFactor(1)
    , m_ScoreLossMult(1)
    , m_ScoreLossFactor(1)
    , m_HighestActiveProgress(0.0f)
    , _pad8c(0)
{
    // Containers default-constructed by member initialisation.
    // Binary explicitly writes m_WaveDtModCur = m_WaveDtModPrev = 1.0f (above).
    // Scalar fields +0x60..+0x88 are technically uninitialised in the
    // binary (bss zero covers it); port explicitly zeroes for safety.
}

// @ 0x001187fc
PowerUpManager::~PowerUpManager() {
    Release();
}

// @ 0x00118724 — drain all containers; called by dtor
void PowerUpManager::Release() {
    // 1. Free every active clone.
    for (std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        PowerUp* p = *it;
        p->Release();
        delete p;
        *it = 0;  // mirror binary's `*ptr = 0`
    }
    // (binary does NOT call m_ActivePowerUps.clear() here — list dtor handles nodes)

    // 2. Free every template in the all-powers map.
    for (std::map<uint32_t, PowerUp*>::iterator it = m_AllPowerUps.begin();
         it != m_AllPowerUps.end(); ++it) {
        PowerUp* p = it->second;
        p->Release();
        delete p;
        it->second = 0;
    }

    // 3. Deactivate every screen effect in the pool, then clear pool + active list.
    for (std::map<uint32_t, ScreenEffect>::iterator it = m_ScreenEffectPool.begin();
         it != m_ScreenEffectPool.end(); ++it) {
        it->second.Deactivate();
    }
    m_ScreenEffectPool.clear();
    m_ActiveScreenEffects.clear();
}

// @ 0x00117a80
void PowerUpManager::SetDefaults() {
    m_HighestActiveProgress = 0.0f;
    m_StopClockAccum        = 0.0f;
    m_pActiveSpecial        = 0;
    m_WaveDtModCur          = 1.0f;
    m_DtMod                 = 1.0f;
    m_SlowClockMult         = 1.0f;
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
    float prevWaveDtMod = m_WaveDtModPrev;
    int   specialIdx    = 0;

    // (2) Reset all per-frame composite multipliers.
    SetDefaults();

    // (3) Tick every active PowerUp clone.
    std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        float perPowerDt = pwr->IsPurchaseable() ? dt : (dt * prevWaveDtMod);
        int expired = pwr->Update(perPowerDt);

        if (expired == 0) {
            float p = pwr->GetCurrentTimeProgress();
            if (p > m_HighestActiveProgress) {
                if (!pwr->IsPurchaseable()) {
                    m_HighestActiveProgress = p;
                    m_pActiveSpecial = pwr;
                } else if (m_HighestActiveProgress < 0.001f) {    // DAT_00118b90
                    m_HighestActiveProgress = 0.001f;
                }
            }
            int numTimed = GetNumActiveTimedPowers();
            float& xpos  = pwr->m_BarXPos;
            float target = (specialIdx * 110.0f)       // DAT
                         + ((numTimed - 1) * -55.0f);  // DAT_00118b94
            xpos += (target - xpos) * 0.2f;            // DAT_00118b98

            if (pwr->IsSpecial()) ++specialIdx;
            ++it;
        } else {
            // Power expired — deactivate + free.
            uint32_t hash = pwr->m_NameHash;
            std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
            if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
            pwr->Deactivate(false);
            pwr->Release();
            delete pwr;
            it = m_ActivePowerUps.erase(it);
        }
    }

    // (4) Tick all active screen-effects.
    std::list<ScreenEffect>::iterator eit = m_ActiveScreenEffects.begin();
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
    m_WaveDtModPrev = m_WaveDtModCur;
}

// @ 0x00119b08
void PowerUpManager::Reset(bool fullReset) {
    m_HighestActiveProgress = 0.0f;
    m_StopClockAccum        = 0.0f;
    m_pActiveSpecial        = 0;
    m_WaveDtModCur          = 1.0f;
    m_WaveDtModPrev         = 1.0f;     // also reset here, unlike SetDefaults
    m_DtMod                 = 1.0f;
    m_SlowClockMult         = 1.0f;
    ClearScoreMultipliers();
    SlashEntity::s_ModPowerMask = 0;
    // TODO: reset SlashEntityState 6 blade-width/colour-mod fields to 1.0
    //       (binary @ 0x00119b08 via g_pFruitNinjaApp->m_pBladeState) when
    //       SlashEntityState is ported.

    if (fullReset) {
        Mortar::NetworkManager::GetInstance()->SyncClear();  // Defunct: online MP
    }

    std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (pwr->IsPurchaseable()) {
            pwr->Deactivate(true);
            if (fullReset) {
                // Full reset: call ActivatePurchase to re-arm the power from template.
                // Binary @ 0x00119ba6 calls PowerUp::ActivatePurchase (method on PowerUp,
                // not PowerUpManager::ActivatePurchase); deferred to PowerUp RE pass.
                // TODO: 0x00119ba6 — call pwr->ActivatePurchase() when PowerUp::ActivatePurchase
                //       is ported (separate RE pass, method on PowerUp not PowerUpManager).
                //       For now PowerUpManager::ActivatePurchase re-arms via template clone path.
                ActivatePurchase(pwr);
                ++it;
                continue;
            } else {
                // Non-fullReset: keep if remaining uses > 0, discard otherwise.
                if (!pwr->m_pPurchaseInfo || pwr->m_pPurchaseInfo->m_RemainingUses < 1) {
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
        }
        // Non-purchaseable: always erase.
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

    // Zen mode (gameMode==2 + fullReset): re-activate all m_bIsSpecial templates.
    if (fullReset) {
        Game* game = Game::GetInstance();
        if (game && game->gameMode == 2) {
            for (std::map<uint32_t, PowerUp*>::iterator it2 = m_AllPowerUps.begin();
                 it2 != m_AllPowerUps.end(); ++it2) {
                PowerUp* tpl = it2->second;
                if (tpl && tpl->m_bIsSpecial) {
                    Vec3 zero(0.0f, 0.0f, 0.0f);
                    ActivatePower(tpl->m_NameHash, &zero, NULL);
                }
            }
        }
    }
}

// @ 0x00118904
void PowerUpManager::ClearTimedPowers() {
    m_HighestActiveProgress = 0.001f;    // DAT_001189b0
    m_pActiveSpecial = 0;

    std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (!pwr->IsPurchaseable() && pwr->IsTimed()) {
            uint32_t hash = pwr->m_NameHash;
            std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
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
PowerUp* PowerUpManager::ActivatePower(uint32_t hash, Vec3* position, float* purchaseExtra) {
    std::map<uint32_t, PowerUp*>::iterator it = m_AllPowerUps.find(hash);
    if (it == m_AllPowerUps.end()) return 0;

    std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
    PowerUp* clone = 0;

    if (byHash != m_ActiveByHash.end()) {
        // (A) Already active -> re-activate same instance with new position/extra.
        PowerUp* existing = byHash->second;
        existing->GetLongestMod();   // observed call in binary; result discarded
        Vec3 posCopy(*position);
        // TODO: PowerUp::Activate signature widening pending separate RE pass.
        // Binary calls Activate(existing, false, (purchaseExtra != null), posCopy, &posCopy.x)
        // (5-arg form). Port has 3-arg form; using it until Activate is widened.
        existing->Activate((purchaseExtra != NULL), posCopy, purchaseExtra ? *purchaseExtra : 0.0f);
        clone = existing;
    } else {
        // (B) Not yet active -> clone template, push back, decide path.
        PowerUp* tpl = it->second;
        clone = tpl->Clone();
        m_ActivePowerUps.push_back(clone);

        int numActiveSpecials = GetNumActiveTimedPowers();
        bool skipPurge =
            (numActiveSpecials == 0)
            || clone->m_bIsSpecial
            || clone->IsPurchaseable()
            || (purchaseExtra != NULL);

        if (skipPurge) {
            // (B1) Single activate, don't purge other specials.
            Vec3 posCopy(*position);
            // TODO: PowerUp::Activate signature widening pending separate RE pass.
            clone->Activate((purchaseExtra != NULL), posCopy, purchaseExtra ? *purchaseExtra : 0.0f);
        } else {
            // (B2) Purge-other-specials path.
            // Note: shortestTime computation below is in the binary but the result is
            // discarded (leftover from a refactor). Replicate exactly.
            float shortestTime = clone->GetLongestMod();
            for (std::list<PowerUp*>::iterator pit = m_ActivePowerUps.begin();
                 pit != m_ActivePowerUps.end(); ++pit) {
                if ((*pit)->IsSpecial()) {
                    float lm = (*pit)->GetLongestMod();
                    if (lm < shortestTime) shortestTime = lm;
                }
            }
            (void)shortestTime;  // binary discards this; suppress unused-variable warning

            // Activate(false, false, ...) on every other special.
            Vec3 ghostPos(0.0f, 0.0f, 0.0f);  // DAT_001199d0 canonical zero Vec3
            for (std::list<PowerUp*>::iterator pit = m_ActivePowerUps.begin();
                 pit != m_ActivePowerUps.end(); ++pit) {
                if ((*pit)->IsSpecial() && *pit != clone) {
                    Vec3 gp(ghostPos);
                    // TODO: PowerUp::Activate signature widening pending separate RE pass.
                    (*pit)->Activate(false, gp, 0.0f);
                }
            }
            Vec3 posCopy(*position);
            // TODO: PowerUp::Activate signature widening pending separate RE pass.
            clone->Activate(false, posCopy, 0.0f);
        }

        // (B-tail) Assign Y-position for HUD bar.
        int n = GetNumActiveTimedPowers();
        clone->m_BarXPos = (float)n * 110.0f;   // DAT_001199c8 = 110.0f

        if (clone->m_bIsPurchasable) {
            m_ActiveByHash[hash] = clone;
        }
    }
    return clone;
}

// @ 0x001193d0
void PowerUpManager::ActivatePurchase(PowerUp* p) {
    p->Deactivate(true);
    uint32_t hash = p->m_NameHash;
    std::map<uint32_t, PowerUp*>::iterator it = m_AllPowerUps.find(hash);
    if (it != m_AllPowerUps.end()) {
        PowerUp* tpl = it->second;
        // Walk template's mod list, clone each, attach to p.
        for (std::list<GameModifier*>::iterator mit = tpl->ModListBegin();
             mit != tpl->ModListEnd(); ++mit) {
            GameModifier* mClone = (*mit)->Clone();
            p->AddModifier(mClone);
        }
        // Apply any newly-attached, not-yet-applied mods.
        for (std::list<GameModifier*>::iterator mit = p->ModListBegin();
             mit != p->ModListEnd(); ++mit) {
            if (!(*mit)->m_bApplied) {
                (*mit)->ApplyModifier(false, NULL);  // vtable slot 5; isPurchase=false
            }
        }
    }
    --p->m_pPurchaseInfo->m_RemainingUses;
}

// @ 0x00119760
bool PowerUpManager::ActivateScreenEffect(uint32_t hash) {
    std::map<uint32_t, ScreenEffect>::iterator it = m_ScreenEffectPool.find(hash);
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

// @ 0x00117df8
void PowerUpManager::SaveActivePowerUps(TiXmlElement* parent) {
    for (std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        PowerUp* p = *it;
        tinyxml2::XMLElement* el = parent->GetDocument()->NewElement("active");
        el->SetAttribute("name",        p->m_Name);
        el->SetAttribute("currentTime", (double)p->m_LongestRemaining);
        el->SetAttribute("totalTime",   (double)p->m_TotalTime);
        el->SetAttribute("onScreenAmt", (double)p->m_BarRamp);
        if (p->m_DeferredPoints >= 0) {
            el->SetAttribute("score",   (double)p->m_DeferredPoints);
        }
        parent->LinkEndChild(el);
    }
}

// @ 0x001199d4
void PowerUpManager::LoadActivePowerUps(TiXmlElement* parent, int gameMode) {
    for (tinyxml2::XMLElement* el = parent->FirstChildElement("active");
         el; el = el->NextSiblingElement("active")) {
        float curTime = 0.0f;
        el->QueryFloatAttribute("currentTime", &curTime);
        const char* nameStr = el->Attribute("name");
        if (!nameStr) continue;
        uint32_t hash = StringHash(nameStr);

        std::map<uint32_t, PowerUp*>::iterator tplIt = m_AllPowerUps.find(hash);
        if (tplIt == m_AllPowerUps.end()) continue;
        PowerUp* tpl = tplIt->second;

        bool skip;
        if (!tpl->IsSpecial() && tpl->m_bIsSpecial == 0) {
            skip = false;
        } else if (gameMode == 2) {
            skip = false;
        } else {
            skip = true;
        }
        if (skip) continue;

        Vec3 pos(0.0f, 0.0f, 0.0f);
        PowerUp* p = ActivatePower(hash, &pos, &curTime);
        if (!p) continue;
        p->SetCurrentTime(curTime);
        float tmp = curTime;
        el->QueryFloatAttribute("totalTime",   &tmp); p->SetTotalTime(tmp);
        tmp = curTime;
        el->QueryFloatAttribute("onScreenAmt", &tmp); p->SetOnScreenAmt(tmp);
        int score = -1;
        el->QueryIntAttribute("score", &score);
        if (score >= 0) {
            FN::AddToCurrentScore(score, 0, false, false);
        }
    }
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
float PowerUpManager::GetActiveProgression(float t) {
    PowerUp* active = 0;
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
    std::map<uint32_t, PowerUp*>::iterator it = m_ActiveByHash.find(hash);
    return (it == m_ActiveByHash.end()) ? 0 : it->second;
}

// @ 0x00117bb8
int PowerUpManager::GetNumActiveTimedPowers() {
    int n = 0;
    for (std::list<PowerUp*>::const_iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        if ((*it)->IsSpecial()) ++n;
    }
    return n;
}

// @ 0x00118134
PowerUpManager* PowerUpManager::GetInstance() {
    static PowerUpManager s_instance;
    return &s_instance;
}

// @ 0x00117a70
void PowerUpManager::StopClock(float duration) {
    m_StopClockAccum += duration;
}

// ---- STUBS (binary) ----
// STUB: PowerUpManager::GetFirstPurchasable -- binary @ 0x???? (TODO RE)
void PowerUpManager::GetFirstPurchasable(std::_List_iterator<PowerUp*>&) {}
// STUB: PowerUpManager::GetNextPurchasable -- binary @ 0x???? (TODO RE)
void PowerUpManager::GetNextPurchasable(std::_List_iterator<PowerUp*>&) {}
// STUB: PowerUpManager::SetAppropriateScoreCallback -- binary @ 0x???? (TODO RE)
void PowerUpManager::SetAppropriateScoreCallback() {}
// STUB: PowerUpManager::UnloadTextures -- binary @ 0x???? (TODO RE)
void PowerUpManager::UnloadTextures() {}
// STUB: PowerUpManager::ActivatePower -- binary @ 0x001197c4 (TODO RE) -- binary by-value Vec3 overload
PowerUp* PowerUpManager::ActivatePower(unsigned long hash, Vec3 position, float* purchaseExtra) { return 0; }
// ---- end STUBS ----
