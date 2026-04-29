#include "WaveManager.h"
#include "Game.h"
#include "entities/ActorManager.h"
#include "entities/Fruit.h"
#include "entities/Bomb.h"
#include "entities/FruitInfo.h"
#include "hud/HUD.h"
#include "math/MathUtil.h"
#include "util/StringHash.h"
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

void WaveManager::SplitWords(const char* str, std::vector<std::string>& out) {
    out.clear();
    if (!str || !*str) return;
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
    , field_0x234(0.0f), field_0x238(0.0f)
    , field_0x23c(0), field_0x23d(0), field_0x23e(0), _pad23f(0)
    , field_0x240(0.0f)
    , field_0x2cc(0), field_0x2d0(0)
    , field_0x2d4(0.0f)
    , m_pWaveQue(nullptr), m_pWaveQueItem(nullptr)
{
    m_Speed[0] = m_Speed[1] = 0.0f;
    m_pCurrentWave[0] = m_pCurrentWave[1] = nullptr;
    m_WaveCount[0] = m_WaveCount[1] = -1;
    m_ScoreThreshold[0] = m_ScoreThreshold[1] = 0;
    m_NextWaveDelay[0] = m_NextWaveDelay[1] = 0.0f;
    m_WaveTimer[0] = m_WaveTimer[1] = 0.0f;
    // DIFFERS: actual per-mode speed multipliers unknown from RE; using 1.0 as placeholder.
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

    for (int mode = 0; mode < 4; ++mode) {
        // Free any previously-loaded wave infos for this mode.
        for (WAVE_INFO* wi : waveInfos[mode])
            delete wi;
        waveInfos[mode].clear();

        std::string path = game->data_dir + "/" + s_WaveXML[mode];
        tinyxml2::XMLDocument doc;
        if (doc.LoadFile(path.c_str()) != tinyxml2::XML_SUCCESS) {
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
            defEl->QueryIntAttribute("waveChance", &def.m_WaveChance);
            defEl->QueryFloatAttribute("waveChanceGrowth", &def.m_WaveChanceGrowth);
            // DIFFERS: attributes "count","critchance","wavedelay","spawntimescale",
            // "bombscale","bombgravity","bombspeed","bombspeedmax","bombmin","bombmax",
            // "critchancemod","allowbombs","allowbombsfrenzy" from older spec NOT
            // present in actual XML files — skipping.
        }

        // Parse <OverideProbability> elements (Arcade mode).
        for (tinyxml2::XMLElement* el = root->FirstChildElement("OverideProbability");
             el; el = el->NextSiblingElement("OverideProbability"))
        {
            PROBABILITY_OVERIDE po;
            const char* types = el->Attribute("types");
            if (types) SplitWords(types, po.m_Types);
            el->QueryFloatAttribute("percentageChance", &po.m_PercentChance);
            el->QueryIntAttribute("perWave", &po.m_PerWave);
            el->QueryIntAttribute("waveCount", &po.m_WaveCount);
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

            // waveNo attr — if "forever", treat as -2 (always eligible).
            const char* waveNoStr = wiEl->Attribute("waveNo");
            if (waveNoStr) {
                if (strcmp(waveNoStr, "forever") == 0)
                    wi->m_WaveNumber = -2;
                else
                    wi->m_WaveNumber = atoi(waveNoStr);
            }
            wi->m_ScoreThreshold = wi->m_WaveNumber;

            // until attr.
            const char* untilStr = wiEl->Attribute("until");
            if (untilStr) {
                if (strcmp(untilStr, "forever") == 0)
                    wi->m_EndScore = -2;
                else
                    wi->m_EndScore = atoi(untilStr);
            } else {
                wi->m_EndScore = -2;   // no until = eligible forever
            }

            // chance / chanceRegrowth.
            wiEl->QueryIntAttribute("chance", &wi->m_Chance);
            wiEl->QueryFloatAttribute("chanceRegrowth", &wi->m_ChanceRegrowth);
            wi->m_CurrentMax = wi->m_Chance;

            // criticalChance (per-wave override).
            wiEl->QueryFloatAttribute("criticalChance", &wi->m_CriticalChance);

            // gamesMin / gamesMax (for dragon-wave / rare-wave gating).
            wiEl->QueryIntAttribute("gamesMin", &wi->m_GamesMin);
            wiEl->QueryIntAttribute("gamesMax", &wi->m_GamesMax);

            // <ChooseFrom> child.
            tinyxml2::XMLElement* cfEl = wiEl->FirstChildElement("ChooseFrom");
            if (cfEl) {
                const char* types = cfEl->Attribute("types");
                if (types) SplitWords(types, wi->m_ChooseFrom);
            }

            // <Wave_dt> child.
            tinyxml2::XMLElement* dtEl = wiEl->FirstChildElement("Wave_dt");
            if (dtEl) {
                dtEl->QueryFloatAttribute("dt", &wi->m_BombScale1);
                dtEl->QueryFloatAttribute("inc", &wi->wave_dt_inc);
            }

            // <NextWaveDelay> child.
            tinyxml2::XMLElement* ndEl = wiEl->FirstChildElement("NextWaveDelay");
            if (ndEl) {
                ndEl->QueryFloatAttribute("delay", &wi->m_WaveDelay);
                // DIFFERS: "wait" attr used in arcadewavelist.xml instead of "delay".
                ndEl->QueryFloatAttribute("wait", &wi->m_WaveDelay);
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
                    sp->QueryFloatAttribute("mininc", &s.m_MinInc);
                    sp->QueryFloatAttribute("maxinc", &s.m_MaxInc);
                    // "delay" in XML maps to m_ZOffset (spawn delay / chuck offset).
                    float delay = 0.0f;
                    if (sp->QueryFloatAttribute("delay", &delay) == tinyxml2::XML_SUCCESS)
                        s.m_ZOffset = delay;
                    sp->QueryFloatAttribute("delayinc", &s.m_DelayInc);
                    sp->QueryFloatAttribute("gravity", &s.m_Gravity);
                    sp->QueryFloatAttribute("timescale", &s.m_TimeScale);
                    sp->QueryFloatAttribute("horizmin", &s.m_MinAngle);
                    sp->QueryFloatAttribute("horizmax", &s.m_MaxAngle);
                    sp->QueryFloatAttribute("velYscale", &s.m_MinVel);

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
        for (WAVE_INFO* wi : waveInfos[mode])
            delete wi;
        waveInfos[mode].clear();
    }
    delete m_pWaveQue;     m_pWaveQue = nullptr;
    delete m_pWaveQueItem; m_pWaveQueItem = nullptr;
}

// ----------------------------------------------------------------------------
// Reset — per wave-system-impl.md §1
// ----------------------------------------------------------------------------

void WaveManager::Reset(bool fullReset) {
    Game* game = Game::GetInstance();
    if (!game) return;

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
    field_0x238 = 0.0f;
    field_0x4c = 0.0f; field_0x60 = 0.0f;
    m_Speed[0] = 0.0f; m_Speed[1] = 0.0f;

    // 3. Game-side flags / score.
    // game->field_0x1c = 0;  -- TODO: not mapped in port Game struct yet
    field_0x23d = 0;
    field_0x23e = 0;
    field_0x240 = m_Random.RandF(10.0f) + 10.0f;
    // SetScore(0, -1) / SetMissCount(0, -1) -- TODO: game score API not ported yet
    // ET_ClearKnownEntities(-1)             -- TODO: not ported

    // 4. Per-player wave state.
    field_0x23c = 1;          // wave-was-spawned flag (player 0)
    m_WaveCount[0] = -1;      // pre-incremented by GetNextWave
    m_WaveCount[1] = -1;
    // Camera reset not ported (FruitCamera stubs).

    // HUD reset.
    // TODO: HUD::ResetControls (binary address unknown) — not yet ported.
    // Binary calls this when game->display->field_0x3c (HUD) exists.
    (void)game->hud;

    // 5. Clear unspawned fruits + bombs, disable active ones.
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();
    ActorManager* am = ActorManager::GetInstance();
    if (am) {
        for (int i = 0; ; i++) {
            Entity* e = am->GetEntity(0, i);
            if (!e) break;
            Fruit::Disable(static_cast<Fruit*>(e));
        }
        for (int i = 0; ; i++) {
            Entity* e = am->GetEntity(1, i);
            if (!e) break;
            static_cast<Bomb*>(e)->Disable();
        }
    }

    // 6. Reset per-wave chance counters + PROBABILITY_OVERIDE state.
    ResetWaveChances();
    field_0x5c = 0;
    for (PROBABILITY_OVERIDE& po : probOverrides[game->gameMode])
        po.SelectType();

    // field_0x2c4 / field_0x2c8 not in port struct (binary resets them here).
    // Clear m_FruitQueue[2][32]: fill with -1.
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 32; ++j)
            m_FruitQueue[i][j] = -1;
    m_FruitQueueSize[0] = m_FruitQueueSize[1] = 0;

    // 7. Kick first wave if waves loaded.
    if (!waveInfos[game->gameMode].empty()) {
        GetNextWave(0);
        // IsSameScreenMultiplayer() not ported — skip MP delay bump.
    }

    // 8. Final per-mode speed-multiplier defaults.
    // game->field_0x199 = 0;  -- TODO: MP sync flag not in port Game struct
    field_0x78 = 1.0f;
    field_0x74 = m_SpeedMultPerMode[game->gameMode];

    if (fullReset)
        NewGame();
}

// ----------------------------------------------------------------------------
// Resume / SaveWaveInfo
// ----------------------------------------------------------------------------

void WaveManager::Resume() {
    // TODO: 0x00124b1c — restore from FruitSaveData (Resume path).
}

int WaveManager::SaveWaveInfo(FruitSaveData* /*save*/) {
    // TODO: 0x001247f0 — serialise wave state.
    return 0;
}

// ----------------------------------------------------------------------------
// GameOver / NewGame
// ----------------------------------------------------------------------------

void WaveManager::GameOver() {
    // TODO: 0x00121f74 — handle game-over wave reset.
}

void WaveManager::NewGame() {
    // TODO: 0x00121f90 — calls PowerUpManager::Reset(true) + ResetGlobalDt(1.0f).
}

void WaveManager::ResetGlobalDt(float /*dt*/) {
    // TODO: 0x00121ed8 — clears per-entity speed-control list.
}

void WaveManager::ResetWaveChances() {
    // Reset m_CurrentMax back to m_Chance for each wave in current mode.
    Game* game = Game::GetInstance();
    if (!game) return;
    for (WAVE_INFO* wi : waveInfos[game->gameMode])
        wi->m_CurrentMax = wi->m_Chance;
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
        float s = field_0x74 + dt * m_SpeedMultPerMode[mode];
        float lo = field_0x8c[mode];
        float hi = field_0x9c[mode];
        field_0x74 = (s < lo) ? lo : (s < hi) ? s : hi;
    }

    // Time accumulator — game->field_0x1ac not mapped in port Game struct.
    // TODO: skip stat tracking (game->field_0x1ac += dt).

    // Fixed-timestep accumulator.
    float accumDt = field_0x2d4 + dt;
    while (accumDt > WAVE_STEP) {
        if (!waveInfos[game->gameMode].empty())
            UpdateWave(WAVE_STEP, 0, 0);
        accumDt -= WAVE_STEP;
    }
    field_0x2d4 = accumDt;

    // TODO: game-end gate (binary @ 0x125b64) -- vector::size() unused, semantics unclear.
    // Binary: if(game[+0x170] && !this[+0x35] && this[+0x37] && this[+0x38] == m_WaveCount[0]+1)
    //         call vector::size()  -- result discarded (likely sentinel/watchpoint).
}

// ----------------------------------------------------------------------------
// UpdateWave — per docs/functions/wave.md (298 lines)
// ----------------------------------------------------------------------------

void WaveManager::UpdateWave(float dt, int playerIdx, int /*unk*/) {
    Game* game = Game::GetInstance();
    if (!game) return;

    // game->field_0x470 = false; -- TODO: not in port Game struct
    UpdateComboSpeed(dt);

    // Skip networking check (always returns 0).
    if (UpdateNetworking(dt, playerIdx)) return;

    WAVE_INFO* wave = m_pCurrentWave[playerIdx];
    if (!wave) return;

    // Wave timer countdown.
    float waveTimer = m_WaveTimer[playerIdx];
    if (waveTimer > 0.0f) {
        m_WaveTimer[playerIdx] = waveTimer - dt;
        return;
    }
    m_WaveTimer[playerIdx] = 0.0f;

    // Process each spawner.
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
                // Random — fall back to Fruit::RandomFruit.
                // TODO: PROBABILITY_OVERIDE power-up logic not ported.
                int rft = Fruit::RandomFruit(false);
                SpawnFruit(1, rft, &spawner, playerIdx);
            } else {
                // Specific fruit type.
                SpawnFruit(1, fruitType, &spawner, playerIdx);
            }

            field_0x23c = 1;
            spawner.m_RemainingCount--;

            // Refill spawner timer.
            float spawnDt = spawner.m_ZOffset - spawner.m_DelayInc * wave->field_0x34;
            if (spawnDt < 0.0f) spawnDt = 0.0f;
            spawner.m_SpawnTimer += spawnDt;
        }
    }

    // When all spawners done and wave is draining, transition to next wave.
    if (!IsWaveProcessing(playerIdx)) {
        float nextDelay = m_NextWaveDelay[playerIdx];
        if (nextDelay > 0.0f) {
            nextDelay -= dt;
            m_NextWaveDelay[playerIdx] = nextDelay;
            if (nextDelay > 0.0f) return;
        }
        GetNextWave(playerIdx);
    }
}

