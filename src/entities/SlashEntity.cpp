//
// SlashEntity -- blade trail visual (entity type 3).
// v1.6.1 binary-faithful port: heap-allocated vertex buffers, no inline ring.
// sizeof(SlashEntity) = 0x188 (392). See SlashEntity.h for field/method addresses.
//

#include "SlashEntity.h"
#include "math/MathUtil.h"
#include "debug/Logger.h"
#include "debug/DebugFlags.h"
#include "ActorManager.h"
#include "Entity.h"
#include "hud/HUDControl.h"
#include "hud/HUD.h"
#include "render/MatrixManager.h"
#include "render/Renderer.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "input/Touch.h"
#include "input/InputEvent.h"
#include "input/InputManager.h"
#include "particle/PSPParticleManager.h"
#include "audio/GameSound.h"
#include "game/ItemManager.h"
#include "game/PowerUpManager.h"
#include "collision/ColLine.h"
#include "collision/ColSphere.h"
#include "util/StringHash.h"
#include "Game.h"
#include "game/GameMode.h"
#include "game/WaveManager.h"
#include "game/GameTaskState.h"
#include "game/GameOver.h"
#include "game/BonusManager.h"
#include "game/AchievementManager.h"
#include "game/FruitSaveData.h"
#include "engine/network/NetworkManager.h"
#include "engine/core/SystemManager.h"
#include "engine/util/Event.h"
#include "Fruit.h"
#include "Bomb.h"
#include "SplatEntity.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include "game/GameWork.h"
#include "game/FruitCamera.h"
#include "Coin.h"
#include "hud/MissControl.h"
#include "math/Random.h"

// File-scope global: multicast event fired when a combo window expires
// (combo cancel / commit). Binary: file-static in Slash.cpp, ctor'd in
// global.ctors.keyed.to.Slash.cpp @ 0x1ea48c. GOT-resolved: 0x00332bd8.
// DIFFERS: original = direct GOT access on every subscribe site; using static
// accessor SlashEntity::OnComboCancelEvent() for cross-TU access in port.
static Mortar::Event1<SlashEntity*> g_OnComboCancel;

Mortar::Event1<SlashEntity*>& SlashEntity::OnComboCancelEvent() {
    return g_OnComboCancel;
}

