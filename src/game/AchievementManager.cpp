// Analysed: 2026-04-30T00:00
// AchievementManager -- v1.6.1 ctor @0x00117494, GetInstance @0x00117e08
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
#include "engine/xml/TiXml.h"
#include "ItemParseUtil.h"
#include "Game.h"
#include "hud/TimeControl.h"

#include <cstring>
#include <cctype>
#include "game/GameWork.h"
#include "game/GameMode.h"

using Mortar::TextureManager;

// ---------------------------------------------------------------------------
// AchievementInfo ctor/dtor  (inlined in v1.6.1 LoadAchievementInfo @0x00118198)
// ---------------------------------------------------------------------------

AchievementInfo::AchievementInfo()
    : m_NameHash(0)
    , m_pDescription(nullptr)
    , m_Total(0)
    , m_Score(0)
    , m_TypeIndex(0xb)  // binary ctor sentinel = 11 (v1.6.1 AchievementInfo @0x00118198)
    , m_ModeBitmask(0)
    , m_IsGameOver(false)
    , m_SpecificOrder(nullptr)
{
    m_DisplayName[0] = '\0';
    m_Name[0]        = '\0';
}

AchievementInfo::~AchievementInfo() {
    delete m_SpecificOrder;
    m_SpecificOrder = nullptr;
}

// ---------------------------------------------------------------------------
// AchievementManager ctor/dtor  (v1.6.1 ctor @0x00117494 / dtor @0x00117f58)
// ---------------------------------------------------------------------------

AchievementManager::AchievementManager() {
    // Binary: constructs m_All + 11 m_ByType maps (loop counter 9..-2)
    // std::map default-constructs automatically in C++; nothing explicit needed.
}

AchievementManager::~AchievementManager() {
    UnLoadAchievementInfo();
}

// ---------------------------------------------------------------------------
// GetInstance  (v1.6.1 AchievementManager::GetInstance @0x00117e08)
// ---------------------------------------------------------------------------

AchievementManager* AchievementManager::GetInstance() {
    static AchievementManager s_instance;
    return &s_instance;
}

// ---------------------------------------------------------------------------
// LoadAchievementInfo  (Binary @ 0x00118198)
//
// mode-mask parsing uses the shared GameMode.h global ParseModeMask
// (v1.6.1 ParseModeMask @0x0014f320) -- see GameMode.cpp for the
// implementation; no local copy here.
// ---------------------------------------------------------------------------

