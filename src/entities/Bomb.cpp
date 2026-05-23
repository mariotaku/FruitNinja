#include "Bomb.h"
#include "game/GameMode.h"
#include "ActorManager.h"
#include "BombBlast.h"
#include "FruitInfo.h"
#include "Game.h"
#include "audio/GameSound.h"
#include "game/BombHit.h"
#include "game/GameOver.h"
#include "game/FruitCamera.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "game/PowerUpManager.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "asset/TextureManager.h"
#include "asset/MeshManager.h"
#include "asset/Mesh.h"
#include "hud/MenuButton.h"
#include "hud/MissControl.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include "debug/Logger.h"
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <string>
#include "game/GameWork.h"
#include "game/GameTaskState.h"

// Analysed: 2026-04-29T00:00

// Per-bomb physics trace for the test_bomb_spawn diagnostic. Off by
// default; enable with -DFRUITNINJA_BOMB_TRACE=ON at cmake config time.
#ifndef MORTAR_BOMB_TRACE
#  define MORTAR_BOMB_TRACE 0
#endif

// --- Constants from binary (docs/entities/bomb.md) ---
static const float SPAWN_TIMER_INIT  = 0.6f;     // DAT_001726ac
static const float DEFAULT_CHUCK_DELAY = 0.2f;    // DAT_00170f80
static const float GRAVITY_Y          = -12.0f;   // literal in Init
static const float DT_NORMALIZE       = 1.0f / 60.0f; // DAT_00172c98
static const float ACCEL_GROWTH_RATE  = 0.2f;     // DAT_00172f30
static const float OFFSCREEN_Y        = -320.0f;  // DAT_00172cb0
static const float BOUNDS_MIN_Y       = -240.0f;  // DAT_00172f34
static const float BOUNDS_MAX_Y       =  240.0f;  // DAT_00172f38
static const float BOUNDS_MIN_X       = -360.0f;  // DAT_00172f3c
static const float BOUNDS_MAX_X       =  360.0f;  // DAT_00172f40
static const float BOMBBLAST_INTERVAL = 0.05f;    // DAT_00172c9c
static const float HIT_COL_RADIUS     = 0.01f;    // DAT_00172cac
static const float HIT_COL_POS        = 1000.0f;  // DAT_00172ca4

// Fixed tilt for draw: 0xBFF4 in 16-bit angle ≈ -83 degrees
static const int16_t DRAW_TILT_ANGLE  = (int16_t)0xBFF4;
// 0xB6 = 182 = ~1 degree in 16-bit (65536/360 ≈ 182)
static const int16_t ANGLE_SCALE      = 0xB6;

// Global bomb data (matches BombGlobalData at GOT+0x464A0, loaded by LoadContent)
// See docs/entities/bomb.md for full struct layout.
// Binary field offsets (kept as comments for reference; port layout may
// differ since we store Mortar::SmartPtr<Texture> separately in g_BombTexture):
//   +0x00  Bomb*            pTrackedBomb
//   +0x04  Mortar::SmartPtr<Texture> tex_02   (bomb_explode.tex — in g_BombTexture)
//   +0x08  uint8_t           bFuseSfxFiredThisFrame
//   +0x0C  Mortar::SmartPtr<Model>[3] model
//   +0x24  Mortar::SmartPtr<Texture> texMinus10
//   +0x28  bool              loaded
//   +0x2C  uint32_t          fuseHash[2]
struct BombGlobalData {
    // +0x00: last qualifying bomb drawn this frame. Bomb::Draw sets it
    // if (this != prev && !menuHit && pos.y > -1000). Write-only in the
    // shipped binary — no visible reader. Preserved for byte-level fidelity.
    Bomb* pTrackedBomb;

    // +0x08: per-frame gate for "Bomb-Fuse" SFX. Cleared at top of every
    // Bomb::Draw; set by Bomb::Update on the frame a bomb's countdown
    // crosses 0.2s downward. Prevents every chained bomb from spamming the
    // SFX in one frame.
    uint8_t bFuseSfxFiredThisFrame;

    Mortar::SmartPtr<Mortar::Model> model[3];
    Mortar::SmartPtr<Mortar::Texture> texMinus10;
    bool loaded;
    uint32_t fuseHash[2];

    BombGlobalData()
        : pTrackedBomb(nullptr), bFuseSfxFiredThisFrame(0), loaded(false) {
        fuseHash[0] = fuseHash[1] = 0;
    }
};
static BombGlobalData g_bombData;

// Global blast / flash texture. Binary stores at `g_bombData->tex_02`
// (+0x04). Despite the name, this is NOT the bomb mesh texture (that
// comes from the .mmd embedded `fruit_atlas.tex`) — it's the
// `bomb_explode.tex` used by BombBlast::DrawBlast for the shockwave
// quads. BombBlast.cpp references it via `extern`.
Mortar::SmartPtr<Mortar::Texture> g_BombTexture;

// Global bomb Z cycling (matches GetBombZPosition at 0x169080)
static float g_BombZCurrent = -10.0f;

static float GetBombZPosition() {
    float z = g_BombZCurrent - 50.0f;  // DAT_001690bc
    if (z < -400.0f)                    // DAT_001690c0
        z = -10.0f;
    g_BombZCurrent = z;
    return z;
}

// SetupLighting @ 0x00175018 — single `bx lr`, a genuine no-op stub in
// the shipped binary. Both Bomb::LoadContent (0x001727d8) and
// Fruit::LoadFruitModels (0x00179654/0x00179718/0x001797d6) reach it
// via PLT trampoline 0x000fb820 → GOT[0x1eecb0] → 0x00175018. No
// material / mesh / GL state is touched.
//
// We keep the call in LoadContent for shape-fidelity — when (if) Mortar
// lighting is revisited, the implementation lands here for free.
static Mortar::SmartPtr<Mortar::Model>& SetupLighting(Mortar::SmartPtr<Mortar::Model>& model) {
    return model;
}