// ASM-spec v1.6.1 CheckCombo @ 0x001320b4: signed-char combo quality score
// (-1, 0x00..0x18) sign-extended to int. Binary file-static globals
// comboTypes@0x002d9f74 interleave {slot.type @+0, slot.n @+4} pairs
// ((&DAT_002d9f78)[uniq*2] = 1 initialises the COUNT field); the port's local
// scratch array is equivalent (the globals have zero external xrefs).
// Score table:
//   0x18: 2 unique types in strict ABAB... (any length). Gate = !alternatingFlag:
//         the flag goes FALSE when a repeat matches a non-last slot -- exactly
//         what a genuine ABAB stream does -- then element-wise verified.
//   0x14: 2 unique types, count==5, slot[0].n==2 || slot[1].n==2 (5 fruit, 3+2 split)
//   0x15: 3 unique types, count==5, slot[0].n==2 || slot[1].n==2 (one pair; the
//         binary only tests slots 0/1, which covers every 2+2+1 split -- at most
//         one slot is the singleton)
//   0x17/0x16: any slot has count 4/3 (only when uniq>1)
//   0x04: all unique, count >= 5
//   Rare single-fruit table: 14 named fruit -> 0x06..0x12 (uniq==1 path)
//   Fallback: {-1,-1,-1,0,1,2,3} for count<7 else 5
int CheckCombo(int* fruitTypes, int count, int* outDominantType) {
    struct Slot { int type; int n; };
    static const struct RareEntry { const char* name; signed char score; } rareTable[16] = {
        {"apple",        0x06}, {"apple_red",   0x06},
        {"orange",       0x07}, {"pineapple",   0x08},
        {"watermelon",   0x09}, {"kiwi",        0x0A},
        {"mango",        0x0B}, {"strawberry",  0x0C},
        {"pear",         0x0D}, {"banana",      0x0E},
        {"lime",         0x0F}, {"lemon",       0x10},
        {"coconut",      0x11}, {"passionfruit",0x12},
        {NULL,           0x00}, {NULL,          0x00},
    };
    static int  rareTypes[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    static bool rareInit = false;
    if (!rareInit) {
        for (int i = 0; i < 14; ++i)
            rareTypes[i] = Fruit::FruitType(rareTable[i].name, false);
        rareInit = true;
    }

    Slot scratch[11];
    int  uniq = 0, maxCount = 0, dom = 0;
    bool alternating = true;

    for (int i = 0; i < count; ++i) {
        int t = fruitTypes[i];
        bool found = false;
        for (int j = 0; j < uniq; ++j) {
            if (scratch[j].type == t) {
                found = true;
                if (++scratch[j].n > maxCount) { maxCount = scratch[j].n; dom = t; }
                if (j != uniq - 1) alternating = false;
            }
        }
        if (!found) {
            scratch[uniq].type = t; scratch[uniq].n = 1; ++uniq;
            if (maxCount == 0) { dom = t; maxCount = 1; }
        }
    }
    if (outDominantType) *outDominantType = dom;

    signed char r = -1;
    if (uniq == 1) {
        for (int k = 0; k < 16; ++k)
            if (fruitTypes[0] == rareTypes[k]) return (signed char)rareTable[k].score;
    } else if (uniq == 2) {
        // Binary gate is if (!flag): genuine ABAB clears the flag (every repeat
        // of type A matches slot 0 while slot 1 is last), then the element-wise
        // ABAB verify confirms. A no-repeat slice (e.g. 2 fruit, 2 types) keeps
        // the flag TRUE and never reaches the 0x18 return.
        if (!alternating) {
            bool ok = true;
            for (int i = 0; i < count; ++i) {
                int expect = (i & 1) ? scratch[1].type : scratch[0].type;
                if (fruitTypes[i] != expect) { ok = false; break; }
            }
            if (ok) return 0x18;
        }
        // Binary tests the COUNT field (slot[k].n == 2), not the fruit type:
        // 5 fruit over 2 types with a 3+2 split.
        if (count == 5 && (scratch[0].n == 2 || scratch[1].n == 2)) return 0x14;
    } else if (uniq == 3 && count == 5) {
        // COUNT field again: 5 fruit over 3 types with one pair (2+2+1). Slots
        // 0/1 suffice -- at most one slot of a 2+2+1 split is the singleton.
        if (scratch[0].n == 2 || scratch[1].n == 2) return 0x15;
    } else if (uniq == count && uniq > 4) {
        return 0x04;
    }

    if (uniq > 1) {
        for (int k = 0; k < uniq; ++k) {
            if      (scratch[k].n == 3 && r == -1)    r = 0x16;
            else if (scratch[k].n == 4 && r <  0x17)  r = 0x17;
        }
        if (r != -1) return r;
    }

    static const signed char fallback[7] = { -1, -1, -1, 0, 1, 2, 3 };
    return ((unsigned)count < 7) ? (int)fallback[count] : 5;
}

// File-static CheckCombo cache sentinel. -1 = uncomputed for current best combo.
// Binary @ BSS (file-scope in SlashEntity.cpp translation unit).
static signed char s_CheckComboFlag = -1;

// ASM-spec v1.6.1 SlashEntity::DrawUpdate @0x001e613c: writes two bytes of a
// file-scope state block -- +0x4 (0x00332a7c) = 1 and +0x5 (0x00332a7d) = 0.
//
// s_TouchIngestArmed (0x00332a7c) is the only one with a reader:
// SlashEntity::UpdateTouchDown @0x001ea0a0 loads it right after the
// m_BombHitTimer test and returns when it is 0. Nothing ever clears it, so it
// is a one-way "the blade post-update has run at least once" latch -- the trail
// cannot append before the entity has been ticked through its vtable slot 6.
//
// s_SlashUpdateSeen (0x00332a7d) is write-only: DrawUpdate clears it and
// SlashEntity::Update writes it at @0x001e86b4, but nothing in the binary reads
// it -- those two stores are its only xrefs. Ported so DrawUpdate keeps both of
// its stores; Update deliberately does not (a dead store buys nothing).
static unsigned char s_TouchIngestArmed = 0;
static unsigned char s_SlashUpdateSeen  = 0;

const float SlashEntity::POINT_SPACING         = 64.0f;   // DAT_0017d5fc
const float SlashEntity::MOVE_THRESH_ACTIVE    = 5.0f;    // sqrt(25)

// Binary global SlashEntity::ModPowerMask @ .bss 0x00332bc8 (GOT 0x002d8674).
uint32_t SlashEntity::s_ModPowerMask = 0;

// NOTE: MOVE_THRESH_INACTIVE is vestigial in the binary. The decomp of
// UpdateTouchDown (0x17D2E4) only reads DAT_0017d5f8 (= 2500 = 50^2) when
// field_0x144 (the "blade active" flag) is clear -- but frame 1 always
// enters the reset branch (LAB_0017d444) via the "tail uninitialised" gate
// and sets field_0x144 |= 1 at the bottom, so the 2500 threshold is never
// actually tested against a nonzero distance.
const float SlashEntity::MOVE_THRESH_INACTIVE  = 50.0f;   // sqrt(DAT_0017d5f8 = 2500)

// --- Global content ---
static Mortar::SmartPtr<Mortar::Texture> g_BladeTex;

// Binary file-static `loaded` (_ZL6loaded) @ .bss 0x00332b44 -- the slash BSS
// block base + 0xcc, i.e. the byte immediately below the three slash
// SmartPtr<Texture> slots (+0xd0/+0xd4/+0xd8).
// Three accessors in v1.6.1, all confirmed by xref:
//   SlashEntity::LoadContent @0x001e7e1c reads it, @0x001e7e34 sets it to 1
//   SlashEntity::Release     @0x001e7a20 stores 0  (strb r3,[r5,#0xcc])
//   CleanupSlash             @0x001e8258 stores 0
// So it is the content-loaded guard, NOT a dead once-flag: Release deliberately
// re-arms LoadContent so the next call reloads the slash textures.
static bool g_SlashLoaded = false;

// --- Global instances ---
SlashEntity* g_pSlashEntities[16] = {0};
SlashEntity* g_pSlashEntity = nullptr;

// ---------------------------------------------------------------------------
// Blade-modifier global state.
// Defaults match the binary's _GLOBAL__I_Slash static-init: 16-entry white
// palette, count=1, type=0 (static), lifeScale=1, scales 1/1/0/1/0, flag2=1.
// ---------------------------------------------------------------------------
static float    g_LifeScale         = 1.0f;   // 0x001F3E54
static int      g_ColourCount       = 1;      // 0x001F3E58
static float    g_PaletteProgress   = 0.0f;   // 0x0024D874
static Colour   g_Palette[16] = {
    Colour(255, 255, 255, 255),                                            // entry[0] = Colour::White
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
};                                            // 0x0024D878
// Binary colourOut global @ 0x0024D77C — persistent computed colour output,
// written by every code path (count==1, snapped, lerp). BSS zero-init.
// Consumed by the epilogue which copies to *outColour if non-null.
static Colour   g_ModColourOut(0, 0, 0, 0);
static int      g_ColourType        = 0;      // 0x0024D8B8 (0=static, 1=per-frame, 2=per-swipe)
static uint8_t  g_DirectionalFlag   = 0;      // 0x0024D8BC
static uint32_t g_TrailHash         = 0;      // 0x0024D8C0
static uint32_t g_ContactHash       = 0;      // 0x0024D8C4
static uint32_t g_SecondHash        = 0;      // 0x0024D8C8
static Mortar::SmartPtr<Mortar::Texture> g_ModTexture;

static float    g_Scale1            = 1.0f;   // 0x2D8D78 (ModSlashThickness; SlashModInfo ctor default=1.0)
static float    g_Scale2            = 0.0f;   // 0x332BCC (ModSlashEndThickness; SlashModInfo ctor default=0.0)
static float    g_Scale3            = 1.0f;   // 0x2D8D74 (ModSlashLength; SlashModInfo ctor default=1.0)
static float    g_Scale4            = 1.0f;   // 0x001F3E64 (UV length)
static float    g_Scale5            = 0.0f;   // 0x0024D8D4 (loop UV length)
static uint8_t  g_ScaleFlag1        = 0;      // 0x0024D8D8 (gates CreateGhost())
static uint8_t  g_ScaleFlag2        = 1;      // 0x001F3E69 (gates UV-mirror branch)
// ASM-spec v1.6.1 game_work+0xc4 STOP / +0xc0 STOP_COUNTER (slice debounce)
static unsigned char g_Stop        = 0;      // game_work+0xc4
static int32_t       g_StopCounter = 0;      // game_work+0xc0


static uint32_t ResolveEmitterHash(const char* path) {
    if (!path || path[0] == '\0') return 0;
    uint32_t h = StringHash(path);
    const uint8_t* t =
        PSPParticleManager::GetInstance().FindTemplate(h);
    return t ? h : 0;
}

// ---------------------------------------------------------------------------
// Content load
// ASM-spec v1.6.1 SlashEntity::LoadContent @0x001e7e08:
//   if (loaded) return;
//   loaded = 1;
//   s_slashTexture      = LoadLocalisedTexture(...);
//   s_slashFlashTexture = LoadLocalisedTexture(...);
//   for (i = 0; i < 8; ++i) { ghost[i].Release();
//                             ghost[i].buf0 = new char[0x16c8];
//                             ghost[i].buf1 = new char[0x16c8];
//                             ghost[i].Reset(); }
// The guard is the `loaded` byte, not texture validity -- see g_SlashLoaded above.
// ---------------------------------------------------------------------------
void SlashEntity::LoadContent() {
    if (g_SlashLoaded) return;
    g_SlashLoaded = true;
    g_BladeTex = Mortar::TextureManager::LoadLocalisedTexture("blade.tex");
    // TODO: v1.6.1 0x001e7e08 (SlashEntity::LoadContent) -- the binary also loads a
    // second "flash" slash texture and allocates the 8-entry SlashEntityGhost ring
    // (two 0x16c8-byte vertex buffers each). Blocked on the SlashEntityGhost port.
}

// Port-only teardown helper (no v1.6.1 counterpart; the binary's texture teardown
// lives in CleanupSlash @0x001e8204). Clears g_SlashLoaded alongside the texture so
// LoadContent re-arms, matching CleanupSlash's texture-null + loaded=0 pairing.
void SlashEntity::ReleaseContent() {
    g_BladeTex.SetNull();
    g_SlashLoaded = false;
}

// ASM-spec v1.6.1 CleanupSlash @ 0x001e8204.
// 1. Null 3 SmartPtr<Texture> at BSS offsets +0xd0, +0xd8, +0xd4 (exact binary order).
//    Port maps: g_BladeTex (+0xd0) and g_ModTexture (+0xd4 or +0xd8); one of them covers
//    the +0xd8 slot and one covers +0xd4; the third is unidentified (RE gap below).
// 2. For i=0..7: SlashEntityGhost::Release(ghost_ring[i]) -- deferred (SlashEntityGhost not ported).
// 3. Clear the `loaded` flag (byte at slash BSS+0xcc = 0x00332b44; write @0x001e8258).
// Called from GameDestroy right after CleanUpSplat(), matching the binary's
// CleanupBomb -> CleanupFruit -> CleanUpSplat -> CleanupSlash order.
void CleanupSlash() {
    // Step 1: null the 3 slash textures in binary order (+0xd0, +0xd8, +0xd4).
    // Port identifies g_BladeTex and g_ModTexture; only 2 of 3 slots are mapped.
    g_BladeTex.SetNull();
    g_ModTexture.SetNull();
    // TODO: v1.6.1 CleanupSlash @0x001e8204 nulls 3 slash textures (+0xd0/+0xd4/+0xd8);
    // only g_BladeTex and g_ModTexture mapped -- RE SlashEntity::LoadContent for the third.

    // Step 2: deferred -- SlashEntityGhost not yet ported.
    // TODO: v1.6.1 CleanupSlash @0x001e8204 -- 8x SlashEntityGhost::Release(ghost_ring[i]);
    // blocked on SlashEntityGhost port.

    // Step 3: clear the content-loaded guard (binary strb 0 @0x001e8258).
    g_SlashLoaded = false;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
SlashEntity::SlashEntity()
    : Mortar::Entity()
    , m_TrailEmitter(nullptr)
    , m_Scale(0.0f)
    , m_BaseColour(255, 255, 255, 255)
    , m_HighlightColour(255, 255, 255, 255)
    , m_BombHitEdge(0)
    , m_SplitPoint(0)
    , m_ComboBaseIdx(0)
    , m_PointCount(0)
    , m_pLeftBuffer(nullptr)
    , m_pRightBuffer(nullptr)
    , m_BladeDir(0, 0, 0)
    , m_TailPos(0, 0, 0)
    , m_HeadPos(0, 0, 0)
    , m_PrevHeadPos(0, 0, 0)
    , m_SegLenSq(0.0f)
    , m_HeadThickScale(0.0f)
    , m_SliceBladeDir(0, 0, 0)
    , m_SliceFruitPos(0, 0, 0)
    , m_SliceFruitType(0)
    , m_SwipeSoundTimer(0.0f)
    , m_GhostIndex(0)
    , m_GhostCount(0)
    , m_GhostDir(0, 0, 0)
    , m_ComboTimer(0.0f)
    , m_pComboMissControl(nullptr)
    , m_GhostSpawnTimer(0.0f)
    , m_GhostSpawnPending(0)
    , m_pLastComboFruit(nullptr)
    , m_PendingSplats(-1)
    , m_SplatTimer(0.0f)
    , m_SplatInterval(0.0f)
    , m_TrailShiftA(-1)
    , m_TrailShiftB(-1)
    , m_BladeActive(0)
    , m_ComboScoreScale(0.0f)
    , m_field_0x148(-1)
    , m_field_0x14c(-1)
    , m_ComboCount(0)
    , m_ComboCounter(0)
    , m_ComboOnlineMode(0)
    , m_AngleIndex(0)
#if !defined(__bada__)
    , m_FingerId(0)
    , m_RawTouchPos(0, 0, 0)
    , m_SmoothedSpeed(0.0f)
    , m_pCurrentTarget(nullptr)
#endif
{
    memset(_pad4d, 0, sizeof(_pad4d));
    memset(m_GhostDirRing, 0, sizeof(m_GhostDirRing));
    memset(_pad125, 0, sizeof(_pad125));
    memset(_pad141, 0, sizeof(_pad141));
    memset(_pad186, 0, sizeof(_pad186));
    for (int i = 0; i < 10; ++i) m_ComboFruitTypes[i] = -1;
    m_ComboCount = -1;
}

SlashEntity::~SlashEntity() {
    Release();
}

// Port-only convenience: stores fingerId, then makes the binary-faithful 3-arg
// Init call. A SlashEntity never subscribes to the InputManager itself -- the
// blade is driven indirectly from TouchDownCallback @0x001cbf18 and
// PointerMoveCallback @0x001cbfcc, which index g_pSlashEntities[] (the binary's
// inputEnts) by finger. See GameTaskInput.cpp.
void SlashEntity::Init(int fingerId) {
#if !defined(__bada__)
    m_FingerId = fingerId;
#else
    (void)fingerId;
#endif
    Init(static_cast<void*>(nullptr), 0L, static_cast<_Vector3<float>*>(nullptr));
}

// ASM-spec v1.6.1 SlashEntity::Release @0x001e79b0 (44 bytes of body, 5 blocks):
//   free m_pLeftBuffer  (+0x5c) and null it
//   free m_pRightBuffer (+0x60) and null it
//   if (m_TrailEmitter) { PSPParticleManager::GetInstance()->ClearEmitter(it); null it }
//   loaded = 0                       <- strb r3,[r5,#0xcc] @0x001e7a20
//   tail-call Mortar::Entity::Release
// The binary does NOT touch m_PointCount (+0x58) here; the port used to zero it.
// So a Released blade keeps a stale m_PointCount alongside null buffers, in the
// binary as much as in the port. GetHeadThicknessScale and AddPoint therefore stay
// buffer-null-unguarded to match the binary -- neither is reachable after Release,
// because Init/Reset re-allocate the buffers and zero m_PointCount before any
// touch callback can run. Do not "fix" this by zeroing m_PointCount here.
void SlashEntity::Release() {
    if (m_pLeftBuffer) {
        delete[] m_pLeftBuffer;
        m_pLeftBuffer = nullptr;
    }
    if (m_pRightBuffer) {
        delete[] m_pRightBuffer;
        m_pRightBuffer = nullptr;
    }
    if (m_TrailEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }
    // Re-arm LoadContent. The slot has three live accessors (LoadContent read+set,
    // Release, CleanupSlash) -- it is the content-loaded guard, not a dead flag.
    g_SlashLoaded = false;

    // Binary tail-calls the base (b Mortar::Entity::Release @0x001e7a28), which
    // frees the ColLine that SlashEntity::Init allocated into m_Col (+0x38).
    // The port used to drop this, leaking one ColLine per blade teardown.
    Mortar::Entity::Release();
}

// ---------------------------------------------------------------------------
// Reset -- binary ~0x17B71C
// Wipe touch/trail state; sentinel-fill both vertex strips up to m_SplitPoint;
// clear 11-entry combo-slice array.
// ---------------------------------------------------------------------------
#if defined(__GNUC__) && !defined(__clang__)
// Binary's vertex-wipe + combo loops are rolled; the cross-build -O2 unrolls them
// (181 instr vs binary's 86). Pin this fn to non-unrolled codegen so it matches.
__attribute__((optimize("no-unroll-loops")))
#endif
void SlashEntity::Reset() {
    m_PointCount = 0;

    // ASM-spec v1.6.1 SlashEntity::Reset @0x001e6688: clears the bomb-hit latch and
    // re-seeds the -65535 tail sentinel. Reachable via ResetGameEntities @0x001cb9c0,
    // which UpdateBombHit @0x001cbbac calls on the m_BombHitTimer 1.5s crossing --
    // so m_BombHitEdge is always cleared ~1.7s after a bomb hit, before the timer
    // reaches 0 and taps can reach the blade. The latch is transient by construction;
    // TouchDown's `m_BombHitEdge == 0` gate cannot strand it.
    m_BombHitEdge = 0;

    // Binary @0x1e66c8: re-arm blade direction to the zero vector on every
    // touch-down (ldmia/stmia copies _Vector3::Zero @0x2d9288;
    // DAT_002d928c/002d9290 = 0.0f). Set before the anchor sentinels.
    m_BladeDir = _Vector3<float>(0.0f, 0.0f, 0.0f);

    // Binary @ 0x1e6688: re-arm the anchor sentinel on every touch-down
    // (do/while i!=3 writes (-65535,-65535,-65535) to +0x70/+0x7c/+0x88).
    // DAT_001e67e0 = 0xc77fff00 = -65535.0f.
    static const float kAnchorSentinel = -65535.0f;
    m_TailPos     = _Vector3<float>(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
    m_HeadPos     = _Vector3<float>(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
    m_PrevHeadPos = _Vector3<float>(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);

    // Binary @0x1e671c..0x1e6744: clear the ghost-ring write cursor/count, then
    // fill all 6 m_GhostDirRing entries (stride 0xc) with _Vector3::Zero.
    // Re-arms the #128 directional trail emitter on every new slice.
    m_GhostIndex = 0;
    m_GhostCount = 0;
    for (int i = 0; i < 6; ++i) m_GhostDirRing[i] = _Vector3<float>(0.0f, 0.0f, 0.0f);
#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "Reset[%d]: seed anchors tail=(%.1f,%.1f,%.1f) head=(%.1f,%.1f,%.1f) prev=(%.1f,%.1f,%.1f) pointCount=%d",
             m_FingerId,
             m_TailPos.x, m_TailPos.y, m_TailPos.z,
             m_HeadPos.x, m_HeadPos.y, m_HeadPos.z,
             m_PrevHeadPos.x, m_PrevHeadPos.y, m_PrevHeadPos.z,
             m_PointCount);
#endif

    // ASM-spec v1.6.1 SlashEntity::Reset @0x1e6688: fully wipe both ribbon buffers
    //   (pos=0, normal=(0,0,1), uv=0, colour=white) for all m_SplitPoint verts.
    //   Clearing only .colour (prior port) left stale positions; DrawSlice submits
    //   m_PointCount+1 verts and the head-cap slot [m_PointCount] is undrawn until
    //   m_PointCount>2, so the first frames of a new slice read the previous slice's
    //   vertex -> bridging triangle.
    if (m_pLeftBuffer && m_pRightBuffer) {
        Colour white(255, 255, 255, 255);
        uint32_t whitePacked = white.PlatformColour();
        for (int side = 0; side < 2; ++side) {
            QUADCUSTOMVERTEX* buf = (side == 0) ? m_pLeftBuffer : m_pRightBuffer;
            for (int i = 0; i < m_SplitPoint; ++i) {
                // Store order mirrors the binary @0x1e6754 (y,z,nx,ny,colour,u,v,nz,x).
                buf[i].y      = 0.0f;
                buf[i].z      = 0.0f;
                buf[i].nx     = 0.0f;
                buf[i].ny     = 0.0f;
                buf[i].colour = whitePacked;
                buf[i].u      = 0.0f;
                buf[i].v      = 0.0f;
                buf[i].nz     = 1.0f;
                buf[i].x      = 0.0f;
            }
        }
    }

    // DO NOT zero m_RawTouchPos here. TouchMoveX/Y set it just before
    // TouchDown calls Reset, and UpdateTouchDown reads it AFTER Reset to
    // start the new trail at the press position.
    if (m_TrailEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }

    for (int i = 0; i < 10; ++i) m_ComboFruitTypes[i] = -1;
    m_ComboCount = -1;

#if !defined(__bada__)
    // Port specific: re-arm the motion-mode speed gate on every new stroke
    // so a stale high reading from the previous slice can't let an aiming
    // move through as a cut.
    m_SmoothedSpeed = 0.0f;
#endif
}

// ---------------------------------------------------------------------------
// Trivial binary stubs
// ---------------------------------------------------------------------------

int SlashEntity::CollisionResponse() {
    return 0;
}

int SlashEntity::UpdateCollisionLine(long /*dt*/) {
    return 0;
}

// ASM-spec v1.6.1 SlashEntity::DrawUpdate @0x001e613c: whole body is
//   s_TouchIngestArmed = 1;   // strb r2,[r3,#0x4]
//   s_SlashUpdateSeen  = 0;   // strb r2,[r3,#0x5]
// This is Entity vtable slot 6 -- the port spells that slot PostUpdate (see
// Entity.h), so PostUpdate forwards here rather than duplicating the body.
void SlashEntity::DrawUpdate(float /*dt*/) {
    s_TouchIngestArmed = 1;
    s_SlashUpdateSeen  = 0;
}

// Binary @ 0x17B388 -- clear back-pointer to combo MissControl when deleted.
void SlashEntity::MissControlDeleted(HUDControl* /*ctrl*/) {
    m_pComboMissControl = nullptr;
}

// ---------------------------------------------------------------------------
// PreUpdate, PostUpdate, PlaySwipe, GetHeadThicknessScale, CreateGhost
// ---------------------------------------------------------------------------

// Entity vtable slot 6 (+0x18). The binary's slot-6 body is
// SlashEntity::DrawUpdate @0x001e613c (_ZTV11SlashEntity+0x20 -> 0x001e613c);
// the port names the slot PostUpdate across every Entity subclass, so this
// forwards instead of holding a second copy of the body.
void SlashEntity::PostUpdate(float dt) { DrawUpdate(dt); }

// ASM-spec v1.6.1 SlashEntity::PreUpdate @0x1e7920
void SlashEntity::PreUpdate(float dt) {
    // ASM-spec v1.6.1 SlashEntity::PreUpdate @0x1e7920: STOP debounce countdown
    if (g_StopCounter < 5) g_StopCounter += 1;
    else                   g_Stop = 0;
    // Port specific: SlashEntityGhost ring (8 slots) deferred.
    // Port specific: ItemManager::PushSwipeLoopVolume deferred.
    if (g_ColourType == 1 /* PER_SLASH */) {
        UpdateModColour(nullptr, dt);
    }
}

// ASM-spec v1.6.1 SlashEntity::PlaySwipe @0x001e8550:
//   if (ItemManager::PlayAlternateSwipeSound(1.0, 1.0) == 0) {   // gate @0x1e857c -- alternate
//       // (callee = v1.6.1 ItemManager::PlayAlternateSwipeSound @0x00139b04; returns the
//       //  SlashSoundMods m_bPlayOntop byte, 0 when the equipped blade has no swipe sounds)
//       idx  = Math::Random::Rand32(g_random, 6) + 1;            // swipe SUPPRESSES sword swipe
//       OS_SPrintf(buf, 0x40, "Sword-swipe-%d", idx);
//       gain = 0.4f + 0.6f * hud->m_globalTimeScale;             // vmla @0x1e85cc; pool literals
//                                    // 0.4f=0x3ECCCCCD @0x1e8660, 0.6f=0x3F19999A @0x1e8664; no clamp
//       SFXPlay(buf, /*atten*/1.0f, /*gain*/gain, {}, /*pitch 0.0f @0x1e8668*/);
//   }
//   RandF(0.5);                          // result discarded -- advances the shared g_Random stream
//   ActorManager::GetNumEntities(0);     // result discarded
//   ActorManager::GetNumEntities(1);     // result discarded (@0x1e862c-0x1e8648)
//   m_ComboScoreScale = 6.0f;            // +0x144 @0x1e8650 (vstr s15,[r6,#0x144]) -- NOT the
//                                        // swipe cooldown; the caller (Update @0x1e96b4..0x1e96bc)
//                                        // writes m_SwipeSoundTimer (+0xb8) = 0.05f after the call.
// Trailing discarded calls are kept for RNG-stream / call-ordering fidelity.
void SlashEntity::PlaySwipe() {
    // Binary is 'bl ItemManager::GetInstance; vmov s0,1.0; bl
    // PlayAlternateSwipeSound' -- the result is used as `this` with no cmp.
    ItemManager* im = ItemManager::GetInstance();
    if (!im->PlayAlternateSwipeSound(1.0f, 1.0f)) {
        char buf[0x40];
        const int idx = (int)Math::g_Random.Rand32(6) + 1;
        snprintf(buf, sizeof(buf), "Sword-swipe-%d", idx);
        // Binary derefs mHud unguarded (boot guarantees it; PlaySwipe only fires
        // during live slash input, after GameInit created the HUD).
        const float gain = 0.4f + 0.6f * game_work.mHud->m_globalTimeScale;
        // v1.6.1 SlashEntity::PlaySwipe @0x001e8550: mGameSound is likewise deref'd
        // unguarded -- the only gate is ItemManager::PlayAlternateSwipeSound above.
        game_work.mGameSound->SFXPlay(buf, 1.0f, gain);
    }

    (void)Math::g_Random.RandF(0.5f);
    // Binary @0x001e8634-0x001e863c: 'bl ActorManager::GetInstance; mov r1,#0;
    // bl GetNumEntities' -- no cmp on the instance.
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    (void)am->GetNumEntities(0);
    (void)am->GetNumEntities(1);

    m_ComboScoreScale = 6.0f;
}

// ASM-spec v1.6.1 SlashEntity::GetHeadThicknessScale @ 0x001e684c. Head thickness
// scale = half the L/R edge separation at the LAST stored vertex, normalized by the
// nominal full half-width (ModSlashThickness*9). Range [0,1]. Consumed by
// OnTouchActive (binary UpdateTouchDown @0x001e9f08) as per-point taper pressure.
// One known delta from the binary body: the binary calls IsSameScreenMultiplayer()
// and discards the result before reading ModSlashThickness. Same-screen MP is
// defunct; the call is dropped.
// The binary's only early-out is `m_PointCount < 1` (@0x001e684c
// `ldr r3,[r0,#0x58] / cmp r3,#0 / ble 0x001e68fc`); it then indexes [+0x5c]/[+0x60]
// unguarded. The port's buffer null checks were port-added and have been removed.
float SlashEntity::GetHeadThicknessScale() const {
    if (m_PointCount < 1) return 0.0f;
    const int i = m_PointCount - 1;                  // last written vertex index
    const float dx = m_pLeftBuffer[i].x - m_pRightBuffer[i].x;
    const float dy = m_pLeftBuffer[i].y - m_pRightBuffer[i].y;
    const float d2 = dx*dx + dy*dy;
    const float half = (d2 <= 0.01f) ? 0.0f : (sqrtf(d2) * 0.5f);
    float scale = half / (g_Scale1 * 9.0f);          // g_Scale1 == ModSlashThickness
    if (scale >= 1.0f) scale = 1.0f;
    return scale;
}

// v1.6.1 SlashEntity::CreateGhost @0x001e67f4 -- snapshot blade vertex strips into
// the global ghost ring. Binary body:
//     s_currentSlashIdx = (s_currentSlashIdx + 1) % 8;
//     s_ghosts[s_currentSlashIdx].StartEffect(&m_pLeftBuffer, m_PointCount);
// Ring size 8, SlashEntityGhost stride 0x10.
// TODO: v1.6.1 0x001e67f4 (SlashEntity::CreateGhost) -- SlashEntityGhost is not
// ported, so this stays a no-op stub. Ghost API for the port:
//   StartEffect @0x001eb048, Update @0x001eaf4c, Draw @0x001eb0f8,
//   Reset @0x001eaaec, Release @0x001eaf10.
// ASM-spec v1.6.1 SlashEntity::CreateGhost @ 0x001e67f4: body is the two-line
// ring-advance + StartEffect call described above; ported as a no-op stub
// since SlashEntityGhost is not ported (see TODO above).
void SlashEntity::CreateGhost() {
}

// ---------------------------------------------------------------------------
// UpdateModColour -- v1.6.1 binary @ 0x0017b0f4
//
// Binary-faithful implementation. The function ALWAYS updates g_ModColourOut
// (the persistent colour global at 0x0024D77C) and optionally writes to
// *outColour if non-null. Three paths:
//
//   count==1: g_ModColourOut = g_Palette[0]
//   snapped:  g_ModColourOut = g_Palette[(int)(progress+0.5) % count]
//   lerp:     g_ModColourOut = lerp(g_Palette[i0], g_Palette[i1], t)
//
// Callers control colour type by the dt they pass:
//   type 0 (static): -2.0f/pointCountF  from UpdatePoints body
//   type 1 (cycle):  localDt            from Update / PreUpdate
//   type 2 (swipe):  1.0f               from ColoursChanged / TouchDown
//
// Negative progress recovery depends on ColourType:
//   type 0: backward-wrap (add count until >= 0)
//   other:  clamp to 0
//
// Zero-clamping on lerp results matches the binary's VCVT.U32.F32 + positive guard.
//
// Binary epilogue uses a custom Colour::operator= (r1=dest, r2=src convention
// at 0x0010c488) to copy colourOut to *outColour.
// ---------------------------------------------------------------------------
void SlashEntity::UpdateModColour(Colour* outColour, float dt) {
    // ASM-verified v1.6.1 SlashEntity::UpdateModColour @0x001e5de4: when dt==0 the
    // binary skips the palette COMPUTATION (vcmp s0,#0; beq epilogue @0x1e6058) but
    // STILL runs the epilogue copy g_ModColourOut -> *outColour. Returning early on
    // dt==0 (as the port used to) means the per-frame Update(dt=0) call never
    // re-seeds m_HighlightColour from the palette, so mod-colour skins (e.g. flame)
    // drift to black while the default (white palette) is unaffected. Skip only the
    // computation; the epilogue below always runs (preserves the disco no-advance
    // because the held g_ModColourOut is copied, not re-advanced).
    if (dt != 0.0f) {
    const int count = g_ColourCount;

    if (count == 1) {
        // Count==1 path: copy first palette entry to colourOut (no animation).
        // Binary: operator=(&colourOut, &ModColours[0]) with custom convention.
        g_ModColourOut = g_Palette[0];
    } else {
        // Advance palette progress.
        g_PaletteProgress += dt * g_LifeScale;

        const float countF = (float)count;

        // Wrap to [0, count).
        while (g_PaletteProgress >= countF) {
            g_PaletteProgress -= countF;
        }

        // Handle negative progress.
        if (g_PaletteProgress < 0.0f) {
            if (g_ColourType == 0) {
                // Type 0: wrap backward (add count until >= 0).
                while (g_PaletteProgress < 0.0f) {
                    g_PaletteProgress += countF;
                }
            } else {
                // Type 1 or 2: clamp to 0.
                g_PaletteProgress = 0.0f;
            }
        }

        // Snap check: progress within +/-0.01 of a palette entry index.
        const int snapInt = (int)(g_PaletteProgress + 0.5f);
        const float frac = g_PaletteProgress - (float)snapInt;
        const bool inSnap = (frac > -0.01f) && (frac < 0.01f);

        if (inSnap) {
            // Snapped: copy the exact palette entry to colourOut.
            // Binary: snapHalf % count via __aeabi_idivmod.
            g_ModColourOut = g_Palette[snapInt % count];
        } else {
            // Not snapped: lerp between consecutive palette entries.
            const int i0 = (int)g_PaletteProgress;
            const int i1 = (i0 + 1) % count;
            const float t = g_PaletteProgress - (float)i0;

            // Binary uses VCVT.U32.F32 (unsigned conversion) with a positive
            // guard to zero-clamp negative results.
            float rResult = (float)g_Palette[i0].r
                + ((float)g_Palette[i1].r - (float)g_Palette[i0].r) * t;
            float gResult = (float)g_Palette[i0].g
                + ((float)g_Palette[i1].g - (float)g_Palette[i0].g) * t;
            float bResult = (float)g_Palette[i0].b
                + ((float)g_Palette[i1].b - (float)g_Palette[i0].b) * t;
            float aResult = (float)g_Palette[i0].a
                + ((float)g_Palette[i1].a - (float)g_Palette[i0].a) * t;

            // Zero-clamp: (0.0 < val) ? (uint8_t)(int)val : 0.
            g_ModColourOut.r = (rResult > 0.0f) ? (uint8_t)(int)rResult : 0;
            g_ModColourOut.g = (gResult > 0.0f) ? (uint8_t)(int)gResult : 0;
            g_ModColourOut.b = (bResult > 0.0f) ? (uint8_t)(int)bResult : 0;
            g_ModColourOut.a = (aResult > 0.0f) ? (uint8_t)(int)aResult : 0;
        }
    }

    }  // end if (dt != 0.0f) -- computation only

    // Epilogue: always copy colourOut to output pointer if non-null (binary
    // @0x1e6058 -- runs for both the dt==0 and dt!=0 paths).
    // Binary uses custom operator= at 0x0010c488 (r1=dest, r2=src convention).
    if (outColour) {
        *outColour = g_ModColourOut;
    }
}

// ---------------------------------------------------------------------------
// Touch ingestion
// ---------------------------------------------------------------------------
void SlashEntity::OnTouchActive(float x, float y) {
    _Vector3<float> newPos(x, y, 0.0f);
#if !defined(__bada__)
    // Port specific: cache SDL touch coordinates for splat emission (lacks Bada InputEvent pipeline).
    m_RawTouchPos = newPos;
    pos = newPos;   // Binary wires TouchMoveX/Y straight into Entity::pos
                    // (base.m_Position @0x001e785c / @0x001e77b4); the port cached
                    // only to m_RawTouchPos, leaving pos at (0,0) so trail-splats
                    // (and ModPowerMask repel/attract) used screen center.
#endif

    const _Vector3<float> lastCenter = m_TailPos;
    const _Vector3<float> distVec(newPos.x - lastCenter.x, newPos.y - lastCenter.y, 0.0f);
    const float distSq = distVec.x * distVec.x + distVec.y * distVec.y;

    // Binary @ 0x1e9f08 (UpdateTouchDown): gate is tail.x <= -65520.0f (DAT_001ea3f8).
    // -65535 (sentinel) <= -65520, so a freshly Reset blade always hits the SEED branch.
    const bool isSeed = (m_TailPos.x <= -65520.0f);

    // Distance threshold: active blade uses MOVE_THRESH_ACTIVE^2, inactive uses MOVE_THRESH_INACTIVE^2.
    // Binary: (this[0x140] & bit0) ? 25.0 : 2500.0.
    const float thresh = (m_BladeActive != 0)
        ? (MOVE_THRESH_ACTIVE   * MOVE_THRESH_ACTIVE)
        : (MOVE_THRESH_INACTIVE * MOVE_THRESH_INACTIVE);

#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "OnTouchActive[%d]: pos=(%.2f,%.2f) isSeed=%d distSq=%.2f thresh=%.2f bladeActive=%d",
              m_FingerId, x, y, (int)isSeed, distSq, thresh, (int)m_BladeActive);
#endif

    if (distSq < thresh && !isSeed) {
        // Binary UpdateTouchDown @0x1e9f08: when below the move threshold but the
        // stroke already has points, the binary does `else if (0 < m_PointCount)
        // goto LAB_001ea3d0` -- it skips AddPoint but STILL re-arms m_BladeActive.
        // Re-arming here is load-bearing: UpdateBladeLatch shifts the latch every
        // tick, so two consecutive ticks without a re-arm decay it 1->2->0; the next
        // per-tick TouchDown then sees m_BladeActive==0, calls Reset() (wiping the
        // trail -> a visibly disconnected segment) AND re-advances the disco mod
        // colour -- splitting one swipe into multiple differently-coloured pieces.
        // ASM-verified: 2026-06-16 v1.6.1 binary @ 0x1ea3d0 (asm-inspector)
        if (m_PointCount > 0) {
            m_BladeActive |= 1;
        }
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "OnTouchActive[%d]: skipped-add, re-armed (below thresh) pointCount=%d",
                 m_FingerId, m_PointCount);
#endif
        return;
    }

    // ASM-spec v1.6.1 SlashEntity::UpdateTouchDown @0x1ea214: m_TrailShiftA = m_PointCount-2 (activity gate, pre-AddPoint).
    m_TrailShiftA = m_PointCount - 2;

    _Vector3<float> dir;
    if (isSeed) {
        // Binary LAB_001ea1b4: copy current touch pos into all three anchors.
        m_TailPos     = newPos;
        m_HeadPos     = newPos;
        m_PrevHeadPos = newPos;
        m_PointCount  = 0;
        m_BladeDir    = _Vector3<float>(1.0f, 0.0f, 0.0f); // non-zero seed so AddPoint guard passes
        // Binary computes seed direction from DAT_001ea41c (global ref vec) - tail.
        // Using (1,0,0) matches binary's "non-degenerate first direction" intent.
        dir = _Vector3<float>(1.0f, 0.0f, 0.0f);
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "OnTouchActive[%d]: SEED branch -> anchors=(%.2f,%.2f) pointCount=%d",
                 m_FingerId, x, y, m_PointCount);
#endif
    } else {
        const float dist = sqrtf(distSq);
        // ASM-spec v1.6.1 SlashEntity::UpdateTouchDown @0x1e9f08: AddPoint receives
        // the RAW position-tail vector (magnitude=travel dist), not unit -- drives
        // m_BladeDir/splat/jerk. Unit vector kept separately ONLY for step positions.
        _Vector3<float> unitDir(distVec.x / dist, distVec.y / dist, 0.0f);
        dir = distVec; // raw dir passed to AddPoint; magnitude == travel distance

        // Binary UpdateTouchDown @0x1e9f08: ramp pressure from headThick -> 1.0 for
        // interpolated points (fVar12=travelled, fVar13=segment length).
        const float headThick = GetHeadThicknessScale();

        // Interpolate intermediate points every POINT_SPACING units.
        float travelled = POINT_SPACING;
        while (travelled < dist) {
            _Vector3<float> step(lastCenter.x + unitDir.x * travelled,
                                 lastCenter.y + unitDir.y * travelled, 0.0f);
            const float pressure = headThick + (travelled / dist) * (1.0f - headThick);
            AddPoint(step, dir, pressure);
            travelled += POINT_SPACING;
        }
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "OnTouchActive[%d]: ADD branch dist=%.2f dir=(%.2f,%.2f) pointCount=%d",
                 m_FingerId, dist, dir.x, dir.y, m_PointCount);
#endif
        // ASM-spec v1.6.1 SlashEntity::UpdateTouchDown @0x1ea2fc
        // Orient trail emitter along swipe direction for particles_directional blades.
        // NOTE: genuine FUSED gate -- @0x001ea2fc is 'ldr r3,[r4,#0x3c]; cmp r3,#0;
        // beq; ldr r3,[GOT]; ldrb r3,[r3]; cmp r3,#2; bne'. The emitter null test is
        // the first half of the binary's own two-part gate, not a port addition.
        if (m_TrailEmitter != NULL && g_DirectionalFlag == 2) {
            short ang  = Math::Atan2Idx(unitDir.x, unitDir.y);
            uint16_t nAng = (uint16_t)(-(short)ang);
            m_TrailEmitter->m_DirSin = -Math::SinIdx(nAng);
            m_TrailEmitter->m_DirCos =  Math::CosIdx(nAng);
        }
    }

    // Always lay the head point at the live touch position (full pressure, binary: 1.0).
    AddPoint(newPos, dir, 1.0f);

    // Binary end-of-frame anchor history shift (UpdateTouchDown epilogue):
    // prevhead <- head <- tail <- touchPos.
    m_PrevHeadPos = m_HeadPos;
    m_HeadPos     = m_TailPos;
    m_TailPos     = newPos;
