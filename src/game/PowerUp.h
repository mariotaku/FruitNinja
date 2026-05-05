#ifndef FN_GAME_POWER_UP_H
#define FN_GAME_POWER_UP_H

// Analysed: 2026-05-03T00:00
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
//   AddDeferedPoints 0x00117a50
//   LoadTextures    0x001183f0

#include <cstdint>
#include <list>
#include "GameModifier.h"
#include "ScreenEffect.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "math/Vec3.h"
#include <tinyxml2.h>

// Minimal PurchaseInfo layout (0xc4-byte struct in binary).
// Fields verified from binary (re-analyst PowerUp lifecycle pass).
class PurchaseInfo {
public:
    // +0x00: vptr (implicit)
    // +0x04: m_Cost — coin cost of the purchasable power-up
    int m_Cost;
    // +0x08..+0xbf: unported fields (binary layout 0xc4 bytes)
    uint8_t _pad08[0xb8];
    // +0xc0: m_RemainingUses — remaining use count; 0 = exhausted
    // ASM-verified @ 0x00119bb0
    int m_RemainingUses;

    PurchaseInfo() : m_Cost(0), m_RemainingUses(0) {
        for (int i = 0; i < (int)sizeof(_pad08); ++i) _pad08[i] = 0;
    }

    // TODO: implement Parse (binary @ 0x? -- re-analyst pass needed)
    void Parse(tinyxml2::XMLElement* /*xml*/) {}

    // TODO: implement LoadTextures (binary @ 0x? -- re-analyst pass needed)
    void LoadTextures() {}
};

// Colour as stored in binary (RGBA8 packed in 4 bytes).
struct PUColour {
    uint8_t r, g, b, a;
};

class PowerUp {
public:
    // +0x00 vptr (implicit)

    // +0x04 m_ModList — std::list<GameModifier*> (8 bytes, Sourcery 2010q1 pre-C++11)
    std::list<GameModifier*> m_ModList;

    // +0x10 m_Name[0x40]
    char m_Name[0x40];

    // +0x50 m_DisplayName[0x40] — m_Name with first char uppercased
    char m_DisplayName[0x40];

    // +0x90 m_bIsPurchasable
    bool m_bIsPurchasable;

    // +0x91 m_bIsSpecial
    bool m_bIsSpecial;

    // +0x92 pad
    uint8_t _pad92[2];

    // +0x94 m_pPurchaseInfo — owned; non-null only for purchaseable powers
    PurchaseInfo* m_pPurchaseInfo;

    // +0x98 m_bCloned — 1 if this is a clone (from copy-ctor), 0 if primary
    uint32_t m_bCloned;

    // +0x9c m_LongestRemaining — "currentTime" / max active modifier remaining
    float m_LongestRemaining;

    // +0xa0 m_TotalTime — max original duration over all mods
    float m_TotalTime;

    // +0xa4 m_Colour (RGBA8)
    PUColour m_Colour;

    // +0xa8 m_BarRamp — [0..1] ramp fraction for DrawBar fade-in/out
    float m_BarRamp;

    // +0xac..+0xb3 — unported binary fields (8 bytes).
    // Prior RE pass placed m_Texture1 here at +0xac; binary confirmed at +0xb4.
    // These 8 bytes correspond to an unknown field pair (possibly a two-pointer
    // iterator slot on libstdc++ 4.4, or two separate 4-byte fields). Kept as
    // opaque padding until a follow-up RE pass resolves them.
    uint8_t _padac[8];

    // +0xb4 m_Texture1 — icon texture (SmartPtr<Texture>, 4 bytes)
    // Binary @ 0x001183f0 LoadTextures: ldr from [r0,#0xb4].
    SmartPtr<Mortar::Texture> m_Texture1;

    // +0xb8 m_Texture2 — popup texture (SmartPtr<Texture>, 4 bytes)
    SmartPtr<Mortar::Texture> m_Texture2;

    // +0xbc m_pScreenEffect — owned screen effect (nullptr if none)
    ScreenEffect* m_pScreenEffect;

    // +0xc0..+0xc3 — 4 bytes gap to reach +0xc4
    uint8_t _padc0[4];

    // +0xc4 m_DeferredPoints — -1 = "no deferred points pending"; >=0 accumulated
    // Binary @ 0x00117a50 AddDeferedPoints: str to [r0,#0xc4].
    int m_DeferredPoints;

    // +0xc8 m_BarXPos — HUD x-position, interpolated each frame
    float m_BarXPos;

    // --- Port-specific (not in 0xCC binary layout) ---
    // Name hash for map lookup (computed from m_Name during Parse).
    uint32_t m_NameHash;

    PowerUp();
    ~PowerUp();

    // @ 0x001194f0 — parse <powerup> XML element
    void Parse(tinyxml2::XMLElement* elem);

    // @ 0x00119134 — activate this power-up clone
    void Activate(bool isPurchase, const Vec3& pos, float extra);

    // @ 0x00117f18 — deactivate, call RemoveModifier on all mods; returns 0
    int Deactivate(bool removeAll);

    // @ 0x00117f90 — update all modifiers; returns 1 when all expired, 0 otherwise
    int Update(float dt);

    // Clone — heap-alloc new instance, copy state, deep-copy modifier list
    PowerUp* Clone();

    // @ 0x001191f8 — draw power-up HUD bar
    void DrawBar();

    // @ 0x00117a44
    bool IsPurchaseable() const { return m_bIsPurchasable; }

    // @ 0x0011a1dc — true iff m_TotalTime > 0
    bool IsTimed() const { return m_TotalTime > 0.0f; }

    bool IsSpecial() const { return m_bIsSpecial; }

    // @ 0x0011a1f0
    float GetCurrentTimeProgress() const { return m_LongestRemaining; }

    // @ 0x00117aec — max m_Duration_remaining across all modifiers
    float GetLongestMod();

    // @ 0x0011a210
    void SetCurrentTime(float t) { m_LongestRemaining = t; }

    // @ 0x001180d4
    void SetTotalTime(float t) { m_TotalTime = t; }

    // @ 0x0011a1c4
    void SetOnScreenAmt(float a) { m_BarRamp = a; }

    // @ 0x00117a50 — accumulate deferred points; returns 0
    int AddDeferedPoints(int n);

    // @ 0x001183f0 — propagate LoadTextures to screen effect and purchase info
    void LoadTextures();

    // Release — free modifier list (called before delete in expiry path)
    void Release();

    // Modifier list iteration helpers used by PowerUpManager::ActivatePurchase.
    // Binary @ 0x001193d0 walks m_ModList via begin()/end() iterator pattern.
    // Wrap as stateless begin/end accessors; callers iterate directly.
    std::list<GameModifier*>::iterator ModListBegin() { return m_ModList.begin(); }
    std::list<GameModifier*>::iterator ModListEnd()   { return m_ModList.end(); }
    std::list<GameModifier*>::const_iterator ModListBegin() const { return m_ModList.begin(); }
    std::list<GameModifier*>::const_iterator ModListEnd()   const { return m_ModList.end(); }

    // @ 0x001193d0 callee — push a modifier onto this power-up's list
    void AddModifier(GameModifier* mod) { m_ModList.push_back(mod); }
};

#endif // FN_GAME_POWER_UP_H