void WaveManager::UpdateComboSpeed(float /*dt*/) {
    // TODO: 0x00122f50 — blitz-combo speed update.
}

// ----------------------------------------------------------------------------
// GetNextWave — per docs/functions/wave.md (227 lines)
// ----------------------------------------------------------------------------

void WaveManager::GetNextWave(int playerIdx) {
    Game* game = Game::GetInstance();
    if (!game) return;

    m_WaveCount[playerIdx]++;

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

    for (WAVE_INFO* wi : waveInfos[gm]) {
        // Regrowth: grow m_CurrentMax toward m_Chance.
        if (wi->m_ChanceRegrowth > 0.0f && wi->m_CurrentMax < wi->m_Chance) {
            float growth = (float)wi->m_Chance * wi->m_ChanceRegrowth;
            if (growth < 1.0f) growth = 1.0f;
            wi->m_CurrentMax = std::min(wi->m_Chance, (int)(wi->m_CurrentMax + growth));
        }

        // Check wave range.
        bool inRange = (wi->m_WaveNumber <= m_WaveCount[playerIdx]) &&
                       (m_WaveCount[playerIdx] <= wi->m_EndScore || wi->m_EndScore == -2);
        if (!inRange) continue;

        if (matchCount == 0)
            m_pCurrentWave[playerIdx] = wi;
        if (matchCount < MAX_CANDIDATES)
            candidates[matchCount++] = wi;
        totalWeight += wi->m_CurrentMax;
    }

    // Weighted random selection among candidates.
    if (matchCount > 1 && totalWeight > 0) {
        uint32_t roll = m_Random.Rand32((uint32_t)(totalWeight * 10));
        int cumulative = 0;
        for (int i = 0; i < matchCount; ++i) {
            cumulative += candidates[i]->m_CurrentMax * 10;
            if (roll < (uint32_t)cumulative) {
                m_pCurrentWave[playerIdx] = candidates[i];
                break;
            }
        }
    }

    WAVE_INFO* wave = m_pCurrentWave[playerIdx];
    if (!wave) return;

    // Decrement selected wave's chance (depletes until regrowth restores it).
    if (wave->m_CurrentMax > 0) wave->m_CurrentMax--;

    // Build ChooseFrom fruit queue if wave has one.
    if (!wave->m_ChooseFrom.empty()) {
        int queueSize = (int)wave->m_ChooseFrom.size();
        if (queueSize > 32) queueSize = 32;
        m_ScoreThreshold[playerIdx] = wave->m_WaveNumber;
        m_FruitQueueSize[playerIdx] = queueSize;
        for (int i = 0; i < queueSize; ++i) {
            const std::string& tn = wave->m_ChooseFrom[i];
            int ft;
            if (tn == "random")
                ft = Fruit::RandomFruit(false);
            else
                ft = Fruit::FruitType(tn.c_str(), false);
            m_FruitQueue[playerIdx][i] = ft;
        }
    }

    // Set wave timing.
    if (wave->m_BombScale1 > 0.0f) {
        float wdt = wave->m_BombScale1 + wave->wave_dt_inc * wave->field_0x34;
        if (wdt < 0.01f) wdt = 0.01f;   // MIN_WAVE_DT
        // m_WaveDt not a separate field in port — stored via m_BombScale1 already.
    }

    // Next wave delay.
    m_NextWaveDelay[playerIdx] = wave->m_WaveDelay;

    // Reset all spawners in this wave.
    for (int i = 0; i < wave->m_SpawnerCount; ++i)
        wave->m_pSpawners[i].Reset(wave->field_0x34);

    // Decrement PROBABILITY_OVERIDE counters.
    // TODO: full override list management not ported.

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

    float& delayField = (playerIdx == 0) ? field_0x234 : field_0x238;
    float v = delayField + delay;
    if (v < 0.0f) v = 0.0f;
    delayField = v;
}

