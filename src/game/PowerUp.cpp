// Analysed: 2026-05-03T00:00

#include "PowerUp.h"
#include "ScoreModifier.h"
#include "TimeModifier.h"
#include "SlashModifier.h"
#include "WaveModifier.h"
#include "ComboModifier.h"
#include "SpawnModifier.h"
#include "TimeSinkModifier.h"
#include "ExplodyFruitModifier.h"
#include "ScreenEffect.h"
#include "Game.h"
#include "FruitSaveData.h"
#include "hud/MissControl.h"
#include "hud/HUDLayer.h"
#include "util/StringHash.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "render/MatrixManager.h"
#include "math/Matrix44.h"
#include "math/Colour.h"
#include <tinyxml2.h>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <algorithm>
#include "game/GameWork.h"

// ---- helper -----------------------------------------------------------------

static int ParseCSVColour(const char* str, uint8_t* out, int maxCount) {
    int count = 0;
    while (str && *str && count < maxCount) {
        out[count++] = (uint8_t)atoi(str);
        while (*str && *str != ',') str++;
        if (*str == ',') str++;
    }
    return count;
}

// ---- ctor / dtor ------------------------------------------------------------

PowerUp::PowerUp()
    : m_bIsPurchasable(false)
    , m_bIsSpecial(false)
    , m_pPurchaseInfo(nullptr)
    , m_bCloned(0)
    , m_LongestRemaining(0.0f)
    , m_TotalTime(0.0f)
    , m_Colour{255, 255, 255, 255}
    , m_BarRamp(0.0f)
    , m_pScreenEffect(nullptr)
    , m_DeferredPoints(-1)
    , m_BarXPos(0.0f)
    , m_NameHash(0)
{
    memset(m_Name, 0, sizeof(m_Name));
    memset(m_DisplayName, 0, sizeof(m_DisplayName));
    memset(_pad92, 0, sizeof(_pad92));
    memset(_padb8, 0, sizeof(_padb8));
}

// Steps 2: dtor (binary @ 0x001186bc)
PowerUp::~PowerUp() {
    Deactivate(true);
    delete m_pPurchaseInfo;  m_pPurchaseInfo  = nullptr;
    delete m_pScreenEffect;  m_pScreenEffect  = nullptr;
}

