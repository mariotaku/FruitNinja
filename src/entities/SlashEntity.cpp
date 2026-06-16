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

static float    g_Scale1            = 1.0f;   // 0x2D8D78 (ModSlashThickness; SlashModInfo ctor default=1.0)
static float    g_Scale2            = 0.0f;   // 0x332BCC (ModSlashEndThickness; SlashModInfo ctor default=0.0)
static float    g_Scale3            = 1.0f;   // 0x2D8D74 (ModSlashLength; SlashModInfo ctor default=1.0)
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

// ASM-verified: 2026-06-16 binary @ 0x1e684c (re-analyst). Head thickness scale =
// half the L/R edge separation at the LAST stored vertex, normalized by the nominal
// full half-width (ModSlashThickness*9). Range [0,1]. Consumed by OnTouchActive
// (binary UpdateTouchDown @0x1e9f08) as per-point taper pressure.
float SlashEntity::GetHeadThicknessScale() const {
    if (!m_pLeftBuffer || !m_pRightBuffer) return 0.0f;
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
    LOG_DEBUG("SLASH", "OnTouchActive[%d]: pos=(%.2f,%.2f) isSeed=%d distSq=%.2f thresh=%.2f state=%d",
              m_FingerId, x, y, (int)isSeed, distSq, thresh, m_State);
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

        // Binary UpdateTouchDown @0x1e9f08: ramp pressure from headThick -> 1.0 for
        // interpolated points (fVar12=travelled, fVar13=segment length).
        const float headThick = GetHeadThicknessScale();

        // Interpolate intermediate points every POINT_SPACING units.
        float travelled = POINT_SPACING;
        while (travelled < dist) {
            Vec3 step(lastCenter.x + dir.x * travelled,
                      lastCenter.y + dir.y * travelled, 0.0f);
            const float pressure = headThick + (travelled / dist) * (1.0f - headThick);
            AddPoint(pressure, &step, &dir);
            travelled += POINT_SPACING;
        }
#ifdef FN_DEBUG_TOUCH
        LOG_DEBUG("SLASH", "OnTouchActive[%d]: ADD branch dist=%.2f dir=(%.2f,%.2f) pointCount=%d",
                 m_FingerId, dist, dir.x, dir.y, m_PointCount);
#endif
    }

    // Always lay the head point at the live touch position (full pressure, binary: 1.0).
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
    // Binary LAB_001ea3d0 (UpdateTouchDown epilogue): re-arm bit0 every frame a
    // TouchDown event arrives so DrawSlice's latch sees an active fuse.
    // ASM-verified: 2026-06-16 binary @ 0x1ea3d0 (asm-inspector)
    m_SwipeFuse |= 1;
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

    // Head half-width (binary AddPoint @0x1e9bf4): param_1 * 9.0 * ModSlashThickness.
    //   param_1 = pressure (caller passes 1.0); ModSlashThickness = g_Scale1 (runtime 1.0).
    // [SLASH-CFG]-confirmed runtime config: g_Scale1=1.0 (Thickness), g_Scale2=0.0 (EndThickness),
    // g_Scale3=1.0 (Length). Port previously used dt*10*1.0 = 0.167 -> below the UpdatePoints
    // shrink rate (45*dt) -> every point retired -> empty blade.
    const float halfWidth = pressure * 9.0f * g_Scale1;

    // Point-spacing gate (binary): param_1 * 9.0 * (ModSlashEndThickness + (ModSlashThickness - ModSlashEndThickness)*0.6)
    //   = pressure * 9.0 * (g_Scale1 + (g_Scale2 - g_Scale1)*0.6). DAT_001e9eb0 = 0.6.
    // Applied only when the trail already has at least one point.
    const float kSpacingThresh = pressure * 9.0f * (g_Scale2 + (g_Scale1 - g_Scale2) * 0.6f);
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
    LOG_DEBUG("SLASH", "AddPoint[%d]: ADDED center=(%.2f,%.2f) halfW=%.3f -> pointCount=%d",
              m_FingerId, center->x, center->y, halfWidth, m_PointCount);
