#include "WaveManager.h"
#include "game/GlobalProbabilityOveride.h"
#include "GameMode.h"
#include "ScoreState.h"
#include "WaveStructs.h"
#include "Game.h"
#include "FruitSaveData.h"
#include "audio/GameSound.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "entities/FruitInfo.h"
#include "hud/HUD.h"
#include "screens/PauseScreen.h"
#include "math/MathUtil.h"
#include "util/StringHash.h"
#include "xml/TiXml.h"
#include "GameOver.h"
#include "PowerUpManager.h"
#include "ScreenEffect.h"
#include "hud/TimeControl.h"
#include "hud/SpeedControl.h"
#include "engine/network/NetworkManager.h"
#include "engine/network/P2PMessageHandling.h"
#include "entities/EntityTracker.h"
#include "debug/Logger.h"

// Port-only debug logging: the binary has no equivalent. Suppressed in non-debug
// builds to avoid inflating asm-verify diffs (already no-ops under __bada__).
#ifndef DEBUG
#undef LOG_DEBUG
#undef LOG_INFO
#define LOG_DEBUG(...) ((void)0)
#define LOG_INFO(...)  ((void)0)
#endif

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>
#include "game/GameWork.h"

// Forward-declare; defined in CoinChanceinator.cpp.
// WaveStructs.h has the full COIN_CHANCEINATOR layout; CoinChanceinator.h
// conflicts (empty struct re-definition). Include neither -- just declare.
void ParseCoinChanceinator(COIN_CHANCEINATOR* pDst, TiXmlElement* pElem);

// Analysed: 2026-04-30T00:00

// Fixed timestep matching binary DAT_00125bd4 = 0x3c888889 = 1/60
static const float WAVE_STEP = 1.0f / 60.0f;

// Mode-to-XML mapping per wave-system-impl.md §7.
// Filenames are lowercase to match the actual files in FruitNinjaBada/Data/xml/.
static const char* const s_WaveXML[4] = {
    "xml/originalwavelist.xml",   // mode 0 = Classic
    "xml/combowavelist.xml",      // mode 1 = Combo
    "xml/arcadewavelist.xml",     // mode 2 = Arcade
    "xml/zenwavelist.xml",        // mode 3 = Zen
};

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

int WaveManager::SplitWords(const char* str, std::vector<std::string>& out) {
    out.clear();
    if (!str || !*str) return 0;
    const char* p = str;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == ',') ++p;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ',' && *p != '\t') ++p;
        const char* end = p;
        while (end > start && (*(end-1) == ' ' || *(end-1) == '\t')) --end;
        if (end > start)
            out.push_back(std::string(start, end));
    }
    return (int)out.size();
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x001231d8 (re-analyst)
}

SpawnPlacement ParsePlacement(const char* side) {
    if (!side) return PLACEMENT_BOTTOM;
    if (strcmp(side, "BOTTOM") == 0 || strcmp(side, "bottom") == 0) return PLACEMENT_BOTTOM;
    if (strcmp(side, "BOTTOM_SLOW") == 0) return PLACEMENT_BOTTOM_SLOW;
    if (strcmp(side, "LEFT") == 0) return PLACEMENT_LEFT;
    if (strcmp(side, "RIGHT") == 0) return PLACEMENT_RIGHT;
    if (strcmp(side, "LEFT_RIGHT") == 0 || strcmp(side, "RANDOM") == 0) return PLACEMENT_RANDOM_SIDE;
    return PLACEMENT_BOTTOM;
}

// ASM-spec v1.6.1 ParseSpawner @0x00129314
// Parses a <Spawn> element into SPAWNER_INFO. Returns 1 if the element's tag hashes
// to StringHash("Spawn"), else 0. Faithful standalone port of the binary's spawner
// parser. NOTE: WaveManager::Init still uses its own inline <Spawn> loop (which
// additionally resolves fruit-type hashes eagerly and accumulates m_TotalWeight);
// consolidating Init onto ParseSpawner would require wiring SPAWNER_INFO::SelectTypes,
// which is a wave-loading change out of scope for the save/load round-trip (#291).
int ParseSpawner(TiXmlElement* elem, SPAWNER_INFO* out) {
    if (!elem || !out) return 0;
    static uint32_t s_spawnHash = StringHash("Spawn");
    const char* tag = elem->Name();
    if (!tag || StringHash(tag) != s_spawnHash) return 0;

    const char* types = elem->Attribute("type");
    out->m_FruitTypeCount = WaveManager::SplitWords(types, out->m_FruitTypeNames);
    if (out->m_FruitTypeCount > 0) {
        out->m_pFruitTypeHashes = new int[out->m_FruitTypeCount];
        for (int i = 0; i < out->m_FruitTypeCount; ++i)
            out->m_pFruitTypeHashes[i] = -1;   // -1 = unresolved (SelectTypes resolves later)
    }

    elem->QueryFloatAttribute("min",      &out->m_SpawnMin);
    elem->QueryFloatAttribute("max",      &out->m_SpawnMax);
    elem->QueryFloatAttribute("mininc",   &out->m_GrowthInc);
    elem->QueryFloatAttribute("maxinc",   &out->m_GrowthInc);   // maxinc overwrites mininc (binary)
    elem->QueryFloatAttribute("delay",    &out->m_Delay);
    elem->QueryFloatAttribute("delayinc", &out->m_DelayInc);
    elem->QueryFloatAttribute("horizmin", &out->m_HorizMin);
    elem->QueryFloatAttribute("horizmax", &out->m_HorizMax);
    elem->QueryFloatAttribute("velscale", &out->m_VelXScale);
    out->m_VelYScale = out->m_VelXScale;
    elem->QueryFloatAttribute("velXscale", &out->m_VelXScale);
    elem->QueryFloatAttribute("velYscale", &out->m_VelYScale);
    elem->QueryFloatAttribute("dt",        &out->m_TimeScale);

    const char* grav = elem->Attribute("gravity");
    if (grav && *grav) {
        Vec3 g = ParseVector(grav);
        out->m_Gravity_x = g.x;
        out->m_Gravity_y = g.y;
        out->m_Gravity_z = g.z;
    }

    const char* placement = elem->Attribute("placement");
    out->m_SpawnType = placement ? ParsePlacement(placement) : PLACEMENT_BOTTOM;

    const char* mirror = elem->Attribute("mirror");
    out->m_bMirror = (uint8_t)((mirror && strcmp("true", mirror) == 0) ? 1 : 0);

    return 1;
}

// ----------------------------------------------------------------------------
// Construction / singleton
// ----------------------------------------------------------------------------

WaveManager::WaveManager()
    : m_SpawnLevel(1.0f)
    , m_CritChanceMult(1.0f)
    , m_SyncLocalReady(1)   // TODO: v1.6.1 WaveManager::WaveManager @ 0x00123ef8 -- binary ctor omits this (BSS-zero); Reset() sets it to 1 unconditionally
    , m_SyncRemotePending(0)
    , m_SyncReceived(0)
    , m_SyncWaveIdx(-1)
    , m_SyncScore(0.0f), m_NetTimerA(0.0f)
    , m_NetTimerB(0.0f)
    , m_BombChance(1.0f)
    , m_FruitChance(1.0f)
    , m_SpeedAccum(1.0f)
    , m_SpeedScale(1.0f), m_ComboSpeedDivisor(1.0f)
    , m_NextWaveDelay_P0(0.0f), m_NextWaveDelay_P1(0.0f)
    , m_WaveActive(0), m_BlitzSpawnCount(0), m_BlitzState(0), _pad247(0)
    , m_NextBlitzTime(0.0f)
    , m_MaxWaveIdP0(0)
    , m_RecentFruitCount{0, 0}
    , m_SyncScoreSnapshot(0)
    , m_StepAccumulator(0.0f)
    , m_SavedWaveDelay(0)
    , m_pWaveQue(nullptr), m_pWaveQueItem(nullptr)
{
    m_ComboTimer = 0.0f;
    m_ComboSpeed = 0.0f; m_TargetComboSpeed = 0.0f;
    m_BlitzLevel = 0;
    m_ColdTimer  = 0.0f;
    m_pCurrentWave[0] = m_pCurrentWave[1] = nullptr;
    m_WaveCount[0] = m_WaveCount[1] = 0;
    // m_DtIncPerMode, m_SpeedClampStart, m_SpeedClampMax:
    // v1.6.1 WaveManager ctor @ 0x00123ef8 leaves these BSS-zero; Init() fills
    // them from <defaults> globalDtInc/globalDtStart/globalDtMax. All 4 shipped
    // modes have globalDtInc=0 (no ramp), so inc=0 is binary-faithful.
    // globalDtStart/globalDtMax default to 1.0 when absent; with inc=0 the
    // clamp range is unreachable anyway, so 1.0 is the correct sentinel.
    m_DtIncPerMode[0] = m_DtIncPerMode[1] = 0.0f;
    m_DtIncPerMode[2] = m_DtIncPerMode[3] = 0.0f;
    m_SpeedClampStart[0] = m_SpeedClampStart[1] = 1.0f;
    m_SpeedClampStart[2] = m_SpeedClampStart[3] = 1.0f;
    m_SpeedClampMax[0] = m_SpeedClampMax[1] = 1.0f;
    m_SpeedClampMax[2] = m_SpeedClampMax[3] = 1.0f;
    for (int j = 0; j < 32; ++j)
        m_FruitQueue[j] = -1;
    m_MaxWaveIdP0 = 0;
    m_RecentFruitCount[0] = 0;
    m_SpeedControl[0] = m_SpeedControl[1] = nullptr;
}

WaveManager::~WaveManager() {
    Destroy();
}

WaveManager* WaveManager::GetInstance() {
    static WaveManager s_Instance;
    return &s_Instance;
}

// ----------------------------------------------------------------------------
// Init — parse XML files
// ----------------------------------------------------------------------------

