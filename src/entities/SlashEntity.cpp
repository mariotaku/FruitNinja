//
// SlashEntity -- blade trail visual (entity type 3).
// v1.6.1 binary-faithful port: heap-allocated vertex buffers, no inline ring.
// sizeof(SlashEntity) = 0x188 (392). See SlashEntity.h for field/method addresses.
//

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

static float    g_Scale1            = 1.0f;   // 0x001F3E5C (start thickness)
static float    g_Scale2            = 1.0f;   // 0x001F3E60 (end thickness)
static float    g_Scale3            = 0.0f;   // 0x0024D8D0 (scale length)
static float    g_Scale4            = 1.0f;   // 0x001F3E64 (UV length)
static float    g_Scale5            = 0.0f;   // 0x0024D8D4 (loop UV length)
static uint8_t  g_ScaleFlag1        = 0;      // 0x0024D8D8 (gates CreateGhost())
static uint8_t  g_ScaleFlag2        = 1;      // 0x001F3E69 (gates UV-mirror branch)
static uint8_t  g_HitLatch          = 0;      // 0x0024D840 frame-hit latch
static int32_t  g_HitResetCounter   = 0;      // 0x0024D83C reset cooldown

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
    , m_bHasHead(false)
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
    m_bHasHead   = false;

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

    if (!m_bHasHead) {
        m_PointCount  = 0;
        m_bHasHead    = true;
        m_State       = 1;
        m_HeadPos     = newPos;
        m_TailPos     = newPos;
        m_PrevHeadPos = newPos;
        m_BladeDir    = Vec3(1, 0, 0);  // non-zero seed so first AddPoint guard passes
        Vec3 seedDir(1.0f, 0.0f, 0.0f);
        AddPoint(1.0f, &newPos, &seedDir);
        return;
    }

    const Vec3 lastCenter = m_HeadPos;
    const Vec3 delta(newPos.x - lastCenter.x, newPos.y - lastCenter.y, 0.0f);
    const float distSq = delta.x * delta.x + delta.y * delta.y;
    const float thresh = (m_State != 0)
        ? (MOVE_THRESH_ACTIVE   * MOVE_THRESH_ACTIVE)
        : (MOVE_THRESH_INACTIVE * MOVE_THRESH_INACTIVE);
    if (distSq < thresh) return;

    const float dist = sqrtf(distSq);
    Vec3 dir(delta.x / dist, delta.y / dist, 0.0f);

    float travelled = POINT_SPACING;
    while (travelled < dist) {
        Vec3 step(lastCenter.x + dir.x * travelled,
                  lastCenter.y + dir.y * travelled, 0.0f);
        AddPoint(1.0f, &step, &dir);
        travelled += POINT_SPACING;
    }

    AddPoint(1.0f, &newPos, &dir);
    m_State = 1;
}

void SlashEntity::OnTouchReleased() {
    if (m_State == 1) m_State = 2;
    m_bHasHead = false;
}