// --- Bomb::LoadContent (0x1726c8) — called once from GameInitialise ---

void Bomb::LoadContent() {
    if (g_bombData.loaded) return;  // +0x28 guard

    Game* game = Game::GetInstance();
    if (!game) return;

    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    if (!meshMgr) return;

    // Model[0]: binary string "models/Fruit/Bomb.mmd" (0x1BCBDB)
    // Bada filesystem was case-insensitive; actual file is lowercase.
    // NOTE: this Bada asset dump's fruit_atlas.tex has a solid red
    // oval at bomb.mmd's UV region (lower-left quadrant), not the
    // dark-body + red-X art seen in iOS/Steam ports. Rendering as
    // red is faithful to the shipped Bada atlas. The other available
    // mesh files (bomb_blue.mmd, bomb_red.mmd, bomb_purple.mmd) all
    // sample different (blue/magenta/etc.) atlas regions in this
    // asset dump -- not the dark-red variant. To restore the
    // iconic dark-body bomb, the atlas texture itself needs to be
    // replaced with a version that has the correct art at these UVs.
    {
        // logical path; FileSystem_Direct prepends data_dir
        g_bombData.model[0] = meshMgr->Load("models/Fruit/bomb.mmd");
    }

    // Model[1]: binary string "models/Fruit/Bomb_purple.mmd" (0x1BCBF1)
    {
        // logical path; FileSystem_Direct prepends data_dir
        g_bombData.model[1] = meshMgr->Load("models/Fruit/bomb_purple.mmd");
    }

    // Original LoadContent (0x001726e8) does NOT assign textures to bomb meshes.
    // Textures are loaded from the .mmd file's embedded texture reference
    // (fruit_atlas.tex) by MeshManager::LoadMeshInternal automatically.

    // Model[2]: not loaded in LoadContent (may be loaded elsewhere for multiplayer)

    // Texture: "minus_10.tex" (0x1BCC0E) — zen mode -10 score indicator
    g_bombData.texMinus10 = Mortar::TextureManager::LoadLocalisedTexture("minus_10.tex");

    // Precompute fuse particle emitter hashes (matches binary LoadContent)
    g_bombData.fuseHash[0] = StringHash("bomb_smoke");
    g_bombData.fuseHash[1] = StringHash("purple_bomb_smoke");

    // Setup lighting on both models (binary 0x001727c4..0x001727e0). The
    // loop guards on SmartPtr::operator bool before each call — matches
    // the disassembly byte-for-byte, and the SetupLighting body is a
    // `bx lr` so the whole block is effectively a null-safe walk over
    // the two SmartPtr slots.
    for (int i = 0; i < 2; i++) {
        if (g_bombData.model[i].IsValid())
            SetupLighting(g_bombData.model[i]);
    }

    g_bombData.loaded = true;
}

// --- Bomb implementation ---

Bomb::Bomb()
    : m_SpawnTimer(0.0f),
      m_BombVariant(0),
      m_bHit(0),
      m_ZPosition(0.0f),
      m_RotVelX(0), m_RotVelY(0),
      m_RotX(0), m_RotY(0),
#ifndef __bada__
      m_RotAccumX(0.0f), m_RotAccumY(0.0f),
#endif
      m_bCollisionGuard(0),
      m_pEmitter(nullptr),
      m_bMovement(0),
      m_pOwnerButton(nullptr),
      m_bMenuBombHit(0),
      m_Countdown(0.0f),
      m_SpeedMult(1.0f)
{
    entityType = 1;  // Bomb
}

Bomb::~Bomb() {
    Release();
    delete m_Col;
    m_Col = nullptr;
}

// Matches Bomb::Release (0x171764) — drops fuse emitter; called from dtor.
// ASM-verified: 2026-04-29T00:00Z binary @ 0x00171764 (asm-inspector)
void Bomb::Release() {
    if (m_pEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter);
        m_pEmitter = nullptr;
    }
}

// ASM-verified: 2026-04-28T00:00 binary @ 0x00172504 (asm-inspector)
// Binary @ 0x00172504 — vtable slot 2. p1/p2 unused; p3=scale (nullable, default 1.0).
void Bomb::Init(void* /*p1*/, long /*p2*/, Vec3* /*scaleOrNull*/) {

    float scaleFactor = 1.0f;
    // Original: if (p3 != nullptr) scaleFactor = *(float*)p3;

    // Lazy-load bomb texture
    if (!g_BombTexture.IsValid()) {
        g_BombTexture = Mortar::TextureManager::LoadLocalisedTexture("bomb_explode.tex");
    }

    // Initial state (matches binary exactly)
    m_SpawnTimer = SPAWN_TIMER_INIT;
    m_BombVariant = 0;
    m_bCollisionGuard = 0;
    m_bHit = 0;
    flags = (flags & ~0x10) | 0x02;  // clear killed, set has-collision
    m_bMovement = 1;
    m_SpeedMult = 1.0f;

    // Random rotation for 2 axes (matches binary loop)
    m_RotVelX = (int16_t)(rand() % 7 + 1);  // 1..8
    m_RotX    = (int16_t)(rand() % 360);     // 0..359
    m_RotVelY = (int16_t)(rand() % 7 + 1);
    m_RotY    = (int16_t)(rand() % 360);
#ifndef __bada__
    m_RotAccumX = 0.0f;
    m_RotAccumY = 0.0f;
#endif

    m_bMenuBombHit = 0;
    m_pOwnerButton = nullptr;
    m_pEmitter = nullptr;  // lazy-created in Update

    // Scale + collision sphere: matches binary multiply chain at 0x172504.
    // Binary reads <bomb size="..." collision="..."/> from fruitlist.xml
    // into g_pFruitInfo+0x88/+0x8C and uses both here. The port parses
    // those into FruitInfo_GetBombSize/Collision during FruitInfo_Load.
    //   scale  = Vec3::One * size * 0.01 * scaleFactor
    //   radius = collision * 0.5 * scaleFactor
    const float bombSize = FruitInfo_GetBombSize();
    const float bombCol  = FruitInfo_GetBombCollision();
    static const float VISUAL_SCALE_MULT = 0.01f; // DAT_001726b0
    Vec3 computedScale = Vec3::One() * (bombSize * VISUAL_SCALE_MULT * scaleFactor);

    // Binary allocates ColSphere at +0x38 in Init if the pointer is null
    // (verified: "allocated at +0x38 if NULL" in bomb_init_binary.s).
    if (!m_Col) m_Col = new ColSphere();
    {
        ColSphere* cs = static_cast<ColSphere*>(m_Col);
        cs->center = Vec3(pos.x, pos.y, 0.0f);
        cs->radius = bombCol * 0.5f * scaleFactor;
    }
    m_Countdown = 0.0f;
    scale = computedScale;
    m_OrigScale = computedScale;
    m_AccelForce = Vec3(0.0f, GRAVITY_Y, 0.0f);
    m_ZPosition = GetBombZPosition();

    // Activate — same intent as the binary's flag clear at Init start.
    flags &= ~ENT_SKIP_MASK;

    // Use pre-loaded model from g_bombData (loaded by LoadContent in GameInitialise)
    // Draw indexes as g_bombData.model[m_BombVariant]; no per-instance mesh load.
}

