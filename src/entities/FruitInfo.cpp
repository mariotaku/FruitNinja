//
// FRUIT_INFO loader — parses Data/xml/fruitlist.xml
// Matches v1.6.1 Fruit::LoadInfo @0x001e1084 (509 lines)

#include "FruitInfo.h"
#include "util/StringHash.h"
#include "Game.h"
#include "asset/TextureManager.h"
#include "game/PowerUpManager.h"
#include "game/WaveManager.h"
#include "math/Random.h"
#include "debug/Logger.h"
#include "xml/TiXml.h"
#include <cstdlib>
#include <cstring>
#include <string>

static FruitInfo s_FruitInfos[FRUIT_INFO_MAX];
static int s_FruitInfoCount = 0;

// Parsed from <bomb size="..." collision="..."/>. Binary stores these on
// g_pFruitInfo+0x88/+0x8C. Port keeps them as module statics.
static float s_BombSize      = 55.0f;  // default from original fruitlist.xml
static float s_BombCollision = 25.0f;  // default from original fruitlist.xml

// Parsed from <critical .../>. Binary: game globals @0x002d8d48 etc (NOT
// per-FruitInfo). ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084.
// Defaults match the shipped fruitlist.xml <critical> block so behaviour is
// sane even before the XML loads.
static int   s_CriticalNewLifeAt      = 100;  // NEW_LIFE_AT @0x002d8d60 .data init; XML omits the attr
static int   s_CriticalScore          = 10;
static int   s_CriticalChance         = 50;
static int   s_CriticalChanceStartInc = 30;
static int   s_CriticalSplats         = 15;
static float s_CriticalSplatScale     = 1.25f;
static float s_CriticalSplatSpread    = 1.25f;
static float s_CriticalDisappearSpeed = 1.0f;
static Colour s_CriticalColour(0, 140, 245, 170);

// --- Helpers ---

// Parse comma-separated ints "R,G,B,A" into 4 bytes (up to maxCount)
static int ParseCSV(const char* str, int* out, int maxCount)
{
    int count = 0;
    while (str && *str && count < maxCount)
    {
        out[count++] = atoi(str);
        while (*str && *str != ',') str++;
        if (*str == ',') str++;
    }
    return count;
}

// No-op: LoadFruitModels — called at end of LoadInfo
// Real implementation is Fruit::LoadFruitModels() (v1.6.1 0x001e08ec),
// called from GameInitialise step 25. Guard inside the real function
// prevents double execution.
// v1.6.1 binary also calls LoadFruitModels from within LoadInfo; when
// both the binary and GameInitialise call it, the guard handles it.
static void LoadFruitModels() {
}

// --- Fruit::LoadInfo implementation (v1.6.1 @0x001e1084, 509 lines) ---
// Called from Fruit::LoadInfo() in Fruit.cpp

// Global shadow texture (loaded on fast hardware only)
static Mortar::SmartPtr<Mortar::Texture> g_FruitShadowTex;

// --- Fruit::LoadInfo implementation (v1.6.1 @0x001e1084, 509 lines) ---

