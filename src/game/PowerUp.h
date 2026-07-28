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
//   Activate        0x00141e60   (v1.6.1)
//   Deactivate      0x00117f18
//   Update          0x00117f90
//   Clone           (vtable; various)
//   DrawBar         0x001191f8
//   IsPurchaseable  0x00117a44
//   IsTimed         0x0011a1dc
//   IsSpecial       (inline / vtable)
//   GetCurrentTimeProgress 0x0011a1f0
//   GetLongestMod   v1.6.1 0x0013ff38 (stale 0x00117aec was v1.5.1)
//   SetCurrentTime  0x0011a210
//   SetTotalTime    v1.6.1 0x001407c0 (stale 0x001180d4 was v1.5.1)
//   SetOnScreenAmt  0x0011a1c4
//   AddDeferedPoints 0x00117a50
//   LoadTextures    0x001183f0

#include <cstdint>
#include <list>
#include "GameModifier.h"
#include "ScreenEffect.h"
#include "util/SmartPtr.h"
#include "asset/Texture.h"
#include "math/_Vector3.h"
#include "engine/xml/TiXmlElement.h"

#include "screens/PurchaseInfo.h"

// Colour as stored in binary (RGBA8 packed in 4 bytes).
struct PUColour {
    uint8_t r, g, b, a;
};

class PowerUp {
public:
    // +0x00 vptr (implicit)

    // +0x04 m_ModList — std::list<GameModifier*> (8 bytes, Sourcery 2010q1 pre-C++11)
    std::list<GameModifier*> m_ModList;

    // +0x0c m_NameHash — StringHash(m_Name); set by Parse @ 0x00119518 (blx StringHash)
    // followed by 0x00119520 (str r0, [r4, #0xc]). Lives between m_ModList and m_Name
    // in the binary's layout; was previously misplaced at the struct's end as
    // "port-specific", which shifted every field from m_Name onward 4 bytes earlier
    // than the binary and caused the cross-build to miss the m_pPurchaseInfo /
    // m_pScreenEffect offset asserts.
    uint32_t m_NameHash;

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

    // +0xac m_Texture1 — icon texture (Mortar::SmartPtr<Texture>, 4 bytes)
    Mortar::SmartPtr<Mortar::Texture> m_Texture1;

    // +0xb0 m_Texture2 — popup texture (Mortar::SmartPtr<Texture>, 4 bytes)
    Mortar::SmartPtr<Mortar::Texture> m_Texture2;

    // +0xb4 m_pScreenEffect — owned screen effect (nullptr if none)
    ScreenEffect* m_pScreenEffect;

    // +0xb8..+0xc3 — 12 bytes of dead space in the binary. Verified via
    // exhaustive scan of all 367 PowerUp-related functions (re-analyst
    // ab6c7206): no ldr/str/vldr/vstr ever touches offsets in [+0xb8, +0xc3]
    // through a PowerUp this-pointer. Likely remnants of removed Vec3 cache
    // fields from earlier Mortar engine ancestors. DO NOT shrink — required
    // for binary-compatible sizeof == 0xCC.
    uint8_t _padb8[12];

    // +0xc4 m_DeferredPoints — -1 = "no deferred points pending"; >=0 accumulated
    // Binary @ 0x00117a50 AddDeferedPoints: str to [r0,#0xc4].
    int m_DeferredPoints;

    // +0xc8 m_BarXPos — HUD x-position, interpolated each frame
    float m_BarXPos;

    PowerUp();
    ~PowerUp();

    // @ 0x001194f0 — parse <powerup> XML element
    void Parse(TiXmlElement* elem);

    // v1.6.1 PowerUp::Activate @0x00141e60 — activate this power-up clone
    // param1: showPopup — display miss control + deduct coins
    // param2: isPurchased — forwarded to ApplyModifier
    // param3: position — by-value (not const&) to match binary ABI/mangling
    // param4: extraParam — NULL on most calls; non-null when re-loading from save
    // v1.6.1 PowerUp::Activate @0x00141e60 -- Vec3 by value (not const-ref) to match binary ABI
    void Activate(bool showPopup, bool isPurchased, _Vector3<float> pos, float* extraParam);

    // @ 0x00117f18 — deactivate, call RemoveModifier on all mods; returns 0
    int Deactivate(bool removeAll);

    // @ 0x00117f90 — update all modifiers; returns 1 when all expired, 0 otherwise.
    // VIRTUAL — sole virtual method in the binary; sits in vtable slot[0] at
    // 0x001e8cb8. Vtable has 3 slots; slots[1,2] are NULL (reserved/unused,
    // never dispatched in the binary). All other "method" calls (Activate,
    // Deactivate, DrawBar, Release, ~PowerUp, etc.) are direct PLT calls,
    // NOT vtable dispatch -- do NOT make them virtual.
    virtual int Update(float dt);