// ASM-verified: 2026-04-28T00:00 binary @ 0x0017121c (asm-inspector)
// Matches Bomb::SetCallback (0x0017121c) — sets the menu-bomb marker flag,
// installs the hit callback, and overwrites rotation fields so menu bombs
// spin slowly (one axis moving, one locked) rather than with the random
// 1..7 velocities from Bomb::Init.
void Bomb::SetCallback(Mortar::Delegate0<void> cb, MenuButton* button) {
    m_bMenuBombHit   = 1;
    m_HitCallback    = cb;
    m_pOwnerButton   = button;
    m_RotY    = 0x2d;   // DAT_0017121c: 45 deg initial Y angle
    m_RotVelX = 2;      // slow spin on X
    m_RotX    = 0;
    m_RotVelY = 0;      // Y axis locked
}

// Binary: rot += vel once per frame. Port adds a fractional accumulator so
// the F7 debug timescale slows rotation proportionally. Under __bada__ the
// accumulator fields are absent; the binary-faithful path is used instead.
#ifndef __bada__
static inline void StepBombRotation(int16_t& rot, int16_t vel,
                                    float& accum, float scaledDt) {
    if (scaledDt <= 0.0f) return;
    accum += (float)vel * scaledDt * 60.0f;  // 60 = 1/dt at normal speed
    int whole = (int)accum;
    if (whole != 0) {
        rot   = (int16_t)(rot + whole);
        accum -= (float)whole;
    }
}
#else
static inline void StepBombRotation(int16_t& rot, int16_t vel) {
    rot = (int16_t)(rot + vel);
}
#endif

// Helper: accel-growth block shared by alive-branch and menu-hit-branch in
// binary Bomb::Update. When velocity and accelForce are componentwise
// aligned, the accel-force magnitude grows by (0.2 * dtNorm * 2) per frame.
//   DAT_00172f30 = 0.2   (ACCEL_GROWTH_RATE)
//   DAT_00172ca0 = 0.2   (same value used in menu-hit branch — coincidental)
static inline void AccelGrowth(Vec3& vel, Vec3& accel, float dtNorm) {
    const bool alignedY = (accel.y < 0.0f && vel.y < 0.0f) ||
                          (accel.y > 0.0f && vel.y > 0.0f);
    const bool alignedX = (accel.x < 0.0f && vel.x < 0.0f) ||
                          (accel.x > 0.0f && vel.x > 0.0f);
    if (!alignedY && !alignedX) return;
    float len = sqrtf(accel.x * accel.x + accel.y * accel.y + accel.z * accel.z);
    if (len <= 0.001f) return;
    float newLen = len + ACCEL_GROWTH_RATE * dtNorm * 2.0f;
    accel *= (newLen / len);
}

