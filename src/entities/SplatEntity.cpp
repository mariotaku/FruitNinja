//
// SplatEntity — juice-splat pool, 1:1 binary port.
// See SplatEntity.h for struct layout.
//
// Analysed: 2026-04-29T03:09
//

#include "SplatEntity.h"
#include "ActorManager.h"
#include "FruitInfo.h"
#include "Game.h"
#include "hud/HUD.h"
#include "math/Colour.h"
#include "audio/GameSound.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "render/QUADCUSTOMVERTEX.h"
#include "render/gl_funcs.h"
#include "asset/Texture.h"
#include "asset/TextureManager.h"
#include "util/SmartPtr.h"
#include "util/MemoryPool.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------
// Binary constants (resolved from Ghidra, all addresses below refer to
// the ARM32 .text/.rodata in FruitNinja.exe)
// ---------------------------------------------------------------------

// ASM-verified: 2026-04-29T03:09Z binary @ 0x001bd08c (asm-inspector)
// Per-splat-type slide-rate table. Indexed by m_SplatType (0..5).
static const float kSlideRate[6] = { 2.5f, 2.5f, 2.5f, 2.9f, 0.0f, 0.0f };

// ASM-verified: 2026-04-29T03:09Z binary @ 0x001bd074 (asm-inspector)
// Per-type post-landing scale multiplier. Applied as: m_Scale *= kLandScale[type] * 2.5f.
static const float kLandScale[6] = { 1.6f, 1.6f, 1.6f, 1.6f, 2.9f, 2.9f };

// MakeSplat @ 0x0017f2f0
static const float MS_Z_ZERO          = 0.0f;     // DAT_0017f564 = 0x00000000
static const float MS_Z_BIAS          = 150.0f;   // DAT_0017f568 = 0x43160000
static const float MS_ANGLE_SCALE     = 182.0f;   // DAT_0017f56c = 0x43360000
static const float MS_ANGLE_FLIP_DEG  = 180.0f;   // DAT_0017f570 = 0x43340000
static const float MS_COL_PHASE_DEF   = 0.75f;    //           0x3fc00000
static const float MS_VEL_FINAL_MULT  = 6.0f;
static const float MS_VEL_Y_STRETCH   = 1.5f;
static const float MS_Z_RAND_RANGE    = 10.0f;
static const float MS_AXIS_HALF_SCALE = 0.5f;

static const float MS_SCALE_BASE      = 10.0f;    // sc = Rand(10) + 10
static const float MS_SCALE_RAND      = 10.0f;

static const float MS_LIFE_BASE       = 3.75f;    // Rand(2.5) + 3.75
static const float MS_LIFE_RAND       = 2.5f;
static const float MS_DECAY_BASE      = 0.375f;   // Rand(0.25) + 0.375
static const float MS_DECAY_RAND      = 0.25f;

// Update @ 0x0017f774
static const float UP_LAND_Z          = -50.0f;   // DAT_0017faa8 (landing)
// Verified 2026-04-15 from instruction at 0x0017fa90:
//   vmov.f32 s13, 0xc1200000   ; -10.0f
//   vmla.f32 s15, s13, s14     ; vel.y += -10.0 * dt
// where s14 = *(GameTaskData + 0x38) is the per-frame dt scalar.
// The port uses the SDL frame dt directly here.
static const float UP_GRAVITY         = -10.0f;

static const float UP_VEL_CLAMP_LO    = -50.0f;   // DAT_0017fd40
static const float UP_VEL_CLAMP_HI    =  50.0f;   // DAT_0017fd44
static const float UP_SCALE_CLAMP_LO  = -50.0f;   // DAT_0017fd48
static const float UP_SCALE_CLAMP_HI  = 200.0f;   // DAT_0017fd4c

static const float UP_LIFE_SLIDE_THR  = 1.25f;    // slide-phase threshold

// Per-frame "spring rate" (binary GameTaskData+0x2c). Recomputed in
// UpdateActive each frame from the active entity counts; UpdateSplat
// reads it during the slide-decay phase. See UpdateActive() for the
// full derivation.
static float s_SpringRate = 1.25f;

// Binary: PlaySplat @ 0x0017f5ec — per-impact splat SFX self-arming gate.
// Starts at 0 (BSS). Incremented by 0.5 each time PlaySplat fires;
// ticked down each frame. While positive, PlaySplat is suppressed.
static float s_SplatSfxGate = 0.0f;

