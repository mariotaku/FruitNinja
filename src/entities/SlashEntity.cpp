//
// SlashEntity -- blade trail visual (entity type 3).
// v1.6.1 binary-faithful port: heap-allocated vertex buffers, no inline ring.
// sizeof(SlashEntity) = 0x188 (392). See SlashEntity.h for field/method addresses.
//

#ifndef FN_DEBUG_TOUCH
#define FN_DEBUG_TOUCH 1   // TEMP: touch-stack debug session; remove when done
#endif

#include "SlashEntity.h"
#include "math/MathUtil.h"
#include "debug/Logger.h"
#include "ActorManager.h"
#include "Entity.h"
#include "hud/HUDControl.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
#include "asset/Mesh.h"
#include "asset/TextureManager.h"
#include "input/Touch.h"
#include "input/InputEvent.h"
#include "input/InputManager.h"
#include "particle/PSPParticleManager.h"
#include "audio/GameSound.h"
#include "game/ItemManager.h"
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
#include "engine/util/Event.h"
#include "Fruit.h"
#include "Bomb.h"
#include "SplatEntity.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include "game/GameWork.h"
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

// ASM-verified: 2026-05-20 binary @ 0x00110cb0 CheckCombo (re-analyst)
// Returns signed-char combo quality score (-1, 0x00..0x18) sign-extended to int.
// Score table:
//   0x18: 2 unique types in strict ABAB... (any length)
//   0x14: 2 unique types, count==5, scratch[0]/[1].type == 2 (pomegranate)
//   0x15: 3 unique types, count==5, scratch[0]/[1].type == 2 (binary quirk
//         omits slot[2] -- preserved verbatim)
//   0x17/0x16: any slot has count 4/3 (only when uniq>1)
//   0x04: all unique, count >= 5
//   Rare single-fruit table: 14 named fruit -> 0x06..0x12 (uniq==1 path)
//   Fallback: {-1,-1,-1,0,1,2,3} for count<7 else 5
static int CheckCombo(int* fruitTypes, int count, int* outDominantType) {
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
        if (alternating) {
            bool ok = true;
            for (int i = 0; i < count; ++i) {
                int expect = (i & 1) ? scratch[1].type : scratch[0].type;
                if (fruitTypes[i] != expect) { ok = false; break; }
            }
            if (ok) return 0x18;
        }
        if (count == 5 && (scratch[0].type == 2 || scratch[1].type == 2)) return 0x14;
    } else if (uniq == 3 && count == 5) {
        if (scratch[0].type == 2 || scratch[1].type == 2) return 0x15;
        // Quirk preserved: binary only checks slots 0 and 1 -- slot 2 pomegranate is missed.
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

const float SlashEntity::POINT_SPACING         = 64.0f;   // DAT_0017d5fc
const float SlashEntity::MOVE_THRESH_ACTIVE    = 5.0f;    // sqrt(25)

// Binary global SlashEntity::ModPowerMask @ BSS 0x0024d8cc.
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
static int      g_ColourType        = 0;      // 0x0024D8B8 (0=static, 1=per-frame, 2=per-swipe)
static uint8_t  g_DirectionalFlag   = 0;      // 0x0024D8BC
static uint32_t g_TrailHash         = 0;      // 0x0024D8C0
static uint32_t g_ContactHash       = 0;      // 0x0024D8C4
static uint32_t g_SecondHash        = 0;      // 0x0024D8C8
static Mortar::SmartPtr<Mortar::Texture> g_ModTexture;

static float    g_Scale1            = 0.0f;   // 0x332BCC (m_ScaleLength / startWidth; SlashModInfo ctor @0x13ae78 default=0.0)
static float    g_Scale2            = 1.0f;   // 0x2D8D78 (m_ScaleEndThickness / endWidth; SlashModInfo ctor @0x13ae78 default=1.0)
static float    g_Scale3            = 0.0f;   // 0x0024D8D0 (scale length)
static float    g_Scale4            = 1.0f;   // 0x001F3E64 (UV length)
static float    g_Scale5            = 0.0f;   // 0x0024D8D4 (loop UV length)
static uint8_t  g_ScaleFlag1        = 0;      // 0x0024D8D8 (gates CreateGhost())
static uint8_t  g_ScaleFlag2        = 1;      // 0x001F3E69 (gates UV-mirror branch)
static uint8_t  g_HitLatch          = 0;      // 0x0024D840 frame-hit latch
static int32_t  g_HitResetCounter   = 0;      // 0x0024D83C reset cooldown

// Global head-cap frame-counter: blade-mod struct +0xbc (binary @ 0x00332b34).
// UpdatePoints (@0x1e6914) increments by 1 per head-cap-emit frame; DrawSlice
// (@0x1e83b0) clears it to 0 when positive. Shared across all 16 SlashEntity
// instances. No other reader -- effectively a write-bump/clear bookkeeping slot,
// ported for state fidelity per stub-don't-skip.
static int32_t  g_HeadCapFrameCounter = 0;   // 0x00332b34

static uint32_t ResolveEmitterHash(const char* path) {
    if (!path || path[0] == '\0') return 0;
    uint32_t h = StringHash(path);
    const PSPEmitterTemplate* t =
        PSPParticleManager::GetInstance().FindTemplate(h);
    return t ? h : 0;
}

// ---------------------------------------------------------------------------
// Content load -- matches LoadContent (0x17C948)
// ---------------------------------------------------------------------------
void SlashEntity::LoadContent() {
    if (!g_BladeTex.IsValid()) {
        g_BladeTex = Mortar::TextureManager::LoadLocalisedTexture("blade.tex");
    }
}

void SlashEntity::ReleaseContent() {
    g_BladeTex.SetNull();
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
    , m_SwipeEndEdge(0)
    , m_SplitPoint(0)
    , _field_0x54(0)
    , m_PointCount(0)
    , m_pLeftBuffer(nullptr)
    , m_pRightBuffer(nullptr)
    , m_BladeDir(0, 0, 0)
    , m_TailPos(0, 0, 0)
    , m_HeadPos(0, 0, 0)
    , m_PrevHeadPos(0, 0, 0)
    , m_SegLenSq(0.0f)
    , m_HeadThickScale(0.0f)
    , m_PendingSplats(0)
    , m_SliceTimerA(0.0f)
    , m_SliceTimerB(0.0f)
    , m_BladeVelAtSlice(0, 0, 0)
    , m_SliceEntityType(0)
    , m_SwipeSoundTimer(0.0f)
    , m_GhostIndex(0)
    , m_GhostCount(0)
    , m_GhostDir(0, 0, 0)
    , m_field_0x118(0.0f)
    , m_SlicePos(0, 0, 0)
    , m_field_0x130(0)
    , m_field_0x134(0.0f)
    , m_field_0x138(-1)
    , m_field_0x13c(-1)
    , m_SwipeFuse(0)
    , m_field_0x144(0.0f)
    , m_field_0x148(-1)
    , m_field_0x14c(-1)
    , m_ComboEntityType(0)
    , m_pComboMissControl(nullptr)  // ASM-verified: 2026-05-18 binary @ 0x0017C82C (re-analyst)
    , m_AngleIndex(0)
#if !defined(__bada__) || defined(FN_ASM_VERIFY_CROSS)
    , m_FingerId(0)
    , m_RawTouchPos(0, 0, 0)
    , m_State(0)
    , m_pCurrentTarget(nullptr)
#endif
{
    memset(_pad4d, 0, sizeof(_pad4d));
    memset(_gap_0xbc, 0, sizeof(_gap_0xbc));
    memset(_gap_0x128, 0, sizeof(_gap_0x128));
    memset(_pad186, 0, sizeof(_pad186));
    for (int i = 0; i < 11; ++i) m_ComboSliceArr[i] = -1;
}

SlashEntity::~SlashEntity() {
    Release();
}

// Port-only convenience: stores fingerId, calls binary-faithful 3-arg Init,
// then registers per-finger input callbacks.
void SlashEntity::Init(int fingerId) {
    m_FingerId = fingerId;
    Init(static_cast<void*>(nullptr), 0L, static_cast<Vec3*>(nullptr));
    RegisterInputCallbacks();
}

void SlashEntity::RegisterInputCallbacks() {
    Mortar::InputManager* mgr = Mortar::InputManager::GetInstance();
    if (!mgr) return;

    char buf[20];
    snprintf(buf, sizeof(buf), "TouchDown_%d", m_FingerId);
    mgr->RegisterInputCallback(StringHash(buf),
        Mortar::Delegate1<bool, InputEvent*>::Make(this, &SlashEntity::TouchDown));

    snprintf(buf, sizeof(buf), "TouchMove_X%d", m_FingerId);
    mgr->RegisterInputCallback(StringHash(buf),
        Mortar::Delegate1<bool, InputEvent*>::Make(this, &SlashEntity::TouchMoveX));

    snprintf(buf, sizeof(buf), "TouchMove_Y%d", m_FingerId);
    mgr->RegisterInputCallback(StringHash(buf),
        Mortar::Delegate1<bool, InputEvent*>::Make(this, &SlashEntity::TouchMoveY));

    snprintf(buf, sizeof(buf), "TouchUp_%d", m_FingerId);
    mgr->RegisterInputCallback(StringHash(buf),
        Mortar::Delegate1<bool, InputEvent*>::Make(this, &SlashEntity::TouchUp));
}

// ASM-verified: 2026-06-13T00:00 binary @ 0x001e79b0 (asm-inspector)
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
    m_PointCount = 0;
    // Defunct: dead BSS guard at 0x0024C848 -- no-op stub; binary @ 0x0017C60C.
    // Binary Release writes a 1-byte 0 to a static slot with no other accessors
    // (likely a once-flag whose set/check sites were inlined out / DCE'd). Port
    // omits the write; semantically equivalent. (re-analyst 2026-05-20)
}

// ---------------------------------------------------------------------------
// Reset -- binary ~0x17B71C
// Wipe touch/trail state; sentinel-fill both vertex strips up to m_SplitPoint;
// clear 11-entry combo-slice array.
// ---------------------------------------------------------------------------
void SlashEntity::Reset() {
    m_PointCount = 0;
    m_State      = 0;

    // Binary @ 0x1e6688: re-arm the anchor sentinel on every touch-down
    // (do/while i!=3 writes (-65535,-65535,-65535) to +0x70/+0x7c/+0x88).
    // DAT_001e67e0 = 0xc77fff00 = -65535.0f.
    static const float kAnchorSentinel = -65535.0f;
    m_TailPos     = Vec3(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
    m_HeadPos     = Vec3(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
    m_PrevHeadPos = Vec3(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "Reset[%d]: seed anchors tail=(%.1f,%.1f,%.1f) head=(%.1f,%.1f,%.1f) prev=(%.1f,%.1f,%.1f) pointCount=%d",
             m_FingerId,
             m_TailPos.x, m_TailPos.y, m_TailPos.z,
             m_HeadPos.x, m_HeadPos.y, m_HeadPos.z,
             m_PrevHeadPos.x, m_PrevHeadPos.y, m_PrevHeadPos.z,
             m_PointCount);
#endif

    if (m_pLeftBuffer && m_pRightBuffer) {
        Colour white(255, 255, 255, 255);
        uint32_t whitePacked = white.PlatformColour();
        for (int i = 0; i < m_SplitPoint; ++i) {
            m_pLeftBuffer[i].colour  = whitePacked;
            m_pRightBuffer[i].colour = whitePacked;
        }
    }

    // DO NOT zero m_RawTouchPos here. TouchMoveX/Y set it just before
    // TouchDown calls Reset, and UpdateTouchDown reads it AFTER Reset to
    // start the new trail at the press position.
    if (m_TrailEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }

    for (int i = 0; i < 11; ++i) m_ComboSliceArr[i] = -1;
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

// Binary @ 0x17B398 -- clears g_state.bombSkipFlag=0, sets g_state.needsDrawFlag=1.
// DIFFERS: g_state is the binary's GameTaskState singleton; bombSkipFlag is
// the "don't slice during bomb-explosion freeze" gate -- port already covers
// this via game_work.m_BombHitTimer > 0 in UpdateTouchDown. needsDrawFlag is the
// SDK's render-needed-this-frame hint; SDL port redraws unconditionally.
// Functionally equivalent no-op.
void SlashEntity::DrawUpdate(float /*dt*/) {
}

// Binary @ 0x17B388 -- clear back-pointer to combo MissControl when deleted.
void SlashEntity::MissControlDeleted(HUDControl* /*ctrl*/) {
    m_pComboMissControl = nullptr;
}

// ---------------------------------------------------------------------------
// PreUpdate, PostUpdate, PlaySwipe, GetHeadThicknessScale, CreateGhost
// ---------------------------------------------------------------------------

void SlashEntity::PostUpdate(float /*dt*/) {}

// ASM-verified: 2026-05-10 binary @ 0x0017C584 (asm-inspector)
void SlashEntity::PreUpdate(float dt) {
    if (g_HitResetCounter < 5) {
        g_HitResetCounter += 1;
    } else {
        g_HitLatch = 0;
    }
    // Port specific: SlashEntityGhost ring (8 slots) deferred.
    // Port specific: ItemManager::PushSwipeLoopVolume deferred.
    if (g_ColourType == 1 /* PER_SLASH */) {
        UpdateModColour(nullptr, dt);
    }
}

// ASM-verified: 2026-05-08 binary @ 0x17CCDC (re-analyst).
// Binary path:
//   if (ItemManager::PlayAlternateSwipeSound(1.0, 1.0) == 0) {
//       int idx = Math::Random::Rand32(g_GlobalRng, 6) + 1;  // [1,6]
//       snprintf(buf, "Sword-swipe-%d", idx);                // literal @ 0x1BCFE3
//       Game::pGameSound->SFXPlay(buf, 1.0, 1.0);
//   }
//   m_SwipeSoundTimer = 6.0f;
void SlashEntity::PlaySwipe() {
    ItemManager* im = ItemManager::GetInstance();
    if (im) {
        im->PlayAlternateSwipeSound(1.0f, 1.0f);
    }

    Game* game = Game::GetInstance();
    if (game && game_work.mGameSound) {
        char buf[20];
        const int idx = (rand() % 6) + 1;
        snprintf(buf, sizeof(buf), "Sword-swipe-%d", idx);
        game_work.mGameSound->SFXPlay(buf, 1.0f, 1.0f);
    }

    m_SwipeSoundTimer = 6.0f;
}

// Binary @ 0x17B87C -- derive head taper scale from last vertex pair.
// Port specific: binary reads the last pair half-width from m_pLeftBuffer;
// only consumed by CreateGhost() which is a no-op stub. Return 1.0f.
float SlashEntity::GetHeadThicknessScale() const {
    return 1.0f;
}

// Binary @ 0x17B82C -- snapshot blade vertex strips into global ghost ring.
// Port specific: SlashEntityGhost ring not yet ported. No-op stub.
// ASM-verified: 2026-05-18 binary @ 0x0017B82C (re-analyst)
void SlashEntity::CreateGhost() {
}

// ---------------------------------------------------------------------------
// UpdateModColour -- binary @ 0x17B0F4
// ASM-verified: 2026-05-09 binary @ 0x0017B0F4 (re-analyst)
// ---------------------------------------------------------------------------
void SlashEntity::UpdateModColour(Colour* outColour, float dt) {
    if (dt == 0.0f) return;

    const int count = g_ColourCount;

    if (g_ColourType == 1 /* PER_SLASH */) {
        g_PaletteProgress += dt * g_LifeScale;
        while (g_PaletteProgress >= (float)count) g_PaletteProgress -= (float)count;
        while (g_PaletteProgress <  0.0f)         g_PaletteProgress += (float)count;

        if (outColour) {
            const float snapHalf = (float)(int)(g_PaletteProgress + 0.5f);
            const float frac = g_PaletteProgress - snapHalf;
            const bool inSnap = (frac > -0.01f) && (frac < 0.01f);

            if (inSnap) {
                *outColour = g_Palette[(int)snapHalf % count];
            } else {
                const int i0 = (int)g_PaletteProgress;
                const int i1 = (i0 + 1) % count;
                const float t = g_PaletteProgress - (float)i0;
                outColour->r = (uint8_t)((float)g_Palette[i0].r + (float)(g_Palette[i1].r - g_Palette[i0].r) * t);
                outColour->g = (uint8_t)((float)g_Palette[i0].g + (float)(g_Palette[i1].g - g_Palette[i0].g) * t);
                outColour->b = (uint8_t)((float)g_Palette[i0].b + (float)(g_Palette[i1].b - g_Palette[i0].b) * t);
                outColour->a = (uint8_t)((float)g_Palette[i0].a + (float)(g_Palette[i1].a - g_Palette[i0].a) * t);
            }
        }
    } else if (g_ColourType == 2 /* PER_SWIPE */) {
        if (outColour && count > 0) {
            *outColour = g_Palette[(int)g_PaletteProgress % count];
        }
    }
    // ColourType 0 (NONE): no animation, no write.
}

// ---------------------------------------------------------------------------
// Touch ingestion
// ---------------------------------------------------------------------------
void SlashEntity::OnTouchActive(float x, float y) {
    Vec3 newPos(x, y, 0.0f);
    m_RawTouchPos = newPos;

    const Vec3 lastCenter = m_TailPos;
    const Vec3 distVec(newPos.x - lastCenter.x, newPos.y - lastCenter.y, 0.0f);
    const float distSq = distVec.x * distVec.x + distVec.y * distVec.y;

    // Binary @ 0x1e9f08 (UpdateTouchDown): gate is tail.x <= -65520.0f (DAT_001ea3f8).
    // -65535 (sentinel) <= -65520, so a freshly Reset blade always hits the SEED branch.
    const bool isSeed = (m_TailPos.x <= -65520.0f);

    // Distance threshold: active blade uses MOVE_THRESH_ACTIVE^2, inactive uses MOVE_THRESH_INACTIVE^2.
    // Binary: (this[0x140] & bit0) ? 25.0 : 2500.0.
    const float thresh = (m_State != 0)
        ? (MOVE_THRESH_ACTIVE   * MOVE_THRESH_ACTIVE)
        : (MOVE_THRESH_INACTIVE * MOVE_THRESH_INACTIVE);

#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "OnTouchActive[%d]: pos=(%.2f,%.2f) isSeed=%d distSq=%.2f thresh=%.2f tail_x=%.1f",
             m_FingerId, x, y, (int)isSeed, distSq, thresh, m_TailPos.x);
#endif

    if (distSq < thresh && !isSeed) {
        // Binary LAB_001ea3d0: nothing to add this frame when close to tail
        // and not a seed frame. (Binary also early-outs when m_PointCount>0.)
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "OnTouchActive[%d]: skipped (below thresh, not seed) pointCount=%d",
                 m_FingerId, m_PointCount);
#endif
        return;
    }

    Vec3 dir;
    if (isSeed) {
        // Binary LAB_001ea1b4: copy current touch pos into all three anchors.
        m_TailPos     = newPos;
        m_HeadPos     = newPos;
        m_PrevHeadPos = newPos;
        m_PointCount  = 0;
        m_State       = 1;
        m_BladeDir    = Vec3(1.0f, 0.0f, 0.0f); // non-zero seed so AddPoint guard passes
        // Binary computes seed direction from DAT_001ea41c (global ref vec) - tail.
        // Using (1,0,0) matches binary's "non-degenerate first direction" intent.
        dir = Vec3(1.0f, 0.0f, 0.0f);
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "OnTouchActive[%d]: SEED branch -> anchors=(%.2f,%.2f) pointCount=%d",
                 m_FingerId, x, y, m_PointCount);
#endif
    } else {
        const float dist = sqrtf(distSq);
        dir = Vec3(distVec.x / dist, distVec.y / dist, 0.0f);

        // Interpolate intermediate points every POINT_SPACING units.
        float travelled = POINT_SPACING;
        while (travelled < dist) {
            Vec3 step(lastCenter.x + dir.x * travelled,
                      lastCenter.y + dir.y * travelled, 0.0f);
            AddPoint(1.0f, &step, &dir);
            travelled += POINT_SPACING;
        }
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "OnTouchActive[%d]: ADD branch dist=%.2f dir=(%.2f,%.2f) pointCount=%d",
                 m_FingerId, dist, dir.x, dir.y, m_PointCount);
#endif
    }

    // Always lay the head point at the live touch position.
    AddPoint(1.0f, &newPos, &dir);

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

    m_State = 1;
}

