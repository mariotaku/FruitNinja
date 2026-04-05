//
// FRUIT_INFO loader — parses Data/xml/fruitlist.xml
// Matches Fruit::LoadInfo (0x17987c, 519 lines)
//
// Original uses TiXmlDocument (TinyXML 1.x); port uses tinyxml2.
//

#include "FruitInfo.h"
#include "util/StringHash.h"
#include <tinyxml2.h>
#include <cstdio>
#include <cstring>

static FruitInfo s_FruitInfos[FRUIT_INFO_MAX];
static int s_FruitInfoCount = 0;

void FruitInfo_Load(const char* xmlPath) {
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError err = doc.LoadFile(xmlPath);
    if (err != tinyxml2::XML_SUCCESS) {
        fprintf(stderr, "FruitInfo_Load: failed to open '%s' (error %d)\n", xmlPath, err);
        return;
    }

    tinyxml2::XMLElement* root = doc.FirstChildElement("fruitInfoFile");
    if (!root) {
        fprintf(stderr, "FruitInfo_Load: no <fruitInfoFile> root element\n");
        return;
    }

    // Parse <FruitInfo> elements (matches original: iterates "FruitInfo" children)
    s_FruitInfoCount = 0;
    for (tinyxml2::XMLElement* elem = root->FirstChildElement("FruitInfo");
         elem && s_FruitInfoCount < FRUIT_INFO_MAX;
         elem = elem->NextSiblingElement("FruitInfo"))
    {
        FruitInfo& info = s_FruitInfos[s_FruitInfoCount];
        memset(&info, 0, sizeof(info));

        // Name
        const char* name = elem->Attribute("name");
        if (name) {
            strncpy(info.name, name, sizeof(info.name) - 1);
            info.nameHash = StringHash(name);
        }

        // Scale (visual scale input, multiplied by 0.01 in SetFruitType)
        info.scale = elem->FloatAttribute("scale", 60.0f);

        // Collision
        info.collision = elem->FloatAttribute("collision", 5.0f);

        // Chance
        info.chance = elem->IntAttribute("chance", 100);

        s_FruitInfoCount++;
    }

    printf("FruitInfo_Load: loaded %d fruit types from '%s'\n", s_FruitInfoCount, xmlPath);
}

const FruitInfo* FruitInfo_Get(int type) {
    if (type < 0 || type >= s_FruitInfoCount)
        return NULL;
    return &s_FruitInfos[type];
}

int FruitInfo_GetCount() {
    return s_FruitInfoCount;
}