// Binary: SplatEntity::Update @ 0x0017fa56 + UpdateActiveSplats @ 0x0017fd68
// Pulp-drip ambient SFX gate.
//   > 0   : armed, counting down to fire edge
//   0..-0.5: cooldown soak (no re-arm allowed)
//   < -0.5 : inert (re-arm allowed)
// BSS-zero-initialised; starts inert (0 is within cooldown soak, but the
// arm condition requires s_PulpDripGate >= -0.5 AND positive arm hasn't
// fired yet — at init 0 the timer block sees it as <= 0 → soak path,
// decays to -0.5 without firing, then becomes inert).
static float s_PulpDripGate = 0.0f;

// DrawActiveSplats @ 0x00180344 — UV atlas table @ 0x001bd014 (6 × 4 floats)
// Each entry is {u0, u1, v0, v1} — verified from raw little-endian dump.
// Parse-order note: the previous comment had this as {u0,v0,u1,v1} which
// gave zero-width quads for the small-cell entries. The true layout
// pairs the U bounds first, then the V bounds.
struct SplatAtlasEntry { float u0, u1, v0, v1; };
static const SplatAtlasEntry SPLAT_ATLAS[6] = {
    { 0.0f, 0.5f, 0.0f,  0.25f },  // type 0: upper-left small cell
    { 0.5f, 1.0f, 0.0f,  0.25f },  // type 1: upper-right small cell
    { 0.0f, 0.5f, 0.25f, 0.5f  },  // type 2: middle-left small cell
    { 0.5f, 1.0f, 0.25f, 0.5f  },  // type 3: middle-right small cell
    { 0.0f, 1.0f, 0.5f,  0.75f },  // type 4: full-width middle row
    { 0.0f, 1.0f, 0.75f, 1.0f  },  // type 5: full-width bottom row
};

// Base colour used as the "pre-fruit" tint during the colour lerp.
// Binary reads `pbVar10` from GOT+0x7A44 (resolved address 0x001F3B74),
// initialized in _GLOBAL__I_Fruit_cpp @ 0x0017a514 as a copy of the
// engine's Blue constant from _GLOBAL__I_Colour_cpp @ 0x0018406a:
//   MakeColour_Engine(ptr, 0, 0, 0xff)  → R=0, G=0, B=255, A=255
// Verified 2026-04-15. The earlier (255,255,255,255) was a placeholder.
static const uint8_t BASE_R = 0;
static const uint8_t BASE_G = 0;
static const uint8_t BASE_B = 255;
static const uint8_t BASE_A = 255;

// ---------------------------------------------------------------------
// Pool + shared content
// ---------------------------------------------------------------------

static Mortar::MemoryPool<SplatEntity> s_Pool;
static SmartPtr<Mortar::Texture>       s_SplatTex;

static const int MAX_SPLATS_PER_FRAME = 128;
static QUADCUSTOMVERTEX s_SplatVerts[MAX_SPLATS_PER_FRAME * 6];

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

// Uniform [0, r]
static float RandRange(float r) {
    return ((float)rand() / (float)RAND_MAX) * r;
}

// Uniform integer [0, n)
static int RandInt(int n) {
    if (n <= 0) return 0;
    return rand() % n;
}

