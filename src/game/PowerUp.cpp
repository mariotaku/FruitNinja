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

// Step 2: dtor (binary @ 0x001186bc)
PowerUp::~PowerUp() {
    Deactivate(true);
    delete m_pPurchaseInfo;  m_pPurchaseInfo  = nullptr;
    delete m_pScreenEffect;  m_pScreenEffect  = nullptr;
}

// Step 3: Parse (binary @ 0x001194f0)
void PowerUp::Parse(TiXmlElement* elem) {
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
    for (TiXmlElement child = elem->FirstChildElement();
         child; child = child.NextSiblingElement()) {
        const char* tag = child.Name();
        if (!tag) continue;

        if (strcmp(tag, "purchase_info") == 0) {
            // Presence of <purchase_info> forces purchasable regardless of attr.
            m_bIsPurchasable = true;
            m_pPurchaseInfo = new PurchaseInfo();
            m_pPurchaseInfo->Parse(&child);
        } else if (strcmp(tag, "effect") == 0) {
            m_pScreenEffect = new ScreenEffect();
            m_pScreenEffect->m_pOwnerPowerUp = this;
            m_pScreenEffect->Parse(&child);
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
            } else if (strcmp(tag, "time_sink_mod") == 0) {
                mod = new TimeSinkModifier();
            } else if (strcmp(tag, "explody_mod") == 0) {
                mod = new ExplodyFruitModifier();
            }
            // ASM-spec v1.6.1 PowerUp::Parse @0x00142388: modifier tags "time_sink_mod"/"explody_mod"
            if (mod) {
                mod->Parse(&child);
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

    // TODO: v1.6.1 PowerUp::Parse @0x00142388 tail -- clear GameModifier+0x19 when
    // m_Duration < m_TotalTime-0.1 (semantics unresolved).
}

// Step 4: Activate
// v1.6.1 PowerUp::Activate @0x00141e60 -- Vec3 by value (not const-ref) to match binary ABI
// ASM-verified: 2026-07-15T00:00Z v1.6.1 PowerUp::Activate @ 0x00141e60..0x00141f9b (asm-inspector)
//   -- logic/field-offset/call-graph faithful. NOTE two structural (non-logic) DIFFERS:
//   Vec3 passed HFA-in-VFP vs binary by-hidden-ptr (port _Vector3 trivially-copyable);
//   ApplyModifier dispatched via port vtable slot 8 (+0x20) vs binary slot 5 (+0x14).
void PowerUp::Activate(bool showPopup, bool isPurchased, _Vector3<float> pos, float* extraParam) {
    if (showPopup) {
        if (m_Texture2.IsValid()) {
            MissControl* m = MissControl::GetFree();
            if (m) {
                _Vector3<float> posCopy(pos);
                m->MakeDisappear(posCopy, 0, m_Texture2);
                m->m_LayerFlags = Mortar::HUD_LAYER_BUTTONS;
            }
        }
        if (m_pPurchaseInfo) {
            AddCoins(-m_pPurchaseInfo->m_Cost);  // v1.6.1 PowerUp::Activate calls AddCoins(-cost) @0x00119f78
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

// Step 6: Update (v1.6.1 PowerUp::Update @0x00140600)
int PowerUp::Update(float dt) {
    m_LongestRemaining = 0.0f;
    int activeCount = 0;

    std::list<GameModifier*>::iterator it = m_ModList.begin();
    while (it != m_ModList.end()) {
        GameModifier* mod = *it;
        if (mod->Update(dt) == 0) {                  // GameModifier vtbl +0xc (slot 3) @0x0013fdc4
            float rem = mod->m_BonusAccum;            // mod+0xc
            if (m_LongestRemaining < rem) {
                m_LongestRemaining = rem;
                if (m_TotalTime < rem) m_TotalTime = rem;
            }
            ++activeCount;
            ++it;
        } else {
            mod->RemoveModifier();                   // vtbl +0x18 (slot 6)
            delete mod;                              // vtbl +0x4 (slot 1 deleting dtor)
            it = m_ModList.erase(it);
        }
    }

    // Bar ramps up only when there are active mods with remaining time
    if (m_LongestRemaining > 0.0f) {
        m_BarRamp += dt * 4.0f;
        if (m_BarRamp > 1.0f) m_BarRamp = 1.0f;
    }

    if (m_pScreenEffect) {
        m_pScreenEffect->Update(dt, m_LongestRemaining, m_TotalTime);
    }

    // Power finished (non-purchasable, no active mods): ramp down then remove
    if (m_pPurchaseInfo == NULL && activeCount == 0) {
        m_BarRamp -= dt * 12.0f;
        if (m_BarRamp < 0.0f) m_BarRamp = 0.0f;
        if (m_BarRamp <= 0.0f) return 1;
        return 0;
    }

    // Purchasable branch
    if (m_pPurchaseInfo != NULL) {
        if (activeCount != 0) return 0;
        return (m_pPurchaseInfo->m_CurrentUses < 0) ? 1 : 0;
    }

    return 0;
}

// Step 7: Clone (v1.6.1 PowerUp::Clone @0x001422b0)
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
    float texW = (float)tex->GetWidth();
    float texH = (float)tex->GetHeight();

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

// GetLongestMod: v1.6.1 PowerUp::GetLongestMod @0x0013ff38 (stale header/cpp
// addr 0x00117aec was v1.5.1). Max of m_Duration (+0x04) across all mods,
// initial floor -1.0f (vmov s16,#-1.0) -- was wrongly maxing m_BonusAccum
// (+0x0c) with a 0.0f floor.
float PowerUp::GetLongestMod() {
    float longest = -1.0f;
    for (std::list<GameModifier*>::const_iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        if ((*it)->m_Duration > longest)
            longest = (*it)->m_Duration;
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

// ASM-spec v1.6.1 PowerUp::AddModifier @ 0x0014219c: mod->m_pDeferInfo=this
// (GameModifier+0x1C); m_ModList.push_back(mod) (list head +0x04,
// push_back @0x001100bc).
void PowerUp::AddModifier(GameModifier* mod) {
    mod->m_pDeferInfo = this;   // GameModifier+0x1C — sets owner back-ptr at attach
    m_ModList.push_back(mod);
}

// SetTotalTime — v1.6.1 PowerUp::SetTotalTime @ 0x001407c0 (stale 0x001180d4 was v1.5.1)
void PowerUp::SetTotalTime(float t) {
    m_TotalTime = t;
    float elapsed = t - m_LongestRemaining;
    float ramp = elapsed * 4.0f;
    if (ramp > 1.0f) ramp = 1.0f;   // clamp UPPER only, no lower clamp
    m_BarRamp = ramp;
    if (m_pScreenEffect) {
        m_pScreenEffect->Update(elapsed, m_LongestRemaining, t);
    }
}

// PowerUp::PowerUp(PowerUp*) copy ctor — v1.6.1 @0x00141b58 (C1) / 0x00141c88 (C2)
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
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00117a44 (re-analyst)
// v1.6.1 PowerUp::Purchaseable @0x0013fe74 -- non-const to match binary ABI/mangling
// ASM-verified: 2026-07-15T00:00Z v1.6.1 PowerUp::Purchaseable @ 0x0013fe74 (asm-inspector)
int PowerUp::Purchaseable() {
    return m_pPurchaseInfo ? m_pPurchaseInfo->m_Cost : 0;
}

// @ 0x00117cdc — walk m_ModList; on type==2 mods, call DeferPoints
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00117cdc (re-analyst)
void PowerUp::SetDeferedPoints(int points) {
    for (std::list<GameModifier*>::iterator it = m_ModList.begin();
         it != m_ModList.end(); ++it) {
        if ((*it)->GetType() == 2) {
            static_cast<ScoreModifier*>(*it)->DeferPoints(points);
        }
    }
}

// @ 0x00118350 — null-guarded UnloadTextures calls
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00118350 (re-analyst)
void PowerUp::UnloadTextures() {
    if (m_pScreenEffect)  m_pScreenEffect->UnloadTextures();
    if (m_pPurchaseInfo)  m_pPurchaseInfo->UnloadTextures();
}
