#include "WaveManager.h"
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
#include "math/MathUtil.h"
#include "util/StringHash.h"
#include "util/PathCI.h"
#include "GameOver.h"
#include "PowerUpManager.h"
#include "hud/TimeControl.h"
#include "hud/SpeedControl.h"
#include "engine/network/NetworkManager.h"
#include <tinyxml2.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

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

// Port specific: binary reads a game-mode byte at g_Game+0x4 to determine if
// same-screen multiplayer is active. Port re-derives from gameMode field.
// TODO: implement full IsSameScreenMultiplayer when gameMode bitmask is further RE'd.
static bool IsSameScreenMultiplayer() {
    return false;
}

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
    // ASM-verified: 2026-05-18 binary @ 0x001231d8 (re-analyst)
}

SpawnPlacement WaveManager::ParsePlacement(const char* side) {
    if (!side) return PLACEMENT_BOTTOM;
    if (strcmp(side, "BOTTOM") == 0 || strcmp(side, "bottom") == 0) return PLACEMENT_BOTTOM;
    if (strcmp(side, "BOTTOM_SLOW") == 0) return PLACEMENT_BOTTOM_SLOW;
    if (strcmp(side, "LEFT") == 0) return PLACEMENT_LEFT;
    if (strcmp(side, "RIGHT") == 0) return PLACEMENT_RIGHT;
    if (strcmp(side, "LEFT_RIGHT") == 0 || strcmp(side, "RANDOM") == 0) return PLACEMENT_RANDOM_SIDE;
    return PLACEMENT_BOTTOM;
}

// ----------------------------------------------------------------------------
// Construction / singleton
// ----------------------------------------------------------------------------