static int ClampInt(int v, int lo, int hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

static float Clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ---------------------------------------------------------------------
// SplatEntity
// ---------------------------------------------------------------------

SplatEntity::SplatEntity()
    : m_ColourPhase(0.0f)
    , m_ColB(255), m_ColG(255), m_ColR(255), m_ColA(255)
    , m_AlphaBase(255.0f)
    , m_Angle(0.0f)
    , m_bParam3(0)
    , m_bSpecial(0)
    , m_AxisA(0, 0, 0)
    , m_AxisB(0, 0, 0)
    , m_bFlipV(0)
    , m_Scale(0, 0, 0)
    , m_Life(0.0f)
    , m_DecayRate(0.0f)
    , m_FruitType(0)
    , m_SplatType(-1)
    , m_bAlive(0)
{
    entityType = 2;
    // SplatEntity runs its own s_Pool lifecycle independent of
    // ActorManager; m_bAlive is the live/dead flag. No ENT_INACTIVE
    // bookkeeping needed here.
}

SplatEntity::~SplatEntity() {}

// Matches SplatEntity::MakeSplat (0x0017f2f0). Every field stored by the
// binary's 131-line init sequence is mirrored here, in order.
void SplatEntity::MakeSplat(const Vec3& p, const Vec3& v, bool param3, int fruitType) {
    m_bParam3 = param3 ? 1 : 0;

    // Colour selection. Binary reads FruitTypeColour(fruitType) when in
    // range, else falls back to a default colour + ColourPhase = 0.75.
    const FruitInfo* info = FruitInfo_Get(fruitType);
    if (info) {
        m_ColourPhase = 0.0f;
        m_ColB = info->m_FruitColour[0];
        m_ColG = info->m_FruitColour[1];
        m_ColR = info->m_FruitColour[2];
        m_ColA = 255;
    } else {
        m_ColourPhase = MS_COL_PHASE_DEF;
        m_ColB = BASE_B;
        m_ColG = BASE_G;
        m_ColR = BASE_R;
        m_ColA = BASE_A;
    }

    m_bFlipV    = (RandInt(2) != 0) ? 1 : 0;
    m_AlphaBase = (float)m_ColA;

    // Position — Z forced to 0.
    pos = p;
    pos.z = MS_Z_ZERO;

    // Velocity transform (binary @ 0x0017f3??):
    //   m_Vel   = vel
    //   speed   = |m_Vel|
    //   m_Vel.y *= 1.5
    //   m_Vel.z = speed * -0.5 - 150.0 - rand(10)
    //   m_Vel  *= 6.0
    vel = v;
    const float speed = vel.Magnitude();
    vel.y *= MS_VEL_Y_STRETCH;
    vel.z = speed * -0.5f - MS_Z_BIAS - RandRange(MS_Z_RAND_RANGE);
    vel = vel * MS_VEL_FINAL_MULT;

    // Angle: uniform [0, 360).
    m_Angle = (float)RandInt(360);

    // bSpecial from FruitInfo.
    m_bSpecial = info ? info->m_bSpecial : 0;

    m_FruitType = fruitType;

    // Scale triple: sc random [10, 20], stored as (sc, -sc, sc).
    const float sc = MS_SCALE_BASE + RandRange(MS_SCALE_RAND);
    m_Scale = Vec3(sc, -sc, sc);

    // Life + decay randomisation (binary tail section @ 0x0017f???).
    m_Life      = MS_LIFE_BASE  + RandRange(MS_LIFE_RAND);
    m_DecayRate = MS_DECAY_BASE + RandRange(MS_DECAY_RAND);

    // Axis vectors. Binary @ 0x0017f1cc: axisA = (cos, sin) * 0.5 and
    // axisB = perp * 0.5 (local_44 = 0x3f000000 = 0.5f).
    // ASM-verified: 2026-04-29T03:09Z binary @ 0x0017f1cc (asm-inspector)
    const float angleRad  = m_Angle * (3.1415926f / 180.0f);
    const float axPerpRad = angleRad + 1.5707963f;  // +90 deg
    m_AxisA = Vec3(cosf(angleRad),  sinf(angleRad),  0.0f) * 0.5f;
    m_AxisB = Vec3(cosf(axPerpRad), sinf(axPerpRad), 0.0f) * 0.5f;

    // Start airborne — Update will pick m_SplatType on landing.
    m_SplatType = -1;
    m_bAlive    = 1;
    flags &= ~ENT_KILLED;
}

// Matches SplatEntity::Update (0x0017f774). Two phases: airborne
// (m_SplatType < 0) does physics + z check; landed (m_SplatType >= 0)
// does slide + decay.
void SplatEntity::UpdateSplat(float dt) {
    if (!m_bAlive) return;

    // --- Physics integration (airborne only) ---
    // Once landed (m_SplatType >= 0) the binary stops integrating pos
    // and only touches m_Scale.y in the slide-decay phase.
    if (m_SplatType < 0) {
        pos = pos + vel * dt;
        vel.y += UP_GRAVITY * dt;

        // Velocity floor clamp — binary uses max(vel, DAT_0017fd40 = -50)
        // to prevent downward velocity runaway.
        if (vel.x < UP_VEL_CLAMP_LO) vel.x = UP_VEL_CLAMP_LO;
        if (vel.y < UP_VEL_CLAMP_LO) vel.y = UP_VEL_CLAMP_LO;
    }

    if (m_SplatType < 0) {
        // --- Airborne phase ---
        // Check landing threshold. Binary: pos.z < -50 → assign type.
        if (pos.z < UP_LAND_Z) {
            // Landing — pick splat variant.
            //   Normal path:
            //     1/4 chance  → type = (Rand(2)==0) ? 2 : 3 (large round)
            //     else (3/4)  → type = (Rand(6)!=0) ? 0 : 1 (small round)
            //   param3 override: 1/2 chance swap to type 4 or 5
            int type;
            if (RandInt(4) == 0) {
                type = (RandInt(2) == 0) ? 2 : 3;
            } else {
                type = (RandInt(6) != 0) ? 0 : 1;
            }
            if (m_bParam3 && RandInt(2) == 0) {
                type = (RandInt(2) == 0) ? 4 : 5;
            }
            m_SplatType = (int8_t)type;

            // Per-type size multiplier — binary at landing branch:
            //   m_Scale *= (kLandScale[type] * 2.5)
            // Table @ 0x001bd074. See kLandScale[] above.
            const int idx = (type >= 0 && type < 6) ? type : 0;
            m_Scale = m_Scale * (kLandScale[idx] * 2.5f);

            // Stick to the "background plane" — splat freezes in place.
            // Binary's landed Update doesn't integrate pos, so we zero
            // vel completely. Slide-decay phase (life <= 1.25) only
            // touches m_Scale.y, not pos.
            pos.z = UP_LAND_Z;
            vel = Vec3(0.0f, 0.0f, 0.0f);

            // Per-impact splat SFX.
            // ASM-verified: 2026-04-29T03:25Z binary @ 0x0017f5ec (asm-inspector)
            PlaySplat();

            // Per-splat ambient pulp-drip arm: 1-in-10 chance, only if the
            // gate isn't currently in its post-fire cooldown soak (>= -0.5).
            // Bin: SplatEntity::Update @ 0x0017f774, arm site @ 0x0017fa56.
            // ASM-verified: 2026-04-29T03:25Z binary @ 0x0017fa56 (asm-inspector)
            if (RandInt(10) == 0 && s_PulpDripGate >= -0.5f) {
                s_PulpDripGate = 0.25f;
            }
        }
        return;
    }

    // --- Landed phase ---
    // Slide / scale decay — binary runs this only while m_Life <= 1.25
    // (the tail slide phase). Above that threshold the splat sits still
    // on the plane. Rate is per-splat-type from kSlideRate[] @ 0x001bd08c.
    // SSMP horizontal-gravity branch (field_0x74) is omitted — port has no SSMP.
    if (m_Life <= UP_LIFE_SLIDE_THR) {
        const int slideIdx = (m_SplatType >= 0 && m_SplatType < 6) ? (int)m_SplatType : 0;
        const float dy = dt * kSlideRate[slideIdx];
        vel.y     = Clampf(vel.y     - dy, UP_VEL_CLAMP_LO,   UP_VEL_CLAMP_HI);
        m_Scale.y = Clampf(m_Scale.y - dy, UP_SCALE_CLAMP_LO, UP_SCALE_CLAMP_HI);
    }

    // Life decay.
    m_Life -= dt * m_DecayRate;
    if (m_Life <= 0.0f) {
        m_Life = 0.0f;
        m_bAlive = 0;
        return;
    }

    // Alpha = min(base, base * life). Binary clamp to uint8.
    const float rawAlpha = m_AlphaBase * m_Life;
    const float aF = rawAlpha < m_AlphaBase ? rawAlpha : m_AlphaBase;
    m_ColA = (uint8_t)Clampf(aF, 0.0f, 255.0f);

    // Colour lerp — binary @ 0x0017f774 ticks m_ColourPhase down each
    // Update and writes the resulting RGBA back into m_ColR/G/B/A.
    // DrawSplat reads the stored fields directly (no re-computation).
    if (m_ColourPhase > 0.0f) {
        m_ColourPhase -= dt;
        if (m_ColourPhase < 0.0f) m_ColourPhase = 0.0f;
    }
    {
        float fade = m_ColourPhase * 2.0f;
        if (fade < 0.0f) fade = 0.0f;
        if (fade > 1.0f) fade = 1.0f;
        const int rMix = (int)m_ColR + (int)(((int)BASE_R - (int)m_ColR) * fade);
        const int gMix = (int)m_ColG + (int)(((int)BASE_G - (int)m_ColG) * fade);
        const int bMix = (int)m_ColB + (int)(((int)BASE_B - (int)m_ColB) * fade);
        m_ColR = (uint8_t)ClampInt(rMix, 0, 255);
        m_ColG = (uint8_t)ClampInt(gMix, 0, 255);
        m_ColB = (uint8_t)ClampInt(bMix, 0, 255);
    }
}

// ---------------------------------------------------------------------
// Binary: PlaySplat @ 0x0017f5ec
// Plays one of 6 splat impact sound variants (chosen via RandInt(6)).
// Self-arming gate: s_SplatSfxGate is incremented by 0.5 on each fire;
// while it is positive the function returns immediately so consecutive
// landings within the gate window don't double-trigger.
// ASM-verified: 2026-04-29T03:25Z binary @ 0x0017f5ec (asm-inspector)
// DIFFERS: exact sound names not documented by re-analyst. Using
// "Splat-1".."Splat-6" as placeholders. Update when names are resolved.
// ---------------------------------------------------------------------
void SplatEntity::PlaySplat() {
    if (s_SplatSfxGate > 0.0f) return;

    static const char* kSplatSfx[6] = {
        "Splat-1", "Splat-2", "Splat-3",
        "Splat-4", "Splat-5", "Splat-6",
    };
    const int idx = RandInt(6);

    Game* game = Game::GetInstance();
    if (game && game->pGameSound) {
        game->pGameSound->SFXPlay(kSplatSfx[idx], 1.0f, 1.0f);
    }

    s_SplatSfxGate += 0.5f;
}

// ---------------------------------------------------------------------
// Pool / static ops
// ---------------------------------------------------------------------

void SplatEntity::CreatePool(int capacity) {
    s_Pool.Create(capacity);
    printf("[SplatEntity] CreatePool: capacity=%d\n", capacity);
}

void SplatEntity::DestroyPool() {
    s_Pool.Destroy();
    s_SplatTex.SetNull();
}

void SplatEntity::LoadContent() {
    if (!s_SplatTex.IsValid()) {
        s_SplatTex = Mortar::TextureManager::LoadLocalisedTexture("white_splash.tex");
        printf("[SplatEntity] LoadContent: white_splash.tex valid=%d\n",
               s_SplatTex.IsValid());
    }
}

void SplatEntity::CleanUp() {
    s_SplatTex.SetNull();
}

SplatEntity* SplatEntity::GetFree() {
    SplatEntity* s = s_Pool.Pop();
    if (!s) return nullptr;
    s->m_bAlive = 0;
    return s;
}

// Binary: SplatEntity::NumActiveSplats @ 0x0017ee34
// Counts pool slots with m_bAlive != 0. Static, reads GOT-relative pool
// globals; the `this` pointer seen in Ghidra's __thiscall annotation is
// ignored by the implementation.
int SplatEntity::NumActiveSplats() {
    const int N = s_Pool.Capacity();
    int activeCount = 0;
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (s && s->m_bAlive) ++activeCount;
    }
    return activeCount;
}