void SlashEntity::OnTouchReleased() {
    if (m_State == 1) m_State = 2;
#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "OnTouchReleased[%d]: stroke ended state=%d pointCount=%d",
             m_FingerId, (int)m_State, m_PointCount);
#endif
}

// ---------------------------------------------------------------------------
// AddPoint -- binary @ 0x1e9bf4 (v1.6.1)
// Appends one vertex pair (center + edge) to both ribbon buffers.
// Called when finger moves far enough (spacing gate) or on seed.
//
// ORIGINAL_SLASH constants (hardcoded; SetEquipped path not yet ported):
//   startW   = 0.0  (m_ScaleLength    @ SlashModInfo+0x70 / binary 0x332BCC)
//   endW     = 1.0  (m_ScaleEndThick  @ SlashModInfo+0x6c / binary 0x2D8D78)
//   widthDiv = 1.0  (m_ScalePointScale @ SlashModInfo+0x74)
//   headTaper = 0.0
//
// Head half-width = dt*10*endW = dt*10*1.0 = dt*10 (binary @ 0x1e9c08 s16 = param_1 * 10.0).
// Edge offset = miterDir * halfWidth where miterDir = CosIdx/SinIdx(m_AngleIndex).
//
// Point-spacing gate: segLen > dt*10*(endW+(startW-endW)*0.6) = dt*10*0.4.
// DAT_001e9eb0 = 0.6 (binary @ 0x1e9be8..0x1e9c70).
// First call (m_PointCount==0) bypasses gate (seed path).
//
// Scroll cap: if m_SplitPoint-2 <= m_PointCount, slide buffers down by one pair
//   and set m_PointCount = m_SplitPoint-4.
// ---------------------------------------------------------------------------
void SlashEntity::AddPoint(float pressure, const Vec3* center, const Vec3* dir) {
    (void)pressure;
    if (!m_pLeftBuffer || !m_pRightBuffer) return;
    if (!center || !dir) return;

    // Guard: zero-length direction -> skip.
    if (dir->MagnitudeSqr() < 1e-8f) {
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "AddPoint[%d]: SKIP dir near-zero pos=(%.2f,%.2f) pointCount=%d",
                 m_FingerId, center->x, center->y, m_PointCount);
#endif
        return;
    }

    // ORIGINAL_SLASH constants.
    static const float kEndW      = 1.0f;    // endW   @ SlashModInfo+0x6c / 0x2D8D78
    static const float kStartW    = 0.0f;    // startW @ SlashModInfo+0x70 / 0x332BCC
    static const float kDt        = 1.0f / 60.0f;  // fixed ARM32 timestep

    // Head half-width: dt*10*endW (binary @ 0x1e9c08 s16 = param_1 * 10.0).
    const float halfWidth = kDt * 10.0f * kEndW;

    // Point-spacing gate: segLen > dt*10*(endW+(startW-endW)*0.6) = dt*10*0.4.
    // Applied only when trail already has at least one point.
    // DAT_001e9eb0 = 0.6 (binary @ 0x1e9be8..0x1e9c70).
    static const float kSpacingThresh = kDt * 10.0f * (kEndW + (kStartW - kEndW) * 0.6f);
    if (m_PointCount > 0) {
        float dx = center->x - m_HeadPos.x;
        float dy = center->y - m_HeadPos.y;
        float segLen = sqrtf(dx * dx + dy * dy);
        if (segLen <= kSpacingThresh) {
#ifdef FN_DEBUG_TOUCH
            LOG_DEBUG("SLASH", "AddPoint[%d]: SKIP spacing gate segLen=%.3f thresh=%.3f",
                     m_FingerId, segLen, kSpacingThresh);
#endif
            return;
        }
    }

    // Ghost-ring averaging bookkeeping (dir-history update @ 0x1e9bf4).
    {
        unsigned int slot = m_GhostIndex % 6;
        float* ringSlot = reinterpret_cast<float*>(_gap_0xbc + slot * 12);
        ringSlot[0] = dir->x;
        ringSlot[1] = dir->y;
        ringSlot[2] = dir->z;

        if (m_GhostCount < 6) m_GhostCount++;
        m_GhostIndex++;

        // Average over filled ghost slots -> m_GhostDir.
        float ax = 0.0f, ay = 0.0f, az = 0.0f;
        unsigned int n = m_GhostCount;
        for (unsigned int i = 0; i < n; ++i) {
            const float* s = reinterpret_cast<const float*>(_gap_0xbc + i * 12);
            ax += s[0]; ay += s[1]; az += s[2];
        }
        if (n > 0) { ax /= (float)n; ay /= (float)n; az /= (float)n; }
        Vec3 avgDir(ax, ay, az);
        Vec3 newest(dir->x, dir->y, dir->z);

        Vec3 diff(avgDir.x - newest.x, avgDir.y - newest.y, avgDir.z - newest.z);
        // Binary @ 0x1e9bf4: if (MagnitudeSqr(avgDir-newest) > 1.69f)
        //   m_field_0x118 = 0.095f. (DAT_001e9ea8=1.69, DAT_001e9eac=0.095)
        if (diff.MagnitudeSqr() > 1.69f) {
            m_field_0x118 = 0.095f;
        }
        m_GhostDir = avgDir;
    }

    // Update blade direction and angle index.
    m_BladeDir = *dir;
    short angle = Math::Atan2Idx(-dir->x, dir->y);
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
        m_field_0x138 -= 2;
    }

    // Reset head-thickness scale to 1.0 each AddPoint (binary @ 0x1e9bf4).
    m_HeadThickScale = 1.0f;

    const uint32_t col = m_BaseColour.PlatformColour();
    const int centerIdx = m_PointCount;
    const int edgeIdx   = m_PointCount + 1;

    // Center vertex (spine): written identically to both buffers.
    m_pLeftBuffer[centerIdx].x      = center->x;
    m_pLeftBuffer[centerIdx].y      = center->y;
    m_pLeftBuffer[centerIdx].z      = center->z;
    m_pLeftBuffer[centerIdx].nx     = 0.0f;
    m_pLeftBuffer[centerIdx].ny     = 0.0f;
    m_pLeftBuffer[centerIdx].nz     = 1.0f;
    m_pLeftBuffer[centerIdx].colour = col;
    m_pLeftBuffer[centerIdx].u      = 0.5f;
    m_pLeftBuffer[centerIdx].v      = 0.5f;

    m_pRightBuffer[centerIdx].x      = center->x;
    m_pRightBuffer[centerIdx].y      = center->y;
    m_pRightBuffer[centerIdx].z      = center->z;
    m_pRightBuffer[centerIdx].nx     = 0.0f;
    m_pRightBuffer[centerIdx].ny     = 0.0f;
    m_pRightBuffer[centerIdx].nz     = 1.0f;
    m_pRightBuffer[centerIdx].colour = col;
    m_pRightBuffer[centerIdx].u      = 0.5f;
    m_pRightBuffer[centerIdx].v      = 0.5f;

    // Edge vertex: center +/- unitPerp*halfWidth.
    // V maps ACROSS ribbon width: left-edge=0.0, center=0.5, right-edge=1.0.
    // blade.tex body is opaque by design; only the last few texel rows fade.
    m_pLeftBuffer[edgeIdx].x      = center->x - halfX;
    m_pLeftBuffer[edgeIdx].y      = center->y - halfY;
    m_pLeftBuffer[edgeIdx].z      = center->z;
    m_pLeftBuffer[edgeIdx].nx     = 0.0f;
    m_pLeftBuffer[edgeIdx].ny     = 0.0f;
    m_pLeftBuffer[edgeIdx].nz     = 1.0f;
    m_pLeftBuffer[edgeIdx].colour = col;
    m_pLeftBuffer[edgeIdx].u      = 0.5f;
    m_pLeftBuffer[edgeIdx].v      = 0.0f;

    m_pRightBuffer[edgeIdx].x      = center->x + halfX;
    m_pRightBuffer[edgeIdx].y      = center->y + halfY;
    m_pRightBuffer[edgeIdx].z      = center->z;
    m_pRightBuffer[edgeIdx].nx     = 0.0f;
    m_pRightBuffer[edgeIdx].ny     = 0.0f;
    m_pRightBuffer[edgeIdx].nz     = 1.0f;
    m_pRightBuffer[edgeIdx].colour = col;
    m_pRightBuffer[edgeIdx].u      = 0.5f;
    m_pRightBuffer[edgeIdx].v      = 1.0f;

    m_PointCount += 2;