void WaveManager::Init() {
    LOG_DEBUG("WaveManager", "Init");

    for (int mode = 0; mode < 4; ++mode) {
        // Free any previously-loaded wave infos for this mode.
        // Range-for replaced with iterator form for GCC 4.4 cross-build.
        for (std::vector<WAVE_INFO*>::iterator it = m_WaveInfo[mode].begin();
             it != m_WaveInfo[mode].end(); ++it)
            delete *it;
        m_WaveInfo[mode].clear();

        TiXmlDocument doc;
        if (!doc.LoadFile(s_WaveXML[mode])) {
            continue;
        }

        TiXmlElement root = doc.RootElement();
        if (!root) continue;

        // ASM-verified: 2026-05-27 v1.6.1 WaveManager::Init @ 0x001144e8 (asm-inspector)
        // -- pre-loop Reset + single-pass dispatch + per-defaults re-Reset +
        //    coin_chances/WaveInfo/defaults/OverideProbability strings all match.
        // Binary resets DEFAULT_WAVE_INFO before the loop so an XML missing
        // <defaults> still produces ctor-default values (rather than inheriting
        // from a prior Init() call's state).
        m_DefaultWaveInfo[mode] = DEFAULT_WAVE_INFO();
        int waveIndex = 0;
        for (TiXmlElement el = root.FirstChildElement();
             el; el = el.NextSiblingElement())
        {
            const char* elName = el.Name();
            if (!elName) continue;

            if (strcmp(elName, "defaults") == 0) {
                // Re-Reset on each occurrence so second block overrides first.
                // Binary calls DEFAULT_WAVE_INFO::Reset (placement-new via ctor).
                m_DefaultWaveInfo[mode] = DEFAULT_WAVE_INFO();
                DEFAULT_WAVE_INFO& def = m_DefaultWaveInfo[mode];
                el.QueryIntAttribute("waveChance",       &def.m_WaveChance);
                // DIFFERS: binary reads "waveChanceRegrowth"; shipping XML uses "waveChanceGrowth".
                // Neither key matches the other; binary default 0.25 covers both. Parse both for safety.
                el.QueryFloatAttribute("waveChanceRegrowth", &def.m_WaveChanceRegrowth);
                el.QueryFloatAttribute("waveChanceGrowth",   &def.m_WaveChanceRegrowth);
                el.QueryFloatAttribute("dt",             &def.m_SpawnTimeScale);
                el.QueryFloatAttribute("criticalChance", &def.m_CritChanceVal);
                // globalDtInc -> per-mode WaveManager speed accumulator (binary key is "globalDtInc", not "dtInc").
                el.QueryFloatAttribute("globalDtInc",   &m_DtIncPerMode[mode]);
                // globalDtStart/globalDtMax -> per-mode speed clamp bounds.
                el.QueryFloatAttribute("globalDtStart", &m_SpeedClampStart[mode]);
                el.QueryFloatAttribute("globalDtMax",   &m_SpeedClampMax[mode]);
                // Additional <defaults> attrs written to DEFAULT_WAVE_INFO per binary audit.
                el.QueryFloatAttribute("dtInc",          &def.m_DtInc);
                el.QueryFloatAttribute("dtSpInc",        &def.m_DtSpInc);
                el.QueryFloatAttribute("beforeDelay",    &def.m_BeforeDelay);
                el.QueryFloatAttribute("beforeDelayInc", &def.m_BeforeDelayInc);
                el.QueryFloatAttribute("nextDelay",      &def.m_NextDelay);
                el.QueryFloatAttribute("nextDelayInc",   &def.m_NextDelayInc);
                el.QueryFloatAttribute("nextDelaySpInc", &def.m_NextDelaySpInc);
                if (const char* wfe = el.Attribute("waitForEntities"))
                    def.m_bWaitForEntities = (strcmp(wfe, "false") != 0) ? 1 : 0;
                if (const char* wfp = el.Attribute("waitForProcessing"))
                    def.m_bWaitForProcessing = (strcmp(wfp, "false") != 0) ? 1 : 0;
                el.QueryFloatAttribute("speedLoss",      &def.m_SpeedLoss);
                el.QueryIntAttribute("overideProbabiltyPool", &def.m_OverideProbabilityPool);
            } else if (strcmp(elName, "coin_chances") == 0) {
                ParseCoinChanceinator(&m_CoinChanceinator[mode], &el);
            } else if (strcmp(elName, "OverideProbability") == 0) {
                PROBABILITY_OVERIDE po;
                const char* types = el.Attribute("types");
                if (types) po.m_TypeCount = SplitWords(types, po.m_Types);
                el.QueryIntAttribute("percentageChance", &po.m_PercentChance);
                el.QueryIntAttribute("perWave", &po.m_PerWave);
                el.QueryIntAttribute("waveCount", &po.m_PerWaveCount);
                el.QueryFloatAttribute("disableWhenPowered", &po.m_DisableWhenPowered);
                m_ProbabilityOverride[mode].push_back(po);
            } else if (strcmp(elName, "WaveInfo") == 0) {
                WAVE_INFO* wi = new WAVE_INFO();
                wi->m_WaveIndex = waveIndex++;
                // ASM-verified: 2026-05-22 v1.6.1 binary @ 0x001267c8 (re-analyst).
                // WAVE_INFO::WAVE_INFO(DEFAULT_WAVE_INFO*) copies m_bWaitForEntities (+0x2c)
                // and m_bWaitForProcessing (+0x2d) from the per-mode DEFAULT_WAVE_INFO.
                wi->m_bWaitForEntities   = m_DefaultWaveInfo[mode].m_bWaitForEntities;
                wi->m_bWaitForProcessing = m_DefaultWaveInfo[mode].m_bWaitForProcessing;
                // v1.6.1 WaveManager::GetNextWave @0x001267c8: WAVE_INFO ctor copies
                // m_OverideProbabilityPool (+0x34) from DEFAULT_WAVE_INFO -> WAVE_INFO+0x70.
                // Without this, SpeedWaves inherit ctor default 100 instead of XML default 150.
                wi->m_OverideProbabilityPool = m_DefaultWaveInfo[mode].m_OverideProbabilityPool;

                // waveNo attr -> binary stores to local then +0x0 (m_ScoreThreshold) via conditional.
                // m_OverideProbabilityPool also written to +0x70 (second read wins in binary).
                const char* waveNoStr = el.Attribute("waveNo");
                if (waveNoStr) {
                    if (strcmp(waveNoStr, "forever") == 0)
                        wi->m_WaveNumber = -2;
                    else
                        wi->m_WaveNumber = atoi(waveNoStr);
                }
                wi->m_ScoreThreshold = wi->m_WaveNumber;

                // overideProbabiltyPool at +0x70 — typo matches binary literal. Writes same slot as waveNo in binary.
                el.QueryIntAttribute("overideProbabiltyPool", &wi->m_OverideProbabilityPool);

                // until attr -> m_EndScore (+0x04).
                const char* untilStr = el.Attribute("until");
                if (untilStr) {
                    if (strcmp(untilStr, "forever") == 0)
                        wi->m_EndScore = -2;
                    else
                        wi->m_EndScore = atoi(untilStr);
                } else {
                    wi->m_EndScore = -2;
                }

                // "chance" -> m_Chance (+0x3c). "chanceRegrowth" -> m_ChanceRegrowth (+0x44).
                el.QueryIntAttribute("chance",           &wi->m_Chance);
                el.QueryFloatAttribute("chanceRegrowth", &wi->m_ChanceRegrowth);
                wi->m_CurrentChance   = wi->m_Chance;
                wi->m_CurrentRegrowth = wi->m_ChanceRegrowth;

                // criticalChance -> m_CriticalChance (+0x64).
                el.QueryFloatAttribute("criticalChance", &wi->m_CriticalChance);

                // "games" / "gamesMin" -> m_GamesMin (+0x4c); "gamesMax" -> m_GamesMax (+0x50).
                // Binary reads "games" first (overwrites +0x4c), then "gamesMin" overwrites same slot.
                el.QueryIntAttribute("games",    &wi->m_GamesMin);
                el.QueryIntAttribute("gamesMin", &wi->m_GamesMin);
                el.QueryIntAttribute("gamesMax", &wi->m_GamesMax);
                // Binary post-process: if GamesMin < 0: GamesMin = GamesMax; if GamesMax < 0: GamesMax = GamesMin.
                if (wi->m_GamesMin < 0) wi->m_GamesMin = wi->m_GamesMax;
                if (wi->m_GamesMax < 0) wi->m_GamesMax = wi->m_GamesMin;

                // <ChooseFrom> child -> m_SpecialFruits (+0x54); m_reserved60 always cleared to 0.
                {
                    TiXmlElement cfEl = el.FirstChildElement("ChooseFrom");
                    if (cfEl) {
                        wi->m_SpecialFruits.clear();
                        wi->m_reserved60 = 0;
                        const char* types = cfEl.Attribute("types");
                        if (types) SplitWords(types, wi->m_SpecialFruits);
                    }
                }

                // <Wave_dt> child.
                {
                    TiXmlElement dtEl = el.FirstChildElement("Wave_dt");
                    if (dtEl) {
                        dtEl.QueryFloatAttribute("dt",    &wi->m_WaveDt);
                        dtEl.QueryFloatAttribute("inc",   &wi->m_WaveDtInc);
                        dtEl.QueryFloatAttribute("spinc", &wi->m_WaveDtSpInc);
                    }
                }

                // <NextWaveDelay> child.
                {
                    TiXmlElement ndEl = el.FirstChildElement("NextWaveDelay");
                    if (ndEl) {
                        ndEl.QueryFloatAttribute("wait",      &wi->m_NextWaveWait);
                        ndEl.QueryFloatAttribute("waitSpinc", &wi->m_NextWaveWaitSpInc);
                        ndEl.QueryFloatAttribute("speedLoss", &wi->m_NextWaveSpeedLoss);
                        // Binary: if (wait > 0) { delay = 0; inc = 0; } then read delay/inc.
                        if (wi->m_NextWaveWait > 0.0f) {
                            wi->m_NextWaveDelay    = 0.0f;
                            wi->m_NextWaveDelayInc = 0.0f;
                        }
                        ndEl.QueryFloatAttribute("delay", &wi->m_NextWaveDelay);
                        ndEl.QueryFloatAttribute("inc",   &wi->m_NextWaveDelayInc);
                        // waitForEntities: 1 if attr absent OR != "false"; 0 if "false".
                        if (const char* wfe = ndEl.Attribute("waitForEntities"))
                            wi->m_bWaitForEntities = (strcmp(wfe, "false") != 0) ? 1 : 0;
                        // waitForProcessing: stored as (CompareWords == 0 => 1; else 0).
                        if (const char* wfp = ndEl.Attribute("waitForProcessing"))
                            wi->m_bWaitForProcessing = (strcmp(wfp, "false") != 0) ? 1 : 0;
                    }
                }

                // <Spawn> children — collect spawners.
                int spawnerCount = 0;
                for (TiXmlElement sp = el.FirstChildElement("Spawn");
                     sp; sp = sp.NextSiblingElement("Spawn"))
                    ++spawnerCount;

                if (spawnerCount > 0) {
                    wi->m_pSpawners = new SPAWNER_INFO[spawnerCount];
                    wi->m_SpawnerCount = spawnerCount;
                    int si = 0;
                    for (TiXmlElement sp = el.FirstChildElement("Spawn");
                         sp; sp = sp.NextSiblingElement("Spawn"), ++si)
                    {
                        SPAWNER_INFO& s = wi->m_pSpawners[si];

                        const char* types = sp.Attribute("type");
                        if (types) {
                            SplitWords(types, s.m_FruitTypeNames);
                            s.m_FruitTypeCount = (int)s.m_FruitTypeNames.size();
                            // Build hash array.
                            if (s.m_FruitTypeCount > 0) {
                                s.m_pFruitTypeHashes = new int[s.m_FruitTypeCount];
                                for (int ti = 0; ti < s.m_FruitTypeCount; ++ti) {
                                    const std::string& tn = s.m_FruitTypeNames[ti];
                                    if (tn == "random")
                                        s.m_pFruitTypeHashes[ti] = -1;
                                    else if (tn == "bomb")
                                        s.m_pFruitTypeHashes[ti] = -2;
                                    else
                                        s.m_pFruitTypeHashes[ti] = Fruit::FruitType(tn.c_str(), false);
                                }
                            }
                        }

                        sp.QueryFloatAttribute("min", &s.m_SpawnMin);
                        sp.QueryFloatAttribute("max", &s.m_SpawnMax);
                        // mininc and maxinc both write to +0x44 (single slot); maxinc wins.
                        sp.QueryFloatAttribute("mininc", &s.m_GrowthInc);
                        sp.QueryFloatAttribute("maxinc", &s.m_GrowthInc);
                        // "delay" -> m_Delay (+0x48, chuck delay base).
                        sp.QueryFloatAttribute("delay",    &s.m_Delay);
                        sp.QueryFloatAttribute("delayinc", &s.m_DelayInc);
                        // "gravity" attr -> Vec3 at +0x18..+0x20 (binary ParseVector).
                        {
                            const char* grav = sp.Attribute("gravity");
                            if (grav) {
                                float gx = 0.0f, gy = 0.0f, gz = 0.0f;
                                sscanf(grav, "%f,%f,%f", &gx, &gy, &gz);
                                s.m_Gravity_x = gx;
                                s.m_Gravity_y = gy;
                                s.m_Gravity_z = gz;
                            }
                        }
                        // velscale -> copies to both +0x24 and +0x28; then velXscale/velYscale override.
                        sp.QueryFloatAttribute("velscale",  &s.m_VelXScale);
                        s.m_VelYScale = s.m_VelXScale;
                        sp.QueryFloatAttribute("velXscale", &s.m_VelXScale);
                        sp.QueryFloatAttribute("velYscale", &s.m_VelYScale);
                        // horizmin -> +0x2c; horizmax -> +0x30.
                        sp.QueryFloatAttribute("horizmin", &s.m_HorizMin);
                        sp.QueryFloatAttribute("horizmax", &s.m_HorizMax);
                        // mirror at +0x60: cleared to 0 when attr absent (movs r3,#0 path in binary).
                        s.m_bMirror = 0;
                        if (const char* mir = sp.Attribute("mirror"))
                            s.m_bMirror = (strcmp(mir, "false") != 0) ? 1 : 0;

                        const char* placement = sp.Attribute("placement");
                        if (placement) s.m_SpawnType = ParsePlacement(placement);

                        // Total weight contribution.
                        wi->m_TotalWeight += (int)((s.m_SpawnMin + s.m_SpawnMax) * 0.5f);
                    }
                }

                m_WaveInfo[mode].push_back(wi);
            }
        } // end single-pass document-order walk

        LOG_DEBUG("WaveManager", "Init: mode %d -> %d waves from %s",
                  mode, (int)m_WaveInfo[mode].size(), s_WaveXML[mode]);
    }

    // v1.6.1 WaveManager::Init @0x00129934: call ParseGlobalProbabilityOverides at tail.
    // ASM-spec v1.6.1 ParseGlobalProbabilityOverides @0x00129718
    ParseGlobalProbabilityOverides("xml/globalprobabilities.xml");
}

// ----------------------------------------------------------------------------
// Destroy
// ----------------------------------------------------------------------------

// ASM-spec v1.6.1 WaveManager::Destroy @0x00123b54:
// 1. For each of 4 modes: dtor+delete+null each slot in m_WaveInfo[mode], then clear().
// 2. m_pCurrentWave[0] = 0.
// 3. Indexed teardown of m_GlobalProbabilityOverride: dtor+delete+null each element, clear().
// 4. Free m_pWaveQue (+0x28) then m_pWaveQueItem (+0x24).
void WaveManager::Destroy() {
    for (int mode = 0; mode < 4; ++mode) {
        std::vector<WAVE_INFO*>& v = m_WaveInfo[mode];
        for (std::vector<WAVE_INFO*>::iterator it = v.begin(); it != v.end(); ++it) {
            WAVE_INFO* w = *it;
            if (w) {
                w->~WAVE_INFO();
                operator delete(w);
                *it = 0;
            }
        }
        v.clear();
    }

    m_pCurrentWave[0] = 0;

    // GlobalProbabilityOveride teardown — indexed loop matching binary.
    // v1.6.1 WaveManager::Destroy @0x00123b54
    std::vector<GlobalProbabilityOveride*>& gpo = m_GlobalProbabilityOverride;
    for (std::vector<GlobalProbabilityOveride*>::size_type i = 0; i < gpo.size(); ++i) {
        if (gpo[i]) {
            gpo[i]->~GlobalProbabilityOveride();
            operator delete(gpo[i]);
            gpo[i] = 0;
        }
    }
    gpo.clear();

    // Free WaveQue (+0x28) before WaveQueItem (+0x24) — binary order.
    if (m_pWaveQue) {
        m_pWaveQue->~WaveQue();
        operator delete(m_pWaveQue);
        m_pWaveQue = 0;
    }
    if (m_pWaveQueItem) {
        m_pWaveQueItem->~WaveQueItem();
        operator delete(m_pWaveQueItem);
        m_pWaveQueItem = 0;
    }
}

// ----------------------------------------------------------------------------
// ParseGlobalProbabilityOverides / CheckForGlobalProbabilityOveride
// ----------------------------------------------------------------------------

// v1.6.1 WaveManager::ParseGlobalProbabilityOverides @0x00129718
// Loads globalprobabilities.xml, selects the file sub-element based on the
// "super_fruit_probability_system" save value, then populates
// m_GlobalProbabilityOverride with base/PointBased/Timed entries.
void WaveManager::ParseGlobalProbabilityOverides(const char* path)
{
    TiXmlDocument doc;
    if (!doc.LoadFile(path)) return;

    int sys = 0;
    if (game_work.m_SaveData)
        sys = game_work.m_SaveData->GetTotal("super_fruit_probability_system");

    char buf[64];
    // OS_SPrintf equivalent: build "probabilityFileN"
    snprintf(buf, sizeof(buf), "probabilityFile%i", sys);

    TiXmlElement file = doc.FirstChildElement(buf);
    if (!file) return;

    // 1) <globalProbability> -> base class
    for (TiXmlElement el = file.FirstChildElement("globalProbability");
         el; el = el.NextSiblingElement("globalProbability"))
    {
        GlobalProbabilityOveride* g = new GlobalProbabilityOveride();
        g->Parse(&el);
        m_GlobalProbabilityOverride.push_back(g);
    }

    // 2) <globalProbabilityPointBased> -> PointBased (memset 0 before ctor matches binary)
    for (TiXmlElement el = file.FirstChildElement("globalProbabilityPointBased");
         el; el = el.NextSiblingElement("globalProbabilityPointBased"))
    {
        GlobalProbabilityOveridePointBased* g = new GlobalProbabilityOveridePointBased();
        g->Parse(&el);
        m_GlobalProbabilityOverride.push_back(g);
    }

    // 3) <globalProbabilityTimed> -> Timed
    for (TiXmlElement el = file.FirstChildElement("globalProbabilityTimed");
         el; el = el.NextSiblingElement("globalProbabilityTimed"))
    {
        GlobalProbabilityOverideTimed* g = new GlobalProbabilityOverideTimed();
        g->Parse(&el);
        m_GlobalProbabilityOverride.push_back(g);
    }

    LOG_DEBUG("WaveManager", "ParseGlobalProbabilityOverides: loaded %d entries from %s (sys=%d)",
              (int)m_GlobalProbabilityOverride.size(), path, sys);
}

