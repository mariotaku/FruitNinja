// Analysed: 2026-05-04T00:00

#include "PowerUpManager.h"
#include "GameMode.h"
#include "PowerUp.h"
#include "GameModifier.h"
#include "ScoreModifier.h"
#include "ScoreDelegate.h"
#include "Game.h"
#include "GameOver.h"
#include "entities/SlashEntity.h"
#include "hud/TimeControl.h"
#include "util/StringHash.h"
#include "math/_Vector3.h"
#include "debug/Logger.h"
#include <cstring>
#include "game/GameWork.h"
#include "hud/HUD.h"
#include "game/WaveManager.h"

// Binary file-static globals at 0x001f3d84..0x001f3da8.
// Port-side: hoisted out of PowerUpManager so the binary-faithful 0x90
// sizeof stays unconditional. Accessed by GameInit.cpp's m_DtMod consumer.
float g_DtModDecayTimer = 1.0f;   // binary @ 0x001f3d84
float g_DtModDecayRate  = 0.0f;   // binary @ 0x001f3d88 (TODO: verify default 0.0 vs 1.0)
float g_PUM_WaveStress  = 1.0f;   // binary @ 0x001f3da8

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

// ASM-verified: 2026-06-19T00:00Z v1.6.1 PowerUpManager::SetDefaults @ 0x0013feb8 (asm-inspector)
void PowerUpManager::SetDefaults() {
    m_HighestActiveProgress = 0.0f;
    m_StopClockAccum        = 0.0f;
    m_pActiveSpecial        = 0;
    m_WaveDtModCur          = 1.0f;
    m_DtMod                 = 1.0f;
    m_SlowClockMult         = 1.0f;
    ClearScoreMultipliers();
    SlashEntity::s_ModPowerMask = 0;
    HUD* pHud = game_work.mHud;
    pHud->scales[0] = 1.0f;
    pHud->scales[1] = 1.0f;
    pHud->scales[2] = 1.0f;
    pHud->scales[3] = 1.0f;
    pHud->scales[4] = 1.0f;
    pHud->scales[5] = 1.0f;
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
        // ASM-spec v1.6.1 PowerUpManager::Update @0x00141484: gate is Purchaseable()
        // (cost-based), not the m_bIsPurchasable flag -- freeze (cost 0) ticks at dt*waveDtMod.
        float perPowerDt = pwr->Purchaseable() != 0 ? dt : (dt * prevWaveDtMod);
        int expired = pwr->Update(perPowerDt);

        if (expired == 0) {
            float p = pwr->GetCurrentTimeProgress();
            if (p > m_HighestActiveProgress) {
                if (pwr->Purchaseable() == 0) {
                    m_HighestActiveProgress = p;
                    m_pActiveSpecial = pwr;
                } else if (m_HighestActiveProgress < 0.001f) {    // DAT_00118b90
                    m_HighestActiveProgress = 0.001f;
                }
            }
            int numTimed = GetNumActiveTimedPowers();
            float& xpos  = pwr->m_BarXPos;
            // Row origin is screen-centre (0) at every aspect: the
            // `(numTimed - 1) * -55` term already recentres the whole row
            // around 0 regardless of slot count, so the meter row stays
            // centred in widescreen without any edge-anchor. (A prior
            // "powerup.bar" MapX edge-anchor here pushed the row off-centre
            // in 16:9 -- removed; see Layout.cpp history.)
            float target = (specialIdx * 110.0f)         // DAT
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
    // Binary PowerUpManager::Update @ 0x001189b4: passes dt / WaveManager::GetInstance()->m_ComboSpeedDivisor.
    // m_ComboSpeedDivisor is always 1.0 in shipped content (no XML sets it), so numerically identical.
    std::list<ScreenEffect>::iterator eit = m_ActiveScreenEffects.begin();
    while (eit != m_ActiveScreenEffects.end()) {
        float effectDt = dt / WaveManager::GetInstance()->m_ComboSpeedDivisor;
        eit->Update(effectDt, 0.0f, 0.0f);   // DAT_00118b9c = 0.0f
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

// v1.6.1 PowerUpManager::Reset @ 0x00142e08
void PowerUpManager::Reset(bool fullReset) {
    m_StopClockAccum        = 0.0f;
    m_HighestActiveProgress = 0.0f;
    m_pActiveSpecial        = 0;
    SlashEntity::s_ModPowerMask = 0;
    m_WaveDtModCur          = 1.0f;
    m_WaveDtModPrev         = 1.0f;     // also reset here, unlike SetDefaults
    m_DtMod                 = 1.0f;
    m_SlowClockMult         = 1.0f;
    ClearScoreMultipliers();

    // Binary @ 0x00142e08: if HUD != NULL, writes 1.0f to all 6 scale slots.
    // Port: SetDefaults() already does this; HUD gate kept faithful to binary.
    HUD* pHud = game_work.mHud;
    if (pHud) {
        pHud->scales[0] = 1.0f;
        pHud->scales[1] = 1.0f;
        pHud->scales[2] = 1.0f;
        pHud->scales[3] = 1.0f;
        pHud->scales[4] = 1.0f;
        pHud->scales[5] = 1.0f;
    }

    if (fullReset) {
        // Binary @ 0x00142e08: (**(code **)(*(int *)game_work.pM_pTimeControl + 0x10))()
        // = TimeControl::Reset() via vtable slot +0x10, called BEFORE the active-list drain.
        if (game_work.mCountDown) {
            game_work.mCountDown->Reset();
        }
    }

    std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (pwr->Purchaseable() != 0) {   // v1.6.1 Reset @0x00142e08: cost!=0, not single= flag
            pwr->Deactivate(true);
            if (fullReset) {
                ActivatePurchase(pwr);
                ++it;
                continue;
            } else {
                // Non-fullReset: keep if remaining uses > 0, discard otherwise.
                if (!pwr->m_pPurchaseInfo || pwr->m_pPurchaseInfo->m_CurrentUses < 1) {
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

    // Arcade mode (gameMode==2 + fullReset): re-activate all m_bIsSpecial templates.
    // ASM-spec v1.6.1 PowerUpManager::Reset @0x00142e08 tail (0x00142fc8):
    // `cmp r7,#0; beq end; ldr r3,[r5,r3]; ldrb r3,[r3,#0x4]; cmp r3,#0x2; bne end`
    // -- game_work from the GOT, no Game::GetInstance, no null test.
    if (fullReset) {
        if (game_work.gameMode == GAME_MODE_ARCADE) {
            for (std::map<uint32_t, PowerUp*>::iterator it2 = m_AllPowerUps.begin();
                 it2 != m_AllPowerUps.end(); ++it2) {
                PowerUp* tpl = it2->second;
                if (tpl && tpl->m_bIsSpecial) {
                    _Vector3<float> zero(0.0f, 0.0f, 0.0f);
                    ActivatePower(tpl->m_NameHash, zero, NULL);
                }
            }
        }
    }
}

// @ 0x00118904
void PowerUpManager::ClearTimedPowers() {
    // ASM-spec v1.6.1 PowerUpManager::ClearTimedPowers @0x0014136c: literal 0.0 store.
    m_HighestActiveProgress = 0.0f;
    m_pActiveSpecial = 0;

    std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
    while (it != m_ActivePowerUps.end()) {
        PowerUp* pwr = *it;
        if (pwr->Purchaseable() == 0 && pwr->IsTimed()) {   // v1.6.1 ClearTimedPowers @0x0014136c: filter on cost==0, NOT the single= flag
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
PowerUp* PowerUpManager::ActivatePower(uint32_t hash, _Vector3<float> position, float* purchaseExtra) {
    std::map<uint32_t, PowerUp*>::iterator it = m_AllPowerUps.find(hash);
    if (it == m_AllPowerUps.end()) return 0;

    std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
    PowerUp* clone = 0;

    if (byHash != m_ActiveByHash.end()) {
        // (A) Already active -> re-activate same instance with new position/extra.
        PowerUp* existing = byHash->second;
        // ASM-spec v1.6.1 PowerUpManager::ActivatePower @0x00142934 (@0x001429f0): re-slicing an
        // already-active power resets its timer -- showPopup=FALSE (mov r1,#0) and extraParam =
        // &(existing->GetLongestMod() result) (vstr'd s0 at sp+0x4c), NOT purchaseExtra. Without
        // this, re-slicing freeze never reset the 7s timer.
        float longestMod = existing->GetLongestMod();
        _Vector3<float> posCopy(position);
        existing->Activate(false, (purchaseExtra != NULL), posCopy, &longestMod);
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
            _Vector3<float> posCopy(position);
            clone->Activate(true, (purchaseExtra != NULL), posCopy, purchaseExtra);
        } else {
            // (B2) Purge-other-specials path.
            // ASM-spec v1.6.1 PowerUpManager::ActivatePower @0x00142934 (@0x00142b30/@0x00142b94):
            // shortestTime IS used, not discarded -- its address is passed as extraParam to both
            // the other-specials' purge Activate() and the final clone Activate(), clamping each
            // purged special's m_BonusAccum (purge semantics).
            float shortestTime = clone->GetLongestMod();
            for (std::list<PowerUp*>::iterator pit = m_ActivePowerUps.begin();
                 pit != m_ActivePowerUps.end(); ++pit) {
                if ((*pit)->IsSpecial()) {
                    float lm = (*pit)->GetLongestMod();
                    if (lm < shortestTime) shortestTime = lm;
                }
            }

            // Activate(false, false, ZERO_VEC, &shortestTime) on every other special.
            _Vector3<float> ghostPos(0.0f, 0.0f, 0.0f);  // DAT_001199d0 canonical zero Vec3
            for (std::list<PowerUp*>::iterator pit = m_ActivePowerUps.begin();
                 pit != m_ActivePowerUps.end(); ++pit) {
                if ((*pit)->IsSpecial() && *pit != clone) {
                    _Vector3<float> gp(ghostPos);
                    (*pit)->Activate(false, false, gp, &shortestTime);
                }
            }
            _Vector3<float> posCopy(position);
            clone->Activate(true, false, posCopy, &shortestTime);
        }

        // (B-tail) Assign initial X-position for HUD bar.
        // ASM-spec v1.6.1 PowerUpManager::ActivatePower @0x00142934 tail: initial x uses 55,
        // not the 110 spacing used by Update's easing target -- Update eases m_BarXPos toward
        // the 110-spaced slot over subsequent frames (0.2f/frame lerp).
        // Row origin is screen-centre (0) at every aspect -- see Update's
        // `target` comment above.
        int n = GetNumActiveTimedPowers();
        clone->m_BarXPos = (float)n * 55.0f;

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
    --p->m_pPurchaseInfo->m_CurrentUses;
}

// @ 0x00119760
bool PowerUpManager::ActivateScreenEffect(uint32_t hash) {
    std::map<uint32_t, ScreenEffect>::iterator it = m_ScreenEffectPool.find(hash);
    if (it == m_ScreenEffectPool.end()) return false;
    // v1.6.1 @0x00119760: binary constructs on stack, Activate()s, push_back()s,
    // then lets the temp's dtor run -- and its EffectImage copy-ctor @0x00145bd4
    // preserves m_pHudCtrl/m_bAddedToHUD, so the list copy stays wired to the live
    // control. Push the pool copy into the list FIRST, then Activate the element
    // that actually lives there, so no post-Activate copy re-nulls the control
    // (fixes blitz_1..6 / arcade screen-effects never appearing).
    m_ActiveScreenEffects.push_back(it->second);
    m_ActiveScreenEffects.back().Activate();
    return true;
}

// @ 0x00117ed8
void PowerUpManager::ClearScreenEffects() {
    for (std::list<ScreenEffect>::iterator it = m_ActiveScreenEffects.begin();
         it != m_ActiveScreenEffects.end(); ++it) it->Deactivate();
    m_ActiveScreenEffects.clear();
}

// v1.6.1 PowerUpManager::SaveActivePowerUps @ 0x001403a0 — save active power-up state to XML
void PowerUpManager::SaveActivePowerUps(TiXmlElement* parent) {
    for (std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        PowerUp* p = *it;
        TiXmlDocument doc = parent->GetDocument();
        TiXmlElement el = doc.NewElement("power");
        el.SetAttribute("name", p->m_Name);
        el.SetDoubleAttribute("time", (double)p->m_LongestRemaining);
        el.SetDoubleAttribute("totalTime", (double)p->m_TotalTime);
        el.SetDoubleAttribute("osa", (double)p->m_BarRamp);
        if (p->m_DeferredPoints >= 0) {
            el.SetDoubleAttribute("deferedPoints", (double)p->m_DeferredPoints);
        }
        parent->LinkEndChild(el);
    }
}

// @ 0x00142c44 (v1.6.1) — restore active power-up state from XML
void PowerUpManager::LoadActivePowerUps(TiXmlElement* parent, int gameMode) {
    for (TiXmlElement el = parent->FirstChildElement("power");
         el; el = el.NextSiblingElement("power")) {
        float curTime = 0.0f;
        el.QueryFloatAttribute("time", &curTime);
        const char* nameStr = el.Attribute("name");
        if (!nameStr) continue;
        uint32_t hash = StringHash(nameStr);

        std::map<uint32_t, PowerUp*>::iterator tplIt = m_AllPowerUps.find(hash);
        if (tplIt == m_AllPowerUps.end()) continue;
        PowerUp* tpl = tplIt->second;

        bool skip;
        if (!tpl->IsSpecial() && tpl->m_bIsSpecial == 0) {
            skip = false;
        } else if (gameMode == GAME_MODE_ARCADE) {
            skip = false;
        } else {
            skip = true;
        }
        if (skip) continue;

        _Vector3<float> pos(0.0f, 0.0f, 0.0f);
        PowerUp* p = ActivatePower(hash, pos, &curTime);
        if (!p) continue;
        p->SetCurrentTime(curTime);
        float tmp = curTime;
        el.QueryFloatAttribute("totalTime",    &tmp); p->SetTotalTime(tmp);
        tmp = curTime;
        el.QueryFloatAttribute("osa",          &tmp); p->SetOnScreenAmt(tmp);
        int deferedPoints = -1;
        el.QueryIntAttribute("deferedPoints", &deferedPoints);
        if (deferedPoints >= 0) {
            AddToCurrentScore(deferedPoints, 0, false, false);
        }
    }
}

// ASM-spec v1.6.1 PowerUpManager::Load @0x00143088 (binary loads "xml/poweruplist.xml", lowercase)
void PowerUpManager::Load() {
    TiXmlDocument doc;
    // Binary literal is lowercase "xml/poweruplist.xml" (byte-exact @0x00143088);
    // the asset on disk matches. Earlier port used camel-case "powerUpList.xml",
    // which silently failed on case-sensitive filesystems (web/emscripten, webOS,
    // HLE) -> empty m_ScreenEffectPool -> no power/combo/blitz popups.
    if (!doc.LoadFile("xml/poweruplist.xml")) {
        return;
    }

    // Real XML uses <powerInfoFile> root + <power> children -- not the
    // <powers>+<powerup> the port guessed. Same fix-pattern as
    // BonusManager's bonusawards.xml schema mismatch.
    TiXmlElement root = doc.FirstChildElement("powerInfoFile");
    if (!root) {
        LOG_WARN("POWERUP/Load", "no <powerInfoFile> root in xml/poweruplist.xml");
        return;
    }

    m_AllPowerUps.clear();
    m_PurchasablePowers.clear();

    for (TiXmlElement child = root.FirstChildElement();
         child; child = child.NextSiblingElement()) {
        const char* tag = child.Name();
        if (!tag) continue;

        if (strcmp(tag, "power") == 0) {
            PowerUp* pu = new PowerUp();
            pu->Parse(&child);
            if (pu->m_NameHash == 0) {
                delete pu;
                continue;
            }
            m_AllPowerUps[pu->m_NameHash] = pu;
            if (pu->m_bIsPurchasable) {
                m_PurchasablePowers.push_back(pu);
            }
        } else if (strcmp(tag, "effect") == 0) {
            const char* nameAttr = child.Attribute("name");
            if (!nameAttr) continue;
            uint32_t hash = StringHash(nameAttr);
            ScreenEffect& se = m_ScreenEffectPool[hash];
            se.m_NameHash = hash;
            se.Parse(&child);
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

// @ 0x00117c50 — return first active purchasable; set outIt to its position
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00117c50 (re-analyst)
PowerUp* PowerUpManager::GetFirstPurchasable(std::list<PowerUp*>::iterator& outIt) {
    outIt = m_ActivePowerUps.begin();
    if (outIt == m_ActivePowerUps.end()) return NULL;
    uint32_t hash = (*outIt)->m_NameHash;
    std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
    if (byHash == m_ActiveByHash.end()) return *outIt;
    return byHash->second;
}

// @ 0x00117bf4 — advance it, return next active purchasable
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00117bf4 (re-analyst)
PowerUp* PowerUpManager::GetNextPurchasable(std::list<PowerUp*>::iterator& it) {
    ++it;
    if (it == m_ActivePowerUps.end()) return NULL;
    uint32_t hash = (*it)->m_NameHash;
    std::map<uint32_t, PowerUp*>::iterator byHash = m_ActiveByHash.find(hash);
    if (byHash == m_ActiveByHash.end()) return *it;
    return byHash->second;
}

// ASM-verified: 2026-05-18 v1.6.1 PowerUpManager::SetAppropriateScoreCallback @ 0x001417c0 (re-analyst)
void PowerUpManager::SetAppropriateScoreCallback() {
    for (std::list<PowerUp*>::iterator it = m_ActivePowerUps.begin();
         it != m_ActivePowerUps.end(); ++it) {
        PowerUp* p = *it;
        for (std::list<GameModifier*>::iterator mit = p->m_ModList.begin();
             mit != p->m_ModList.end(); ++mit) {
            GameModifier* mod = *mit;
            if (mod->GetType() == 2) {
                ScoreModifier* sm = static_cast<ScoreModifier*>(mod);
                if (sm->m_bDeferPoints) {
                    SetScoreDelegate(Mortar::Delegate1<int,int>::Make(sm, &ScoreModifier::DeferPoints));
                    return;
                }
            }
        }
    }
    SetDefaultScoreDelegate();
}

// @ 0x0011836c — walk m_AllPowerUps and m_ScreenEffectPool, call UnloadTextures
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0011836c (re-analyst)
void PowerUpManager::UnloadTextures() {
    for (std::map<uint32_t, PowerUp*>::iterator it = m_AllPowerUps.begin();
         it != m_AllPowerUps.end(); ++it) {
        it->second->UnloadTextures();
    }
    for (std::map<uint32_t, ScreenEffect>::iterator it = m_ScreenEffectPool.begin();
         it != m_ScreenEffectPool.end(); ++it) {
        it->second.UnloadTextures();
    }
}