#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "AddPoint[%d]: ADDED center=(%.2f,%.2f) halfW=%.3f idx=%d -> pointCount=%d",
             m_FingerId, center->x, center->y, halfWidth, centerIdx, m_PointCount);
#endif

    m_PrevHeadPos = m_HeadPos;
    m_HeadPos     = *center;
}

// ---------------------------------------------------------------------------
// UpdatePoints -- binary @ 0x1e6914 (v1.6.1)
// Per-frame full geometry rebuild: miter vectors from segment lengths, arc-length
// U mapping, head cap.
//
// ORIGINAL_SLASH constants (hardcoded; SetEquipped path not yet ported):
//   startW (cfgA) = 0.0 @ 0x332BCC, endW (cfgB) = 1.0 @ 0x2D8D78, cfgDiv = 1.0 @ 0x2D8D74
//
// Body point half-width (binary @ 0x1e6914, aging coefficient DAT_001e6f20 = -45.0f):
//   halfWidth = segLen + (timeScale * -45.0f * (cfgB - cfgA)) / cfgDiv
// For default config (cfgB-cfgA)=1.0, cfgDiv=1.0:
//   halfWidth = segLen - 45.0f * timeScale
// where timeScale = dt (1/60) for normal points, 2*dt when (m_SplitPoint*2 <= pointIndex).
// segLen = distance between CONSECUTIVE trail centers (stride-1 in points, stride-2 in vertices).
// Speed-proportional ribbon: faster swipe = wider, slower = thinner, slightly tapered with age.
//
// unitPerp = Normalise(Cross(segDir, +Z)) = (-segDir.y, segDir.x, 0) -- must be unit-length.
// Edge offset = center +/- unitPerp * halfWidth (half on each side, full width = 2*halfWidth).
//
// Retract (m_BladeActive off): retire oldest point pair each frame.
// TODO: 0x1e6914 -- exact tail-retire trigger: rate TBD from binary; current
//   implementation retires 2 vertices per frame while !m_BladeActive (m_State==0),
//   giving ~trail_len/120 s drain time at 60fps. Tune against binary when RE'd.
// ---------------------------------------------------------------------------
void SlashEntity::UpdatePoints(float dt) {
    (void)dt;
    if (!m_pLeftBuffer || !m_pRightBuffer) return;

    // Sync m_Angle from m_AngleIndex (binary @ 0x1e6914 early).
    m_Angle = (uint16_t)m_AngleIndex;

    // Validity gate: m_PointCount < 4 or !m_BladeActive -> reset trail/shift fields.
    // m_BladeActive in binary = blade is being stroked (AddPoint feeding). Port uses
    // m_State == 1 (actively stroking) as the equivalent.
    if (m_PointCount < 4 || m_State != 1) {
        m_field_0x138 = -1;
        m_field_0x13c = -1;
        m_SegLenSq    = -1.0f;
        m_BladeDir    = Vec3(0.0f, 0.0f, 0.0f);
    } else {
        // Update ColLine collision segment from trail anchors (binary @ 0x1e6914).
        if (m_Col) {
            ColLine* cl = static_cast<ColLine*>(m_Col);
            cl->a() = m_TailPos;
            cl->b   = m_HeadPos;
        }
        Vec3 segDiff(m_HeadPos.x - m_TailPos.x,
                     m_HeadPos.y - m_TailPos.y,
                     m_HeadPos.z - m_TailPos.z);
        m_SegLenSq = segDiff.MagnitudeSqr();
    }

    if (m_PointCount < 4) {
        // Retract: drain oldest pair when blade is released (m_State != 1).
        if (m_State != 1 && m_PointCount >= 2) {
            int keep = m_PointCount - 2;
            if (keep > 0) {
                memmove(m_pLeftBuffer,  m_pLeftBuffer  + 2, keep * sizeof(QUADCUSTOMVERTEX));
                memmove(m_pRightBuffer, m_pRightBuffer + 2, keep * sizeof(QUADCUSTOMVERTEX));
            }
            m_PointCount = keep;
        }
        return;
    }

    // Derive m_BaseColour from m_Scale / m_HighlightColour envelope
    // (m_Scale decays at -2/s in Update; drives highlight/critical tint).
    if (g_ColourType == 1) {
        UpdateModColour(&m_HighlightColour, -2.0f / (float)m_PointCount);
    }
    if (m_Scale > 0.0f) {
        const float blend = 1.0f - m_Scale;
        m_BaseColour.r = (uint8_t)(255.0f + (float)((int)m_HighlightColour.r - 255) * blend);
        m_BaseColour.g = (uint8_t)(255.0f + (float)((int)m_HighlightColour.g - 255) * blend);
        m_BaseColour.b = (uint8_t)(255.0f + (float)((int)m_HighlightColour.b - 255) * blend);
        m_BaseColour.a = (uint8_t)(255.0f + (float)((int)m_HighlightColour.a - 255) * blend);
    } else {
        m_BaseColour = m_HighlightColour;
    }
    // Per-vertex alpha ramp: tail (pi=0) transparent, head (pi=pairCount-1) opaque.
    // The binary drives alpha per-vertex from trail-point age (v1.5.1 TRAIL_LIFETIME=0.25s).
    // The v1.6.1 binary layout stores geometry directly in vertex buffers without a
    // separate age array; position-in-strip is used as the equivalent ramp here.
    // TODO: 0x1e6914 -- exact ramp is age-based (v1.5.1 TRAIL_LIFETIME=0.25); using
    //   position-along-trail as equivalent. Add a per-point age array when the binary's
    //   exact age-decay coefficients are confirmed for v1.6.1.
    // Alpha is also modulated by m_BaseColour.a so the m_Scale fade-out still applies.

    // Retract when blade released: retire oldest pair each frame.
    if (m_State != 1 && m_PointCount >= 2) {
        int keep = m_PointCount - 2;
        if (keep > 0) {
            memmove(m_pLeftBuffer,  m_pLeftBuffer  + 2, keep * sizeof(QUADCUSTOMVERTEX));
            memmove(m_pRightBuffer, m_pRightBuffer + 2, keep * sizeof(QUADCUSTOMVERTEX));
        }
        m_PointCount = keep;
        if (m_PointCount < 4) return;
    }

    // MAIN geometry rebuild loop (binary @ 0x1e6914).
    // Iterates pairs at step 2 (center@k, edge@k+1). For each pair:
    //   halfWidth = segLen - 45.0f * timeScale  (aging coefficient DAT_001e6f20 = -45.0f)
    //   timeScale = dt, or 2*dt when (m_SplitPoint*2 <= pointIndex) (binary @0x1e6c40).
    //   unitPerp = Normalise(Cross(segDir, +Z)) = (-segDir.y, segDir.x, 0) -- always unit-length.
    //   edge offset = center +/- unitPerp * halfWidth.
    //   For the very first pair (k==0) use angle table halfWidth = dt*10 (no prev center).
    // V coordinate = (i / (pairCount-1)) * 0.98 (from 0=tail to 0.98=head).
    // After loop: U-remap pass (arc-length parameterized, 0.98*arc[k]/arc[last]).

    static const int MAX_PAIRS = 80;
    float arcLen[MAX_PAIRS + 1];
    arcLen[0] = 0.0f;

    static const float kAgingCoeff = -45.0f;   // DAT_001e6f20
    static const float kCfgB       =  1.0f;    // endThick  @ 0x2D8D78
    static const float kCfgA       =  0.0f;    // startThick @ 0x332BCC
    static const float kCfgDiv     =  1.0f;    // @ 0x2D8D74
    static const float kBodyDt     =  1.0f / 60.0f;

    int pairCount = m_PointCount / 2;
    if (pairCount > MAX_PAIRS) pairCount = MAX_PAIRS;

    for (int pi = 0; pi < pairCount; ++pi) {
        int k = pi * 2;

        float cx = m_pLeftBuffer[k].x;
        float cy = m_pLeftBuffer[k].y;
        float cz = m_pLeftBuffer[k].z;

        // Body halfWidth = segLen - 45.0f * timeScale.
        // timeScale doubles for older tail points (binary @0x1e6c40: vmovlt/vaddge).
        // First pair uses angle table (no prev center).
        float halfWidth;
        float miterX, miterY;
        if (pi > 0) {
            float px = m_pLeftBuffer[k - 2].x;
            float py = m_pLeftBuffer[k - 2].y;
            float ddx = cx - px;
            float ddy = cy - py;
            float segLen = sqrtf(ddx * ddx + ddy * ddy);

            // timeScale doubles for points past the split boundary (binary @0x1e6c40).
            float timeScale = (m_SplitPoint * 2 <= k) ? (2.0f * kBodyDt) : kBodyDt;

            halfWidth = segLen + (timeScale * kAgingCoeff * (kCfgB - kCfgA)) / kCfgDiv;

            // unitPerp = Normalise(Cross(segDir, +Z)) = (-segDir.y, segDir.x, 0).
            // segDir = (ddx, ddy) / segLen. unitPerp = (-ddy/segLen, ddx/segLen).
            float tlen = (segLen > 1e-6f) ? segLen : 1e-6f;
            float unitPerpX = -ddy / tlen;
            float unitPerpY =  ddx / tlen;
            miterX = unitPerpX * halfWidth;
            miterY = unitPerpY * halfWidth;
        } else {
            // First pair: fallback to head angle table (binary @ 0x1e6914 head path).
            // halfWidth matches AddPoint head half-width: dt*10 (binary @ 0x1e9c08).
            halfWidth = kBodyDt * 10.0f;
            // unitPerp from angle table: (CosIdx, SinIdx) is unit-length by definition.
            miterX = CosIdx(m_Angle) * halfWidth;
            miterY = SinIdx(m_Angle) * halfWidth;
        }

        // V coordinate: 0 at tail, 0.98 at head (binary DAT_001e6f5c = 0.98).
        float vCoord = (pairCount > 1)
            ? ((float)pi / (float)(pairCount - 1)) * 0.98f
            : 0.0f;

        // Per-pair alpha ramp: position 0 (tail) -> alpha 0, position last (head) -> alpha m_BaseColour.a.
        // alphaFrac = pi / (pairCount-1), clamped [0,1].
        float alphaFrac = (pairCount > 1) ? ((float)pi / (float)(pairCount - 1)) : 1.0f;
        if (alphaFrac < 0.0f) alphaFrac = 0.0f;
        if (alphaFrac > 1.0f) alphaFrac = 1.0f;
        uint32_t alpha = (uint32_t)(alphaFrac * (float)m_BaseColour.a);
        uint32_t col = (alpha << 24)
                     | ((uint32_t)m_BaseColour.b << 16)
                     | ((uint32_t)m_BaseColour.g <<  8)
                     |  (uint32_t)m_BaseColour.r;

        // Center (spine) vertex -- identical for both buffers.
        m_pLeftBuffer[k].colour = col;
        m_pLeftBuffer[k].u      = vCoord;   // U will be overwritten by arc-length pass
        m_pLeftBuffer[k].v      = 0.5f;

        m_pRightBuffer[k].colour = col;
        m_pRightBuffer[k].u      = vCoord;
        m_pRightBuffer[k].v      = 0.5f;

        // Edge (miter) vertices: left = center - miter, right = center + miter.
        // V maps ACROSS ribbon width: left-edge=0.0, center=0.5, right-edge=1.0.
        m_pLeftBuffer[k+1].x      = cx - miterX;
        m_pLeftBuffer[k+1].y      = cy - miterY;
        m_pLeftBuffer[k+1].z      = cz;
        m_pLeftBuffer[k+1].colour = col;
        m_pLeftBuffer[k+1].u      = vCoord;
        m_pLeftBuffer[k+1].v      = 0.0f;

        m_pRightBuffer[k+1].x      = cx + miterX;
        m_pRightBuffer[k+1].y      = cy + miterY;
        m_pRightBuffer[k+1].z      = cz;
        m_pRightBuffer[k+1].colour = col;
        m_pRightBuffer[k+1].u      = vCoord;
        m_pRightBuffer[k+1].v      = 1.0f;

        // Accumulate arc length for U-remap pass.
        float segArc = 0.0f;
        if (pi > 0) {
            float px = m_pLeftBuffer[k - 2].x;
            float py = m_pLeftBuffer[k - 2].y;
            float ddx = cx - px;
            float ddy = cy - py;
            segArc = sqrtf(ddx * ddx + ddy * ddy);
        }
        arcLen[pi + 1] = arcLen[pi] + segArc;
    }

    // U-remap pass: U = 0.98 * arc[k] / arc[last] (tail U~0, head U~0.98).
    // DAT_001e6f5c = 0.98 (binary @ 0x1e6914 u-taper constant).
    {
        float totalArc = (pairCount > 0) ? arcLen[pairCount] : 1.0f;
        if (totalArc < 1e-6f) totalArc = 1.0f;
        for (int pi = 0; pi < pairCount; ++pi) {
            float u = 0.98f * arcLen[pi] / totalArc;
            int k = pi * 2;
            m_pLeftBuffer[k].u    = u;
            m_pLeftBuffer[k+1].u  = u;
            m_pRightBuffer[k].u   = u;
            m_pRightBuffer[k+1].u = u;
        }
    }

    // HEAD CAP (binary @ 0x1e6914 cap path): gate m_PointCount > 2.
    // Appends a single apex vertex at lastCenter +/- capEdge with U=1.0, V=0.5.
    // capEdge = Cross(tipDir, +Z) * 2.5 (DAT 2.5 from binary).
    if (m_PointCount > 2) {
        const int n = m_PointCount;
        // Last pair: center at [n-2], edge at [n-1].
        float lastCX = m_pLeftBuffer[n - 2].x;
        float lastCY = m_pLeftBuffer[n - 2].y;
        float lastCZ = m_pLeftBuffer[n - 2].z;

        // tipDir = normalize(lastCenter - prevCenter).
        float tipDirX = 0.0f, tipDirY = 0.0f;
        if (n >= 4) {
            float prevCX = m_pLeftBuffer[n - 4].x;
            float prevCY = m_pLeftBuffer[n - 4].y;
            float tdx = lastCX - prevCX;
            float tdy = lastCY - prevCY;
            float tlen = sqrtf(tdx * tdx + tdy * tdy);
            if (tlen > 1e-6f) { tipDirX = tdx / tlen; tipDirY = tdy / tlen; }
        } else {
            tipDirX = CosIdx(m_Angle);
            tipDirY = SinIdx(m_Angle);
        }

        // capEdge = Cross(tipDir, +Z) * 2.5 = (-tipDir.y, tipDir.x, 0) * 2.5.
        float capX = -tipDirY * 2.5f;
        float capY =  tipDirX * 2.5f;

        uint32_t capCol = m_BaseColour.PlatformColour();

        // Left cap: lastCenter - capEdge; right cap: lastCenter + capEdge.
        m_pLeftBuffer[n].x      = lastCX - capX;
        m_pLeftBuffer[n].y      = lastCY - capY;
        m_pLeftBuffer[n].z      = lastCZ;
        m_pLeftBuffer[n].colour = capCol;
        m_pLeftBuffer[n].u      = 1.0f;
        m_pLeftBuffer[n].v      = 0.5f;

        m_pRightBuffer[n].x      = lastCX + capX;
        m_pRightBuffer[n].y      = lastCY + capY;
        m_pRightBuffer[n].z      = lastCZ;
        m_pRightBuffer[n].colour = capCol;
        m_pRightBuffer[n].u      = 1.0f;
        m_pRightBuffer[n].v      = 0.5f;

        // Bump global head-cap frame counter (blade-mod +0xbc @ 0x00332b34).
        g_HeadCapFrameCounter += 1;
    }

    // Update head/tail tracking from surviving spine vertices (even indices).
    if (m_PointCount >= 2) {
        m_TailPos.x = m_pLeftBuffer[0].x;
        m_TailPos.y = m_pLeftBuffer[0].y;
        m_TailPos.z = m_pLeftBuffer[0].z;
        int headSpine = m_PointCount - 2;
        if (headSpine < 0) headSpine = 0;
        m_HeadPos.x = m_pLeftBuffer[headSpine].x;
        m_HeadPos.y = m_pLeftBuffer[headSpine].y;
        m_HeadPos.z = m_pLeftBuffer[headSpine].z;
    }
}