#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "OnTouchActive[%d]: anchor shift -> tail=(%.2f,%.2f) head=(%.2f,%.2f) prev=(%.2f,%.2f) pointCount=%d",
             m_FingerId,
             m_TailPos.x, m_TailPos.y,
             m_HeadPos.x, m_HeadPos.y,
             m_PrevHeadPos.x, m_PrevHeadPos.y,
             m_PointCount);
#endif

    // ASM-spec v1.6.1 @0x1ea3bc: m_TrailShiftB = m_PointCount-2 (post-AddPoint).
    m_TrailShiftB = m_PointCount - 2;

    // Binary LAB_001ea3d0 (UpdateTouchDown epilogue): re-arm bit0 every tick a
    // TouchDown event arrives so the latch shift sees an active fuse.
    // ASM-verified: 2026-06-16 v1.6.1 binary @ 0x1ea3d0 (asm-inspector)
    m_BladeActive |= 1;
}

// ---------------------------------------------------------------------------
// AddPoint -- v1.6.1 SlashEntity::AddPoint @0x1e9918
// Appends one vertex pair (center + edge) to both ribbon buffers.
// ALWAYS appends on every call -- NO per-point spacing gate exists in the binary.
// Spacing is handled upstream in OnTouchActive (64u interpolation + head seed).
//
// ASM-spec v1.6.1 SlashEntity::AddPoint @0x1e9918: the block @0x1e9c00-0x1e9cd8
// that the prior port turned into a spacing gate is NOT a gate. It only computes
// a scratch perpendicular half-width of the previously stored edge pair
// (lastEdge - lastCenter) as a dead/unused second arg; the actual scalar used for
// thickness is s0 = param_1*9*ModSlashThickness. DAT_001e9eb0=0.6 is the thickness
// coefficient, NOT a distance threshold. The prior port misread it and dropped points
// closer than ~5.4u apart, causing the ribbon to dash on slow slices. Removed.
//
// Two exits only: (1) near-zero direction guard (top), (2) normal fall-through.
//
// ORIGINAL_SLASH constants (hardcoded; SetEquipped path not yet ported):
//   startW   = 0.0  (m_ScaleLength    @ SlashModInfo+0x70 / binary 0x332BCC)
//   endW     = 1.0  (m_ScaleEndThick  @ SlashModInfo+0x6c / binary 0x2D8D78)
//   widthDiv = 1.0  (m_ScalePointScale @ SlashModInfo+0x74)
//   headTaper = 0.0
//
// Head half-width = pressure * 9.0 * ModSlashThickness (g_Scale1).
// Edge offset = miterDir * halfWidth where miterDir = CosIdx/SinIdx(m_AngleIndex).
//
// Scroll cap: if m_SplitPoint-2 <= m_PointCount, slide buffers down by one pair
//   and set m_PointCount = m_SplitPoint-4.
// ---------------------------------------------------------------------------
void SlashEntity::AddPoint(_Vector3<float> center, _Vector3<float> dir, float pressure) {
    // AddPoint @0x001e9918 entry is just `bl 0x001e8340` (the zero-direction test);
    // there is no buffer null check. The port's was port-added and has been removed.

    // ASM-spec v1.6.1 T_1399 @0x1e8340: skip only if dir AND m_BladeDir both zero.
    // Binary OR-guard: if both are (0,0,0) there is no direction to draw with.
    // With raw dir (Fix A), a near-stationary frame gives tiny-but-nonzero distVec;
    // the binary still appends (m_BladeDir nonzero from prior frame), so we must not
    // gate on dir-magnitude alone.
    if (dir.MagnitudeSqr() < 1e-8f && m_BladeDir.MagnitudeSqr() < 1e-8f) {
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "AddPoint[%d]: SKIP dir+bladeDir both zero pos=(%.2f,%.2f) pointCount=%d",
                 m_FingerId, center.x, center.y, m_PointCount);
#endif
        return;
    }

    // Head half-width (binary AddPoint @0x1e9bf4): param_1 * 9.0 * ModSlashThickness.
    //   param_1 = pressure (caller passes 1.0); ModSlashThickness = g_Scale1 (runtime 1.0).
    // [SLASH-CFG]-confirmed runtime config: g_Scale1=1.0 (Thickness), g_Scale2=0.0 (EndThickness),
    // g_Scale3=1.0 (Length). Port previously used dt*10*1.0 = 0.167 -> below the UpdatePoints
    // shrink rate (45*dt) -> every point retired -> empty blade.
    const float halfWidth = pressure * 9.0f * g_Scale1;

    // ASM-spec v1.6.1 SlashEntity::AddPoint @0x001e9918
    // Ghost-ring bookkeeping: normalize before storing so m_GhostDir averages unit vectors;
    // average PREVIOUS slots only (binary excludes the current write);
    // m_GhostIndex is always 0..5 (binary invariant).
    {
        int slot = (int)m_GhostIndex;   // 0..5 invariant maintained below

        if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f) {
            // dir zero: binary substitutes m_BladeDir for the rest of AddPoint
            // so that downstream m_BladeDir=dir keeps the previous direction.
            // dir is by value (binary ABI), so this write never propagates to the caller.
            dir = m_BladeDir;
            m_GhostDirRing[slot] = _Vector3<float>(0.0f, 0.0f, 0.0f);
        } else if (m_GhostCount == 0) {
            // First stroke point: write zero to ring (binary special case).
            m_GhostDirRing[slot] = _Vector3<float>(0.0f, 0.0f, 0.0f);
        } else {
            // Normal: copy dir then normalize in-place (binary _Vector3::Normalise @0x00138ce8).
            m_GhostDirRing[slot] = dir;
            m_GhostDirRing[slot].Normalise();
        }

        // Average PREVIOUS slots only, excluding the just-written current slot.
        m_GhostDir = _Vector3<float>(0.0f, 0.0f, 0.0f);
        if (m_GhostCount > 1) {
            for (int i = 1; i < (int)m_GhostCount; ++i) {
                int prevSlot = (int)(((unsigned int)m_GhostIndex + 18u - (unsigned int)i) % 6u);
                m_GhostDir.x += m_GhostDirRing[prevSlot].x;
                m_GhostDir.y += m_GhostDirRing[prevSlot].y;
                m_GhostDir.z += m_GhostDirRing[prevSlot].z;
            }
            float denom = (float)(m_GhostCount - 1);
            m_GhostDir.x /= denom;
            m_GhostDir.y /= denom;
            m_GhostDir.z /= denom;
            // Binary: MagnitudeSqr(m_GhostDir) > 1.69f (DAT_001e9ea8=1.69, DAT_001e9eac=0.095).
            // With normalized ring entries this never fires; port faithfully --
            // removes the false-firing that occurred with raw distance vectors.
            if (m_GhostDir.MagnitudeSqr() > 1.69f) {
                m_ComboTimer = 0.095f;
            }
        }

        // Update ring state (binary order: count capped then index mod 6).
        if (m_GhostCount < 6) m_GhostCount++;
        m_GhostIndex = (m_GhostIndex + 1u) % 6u;
    }

    // Update blade direction and angle index.
    m_BladeDir = dir;
    short angle = Math::Atan2Idx(-dir.x, dir.y);
    m_AngleIndex = angle;
    m_Angle      = (uint16_t)angle;

    // Miter direction from angle table: Cross(dir, +Z) perpendicular.
    float halfX = CosIdx((uint16_t)angle) * halfWidth;
    float halfY = SinIdx((uint16_t)angle) * halfWidth;

    // Scroll cap: if m_SplitPoint-2 <= m_PointCount, slide down by one pair
    // and set m_PointCount = m_SplitPoint-4 (binary @ 0x1e9bf4).
    if (m_SplitPoint - 2 <= m_PointCount) {
        const int newCount = m_SplitPoint - 4;
        if (newCount > 0) {
            memmove(m_pLeftBuffer,  m_pLeftBuffer  + 2, newCount * sizeof(QUADCUSTOMVERTEX));
            memmove(m_pRightBuffer, m_pRightBuffer + 2, newCount * sizeof(QUADCUSTOMVERTEX));
        }
        m_PointCount = (newCount > 0) ? newCount : 0;
        m_TrailShiftA -= 2;
    }

    // Reset head-thickness scale to 1.0 each AddPoint (binary @ 0x1e9bf4).
    m_HeadThickScale = 1.0f;

    const uint32_t col = m_BaseColour.PlatformColour();
    const int centerIdx = m_PointCount;
    const int edgeIdx   = m_PointCount + 1;

    // Center vertex (spine): written identically to both buffers.
    m_pLeftBuffer[centerIdx].x      = center.x;
    m_pLeftBuffer[centerIdx].y      = center.y;
    m_pLeftBuffer[centerIdx].z      = center.z;
    m_pLeftBuffer[centerIdx].nx     = 0.0f;
    m_pLeftBuffer[centerIdx].ny     = 0.0f;
    m_pLeftBuffer[centerIdx].nz     = 1.0f;
    m_pLeftBuffer[centerIdx].colour = col;
    m_pLeftBuffer[centerIdx].u      = 0.5f;
    m_pLeftBuffer[centerIdx].v      = 0.5f;

    m_pRightBuffer[centerIdx].x      = center.x;
    m_pRightBuffer[centerIdx].y      = center.y;
    m_pRightBuffer[centerIdx].z      = center.z;
    m_pRightBuffer[centerIdx].nx     = 0.0f;
    m_pRightBuffer[centerIdx].ny     = 0.0f;
    m_pRightBuffer[centerIdx].nz     = 1.0f;
    m_pRightBuffer[centerIdx].colour = col;
    m_pRightBuffer[centerIdx].u      = 0.5f;
    m_pRightBuffer[centerIdx].v      = 0.5f;

    // Edge vertex: center +/- unitPerp*halfWidth.
    // V maps ACROSS ribbon width: left-edge=0.0, center=0.5, right-edge=1.0.
    // blade.tex body is opaque by design; only the last few texel rows fade.
    m_pLeftBuffer[edgeIdx].x      = center.x - halfX;
    m_pLeftBuffer[edgeIdx].y      = center.y - halfY;
    m_pLeftBuffer[edgeIdx].z      = center.z;
    m_pLeftBuffer[edgeIdx].nx     = 0.0f;
    m_pLeftBuffer[edgeIdx].ny     = 0.0f;
    m_pLeftBuffer[edgeIdx].nz     = 1.0f;
    m_pLeftBuffer[edgeIdx].colour = col;
    m_pLeftBuffer[edgeIdx].u      = 0.5f;
    m_pLeftBuffer[edgeIdx].v      = 0.0f;

    m_pRightBuffer[edgeIdx].x      = center.x + halfX;
    m_pRightBuffer[edgeIdx].y      = center.y + halfY;
    m_pRightBuffer[edgeIdx].z      = center.z;
    m_pRightBuffer[edgeIdx].nx     = 0.0f;
    m_pRightBuffer[edgeIdx].ny     = 0.0f;
    m_pRightBuffer[edgeIdx].nz     = 1.0f;
    m_pRightBuffer[edgeIdx].colour = col;
    m_pRightBuffer[edgeIdx].u      = 0.5f;
    m_pRightBuffer[edgeIdx].v      = 1.0f;

    m_PointCount += 2;