void FruitInfo_Load(const char* xmlPath)
{
    // --- Step 0: fruit_shadow.tex (before XML, fast hardware only) ---
    // Original: if (IsFastHardware()) LoadLocalisedTexture("fruit_shadow.tex") -> global+0xC0
    // Port specific: always load (IsFastHardware not meaningful for port)
    if (!g_FruitShadowTex.IsValid()) {
        g_FruitShadowTex = Mortar::TextureManager::LoadLocalisedTexture("fruit_shadow.tex");
    }

    TiXmlDocument doc;
    if (!doc.LoadFile(xmlPath)) {
        return;
    }
    // ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084:
    // doc.FirstChildElement("fruitInfoFile") is how binary walks to root.
    TiXmlElement root = doc.FirstChildElement("fruitInfoFile");
    if (!root)
    {
        LOG_ERROR("FRUITINFO/LoadInfo", "no <fruitInfoFile> root element");
        return;
    }

    // --- Parse <critical> element (global game settings) ---
    // ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084 (critical block @0x1e128c-0x1e12a4,
    // confirmed by get_xrefs_to on CRITICAL_SCORE @0x002d8d48 / CRITICAL_CHANCE @0x002d8d4c).
    // 5x QueryIntAttribute + 3x QueryFloatAttribute (game globals, NOT per-FruitInfo),
    // plus a "colour" CSV (R,G,B,A ints, NOT byte-swapped -- direct field order).
    TiXmlElement critElem = root.FirstChildElement("critical");
    if (critElem)
    {
        critElem.QueryIntAttribute("new_life_at",      &s_CriticalNewLifeAt);
        critElem.QueryIntAttribute("score",             &s_CriticalScore);
        critElem.QueryIntAttribute("chance",            &s_CriticalChance);
        critElem.QueryIntAttribute("chance_inc",        &s_CriticalChanceStartInc);
        critElem.QueryIntAttribute("splats",            &s_CriticalSplats);
        critElem.QueryFloatAttribute("scale",           &s_CriticalSplatScale);
        critElem.QueryFloatAttribute("spread",          &s_CriticalSplatSpread);
        // CRITICAL_DISAPPEAR_SPEED @0x002d8d44 is parsed here and then never
        // read: zero READ xrefs anywhere in v1.6.1 .text. Kept for load-path
        // fidelity -- do not invent a consumer.
        critElem.QueryFloatAttribute("disappear_speed", &s_CriticalDisappearSpeed);

        const char* colourStr = critElem.Attribute("colour");
        if (colourStr && *colourStr)
        {
            int rgba[4] = {0, 0, 0, 0};
            ParseCSV(colourStr, rgba, 4);
            s_CriticalColour = Colour((uint8_t)rgba[0], (uint8_t)rgba[1],
                                       (uint8_t)rgba[2], (uint8_t)rgba[3]);
        }
    }

    // --- Parse <bomb> element (global bomb settings) ---
    // Original: reads 2 float attrs -> globals at g_pFruitInfo+0x88/+0x8C
    TiXmlElement bombElem = root.FirstChildElement("bomb");
    if (bombElem)
    {
        bombElem.QueryFloatAttribute("size",      &s_BombSize);
        bombElem.QueryFloatAttribute("collision", &s_BombCollision);
    }
    // --- Count <FruitInfo> elements ---
    s_FruitInfoCount = 0;
    for (TiXmlElement e = root.FirstChildElement("FruitInfo");
         e; e = e.NextSiblingElement("FruitInfo"))
    {
        s_FruitInfoCount++;
    }
    if (s_FruitInfoCount > FRUIT_INFO_MAX)
        s_FruitInfoCount = FRUIT_INFO_MAX;
    // --- Parse each <FruitInfo> element ---
    int idx = 0;
    for (TiXmlElement elem = root.FirstChildElement("FruitInfo");
         elem && idx < s_FruitInfoCount;
         elem = elem.NextSiblingElement("FruitInfo"), idx++)
    {
        FruitInfo& fi = s_FruitInfos[idx];
        // Port note: FruitInfo is not trivially-copyable (m_HudTexture/m_ZenTexture are
        // Mortar::SmartPtr<Texture>, refcounted), so a raw memset() over a live element
        // would corrupt/skip refcount bookkeeping if this entry ever held a texture.
        // Value-initialising a temporary zeroes the POD fields exactly like the binary's
        // FRUIT_INFO ctor memset (v1.6.1 FRUIT_INFO::FRUIT_INFO @0x001e3d44), and
        // default-constructs the SmartPtrs to null; assigning it over fi runs SmartPtr::operator= (AddRef-null/Release-old),
        // which is a safe no-op on the first (only) FruitInfo_Load call and would also
        // correctly release any previously-held texture were this ever called again.
        fi = FruitInfo();
        // Re-apply FRUIT_INFO ctor defaults (v1.6.1 FRUIT_INFO::FRUIT_INFO
        // @0x001e3d44; dtor @0x001e3c54). The XML parser uses TinyXML QueryIntAttribute / QueryFloatAttribute which
        // leave the destination unchanged when the attr is missing -- so
        // any field with a non-zero default must be initialised here, NOT
        // in the per-attr block below. Apple, banana, etc. omit `score=`,
        // and the binary defaults to 1 via this ctor write.
        fi.m_Score        = 1;     // FRUIT_INFO::FRUIT_INFO sets m_BaseScore=1
        fi.m_Scale        = 1.0f;  // ctor default 0x3F800000 (overridden via "scale" attr)
        fi.m_HitInfluence = 0.75f; // ctor default 0x3F400000
        fi.m_CollisionScale = 25.0f; // ctor default 0x41C80000
        fi.m_bScorable    = 1;     // ctor default; cleared if noCritical="true" or alpha=0

        // --- "name" attr (required) -> +0x000 ---
        const char* name = elem.Attribute("name");
        if (!name || !*name) continue;
        strncpy(fi.m_Name, name, 63);

        // --- Name hashes (+0x250, +0x254) ---
        fi.m_NameHash = StringHash(fi.m_Name);

        // Uppercase first char for second hash
        char upperName[64];
        strncpy(upperName, fi.m_Name, 63);
        upperName[63] = 0;
        if (upperName[0] >= 'a' && upperName[0] <= 'z')
            upperName[0] -= 0x20;
        fi.m_NameHashUpper = StringHash(upperName);
        // Restore lowercase
        if (fi.m_Name[0] >= 'A' && fi.m_Name[0] <= 'Z')
            fi.m_Name[0] += 0x20; // original does this in-place

        // --- Pattern hashes (+0x258..+0x268) ---
        {
            char buf[64];
            snprintf(buf, 64, "%s_trail", fi.m_Name);
            fi.m_TrailHash = StringHash(buf); // +0x258

            snprintf(buf, 64, "%s_sliced", fi.m_Name);
            fi.m_SlicedHash = StringHash(buf); // +0x25C

            snprintf(fi.m_TotalStatKey, 64, "%s_total", fi.m_Name);
            fi.m_TotalStatHash = StringHash(fi.m_TotalStatKey); // +0x260

            snprintf(fi.m_PointTotalKey, 64, "%s_point_total", fi.m_Name);
            fi.m_PointTotalHash = StringHash(fi.m_PointTotalKey); // +0x264

            snprintf(fi.m_DropsKey, 64, "%s_drops", fi.m_Name);
            fi.m_DropsHash = StringHash(fi.m_DropsKey); // +0x268
        }

        // --- Textures: hud_%s.tex -> +0x300, zen_%s.tex -> +0x304 ---
#if defined(FN_BLOCK_PRELOAD)
        // Deferred to BlockLoader::PreloadBlock(RES_BLOCK_INGAME) -- task #59
        // (see FruitInfo_LoadHudTextures() below)
#else
        {
            char texName[64];
            snprintf(texName, 64, "hud_%s.tex", name);
            fi.m_HudTexture = Mortar::TextureManager::LoadLocalisedTexture(texName);

            snprintf(texName, 64, "zen_%s.tex", name);
            fi.m_ZenTexture = Mortar::TextureManager::LoadLocalisedTexture(texName);
        }
#endif

        // ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084: colour/factColour read from a <colours> CHILD of <FruitInfo>; plural/singular/pluralEnglish/singularEnglish from a <titles> CHILD (NOT the parent). alpha==0 (4th CSV) clears m_bScorable + suppresses the SplatEntity::MakeSplat @0x001eb910 background juice splat.

        // --- String attrs with fallbacks ---
        // "plural", "singular", "pluralEnglish", "singularEnglish" from <titles> CHILD.
        {
            TiXmlElement titlesEl = elem.FirstChildElement("titles");
            if (titlesEl)
            {
                const char* plural = titlesEl.Attribute("plural");
                if (plural && *plural)
                    strncpy(fi.m_Plural, plural, 63);
                else
                    snprintf(fi.m_Plural, 64, "%ss", fi.m_Name);

                const char* singular = titlesEl.Attribute("singular");
                strncpy(fi.m_Singular, (singular && *singular) ? singular : fi.m_Name, 63);

                const char* pluralEng = titlesEl.Attribute("pluralEnglish");
                if (pluralEng && *pluralEng)
                    strncpy(fi.m_PluralEnglish, pluralEng, 63);
                else
                    snprintf(fi.m_PluralEnglish, 64, "%ss", fi.m_Name);

                const char* singularEng = titlesEl.Attribute("singularEnglish");
                strncpy(fi.m_SingularEnglish, (singularEng && *singularEng) ? singularEng : fi.m_Name, 63);
            }
            else
            {
                snprintf(fi.m_Plural, 64, "%ss", fi.m_Name);
                strncpy(fi.m_Singular, fi.m_Name, 63);
                snprintf(fi.m_PluralEnglish, 64, "%ss", fi.m_Name);
                strncpy(fi.m_SingularEnglish, fi.m_Name, 63);
            }
        }

        // "factTexture" -> +0x278 (optional; empty if missing)
        const char* factTex = elem.Attribute("factTexture");
        if (factTex && *factTex)
            strncpy(fi.m_FactTexture, factTex, 63);

        // "modelName" -> +0x200 (fallback: m_Name)
        const char* modelName = elem.Attribute("modelName");
        strncpy(fi.m_ModelName, (modelName && *modelName) ? modelName : fi.m_Name, 63);

        // --- Colour: "colour" -> +0x240 (R,G,B,A -> BGRA bytes); from <colours> CHILD ---
        // --- factColour: "factColour" -> +0x2F8 (R,G,B -> BGRA with A=0xFF); from <colours> CHILD ---
        {
            TiXmlElement coloursEl = elem.FirstChildElement("colours");
            if (coloursEl)
            {
                const char* colourStr = coloursEl.Attribute("colour");
                if (colourStr && *colourStr)
                {
                    int rgba[4] = {0, 0, 0, 0};
                    ParseCSV(colourStr, rgba, 4);
                    fi.m_FruitColour[0] = (uint8_t)rgba[2]; // B
                    fi.m_FruitColour[1] = (uint8_t)rgba[1]; // G
                    fi.m_FruitColour[2] = (uint8_t)rgba[0]; // R
                    fi.m_FruitColour[3] = (uint8_t)rgba[3]; // A
                }

                const char* factColStr = coloursEl.Attribute("factColour");
                if (factColStr && *factColStr)
                {
                    int rgb[3] = {0, 0, 0};
                    ParseCSV(factColStr, rgb, 3);
                    fi.m_FactColour[0] = (uint8_t)rgb[2]; // B
                    fi.m_FactColour[1] = (uint8_t)rgb[1]; // G
                    fi.m_FactColour[2] = (uint8_t)rgb[0]; // R
                    fi.m_FactColour[3] = 0xFF; // A = 0xFF always
                }
            }
        }

        // ASM-verified: 2026-05-03 v1.6.1 Fruit::LoadInfo @ 0x001e1084 (asm-inspector / re-analyst)
        // The three QueryFloatAttribute calls below are @0x001e19c8 (collision),
        // 0x001e19dc (scale), 0x001e19f0 (hitInfluence); the int-attr run starts
        // at 0x001e1a04.
        // --- Float attrs with defaults ---
        // Floats (defaults already applied at top of loop via ctor mirror).
        elem.QueryFloatAttribute("collision", &fi.m_CollisionScale);
        elem.QueryFloatAttribute("scale", &fi.m_Scale);
        elem.QueryFloatAttribute("hitInfluence", &fi.m_HitInfluence);

        // --- Int attrs (defaults applied at top of loop via ctor mirror) ---
        elem.QueryIntAttribute("chance", &fi.m_Chance);
        elem.QueryIntAttribute("score", &fi.m_Score);
        fi.m_CoinsMax = fi.m_CoinsMin; // original: copy before override
        elem.QueryIntAttribute("coinsMin", &fi.m_CoinsMin);
        elem.QueryIntAttribute("coinsMax", &fi.m_CoinsMax);

        // --- Bool attrs (strcmp "true") ---
        const char* hasSplat = elem.Attribute("hasSplatSeeds");
        fi.m_bHasSplatSeeds = (hasSplat && strcmp(hasSplat, "true") == 0) ? 1 : 0;

        const char* onSide = elem.Attribute("onSide");
        if (!onSide) onSide = elem.Attribute("onside");
        fi.m_bOnSide = (onSide && strcmp(onSide, "true") == 0) ? 1 : 0;

        // m_bScorable: 1 = fruit can receive a critical hit, 0 = cannot.
        // XML "noCritical"="true" means NO critical, so m_bScorable=0 when attr is "true".
        // v1.6.1 Fruit::LoadInfo @0x001e1084: store sequence sets field to 1 unless "noCritical"=="true",
        // then clears it if colour alpha == 0.
        const char* noCrit = elem.Attribute("noCritical");
        fi.m_bScorable = (noCrit && strcmp(noCrit, "true") == 0) ? 0 : 1;
        if (fi.m_FruitColour[3] == 0) fi.m_bScorable = 0;

        // "onlySprinkle" -> +0x319 (QueryIntAttribute == 1)
        int sprinkle = 0;
        elem.QueryIntAttribute("onlySprinkle", &sprinkle);
        fi.m_bSpecial = (sprinkle == 1) ? 1 : 0;

        // m_bIsSuperFruit set iff m_Score==0 (super_pomegranate has no score attr).
        // ASM-spec v1.6.1 Fruit::LoadInfo @0x001e1084
        fi.m_bIsSuperFruit = (fi.m_Score == 0) ? 1 : 0;

        // --- <fact> child elements -> +0x270/+0x274 ---
        fi.m_FactCount = 0;
        for (TiXmlElement f = elem.FirstChildElement("fact"); f;
             f = f.NextSiblingElement("fact"))
        {
            fi.m_FactCount++;
        }
        if (fi.m_FactCount > 0)
        {
            fi.m_pFacts = (char**)malloc(fi.m_FactCount * sizeof(char*));
            int i = 0;
            for (TiXmlElement f = elem.FirstChildElement("fact"); f && i < fi.m_FactCount;
                 f = f.NextSiblingElement("fact"), i++)
            {
                fi.m_pFacts[i] = (char*)calloc(256, 1);
                const char* text = f.GetText();
                if (text) strncpy(fi.m_pFacts[i], text, 255);
            }
        }

        // --- <impact_sound> child elements -> +0x31C/+0x320 ---
        fi.m_SoundCount = 0;
        for (TiXmlElement s = elem.FirstChildElement("impact_sound"); s;
             s = s.NextSiblingElement("impact_sound"))
        {
            fi.m_SoundCount++;
        }
        if (fi.m_SoundCount > 0)
        {
            fi.m_pSounds = (ImpactSound*)calloc(fi.m_SoundCount, sizeof(ImpactSound));
            int i = 0;
            int cumWeight = 0;
            for (TiXmlElement s = elem.FirstChildElement("impact_sound");
                 s && i < fi.m_SoundCount; s = s.NextSiblingElement("impact_sound"), i++)
            {
                s.QueryIntAttribute("chance", &fi.m_pSounds[i].m_Weight);
                cumWeight += fi.m_pSounds[i].m_Weight;
                fi.m_pSounds[i].m_CumulativeWeight = cumWeight;
                const char* sndName = s.GetText();
                if (sndName)
                {
                    fi.m_pSounds[i].m_SoundName = (char*)malloc(strlen(sndName) + 1);
                    strcpy(fi.m_pSounds[i].m_SoundName, sndName);
                }
            }
        }
        else
        {
            // Default: 1 sound auto-generated from fruit name ("Impact-%C%s")
            fi.m_SoundCount = 1;
            fi.m_pSounds = (ImpactSound*)calloc(1, sizeof(ImpactSound));
            size_t nameLen = strlen(fi.m_Name);
            fi.m_pSounds[0].m_SoundName = (char*)malloc(nameLen + 9);
            // "Impact-%c%s" -- uppercase first char + rest of name
            char upper = fi.m_Name[0];
            if (upper >= 'a' && upper <= 'z') upper -= 0x20;
            sprintf(fi.m_pSounds[0].m_SoundName, "Impact-%c%s", upper, fi.m_Name + 1);
        }

        // --- <power> child elements -> +0x32C ---
        int powerCount = 0;
        for (TiXmlElement p = elem.FirstChildElement("power"); p;
             p = p.NextSiblingElement("power"))
        {
            powerCount++;
        }
        if (powerCount > 0)
        {
            fi.m_pPowers = (FruitPowers*)calloc(1, sizeof(FruitPowers));
            fi.m_pPowers->m_Count = powerCount;
            fi.m_pPowers->m_pArray = (FruitPower*)calloc(powerCount, sizeof(FruitPower));
            int i = 0;
            uint32_t cumWeight = 0;
            for (TiXmlElement p = elem.FirstChildElement("power");
                 p && i < powerCount; p = p.NextSiblingElement("power"), i++)
            {
                const char* pname = p.Attribute("name");
                if (pname && *pname)
                    fi.m_pPowers->m_pArray[i].m_PowerHash = StringHash(pname);
                p.QueryIntAttribute("chance", (int*)&fi.m_pPowers->m_pArray[i].m_Weight);
                cumWeight += fi.m_pPowers->m_pArray[i].m_Weight;
                fi.m_pPowers->m_pArray[i].m_CumulativeWeight = cumWeight;
            }
        }
    }
    LOG_INFO("FRUITINFO", "Fruit::LoadInfo: loaded %d fruit types from '%s'", s_FruitInfoCount, xmlPath);

    // Original: calls LoadFruitModels() at the very end (loads 3D mesh per fruit type)
    LoadFruitModels();
}