// ASM-verified: 2026-04-28T00:00 binary @ 0x001729fc (asm-inspector)
// ASM-verified: 2026-05-20 binary @ Bomb::Update (re-analyst) -- no IsActive early-return; tail OOB-kill must always fire.
// Matches Bomb::Update (0x001729fc, 195 lines).
void Bomb::Update(float /*dt*/) {
    Game* game = Game::GetInstance();
    if (!game) return;

    const float gameDt  = game_work.dt;
    const float scaledDt = gameDt * m_SpeedMult;
    const float dtNorm   = (DT_NORMALIZE > 0.0f) ? scaledDt / DT_NORMALIZE : 1.0f;

    if (m_bHit == 0) {
        // === ALIVE BRANCH ===
        if (m_Countdown > 0.0f) {
            // Early-kill: if a bomb just exploded (bombHitTimer>0) or game
            // is transitioning out (levelTransitionFlag!=0), force this bomb off-screen
            // so it expires on the OOB check below. Binary resets countdown
            // to 0 (DAT_00172f28) and pos.y to -320 (DAT_00172cb0).
            if (game_work.m_BombHitTimer > 0.0f || game_work.m_LevelTransitionFlag != 0) {
                m_Countdown = 0.0f;
                pos.y = OFFSCREEN_Y;
                vel = Vec3(HIT_COL_POS, -1.0f, HIT_COL_POS);
            }

            const float prevCountdown = m_Countdown;
            // Tick countdown using GAME dt (not entity scaledDt) — but only
            // when game is active (!pausedFlag).
            if (!game_work.m_Paused) {
                m_Countdown -= gameDt;
            }

            // DIFFERS (port-side simplification): the binary @ 0x00172bd8
            // fires a per-bomb "Bomb-Fuse" SFXPlay here (gated by
            // bFuseSfxFiredThisFrame on Bomb global +0x08). The Bomb-Fuse
            // wav loops indefinitely (loopStart=12736 in the .wav.pcm hdr)
            // and the binary never explicitly stops these per-bomb slots --
            // they accumulate over time as silent-when-no-bomb but the
            // SetVolume-based mute lives in GameUpdate (see GameInit.cpp's
            // Bomb-Fuse block). Port omits this redundant call because we
            // don't currently slot-age stale looping SFX; the GameUpdate
            // channel is the dominant audible fuse hiss anyway. Removing
            // this fixes the user-reported "fuse hiss doesn't stop after
            // explosion" by ensuring the only Bomb-Fuse handle is the one
            // GameUpdate volume-modulates to 0 when no bombs are present.
            static const float FUSE_SFX_THRESHOLD = 0.2f;  // DAT_00172ca0
            (void)prevCountdown; (void)FUSE_SFX_THRESHOLD;

            if (m_Countdown > 0.0f) return;

            // Countdown expired — chain-bomb spawning.
            // Binary: iVar7 = (int)WaveManager::spawnLevel, with a random
            // ceil based on the fractional part (rand100 < frac*100 -> +1).
            //   if (iVar7 < 1): countdown = 0; pos.y = -320; vel = (0,-1,0);
            //   else if (iVar7 != 1): WaveManager::SpawnBomb(iVar7 - 1, 0, nullptr, ...);
            // WaveManager::SpawnBomb is currently a stub (no-op), so chain
            // bombs produce no visible effect yet — the control flow is
            // wired so it lights up for free once spawning is ported.
            {
                WaveManager* wm = WaveManager::GetInstance();
                const float sl = wm->spawnLevel;
                int iVar7 = (int)sl;
                const float frac = sl - (float)iVar7;
                const int rand100 = rand() % 100;
                if ((float)rand100 < frac * 100.0f) iVar7++;
                if (iVar7 < 1) {
                    m_Countdown = 0.0f;
                    pos.y = OFFSCREEN_Y;
                    vel = Vec3(0.0f, -1.0f, 0.0f);
                } else if (iVar7 != 1) {
                    wm->SpawnBomb(iVar7 - 1, 0, nullptr, 0);
                }
            }
        }

        // Physics — always runs in alive branch after countdown check.
        // Binary Bomb::Update (0x001729fc): velocity uses scaledDt, but
        // POSITION uses dtNorm (= scaledDt / DT_NORMALIZE = scaledDt * 60).
        // The ×60 factor is the binary's pos-integration tuning fudge,
        // identical to Fruit::Update's DAT_00177d00. Without it bombs
        // drift roughly 1/60th the expected speed.
        if (m_bMovement) {
            vel += m_AccelForce * scaledDt;
            AccelGrowth(vel, m_AccelForce, dtNorm);
        }
        pos += vel * dtNorm;
#ifndef __bada__
        StepBombRotation(m_RotX, m_RotVelX, m_RotAccumX, scaledDt);
        StepBombRotation(m_RotY, m_RotVelY, m_RotAccumY, scaledDt);
#else
        StepBombRotation(m_RotX, m_RotVelX);
        StepBombRotation(m_RotY, m_RotVelY);
#endif

#if MORTAR_BOMB_TRACE
        // Skip still bombs (menu / pre-throw): vel exactly 0 means the
        // entity isn't moving this frame, so the trace would be the same
        // pos/vel/accel line spamming every tick. Once physics imparts
        // any velocity (slice, gravity integration, chuck-launch) the
        // trace lights up.
        if (vel.x != 0.0f || vel.y != 0.0f) {
            unsigned id = (unsigned)((uintptr_t)this >> 4) & 0xfff;
            LOG_VERBOSE("BOMB", "%03x pos=(%6.1f,%6.1f) vel=(%6.2f,%6.2f) "
                   "accel=(%5.2f,%6.2f) scl.y=%.3f bMv=%d bHit=%d "
                   "cd=%.3f dt=%.4f",
                   id, pos.x, pos.y, vel.x, vel.y,
                   m_AccelForce.x, m_AccelForce.y,
                   scale.y,
                   (int)m_bMovement, (int)m_bHit,
                   m_Countdown, gameDt);
        }
#endif

        // Update collision sphere to follow bomb. Binary writes pos.xyz then
        // immediately overwrites center.z with DAT_00172f28=0.0 — effectively
        // center = (pos.x, pos.y, 0).
        if (m_Col) static_cast<ColSphere*>(m_Col)->center = Vec3(pos.x, pos.y, 0.0f);

    } else {
        // === HIT BRANCH ===
        if (m_bMenuBombHit == 0) {
            // Non-menu hit: spawn a BombBlast every 0.05s using GAME dt.
            m_SpawnTimer -= gameDt;
            if (m_SpawnTimer < 0.0f) {
                if (Mortar::ActorManager* am = Mortar::ActorManager::GetInstance()) {
                    Mortar::Entity* e = am->Add(4, true);   // type 4 = BombBlast
                    if (e) {
                        e->pos = pos;
                        e->Init(nullptr, 0, nullptr);
                    }
                }
                m_SpawnTimer = BOMBBLAST_INTERVAL;  // 0.05f (DAT_00172c9c)
            }
        } else {
            // Menu-hit: same physics as alive branch (vel uses scaledDt,
            // pos uses dtNorm). Critical for back-bomb fly-out animation.
            if (m_bMovement) {
                vel += m_AccelForce * scaledDt;
                AccelGrowth(vel, m_AccelForce, dtNorm);
            }
            pos += vel * dtNorm;
#ifndef __bada__
            StepBombRotation(m_RotX, m_RotVelX, m_RotAccumX, scaledDt);
            StepBombRotation(m_RotY, m_RotVelY, m_RotAccumY, scaledDt);
#else
            StepBombRotation(m_RotX, m_RotVelX);
            StepBombRotation(m_RotY, m_RotVelY);
#endif
        }

        // Hide collision — DAT_00172ca4=1000 / DAT_00172ca8=0 / DAT_00172cac=0.01.
        if (m_Col) {
            ColSphere* cs = static_cast<ColSphere*>(m_Col);
            cs->center = Vec3(HIT_COL_POS, HIT_COL_POS, 0.0f);
            cs->radius = HIT_COL_RADIUS;
        }
    }

    // OOB check — kill if off-playfield, else (and only else) lazy-create
    // the fuse emitter. Binary uses `else if` so a killed bomb never gets
    // an emitter attached in the same frame.
    if (pos.y <= BOUNDS_MIN_Y || pos.y >= BOUNDS_MAX_Y ||
        pos.x <= BOUNDS_MIN_X || pos.x >= BOUNDS_MAX_X) {
#if MORTAR_BOMB_TRACE
        {
            unsigned id = (unsigned)((uintptr_t)this >> 4) & 0xfff;
            LOG_VERBOSE("BOMB", "%03x OOB KILL pos=(%.1f,%.1f) vel=(%.2f,%.2f)",
                   id, pos.x, pos.y, vel.x, vel.y);
        }
#endif
        KillBomb();
    } else if (!m_pEmitter) {
        const int variant = (m_BombVariant == 0) ? 0 : 1;
        const uint32_t hash = g_bombData.fuseHash[variant];
        if (hash != 0) {
            PSPParticleManager::GetInstance().AddEmitter(
                hash, &m_pEmitter,
                /*paused*/ gameDt == 0.0f);
        }
        if (m_pEmitter) {
            // Binary writes raw bomb.pos to emitter pos once at creation
            // (0x00172f12) — no per-frame update, no fuse-tip offset.
            m_pEmitter->m_Pos = pos;
        }
    }
}