void SplatEntity::UpdateActiveSplats(float dt) {
    // Per-frame spring rate compute — matches binary's
    // SplatEntity::UpdateActiveSplats @ 0x0017fd68 (instructions
    // 0x0017fe46..0x0017feda):
    //
    //   N_total  = ActorManager::GetNumEntities()
    //   N_active = NumActiveSplats()
    //   raw      = (N_total + N_active) / 15.0 - 0.15
    //   if raw <= 0:   spring = 1.25
    //   elif raw >= 3: spring = 4.25
    //   else:          spring = raw + 1.25  (linear ramp 1.25..4.25)
    //
    // The binary additionally multiplies by 1.5 if !IsFastHardware()
    // OR (multiplayer && game->field_0x170 == 0). Port skips the slow-
    // hardware branch (always treats as fast hw) and the multiplayer
    // gate (no MP support yet).
    const int activeCount = NumActiveSplats();
    int totalEntities = 0;
    if (ActorManager* am = ActorManager::GetInstance()) {
        totalEntities = am->GetNumEntities();
    }
    {
        const float raw = (float)(totalEntities + activeCount) / 15.0f - 0.15f;
        if (raw <= 0.0f)      s_SpringRate = 1.25f;
        else if (raw >= 3.0f) s_SpringRate = 4.25f;
        else                  s_SpringRate = raw + 1.25f;
    }

    const int N = s_Pool.Capacity();
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive) continue;

        s->UpdateSplat(dt);

        if (!s->m_bAlive) {
            s_Pool.Push(s);
        }
    }

    // Per-impact splat SFX gate tick — allows PlaySplat to fire again
    // once the 0.5 s suppression window drains to zero.
    if (s_SplatSfxGate > 0.0f) {
        s_SplatSfxGate -= dt;
        if (s_SplatSfxGate < 0.0f) s_SplatSfxGate = 0.0f;
    }

    // Pulp-drip ambient SFX gate tick + fire on positive->non-positive edge.
    // Bin: 0x0017fd68 timer block.
    // ASM-verified: 2026-04-29T03:25Z binary @ 0x0017fd68 (asm-inspector)
    if (s_PulpDripGate <= 0.0f) {
        if (s_PulpDripGate >= -0.5f) s_PulpDripGate -= dt;
    } else {
        s_PulpDripGate -= dt;
        if (s_PulpDripGate <= 0.0f) {
            const char* name = (RandInt(2) == 0) ? "Pulp-drip-2" : "Pulp-drip-1";
            Game* game = Game::GetInstance();
            if (game && game->pGameSound) {
                game->pGameSound->SFXPlay(name, 1.0f, 1.0f);
            }
        }
    }
}

