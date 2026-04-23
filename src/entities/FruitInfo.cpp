//
// FRUIT_INFO loader — parses Data/xml/fruitlist.xml
// Matches Fruit::LoadInfo (0x17987c, 509 lines)
// See docs/structs/data.md for full struct layout
//
// Analysed: 2026-04-10T13:00

#include "FruitInfo.h"
#include "util/StringHash.h"
#include "Game.h"
#include "asset/TextureManager.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static FruitInfo s_FruitInfos[FRUIT_INFO_MAX];
static int s_FruitInfoCount = 0;

// Parsed from <bomb size="..." collision="..."/>. Binary stores these on
// g_pFruitInfo+0x88/+0x8C. Port keeps them as module statics.
static float s_BombSize      = 55.0f;  // default from original fruitlist.xml
static float s_BombCollision = 25.0f;  // default from original fruitlist.xml

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

// Stub: LoadFruitModels — called at end of LoadInfo
// Original loads 3D mesh for each fruit type using MeshManager
static void LoadFruitModels() {
    // TODO: for each fruit, load models/Fruit/<name>_single.mmd via MeshManager
    // Currently Fruit::Init loads meshes per-instance instead
}

// --- Fruit::LoadInfo implementation (matches 0x17987c, 509 lines) ---
// Called from Fruit::LoadInfo() in Fruit.cpp

// Global shadow texture (loaded on fast hardware only)
static SmartPtr<Mortar::Texture> g_FruitShadowTex;

// --- Fruit::LoadInfo implementation (matches 0x17987c, 509 lines) ---