// ---------------------------------------------------------------------------
// Update -- matches SlashEntity::Update (0x1e867k v1.6.1)
// ---------------------------------------------------------------------------
void SlashEntity::Update(float dt) {
    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    const bool bladeActive = (m_State != 0) && (m_PointCount > 0);
    const bool wantTrail = bladeActive && g_DirectionalFlag != 0 && g_TrailHash != 0;
    if (wantTrail) {
        if (!m_TrailEmitter) {
            m_TrailEmitter = pm.AddEmitter(g_TrailHash, &m_TrailEmitter, /*persistent=*/true);
            if (m_TrailEmitter) {
                m_TrailEmitter->m_bUpdateWhenPaused = true;
            }
        }
        if (m_TrailEmitter) {
            m_TrailEmitter->m_Pos = m_RawTouchPos;
        }
    } else if (!bladeActive && m_TrailEmitter) {
        pm.ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }

    // Per-frame UpdatePoints: re-derive alpha/colour, fade tail if released.
    UpdatePoints(dt);

    // State machine collapse: trail fully drained after release.
    if (m_State == 2 && m_PointCount == 0) {
        m_State = 0;
    }

    // Slice-test pass.
    Game* game = Game::GetInstance();
    const bool bombHitActive = game && game_work.m_BombHitTimer > 0.0f;

    // Binary @ 0x17D664: bit 0x40 = ScrollingMenu drag-acquire suppresses slicing.
    const bool menuDragActive = (SlashEntity::s_ModPowerMask & 0x40u) != 0u;

    // Port-side pause gate (belt-and-braces; see comment in old Update).
    const bool gamePaused = game && game_work.m_Paused;

    // Tick swipe-SFX cooldown.
    if (m_SwipeSoundTimer > 0.0f) {
        m_SwipeSoundTimer -= 1.0f;
        if (m_SwipeSoundTimer < 0.0f) m_SwipeSoundTimer = 0.0f;
    }

    // binary @0x1e98b0: normal mode: m_Scale += dt * -2.0f; clamp >= 0.
    // critical/charged mode would add +2.0f and clamp <= 1; handled at slice site
    // where m_Scale is set to 1.0f (binary @0x0017d9b4).
    m_Scale -= 2.0f * dt;
    if (m_Scale < 0.0f) m_Scale = 0.0f;

    bool slicedThisFrame = false;
    if (m_PointCount >= 2 && m_State != 0 && !bombHitActive
        && !menuDragActive && !gamePaused) {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        if (am) {
            // ASM-verified: 2026-06-07 binary @ 0x0017d664/0x0017c596 (re-analyst)
            for (int t = 0; t <= 1; t++) {
                if (g_HitLatch != 0) break;
                const std::list<Mortar::Entity*>& list = am->GetTypeList(t);
                for (std::list<Mortar::Entity*>::const_iterator it = list.begin(); it != list.end(); ++it) {
                    if (g_HitLatch != 0) break;
                    Mortar::Entity* e = *it;
                    if (!e) continue;
                    // ASM-verified: 2026-05-20 binary @ 0x0017D788 (re-analyst)
                    if (t == 0 && static_cast<Fruit*>(e)->Sliced()) continue;
                    if (!e->IsActive()) continue;
                    if (!e->m_Col) continue;
                    ColSphere* cs = static_cast<ColSphere*>(e->m_Col);
                    if (cs->radius <= 0.0f) continue;

                    Vec3 bladeVel;
                    if (CollideWithSphere(*cs, bladeVel)) {
                        LOG_DEBUG("SLASH", "hit %s %p at (%.1f,%.1f) trail_n=%d",
                                    t == 0 ? "fruit" : "bomb",
                                    static_cast<void*>(e), cs->center().x, cs->center().y, m_PointCount);
                        e->CollisionResponse(nullptr, 0, 0, &bladeVel);
                        if (t == 0) {
                        // ASM-verified: 2026-06-07 binary @ 0x0017d664 (re-analyst)
                        m_SliceTimerA    = 0.0f;
                        m_SliceTimerB    = 0.0f;
                        m_PendingSplats += 2;
                        m_BladeVelAtSlice = m_BladeDir;
                        m_SlicePos        = e->pos;
                        }
                        // ASM-verified: 2026-05-22 binary @ 0x0017d8a4 (re-analyst).
                        const bool isMenuFruit = (t == 0) &&
                            static_cast<Fruit*>(e)->m_bMenuFling != 0;
                        if (t == 0) {
                            Fruit* fruit = static_cast<Fruit*>(e);
                            m_SliceEntityType = (int)fruit->m_FruitType;
                            if (!isMenuFruit) {
                            if (fruit->m_bCritical) {
                                m_Scale = 1.0f;
                            }
                            // ASM-verified: 2026-05-20 binary @ 0x0017dad8 (re-analyst)
                            m_ComboTimerRef() = 0.0f;
                            m_ComboSliceArr[m_ComboCountVal()] = (int)fruit->m_FruitType;
                            m_ComboEntityType = (m_FingerId == 0) ? 0 : (m_FingerId == 2 ? 2 : 1);
                            m_ComboCountRef() += 1;
                            if (m_ComboCountVal() >= 10) m_ComboTimerRef() = 0.095f;
                            m_SwipeSoundTimer -= (float)m_ComboCountVal() * (Math::g_Random.RandF(0.5f) + 0.75f);
                            // Binary @ 0x0017dad8: popup gated on (2 < combo) && CombosEnabled().
                            if (m_ComboCountVal() > 2 && game_work.gameMode != Mortar::GAME_MODE_COMBO) {
                                bool online = Mortar::NetworkManager::GetInstance()->IsOnlineMultiplayer();
                                if (!online || m_ComboEntityType != 2) {
                                    if (m_pComboMissControl == nullptr) {
                                        m_pComboMissControl = MissControl::GetFree();
                                        if (m_pComboMissControl) {
                                            m_pComboMissControl->MakeCombo(m_SlicePos, m_ComboCountVal(), m_ComboEntityType);
                                            // ASM-verified: 2026-05-20 binary @ 0x0017d8e4..0x0017d908 (re-analyst).
                                            m_pComboMissControl->m_RemoveCallback =
                                                Mortar::Delegate1<void, HUDControl*>::Make(
                                                    this, &SlashEntity::MissControlDeleted);
                                        }
                                    } else {
                                        Vec3 existingPos = m_pComboMissControl->pos;
                                        m_pComboMissControl->MakeCombo(existingPos, m_ComboCountVal(), m_ComboEntityType);
                                    }
                                }
                            }
                            } // !isMenuFruit
                            // Binary @ 0x17d9c0: special/menu fruit sets latch.
                            if (isMenuFruit) {
                                g_HitLatch        = 1;
                                g_HitResetCounter = 0;
                            }
                        }
                        if (t == 1) {
                            // Binary @ 0x0017db7e-0x17db9e: bomb always sets latch.
                            Bomb* bomb = static_cast<Bomb*>(e);
                            g_HitLatch        = 1;
                            g_HitResetCounter = 0;
                            if (bomb->m_bHit && !bomb->m_bMenuBombHit) {
                                m_SwipeEndEdge = 1;
                            }
                        }
                        slicedThisFrame = true;
                    }
                }
            }
        }
    }

    if (slicedThisFrame && m_SwipeSoundTimer == 0.0f) {
        PlaySwipe();
    }

    // Per-swipe combo resolution / combo-cancel timer -- binary SlashEntity::Update
    // @ 0x1e90d4. The cancel timer is m_field_0x118 (+0x118), DISTINCT from the
    // per-slice accumulator m_ComboTimerRef() (+0x174). Each frame +0x118 advances
    // by dt; when it reaches DAT_001e9224 (=0.095f) the combo window closes:
    // g_OnComboCancel fires, the combo is resolved, then +0x118 is pinned to
    // DAT_001e9220 (=0.1f). Once it is >= 0.1f the timer is idle and only resets
    // combo state (no fire). m_field_0x118 is pumped to 0.095f by the slice path
    // (binary writes DAT_001e8a88=0.095f at this+0x118 once combo count >= 10), and
    // is otherwise driven here.
    static const float kComboFireThresh = 0.095f;   // DAT_001e9224
    static const float kComboIdleValue  = 0.1f;     // DAT_001e9220
    if (m_field_0x118 >= kComboIdleValue) {
        // Idle / already-fired: reset combo state, no event.
        m_ComboCountRef()   = 0;
        m_ComboEntityType   = 0;
        m_pComboMissControl = nullptr;
        for (int i = 0; i < 11; ++i) m_ComboSliceArr[i] = -1;
    } else {
        m_field_0x118 += dt;
        if (m_field_0x118 >= kComboFireThresh) {
            m_field_0x118 = kComboIdleValue;
            // Fire g_OnComboCancel -- binary @ 0x1e90d4 Event1<SlashEntity*>::Trigger.
            // ComboModifier::ComboWasCanceled subscribes here.
            g_OnComboCancel(this);
            if (m_ComboCountVal() > 1 && m_ComboSliceArr[0] >= 0) {
                // (a) Score-threshold refund.
                // ASM-verified: 2026-05-20 binary @ 0x0017dde6 (asm-inspector)
                {
                    int threshold = game_work.m_ScoreThreshold - m_ComboCountVal();
                    if (threshold < 2) threshold = 2;
                    game_work.m_ScoreThreshold = threshold;
                }

                // (b) Combo body: only if count >= 3 AND m_ComboSliceArr[1] >= 0.
                if (m_ComboCountVal() > 2 && m_ComboSliceArr[1] >= 0) {
                    if (game && game_work.gameMode == Mortar::GAME_MODE_ARCADE) {
                        LOG_INFO("BLITZ", "SlashEntity arcade combo: count=%d amount=%.3f -> AddSpeed",
                                 m_ComboCountVal(), (float)m_ComboCountVal() / 3.0f);
                        WaveManager::GetInstance()->AddSpeed(
                            (float)m_ComboCountVal() / 3.0f, 0);
                        FN::AddToCurrentScore(m_ComboCountVal(), m_ComboEntityType, true, true);
                    } else if (!Mortar::NetworkManager::GetInstance()->IsOnlineMultiplayer() || m_ComboEntityType != 2) {
                        FN::AddToCurrentScore(m_ComboCountVal(), m_ComboEntityType, true, false);
                    }
                    BonusManager::GetInstance()->AddCombo(m_ComboCountVal());
                    // ASM-verified: 2026-05-22 binary @ 0x0017df80..0x0017dff0 (re-analyst).
                    if (game && game_work.m_SaveData) {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "%s_combos", Mortar::GetModeName(game_work.gameMode));
                        game_work.m_SaveData->AddToTotal(buf, StringHash(buf), 1, true, true);

                        static int s_StrawberryType = -2;
                        if (s_StrawberryType == -2)
                            s_StrawberryType = Fruit::FruitType("strawberry", false);
                        if (s_StrawberryType >= 0) {
                            for (int i = 0; i < m_ComboCountVal(); ++i) {
                                if (m_ComboSliceArr[i] == s_StrawberryType) {
                                    static const uint32_t hStrawberryCombo = StringHash("strawberry_combo_total");
                                    game_work.m_SaveData->AddToTotal(
                                        "strawberry_combo_total", hStrawberryCombo, 1, true, false);
                                    break;
                                }
                            }
                        }
                    }
                    // (c) Combo coin spawn.
                    {
                        int bonusCoins = 0;
                        for (int i = 0; i < m_ComboCountVal(); ++i) {
                            const ::FruitInfo* fi = Fruit::FruitInfo(m_ComboSliceArr[i]);
                            if (fi && fi->m_CoinsMax > 0) { bonusCoins = m_ComboCountVal(); break; }
                        }
                        Vec3 coinPos = m_SlicePos;
                        if (m_pComboMissControl) coinPos = m_pComboMissControl->pos;
                        Mortar::Delegate1<void, Coin*> onArrived =
                            Coin::DefaultArrivedDelegate();
                        Coin::MakeCoins(bonusCoins, 1,
                                        Vec3(0.02f, 0.15f, 0.0f), 0, 0xff3a,
                                        &coinPos, nullptr, nullptr,
                                        onArrived, true);
                    }
                    // (d) Achievement unlock.
                    AchievementManager::GetInstance()->UnlockComboAchievement(m_ComboCountVal(), m_ComboSliceArr);
                    // (e) Best-combo save + CheckCombo cache.
                    {
                        FruitSaveData* sd = game_work.m_SaveData;
                        int len = m_ComboCountVal();
                        if (sd && len > sd->m_BestComboLength) {
                            for (int i = 0; i < 11; ++i) sd->m_BestComboFruits[i] = m_ComboSliceArr[i];
                            sd->m_BestComboLength = len;
                            s_CheckComboFlag = (signed char)CheckCombo(m_ComboSliceArr, len, nullptr);
                        } else if (sd && len == sd->m_BestComboLength) {
                            if (s_CheckComboFlag == -1)
                                s_CheckComboFlag = (signed char)CheckCombo(sd->m_BestComboFruits, len, nullptr);
                            int newScore = (signed char)CheckCombo(m_ComboSliceArr, m_ComboCountVal(), nullptr);
                            if (s_CheckComboFlag < newScore) {
                                for (int i = 0; i < 11; ++i) sd->m_BestComboFruits[i] = m_ComboSliceArr[i];
                                sd->m_BestComboLength = m_ComboCountVal();
                            }
                        }
                    }
                    // Online MP PointsPacket Send -- Defunct: online-services stub per policy
                }
            }
            // (f) State reset (unconditional in this arm).
            m_ComboCountRef()   = 0;
            m_ComboEntityType   = 0;
            m_pComboMissControl = nullptr;
            for (int i = 0; i < 11; ++i) m_ComboSliceArr[i] = -1;
        }
    }

    // ---------------------------------------------------------------------------
    // Per-frame splat-stream loop -- binary SlashEntity::Update tail @ 0x0017e248.
    // ---------------------------------------------------------------------------
    if (m_SliceTimerA > -1.0f) {
        m_SliceTimerA -= dt;
    }
    while (m_PendingSplats >= 0 && m_SliceTimerA <= 0.0f) {
        float sq = m_BladeDir.MagnitudeSqr();
        if (sq > 1.0f && sq < 10000.0f) {
            m_BladeVelAtSlice = m_BladeDir;
        }
        m_PendingSplats--;
        float B = m_SliceTimerB + Math::g_Random.RandF(1.0f) * 0.5f + 0.01f;
        m_SliceTimerB = (B >= 0.03f) ? B : 0.03f;
        m_SliceTimerA += m_SliceTimerB;
        SplatEntity* s = SplatEntity::GetFree();
        if (s) {
            Vec3 vel(m_BladeVelAtSlice.x * (Math::g_Random.RandF(1.0f) * 0.5f + 0.75f),
                     m_BladeVelAtSlice.y * (Math::g_Random.RandF(1.0f) * 0.5f + 0.75f),
                     0.0f);
            // DIFFERS: binary param3 passes incidental register-reuse bits, not a designed flag. Pass false.
            s->MakeSplat(m_RawTouchPos, vel, false, m_SliceEntityType);
        }
    }
}