// v1.6.1 WaveManager::CheckForGlobalProbabilityOveride @0x00123228
// Iterates m_GlobalProbabilityOverride; calls CheckForOverride on each.
// Returns the first GPO that fires (writes outType) or null if none fires.
GlobalProbabilityOveride* WaveManager::CheckForGlobalProbabilityOveride(int& outType)
{
    for (size_t i = 0; i < m_GlobalProbabilityOverride.size(); ++i) {
        GlobalProbabilityOveride* g = m_GlobalProbabilityOverride[i];
        if (g && g->CheckForOverride(outType)) return g;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Reset — per wave-system-impl.md §1
// ----------------------------------------------------------------------------

void WaveManager::Reset(bool fullReset) {
    // v1.6.1 WaveManager::Reset @ 0x0012ba78
    Game* game = Game::GetInstance();
    if (!game) return;

    // ASM-verified: 2026-05-23 v1.6.1 binary @ 0x00125bfe..0x00125c0a (re-analyst).
    // Reset prologue: load arcade powerup textures unconditionally on every
    // Reset when gameMode == ARCADE, independent of fullReset. This is the
    // ONLY path that loads ScreenEffect / EffectImage textures during normal
    // play; without it ScreenEffect::Activate copies empty SmartPtrs into
    // HUDControl3d, HUDControl3d::Draw's `if (m_Texture)` gate fails, and
    // arcade_go / arcade_60seconds / ice_cover / clock_freeze / hud_x2_sign
    // popups never render. Binary's resume-from-save path repeats the same
    // gated call (0x00124d12..0x00124d1e); the port already mirrors that one
    // in WaveManager::Load.
    if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
        PowerUpManager::GetInstance()->LoadTextures();
    }

    // Reset combo state — binary @ 0x00125cdc (g_ComboCount = 0) and
    // adjacent last-slasher write. Binary writes 1 to last-slasher at reset;
    // port uses -1 (cold-boot sentinel) to keep consistent with TimeControl
    // game-over path and avoid a spurious same-player guard on first slice.
    g_ComboCount     = 0;
    g_ComboFruitType = -1;

    LOG_DEBUG("WaveManager", "Reset(full=%d) gameMode=%d waveInfos[%d].size=%zu",
              fullReset ? 1 : 0, (int)game_work.gameMode, (int)game_work.gameMode,
              m_WaveInfo[game_work.gameMode].size());

    // 1. Drop wave queue.
    delete m_pWaveQue;     m_pWaveQue = nullptr;
    delete m_pWaveQueItem; m_pWaveQueItem = nullptr;

    // 2. Per-frame state flags.
    m_SyncRemotePending = 0;
    m_SyncLocalReady = 1;
    m_SyncReceived = 0;
    m_SyncWaveIdx = -1;
    m_SyncScoreSnapshot = 0;
    m_RecentFruitCount[1] = 0;
    m_StepAccumulator = 0.0f;
    m_SyncScore = 0.0f; m_NetTimerA = 0.0f;
    m_NetTimerB = 0.0f;
    m_NextWaveDelay_P0 = 0.0f;
    m_NextWaveDelay_P1 = 0.0f;
    m_ComboTimer = 0.0f;
    m_ComboSpeed = 0.0f; m_TargetComboSpeed = 0.0f;

    // 3. Game-side flags / score.
    game_work.m_bUnsullied = 0;
    m_BlitzSpawnCount = 0;
    m_BlitzState = 0;
    m_NextBlitzTime = m_Random.RandF(10.0f) + 10.0f;
    SetScore(0, -1);       // Binary @ 0x0010a4b8; playerIdx -1 = all (defunct MP sig)
    SetMissCount(0, -1);   // Binary @ 0x0010a4e8
    ET_ClearKnownEntities(-1);

    // 4. Per-player wave state.
    m_WaveActive = 1;          // wave-active flag (player 0)
    m_WaveCount[0] = -1;      // pre-incremented by GetNextWave
    m_WaveCount[1] = -1;
    // Camera reset not ported (FruitCamera stubs).

    // HUD reset — binary @ 0x00144b78 called when game->display->field_0x3c (HUD) exists.
    if (game_work.mHud) {
        game_work.mHud->ResetControls();
    }

    // 5. Clear unspawned fruits + bombs, disable active ones.
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (am) {
        for (int i = 0; ; i++) {
            Mortar::Entity* e = am->GetEntity(0, i);
            if (!e) break;
            static_cast<Fruit*>(e)->Disable();
        }
        for (int i = 0; ; i++) {
            Mortar::Entity* e = am->GetEntity(1, i);
            if (!e) break;
            static_cast<Bomb*>(e)->Disable();
        }
    }

    // 6. Reset per-wave chance counters + PROBABILITY_OVERIDE state.
    ResetWaveChances();
    m_BlitzLevel = 0;
    m_ColdTimer  = 0.0f;
    for (std::vector<PROBABILITY_OVERIDE>::iterator pit = m_ProbabilityOverride[game_work.gameMode].begin();
         pit != m_ProbabilityOverride[game_work.gameMode].end(); ++pit)
        pit->SelectType();

    // Binary resets m_RecentFruitCount and clears the fruit queue here.
    for (int j = 0; j < 32; ++j)
        m_FruitQueue[j] = -1;
    m_MaxWaveIdP0 = 0;
    m_RecentFruitCount[0] = 0;

    // 7. Kick first wave if waves loaded.
    if (!m_WaveInfo[game_work.gameMode].empty()) {
        LOG_DEBUG("WaveManager", "Reset: calling GetNextWave(0)");
        GetNextWave(0);
        LOG_DEBUG("WaveManager", "Reset: m_pCurrentWave[0]=%p",
                  (void*)m_pCurrentWave[0]);
        // IsSameScreenMultiplayer() returns false (single-player) — skip MP delay bump.
    } else {
        LOG_WARN("WaveManager", "Reset: NO WAVES for mode %d! GetNextWave skipped.",
                 (int)game_work.gameMode);
    }

    // 8. Final per-mode speed-multiplier defaults.
    // Binary @ 0x00125eb8: dead-code MP sync flag cleared to 0.
    game_work.m_bP2PReady = 0;
    m_SpeedScale = 1.0f;
    // ASM-spec v1.6.1: globalDt base is m_SpeedAccum (+0x78), not field_0x74
    m_SpeedAccum = m_SpeedClampStart[game_work.gameMode];

    // v1.6.1 WaveManager::Reset @0x0012ba78: iterate m_GlobalProbabilityOverride,
    // call slot4 NewGameStarted on each, then if (fullReset) NewGame().
    // ASM-spec v1.6.1 NewGameStarted loop @0x00125be4 area (before fullReset tail).
    for (size_t i = 0; i < m_GlobalProbabilityOverride.size(); ++i) {
        GlobalProbabilityOveride* g = m_GlobalProbabilityOverride[i];
        if (g) g->NewGameStarted();
    }

    if (fullReset)
        NewGame();
}

// ----------------------------------------------------------------------------
// Resume / SaveWaveInfo
// ----------------------------------------------------------------------------

// v1.6.1 SkipToPause @ 0x001cb424 — free function defined in PauseScreen.cpp.
// (Stale marker was 0x00169c48, a v1.5.1 address.)
// Implemented in screens/PauseScreen.cpp alongside PauseGame/UnpauseGame.

// ASM-spec corrected 2026-05-18: SkipToGameOver is also a free function at
// binary @ 0x0016ada0 (not 0x00125450). Binary body conditionally zeroes
// pCamera +0x10c and TimeControl +0x7c when IsTimedGame, then writes
// gs->bombHitTimer/m_TransitionTimer, conditionally fires GameOver,
// clears MainScreen flag_0xf8, then HUD::Skip(hud). Port stub kept until
// MainScreen::Hide + the camera/timecontrol field offsets are wired.
void SkipToGameOver(int /*goState*/, float /*goTimer*/,
                    float /*nextComboBonus*/, float /*bombHitTimer*/,
                    int /*field5*/) {
    // TODO: implement once MainScreen::Hide is exposed; full spec above.
}

// v1.6.1 WaveManager::Resume @ 0x0012bf58
void WaveManager::Resume() {
    // Resume is only called on restore-from-save, never on cold boot.
    // Cold boot uses WaveManager::NewGame() -> Reset(true) -> GetNextWave(0).

    Game* game = Game::GetInstance();
    if (!game) return;

    FruitSaveData* sd = game_work.m_SaveData;
    if (!sd) return;

    // Sentinel: if no active game was saved, nothing to restore.
    if (!sd->m_bHasActiveGame) return;

    // 1. Restore score + miss count.
    // Binary @ 0x00124b7c-0x00124b8c: calls SetScore(sd->m_CurrentScore, -1) +
    // SetMissCount(sd->m_CurrentMissCount, -1).
    SetScore(sd->m_CurrentScore, -1);
    SetMissCount((int)sd->m_CurrentMissCount, -1);

    // 2. Restore per-player base speed from save.
    // m_ComboTimer <- sd+0x100 (combo timer snapshot).
    // m_ColdTimer  <- sd+0x108 (cold-timer snapshot; binary vldr/vstr raw word move).
    m_ComboTimer = sd->m_Speed_P0;
    m_ColdTimer  = sd->m_Speed_P1;  // ASM-verified: v1.6.1 WaveManager::Resume @0x0012bf58 vldr/vstr -- raw word move

    // 3. Restore was-game-over flag.
    game_work.m_bUnsullied = sd->m_bWasGameOver;

    // 4. Re-roll all PROBABILITY_OVERIDE entries.
    // Port specific: PROBABILITY_OVERIDE::SelectType() not yet ported; skipped.
    // for each po in m_ProbabilityOverride[game_work.gameMode]: po.SelectType();

    // 5. Reset transient queue fields.
    m_MaxWaveIdP0 = 0;
    m_RecentFruitCount[0] = 1;
    for (int i = 0; i < 32; ++i)
        m_FruitQueue[i] = -1;

    // 6. Re-spawn saved entities from sd->m_EntityStates.
    // Binary @ 0x00124b9c-0x00124cd8: per-EntityState dispatch.
    // g_FruitCount = **DAT_00124f04; port derives via FruitInfo_GetCount().
    // Default spawn Vec3* = DAT_00124f08 -> GOT slot -> Save.cpp file-scope
    // global Vec3 (1,1,1), constructed in _GLOBAL__I_Save_cpp @ 0x0012bfe0
    // (same GOT offset 0x77cc as DAT_00124f08; the ctor literal is 1.0,1.0,1.0).
    // It is passed as Init's p3 = scale. Entity::Init's documented contract is
    // "p3 nullable, defaults to (1,1,1)", so passing nullptr is the exact
    // functional equivalent of passing &g_DefaultSpawnVec.
    // Binary @ 0x00124f08
    const int g_FruitCount = FruitInfo_GetCount();
    bool respawned = false;
    for (std::list<EntityState>::iterator eit = sd->m_EntityStates.begin();
         eit != sd->m_EntityStates.end(); ++eit)
    {
        EntityState& es = *eit;

        // Kind selection from m_KindIndex (binary @ 0x00124b9c):
        //   idx >= g_FruitCount -> kind=1 (Bomb)
        //   idx < 0             -> kind=4 (PowerUp)
        //   else                -> kind=0 (Fruit)
        int idx = es.m_KindIndex;
        int kind;
        if (idx >= g_FruitCount)     kind = 1;
        else if (idx < 0)            kind = 4;
        else                         kind = 0;

        Mortar::Entity* e = Mortar::ActorManager::GetInstance()->Add(kind, true);
        if (!e) continue;

        // vtable+0x08 = Init(this, 0, typeIndex, &defaultSpawnVec).
        // m_KindIndex serves as BOTH kind discriminator and Init type-index.
        e->Init(nullptr, (long)idx, nullptr);

        // Restore velocity then position (binary order: vel first, then pos).
        e->vel = Vec3(es.m_Velocity[0], es.m_Velocity[1], es.m_Velocity[2]);
        e->pos = Vec3(es.m_Position[0], es.m_Position[1], es.m_Position[2]);

        if (kind == 1) {
            // Bomb overlay -> m_AccelForce (acceleration/gravity Vec3 at binary +0x8c).
            // ASM @ 0x00124c50-0x00124c66: the kind==1 branch does a plain 3-word
            //   ldmia es+0x20,{r0,r1,r2}; stm bomb+0x8c,{r0,r1,r2}
            // block-copy of the EntityState overlay into the Bomb's m_AccelForce
            // Vec3. The decompiler's "m_RotAxis_z/m_PlayerIdx/m_TimeScale" labels
            // are a misread of the three m_AccelForce components -- there are no
            // separate fields; SaveWaveInfo stores m_AccelForce here on save.
            // Binary @ 0x00124b1c (kind==1 overlay restore).
            Bomb* b = static_cast<Bomb*>(e);
            b->m_AccelForce = Vec3(es.m_Overlay[0], es.m_Overlay[1], es.m_Overlay[2]);
            if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) b->SetForPlayer(1);
            // Chuck/SetHit gate: m_ChuckMag > 0.0f; m_BombHitFlag==0 -> Chuck, else -> SetHit.
            if (es.m_ChuckMag > 0.0f) {
                if (es.m_BombHitFlag == 0)
                    b->Chuck(es.m_ChuckMag);
                else
                    b->SetHit(es.m_ChuckMag);
            }
        } else if (kind == 0) {
            // Fruit overlay: m_Overlay = gravity Vec3.
            Fruit* f = static_cast<Fruit*>(e);
            f->m_Gravity = Vec3(es.m_Overlay[0], es.m_Overlay[1], es.m_Overlay[2]);
            if (es.m_ChuckMag > 0.0f)
                f->Chuck(es.m_ChuckMag);
        } else if (kind == 4) {
            // PowerUp ENTITY (ActorManager type 4) -- distinct from game/PowerUp.h
            // (the XML-template/modifier object). ASM @ 0x00124cc0-0x00124cd0:
            //   ldr r3,[entity+0x0]; vldr s0,[es+0x34]; ldr r3,[r3+0x10]; blx r3
            // i.e. a virtual call through the entity's own vtable slot +0x10 with
            // signature (float chuckMag, Entity*). The ported Mortar::Entity base
            // has Update(float) at +0x10, NOT this (float,Entity*) activate slot,
            // so the kind-4 PowerUp-entity class is entirely unported and has no
            // vtable to dispatch through.
            // TODO: v1.6.1 WaveManager::Resume @0x0012bf58 -- PowerUp-entity (ActorManager type 4) vtable+0x10
            //   activate(m_ChuckMag, e); blocked on unported PowerUp-entity subsystem.
        }
        respawned = true;
    }

    // 7. Mortar::ActorManager::Update(dt=0) to settle respawned entities.
    Mortar::ActorManager::GetInstance()->Update(0.0f);

    // 8. Arcade mode (m_GameMode == 2): PowerUpManager::LoadTextures().
    // Binary @ 0x0011840c — iterates m_AllPowerUps and m_ScreenEffectPool.
    if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
        PowerUpManager::GetInstance()->LoadTextures();
    }

    // 9. Branch selection: SkipToGameOver vs SkipToPause.
    bool gameOver = (sd->m_BombHitTimer > 0.0f && sd->m_GameMode != 2)
                    || (sd->m_GameOverScreenState >= 0);

    if (gameOver) {
        SkipToGameOver(sd->m_GameOverScreenState,
                       sd->m_GameOverTimer,
                       sd->m_NextComboBonus,
                       sd->m_BombHitTimer,
                       /*field5=*/-1);
    } else if ((respawned || !sd->m_WaveStates.empty())
               && sd->m_CurrentMissCount < 3) {
        SkipToPause(true);

        // Wave-state restore after SkipToPause.
        m_RecentFruitCount[0]  = sd->m_FruitQueueCount;
        memcpy(&m_FruitQueue[0], &sd->m_FruitQueue[0], 0x80);
        m_BlitzSpawnCount      = (uint8_t)sd->m_blitzSpawnedThisGame;
        m_BlitzState           = (uint8_t)sd->m_blitzForceSpawnedCounter;
        m_NextBlitzTime        = sd->m_blitzSpawnTime;
        m_NextWaveDelay_P0     = sd->m_WaveDelay;
        m_NextWaveDelay_P1     = sd->m_WaveWait;
        // ASM-spec v1.6.1: globalDt base is m_SpeedAccum (+0x78), not field_0x74
        m_SpeedAccum         = sd->m_ProbabilityOverideFlag;
        // sd->m_pCurrentWave_P1 (FruitSaveData+0x140) stores the SAVED WAVE INDEX
        // (uint), used to look up via the WaveState restore loop. The field name
        // inherited from earlier RE was misleading.
        m_SyncWaveIdx        = sd->m_pCurrentWave_P1;   // saved wave index
        m_ComboTimer         = sd->m_Speed_P0;
        m_ColdTimer          = sd->m_Speed_P1;  // ASM-verified: v1.6.1 WaveManager::Resume @0x0012bf58 vldr/vstr -- raw word move
        m_WaveActive = 1; m_SyncLocalReady = 1;
        m_SyncRemotePending = 0; m_SyncReceived = 0;
        m_ComboSpeed         = sd->m_Speed_P0_alias;
        m_TargetComboSpeed   = sd->m_Speed_P0_alias;
        // Binary: hash of "blitz_bonus" — same key AddSpeed increments.
        m_BlitzLevel = sd->GetTotal("blitz_bonus");

        ResetWaveChances();

        // Binary @ 0x00124d20-0x00124df8: WaveState/SpawnState restore.
        int mode = game_work.gameMode;
        for (std::list<WaveState>::iterator wit = sd->m_WaveStates.begin();
             wit != sd->m_WaveStates.end(); ++wit)
        {
            WaveState& ws = *wit;
            // ws.index = sequential index into m_WaveInfo[mode] (SaveWaveInfo stores candidateIdx).
            // ws.waveIdx = revisit counter (m_RevisitCounter).
            if (ws.index < 0 || ws.index >= (int)m_WaveInfo[mode].size()) continue;
            WAVE_INFO* w = m_WaveInfo[mode][ws.index];
            w->m_RevisitCounter = (float)ws.waveIdx;
            if (!ws.spawners.empty()) {
                m_pCurrentWave[0] = w;
                int s = 0;
                for (std::list<SpawnState>::iterator sit = ws.spawners.begin();
                     sit != ws.spawners.end(); ++sit, ++s)
                {
                    if (s >= w->m_SpawnerCount) break;
                    SPAWNER_INFO& sp = w->m_pSpawners[s];
                    sp.m_SpawnTimer     = sit->timer;
                    sp.m_RemainingCount = (int)sit->count;
                    sp.m_reserved58     = 0;
                    sp.m_SpawnCountF    = sit->count;
                    sp.SelectTypes();
                }
            }
        }
    }

    // 10. Copy ShakeIntensity/ShakeDecay to fade screen; clear m_EntityStates.
    // TODO: fade-screen shake fields not yet mapped.
    sd->m_EntityStates.clear();
}