// Step 3: Parse (binary @ 0x001194f0)
void PowerUp::Parse(tinyxml2::XMLElement* elem) {
    if (!elem) return;

    // "name" attr — sets m_Name and m_DisplayName (first char uppercased)
    const char* name = elem->Attribute("name");
    if (name) {
        strncpy(m_Name, name, sizeof(m_Name) - 1);
        m_Name[sizeof(m_Name) - 1] = '\0';
        m_NameHash = StringHash(m_Name);

        strncpy(m_DisplayName, m_Name, sizeof(m_DisplayName) - 1);
        m_DisplayName[sizeof(m_DisplayName) - 1] = '\0';
        if (m_DisplayName[0]) m_DisplayName[0] = (char)toupper((unsigned char)m_DisplayName[0]);
    }

    // "single" attr (boolean) — m_bIsPurchasable
    const char* single = elem->Attribute("single");
    if (single) {
        m_bIsPurchasable = (strcmp(single, "true") == 0 || strcmp(single, "1") == 0);
    }

    // "automatic" attr — m_bIsSpecial
    const char* automatic = elem->Attribute("automatic");
    if (automatic) {
        m_bIsSpecial = (strcmp(automatic, "true") == 0 || strcmp(automatic, "1") == 0);
    }

    // "colour" attr — CSV "R,G,B,A"
    const char* colourStr = elem->Attribute("colour");
    if (colourStr) {
        uint8_t rgba[4] = {255, 255, 255, 255};
        ParseCSVColour(colourStr, rgba, 4);
        m_Colour.r = rgba[0];
        m_Colour.g = rgba[1];
        m_Colour.b = rgba[2];
        m_Colour.a = rgba[3];
    }

    // "bar" attr — icon/bar texture name. XML values omit the .tex suffix
    // (e.g. "arcade_banana_meter_freeze"); the shipped files have it. Mirror
    // Bonus.cpp's pattern (snprintf "%s.tex") so loads land on the real file.
    const char* bar = elem->Attribute("bar");
    if (bar && bar[0]) {
        char texPath[128];
        snprintf(texPath, sizeof(texPath), "%s.tex", bar);
        m_Texture1 = Mortar::TextureManager::LoadLocalisedTexture(texPath);
    }

    // "popup" attr — popup texture name. Same .tex-suffix convention.
    const char* popup = elem->Attribute("popup");
    if (popup && popup[0]) {
        char texPath[128];
        snprintf(texPath, sizeof(texPath), "%s.tex", popup);
        m_Texture2 = Mortar::TextureManager::LoadLocalisedTexture(texPath);
    }

    // Iterate child elements
    for (tinyxml2::XMLElement* child = elem->FirstChildElement();
         child; child = child->NextSiblingElement()) {
        const char* tag = child->Name();
        if (!tag) continue;

        if (strcmp(tag, "purchase_info") == 0) {
            // Presence of <purchase_info> forces purchasable regardless of attr.
            m_bIsPurchasable = true;
            m_pPurchaseInfo = new PurchaseInfo();
            m_pPurchaseInfo->Parse(child);
        } else if (strcmp(tag, "effect") == 0) {
            m_pScreenEffect = new ScreenEffect();
            m_pScreenEffect->m_pOwnerPowerUp = this;
            m_pScreenEffect->Parse(child);
        } else {
            // Modifier factory — binary @ 0x00142388.
            // String-compare element name and new the matching modifier subclass,
            // call Parse(child), then AddModifier to attach.
            GameModifier* mod = nullptr;
            if (strcmp(tag, "score_mod") == 0) {
                mod = new ScoreModifier();
            } else if (strcmp(tag, "time_mod") == 0) {
                mod = new TimeModifier();
            } else if (strcmp(tag, "slash_mod") == 0) {
                mod = new SlashModifier();
            } else if (strcmp(tag, "wave_mod") == 0) {
                mod = new WaveModifier();
            } else if (strcmp(tag, "combo_mod") == 0) {
                mod = new ComboModifier();
            } else if (strcmp(tag, "spawn_mod") == 0) {
                mod = new SpawnModifier();
            } else if (strcmp(tag, "timesink_mod") == 0) {
                mod = new TimeSinkModifier();
            } else if (strcmp(tag, "explodyfruit_mod") == 0) {
                mod = new ExplodyFruitModifier();
            }
            if (mod) {
                mod->Parse(child);
                AddModifier(mod);
            }
        }

        // Accumulate m_TotalTime as max over all modifier durations.
        // Each modifier's m_Duration is set by its ParseSpecific; we read it here
        // for the modifiers already in m_ModList (set by ParseSpecific callers).
    }

    // After all mods parsed: m_TotalTime = max(mod->m_Duration) over m_ModList
    m_TotalTime = 0.0f;
    for (std::list<GameModifier*>::iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        if ((*it)->m_Duration > m_TotalTime) m_TotalTime = (*it)->m_Duration;
    }
}

// Step 4: Activate (binary @ 0x00119134)
// ASM-verified: 2026-05-18 binary @ 0x00119134 (re-analyst)
void PowerUp::Activate(bool showPopup, bool isPurchased, const Vec3& pos, float* extraParam) {
    if (showPopup) {
        if (m_Texture2.IsValid()) {
            MissControl* m = MissControl::GetFree();
            if (m) {
                Vec3 posCopy(pos);
                m->MakeDisappear(posCopy, 0, m_Texture2);
                m->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
            }
        }
        if (m_pPurchaseInfo) {
            Game* game = Game::GetInstance();
            if (game) {
                // AddCoins(-cost): coin balance lives in game_work (+0x20), not FruitSaveData.
                // Binary AddCoins @ 0x0010a3bc: m_CoinsBalance += delta (no m_CoinsTotal update for negative).
                game_work.m_CoinsBalance -= m_pPurchaseInfo->m_Cost;
            }
        }
    }
    for (std::list<GameModifier*>::iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        GameModifier* mod = *it;
        if (!mod->m_bApplied) {
            mod->ApplyModifier(isPurchased, extraParam);
        }
    }
    if (m_pScreenEffect && showPopup) {
        m_pScreenEffect->Activate();
    }
}

// Step 5: Deactivate (binary @ 0x00117f18)
int PowerUp::Deactivate(bool removeAll) {
    std::list<GameModifier*>::iterator it = m_ModList.begin();
    while (it != m_ModList.end()) {
        GameModifier* m = *it;
        if (!m->m_bApplied || removeAll) m->RemoveModifier();
        delete m;
        it = m_ModList.erase(it);
    }
    if (m_pScreenEffect) {
        m_pScreenEffect->Deactivate();
        delete m_pScreenEffect;
        m_pScreenEffect = nullptr;
    }
    return 0;
}