void WaveManager::SetupWaveQue() {
    // TODO: 0x00124564 — survival/combo wave queue. Not used in Classic/Arcade/Zen.
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

bool WaveManager::IsWaveProcessing(int playerIdx) {
    // Per-player flag: if 0, wave already resolved.
    uint8_t flag = (&field_0x23c)[playerIdx];
    if (!flag) return false;

    WAVE_INFO* w = m_pCurrentWave[playerIdx];

    if (playerIdx == 0) {
        if (w) {
            if (w->m_bAllowBombsFrenzy == 0) goto done0;
            if (w->m_bAllowBombs == 0) {
                if (Fruit::GetNumActiveForPlayer(-1, false) >= 1) return true;
                if (Bomb::GetNumActiveForPlayer(-1, true) >= 1) return true;
                goto done0;
            }
        }
        ActorManager* am = ActorManager::GetInstance();
        if (am && am->GetNumEntities(0) != 0) return true;
        if (am && am->GetNumEntities(1) != 0) return true;
done0:
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
    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;

    for (long i = 0; i < count; ++i) {
        float minAngle = info ? info->m_MinAngle : -1.0f;
        float maxAngle = info ? info->m_MaxAngle :  1.0f;

        // Angle math: range in 182-unit (deg * 182) space.
        float range = minAngle * (-182.0f) + maxAngle * 182.0f;  // sum of components
        uint32_t r1 = (range > 0.0f) ? m_Random.Rand32((uint32_t)range) : 0;
        uint16_t angle = (uint16_t)(((int)r1 + (int)(minAngle * 182.0f)) * 0xb6);

        float speed = m_Random.RandF(1.5f) + 9.5f;   // 9.5..11.0
        float sin_a = SinIdx(angle);
        float cos_a = CosIdx(angle);

        float velMultX = info ? info->m_MinVel : 1.0f;
        float velMultY = info ? info->m_MaxVel : 1.0f;
        float velX = sin_a * speed * velMultX;
        float velY = cos_a * speed * velMultY;

        float posX = 0.0f;
        float posY = 0.0f;

        SpawnPlacement spawnType = info ? info->m_SpawnType : PLACEMENT_BOTTOM;

        switch (spawnType) {
        case PLACEMENT_BOTTOM:
        default:
            posX = (float)((int)(r1 % 320) - 160);
            posY = -240.0f;
            break;
        case PLACEMENT_BOTTOM_SLOW:
            posX = (float)((int)(r1 % 320) - 160);
            posY = -240.0f;
            velY *= 0.5f;
            break;
        case PLACEMENT_RANDOM_SIDE: {
            bool goLeft = (m_Random.Rand32(2) == 0);
            spawnType = goLeft ? PLACEMENT_LEFT : PLACEMENT_RIGHT;
        }   /* fall through */
        case PLACEMENT_LEFT:
            posX = -240.0f;
            posY = (float)((int)(m_Random.Rand32(320)) - 160);
            { float tmp = velX; velX = velY; velY = tmp; }
            velX = fabsf(velX);   // always launch rightward
            break;
        case PLACEMENT_RIGHT:
            posX = 240.0f;
            posY = (float)((int)(m_Random.Rand32(320)) - 160);
            { float tmp = velX; velX = velY; velY = tmp; }
            velX = -fabsf(velX);  // always launch leftward
            break;
        }

        float zOffset = info ? info->m_ZOffset : 0.0f;
        float chuckDelay = (zOffset > 0.0f) ? zOffset + 0.21f : 0.21f;

        Entity* e = am->Add(0, true);
        if (!e) continue;
        Fruit* f = static_cast<Fruit*>(e);
        f->pos  = Vec3(posX, posY, (float)(i * 32));
        f->vel  = Vec3(velX, velY, 0.0f);
        f->Init(0, (int)fruitType, 0);
        // gravity / timescale modifiers from spawner.
        // TODO: info->m_Gravity / m_TimeScale application matches binary call pattern.
        f->Chuck(f->vel, chuckDelay);
    }
}

// ----------------------------------------------------------------------------
// SpawnBomb — per wave-system-impl.md §2
// ----------------------------------------------------------------------------

void WaveManager::SpawnBomb(long count, long type, SPAWNER_INFO* spawner, int playerIdx) {
    (void)playerIdx;
    ActorManager* am = ActorManager::GetInstance();
    if (!am) return;

    for (long i = 1; i <= count; ++i) {
        float minAngle, maxAngle;
        if (type == 0) { minAngle = -1.0f; maxAngle = 1.0f; }
        else           { minAngle = spawner->m_MinAngle; maxAngle = spawner->m_MaxAngle; }

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
        float velMultX = (type == 0) ? 1.0f : spawner->m_MinVel;
        float velMultY = (type == 0) ? 1.0f : spawner->m_MaxVel;
        float zOffset  = (type == 0) ? 0.0f : spawner->m_ZOffset;

        float velX = sin_a * speed * velMultX;
        float velY = cos_a * speed * velMultY * 1.075f;  // DAT_00122218

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
                long newVelY = (long)(velX + speed * spawner->m_Gravity * (-0.65f));  // DAT_00122224
                spawnY = (long)(velY * -0.75f);
                spawnY = (long)((float)spawnY * (320.0f / 480.0f));   // DAT_0012221c/20
                velX = (float)(long)velY;
                velY = (float)newVelY;
                if (st == PLACEMENT_LEFT) { spawnX = -spawnX; velX = -velX; }
                break;
            }
            case PLACEMENT_RANDOM_SIDE: break;  // handled above
            }
        }

        float chuckDelay = (zOffset >= 0.0f) ? zOffset + 0.21f : 0.21f;  // DAT_0012258c

        // Single-player path only (MP not ported).
        Entity* e = am->Add(1, true);
        if (!e) continue;
        Bomb* b = static_cast<Bomb*>(e);
        b->pos = Vec3(spawnX, spawnY, spawnZ);
        b->vel = Vec3(velX, velY, 0.0f);                 // DAT_00122584 = 0
        b->Init(0, 0, 0);
        b->pos.y += -100.0f * b->scale.y;               // DAT_00122588 = -100
        b->Chuck(chuckDelay);

        Game* game = Game::GetInstance();
        if (game && game->gameMode == 2)
            Bomb::SetForPlayer(b, 1);  // arcade single-player
    }
}