int WaveManager::SaveWaveInfo(FruitSaveData* sd) {
    // v1.6.1 WaveManager::SaveWaveInfo @ 0x001254b0
    if (!sd) return 0;

    sd->m_Speed_P0       = 0.0f;
    sd->m_Speed_P0_alias = 0.0f;
    sd->m_Speed_P1       = 0.0f;

    sd->m_blitzSpawnedThisGame     = m_BlitzSpawnCount;
    sd->m_blitzSpawnTime           = m_NextBlitzTime;
    sd->m_blitzForceSpawnedCounter = m_BlitzState;

    // Binary @ 0x001254b0 writes WaveManager+0x78 into FruitSaveData::m_WaveScalar_v161.
    sd->m_WaveScalar_v161 = m_SpeedScale;

    sd->m_WaveStates.clear();

    Game* game = Game::GetInstance();
    if (!game) return 0;

    // Sentinel: only save if single-player (m_bSplitPlayerWaves == 0 or waveCount < 0)
    // and waves are loaded for this mode.
    bool splitPlayer = IsSameScreenMultiplayer();
    if ((!splitPlayer || m_WaveCount[1] < 0)
        && !m_WaveInfo[game_work.gameMode].empty())
    {
        // ASM-spec v1.6.1: globalDt base is m_SpeedAccum (+0x78), not field_0x74
        sd->m_ProbabilityOverideFlag = m_SpeedAccum;

        static const int MAX_CAND = 20;
        WAVE_INFO* candidates[MAX_CAND];
        int candidateIdx[MAX_CAND];
        int numCandidates = 0;
        int waveIdx = 0;
        for (std::vector<WAVE_INFO*>::iterator wit = m_WaveInfo[game_work.gameMode].begin();
             wit != m_WaveInfo[game_work.gameMode].end(); ++wit) {
            WAVE_INFO* wi = *wit;
            if (wi->m_ScoreThreshold <= m_WaveCount[0]
                && (m_WaveCount[0] <= wi->m_EndScore || wi->m_EndScore == -2))
            {
                if (numCandidates < MAX_CAND) {
                    candidates[numCandidates]  = wi;
                    candidateIdx[numCandidates] = waveIdx;
                    ++numCandidates;
                }
            }
            ++waveIdx;
        }

        for (int i = 0; i < numCandidates; ++i) {
            WaveState state;
            state.index   = candidateIdx[i];
            state.waveIdx = (int)candidates[i]->m_RevisitCounter;  // revisit counter
            state.spawners.clear();
            if (candidates[i] == m_pCurrentWave[0]) {
                for (int s = 0; s < candidates[i]->m_SpawnerCount; ++s) {
                    SpawnState ss;
                    SPAWNER_INFO& sp = candidates[i]->m_pSpawners[s];
                    ss.timer = sp.m_SpawnTimer;
                    ss.count = sp.m_SpawnCountF;
                    state.spawners.push_back(ss);
                }
            }
            sd->m_WaveStates.push_back(state);
        }

        sd->m_pCurrentWave_P1 = m_WaveCount[0];
        sd->m_FruitQueueCount = m_MaxWaveIdP0;
        sd->m_WaveDelay       = m_NextWaveDelay_P0;
        sd->m_WaveWait        = m_NextWaveDelay_P1;
        sd->m_Speed_P0        = m_ComboSpeed;
        sd->m_Speed_P1        = m_ColdTimer;  // ASM-verified: v1.6.1 WaveManager::SaveWaveInfo @0x001254b0 vldr/vstr -- raw word move
        sd->m_Speed_P0_alias  = m_TargetComboSpeed;
        memcpy(&sd->m_FruitQueue[0], &m_FruitQueue[0], 0x80);
        // ASM-verified: v1.6.1 WaveManager::SaveWaveInfo @0x001254b0 reads +0x2d0 (m_RecentFruitCount[0]) into save+0x7c.
        sd->m_FruitQueueCount = m_RecentFruitCount[0];
        return 1;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// GameOver / NewGame
// ----------------------------------------------------------------------------

void WaveManager::GameOver() {
    // ASM-verified: 2026-05-02 v1.6.1 binary @ 0x00121f74 -- ResetGlobalDt first, then PowerUpManager::Reset.
    WaveManager* self = GetInstance();
    if (self) self->ResetGlobalDt(1.0f);
    if (PowersEnabled()) {
        PowerUpManager::GetInstance()->Reset(false);
    }
}

void WaveManager::NewGame() {
    // ASM-verified: 2026-05-02 v1.6.1 binary @ 0x00121f90 -- ResetGlobalDt first, then PowerUpManager::Reset.
    WaveManager* self = GetInstance();
    if (self) self->ResetGlobalDt(1.0f);
    if (PowersEnabled()) {
        PowerUpManager::GetInstance()->Reset(true);
    }
}

// ASM-verified: 2026-06-24 v1.6.1 PowersEnabled @ 0x0011a034 (thunk 0x001069e0) (asm-inspector)
//   Cross-compile matches binary instruction-for-instruction: ldrb game_work+0x4 == 2, movne/moveq.
bool PowersEnabled() {
    // Binary: ldrb game_work+0x4 (1-byte gameMode) == 2 (GAME_MODE_ARCADE); no Game::GetInstance guard.
    return game_work.gameMode == Mortar::GAME_MODE_ARCADE;
}

void WaveManager::ResetGlobalDt(float dt) {
    // v1.6.1 WaveManager::ResetGlobalDt @ 0x0012b770
    // Walks m_ProbabilityOverride[gameMode], erasing entries
    // with m_SelectedType >= 0; advances past those with m_SelectedType < 0.
    // DIVERGES fix: binary checks *(it+0x74) = m_SelectedType, not m_PerWaveCount (+0x70).
    // Binary @ 0x00121ee8 confirms ldr from offset +0x74 of PROBABILITY_OVERIDE.
    Game* game = Game::GetInstance();
    if (game) {
        std::vector<PROBABILITY_OVERIDE>& vec = m_ProbabilityOverride[game_work.gameMode];
        for (std::vector<PROBABILITY_OVERIDE>::iterator it = vec.begin(); it != vec.end(); ) {
            if (it->m_SelectedType < 0) {
                ++it;
            } else {
                it = vec.erase(it);
            }
        }
    }
    // ASM-spec v1.6.1: globalDt base is m_SpeedAccum (+0x78), not field_0x74
    m_SpeedAccum        = dt;
    m_StepAccumulator   = 0.0f;   // m_StepAccumulator (+0x2dc)
}

void WaveManager::ResetWaveChances() {
    // Reset m_CurrentChance (+0x40) back to m_Chance (+0x3c) for each wave in current mode.
    // Binary @ 0x001249d0: also resets m_CurrentRegrowth (+0x48) = m_ChanceRegrowth (+0x44)
    // and m_RevisitCounter (revisit counter) = 1.0.
    Game* game = Game::GetInstance();
    if (!game) return;
    for (std::vector<WAVE_INFO*>::iterator it = m_WaveInfo[game_work.gameMode].begin();
         it != m_WaveInfo[game_work.gameMode].end(); ++it) {
        WAVE_INFO* wi = *it;
        wi->m_CurrentChance   = wi->m_Chance;
        wi->m_CurrentRegrowth = wi->m_ChanceRegrowth;
        wi->m_RevisitCounter        = 1.0f;
    }
}

// ----------------------------------------------------------------------------
// Update — fixed timestep pump (wave-system-impl.md §8)
// ----------------------------------------------------------------------------

void WaveManager::Update(float dt) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Reset per-frame multipliers.
    // v1.6.1 WaveManager::Update @0x001267a0
    m_SpawnLevel     = 1.0f;
    m_BombChance     = 1.0f;
    m_FruitChance    = 1.0f;
    m_CritChanceMult = 1.0f;
    // v1.6.1 WaveManager::Update @0x001267a0: drive m_SpeedScale from PowerUpManager's
    // m_DtMod (modifier dt mult: freeze<1 slows, frenzy>1 speeds spawn cadence). PUM::Update
    // IS ported (the old "not ported" note was stale) -- this was why arcade freeze/frenzy
    // never affected spawn cadence.
    {
        float comboDt = dt * m_ComboSpeedDivisor;   // +0x80, always 1.0
        PowerUpManager* pum = PowerUpManager::GetInstance();
        if (game_work.m_PauseAmount >= 1.0f || !PowersEnabled()) {   // flM_PauseAmount @+0x0C
            pum->SetDefaults();
            m_SpeedScale = 1.0f;
        } else {
            pum->Update(comboDt);
            m_SpeedScale = pum->m_DtMod;            // +0x64
        }
    }

    // Wave speed accumulator: per-mode inc=0 in all shipped modes (globalDtInc absent),
    // so m_SpeedAccum stays at m_SpeedClampStart (1.0) and the clamp is a no-op.
    {
        int mode = game_work.gameMode;
        // Binary @ 0x001267a0: accumulator is at +0x78 (m_SpeedAccum), NOT +0x74.
        // binary @ 0x00125ac4: speed = m_SpeedAccum + dt * *(float*)(&this->m_DtIncPerMode + gameMode*4)
        float s = m_SpeedAccum + dt * m_DtIncPerMode[mode];
        float lo = m_SpeedClampStart[mode];
        float hi = m_SpeedClampMax[mode];
        m_SpeedAccum = (s < lo) ? lo : (s < hi) ? s : hi;
    }

    // Time accumulator — game->field_0x1ac not mapped in port Game struct.
    // TODO: skip stat tracking (game->field_0x1ac += dt).

    // Spawn-pump gate (binary @ 0x00125a30 / 0x00125a62):
    //   if (g_GameData->bM_bPaused[+0x05] == 0 || this+0x230 (m_WaveCount[0] in SP) <= 0) {
    //       <fixed-step UpdateWave loop>
    //   } else {
    //       UpdateComboSpeed(dt);  // bM_bPaused=1 AND wave already loaded -> combo tick only
    //   }
    //
    // Binary +0x230 is DUAL-PURPOSE storage: m_pCurrentWave[1] in MP,
    // m_WaveCount[0] in SP. Port keeps these as separate fields; SP semantic reads
    // m_WaveCount[0] directly.
    //
    // Menu suppression flow (cold boot / menu state):
    //   - GameInit @0x1ce1c0 sets bM_bPaused[+0x05]=1, bM_Mode[+0x02]=0 (menu state).
    //   - Ctor BSS-zeros m_WaveCount[0] = 0.
    //   - Frame 1: bM_bPaused=1, m_WaveCount[0]=0 -> gate FALSE -> spawn pump runs ->
    //     UpdateWave with m_pCurrentWave[0]==null -> wave-end -> GetNextWave(0)
    //     populates m_pCurrentWave[0] AND increments m_WaveCount[0] to 1.
    //     No fruit spawns (spawn body skipped because wave was null on first call).
    //   - Frame 2+: bM_bPaused=1, m_WaveCount[0]=1 -> gate TRUE -> only UpdateComboSpeed runs.
    //   - PrepareForLevelStart -> Reset sets m_WaveCount[0]=-1 then GetNextWave
    //     bumps to 0; bM_bPaused cleared to 0 -> gate always FALSE -> spawn pump runs
    //     for real gameplay.
    // ASM-verified: 2026-05-20 v1.6.1 binary @ 0x00125a62 (re-analyst).
    if (game_work.bM_bPaused == 0 || m_WaveCount[0] <= 0) {
        float accumDt = m_StepAccumulator + dt;
        while (accumDt > WAVE_STEP) {
            // ASM-verified: 2026-06-21T00:00:00Z v1.6.1 WaveManager::Update @0x001267a0 (re-analyst):
            //   call UpdateWave only when m_WaveInfo[gameMode] is non-empty; otherwise the
            //   menu pump would deref a stale m_pCurrentWave[mode] (dangling from the prior
            //   game) -> ACCESS_VIOLATION, and spawn fruit with that wave's garbage
            //   SPAWNER_INFO.m_TimeScale (+0x14) -> over-fast spin (bombs ignore +0x14).
            if (!m_WaveInfo[game_work.gameMode].empty()) {
                UpdateWave(WAVE_STEP, 0, game_work.gameMode);
            }
            accumDt -= WAVE_STEP;
        }
        m_StepAccumulator = accumDt;
    } else {
        // v1.6.1 WaveManager::Update @0x001267a0: passes dt * m_ComboSpeedDivisor to UpdateComboSpeed.
        UpdateComboSpeed(dt * m_ComboSpeedDivisor);
    }
    // Removed per-frame Update spam; spawn events themselves print via [Spawn].

    // Binary @ 0x00125b64 dispatches a vector::size() whose result is unused.
    // Likely a debug/profiling watchpoint left in. Elided in port.
}

// ----------------------------------------------------------------------------
// UpdateWave — per docs/functions/wave.md (298 lines)
// ----------------------------------------------------------------------------

// Wave-end gate flag. Binary @ 0x00132f70 (TU-local file-scope static byte
// in WaveManager's translation unit, NOT a struct member). Cleared at the
// top of UpdateWave; set to 1 in the pre-spawn-timer-ticking branch (when
// m_NextWaveDelay_P0 > 0); read by the wave-end block to suppress GetNextWave
// while the pre-spawn delay is still counting down.
// ASM-verified: 2026-05-10 v1.6.1 binary @ 0x001253b0 / 0x00125928 / 0x0012593e
// (asm-inspector). Without this gate the wave-end block fires GetNextWave
// every frame the pre-spawn delay is active (no entities yet but timer is
// ticking), which resets m_NextWaveDelay_P0 -> infinite loop -> first wave never
// spawns.
static bool s_PreSpawnTickedThisFrame = false;

void WaveManager::UpdateWave(float dt, int playerIdx, int /*unk*/) {
    // v1.6.1 binary @ 0x00125d7c: TU-local static still_spawning flag cleared at entry.
    // ASM: 00125da8 strb r2,[r3,#0x448] sets byte at GOT+0x448 to 0.
    s_PreSpawnTickedThisFrame = false;

    // v1.6.1 binary @ 0x00125dac: gate entry — check first byte of this+0x00
    // (m_SpeedControl[0]). If non-zero (SpeedControl already created), skip to
    // wave-end check. Non-Arcade modes never create SpeedControl, so gate is
    // always open. Arcade creates SpeedControl on first frame, gate closes
    // after — spawn loop only runs in burst (Arcade waves use delay=0).
    // ASM: ldrb r3,[r0,#0x0]; cmp r3,r2; bne epilogue
#if defined(__bada__)
    // v1.6.1 binary register-level access: offset-based per-player aliased slots.
    WAVE_INFO*& pCurrentWave = *(WAVE_INFO**)((uint8_t*)this + 0x234 + playerIdx * 4);
    int& waveCount = *(int*)((uint8_t*)this + 0x238 + playerIdx * 4);
    float& delay = *(float*)((uint8_t*)this + 0x23c + playerIdx * 4);
    float& wait = *(float*)((uint8_t*)this + 0x240 + playerIdx * 4);
    // Gate check: first byte of this+0x00 (m_SpeedControl[0] LSB on __bada__).
    if (*(const uint8_t*)this != 0)
        goto wave_end_check;
#else
    WAVE_INFO*& pCurrentWave = m_pCurrentWave[playerIdx];
    int& waveCount = m_WaveCount[playerIdx];
    float& delay = m_NextWaveDelay_P0;     // SP only
    float& wait = m_NextWaveDelay_P1;      // SP only
    // Gate check: v1.6.1 binary @ 0x00125dac: ldrb r3,[r0,#0x0]; cmp r3,r2; bne epilogue.
    if (m_SpeedControl[0] != nullptr)
        goto wave_end_check;
#endif

    UpdateComboSpeed(dt);

    // v1.6.1 binary @ 0x00125dbc-0x00125de4: MP retry timer increment (stub).
    // game_work.m_bMPRetryPending -> m_NetTimerA += dt; m_NetTimerB += dt.
    // Defunct: P2P MP timers unused in port.

    // v1.6.1 binary @ 0x00125de8-0x00125df8: UpdateNetworking gate.
    // If non-zero (network busy), skip spawn — fall through to wave-end check.
    if (UpdateNetworking(dt, playerIdx) == 0 && pCurrentWave != 0) {

        // v1.6.1 binary @ 0x00126710-0x00126728: two-way branch on delay slot.
        if (delay <= 0.0f) {
            // v1.6.1 binary @ 0x00125e14: *(undefined4*)(slot) = 0 (clamp).
            delay = 0.0f;

            // Process each spawner.
            for (int s = 0; s < pCurrentWave->m_SpawnerCount; ++s) {
                SPAWNER_INFO& spawner = pCurrentWave->m_pSpawners[s];

                float dtMod = m_SpeedScale;
                if (dtMod < 1.0f) dtMod = 1.0f;

                spawner.m_SpawnTimer -= dt * dtMod;

                while (spawner.m_RemainingCount > 0) {
                    if (spawner.m_SpawnTimer > 0.0f) break;

                    if (spawner.m_FruitTypeCount < 1) {
                        spawner.m_SpawnTimer = 0.0f;
                        spawner.m_RemainingCount = 0;
                        break;
                    }

                    // Pick a random type from the spawner's list.
                    int typeIdx = (spawner.m_FruitTypeCount > 1)
                        ? (int)m_Random.Rand32((uint32_t)spawner.m_FruitTypeCount)
                        : 0;
                    int fruitType = spawner.m_pFruitTypeHashes
                        ? spawner.m_pFruitTypeHashes[typeIdx]
                        : -1;

                    if (fruitType == -2) {
                        SpawnBomb(1, &spawner, playerIdx);
                    } else if (fruitType == -1) {
                        // Probabilistic override blitz selection.
                        int chosenType = -1;
                        // ASM-spec v1.6.1 WaveManager::UpdateWave override block @0x00126124-0x0012676c:
                        // corner-spawner pick, set only when the override block below resolves a
                        // super-fruit or a still-active-power skip; overrides blitzSpawner/spawner.
                        SPAWNER_INFO* powerSpawner = 0;

                        int blitzAdvance = 0;
                        bool gateOpen = false;
                        if (game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
                            float timeRemaining = 0.0f;
                            float countdownStart = 0.0f;
                            if (game_work.mCountDown) {
                                timeRemaining  = game_work.mCountDown->m_TimeRemaining;
                                countdownStart = game_work.mCountDown->GetCountDown();
                            }
                            if (countdownStart - m_NextBlitzTime >= timeRemaining) {
                                gateOpen = true;
                            }
                        }

                        if (!gateOpen && game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
                            blitzAdvance = 0;
                        } else if (m_BlitzState == 0) {
                            m_BlitzState = 1;
                            blitzAdvance = (m_BlitzSpawnCount < 2) ? (1 - (int)m_BlitzSpawnCount) : 0;
                            m_NextBlitzTime = m_Random.RandF(10.0f) + 35.0f;
                        } else if (m_BlitzState == 1 && m_BlitzSpawnCount == 1) {
                            m_BlitzState = 2;
                            blitzAdvance = 1;
                        } else {
                            blitzAdvance = 0;
                        }

                        static bool s_BlitzSpawnersInited = false;
                        static SPAWNER_INFO k_BlitzSpawners[3];
                        if (!s_BlitzSpawnersInited) {
                            s_BlitzSpawnersInited = true;
                            k_BlitzSpawners[0].m_TimeScale  = 0.75f;
                            k_BlitzSpawners[0].m_Gravity_x  = 0.0f;
                            k_BlitzSpawners[0].m_Gravity_y  = -1.1f;
                            k_BlitzSpawners[0].m_Gravity_z  = 0.0f;
                            k_BlitzSpawners[0].m_HorizMin   = 0.6660f;
                            k_BlitzSpawners[0].m_HorizMax   = 0.0f;
                            k_BlitzSpawners[0].m_SpawnMin   = -0.5f;
                            k_BlitzSpawners[0].m_SpawnMax   = 0.5f;
                            k_BlitzSpawners[0].m_SpawnType  = PLACEMENT_BOTTOM_SLOW;
                            k_BlitzSpawners[1].m_TimeScale  = 0.75f;
                            k_BlitzSpawners[1].m_Gravity_x  = 0.0f;
                            k_BlitzSpawners[1].m_Gravity_y  = -1.1f;
                            k_BlitzSpawners[1].m_Gravity_z  = 0.0f;
                            k_BlitzSpawners[1].m_HorizMin   = 0.6660f;
                            k_BlitzSpawners[1].m_HorizMax   = 0.0f;
                            k_BlitzSpawners[1].m_SpawnMin   = -1.0f;
                            k_BlitzSpawners[1].m_SpawnMax   = -0.5f;
                            k_BlitzSpawners[1].m_SpawnType  = PLACEMENT_RIGHT;
                            k_BlitzSpawners[2].m_TimeScale  = 0.75f;
                            k_BlitzSpawners[2].m_Gravity_x  = 0.0f;
                            k_BlitzSpawners[2].m_Gravity_y  = -1.1f;
                            k_BlitzSpawners[2].m_Gravity_z  = 0.0f;
                            k_BlitzSpawners[2].m_HorizMin   = 0.6660f;
                            k_BlitzSpawners[2].m_HorizMax   = 0.75f;
                            k_BlitzSpawners[2].m_SpawnMin   = -1.0f;
                            k_BlitzSpawners[2].m_SpawnMax   = -0.5f;
                            k_BlitzSpawners[2].m_SpawnType  = PLACEMENT_LEFT;
                        }
                        SPAWNER_INFO* blitzSpawner = (blitzAdvance && game_work.gameMode == Mortar::GAME_MODE_ARCADE)
                            ? &k_BlitzSpawners[m_Random.Rand32(3)]
                            : nullptr;
                        int mode = game_work.gameMode;
                        std::vector<PROBABILITY_OVERIDE>& overrides = m_ProbabilityOverride[mode];

                        if (!overrides.empty()) {
                            if (pCurrentWave->m_OverideProbabilityPool > 0) {
                                int roll = (int)m_Random.Rand32((uint32_t)pCurrentWave->m_OverideProbabilityPool);
                                int cumulative = 0;
                                for (std::vector<PROBABILITY_OVERIDE>::iterator oit = overrides.begin();
                                     oit != overrides.end(); ++oit)
                                {
                                    PROBABILITY_OVERIDE& po = *oit;
                                    if (po.m_PerWave > 0 && po.m_Counter >= po.m_PerWave) continue;
                                    // ASM-verified: 2026-05-27 v1.6.1 binary @ 0x00125606..0x00125622 (re-analyst).
                                    // Uses m_WaveCount[playerIdx] at binary offset this+0x238+p*4.
                                    if (waveCount >= 0
                                            && po.m_PerWaveCount > 0
                                            && waveCount < po.m_PerWaveCount) continue;
                                    if (po.m_DisableWhenPowered > 0.0f) {
                                        float prog = PowerUpManager::GetInstance()
                                                         ? PowerUpManager::GetInstance()->GetActiveProgression(0.0f)
                                                         : 2.0f;
                                        if (po.m_DisableWhenPowered >= prog) continue;
                                    }
                                    int pc = po.m_PercentChance;
                                    if (m_BlitzSpawnCount > 5) pc >>= 1;
                                    cumulative += pc;
                                    if (roll < cumulative || (po.m_PercentChance > 0 && blitzAdvance != 0)) {
                                        // ASM-spec v1.6.1 WaveManager::UpdateWave @0x00126124: timed-power
                                        // allowance reject. Empty on all shipped data (m_PowerAllowance
                                        // never populated by arcadewavelist.xml) -- inert, kept for call-graph
                                        // parity (#340).
                                        int allowSz = (int)po.m_PowerAllowance.size();
                                        if (allowSz > 0) {
                                            int numActive = PowerUpManager::GetInstance()->GetNumActiveTimedPowers();
                                            int idx = (numActive < allowSz) ? numActive : (allowSz - 1);
                                            if (po.m_PowerAllowance[idx] <= (int)m_Random.Rand32(100))
                                                break;
                                        }
                                        // ASM-spec v1.6.1 WaveManager::UpdateWave override block
                                        // @0x00126124-0x0012676c: retry GetType() up to 30x while the
                                        // picked type's powers are already active elsewhere (avoid
                                        // stacking duplicate power-fruits), then pick a corner spawner
                                        // for super-fruits / power-fruits and gate the blitz-spawn count.
                                        chosenType = po.GetType();
                                        const FruitInfo* fi = (chosenType >= 0) ? FruitInfo_Get(chosenType) : nullptr;
                                        const FRUIT_POWERS* powers = fi ? fi->m_pPowers : nullptr;
                                        bool hasPowers = false;
                                        for (int n = 30; ; --n) {
                                            hasPowers = (powers != nullptr);
                                            if (!powers || n == 0) break;
                                            if (!powers->AnyActivePowers()) break;  // usable type found
                                            chosenType = po.GetType();              // retry
                                            fi = (chosenType >= 0) ? FruitInfo_Get(chosenType) : nullptr;
                                            powers = fi ? fi->m_pPowers : nullptr;
                                        }

                                        if (fi && fi->m_bIsSuperFruit)
                                            powerSpawner = GetRandomPowerSpawner(false);  // exclude centre
                                        if (hasPowers) {
                                            if (powers->AnyActivePowers()) {
                                                // Retries exhausted, still colliding with an active
                                                // power -- spawn goes ahead with this chosenType as-is,
                                                // no corner override, no blitz-count credit.
                                            } else {
                                                powerSpawner = GetRandomPowerSpawner(true);  // include centre; overrides corner pick
                                                if (m_FruitChance > 0.0f && Fruit::NumberOfPowerupFruits() < 1
                                                        && (game_work.m_SaveData->m_TimeRemainingSave >= 8.0f
                                                            || powers->m_pArray[0].m_PowerHash == StringHash("freeze"))
                                                        && !powers->AnyActivePowers())
                                                    m_BlitzSpawnCount++;
                                            }
                                        }
                                        po.m_Counter++;
                                        break;
                                    }
                                }
                            }
                        }

                        // v1.6.1 WaveManager::UpdateWave @LAB_001263ec: GlobalProbabilityOveride check.
                        // ASM-spec v1.6.1 CheckForGlobalProbabilityOveride @0x00123228.
                        // Only fires when no per-wave override already picked a type (chosenType < 0).
                        GlobalProbabilityOveride* gpo = 0;
                        if (chosenType < 0)
                            gpo = CheckForGlobalProbabilityOveride(chosenType);
                        if (chosenType < 0) {
                            chosenType = Fruit::RandomFruit(false);
                        }
                        Mortar::Entity* spawnedGPO = SpawnFruit(1, chosenType,
                            powerSpawner ? powerSpawner : (blitzSpawner ? blitzSpawner : &spawner), playerIdx);
                        // Subscribe Entity events for GPO re-arm when the super-fruit is killed/expires.
                        // ASM-spec v1.6.1 UpdateWave @0x001263ec: onKilled (+0x178) -> FruitWasKilled,
                        //   onThrown (+0x180) -> FruitWasThrown.
                        if (gpo && spawnedGPO) {
                            Fruit* spawnedFruit = static_cast<Fruit*>(spawnedGPO);
                            spawnedFruit->m_OnKilled +=
                                Mortar::Delegate1<void, Fruit*>::Make(gpo,
                                    &GlobalProbabilityOveride::FruitWasKilled);
                            spawnedFruit->m_OnExpired +=
                                Mortar::Delegate1<void, Fruit*>::Make(gpo,
                                    &GlobalProbabilityOveride::FruitWasThrown);
                        }
                    } else {
                        SpawnFruit(1, fruitType, &spawner, playerIdx);
                    }

                    m_WaveActive = 1;
                    spawner.m_RemainingCount--;

                    float spawnDt = spawner.m_Delay - spawner.m_DelayInc * pCurrentWave->m_RevisitCounter;
                    if (spawnDt < 0.0f) spawnDt = 0.0f;
                    spawner.m_SpawnTimer += spawnDt;
                }
            }
        } else {
            // v1.6.1 binary @ 0x001265ec-0x00126600: delay > 0 branch.
            delay -= dt;
            s_PreSpawnTickedThisFrame = true;  // still_spawning = 1
        }
    }

wave_end_check:
    // v1.6.1 binary @ 0x00126604-0x00126780: wave-end block (unconditional entry).
    if (!IsWaveProcessing(playerIdx) && !s_PreSpawnTickedThisFrame) {
        if (pCurrentWave == 0) {
            // wave was null — fall through to GetNextWave directly.
        } else if (wait > 0.0f) {
            // v1.6.1 binary @ 0x00126654-0x00126668: write decremented wait back,
            // then suppress GetNextWave if still positive.
            wait -= dt;
            if (wait > 0.0f)
                goto epilogue;
        }
        // v1.6.1 WaveManager::UpdateWave @0x0012666c: freeze wave-hold gate.
        // Advance UNLESS single-player + game_work.bM_bPaused + freeze NOT active.
        if (!IsOnlineMultiplayer() && game_work.bM_bPaused != 0) {
            static uint32_t s_FreezeHash = StringHash("freeze");
            if (PowerUpManager::GetInstance()->GetActiveSingle(s_FreezeHash) == 0)
                goto epilogue;
        }
        GetNextWave(playerIdx);
    }

epilogue:
    ;  // matching binary's branch-to-epilogue pattern
}

void WaveManager::UpdateComboSpeed(float dtIn) {
    // v1.6.1 WaveManager::UpdateComboSpeed @0x001238dc
    // ASM-verified: 2026-05-20 v1.6.1 binary @ 0x00122f5e (re-analyst). DUAL gate:
    //   (game_work.m_PauseAmount == 0.0f) AND (gameMode == ARCADE)
    // game_work.m_PauseAmount (+0x0C) is the pause/fade indicator: 0.0f during
    // active gameplay, non-zero during pause/gameover/quit transitions
    // (e.g. -1.0f post-quit-to-main). The previous port comment claiming
    // "m_PauseAmount not in port" was wrong (see GameWork.h:39).
    // Without the m_PauseAmount == 0 half, quit-to-main from Arcade leaves
    // gameMode==ARCADE and the body lazy-recreates SpeedControl in the
    // menu HUD, leaking the empty-gauge frame.
    Game* game = Game::GetInstance();
    if (!game) return;
    if (game_work.m_PauseAmount != 0.0f) return;
    if (game_work.gameMode != Mortar::GAME_MODE_ARCADE) return;

    // Ease m_ComboSpeed (+0x58) toward target (or 0 if target < 2.9).
    // Binary: divide both ease delta and decay by m_ComboSpeedDivisor (+0x80).
    // v1.6.1 UpdateComboSpeed @0x001238dc
    float curSpeed  = m_ComboSpeed;        // +0x58
    float target    = m_TargetComboSpeed;  // +0x5c
    if (target < 2.9f) target = 0.0f;     // DAT_001230dc/e0

    float easeRate = (dtIn * -5.0f) / m_ComboSpeedDivisor;
    float delta;
    if (curSpeed == target)         delta = 0.0f;
    else if (target < curSpeed)     delta = std::max(target - curSpeed, easeRate);
    else                            delta = std::min(target - curSpeed, -easeRate);
    m_ComboSpeed = curSpeed + delta;

    // Binary @ 0x00122f50: lazy SpeedControl alloc + push to HUD.
    // Widget lives in slot [1] (+0x04); slot [0] (+0x00) is the spawn-gate (always 0 in SP).
    float cur = m_ComboSpeed;
    HUDControl3d* sc = m_SpeedControl[1];
    if (!sc) {
        sc = new SpeedControl();
        m_SpeedControl[1] = sc;
        // ASM-verified: v1.6.1 WaveManager::UpdateComboSpeed @0x001238dc (re-analyst) constructs a
        // Delegate1<void, HUDControl*>::QCallee<WaveManager>(this, &DeleteSpeedControl)
        // and stores it into sc->m_RemoveCallback so HUD::Release nulls
        // m_SpeedControl when the control is torn down on GameExit.
        sc->m_RemoveCallback = Mortar::Delegate1<void, HUDControl*>::Make(
            this, &WaveManager::DeleteSpeedControl);
        if (game_work.mHud) game_work.mHud->AddControl(sc, false);
    }
    SpeedControl* spc = static_cast<SpeedControl*>(sc);
    spc->m_DisplayedSpeed = cur;
    spc->m_Speed          = m_ComboTimer;  // +0x50

    // Decay m_ComboTimer (+0x50) via GetWavedt/m_ComboSpeedDivisor/m_NextWaveSpeedLoss.
    // v1.6.1 UpdateComboSpeed @0x001238dc: wd = clamp(GetWavedt(0)/m_ComboSpeedDivisor, ...1.0)
    if (m_ComboTimer > 0.0f && m_pCurrentWave[0] != nullptr
        && m_pCurrentWave[0]->m_NextWaveSpeedLoss > 0.0f)
    {
        float wd = GetWavedt(0) / m_ComboSpeedDivisor;
        if (wd > 1.0f) wd = 1.0f;
        m_ComboTimer -= (wd * dtIn) / m_pCurrentWave[0]->m_NextWaveSpeedLoss;
        if (m_ComboTimer <= 0.0f)
            ResetSpeed(0);
    }
}

// ----------------------------------------------------------------------------
// GetNextWave — per docs/functions/wave.md (227 lines)
// ----------------------------------------------------------------------------

void WaveManager::GetNextWave(int playerIdx) {
#if defined(__bada__)
    // v1.6.1 binary @ 0x0012573c: offset-based per-player aliased slot access.
    WAVE_INFO*& pCurrentWave = *(WAVE_INFO**)((uint8_t*)this + 0x234 + playerIdx * 4);
    int& waveCount = *(int*)((uint8_t*)this + 0x238 + playerIdx * 4);
    float& delay = *(float*)((uint8_t*)this + 0x23c + playerIdx * 4);
    float& wait = *(float*)((uint8_t*)this + 0x240 + playerIdx * 4);
#else
    WAVE_INFO*& pCurrentWave = m_pCurrentWave[playerIdx];
    int& waveCount = m_WaveCount[playerIdx];
    float& delay = m_NextWaveDelay_P0;     // SP only (SSM would need array)
    float& wait = m_NextWaveDelay_P1;      // SP only (SSM would need array)
#endif
    Game* game = Game::GetInstance();
    if (!game) return;

    waveCount++;
    LOG_DEBUG("WaveManager", "GetNextWave(p=%d) waveCount=%d mode=%d waveInfos=%zu",
              playerIdx, waveCount, (int)game_work.gameMode,
              m_WaveInfo[game_work.gameMode].size());

    // Speed ramp: increment revisit counter on previously-visited wave.
    if (waveCount > 1 && pCurrentWave)
        pCurrentWave->m_RevisitCounter += 1.0f;

    // Wave queue path (survival/combo — null in normal play).
    if (m_pWaveQue) {
        // TODO: queue path not ported.
        return;
    }

    // Score-based selection.
    int gm = game_work.gameMode;
    int totalWeight = 0;
    int matchCount = 0;
    static const int MAX_CANDIDATES = 20;
    WAVE_INFO* candidates[MAX_CANDIDATES];

    // ASM-verified: 2026-06-21T00:00:00Z v1.6.1 WaveManager::GetNextWave @0x00125884 (re-analyst):
    //   seed m_pCurrentWave[mode] = m_WaveInfo[mode].front() UNCONDITIONALLY before the
    //   match loop, so a no-match (e.g. waveCount==0 right after Reset) never leaves it
    //   stale. The per-match assignment below only refines this.
    pCurrentWave = m_WaveInfo[gm].front();

    for (std::vector<WAVE_INFO*>::iterator wit = m_WaveInfo[gm].begin();
         wit != m_WaveInfo[gm].end(); ++wit) {
        WAVE_INFO* wi = *wit;
        // Regrowth: grow m_CurrentChance toward m_Chance.
        if (wi->m_ChanceRegrowth > 0.0f && wi->m_CurrentChance < wi->m_Chance) {
            float growth = (float)wi->m_Chance * wi->m_ChanceRegrowth;
            if (growth < 1.0f) growth = 1.0f;
            wi->m_CurrentChance = std::min(wi->m_Chance, (int)(wi->m_CurrentChance + growth));
        }

        // Check wave range using m_ScoreThreshold (waveNo) and m_EndScore (until).
        bool inRange = (wi->m_ScoreThreshold <= waveCount) &&
                       (waveCount <= wi->m_EndScore || wi->m_EndScore == -2);
        if (!inRange) continue;

        if (matchCount == 0)
            pCurrentWave = wi;
        if (matchCount < MAX_CANDIDATES)
            candidates[matchCount++] = wi;
        totalWeight += wi->m_CurrentChance;
    }

    // Weighted random selection among candidates.
    if (matchCount > 1 && totalWeight > 0) {
        uint32_t roll = m_Random.Rand32((uint32_t)(totalWeight * 10));
        int cumulative = 0;
        for (int i = 0; i < matchCount; ++i) {
            cumulative += candidates[i]->m_CurrentChance * 10;
            if (roll < (uint32_t)cumulative) {
                pCurrentWave = candidates[i];
                break;
            }
        }
    }

    WAVE_INFO* wave = pCurrentWave;
    LOG_DEBUG("WaveManager", "GetNextWave: matchCount=%d totalWeight=%d picked=%p (waveNo=%d, spawners=%d)",
              matchCount, totalWeight, (void*)wave,
              wave ? wave->m_WaveNumber : -999,
              wave ? wave->m_SpawnerCount : -1);
    if (!wave) return;

    // Decrement selected wave's chance (depletes until regrowth restores it).
    if (wave->m_CurrentChance > 0) wave->m_CurrentChance--;

    // Build ChooseFrom fruit queue if wave has one (m_SpecialFruits at +0x54).
    if (!wave->m_SpecialFruits.empty()) {
        int queueSize = (int)wave->m_SpecialFruits.size();
        if (queueSize > 32) queueSize = 32;
        m_RecentFruitCount[0] = queueSize;
        (void)playerIdx;  // SP only uses [0]
        for (int i = 0; i < queueSize; ++i) {
            const std::string& tn = wave->m_SpecialFruits[i];
            int ft;
            if (tn == "random")
                ft = Fruit::RandomFruit(false);
            else
                ft = Fruit::FruitType(tn.c_str(), false);
            m_FruitQueue[i] = ft;
        }
    }

    // Set wave timing (m_WaveDt at +0x10, m_WaveDtInc at +0x14, m_WaveDtSpInc at +0x18).
    (void)(wave->m_WaveDt + wave->m_WaveDtInc * wave->m_RevisitCounter);  // consumed by GetWavedt

    // v1.6.1 binary @ 0x001251cc / 0x00125210 (re-analyst):
    //   WAVE_INFO+0x20 (m_NextWaveDelay, XML "delay") -> delay slot (pre-spawn timer)
    //   WAVE_INFO+0x28 (m_NextWaveWait,  XML "wait")  -> wait slot (wave-end gate)
    if (wave->m_NextWaveDelay > 0.0f) {
        float d = wave->m_NextWaveDelay + wave->m_NextWaveDelayInc * wave->m_RevisitCounter;
        if (d < 0.05f) d = 0.05f;
        delay = d;
    } else {
        delay = 0.0f;          // DAT_00125328
    }
    {
        float w  = wave->m_NextWaveWait;
        float spinc = wave->m_NextWaveWaitSpInc;
        if (spinc != 0.0f) {
            float w2 = w + spinc * m_ComboSpeed;  // +0x58: displayed speed (P0); v1.6.1 @0x00123050
            if (w2 < 0.05f) w2 = 0.05f;
            w = w2;
        }
        wait = w;
    }

    // Reset all spawners in this wave.
    for (int i = 0; i < wave->m_SpawnerCount; ++i)
        wave->m_pSpawners[i].Reset(wave->m_RevisitCounter);

    // v1.6.1 WaveManager::GetNextWave @0x0012573c (tail): per-wave reset of each
    // probability-override's budget counter + age-out of timed overrides.
    // Without this, freeze/frenzy/scorex2 OverideProbability (perWave=1) is consumed
    // after one banana spawn and m_Counter (1) >= m_PerWave (1) gates it off forever.
    // Static arcade overrides have m_SelectedType=-1 (< 1), so only the m_Counter=0
    // branch runs for them; the erase branch handles timed (countdown) overrides only.
    {
        std::vector<PROBABILITY_OVERIDE>& ov = m_ProbabilityOverride[gm];
        for (std::vector<PROBABILITY_OVERIDE>::iterator it = ov.begin(); it != ov.end(); ) {
            if (it->m_SelectedType < 1 || wave == nullptr || (--it->m_SelectedType != 0)) {
                it->m_Counter = 0;
                ++it;
            } else {
                it = ov.erase(it);
            }
        }
    }

    // Multiplayer sync (not ported).
    // if (IsMultiplayer()) SendWaveSyncPacket();
}

// ----------------------------------------------------------------------------
// SetCurrentWave — per wave-system-impl.md §3
// ----------------------------------------------------------------------------

void WaveManager::SetCurrentWave(int waveNo, float aDelay, int playerIdx) {
#if defined(__bada__)
    // v1.6.1 binary @ 0x00125d1c: offset-based per-player slot access.
    int& count = *(int*)((uint8_t*)this + 0x238 + playerIdx * 4);
    float& delay = *(float*)((uint8_t*)this + 0x23c + playerIdx * 4);
#else
    int& count = m_WaveCount[playerIdx];
    float& delay = m_NextWaveDelay_P0;     // SP only
#endif
    ClearUnspawned();
    // v1.6.1 binary @ 0x00125d1c: *(int*)(&m_pCurrentWave + playerIdx*4 + 4) = waveNo - 1
    count = waveNo - 1;
    GetNextWave(0);  // binary always calls GetNextWave with playerIdx=0

    // v1.6.1 binary: delay slot at this+0x23c+playerIdx*4 += aDelay, clamped >= 0
    float v = delay + aDelay;
    if (v < 0.0f) v = 0.0f;
    delay = v;
}

void WaveManager::SetupWaveQue() {
    // Deferred: WaveManager::SetupWaveQue (binary @ 0x00124564) is only called
    // in survival/combo modes. m_pWaveQue stays null in Classic/Arcade/Zen
    // (the modes shipped); GetNextWave's early-out handles this. Re-open if
    // Combo mode is ported.
}

// ----------------------------------------------------------------------------
// ClearUnspawned — per wave-system-impl.md §5
// ----------------------------------------------------------------------------

void WaveManager::ClearUnspawned() {
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();
}

// ----------------------------------------------------------------------------
// IsWaveProcessing — per wave-system-impl.md §4
// ----------------------------------------------------------------------------

// ASM-verified: 2026-05-22 v1.6.1 binary @ 0x00122a40..0x00122ad6 (re-analyst).
// Updated 2026-05-22: restored the entry-flag gate (was incorrectly removed
// as "invented" -- the binary genuinely has `ldrb r3,[r4,#0x244+p]; cbz r3, ...`
// at 0x00122a48). Flag is set by Reset (m_WaveActive = 1 for player 0) and
// by UpdateWave's per-spawn loop (`m_WaveActive = 1` after each
// SpawnFruit/SpawnBomb). Flag is cleared by IsWaveProcessing tail when
// "nothing left to wait for". This stateful counter lets IsWaveProcessing
// return false immediately on the frame AFTER the last entity drained,
// matching binary semantics that prevent the tight GetNextWave loop.
bool WaveManager::IsWaveProcessing(int playerIdx) {
    // Entry-flag gate (binary @ 0x00122a48). If the per-player wave-active
    // flag is 0, no wave activity is in progress for this player -- return
    // false without checking entities or clearing the flag.
    if ((&m_WaveActive)[playerIdx] == 0) return false;

    WAVE_INFO* w = m_pCurrentWave[playerIdx];

    if (playerIdx == 0) {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        bool checkedShortPath = false;
        if (w) {
            if (w->m_bWaitForProcessing == 0) {
                checkedShortPath = true;
            } else if (w->m_bWaitForEntities == 0) {
                if (Fruit::GetNumActiveForPlayer(-1, false) >= 1) return true;
                // Fix 5 (binary @ 0x00122a76): arg2=false, NOT true. Port was over-counting bombs.
                if (Bomb::GetNumActiveForPlayer(-1, false) >= 1) return true;
                checkedShortPath = true;
            }
        }
        if (!checkedShortPath) {
            if (am && am->GetNumEntities(0) != 0) return true;
            if (am && am->GetNumEntities(1) != 0) return true;
        }
        (&m_WaveActive)[0] = 0;
        return false;
    } else {
        if (Fruit::GetNumActiveForPlayer(playerIdx, true) >= 1) return true;
        if (Bomb::GetNumActiveForPlayer(playerIdx, true) >= 1) return true;
        (&m_WaveActive)[playerIdx] = 0;
        return false;
    }
}

// ----------------------------------------------------------------------------
// SpawnFruit — per docs/functions/wave.md
// ----------------------------------------------------------------------------

Mortar::Entity* WaveManager::SpawnFruit(long count, long fruitType, SPAWNER_INFO* info, int playerIdx) {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return NULL;

    // Binary @ 0x00124298 returns the last spawned Entity* (this_00).
    Mortar::Entity* lastSpawned = NULL;

    // Z-stride loop counter starts at 1 (binary iVar8 = 1, increments each iteration).
    // binary @ 0x001225a0
    for (long i = 0; i < count; ++i) {
        float minAngle = info ? info->m_HorizMin : -1.0f;
        float maxAngle = info ? info->m_HorizMax :  1.0f;

        // Stage 1: degree baseline. DAT_00122844=-150.0, DAT_00122848=+150.0.
        // binary @ 0x001225e2..0x00122610
        float baseRange = -150.0f * minAngle + 150.0f * maxAngle;
        uint32_t roll1 = (baseRange > 0.0f) ? m_Random.Rand32((uint32_t)baseRange) : 0;
        int iBase = (int)((float)roll1 + minAngle * 150.0f);

        // Stage 2: parabolic spread. binary @ 0x00122614..0x0012267e
        // spread = 20 for BOTTOM/BOTTOM_SLOW (spawner==0 or type<2), 12 for LEFT/RIGHT.
        float spread = (info && (uint8_t)info->m_SpawnType >= 2) ? 12.0f : 20.0f;
        float r      = m_Random.RandF(1.0f);
        // ASM-verified: 2026-05-26 v1.6.1 binary @ 0x00122644 (re-analyst)
        // vsub.f32 s0,s0,s15 (s0=r-0.5); vadd.mi.f32 s15,s0,s15 (s15=r) if r<0.5,
        // else vsub.pl.f32 s15,s15,s0 (s15=1-r). halfR in [0,0.5]; sign flips neg when r<0.5.
        float halfR  = (r < 0.5f) ? r : (1.0f - r);
        float sign   = (r < 0.5f) ? -1.0f : 1.0f;
        int center   = (int)(((float)iBase / -150.0f) * spread * 0.5f);
        int off      = (int)(spread * (halfR * halfR * -2.0f + 0.5f) * sign);
        // Final multiplier 0xb6=182 applied after spread. binary @ 0x0012267c
        uint16_t angle = (uint16_t)(((short)(center + off)) * 0xb6);

        float speed = m_Random.RandF(1.5f) + 9.5f;   // 9.5..11.0
        float sin_a = SinIdx(angle);
        float cos_a = CosIdx(angle);

        float velMultX = info ? info->m_VelXScale : 1.0f;
        float velMultY = info ? info->m_VelYScale : 1.0f;
        // ASM-verified: 2026-05-27 v1.6.1 binary @ 0x00122744 (fruit) (re-analyst).
        // The 1.075f boost is on the VERTICAL (cos*velMultY -> vel.y) component, NOT
        // horizontal. Prior port had it on velX which slowed vertical climb by 7% and
        // over-scattered horizontal arrival.
        float velX = sin_a * speed * velMultX;
        float velY = cos_a * speed * 1.075f * velMultY;

        float posX = 0.0f;
        float posY = 0.0f;

        SpawnPlacement spawnType = info ? info->m_SpawnType : PLACEMENT_BOTTOM;

        switch (spawnType) {
        case PLACEMENT_BOTTOM:
        default:
            // spawnX = iBase, spawnY = -160 * Vec3::One.y = -160. Binary
            // literal at 0x00122776 = mvn r9,#0x9f = -160; multiplied by the
            // GOT-resident _Vector3<float>::One (confirmed (1,1,1) so the
            // multiplication is a no-op). Binary @ 0x001228ca/0xce.
            posX = (float)iBase;
            posY = -160.0f;
            break;
        case PLACEMENT_BOTTOM_SLOW:
            posX = (float)iBase;
            posY = -160.0f;
            velY *= 0.5f;
            break;
        case PLACEMENT_RANDOM_SIDE: {
            bool goLeft = (m_Random.Rand32(2) == 0);
            spawnType = goLeft ? PLACEMENT_LEFT : PLACEMENT_RIGHT;
        }   /* fall through */
        case PLACEMENT_LEFT:
        case PLACEMENT_RIGHT: {
            // ASM-verified: 2026-05-27 v1.6.1 binary @ 0x001227fe..0x0012828e + 0x001228d2 (asm-inspector).
            // Binary swaps velocity sources between axes (vel.x basis = velY_orig*-0.75,
            // vel.y basis = velX_orig + gravity term), then applies the LEFT/RIGHT sign
            // vector via a unified Vec3 multiply at the join: vel.x and pos.x both get
            // scaled by signX; vel.y and pos.y by 1.0. Net:
            //   pos.x = (int)(240.0f * sign)               // DAT_00122870 = int 240
            //   pos.y = (int)(baseDeg * 320 / 480)         // DAT_00122850/54
            //   vel.x = velY_orig * -0.75f * sign          // sign applies HERE TOO
            //   vel.y = velX_orig + speed * spawner.m_Gravity_y * -0.65f
            //   sign  = -1 for LEFT, +1 for RIGHT
            float gravY = info ? info->m_Gravity_y : 0.0f;
            float signX = (spawnType == PLACEMENT_LEFT) ? -1.0f : 1.0f;
            posX = (float)((long)(240.0f * signX));
            posY = (float)((long)(((float)iBase * 320.0f) / 480.0f));
            float newVelX = velY * (-0.75f) * signX;
            float newVelY = velX + speed * gravY * (-0.65f);
            velX = newVelX;
            velY = newVelY;
            break;
        }
        }

        // chuckDelay: binary always uses m_SpawnTimer (+0x5c) at fire moment.
        // At fire, m_SpawnTimer is ~0 or slightly negative, so chuckDelay = 0.21 always in normal play.
        float zOffset = info ? info->m_SpawnTimer : 0.0f;
        float chuckDelay = (zOffset > 0.0f) ? zOffset + 0.21f : 0.21f;

        Mortar::Entity* e = am->Add(0, true);
        if (!e) {
            LOG_WARN("WAVE/SpawnFruit", "ActorManager::Add returned null");
            continue;
        }
        Fruit* f = static_cast<Fruit*>(e);
        // Z stride: (i+1)*32. binary iVar8 starts at 1. binary @ 0x001229..
        f->pos  = Vec3(posX, posY, (float)((i + 1) * 32));
        f->vel  = Vec3(velX, velY, 0.0f);
        f->Init(nullptr, (long)fruitType, nullptr);
        // DIFFERS: v1.6.1 binary @ 0x00122a00 only writes m_PlayerIdx in the online-MP branch
        // via SetForPlayer(newFruit, 0). For SSM (same-screen split-touch), per-fruit
        // attribution is required by AddShadow / KillFruit -> MissControl::MakeDisappear,
        // so we wire it from the spawn parameter. The binary's omission appears to be a
        // Halfbrick bug; the port honors SSM design intent inferred from the downstream
        // consumers.
        // ASM-verified: 2026-05-20 v1.6.1 binary @ 0x00122a00 (re-analyst).
        f->m_PlayerIdx = playerIdx;
        // Diagnostic: spawn parameters (low-rate, only fires per spawn-event)
        LOG_VERBOSE("Spawn", "fruit type=%ld pos=(%.1f,%.1f) vel=(%.2f,%.2f) cd=%.2f place=%d",
                    fruitType, posX, posY, velX, velY, chuckDelay, (int)spawnType);

        // Post-Init gravity from spawner Vec3. binary @ 0x00122954..0x0012299e
        // f->m_Gravity = spawner.gravityVec3 * (-f->m_Gravity.y)
        if (info) {
            float negGravY = -f->m_Gravity.y;
            f->m_Gravity = Vec3(info->m_Gravity_x * negGravY,
                                info->m_Gravity_y * negGravY,
                                info->m_Gravity_z * negGravY);
            // ±0.01 nudge for side-spawned fruit. binary @ 0x00122a2c/0x00122a30
            if (spawnType == PLACEMENT_LEFT)  f->m_Gravity.x += 0.01f;
            else if (spawnType == PLACEMENT_RIGHT) f->m_Gravity.x -= 0.01f;
        }
        if (info) f->m_TimeScale = info->m_TimeScale;  // spawner+0x14

        f->Chuck(chuckDelay);
        lastSpawned = e;
    }
    return lastSpawned;
}

// ----------------------------------------------------------------------------
// SpawnBomb — per wave-system-impl.md §2
// ----------------------------------------------------------------------------

void WaveManager::SpawnBomb(long count, SPAWNER_INFO* spawner, int playerIdx) {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    for (long i = 1; i <= count; ++i) {
        float minAngle, maxAngle;
        if (spawner == nullptr) { minAngle = -1.0f; maxAngle = 1.0f; }
        else                    { minAngle = spawner->m_HorizMin; maxAngle = spawner->m_HorizMax; }

        float range = minAngle * (-150.0f) + maxAngle * 150.0f;  // DAT_00122208/0c
        uint32_t r1 = (range > 0.0f) ? m_Random.Rand32((uint32_t)range) : 0;
        int baseDeg = (int)((float)r1 + minAngle * 150.0f);

        float spread = (spawner == nullptr || spawner->m_SpawnType < 2) ? 20.0f : 12.0f;
        long center  = (long)(((float)baseDeg / -300.0f) * spread * 0.5f);  // DAT_00122210
        long lo      = (long)((float)center + spread * -0.5f);
        long hi      = (long)((float)center + spread *  0.5f);
        long rng2    = (hi > lo) ? (long)m_Random.Rand32((uint32_t)(hi - lo)) : 0;
        uint16_t angle = (uint16_t)((short)(lo + rng2) * 0xb6);  // 0xb6 = 182

        float speed = m_Random.RandF(1.5f) + 9.5f;
        float sin_a = SinIdx(angle);
        float cos_a = CosIdx(angle);
        float velMultX = (spawner == nullptr) ? 1.0f : spawner->m_VelXScale;
        float velMultY = (spawner == nullptr) ? 1.0f : spawner->m_VelYScale;
        float zOffset  = (spawner == nullptr) ? 0.0f : spawner->m_SpawnTimer;

        // ASM-verified: 2026-05-27 v1.6.1 binary @ 0x001220e2 (bomb) (re-analyst).
        // The 1.075f boost is on the VERTICAL (cos*velMultY -> vel.y) component, NOT
        // horizontal. Prior port had it on velX which slowed vertical climb by 7% and
        // over-scattered horizontal arrival.
        float velX = sin_a * speed * velMultX;
        float velY = cos_a * speed * 1.075f * velMultY;

        // Spawn position (bottom default).
        float spawnX = (float)baseDeg;
        // ASM-verified: 2026-05-27 binary @ DAT_00122240 (re-analyst).
        // pos.y basis for default-bottom bomb is -160 (off-screen below the visible
        // y=0..-160 strip), NOT the per-spread angle floor `lo`. Binary stores
        // iVar16 = -160 in the fall-through arm of the spawn-type switch, clobbering
        // the lo value used only for the angle-band randomisation above. Symptom of
        // the prior wrong value: bomb spawns near y=-10 instead of -160, so after the
        // post-Init pos.y += -100*scale.y nudge it appears at y~-110 (visible) instead
        // of y~-260 (off-screen, rising into view).
        float spawnY = -160.0f;
        float spawnZ = (float)i * 32.0f;  // DAT_00122580

        if (spawner != nullptr) {
            SpawnPlacement st = spawner->m_SpawnType;
            if (st == PLACEMENT_RANDOM_SIDE)
                st = (m_Random.Rand32(2) == 0) ? PLACEMENT_LEFT : PLACEMENT_RIGHT;

            switch (st) {
            case PLACEMENT_BOTTOM:
            default:
                break;
            case PLACEMENT_BOTTOM_SLOW:
                velY *= 0.5f;
                break;
            case PLACEMENT_RIGHT:
            case PLACEMENT_LEFT: {
                // ASM-verified: 2026-05-22 v1.6.1 binary @ 0x00121fa8 (side-spawn block
                // mirrors SpawnFruit @ 0x001225a0) (asm-inspector). Formula:
                //   pos.x = (int)(240.0f * sign)                  // DAT_00122228 = int 240
                //   pos.y = (int)(baseDeg * 320 / 480)            // DAT_0012221c/20
                //   vel.x = (velX_pre * -0.75f) * sign            // DAT 0xBF400000
                //   vel.y = velY_pre + speed * spawner.m_Gravity_y * -0.65f  // DAT_00122224
                //   sign  = -1 for LEFT, +1 for RIGHT
                // Previous "2026-05-16" port spec was wrong on two counts: X-basis is
                // the constant 240 (not baseDeg), and the velocity sources are NOT
                // swapped (vel.x <- velX_pre * -0.75, NOT velY_pre * -0.75).
                float signX = (st == PLACEMENT_LEFT) ? -1.0f : 1.0f;
                spawnX = (float)((long)(240.0f * signX));
                spawnY = (float)((long)(((float)baseDeg * 320.0f) / 480.0f));
                velX   = velX * (-0.75f) * signX;
                velY   = velY + speed * spawner->m_Gravity_y * (-0.65f);
                break;
            }
            case PLACEMENT_RANDOM_SIDE: break;  // handled above
            }
        }

        float chuckDelay = (zOffset >= 0.0f) ? zOffset + 0.21f : 0.21f;  // DAT_0012258c

        // Single-player path only (MP not ported).
        Mortar::Entity* e = am->Add(1, true);
        if (!e) continue;
        Bomb* b = static_cast<Bomb*>(e);
        b->pos = Vec3(spawnX, spawnY, spawnZ);
        b->vel = Vec3(velX, velY, 0.0f);                 // DAT_00122584 = 0
        b->Init(nullptr, 0, nullptr);
        b->pos.y += -100.0f * b->scale.y;               // DAT_00122588 = -100
        b->Chuck(chuckDelay);

        // Binary @ 0x001247c4 tail: if default-spawner bomb and bomb-multiplier
        // powerup active (playerIdx > 0), scale the bomb up.
        if (spawner == nullptr && playerIdx > 0)
            b->MakeFat(false);

        Game* game = Game::GetInstance();
        if (game && game_work.gameMode == Mortar::GAME_MODE_ARCADE)
            b->SetForPlayer(1);  // arcade single-player
    }
}

// ----------------------------------------------------------------------------
// Draw
// ----------------------------------------------------------------------------

void WaveManager::Draw(int playerIdx) {
    // Binary @ 0x00122ae8. Delegates to PowerUpManager::Draw for player 0 only.
    if (playerIdx == 0) {
        PowerUpManager::GetInstance()->Draw();
    }
}

// ASM-verified: 2026-05-03 v1.6.1 binary @ 0x001217d4 (re-analyst)
void WaveManager::DeleteSpeedControl(HUDControl* c) {
    // Binary @ 0x001217d4: ldr from [r0, #0x4] — slot [1] (+0x04), the SpeedControl widget.
    // (Old port comment "slot 0" was the collapse bug; binary checks +0x04, not +0x00.)
    if (m_SpeedControl[1] == c) m_SpeedControl[1] = nullptr;
}

// ----------------------------------------------------------------------------
// Queries
// ----------------------------------------------------------------------------

float WaveManager::GetSpeed(int /*playerIdx*/) {
    // v1.6.1 @0x00122fa0: reads m_ComboSpeed (+0x58) for P0.
    // TODO: v1.6.1 @0x00122fa0 -- P1 path reads +0x5c (m_TargetComboSpeed); single-player port returns P0 only.
    return m_ComboSpeed;  // +0x58
}

float WaveManager::GetWavedt(int playerIdx) {
    // v1.6.1 WaveManager::GetWavedt @0x00123050
    // SpInc term uses m_ComboSpeed (+0x58) for P0.
    WAVE_INFO* w = m_pCurrentWave[playerIdx];
    float waveDt = (w == nullptr)
        ? 1.0f
        : w->m_WaveDt
          + w->m_WaveDtInc * w->m_RevisitCounter
          + w->m_WaveDtSpInc * m_ComboSpeed;  // +0x58 (P0 displayed speed)

    float dtMod = (playerIdx == 0) ? (m_SpeedAccum * m_SpeedScale) : 1.0f;
    float result = waveDt * dtMod * m_ComboSpeedDivisor;
    if (result <= -100.0f) return -100.0f;
    if (result >= 100.0f) return 100.0f;
    return result;
}

float WaveManager::GetCriticalChance(int playerIdx) {
    // v1.6.1 @0x00123174: waveCritChance * m_CritChanceMult (+0x74)
    WAVE_INFO* w = m_pCurrentWave[playerIdx];
    float cc = w ? w->m_CriticalChance : 1.0f;
    return cc * m_CritChanceMult;  // m_CritChanceMult now at +0x74
}

bool WaveManager::CriticalMode(int playerIdx) {
    // ASM-spec v1.6.1 WaveManager::CriticalMode @0x001219e4: GetCriticalChance(p) > (float)((int64_t)LCG_state/2) (peek, no consume)
    return GetCriticalChance(playerIdx) > (float)((int64_t)m_Random.PeekState() / 2);
}

float WaveManager::GetComboBonusProgression(int /*playerIdx*/) {
    // v1.6.1 WaveManager::GetComboBonusProgression @0x00122fb0
    // Reads m_BlitzLevel (+0x60) + clamp(m_ColdTimer/-2.5+1, 0, 1), /6 clamp<=1.
    // TODO: v1.6.1 @0x00122fb0 -- binary indexes cold timer by playerIdx; single-field port uses P0 only.
    float progress = m_ColdTimer / -2.5f + 1.0f;  // m_ColdTimer at +0x64
    if (!(progress >= 0.0f)) progress = 0.0f;  // NaN-safe lower clamp
    if (progress > 1.0f) progress = 1.0f;
    float result = ((float)m_BlitzLevel + progress) / 6.0f;  // m_BlitzLevel at +0x60
    if (result > 1.0f) result = 1.0f;
    return result;
}

PROBABILITY_OVERIDE* WaveManager::GetCurrentOverideList(int playerIdx) {
    // Binary @ 0x0012180c. Returns pointer to the vector header at
    // this+0x1fc + gameMode*0xc + playerIdx*0x30 (callers cast to vector<PROBABILITY_OVERIDE>*).
    // Port uses m_ProbabilityOverride[gameMode] directly; playerIdx 0 is the primary slot.
    Game* game = Game::GetInstance();
    if (!game || m_ProbabilityOverride[game_work.gameMode].empty()) return nullptr;
    (void)playerIdx;  // port has single-player override list only
    return m_ProbabilityOverride[game_work.gameMode].data();
}

// ----------------------------------------------------------------------------
// Mutators
// ----------------------------------------------------------------------------

void WaveManager::AddToSpeedLossTime(float amount, int playerIdx) {
    // Binary @ 0x001218ac. Clamps DOWN to 1.0 -- speed-loss accumulator cannot exceed 1.0.
    // TODO: v1.6.1 @0x001218ac -- binary indexes combo timer by playerIdx; single-field port uses P0 only.
    (void)playerIdx;
    if (m_ComboTimer > 0.0f) {
        float v = m_ComboTimer + amount;
        if (v > 1.0f) v = 1.0f;  // caps at maximum 1.0f (binary: vcmpe s0,s15; it pl; vmovpl s0,s15)
        m_ComboTimer = v;
    }
}

void WaveManager::ResetSpeed(int playerIdx) {
    // Binary @ 0x00122e94. Re-verified 2026-05-22 (asm-inspector): clears
    // both the int blitz-level AND the float cold-timer (binary writes
    // separate stores: `mov r1,#0; str.w r1, [r2,#0x4]` for the int and
    // `vmov r1,s15; str.w r1, [r6,r5,lsl#0x2]` with s15=0.0f for the float).
    // TODO: v1.6.1 @0x00122e94 -- binary indexes speed/blitz/cold by playerIdx; single-field port uses P0 only.
    (void)playerIdx;
    m_TargetComboSpeed = 0.0f;  // +0x5c
    m_ComboSpeed       = 0.0f;  // +0x58
    m_ComboTimer       = 0.0f;  // +0x50

    // Lazy-init "blitz_bonus" hash and clear total.
    static uint32_t s_blitzBonusHash = 0;
    if (s_blitzBonusHash == 0)
        s_blitzBonusHash = StringHash("blitz_bonus");
    Game* game = Game::GetInstance();
    if (game && game_work.m_SaveData)
        game_work.m_SaveData->ClearTotal(s_blitzBonusHash);

    m_BlitzLevel = 0;   // +0x60
    m_ColdTimer  = 0.0f;  // +0x64

    // ASM-verified: 2026-05-03 v1.6.1 binary @ 0x00122e94 (re-analyst)
    // Binary @ 0x00122e94: ldr from [r0, #0x4] -> slot [1] (+0x04), the SpeedControl widget.
    HUDControl3d* sc = m_SpeedControl[1];
    if (sc) {
        SpeedControl* spc = static_cast<SpeedControl*>(sc);
        spc->m_Speed          = 0.0f;
        spc->m_DisplayedSpeed = 0.0f;
    }
}

// Binary g_BlitzSfxTable @ 0x001e87d8. 6 entries indexed by clamp(level, 1, 6) - 1.
static const char* const k_BlitzSfx[6] = {
    "combo-blitz-1", "combo-blitz-2", "combo-blitz-3",
    "combo-blitz-4", "combo-blitz-5", "combo-blitz-6",
};

void WaveManager::AddSpeed(float amount, int playerIdx) {
    // v1.6.1 WaveManager::AddSpeed @0x00124f48
    // Re-verified 2026-05-22 (asm-inspector): the cold-start gate / timer-
    // countdown logic operates on the FLOAT m_ColdTimer field at +0x64
    // (vldr.32 / vcmpe / vsub / vmov.f32 #3.0 / vstr.32). The int
    // m_BlitzLevel field at +0x60 only holds the AddToTotal()-returned
    // level counter (1..6+) used to drive score and SFX selection.
    // TODO: v1.6.1 @0x00124f48 -- binary indexes speed/cold/blitz by playerIdx; single-field port uses P0 only.
    (void)playerIdx;

    // Accumulate m_TargetComboSpeed (+0x5c), clamp [0, 14].
    const float oldSpeed = m_TargetComboSpeed;
    float v = oldSpeed + amount;
    if (v <= 0.0f)       v = 0.0f;
    else if (v >= 14.0f) v = 14.0f;
    m_TargetComboSpeed = v;

    if (amount <= 0.0f) return;

    LOG_INFO("BLITZ", "AddSpeed p=%d amount=%.3f speed=%.3f->%.3f coldTimer=%.3f level=%d",
             playerIdx, amount, oldSpeed, m_TargetComboSpeed,
             m_ColdTimer, m_BlitzLevel);

    static uint32_t s_blitzBonusHash = 0;
    if (!s_blitzBonusHash) s_blitzBonusHash = StringHash("blitz_bonus");

    // On amount>0 set m_ComboTimer (+0x50) to 1.
    m_ComboTimer = 1.0f;

    Game* game = Game::GetInstance();
    FruitSaveData* sd = game ? game_work.m_SaveData : nullptr;

    if (m_ColdTimer <= 0.0f) {
        // Cold-start path (binary @ 0x001236da onwards).
        if (m_TargetComboSpeed > 2.9f) {    // DAT_00123828
            if (sd) {
                m_ColdTimer = 3.0f;  // binary vmov.f32 #0x40200000
                sd->ClearTotal(s_blitzBonusHash);
                int newCount = sd->AddToTotal("blitz_bonus", s_blitzBonusHash, 1, false, false);
                m_BlitzLevel = newCount;
                AddToCurrentScore(5, playerIdx, false, false);
                // ASM-verified: 2026-05-23 v1.6.1 binary @ 0x00123760..0x00123798 (re-analyst).
                // Cold-start branch hashes the literal "blitz_1" (rodata @ 0x001ba773).
                static uint32_t s_blitz1Hash = 0;
                if (!s_blitz1Hash) s_blitz1Hash = StringHash("blitz_1");
                PowerUpManager::GetInstance()->ActivateScreenEffect(s_blitz1Hash);
                if (game_work.mGameSound) game_work.mGameSound->SFXPlay("combo-blitz-1", 1.0f, 1.0f);
                LOG_INFO("BLITZ", "  COLD-START FIRE p=%d level=%d +5 score, combo-blitz-1 SFX, blitz_1 effect",
                         playerIdx, newCount);
            } else {
                LOG_INFO("BLITZ", "  cold-start gate passed (speed>2.9) but FruitSaveData is null -- skipped");
            }
        } else {
            LOG_INFO("BLITZ", "  cold-start: speed=%.3f <= 2.9, no fire yet (need %.3f more)",
                     m_TargetComboSpeed, 2.9f - m_TargetComboSpeed);
        }
    } else {
        // Combo continuation path (binary @ 0x001235d2 onwards).
        // Binary subtracts the FLOAT amount from the cold-timer (vsub.f32).
        const float oldTimer = m_ColdTimer;
        m_ColdTimer -= amount;
        if (m_ColdTimer <= 0.0f) {
            if (sd) {
                int newCount = sd->AddToTotal("blitz_bonus", s_blitzBonusHash, 1, false, false);
                int level = (newCount < 6) ? newCount : 6;
                m_BlitzLevel = newCount;

                {
                    // ASM-verified: 2026-05-23 v1.6.1 binary @ 0x00123614..0x00123642 (re-analyst).
                    // Continuation tier uses format "blitz_%i" (rodata @ 0x001ba76a).
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "blitz_%i", level);
                    PowerUpManager::GetInstance()->ActivateScreenEffect(StringHash(buf));
                }
                {
                    int sfxLevel = newCount;
                    if (sfxLevel < 1) sfxLevel = 1;
                    if (sfxLevel > 6) sfxLevel = 6;
                    if (game_work.mGameSound) game_work.mGameSound->SFXPlay(k_BlitzSfx[sfxLevel - 1], 1.0f, 1.0f);
                }

                int clamped = (m_BlitzLevel > 5) ? 6 : m_BlitzLevel;
                AddToCurrentScore(clamped * 5, playerIdx, false, false);
                m_ColdTimer = 3.0f;  // reset timer for next level-up
                LOG_INFO("BLITZ", "  LEVEL-UP FIRE p=%d level=%d (clamped=%d) +%d score, combo-blitz-%d SFX, blitz_%d effect",
                         playerIdx, newCount, clamped, clamped * 5, level, level);
            }
        } else {
            LOG_INFO("BLITZ", "  continuation: timer %.3f->%.3f (need %.3f more drain to fire next tier)",
                     oldTimer, m_ColdTimer, m_ColdTimer);
        }
    }

    // v1.6.1 AddSpeed @0x00124f48: trailing best_blitz update (unconditional when amount>0).
    // Binary: AddToTotal("best_blitz", max(m_BlitzLevel - GetTotal("best_blitz"), 0))
    static uint32_t s_bestBlitzHash = 0;
    if (!s_bestBlitzHash) s_bestBlitzHash = StringHash("best_blitz");
    if (sd) {
        int existing = sd->GetTotal(s_bestBlitzHash);
        int delta    = m_BlitzLevel - existing;
        if (delta > 0)
            sd->AddToTotal("best_blitz", s_bestBlitzHash, delta, false, false);
    }
}

