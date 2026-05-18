//
// SlashEntity — blade trail visual-only port.
// Matches binary 0x17C82C..0x17E504. See SlashEntity.h for method addresses.
//
// Analysed: 2026-04-13T20:00
//

#include "SlashEntity.h"
#include "ActorManager.h"
#include "Entity.h"
#include "hud/HUDControl.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/gl_funcs.h"
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
#include <cstring>
#include <cmath>
#include <cstdio>

const float SlashEntity::POINT_SPACING         = 64.0f;   // DAT_0017d5fc
const float SlashEntity::MOVE_THRESH_ACTIVE    = 5.0f;    // sqrt(25)

// Binary global SlashEntity::ModPowerMask @ BSS 0x0024d8cc. See
// SlashEntity.h for bit layout + lifecycle notes.
uint32_t SlashEntity::s_ModPowerMask = 0;
// NOTE: MOVE_THRESH_INACTIVE is vestigial in the binary. The decomp of
// UpdateTouchDown (0x17D2E4) only reads DAT_0017d5f8 (= 2500 = 50²) when
// field_0x144 (the "blade active" flag) is clear — but frame 1 always
// enters the reset branch (LAB_0017d444) via the "tail uninitialised" gate
// and sets field_0x144 |= 1 at the bottom, so the 2500 threshold is never
// actually tested against a nonzero distance. Pure taps are filtered
// _implicitly_ by the 2-point minimum in RebuildGeometry and
// CollideWithSphere below — frame 1 adds exactly one point at the touch
// position, and a single point is non-renderable and non-colliding. A
// no-motion mouse click therefore cannot slice a fruit, matching the
// mobile behaviour where a tap without drag fires no move events.
const float SlashEntity::MOVE_THRESH_INACTIVE  = 50.0f;   // sqrt(DAT_0017d5f8 = 2500) — vestigial, see note above

// Per-point half-width of the blade. Binary uses 9.0 × thicknessFactor.
static const float BLADE_HALF_WIDTH = 12.0f;

// Number of trailing points to taper for the head tip. The last N points
// get progressively smaller thickness so the blade has a pinched tip.
static const int   HEAD_TAPER_COUNT = 5;

// Trail point lifetime in seconds. Each frame, points older than this are
// dropped from the front of the trail — this creates the "blade fades even
// while the finger is down" behaviour of the binary (which uses a per-frame
// perp-length extension with speed-scaled threshold — see UpdatePoints
// 0x17B92C). The port replaces that formula with simple time-based decay
// for clarity; visual feel is approximately the same.
static const float TRAIL_LIFETIME = 0.25f;

// TODO: drive g_TrailHash + g_DirectionalFlag from ItemManager equip state.
// Upstream blockers: ItemManager XML parser, SlashModInfo::SetEquipped,
// Dojo shop UI. Until then both stay 0 (no trail for default ORIGINAL_SLASH).

// --- Global content ---
static Mortar::SmartPtr<Mortar::Texture> g_BladeTex;

// --- Global instance ---
// Per-finger array (binary SlashEntity[16]). Created/destroyed in GameInit.
SlashEntity* g_pSlashEntities[16] = {0};

// Backward-compat alias for slot 0; pointer-not-reference so callers don't
// see it as "always slot 0" by name -- it tracks whatever slot 0 contains.
SlashEntity* g_pSlashEntity = nullptr;

// ---------------------------------------------------------------------------
// Blade-modifier global state. Per docs/entities/slash-mod-pipeline.md.
// All file-scope so the three setters operate on globals (no `this`).
//
// Defaults match the binary's _GLOBAL__I_Slash static-init: 16-entry white
// palette, count=1, type=0 (static), lifeScale=1, scales 1/1/0/1/0, flag2=1.
// ---------------------------------------------------------------------------
static float    g_LifeScale         = 1.0f;   // 0x001F3E54
static int      g_ColourCount       = 1;      // 0x001F3E58
static float    g_PaletteProgress   = 0.0f;   // 0x0024D874
// @ 0x0017e6cc-0x0017e6d0: 16-entry default-ctor (Black 0,0,0,255), then
// entry[0] copy-ctor from Colour::White @ 0x00268f64 (resolved via GOT
// offset 0x73a4 -> 0x001f34d4 -> 0x00268f64).
static Colour   g_Palette[16] = {
    Colour(255, 255, 255, 255),                                            // entry[0] = Colour::White
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
    Colour(0, 0, 0, 255), Colour(0, 0, 0, 255), Colour(0, 0, 0, 255),
};                                            // 0x0024D878
static int      g_ColourType        = 0;      // 0x0024D8B8 (0=static, 1=per-frame, 2=per-swipe)
static uint8_t  g_DirectionalFlag   = 0;      // 0x0024D8BC (0=no trail, 1=trail, 2=trail-rotates)
static uint32_t g_TrailHash         = 0;      // 0x0024D8C0
static uint32_t g_ContactHash       = 0;      // 0x0024D8C4
static uint32_t g_SecondHash        = 0;      // 0x0024D8C8
static Mortar::SmartPtr<Mortar::Texture> g_ModTexture; // g_SlashState.modTexture (+0xd8)

static float    g_Scale1            = 1.0f;   // 0x001F3E5C (lifetime divisor)
static float    g_Scale2            = 1.0f;   // 0x001F3E60 (max thickness coeff; max width = g_Scale2 * 9.0)
static float    g_Scale3            = 0.0f;   // 0x0024D8D0 (min thickness floor)
static float    g_Scale4            = 1.0f;   // 0x001F3E64
static float    g_Scale5            = 0.0f;   // 0x0024D8D4
static uint8_t  g_ScaleFlag1        = 0;      // 0x0024D8D8 (gates CreateGhost())
static uint8_t  g_ScaleFlag2        = 1;      // 0x001F3E69 (gates UV-mirror branch)

// Resolve a particle-emitter name to its template hash, validating that the
// emitter actually exists in PSPParticleManager. Binary calls
// `PSPParticleManager::EmitterExists(hash)` after StringHash; if not, the
// hash is zeroed so render consumers skip the emitter cleanly.
static uint32_t ResolveEmitterHash(const char* path) {
    if (!path || path[0] == '\0') return 0;
    uint32_t h = StringHash(path);
    const PSPEmitterTemplate* t =
        PSPParticleManager::GetInstance().FindTemplate(h);
    return t ? h : 0;
}

// ---------------------------------------------------------------------------
// Content load — matches LoadContent (0x17C948)
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
// Binary @ 0x0017C82C -- ctor sets vtable, default-constructs m_BaseColour /
// m_HighlightColour, zeros field_0x130 and m_TrailEmitter. The buffer
// pointers (m_pLeftBuffer, m_pRightBuffer) inherit zero from Mortar::Entity
// zero-init; port makes them explicit nullptr for clarity.
SlashEntity::SlashEntity()
    : Mortar::Entity()
    , m_NumPoints(0)
    , m_SplitPoint(0)
    , m_pLeftBuffer(nullptr)
    , m_pRightBuffer(nullptr)
    , m_TrailEmitter(nullptr)
    , m_BaseColour(255, 255, 255, 255)
    , m_HighlightColour(255, 255, 255, 255)
    , m_pCurrentTarget(nullptr)
    , m_State(0)
    , m_bHasHead(false)
    , m_Scale(0.0f)
    , m_FingerId(0)
    , m_SwipeSoundTimer(0.0f)
    , m_RawTouchPos(0, 0, 0)
    , field_0x130(0)    // ASM-verified: 2026-05-18 binary @ 0x0017C82C (re-analyst)
    , m_ComboTimer(0.0f)
    , m_ComboCount(0)
    , m_ComboEntityType(0)
    , m_SwipeEndEdge(0)
{
    for (int i = 0; i < 11; ++i) m_ComboSliceArr[i] = -1;
}

// Binary @ 0x17C774 — restore vtable, call Release, chain to Mortar::Entity::~Mortar::Entity.
// (vtable-restore is implicit in C++.)
SlashEntity::~SlashEntity() {
    Release();
}