void FruitInfo_Load(const char* xmlPath)
{
    // --- Step 0: fruit_shadow.tex (before XML, fast hardware only) ---
    // Original: if (IsFastHardware()) LoadLocalisedTexture("fruit_shadow.tex") → global+0xC0
    // Port specific: always load (IsFastHardware not meaningful for port)
    if (!g_FruitShadowTex.IsValid()) {
        g_FruitShadowTex = Mortar::TextureManager::LoadLocalisedTexture("fruit_shadow.tex");
    }

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.LoadFile(xmlPath);
    if (err != tinyxml2::XML_SUCCESS)
    {
        fprintf(stderr, "Fruit::LoadInfo: failed to open '%s' (error %d)\n", xmlPath, err);
        return;
    }
    tinyxml2::XMLElement* root = doc.FirstChildElement("fruitInfoFile");
    if (!root)
    {
        fprintf(stderr, "Fruit::LoadInfo: no <fruitInfoFile> root element\n");
        return;
    }

    // --- Parse <critical> element (global game settings) ---
    // Original: "colour" CSV → global bytes, 5× QueryIntAttribute, 3× QueryFloatAttribute
    // These go to game globals (not per-fruit FRUIT_INFO).
    tinyxml2::XMLElement* critElem = root->FirstChildElement("critical");
    if (critElem)
    {
        // TODO: parse "colour" CSV → global border colour bytes
        // TODO: QueryIntAttribute: "chance", "chance_inc", "score", "splats", "spread"
        //   → these go to g_GameData globals (not yet defined in port)
        // TODO: QueryFloatAttribute: "collision", "scale", "disappear_speed"
        //   → these go to g_GameData globals
    }

    // --- Parse <bomb> element (global bomb settings) ---
    // Original: reads 2 float attrs → globals at g_pFruitInfo+0x88/+0x8C
    tinyxml2::XMLElement* bombElem = root->FirstChildElement("bomb");
    if (bombElem)
    {
        bombElem->QueryFloatAttribute("size",      &s_BombSize);
        bombElem->QueryFloatAttribute("collision", &s_BombCollision);
    }
    // --- Count <FruitInfo> elements ---
    s_FruitInfoCount = 0;
    for (tinyxml2::XMLElement* e = root->FirstChildElement("FruitInfo");
         e; e = e->NextSiblingElement("FruitInfo"))
    {
        s_FruitInfoCount++;
    }
    if (s_FruitInfoCount > FRUIT_INFO_MAX)
        s_FruitInfoCount = FRUIT_INFO_MAX;
    // --- Parse each <FruitInfo> element ---
    int idx = 0;
    for (tinyxml2::XMLElement* elem = root->FirstChildElement("FruitInfo");
         elem && idx < s_FruitInfoCount;
         elem = elem->NextSiblingElement("FruitInfo"), idx++)
    {
        FruitInfo& fi = s_FruitInfos[idx];
        memset(&fi, 0, sizeof(fi));

        // --- "name" attr (required) → +0x000 ---
        const char* name = elem->Attribute("name");
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

        // --- Textures: hud_%s.tex → +0x300, zen_%s.tex → +0x304 ---
        {
            char texName[64];
            snprintf(texName, 64, "hud_%s.tex", name);
            fi.m_HudTexture = Mortar::TextureManager::LoadLocalisedTexture(texName);

            snprintf(texName, 64, "zen_%s.tex", name);
            fi.m_ZenTexture = Mortar::TextureManager::LoadLocalisedTexture(texName);
        }

        // --- String attrs with fallbacks ---
        // "plural" → +0x100 (fallback: "%ss")
        const char* plural = elem->Attribute("plural");
        if (plural && *plural)
            strncpy(fi.m_Plural, plural, 63);
        else
            snprintf(fi.m_Plural, 64, "%ss", fi.m_Name);

        // "factTexture" → +0x278 (optional; empty if missing)
        const char* factTex = elem->Attribute("factTexture");
        if (factTex && *factTex)
            strncpy(fi.m_FactTexture, factTex, 63);

        // "modelName" → +0x200 (fallback: m_Name)
        const char* modelName = elem->Attribute("modelName");
        strncpy(fi.m_ModelName, (modelName && *modelName) ? modelName : fi.m_Name, 63);

        // "singular" → +0x0C0 (fallback: m_Name)
        const char* singular = elem->Attribute("singular");
        strncpy(fi.m_Singular, (singular && *singular) ? singular : fi.m_Name, 63);

        // "pluralEnglish" → +0x080 (fallback: sprintf("%ss", m_Name))
        const char* pluralEng = elem->Attribute("pluralEnglish");
        if (pluralEng && *pluralEng)
            strncpy(fi.m_PluralEnglish, pluralEng, 63);
        else
            snprintf(fi.m_PluralEnglish, 64, "%ss", fi.m_Name);

        // "singularEnglish" → +0x040 (fallback: m_Name)
        const char* singularEng = elem->Attribute("singularEnglish");
        strncpy(fi.m_SingularEnglish, (singularEng && *singularEng) ? singularEng : fi.m_Name, 63);

        // --- Colour: "colour" → +0x240 (R,G,B,A → BGRA bytes) ---
        const char* colourStr = elem->Attribute("colour");
        if (colourStr && *colourStr)
        {
            int rgba[4] = {0, 0, 0, 0};
            ParseCSV(colourStr, rgba, 4);
            fi.m_FruitColour[0] = (uint8_t)rgba[2]; // B
            fi.m_FruitColour[1] = (uint8_t)rgba[1]; // G
            fi.m_FruitColour[2] = (uint8_t)rgba[0]; // R
            fi.m_FruitColour[3] = (uint8_t)rgba[3]; // A
        }

        // --- factColour: "factColour" → +0x2F8 (R,G,B → BGRA with A=0xFF) ---
        const char* factColStr = elem->Attribute("factColour");
        if (factColStr && *factColStr)
        {
            int rgb[3] = {0, 0, 0};
            ParseCSV(factColStr, rgb, 3);
            fi.m_FactColour[0] = (uint8_t)rgb[2]; // B
            fi.m_FactColour[1] = (uint8_t)rgb[1]; // G
            fi.m_FactColour[2] = (uint8_t)rgb[0]; // R
            fi.m_FactColour[3] = 0xFF; // A = 0xFF always
        }

        // --- Float attrs with defaults ---
        fi.m_CollisionScale = 25.0f; // default 0x41C80000
        fi.m_Scale = 1.0f; // default 0x3F800000
        fi.m_HitInfluence = 0.75f; // default 0x3F400000
        elem->QueryFloatAttribute("collision", &fi.m_CollisionScale);
        elem->QueryFloatAttribute("scale", &fi.m_Scale);
        elem->QueryFloatAttribute("hitInfluence", &fi.m_HitInfluence);

        // --- Int attrs ---
        fi.m_Chance = 0;
        fi.m_Score = 0;
        fi.m_CoinsMin = 0;
        fi.m_CoinsMax = 0;
        elem->QueryIntAttribute("chance", &fi.m_Chance);
        elem->QueryIntAttribute("score", &fi.m_Score);
        fi.m_CoinsMax = fi.m_CoinsMin; // original: copy before override
        elem->QueryIntAttribute("coinsMin", &fi.m_CoinsMin);
        elem->QueryIntAttribute("coinsMax", &fi.m_CoinsMax);

        // --- Bool attrs (strcmp "true") ---
        const char* hasSplat = elem->Attribute("hasSplatSeeds");
        fi.m_bHasSplatSeeds = (hasSplat && strcmp(hasSplat, "true") == 0) ? 1 : 0;

        const char* onSide = elem->Attribute("onSide");
        if (!onSide) onSide = elem->Attribute("onside");
        fi.m_bOnSide = (onSide && strcmp(onSide, "true") == 0) ? 1 : 0;

        const char* noCrit = elem->Attribute("noCritical");
        fi.m_bNoCritical = (noCrit && strcmp(noCrit, "true") != 0) ? 1 : 0; // inverted: "true" = has critical
        // Also cleared if colour alpha == 0 or score >= some threshold
        if (fi.m_FruitColour[3] == 0) fi.m_bNoCritical = 0;

        // "onlySprinkle" → +0x319 (QueryIntAttribute == 1)
        int sprinkle = 0;
        elem->QueryIntAttribute("onlySprinkle", &sprinkle);
        fi.m_bSpecial = (sprinkle == 1) ? 1 : 0;

        // --- <fact> child elements → +0x270/+0x274 ---
        fi.m_FactCount = 0;
        for (tinyxml2::XMLElement* f = elem->FirstChildElement("fact"); f;
             f = f->NextSiblingElement("fact"))
        {
            fi.m_FactCount++;
        }
        if (fi.m_FactCount > 0)
        {
            fi.m_pFacts = (char**)malloc(fi.m_FactCount * sizeof(char*));
            int i = 0;
            for (tinyxml2::XMLElement* f = elem->FirstChildElement("fact"); f && i < fi.m_FactCount;
                 f = f->NextSiblingElement("fact"), i++)
            {
                fi.m_pFacts[i] = (char*)calloc(256, 1);
                const char* text = f->GetText();
                if (text) strncpy(fi.m_pFacts[i], text, 255);
            }
        }

        // --- <impact_sound> child elements → +0x31C/+0x320 ---
        fi.m_SoundCount = 0;
        for (tinyxml2::XMLElement* s = elem->FirstChildElement("impact_sound"); s;
             s = s->NextSiblingElement("impact_sound"))
        {
            fi.m_SoundCount++;
        }
        if (fi.m_SoundCount > 0)
        {
            fi.m_pSounds = (ImpactSound*)calloc(fi.m_SoundCount, sizeof(ImpactSound));
            int i = 0;
            int cumWeight = 0;
            for (tinyxml2::XMLElement* s = elem->FirstChildElement("impact_sound");
                 s && i < fi.m_SoundCount; s = s->NextSiblingElement("impact_sound"), i++)
            {
                s->QueryIntAttribute("chance", &fi.m_pSounds[i].m_Weight);
                cumWeight += fi.m_pSounds[i].m_Weight;
                fi.m_pSounds[i].m_CumulativeWeight = cumWeight;
                const char* sndName = s->GetText();
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
            // "Impact-%c%s" — uppercase first char + rest of name
            char upper = fi.m_Name[0];
            if (upper >= 'a' && upper <= 'z') upper -= 0x20;
            sprintf(fi.m_pSounds[0].m_SoundName, "Impact-%c%s", upper, fi.m_Name + 1);
        }

        // --- <power> child elements → +0x32C ---
        int powerCount = 0;
        for (tinyxml2::XMLElement* p = elem->FirstChildElement("power"); p;
             p = p->NextSiblingElement("power"))
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
            for (tinyxml2::XMLElement* p = elem->FirstChildElement("power");
                 p && i < powerCount; p = p->NextSiblingElement("power"), i++)
            {
                const char* pname = p->Attribute("name");
                if (pname && *pname)
                    fi.m_pPowers->m_pArray[i].m_PowerHash = StringHash(pname);
                p->QueryIntAttribute("chance", (int*)&fi.m_pPowers->m_pArray[i].m_Weight);
                cumWeight += fi.m_pPowers->m_pArray[i].m_Weight;
                fi.m_pPowers->m_pArray[i].m_CumulativeWeight = cumWeight;
            }
        }
    }
    printf("Fruit::LoadInfo: loaded %d fruit types from '%s'\n", s_FruitInfoCount, xmlPath);

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