WaveManager::WaveManager()
    : spawnLevel(0.0f)
    , m_CritChanceMult(1.0f)
    , field_0x35(1)
    , field_0x36(0)
    , field_0x37(0)
    , field_0x38(-1)
    , field_0x40(0.0f), field_0x44(0.0f)
    , field_0x48(0)
    , field_0x4c(0.0f), field_0x4c_p1(0.0f)
    , field_0x5c(0)
    , field_0x60(0.0f), field_0x60_p1(0.0f)
    , field_0x64(1.0f)
    , field_0x6c(1.0f)
    , field_0x74(1.0f)
    , field_0x78(1.0f)
    , field_0x23c(0), field_0x23d(0), field_0x23e(0), _pad23f(0)
    , field_0x240(0.0f)
    , field_0x2cc(0), field_0x2d0(0)
    , field_0x2d4(0.0f)
    , field_0x108(1), field_0x109(0)
    , m_pWaveQue(nullptr), m_pWaveQueItem(nullptr)
{
    m_Speed[0] = m_Speed[1] = 0.0f;
    m_pCurrentWave[0] = m_pCurrentWave[1] = nullptr;
    m_WaveCount[0] = m_WaveCount[1] = -1;
    m_ScoreThreshold[0] = m_ScoreThreshold[1] = 0;
    field_0x234[0] = field_0x234[1] = 0.0f;
    field_0x238[0] = field_0x238[1] = 0.0f;
    // m_DtIncPerMode (+0x7c): parsed from <defaults> "dtInc" attr per mode.
    // DIFFERS: placeholder 0.0 (no speed accumulation until XML parsed). binary @ 0x00125ac4
    m_DtIncPerMode[0] = m_DtIncPerMode[1] = 0.0f;
    m_DtIncPerMode[2] = m_DtIncPerMode[3] = 0.0f;
    // DIFFERS: actual per-mode globalDtStart unknown from RE; using 1.0 as placeholder.
    m_SpeedMultPerMode[0] = m_SpeedMultPerMode[1] = 1.0f;
    m_SpeedMultPerMode[2] = m_SpeedMultPerMode[3] = 1.0f;
    // DIFFERS: per-mode speed lower bounds (field_0x8c) -- need RE; using 1.0f.
    field_0x8c[0] = field_0x8c[1] = field_0x8c[2] = field_0x8c[3] = 1.0f;
    // DIFFERS: per-mode speed upper bounds (field_0x9c) -- need RE; using 100.0f.
    field_0x9c[0] = field_0x9c[1] = field_0x9c[2] = field_0x9c[3] = 100.0f;
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 32; ++j)
            m_FruitQueue[i][j] = -1;
    m_FruitQueueSize[0] = m_FruitQueueSize[1] = 0;
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
    Game* game = Game::GetInstance();
    if (!game) return;

    printf("[WaveManager] Init: data_dir=%s\n", game->data_dir.c_str());

    for (int mode = 0; mode < 4; ++mode) {
        // Free any previously-loaded wave infos for this mode.
        // Range-for replaced with iterator form for GCC 4.4 cross-build.
        for (std::vector<WAVE_INFO*>::iterator it = waveInfos[mode].begin();
             it != waveInfos[mode].end(); ++it)
            delete *it;
        waveInfos[mode].clear();

        std::string path = game->data_dir + "/" + s_WaveXML[mode];
        tinyxml2::XMLDocument doc;
        tinyxml2::XMLError xerr = doc.LoadFile(path.c_str());
        if (xerr != tinyxml2::XML_SUCCESS) {
            // POSIX case-insensitive fallback (no-op on Windows; NTFS CI).
            std::string ci = Mortar::ResolvePathCI(path.c_str());
            if (!ci.empty()) xerr = doc.LoadFile(ci.c_str());
        }
        if (xerr != tinyxml2::XML_SUCCESS) {
            printf("[WaveManager] Init: failed to load %s\n", path.c_str());
            continue;
        }

        tinyxml2::XMLElement* root = doc.RootElement();
        if (!root) continue;

        // Parse <defaults> element (optional).
        tinyxml2::XMLElement* defEl = root->FirstChildElement("defaults");
        if (defEl) {
            DEFAULT_WAVE_INFO& def = defaultWaveInfo[mode];
            defEl->QueryFloatAttribute("criticalChance", &def.m_CritChance);
            defEl->QueryIntAttribute("waveChance",       &def.m_WaveChance);
            // DIFFERS: binary reads "waveChanceRegrowth"; shipping XML uses "waveChanceGrowth".
            // Neither key matches the other; binary default 0.33 covers both. Parse both for safety.
            defEl->QueryFloatAttribute("waveChanceRegrowth", &def.m_WaveChanceRegrowth);
            defEl->QueryFloatAttribute("waveChanceGrowth",   &def.m_WaveChanceRegrowth);
            // globalDtInc -> per-mode WaveManager speed accumulator (binary key is "globalDtInc", not "dtInc").
            defEl->QueryFloatAttribute("globalDtInc",   &m_DtIncPerMode[mode]);
            // globalDtStart/globalDtMax -> per-mode speed clamp bounds.
            defEl->QueryFloatAttribute("globalDtStart", &field_0x8c[mode]);
            defEl->QueryFloatAttribute("globalDtMax",   &field_0x9c[mode]);
            // Additional <defaults> attrs written to DEFAULT_WAVE_INFO per binary audit.
            defEl->QueryFloatAttribute("dtInc",          &def.m_DtInc);
            defEl->QueryFloatAttribute("dtSpInc",        &def.m_DtSpInc);
            defEl->QueryFloatAttribute("nextDelay",      &def.m_NextDelay);
            defEl->QueryFloatAttribute("nextDelayInc",   &def.m_NextDelayInc);
            defEl->QueryFloatAttribute("nextDelaySpInc", &def.m_NextDelaySpInc);
            defEl->QueryFloatAttribute("beforeDelay",    &def.m_BeforeDelay);
            defEl->QueryFloatAttribute("beforeDelayInc", &def.m_BeforeDelayInc);
            defEl->QueryFloatAttribute("speedLoss",      &def.m_DefSpeedLoss);
            defEl->QueryIntAttribute("overideProbabiltyPool", &def.m_OverideProbabilityPool);
            // "waitForEntities" / "waitForProcessing" per binary write to WaveManager+0x108/0x109.
            if (const char* wfe = defEl->Attribute("waitForEntities"))
                field_0x108 = (strcmp(wfe, "false") != 0) ? 1 : 0;
            if (const char* wfp = defEl->Attribute("waitForProcessing"))
                field_0x109 = (strcmp(wfp, "false") != 0) ? 1 : 0;
        }

        // Parse <OverideProbability> elements (Arcade mode).
        // Pre-count + reserve() so vector::push_back never reallocates --
        // libstdc++ 4.4 in -std=gnu++0x mode trips a known bug instantiating
        // vector<PROBABILITY_OVERIDE>::_M_insert_aux (move_iterator + non-
        // nothrow-movable element). Reserving up-front skips the realloc
        // path entirely, dodging the bug while keeping host-build behaviour
        // identical (no functional change; just a hint to the allocator).
        int probCount = 0;
        for (tinyxml2::XMLElement* el = root->FirstChildElement("OverideProbability");
             el; el = el->NextSiblingElement("OverideProbability"))
        {
            ++probCount;
        }
        if (probCount > 0) probOverrides[mode].reserve(probCount);
        for (tinyxml2::XMLElement* el = root->FirstChildElement("OverideProbability");
             el; el = el->NextSiblingElement("OverideProbability"))
        {
            PROBABILITY_OVERIDE po;
            const char* types = el->Attribute("types");
            if (types) po.m_field68 = SplitWords(types, po.m_Types);
            el->QueryIntAttribute("percentageChance", &po.m_PercentChance);
            el->QueryIntAttribute("perWave", &po.m_PerWave);
            el->QueryIntAttribute("waveCount", &po.m_PerWaveCount);
            el->QueryFloatAttribute("disableWhenPowered", &po.m_DisableWhenPowered);
            probOverrides[mode].push_back(po);
        }

        // Parse <WaveInfo> elements.
        int waveIndex = 0;
        for (tinyxml2::XMLElement* wiEl = root->FirstChildElement("WaveInfo");
             wiEl; wiEl = wiEl->NextSiblingElement("WaveInfo"))
        {
            WAVE_INFO* wi = new WAVE_INFO();
            wi->m_WaveIndex = waveIndex++;

            // waveNo attr -> binary stores to local then +0x0 (m_ScoreThreshold) via conditional.
            // m_OverideProbabilityPool also written to +0x70 (second read wins in binary).
            const char* waveNoStr = wiEl->Attribute("waveNo");
            if (waveNoStr) {
                if (strcmp(waveNoStr, "forever") == 0)
                    wi->m_WaveNumber = -2;
                else
                    wi->m_WaveNumber = atoi(waveNoStr);
            }
            wi->m_ScoreThreshold = wi->m_WaveNumber;

            // overideProbabiltyPool at +0x70 — typo matches binary literal. Writes same slot as waveNo in binary.
            wiEl->QueryIntAttribute("overideProbabiltyPool", &wi->m_OverideProbabilityPool);

            // until attr -> m_EndScore (+0x04).
            const char* untilStr = wiEl->Attribute("until");
            if (untilStr) {
                if (strcmp(untilStr, "forever") == 0)
                    wi->m_EndScore = -2;
                else
                    wi->m_EndScore = atoi(untilStr);
            } else {
                wi->m_EndScore = -2;
            }

            // "chance" -> m_Chance (+0x3c). "chanceRegrowth" -> m_ChanceRegrowth (+0x44).
            wiEl->QueryIntAttribute("chance",           &wi->m_Chance);
            wiEl->QueryFloatAttribute("chanceRegrowth", &wi->m_ChanceRegrowth);
            wi->m_CurrentChance   = wi->m_Chance;
            wi->m_CurrentRegrowth = wi->m_ChanceRegrowth;

            // criticalChance -> m_CriticalChance (+0x64).
            wiEl->QueryFloatAttribute("criticalChance", &wi->m_CriticalChance);

            // "games" / "gamesMin" -> m_GamesMin (+0x4c); "gamesMax" -> m_GamesMax (+0x50).
            // Binary reads "games" first (overwrites +0x4c), then "gamesMin" overwrites same slot.
            wiEl->QueryIntAttribute("games",    &wi->m_GamesMin);
            wiEl->QueryIntAttribute("gamesMin", &wi->m_GamesMin);
            wiEl->QueryIntAttribute("gamesMax", &wi->m_GamesMax);
            // Binary post-process: if GamesMin < 0: GamesMin = GamesMax; if GamesMax < 0: GamesMax = GamesMin.
            if (wi->m_GamesMin < 0) wi->m_GamesMin = wi->m_GamesMax;
            if (wi->m_GamesMax < 0) wi->m_GamesMax = wi->m_GamesMin;

            // <ChooseFrom> child -> m_SpecialFruits (+0x54); m_field60 always cleared to 0.
            tinyxml2::XMLElement* cfEl = wiEl->FirstChildElement("ChooseFrom");
            if (cfEl) {
                wi->m_SpecialFruits.clear();
                wi->m_field60 = 0;
                const char* types = cfEl->Attribute("types");
                if (types) SplitWords(types, wi->m_SpecialFruits);
            }

            // <Wave_dt> child.
            tinyxml2::XMLElement* dtEl = wiEl->FirstChildElement("Wave_dt");
            if (dtEl) {
                dtEl->QueryFloatAttribute("dt",    &wi->m_WaveDt);
                dtEl->QueryFloatAttribute("inc",   &wi->m_WaveDtInc);
                dtEl->QueryFloatAttribute("spinc", &wi->m_WaveDtSpInc);
            }

            // <NextWaveDelay> child.
            tinyxml2::XMLElement* ndEl = wiEl->FirstChildElement("NextWaveDelay");
            if (ndEl) {
                ndEl->QueryFloatAttribute("wait",      &wi->m_NextWaveWait);
                ndEl->QueryFloatAttribute("waitSpinc", &wi->m_NextWaveWaitSpInc);
                ndEl->QueryFloatAttribute("speedLoss", &wi->m_NextWaveSpeedLoss);
                // Binary: if (wait > 0) { delay = 0; inc = 0; } then read delay/inc.
                if (wi->m_NextWaveWait > 0.0f) {
                    wi->m_NextWaveDelay    = 0.0f;
                    wi->m_NextWaveDelayInc = 0.0f;
                }
                ndEl->QueryFloatAttribute("delay", &wi->m_NextWaveDelay);
                ndEl->QueryFloatAttribute("inc",   &wi->m_NextWaveDelayInc);
                // waitForEntities: 1 if attr absent OR != "false"; 0 if "false".
                if (const char* wfe = ndEl->Attribute("waitForEntities"))
                    wi->m_bWaitForEntities = (strcmp(wfe, "false") != 0) ? 1 : 0;
                // waitForProcessing: stored as (CompareWords == 0 => 1; else 0).
                if (const char* wfp = ndEl->Attribute("waitForProcessing"))
                    wi->m_bWaitForProcessing = (strcmp(wfp, "false") != 0) ? 1 : 0;
            }

            // <Spawn> children — collect spawners.
            int spawnerCount = 0;
            for (tinyxml2::XMLElement* sp = wiEl->FirstChildElement("Spawn");
                 sp; sp = sp->NextSiblingElement("Spawn"))
                ++spawnerCount;

            if (spawnerCount > 0) {
                wi->m_pSpawners = new SPAWNER_INFO[spawnerCount];
                wi->m_SpawnerCount = spawnerCount;
                int si = 0;
                for (tinyxml2::XMLElement* sp = wiEl->FirstChildElement("Spawn");
                     sp; sp = sp->NextSiblingElement("Spawn"), ++si)
                {
                    SPAWNER_INFO& s = wi->m_pSpawners[si];

                    const char* types = sp->Attribute("type");
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

                    sp->QueryFloatAttribute("min", &s.m_SpawnMin);
                    sp->QueryFloatAttribute("max", &s.m_SpawnMax);
                    // mininc and maxinc both write to +0x44 (single slot); maxinc wins.
                    sp->QueryFloatAttribute("mininc", &s.m_GrowthInc);
                    sp->QueryFloatAttribute("maxinc", &s.m_GrowthInc);
                    // "delay" -> m_Delay (+0x48, chuck delay base).
                    sp->QueryFloatAttribute("delay",    &s.m_Delay);
                    sp->QueryFloatAttribute("delayinc", &s.m_DelayInc);
                    // "gravity" attr -> Vec3 at +0x18..+0x20 (binary ParseVector).
                    {
                        const char* grav = sp->Attribute("gravity");
                        if (grav) {
                            float gx = 0.0f, gy = 0.0f, gz = 0.0f;
                            sscanf(grav, "%f,%f,%f", &gx, &gy, &gz);
                            s.m_Gravity_x = gx;
                            s.m_Gravity_y = gy;
                            s.m_Gravity_z = gz;
                        }
                    }
                    // velscale -> copies to both +0x24 and +0x28; then velXscale/velYscale override.
                    sp->QueryFloatAttribute("velscale",  &s.m_VelXScale);
                    s.m_VelYScale = s.m_VelXScale;
                    sp->QueryFloatAttribute("velXscale", &s.m_VelXScale);
                    sp->QueryFloatAttribute("velYscale", &s.m_VelYScale);
                    // horizmin -> +0x2c; horizmax -> +0x30.
                    sp->QueryFloatAttribute("horizmin", &s.m_HorizMin);
                    sp->QueryFloatAttribute("horizmax", &s.m_HorizMax);
                    // mirror at +0x60: cleared to 0 when attr absent (movs r3,#0 path in binary).
                    s.m_bMirror = 0;
                    if (const char* mir = sp->Attribute("mirror"))
                        s.m_bMirror = (strcmp(mir, "false") != 0) ? 1 : 0;

                    const char* placement = sp->Attribute("placement");
                    if (placement) s.m_SpawnType = ParsePlacement(placement);

                    // Total weight contribution.
                    wi->m_TotalWeight += (int)((s.m_SpawnMin + s.m_SpawnMax) * 0.5f);
                }
            }

            waveInfos[mode].push_back(wi);
        }

        printf("[WaveManager] Init: mode %d -> %d waves from %s\n",
               mode, (int)waveInfos[mode].size(), s_WaveXML[mode]);
    }
}

// ----------------------------------------------------------------------------
// Destroy
// ----------------------------------------------------------------------------

void WaveManager::Destroy() {
    for (int mode = 0; mode < 4; ++mode) {
        for (std::vector<WAVE_INFO*>::iterator it = waveInfos[mode].begin();
             it != waveInfos[mode].end(); ++it)
            delete *it;
        waveInfos[mode].clear();
    }
    delete m_pWaveQue;     m_pWaveQue = nullptr;
    delete m_pWaveQueItem; m_pWaveQueItem = nullptr;
}

// ----------------------------------------------------------------------------
// Reset — per wave-system-impl.md §1
// ----------------------------------------------------------------------------