float FruitInfo_GetBombSize()
{
    return s_BombSize;
}

float FruitInfo_GetBombCollision()
{
    return s_BombCollision;
}

int FruitInfo_GetCriticalNewLifeAt()      { return s_CriticalNewLifeAt; }
int FruitInfo_GetCriticalScore()          { return s_CriticalScore; }
int FruitInfo_GetCriticalChance()         { return s_CriticalChance; }
int FruitInfo_GetCriticalChanceStartInc() { return s_CriticalChanceStartInc; }
int FruitInfo_GetCriticalSplats()         { return s_CriticalSplats; }
float FruitInfo_GetCriticalSplatScale()     { return s_CriticalSplatScale; }
float FruitInfo_GetCriticalSplatSpread()    { return s_CriticalSplatSpread; }
float FruitInfo_GetCriticalDisappearSpeed() { return s_CriticalDisappearSpeed; }

const Colour& FruitInfo_GetCriticalColour()
{
    return s_CriticalColour;
}

const FruitInfo* FruitInfo_Get(int type)
{
    if (type < 0 || type >= s_FruitInfoCount)
        return nullptr;
    return &s_FruitInfos[type];
}

int FruitInfo_GetCount()
{
    return s_FruitInfoCount;
}

FruitInfo* FruitInfo_GetArray()
{
    return s_FruitInfos;
}

