#ifndef FN_GAME_POWER_UP_H
#define FN_GAME_POWER_UP_H

// Analysed: 2026-04-30T00:00
//
// PowerUp — XML-parsed template + active-instance object.
// Binary size 0xCC (204 bytes). Both the template (m_AllPowerUps) and
// the active clone live in this type. ActivatePower calls Clone() to
// make a heap copy.
//
// Binary addresses:
//   ctor            0x00118d3c
//   dtor            0x001186bc / 0x00118ba0
//   Parse           0x001194f0
//   Activate        0x00119134
//   Deactivate      0x00117f18
//   Update          0x00117f90
//   Clone           (vtable; various)
//   DrawBar         0x001191f8
//   IsPurchaseable  0x00117a44
//   IsTimed         0x0011a1dc
//   IsSpecial       (inline / vtable)
//   GetCurrentTimeProgress 0x0011a1f0
//   GetLongestMod   0x00117aec
//   SetCurrentTime  0x0011a210
//   SetTotalTime    0x001180d4
//   SetOnScreenAmt  0x0011a1c4
//   AddDeferedPoints 0x000f81f0
//   LoadTextures    0x001183f0

#include <cstdint>
#include <list>
#include "GameModifier.h"

class TiXmlElement;

// Forward declarations for unported types — stubs only.
class ScreenEffect;

// Minimal PurchaseInfo layout (0xc4-byte struct in binary).
// Only field_0xc0 is required; remainder is unported.
class PurchaseInfo {
public:
    uint8_t _pad[0xc0];
    int field_0xc0;  // remaining-uses count; ASM-verified @ 0x00119bb0
};

// Colour as stored in binary (BGRA8 packed in 4 bytes).
struct PUColour {
    uint8_t r, g, b, a;
};

// SmartPtr stand-in: stores raw pointer; Load/Release lifecycle owned externally.
template<typename T> struct PURawPtr {
    T* ptr = nullptr;
    bool IsValid() const { return ptr != nullptr; }
    T* Get() const { return ptr; }
    void SetNull() { ptr = nullptr; }
};

class PowerUp {
public:
    // +0x00 (pad / list sentinel)
    uint32_t field_0x00;

    // +0x04 std::list<GameModifier*> m_ModList (12 bytes: prev,next,size)
    std::list<GameModifier*> m_ModList;

    // +0x10  m_NameHash
    uint32_t m_NameHash;

    // +0x10..+0x4F  m_Name[64]
    char m_Name[64];

    // +0x50..+0x8F  m_DisplayName[64]
    char m_DisplayName[64];

    // +0x90  m_bIsPurchasable
    bool m_bIsPurchasable;
    // +0x91 (pad)
    uint8_t _pad91[3];

    // +0x94  m_pPurchaseInfo (0xc4-byte struct; non-null only for purchaseable powers)
    PurchaseInfo* m_pPurchaseInfo;

    // +0x98..+0x9b  (pad)
    uint32_t field_0x98;

    // +0x9c  CurrentTimeProgress — max of all GameModifier m_Duration across active mods
    float field_0x9c;

    // +0xa0  MaxTotalTime — max duration the bar showed for
    float m_TotalTime;

    // +0xa4  m_Colour (BGRA8)
    PUColour m_Colour;

    // +0xa8  OnScreenAmt — bar reveal/hide animation [0..1]
    float field_0xa8;

    // +0xac  m_Texture1 (icon tex)
    PURawPtr<void> m_Texture1;

    // +0xb0  m_Texture2 (popup tex)
    PURawPtr<void> m_Texture2;

    // +0xb4  m_pScreenEffect
    ScreenEffect* m_pScreenEffect;

    // +0xb8..+0xc3  (pad / spawned-flags)
    uint8_t _padb8[12];

    // +0xc4  DeferedPoints — negative = "no deferred points pending"
    int field_0xc4;

    // +0xc8  HUD X-position — interpolated each frame
    float field_0xc8;

    // +0x91  m_bIsSpecial
    bool m_bIsSpecial;

    PowerUp();
    ~PowerUp();

    // @ 0x001194f0 — parse <powerup> XML element
    // TODO: implement via TiXmlElement -- power-up XML loading
    void Parse(TiXmlElement* elem);

    // @ 0x00119134 — activate this power-up clone
    // TODO: calls modifier Activate, sets OnScreenAmt increment
    void Activate(bool showPopup, bool isPurchase, float posX, float posY, float posZ, float* extra);

    // @ 0x00117f18 — deactivate, call RemoveModifier on all mods
    // TODO: real deactivation impl
    void Deactivate(bool removeAll);

    // @ 0x00117f90 — update all modifiers, returns 1 when all expired
    // TODO: real update impl
    int Update(float dt);

    // Clone — heap-alloc new instance, copy state
    // TODO: real clone impl
    PowerUp* Clone();

    // @ 0x001191f8 — draw power-up HUD bar
    // TODO: Tier-2 -- DrawBar renders icon row at top of screen
    void DrawBar();

    // @ 0x00117a44
    bool IsPurchaseable() const { return m_bIsPurchasable; }

    // @ 0x0011a1dc — true iff m_TotalTime > 0
    bool IsTimed() const { return m_TotalTime > 0.0f; }

    bool IsSpecial() const { return m_bIsSpecial; }

    // @ 0x0011a1f0
    float GetCurrentTimeProgress() const { return field_0x9c; }

    // @ 0x00117aec — max m_Duration_remaining across all modifiers
    // TODO: iterate m_ModList
    float GetLongestMod() const;

    // @ 0x0011a210
    void SetCurrentTime(float t) { field_0x9c = t; }

    // @ 0x001180d4
    void SetTotalTime(float t) { m_TotalTime = t; }

    // @ 0x0011a1c4
    void SetOnScreenAmt(float a) { field_0xa8 = a; }

    // @ 0x000f81f0 — hold back deferred points
    // TODO: real impl -- sets field_0xc4 and interacts with score delegate
    void AddDeferedPoints(int n);

    // @ 0x001183f0 — load icon/popup textures
    // TODO: real impl -- calls TextureManager::Load for m_Texture1/2 etc.
    void LoadTextures();

    // Release — free modifier list (called before delete in expiry path)
    void Release();
};

#endif // FN_GAME_POWER_UP_H