void WaveManager::RecievedSync(int /*waveIdx*/, float /*score*/) {
    // Binary @ 0x00122af8: per-frame wave-state network sync.
    // Defunct (P2P MP): NetworkManager::SyncWaveState is a no-op stub.
    Mortar::NetworkManager::GetInstance()->SyncWaveState();
}

// ----------------------------------------------------------------------------
// Power-up modifiers
// ----------------------------------------------------------------------------

void WaveManager::BombScale(float mult)          { m_BombChance    *= mult; }   // +0x6c
void WaveManager::BombMultiplyer(float mult)     { m_SpawnLevel    *= mult; }   // +0x68
void WaveManager::FruitMultiplyer(float mult)    { m_FruitChance   *= mult; }   // +0x70
void WaveManager::CriticalChanceMod(float mult)  { m_CritChanceMult *= mult; }  // +0x74

// ----------------------------------------------------------------------------
// Networking stubs
// ----------------------------------------------------------------------------

int  WaveManager::UpdateNetworking(float /*dt*/, int /*playerIdx*/) { return 0; }
// Defunct: P2P MP wave-sync packet -- empty in v1.6.1 binary @ 0x0012197c too
// (literal `return;`); only the GOT trampoline at 0x00102390 had a body,
// and that calls a NetworkManager fn pointer that's null on Bada.
void WaveManager::SendWaveSyncPacket()                               {}
bool WaveManager::ShouldDisplayNetworkWaitIndicator()               { return false; }