#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "AddPoint[%d]: ADDED center=(%.2f,%.2f) halfW=%.3f -> pointCount=%d",
              m_FingerId, center.x, center.y, halfWidth, m_PointCount);
#endif

    m_PrevHeadPos = m_HeadPos;
    m_HeadPos     = center;
}

// Global slash-active frame counter (binary BSS; incremented each frame
// a head-cap vertex is emitted by UpdatePoints @ 0x1e6914).
static int s_slashes = 0;

// ---------------------------------------------------------------------------
// UpdatePoints -- v1.6.1 @ 0x1e6914
// ASM-verified: 2026-06-15T00:00 v1.6.1 UpdatePoints @ 0x1e6914 (user Ghidra decompile)
//
// Mod globals (decompiler name -> port global):
//   ModSlashThickness            -> g_Scale1  (SetModScales p2; default 1.0 @ 0x2D8D78)
//   ModSlashEndThickness         -> g_Scale2  (SetModScales p3; default 0.0 @ 0x332BCC)
//   ModSlashLength               -> g_Scale3  (SetModScales p1; default 1.0 @ 0x2D8D74)
//   ModSlashUVNormalLength       -> g_Scale4  (uvLen;      1.0 ctor default)
//   ModSlashUVFlipWhenUpsideDown -> g_ScaleFlag2 (loop;    1 ctor default)
//   ModSlashPointUVsTaper        -> g_ScaleFlag1 (flipUD;  0 ctor default)
//   ModColourType                -> g_ColourType
//   ModColourTime                -> g_PaletteProgress
//
// Binary field mapping (port name -> binary offset):
//   m_TrailShiftA +0x138 (Init: -1)
//   m_TrailShiftB +0x13c (Init: -1)
//   m_BombHitEdge +0x4c  (bomb-hit one-shot)
//   m_BladeActive +0x140 (uchar shift-register; port stores int, low byte used)
//   DAT_002d928c  -> 0.0f (BSS zero-init: default m_BladeDir.y on reset)
//   DAT_002d9290  -> 0.0f (BSS zero-init: default m_BladeDir.z on reset)
//
// VectorSignedToFloat(v, rounding) -> (float)v (ARM VCVT.F32.S32).
//
// Buffer layout per pair k (k=0,2,4,...):
//   m_pLeftBuffer[k]   = center vertex (spine)
//   m_pLeftBuffer[k+1] = left edge vertex (center - perp*halfWidth)
//   m_pRightBuffer[k]  = center vertex (spine, same pos as left)
//   m_pRightBuffer[k+1]= right edge vertex (center + perp*halfWidth)
//   Pair byte stride = 0x48 = 2 * sizeof(QUADCUSTOMVERTEX).
// ---------------------------------------------------------------------------
void SlashEntity::UpdatePoints(float dt) {
    // -----------------------------------------------------------------------
    // TOP GATE:
    //   if (dt != 0.0f) OR (game_work.flM_BombHitTimer <= 0.0f): run main body.
    //   else (dt == 0 AND BombHitTimer > 0): bomb-flash path.
    // Binary FPSCR: uVar29[30] = (dt==0.0); gate = NOT(dt==0 AND BombHitTimer>0).
    // -----------------------------------------------------------------------
    const bool dtIsZero = (dt == 0.0f);
    const bool bombHitPositive = (game_work.m_BombHitTimer > 0.0f);

    if (dtIsZero && bombHitPositive) {
        // BOMB-FLASH PATH: if m_BombHitEdge (m_BombHitEdge) set, paint all verts red.
        // Binary: Colour::Colour(&CStack_3c, (Colour*)&Colour::Red); two-pass loop
        // (pSVar24 iterates over this and this+4, hitting m_pLeftBuffer and m_pRightBuffer
        // via the 4-byte struct pointer shift trick).
        if (m_BombHitEdge != 0) {
            uint32_t redPacked = Colour::Red.PlatformColour();
            // Binary: for i = 0..m_PointCount inclusive, stride iVar14 += 0x24.
            // Paints both the center and edge verts of each pair PLUS the head-cap slot.
            for (int i = 0; i <= m_PointCount; i++) {
                if (m_pLeftBuffer)  m_pLeftBuffer[i].colour  = redPacked;
                if (m_pRightBuffer) m_pRightBuffer[i].colour = redPacked;
            }
        }
        m_SegLenSq = -1.0f;
        return;
    }

    // -----------------------------------------------------------------------
    // MAIN BODY: copy m_AngleIndex into Entity base angle bytes.
    // Binary: b_pad_36 = low byte, b_pad_37 = high byte of m_AngleIndex.
    // -----------------------------------------------------------------------
    m_Angle = (uint16_t)m_AngleIndex;

    // -----------------------------------------------------------------------
    // COLLISION LINE UPDATE (gated by PointCount, BladeActive, TrailShifts).
    // Binary: if any gate fires -> reset TrailShifts + SegLenSq.
    // else -> FruitCamera::TranslatePos(m_HeadPos) + TranslatePos(m_TailPos),
    //   midPt = (headT + tailT) * 0.5, write ColLine endpoints, compute SegLenSq.
    // -----------------------------------------------------------------------
    const bool bladeActive = (m_BladeActive != 0);
    if (m_PointCount < 4 || !bladeActive || m_TrailShiftA == -1 || m_TrailShiftB == -1) {
        m_TrailShiftA = -1;
        m_TrailShiftB = -1;
        m_SegLenSq    = -1.0f;
    } else {
        // ASM-spec v1.6.1 SlashEntity::UpdatePoints @0x001e6914: both endpoints go
        // through FruitCamera::TranslatePos(pos, inverse=true, useZeroCenter=false)
        // (calls @0x001e6a44 head +0x7c / @0x001e6a84 tail +0x70; r3=1, [sp]=0).
        // ColLine.a = midpoint of the TRANSLATED endpoints, ColLine.b = TRANSLATED
        // tail, and m_SegLenSq uses the translated mid-tail delta.
        FruitCamera* cam = game_work.m_FruitCamera;
        _Vector3<float> headT = cam ? cam->TranslatePos(m_HeadPos, true, false) : m_HeadPos;
        _Vector3<float> tailT = cam ? cam->TranslatePos(m_TailPos, true, false) : m_TailPos;

        float midX = (headT.x + tailT.x) * 0.5f;
        float midY = (headT.y + tailT.y) * 0.5f;
        float midZ = (headT.z + tailT.z) * 0.5f;

        ColLine* pLine = static_cast<ColLine*>(m_Col);
        if (pLine) {
            pLine->a().x = midX;
            pLine->a().y = midY;
            pLine->a().z = midZ;
            pLine->b.x   = tailT.x;
            pLine->b.y   = tailT.y;
            pLine->b.z   = tailT.z;
        }

        float dx = midX - tailT.x;
        float dy = midY - tailT.y;
        m_SegLenSq = dx * dx + dy * dy;
    }

    // -----------------------------------------------------------------------
    // m_BladeDir RESET when PointCount < 4.
    // DAT_002d928c = 0.0f, DAT_002d9290 = 0.0f (BSS zero-init).
    // -----------------------------------------------------------------------
    if (m_PointCount < 4) {
        m_BladeDir.x = 0.0f;
        m_BladeDir.y = 0.0f;  // DAT_002d928c
        m_BladeDir.z = 0.0f;  // DAT_002d9290
    }

    // ModColourType==0: reset ModColourTime (g_PaletteProgress) to 0.
    if (g_ColourType == 0) {
        g_PaletteProgress = 0.0f;
    }

    // -----------------------------------------------------------------------
    // REBUILD LOOP
    // Binary: if (m_BladeActive == '\0') || m_PointCount > 2: run loop.
    // Note: binary condition is (bladeActive==0 OR pointCount>2); the loop
    // only does meaningful work when pointCount>2. With bladeActive==0 and
    // pointCount<=2 the loop body never fires (loop range [0..pointCount) is empty).
    // -----------------------------------------------------------------------
    // arc length accumulator (local_310[100])
    float arcLen[100];
    arcLen[0] = 0.0f;

    if (!bladeActive || m_PointCount > 2) {
        Colour* pBaseColour = &m_BaseColour;
        int local_31c = 0;   // count of RETIRED pairs (stride 2)
        int iVar14    = 0;   // arc-length array index (advances for BODY pairs only)

        for (int local_320 = 0; local_320 < m_PointCount; local_320 += 2) {
            // ------------------------------------------------------------------
            // READ existing stored center and left-edge positions.
            // Binary pointer arithmetic: iVar25 byte offset from buffer[0] / buffer[1].
            // center = m_pLeftBuffer[local_320], edgeL = m_pLeftBuffer[local_320+1].
            // ------------------------------------------------------------------
            float centerX = m_pLeftBuffer[local_320].x;
            float centerY = m_pLeftBuffer[local_320].y;

            // Recover stored half-width: magnitude of (edgeL - center) before normalizing.
            float dx = m_pLeftBuffer[local_320 + 1].x - centerX;
            float dy = m_pLeftBuffer[local_320 + 1].y - centerY;
            float fVar30 = sqrtf(dx * dx + dy * dy);

            // ------------------------------------------------------------------
            // AGE half-width.
            // fVar31 = dt (doubled for pairs past m_SplitPoint * 2).
            // fVar35 = ModSlashEndThickness (SSM: no-op in SP since IsSameScreenMultiplayer()=false).
            // AGE: fVar30 += (fVar31 * -45 * (ModSlashThickness - ModSlashEndThickness)) / ModSlashLength
            // ModSlashLength guard: if 0, use 1 (prevents div-by-zero in default ctor state).
            // ------------------------------------------------------------------
            float fVar31 = dt;
            if (m_SplitPoint * 2 <= local_320) fVar31 = dt + dt;

            // TODO: 0x1e6914 -- SSM flM_PauseAmount adjust for fVar35 not ported (IsSameScreenMultiplayer() is always false in SP).
            // Runtime-confirmed mapping ([SLASH-CFG]): g_Scale1 = ModSlashThickness (1.0),
            // g_Scale2 = ModSlashEndThickness (0.0), g_Scale3 = ModSlashLength (1.0).
            float fVar35 = g_Scale2;  // ModSlashEndThickness

            float modSlashLen = g_Scale3;
            if (modSlashLen == 0.0f) modSlashLen = 1.0f;

            // (ModSlashThickness - ModSlashEndThickness) = (g_Scale1 - g_Scale2).
            fVar30 = fVar30 + (fVar31 * -45.0f * (g_Scale1 - g_Scale2)) / modSlashLen;

            // ------------------------------------------------------------------
            // RETIRE DECISION (binary FPSCR compare chain).
            //
            // The binary's outer if condition enters the "keep/body" block when:
            //   (ModSlashThickness <= ModSlashEndThickness)  [i.e. g_Scale1 <= g_Scale2]
            //   OR
            //   (fVar30 > fVar35 * 9.0f * m_HeadThickScale)  [still wider than max]
            //
            // Inside that block, a secondary goto-retire fires when:
            //   (ModSlashThickness < ModSlashEndThickness) AND (fVar30 < ModSlashEndThickness * 9.0)
            //   (the binary compares against the already-x9 EndThickness value)
            //
            // The else of the outer if = RETIRE directly.
            //
            // Summary:
            //   RETIRE_A = (g_Scale1 > g_Scale2) AND (fVar30 <= maxHW)
            //   RETIRE_B = (g_Scale1 < g_Scale2) AND (fVar30 < g_Scale2 * 9.0)  [inner goto]
            //   KEEP     = everything else
            // ------------------------------------------------------------------
            float maxHW = fVar35 * 9.0f * m_HeadThickScale;

            // Mapping: Thickness=g_Scale1 (1.0), EndThickness=g_Scale2 (0.0) -- runtime-confirmed.
            bool retire = false;
            if (g_Scale1 > g_Scale2) {
                // Thickness > EndThickness -> RETIRE_A when no longer wider than maxHW.
                if (fVar30 <= maxHW) retire = true;
            } else {
                // Thickness <= EndThickness.
                // Inner goto-retire (RETIRE_B): only when strictly less-than.
                // Binary threshold = EndThickness * 9.0 (pre-multiplied before the compare).
                if (g_Scale1 < g_Scale2 && fVar30 < g_Scale2 * 9.0f) retire = true;
            }

            if (retire) {
                // LAB_001e6d04: count this pair as retired, advance byte offset.
                local_31c += 2;
                continue;
            }

            // ------------------------------------------------------------------
            // WRITE DESTINATION INDICES (compact retired pairs from front).
            // ------------------------------------------------------------------
            int dstCtr  = local_320 - local_31c;       // destination center index
            int dstEdge = dstCtr + 1;                  // destination edge index

            // ------------------------------------------------------------------
            // HEAD-CAP vs BODY discriminator.
            // Binary: (m_PointCount < 3) || (m_PointCount <= local_320 + 3) -> HEAD-CAP.
            // ------------------------------------------------------------------
            if (m_PointCount < 3 || m_PointCount <= local_320 + 3) {
                // ==============================================================
                // HEAD-CAP PATH: perp from angle look-up table.
                // perpVec = (CosIdx(m_Angle), SinIdx(m_Angle), 0) * fVar30.
                // ==============================================================
                uint16_t ang = (uint16_t)m_AngleIndex;
                float cosA = Math::CosIdx(ang) * fVar30;
                float sinA = Math::SinIdx(ang) * fVar30;

                // UV.x = 0.98 for both slots.
                m_pLeftBuffer[dstCtr].u  = 0.98f;
                m_pLeftBuffer[dstEdge].u = 0.98f;

                // Inner do/while runs twice: iter=0 -> left side, iter=1 -> right side.
                // pSVar24 advances by 4 bytes each iteration to toggle which buffer is
                // written: iter0 writes m_pLeftBuffer, iter1 writes m_pRightBuffer.
                uint32_t headCol = pBaseColour->PlatformColour();
                for (int iter = 0; iter < 2; iter++) {
                    QUADCUSTOMVERTEX* buf = (iter == 0) ? m_pLeftBuffer : m_pRightBuffer;

                    // UV.y: ModSlashUVFlipWhenUpsideDown (g_ScaleFlag2) controls V assignment.
                    // Binary SP path (IsSameScreenMultiplayer() == false):
                    //   if g_ScaleFlag2 == 0: v=0 (iter==0), v=1 (iter==1).
                    //   else: check perp.y < 0 -> flip assignment.
                    //     perp.y >= 0: v=1 (iter==0), v=0 (iter==1)  [edge verts]
                    //     perp.y <  0: v=0 (iter==0), v=1 (iter==1)
                    // (TODO: SSM branch flips based on centerX sign -- not ported.)
                    float vVal = 0.0f;
                    if (g_ScaleFlag2 == 0) {
                        vVal = (iter != 0) ? 1.0f : 0.0f;
                    } else {
                        bool perpYNeg = (sinA < 0.0f);
                        if (perpYNeg) {
                            vVal = (iter != 0) ? 1.0f : 0.0f;
                        } else {
                            vVal = (iter == 0) ? 1.0f : 0.0f;
                        }
                    }

                    float ePosX = (iter == 0) ? (centerX - cosA) : (centerX + cosA);
                    float ePosY = (iter == 0) ? (centerY - sinA) : (centerY + sinA);
                    buf[dstCtr].x  = centerX;
                    buf[dstCtr].y  = centerY;
                    buf[dstEdge].x = ePosX;
                    buf[dstEdge].y = ePosY;
                    buf[dstEdge].v = vVal;

                    buf[dstCtr].colour  = headCol;
                    buf[dstEdge].colour = headCol;
                }
                // iVar14 (arc index) stays unchanged for head-cap pairs (no arc accumulation).

            } else {
                // ==============================================================
                // BODY PATH: perp from Cross(Normalise(nextCenter - center), +Z).
                // ==============================================================
                float nextCx = m_pLeftBuffer[local_320 + 2].x;
                float nextCy = m_pLeftBuffer[local_320 + 2].y;

                float segDx = nextCx - centerX;
                float segDy = nextCy - centerY;
                float segLen = sqrtf(segDx * segDx + segDy * segDy);

                // Cross(unit, Z_hat) = (unit.y, -unit.x, 0).
                float invSegLen = 1.0f / segLen;
                float perpX = segDy * invSegLen * fVar30;
                float perpY = -segDx * invSegLen * fVar30;

                // Accumulate arc length for this segment.
                float prevArc = arcLen[iVar14];

                // ModColourType==0: UpdateModColour + blend toward CRITICAL_COLOUR.
                // Decompiler: UpdateModColour(&m_HighlightColour, pCVar15, -2.0/fVar32)
                // where fVar32 = VectorSignedToFloat(m_PointCount) = (float)m_PointCount.
                float pointCountF = (float)m_PointCount;
                if (g_ColourType == 0) {
                    UpdateModColour(&m_HighlightColour, -2.0f / pointCountF);

                    float blendScale = m_Scale;
                    if (blendScale <= 0.0f) {
                        // No hit-flash: m_BaseColour = copy of current (i.e. keep existing).
                        // Binary: Colour::operator=(&CStack_44, this_00) where this_00=pBaseColour.
                        // This writes m_BaseColour = *pBaseColour (i.e. identity, already there).
                    } else {
                        // Hit-flash blend: toward Fruit::CRITICAL_COLOUR.
                        float t = 1.0f - blendScale;
                        float cr = (float)(int)Fruit::CRITICAL_COLOUR.r;
                        float cg = (float)(int)Fruit::CRITICAL_COLOUR.g;
                        float cb = (float)(int)Fruit::CRITICAL_COLOUR.b;
                        float hr = (float)(int)m_HighlightColour.r;
                        float hg = (float)(int)m_HighlightColour.g;
                        float hb = (float)(int)m_HighlightColour.b;

                        float nr = cr + (hr - cr) * t;
                        float ng = cg + (hg - cg) * t;
                        float nb = cb + (hb - cb) * t;

                        m_BaseColour.r = (nr > 0.0f) ? (uint8_t)(int)nr : 0;
                        m_BaseColour.g = (ng > 0.0f) ? (uint8_t)(int)ng : 0;
                        m_BaseColour.b = (nb > 0.0f) ? (uint8_t)(int)nb : 0;
                        m_BaseColour.a = 0xff;
                    }
                }

                // UV.x = (VectorSignedToFloat(local_320) / VectorSignedToFloat(m_PointCount)) * 0.98
                float uVal = ((float)local_320 / pointCountF) * 0.98f;

                // Inner do/while runs twice: iter=0 -> left buffer, iter=1 -> right buffer.
                uint32_t bodyCol = pBaseColour->PlatformColour();
                for (int iter = 0; iter < 2; iter++) {
                    QUADCUSTOMVERTEX* buf = (iter == 0) ? m_pLeftBuffer : m_pRightBuffer;

                    // UV.y: same flip logic as head-cap path.
                    // Binary uses local_e4 (= perpDir * fVar30 = perp) for the sign check.
                    float vVal = 0.0f;
                    if (g_ScaleFlag2 == 0) {
                        vVal = (iter != 0) ? 1.0f : 0.0f;
                    } else {
                        bool perpYNeg = (perpY < 0.0f);
                        if (perpYNeg) {
                            vVal = (iter != 0) ? 1.0f : 0.0f;
                        } else {
                            vVal = (iter == 0) ? 1.0f : 0.0f;
                        }
                    }

                    float ePosX = (iter == 0) ? (centerX - perpX) : (centerX + perpX);
                    float ePosY = (iter == 0) ? (centerY - perpY) : (centerY + perpY);
                    buf[dstCtr].x  = centerX;
                    buf[dstCtr].y  = centerY;
                    buf[dstEdge].x = ePosX;
                    buf[dstEdge].y = ePosY;
                    buf[dstEdge].v = vVal;

                    // UV.x written after the inner loop in binary (via iVar27 byte offset).
                    // Write now to both slots -- arc-length remap will overwrite after the loop.
                    buf[dstCtr].u  = uVal;
                    buf[dstEdge].u = uVal;

                    buf[dstCtr].colour  = bodyCol;
                    buf[dstEdge].colour = bodyCol;
                }

                // Accumulate arc for this segment.
                arcLen[iVar14 + 1] = prevArc + segLen;
                iVar14++;
            }
        } // end rebuild loop

        // -----------------------------------------------------------------------
        // ARC-LENGTH U REMAP
        // Binary: if (ModSlashUVNormalLength > 0): normFactor = arcLen[iVar14] / uvLen,
        //                                          fVar35 (normFactor) = arcLen[last] / uvLen.
        //         else: fVar35 = 1.0.
        // Then for each pair i (step 2), arcIdx = min(i/2, iVar14):
        //   U = 0.98 - (1 - (arcLen[arcIdx]*0.98/arcLen[iVar14])) * normFactor
        // Apply to BOTH left and right buffers (binary outer do/while iterates twice).
        // -----------------------------------------------------------------------
        // ASM-spec v1.6.1 SlashEntity::UpdatePoints @0x001e72d8: the remap divisor is
        // ModSlashUVNormalLength (port g_Scale5, default 0.0 from SlashModInfo ctor), NOT
        // ModSlashPointScale (g_Scale4). g_Scale5<=0 -> normFactor=1.0 (the default path for
        // items with no <scales>), giving U in ~-0.02..0.96. Using g_Scale4 (default 1.0)
        // took the >0 branch -> normFactor=arcTotal (~384) -> U~-383, wrapping the texture
        // (GL_REPEAT) so the flame blade sampled its dark edge and rendered black.
        float normFactor;
        if (g_Scale5 > 0.0f) {
            normFactor = (iVar14 > 0) ? (arcLen[iVar14] / g_Scale5) : 1.0f;
        } else {
            normFactor = 1.0f;
        }

        for (int bufPass = 0; bufPass < 2; bufPass++) {
            QUADCUSTOMVERTEX* buf = (bufPass == 0) ? m_pLeftBuffer : m_pRightBuffer;
            // byteOff always equals i * sizeof(QUADCUSTOMVERTEX) because both
            // advance in lockstep (i by 2, byteOff by 2*0x24=0x48). Use i directly.
            for (int i = 0; i < m_PointCount; i += 2) {
                int arcIdx = i / 2;
                if (arcIdx > iVar14) arcIdx = iVar14;
                float arcTotal = (iVar14 > 0) ? arcLen[iVar14] : 1.0f;
                float uRemap = 0.98f - (1.0f - (arcLen[arcIdx] * 0.98f) / arcTotal) * normFactor;
                buf[i].u     = uRemap;
                buf[i + 1].u = uRemap;
            }
        }

        // -----------------------------------------------------------------------
        // COMPACT: subtract retired count from m_PointCount.
        // Binary: iVar13 = iVar13 - local_31c; this->m_PointCount = iVar13.
        // -----------------------------------------------------------------------
        m_PointCount = m_PointCount - local_31c;

        // -----------------------------------------------------------------------
        // HEAD-CAP SPIKE APPEND (binary: if m_PointCount > 2).
        // Appends a single spike vertex at [m_PointCount] (one-past the last pair).
        //
        //   lastEdge = m_pLeftBuffer[m_PointCount-1]   (edge slot of head pair)
        //   prevCtr  = m_pLeftBuffer[m_PointCount-2]   (center slot of head pair)
        //   dir      = Normalise(lastEdge - prevCtr), then Cross(dir, Z) * 2.5
        //   capPos   = prevCtr - capOffset             (same for left and right)
        //   U = 1.0; V from ModSlashPointUVsTaper (g_ScaleFlag1) or 0.5.
        //   slashes += 1.
        // -----------------------------------------------------------------------
        if (m_PointCount > 2) {
            s_slashes++;

            float hdx = m_pLeftBuffer[m_PointCount - 1].x - m_pLeftBuffer[m_PointCount - 2].x;
            float hdy = m_pLeftBuffer[m_PointCount - 1].y - m_pLeftBuffer[m_PointCount - 2].y;
            float hMag = sqrtf(hdx * hdx + hdy * hdy);

            // Cross(unit(hDir), Z_hat) * 2.5 -- binary local_48 = 2.5f.
            // unit = (hdx/hMag, hdy/hMag, 0). Cross(unit, Z) = (unit.y, -unit.x, 0).
            // cap = unitDirCross * 2.5.
            static const float kCapScale = 2.5f;
            float capOffX = (hdy / hMag) * kCapScale;
            float capOffY = (-hdx / hMag) * kCapScale;

            // capPos = prevCtr - capOff (both buffers get the same position).
            float capX = m_pLeftBuffer[m_PointCount - 2].x - capOffX;
            float capY = m_pLeftBuffer[m_PointCount - 2].y - capOffY;

            m_pLeftBuffer[m_PointCount].x  = capX;
            m_pLeftBuffer[m_PointCount].y  = capY;
            m_pRightBuffer[m_PointCount].x = capX;
            m_pRightBuffer[m_PointCount].y = capY;

            // V: if ModSlashPointUVsTaper (g_ScaleFlag1), copy from last edge slot;
            //    else 0.5 for center.
            float leftV, rightV;
            if (g_ScaleFlag1 != 0) {
                leftV  = m_pLeftBuffer[m_PointCount - 1].v;
                rightV = m_pRightBuffer[m_PointCount - 1].v;
            } else {
                leftV  = 0.5f;
                rightV = 0.5f;
            }
            m_pLeftBuffer[m_PointCount].v  = leftV;
            m_pRightBuffer[m_PointCount].v = rightV;

            // U = 1.0.
            m_pLeftBuffer[m_PointCount].u  = 1.0f;
            m_pRightBuffer[m_PointCount].u = 1.0f;

            // Colour.
            uint32_t col = pBaseColour->PlatformColour();
            m_pLeftBuffer[m_PointCount].colour  = col;
            m_pRightBuffer[m_PointCount].colour = col;
        }
    }
}