// ASM-verified: 2026-05-09 binary @ 0x001714e4 (asm-inspector).
// Matches Bomb::DrawUpdate -- pure 2D circle in XY using m_RotY only:
//   angle       = m_RotY * -0xB6
//   offset.x    = sin(angle) * 0.9 * scale.x * 100.0
//   offset.y    = cos(angle) * 0.9 * scale.x * 100.0
//   offset.z    = 5.0
//   emitter.pos = bomb.pos + offset
//   m_ScaleY    =  cos(angle)
//   m_field30   = -sin(angle)
// Earlier port commit extended this with a full RotX*RotY*RotZ chain
// (and DRAW_TILT_ANGLE) to "track the visible fuse tip" as the bomb
// tumbles -- but the binary literally ignores m_RotX and the X-tilt
// here. Reverted to the binary-faithful 2D circle so the smoke /
// sparks orbit on the same path as the original.
void Bomb::PostUpdate(float /*dt*/) {
    if (!m_pEmitter) return;

    static const float FUSE_OFFSET_LEN   = 0.9f;    // DAT_0017159c
    static const float FUSE_SCALE_FACTOR = 100.0f;  // DAT_001715a0

    const uint16_t angle = (uint16_t)(int16_t)(m_RotY * -ANGLE_SCALE);
    const float s = SinIdx(angle);
    const float c = CosIdx(angle);
    const float L = scale.x * FUSE_OFFSET_LEN * FUSE_SCALE_FACTOR;

    m_pEmitter->m_Pos.x = pos.x + s * L;
    m_pEmitter->m_Pos.y = pos.y + c * L;
    m_pEmitter->m_Pos.z = pos.z + 5.0f;

    m_pEmitter->m_ScaleY  =  c;
    m_pEmitter->m_field30 = -s;
}