// Binary @ 0x00121778.
int COIN_CHANCEINATOR::GetCoins() const {
    Math::Random& rng = WaveManager::GetInstance()->GetRandom();
    for (int i = 0; i < m_Count; ++i) {
        Entry* e = &m_pEntries[i];
        if (rng.Rand32(e->chance) == 0) {
            if (e->min < e->max)
                return e->min + (int)rng.Rand32((uint32_t)(e->max - e->min));
            return e->min;
        }
    }
    return 0;
}

// Binary @ 0x00121a1c.
// First: try the global chanceinator via m_pCurrentWave[0]->m_pCoinChance.
// If that yields > 0, done. Else: advance RNG via fallback m_CoinChanceinator[idx].
// The fallback byte index comes from the current game-mode coin-table slot.
void WaveManager::RequestCoins() {
    WaveManager* self = GetInstance();
    WAVE_INFO* curWave = self->m_pCurrentWave[0];
    if (curWave) {
        COIN_CHANCEINATOR* primary = static_cast<COIN_CHANCEINATOR*>(curWave->m_pCoinChance);
        if (primary && primary->GetCoins() > 0)
            return;
    }
    // Fallback: RNG-advance only — return value discarded (binary behaviour).
    // ASM-verified: 2026-05-20 v1.6.1 binary @ 0x00121a1c — coinChance index = game_work.gameMode
    // (uint8 @ +0x04). Per-mode table at WaveManager+0x1dc, stride 8.
    int idx = game_work.gameMode;
    if (idx >= 0 && idx < 4)
        self->m_CoinChanceinator[idx].GetCoins();
}