Mortar::Texture* FruitInfo_GetShadowTex()
{
    return g_FruitShadowTex.IsValid() ? g_FruitShadowTex.Get() : nullptr;
}

#if defined(FN_BLOCK_PRELOAD)
// Boot trim (task #59). See FruitInfo.h contract comment.
// fruit_shadow.tex loads at boot (FruitInfo_Load Step 0, un-deferred --
// task #59 Stage A scope correction); only the per-fruit hud_%s/zen_%s
// icons (gameplay HUD only, never shown at menu) stay deferred here.
void FruitInfo_LoadHudTextures()
{
    for (int i = 0; i < s_FruitInfoCount; ++i) {
        FruitInfo& fi = s_FruitInfos[i];
        if (!fi.m_Name[0]) continue;

        char texName[64];
        snprintf(texName, 64, "hud_%s.tex", fi.m_Name);
        fi.m_HudTexture = Mortar::TextureManager::LoadLocalisedTexture(texName);

        snprintf(texName, 64, "zen_%s.tex", fi.m_Name);
        fi.m_ZenTexture = Mortar::TextureManager::LoadLocalisedTexture(texName);
    }
}
#endif

// FRUIT_POWERS::AnyActivePowers -- binary @ 0x00175714
// Returns true if any power in m_pArray is currently active via PowerUpManager.
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00175714 (re-analyst)
bool FRUIT_POWERS::AnyActivePowers() {
    PowerUpManager* mgr = PowerUpManager::GetInstance();
    if (!mgr) return false;
    for (uint32_t i = 0; i < m_Count; ++i) {
        if (mgr->GetActiveSingle(m_pArray[i].m_PowerHash)) return true;
    }
    return false;
}

// ASM-verified: 2026-05-22 v1.6.1 binary @ 0x0017a7d8 (re-analyst).
// Weighted random pick. m_CumulativeWeight is a running total set at XML load;
// total weight == m_pArray[m_Count-1].m_CumulativeWeight. Roll in [0, total),
// return first entry whose cumulative > roll. Falls back to last entry (cannot
// happen given roll < total, but the binary's loop structure permits it).
uint32_t FRUIT_POWERS::RandomPower() {
    if (m_Count == 0 || m_pArray == nullptr) return 0;
    Math::Random& rng = WaveManager::GetInstance()->GetRandom();
    uint32_t total = m_pArray[m_Count - 1].m_CumulativeWeight;
    uint32_t roll  = rng.Rand32(total);
    FRUIT_POWER* p = m_pArray;
    for (uint32_t i = 0; i < m_Count; ++i) {
        p = &m_pArray[i];
        if (roll < p->m_CumulativeWeight) break;
    }
    return p->m_PowerHash;
}