void AchievementManager::LoadAchievementInfo() {
    // ASM-verified: 2026-05-23 v1.6.1 AchievementManager::LoadAchievementInfo @ 0x00118198 (re-analyst)
    // Binary loads exactly 2 preamble textures BEFORE opening the XML doc, assigned
    // directly to NotificationControl's class statics (mangled
    // _ZN19NotificationControl8s_bannerE / s_unlockBannerE) -- Draw() reads them via
    // IsValid()/Get() to gate the banner quad.
    NotificationControl::s_banner = TextureManager::LoadLocalisedTexture("achievment_banner.tex");
    NotificationControl::s_unlockBanner = TextureManager::LoadLocalisedTexture("hud_unlocked_dialog.tex");

    // Binary @ 0x00118198: TiXmlDocument("xml/achievementList.xml")
    TiXmlDocument doc;
    if (!doc.LoadFile("xml/achievementList.xml")) return;

    TiXmlElement root = doc.FirstChildElement("achievementManagerFile");
    if (!root) return;

    // Autoincrement counter used for SCORE and SCORE_UNSULLIED type secondary keys
    // (types 1,2 — keyed by load order rather than a field value)
    int autoKey = 0;

    for (TiXmlElement e = root.FirstChildElement("achievement");
         e; e = e.NextSiblingElement("achievement"))
    {
        // Read "id" attribute — the unique save/map key matching ItemInfo::m_Hash hash space.
        // Binary @ 0x00118198 (LoadAchievementInfo): m_All keyed by StringHash(e.Attribute("id")).
        const char* idAttr = e.Attribute("id");
        if (!idAttr || idAttr[0] == '\0') continue;

        uint32_t idHash = StringHash(idAttr);

        // Read "name" attribute — localized display name.
        // ASM-spec v1.6.1 LoadAchievementInfo @0x00118198: strcpy(info->m_DisplayName,
        // GETSTRING_CAST_0_STR(nameAttr)) -- localized name, drawn by the unlock popup.
        const char* nameAttr = e.Attribute("name");

        // Skip if already unlocked (binary: game_work.m_SaveData->IsAchievementUnlocked)
        if (game_work.m_SaveData && game_work.m_SaveData->IsAchievementUnlocked(idHash)) continue;

        // Skip if already loaded (id is unique per entry, unlike name which is shared)
        if (m_All.find(idHash) != m_All.end()) continue;

        AchievementInfo* info = new AchievementInfo();

        // id is the save-key and map key (must match ItemInfo::m_Hash for AchievementExists gates)
        strncpy(info->m_Name, idAttr, sizeof(info->m_Name) - 1);
        info->m_Name[sizeof(info->m_Name) - 1] = '\0';
        info->m_NameHash = idHash;

        // display name (localised) -- ASM-spec v1.6.1 LoadAchievementInfo @0x00118198
        if (nameAttr) {
            strncpy(info->m_DisplayName, GETSTRING_CAST_0_STR(nameAttr),
                    sizeof(info->m_DisplayName) - 1);
            info->m_DisplayName[sizeof(info->m_DisplayName) - 1] = '\0';
        }

        // description key (constructed, NOT read from an XML "description" attribute).
        // ASM-spec v1.6.1 LoadAchievementInfo @0x00118198: the binary derives the
        // GETSTRING key by patching a two-digit numeric suffix, lifted out of
        // nameAttr, into one of two fixed literal templates:
        //   strlen(nameAttr) >= 0x13 && isdigit(nameAttr[0x11])
        //     -> key = "LITE_ACHIEVEMENT_DESC_XX" with nameAttr[0x11..0x12] at [22..23]
        //   strlen(nameAttr) > 0xd && isdigit(nameAttr[0xc])
        //     -> key = "ACHIEVEMENT_DESC_XX" with nameAttr[0xc..0xd] at [17..18]
        //   otherwise -> m_pDescription = 0 (no description)
        info->m_pDescription = 0;
        if (nameAttr) {
            size_t nameLen = strlen(nameAttr);
            if (nameLen >= 0x13 && isdigit((unsigned char)nameAttr[0x11])) {
                char key[32];
                strncpy(key, "LITE_ACHIEVEMENT_DESC_XX", sizeof(key));
                key[22] = nameAttr[0x11];
                key[23] = nameAttr[0x12];
                info->m_pDescription = GETSTRING_CAST_0_STR(key);
            } else if (nameLen > 0xd && isdigit((unsigned char)nameAttr[0xc])) {
                char key[32];
                strncpy(key, "ACHIEVEMENT_DESC_XX", sizeof(key));
                key[17] = nameAttr[0xc];
                key[18] = nameAttr[0xd];
                info->m_pDescription = GETSTRING_CAST_0_STR(key);
            }
        }

        // total (threshold)
        e.QueryIntAttribute("total", &info->m_Total);

        // score
        e.QueryIntAttribute("score", &info->m_Score);

        // type index
        const char* typeAttr = e.Attribute("type");
        info->m_TypeIndex = 0xb;  // sentinel (binary ctor default)
        if (typeAttr) {
            // v1.6.1 AchievementManager::LoadAchievementInfo @0x00118198: type-string
            // literals are UPPERCASE in the binary; achievementlist.xml uses uppercase
            // "type" values exclusively, so lowercase literals here never matched and
            // every m_TypeIndex stayed at the 0xb sentinel.
            if      (strcmp(typeAttr, "TOTAL")            == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_TOTAL;
            else if (strcmp(typeAttr, "SCORE")            == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_SCORE;
            else if (strcmp(typeAttr, "SCORE_UNSULLIED")  == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_SCORE_UNSULLIED;
            else if (strcmp(typeAttr, "END_SCORE")        == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_END_SCORE;
            else if (strcmp(typeAttr, "SPECIFIC")         == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_SPECIFIC;
            else if (strcmp(typeAttr, "CONSECUTIVE")      == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_CONSECUTIVE;
            else if (strcmp(typeAttr, "CONSECUTIVE_ANY")  == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_CONSECUTIVE_ANY;
            else if (strcmp(typeAttr, "COMBO")            == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_COMBO;
            else if (strcmp(typeAttr, "COMBO_STAR")       == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_COMBO_STAR;
            else if (strcmp(typeAttr, "SPECIFIC_ORDER")   == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_SPECIFIC_ORDER;
            else if (strcmp(typeAttr, "BONUS_ACHIEVED")   == 0) info->m_TypeIndex = ACHIEVEMENT_TYPE_BONUS;
        }

        // mode bitmask (e.g. "CLASSIC,ZEN" — binary packs as bit flags)
        // v1.6.1 LoadAchievementInfo @0x00118198: info->m_ModeBitmask = ParseModeMask(modeAttr)
        // unconditionally -- ParseModeMask itself defaults an absent/empty attr to the
        // wildcard (all bits), NOT 0. Previous port bug: case-sensitive strstr against
        // lowercase literals never matched the XML's uppercase mode values, and the
        // absent-attr default was 0 (deny-all) instead of the binary's -1 (allow-all),
        // so every achievement's m_ModeBitmask stayed 0 and ModeBitmaskAllows() always
        // returned false.
        const char* modeAttr = e.Attribute("mode");
        info->m_ModeBitmask = ParseModeMask(modeAttr);

        // game-over flag -- ASM-spec v1.6.1 LoadAchievementInfo @0x00118198: reads
        // "isGameOver" attribute, true when == 1 (not "requires_unsullied"/"true").
        int isGameOverVal = 0;
        e.QueryIntAttribute("isGameOver", &isGameOverVal);
        info->m_IsGameOver = (isGameOverVal == 1);

        // texture
        const char* texAttr = e.Attribute("texture");
        if (texAttr) {
            info->m_Texture = TextureManager::LoadLocalisedTexture(texAttr);
        }

        // Insert into m_All (owning map)
        m_All[idHash] = info;
        // v1.6.1 LoadAchievementInfo @0x00118198 tail: push_back the kept id-hash.
        // Write-only in v1.6.1 (no reader; kept for byte-faithful behavior).
        m_AllHashes.push_back(idHash);

        // Insert into m_ByType secondary map
        // Key selection rule per RE:
        //   types 4 (SPECIFIC), 5 (CONSECUTIVE), 8 (COMBO_STAR), 10 (BONUS):
        //     secondary key = hash of "specific_type" attribute
        //   types 1 (SCORE), 2 (SCORE_UNSULLIED):
        //     secondary key = autoincrement counter
        //   all others (0,3,6,7,9):
        //     secondary key = threshold value
        if (info->m_TypeIndex <= 10) {  // uint32_t; 0xb = sentinel (no type), 0..10 = valid
            uint32_t secondaryKey = 0;
            int ti = (int)info->m_TypeIndex;
            if (ti == ACHIEVEMENT_TYPE_SPECIFIC ||
                ti == ACHIEVEMENT_TYPE_CONSECUTIVE ||
                ti == ACHIEVEMENT_TYPE_COMBO_STAR  ||
                ti == ACHIEVEMENT_TYPE_BONUS)
            {
                const char* stAttr = e.Attribute("specific_type");
                secondaryKey = stAttr ? StringHash(stAttr) : 0;
            } else if (ti == ACHIEVEMENT_TYPE_SCORE ||
                       ti == ACHIEVEMENT_TYPE_SCORE_UNSULLIED)
            {
                secondaryKey = (uint32_t)autoKey++;
            } else {
                secondaryKey = (uint32_t)info->m_Total;
            }

            // v1.6.1 LoadAchievementInfo @0x00118198: SpecificOrder construction --
            // NOT from an invented "<specific_order>" child element (no such element
            // exists in the real XML schema); built from the existing "specific_type"
            // attribute instead.
            if (ti == ACHIEVEMENT_TYPE_SPECIFIC_ORDER) {
                // Binary constructs unconditionally, even if specific_type is absent
                // (ctor treats null/empty spec as a no-op, zero slots parsed).
                const char* soAttr = e.Attribute("specific_type");
                info->m_SpecificOrder = new SpecificOrder(soAttr ? soAttr : "");
            } else if (ti == ACHIEVEMENT_TYPE_COMBO) {
                // "SpecificOrder gate (COMBO semantics)": COMBO achievements also get a
                // SpecificOrder when specific_type is present and non-empty.
                const char* soAttr = e.Attribute("specific_type");
                if (soAttr && soAttr[0] != '\0') {
                    info->m_SpecificOrder = new SpecificOrder(soAttr);
                }
            }

            m_ByType[ti][secondaryKey] = info;
        }
    }
}

// ---------------------------------------------------------------------------
// UnLoadAchievementInfo  (v1.6.1 AchievementManager::UnLoadAchievementInfo @0x00117ea4)
// ---------------------------------------------------------------------------

void AchievementManager::UnLoadAchievementInfo() {
    // Binary body @0x00117ea4 opens with two SmartPtr nulls -- the exact inverse of
    // LoadAchievementInfo's two preamble texture loads -- before the map walk.
    // Nulling twice is safe: GameDestroy calls this, then the singleton dtor runs it
    // again at atexit against already-null slots.
    NotificationControl::s_banner.SetNull();
    NotificationControl::s_unlockBanner.SetNull();

    // Binary: iterates only m_All to free heap entries, then clears all 12 maps
    // GCC 4.4: range-for is C++11 (added in GCC 4.6); use explicit iterator loop.
    for (std::map<uint32_t, AchievementInfo*>::iterator it = m_All.begin(); it != m_All.end(); ++it) {
        delete it->second;
    }
    m_All.clear();
    for (int i = 0; i < 11; ++i) {
        m_ByType[i].clear();
    }
    m_AllHashes.clear();  // v1.6.1: UnLoadAchievementInfo also clears the id-hash vector
}

// ---------------------------------------------------------------------------
// AchievementExists  (Binary @ 0x00116ea8)
// ---------------------------------------------------------------------------

int AchievementManager::AchievementExists(uint32_t hash) {
    // Binary: find() in m_All; returns iterator distance (non-zero = found) or 0
    return (m_All.find(hash) != m_All.end()) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// QueAchievement
// ASM-spec v1.6.1 AchievementManager::QueAchievement @ 0x0011750c
// (Downgraded from ASM-verified: the stamp survived a port-added
//  Game::GetInstance + m_SaveData null guard being inserted into the body.)
// ---------------------------------------------------------------------------

int AchievementManager::QueAchievement(AchievementInfo* info,
                                        std::map<uint32_t, AchievementInfo*>::iterator& it)
{
    // v1.6.1 @0x0011750c: call FruitSaveData::AddToQue(name, hash);
    // on success pre-advance caller's iterator, then erase from m_ByType[typeIdx].
    // Pre-advance before erase so callers iterating m_All or m_ByType[x]
    // can unconditionally continue without using the erased iterator.
    // ASM-spec v1.6.1 QueAchievement @0x0011750c: `subs r4,r1,#0; beq ret0` is the
    // only guard (the info arg). m_SaveData then comes from
    // `ldr r3,[r3,r2]; ldr r0,[r3,#0x50]` -- game_work off the GOT, no
    // Game::GetInstance and no null test before AddToQue.
    if (!info) return 0;
    FruitSaveData* sd = game_work.m_SaveData;

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
// UnlockedAchievement  (Binary @ 0x001180a8)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockedAchievement(uint32_t hash, HUD* hud) {
    // ASM-spec v1.6.1 AchievementManager::UnlockedAchievement @ 0x001180a8
    std::map<uint32_t, AchievementInfo*>::iterator it = m_All.find(hash);
    if (it == m_All.end()) return 0;
    AchievementInfo* a = it->second;
    // Binary: m_Name[0] (raw id) in '0'..'9' => Type_Numeric (1), else Type_Named (2)
    NotificationControl::NotificationType notifType =
        (a->m_Name[0] >= '0' && a->m_Name[0] <= '9')
        ? NotificationControl::Type_Numeric
        : NotificationControl::Type_Named;
    // v1.6.1 @0x001180a8: ctor arg 1 is m_DisplayName (localized), not the raw id.
    NotificationControl* ctrl = new NotificationControl(
        a->m_DisplayName, a->m_Score, a->m_Texture, notifType);
    ctrl->Init();
    if (hud) hud->AddControl(ctrl, false);
    return 1;
}

// ---------------------------------------------------------------------------
// UnlockAchievementInNetwork  (Binary @ 0x00116ee4)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockAchievementInNetwork(const char* /*name*/) {
    // Defunct: NetworkManager -- no-op stub; v1.6.1 AchievementManager::UnlockAchievementInNetwork @ 0x00116ee4
    return 0;
}

// ---------------------------------------------------------------------------
// Helper: mode bitmask gate
// Returns non-zero if the current game mode is permitted by m_ModeBitmask.
// Binary: tests (1 << gameMode) & m_ModeBitmask
// ---------------------------------------------------------------------------

// UnlockTotalFruitAchievement @0x00117d48 does NOT use this helper -- its loop has
// no GetModeBitMask call at all. UnlockScoreAchievement @0x00117bd0 does. The
// remaining call sites below are still unconfirmed against their binary bodies.
// TODO: v1.6.1 0x00117bd0 (UnlockScoreAchievement) -- the binary gate is
// `GetModeBitMask(game_work+0x4) & info->+0x98`, a real function call. This helper
// models it as `1u << (gameMode & 3)`, which folds modes 4+ onto 0..3. RE
// GetModeBitMask and port it as its own function.
// Every peer reads game_work straight off the GOT with no null test, so no guard here.
static int ModeBitmaskAllows(uint32_t bitmask) {
    uint8_t gm = game_work.gameMode & 0x03;
    return (bitmask & (1u << gm)) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// UnlockTotalFruitAchievement  (v1.6.1 @0x00117d48)
// ---------------------------------------------------------------------------

// ASM-spec v1.6.1 AchievementManager::UnlockTotalFruitAchievement @ 0x00117d48:
// iterates m_ByType[TOTAL]; the ONLY test is `info->m_Total(+0x8c) <= total`,
// then a straight QueAchievement call. No GetModeBitMask anywhere in the loop --
// unlike UnlockScoreAchievement @0x00117bd0, which does gate on it. Returns a 0/1
// "queued something" flag, not a count.
int AchievementManager::UnlockTotalFruitAchievement(int total) {
    int unlocked = 0;
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_TOTAL];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Total <= total) {
            if (QueAchievement(info, it)) {
                unlocked = 1;
                // it was pre-advanced by QueAchievement; don't ++it
            } else {
                ++it;  // binary always advances, even when QueAchievement fails
            }
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockScoreAchievement  (Binary @ 0x00117bd0)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockScoreAchievement(int score) {
    // Binary: iterates m_ByType[SCORE]; threshold <= score + mode gate
    int unlocked = 0;
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_SCORE];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Total <= score && ModeBitmaskAllows(info->m_ModeBitmask)) {
            if (QueAchievement(info, it)) {
                ++unlocked;
                // it was pre-advanced by QueAchievement; don't ++it
            } else {
                ++it;  // binary always advances, even when QueAchievement fails
            }
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockScoreUnsulliedAchievement  (Binary @ 0x00117c8c)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockScoreUnsulliedAchievement(int score) {
    // Binary: same as UnlockScoreAchievement but also checks m_IsGameOver (requires_unsullied)
    int unlocked = 0;
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_SCORE_UNSULLIED];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        if (info->m_Total <= score && ModeBitmaskAllows(info->m_ModeBitmask)) {
            if (QueAchievement(info, it)) {
                ++unlocked;
                // it was pre-advanced by QueAchievement; don't ++it
            } else {
                ++it;  // binary always advances, even when QueAchievement fails
            }
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockEndScoreAchievement  (Binary @ 0x00117880)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockEndScoreAchievement(int score, int hiScore) {
    // v1.6.1 AchievementManager::UnlockEndScoreAchievement @0x00117880: iterates
    // m_ByType[END_SCORE]; exact-match/sentinel test, NOT a "score>threshold" test --
    //   match = (score == info->m_Total) || (info->m_Total < 0 && score == hiScore)
    // Prior port condition (info->m_Total <= score && score > hiScore/2) was fabricated.
    int unlocked = 0;
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_END_SCORE];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }
        const bool match = (score == info->m_Total) ||
                            (info->m_Total < 0 && score == hiScore);
        if (match && ModeBitmaskAllows(info->m_ModeBitmask)) {
            if (QueAchievement(info, it)) {
                ++unlocked;
                // it was pre-advanced by QueAchievement; don't ++it
            } else {
                ++it;  // binary always advances, even when QueAchievement fails
            }
        } else {
            ++it;
        }
    }
    return unlocked;
}

// ---------------------------------------------------------------------------
// UnlockBonusAchievement  (Binary @ 0x0011773c)
// ASM-verified: 2026-05-18T00:00 v1.6.1 AchievementManager::UnlockBonusAchievement @ 0x0011773c (asm-inspector)
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
// UnlockSpecificFruitAchievement  (v1.6.1 @0x00117a68)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockSpecificFruitAchievement(int fruitTypeHash, unsigned int count) {
    // Binary: looks up m_ByType[SPECIFIC] by fruitTypeHash;
    // threshold (as uint32_t) <= count
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_SPECIFIC];
    std::map<uint32_t, AchievementInfo*>::iterator it = bucket.find((uint32_t)fruitTypeHash);
    if (it == bucket.end()) return 0;
    AchievementInfo* info = it->second;
    if (!info) return 0;
    if ((uint32_t)info->m_Total > count) return 0;
    if (!ModeBitmaskAllows(info->m_ModeBitmask)) return 0;
    return QueAchievement(info, it);
}

// ---------------------------------------------------------------------------
// UnlockConsecutiveAchievement  (v1.6.1 @0x00117948)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockConsecutiveAchievement(int count, unsigned int fruitTypeHash) {
    // v1.6.1 @0x00117948: two bucket lookups.
    // Bucket CONSECUTIVE (5): keyed by fruitTypeHash; threshold <= count + mode gate.
    // Bucket CONSECUTIVE_ANY (6): keyed by count itself (m_Total == count).
    int awarded = 0;
    {
        std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_CONSECUTIVE];
        std::map<uint32_t, AchievementInfo*>::iterator it = bucket.find(fruitTypeHash);
        if (it != bucket.end()) {
            AchievementInfo* info = it->second;
            if (info &&
                info->m_Total <= count &&
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
// UnlockComboStarAchievement  (v1.6.1 @0x00117b20)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockComboStarAchievement(int combo, uint32_t starTypeHash) {
    // Binary: looks up m_ByType[COMBO_STAR] by starTypeHash;
    // threshold <= combo + mode gate
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_COMBO_STAR];
    std::map<uint32_t, AchievementInfo*>::iterator it = bucket.find(starTypeHash);
    if (it == bucket.end()) return 0;
    AchievementInfo* info = it->second;
    if (!info) return 0;
    if (info->m_Total > combo) return 0;
    if (!ModeBitmaskAllows(info->m_ModeBitmask)) return 0;
    return QueAchievement(info, it);
}

// ---------------------------------------------------------------------------
// UnlockSpecificOrderAchievement
// ASM-spec v1.6.1 AchievementManager::UnlockSpecificOrderAchievement @ 0x001177e0
// ---------------------------------------------------------------------------

int AchievementManager::UnlockSpecificOrderAchievement(uint32_t newFruitHash) {
    // v1.6.1 @0x001177e0: iterates m_ByType[ACHIEVEMENT_TYPE_SPECIFIC_ORDER] (pM_ByType_9),
    // NOT m_All -- port previously walked every achievement type (m_All), evaluating the
    // SpecificOrder gate against unrelated types. No mode gate here; mode-gating is implicit
    // at load time (only achievements for the current mode are inserted via LoadAchievementInfo).
    // Every entry with a m_SpecificOrder pointer is checked via Check(newFruitHash).
    int unlocked = 0;
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_SPECIFIC_ORDER];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
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
// UnlockComboAchievement
// ASM-spec v1.6.1 AchievementManager::UnlockComboAchievement @ 0x001175e8 (thunk @ 0x0010ce60)
// ---------------------------------------------------------------------------

int AchievementManager::UnlockComboAchievement(int comboLen, int* fruitArr) {
    // v1.6.1 @0x001175e8: iterates m_ByType[ACHIEVEMENT_TYPE_COMBO] (pM_ByType_7), NOT m_All --
    // confirmed by LoadAchievementInfo @0x00118198 (unlockTypeHashes[7]==StringHash("COMBO") into
    // pM_ByType_0+7) and QueAchievement @0x0011750c (erases from pM_ByType_0+info->m_TypeIndex).
    // Port previously walked every achievement type (m_All), evaluating combo-threshold/mode/
    // SpecificOrder/unsullied gates against unrelated types (score/specific/etc.) -> false unlocks.
    // threshold <= comboLen + mode gate.
    // SpecificOrder gate (COMBO semantics): count fruitArr entries whose hash
    //   matches GetFirstFruitTypeHash(); require count >= threshold.
    //   (Different from UnlockSpecificOrderAchievement which tracks a full sequence.)
    // RequiresUnsullied gate: pTimeCtrl != NULL AND comboLen > 2 AND
    //   pTimeCtrl->m_TimeRemaining <= 0. See gate body below.
    int unlocked = 0;
    std::map<uint32_t, AchievementInfo*>& bucket = m_ByType[ACHIEVEMENT_TYPE_COMBO];
    for (std::map<uint32_t, AchievementInfo*>::iterator it = bucket.begin(); it != bucket.end(); ) {
        AchievementInfo* info = it->second;
        if (!info) { ++it; continue; }

        if (info->m_Total > comboLen || !ModeBitmaskAllows(info->m_ModeBitmask)) {
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
            if (matches < info->m_Total) {
                ++it;
                continue;
            }
        }

        // RequiresUnsullied gate (mid-body of v1.6.1 UnlockComboAchievement @0x001175e8):
        //   Despite the XML attribute name, this gates on the TIMED-MODE COUNTDOWN
        //   having expired, NOT on Game::m_bUnsullied. Binary reads
        //   g_GameData->pTimeCtrl->m_TimeRemaining. If pTimeCtrl is NULL (Classic mode,
        //   no timer HUD), reject. If comboLen <= 2, reject. If countdown still
        //   running, reject. Only when the Arcade/Zen-timed countdown has hit 0.0f
        //   does the achievement become eligible.
        // ASM-verified: 2026-05-18 v1.6.1 AchievementManager::QueAchievement @ 0x0011750c (re-analyst)
        // (IsGameOver gate is a mid-function range within QueAchievement; exact offset unverified -- asm-inspector to pin)
        if (info->m_IsGameOver) {
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

// ASM-spec v1.6.1 ParseAchievements @0x00154830
// Loads the per-<ach> achievement records out of a <que> (pending) or <unlocked>
// (confirmed) container into the FruitSaveData maps. Invoked by ParseSaveFile:
// que -> pending=true (also reads the "time" countdown attr), unlocked -> pending=false.
// Each <ach name="..."> keys its record by StringHash(name).
// (The online-publish side stays defunct; the local-save records are live and
// drive IsAchievementUnlocked / AddToQue / Update, so this parse is required.)
void ParseAchievements(TiXmlElement* root, FruitSaveData* save, bool pending) {
    if (!root || !save) return;
    for (TiXmlElement e = root->FirstChildElement("ach"); e;
         e = e.NextSiblingElement("ach")) {
        const char* name = e.Attribute("name");
        if (!name || !*name) continue;
        AchievementItem item;
        strncpy(item.m_Name, name, sizeof(item.m_Name) - 1);
        item.m_Name[sizeof(item.m_Name) - 1] = '\0';
        uint32_t hash = StringHash(name);
        if (pending) {
            e.QueryFloatAttribute("time", &item.m_Timer);
            save->m_PendingUnlocks[hash] = item;
        } else {
            save->m_UnlockedAchievements[hash] = item;
        }
    }
}