// ---------------------------------------------------------------------------
// AddPoint -- binary @ 0x1e9918 (v1.6.1)
// Signature: pressure FIRST (s0 register), then center pointer, then dir pointer.
// ---------------------------------------------------------------------------
void SlashEntity::AddPoint(float pressure, const Vec3* center, const Vec3* dir) {
    if (!m_pLeftBuffer || !m_pRightBuffer) return;
    if (!center || !dir) return;

    // Guard: if IsNearZero(*dir) || IsNearZero(m_BladeDir) -> return.
    // IsNearZero: MagnitudeSqr < some epsilon (port uses 1e-8f as threshold).
    // TODO: 0x1e9918 -- confirm IsNearZero epsilon from binary DAT.
    if (dir->MagnitudeSqr() < 1e-8f) return;
    if (m_BladeDir.MagnitudeSqr() < 1e-8f) {
        // First point: m_BladeDir not yet seeded -- skip guard for initial seed.
    }

    // Ghost-ring averaging bookkeeping.
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
        if (diff.MagnitudeSqr() > 1.6886f) {
            // TODO: 0x1e9918 -- write DAT value to m_field_0x118 when avg-newest diverges.
            m_field_0x118 = 1.0f;
        }
        m_GhostDir = avgDir;
    }

    // Update blade direction.
    m_BladeDir = *dir;

    // Angle: Atan2Idx(-dir->x, dir->y); store to m_AngleIndex (+0x184) and m_Angle (+0x36).
    short angle = Math::Atan2Idx(-dir->x, dir->y);
    m_AngleIndex = angle;
    m_Angle      = (uint16_t)angle;

    // Perpendicular offset: perp = (CosIdx(angle), SinIdx(angle), 0) * pressure * 9.0 * g_Scale1.
    // No pressure==0 -> g_Scale2 fallback (spec: delete that branch).
    float halfWidth = pressure * 9.0f * g_Scale1;
    float perpX = CosIdx((uint16_t)angle) * halfWidth;
    float perpY = SinIdx((uint16_t)angle) * halfWidth;

    // Capacity: if m_PointCount >= m_SplitPoint-2, shift strip dropping 2 PAIRS.
    if (m_PointCount >= m_SplitPoint - 2) {
        const int newCount = m_SplitPoint - 4;
        if (newCount > 0) {
            memmove(m_pLeftBuffer,  m_pLeftBuffer  + 2, newCount * sizeof(QUADCUSTOMVERTEX));
            memmove(m_pRightBuffer, m_pRightBuffer + 2, newCount * sizeof(QUADCUSTOMVERTEX));
        }
        m_PointCount = (newCount > 0) ? newCount : 0;
        // Decrement +0x138 counter by 2.
        m_field_0x138 -= 2;
    }

    m_HeadThickScale = 1.0f;

    const uint32_t col = m_BaseColour.PlatformColour();

    // Write the pair at index m_PointCount.
    // left = *center - perp, right = *center + perp; uv.x = 0.5 for both.
    // Defunct: same-screen-MP miter-sign branch (picks uv.y 0.5 vs 1.0).
    // Non-MP branch: uv.y = 0.5.
    QUADCUSTOMVERTEX& lv = m_pLeftBuffer[m_PointCount];
    lv.x  = center->x - perpX;
    lv.y  = center->y - perpY;
    lv.z  = center->z;
    lv.nx = 0.0f; lv.ny = 0.0f; lv.nz = 0.0f;
    lv.colour = col;
    lv.u = 0.5f; lv.v = 0.5f;

    QUADCUSTOMVERTEX& rv = m_pRightBuffer[m_PointCount];
    rv.x  = center->x + perpX;
    rv.y  = center->y + perpY;
    rv.z  = center->z;
    rv.nx = 0.0f; rv.ny = 0.0f; rv.nz = 0.0f;
    rv.colour = col;
    rv.u = 0.5f; rv.v = 0.5f;

    m_PointCount += 2;

    m_PrevHeadPos = m_HeadPos;
    m_HeadPos     = *center;
}