void SplatEntity::RemoveAllSplats() {
    const int N = s_Pool.Capacity();
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive) continue;
        s->m_bAlive = 0;
        s_Pool.Push(s);
    }
}

void SplatEntity::ForEachInPool(PoolVisitor fn, void* user) {
    if (!fn) return;
    const int N = s_Pool.Capacity();
    for (int i = 0; i < N; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (s) fn(s, user);
    }
}

// ---------------------------------------------------------------------
// SplatEntity::DrawSplat (0x0017f008) — virtual per-instance render
// ---------------------------------------------------------------------
// Writes 6 QUADCUSTOMVERTEX entries for this splat into the
// caller-provided buffer (outVerts must point to at least 6 slots).
// Called indirectly via vtable from DrawActiveSplats.
void SplatEntity::DrawSplat(QUADCUSTOMVERTEX* outVerts, const float tintRGB[3]) {
    // Quad corner construction — matches binary DrawSplat (0x0017f008).
    // The four corners are built from the sum/diff of the two axis
    // vectors, scaled by m_Scale.x for X and m_Scale.y for Y. Note
    // that m_Scale.y is the negative `-sc` from the spawn triple,
    // which the binary keeps negative on purpose (it's the value the
    // slide-decay phase mutates).
    //
    //   v0 (TL): pos + (+ax + bx) * scX, (+ay + by) * scY
    //   v1 (TR): pos + (-ax + bx) * scX, (-ay + by) * scY
    //   v2 (BL): pos + (+ax - bx) * scX, (+ay - by) * scY
    //   v5 (BR): pos + (-ax - bx) * scX, (-ay - by) * scY
    const float scX = m_Scale.x;
    const float scY = m_Scale.y;
    const float ax = m_AxisA.x, ay = m_AxisA.y;
    const float bx = m_AxisB.x, by = m_AxisB.y;

    // Colour pre-computed in UpdateSplat each frame (binary @ 0x0017f774).
    // Apply per-channel tint from &pHUD->scales[3] — world tint window.
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0017f1ec (asm-inspector)
    Colour splatColour(m_ColR, m_ColG, m_ColB, m_ColA);
    Colour tinted = Colour::TintColour(splatColour, tintRGB);
    const uint32_t col =
        ((uint32_t)tinted.a << 24) |
        ((uint32_t)tinted.b << 16) |
        ((uint32_t)tinted.g <<  8) |
        ((uint32_t)tinted.r);

    // Atlas UV — verified via SplatEntity::DrawSplat @ 0x0017f008.
    // The binary halves the table-stored U coords (`* 0.5`) before
    // applying them, then optionally shifts both by +0.5 when
    // m_bSpecial (binary field_0x19) is set — selecting the right
    // half of the texture. Without the halving the quad samples
    // twice as wide as the actual splat sprite cell and ends up
    // mostly transparent / invisible.
    //
    //   u0 = table.u_start * 0.5
    //   u1 = table.u_end   * 0.5
    //   if (m_bSpecial)  { u0 += 0.5; u1 += 0.5; }
    //   if (m_bFlipV)    swap(v0, v1);
    const int type = ClampInt(m_SplatType, 0, 5);
    const SplatAtlasEntry& e = SPLAT_ATLAS[type];
    float u0 = e.u0 * 0.5f;
    float u1 = e.u1 * 0.5f;
    if (m_bSpecial) { u0 += 0.5f; u1 += 0.5f; }
    float v0 = e.v0, v1 = e.v1;
    if (m_bFlipV) { float t = v0; v0 = v1; v1 = t; }

    QUADCUSTOMVERTEX* v = outVerts;
    const float px = pos.x, py = pos.y, pz = pos.z;

    // Vertex layout matches binary DrawSplat (vertices 0..5 written in
    // order, then v3=v2 and v4=v1 via memcpy):
    //   v[0] = TL
    //   v[1] = TR
    //   v[2] = BL
    //   v[3] = v[2]
    //   v[4] = v[1]
    //   v[5] = BR
    v[0].x = px + ( ax + bx) * scX;
    v[0].y = py + ( ay + by) * scY;
    v[0].z = pz;
    v[0].u = u0; v[0].v = v0;

    v[1].x = px + (-ax + bx) * scX;
    v[1].y = py + (-ay + by) * scY;
    v[1].z = pz;
    v[1].u = u1; v[1].v = v0;

    v[2].x = px + ( ax - bx) * scX;
    v[2].y = py + ( ay - by) * scY;
    v[2].z = pz;
    v[2].u = u0; v[2].v = v1;

    v[3] = v[2];
    v[4] = v[1];

    v[5].x = px + (-ax - bx) * scX;
    v[5].y = py + (-ay - by) * scY;
    v[5].z = pz;
    v[5].u = u1; v[5].v = v1;

    for (int k = 0; k < 6; ++k) {
        v[k].nx = 0.0f;
        v[k].ny = 0.0f;
        v[k].nz = 1.0f;
        v[k].colour = col;
    }
}