void WaveManager::Reset(bool fullReset) {
    // Reset combo state — binary @ 0x00125cdc (g_ComboCount = 0) and
    // adjacent last-slasher write. Binary writes 1 to last-slasher at reset;
    // port uses -1 (cold-boot sentinel) to keep consistent with TimeControl
    // game-over path and avoid a spurious same-player guard on first slice.
    g_ComboCount  = 0;
    g_LastSlasher = -1;

    Game* game = Game::GetInstance();
    if (!game) return;

    printf("[WaveManager] Reset(full=%d) gameMode=%d waveInfos[%d].size=%zu\n",
           fullReset ? 1 : 0, (int)game->gameMode, (int)game->gameMode,
           waveInfos[game->gameMode].size());

    // 1. Drop wave queue.
    delete m_pWaveQue;     m_pWaveQue = nullptr;
    delete m_pWaveQueItem; m_pWaveQueItem = nullptr;

    // 2. Per-frame state flags.
    field_0x36 = 0;
    field_0x35 = 1;
    field_0x37 = 0;
    field_0x38 = -1;
    field_0x2d0 = 0;
    field_0x2cc = 0;
    field_0x2d4 = 0.0f;
    field_0x40 = 0.0f; field_0x44 = 0.0f;
    field_0x48 = 0;
    field_0x238[0] = field_0x238[1] = 0.0f;
    field_0x234[0] = field_0x234[1] = 0.0f;
    field_0x4c = 0.0f; field_0x60 = 0.0f;
    m_Speed[0] = 0.0f; m_Speed[1] = 0.0f;

    // 3. Game-side flags / score.
    game->m_bUnsullied = 0;
    field_0x23d = 0;
    field_0x23e = 0;
    field_0x240 = m_Random.RandF(10.0f) + 10.0f;
    FN::SetScore(0, -1);       // Binary @ 0x0010a4b8; playerIdx -1 = all (defunct MP sig)
    FN::SetMissCount(0, -1);   // Binary @ 0x0010a4e8
    // ET_ClearKnownEntities(-1) -- TODO: not ported

    // 4. Per-player wave state.
    field_0x23c = 1;          // wave-was-spawned flag (player 0)
    m_WaveCount[0] = -1;      // pre-incremented by GetNextWave
    m_WaveCount[1] = -1;
    // Camera reset not ported (FruitCamera stubs).

    // HUD reset — binary @ 0x00144b78 called when game->display->field_0x3c (HUD) exists.
    if (game->hud) {
        game->hud->ResetControls();
    }

    // 5. Clear unspawned fruits + bombs, disable active ones.
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (am) {
        for (int i = 0; ; i++) {
            Mortar::Entity* e = am->GetEntity(0, i);
            if (!e) break;
            Fruit::Disable(static_cast<Fruit*>(e));
        }
        for (int i = 0; ; i++) {
            Mortar::Entity* e = am->GetEntity(1, i);
            if (!e) break;
            static_cast<Bomb*>(e)->Disable();
        }
    }

    // 6. Reset per-wave chance counters + PROBABILITY_OVERIDE state.
    ResetWaveChances();
    field_0x5c = 0;
    for (std::vector<PROBABILITY_OVERIDE>::iterator pit = probOverrides[game->gameMode].begin();
         pit != probOverrides[game->gameMode].end(); ++pit)
        pit->SelectType();

    // field_0x2c4 / field_0x2c8 not in port struct (binary resets them here).
    // Clear m_FruitQueue[2][32]: fill with -1.
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 32; ++j)
            m_FruitQueue[i][j] = -1;
    m_FruitQueueSize[0] = m_FruitQueueSize[1] = 0;

    // 7. Kick first wave if waves loaded.
    if (!waveInfos[game->gameMode].empty()) {
        printf("[WaveManager] Reset: calling GetNextWave(0)\n");
        GetNextWave(0);
        printf("[WaveManager] Reset: m_pCurrentWave[0]=%p\n",
               (void*)m_pCurrentWave[0]);
        // IsSameScreenMultiplayer() returns false (single-player) — skip MP delay bump.
    } else {
        printf("[WaveManager] Reset: NO WAVES for mode %d! GetNextWave skipped.\n",
               (int)game->gameMode);
    }

    // 8. Final per-mode speed-multiplier defaults.
    // Binary @ 0x00125eb8: dead-code MP sync flag cleared to 0.
    game->field_0x199 = 0;
    field_0x78 = 1.0f;
    field_0x74 = m_SpeedMultPerMode[game->gameMode];

    if (fullReset)
        NewGame();
}

// ----------------------------------------------------------------------------
// Resume / SaveWaveInfo
// ----------------------------------------------------------------------------

// ASM-spec corrected 2026-05-18 (re-analyst): SkipToPause is a FREE function
// at binary @ 0x00169c48 -- NOT a WaveManager member, addr 0x001255b8 was
// an internal call site inside WaveManager::UpdateWave. Binary body:
//   if (force || (g_PauseScreen && g_PauseScreen->IsEnabled())) {
//       gs->m_TransitionTimer = 0.0f;
//       PauseScreen::SkipTo(g_PauseScreen);
//       gs->pausedFlag = 1;
//       gs->levelTransitionFlag = 0;
//       MainScreen::Hide(g_MainScreen);
//       HUD::Skip(gs->hud);
//       PreloadInGameSounds();
//   }
// Port stub kept until PauseScreen::SkipTo + MainScreen::Hide are exposed.
static void SkipToPause(bool /*flag*/) {
    // TODO: implement once PauseScreen::SkipTo + MainScreen::Hide + HUD::Skip + PreloadInGameSounds are wired.
}

// ASM-spec corrected 2026-05-18: SkipToGameOver is also a free function at
// binary @ 0x0016ada0 (not 0x00125450). Binary body conditionally zeroes
// pCamera +0x10c and TimeControl +0x7c when IsTimedGame, then writes
// gs->bombHitTimer/m_TransitionTimer, conditionally fires GameOver,
// clears MainScreen flag_0xf8, then HUD::Skip(hud). Port stub kept until
// MainScreen::Hide + the camera/timecontrol field offsets are wired.
static void SkipToGameOver(int /*goState*/, float /*goTimer*/,
                           float /*nextComboBonus*/, float /*bombHitTimer*/,
                           int /*field5*/) {
    // TODO: implement once MainScreen::Hide is exposed; full spec above.
}