// ---------------------------------------------------------------------------
// UpdatePoints -- binary @ 0x1e6914 (v1.6.1)
// Full per-frame re-derivation of vertex geometry, colour, alpha, m_Col, head cap.
// ---------------------------------------------------------------------------
void SlashEntity::UpdatePoints(float dt) {
    if (!m_pLeftBuffer || !m_pRightBuffer) return;

    // dt==0 fast path: if dt==0 and m_SwipeEndEdge!=0, recolour all verts white.
    // TODO: 0x1e6914 -- GameTaskState slice-byte >= 0 gate (second condition).
    if (dt == 0.0f && m_SwipeEndEdge != 0) {
        Colour white(255, 255, 255, 255);
        uint32_t wc = white.PlatformColour();
        for (int i = 0; i < m_PointCount; ++i) {
            m_pLeftBuffer[i].colour  = wc;
            m_pRightBuffer[i].colour = wc;
        }
        m_SegLenSq = -1.0f;
        return;
    }

    // Sync m_Angle from m_AngleIndex (set by AddPoint on each new point).
    m_Angle = (uint16_t)m_AngleIndex;

    // m_Col update: if m_PointCount>=4 && m_SwipeFuse!=0 && m_field_0x138!=-1 && m_field_0x13c!=-1.
    // TODO: 0x1e6914 -- TranslatePos via FruitCamera slot+0x4c for head/tail.
    if (m_PointCount >= 4 && m_SwipeFuse != 0 && m_field_0x138 != -1 && m_field_0x13c != -1) {
        if (m_Col) {
            ColLine* cl = static_cast<ColLine*>(m_Col);
            cl->a() = m_TailPos;
            cl->b   = m_HeadPos;
        }
        Vec3 h = m_HeadPos, t = m_TailPos;
        Vec3 diff(h.x - t.x, h.y - t.y, h.z - t.z);
        m_SegLenSq = diff.MagnitudeSqr();
    } else {
        m_field_0x138 = -1;
        m_field_0x13c = -1;
        m_SegLenSq    = -1.0f;
    }

    // If too few points, reset blade direction from DAT.
    if (m_PointCount < 4) {
        // TODO: 0x1e6914 -- reset m_BladeDir from its DAT seed when m_PointCount < 4.
        m_BladeDir = Vec3(0.0f, 0.0f, 0.0f);
    }

    if (m_PointCount < 1) return;

    // ------------------------------------------------------------------
    // MAIN loop: per pair (i += 2), re-derive miter, width, UV, colour.
    // ------------------------------------------------------------------

    // Derive fade factor from m_field_0x118 / g_Scale3 if positive, else 1.0.
    float fade = 1.0f;
    if (m_field_0x118 > 0.0f && g_Scale3 > 0.0f) {
        fade = m_field_0x118 / g_Scale3;
        if (fade > 1.0f) fade = 1.0f;
    }

    // Advance colour palette (UpdateModColour once per UpdatePoints call).
    if (g_ColourType == 1) {
        UpdateModColour(&m_HighlightColour, -2.0f / (float)m_PointCount);
    }

    // m_BaseColour <- lerp white -> m_HighlightColour by (1-m_Scale).
    if (m_Scale > 0.0f) {
        const float blend = 1.0f - m_Scale;
        m_BaseColour.r = (uint8_t)(255.0f + (float)((int)m_HighlightColour.r - 255) * blend);
        m_BaseColour.g = (uint8_t)(255.0f + (float)((int)m_HighlightColour.g - 255) * blend);
        m_BaseColour.b = (uint8_t)(255.0f + (float)((int)m_HighlightColour.b - 255) * blend);
        m_BaseColour.a = (uint8_t)(255.0f + (float)((int)m_HighlightColour.a - 255) * blend);
    } else {
        m_BaseColour = m_HighlightColour;
    }

    // Per-pair arc-length accumulation for alpha second pass.
    // We allocate a small per-pair arc array on stack (max 160/2 = 80 pairs).
    static const int MAX_PAIRS = 80;
    float arcLen[MAX_PAIRS];
    int   dropCount = 0;
    float arcTotal  = 0.0f;
    int   pairCount = m_PointCount / 2;
    if (pairCount > MAX_PAIRS) pairCount = MAX_PAIRS;

    for (int pi = 0; pi < pairCount; ++pi) {
        int i = pi * 2;

        // Reconstruct pair centre from left[i]+right[i] midpoint.
        float cx = (m_pLeftBuffer[i].x + m_pRightBuffer[i].x) * 0.5f;
        float cy = (m_pLeftBuffer[i].y + m_pRightBuffer[i].y) * 0.5f;
        float cz = (m_pLeftBuffer[i].z + m_pRightBuffer[i].z) * 0.5f;

        // Current half-width from left[i] to centre.
        float dx = m_pLeftBuffer[i].x - cx;
        float dy = m_pLeftBuffer[i].y - cy;
        float len = sqrtf(dx * dx + dy * dy);

        // Time factor: doubled for second half of strip.
        float timeFactor = dt;
        if (m_SplitPoint * 2 <= i) timeFactor *= 2.0f;

        // Width grows toward endThick.
        float startThick = g_Scale1 * 9.0f;
        float endThick   = g_Scale2 * 9.0f;
        // TODO: 0x1e6914 -- resolve DAT_001e6f40 divisor for width growth rate.
        static const float kWidthDivisor = 1.0f;
        len += (timeFactor * g_Scale3 * (endThick - startThick)) / kWidthDivisor;

        // Clamp half-width.
        float maxHW = startThick * m_HeadThickScale;
        if (len > maxHW) len = maxHW;

        // Collapse: drop pair if width below threshold.
        if (len < 0.01f) {
            dropCount += 2;
            arcLen[pi] = 0.0f;
            continue;
        }

        // Miter direction: short-strip path uses m_Angle directly.
        float miterX, miterY;
        if (pairCount <= 2) {
            miterX = CosIdx((uint16_t)m_AngleIndex) * len;
            miterY = SinIdx((uint16_t)m_AngleIndex) * len;
        } else {
            // Cross path: Normalise(nextCenter - center) then Cross(dir,(0,0,1))*width.
            int ni = (pi + 1 < pairCount) ? (pi + 1) * 2 : i;
            float ncx = (m_pLeftBuffer[ni].x + m_pRightBuffer[ni].x) * 0.5f;
            float ncy = (m_pLeftBuffer[ni].y + m_pRightBuffer[ni].y) * 0.5f;
            float ddx = ncx - cx, ddy = ncy - cy;
            float dl = sqrtf(ddx * ddx + ddy * ddy);
            if (dl > 1e-6f) { ddx /= dl; ddy /= dl; }
            // Cross((ddx,ddy,0),(0,0,1)) = (ddy, -ddx, 0).
            miterX = ddy * len;
            miterY = -ddx * len;
        }

        uint32_t col = m_BaseColour.PlatformColour();

        // Taper uv.x = (i / m_PointCount) * 0.5.
        float uvx = (m_PointCount > 0)
            ? ((float)i / (float)m_PointCount) * 0.5f
            : 0.0f;

        m_pLeftBuffer[i].x  = cx - miterX;
        m_pLeftBuffer[i].y  = cy - miterY;
        m_pLeftBuffer[i].z  = cz;
        m_pLeftBuffer[i].colour = col;
        m_pLeftBuffer[i].u  = uvx;

        m_pRightBuffer[i].x  = cx + miterX;
        m_pRightBuffer[i].y  = cy + miterY;
        m_pRightBuffer[i].z  = cz;
        m_pRightBuffer[i].colour = col;
        m_pRightBuffer[i].u  = uvx;

        // Arc length for this pair (distance from previous centre).
        if (pi > 0) {
            float pcx = (m_pLeftBuffer[(pi-1)*2].x + m_pRightBuffer[(pi-1)*2].x) * 0.5f;
            float pcy = (m_pLeftBuffer[(pi-1)*2].y + m_pRightBuffer[(pi-1)*2].y) * 0.5f;
            float adx = cx - pcx, ady = cy - pcy;
            float a = sqrtf(adx * adx + ady * ady);
            arcLen[pi]  = a;
            arcTotal   += a;
        } else {
            arcLen[pi] = 0.0f;
        }
    }

    // SECOND loop: per vertex alpha = 0.5 - (1 - 0.5*arc[i]/arcTotal)*fade.
    float arcAcc = 0.0f;
    for (int pi = 0; pi < pairCount; ++pi) {
        int i = pi * 2;
        arcAcc += arcLen[pi];
        float arcFrac = (arcTotal > 0.0f) ? arcAcc / arcTotal : 1.0f;
        float alpha01 = 0.5f - (1.0f - 0.5f * arcFrac) * fade;
        if (alpha01 < 0.0f) alpha01 = 0.0f;
        if (alpha01 > 1.0f) alpha01 = 1.0f;
        uint32_t a = (uint32_t)(alpha01 * 255.0f);

        uint32_t lc = m_pLeftBuffer[i].colour;
        uint32_t rc = m_pRightBuffer[i].colour;
        m_pLeftBuffer[i].colour  = (lc & 0x00FFFFFFu) | (a << 24);
        m_pRightBuffer[i].colour = (rc & 0x00FFFFFFu) | (a << 24);
    }

    // Drop collapsed pairs.
    if (dropCount > 0 && dropCount < m_PointCount) {
        int keep = m_PointCount - dropCount;
        if (keep > 0) {
            memmove(m_pLeftBuffer,  m_pLeftBuffer  + dropCount, keep * sizeof(QUADCUSTOMVERTEX));
            memmove(m_pRightBuffer, m_pRightBuffer + dropCount, keep * sizeof(QUADCUSTOMVERTEX));
        }
        m_PointCount = keep;
    } else if (dropCount >= m_PointCount) {
        m_PointCount = 0;
    }

    // Head cap: if m_PointCount > 2, write a head-cap pair at index m_PointCount.
    // TODO: 0x1e6914 -- resolve DAT_001e75c0 for head-cap miter scale.
    static const float kHeadCapScale = 1.0f;
    if (m_PointCount > 2) {
        const int last  = m_PointCount - 1;
        const int last2 = (m_PointCount >= 2) ? m_PointCount - 2 : 0;
        float hcx = (m_pLeftBuffer[last].x  + m_pRightBuffer[last].x)  * 0.5f;
        float hcy = (m_pLeftBuffer[last].y  + m_pRightBuffer[last].y)  * 0.5f;
        float hcx2= (m_pLeftBuffer[last2].x + m_pRightBuffer[last2].x) * 0.5f;
        float hcy2= (m_pLeftBuffer[last2].y + m_pRightBuffer[last2].y) * 0.5f;
        float ddx = hcx - hcx2, ddy = hcy - hcy2;
        float dl = sqrtf(ddx * ddx + ddy * ddy);
        if (dl > 1e-6f) { ddx /= dl; ddy /= dl; }
        // Cross(dir,(0,0,1)) = (ddy,-ddx,0).
        float hw = 2.5f * kHeadCapScale;
        float mx = ddy * hw, my = -ddx * hw;

        uint32_t wc = m_BaseColour.PlatformColour();
        m_pLeftBuffer[m_PointCount].x  = hcx - mx;
        m_pLeftBuffer[m_PointCount].y  = hcy - my;
        m_pLeftBuffer[m_PointCount].z  = m_pLeftBuffer[last].z;
        m_pLeftBuffer[m_PointCount].u  = 1.0f;
        m_pLeftBuffer[m_PointCount].colour = wc;

        m_pRightBuffer[m_PointCount].x  = hcx + mx;
        m_pRightBuffer[m_PointCount].y  = hcy + my;
        m_pRightBuffer[m_PointCount].z  = m_pRightBuffer[last].z;
        m_pRightBuffer[m_PointCount].u  = 1.0f;
        m_pRightBuffer[m_PointCount].colour = wc;

        // Head cap is written beyond m_PointCount (doesn't increment m_PointCount).
        // DrawSlice draws m_PointCount+1 verts to include the cap.
    }

    // Update head/tail tracking.
    if (m_PointCount > 0) {
        m_TailPos.x = (m_pLeftBuffer[0].x + m_pRightBuffer[0].x) * 0.5f;
        m_TailPos.y = (m_pLeftBuffer[0].y + m_pRightBuffer[0].y) * 0.5f;
        m_TailPos.z = m_pLeftBuffer[0].z;
        const int last = m_PointCount - 1;
        m_HeadPos.x = (m_pLeftBuffer[last].x + m_pRightBuffer[last].x) * 0.5f;
        m_HeadPos.y = (m_pLeftBuffer[last].y + m_pRightBuffer[last].y) * 0.5f;
        m_HeadPos.z = m_pLeftBuffer[last].z;
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

    // m_Scale decay.
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
                        LOG_INFO("SLASH", "hit %s %p at (%.1f,%.1f) trail_n=%d",
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
                            static_cast<Fruit*>(e)->m_bSpawnedByCriticalSplash != 0;
                        if (t == 0) {
                            Fruit* fruit = static_cast<Fruit*>(e);
                            m_SliceEntityType = (int)fruit->m_FruitType;
                            if (!isMenuFruit) {
                            if (fruit->m_bCriticalEligible) {
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

    // Per-swipe combo resolution -- binary SlashEntity::Update @ 0x0017dde6..0x0017dfd0.
    // ASM-verified: 2026-05-18 binary @ 0x0017dde6..0x0017dfd0 (re-analyst)
    // DAT_0017e004 = 0.1f, verified 2026-05-20.
    static const float kComboWindow = 0.1f;
    if (m_ComboTimerVal() < kComboWindow) {
        m_ComboTimerRef() += dt;
        if (m_ComboTimerVal() >= kComboWindow) {
            // Fire g_OnComboCancel — binary @ 0x1e90d4, fires when combo timer
            // this+0x118 crosses its threshold. Port equiv: m_ComboTimerRef() (+0x174)
            // crossing kComboWindow. ComboModifier::ComboWasCanceled subscribes here.
            // TODO: 0x1e90d4 — verify binary +0x118 is distinct from +0x174 (m_ComboTimerRef);
            //   if so, track the separate +0x118 timer and fire at its threshold instead.
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
            m_ComboCountRef()  = 0;
            m_ComboEntityType  = 0;
            m_pComboMissControl = nullptr;
            for (int i = 0; i < 11; ++i) m_ComboSliceArr[i] = -1;
        }
    } else {
        m_ComboCountRef()  = 0;
        m_ComboEntityType  = 0;
        m_pComboMissControl = nullptr;
        for (int i = 0; i < 11; ++i) m_ComboSliceArr[i] = -1;
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

    // Reconstruct centre points from the averaged left/right vertex pair.
    for (int i = 0; i + 1 < m_PointCount; ++i) {
        Vec3 a(
            (m_pLeftBuffer[i].x   + m_pRightBuffer[i].x  ) * 0.5f,
            (m_pLeftBuffer[i].y   + m_pRightBuffer[i].y  ) * 0.5f,
            m_pLeftBuffer[i].z
        );
        Vec3 b(
            (m_pLeftBuffer[i+1].x + m_pRightBuffer[i+1].x) * 0.5f,
            (m_pLeftBuffer[i+1].y + m_pRightBuffer[i+1].y) * 0.5f,
            m_pLeftBuffer[i+1].z
        );
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
// DrawSlice -- binary @ 0x17E424
// Called from GameDraw's 16-slot loop, NOT from ActorManager::Draw.
// ASM-verified: 2026-05-18 binary @ 0x0017E424 (re-analyst)
// ---------------------------------------------------------------------------
void SlashEntity::Draw(Renderer& /*r*/) {
    // Entity vtable slot 5. Binary @ 0x17B3B8 is a 1-instruction BX lr stub.
    // ASM-verified: 2026-05-18 binary @ 0x0017B3B8 (re-analyst)
}

void SlashEntity::DrawSlice() {
    // Fuse: b = m_SwipeFuse & 1; m_SwipeFuse = b<<1; if b==0 -> fire swipe SFX/ghost.
    // Binary @ 0x1e83b0.
    {
        int b = m_SwipeFuse & 1;
        m_SwipeFuse = b << 1;
        if (b == 0) {
            // Fire swipe SFX and ghost burst on fuse completion.
            // TODO: 0x1e83b0 -- fire swipe SFX here (PlaySwipe equivalent).
            // TODO: 0x1e83b0 -- if global ghost-counter+0xbc > 0, reset it = 0.
            if (g_ScaleFlag1) CreateGhost();
            if (g_ContactHash != 0) {
                PSPParticleEmitter* eBurst =
                    PSPParticleManager::GetInstance().AddEmitter(
                        g_ContactHash, nullptr, /*persistent=*/false);
                if (eBurst) eBurst->m_Pos = pos;
            }
        }
    }

    // Gate on m_PointCount > 3 (binary @ 0x1e83b0).
    if (m_PointCount <= 3) return;
    if (!m_pLeftBuffer || !m_pRightBuffer) return;

    Mortar::SmartPtr<Mortar::Texture>& bladeTex =
        g_ModTexture.IsValid() ? g_ModTexture : g_BladeTex;
    if (!bladeTex.IsValid()) return;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    // Draw count = m_PointCount+1 (includes the head-cap vert written by UpdatePoints).
    bladeTex->Set();
    Mortar::Mesh::DrawTriStrip(m_pLeftBuffer,  m_PointCount + 1, false, NULL);
    Mortar::Mesh::DrawTriStrip(m_pRightBuffer, m_PointCount + 1, false, NULL);
    bladeTex->UnSet();
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

    // +0x118 = DAT seed (m_field_0x118); +0xb8 = m_SwipeSoundTimer DAT seed.
    // Binary writes DAT values; port uses 0.0f (DAT value not yet resolved).
    // TODO: 0x1e7a34 -- resolve DAT seeds at +0xb8 and +0x118 from binary.
    m_SwipeSoundTimer = 0.0f;
    m_field_0x118     = 0.0f;

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

    // Seed m_TailPos/m_HeadPos/m_PrevHeadPos to 0.0 (do/while i!=3 in binary).
    m_TailPos     = Vec3(0.0f, 0.0f, 0.0f);
    m_HeadPos     = Vec3(0.0f, 0.0f, 0.0f);
    m_PrevHeadPos = Vec3(0.0f, 0.0f, 0.0f);
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