// Binary: SplatEntity::DrawUpdate @ 0x0017ee2c (virtual no-op; single bx lr)
void SplatEntity::DrawUpdate(float /*dt*/) {}

// ---------------------------------------------------------------------
// Batched draw — matches DrawActiveSplats (0x00180344)
// ---------------------------------------------------------------------
//
// For each alive splat with m_SplatType >= 0, calls DrawSplat() to
// write 6 vertices, then submits the batch.
void SplatEntity::DrawActiveSplats() {
    if (!s_SplatTex.IsValid()) return;

    // Fetch world tint from HUD::scales[3..5].
    // Binary @ 0x0017f1ec passes &pHUD->scales[3] to DrawSplat.
    // ASM-verified: 2026-04-29T03:29Z binary @ 0x0017f1ec (asm-inspector)
    const float* worldTint = Colour::IdentityTint();
    if (Game* game = Game::GetInstance()) {
        if (game->hud) {
            worldTint = &game->hud->scales[3];
        }
    }

    const int N = s_Pool.Capacity();
    int count = 0;

    for (int i = 0; i < N && count < MAX_SPLATS_PER_FRAME; ++i) {
        SplatEntity* s = s_Pool.SlotAt(i);
        if (!s || !s->m_bAlive)     continue;
        if (s->m_SplatType < 0)     continue;  // still airborne

        s->DrawSplat(&s_SplatVerts[count * 6], worldTint);

        ++count;
    }

    if (count == 0) return;

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.UploadModelViewOnly();

    s_SplatTex->Set();

    // Read-only depth test — matches binary's SetDepthBufferWrite(0)
    // call BEFORE this draw (after ActorManager::Draw). Splats render
    // on the background plane (z=-50) but should be culled by any 3D
    // fruit/bomb pixel currently in front. The port's Fruit::Draw
    // explicitly disables depth test on exit, so we re-enable read-only
    // here so the cleared/fruit depth values still drive z-rejection.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_FALSE);

    if (Renderer* r = Renderer::GetInstance()) {
        r->DrawTriList(s_SplatVerts, count * 6);
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    s_SplatTex->UnSet();
}