// ASM-spec v1.6.1 GetRandomPowerSpawner @0x0012403c
// Function-local static SPAWNER_INFO[3]: centre-bottom, right-side, left-side.
// Lazy-initialised on first call (C++ static guard). Mirrors k_BlitzSpawners idiom above.
// includeCenter=true: picks from all 3 entries (base=0, range=3).
// includeCenter=false: skips entry 0 (centre), picks from entries 1/2 (base=1, range=2).
SPAWNER_INFO* GetRandomPowerSpawner(bool includeCenter) {
    static bool s_inited = false;
    static SPAWNER_INFO spinfos[3];
    if (!s_inited) {
        s_inited = true;

        // Entry 0: centre-bottom slow (PLACEMENT_BOTTOM_SLOW)
        spinfos[0].m_SpawnType  = PLACEMENT_BOTTOM_SLOW;
        spinfos[0].m_Gravity_x  = 0.0f;
        spinfos[0].m_Gravity_y  = -1.1f;
        spinfos[0].m_Gravity_z  = 0.0f;
        spinfos[0].m_VelXScale  = 0.6667f;   // 0x3f2a7efa ~ 2/3
        spinfos[0].m_VelYScale  = 0.0f;
        spinfos[0].m_HorizMin   = -0.5f;
        spinfos[0].m_HorizMax   =  0.5f;
        spinfos[0].m_TimeScale  =  0.75f;
        spinfos[0].m_SpawnTimer = -3.0f;     // immediately ready

        // Entry 1: right-side (PLACEMENT_RIGHT)
        spinfos[1].m_SpawnType  = PLACEMENT_RIGHT;
        spinfos[1].m_VelYScale  = 0.75f;
        spinfos[1].m_HorizMin   = -1.0f;
        spinfos[1].m_HorizMax   = -0.5f;
        spinfos[1].m_TimeScale  =  0.75f;
        spinfos[1].m_SpawnTimer = -3.0f;

        // Entry 2: left-side (PLACEMENT_LEFT)
        spinfos[2].m_SpawnType  = PLACEMENT_LEFT;
        spinfos[2].m_VelYScale  = 0.75f;
        spinfos[2].m_HorizMin   = -1.0f;
        spinfos[2].m_HorizMax   = -0.5f;
        spinfos[2].m_TimeScale  =  0.75f;
        spinfos[2].m_SpawnTimer = -3.0f;
    }

    WaveManager* wm = WaveManager::GetInstance();
    uint32_t range = includeCenter ? 3u : 2u;
    uint32_t idx   = wm->m_Random.Rand32(range);
    int base       = includeCenter ? 0 : 1;
    return &spinfos[base + (int)idx];
}

// ASM-spec v1.6.1 ReachedEnd @0x1253c0
// Saves "limitsReached" stat, plays "time-up" SFX, then triggers game over.
void ReachedEnd() {
    FruitSaveData* sd = game_work.m_SaveData;
    if (sd) {
        uint32_t h = StringHash("limitsReached");
        sd->AddToTotal("limitsReached", h, 1, true, true);
    }
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("time-up", 1.0f, 1.0f);
    }
    GameOver(-1, -1.0f, -1);
}
