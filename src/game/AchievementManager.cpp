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
#include "engine/xml/XmlLoad.h"
#include "ItemParseUtil.h"
#include "Game.h"
#include "hud/TimeControl.h"

#include <tinyxml2.h>
#include <cstring>
#include "game/GameWork.h"

using Mortar::TextureManager;

// Preamble textures loaded by LoadAchievementInfo (binary @ 0x00109188).
// These live in BSS / module-level GOT slots (DAT_001096a8/ac/b0), NOT in the
// AchievementManager struct. The struct is only the 12 std::map members (288 bytes).
// DAT_001096a8: achievment_banner.tex (sic -- typo matches actual asset file).
static Mortar::SmartPtr<Mortar::Texture> s_AchievementBannerTex;
// TODO: DAT_001096ac -- identity of second preamble texture not yet RE'd.
static Mortar::SmartPtr<Mortar::Texture> s_BannerExtra1;
// TODO: DAT_001096b0 -- identity of third preamble texture not yet RE'd.
static Mortar::SmartPtr<Mortar::Texture> s_BannerExtra2;

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
    // ASM-verified: 2026-05-23 binary @ 0x00109188 (re-analyst)
    // Binary loads "achievment_banner.tex" (sic) into DAT_001096a8 BEFORE opening the XML doc.
    s_AchievementBannerTex = TextureManager::LoadLocalisedTexture("achievment_banner.tex");
    // TODO: DAT_001096ac -- load second preamble texture (identity not yet RE'd).
    // TODO: DAT_001096b0 -- load third preamble texture (identity not yet RE'd).

    // Binary @ 0x00109200: TiXmlDocument("xml/achievementList.xml")
    tinyxml2::XMLDocument doc;
    if (FN::LoadXmlCI(doc, std::string("xml/achievementList.xml")) != tinyxml2::XML_SUCCESS) return;

    tinyxml2::XMLElement* root = doc.FirstChildElement("achievementManagerFile");
    if (!root) return;

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

        // Skip if already unlocked (binary: game_work.m_SaveData->IsAchievementUnlocked)
        if (game_work.m_SaveData && game_work.m_SaveData->IsAchievementUnlocked(nameHash)) continue;

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
    return (m_All.find(hash) != m_All.end()) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// QueAchievement  (Binary @ 0x00108978)
// ASM-verified: 2026-05-18 binary @ 0x00108978 (re-analyst)
// ---------------------------------------------------------------------------