// ---------------------------------------------------------------------------
// CollideWithSphere -- blade segment vs entity sphere test.
// The binary's ColLine (head<->tail) is updated by UpdatePoints each frame.
// Port iterates vertex pairs for sub-frame multi-segment precision.
// ---------------------------------------------------------------------------
bool SlashEntity::CollideWithSphere(const ColSphere& sphere,
                                     Vec3& outBladeVel) const {
    if (m_State == 0 || m_PointCount < 2) {
        outBladeVel = Vec3(0, 0, 0);
        return false;
    }
    if (!m_pLeftBuffer || !m_pRightBuffer) {
        outBladeVel = Vec3(0, 0, 0);
        return false;
    }

    // Buffer model: spine points are at even indices (0, 2, 4, ...).
    // Both buffers share the same spine x,y,z at even slots. Iterate consecutive
    // spine pairs (step 2) to reconstruct the trail centre segments for collision.
    for (int i = 0; i + 2 < m_PointCount; i += 2) {
        Vec3 a(m_pLeftBuffer[i].x,   m_pLeftBuffer[i].y,   m_pLeftBuffer[i].z);
        Vec3 b(m_pLeftBuffer[i+2].x, m_pLeftBuffer[i+2].y, m_pLeftBuffer[i+2].z);
        ColLine seg(a, b);
        if (sphere.IntersectsLine(seg)) {
            outBladeVel = Vec3(b.x - a.x, b.y - a.y, b.z - a.z);
            return true;
        }
    }
    outBladeVel = Vec3(0, 0, 0);
    return false;
}

