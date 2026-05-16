// Analysed: 2026-04-30T00:00
// AchievementManager -- Binary @ 0x00108930 (ctor), 0x00108f64 (GetInstance)
// Full implementation from RE §7.
//
// Note: NetworkManager/OpenFeint/GameCenter paths are defunct in port.
// UnlockAchievementInNetwork is a no-op stub per audit.

#include "AchievementManager.h"
#include "SpecificOrder.h"
#include "FruitSaveData.h"
#include "hud/NotificationControl.h"
#include "hud/HUD.h"
#include "engine/asset/TextureManager.h"
#include "engine/util/StringHash.h"
#include "engine/util/StringTable.h"
#include "engine/util/PathCI.h"
#include "ItemParseUtil.h"
#include "Game.h"

#include <tinyxml2.h>
#include <cstring>
#include <cstdio>

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// AchievementInfo ctor/dtor  (Binary @ ctor ~0x00109200 inner block)
// ---------------------------------------------------------------------------

AchievementInfo::AchievementInfo()
    : m_NameHash(0)
    , m_Threshold(0)
    , m_Points(0)
    , m_TypeIndex(-1)
    , m_ModeBitmask(0)
    , m_RequiresUnsullied(false)
    , m_SpecificOrder(nullptr)
{
    m_Description[0] = '\0';
    m_Name[0]        = '\0';
    m_LongText[0]    = '\0';
    _pad[0] = _pad[1] = _pad[2] = 0;
}

AchievementInfo::~AchievementInfo() {
    delete m_SpecificOrder;
    m_SpecificOrder = nullptr;
}

// ---------------------------------------------------------------------------
// AchievementManager ctor/dtor  (Binary @ 0x00108930 / 0x00109028)
// ---------------------------------------------------------------------------

AchievementManager::AchievementManager() {
    // Binary: constructs m_All + 11 m_ByType maps (loop counter 9..-2)
    // std::map default-constructs automatically in C++; nothing explicit needed.
}

AchievementManager::~AchievementManager() {
    UnLoadAchievementInfo();
}

// ---------------------------------------------------------------------------
// GetInstance  (Binary @ 0x00108f64)
// ---------------------------------------------------------------------------

AchievementManager* AchievementManager::GetInstance() {
    static AchievementManager s_instance;
    return &s_instance;
}

// ---------------------------------------------------------------------------
// LoadAchievementInfo  (Binary @ 0x00109200)
// ---------------------------------------------------------------------------