// Matches Bomb::Draw (0x171be8).
// Binary shape:
//   if (countdown <= 0) {
//     // tracking side-effect on highest-bomb pointer (stubbed)
//     if (model[variant].IsValid()) {
//       Scale44(scaleMat, s, s, s)
//       RotX44(rotMat, SinIdx(0xBFF4), CosIdx(0xBFF4))
//       RotY44(rotMat, SinIdx(m_RotX * 0xB6), CosIdx(m_RotX * 0xB6))
//       RotZ44(rotMat, SinIdx(m_RotY * 0xB6), CosIdx(m_RotY * 0xB6))
//       translate = zOffsetVec * zMult + pos
//       Translate44(rotMat, translate)
//       combined = scaleMat * rotMat
//       Model::Draw(model[variant], combined)
//     }
//   }
//
// See the PORT QUIRK note inside the function body near the
// modelPtr->Draw(mat) call for the "bomb is a white ball" z-fight
// workaround (depth test disabled for the bomb mesh only).
void Bomb::Draw(Renderer& r) {
    (void)r;

    // Binary @ 0x171be8: clears the per-frame fuse-SFX gate at the top of
    // EVERY Bomb::Draw (before the countdown check). Set later by
    // Bomb::Update when a bomb's countdown crosses 0.2s downward.
    g_bombData.bFuseSfxFiredThisFrame = 0;

    if (m_Countdown > 0.0f) return;

    // Binary @ 0x171be8: update "tracked bomb" pointer. Written iff this
    // bomb isn't the previously-tracked one, isn't menu-hit, and is on the
    // playfield (pos.y > -1000). No visible reader in the shipped binary,
    // but mirrored for fidelity.
    static const float TRACKED_BOMB_MIN_Y = -1000.0f;  // DAT_00171d30
    if (this != g_bombData.pTrackedBomb &&
        m_bMenuBombHit == 0 &&
        pos.y > TRACKED_BOMB_MIN_Y) {
        g_bombData.pTrackedBomb = this;
    }

    Mortar::SmartPtr<Mortar::Model>& modelPtr = g_bombData.model[m_BombVariant];
    if (!modelPtr.IsValid()) return;

    // Matrix order must match Fruit::Draw (which renders correctly): build
    //   final = Translate * Rotate * Scale
    // so a vertex v becomes final*v = R*S*v + T_col. Mesh origin lands at
    // T_col (the bomb's world position).
    //
    // Binary decomp LOOKS like Stack_88 = S * RotX * RotY * RotZ then
    // combined = Stack_88 * Translate, which would give mesh-origin =
    // S*R*(pos + zoff*m_ZPos) — scaled-and-rotated translation, placing
    // the mesh far from its intended button slot. Our port previously
    // mirrored that literal order and the bomb rendered near the play
    // button instead of the quit button. Matching Fruit's proven order
    // fixes it. The discrepancy with the decomp's apparent chain is
    // unresolved; leaving the Fruit-style order here since it produces
    // the visually correct result on both this Bada asset set and the
    // binary's intended layout.
    Matrix44 mat = Matrix44::Scale44(scale);     // mat = S

    Matrix44 rotMat;
    rotMat.RotX44(SinIdx((uint16_t)DRAW_TILT_ANGLE),
                  CosIdx((uint16_t)DRAW_TILT_ANGLE));
    rotMat.RotY44(SinIdx((uint16_t)(m_RotX * ANGLE_SCALE)),
                  CosIdx((uint16_t)(m_RotX * ANGLE_SCALE)));
    rotMat.RotZ44(SinIdx((uint16_t)(m_RotY * ANGLE_SCALE)),
                  CosIdx((uint16_t)(m_RotY * ANGLE_SCALE)));

    mat = rotMat * mat;                          // mat = R * S
    mat.GlobalTranslate44(Vec3(pos.x, pos.y, pos.z + m_ZPosition));  // + T in col3

    // DIFFERS: original = CULL_FACE off, port = CULL_FACE on for bomb only.
    //
    // Verified end-to-end via asm-inspector (2026-05-06):
    //  - DisplayManagerBada::BeginFrame @ 0x0019dfec disables GL_CULL_FACE
    //    twice per frame via raw glDisable (bypasses any shadow byte).
    //  - Geometry::Render's CULL_FACE one-shot guard @ 0x001a3f32 reads
    //    a *global* shadow byte at 0x0027488c (resolved via GOT slot
    //    DAT_001a4050) -- the guard fires exactly once on frame 0 and
    //    never re-enables CULL_FACE again. After frame 0, cull stays OFF.
    //  - Bomb::Draw @ 0x00171be8 makes zero GL state calls -- only matrix
    //    builds + Model::Draw.
    //  - bomb.mmd genuinely ships with a duplicated back-face interior
    //    shell: 314 triangles, 238 vertices, 176 unique XYZ positions,
    //    44 positions duplicated with opposite-winding triangles
    //    (156 outward / 158 inward, ~50/50). All bomb_*.mmd variants
    //    are identical. Not a port mis-load.
    //
    // How the Bada original gets away without cull: Z-fighting determinism.
    // The two shells share identical XYZ, so glDepthFunc(GL_LESS) rejects
    // the second-drawn equal-Z fragment. The first 156 triangles in the
    // index stream are predominantly outward, so the outer (textured) shell
    // wins on Bada's specific GLES1 driver. GLES2 drivers don't tie-break
    // equal-Z fragments the same way -- "first writes, second rejected"
    // isn't guaranteed by the spec, and on desktop GL the inner shell
    // sometimes wins, producing the pure-white sphere.
    //
    // Workaround: enable GL_CULL_FACE just for Bomb::Draw. The mesh's
    // GL_BACK / GL_CCW front-face convention drops the inverted-winding
    // interior shell wholesale, leaving the outer shell visible regardless
    // of Z-tie ordering. Scoped to bomb only because every other mesh in
    // the game (fruits, slash blades, splats) matches the binary's
    // cull-off behavior fine -- only bomb has the duplicated-shell layout.
    //
    // Regression history (this workaround has been dropped + restored
    // multiple times):
    //   e93669c  added the bomb-only glEnable(GL_CULL_FACE) workaround
    //   (later) DrawGeometry held cull on globally; bomb cover-fix moot
    //   (this branch's earlier commit) DrawGeometry stops culling to
    //                                  match binary; bomb white sphere
    //                                  reappears -- analog to gallery
    //                                  commit 6785a58 stripping a
    //                                  "redundant" Y<->Z toggle that
    //                                  was actually load-bearing
    //   this commit  restore the bomb-only cull (analog to f721ed0)
    glEnable(GL_CULL_FACE);
    modelPtr->Draw(mat);
    glDisable(GL_CULL_FACE);
}

// Non-virtual cleanup helper called by Mortar::ActorManager::Deactivate.
// Drops the fuse emitter before the entity returns to the free pool.
void Bomb::Deactivate() {
    Release();
}

// Matches Bomb::Chuck (0x170f68)
void Bomb::Chuck(float delay) {
    if (delay <= 0.0f)
        delay = DEFAULT_CHUCK_DELAY;  // 0.2
    m_Countdown = delay;
}

// ASM-verified: 2026-05-03 binary @ 0x001716e8..0x0017171a (asm-inspector)
// Matches Bomb::KillBomb (0x1716e8)
void Bomb::KillBomb() {
    flags |= 0x10;  // mark for removal
    if (m_pEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter);
        m_pEmitter = nullptr;
    }
    // MenuButton owns either a Fruit or a Bomb at +0x134; clear if ours.
    // No ET_RemoveEntity here (Bomb::Update's hit branch handles tracker removal).
    // No active-bomb counter analog to Fruit's g_PowerFruitCount.
    // m_pFruitPiece is Fruit* but the binary stores either a Fruit or a Bomb
    // at this slot; compare by raw address (the stored Bomb pointer IS this).
    if (m_pOwnerButton && reinterpret_cast<void*>(m_pOwnerButton->m_pFruitPiece) == static_cast<void*>(this)) {
        m_pOwnerButton->m_pFruitPiece = nullptr;
    }
}