// ---------------------------------------------------------------------------
// ASM-spec v1.6.1 SlashEntity::Update @0x001e867c
// Binary-faithful port with dt branching, blade velocity volume, ghost spawn
// timing, combo timer, velocity repulsion/attraction, and swipe-SFX logic.
// The sub-block notes below cite their own addresses inside this symbol; the
// combo-resolve tail is specced separately at @0x001e90d8. No compile+diff has
// been run on this body, so nothing here carries an ASM-verified stamp.
// ---------------------------------------------------------------------------
void SlashEntity::Update(float dt) {
    // =====================================================================
    // 1. DT BRANCHING
    //    If dt == 0: use game_work.dt instead (frozen-frame fallback).
    //    comboDt = dt * (gameMode == COMBO ? 0.666f * PowerUpManager::m_DtMod : 1.0f)
    //    Only the combo timer uses comboDt; everything else uses localDt.
    // =====================================================================
    float localDt;
    float comboDt;
    if (dt == 0.0f) {
        localDt = dt;
        comboDt = 0.0f;
    } else {
        localDt = game_work.dt;
        comboDt = game_work.dt;
        if (game_work.gameMode == GAME_MODE_COMBO) {
            // ASM-spec v1.6.1 SlashEntity::Update @0x001e86dc: the binary calls
            // PowerUpManager::GetInstance (bl 0x0010aca0) and reads m_DtMod (+0x64)
            // off the result with no null test -- twice, once per use.
            comboDt = game_work.dt * 0.666f;
            if (PowerUpManager::GetInstance()->m_DtMod < 0.9f) {
                comboDt = comboDt * PowerUpManager::GetInstance()->m_DtMod;
            }
        }
    }

    // =====================================================================
    // 2. TRAIL EMITTER MANAGEMENT
    //    Binary gate: m_BladeActive != 0.
    // =====================================================================
    {
        PSPParticleManager& pm = PSPParticleManager::GetInstance();
        const bool bladeActiveByte = (m_BladeActive != 0);
        const bool wantTrail = bladeActiveByte && g_DirectionalFlag != 0 && g_TrailHash != 0;
        if (wantTrail) {
            if (!m_TrailEmitter) {
                // ASM-spec v1.6.1 SlashEntity::Update @0x001e8b54: r3=1.
                m_TrailEmitter = pm.AddEmitter(g_TrailHash, &m_TrailEmitter,
                                               /*updateWhenPaused=*/true);
                if (m_TrailEmitter) {
                    m_TrailEmitter->m_bUpdateWhenPaused = true;
                }
            }
            if (m_TrailEmitter) {
#if !defined(__bada__)
                // Port specific: binary reads live touch from InputEvent; port caches in m_RawTouchPos.
                m_TrailEmitter->m_Pos = m_RawTouchPos;
#else
                // Binary source is Entity::pos, written by TouchMoveX/Y.
                m_TrailEmitter->m_Pos = pos;
#endif
            }
        } else if (!bladeActiveByte && m_TrailEmitter) {
            pm.ClearEmitter(m_TrailEmitter);
            m_TrailEmitter = nullptr;
        }
    }

    // =====================================================================
    // 3. UPDATEPOINTS (uses localDt, not dt)
    // =====================================================================
    UpdatePoints(localDt);

    // =====================================================================
    // 5. BLADE VELOCITY -> ITEMMANAGER SWISH LOOP VOLUME
    //    Binary @ 0x1e8810: if m_BladeActive: compute |m_BladeDir|/15,
    //    clamp to [0.5, 1.0], push to the global loop-volume cap.
    // =====================================================================
    // DIFFERS: binary pushes to ItemManager::maxLoopVolume; port would need
    // that global. For now the volume calculation is computed but not pushed
    // (the ItemManager's swish-loop volume control is not yet ported).
    if (m_BladeActive != 0) {
        float bladeMag = m_BladeDir.Magnitude();
#if !defined(__bada__)
        // Port specific: light EMA smoothing of the raw blade speed so a
        // jittery Magic-Remote/mouse read doesn't flicker the motion-mode
        // cut gate (see the cut-decision block below). k=0.4 -- fast enough
        // to follow a real flick, slow enough to average out per-tick noise.
        m_SmoothedSpeed += (bladeMag - m_SmoothedSpeed) * 0.4f;
#endif
        float volScale = 1.0f;
        float rawScale = bladeMag / 15.0f;
        if (rawScale >= 0.5f && rawScale <= 1.0f) {
            volScale = rawScale;
        }
        float loopVol = volScale * 0.5f + 0.5f;
        // TODO: v1.6.1 @ 0x1e8810 -- push loopVol to ItemManager::maxLoopVolume
        // Not ported; binary does if (loopVol < maxLoopVol) maxLoopVol = loopVol.
        (void)loopVol;
    }

    // =====================================================================
    // 6. GHOST SPAWN TIMER
    //    Binary @ 0x1e87ac: if m_GhostSpawnPending, accumulate localDt;
    //    if >= 0.05, CreateGhost + clear. Else set timer to 0.
    // =====================================================================
    if (m_GhostSpawnPending != 0) {
        m_GhostSpawnTimer += localDt;
        if (m_GhostSpawnTimer >= 0.05f) {
            CreateGhost();
            m_GhostSpawnPending = 0;
        }
    } else {
        m_GhostSpawnTimer = 0.0f;
    }

    // =====================================================================
    // 7. HEADTHICKSCALE GATE + SLICE-LOOP GUARD
    //    Binary @ 0x1e87ac: if m_PointCount < 4 OR ModPowerMask bit 6,
    //    set m_HeadThickScale = 0 AND skip the slice-test iterator blocks.
    //    Inside the else: first check bomb timer, then run fruit+bomb iter.
    // =====================================================================
    // v1.6.1 SlashEntity::Update @0x001e867c calls Game::GetInstance nowhere in the
    // whole body -- its only GetInstance calls are PowerUpManager / ActorManager /
    // PSPParticleManager / WaveManager / BonusManager / AchievementManager. The
    // port-added `game != nullptr` term on this gate had no binary counterpart and
    // is removed; the binary's gate is the bomb-hit timer alone.
    // Bomb-timer gate @0x001e87d4:
    //   vldr.32 s15,[r3,#0x10] / vcmpe.f32 s15,#0 / vmrs apsr,fpscr / bhi 0x001e8f4c.
    //   BHI is "ordered and greater than", so the slice pass runs on
    //   m_BombHitTimer <= 0.0f. The port had ==, which drops the pass on the one
    //   tick where the timer is already negative and GameInit's countdown has not
    //   yet clamped it back to 0 (see GameInit.cpp, "if (m_BombHitTimer < 0) = 0").
    if (m_PointCount < 4 || (s_ModPowerMask & 0x40u) != 0) {
        m_HeadThickScale = 0.0f;
    } else if (game_work.m_BombHitTimer <= 0.0f) {
        // Slice-test pass -- fruit (type 0) and bomb (type 1).
        // Binary uses the low-level ActorManager iterator API:
        //   ActorManager::GetEntityFirst(actorMgr, type, &iter)
        //   ActorManager::GetEntityNext(actorMgr, type, &iter)
        // Both loops guarded by g_Stop == 0 (re-checked each iteration).
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();

        // Port specific: FN::g_MotionMode velocity gate -- the pointer blade
        // tracks the cursor every tick (see InputTranslatorSDL /
        // InputTranslatorWii), but a cut only registers once its smoothed
        // speed clears FN::g_MotionSpeedThreshold: slow movement aims, a fast
        // flick cuts. FN::MOTION_GATE_CHANNEL_MIN/MAX (DebugFlags.h) is the
        // single inclusive channel range gated -- SDL/host: just
        // POINTER_FINGER_CHANNEL (15); Wii: [0, WII_POINTER_CHANNEL_LAST]
        // (0-3 A-press channels included so A is menu-click-only in motion
        // mode -- holding A must never produce an ungated slice -- plus the
        // 12-15 hover channels; 4-11 are unused, gating them is harmless).
        // Touch blades outside the range and motion-OFF are never gated --
        // runCut stays true and this block is byte-identical to the
        // pre-motion-mode fruit+bomb loops below.
        bool runCut = true;
#if !defined(__bada__)
        if (FN::g_MotionMode
            && m_FingerId >= FN::MOTION_GATE_CHANNEL_MIN && m_FingerId <= FN::MOTION_GATE_CHANNEL_MAX
            && m_SmoothedSpeed < FN::g_MotionSpeedThreshold) {
            runCut = false;
        }
#endif

        if (am && runCut) {
            // -----------------------------------------------------------------
            // 8. FRUIT LOOP (type 0)
            // -----------------------------------------------------------------
            // The STOP mechanism: when a bomb-special or extra-score fruit is
            // hit, binary sets g_StopCounter to 0 and g_Stop (byte) to 1,
            // which is checked at the head of each iter advance.

            {
                std::list<Mortar::Entity*>::const_iterator it;
                const std::list<Mortar::Entity*>& fruitList = am->GetTypeList(0);
                for (it = fruitList.begin(); it != fruitList.end() && g_Stop == 0; ++it) {
                    Fruit* fruit = static_cast<Fruit*>(*it);
                    if (!fruit) continue;
                    if (fruit->Sliced()) continue;
                    if (!fruit->IsActive()) continue;
                    if (fruit->flags & 0x01) continue; // v1.6.1 @0x001e8830: skip ENT_INACTIVE (MenuButton grow-in)

                    bool hit = CollideWithEntity(fruit);

                    if (hit) {
                        // --- HIT PATH ---
                        m_SliceBladeDir = m_BladeDir;
                        m_SliceFruitPos = fruit->pos;
                        m_SliceFruitType = (int)fruit->m_FruitType;
                        m_SplatTimer     = 0.0f;
                        m_SplatInterval  = 0.0f;
                        if (/* TODO: ModHasSlashFlash check */ 1) {
                            m_GhostSpawnPending = 1;
                        }
                        m_PendingSplats += 2;

                        // --- COMBO LOGIC ---
                        const bool isMenuFruit = (fruit->m_bMenuFling != 0);
                        if (!isMenuFruit) {
                            // m_pLastComboFruit guard: skip if same fruit as last
                            // OR if m_bExtraScore / m_OnlineSliceMode == 2
                            // m_OnlineSliceMode is Fruit struct field @ +0x166 (not yet ported).
                            // Binary reads this from the fruit; defaults to 0 for SP.
                            // DIFFERS: original reads fruit->m_OnlineSliceMode; port assumes 0
                            // because online MP is stubbed.
                            if (!fruit->m_bMenuFling && m_pLastComboFruit != fruit)
                            {
                                m_pLastComboFruit = fruit;
                                m_ComboTimer = 0.0f;
                                m_ComboFruitTypes[m_ComboCounter] = (int)fruit->m_FruitType;
                                m_ComboOnlineMode = 0;
                                // Binary order: stash the old scale, bump the counter, run the
                                // >9 timer check, THEN subtract using the INCREMENTED counter.
                                // The port used to compute the scale from the pre-increment
                                // counter, which shifted every combo step by one.
                                const float oldComboScoreScale = m_ComboScoreScale;
                                m_ComboCounter++;
                                if (m_ComboCounter > 9) {
                                    m_ComboTimer = 0.095f;
                                }
                                {
                                    float r = Math::g_Random.RandF(0.5f);
                                    m_ComboScoreScale = oldComboScoreScale
                                        - (float)m_ComboCounter * (r + 0.75f);
                                }
                                // MissControl popup: binary gate order is
                                // `count>2 -> CombosEnabled() -> (online && mode==2 -> skip) ->
                                // (ModPowerMask & 0x80 -> skip)`. All four terms are pure, so the
                                // order is asm-cosmetic only; it is kept to match the binary.
                                // CombosEnabled @0x00119fd0 is `game_work.gameMode != 1`
                                // (0x0010c410 is only its PLT stub).
                                // IsOnlineMultiplayer @0x0011a09c is a free function in the binary
                                // (`mov r0,#0; bx lr`); the port routes it through the
                                // NetworkManager singleton. Cosmetic call-shape difference.
                                if (m_ComboCounter > 2 && CombosEnabled())
                                {
                                    bool online = Mortar::NetworkManager::GetInstance()->IsOnlineMultiplayer();
                                    if ((!online || m_ComboOnlineMode != 2)
                                        && (s_ModPowerMask & 0x80u) == 0) {
                                        // Outer test is genuine: 'ldr r7,[r4,#0x11c]; cmp r7,#0;
                                        // bne' @0x001e8988. The post-GetFree null test is NOT --
                                        // @0x001e8994 is 'bl GetFree; cpy r10,r0; str r0,[r4,#0x11c]'
                                        // then straight into MakeCombo, unconditional.
                                        if (m_pComboMissControl == nullptr) {
                                            m_pComboMissControl = MissControl::GetFree();
                                            m_pComboMissControl->MakeCombo(
                                                m_SliceFruitPos, m_ComboCounter, m_ComboOnlineMode);
                                            m_pComboMissControl->m_RemoveCallback =
                                                Mortar::Delegate1<void, HUDControl*>::Make(
                                                    this, &SlashEntity::MissControlDeleted);
                                        } else {
                                            m_pComboMissControl->MakeCombo(
                                                m_pComboMissControl->pos,
                                                m_ComboCounter, m_ComboOnlineMode);
                                        }
                                    }
                                }
                            } else {
                                // Same fruit or MP-slice-mode block: bump timer if not already idle
                                if (m_ComboTimer < 0.1f) {
                                    m_ComboTimer = 0.095f;
                                }
                            }
                        } // !isMenuFruit

                        // CollisionResponse on the hit entity
                        // v1.6.1 SlashEntity::Update @0x001e867c: hitter MUST be the SlashEntity (this) --
                        // nullptr made Fruit::CollisionResponse @0x001dd500 take the early-return path and never
                        // reach AddToCurrentScore @0x0011a4c0 -> zero score (#100).
                        // TODO: v1.6.1 -- binary's 4th arg (blade dir) may be null; confirm via asm-inspector before changing.
                        fruit->CollisionResponse(this, 0, 0, &m_SliceBladeDir);

                        // Critical fruit: adjust fruit type and scale
                        if (fruit->m_bCritical) {
                            // Binary adds Fruit::MAX_FRUIT_TYPES -- the RUNTIME fruit count
                            // written by Fruit::LoadInfo (g_FruitInfoCount, ~22), not a fixed
                            // 0x100. SplatEntity::MakeSplat recovers the colour with
                            // `fruitType % count`, so a 0x100 sentinel wrapped to the wrong
                            // fruit's splat colour on every critical slice.
                            m_SliceFruitType += g_FruitInfoCount;
                            float r = Math::g_Random.RandF(0.5f);
                            m_ComboScoreScale = m_ComboScoreScale + (r + 0.75f) * -3.0f;
                        }

                        // Extra-score fruit: halt iteration (binary @0x1e8b00, predicate m_bExtraScore==m_bMenuFling +0x164)
                        if (fruit->m_bMenuFling) {
                            g_StopCounter = 0;
                            g_Stop = 1;
                        }

                    } else {
                        // --- NON-HIT: FRUIT VELOCITY REPULSION/ATTRACTION ---
                        // ModPowerMask bit 0 = repel, bit 1 = attract.
                        // Executed in order: bit 2 (attract) preferred over bit 0 (repel).
                        _Vector3<float> dirToFruit(fruit->pos.x - pos.x,
                                                   fruit->pos.y - pos.y,
                                                   fruit->pos.z - pos.z);
                        float dist = dirToFruit.Magnitude();
                        if (dist > 0.001f) dirToFruit = dirToFruit * (1.0f / dist);

                        if (s_ModPowerMask & 2) {
                            // Attract (bit 1)
                            float mag = dist * 0.5f;
                            if (mag < 50.0f) mag = 50.0f;
                            fruit->vel += dirToFruit * mag;
                            float speed = fruit->vel.Magnitude();
                            if (speed < 8.0f) speed = 8.0f;
                            fruit->vel = fruit->vel * (speed / fruit->vel.Magnitude());
                        } else if (s_ModPowerMask & 1) {
                            // Repel (bit 0)
                            float mag = dist * 0.5f;
                            if (mag < 50.0f) mag = 50.0f;
                            fruit->vel -= dirToFruit * mag;
                            float speed = fruit->vel.Magnitude();
                            if (speed < 8.0f) speed = 8.0f;
                            fruit->vel = fruit->vel * (speed / fruit->vel.Magnitude());
                        }
                    }
                }
            }

            // -----------------------------------------------------------------
            // 9. BOMB LOOP (type 1)
            // -----------------------------------------------------------------
            if (g_Stop == 0) {
                std::list<Mortar::Entity*>::const_iterator itBomb;
                const std::list<Mortar::Entity*>& bombList = am->GetTypeList(1);
                for (itBomb = bombList.begin(); itBomb != bombList.end() && g_Stop == 0; ++itBomb) {
                    Bomb* bomb = static_cast<Bomb*>(*itBomb);
                    if (!bomb) continue;
                    if (!bomb->IsActive()) continue;
                    if (bomb->flags & 0x01) continue; // v1.6.1 @0x001e8ce0: skip ENT_INACTIVE (MenuButton grow-in)

                    _Vector3<float> bladeDirCopy = m_BladeDir; // binary saves dir before test
                    bool hit = CollideWithEntity(bomb);

                    if (hit) {
                        if ((s_ModPowerMask & 0x10) == 0) {
                            // Normal bomb hit (ModPowerMask bit 4 NOT set: no push)
                            bomb->CollisionResponse(this, 0, 0, &bladeDirCopy);
                            g_StopCounter = 0;
                            g_Stop = 1;
                            if (bomb->m_bHit && !bomb->m_bMenuBombHit) {
                                m_BombHitEdge = 1;
                            }
                        } else {
                            // ModPowerMask bit 4: push bomb away with blade velocity * 10
                            bomb->vel += m_BladeDir * 10.0f;
                        }
                    } else {
                        // --- NON-HIT: BOMB VELOCITY REPULSION/ATTRACTION ---
                        // ModPowerMask bit 3 = repel, bit 4 = attract.
                        _Vector3<float> dirToBomb(bomb->pos.x - pos.x,
                                                  bomb->pos.y - pos.y,
                                                  bomb->pos.z - pos.z);
                        float dist = dirToBomb.Magnitude();
                        if (dist > 0.001f) dirToBomb = dirToBomb * (1.0f / dist);

                        if (s_ModPowerMask & 8) {
                            // Repel (bit 3)
                            float mag = dist * 0.5f;
                            if (mag < 50.0f) mag = 50.0f;
                            bomb->vel -= dirToBomb * mag;
                            float speed = bomb->vel.Magnitude();
                            if (speed < 8.0f) speed = 8.0f;
                            bomb->vel = bomb->vel * (speed / bomb->vel.Magnitude());
                        } else if (s_ModPowerMask & 4) {
                            // Attract (bit 2)
                            float mag = dist * 0.5f;
                            if (mag < 50.0f) mag = 50.0f;
                            bomb->vel += dirToBomb * mag;
                            float speed = bomb->vel.Magnitude();
                            if (speed < 8.0f) speed = 8.0f;
                            bomb->vel = bomb->vel * (speed / bomb->vel.Magnitude());
                        }
                    }
                }
            }
        }
    }

    // =====================================================================
    // 10. m_Scale UPDATE
    //     Binary @ 0x1e98b0: if m_TrailEmitter && WaveManager::CriticalMode():
    //       m_Scale += localDt * 2.0f, clamp <= 1.0
    //     else:
    //       m_Scale -= localDt * 2.0f, clamp >= 0
    // =====================================================================
    {
        // ASM-spec v1.6.1 SlashEntity::Update @0x001e8f74: gate is
        //   if (m_TrailEmitter(+0x3c) == 0) goto else;
        //   if (CriticalMode(WaveManager::GetInstance(), 0) == 0) goto else;
        // The WaveManager pointer (bl 0x0010d064) is passed straight to CriticalMode
        // with no null test.
        if (m_TrailEmitter && WaveManager::GetInstance()->CriticalMode(0)) {
            m_Scale += localDt * 2.0f;
            if (m_Scale > 1.0f) m_Scale = 1.0f;
        } else {
            m_Scale -= localDt * 2.0f;
            if (m_Scale < 0.0f) m_Scale = 0.0f;
        }
    }

    // =====================================================================
    // 11. BASE COLOUR BLEND (binary @ 0x1e98b0: after scale update)
    //     UpdateModColour if g_ColourType < 2.
    //     Then blend m_BaseColour toward CRITICAL_COLOUR based on m_Scale.
    // ASM-spec v1.6.1 SlashEntity::Update @0x001e8fd4: UpdateModColour(&m_HighlightColour, 0.0)
    //   -- dt=0 causes UpdateModColour to early-return; per-frame does NOT advance mod colour.
    //   Type-2 (disco) advance happens only in TouchDown @0x001ea46c (dt=1.0, new-stroke edge).
    // =====================================================================
    if (g_ColourType < 2) {
        UpdateModColour(&m_HighlightColour, 0.0f);
    }
    {
        float sc = m_Scale;
        if (sc < 0.0f) {
            m_BaseColour = m_HighlightColour;
        } else {
            float t = 1.0f - sc;
            m_BaseColour.r = (uint8_t)((float)Fruit::CRITICAL_COLOUR.r
                + (float)(m_HighlightColour.r - Fruit::CRITICAL_COLOUR.r) * t);
            m_BaseColour.g = (uint8_t)((float)Fruit::CRITICAL_COLOUR.g
                + (float)(m_HighlightColour.g - Fruit::CRITICAL_COLOUR.g) * t);
            m_BaseColour.b = (uint8_t)((float)Fruit::CRITICAL_COLOUR.b
                + (float)(m_HighlightColour.b - Fruit::CRITICAL_COLOUR.b) * t);
            m_BaseColour.a = 0xff;
        }
    }

    // =====================================================================
    // 12. COMBO TIMER / CANCEL (uses comboDt, NOT localDt)
    //     Binary @ 0x1e90d4: identical structure but with m_ComboTimer
    //     directly at +0x118 (was the old mis-named m_field_0x118).
    //     m_ComboTimer is the per-slice accumulator;
    //     m_ComboCount tracks the current combo swing length.
    // =====================================================================
    {
        // ASM-spec v1.6.1 SlashEntity::Update combo-resolve @0x001e90d8: gate/save on m_ComboCounter (+0x17c), not m_ComboCount (+0x178, always -1). Restores combos/achievements/coins in all modes.
        static const float kComboFireThresh = 0.095f;   // DAT_001e9224
        static const float kComboIdleValue  = 0.1f;     // DAT_001e9220
        if (m_ComboTimer >= kComboIdleValue) {
            // Idle / already-fired: reset combo state, no event.
            m_pLastComboFruit = nullptr;
            m_ComboCounter = 0;
            m_ComboOnlineMode = 0;
            for (int i = 0; i < 10; ++i) m_ComboFruitTypes[i] = -1;
            m_ComboCount = -1;
            m_pComboMissControl = nullptr;
        } else {
            m_ComboTimer += comboDt;
            if (m_ComboTimer >= kComboFireThresh) {
                m_ComboTimer = kComboIdleValue;
                // Fire g_OnComboCancel -- ComboModifier::ComboWasCanceled subscribes.
                g_OnComboCancel(this);
                if (m_ComboCounter > 1 && m_ComboFruitTypes[0] >= 0) {
                    // (a) Score-threshold refund.
                    {
                        int threshold = game_work.m_ScoreThreshold - m_ComboCounter;
                        if (threshold < 2) threshold = 2;
                        game_work.m_ScoreThreshold = threshold;
                    }
                    // (b) Combo body: only if count > 2 AND m_ComboFruitTypes[1] >= 0.
                    if (m_ComboCounter > 2 && m_ComboFruitTypes[1] >= 0) {
                        // v1.6.1 SlashEntity::Update @0x001e867c tests game_work+0x4
                        // (gameMode) alone -- there is no Game-instance term. The
                        // port-added `game &&` is removed.
                        if (game_work.gameMode == GAME_MODE_ARCADE) {
                            LOG_INFO("BLITZ", "SlashEntity arcade combo: count=%d amount=%.3f -> AddSpeed",
                                       m_ComboCounter, (float)m_ComboCounter / 3.0f);
                            WaveManager::GetInstance()->AddSpeed(
                                (float)m_ComboCounter / 3.0f, 0);
                            AddToCurrentScore(m_ComboCounter, m_ComboOnlineMode, true, true);
                            // @0x001e9160: AddCombo lives INSIDE the arcade arm (bl 0x0010dbd0 GetInstance,
                            // bl 0x0010a150 AddCombo, then b 0x001e919c to the join). The classic/zen
                            // arm at 0x001e9170 has no AddCombo -- bonuses are an arcade-only feature.
                            BonusManager::GetInstance()->AddCombo(m_ComboCounter);
                        } else if (!Mortar::NetworkManager::GetInstance()->IsOnlineMultiplayer() || m_ComboOnlineMode != 2) {
                            AddToCurrentScore(m_ComboCounter, m_ComboOnlineMode, true, false);
                        }
                        // v1.6.1 SlashEntity::Update @0x001e867c: the combo-stat block
                        // reads game_work.m_SaveData (+0x50) with no null test and runs
                        // unconditionally -- the binary never calls Game::GetInstance in
                        // Update, so the port-added `if (game)` wrapper is removed.
                        {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "%s_combos", GetModeName((GAME_MODE)game_work.gameMode));
                            game_work.m_SaveData->AddToTotal(buf, StringHash(buf), 1, true, true);
                        }
                        // (c) Combo coin spawn.
                        {
                            int bonusCoins = 0;
                            for (int i = 0; i < m_ComboCounter; ++i) {
                                // m_ComboFruitTypes[i] defaults to -1 (see the resets above) and
                                // otherwise only ever holds a live fruit's m_FruitType, which is
                                // always in [0, g_FruitInfoCount) -- so only the negative sentinel
                                // needs guarding; FruitInfo_Get no longer bounds-checks.
                                if (m_ComboFruitTypes[i] < 0) continue;
                                const ::FruitInfo* fi = Fruit::FruitInfo(m_ComboFruitTypes[i]);
                                if (fi->m_CoinsMax > 0) { bonusCoins = m_ComboCounter; break; }
                            }
                            _Vector3<float> coinPos = m_SliceFruitPos;
                            if (m_pComboMissControl) coinPos = m_pComboMissControl->pos;
                            Coin::MakeCoins(bonusCoins, 1,
                                            coinPos, 0, 0xff3a,
                                            /*target=*/nullptr, 0.02f, 0.15f,
                                            nullptr, nullptr,
                                            Coin::DefaultArrivedDelegate(), true);
                        }
                        // TODO: v1.6.1 0x001e92f8 (SlashEntity::Update) -- the binary tails this
                        // block with `if (IsOnlineMultiplayer() && m_ComboOnlineMode != 2)
                        // SendP2PPacket(PointsPacket(wave->id, m_ComboCounter, (int)coinPos.x,
                        // (int)coinPos.y), false)`. Unported: P2P multiplayer is defunct, and
                        // IsOnlineMultiplayer() is a stub that always returns false, so the call
                        // is unreachable either way.
                    }
                    // -----------------------------------------------------------------
                    // OUTER combo level -- still inside `m_ComboCounter > 1 &&
                    // m_ComboFruitTypes[0] >= 0`, but OUTSIDE the `> 2` block above.
                    // @0x001e93a4: the `> 2` gate at 0x001e9110 (`cmp r2,#2 / ble 0x001e93a4`) and the
                    // m_ComboFruitTypes[1] gate at 0x001e9118 (`ldr r2,[r4,#0x154] / blt
                    // 0x001e93a4`) both branch TO 0x001e93a4, which is where r7 = this+0x150
                    // (m_ComboFruitTypes) is formed for the three blocks below. So a 2-fruit
                    // swipe DOES reach them.
                    // -----------------------------------------------------------------
                    // (d) Achievement unlock -- @0x001e93b4 (bl 0x0010ce60).
                    // Safe to call at m_ComboCounter == 2: UnlockComboAchievement carries its
                    // OWN count gate (`info->m_Total > comboLen -> skip`, @0x001175e8), and the
                    // shipped achievementlist.xml COMBO entries need 6 (combo_mambo) or go
                    // through the isGameOver arm, which itself rejects comboLen <= 2.
                    AchievementManager::GetInstance()->UnlockComboAchievement(m_ComboCounter, m_ComboFruitTypes);
                    // (e) strawberry_combo_total scan -- @0x001e93b8 (`ldr r3,[r4,#0x17c] /
                    // cmp r3,#2 / ble 0x001e94e0`). Its own `> 2` gate, separate from the one
                    // above; the false arm lands directly on the best-combo save in (f).
                    if (m_ComboCounter > 2) {
                        static int s_StrawberryType = -2;
                        if (s_StrawberryType == -2)
                            s_StrawberryType = Fruit::FruitType("strawberry", false);
                        if (s_StrawberryType >= 0) {
                            for (int i = 0; i < m_ComboCounter; ++i) {
                                if (m_ComboFruitTypes[i] == s_StrawberryType) {
                                    static const uint32_t hStrawberryCombo = StringHash("strawberry_combo_total");
                                    game_work.m_SaveData->AddToTotal(
                                        "strawberry_combo_total", hStrawberryCombo, 1, true, false);
                                    break;
                                }
                            }
                        }
                    }
                    // (f) Best-combo save + CheckCombo cache -- @0x001e94e0.
                    {
                        FruitSaveData* sd = game_work.m_SaveData;
                        if (sd && m_ComboCounter > sd->m_BestComboLength) {
                            // ASM-spec v1.6.1 SlashEntity::Update @0x1e9504 / @0x1e95a8: writer copies 11 ints via a
                            // stepping ptr from +0x150; the 11th read is +0x178 (m_ComboCount), one past the 10-elem
                            // m_ComboFruitTypes. Reproduce the 11th-slot write explicitly (NOT i<11 -- that would read
                            // m_ComboFruitTypes[10] out of bounds on the port's 10-element array).
                            for (int i = 0; i < 10; ++i) sd->m_BestComboFruits[i] = m_ComboFruitTypes[i];
                            sd->m_BestComboFruits[10] = m_ComboCount;   // +0x178 spill -- binary's 11th slot
                            sd->m_BestComboLength = m_ComboCounter;
                            s_CheckComboFlag = (signed char)CheckCombo(m_ComboFruitTypes, m_ComboCounter, nullptr);
                        } else if (sd && m_ComboCounter == sd->m_BestComboLength) {
                            if (s_CheckComboFlag == -1)
                                s_CheckComboFlag = (signed char)CheckCombo(sd->m_BestComboFruits, m_ComboCounter, nullptr);
                            int newScore = (signed char)CheckCombo(m_ComboFruitTypes, m_ComboCounter, nullptr);
                            if (s_CheckComboFlag < newScore) {
                                // ASM-spec v1.6.1 SlashEntity::Update @0x1e95a8: same 11-int stepping-ptr copy as
                                // the new-high path -- 11th slot is m_ComboCount (+0x178 spill).
                                for (int i = 0; i < 10; ++i) sd->m_BestComboFruits[i] = m_ComboFruitTypes[i];
                                sd->m_BestComboFruits[10] = m_ComboCount;   // +0x178 spill -- binary's 11th slot
                                sd->m_BestComboLength = m_ComboCounter;
                            }
                        }
                    }
                }
                // (g) State reset (unconditional when timer fires) -- @0x001e95e4.
                m_pLastComboFruit = nullptr;
                m_ComboCounter = 0;
                m_ComboOnlineMode = 0;
                for (int i = 0; i < 10; ++i) m_ComboFruitTypes[i] = -1;
                m_ComboCount = -1;
                m_pComboMissControl = nullptr;
            }
        }
    }

    // =====================================================================
    // 13. SWIPE SOUND TIMER (binary-faithful, uses localDt, NOT 1.0f)
    //     Binary @ 0x1e96c0: if timer > 0 AND < 0.05, OR bladeVel < 20:
    //       subtract localDt from timer.
    //     Else if timer == 0 AND bladeVel > 35:
    //       PlaySwipe(); timer = 0.05.
    //     Else: goto skip (timer unchanged).
    // =====================================================================
    {
        float bladeMag = m_BladeDir.Magnitude();
        if ((m_SwipeSoundTimer > 0.0f && m_SwipeSoundTimer < 0.05f)
            || bladeMag < 20.0f)
        {
            m_SwipeSoundTimer -= localDt;
        } else {
            if (m_SwipeSoundTimer > 0.0f || bladeMag <= 35.0f) {
                // no-op: fall through to skip
            } else {
                PlaySwipe();
                m_SwipeSoundTimer = 0.05f;
            }
        }
        if (m_SwipeSoundTimer < 0.0f) m_SwipeSoundTimer = 0.0f;
    }

    // =====================================================================
    // 14. SPLAT LOOP (binary @ 0x1e96c0 tail)
    //     Uses localDt. Includes Fruit::FruitInfo lookup and TranslatePos.
    // =====================================================================
    if (m_SplatTimer > -1.0f) {
        m_SplatTimer -= localDt;
    }
    while (m_PendingSplats >= 0 && m_SplatTimer <= 0.0f) {
        float sq = m_BladeDir.MagnitudeSqr();
        if (sq > 1.0f && sq < 10000.0f) {
            m_SliceBladeDir = m_BladeDir;
        }
        m_PendingSplats--;
        // ASM-spec v1.6.1 SlashEntity::Update @0x001e96c0: m_SplatInterval is CAPPED at 0.03f
        // (DAT @0x1e9904), NOT floored. RandF max = 0.05f (@0x1e98f4), addend 0.01f (@0x1e9900).
        // Prior port used RandF(1.0)*0.5 (10x range) + an INVERTED clamp (B>=0.03?B:0.03), so the
        // interval accumulated -> backlog drained ~1/s (40s drip). Capping it drains ~33/s (~1.2s).
        float tmp = m_SplatInterval + Math::g_Random.RandF(0.05f) + 0.01f;
        if (tmp >= 0.03f) {
            m_SplatInterval = 0.03f;                                             // clamp DOWN to cap
        } else {
            m_SplatInterval = m_SplatInterval + Math::g_Random.RandF(0.05f) + 0.01f;  // fresh reroll (<0.03)
        }
        m_SplatTimer += m_SplatInterval;
        // GetFree() never returns null (v1.6.1 SplatEntity::GetFree @0x001eb318 --
        // flat round-robin pool steals the cursor slot when full).
        SplatEntity* s = SplatEntity::GetFree();
        // FruitInfo lookup -- feeds the MakeSplat mute arg below.
        const ::FruitInfo* sliceInfo = 0;
        if (m_SliceFruitType < 0x100) {
            sliceInfo = Fruit::FruitInfo(m_SliceFruitType);
        }
        // ASM-spec v1.6.1 SlashEntity::Update @0x001e97cc: splat world-pos =
        // FruitCamera::TranslatePos(this->pos, inverse=true, useZeroCenter=true).
        // this->pos is the live blade position (Entity +0x10), so splats trail the blade
        // across the multi-frame spawn (NOT frozen at the fruit). Camera = game_work.m_FruitCamera.
        _Vector3<float> v(m_SliceBladeDir.x * (Math::g_Random.RandF(0.75f) + 0.75f),
                          m_SliceBladeDir.y * (Math::g_Random.RandF(0.75f) + 0.75f),
                          0.0f);
        // v1.6.1 @0x1e97cc scatter scale RandF(0.75)+0.75 = [0.75,1.5].
        FruitCamera* cam = game_work.m_FruitCamera;
        _Vector3<float> splatPos = cam ? cam->TranslatePos(pos, true, true) : pos;
        // ASM-spec v1.6.1 SlashEntity::Update @0x001e982c: param3 = 1 (mov r3,#0x1
        // @0x001e9828) -- ONLY slash-trail splats are streak-eligible (types 4/5,
        // 1-in-2 in UpdateSplat). The other three spawn sites (Fruit::Slice
        // @0x001dcfd0, Jiblet::Update @0x001e5638, ExplodeSuperFruit @0x001bab08)
        // pass 0.
        // ASM-spec v1.6.1 trail caller @0x001e9788: mute arg = (FruitInfo+0x330
        // m_bIsSuperFruit != 0) -- super-fruit splats land silent.
        s->MakeSplat(splatPos, v, true,
                     /*mute=*/sliceInfo != 0 && sliceInfo->m_bIsSuperFruit != 0,
                     (long)m_SliceFruitType);
    }
}