// Port-only convenience: stores fingerId, calls binary-faithful 3-arg Init,
// then registers per-finger input callbacks (done by GameTaskInitInput in
// the binary, externally to Init itself).
void SlashEntity::Init(int fingerId) {
    m_FingerId = fingerId;
    // Delegate to binary-faithful 3-arg form (vtable slot 2).
    // Binary's GameTaskInitInput @ 0x00169670 calls Init(0, 0, &initialPos)
    // per finger after ActorManager::Add(3, true) constructs a fresh instance.
    Init(static_cast<void*>(nullptr), 0L, static_cast<Vec3*>(nullptr));
    RegisterInputCallbacks();
}

// Binary @ GameTaskInitInput 0x00169670 -- registers per-finger TouchDown/X/Y
// callbacks on InputManager for each of 16 SlashEntity[i] slots. Each slot
// registers for ITS fingerId only; the dispatch chain routes a finger's
// events to the matching SlashEntity instance.
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

    // Port-only: release dispatch. Binary doesn't have TouchReleased_n
    // (Bada delivers moves as TouchDown_n; release is implicit). SDL fires
    // explicit FINGERUP -> InputTranslatorSDL dispatches TouchUp_n.
    snprintf(buf, sizeof(buf), "TouchUp_%d", m_FingerId);
    mgr->RegisterInputCallback(StringHash(buf),
        Mortar::Delegate1<bool, InputEvent*>::Make(this, &SlashEntity::TouchUp));
}

// Binary @ 0x0017C60C -- frees heap vertex buffers, clears trail emitter,
// clears a 1-byte input-init guard (port: TODO), chains to Entity::Release.
// ASM-verified: 2026-05-18 binary @ 0x0017C60C (re-analyst)
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
    m_NumPoints = 0;
    // TODO: 0x0017C60C -- clear 1-byte input-init guard at GOT+DAT_0017c658+0x17c6e4
    //   (likely an "input-registered" flag to prevent double-registration).
}