// Binary @ 0x0017280c — vtable slot 9. Returns 0.
// Three branches:
//   1. m_bMenuBombHit == 0, Classic/Zen: HitBomb — bombHitTimer = 3.2,
//      camera shake (1.6, 2.0), explosion SFX. Game-over fires via
//      GameUpdate's cross-1.5 trigger when m_bMenuBombFlashFlag == 0.
//   2. m_bMenuBombHit == 0, Arcade mode (gameMode == 2): HitMenuBomb —
//      bombHitTimer = 2.0, camera shake (2.0, 3.0), -10 score, clear
//      timed power-ups. HitMenuBomb sets m_bMenuBombFlashFlag = 1
//      (binary @ 0x0016b270) which suppresses GameUpdate's cross-1.5
//      GameOver trigger, so arcade survives the bomb hit. Marks
//      m_bMenuBombHit=1 so subsequent Update keeps the physics alive
//      and the bomb falls off-screen instead of exploding.
//   3. m_bMenuBombHit != 0 (menu bomb re-hit): just fire the hit callback.
int Bomb::CollisionResponse(Mortar::Entity* /*hitter*/,
                             unsigned long /*flagsA*/,
                             unsigned long /*flagsB*/,
                             Vec3* /*bladeVelocity*/) {

    if (m_bCollisionGuard != 0) return 0;   // +0x78 processed guard
    m_bCollisionGuard = 1;

    Game* game = Game::GetInstance();

    if (m_bMenuBombHit == 0 && game != nullptr) {
        FN::SetBombHitPos(pos);

        // ASM-verified: 2026-05-20 binary @ 0x00175066 (re-analyst) — gate is
        // gameMode == ARCADE (literal cmp #0x2). Variable was historically
        // mislabelled "isArcade" in port comments; renamed to isArcade.
        const bool isArcade = (game_work.gameMode == Mortar::GAME_MODE_ARCADE);

        // Camera shake — FruitCamera::CreateCameraShake at 0x180d10.
        // Binary intensities: Classic/Arcade = 1.6/2.0, Zen = 2.0/3.0.
        if (game_work.m_FruitCamera) {
            if (isArcade)
                game_work.m_FruitCamera->CreateCameraShake(pos, 2.0f, 3.0f);
            else
                game_work.m_FruitCamera->CreateCameraShake(pos, 1.6f, 2.0f);
        }

        if (isArcade) {
            // Arcade penalty path -- route through FN::HitMenuBomb so the
            // m_bMenuBombFlashFlag=1 write (binary @ 0x0016b270) actually
            // fires. Previous inline implementation set m_BombHitTimer=2.0
            // and the menu-bomb SFX, but forgot the flash-flag write --
            // so ~0.5s later GameUpdate's cross-1.5 GameOver trigger fired
            // and ended arcade immediately on every bomb slice.
            FN::HitMenuBomb(pos);  // timer=2.0, "menu-bomb" SFX, flash-flag=1
            FN::AddToCurrentScore(-10, 0, false, false);
            PowerUpManager::GetInstance()->ClearTimedPowers();
            WaveManager::GetInstance()->ResetSpeed(0);  // stub until blitz combo lands
            // "X" MissControl indicator for arcade bomb hit. Uses the
            // shared hud_cross overlay; MissControl pre-loads it.
            if (MissControl* mc = MissControl::GetFree()) {
                // Binary: miss->MakeDisappear(pos, 0, bombTex);
                //         miss->field_0x34 = 0x200;  // size multiplier
                // Port: passes Mortar::SmartPtr<Texture>() so MakeDisappear falls
                // back to its default (hud_cross) via internal pick.
                Mortar::SmartPtr<Mortar::Texture> noTex;
                mc->MakeDisappear(pos, 0x200, noTex);
            }
            // Mark as menu-hit so Update's hit branch runs the falling
            // physics instead of the BombBlast shockwave spawn loop.
            m_bMenuBombHit = 1;
        } else {
            // Classic/Arcade game-over path. Binary HitBomb (0x16b0fc) —
            // plays "Bomb-explode" SFX (string at 0x001B96FC), records
            // stat for hash "bomb" (0x001B96CE), sets bombHitTimer = 3.2,
            // camera shake already fired above.
            // GameOver is triggered by GameUpdate when bombHitTimer crosses 1.5 downward.
            // Clear menu-bomb flash flag so GameUpdate's cross-1.5 trigger fires
            // GameOver normally on a real gameplay bomb hit. binary @ 0x0016b154.
            if (GameTaskState* ts = GetTaskState()) {
                ts->m_bMenuBombFlashFlag = 0;
            }
            game_work.m_BombHitTimer = 3.2f;      // DAT_0016b218 = 3.2
            if (game_work.mGameSound) game_work.mGameSound->SFXPlay("Bomb-explode", 1.0f, 1.0f);
            if (game_work.m_SaveData) game_work.m_SaveData->AddToTotal("bomb", 1);
        }
    } else if (m_bMenuBombHit != 0) {
        // ASM-verified: 2026-05-20T00:00Z binary @ 0x00172826..0x0017283c (asm-inspector)
        // Binary: gate at +0x123 (MenuButton::m_bEnabled) only guards ClearMenuItems.
        // Callback is always unconditional.
        if (m_pOwnerButton == nullptr || m_pOwnerButton->m_bEnabled != 0) {
            FN::ClearMenuItems();
        }
        if (m_HitCallback) {
            LOG_INFO("BUTTON", "Bomb::CollisionResponse fires m_HitCallback re-hit (owner=%p enabled=%d pos=(%.1f,%.1f))",
                     static_cast<void*>(m_pOwnerButton),
                     m_pOwnerButton ? (int)m_pOwnerButton->m_bEnabled : -1,
                     pos.x, pos.y);
            m_HitCallback();
        }
    }

    m_bHit = 1;   // +0x68 -- triggers hit branch in Update next tick
    return 0;
}