// ---------------------------------------------------------------------------
// Draw -- Entity vtable slot 5. v1.6.1 SlashEntity::Draw @0x001e6168 is 4 bytes,
// a single `bx lr`. The blade is rendered by DrawSlice from GameDraw's 16-slot
// loop instead.
// ASM-verified: 2026-05-18 v1.6.1 SlashEntity::Draw @ 0x001e6168 (re-analyst)
// ---------------------------------------------------------------------------
void SlashEntity::Draw(Renderer& /*r*/) {
}

// ---------------------------------------------------------------------------
// UpdateBladeLatch -- the sim-tick half of v1.6.1 SlashEntity::DrawSlice
// @0x001e83d4. Body is the binary's latch block verbatim:
//   old = (uchar)m_BladeActive; if (old!=0) { nv=(old<<1)&2; m_BladeActive=nv; if(nv==0) burst; }
//
// DIFFERS: original = m_BladeActive shift lives in SlashEntity::DrawSlice
// @0x001e83d4 (v1.6.1), which is correct because Bada ran render 1:1 with the sim
// tick; the port has interpolated frames, so the shift is sim-tick-gated to
// preserve the binary's ONE-shift-per-tick semantic.
//
// This diverges in STRUCTURE to preserve binary BEHAVIOUR -- the opposite of the
// deleted `#ifndef __bada__` variant, which changed the semantic to suit a
// headless test.
//
// Why the split is needed: a held finger re-arms the latch once per SIM tick
// (TouchDown -> UpdateTouchDown -> OnTouchActive -> `m_BladeActive |= 1`), while
// DrawSlice runs once per DISPLAY frame. At 120Hz the shift ran twice per re-arm,
// so the latch was 0 at every TouchDown, TouchDown took its Reset() arm on every
// tick, and no stroke ever accumulated. Bada never hit this because render and
// sim were 1:1 there.
//
// Call site is GameUpdate's common tail (unconditional 16-slot loop, after both
// branches' SlashEntity::Update), so the per-tick order stays the binary's
// re-arm -> Update -> shift.
// ---------------------------------------------------------------------------
void SlashEntity::UpdateBladeLatch() {
    unsigned char old = (unsigned char)m_BladeActive;
    if (old != 0) {
        int nv = (old << 1) & 2;
        m_BladeActive = nv;
        if (nv == 0) {
            // old==2 -> release edge: fire burst ONCE.
            if (g_ScaleFlag1) CreateGhost();
            // ModParticlesReleaseHash = g_SecondHash (particle2 slot in SetModColours).
            if (g_SecondHash != 0) {
                // ASM-spec v1.6.1 SlashEntity::DrawSlice @0x001e841c: r2=NULL, r3=1.
                PSPParticleEmitter* eBurst =
                    PSPParticleManager::GetInstance().AddEmitter(
                        g_SecondHash, nullptr, /*updateWhenPaused=*/true);
                if (eBurst) eBurst->m_Pos = pos;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// DrawSlice -- v1.6.1 @ 0x1e83b0
// Called from GameDraw's 16-slot loop, NOT from ActorManager::Draw.
//
// The binary opens this function with the m_BladeActive shift register. The port
// runs that block once per SIM TICK from UpdateBladeLatch() instead -- see its
// DIFFERS note above. Everything from `s_slashes` down is the binary's body.
// Draw if m_PointCount > 3: reset+upload modelview, bind blade.tex, DrawTriStrip
// both buffers with count = m_PointCount + 1 (includes head-cap vertex).
// ---------------------------------------------------------------------------
void SlashEntity::DrawSlice() {
    // ASM-spec v1.6.1 SlashEntity::DrawSlice @0x001e83b0: the `> 0` test is genuine,
    // not an unconditional clamp. @0x001e843c is
    // `ldr r2,[r3,#0xbc] / cmp r2,#0 / movgt r2,#0 / strgt r2,[r3,#0xbc]` -- the
    // store is predicated on GT, so a zero/negative counter is left alone.
    // s_slashes is incremented per sim tick (UpdatePoints) and cleared per render frame here.
    if (s_slashes > 0) {
        s_slashes = 0;
    }

    // Gate: m_PointCount > 3 (binary @ 0x1e83b0).
    if (m_PointCount <= 3) {
#ifdef FN_DEBUG_TOUCH
        // Only log when points are actually accumulating; idle channels at
        // pointCount==0 would spam this every frame for all 16 fingers.
        if (m_PointCount > 0)
            LOG_DEBUG("SLASH", "DrawSlice[%d]: early-return pointCount=%d (<=3)",
                     m_FingerId, m_PointCount);
#endif
        return;
    }
    if (!m_pLeftBuffer || !m_pRightBuffer) return;

    Mortar::SmartPtr<Mortar::Texture>& bladeTex =
        g_ModTexture.IsValid() ? g_ModTexture : g_BladeTex;
    if (!bladeTex.IsValid()) return;

#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "DrawSlice[%d]: pointCount=%d drawCount=%d",
             m_FingerId, m_PointCount, m_PointCount + 1);
#endif

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    // Vertex count = m_PointCount + 1 (includes head-cap vertex at [m_PointCount]).
    // ASM-verified v1.6.1 Texture2D_Bada::Set @0x229788: blade tex-env RGB=MODULATE (texture.rgb
    // x vertex.rgb); ALPHA=REPLACE from GL_PRIMARY_COLOR (vertex.alpha). blade.tex alpha is
    // uniformly opaque so REPLACE-alpha == MODULATE-alpha here. Shader modulates texel*color
    // for all Mesh::DrawTriStrip draws now, so no explicit tex-env call is needed.
    bladeTex->Set();
    Mortar::Mesh::DrawTriStrip(m_pLeftBuffer,  m_PointCount + 1, false, NULL);
    Mortar::Mesh::DrawTriStrip(m_pRightBuffer, m_PointCount + 1, false, NULL);
    bladeTex->UnSet();
}

// ---------------------------------------------------------------------------
// Init (3-arg binary form) -- v1.6.1 @ 0x1e7a34
// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017C65C (re-analyst)
// ---------------------------------------------------------------------------
void SlashEntity::Init(void* /*unused*/, long /*unused*/, _Vector3<float>* /*unused*/) {
    // 1. Allocate ColLine into m_Col (+0x38).
    m_Col = new ColLine();
    // ASM-verified: 2026-05-27 v1.6.1 binary @ 0x0017c68c (re-analyst)
    flags |= ENT_HAS_COLLISION;

    // 2. Reset scale-adjacent float at +0x94.
    m_SegLenSq = -1.0f;

    // 3. Build vertex buffers (160 pairs).
    InitPoints(160);

    // 4. Per-frame scratch state.
    m_HeadThickScale = 0.0f;
    m_TrailEmitter = nullptr;
    m_Scale        = 0.0f;
    m_PendingSplats = -1;

    // 5. Copy Colour::White into both colour fields.
    Colour white(255, 255, 255, 255);
    m_HighlightColour = white;
    m_BaseColour      = white;

    // 6. Ghost ring init: zero all 6 Vec3 entries at +0xbc.
    for (int i = 0; i < 6; ++i) {
        m_GhostDirRing[i] = _Vector3<float>(0.0f, 0.0f, 0.0f);
    }

    // Ghost index/count at +0x104/+0x108 = 0; ghost-dir at +0x10c = zero.
    m_GhostIndex = 0;
    m_GhostCount = 0;
    m_GhostDir   = _Vector3<float>(0.0f, 0.0f, 0.0f);

    // +0x118 = m_ComboTimer seeded to 0.1f (DAT_001e7b98 = 0x3dcccccd).
    // +0xb8 = m_SwipeSoundTimer seeded to 0.0f (DAT_001e7b94 = 0x00000000).
    m_SwipeSoundTimer = 0.0f;
    m_ComboTimer      = 0.1f;

    // +0x144 = m_ComboScoreScale = 6.0f; +0x148/+0x14c = -1.
    m_ComboScoreScale = 6.0f;
    m_field_0x148     = -1;
    m_field_0x14c     = -1;

    // 7. Combo / state init.
    m_ComboCount     = -1;
    m_ComboCounter   = 0;
    m_ComboOnlineMode = 0;
    m_pComboMissControl = nullptr;
    m_pLastComboFruit   = nullptr;
    m_GhostSpawnTimer   = 0.0f;
    m_GhostSpawnPending = 0;
    m_BombHitEdge       = 0;
    m_Angle             = 0;

    // 8. Combo fruit type array, all -1.
    for (int i = 0; i < 10; ++i) {
        m_ComboFruitTypes[i] = -1;
    }
}

// ---------------------------------------------------------------------------
// InitPoints -- v1.6.1 @ 0x1e75d0
// Allocates m_pLeftBuffer/m_pRightBuffer each as (count+2) QUADCUSTOMVERTEX.
// Binary: 162 * 36 = 5832 bytes per buffer for count=160.
// Fills elements with zeroed pos/normal.xy, normal.z=1.0, uv=(0,0), white colour.
// ASM-verified: 2026-05-18 v1.6.1 InitPoints @ 0x1e75d0 (re-analyst)
// ---------------------------------------------------------------------------
void SlashEntity::InitPoints(long count) {
    Colour whiteColour(255, 255, 255, 255);
    uint32_t whitePacked = whiteColour.PlatformColour();

    m_SplitPoint = (int)count;   // +0x50: set to 160
    m_PointCount = 0;            // +0x58: set to 0

    // Binary allocates exactly (count+2) records per buffer (162 for count=160).
    if (m_pLeftBuffer) {
        delete[] m_pLeftBuffer;
        m_pLeftBuffer = nullptr;
    }
    if (m_pRightBuffer) {
        delete[] m_pRightBuffer;
        m_pRightBuffer = nullptr;
    }

    m_pLeftBuffer  = new QUADCUSTOMVERTEX[count + 2];
    m_pRightBuffer = new QUADCUSTOMVERTEX[count + 2];

    // Fill m_SplitPoint (=160) records (not count+2) per binary @ 0x1e75d0.
    // DAT_001e76ec = 0.0f: pos.xyz = 0, normal.xy = 0, normal.z = 1.0, uv = (0,0); colour = white.
    // Binary InitPoints @0x1e75d0 writes normal.z = 1.0f per binary fill.
    // (Overwritten later by AddPoint, but binary fill sets 1.0 here.)
    for (int side = 0; side < 2; ++side) {
        QUADCUSTOMVERTEX* buf = (side == 0) ? m_pLeftBuffer : m_pRightBuffer;
        for (int i = 0; i < m_SplitPoint; ++i) {
            buf[i].x  = 0.0f;
            buf[i].y  = 0.0f;
            buf[i].z  = 0.0f;
            buf[i].nx = 0.0f;
            buf[i].ny = 0.0f;
            buf[i].nz = 1.0f;
            buf[i].colour = whitePacked;
            buf[i].u  = 0.0f;
            buf[i].v  = 0.0f;
        }
    }

    // Binary @ 0x1e75d0: do/while seeds all three anchors to (-65535,-65535,-65535)
    // (DAT_001e76e8 = 0xc77fff00 = -65535.0f). The sentinel is tested by
    // UpdateTouchDown: tail.x <= -65520 means "first point of new slash".
    static const float kAnchorSentinel = -65535.0f;
    m_TailPos     = _Vector3<float>(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
    m_HeadPos     = _Vector3<float>(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
    m_PrevHeadPos = _Vector3<float>(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "InitPoints: seed anchors tail=(%.1f,%.1f,%.1f) head=(%.1f,%.1f,%.1f) prev=(%.1f,%.1f,%.1f) pointCount=%d",
             m_TailPos.x, m_TailPos.y, m_TailPos.z,
             m_HeadPos.x, m_HeadPos.y, m_HeadPos.z,
             m_PrevHeadPos.x, m_PrevHeadPos.y, m_PrevHeadPos.z,
             m_PointCount);
#endif
}

// ---------------------------------------------------------------------------
// SetModColours / InitModColours / SetModScales
// ---------------------------------------------------------------------------
// v1.6.1 SlashEntity::SetModColours @0x001e7f24 -- binary signature (non-const
// Colour*, per mangled _ZN11SlashEntity13SetModColoursEP6ColouriifPKcS3_bS3_S3_)
// owns the body; the port-added const overload below forwards here.
void SlashEntity::SetModColours(
    Colour*        colours,
    int            colourCount,
    int            colourType,
    float          lifeScale,
    const char*    particlePath,
    const char*    textureName2,
    bool           directional,
    const char*    contactParticle,
    const char*    particle2)
{
    g_LifeScale  = lifeScale;
    g_ColourType = colourType;

    if (colourCount < 0) colourCount = 0;
    if (colourCount > 16) colourCount = 16;
    g_ColourCount = colourCount;
    for (int i = 0; i < colourCount; ++i) {
        g_Palette[i] = colours ? colours[i] : Colour(255, 255, 255, 255);
    }

    // ASM-spec v1.6.1 SlashEntity::SetModColours @0x001e7f24: colourOut = ModColours[0];
    // ModColourTime = 0.0f; if (ModColourType==2) ModColourTime = (float)Rand32(count).
    g_ModColourOut    = g_Palette[0];
    g_PaletteProgress = 0.0f;
    // The binary gates on ModColourType == 2 alone. Rand32 advances the shared
    // LCG whatever its argument, so an extra `g_ColourCount > 0` guard would
    // drop a draw and desync every later consumer.
    if (g_ColourType == 2) {
        g_PaletteProgress = (float)Math::g_Random.Rand32((uint32_t)g_ColourCount);
    }

    if (textureName2 && textureName2[0] != '\0') {
        char texPath[256];
        snprintf(texPath, sizeof(texPath), "%s.tex", textureName2);
        g_ModTexture = Mortar::TextureManager::LoadLocalisedTexture(texPath);
    } else {
        g_ModTexture.SetNull();
    }

    g_TrailHash = (particlePath && particlePath[0] != '\0')
                ? StringHash(particlePath) : 0;
    bool trailExists = g_TrailHash != 0 &&
        PSPParticleManager::GetInstance().FindTemplate(g_TrailHash) != nullptr;

    g_ContactHash = ResolveEmitterHash(contactParticle);
    g_SecondHash  = ResolveEmitterHash(particle2);

    g_DirectionalFlag = trailExists ? (directional ? 2 : 1) : 0;

    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) {
            g_pSlashEntities[i]->ColoursChanged();
        }
    }
}

void SlashEntity::InitModColours()
{
    for (int i = 0; i < 16; ++i) {
        g_Palette[i] = Colour(255, 255, 255, 255);
    }
    g_ColourCount     = 1;
    g_ColourType      = 0;
    g_PaletteProgress = 0.0f;
    g_TrailHash       = 0;
    g_ContactHash     = 0;
    g_SecondHash      = 0;
    g_DirectionalFlag = 0;
    g_ModTexture.SetNull();
}

// ASM-verified: 2026-06-16 v1.6.1 binary @ 0x1e60a8 (re-analyst)
// Binary param order: (length, thickness, endThickness, pointScale, flipUD, loop, uvNormalLen)
// ModSlashLength=p1->g_Scale3, ModSlashThickness=p2->g_Scale1, ModSlashEndThickness=p3->g_Scale2
void SlashEntity::SetModScales(
    float length,
    float thickness,
    float endThickness,
    float pointScale,
    bool  flipUD,
    bool  loop,
    float uvNormalLen)
{
    g_Scale3     = length;
    g_Scale1     = thickness;
    g_Scale2     = endThickness;
    g_Scale4     = pointScale;
    g_Scale5     = uvNormalLen;
    g_ScaleFlag1 = flipUD ? 1 : 0;
    g_ScaleFlag2 = loop   ? 1 : 0;
}

// ColoursChanged v1.6.1 @ 0x1e76fc.
// Binary order: (1) ClearEmitter if non-null; (2) if m_BladeActive==0 return;
// (3) m_PointCount=0; (4) if ColourType==2 UpdateModColour(&m_HighlightColour,1.0f);
// (5) AddEmitter(g_TrailHash) if g_DirectionalFlag+g_TrailHash.
void SlashEntity::ColoursChanged() {
    if (m_TrailEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }
    if (m_BladeActive == 0) {
        return;
    }
    m_PointCount = 0;

    if (g_ColourType == 2) {
        UpdateModColour(&m_HighlightColour, 1.0f);
    }

    if (g_DirectionalFlag != 0 && g_TrailHash != 0) {
        // ASM-spec v1.6.1 PSPParticleManager::AddEmitter @0x0013c1b8
        // auto-null contract: ppRef=&m_TrailEmitter so reap/ClearEmitter nulls it
        // ASM-spec v1.6.1 SlashEntity::ColoursChanged @0x001e778c: r3=1.
        m_TrailEmitter = PSPParticleManager::GetInstance()
            .AddEmitter(g_TrailHash, &m_TrailEmitter, /*updateWhenPaused=*/true);
        if (m_TrailEmitter) {
            m_TrailEmitter->m_bUpdateWhenPaused = true;
        }
    }
}

// Accessors.
const Mortar::SmartPtr<Mortar::Texture>& SlashEntity::GetModTexture()    { return g_ModTexture; }
uint32_t SlashEntity::GetTrailEmitterHash()                       { return g_TrailHash; }
uint32_t SlashEntity::GetContactEmitterHash()                     { return g_ContactHash; }
uint32_t SlashEntity::GetSecondEmitterHash()                      { return g_SecondHash; }
uint8_t  SlashEntity::GetDirectionalFlag()                        { return g_DirectionalFlag; }
int      SlashEntity::GetColourCount()                            { return g_ColourCount; }
int      SlashEntity::GetColourType()                             { return g_ColourType; }
const Colour* SlashEntity::GetPalette()                           { return g_Palette; }

// Binary @ 0x17CE0C -- main per-point append (binary symbol parity).
// The 3-arg AddPoint above IS the binary-faithful implementation.

// ASM-spec v1.6.1 SlashEntity::CollideWithEntity @0x001e6420
bool SlashEntity::CollideWithEntity(Mortar::Entity* entity) {
    ColLine* L = static_cast<ColLine*>(m_Col);
    if (!L || !(m_SegLenSq > 0.0f) || !entity) return false;
    Col* eCol = entity->m_Col;
    if (!eCol || game_work.bM_Mode || game_work.retryFlag) return false;
    _Vector3<float> pen;
    if (eCol->GetType() != Col::TYPE_SPHERE) {
        return L->Collide(eCol, &pen) != 0;
    }
    ColSphere* S = static_cast<ColSphere*>(eCol);
    if (ColSphere::ColSphereLine(S, L, &pen) == 0) return false;
    float eR2 = S->radius * S->radius;
    _Vector3<float> anchor = L->a();
    _Vector3<float> eCenter = S->center();
    if ((anchor - eCenter).MagnitudeSqr() < eR2) return true;
    _Vector3<float> contactBase = pen + eCenter;
    _Vector3<float> chordOffset(0.0f, 0.0f, 0.0f);
    if (pen.MagnitudeSqr() < eR2) {
        float half = sqrtf(eR2 - pen.MagnitudeSqr());
        chordOffset = _Vector3<float>::Cross(pen, _Vector3<float>(0.0f, 0.0f, 1.0f));
        chordOffset.Normalise();
        chordOffset = chordOffset * (S->radius - half);
    }
    _Vector3<float> hitA = contactBase + chordOffset;
    if ((anchor - hitA).MagnitudeSqr() < m_SegLenSq) return true;
    _Vector3<float> hitB = contactBase - chordOffset;
    return (anchor - hitB).MagnitudeSqr() < m_SegLenSq;
}

// v1.6.1 SlashEntity::CollisionResponse @0x001e616c -- 8 bytes, `mov r0,#0; bx lr`.
int SlashEntity::CollisionResponse(Mortar::Entity* /*hitter*/, unsigned long /*mask1*/,
                                    unsigned long /*mask2*/, _Vector3<float>* /*bladeVel*/) { return 0; }

// Port-added const-correct convenience overload -- forwards to the binary-mangled
// non-const overload (v1.6.1 SlashEntity::SetModColours @0x001e7f24), which owns
// the body. The body only reads the palette, so the const_cast is safe.
void SlashEntity::SetModColours(
    const Colour* colours,
    int           colourCount,
    int           colourType,
    float         lifeScale,
    const char*   particlePath,
    const char*   textureName2,
    bool          directional,
    const char*   contactParticle,
    const char*   particle2)
{
    SetModColours(const_cast<Colour*>(colours), colourCount, colourType,
                  lifeScale, particlePath, textureName2, directional,
                  contactParticle, particle2);
}

// ASM-spec: SlashEntity::TouchDown @ 0x17D61C
bool SlashEntity::TouchDown(InputEvent* event) {
    // Binary gate (v1.6.1 SlashEntity::TouchDown @0x1ea420): Reset() only when the
    // blade latch has decayed to 0. That is self-clearing because the poll emits
    // ButtonPressed(Touch<n>, 2) EVERY tick a finger is held and nothing on the
    // tick it is released, so a lift always leaves >=1 tick without a
    // TouchDown -- enough for UpdateBladeLatch's `(old << 1) & 2` shift to reach 0
    // before the next press. No press-edge flag is needed, and the port must not
    // invent one: the mapper chain has no such concept.
    //
    // No position seed is needed here either. Touch::SendIndividualTouchCallbacks
    // @0x00242bc4 emits the two TouchAxis events for a slot BEFORE its
    // ButtonPressed, so PointerMoveCallback has already written m_RawTouchPos to
    // the fresh press position by the time this runs.
    //
    // m_BombHitEdge is a transient latch, not a session-long one: UpdateBombHit
    // @0x001cbbac calls ResetGameEntities @0x001cb9c0 on the m_BombHitTimer 1.5s
    // crossing, and that Resets all 16 blades (see SlashEntity::Reset). So this
    // gate is open again well before the timer drains and taps reach the blade.
    if (m_BombHitEdge == 0 && m_BladeActive == 0) {
        Reset();
        if (g_ColourType == 2) {
            UpdateModColour(&m_HighlightColour, 1.0f);
        }
    }
    UpdateTouchDown(event);
    return true;
}

// ASM-spec v1.6.1 SlashEntity::TouchMoveX @0x001e785c: the first thing the binary does
// is load game_work from the GOT and test m_BombHitTimer(+0x10) > 0 -> return false.
// No Game::GetInstance call, no null test.
bool SlashEntity::TouchMoveX(InputEvent* event) {
    if (game_work.m_BombHitTimer > 0.0f) return false;
    // TouchAxisX<n+1> axis event: the position is the axis-value word
    // (InputEvent +0x08), exactly as InputDevice::AxisEvent @0x0027582c packs it.
#if !defined(__bada__)
    m_RawTouchPos.x = event->m_Value;
#else
    // Binary writes Entity::pos directly (v1.6.1 SlashEntity::TouchMoveX @0x001e785c);
    // m_RawTouchPos is the port-only SDL cache of the same value.
    pos.x = event->m_Value;
#endif
    return true;
}

// ASM-spec v1.6.1 SlashEntity::TouchMoveY @0x001e77b4: same shape as TouchMoveX --
// game_work from the GOT, m_BombHitTimer(+0x10) > 0 -> return false. No guards.
bool SlashEntity::TouchMoveY(InputEvent* event) {
    if (game_work.m_BombHitTimer > 0.0f) return false;
    // TouchAxisY<n+1> axis event -- see TouchMoveX above.
#if !defined(__bada__)
    m_RawTouchPos.y = event->m_Value;
#else
    // Binary writes Entity::pos directly (v1.6.1 SlashEntity::TouchMoveY @0x001e77b4).
    pos.y = event->m_Value;
#endif
    return true;
}

// Binary @ 0x17B92C -- SlashEntity::UpdatePoints(float dt).
// The real work is above in UpdatePoints(). This entry point keeps binary
// symbol parity; it is distinct from the UpdatePoints(float) called from Update().

// ASM-spec v1.6.1 SlashEntity::UpdateTouchDown @0x001e9f08: @0x001ea080 the binary
// loads game_work from the GOT and short-circuits to the epilogue when
// m_BombHitTimer(+0x10) > 0. No Game::GetInstance call, no null test.
void SlashEntity::UpdateTouchDown(InputEvent* /*event*/) {
    if (game_work.m_BombHitTimer > 0.0f) return;
    // ASM-spec v1.6.1 SlashEntity::UpdateTouchDown @0x001ea0a0: `ldrb r2,[r2,#0x4]
    // ; cmp r2,#0 ; beq epilogue` immediately after the m_BombHitTimer test. The
    // trail cannot append until DrawUpdate (vtable slot 6) has run at least once.
    if (s_TouchIngestArmed == 0) return;
#if !defined(__bada__)
    OnTouchActive(m_RawTouchPos.x, m_RawTouchPos.y);
#else
    // Binary data flow: TouchMoveX/Y write Entity::pos (@0x001e785c/@0x001e77b4),
    // UpdateTouchDown consumes it. m_RawTouchPos is the port-only SDL cache of the
    // same value (synced to pos in OnTouchActive), so both arms are equivalent.
    OnTouchActive(pos.x, pos.y);
#endif
}

// The binary has NO per-finger release handler: v1.6.1 registers no
// "TouchReleased_<i>" callback at all (GameTaskInitInput @0x001cae0c builds the
// name and drops it). A stroke ends purely because TouchDown stops arriving --
// UpdateBladeLatch then decays the m_BladeActive shift register 1 -> 2 -> 0 and
// Update's !bladeActive branch tears the trail emitter down.

// v1.6.1 SlashEntity::PreDraw @0x001e8514 -- `for (i = 0; i < 8; ++i)
// SlashEntityGhost::Draw(&s_ghosts[i]);` (stride 0x10). SlashEntityGhost is not ported
// yet, so the body stays empty.
void SlashEntity::PreDraw() {
}