// Step 6: Update (binary @ 0x00117f90)
int PowerUp::Update(float dt) {
    int activeCount = 0;

    for (std::list<GameModifier*>::iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        GameModifier* mod = *it;
        int expired = mod->Update(dt);
        if (expired == 0) ++activeCount;
    }

    // Compute m_LongestRemaining = max remaining duration across mods
    float longest = GetLongestMod();
    m_LongestRemaining = longest;

    // Bar ramp: fade-in at 4/sec, fade-out at 12/sec
    // Purchasable special-case: check remaining uses
    bool keepAlive = (activeCount > 0);
    if (m_bIsPurchasable && m_pPurchaseInfo && m_pPurchaseInfo->m_CurrentUses > 0) {
        keepAlive = true;
    }

    if (keepAlive) {
        // Ramp up at 4 units/sec
        m_BarRamp += dt * 4.0f;
        if (m_BarRamp > 1.0f) m_BarRamp = 1.0f;
    } else {
        // Ramp down at 12 units/sec
        m_BarRamp -= dt * 12.0f;
        if (m_BarRamp < 0.0f) m_BarRamp = 0.0f;
    }

    if (m_pScreenEffect) {
        m_pScreenEffect->Update(dt, longest, m_TotalTime);
    }

    // Return 1 when ready to remove (no active mods AND bar fully hidden)
    if (!keepAlive && m_BarRamp <= 0.0f) return 1;
    return 0;
}

// Step 7: Clone (binary @ 0x00119468)
PowerUp* PowerUp::Clone() {
    // Binary passes `this` (pointer) to the PowerUp(PowerUp*) copy ctor.
    PowerUp* clone = new PowerUp(this);
    // Copy ctor already sets m_bCloned=1, m_DeferredPoints=-1, m_pPurchaseInfo=NULL,
    // leaves m_ModList empty. Deep-copy modifiers below.
    for (std::list<GameModifier*>::const_iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        // virtual slot 9 — per-subclass Clone
        // TODO: per-subclass Clone impls when Modifier trio lands
        GameModifier* modClone = (*it)->Clone();
        if (modClone) {
            modClone->m_pDeferInfo = static_cast<void*>(clone);
            clone->m_ModList.push_back(modClone);
        }
    }
    if (m_pPurchaseInfo) {
        clone->m_pPurchaseInfo = new PurchaseInfo();
        *clone->m_pPurchaseInfo = *m_pPurchaseInfo;
    }
    // m_pScreenEffect already deep-copied in the copy ctor.
    return clone;
}

// Step 8: DrawBar (binary @ 0x001191f8)
void PowerUp::DrawBar() {
    if (m_BarRamp <= 0.0f) return;
    if (!m_Texture1.IsValid()) return;

    Mortar::Texture* tex = m_Texture1.Get();
    float texW = (float)tex->m_Width;
    float texH = (float)tex->m_Height;

    // Build world matrix directly:
    //   M[0][0] = texW
    //   M[1][1] = texH
    //   M[3][3] = 1.0
    //   M[3][0] = m_BarXPos
    //   M[3][1] = 160.0 + texH * ((1 - m_BarRamp)*(1 - m_BarRamp) - 0.5) + 1.0
    //   M[3][2] = 0.0
    float oneMinusRamp = 1.0f - m_BarRamp;
    float yPos = 160.0f + texH * (oneMinusRamp * oneMinusRamp - 0.5f) + 1.0f;

    Matrix44 mat;
    // Build from scratch: scale + translate only; column-major flat array.
    // mat.Identity() sets m[0]=m[5]=m[10]=m[15]=1, rest 0.
    memset(mat.m, 0, sizeof(mat.m));
    mat.m[0]  = texW;   // M[0][0] — x scale
    mat.m[5]  = texH;   // M[1][1] — y scale
    mat.m[10] = 1.0f;   // M[2][2]
    mat.m[12] = m_BarXPos; // M[3][0] — tx
    mat.m[13] = yPos;      // M[3][1] — ty
    mat.m[14] = 0.0f;      // M[3][2] — tz
    mat.m[15] = 1.0f;      // M[3][3] — w

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    tex->Set();
    Colour white(255, 255, 255, 255);
    Mortar::Mesh::DrawQuadUnCached(white, 0.0f, 1.0f, 0.0f, 1.0f, NULL);
    tex->UnSet();
}