#endif

    m_PrevHeadPos = m_HeadPos;
    m_HeadPos     = *center;
}

// Global slash-active frame counter (binary BSS; incremented each frame
// a head-cap vertex is emitted by UpdatePoints @ 0x1e6914).
static int s_slashes = 0;

// ---------------------------------------------------------------------------
// UpdatePoints -- binary @ 0x1e6914 (v1.6.1)
// ASM-verified: 2026-06-15T00:00 binary @ 0x1e6914 (user Ghidra decompile)
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
// Binary field mapping:
//   m_TrailShiftA -> m_field_0x138 (+0x138, Init: -1)
//   m_TrailShiftB -> m_field_0x13c (+0x13c, Init: -1)
//   m_BombHitEdge -> m_SwipeEndEdge (+0x4c)
//   m_BladeActive -> (m_State != 0) in port (binary byte field, no standalone port equivalent)
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
        // BOMB-FLASH PATH: if m_BombHitEdge (m_SwipeEndEdge) set, paint all verts red.
        // Binary: Colour::Colour(&CStack_3c, (Colour*)&Colour::Red); two-pass loop
        // (pSVar24 iterates over this and this+4, hitting m_pLeftBuffer and m_pRightBuffer
        // via the 4-byte struct pointer shift trick).
        if (m_SwipeEndEdge != 0) {
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
    const bool bladeActive = (m_State != 0);
    if (m_PointCount < 4 || !bladeActive || m_field_0x138 == -1 || m_field_0x13c == -1) {
        m_field_0x13c = -1;
        m_field_0x138 = -1;
        m_SegLenSq    = -1.0f;
    } else {
        // TODO: 0x1e6914 -- FruitCamera::TranslatePos not yet ported; use raw positions.
        // Binary transforms m_HeadPos/m_TailPos through the camera before computing ColLine.
        Vec3 headT = m_HeadPos;
        Vec3 tailT = m_TailPos;
        Vec3 midPt((headT.x + tailT.x) * 0.5f,
                   (headT.y + tailT.y) * 0.5f,
                   (headT.z + tailT.z) * 0.5f);

        ColLine* pLine = static_cast<ColLine*>(m_Col);
        if (pLine) {
            pLine->a() = midPt;
            pLine->b   = tailT;
        }

        float dx = midPt.x - tailT.x;
        float dy = midPt.y - tailT.y;
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
            Vec3 center(m_pLeftBuffer[local_320].x,
                        m_pLeftBuffer[local_320].y,
                        0.0f);
            Vec3 edgeL(m_pLeftBuffer[local_320 + 1].x,
                       m_pLeftBuffer[local_320 + 1].y,
                       0.0f);

            // Recover stored half-width: magnitude of (edgeL - center) before normalizing.
            Vec3 hwVec(edgeL.x - center.x, edgeL.y - center.y, 0.0f);
            float fVar30 = hwVec.Normalise();   // returns original magnitude

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
            //   (ModSlashThickness < ModSlashEndThickness) AND (fVar30 < ModSlashEndThickness)
            //
            // The else of the outer if = RETIRE directly.
            //
            // Summary:
            //   RETIRE_A = (g_Scale1 > g_Scale2) AND (fVar30 <= maxHW)
            //   RETIRE_B = (g_Scale1 < g_Scale2) AND (fVar30 < g_Scale2)  [inner goto]
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
                if (g_Scale1 < g_Scale2 && fVar30 < g_Scale2) retire = true;
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
                float cosA = Math::CosIdx(ang);
                float sinA = Math::SinIdx(ang);
                Vec3 perp(cosA * fVar30, sinA * fVar30, 0.0f);

                // UV.x = 0.98 for both slots.
                m_pLeftBuffer[dstCtr].u  = 0.98f;
                m_pLeftBuffer[dstEdge].u = 0.98f;

                // Inner do/while runs twice: iter=0 -> left side, iter=1 -> right side.
                // pSVar24 advances by 4 bytes each iteration to toggle which buffer is
                // written: iter0 writes m_pLeftBuffer, iter1 writes m_pRightBuffer.
                for (int iter = 0; iter < 2; iter++) {
                    QUADCUSTOMVERTEX* buf = (iter == 0) ? m_pLeftBuffer : m_pRightBuffer;

                    // Edge position: iter==0 -> center - perp (left), iter==1 -> center + perp (right).
                    Vec3 ePos;
                    if (iter == 0) { ePos = Vec3(center.x - perp.x, center.y - perp.y, 0.0f); }
                    else           { ePos = Vec3(center.x + perp.x, center.y + perp.y, 0.0f); }

                    // UV.y: ModSlashUVFlipWhenUpsideDown (g_ScaleFlag2) controls V assignment.
                    // Binary SP path (IsSameScreenMultiplayer() == false):
                    //   if g_ScaleFlag2 == 0: v=0 (iter==0), v=1 (iter==1).
                    //   else: check perp.y < 0 -> flip assignment.
                    //     perp.y >= 0: v=1 (iter==0), v=0 (iter==1)  [edge verts]
                    //     perp.y <  0: v=0 (iter==0), v=1 (iter==1)
                    // (TODO: SSM branch flips based on center.x sign -- not ported.)
                    float vVal = 0.0f;
                    if (g_ScaleFlag2 == 0) {
                        vVal = (iter != 0) ? 1.0f : 0.0f;
                    } else {
                        bool perpYNeg = (perp.y < 0.0f);
                        if (perpYNeg) {
                            vVal = (iter != 0) ? 1.0f : 0.0f;
                        } else {
                            vVal = (iter == 0) ? 1.0f : 0.0f;
                        }
                    }

                    buf[dstCtr].x  = center.x;
                    buf[dstCtr].y  = center.y;
                    buf[dstEdge].x = ePos.x;
                    buf[dstEdge].y = ePos.y;
                    buf[dstEdge].v = vVal;

                    uint32_t col = pBaseColour->PlatformColour();
                    buf[dstCtr].colour  = col;
                    buf[dstEdge].colour = col;
                }
                // iVar14 (arc index) stays unchanged for head-cap pairs (no arc accumulation).

            } else {
                // ==============================================================
                // BODY PATH: perp from Cross(Normalise(nextCenter - center), +Z).
                // ==============================================================
                Vec3 nextCtr(m_pLeftBuffer[local_320 + 2].x,
                             m_pLeftBuffer[local_320 + 2].y,
                             0.0f);

                Vec3 segDir(nextCtr.x - center.x, nextCtr.y - center.y, 0.0f);
                float segLen = segDir.Normalise();

                // Cross(segDir, Z_hat) = (segDir.y, -segDir.x, 0).
                Vec3 zHat(0.0f, 0.0f, 1.0f);
                Vec3 perpDir = Vec3::Cross(segDir, zHat);
                Vec3 perp(perpDir.x * fVar30, perpDir.y * fVar30, 0.0f);

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
                for (int iter = 0; iter < 2; iter++) {
                    QUADCUSTOMVERTEX* buf = (iter == 0) ? m_pLeftBuffer : m_pRightBuffer;

                    // Edge: iter==0 -> center - perp (left), iter==1 -> center + perp (right).
                    Vec3 ePos;
                    if (iter == 0) { ePos = Vec3(center.x - perp.x, center.y - perp.y, 0.0f); }
                    else           { ePos = Vec3(center.x + perp.x, center.y + perp.y, 0.0f); }

                    // UV.y: same flip logic as head-cap path.
                    // Binary uses local_e4 (= perpDir * fVar30 = perp) for the sign check.
                    float vVal = 0.0f;
                    if (g_ScaleFlag2 == 0) {
                        vVal = (iter != 0) ? 1.0f : 0.0f;
                    } else {
                        bool perpYNeg = (perp.y < 0.0f);
                        if (perpYNeg) {
                            vVal = (iter != 0) ? 1.0f : 0.0f;
                        } else {
                            vVal = (iter == 0) ? 1.0f : 0.0f;
                        }
                    }

                    buf[dstCtr].x  = center.x;
                    buf[dstCtr].y  = center.y;
                    buf[dstEdge].x = ePos.x;
                    buf[dstEdge].y = ePos.y;
                    buf[dstEdge].v = vVal;

                    // UV.x written after the inner loop in binary (via iVar27 byte offset).
                    // Write now to both slots -- arc-length remap will overwrite after the loop.
                    buf[dstCtr].u  = uVal;
                    buf[dstEdge].u = uVal;

                    uint32_t col = pBaseColour->PlatformColour();
                    buf[dstCtr].colour  = col;
                    buf[dstEdge].colour = col;
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
        float normFactor;
        if (g_Scale4 > 0.0f) {
            normFactor = (iVar14 > 0) ? (arcLen[iVar14] / g_Scale4) : 1.0f;
        } else {
            normFactor = 1.0f;
        }

        for (int bufPass = 0; bufPass < 2; bufPass++) {
            QUADCUSTOMVERTEX* buf = (bufPass == 0) ? m_pLeftBuffer : m_pRightBuffer;
            int byteOff = 0;
            for (int i = 0; i < m_PointCount; i += 2) {
                int arcIdx = i / 2;
                if (arcIdx > iVar14) arcIdx = iVar14;
                float arcTotal = (iVar14 > 0) ? arcLen[iVar14] : 1.0f;
                float uRemap = 0.98f - (1.0f - (arcLen[arcIdx] * 0.98f) / arcTotal) * normFactor;
                // byteOff is the byte offset into the buffer; slot = byteOff / 0x24.
                int slot = byteOff / (int)sizeof(QUADCUSTOMVERTEX);
                buf[slot].u     = uRemap;
                buf[slot + 1].u = uRemap;
                byteOff += 0x48;
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

            Vec3 lastEdge(m_pLeftBuffer[m_PointCount - 1].x,
                          m_pLeftBuffer[m_PointCount - 1].y,
                          m_pLeftBuffer[m_PointCount - 1].z);
            Vec3 prevCtr(m_pLeftBuffer[m_PointCount - 2].x,
                         m_pLeftBuffer[m_PointCount - 2].y,
                         m_pLeftBuffer[m_PointCount - 2].z);

            Vec3 hDir(lastEdge.x - prevCtr.x, lastEdge.y - prevCtr.y, 0.0f);
            hDir.Normalise();

            // Cross(hDir, Z_hat) * 2.5 -- binary local_48 = 2.5f.
            static const float kCapScale = 2.5f;
            Vec3 zHat(0.0f, 0.0f, 1.0f);
            Vec3 capOff = Vec3::Cross(hDir, zHat) * kCapScale;

            // capPos.x = prevCtr.x - capOff.x  (both buffers get the same position).
            float capX = prevCtr.x - capOff.x;
            float capY = prevCtr.y - capOff.y;

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
            // Clear slashes counter (binary @ 0x00332b34).
            // movgt/strgt @ 0x1e8444/0x1e8448: only when > 0.
            if (s_slashes > 0) {
                s_slashes = 0;
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
    // DAT_001e76ec = 0.0f: pos.xyz = 0, normal.xyz = 0, uv.u = 0, uv.v = 0; colour = white.
    // re-analyst confirmed binary InitPoints @0x1e75d0 writes per-vertex uv=(0,0).
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
            buf[i].u  = 0.0f;
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

// ASM-verified: 2026-06-16 binary @ 0x1e60a8 (re-analyst)
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
    // Binary @ 0x1ea420: gate is (m_BladeActive == 0), i.e. m_SwipeFuse == 0.
    // DrawSlice drives m_SwipeFuse to 0 within <=2 frames of lift via the
    // bit0 latch, independently of trail length / m_PointCount.
    if (m_SwipeEndEdge == 0 && m_SwipeFuse == 0) {
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