// ---------------------------------------------------------------------------
// Reset — binary @ 0x17B71C
// Wipe touch/trail state, sentinel-mark positions (-65504), white-fill both
// vertex strips, clear 11-entry combo-slice array.
// Port note: binary fields m_pLeftBuffer/m_pRightBuffer/m_SplitPoint/
//   m_SliceFruitTypes/m_BladeDir/m_HeadPos/m_TailPos/m_PrevHeadPos/
//   m_GhostIndex/m_GhostCount/m_GhostDirs don't exist in port; port resets
//   its equivalent state (m_NumPoints, m_State, m_bHasHead).
// DIFFERS: binary @ 0x17B71C also clears m_bFlag4c (swipe-just-released edge)
//   and white-fills m_pLeftBuffer/m_pRightBuffer up to m_SplitPoint. Port
//   carries neither field; per-frame RebuildGeometry stamps m_BaseColour onto
//   every vertex, achieving the same trail-colour-flush visual.
// ---------------------------------------------------------------------------
void SlashEntity::Reset() {
    m_NumPoints = 0;
    m_State     = 0;
    m_bHasHead  = false;
    // DO NOT zero m_RawTouchPos here. Binary's Reset @ 0x17B71C clears
    // trail buffers + per-slice combo state but leaves pos.{x,y} alone --
    // they're set by TouchMoveX/Y just before TouchDown calls Reset, and
    // UpdateTouchDown reads them AFTER Reset to start the new trail at
    // the press position. Clearing here was the cause of "trail starts at
    // screen centre" on every press.
    if (m_TrailEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Chunk A stubs — trivial binary stubs
// ---------------------------------------------------------------------------

// Binary @ 0x17B3BC — empty stub, returns 0.
// SlashEntity is pure aggressor (blade), never collides into.
// Port note: port doesn't derive from Mortar::Entity; provided for call-graph completeness.
int SlashEntity::CollisionResponse() {
    return 0;
}

// Binary @ 0x17B3C0 — 4-byte stub, returns 0.
int SlashEntity::UpdateCollisionLine(long /*dt*/) {
    return 0;
}

// Binary @ 0x17B398 — clears g_state.bombSkipFlag=0, sets g_state.needsDrawFlag=1.
// DIFFERS: g_state is the binary's GameTaskState singleton; bombSkipFlag is
// the "don't slice during bomb-explosion freeze" gate -- port already covers
// this via game->bombHitTimer > 0 in UpdateTouchDown. needsDrawFlag is the
// SDK's render-needed-this-frame hint; SDL port redraws unconditionally so
// it's irrelevant. Functionally equivalent no-op.
void SlashEntity::DrawUpdate(float /*dt*/) {
}

// Binary @ 0x17B388 — clear back-pointer to combo MissControl when it gets deleted.
// DIFFERS: m_pComboMissControl back-ref isn't modelled in port. The binary's
// HUDControl::~HUDControl walks all SlashEntity[16] and nulls this slot;
// port's combo-counter subsystem isn't yet ported, so no dangling-ref hazard.
// Revisit when HUD combo subsystem ports.
void SlashEntity::MissControlDeleted(HUDControl* /*ctrl*/) {
}

// ---------------------------------------------------------------------------
// Chunk E stubs — PreUpdate, PlaySwipe, GetHeadThicknessScale
// ---------------------------------------------------------------------------

// Binary @ 0x17C584 — bump ghost frame counter, tick 8 ghost slots, advance
// per-frame palette cycle, push swipe-loop volume to ItemManager and reset
// accumulator.
// Binary @ 0x17B3B8 frozen-branch stub — no post-step work in port.
void SlashEntity::PostUpdate(float /*dt*/) {}

// ASM-verified: 2026-05-10 binary @ 0x0017C584 (asm-inspector)
//   The palette tick passes the caller's dt straight through (vmov.f32 s16,s0
//   at 0x0017c58e then vmov.f32 s0,s16 at 0x0017c5d2 before the call). The
//   DAT_0017c5fc = 0.0f the prior comment cited is the m_PreAccum (+0xc8)
//   reset at 0x0017c5ec, not a constant dt argument.
void SlashEntity::PreUpdate(float dt) {
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
// Note: previous port had "bigslice%d" / range [1,6] which had no on-disk
// assets. The actual binary literal is "Sword-swipe-%d" (Title-Case);
// Bada's loader resolves it CI to sword-swipe-{1..6}.wav.pcm. The SDL
// port matches the same CI behavior in SoundManager::LoadSound.
// (sword-swipe-7.wav.pcm exists on disk but is unreachable -- RNG max=6.)
void SlashEntity::PlaySwipe() {
    ItemManager* im = ItemManager::GetInstance();
    if (im) {
        // Stub returns void; binary returns int (0=not played, 1=played).
        // Port-side stub never plays, so always fall through.
        im->PlayAlternateSwipeSound(1.0f, 1.0f);
    }

    Game* game = Game::GetInstance();
    if (game && game->pGameSound) {
        char buf[20];
        const int idx = (rand() % 6) + 1;  // [1, 6] — matches Rand32(rng, 6) + 1
        snprintf(buf, sizeof(buf), "Sword-swipe-%d", idx);
        game->pGameSound->SFXPlay(buf, 1.0f, 1.0f);
    }

    m_SwipeSoundTimer = 6.0f;
}

// Binary @ 0x17B87C — derive head taper scale =
//   lastPairHalfWidth / (g_Scale2 * 9.0), clamped to >= 1.0.
// DIFFERS: lastPairHalfWidth comes from binary's m_pLeftBuffer last vertex
//   pair. Port stores per-point centre+thickness in m_Points[] instead;
//   binary's only consumer is CreateGhost() (unported) so return 1.0
//   is a binary-equivalent no-op (no taper override).
float SlashEntity::GetHeadThicknessScale() const {
    return 1.0f;
}

// Binary @ 0x17B82C — push next slot in global SlashEntityGhost effect ring (8 slots,
// DAT_0017b878 base+0x3c, wrap mask 0x80000007), snapshot blade vertex strips for
// fade-out replay. Distinct from the per-entity ghost-direction ring (6 slots at +0xc8).
// Port specific: SlashEntityGhost ring (s_Ghosts[8], s_GhostHead) not yet ported. No-op stub.
// ASM-verified: 2026-05-18 binary @ 0x0017B82C (re-analyst)
void SlashEntity::CreateGhost() {
}

// ---------------------------------------------------------------------------
// Chunk G — UpdateModColour
// ---------------------------------------------------------------------------

// Binary @ 0x17B0F4 — advance palette progress by dt*lifeScale, lerp between
// consecutive palette entries (or snap inside per-entry deadzone ±0.01).
// NULL outColour = global advance only (PreUpdate path).
// ASM-verified: 2026-05-09 binary @ 0x0017B0F4 (re-analyst)
void SlashEntity::UpdateModColour(Colour* outColour, float dt) {
    if (dt == 0.0f) return;  // binary's vcmpe.f32 / beq early-out

    const int count = g_ColourCount;

    if (g_ColourType == 1 /* PER_SLASH */) {
        g_PaletteProgress += dt * g_LifeScale;
        while (g_PaletteProgress >= (float)count) g_PaletteProgress -= (float)count;
        while (g_PaletteProgress <  0.0f)         g_PaletteProgress += (float)count;

        if (outColour) {
            // Snap zone: if within ±0.01 of a palette entry centre, use exact colour.
            // DAT_0017B304 = -0.01f, DAT_0017B308 = +0.01f
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
// Touch ingestion — matches UpdateTouchDown (0x17D2E4) / AddPoint (0x17CE0C)
// ---------------------------------------------------------------------------
void SlashEntity::OnTouchActive(float x, float y) {
    Vec3 newPos(x, y, 0.0f);
    // Capture the raw touch position every frame. The interpolated trail
    // points we push below can lag the true finger by up to POINT_SPACING
    // units on fast swipes — the binary emitter follows base.pos (raw)
    // not the last trail point, so store the raw value for Update to read.
    m_RawTouchPos = newPos;

    if (!m_bHasHead) {
        // First touch: start a fresh trail. Matches the reset branch in
        // binary UpdateTouchDown at LAB_0017d444 — seeds tail=head=prevHead
        // to the current touch pos and adds one point at that position,
        // then sets the "blade active" flag. A single point is intentional:
        // RebuildGeometry and CollideWithSphere both early-out at
        // m_NumPoints < 2, so a zero-motion click produces neither a visible
        // blade nor a slice. Subsequent motion of >5 units (MOVE_THRESH_ACTIVE)
        // adds the second point and the trail becomes visible/sliceable.
        m_NumPoints = 0;
        m_bHasHead = true;
        m_State = 1;
        AddPoint(newPos, Vec3(0, 0, 0));
        return;
    }

    // Movement threshold check: 5 units² = 25 when active, 50² = 2500 off.
    const Vec3 lastCenter = m_NumPoints > 0
                          ? m_Points[m_NumPoints - 1].center
                          : newPos;
    const Vec3 delta(newPos.x - lastCenter.x, newPos.y - lastCenter.y, 0.0f);
    const float distSq = delta.x * delta.x + delta.y * delta.y;
    const float thresh = (m_State != 0)
        ? (MOVE_THRESH_ACTIVE   * MOVE_THRESH_ACTIVE)
        : (MOVE_THRESH_INACTIVE * MOVE_THRESH_INACTIVE);
    if (distSq < thresh) return;

    // Interpolate intermediate points along the movement vector at
    // POINT_SPACING intervals. Matches binary's stepSize=64 loop.
    const float dist = sqrtf(distSq);
    const Vec3 dir(delta.x / dist, delta.y / dist, 0.0f);

    float travelled = POINT_SPACING;
    while (travelled < dist) {
        Vec3 step(lastCenter.x + dir.x * travelled,
                  lastCenter.y + dir.y * travelled, 0.0f);
        AddPoint(step, dir);
        travelled += POINT_SPACING;
    }

    // Final point at current touch position.
    AddPoint(newPos, dir);
    m_State = 1;
}

void SlashEntity::OnTouchReleased() {
    // Matches binary state-machine bit shift: 1 → 2 (deactivating).
    if (m_State == 1) m_State = 2;
    m_bHasHead = false;

    // Lay a graded "minimum age" gradient across the trail so the entire
    // ribbon drains within TRAIL_LIFETIME of release. Without this, the
    // head (newest point) was added this frame with age≈0 and lingers at
    // full alpha for the full 0.25 s — visible as a stuck bright tip
    // after the finger lifts. The binary's UpdatePoints @ 0x17B92C
    // collapses all pairs together via the m_Scale drop threshold
    // (decays at -2*dt per frame after release, see 0x16e3a4); replicated
    // here as a per-point age push. Existing ages are only pushed
    // forward, never reset younger — so an already-fading mid-swipe
    // release still drains as fast as before, never slower.
    // See docs/engine/slash-entity-asm-audit.md.
    if (m_NumPoints > 1) {
        const float invN = 1.0f / (float)(m_NumPoints - 1);
        for (int i = 0; i < m_NumPoints; ++i) {
            // t = 1 at tail (oldest), 0 at head (newest)
            const float t = (float)((m_NumPoints - 1) - i) * invN;
            const float minAge = TRAIL_LIFETIME * t;
            if (m_Points[i].age < minAge) m_Points[i].age = minAge;
        }
    }
}

// ---------------------------------------------------------------------------
// AddPoint — matches binary 0x17CE0C (simplified)
// ---------------------------------------------------------------------------
void SlashEntity::AddPoint(const Vec3& pos, const Vec3& dir) {
    if (m_NumPoints >= MAX_POINTS) {
        // Shift-drop the oldest point (overflow guard; time-based decay in
        // Update normally keeps the trail well below MAX_POINTS).
        for (int i = 1; i < MAX_POINTS; ++i) {
            m_Points[i - 1] = m_Points[i];
        }
        m_NumPoints = MAX_POINTS - 1;
    }

    TrailPoint& p = m_Points[m_NumPoints];
    p.center = pos;
    p.dir    = dir;
    p.age    = 0.0f;

    // Cumulative arc length from the oldest point.
    if (m_NumPoints == 0) {
        p.arcLen = 0.0f;
    } else {
        const TrailPoint& prev = m_Points[m_NumPoints - 1];
        const float dx = pos.x - prev.center.x;
        const float dy = pos.y - prev.center.y;
        p.arcLen = prev.arcLen + sqrtf(dx * dx + dy * dy);
    }

    m_NumPoints++;
}

// ---------------------------------------------------------------------------
// RebuildGeometry — matches UpdatePoints (0x17B92C) simplified.
// Generates left/right triangle-strip vertex buffers from m_Points with
// miter-joined perpendiculars, arc-length U, alpha fade, and head taper.
// Per-pair colour: UpdateModColour back-steps palette for each pair, then
// blends white toward m_HighlightColour by (1.0f - m_Scale) into m_BaseColour.
// ASM-verified: 2026-05-09 binary @ 0x0017B92C (re-analyst)
// ---------------------------------------------------------------------------
void SlashEntity::RebuildGeometry() {
    if (m_NumPoints < 2) return;
    if (!m_pLeftBuffer || !m_pRightBuffer) return;

    // Advance the highlight colour for this geometry rebuild pass.
    // Binary UpdatePoints @ 0x17B92C calls UpdateModColour(&m_HighlightColour,
    // -2.0f / numPoints) once per pair in the inner loop (stepping backward
    // through the palette so tail pairs use earlier palette entries).
    // Port: call once per rebuild with the same delta used by the binary's
    // per-pair inner loop.
    UpdateModColour(&m_HighlightColour, -2.0f / (float)m_NumPoints);

    // Derive m_BaseColour from m_Scale. Binary:
    //   if (m_Scale > 0) lerp white -> m_HighlightColour by (1-m_Scale)
    //   else             m_BaseColour = m_HighlightColour
    // m_Scale defaults to 0 (not yet lifecycle-managed), so always use
    // m_HighlightColour directly, which IS the correct fully-saturated disco
    // visual when m_Scale == 0.
    if (m_Scale > 0.0f) {
        const float blend = 1.0f - m_Scale;  // 0 = full white, 1 = full highlight
        m_BaseColour.r = (uint8_t)(255.0f + (float)((int)m_HighlightColour.r - 255) * blend);
        m_BaseColour.g = (uint8_t)(255.0f + (float)((int)m_HighlightColour.g - 255) * blend);
        m_BaseColour.b = (uint8_t)(255.0f + (float)((int)m_HighlightColour.b - 255) * blend);
        m_BaseColour.a = (uint8_t)(255.0f + (float)((int)m_HighlightColour.a - 255) * blend);
    } else {
        m_BaseColour = m_HighlightColour;
    }

    const float totalArc = m_Points[m_NumPoints - 1].arcLen;
    const float invArc   = (totalArc > 0.0f) ? (1.0f / totalArc) : 0.0f;

    for (int i = 0; i < m_NumPoints; ++i) {
        const TrailPoint& p = m_Points[i];

        // Miter-join direction: average of incoming and outgoing dirs at
        // interior points; endpoints use their own dir. This smooths out
        // the "kink" the original code had at direction changes.
        Vec3 d = p.dir;
        if (i + 1 < m_NumPoints) {
            const Vec3& next = m_Points[i + 1].dir;
            d.x = (d.x + next.x) * 0.5f;
            d.y = (d.y + next.y) * 0.5f;
        }
        // Normalise d (fallback to incoming if both zero).
        const float dlen = sqrtf(d.x * d.x + d.y * d.y);
        if (dlen > 0.0001f) {
            d.x /= dlen;
            d.y /= dlen;
        } else {
            d = p.dir;
        }

        // Head taper: last HEAD_TAPER_COUNT points shrink toward tip.
        float thickness = 1.0f;
        const int headDist = (m_NumPoints - 1) - i;  // 0 at tip
        if (headDist < HEAD_TAPER_COUNT) {
            thickness = (float)headDist / (float)HEAD_TAPER_COUNT;
        }

        // Perpendicular: 90° CW rotation of miter direction, scaled.
        const float half = BLADE_HALF_WIDTH * thickness;
        const float perpX = -d.y * half;
        const float perpY =  d.x * half;

        // Arc-length U (0 at tail, approaching 1 at head).
        const float u = p.arcLen * invArc * 0.98f;

        // Alpha fade by age: full at 0, zero at TRAIL_LIFETIME. Oldest
        // points (the tail) fade out visually as they approach expiry.
        // Colour RGB comes from m_BaseColour (disco-cycle or white).
        float alphaFrac = 1.0f - (p.age / TRAIL_LIFETIME);
        if (alphaFrac < 0.0f) alphaFrac = 0.0f;
        if (alphaFrac > 1.0f) alphaFrac = 1.0f;
        const uint32_t alpha = (uint32_t)(alphaFrac * (float)m_BaseColour.a);
        // Pack ABGR (OpenGL ES expects RGBA in memory on little-endian ARM; port
        // uses the same 0xAABBGGRR layout as the binary's QUADCUSTOMVERTEX colour).
        const uint32_t col = (alpha << 24)
                           | ((uint32_t)m_BaseColour.b << 16)
                           | ((uint32_t)m_BaseColour.g <<  8)
                           |  (uint32_t)m_BaseColour.r;

        // Left strip: outer edge to centre.
        QUADCUSTOMVERTEX& l0 = m_pLeftBuffer[i * 2    ];
        QUADCUSTOMVERTEX& l1 = m_pLeftBuffer[i * 2 + 1];
        // Right strip: centre to outer edge.
        QUADCUSTOMVERTEX& r0 = m_pRightBuffer[i * 2    ];
        QUADCUSTOMVERTEX& r1 = m_pRightBuffer[i * 2 + 1];

        l0.x = p.center.x - perpX;
        l0.y = p.center.y - perpY;
        l0.z = p.center.z;
        l0.u = u; l0.v = 0.0f;
        l0.colour = col;

        l1.x = p.center.x;
        l1.y = p.center.y;
        l1.z = p.center.z;
        l1.u = u; l1.v = 0.5f;
        l1.colour = col;

        r0.x = p.center.x;
        r0.y = p.center.y;
        r0.z = p.center.z;
        r0.u = u; r0.v = 0.5f;
        r0.colour = col;

        r1.x = p.center.x + perpX;
        r1.y = p.center.y + perpY;
        r1.z = p.center.z;
        r1.u = u; r1.v = 1.0f;
        r1.colour = col;
    }
}

// ---------------------------------------------------------------------------
// Update — matches SlashEntity::Update (0x17D664) + UpdateTouchDown (0x17D2E4)
// ---------------------------------------------------------------------------
void SlashEntity::Update(float dt) {
    // Touch ingestion is event-driven via the four InputManager-registered
    // callbacks (TouchDown_0, TouchMove_X0, TouchMove_Y0, TouchUp_0) bound
    // in RegisterInputCallbacks(). Binary @ 0x17D664 -- the per-frame body
    // is purely combo/scoring/SFX work; trail extension happens in
    // UpdateTouchDown which fires on every Bada TouchDown_n event (press
    // AND each move while held). Port replicates this by re-dispatching
    // TouchDown_n on SDL_MOUSEMOTION/SDL_FINGERMOTION in InputTranslatorSDL.

    // Trail particle emitter — matches binary UpdateTouchDown (0x17D2E4).
    // Created on first active touch, follows the head each frame, cleared
    // on release. See TRAIL_EMITTER_NAME TODO above for the full ItemManager
    // path this should come from eventually.
    PSPParticleManager& pm = PSPParticleManager::GetInstance();
    const bool bladeActive = (m_State != 0) && (m_NumPoints > 0);
    // Trail emitter only spawns when blade-mod has set g_DirectionalFlag and
    // a valid g_TrailHash (resolved by SetModColours). The default blade
    // ("ORIGINAL_SLASH" in itemlist.xml) leaves both at 0 so no trail.
    // Hardcoded TRAIL_EMITTER_NAME is dead code -- kept as a fallback while
    // the blade equip pipeline ramps up. Once shop equip is verified
    // end-to-end, drop the fallback.
    const bool wantTrail = bladeActive && g_DirectionalFlag != 0 && g_TrailHash != 0;
    if (wantTrail) {
        if (!m_TrailEmitter) {
            m_TrailEmitter = pm.AddEmitter(g_TrailHash, &m_TrailEmitter, /*persistent=*/true);
            if (m_TrailEmitter) {
                m_TrailEmitter->m_bUpdateWhenPaused = true;
            }
        }
        if (m_TrailEmitter) {
            // Follow the raw finger position, not the last interpolated
            // trail point — matches UpdateTouchDown @ 0x17D2E4 which writes
            // m_TrailEmitter->m_Pos = this->base.pos (the raw touch).
            m_TrailEmitter->m_Pos = m_RawTouchPos;
        }
    } else if (!bladeActive && m_TrailEmitter) {
        pm.ClearEmitter(m_TrailEmitter);
        m_TrailEmitter = nullptr;
    }

    // Age every point by dt. This runs unconditionally — even while the
    // finger is still down — so the trail naturally fades from the tail
    // even during a continuous swipe. Mirrors the binary's per-frame
    // UpdatePoints pass at 0x17B92C where each pair's perp length is
    // extended toward a drop threshold.
    for (int i = 0; i < m_NumPoints; ++i) {
        m_Points[i].age += dt;
    }

    // Drop expired points from the front of the trail (oldest = tail).
    int dropCount = 0;
    while (dropCount < m_NumPoints &&
           m_Points[dropCount].age >= TRAIL_LIFETIME) {
        dropCount++;
    }
    if (dropCount > 0) {
        for (int i = dropCount; i < m_NumPoints; ++i) {
            m_Points[i - dropCount] = m_Points[i];
        }
        m_NumPoints -= dropCount;
    }

    // State machine collapse: if the finger was released and the trail
    // drained, reset to idle. The binary's m_bBladeActive state machine
    // (1 → 2 → 0) is simulated by the age-based drop above.
    if (m_State == 2 && m_NumPoints == 0) {
        m_State = 0;
    }

    // Slice-test pass. Matches the FRUIT/BOMB collision loops inside
    // SlashEntity::Update (0x17D664). Only runs when the blade has at
    // least 2 points, isn't deactivating, and the post-explosion game-over
    // window isn't already ticking (binary gate: `if (game->bombTimer > 0)
    // return` at 0x17D664 line ~442). This stops the blade from registering
    // more slices once a bomb has already gone off.
    Game* game = Game::GetInstance();
    // bombHitTimer is dual-purpose (re-analyst 2026-05-17): bomb-hit freeze
    // (3.2s set by HitBomb) AND unpause input-freeze (0.4f set by
    // UnpauseGame @ 0x168fb0). Either way `> 0` gates the collision loop.
    // The matching gate in UpdateTouchDown above prevents new points from
    // being added during the freeze, so `m_NumPoints < 4` short-circuits
    // naturally -- the same mechanism the binary uses post-unpause.
    const bool bombHitActive = game && game->bombHitTimer > 0.0f;

    // Binary @ 0x17D664: also gates on `(s_ModPowerMask & 0x40)` -- bit
    // 0x40 set by ScrollingMenu::Update on touch-acquire (@ 0x0015b7cc)
    // and cleared on release (@ 0x0015baba) + DestroyList (@ 0x0015afea).
    // Suppresses slicing while the player is dragging a shop / dojo
    // scroll list (so taps don't slice fruits behind it).
    const bool menuDragActive = (SlashEntity::s_ModPowerMask & 0x40u) != 0u;

    // Note: the binary has NO pause gate inside SlashEntity itself.
    // Slicing during pause is suppressed STRUCTURALLY in the binary:
    //   - GameTaskUpdate @ 0x0010a5d4 forces `active = false` when
    //     Game->pausedFlag != 0.
    //   - GameUpdate @ 0x0016c378 only calls ActorManager::Update on
    //     the active branch -- so Fruit/Bomb collision tests never
    //     run while paused.
    //   - SlashEntity::Update keeps running with real dt; the trail
    //     builds and fades normally, just has nothing to hit.
    // Earlier RE pass speculated about "Bada OS-level touch routing"
    // intercepting touches during pause -- that was wrong, the
    // PauseScreen is just an in-game UI element; touches still reach
    // SlashEntity in both binary and port.
    //
    // Port-side defense (NOT in binary): also short-circuit our own
    // collision loop on pausedFlag. Redundant if ActorManager::Update
    // is properly gated above; kept as belt-and-braces.
    const bool gamePaused = game && game->pausedFlag;

    // Tick the swipe-SFX cooldown timer (binary +0x148, decremented per
    // frame; PlaySwipe resets to 6.0f).
    if (m_SwipeSoundTimer > 0.0f) {
        m_SwipeSoundTimer -= 1.0f;
        if (m_SwipeSoundTimer < 0.0f) m_SwipeSoundTimer = 0.0f;
    }

    bool slicedThisFrame = false;
    if (m_NumPoints >= 2 && m_State != 0 && !bombHitActive
        && !menuDragActive && !gamePaused) {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        if (am) {
            // Only fruit (0) and bomb (1) participate in blade collision
            // — matches binary.
            for (int t = 0; t <= 1; t++) {
                const std::list<Mortar::Entity*>& list = am->GetTypeList(t);
                for (auto it = list.begin(); it != list.end(); ++it) {
                    Mortar::Entity* e = *it;
                    if (!e || !e->IsActive()) continue;
                    if (!e->m_Col) continue;
                    ColSphere* cs = static_cast<ColSphere*>(e->m_Col);
                    if (cs->radius <= 0.0f) continue;

                    Vec3 bladeVel;
                    if (CollideWithSphere(*cs, bladeVel)) {
                        // Binary @ 0x0017d664: vtable[9](victim, slashEntity, 0, 0, &bladeVel).
                        // Port: SlashEntity does not inherit Mortar::Entity, pass nullptr for hitter.
                        // Fruit/Bomb CollisionResponse only reads bladeVelocity; hitter unused.
                        e->CollisionResponse(nullptr, 0, 0, &bladeVel);
                        slicedThisFrame = true;
                    }
                }
            }
        }
    }

    // Per-slice swipe SFX. Binary @ 0x17D664 fires PlaySwipe when a slice
    // landed this frame and the cooldown has elapsed (m_SwipeSoundTimer ==
    // 0). PlaySwipe re-arms the cooldown to 6.0f preventing back-to-back
    // sounds when one swipe slices several fruits in quick succession.
    if (slicedThisFrame && m_SwipeSoundTimer == 0.0f) {
        PlaySwipe();
    }

    // Per-swipe combo counter — binary SlashEntity::Update @ 0x0017de72.
    // Ticks m_ComboTimer; fires AddSpeed when ComboCount > 2 in Arcade mode.
    // ASM-verified: 2026-05-18 binary @ 0x0017de72 (re-analyst)
    if (m_ComboTimer >= 0.0f) {
        m_ComboTimer += dt;
        if (m_ComboCount > 1 && m_ComboSliceArr[1] >= 0) {
            if (m_ComboCount > 2 && game && game->gameMode == Mortar::GAME_MODE_ARCADE) {
                WaveManager::GetInstance()->AddSpeed(
                    (float)m_ComboCount / 3.0f, 0);
                // TODO: 0x0017dde6 — AddToCurrentScore + combo-bonus VFX/SFX trailing block
                // (binary @ 0x0017dde6..0x0017dec4; ~30 ARM instructions; RE needed).
            }
        }
    }

    RebuildGeometry();
}

// ---------------------------------------------------------------------------
// CollideWithSphere — matches CollideWithEntity (0x17B570) simplified.
// The binary tests a single blade ColLine (head↔tail of this frame's swipe
// delta) against a fruit/bomb ColSphere. The port instead iterates every
// segment between consecutive trail points so that a fast swipe — which
// OnTouchActive interpolates into many POINT_SPACING=64 sub-points within a
// single frame — still registers the hit.
// ---------------------------------------------------------------------------
bool SlashEntity::CollideWithSphere(const ColSphere& sphere,
                                     Vec3& outBladeVel) const {
    if (m_State == 0 || m_NumPoints < 2) {
        outBladeVel = Vec3(0, 0, 0);
        return false;
    }

    // Scan every segment; return the direction+length of the one that hit so
    // OnSliced can derive impulse magnitude AND slice angle. Binary path:
    // CollideWithEntity (0x17B570) uses the per-frame blade delta — one
    // segment per update. The port has N interpolated sub-segments per frame,
    // so we pick the segment that actually intersects.
    for (int i = 0; i + 1 < m_NumPoints; ++i) {
        ColLine seg(m_Points[i].center, m_Points[i + 1].center);
        if (sphere.IntersectsLine(seg)) {
            outBladeVel = m_Points[i + 1].center - m_Points[i].center;
            return true;
        }
    }
    outBladeVel = Vec3(0, 0, 0);
    return false;
}

// ---------------------------------------------------------------------------
// DrawSlice — matches 0x17E424
// ---------------------------------------------------------------------------

// Entity vtable slot 5 (+0x14): Draw(Renderer&) override.
// Binary @ 0x17B3B8 is a 1-instruction BX lr stub. ActorManager::Draw walks
// type-3 entities and calls this slot, getting no output. All blade rendering
// goes through DrawSlice, dispatched explicitly from GameDraw's 16-slot loop.
// ASM-verified: 2026-05-18 binary @ 0x0017B3B8 (re-analyst)
void SlashEntity::Draw(Renderer& /*r*/) {
    // No-op. Matches binary's single-instruction BX lr stub.
}

// ---------------------------------------------------------------------------
// Blade modifier apply functions (called from SlashModInfo::SetEquipped)
// ---------------------------------------------------------------------------

// SetModColours @ 0x0017ca0c. Full spec: docs/entities/slash-mod-pipeline.md.
//
// Writes the colour palette + particle hashes + overlay texture into the
// file-scope globals, then walks Mortar::ActorManager type-3 (SlashEntity) actors
// and direct-calls ColoursChanged on each so live blade entities pick up
// the change.
//
// Note: g_DirectionalFlag is only set when the trail particle path resolves
// to an existing emitter — otherwise stays at 0 (no trail). The other two
// hashes (contactParticle, particle2) zero on miss but don't toggle the
// directional flag.
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
    // Scalar globals
    g_LifeScale  = lifeScale;
    g_ColourType = colourType;

    // Palette copy (count clamped to 16 for safety; binary trusts the caller).
    if (colourCount < 0) colourCount = 0;
    if (colourCount > 16) colourCount = 16;
    g_ColourCount = colourCount;
    for (int i = 0; i < colourCount; ++i) {
        g_Palette[i] = colours ? colours[i] : Colour(255, 255, 255, 255);
    }

    // Palette progress init. Binary @ 0x17ca7c-0x17caa2:
    //   default progress = 1.0
    //   if (colourType == 2): progress = (float)(Rand32() % colourCount)
    g_PaletteProgress = 1.0f;
    if (g_ColourType == 2 && g_ColourCount > 0) {
        g_PaletteProgress = (float)((unsigned)rand() % (unsigned)g_ColourCount);
    }

    // Overlay texture: load from name (or null when name is empty).
    // XML stores names without extension (e.g. "disco_blade"); append .tex.
    if (textureName2 && textureName2[0] != '\0') {
        char texPath[256];
        snprintf(texPath, sizeof(texPath), "%s.tex", textureName2);
        g_ModTexture = Mortar::TextureManager::LoadLocalisedTexture(texPath);
    } else {
        g_ModTexture.SetNull();
    }

    // Trail particle hash. Binary @ 0x17cb02-0x17cb1a: keeps the StringHash
    // even when the emitter doesn't exist; only g_DirectionalFlag is gated
    // on existence (so consumers must check the flag first, not the hash).
    g_TrailHash = (particlePath && particlePath[0] != '\0')
                ? StringHash(particlePath) : 0;
    bool trailExists = g_TrailHash != 0 &&
        PSPParticleManager::GetInstance().FindTemplate(g_TrailHash) != nullptr;

    // Contact + second hashes: zero on miss (binary's `EmitterExists` gate).
    g_ContactHash = ResolveEmitterHash(contactParticle);
    g_SecondHash  = ResolveEmitterHash(particle2);

    // g_DirectionalFlag: 0 = no trail, 1 = trail, 2 = trail rotates with swipe.
    // Set to non-zero only when trail emitter exists.
    g_DirectionalFlag = trailExists ? (directional ? 2 : 1) : 0;

    // Live-update walker. Binary @ 0x0017ca0c walks
    // Mortar::ActorManager::GetEntityFirst(type=3) and direct-calls
    // SlashEntity::ColoursChanged on each of 16 SlashEntity instances (NOT
    // through vtable). Port: iterate the per-finger array directly.
    for (int i = 0; i < 16; ++i) {
        if (g_pSlashEntities[i]) {
            g_pSlashEntities[i]->ColoursChanged();
        }
    }
}

// InitModColours @ 0x0017cc38. Resets blade-mod state to defaults
// (16-entry white palette, count=1, type=0 static, lifeScale unchanged
// per binary -- the function does NOT touch lifeScale or scales).
// Does NOT walk active entities.
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

// SetModScales @ 0x0017b328. Pure global-write, no validation, no walker.
// Default no-mod call: SetModScales(1.0f, 1.0f, 0.0f, 1.0f, false, false, 0.0f).
void SlashEntity::SetModScales(
    float startThick,
    float endThick,
    float scaleLen,
    float uvLen,
    bool  flipUD,
    bool  loop,
    float loopUVLen)
{
    // Param mapping per RE doc:
    //   param_1 startThick -> g_Scale1 (lifetime divisor)
    //   param_2 endThick   -> g_Scale2 (max thickness coeff)
    //   param_3 scaleLen   -> g_Scale3 (min thickness floor)
    //   param_4 uvLen      -> g_Scale4
    //   param_5 flipUD     -> g_ScaleFlag1
    //   param_6 loop       -> g_ScaleFlag2
    //   param_7 loopUVLen  -> g_Scale5
    g_Scale1     = startThick;
    g_Scale2     = endThick;
    g_Scale3     = scaleLen;
    g_Scale4     = uvLen;
    g_Scale5     = loopUVLen;
    g_ScaleFlag1 = flipUD ? 1 : 0;
    g_ScaleFlag2 = loop   ? 1 : 0;
}

// ResetModScales — set all 6 blade-mod scale globals back to 1.0f.
// Called by PowerUpManager::SetDefaults (0x00117a80) and ::Reset (0x00119b08).
// Binary writes 6 float fields to 0x3f800000 (1.0f) via the struct held in
// g_pFruitNinjaApp->m_pGame at +0x3c; port maps these to the file-scope globals.
void SlashEntity::ResetModScales() {
    g_Scale1     = 1.0f;
    g_Scale2     = 1.0f;
    g_Scale3     = 1.0f;
    g_Scale4     = 1.0f;
    g_Scale5     = 1.0f;
    g_ScaleFlag1 = 1;
    g_ScaleFlag2 = 1;
}

// ColoursChanged @ 0x0017c41c. Per-instance live-update fired by the
// SetModColours walker. NOT virtual (binary direct-calls).
//
// - Clears existing trail emitter (so it gets re-created from new hash).
// - If blade is currently active (m_State != 0), truncates trail geometry
//   and re-creates the trail emitter from g_TrailHash if directional flag set.
//
// Port note: m_HighlightColour and UpdateModColour are not yet ported; the
// binary also re-snaps the per-swipe highlight colour here when
// g_ColourType == 2. We skip that until the highlight system lands —
// currently visible only for type-2 mods which aren't shipped.
void SlashEntity::ColoursChanged() {
    // DIFFERS: port-side plug for the m_Scale-lifecycle gap (binary @ 0x0017C41C
    // does NOT reset m_HighlightColour or m_BaseColour here -- per asm-inspector
    // 2026-05-10). The binary only refreshes m_HighlightColour for PER_SWIPE
    // (g_ColourType == 2) via UpdateModColour(&m_HighlightColour, 1.0f) inside
    // the m_bDirty branch; PER_SLASH continuously refreshes via PreUpdate, and
    // NONE leaves the field untouched because the binary's RebuildGeometry only
    // writes m_HighlightColour when g_ColourType != 0. With m_Scale lifecycle
    // ported (1.0 in critical, -2*dt decay), the binary's m_Scale > 0 path
    // would lerp white -> stale m_HighlightColour, hiding the leak. Port has
    // m_Scale stuck at 0 so the m_Scale==0 else-branch in RebuildGeometry
    // copies stale bytes straight to the vertex stamp -- visible as disco
    // tint persisting through a NONE-blade swap. Snap m_HighlightColour /
    // m_BaseColour to g_Palette[0] until m_Scale lifecycle lands.
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
        // Truncate trail so the new colour palette / overlay tex doesn't
        // get applied retroactively to mid-swipe geometry.
        m_NumPoints = 0;

        // Re-create trail emitter from new hash if directional flag set.
        // Binary @ 0x17c466-0x17c47a calls AddEmitter(hash, NULL, true) and
        // sets m_bUpdateWhenPaused = 1 on the result.
        if (g_DirectionalFlag != 0 && g_TrailHash != 0) {
            m_TrailEmitter = PSPParticleManager::GetInstance()
                .AddEmitter(g_TrailHash, /*ppRef=*/nullptr, /*persistent=*/true);
            if (m_TrailEmitter) {
                m_TrailEmitter->m_bUpdateWhenPaused = true;
            }
        }
    }
}

// Accessors used by render consumers in this file. Hot inlines kept in
// the .cpp so the globals stay file-scope.
const Mortar::SmartPtr<Mortar::Texture>& SlashEntity::GetModTexture()    { return g_ModTexture; }
uint32_t SlashEntity::GetTrailEmitterHash()                       { return g_TrailHash; }
uint32_t SlashEntity::GetContactEmitterHash()                     { return g_ContactHash; }
uint32_t SlashEntity::GetSecondEmitterHash()                      { return g_SecondHash; }
uint8_t  SlashEntity::GetDirectionalFlag()                        { return g_DirectionalFlag; }
int      SlashEntity::GetColourCount()                            { return g_ColourCount; }
int      SlashEntity::GetColourType()                             { return g_ColourType; }
const Colour* SlashEntity::GetPalette()                           { return g_Palette; }

// ---------------------------------------------------------------------------
// Binary symbol parity (re-analyst 2026-05-18) — these match binary symbol
// names but the actual port-side equivalents live elsewhere. Each function
// forwards or no-ops with documentation.
// ---------------------------------------------------------------------------

// Binary @ 0x17CE0C -- main per-point append into m_pLeftBuffer/m_pRightBuffer
// with rotation-angle bookkeeping. Port replaces with the OnTouchActive +
// AddPoint(const Vec3&) + RebuildGeometry pipeline driven from the touch
// move events. This 3-arg form has no port equivalent caller; keep as
// no-op stub for binary-symbol parity.
void SlashEntity::AddPoint(Vec3 /*pos*/, Vec3 /*dir*/, float /*unused*/) {}

// Binary @ 0x17B570 -- vtable slot 9 override on Mortar::Entity. Tests this
// blade's ColLine against entity->m_Col (a ColSphere). Port's Update slice
// loop calls CollideWithSphere() per-entity directly (iterates full trail),
// so this entry point is unreached. Keep `return false` for safety.
bool SlashEntity::CollideWithEntity(Mortar::Entity* /*entity*/) { return false; }

// Binary @ 0x17B3BC -- 1-instruction stub `mov r0,#0; bx lr`. SlashEntity is
// pure aggressor (it hits things, not the other way around).
int SlashEntity::CollisionResponse(Mortar::Entity* /*hitter*/, unsigned long /*mask1*/,
                                    unsigned long /*mask2*/, Vec3* /*bladeVel*/) { return 0; }

// Binary @ 0x17E424 -- main blade render (two mirrored tri-strips).
// Called from GameDraw's 16-slot vtable loop (binary @ 0x0016b888),
// NOT from ActorManager::Draw (which hits the BX lr Draw stub instead).
// ASM-verified: 2026-05-18 binary @ 0x0017E424 (re-analyst)
//
// m_SwipeEndEdge is a 2-bit shift-register fuse: writer (touch-up handler,
// outside SlashEntity) sets bit0; each DrawSlice call shifts left and fires
// CreateGhost + contact-burst emitter on the frame when the last bit falls off.
// TODO: 0x???????? -- touch-up writer for m_SwipeEndEdge bit 0
// DIFFERS: binary gate is m_NumPoints > 3; port uses >= 2 to render
//   single-tap micro-trails (cosmetic only).
void SlashEntity::DrawSlice() {
    // 2-frame fuse: shift register fires burst exactly 2 DrawSlice frames
    // after touch-up writer set bit0. Matches binary @ 0x0017E424.
    // ASM-verified: 2026-05-18 binary @ 0x0017E424 (re-analyst)
    {
        uint8_t prev = m_SwipeEndEdge;
        m_SwipeEndEdge = (prev << 1) & 0x02;
        if (prev != 0 && m_SwipeEndEdge == 0) {
            if (g_ScaleFlag1) CreateGhost();
            // TODO: 0x0017E424 -- contact-burst emitter spawn at this->pos
        }
    }

    if (m_NumPoints < 2) return;
    if (!m_pLeftBuffer || !m_pRightBuffer) return;

    // Texture select: blade-mod overlay (g_ModTexture) replaces the default
    // blade.tex when set. Binary @ 0x0017E424:
    //   if (SmartPtr::IsValid(g_SlashState.modTexture)) bind modTexture
    //   else bind defaultTexture
    Mortar::SmartPtr<Mortar::Texture>& bladeTex =
        g_ModTexture.IsValid() ? g_ModTexture : g_BladeTex;
    if (!bladeTex.IsValid()) return;

    // Matrix reset + MVP upload. Matches binary DrawSlice 0x17E424 prelude.
    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    bladeTex->Set();

    if (Renderer* r = Renderer::GetInstance()) {
        // 2 verts per trail point (interleaved edge + centre in each buffer).
        // RebuildGeometry writes indices 0..m_NumPoints*2-1.
        const int vertCount = m_NumPoints * 2;
        r->DrawTriStrip(m_pLeftBuffer,  vertCount);
        r->DrawTriStrip(m_pRightBuffer, vertCount);
    }

    bladeTex->UnSet();
}

// Binary @ 0x17C65C -- vtable slot 2. All 3 params are vestigial and ignored.
// Allocates ColLine into m_Col, calls InitPoints(160), inits ghost-dir ring,
// 11-entry combo-slice array to -1, sets field_0x148 = 6.0f.
// ASM-verified: 2026-05-18 binary @ 0x0017C65C (re-analyst)
void SlashEntity::Init(void* /*unused*/, long /*unused*/, Vec3* /*unused*/) {
    // 1. Allocate ColLine into m_Col (+0x38). Set "has collider" flag bit.
    m_Col = new ColLine();
    flags |= 0x02;

    // 2. Reset scale-adjacent float at +0x94.
    *(float*)((char*)this + 0x94) = -1.0f;

    // 3. Build vertex buffers (160 pairs).
    InitPoints(160);

    // 4. Per-frame scratch state.
    *(float*)((char*)this + 0x98) = 0.0f;
    m_TrailEmitter = nullptr;
    m_Scale        = 0.0f;
    *(uint32_t*)((char*)this + 0x9c) = 0xFFFFFFFFu;

    // 5. Copy Colour::White into both colour fields.
    Colour white(255, 255, 255, 255);
    m_HighlightColour = white;
    m_BaseColour      = white;

    // 6. Combo / ghost-ring init.
    m_ComboTimer      = 0.1f;   // per-swipe accumulator (DAT_0017c764)
    m_ComboCount      = 0;
    m_ComboEntityType = 0;
    // m_GhostDir at +0x118 (Vec3): zero
    *(float*)((char*)this + 0x118) = 0.0f;
    *(float*)((char*)this + 0x11c) = 0.0f;
    *(float*)((char*)this + 0x120) = 0.0f;
    *(float*)((char*)this + 0x134) = 0.0f;
    *(uint8_t*)((char*)this + 0x4c) = 0;   // m_bFlag4c
    *(int*)    ((char*)this + 0x138) = 0;
    *(uint16_t*)((char*)this + 0x36) = 0;  // angle
    *(int*)    ((char*)this + 0x114) = 0;  // m_GhostCount
    *(int*)    ((char*)this + 0x110) = 0;  // m_GhostIndex

    // 7. Per-entity ghost-direction average ring: 6 slots (Vec3[6] at +0xc8, stride 12).
    // This is distinct from the global SlashEntityGhost effect ring (8 slots, DAT_0017b878).
    // Binary Init @ 0x0017C65C zeroes 6 entries (loop bound 0x48 = 72 = 6*12).
    // AddPoint mod-divisor is 6 (binary @ 0x0017CF8E: movs r1,#0x6; idivmod).
    // ASM-verified: 2026-05-18 binary @ 0x0017CE0C (re-analyst)
    for (int i = 0; i < 6; ++i) {
        *(float*)((char*)this + 0xc8 + i * 12     ) = 0.0f;
        *(float*)((char*)this + 0xc8 + i * 12 + 4 ) = 0.0f;
        *(float*)((char*)this + 0xc8 + i * 12 + 8 ) = 0.0f;
    }

    // 8. 11-entry int32 combo-slice array, all set to -1.
    for (int i = 0; i < 11; ++i) {
        m_ComboSliceArr[i] = -1;
    }

    // 9. Swipe-SFX cooldown timer (binary m_SwipeSoundTimer at +0xc4).
    m_SwipeSoundTimer = 0.0f;

    // 10. Extra combo fields (binary +0x14c, +0x150 within combo-slice array range).
    *(int*)((char*)this + 0x150) = -1;  // m_ExtraFieldB
    *(int*)((char*)this + 0x14c) = -1;  // m_ExtraFieldA
    m_ComboEntityType = 0;
    m_ComboCount      = 0;

    // 11. Initial post-init swipe SFX cooldown at +0x148 = 6.0f.
    *(float*)((char*)this + 0x148) = 6.0f;
}

// Binary @ 0x17C340 -- allocates m_pLeftBuffer/m_pRightBuffer arrays,
// fills with sentinel/white records, resets counters and Vec3 sentinels.
// ASM-verified: 2026-05-18 binary @ 0x0017C340 (re-analyst)
void SlashEntity::InitPoints(long count) {
    // Stack-local Colour::White -> packed PlatformColour (0xFFFFFFFF for white).
    Colour whiteColour(255, 255, 255, 255);
    uint32_t whitePacked = whiteColour.PlatformColour();

    // Reset point counters.
    m_NumPoints  = 0;
    m_SplitPoint = (int)count;   // +0x50: set to 160 from Init's call

    // Sentinel value -65504.0f (DAT_0017c408 = 0xC77FFF00).
    static const float SENTINEL = -65504.0f;
    const Vec3 sentinel(SENTINEL, SENTINEL, SENTINEL);

    // NaN bit pattern reinterpreted as float (0xFFFFFFFF = quiet NaN).
    uint32_t nanBits = 0xFFFFFFFFu;
    float nanFloat;
    memcpy(&nanFloat, &nanBits, sizeof(float));

    // Allocate left + right vertex buffers. Binary allocates (count+2) records
    // per buffer at 1 vert per point. Port's RebuildGeometry writes 2 verts
    // per trail point (edge + centre interleaved), so each buffer needs
    // (count*2 + 2) entries.
    // DIFFERS: binary = (count+2) * 0x24; port = (count*2+2) * 0x24 because
    //   RebuildGeometry interleaves 2 verts per trail point per buffer.
    for (int side = 0; side < 2; ++side) {
        QUADCUSTOMVERTEX* buf = new QUADCUSTOMVERTEX[(count * 2 + 2)];
        (&m_pLeftBuffer)[side] = buf;

        // Fill valid slots [0..count*2-1] with sentinel/white.
        // Slots [count*2, count*2+1] left uninitialised (scratch shift slots).
        for (int i = 0; i < count * 2; ++i) {
            buf[i].x  = nanFloat; buf[i].y  = nanFloat; buf[i].z  = nanFloat;
            buf[i].u  = nanFloat; buf[i].v  = nanFloat;
            buf[i].nx = nanFloat; buf[i].ny = nanFloat; buf[i].nz = 1.0f;
            buf[i].colour = whitePacked;
        }
    }
}

// STUB: SlashEntity::SetModColours(Colour*, ...) — non-const binary form
// Binary @ 0x17CA0C — binary passes Colour* (non-const); delegates to const form.
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
// Press-edge handler. Gate: blade idle (m_State == 0). Binary also checks
// m_bFlag4c (+0x4c) -- not yet modeled in port, omit guard. On idle press:
// Reset() the trail; if PER_SWIPE colour mode (g_ColourType == 2), advance
// the palette via UpdateModColour. Then call UpdateTouchDown to ingest the
// initial touch position. Returns true (event consumed).
bool SlashEntity::TouchDown(InputEvent* event) {
    if (m_State == 0) {
        Reset();
        if (g_ColourType == 2) {
            UpdateModColour(&m_HighlightColour, 1.0f);
        }
    }
    UpdateTouchDown(event);
    return true;
}

// ASM-spec: SlashEntity::TouchMoveX @ 0x17C50C
// Per-event X-axis ingestion. Binary writes pos.x = event->m_mapper - 0.5*W
// (raw pixel -> centred ortho). Port's InputTranslatorSDL pre-centres into
// event->x, so just copy. Port stores into m_RawTouchPos (no Entity::pos).
bool SlashEntity::TouchMoveX(InputEvent* event) {
    Game* g = Game::GetInstance();
    if (g && g->bombHitTimer > 0.0f) return false;
    m_RawTouchPos.x = event->x;
    return true;
}

// ASM-spec: SlashEntity::TouchMoveY @ 0x17C490
// Per-event Y-axis ingestion. Binary writes pos.y = -(event->m_mapper - 0.5*H)
// (Bada portrait pixel -> Y-up centred). Port's InputTranslatorSDL already
// produces Y-up centred, so no sign flip here.
bool SlashEntity::TouchMoveY(InputEvent* event) {
    Game* g = Game::GetInstance();
    if (g && g->bombHitTimer > 0.0f) return false;
    m_RawTouchPos.y = event->y;
    return true;
}

// STUB: SlashEntity::UpdatePoints(float)
// Binary @ 0x17B92C — per-frame geometry rebuild from binary vertex buffers.
// Port uses RebuildGeometry() from trail-point array instead.
void SlashEntity::UpdatePoints(float /*dt*/) {}

// ASM-spec: SlashEntity::UpdateTouchDown @ 0x17D2E4
// Per-event trail-builder. Binary reads pos.{x,y} (set by TouchMoveX/Y this
// same SDL event burst) and interpolates AddPoint calls along the move
// delta. Port's existing OnTouchActive(x, y) does the same thing -- forward.
void SlashEntity::UpdateTouchDown(InputEvent* /*event*/) {
    // Binary @ 0x17D3AC short-circuits this function when bombHitTimer > 0
    // (the timer is the dual-purpose "input freeze" window written to 0.4f
    // by UnpauseGame and to non-zero by bomb-hit). With no AddPoint calls
    // during the freeze, m_NumPoints stays < 4 and the collision loop in
    // Update naturally short-circuits. Binary-faithful pause-time slice
    // suppression -- no port-specific pausedFlag gate needed.
    Game* g = Game::GetInstance();
    if (g && g->bombHitTimer > 0.0f) return;
    OnTouchActive(m_RawTouchPos.x, m_RawTouchPos.y);
}

// Port-only release handler -- see header comment. Binary detects release
// implicitly (no more TouchDown_n events fire after Bada finger-lift; trail
// ages out via per-frame logic). SDL has explicit FINGERUP/MOUSEBUTTONUP
// which we route via TouchUp_n to this handler.
bool SlashEntity::TouchUp(InputEvent* /*event*/) {
    OnTouchReleased();
    return true;
}

// @ 0x0017e504. Iterates 8 SlashEntityGhost slots (base+0x3c, stride 0x10).
// Render state inherited from GameDraw (alpha-blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA,
// depth-test on, depth-write off -- set at 0x0016ba88-0x0016bb52).
// Tier-2: ghost array not yet ported (only spawned by type-2 slash mods that
// aren't in the default game). No-op until SlashEntityGhost lands.
// for (int i = 0; i < 8; ++i) m_Ghosts[i].Draw();
void SlashEntity::PreDraw() {
}