// ---------------------------------------------------------------------------
// Draw -- Entity vtable slot 5. Binary @ 0x17B3B8 is a 1-instruction BX lr stub.
// ASM-verified: 2026-05-18 binary @ 0x0017B3B8 (re-analyst)
// ---------------------------------------------------------------------------
void SlashEntity::Draw(Renderer& /*r*/) {
}

// ---------------------------------------------------------------------------
// DrawSlice -- binary @ 0x1e83b0 (v1.6.1)
// Called from GameDraw's 16-slot loop, NOT from ActorManager::Draw.
//
// m_BladeActive (m_SwipeFuse) latch: tmp = m_SwipeFuse & 1; m_SwipeFuse = tmp*2.
// On the 1->2 transition (tmp==1, was set) spawn ghost/contact emitter burst.
// Draw if m_PointCount > 3: reset+upload modelview, bind blade.tex, DrawTriStrip
// both buffers with count = m_PointCount + 1 (includes head-cap vertex).
// ---------------------------------------------------------------------------
void SlashEntity::DrawSlice() {
    // m_BladeActive latch (binary @ 0x1e83b0):
    //   tmp = m_SwipeFuse & 1; m_SwipeFuse = tmp * 2.
    //   On 1->0 transition (tmp was set): fire ghost/contact burst.
    {
        int tmp = m_SwipeFuse & 1;
        m_SwipeFuse = tmp * 2;
        if (tmp != 0) {
            // Transition: blade was active last draw, now latching to 2.
            // Clear head-cap counter (blade-mod +0xbc @ 0x00332b34).
            // movgt/strgt @ 0x1e8444/0x1e8448: only when > 0.
            if (g_HeadCapFrameCounter > 0) {
                g_HeadCapFrameCounter = 0;
            }
            if (g_ScaleFlag1) CreateGhost();
            if (g_ContactHash != 0) {
                PSPParticleEmitter* eBurst =
                    PSPParticleManager::GetInstance().AddEmitter(
                        g_ContactHash, nullptr, /*persistent=*/false);
                if (eBurst) eBurst->m_Pos = pos;
            }
        }
    }

    // Gate: m_PointCount > 3 (binary @ 0x1e83b0).
    if (m_PointCount <= 3) {
#ifdef FN_DEBUG_TOUCH
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
    // binary @0x229788: blade tex-env GL_COMBINE RGB=REPLACE<-PRIMARY_COLOR, alpha=MODULATE.
    // RGB comes entirely from vertex colour (blade tint); alpha = tex.a * vertex.a (fade).
    TexEnvCombineReplaceRGB();
    bladeTex->Set();
    Mortar::Mesh::DrawTriStrip(m_pLeftBuffer,  m_PointCount + 1, false, NULL);
    Mortar::Mesh::DrawTriStrip(m_pRightBuffer, m_PointCount + 1, false, NULL);
    bladeTex->UnSet();
    TexEnvModulate();  // restore default so subsequent draws are unaffected
}

// ---------------------------------------------------------------------------
// Init (3-arg binary form) -- v1.6.1 @ 0x1e7a34
// ASM-verified: 2026-05-18 binary @ 0x0017C65C (re-analyst)
// ---------------------------------------------------------------------------
void SlashEntity::Init(void* /*unused*/, long /*unused*/, Vec3* /*unused*/) {
    // 1. Allocate ColLine into m_Col (+0x38).
    m_Col = new ColLine();
    // ASM-verified: 2026-05-27 binary @ 0x0017c68c (re-analyst)
    flags |= ENT_HAS_COLLISION;

    // 2. Reset scale-adjacent float at +0x94.
    m_SegLenSq = -1.0f;

    // 3. Build vertex buffers (160 pairs).
    InitPoints(160);

    // 4. Alloc m_Col = new(0x20) per binary @ 0x1e7a34 (already done above).
    // Per-frame scratch state.
    m_HeadThickScale = 0.0f;
    m_TrailEmitter = nullptr;
    m_Scale        = 0.0f;
    m_PendingSplats = -1;

    // 5. Copy Colour::White into both colour fields.
    Colour white(255, 255, 255, 255);
    m_HighlightColour = white;
    m_BaseColour      = white;

    // 6. Ghost ring init: 6 Vec3 slots at +0xbc (stride 12).
    for (int i = 0; i < 6; ++i) {
        float* slot = reinterpret_cast<float*>(_gap_0xbc + i * 12);
        slot[0] = 0.0f;
        slot[1] = 0.0f;
        slot[2] = 0.0f;
    }

    // Ghost index/count at +0x104/+0x108 = 0; ghost-dir at +0x10c = zero.
    m_GhostIndex = 0;
    m_GhostCount = 0;
    m_GhostDir   = Vec3(0.0f, 0.0f, 0.0f);

    // +0x118 = DAT_001e7b98 = 0.1f (m_field_0x118 seeded to the combo/fade
    //   timer initial value, NOT zero). +0xb8 = DAT_001e7b94 = 0.0f
    //   (m_SwipeSoundTimer). Binary @ 0x1e7ae4 (vstr s15=DAT_001e7b98 -> +0x118)
    //   and 0x1e7b84 (vstr s15=DAT_001e7b94 -> +0xb8). Values read from
    //   FruitNinja_v1_6_1.exe: DAT_001e7b94=0x00000000, DAT_001e7b98=0x3dcccccd.
    m_SwipeSoundTimer = 0.0f;
    m_field_0x118     = 0.1f;

    // +0x144 = 6.0f; +0x148/+0x14c = -1.
    m_field_0x144 = 6.0f;
    m_field_0x148 = -1;
    m_field_0x14c = -1;

    // 7. Combo / state init.
    m_ComboTimerRef() = 0.1f;   // per-swipe accumulator (DAT_0017c764)
    m_ComboCountRef() = 0;
    m_ComboEntityType = 0;
    m_SwipeEndEdge    = 0;
    m_Angle           = 0;

    // 8. 11-entry combo-slice array, all -1.
    for (int i = 0; i < 11; ++i) {
        m_ComboSliceArr[i] = -1;
    }
}

// ---------------------------------------------------------------------------
// InitPoints -- v1.6.1 @ 0x1e75d0
// Allocates m_pLeftBuffer/m_pRightBuffer each as (count+2) QUADCUSTOMVERTEX.
// Binary: 162 * 36 = 5832 bytes per buffer for count=160.
// Fills elements with sentinel/white.
// ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
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
    // DAT_001e76ec = 0.0f: pos.xyz = 0, normal.xyz = 0, uv.v = 0; uv.u = 1.0f; colour = white.
    for (int side = 0; side < 2; ++side) {
        QUADCUSTOMVERTEX* buf = (side == 0) ? m_pLeftBuffer : m_pRightBuffer;
        for (int i = 0; i < m_SplitPoint; ++i) {
            buf[i].x  = 0.0f;
            buf[i].y  = 0.0f;
            buf[i].z  = 0.0f;
            buf[i].nx = 0.0f;
            buf[i].ny = 0.0f;
            buf[i].nz = 0.0f;
            buf[i].colour = whitePacked;
            buf[i].u  = 1.0f;
            buf[i].v  = 0.0f;
        }
    }

    // Binary @ 0x1e75d0: do/while seeds all three anchors to (-65535,-65535,-65535)
    // (DAT_001e76e8 = 0xc77fff00 = -65535.0f). The sentinel is tested by
    // UpdateTouchDown: tail.x <= -65520 means "first point of new slash".
    static const float kAnchorSentinel = -65535.0f;
    m_TailPos     = Vec3(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
    m_HeadPos     = Vec3(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
    m_PrevHeadPos = Vec3(kAnchorSentinel, kAnchorSentinel, kAnchorSentinel);
#ifdef FN_DEBUG_TOUCH
    LOG_DEBUG("SLASH", "InitPoints: seed anchors tail=(%.1f,%.1f,%.1f) head=(%.1f,%.1f,%.1f) prev=(%.1f,%.1f,%.1f) pointCount=%d",
             m_TailPos.x, m_TailPos.y, m_TailPos.z,
             m_HeadPos.x, m_HeadPos.y, m_HeadPos.z,
             m_PrevHeadPos.x, m_PrevHeadPos.y, m_PrevHeadPos.z,
             m_PointCount);
#endif
}

// ---------------------------------------------------------------------------
// SetModColours / InitModColours / SetModScales / ResetModScales
// ---------------------------------------------------------------------------
void SlashEntity::SetModColours(
    const Colour*  colours,
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

    g_PaletteProgress = 1.0f;
    if (g_ColourType == 2 && g_ColourCount > 0) {
        g_PaletteProgress = (float)((unsigned)rand() % (unsigned)g_ColourCount);
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

void SlashEntity::SetModScales(
    float startThick,
    float endThick,
    float scaleLen,
    float uvLen,
    bool  flipUD,
    bool  loop,
    float loopUVLen)
{
    g_Scale1     = startThick;
    g_Scale2     = endThick;
    g_Scale3     = scaleLen;
    g_Scale4     = uvLen;
    g_Scale5     = loopUVLen;
    g_ScaleFlag1 = flipUD ? 1 : 0;
    g_ScaleFlag2 = loop   ? 1 : 0;
}

// ASM-verified: 2026-05-18 binary @ 0x00117a80 / 0x00119b08 (re-analyst)
void SlashEntity::ResetModScales() {
    g_Scale1     = 1.0f;
    g_Scale2     = 1.0f;
    g_Scale3     = 1.0f;
    g_Scale4     = 1.0f;
    g_Scale5     = 1.0f;
    g_ScaleFlag1 = 1;
    g_ScaleFlag2 = 1;
}

// ColoursChanged @ 0x0017c41c.
// DIFFERS: binary @ 0x0017C41C does NOT snap m_HighlightColour here
// (per asm-inspector 2026-05-10). The binary refreshes m_HighlightColour
// only via PreUpdate (PER_SLASH) or the m_bDirty UpdateModColour branch
// (PER_SWIPE/g_ColourType==2); NONE leaves it untouched. Port snaps to
// g_Palette[0] here because the PER_SLASH/PER_SWIPE refresh paths are not
// yet ported, so stale bytes would persist through a blade-type swap.
// TODO: remove this snap once PreUpdate colour refresh (binary @ 0x17C3C4)
//   and the m_bDirty UpdateModColour branch are ported.
void SlashEntity::ColoursChanged() {
    if (g_ColourCount > 0) {
        m_HighlightColour = g_Palette[0];
    } else {
        m_HighlightColour = Colour(255, 255, 255, 255);
    }
    m_BaseColour = m_HighlightColour;

    if (m_TrailEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }
    if (m_State != 0) {
        m_PointCount = 0;

        if (g_DirectionalFlag != 0 && g_TrailHash != 0) {
            m_TrailEmitter = PSPParticleManager::GetInstance()
                .AddEmitter(g_TrailHash, /*ppRef=*/nullptr, /*persistent=*/true);
            if (m_TrailEmitter) {
                m_TrailEmitter->m_bUpdateWhenPaused = true;
            }
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

// Binary @ 0x17B570 -- ColLine vs ColSphere test; port's Update uses
// CollideWithSphere() per-entity directly so this entry point is unreached.
bool SlashEntity::CollideWithEntity(Mortar::Entity* /*entity*/) { return false; }

// Binary @ 0x17B3BC -- 1-instruction stub `mov r0,#0; bx lr`.
int SlashEntity::CollisionResponse(Mortar::Entity* /*hitter*/, unsigned long /*mask1*/,
                                    unsigned long /*mask2*/, Vec3* /*bladeVel*/) { return 0; }

// Binary @ 0x17CA0C -- non-const Colour* overload; body identical to const overload.
void SlashEntity::SetModColours(
    Colour*     colours,
    int         colourCount,
    int         colourType,
    float       lifeScale,
    const char* particlePath,
    const char* textureName2,
    bool        directional,
    const char* contactParticle,
    const char* particle2)
{
    SetModColours(static_cast<const Colour*>(colours), colourCount, colourType,
                  lifeScale, particlePath, textureName2, directional,
                  contactParticle, particle2);
}

// ASM-spec: SlashEntity::TouchDown @ 0x17D61C
bool SlashEntity::TouchDown(InputEvent* event) {
    if (m_SwipeEndEdge == 0 && m_State == 0) {
        Reset();
        if (g_ColourType == 2) {
            UpdateModColour(&m_HighlightColour, 1.0f);
        }
    }
    UpdateTouchDown(event);
    return true;
}

// ASM-spec: SlashEntity::TouchMoveX @ 0x17C50C
bool SlashEntity::TouchMoveX(InputEvent* event) {
    Game* g = Game::GetInstance();
    if (g && game_work.m_BombHitTimer > 0.0f) return false;
    m_RawTouchPos.x = event->x;
    return true;
}

// ASM-spec: SlashEntity::TouchMoveY @ 0x17C490
bool SlashEntity::TouchMoveY(InputEvent* event) {
    Game* g = Game::GetInstance();
    if (g && game_work.m_BombHitTimer > 0.0f) return false;
    m_RawTouchPos.y = event->y;
    return true;
}

// Binary @ 0x17B92C -- SlashEntity::UpdatePoints(float dt).
// The real work is above in UpdatePoints(). This entry point keeps binary
// symbol parity; it is distinct from the UpdatePoints(float) called from Update().

// ASM-spec: SlashEntity::UpdateTouchDown @ 0x17D2E4
void SlashEntity::UpdateTouchDown(InputEvent* /*event*/) {
    // Binary @ 0x17D3AC short-circuits when bombHitTimer > 0.
    Game* g = Game::GetInstance();
    if (g && game_work.m_BombHitTimer > 0.0f) return;
    OnTouchActive(m_RawTouchPos.x, m_RawTouchPos.y);
}

// Port-only release handler.
bool SlashEntity::TouchUp(InputEvent* /*event*/) {
    OnTouchReleased();
    return true;
}

// @ 0x0017e504. Ghost slots not yet ported -- no-op stub.
void SlashEntity::PreDraw() {
}
