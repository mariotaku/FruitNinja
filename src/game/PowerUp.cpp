// Analysed: 2026-04-30T00:00

#include "PowerUp.h"
#include <cstring>
#include <algorithm>

PowerUp::PowerUp()
    : field_0x00(0)
    , m_NameHash(0)
    , m_bIsPurchasable(false)
    , _pad91{0, 0, 0}
    , m_pPurchaseInfo(nullptr)
    , field_0x98(0)
    , field_0x9c(0.0f)
    , m_TotalTime(0.0f)
    , m_Colour{255, 255, 255, 255}
    , field_0xa8(0.0f)
    , m_pScreenEffect(nullptr)
    , _padb8{0,0,0,0,0,0,0,0,0,0,0,0}
    , field_0xc4(-1)
    , field_0xc8(0.0f)
    , m_bIsSpecial(false)
{
    memset(m_Name, 0, sizeof(m_Name));
    memset(m_DisplayName, 0, sizeof(m_DisplayName));
}

PowerUp::~PowerUp() {
    // Modifiers are freed in Release().
    Release();
    // TODO: delete m_pPurchaseInfo; delete m_pScreenEffect when those types are ported
}

void PowerUp::Parse(TiXmlElement* /*elem*/) {
    // TODO: parse <powerup> children (<score>,<time>,<slash>,<wave>,<screeneffect>),
    //       set m_Name/m_DisplayName/m_NameHash/m_bIsPurchasable/m_bIsSpecial/m_Colour.
    // @ 0x001194f0
}

void PowerUp::Activate(bool /*showPopup*/, bool /*isPurchase*/,
                       float /*posX*/, float /*posY*/, float /*posZ*/, float* /*extra*/) {
    // TODO: call ApplyModifier on each modifier in m_ModList, set field_0xa8 ramp.
    // @ 0x00119134
}

void PowerUp::Deactivate(bool /*removeAll*/) {
    // TODO: call RemoveModifier on each active modifier.
    // @ 0x00117f18
}

int PowerUp::Update(float dt) {
    // TODO: tick each modifier; returns 1 when all modifiers expired.
    // @ 0x00117f90
    (void)dt;
    return 1;   // stub: immediately expires (safe — nothing activated yet)
}

PowerUp* PowerUp::Clone() {
    // TODO: heap-alloc new PowerUp, memcpy state, deep-copy modifier list.
    // Binary clones each GameModifier via virtual Clone() slot.
    PowerUp* c = new PowerUp();
    *c = *this;
    c->m_ModList.clear();
    for (GameModifier* m : m_ModList) {
        // TODO: GameModifier::Clone() virtual -- for now shallow-copy
        (void)m;
    }
    return c;
}

void PowerUp::DrawBar() {
    // TODO: Tier-2 -- renders icon at top of screen using MatrixManager + DrawQuadUnCached.
    // @ 0x001191f8
}

float PowerUp::GetLongestMod() const {
    float longest = 0.0f;
    for (GameModifier* m : m_ModList) {
        if (m->m_Duration_remaining > longest)
            longest = m->m_Duration_remaining;
    }
    return longest;
}

void PowerUp::AddDeferedPoints(int /*n*/) {
    // TODO: sets field_0xc4 and interacts with score delegate.
    // @ 0x000f81f0
}

void PowerUp::LoadTextures() {
    // TODO: call TextureManager::Load for m_Texture1/m_Texture2/m_pScreenEffect->LoadTextures().
    // @ 0x001183f0
}

void PowerUp::Release() {
    for (GameModifier* m : m_ModList) {
        delete m;
    }
    m_ModList.clear();
}