// Step 9: AddDeferedPoints (binary @ 0x00117a50)
int PowerUp::AddDeferedPoints(int n) {
    if (m_DeferredPoints < 0) m_DeferredPoints = 0;
    m_DeferredPoints += n;
    return 0;
}

// Step 10: LoadTextures (binary @ 0x001183f0)
void PowerUp::LoadTextures() {
    if (m_pScreenEffect) m_pScreenEffect->LoadTextures();
    if (m_pPurchaseInfo)  m_pPurchaseInfo->LoadTextures();
}

// GetLongestMod (binary @ 0x00117aec)
float PowerUp::GetLongestMod() {
    float longest = 0.0f;
    for (std::list<GameModifier*>::const_iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        if ((*it)->m_BonusAccum > longest)
            longest = (*it)->m_BonusAccum;
    }
    return longest;
}

// Release — free modifier list (called before delete in expiry path)
void PowerUp::Release() {
    for (std::list<GameModifier*>::iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        delete *it;
    }
    m_ModList.clear();
}

// AddModifier (binary @ 0x001193d0 callee — de-inlined)
void PowerUp::AddModifier(GameModifier* mod) {
    m_ModList.push_back(mod);
}

// SetTotalTime (binary @ 0x001180d4 — de-inlined)
void PowerUp::SetTotalTime(float t) {
    m_TotalTime = t;
}

// PowerUp::PowerUp(PowerUp*) copy ctor — binary @ 0x00118ed4 (C1) / 0x00119004 (C2)
// ASM-verified: 2026-05-18 binary @ 0x00118ed4 (re-analyst)
PowerUp::PowerUp(PowerUp* src)
    : m_bIsPurchasable(false)
    , m_bIsSpecial(false)
    , m_pPurchaseInfo(NULL)
    , m_bCloned(1)
    , m_LongestRemaining(0.0f)
    , m_TotalTime(0.0f)
    , m_Colour{255, 255, 255, 255}
    , m_BarRamp(0.0f)
    , m_pScreenEffect(NULL)
    , m_DeferredPoints(-1)
    , m_BarXPos(0.0f)
    , m_NameHash(0)
{
    memset(m_Name, 0, sizeof(m_Name));
    memset(m_DisplayName, 0, sizeof(m_DisplayName));
    memset(_pad92, 0, sizeof(_pad92));
    memset(_padb8, 0, sizeof(_padb8));

    m_NameHash = src->m_NameHash;
    strcpy(m_Name,        src->m_Name);
    strcpy(m_DisplayName, src->m_DisplayName);
    m_Colour         = src->m_Colour;
    m_bIsSpecial     = src->m_bIsSpecial;
    m_bIsPurchasable = src->m_bIsPurchasable;
    m_TotalTime      = src->m_TotalTime;
    // m_BarXPos: NOT copied — reset to 0 (binary confirmed @ +0xc8 not in copy path)
    // m_pPurchaseInfo: NOT deep-copied — ActivatePurchase reseeds
    // m_ModList: left EMPTY — Clone() deep-copies separately
    if (src->m_pScreenEffect) {
        m_pScreenEffect = new ScreenEffect();
        *m_pScreenEffect = *src->m_pScreenEffect;
        m_pScreenEffect->m_pOwnerPowerUp = this;
    }
    m_Texture1 = src->m_Texture1;
    m_Texture2 = src->m_Texture2;
}

// @ 0x00117a44 — returns coin cost if purchaseable, else 0
// ASM-verified: 2026-05-18 binary @ 0x00117a44 (re-analyst)
int PowerUp::Purchaseable() const {
    return m_pPurchaseInfo ? m_pPurchaseInfo->m_Cost : 0;
}

// @ 0x00117cdc — walk m_ModList; on type==2 mods, call DeferPoints
// ASM-verified: 2026-05-18 binary @ 0x00117cdc (re-analyst)
void PowerUp::SetDeferedPoints(int points) {
    for (std::list<GameModifier*>::iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        if ((*it)->GetType() == 2) {
            static_cast<ScoreModifier*>(*it)->DeferPoints(points);
        }
    }
}

// @ 0x00118350 — null-guarded UnloadTextures calls
// ASM-verified: 2026-05-18 binary @ 0x00118350 (re-analyst)
void PowerUp::UnloadTextures() {
    if (m_pScreenEffect)  m_pScreenEffect->UnloadTextures();
    if (m_pPurchaseInfo)  m_pPurchaseInfo->UnloadTextures();
}