// ASM-verified: not yet — implementation from spec A7 @ 0x00124b1c.
// Analysed: 2026-04-30T00:00
void WaveManager::Resume() {
    // Resume is only called on restore-from-save, never on cold boot.
    // Cold boot uses WaveManager::NewGame() -> Reset(true) -> GetNextWave(0).

    Game* game = Game::GetInstance();
    if (!game) return;

    FruitSaveData* sd = game->pSaveData;
    if (!sd) return;

    // Sentinel: if no active game was saved, nothing to restore.
    if (!sd->m_bHasActiveGame) return;

    // 1. Restore score + miss count.
    // Binary @ 0x00124b7c-0x00124b8c: calls SetScore(sd->m_CurrentScore, -1) +
    // SetMissCount(sd->m_CurrentMissCount, -1).
    FN::SetScore(sd->m_CurrentScore, -1);
    FN::SetMissCount((int)sd->m_CurrentMissCount, -1);

    // 2. Restore per-player base speed from save.
    // field_0x4c <- +0x100 (m_Speed_P0), field_0x60 <- +0x108 (m_Speed_P1).
    field_0x4c    = sd->m_Speed_P0;
    field_0x60    = sd->m_Speed_P1;

    // 3. Restore was-game-over flag.
    game->m_bUnsullied = sd->m_bWasGameOver;

    // 4. Re-roll all PROBABILITY_OVERIDE entries.
    // Port specific: PROBABILITY_OVERIDE::SelectType() not yet ported; skipped.
    // for each po in probOverrides[game->gameMode]: po.SelectType();

    // 5. Reset transient queue fields.
    m_FruitQueueSize[0] = 0;
    m_FruitQueueSize[1] = 1;
    for (int p = 0; p < 2; ++p)
        for (int i = 0; i < 32; ++i)
            m_FruitQueue[p][i] = -1;

    // 6. Re-spawn saved entities from sd->m_EntityStates.
    // Binary @ 0x00124b9c-0x00124cd8: per-EntityState dispatch
    bool respawned = false;
    for (std::list<EntityState>::iterator eit = sd->m_EntityStates.begin();
         eit != sd->m_EntityStates.end(); ++eit)
    {
        EntityState& es = *eit;
        int kind = es.layer;   // 0=Fruit, 1=Bomb, 4=PowerUp (binary: ent.layer)
        Mortar::Entity* e = Mortar::ActorManager::GetInstance()->Add(kind, true);
        if (!e) continue;
        // vtable+0x8 == Init(void* p1, long fruitType, const Vec3* scale).
        e->Init(nullptr, (long)es.type, nullptr);
        e->pos = Vec3(es.pos[0], es.pos[1], es.pos[2]);
        e->vel = Vec3(es.vel[0], es.vel[1], es.vel[2]);
        if (e->entityType == 1) {
            // Bomb
            Bomb* b = static_cast<Bomb*>(e);
            // m_AccelForce maps to the gravity overlay for Bomb (binary +0x20..+0x28).
            b->m_AccelForce = Vec3(es.grav[0], es.grav[1], es.grav[2]);
            // m_BombVariant maps to playerIdx overlay (binary +0x28).
            b->m_BombVariant = (int)es.grav[2];
            if (game->gameMode == Mortar::GAME_MODE_ARCADE) Bomb::SetForPlayer(b, 1);
            if (es.wait > 0.0f) {
                if (!es.hit) {
                    b->Chuck(es.wait);
                } else {
                    Bomb::SetHit(b, es.wait);
                }
            }
        } else if (e->entityType == 0) {
            // Fruit
            Fruit* f = static_cast<Fruit*>(e);
            f->m_Gravity = Vec3(es.grav[0], es.grav[1], es.grav[2]);
            if (es.wait > 0.0f) {
                f->Chuck(es.wait);
            }
        }
        // type 4 (PowerUp): TODO if power-ups are ported.
        respawned = true;
    }

    // 7. Mortar::ActorManager::Update(dt=0) to settle respawned entities.
    Mortar::ActorManager::GetInstance()->Update(0.0f);

    // 8. Arcade mode (m_GameMode == 2): PowerUpManager::LoadTextures().
    // Binary @ 0x0011840c — iterates m_AllPowerUps and m_ScreenEffectPool.
    if (game->gameMode == Mortar::GAME_MODE_ARCADE) {
        PowerUpManager::GetInstance()->LoadTextures();
    }

    // 9. Branch selection: SkipToGameOver vs SkipToPause.
    bool gameOver = (sd->m_BombHitTimer > 0.0f && sd->m_GameMode != 2)
                    || (sd->m_GameOverScreenState >= 0);

    if (gameOver) {
        SkipToGameOver(sd->m_GameOverScreenState,
                       sd->m_GameOverTimer,
                       sd->m_field134,
                       sd->m_BombHitTimer,
                       /*field5=*/-1);
    } else if ((respawned || !sd->m_WaveStates.empty())
               && sd->m_CurrentMissCount < 3) {
        SkipToPause(true);

        // Wave-state restore after SkipToPause.
        m_FruitQueueSize[1]  = sd->m_FruitQueueCount;
        memcpy(&m_FruitQueue[0][0], &sd->m_FruitQueue[0], 0x80);
        field_0x23d          = (uint8_t)sd->m_blitzSpawnedThisGame;
        field_0x23e          = (uint8_t)sd->m_blitzForceSpawnedCounter;
        field_0x240          = sd->m_blitzSpawnTime;
        field_0x234[0]       = sd->m_WaveDelay;
        field_0x238[0]       = sd->m_WaveWait;
        field_0x74           = sd->m_ProbabilityOverideFlag;
        // sd->m_pCurrentWave_P1 (FruitSaveData+0x140) stores the SAVED WAVE INDEX
        // (uint), used to look up via the WaveState restore loop. The field name
        // inherited from earlier RE was misleading.
        field_0x38           = sd->m_pCurrentWave_P1;   // saved wave index
        field_0x4c           = sd->m_Speed_P0;
        field_0x60           = sd->m_Speed_P1;
        field_0x23c = 1; field_0x35 = 1;
        field_0x36 = 0; field_0x37 = 0;
        m_Speed[0]           = sd->m_Speed_P0_alias;
        m_Speed[1]           = sd->m_Speed_P0_alias;
        // Binary: hash of "blitz_bonus" — same key AddSpeed increments.
        field_0x5c = sd->GetTotal(StringHash("blitz_bonus"));

        ResetWaveChances();

        // Binary @ 0x00124d20-0x00124df8: WaveState/SpawnState restore.
        int mode = game->gameMode;
        for (std::list<WaveState>::iterator wit = sd->m_WaveStates.begin();
             wit != sd->m_WaveStates.end(); ++wit)
        {
            WaveState& ws = *wit;
            // ws.index = sequential index into waveInfos[mode] (SaveWaveInfo stores candidateIdx).
            // ws.waveIdx = revisit counter (field_0x34).
            if (ws.index < 0 || ws.index >= (int)waveInfos[mode].size()) continue;
            WAVE_INFO* w = waveInfos[mode][ws.index];
            w->field_0x34 = (float)ws.waveIdx;
            if (!ws.spawners.empty()) {
                m_pCurrentWave[0] = w;
                int s = 0;
                for (std::list<SpawnState>::iterator sit = ws.spawners.begin();
                     sit != ws.spawners.end(); ++sit, ++s)
                {
                    if (s >= w->m_SpawnerCount) break;
                    SPAWNER_INFO& sp = w->m_pSpawners[s];
                    sp.m_SpawnTimer     = sit->delay;
                    sp.m_RemainingCount = sit->toSpawn;
                    sp.m_field58        = 0;
                    sp.m_SpawnCountF    = (float)sit->toSpawn;
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
    // Binary @ 0x001247f0.
    if (!sd) return 0;

    sd->m_Speed_P0       = 0.0f;
    sd->m_Speed_P0_alias = 0.0f;
    sd->m_Speed_P1       = 0.0f;

    sd->m_blitzSpawnedThisGame     = field_0x23d;
    sd->m_blitzSpawnTime           = field_0x240;
    sd->m_blitzForceSpawnedCounter = field_0x23e;

    sd->m_WaveStates.clear();

    Game* game = Game::GetInstance();
    if (!game) return 0;

    // Sentinel: only save if single-player (m_bSplitPlayerWaves == 0 or waveCount < 0)
    // and waves are loaded for this mode.
    // Port specific: binary uses game-mode byte at g_Game+0x4 — port re-derives via IsSameScreenMultiplayer().
    // TODO: implement IsSameScreenMultiplayer (binary needs further RE for full gameMode bitmask).
    bool splitPlayer = IsSameScreenMultiplayer();
    if ((!splitPlayer || m_WaveCount[1] < 0)
        && !waveInfos[game->gameMode].empty())
    {
        sd->m_ProbabilityOverideFlag = field_0x74;  // m_GlobalDt

        static const int MAX_CAND = 20;
        WAVE_INFO* candidates[MAX_CAND];
        int candidateIdx[MAX_CAND];
        int numCandidates = 0;
        int waveIdx = 0;
        for (std::vector<WAVE_INFO*>::iterator wit = waveInfos[game->gameMode].begin();
             wit != waveInfos[game->gameMode].end(); ++wit) {
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
            state.waveIdx = (int)candidates[i]->field_0x34;  // revisit counter
            state.spawners.clear();
            if (candidates[i] == m_pCurrentWave[0]) {
                for (int s = 0; s < candidates[i]->m_SpawnerCount; ++s) {
                    SpawnState ss;
                    SPAWNER_INFO& sp = candidates[i]->m_pSpawners[s];
                    ss.delay   = sp.m_SpawnTimer;
                    ss.toSpawn = sp.m_RemainingCount;
                    state.spawners.push_back(ss);
                }
            }
            sd->m_WaveStates.push_back(state);
        }

        // DIVERGES fix: binary @ 0x00124986 sources +0x230 slot = m_WaveCount[0], not [1].
        sd->m_pCurrentWave_P1 = m_WaveCount[0];
        sd->m_FruitQueueCount = m_FruitQueueSize[0];
        sd->m_WaveDelay       = field_0x234[0];
        sd->m_WaveWait        = field_0x238[0];
        sd->m_Speed_P0        = field_0x4c;       // m_SpeedLossTime[0]
        sd->m_Speed_P1        = field_0x60;       // m_ComboTimer[0]
        sd->m_Speed_P0_alias  = m_Speed[1];
        memcpy(&sd->m_FruitQueue[0], &m_FruitQueue[0][0], 0x80);
        // Binary @ 0x00124986: sd->m_FruitQueueCount = this->field_0x2c8 = m_FruitQueueSize[1]
        // (renamed from field82_0x7c; RE confirmed this is the fruit queue count for resume)
        sd->m_FruitQueueCount = m_FruitQueueSize[1];
        return 1;
    }
    return 0;
}

// ----------------------------------------------------------------------------
// GameOver / NewGame
// ----------------------------------------------------------------------------

void WaveManager::GameOver() {
    // ASM-verified: 2026-05-02 binary @ 0x00121f74 -- ResetGlobalDt first, then PowerUpManager::Reset.
    WaveManager* self = GetInstance();
    if (self) self->ResetGlobalDt(1.0f);
    if (PowersEnabled()) {
        PowerUpManager::GetInstance()->Reset(false);
    }
}

void WaveManager::NewGame() {
    // ASM-verified: 2026-05-02 binary @ 0x00121f90 -- ResetGlobalDt first, then PowerUpManager::Reset.
    WaveManager* self = GetInstance();
    if (self) self->ResetGlobalDt(1.0f);
    if (PowersEnabled()) {
        PowerUpManager::GetInstance()->Reset(true);
    }
}

// ASM-verified: 2026-05-18 binary @ 0x0010a42c (re-analyst)
bool WaveManager::PowersEnabled() {
    Game* game = Game::GetInstance();
    return game && game->gameMode == Mortar::GAME_MODE_ARCADE;
}

void WaveManager::ResetGlobalDt(float dt) {
    // Binary @ 0x00121ed8. Walks probOverrides[gameMode], erasing entries
    // with m_SelectedType >= 0; advances past those with m_SelectedType < 0.
    // DIVERGES fix: binary checks *(it+0x74) = m_SelectedType, not m_PerWaveCount (+0x70).
    // Binary @ 0x00121ee8 confirms ldr from offset +0x74 of PROBABILITY_OVERIDE.
    Game* game = Game::GetInstance();
    if (game) {
        auto& vec = probOverrides[game->gameMode];
        for (auto it = vec.begin(); it != vec.end(); ) {
            if (it->m_SelectedType < 0) {
                ++it;
            } else {
                it = vec.erase(it);
            }
        }
    }
    field_0x74  = dt;      // m_GlobalDt (+0x74)
    field_0x2d4 = 0.0f;   // m_StepAccum (+0x2d4)
}

void WaveManager::ResetWaveChances() {
    // Reset m_CurrentChance (+0x40) back to m_Chance (+0x3c) for each wave in current mode.
    // Binary @ 0x001249d0: also resets m_CurrentRegrowth (+0x48) = m_ChanceRegrowth (+0x44)
    // and field_0x34 (revisit counter) = 1.0.
    Game* game = Game::GetInstance();
    if (!game) return;
    for (std::vector<WAVE_INFO*>::iterator it = waveInfos[game->gameMode].begin();
         it != waveInfos[game->gameMode].end(); ++it) {
        WAVE_INFO* wi = *it;
        wi->m_CurrentChance   = wi->m_Chance;
        wi->m_CurrentRegrowth = wi->m_ChanceRegrowth;
        wi->field_0x34        = 1.0f;
    }
}

// ----------------------------------------------------------------------------
// Update — fixed timestep pump (wave-system-impl.md §8)
// ----------------------------------------------------------------------------

void WaveManager::Update(float dt) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Reset per-frame multipliers.
    m_CritChanceMult = 1.0f;
    field_0x78 = 1.0f;
    field_0x64 = 1.0f;
    spawnLevel = 1.0f;
    field_0x6c = 1.0f;

    // Skip PowerUpManager::Update — not ported. field_0x78 stays 1.0.

    // Wave speed accumulator — binary @ 0x125ba2-0x125aa6.
    // Binary uses TWO per-mode arrays: field_0x8c[4] (lower bound) and field_0x9c[4] (upper bound).
    // TODO: per-mode bounds need RE — initialised to {1.0,1.0,1.0,1.0} / {100.0,...} as placeholders.
    {
        int mode = game->gameMode;
        // Use m_DtIncPerMode (+0x7c[mode]). DIFFERS: was m_SpeedMultPerMode (+0x8c, wrong field).
        // binary @ 0x00125ac4: speed = field_0x74 + dt * *(float*)(&this->field_0x7c + gameMode*4)
        float s = field_0x74 + dt * m_DtIncPerMode[mode];
        float lo = field_0x8c[mode];
        float hi = field_0x9c[mode];
        field_0x74 = (s < lo) ? lo : (s < hi) ? s : hi;
    }

    // Time accumulator — game->field_0x1ac not mapped in port Game struct.
    // TODO: skip stat tracking (game->field_0x1ac += dt).

    // Spawn-pump gate (binary @ 0x00125a30):
    //   if (g_GameData->levelTransitionFlag == 0 || m_pCurrentWave[0] == nullptr) {
    //       <speed accumulator + fixed-step UpdateWave loop>
    //   } else {
    //       UpdateComboSpeed(dt);  // combo tick only, no spawning
    //   }
    // Spawn pump runs unless (paused AND a wave is already active).
    //
    // MainScreen suppression flow (cold boot):
    //   - GameInit sets levelTransitionFlag = 1 (binary @ 0x0010caa8 / port GameInit step 13).
    //   - Frame 1: m_pCurrentWave[0] is null, so the OR-clause is true:
    //     spawn pump runs, UpdateWave is called, but its inner spawner loop
    //     is gated on `wave != null` (line ~921) -- the wave-end fall-through
    //     fires GetNextWave to populate m_pCurrentWave[0], no fruit spawns.
    //   - Frame 2+: levelTransitionFlag == 1 AND m_pCurrentWave[0] != null -> outer
    //     gate FALSE, only UpdateComboSpeed runs. No spawning.
    //   - When the user clicks Classic and PrepareForLevelStart -> Reset
    //     populates state and MainScreen's case-2/0x11 path clears
    //     levelTransitionFlag = 0, the OR-clause becomes true via levelTransitionFlag and the
    //     spawn pump resumes for real gameplay.
    if (game->levelTransitionFlag == 0 || m_pCurrentWave[0] == nullptr) {
        float accumDt = field_0x2d4 + dt;
        while (accumDt > WAVE_STEP) {
            UpdateWave(WAVE_STEP, 0, 0);
            accumDt -= WAVE_STEP;
        }
        field_0x2d4 = accumDt;
    } else {
        UpdateComboSpeed(dt);
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
// field_0x234 > 0); read by the wave-end block to suppress GetNextWave
// while the pre-spawn delay is still counting down.
// ASM-verified: 2026-05-10 binary @ 0x001253b0 / 0x00125928 / 0x0012593e
// (asm-inspector). Without this gate the wave-end block fires GetNextWave
// every frame the pre-spawn delay is active (no entities yet but timer is
// ticking), which resets field_0x234 -> infinite loop -> first wave never
// spawns.
static bool s_PreSpawnTickedThisFrame = false;

void WaveManager::UpdateWave(float dt, int playerIdx, int /*unk*/) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // Binary @ 0x001253b0: clear the wave-end gate flag at function entry.
    s_PreSpawnTickedThisFrame = false;

    UpdateComboSpeed(dt);

    // Skip networking check (always returns 0).
    if (UpdateNetworking(dt, playerIdx)) return;

    WAVE_INFO* wave = m_pCurrentWave[playerIdx];
    // Fix 3 (binary @ 0x001253fc): binary does NOT early-return on null wave.
    // It falls through to the wave-end block where IsWaveProcessing returns false
    // and GetNextWave is called to recover. Wrap spawn loop in if(wave) only.
    if (wave) {
    // Fix 4 (binary @ 0x00125930): after WaveTimer decrement, binary falls through
    // to the wave-end block. Port's 'return' skipped wave-end check entirely.
    // Binary reads field_0x234+p*4 (delay slot) @ 0x0012598c.
    float waveTimer = field_0x234[playerIdx];  // Fix 1: delay slot @ +0x234+p*4
    if (waveTimer > 0.0f) {
        // Binary @ 0x00125928: set the wave-end gate flag = 1 here, in the
        // timer-still-ticking branch, so the wave-end block won't fire
        // GetNextWave while the pre-spawn delay is counting down.
        s_PreSpawnTickedThisFrame = true;
        field_0x234[playerIdx] = waveTimer - dt;  // Fix 1: write back to delay slot
        // No 'return' here -- binary @ 0x00125930 falls through to wave-end block.
    } else {
        field_0x234[playerIdx] = 0.0f;
    }

    // Process each spawner (only when wave != null and timer <= 0).
    if (field_0x234[playerIdx] <= 0.0f)
    for (int s = 0; s < wave->m_SpawnerCount; ++s) {
        SPAWNER_INFO& spawner = wave->m_pSpawners[s];

        float dtMod = field_0x78;
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
                // Bomb. Binary convention: type != 0 means &spawner is the SPAWNER_INFO* template.
                // DIFFERS: using intptr_t cast to avoid MSVC C4311 truncation warning.
                SpawnBomb(1, (long)(intptr_t)(&spawner), &spawner, playerIdx);
            } else if (fruitType == -1) {
                // Binary @ 0x001254f2-0x001256f2: PROBABILITY_OVERIDE blitz selection.
                int chosenType = -1;

                // Step A: Arcade/Zen blitz state machine (gameMode == 2 only).
                // Uses TimeControl::field_0x7c (GetCountDown) and blitz timers.
                // DAT_0012558c = 30.0f (blitz refresh base), DAT_001255a0 = 0.20979f (timer compensation).
                int blitzAdvance = 0;
                if (game->gameMode == Mortar::GAME_MODE_ARCADE) {
                    float countdown = 0.0f;
                    if (game->pTimeCtrl) countdown = game->pTimeCtrl->GetCountDown();

                    if (field_0x23d == 0) {
                        // First blitz spawn not yet triggered this game.
                        if (field_0x240 < countdown) {
                            field_0x23d = 1;
                            blitzAdvance = 1;
                        }
                    } else {
                        // Blitz already triggered; check for forced re-spawn.
                        // DAT_0012558c=30.0f: blitz repeats every ~30s.
                        float blitzBase = 30.0f;  // DAT_0012558c
                        float blitzComp = 0.20979f; // DAT_001255a0 (spawner timer compensation)
                        float threshold = field_0x240 - blitzBase + blitzComp * (float)field_0x5c;
                        if (threshold < countdown) {
                            field_0x23d = 1;
                            blitzAdvance = 1;
                        }
                    }
                }

                // Step B: weighted roll over probOverrides[mode].
                // Gate predicates: m_PerWave, m_PerWaveCount, m_DisableWhenPowered, m_BombHitTimer.
                // Binary: 3 static blitz-spawner overrides selected by Rand32(3).
                // All share m_TimeScale=0.75, m_Offset=(0,-1.1,0), m_MinAngle=0.6660.
                // Differ in:
                //   [0]: m_MaxAngle=0.0,  m_MinVel=-0.5, m_MaxVel= 0.5, m_SpawnType=1
                //   [1]: m_MaxAngle=0.0,  m_MinVel=-1.0, m_MaxVel=-0.5, m_SpawnType=3
                //   [2]: m_MaxAngle=0.75, m_MinVel=-1.0, m_MaxVel=-0.5, m_SpawnType=2
                // TODO: k_BlitzSpawners field init requires designated initializer support
                // (C++20) or constructor overloads. Currently initialised as defaults.
                // Wire blitz-spawner selection when SPAWNER_INFO gains an init-from-constants ctor.
                // Binary selects one of 3 static blitz-spawner templates (non-const pointer in binary).
                static SPAWNER_INFO k_BlitzSpawners[3] = {
                    SPAWNER_INFO(), SPAWNER_INFO(), SPAWNER_INFO()
                };
                SPAWNER_INFO* blitzSpawner = (blitzAdvance && game->gameMode == Mortar::GAME_MODE_ARCADE)
                    ? &k_BlitzSpawners[m_Random.Rand32(3)]
                    : nullptr;
                int mode = game->gameMode;
                std::vector<PROBABILITY_OVERIDE>& overrides = probOverrides[mode];

                if (!overrides.empty() && blitzAdvance) {
                    int totalChance = 0;
                    for (std::vector<PROBABILITY_OVERIDE>::iterator oit = overrides.begin();
                         oit != overrides.end(); ++oit)
                    {
                        PROBABILITY_OVERIDE& po = *oit;
                        // Gate: perWave cap
                        if (po.m_PerWave > 0 && po.m_Counter >= po.m_PerWave) continue;
                        // Gate: waveCount minimum (field_0x5c = blitz score total)
                        if (po.m_PerWaveCount > 0 && field_0x5c < po.m_PerWaveCount) continue;
                        // Gate: disableWhenPowered — binary @ 0x00117b38
                        // GetActiveProgression returns 2.0 when no power active, [0..1] otherwise.
                        // Skip only when a power is active AND progression dropped to/below threshold.
                        // ASM-verified: 2026-05-18 binary @ 0x00125390 (re-analyst)
                        if (po.m_DisableWhenPowered > 0.0f) {
                            float prog = PowerUpManager::GetInstance()
                                             ? PowerUpManager::GetInstance()->GetActiveProgression(0.0f)
                                             : 2.0f;
                            if (po.m_DisableWhenPowered >= prog) continue;
                        }
                        totalChance += po.m_PercentChance;
                    }

                    if (totalChance > 0) {
                        int roll = (int)m_Random.Rand32((uint32_t)totalChance);
                        int cumulative = 0;
                        for (std::vector<PROBABILITY_OVERIDE>::iterator oit = overrides.begin();
                             oit != overrides.end(); ++oit)
                        {
                            PROBABILITY_OVERIDE& po = *oit;
                            if (po.m_PerWave > 0 && po.m_Counter >= po.m_PerWave) continue;
                            if (po.m_PerWaveCount > 0 && field_0x5c < po.m_PerWaveCount) continue;
                            if (po.m_DisableWhenPowered > 0.0f) {
                                float prog = PowerUpManager::GetInstance()
                                                 ? PowerUpManager::GetInstance()->GetActiveProgression(0.0f)
                                                 : 2.0f;
                                if (po.m_DisableWhenPowered >= prog) continue;
                            }
                            cumulative += po.m_PercentChance;
                            if (roll < cumulative) {
                                chosenType = po.GetType();
                                if (chosenType >= 0) {
                                    const FruitInfo* fi = FruitInfo_Get(chosenType);
                                    // Bug 4: AnyActivePowers early-exit — binary @ 0x001254f2.
                                    // If this fruit's power is already active, abort spawn.
                                    // ASM-verified: 2026-05-18 binary @ 0x001254f2 (re-analyst)
                                    if (fi && fi->m_pPowers && fi->m_pPowers->AnyActivePowers()) {
                                        chosenType = -1;
                                        break;
                                    }
                                    po.m_Counter++;
                                    field_0x23d++;
                                }
                                break;
                            }
                        }
                    }
                    field_0x23e++;
                }

                if (chosenType < 0) {
                    chosenType = Fruit::RandomFruit(false);
                }
                SpawnFruit(1, chosenType,
                    blitzSpawner ? blitzSpawner : &spawner, playerIdx);
            } else {
                // Specific fruit type.
                SpawnFruit(1, fruitType, &spawner, playerIdx);
            }

            field_0x23c = 1;
            spawner.m_RemainingCount--;

            // Refill spawner timer: delay base - delayinc * wave revisit counter.
            float spawnDt = spawner.m_Delay - spawner.m_DelayInc * wave->field_0x34;
            if (spawnDt < 0.0f) spawnDt = 0.0f;
            spawner.m_SpawnTimer += spawnDt;
        }
    }
    } // end if (wave) -- timer + spawner loop

    // Wave-end block: runs regardless of whether wave is null (binary @ 0x001253fc falls through).
    // Fix 1 (binary @ 0x00125956): reads wait slot field_0x238+p*4, NOT delay slot.
    // Fix 2 (binary @ 0x00125224): no writeback to port-owned m_NextWaveDelay[] -- removed.
    // Binary @ 0x0012593e gate: also skip when s_PreSpawnTickedThisFrame is set
    // (pre-spawn delay is still counting down -- no entities yet but the wave
    // is "in progress" via the timer).
    if (!s_PreSpawnTickedThisFrame && !IsWaveProcessing(playerIdx)) {
        float nextDelay = field_0x238[playerIdx];  // Fix 1: wait slot @ +0x238+p*4
        if (nextDelay > 0.0f) {
            nextDelay -= dt;
            field_0x238[playerIdx] = nextDelay;    // Fix 1: write back to wait slot
            if (nextDelay > 0.0f) return;
        }
        GetNextWave(playerIdx);
    }
}

void WaveManager::UpdateComboSpeed(float dt) {
    // Binary @ 0x00122f50. Gate: Arcade mode only (gameMode==2).
    // game[+0x0c] pause float not in port Game struct; drop that half of gate.
    Game* game = Game::GetInstance();
    if (!game || game->gameMode != Mortar::GAME_MODE_ARCADE) return;

    float curSpeed  = m_Speed[0];
    float targetP1  = m_Speed[1];
    if (targetP1 < 2.9f) targetP1 = 0.0f;   // DAT_001230dc/e0

    float delta;
    if (curSpeed == targetP1)        delta = 0.0f;
    else if (targetP1 < curSpeed)    delta = std::max(targetP1 - curSpeed, dt * -5.0f);
    else                              delta = std::min(targetP1 - curSpeed, dt *  5.0f);
    m_Speed[0] = curSpeed + delta;

    // Binary @ 0x00122f50: lazy SpeedControl alloc + push to HUD.
    float cur = m_Speed[0];
    HUDControl3d* sc = m_SpeedControl[0];
    if (!sc) {
        sc = new SpeedControl();
        m_SpeedControl[0] = sc;
        // TODO: install OnDestroy delegate calling WaveManager::DeleteSpeedControl.
        if (game->hud) game->hud->AddControl(sc, false);
    }
    SpeedControl* spc = static_cast<SpeedControl*>(sc);
    spc->m_DisplayedSpeed = cur;
    spc->m_Speed          = field_0x4c;

    // Decay the speed-loss timer (field_0x4c) via GetWavedt/m_NextWaveSpeedLoss.
    if (field_0x4c > 0.0f && m_pCurrentWave[0] != nullptr
        && m_pCurrentWave[0]->m_NextWaveSpeedLoss > 0.0f)
    {
        float wd = GetWavedt(0);
        if (wd > 1.0f) wd = 1.0f;
        field_0x4c -= (wd * dt) / m_pCurrentWave[0]->m_NextWaveSpeedLoss;
        if (field_0x4c <= 0.0f)
            ResetSpeed(0);
    }
}

// ----------------------------------------------------------------------------
// GetNextWave — per docs/functions/wave.md (227 lines)
// ----------------------------------------------------------------------------

void WaveManager::GetNextWave(int playerIdx) {
    Game* game = Game::GetInstance();
    if (!game) return;

    m_WaveCount[playerIdx]++;
    printf("[WaveManager] GetNextWave(p=%d) waveCount=%d mode=%d waveInfos=%zu\n",
           playerIdx, m_WaveCount[playerIdx], (int)game->gameMode,
           waveInfos[game->gameMode].size());

    // Speed ramp: increment revisit counter on previously-visited wave.
    if (m_WaveCount[playerIdx] > 1 && m_pCurrentWave[playerIdx])
        m_pCurrentWave[playerIdx]->field_0x34 += 1.0f;

    // Wave queue path (survival/combo — null in normal play).
    if (m_pWaveQue) {
        // TODO: queue path not ported.
        return;
    }

    // Score-based selection.
    int gm = game->gameMode;
    int totalWeight = 0;
    int matchCount = 0;
    static const int MAX_CANDIDATES = 20;
    WAVE_INFO* candidates[MAX_CANDIDATES];

    for (std::vector<WAVE_INFO*>::iterator wit = waveInfos[gm].begin();
         wit != waveInfos[gm].end(); ++wit) {
        WAVE_INFO* wi = *wit;
        // Regrowth: grow m_CurrentChance toward m_Chance.
        if (wi->m_ChanceRegrowth > 0.0f && wi->m_CurrentChance < wi->m_Chance) {
            float growth = (float)wi->m_Chance * wi->m_ChanceRegrowth;
            if (growth < 1.0f) growth = 1.0f;
            wi->m_CurrentChance = std::min(wi->m_Chance, (int)(wi->m_CurrentChance + growth));
        }

        // Check wave range using m_ScoreThreshold (waveNo) and m_EndScore (until).
        bool inRange = (wi->m_ScoreThreshold <= m_WaveCount[playerIdx]) &&
                       (m_WaveCount[playerIdx] <= wi->m_EndScore || wi->m_EndScore == -2);
        if (!inRange) continue;

        if (matchCount == 0)
            m_pCurrentWave[playerIdx] = wi;
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
                m_pCurrentWave[playerIdx] = candidates[i];
                break;
            }
        }
    }

    WAVE_INFO* wave = m_pCurrentWave[playerIdx];
    printf("[WaveManager] GetNextWave: matchCount=%d totalWeight=%d picked=%p (waveNo=%d, spawners=%d)\n",
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
        m_ScoreThreshold[playerIdx] = wave->m_ScoreThreshold;
        m_FruitQueueSize[playerIdx] = queueSize;
        for (int i = 0; i < queueSize; ++i) {
            const std::string& tn = wave->m_SpecialFruits[i];
            int ft;
            if (tn == "random")
                ft = Fruit::RandomFruit(false);
            else
                ft = Fruit::FruitType(tn.c_str(), false);
            m_FruitQueue[playerIdx][i] = ft;
        }
    }

    // Set wave timing (m_WaveDt at +0x10, m_WaveDtInc at +0x14, m_WaveDtSpInc at +0x18).
    (void)(wave->m_WaveDt + wave->m_WaveDtInc * wave->field_0x34);  // consumed by GetWavedt

    // ASM-verified: 2026-05-10 binary @ 0x001251cc / 0x00125210 (re-analyst).
    // Binary mapping (the previous "SLOT SWAP CORRECTION" was wrong, restored):
    //   WAVE_INFO+0x20 (m_NextWaveDelay, XML "delay") -> field_0x234[p]
    //     This is the PRE-SPAWN timer. UpdateWave gates the spawn loop on
    //     field_0x234 <= 0; while > 0 it ticks down and early-returns.
    //     For classic wave 0 with delay="1.0", first wave waits ~1s before
    //     fruit spawn.
    //   WAVE_INFO+0x28 (m_NextWaveWait,  XML "wait")  -> field_0x238[p]
    //     This is the wave-end gate (delays the next GetNextWave call after
    //     fruit clears).
    // Earlier port had these swapped, which made field_0x234 = 0 each frame
    // and the pre-spawn loop fire immediately on frame 1 (user-visible: first
    // wave came too fast).
    if (wave->m_NextWaveDelay > 0.0f) {
        float delay = wave->m_NextWaveDelay + wave->m_NextWaveDelayInc * wave->field_0x34;
        if (delay < 0.05f) delay = 0.05f;
        field_0x234[playerIdx] = delay;
    } else {
        field_0x234[playerIdx] = 0.0f;          // DAT_00125328
    }
    {
        float wait  = wave->m_NextWaveWait;
        float spinc = wave->m_NextWaveWaitSpInc;
        if (spinc != 0.0f) {
            float w2 = wait + spinc * m_Speed[playerIdx];
            if (w2 < 0.05f) w2 = 0.05f;
            wait = w2;
        }
        field_0x238[playerIdx] = wait;
    }

    // Reset all spawners in this wave.
    for (int i = 0; i < wave->m_SpawnerCount; ++i)
        wave->m_pSpawners[i].Reset(wave->field_0x34);

    // Decrement PROBABILITY_OVERIDE counters.
    // No separate per-tick iteration — override selection is in UpdateWave
    // (PROBABILITY_OVERIDE block) and per-game reset in ResetGlobalDt.

    // Multiplayer sync (not ported).
    // if (IsMultiplayer()) SendWaveSyncPacket();
}