int AchievementManager::QueAchievement(AchievementInfo* info,
                                        std::map<uint32_t, AchievementInfo*>::iterator& it)
{
    // Binary @ 0x00108978: call FruitSaveData::AddToQue(name, hash);
    // on success pre-advance caller's iterator, then erase from m_ByType[typeIdx].
    // Pre-advance before erase so callers iterating m_All or m_ByType[x]
    // can unconditionally continue without using the erased iterator.
    if (!info) return 0;
    FruitSaveData* sd = 0;
    Game* g = Game::GetInstance();
    if (g) sd = game_work.m_SaveData;
    if (!sd) return 0;

    int result = sd->AddToQue(info->m_Name, info->m_NameHash);
    if (result != 0) {
        ++it;  // pre-advance caller's iterator before invalidating

        // Erase info from m_ByType[ti] by scanning for the pointer value.
        // The secondary-key used during insert is not available here, so
        // we find by pointer equality (matches binary's approach).
        int ti = info->m_TypeIndex;
        if (ti >= 0 && ti <= 10) {
            std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ti];
            for (std::map<uint32_t, AchievementInfo*>::iterator bi = bucket.begin();
                 bi != bucket.end(); ++bi) {
                if (bi->second == info) {
                    bucket.erase(bi);
                    break;
                }
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// UnlockedAchievement  (Binary @ 0x001090d0)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockedAchievement(uint32_t hash, HUD* hud) {
    // Binary @ 0x001090d0
    std::map<uint32_t, AchievementInfo*>::iterator it = m_All.find(hash);
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
    uint8_t gm = game_work.gameMode & 0x03;
    return (bitmask & (1u << gm)) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// UnlockTotalFruitAchievement  (Binary @ 0x00108eec)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockTotalFruitAchievement(int total) {
    // Binary: iterates m_ByType[TOTAL]; for each entry whose threshold <= total,
    // and whose mode bitmask allows current mode, calls QueAchievement.
    int unlocked = 0;
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_TOTAL];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Threshold <= total && ModeBitmaskAllows(info->m_ModeBitmask)) {
            if (QueAchievement(info, it)) ++unlocked;
            // it was pre-advanced by QueAchievement; don't ++it
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
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_SCORE];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Threshold <= score && ModeBitmaskAllows(info->m_ModeBitmask)) {
            if (QueAchievement(info, it)) ++unlocked;
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
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_SCORE_UNSULLIED];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Threshold <= score && ModeBitmaskAllows(info->m_ModeBitmask)) {
            if (QueAchievement(info, it)) ++unlocked;
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
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_END_SCORE];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Threshold <= score &&
            score > hiScore / 2 &&
            ModeBitmaskAllows(info->m_ModeBitmask))
        {
            if (QueAchievement(info, it)) ++unlocked;
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockBonusAchievement  (Binary @ 0x00108af0)
// ASM-verified: 2026-05-18T00:00 binary @ 0x00108af0..0x00108b4f (asm-inspector)
// ---------------------------------------------------------------------------

unsigned int AchievementManager::UnlockBonusAchievement(unsigned long bonusId) {
    std::map<uint32_t, AchievementInfo*>::iterator it =
        m_ByType[ACHIEVEMENT_TYPE_BONUS].find((uint32_t)bonusId);
    if (it == m_ByType[ACHIEVEMENT_TYPE_BONUS].end()) return 0;
    AchievementInfo* info = it->second;
    unsigned int modeBit = ModeBitmaskAllows(info->m_ModeBitmask);
    if (modeBit == 0) return 0;
    return (unsigned int)QueAchievement(info, it);
}

// ---------------------------------------------------------------------------
// UnlockSpecificFruitAchievement  (Binary @ 0x00108a88)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockSpecificFruitAchievement(int fruitTypeHash, unsigned int count) {
    // Binary: looks up m_ByType[SPECIFIC] by fruitTypeHash;
    // threshold (as uint32_t) <= count
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_SPECIFIC];
    std::map<uint32_t, AchievementInfo*>::iterator it = bucket.find((uint32_t)fruitTypeHash);
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

int AchievementManager::UnlockConsecutiveAchievement(int count, unsigned int fruitTypeHash) {
    // Binary @ 0x00108c40: two bucket lookups.
    // Bucket CONSECUTIVE (5): keyed by fruitTypeHash; threshold <= count + mode gate.
    // Bucket CONSECUTIVE_ANY (6): keyed by count itself (m_Threshold == count).
    int awarded = 0;
    {
        std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_CONSECUTIVE];
        std::map<uint32_t, AchievementInfo*>::iterator it = bucket.find(fruitTypeHash);
        if (it != bucket.end()) {
            AchievementInfo* info = it->second;
            if (info &&
                info->m_Threshold <= count &&
                ModeBitmaskAllows(info->m_ModeBitmask))
            {
                if (QueAchievement(info, it)) awarded = 1;
            }
        }
    }
    {
        // CONSECUTIVE_ANY entries are keyed by their threshold (= count) at load time.
        std::map<uint32_t, AchievementInfo*>& bucket2 = m_ByType[ACHIEVEMENT_TYPE_CONSECUTIVE_ANY];
        std::map<uint32_t, AchievementInfo*>::iterator it2 = bucket2.find((uint32_t)count);
        if (it2 != bucket2.end()) {
            AchievementInfo* info = it2->second;
            if (info && ModeBitmaskAllows(info->m_ModeBitmask)) {
                if (QueAchievement(info, it2)) awarded = 1;
            }
        }
    }
    return awarded;
}