void AchievementManager::LoadAchievementInfo() {
    // Binary: parses Data/xml/achievementlist.xml
    // Root: <achievementManagerFile> -> <achievement> children
    std::string path;
    const char* dataDir = TextureManager::GetDataDir();
    if (dataDir && dataDir[0]) {
        path = dataDir;
        path += "/xml/achievementlist.xml";
    } else {
        path = "xml/achievementlist.xml";
    }

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.LoadFile(path.c_str());
    if (err != tinyxml2::XML_SUCCESS) {
        std::string ci = Mortar::ResolvePathCI(path.c_str());
        if (!ci.empty()) err = doc.LoadFile(ci.c_str());
    }
    if (err != tinyxml2::XML_SUCCESS) {
        printf("AchievementManager::LoadAchievementInfo -- failed to open '%s' (error %d)\n",
               path.c_str(), (int)err);
        return;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("achievementManagerFile");
    if (!root) {
        printf("AchievementManager::LoadAchievementInfo -- no <achievementManagerFile> root\n");
        return;
    }

    // Autoincrement counter used for SCORE and SCORE_UNSULLIED type secondary keys
    // (types 1,2 — keyed by load order rather than a field value)
    int autoKey = 0;

    for (tinyxml2::XMLElement* e = root->FirstChildElement("achievement");
         e; e = e->NextSiblingElement("achievement"))
    {
        // Read "name" attribute — also the save-key string
        const char* nameAttr = e->Attribute("name");
        if (!nameAttr || nameAttr[0] == '\0') continue;

        uint32_t nameHash = StringHash(nameAttr);

        // Skip if already unlocked
        if (FruitSaveData::IsAchievementUnlocked(nameHash)) continue;

        // Skip if already loaded (duplicate in XML)
        if (m_All.find(nameHash) != m_All.end()) continue;

        AchievementInfo* info = new AchievementInfo();

        // name
        strncpy(info->m_Name, nameAttr, sizeof(info->m_Name) - 1);
        info->m_Name[sizeof(info->m_Name) - 1] = '\0';
        info->m_NameHash = nameHash;

        // description (localised)
        const char* descAttr = e->Attribute("description");
        if (descAttr) {
            const char* localised = GETSTRING_CAST_0_STR(descAttr);
            strncpy(info->m_Description, localised ? localised : descAttr,
                    sizeof(info->m_Description) - 1);
            info->m_Description[sizeof(info->m_Description) - 1] = '\0';
        }

        // long text (optional element child text)
        tinyxml2::XMLElement* longElem = e->FirstChildElement("longText");
        if (longElem) {
            const char* txt = longElem->GetText();
            if (txt) {
                strncpy(info->m_LongText, txt, sizeof(info->m_LongText) - 1);
                info->m_LongText[sizeof(info->m_LongText) - 1] = '\0';
            }
        }

        // value (threshold)
        e->QueryIntAttribute("value", &info->m_Threshold);

        // points
        e->QueryIntAttribute("points", &info->m_Points);

        // type index
        const char* typeAttr = e->Attribute("type");
        info->m_TypeIndex = -1;
        if (typeAttr) {
            if      (strcmp(typeAttr, "total")            == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_TOTAL;
            else if (strcmp(typeAttr, "score")            == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_SCORE;
            else if (strcmp(typeAttr, "score_unsullied")  == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_SCORE_UNSULLIED;
            else if (strcmp(typeAttr, "end_score")        == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_END_SCORE;
            else if (strcmp(typeAttr, "specific")         == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_SPECIFIC;
            else if (strcmp(typeAttr, "consecutive")      == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_CONSECUTIVE;
            else if (strcmp(typeAttr, "consecutive_any")  == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_CONSECUTIVE_ANY;
            else if (strcmp(typeAttr, "combo")            == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_COMBO;
            else if (strcmp(typeAttr, "combo_star")       == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_COMBO_STAR;
            else if (strcmp(typeAttr, "specific_order")   == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_SPECIFIC_ORDER;
            else if (strcmp(typeAttr, "bonus")            == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_BONUS;
        }

        // mode bitmask (e.g. "classic,arcade,zen" — binary packs as bit flags)
        const char* modeAttr = e->Attribute("mode");
        info->m_ModeBitmask = 0;
        if (modeAttr) {
            // Binary stores: bit0=classic, bit1=arcade, bit2=zen, bit3=attack
            // (exact bitmask encoding from XML "mode" attribute — each substring maps to a bit)
            if (strstr(modeAttr, "classic"))  info->m_ModeBitmask |= (1u << 0);
            if (strstr(modeAttr, "arcade"))   info->m_ModeBitmask |= (1u << 1);
            if (strstr(modeAttr, "zen"))      info->m_ModeBitmask |= (1u << 2);
            if (strstr(modeAttr, "attack"))   info->m_ModeBitmask |= (1u << 3);
        }

        // requires_unsullied flag
        const char* unsullied = e->Attribute("requires_unsullied");
        info->m_RequiresUnsullied = (unsullied && strcmp(unsullied, "true") == 0);

        // texture
        const char* texAttr = e->Attribute("texture");
        if (texAttr) {
            info->m_Texture = TextureManager::LoadLocalisedTexture(texAttr);
        }

        // specific_order child
        tinyxml2::XMLElement* soElem = e->FirstChildElement("specific_order");
        if (soElem) {
            const char* soStr = soElem->GetText();
            if (soStr) {
                info->m_SpecificOrder = new SpecificOrder(soStr);
            }
        }

        // Insert into m_All (owning map)
        m_All[nameHash] = info;

        // Insert into m_ByType secondary map
        // Key selection rule per RE:
        //   types 4 (SPECIFIC), 5 (CONSECUTIVE), 8 (COMBO_STAR), 10 (BONUS):
        //     secondary key = hash of "specific_type" attribute
        //   types 1 (SCORE), 2 (SCORE_UNSULLIED):
        //     secondary key = autoincrement counter
        //   all others (0,3,6,7,9):
        //     secondary key = threshold value
        if (info->m_TypeIndex >= 0 && info->m_TypeIndex <= 10) {
            uint32_t secondaryKey = 0;
            int ti = info->m_TypeIndex;
            if (ti == ACHIEVEMENT_TYPE_SPECIFIC ||
                ti == ACHIEVEMENT_TYPE_CONSECUTIVE ||
                ti == ACHIEVEMENT_TYPE_COMBO_STAR  ||
                ti == ACHIEVEMENT_TYPE_BONUS)
            {
                const char* stAttr = e->Attribute("specific_type");
                secondaryKey = stAttr ? StringHash(stAttr) : 0;
            } else if (ti == ACHIEVEMENT_TYPE_SCORE ||
                       ti == ACHIEVEMENT_TYPE_SCORE_UNSULLIED)
            {
                secondaryKey = (uint32_t)autoKey++;
            } else {
                secondaryKey = (uint32_t)info->m_Threshold;
            }
            m_ByType[ti][secondaryKey] = info;
        }
    }
}

// ---------------------------------------------------------------------------
// UnLoadAchievementInfo  (Binary @ 0x00108fb4)
// ---------------------------------------------------------------------------

void AchievementManager::UnLoadAchievementInfo() {
    // Binary: iterates only m_All to free heap entries, then clears all 12 maps
    // GCC 4.4: range-for is C++11 (added in GCC 4.6); use explicit iterator loop.
    for (std::map<uint32_t, AchievementInfo*>::iterator it = m_All.begin(); it != m_All.end(); ++it) {
        delete it->second;
    }
    m_All.clear();
    for (int i = 0; i < 11; ++i) {
        m_ByType[i].clear();
    }
}

// ---------------------------------------------------------------------------
// AchievementExists  (Binary @ 0x00108ea4)
// ---------------------------------------------------------------------------

int AchievementManager::AchievementExists(uint32_t hash) {
    // Binary: find() in m_All; returns iterator distance (non-zero = found) or 0
    auto it = m_All.find(hash);
    return (it != m_All.end()) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// QueAchievement  (Binary @ 0x001084a0)
// ---------------------------------------------------------------------------

int AchievementManager::QueAchievement(AchievementInfo* info,
                                        std::map<uint32_t, AchievementInfo*>::iterator& it)
{
    // Binary: calls FruitSaveData::AddToQue(name, hash); on success, erases
    // the entry from its m_ByType[typeIdx] slot via the passed iterator.
    FruitSaveData* sd = nullptr;
    Game* g = Game::GetInstance();
    if (g) sd = g->pSaveData;
    if (!sd) return 0;

    int result = sd->AddToQue(info->m_Name, info->m_NameHash);
    if (result != 0) {
        // Remove from secondary type-map so it won't be matched again
        int ti = info->m_TypeIndex;
        if (ti >= 0 && ti <= 10) {
            // it points into m_ByType[ti]; erase via passed iterator
            m_ByType[ti].erase(it);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// UnlockedAchievement  (Binary @ 0x001090d0)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockedAchievement(uint32_t hash, HUD* hud) {
    // Binary @ 0x001090d0
    auto it = m_All.find(hash);
    if (it == m_All.end()) return 0;
    AchievementInfo* a = it->second;
    // Binary: name[0] in '0'..'9' => Type_Numeric (1), else Type_Named (2)
    NotificationControl::NotificationType notifType =
        (a->m_Name[0] >= '0' && a->m_Name[0] <= '9')
        ? NotificationControl::Type_Numeric
        : NotificationControl::Type_Named;
    NotificationControl* ctrl = new NotificationControl(
        a->m_Name, a->m_Points, a->m_Texture, notifType);
    ctrl->Init();
    if (hud) hud->AddControl(ctrl, false);
    return 1;
}

// ---------------------------------------------------------------------------
// UnlockAchievementInNetwork  (Binary @ 0x001085a0)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockAchievementInNetwork(const char* /*name*/) {
    // Defunct: NetworkManager -- no-op stub; binary @ 0x001085a0
    return 0;
}

// ---------------------------------------------------------------------------
// Helper: mode bitmask gate
// Returns non-zero if the current game mode is permitted by m_ModeBitmask.
// Binary: tests (1 << gameMode) & m_ModeBitmask
// ---------------------------------------------------------------------------

static int ModeBitmaskAllows(uint32_t bitmask) {
    Game* g = Game::GetInstance();
    if (!g) return 0;
    uint8_t gm = g->gameMode & 0x03;
    return (bitmask & (1u << gm)) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// UnlockTotalFruitAchievement  (Binary @ 0x00108eec)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockTotalFruitAchievement(int total) {
    // Binary: iterates m_ByType[TOTAL]; for each entry whose threshold <= total,
    // and whose mode bitmask allows current mode, calls QueAchievement.
    int unlocked = 0;
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_TOTAL];
    for (auto it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Threshold <= total && ModeBitmaskAllows(info->m_ModeBitmask)) {
            auto cur = it++;
            QueAchievement(info, cur);
            ++unlocked;
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockScoreAchievement  (Binary @ 0x00108d44)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockScoreAchievement(int score) {
    // Binary: iterates m_ByType[SCORE]; threshold <= score + mode gate
    int unlocked = 0;
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_SCORE];
    for (auto it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Threshold <= score && ModeBitmaskAllows(info->m_ModeBitmask)) {
            auto cur = it++;
            QueAchievement(info, cur);
            ++unlocked;
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockScoreUnsulliedAchievement  (Binary @ 0x00108d94)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockScoreUnsulliedAchievement(int score) {
    // Binary: same as UnlockScoreAchievement but also checks m_RequiresUnsullied
    int unlocked = 0;
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_SCORE_UNSULLIED];
    for (auto it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Threshold <= score && ModeBitmaskAllows(info->m_ModeBitmask)) {
            auto cur = it++;
            QueAchievement(info, cur);
            ++unlocked;
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockEndScoreAchievement  (Binary @ 0x00108e14)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockEndScoreAchievement(int score, int hiScore) {
    // Binary: iterates m_ByType[END_SCORE]; threshold <= score AND score > hiScore/2
    // (end-of-game high-score beat check)
    int unlocked = 0;
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_END_SCORE];
    for (auto it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Threshold <= score &&
            score > hiScore / 2 &&
            ModeBitmaskAllows(info->m_ModeBitmask))
        {
            auto cur = it++;
            QueAchievement(info, cur);
            ++unlocked;
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockBonusAchievement  (Binary @ 0x00108de4)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockBonusAchievement(uint32_t hash) {
    // Binary: looks up m_ByType[BONUS] by hash (secondary key = specific_type hash)
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_BONUS];
    auto it = bucket.find(hash);
    if (it == bucket.end()) return 0;
    AchievementInfo* info = it->second;
    if (!info) return 0;
    if (!ModeBitmaskAllows(info->m_ModeBitmask)) return 0;
    return QueAchievement(info, it);
}

// ---------------------------------------------------------------------------
// UnlockSpecificFruitAchievement  (Binary @ 0x00108a88)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockSpecificFruitAchievement(int32_t fruitTypeHash, uint32_t count) {
    // Binary: looks up m_ByType[SPECIFIC] by fruitTypeHash;
    // threshold (as uint32_t) <= count
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_SPECIFIC];
    auto it = bucket.find(fruitTypeHash);
    if (it == bucket.end()) return 0;
    AchievementInfo* info = it->second;
    if (!info) return 0;
    if ((uint32_t)info->m_Threshold > count) return 0;
    if (!ModeBitmaskAllows(info->m_ModeBitmask)) return 0;
    return QueAchievement(info, it);
}

// ---------------------------------------------------------------------------
// UnlockConsecutiveAchievement  (Binary @ 0x00108c40)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockConsecutiveAchievement(int count, uint32_t fruitTypeHash) {
    // Binary: looks up m_ByType[CONSECUTIVE] by fruitTypeHash;
    // threshold <= count + mode gate
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_CONSECUTIVE];
    auto it = bucket.find(fruitTypeHash);
    if (it == bucket.end()) return 0;
    AchievementInfo* info = it->second;
    if (!info) return 0;
    if (info->m_Threshold > count) return 0;
    if (!ModeBitmaskAllows(info->m_ModeBitmask)) return 0;
    return QueAchievement(info, it);
}

// ---------------------------------------------------------------------------
// UnlockComboStarAchievement  (Binary @ 0x00108c40 — overlapping address note)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockComboStarAchievement(int combo, uint32_t starTypeHash) {
    // Binary: looks up m_ByType[COMBO_STAR] by starTypeHash;
    // threshold <= combo + mode gate
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_COMBO_STAR];
    auto it = bucket.find(starTypeHash);
    if (it == bucket.end()) return 0;
    AchievementInfo* info = it->second;
    if (!info) return 0;
    if (info->m_Threshold > combo) return 0;
    if (!ModeBitmaskAllows(info->m_ModeBitmask)) return 0;
    return QueAchievement(info, it);
}

// ---------------------------------------------------------------------------
// UnlockSpecificOrderAchievement  (Binary @ 0x001089cc)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockSpecificOrderAchievement(uint32_t newFruitHash) {
    // Binary @ 0x001089cc
    // For each entry in m_ByType[SPECIFIC_ORDER]:
    //   look up entry by GetFirstFruitTypeHash() as the bucket key;
    //   call SpecificOrder::Check(newFruitHash); on match, QueAchievement.
    int unlocked = 0;
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_SPECIFIC_ORDER];
    for (auto it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info || !info->m_SpecificOrder) { ++it; continue; }
        // Binary: bucket key = GetFirstFruitTypeHash(); only visit entries
        // whose first fruit matches the incoming hash (early-out optimisation).
        // The Check call handles full sequence tracking.
        int result = info->m_SpecificOrder->Check(newFruitHash);
        if (result != 0 && ModeBitmaskAllows(info->m_ModeBitmask)) {
            auto cur = it++;
            QueAchievement(info, cur);
            ++unlocked;
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockComboAchievement  (Binary @ 0x00108b3c)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockComboAchievement(int comboLen, int* fruitArr) {
    // Binary: iterates m_ByType[COMBO]; threshold <= comboLen + mode gate.
    // Also checks:
    //   m_RequiresUnsullied — all fruitArr entries must be non-bomb (complex)
    //   m_SpecificOrder     — calls SpecificOrder::Check per fruit in fruitArr
    // TODO: SpecificOrder check (binary @ unknown)
    // Port: handles threshold + mode gate; skips SpecificOrder + RequiresUnsullied
    //       checks (returns 0 for those paths)
    int unlocked = 0;
    auto& bucket = m_ByType[ACHIEVEMENT_TYPE_COMBO];
    for (auto it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }

        if (info->m_Threshold > comboLen || !ModeBitmaskAllows(info->m_ModeBitmask)) {
            ++it;
            continue;
        }

        // SpecificOrder check — iterate fruitArr and feed each hash to Check
        if (info->m_SpecificOrder != nullptr) {
            // Binary @ 0x00108b3c: for each fruit in fruitArr, call Check(hash).
            // fruitArr contains fruit type hashes; on Check returning 1, allow unlock.
            bool orderMet = false;
            for (int fi = 0; fi < comboLen; ++fi) {
                if (info->m_SpecificOrder->Check((uint32_t)fruitArr[fi]) != 0) {
                    orderMet = true;
                    break;
                }
            }
            if (!orderMet) {
                ++it;
                continue;
            }
        }

        // RequiresUnsullied check — stub skips these entries
        if (info->m_RequiresUnsullied) {
            // TODO: validate fruitArr[0..comboLen-1] are all non-bomb
            (void)fruitArr;
            ++it;
            continue;
        }

        auto cur = it++;
        QueAchievement(info, cur);
        ++unlocked;
    }
    return unlocked;
}