// ----------------------------------------------------------------------------
// SetCurrentWave — per wave-system-impl.md §3
// ----------------------------------------------------------------------------

void WaveManager::SetCurrentWave(int waveNo, float delay, int playerIdx) {
    ClearUnspawned();
    // Write waveNo-1 so GetNextWave lands on waveNo (binary quirk per impl spec §10).
    m_WaveCount[playerIdx] = waveNo - 1;
    GetNextWave(0);

    float v = field_0x234[playerIdx] + delay;
    if (v < 0.0f) v = 0.0f;
    field_0x234[playerIdx] = v;
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

// ASM-verified: 2026-04-30T07:15 binary @ 0x00122a40..0x00122ad6 (asm-inspector)
bool WaveManager::IsWaveProcessing(int playerIdx) {
    // Binary @ 0x00122a48 loads flag = (&field_0x23c)[p] but does NOT
    // branch on it at entry. The flag is referenced only at the exit
    // (LAB_ac4 stores 0). The earlier port-side `if (!flag) return false`
    // short-circuit was an invented gate -- once any frame cleared the
    // flag, every subsequent frame returned false and GetNextWave fired
    // in a tight loop. Removed.
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
        (&field_0x23c)[0] = 0;
        return false;
    } else {
        if (Fruit::GetNumActiveForPlayer(playerIdx, true) >= 1) return true;
        if (Bomb::GetNumActiveForPlayer(playerIdx, true) >= 1) return true;
        (&field_0x23c)[playerIdx] = 0;
        return false;
    }
}