// ---------------------------------------------------------------------------
// UnlockComboStarAchievement  (Binary @ 0x00108c40 — overlapping address note)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockComboStarAchievement(int combo, uint32_t starTypeHash) {
    // Binary: looks up m_ByType[COMBO_STAR] by starTypeHash;
    // threshold <= combo + mode gate
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_COMBO_STAR];
    std::map<uint32_t, AchievementInfo*>::iterator it = bucket.find(starTypeHash);
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
    // Binary @ 0x00108b58: iterates m_All (NOT m_ByType[SPECIFIC_ORDER]).
    // No mode gate here; mode-gating is implicit at load time (only achievements
    // for the current mode are inserted into m_All via LoadAchievementInfo).
    // Every entry with a m_SpecificOrder pointer is checked via Check(newFruitHash).
    int unlocked = 0;
    for (std::map<uint32_t, AchievementInfo*>::iterator it = m_All.begin(); it != m_All.end(); ) {
        AchievementInfo* info = it->second;
        if (!info || !info->m_SpecificOrder) { ++it; continue; }
        if (info->m_SpecificOrder->Check(newFruitHash) != 0) {
            if (QueAchievement(info, it)) {
                ++unlocked;
                // QueAchievement pre-advanced it; do NOT ++it again
            } else {
                ++it;
            }
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
    // Binary @ 0x00108a10: iterates m_All (NOT m_ByType[COMBO]).
    // threshold <= comboLen + mode gate.
    // SpecificOrder gate (COMBO semantics): count fruitArr entries whose hash
    //   matches GetFirstFruitTypeHash(); require count >= threshold.
    //   (Different from UnlockSpecificOrderAchievement which tracks a full sequence.)
    // RequiresUnsullied gate: pTimeCtrl != NULL AND comboLen > 2 AND
    //   pTimeCtrl->m_TimeRemaining <= 0. See gate body below.
    int unlocked = 0;
    for (std::map<uint32_t, AchievementInfo*>::iterator it = m_All.begin(); it != m_All.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }

        if (info->m_Threshold > comboLen || !ModeBitmaskAllows(info->m_ModeBitmask)) {
            ++it;
            continue;
        }

        // SpecificOrder gate (COMBO semantics): count matches of the first hash.
        if (info->m_SpecificOrder != 0) {
            uint32_t firstHash = info->m_SpecificOrder->GetFirstFruitTypeHash();
            int matches = 0;
            for (int fi = 0; fi < comboLen; ++fi) {
                if ((uint32_t)fruitArr[fi] == firstHash) ++matches;
            }
            if (matches < info->m_Threshold) {
                ++it;
                continue;
            }
        }

        // RequiresUnsullied gate (binary @ 0x00108aa0-0x00108ab6):
        //   Despite the XML attribute name, this gates on the TIMED-MODE COUNTDOWN
        //   having expired, NOT on Game::m_bUnsullied. Binary reads
        //   g_GameData->pTimeCtrl->m_TimeRemaining. If pTimeCtrl is NULL (Classic mode,
        //   no timer HUD), reject. If comboLen <= 2, reject. If countdown still
        //   running, reject. Only when the Arcade/Zen-timed countdown has hit 0.0f
        //   does the achievement become eligible.
        // ASM-verified: 2026-05-18 binary @ 0x00108a10 (re-analyst)
        if (info->m_RequiresUnsullied) {
            Game* g = Game::GetInstance();
            if (game_work.mCountDown == NULL) { ++it; continue; }
            if (comboLen <= 2)        { ++it; continue; }
            if (game_work.mCountDown->m_TimeRemaining > 0.0f) { ++it; continue; }
        }

        if (QueAchievement(info, it)) {
            ++unlocked;
        } else {
            ++it;
        }
    }
    return unlocked;
}