// ----------------------------------------------------------------------------
// Draw
// ----------------------------------------------------------------------------

void WaveManager::Draw(int /*playerIdx*/) {
    // TODO: 0x00122ae8 — wave banner overlay draw.
}

void WaveManager::DeleteSpeedControl(HUDControl* /*c*/) {
    // TODO: 0x001217d4 — clear cached speed-control HUDControl* if matching.
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
        : w->m_BombScale1
          + w->wave_dt_inc * w->field_0x34
          + w->delaySpeedScale * m_Speed[playerIdx];

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
    // TODO: binary RNG check against ScoreThreshold / 2. Stub returns false.
    return false;
}

float WaveManager::GetComboBonusProgression(int /*playerIdx*/) {
    // TODO: 0x00121840.
    return 0.0f;
}

PROBABILITY_OVERIDE* WaveManager::GetCurrentOverideList(int /*playerIdx*/) {
    // TODO: 0x0012180c.
    return nullptr;
}

// ----------------------------------------------------------------------------
// Mutators
// ----------------------------------------------------------------------------

void WaveManager::AddToSpeedLossTime(float /*amount*/, int /*playerIdx*/) {
    // TODO: 0x001218ac.
}

void WaveManager::ResetSpeed(int /*playerIdx*/) {
    // TODO: 0x00122e94.
}

void WaveManager::AddSpeed(float amount, int playerIdx) {
    // Per wave-system-impl.md §6: writes to +0x58 + playerIdx*4 (overlapping slot).
    float* slot = &m_Speed[1 + playerIdx];   // +0x58 = m_Speed[1] for player 0
    float v = amount + *slot;
    if (v <= 0.0f)       v = 0.0f;
    else if (v >= 14.0f) v = 14.0f;
    *slot = v;
    // TODO: combo SFX, score bonus, PowerUpManager::ActivateScreenEffect — not ported.
}

void WaveManager::RecievedSync(int /*waveIdx*/, float /*score*/) {
    // TODO: 0x00122af8 — network sync.
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
void WaveManager::SendWaveSyncPacket()                               {}
bool WaveManager::ShouldDisplayNetworkWaitIndicator()               { return false; }
void WaveManager::RequestCoins()                                     {}