// ----------------------------------------------------------------------------
// SpawnFruit — per docs/functions/wave.md
// ----------------------------------------------------------------------------

void WaveManager::SpawnFruit(long count, long fruitType, SPAWNER_INFO* info, int playerIdx) {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

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
        // halfR = (r < 0.5) ? (r + 0.5) : (0.5 - r)   binary: vsub then ite mi/pl
        float halfR  = (r < 0.5f) ? (r + 0.5f) : (0.5f - r);
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
        float velX = sin_a * speed * velMultX;
        // DIFFERS: binary @ 0x00122744 has vmul s14,s14,s15 with
        // DAT_0012284c = 1.075f, but applying it runtime-traced bombs past
        // the +240 OOB top so they died at apex instead of arcing back. The
        // 1.075 likely applies to a different quantity in the binary's per-
        // frame integration that we haven't pinned down yet; dropping it
        // here keeps apex within bounds and produces visibly faithful arcs.
        float velY = cos_a * speed * velMultY;

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
            // Side: spawnX = baseDeg * 320/480. binary @ 0x00122802: DAT_00122850/4=320/480
            float spawnXf = (float)((long)((float)iBase * 320.0f / 480.0f));
            // spawnY = velY * -0.75 * 320/480. binary @ 0x00122802
            float spawnYf = (float)((long)(velY * -0.75f * (320.0f / 480.0f)));
            // velY uses spawner gravity.y: velX + speed * gravity_y * -0.65
            // binary @ 0x00122818: spawner+0x1c = gravity.y
            float gravY = info ? info->m_Gravity_y : 0.0f;
            float newVelX = velY;
            float newVelY = (float)((long)(velX + speed * gravY * (-0.65f)));
            if (spawnType == PLACEMENT_LEFT) {
                posX = -spawnXf;
                velX = -newVelX;
            } else {
                posX = spawnXf;
                velX = newVelX;
            }
            posY = spawnYf;
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
            fprintf(stderr, "[SpawnFruit] Mortar::ActorManager::Add returned null\n");
            continue;
        }
        Fruit* f = static_cast<Fruit*>(e);
        // Z stride: (i+1)*32. binary iVar8 starts at 1. binary @ 0x001229..
        f->pos  = Vec3(posX, posY, (float)((i + 1) * 32));
        f->vel  = Vec3(velX, velY, 0.0f);
        f->Init(nullptr, (long)fruitType, nullptr);
        // Diagnostic: spawn parameters (low-rate, only fires per spawn-event)
        printf("[Spawn] fruit type=%ld pos=(%.1f,%.1f) vel=(%.2f,%.2f) cd=%.2f place=%d\n",
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
    }
}

