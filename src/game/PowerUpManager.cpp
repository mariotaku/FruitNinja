// Analysed: 2026-04-30T00:00

#include "PowerUpManager.h"
#include "PowerUp.h"
#include <cstdio>

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
    // Free all templates in m_AllPowerUps.
    for (auto& kv : m_AllPowerUps) delete kv.second;
    m_AllPowerUps.clear();
    // Active clones should have been freed by Reset/ClearTimedPowers already.
    for (PowerUp* p : m_ActivePowerUps) delete p;
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
    // TODO: clear global slash-power mask: *(uint32_t*)(GOT + 0x7740) = 0
    // TODO: reset SlashEntityState blade-width/colour-mod fields via g_GameData
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
            float& xpos  = pwr->field_0xc8;
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
        if (eit->m_Lifetime <= 0.0f) {
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
    // TODO: clear global slash mask, reset SlashEntityState

    if (fullReset) {
        // TODO: (*game->m_pNetMgr->vtable[4])() - NetworkManager::SyncClear
    }

    auto it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (pwr->IsPurchaseable()) {
            pwr->Deactivate(true);
            if (fullReset) {
                // TODO: ActivatePurchase(pwr) -- re-apply purchase-active modifier
            } else if (pwr->m_pPurchaseInfo == nullptr) {
                // No purchases remaining path -- fall through to deactivate+free
                goto deactivate_and_free;
            }
            ++it;
            continue;
        }
        deactivate_and_free:
        {
            uint32_t hash = pwr->m_NameHash;
            auto byHash = m_ActiveByHash.find(hash);
            if (byHash != m_ActiveByHash.end()) m_ActiveByHash.erase(byHash);
            pwr->Deactivate(false);
            pwr->Release();
            delete pwr;
            it = m_ActivePowerUps.erase(it);
        }
    }

    ClearScreenEffects();

    // Zen mode (gameMode==2): re-activate always-on specials.
    // TODO: if (fullReset && g_GameData->gameMode == 2) { for specials: ActivatePower(...) }
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
    for (auto& se : m_ActiveScreenEffects) se.Deactivate();
    m_ActiveScreenEffects.clear();
}

// @ 0x00119cb0
void PowerUpManager::Load() {
    // TODO: parse xml/powerUpList.xml using TiXmlDocument.
    // Pseudocode per docs/engine/powerup-manager-deep-re.md §4.9:
    //   TiXmlDocument* doc = new TiXmlDocument("xml/powerUpList.xml");
    //   m_AllPowerUps.clear(); m_PurchasablePowers.clear();
    //   if (!doc->LoadFile(0)) { delete doc; return; }
    //   parse each <powerup> into PowerUp, insert into m_AllPowerUps;
    //   parse each top-level <screeneffect> into m_ScreenEffectPool.
    //   delete doc;
    //
    // Blocked: TiXmlElement full port needed first. Logged, not crashing.
    printf("PowerUpManager::Load -- xml/powerUpList.xml not yet parsed (TODO)\n");
}

// @ 0x0011840c
void PowerUpManager::LoadTextures() {
    for (auto& kv : m_AllPowerUps)      kv.second->LoadTextures();
    for (auto& kv : m_ScreenEffectPool) kv.second.LoadTextures();
}

// @ 0x00119384
void PowerUpManager::Draw() {
    // TODO: Tier-2 -- for each pwr in m_ActivePowerUps: pwr->DrawBar()
}

// @ 0x00117b38
float PowerUpManager::GetActiveProgression(float t) const {
    PowerUp* active = nullptr;
    for (PowerUp* p : m_ActivePowerUps) {
        if (p->IsSpecial()) active = p;
    }
    if (!active) return 2.0f;
    if (t > 0.0f && active->m_TotalTime > 0.0f) {
        return (active->field_0x9c - t) / active->m_TotalTime;
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
    for (PowerUp* p : m_ActivePowerUps) {
        if (p->IsSpecial()) ++n;
    }
    return n;
}