    // Clone — heap-alloc new instance, copy state, deep-copy modifier list
    PowerUp* Clone();

    // @ 0x001191f8 — draw power-up HUD bar
    void DrawBar();

    // @ 0x00117a44
    bool IsPurchaseable() const { return m_bIsPurchasable; }

    // @ 0x0011a1dc — true iff m_TotalTime > 0
    bool IsTimed() const { return m_TotalTime > 0.0f; }

    // ASM-spec v1.6.1 PowerUp::IsSpecial @0x00143868: IsTimed() && !m_bIsSpecial && Purchaseable()==0.
    // Despite the name, "special" here means "regular timed banana power" (freeze/frenzy/x2),
    // NOT the "automatic" m_bIsSpecial flag (that's the opposite: zen-mode auto-activated powers).
    // Non-const: calls the now non-const Purchaseable() (matches binary ABI, see below).
    bool IsSpecial() { return IsTimed() && !m_bIsSpecial && Purchaseable() == 0; }

    // @ 0x0011a1f0
    float GetCurrentTimeProgress() const { return m_LongestRemaining; }

    // v1.6.1 PowerUp::GetLongestMod @0x0013ff38 — max m_Duration across all modifiers
    float GetLongestMod();

    // @ 0x0011a210
    void SetCurrentTime(float t) { m_LongestRemaining = t; }

    // v1.6.1 PowerUp::SetTotalTime @ 0x001407c0 (stale 0x001180d4 was v1.5.1)
    void SetTotalTime(float t);

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
    void AddModifier(GameModifier* mod);

    // v1.6.1 PowerUp::PowerUp(PowerUp*) C1 @0x00141b58 / C2 @0x00141cdc — copy constructor
    // ASM-verified: 2026-05-18 v1.6.1 PowerUp::PowerUp(PowerUp*) @ 0x00141b58 (re-analyst)
    PowerUp(PowerUp* src);

    // @ 0x00117a44 — returns coin cost if purchaseable, else 0
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00117a44 (re-analyst)
    // v1.6.1 PowerUp::Purchaseable @0x0013fe74 -- non-const to match binary ABI/mangling
    int Purchaseable();

    // @ 0x00117cdc — walk m_ModList, forward to ScoreModifier::DeferPoints on type==2
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00117cdc (re-analyst)
    void SetDeferedPoints(int points);

    // @ 0x00118350 — null-guarded calls to m_pScreenEffect and m_pPurchaseInfo
    // ASM-verified: 2026-05-18 v1.6.1 binary @ 0x00118350 (re-analyst)
    void UnloadTextures();
};

// std::list<GameModifier*> is 8 bytes on BOTH the Bada production binary AND the
// asm-verify cross-toolchain (Sourcery 2010q1 sentinel-only, per R4 W1 RE and the
// cross-build probe in tmp/asm-compare/list_size_probe.s). With m_NameHash placed
// at its binary-correct +0x0c slot, the virtual Update declaration providing the
// vptr at +0x00, and _padb8 sized to 12 bytes (binary-faithful dead region per
// re-analyst ab6c7206), every offset below holds under both ABIs so the asserts
// validate both the production Bada build and the asm-verify cross-build.
// Host (x86_64) has a different ABI (24-byte list, 8-byte pointers) so the guard
// excludes the host build. The cross-build defines __bada__ via -D__bada__ in
// toolchain.cmake, so these asserts DO fire during asm-verify.
#if defined(__bada__)
static_assert(offsetof(PowerUp, m_NameHash)       == 0x0c, "PowerUp::m_NameHash @ +0x0c");
static_assert(offsetof(PowerUp, m_Name)           == 0x10, "PowerUp::m_Name @ +0x10");
static_assert(offsetof(PowerUp, m_pPurchaseInfo)  == 0x94, "PowerUp::m_pPurchaseInfo @ +0x94");
static_assert(offsetof(PowerUp, m_Texture1)       == 0xac, "PowerUp::m_Texture1 @ +0xac");
static_assert(offsetof(PowerUp, m_Texture2)       == 0xb0, "PowerUp::m_Texture2 @ +0xb0");
static_assert(offsetof(PowerUp, m_pScreenEffect)  == 0xb4, "PowerUp::m_pScreenEffect @ +0xb4");
static_assert(offsetof(PowerUp, m_DeferredPoints) == 0xc4, "PowerUp::m_DeferredPoints @ +0xc4");
static_assert(offsetof(PowerUp, m_BarXPos)        == 0xc8, "PowerUp::m_BarXPos @ +0xc8");
static_assert(sizeof(PowerUp)                     == 0xcc, "PowerUp size == 0xCC (binary; verified in Ghidra)");
#endif

#endif // FN_GAME_POWER_UP_H