// ----------------------------------------------------------------------------
// SpawnBomb — per wave-system-impl.md §2
// ----------------------------------------------------------------------------

void WaveManager::SpawnBomb(long count, long type, SPAWNER_INFO* spawner, int playerIdx) {
    (void)playerIdx;
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    for (long i = 1; i <= count; ++i) {
        float minAngle, maxAngle;
        if (type == 0) { minAngle = -1.0f; maxAngle = 1.0f; }
        else           { minAngle = spawner->m_HorizMin; maxAngle = spawner->m_HorizMax; }

        float range = minAngle * (-150.0f) + maxAngle * 150.0f;  // DAT_00122208/0c
        uint32_t r1 = (range > 0.0f) ? m_Random.Rand32((uint32_t)range) : 0;
        int baseDeg = (int)((float)r1 + minAngle * 150.0f);

        float spread = (type == 0 || spawner->m_SpawnType < 2) ? 20.0f : 12.0f;
        long center  = (long)(((float)baseDeg / -300.0f) * spread * 0.5f);  // DAT_00122210
        long lo      = (long)((float)center + spread * -0.5f);
        long hi      = (long)((float)center + spread *  0.5f);
        long rng2    = (hi > lo) ? (long)m_Random.Rand32((uint32_t)(hi - lo)) : 0;
        uint16_t angle = (uint16_t)((short)(lo + rng2) * 0xb6);  // 0xb6 = 182

        float speed = m_Random.RandF(1.5f) + 9.5f;
        float sin_a = SinIdx(angle);
        float cos_a = CosIdx(angle);
        float velMultX = (type == 0) ? 1.0f : spawner->m_VelXScale;
        float velMultY = (type == 0) ? 1.0f : spawner->m_VelYScale;
        float zOffset  = (type == 0) ? 0.0f : spawner->m_SpawnTimer;

        float velX = sin_a * speed * velMultX;
        // DIFFERS: binary @ 0x001220e6 has vmul s14,s14,s15 with
        // DAT_00122218 = 1.075f. See SpawnFruit comment above -- same reason
        // (apex past +240 OOB), same workaround.
        float velY = cos_a * speed * velMultY;

        // Spawn position (bottom default).
        float spawnX = (float)baseDeg;
        float spawnY = (float)lo;
        float spawnZ = (float)i * 32.0f;  // DAT_00122580

        if (type != 0) {
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
                // ASM-verified 2026-05-16 binary @ 0x00121fa8 case 0x02/0x03
                // (0x00122140..0x0012226c). Port previously had X/Y SWAPPED and
                // the -0.75 factor applied to the wrong slot.
                //   X-pos = raw baseDeg          (sign-flipped for LEFT via local_60.x = -1)
                //   Y-pos = baseDeg * 320/480    (DAT_0012221c/20; NOT sign-flipped)
                //   velX_new = velY_pre * -0.75
                //   velY_new = velX_pre + speed * gravity.y * -0.65   (DAT_00122224)
                long  baseDegInt = (long)baseDeg;
                float newVelY = velX + speed * spawner->m_Gravity_y * (-0.65f);
                float newVelX = velY * (-0.75f);
                spawnX = (float)baseDegInt;
                spawnY = (float)((long)(((float)baseDegInt * 320.0f) / 480.0f));
                velX   = newVelX;
                velY   = newVelY;
                if (st == PLACEMENT_LEFT) {
                    spawnX = -spawnX;
                    velX   = -velX;
                }
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

        Game* game = Game::GetInstance();
        if (game && game->gameMode == Mortar::GAME_MODE_ARCADE)
            Bomb::SetForPlayer(b, 1);  // arcade single-player
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

// ASM-verified: 2026-05-03 binary @ 0x001217d4 (re-analyst)
void WaveManager::DeleteSpeedControl(HUDControl* c) {
    // Binary @ 0x001217d4: only checks slot 0 (slot 1 never populated).
    if (m_SpeedControl[0] == c) m_SpeedControl[0] = nullptr;
}

// ----------------------------------------------------------------------------
// Queries
// ----------------------------------------------------------------------------

float WaveManager::GetSpeed(int playerIdx) {
    return (&m_Speed[0])[playerIdx];   // +0x54 + playerIdx*4
}

float WaveManager::GetWavedt(int playerIdx) {
    WAVE_INFO* w = m_pCurrentWave[playerIdx];
    float waveDt = (w == nullptr)
        ? 1.0f
        : w->m_WaveDt
          + w->m_WaveDtInc * w->field_0x34
          + w->m_WaveDtSpInc * m_Speed[playerIdx];

    float dtMod = (playerIdx == 0) ? field_0x74 * field_0x78 : 1.0f;
    float result = waveDt * dtMod;
    if (result <= 0.0f) return 0.0f;
    if (result < 100.0f) return result;
    return 100.0f;
}

float WaveManager::GetCriticalChance(int playerIdx) {
    WAVE_INFO* w = m_pCurrentWave[playerIdx];
    float cc = w ? w->m_CriticalChance : 1.0f;
    return cc * m_CritChanceMult;
}

bool WaveManager::CriticalMode(int playerIdx) {
    // Binary @ 0x001219e4: returns
    //   (float)((int64_t)*(int*)g_RandomState / 2) < GetCriticalChance(p)
    // The LHS is a huge quasi-random float [INT_MIN/2..INT_MAX/2]; comparison
    // against a small [0..N] chance makes it effectively a sign-of-seed coin
    // flip. Requires Math::Random::PeekState() (currently absent in port's
    // Random class) -- peeks the first 4 bytes of the LCG state without
    // advancing. Returning false (no crits) is safe -- minor cosmetic gap
    // until Random::PeekState() is added.
    (void)playerIdx;
    return false;
}

float WaveManager::GetComboBonusProgression(int playerIdx) {
    // Binary @ 0x00121840.
    float progress = (&field_0x60)[playerIdx] / -2.5f + 1.0f;  // m_ComboTimer[p] / -2.5 + 1
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    float result = ((float)(&field_0x5c)[playerIdx] + progress) / 6.0f;  // m_BlitzBonus[p]
    if (result > 1.0f) result = 1.0f;
    return result;
}

PROBABILITY_OVERIDE* WaveManager::GetCurrentOverideList(int playerIdx) {
    // Binary @ 0x0012180c. Returns pointer to the vector header at
    // this+0x1fc + gameMode*0xc + playerIdx*0x30 (callers cast to vector<PROBABILITY_OVERIDE>*).
    // Port uses probOverrides[gameMode] directly; playerIdx 0 is the primary slot.
    Game* game = Game::GetInstance();
    if (!game || probOverrides[game->gameMode].empty()) return nullptr;
    (void)playerIdx;  // port has single-player override list only
    return probOverrides[game->gameMode].data();
}

// ----------------------------------------------------------------------------
// Mutators
// ----------------------------------------------------------------------------

void WaveManager::AddToSpeedLossTime(float amount, int playerIdx) {
    // Binary @ 0x001218ac. Clamps DOWN to 1.0 -- speed-loss accumulator cannot exceed 1.0.
    float* slot = &field_0x4c + playerIdx;  // +0x4c + p*4
    if (*slot > 0.0f) {
        float v = *slot + amount;
        if (v > 1.0f) v = 1.0f;  // Clamps DOWN to 1.0 -- caps at maximum 1.0f per second (binary: vcmpe s0,s15; it pl; vmovpl s0,s15)
        *slot = v;
    }
}

void WaveManager::ResetSpeed(int playerIdx) {
    // Binary @ 0x00122e94.
    m_Speed[1 + playerIdx] = 0.0f;      // +0x58 + p*4 (combo-speed overlap slot)
    m_Speed[playerIdx]     = 0.0f;      // +0x54 + p*4
    (&field_0x4c)[playerIdx] = 0.0f;   // m_SpeedLossTime[p]

    // Lazy-init "blitz_bonus" hash and clear total.
    static uint32_t s_blitzBonusHash = 0;
    if (s_blitzBonusHash == 0)
        s_blitzBonusHash = StringHash("blitz_bonus");
    Game* game = Game::GetInstance();
    if (game && game->pSaveData)
        game->pSaveData->ClearTotal(s_blitzBonusHash);

    (&field_0x60)[playerIdx] = 0.0f;   // m_ComboTimer[p] at +0x60 + p*4
    (&field_0x5c)[playerIdx] = 0;      // m_BlitzBonus[p] at +0x5c + p*4

    // ASM-verified: 2026-05-03 binary @ 0x00122e94 (re-analyst)
    // Binary @ 0x00122e94: if SpeedControl exists, zero its display state.
    HUDControl3d* sc = m_SpeedControl[playerIdx];
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
    // Binary @ 0x00123510.
    float v = m_Speed[1 + playerIdx] + amount;
    if (v <= 0.0f)       v = 0.0f;
    else if (v >= 14.0f) v = 14.0f;
    m_Speed[1 + playerIdx] = v;

    if (amount <= 0.0f) return;

    static uint32_t s_blitzBonusHash = 0;
    if (!s_blitzBonusHash) s_blitzBonusHash = StringHash("blitz_bonus");

    (&field_0x4c)[playerIdx] = 1.0f;   // m_SpeedLossTime[p] = 1.0 (full timer)

    Game* game = Game::GetInstance();
    FruitSaveData* sd = game ? game->pSaveData : nullptr;

    if ((&field_0x60)[playerIdx] <= 0.0f) {
        // Cold-start path.
        if (m_Speed[1 + playerIdx] > 2.9f) {    // DAT_00123828
            if (sd) {
                (&field_0x60)[playerIdx] = 2.5f; // m_ComboTimer[p]
                sd->ClearTotal(s_blitzBonusHash);
                int newCount = sd->AddToTotal("blitz_bonus", s_blitzBonusHash, 1, false, false);
                (&field_0x5c)[playerIdx] = newCount;  // m_BlitzBonus[p]
                FN::AddToCurrentScore(5, playerIdx, false, false);
                static uint32_t s_blitzCountHash = 0;
                if (!s_blitzCountHash) s_blitzCountHash = StringHash("blitz_count");
                PowerUpManager::GetInstance()->ActivateScreenEffect(s_blitzCountHash);
                if (game->pGameSound) game->pGameSound->SFXPlay("combo-blitz-1", 1.0f, 1.0f);
            }
        }
    } else {
        // Combo continuation path.
        (&field_0x60)[playerIdx] -= amount;
        if ((&field_0x60)[playerIdx] <= 0.0f) {
            if (sd) {
                int newCount = sd->AddToTotal("blitz_bonus", s_blitzBonusHash, 1, false, false);
                int level = (newCount < 6) ? newCount : 6;
                (&field_0x5c)[playerIdx] = newCount;  // m_BlitzBonus[p]

                {
                    char buf[40];
                    std::snprintf(buf, sizeof(buf), "blitz_%d_count", level);
                    PowerUpManager::GetInstance()->ActivateScreenEffect(StringHash(buf));
                }
                {
                    int sfxLevel = newCount;
                    if (sfxLevel < 1) sfxLevel = 1;
                    if (sfxLevel > 6) sfxLevel = 6;
                    if (game->pGameSound) game->pGameSound->SFXPlay(k_BlitzSfx[sfxLevel - 1], 1.0f, 1.0f);
                }

                int clamped = ((&field_0x5c)[playerIdx] > 5) ? 6 : (&field_0x5c)[playerIdx];
                FN::AddToCurrentScore(clamped * 5, playerIdx, false, false);
                (&field_0x60)[playerIdx] = 2.5f;
            }
        }
    }

    // Update "blitz_max" stat.
    static uint32_t s_blitzMaxHash = 0;
    if (!s_blitzMaxHash) s_blitzMaxHash = StringHash("blitz_max");
    if (sd) {
        int existing = sd->GetTotal(s_blitzMaxHash);
        int delta    = (&field_0x5c)[playerIdx] - existing;
        if (delta > 0)
            sd->AddToTotal("blitz_max", s_blitzMaxHash, delta, false, false);
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

void WaveManager::BombScale(float mult)          { field_0x64 *= mult; }
void WaveManager::BombMultiplyer(float mult)     { spawnLevel *= mult; }
void WaveManager::FruitMultiplyer(float mult)    { field_0x6c *= mult; }
void WaveManager::CriticalChanceMod(float mult)  { m_CritChanceMult *= mult; }

// ----------------------------------------------------------------------------
// Networking stubs
// ----------------------------------------------------------------------------

int  WaveManager::UpdateNetworking(float /*dt*/, int /*playerIdx*/) { return 0; }
// Defunct: P2P MP wave-sync packet -- empty in binary @ 0x0012197c too
// (literal `return;`); only the GOT trampoline at 0x00102390 had a body,
// and that calls a NetworkManager fn pointer that's null on Bada.
void WaveManager::SendWaveSyncPacket()                               {}
bool WaveManager::ShouldDisplayNetworkWaitIndicator()               { return false; }
// TODO: 0x00121a1c -- COIN_CHANCEINATOR::GetCoins() singleton not yet ported.
// Binary path: if (m_pCurrentWave[0]->m_CoinSpawnChance == 0 ||
//                  COIN_CHANCEINATOR::GetCoins() < 1)
//                  COIN_CHANCEINATOR::GetCoins();   // RNG advance
// Coin-drop rate gating. Implement when coin spawn subsystem ports.
void WaveManager::RequestCoins()                                     {}