// Matches Bomb::GetNumActiveForPlayer (0x00171250).
// Binary @ 0x00171250: counts bombs by m_BombVariant, not playerIdx.
// playerIdx==-1 matches "regular bombs" (variant <= 0); playerIdx 1/2 matches MP-zone-tagged bombs.
// countPrespawn=false: count prespawn bombs (countdown > 0 and not yet hit) of any variant.
// countPrespawn=true:  count bombs filtered by variant (no countdown filter).
// ASM-verified: 2026-05-03 binary @ 0x00171250 (asm-inspector)
// ASM-verified: 2026-05-18 binary @ 0x001712c8 (re-analyst).
// Used by GameUpdate fuse-vol block. SP-only path: iterate ActorManager
// type-1 bomb list, return max (pos.y + 160) across bombs whose
// m_bMenuBombHit == 0. Returns -10000.0f sentinel when no qualifying
// bomb exists -- caller treats `<= 0.0f` as "no audible bomb".
float Bomb::GetHeighestBomb() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return -10000.0f;
    float best = -10000.0f;
    std::list<Mortar::Entity*>::iterator it;
    for (Mortar::Entity* e = am->GetEntityFirst(1, it); e; e = am->GetEntityNext(1, it)) {
        Bomb* b = static_cast<Bomb*>(e);
        if (b->m_bMenuBombHit != 0) continue;
        const float metric = b->pos.y + 160.0f;
        if (metric > best) best = metric;
    }
    return best;
}

int Bomb::GetNumActiveForPlayer(int playerIdx, bool countPrespawn) {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return 0;
    int count = 0;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(1, it);
    if (!countPrespawn) {
        // Active pre-spawn bombs (countdown still ticking, not yet hit).
        // Binary GetWait() returns m_Countdown when !m_bHit, else m_SpawnTimer;
        // combined with the m_bHit==0 guard the test reduces to:
        while (e) {
            Bomb* b = static_cast<Bomb*>(e);
            if (b->m_Countdown > 0.0f && b->m_bHit == 0)
                count++;
            e = am->GetEntityNext(1, it);
        }
    } else {
        // Variant-filtered count (no countdown filter):
        //   playerIdx == -1: any bomb with m_BombVariant <= 0
        //   else:            m_BombVariant == playerIdx
        while (e) {
            Bomb* b = static_cast<Bomb*>(e);
            if ((playerIdx == -1 && b->m_BombVariant <= 0) ||
                playerIdx == b->m_BombVariant) {
                count++;
            }
            e = am->GetEntityNext(1, it);
        }
    }
    return count;
}

// ASM-verified: 2026-05-18 binary @ 0x0017171c (re-analyst)
void Bomb::ClearUnspawned() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(1, it);
    while (e) {
        Bomb* b = static_cast<Bomb*>(e);
        Mortar::Entity* next_e = am->GetEntityNext(1, it);
        if (b->m_bHit == 0 && b->m_Countdown > 0.0f)
            b->KillBomb();
        e = next_e;
    }
}

// ASM-verified-via-RE: 2026-05-03 binary @ 0x00126384
void Bomb::SetHit(Bomb* b, float speed) {
    if (!b) return;
    b->m_SpawnTimer = speed;   // Bomb+0xC8 (blast interval, sets the hit-branch timer)
    b->m_bHit       = 1;       // Bomb+0x68 (triggers hit branch in Update)
}

// ASM-verified: 2026-05-03 binary @ 0x00126390 (asm-inspector)
// Binary @ 0x00126390: single store, no other state.
// Spawn-time assignment from WaveManager::SpawnBomb @ 0x00121fa8:
//   - Singleplayer split-screen co-op (gameMode == 2): SetForPlayer(b, 1) only.
//   - Multiplayer: bomb spawns twice (left+right zones); first gets variant 1,
//     second mirrored gets variant 2 (X-position negated, velocity flipped).
void Bomb::SetForPlayer(Bomb* b, int playerIdx) {
    b->m_BombVariant = playerIdx;
}

// ASM-verified: 2026-05-03 binary @ 0x00171d78..0x00171ee8 (asm-inspector, field stores only; FX/SFX block remains TODO)
// Binary @ 0x00171d78: bomb-multiplier-powerup upgrade. Fires when default-spawner
// bomb is created with bomb-multiplier active (playerIdx>0 from SpawnBomb).
// Trigger condition in WaveManager::SpawnBomb post-spawn branch:
//   if (type == 0 && pBomb != nullptr && playerIdx > 0)
//       Bomb::MakeFat(pBomb, false);
void Bomb::MakeFat(bool skipSpawnFx) {
    m_SpeedMult = 0.66597f;                    // DAT_00171eec
    scale      *= 1.33002f;                    // DAT_00171ef0
    m_OrigScale = scale;                       // binary writes field_0x98/9c/a0 (m_OrigScale)
    if (m_Col) static_cast<ColSphere*>(m_Col)->radius *= 1.33002f;   // DAT_00171ef0
    if (!skipSpawnFx) {
        // Spawn particle emitter at +/-240.0 X anchor based on pos.x sign.
        // Hash key: variant!=2 -> DAT_00171f00; variant==2 -> DAT_00171f04.
        // SFX: name string at DAT_00171f0c, MakeSFXDelegate_Coin callback.
        // TODO: PSPParticleManager::AddEmitter and SFXPlay wiring when those callbacks land.
        Chuck(0.25f);  // fuse reset to 0.25s post-upgrade
    }
}
