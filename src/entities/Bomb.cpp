#include "Bomb.h"
#include "game/GameMode.h"
#include "network/P2PMessageHandling.h"
#include "ActorManager.h"
#include "BombBlast.h"
#include "BombFlash.h"
#include "FruitInfo.h"
#include "Game.h"
#include "audio/GameSound.h"
#include "game/BombHit.h"
#include "game/GameOver.h"
#include "game/FruitCamera.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "game/PowerUpManager.h"
#include "render/MatrixManager.h"
#include "asset/TextureManager.h"
#include "asset/MeshManager.h"
#include "asset/Mesh.h"
#include "asset/Model.h"
#include "hud/MenuButton.h"
#include "hud/MissControl.h"
#include "screens/SettingsScreen.h"
#include "screens/MainScreen.h"
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

// --- Constants from binary ---
static const float SPAWN_TIMER_INIT  = 0.6f;       // DAT_001726ac
static const float DEFAULT_CHUCK_DELAY = 0.2f;     // DAT_00170f80
static const float GRAVITY_Y          = -12.0f;    // literal in Init
static const float DT_NORMALIZE       = 1.0f / 60.0f;
static const float ACCEL_GROWTH_RATE  = 0.2f;      // DAT_00172f30
static const float OFFSCREEN_Y        = -320.0f;   // DAT_00172cb0
static const float BOUNDS_MIN_Y       = -240.0f;   // DAT_00172f34
static const float BOUNDS_MAX_Y       =  240.0f;   // DAT_00172f38
static const float BOUNDS_MIN_X       = -360.0f;   // DAT_00172f3c
static const float BOUNDS_MAX_X       =  360.0f;   // DAT_00172f40
static const float BOMBBLAST_INTERVAL = 0.05f;     // DAT_00172c9c
static const float HIT_COL_RADIUS     = 0.01f;     // DAT_00172cac
static const float HIT_COL_POS        = 1000.0f;   // DAT_00172ca4
static const float FUSE_SFX_THRESHOLD = 0.2f;      // DAT_00172ca0

// Fixed tilt for draw: 0xBFF4 in 16-bit angle
static const int16_t DRAW_TILT_ANGLE  = (int16_t)0xBFF4;
// 0xB6 = 182 ~ 1 degree in 16-bit (65536/360 ~ 182)
static const int16_t ANGLE_SCALE      = 0xB6;

// Global bomb data (v1.6.1 binary @ 0x31785C, size 0x48). Layout declared in Bomb.h.
BombGlobalData g_bombData;

// SetupLighting @ 0x001ca5e8 — single `bx lr`, genuine no-op stub in binary.
// Both Bomb::LoadContent and Fruit::LoadFruitModels reach it via PLT trampoline.
// No material / mesh / GL state is touched.
void SetupLighting(const Mortar::SmartPtr<Mortar::Model>&) {}

// --- Bomb::LoadContent / CleanupBomb ---

// ASM-spec v1.6.1 Bomb::LoadContent @ 0x1d6dd4
void Bomb::LoadContent() {
    if (g_bombData.loaded) return;

    // v1.6.1 Bomb::LoadContent @0x001d6dd4 never calls Game::GetInstance, and it
    // feeds MeshManager::GetInstance()'s result (bl 0x00111168) straight into
    // Load with no null test. No guards here.
    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();

    // Binary casing is capital-B (v1.6.1 literals "models/Fruit/Bomb.mmd" /
    // "Bomb_purple.mmd"); on-disk extracted asset is lowercase, resolved by
    // ResolvePathCI in the FS layer. Keep the binary-faithful string.
    g_bombData.model[0] = meshMgr->Load("models/Fruit/Bomb.mmd");
    g_bombData.model[1] = meshMgr->Load("models/Fruit/Bomb_purple.mmd");

    g_bombData.texMinus10 = Mortar::TextureManager::LoadLocalisedTexture("minus_10.tex");

    g_bombData.fuseHash[0] = StringHash("bomb_smoke");
    g_bombData.fuseHash[1] = StringHash("purple_bomb_smoke");

    for (int i = 0; i < 2; i++) {
        if (g_bombData.model[i].IsValid())
            SetupLighting(g_bombData.model[i]);
    }

    g_bombData.loaded = true;
}

// ASM-spec v1.6.1 CleanupBomb @ 0x1d6758
void CleanupBomb() {
    for (int i = 0; i < 2; i++)
        g_bombData.model[i] = Mortar::SmartPtr<Mortar::Model>();
    g_bombData.texMinus10 = Mortar::SmartPtr<Mortar::Texture>();
    g_bombData.m_blastTexture = Mortar::SmartPtr<Mortar::Texture>();
    BombFlash::CleanUp();
}

// --- Bomb implementation ---

// ASM-spec v1.6.1 Bomb::Bomb (C1) @ 0x1d55c0 / (C2) @ 0x1d5610
Bomb::Bomb()
    : m_SpawnTimer(0.0f),
      m_BombVariant(0),
      m_bHit(0),
      m_ZPosition(0.0f),
      m_RotVelX(0), m_RotVelY(0),
      m_RotX(0), m_RotY(0),
      m_bCollisionGuard(0),
      m_pEmitter(nullptr),
      m_bMovement(0),
      m_pOwnerButton(nullptr),
      m_bMenuBombHit(0),
      m_Countdown(0.0f),
      m_SpeedMult(1.0f),
      m_Field_0xAC(0.0f)
{
    entityType = 1;  // Bomb
}

// ASM-spec v1.6.1 Bomb::~Bomb (D1) @ 0x1d5794 / (D0) @ 0x1d5810 / (D2) @ 0x1d5884
// Binary: __vptr; Release(this); ~Delegate0(&m_HitCallback); ~Entity. NO delete m_Col.
Bomb::~Bomb() {
    Release();
}

// ASM-spec v1.6.1 Bomb::Release @ 0x1d5720
// Drops fuse emitter; clears owner-button m_pTrackedFruit backref;
// clears highestBomb (pTrackedBomb) if this; calls Entity::Release (base).
void Bomb::Release() {
    if (m_pEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter);
        m_pEmitter = nullptr;
    }
    if (m_pOwnerButton && m_pOwnerButton->m_pTrackedFruit == reinterpret_cast<Fruit*>(this)) {
        m_pOwnerButton->m_pTrackedFruit = nullptr;
    }
    if (g_bombData.pTrackedBomb == this) {
        g_bombData.pTrackedBomb = nullptr;
    }
    Mortar::Entity::Release();
}

// ASM-spec v1.6.1 Bomb::Init @ 0x1d69e0
// p1/p2 unused; p3 = scale Vec3* (nullable, default 1.0)
void Bomb::Init(void* /*p1*/, long /*p2*/, _Vector3<float>* scaleOrNull) {
    float scaleFactor = 1.0f;
    if (scaleOrNull) scaleFactor = scaleOrNull->x;

    if (!g_bombData.m_blastTexture.IsValid()) {
        g_bombData.m_blastTexture = Mortar::TextureManager::LoadLocalisedTexture("bomb_explode.tex");
    }

    m_BombVariant = 0;
    m_bCollisionGuard = 0;
    // ASM-verified: 2026-05-27 v1.6.1 Bomb::Init @ 0x001d69e0 (re-analyst)
    // `orr r3,r3,#0x2` @0x001d6af4 ; `bfi r3,r7,#0x4,#0x1` (r7 = 0) @0x001d6afc ;
    // `strb r3,[r4,#0xc]` -- confirms Entity::flags lives at +0xC, with
    // ENT_HAS_COLLISION = bit 1 and ENT_KILLED = bit 4.
    flags = (flags & ~ENT_KILLED) | ENT_HAS_COLLISION;
    m_bHit = 0;
    m_bMovement = 1;
    m_SpeedMult = 1.0f;
    m_Field_0xAC = 0.0f;
    m_SpawnTimer = SPAWN_TIMER_INIT;

    // Binary: loop x2 assigns both X and Y axes (rand 1..7 vel, rand 0..0x166 rot),
    // drawn from the shared WaveManager::m_Random (not libc rand()) so bomb spawns
    // advance the same stream, in the same order, as every other wave-system draw.
    // That stream is reseeded per level from the wall-clock-seeded global, so the
    // values are not reproducible -- only the draw count and order are fixed.
    // ASM-spec v1.6.1 Bomb::Init @ 0x1d69e0: loop at 0x1d6b28-0x1d6b60 draws
    // Rand32(7), Rand32(0x167), Rand32(7), Rand32(0x167) in order via WaveManager's
    // shared Math::Random (not libc rand()).
    Math::Random& rng = WaveManager::GetInstance()->GetRandom();
    m_RotVelX = (int16_t)(rng.Rand32(7) + 1);
    m_RotX    = (int16_t)rng.Rand32(0x167);
    m_RotVelY = (int16_t)(rng.Rand32(7) + 1);
    m_RotY    = (int16_t)rng.Rand32(0x167);

    m_bMenuBombHit = 0;
    m_pEmitter = nullptr;
    m_pOwnerButton = nullptr;

    const float bombSize = FruitInfo_GetBombSize();
    const float bombCol  = FruitInfo_GetBombCollision();
    static const float VISUAL_SCALE_MULT = 0.01f;  // DAT_001726b0
    _Vector3<float> computedScale = _Vector3<float>::One() * (bombSize * VISUAL_SCALE_MULT * scaleFactor);

    if (!m_Col) m_Col = new ColSphere();
    {
        ColSphere* cs = static_cast<ColSphere*>(m_Col);
        cs->center() = _Vector3<float>(pos.x, pos.y, 0.0f);
        cs->radius = bombCol * 0.5f * scaleFactor;
    }

    m_Countdown = 0.0f;
    scale = computedScale;
    m_OrigScale = computedScale;
    m_AccelForce = _Vector3<float>(0.0f, GRAVITY_Y, 0.0f);
    m_ZPosition = GetBombZPosition();

    flags &= ~ENT_SKIP_MASK;
}

// ASM-verified: 2026-04-28T00:00 v1.6.1 binary @ 0x0017121c (asm-inspector)
// Matches Bomb::SetCallback (0x0017121c)
void Bomb::SetCallback(Mortar::Delegate0<void> cb, MenuButton* button) {
    m_bMenuBombHit   = 1;
    m_HitCallback    = cb;
    m_pOwnerButton   = button;
    m_RotY    = 0x2d;   // DAT_0017121c: 45 deg initial Y angle
    m_RotVelX = 2;      // slow spin on X
    m_RotX    = 0;
    m_RotVelY = 0;      // Y axis locked
}

// Helper: accel-growth block shared by alive-branch and menu-hit-branch in Update.
// When velocity and accelForce are componentwise aligned, grow accel magnitude
// by (0.2 * dtNorm * 2) per frame. DAT_00172f30 = 0.2
static inline void AccelGrowth(_Vector3<float>& vel, _Vector3<float>& accel, float dtNorm) {
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

// ASM-spec v1.6.1 Bomb::Update @ 0x001d6098
//   (downgraded from ASM-verified 2026-07-25T17:31Z: the stamp covered a body that
//    carried a port-added `game_work.mGameSound` null guard the binary has not.)
void Bomb::Update(float dt) {
    const float gameDt   = game_work.dt;               // countdown/SFX/spawn-timer gating only, ASM @0x1d60ec/0x1d6350
    const float scaledDt = dt * m_SpeedMult;            // v1.6.1 @0x1d6098: scaledDt derives from the incoming dt param, NOT game_work.dt
    const float dtNorm   = (DT_NORMALIZE > 0.0f) ? scaledDt / DT_NORMALIZE : 1.0f;

    if (m_bHit == 0) {
        // === ALIVE BRANCH ===
        if (m_Countdown > 0.0f) {
            // Early-kill: bomb-hit-in-progress or transitioning out
            if (game_work.m_BombHitTimer > 0.0f || game_work.bM_bPaused != 0) {
                m_Countdown = 0.0f;
                pos.y = OFFSCREEN_Y;
                vel = _Vector3<float>(0.0f, -1.0f, 0.0f);
            }

            const float prevCountdown = m_Countdown;
            if (!game_work.bM_Mode) {
                m_Countdown -= gameDt;
            }

            // Fuse SFX on negative-going edge of countdown crossing 0.2s
            if (prevCountdown >= FUSE_SFX_THRESHOLD && m_Countdown < FUSE_SFX_THRESHOLD
                && !g_bombData.bFuseSfxFiredThisFrame
                && game_work.bM_bPaused == 0) {
                // v1.6.1 @0x1d63a4-0x1d63b8: SoundManager::GetInstance() then vtable
                // slot 0 (PreLoadSound) with "Bomb-Fuse" (rodata 0x00283fdd),
                // immediately before the throw SFX.
                Mortar::SoundManager::GetInstance().PreLoadSound("Bomb-Fuse");
                // v1.6.1 Bomb::Update @0x001d6098: SFXPlay is reached with no null test
                // on game_work.mGameSound -- the surrounding gates are the fuse/pause ones.
                game_work.mGameSound->SFXPlay("Throw-bomb", 1.0f, 1.0f);
                g_bombData.bFuseSfxFiredThisFrame = 1;  // @0x1d6488: set after the play
            }

            if (m_Countdown > 0.0f) return;

            // Countdown expired: chain-bomb spawning
            {
                WaveManager* wm = WaveManager::GetInstance();
                // v1.6.1 @0x1d64a4: vldr s16,[wm,#0x6c] = m_BombChance (NOT m_SpawnLevel +0x68)
                const float chance = wm->m_BombChance;
                int iVar7 = (int)chance;
                const float frac = chance - (float)iVar7;
                if (frac > 0.01f) {
                    // v1.6.1 @0x1d64d0: add r0,r0,#0x8 -> WaveManager member RNG
                    // (m_Random, +0x8) Rand32(100); NOT libc rand().
                    const uint32_t rand100 = wm->GetRandom().Rand32(100);
                    if ((float)rand100 < frac * 100.0f) iVar7++;
                }
                if (iVar7 < 1) {
                    m_Countdown = 0.0f;
                    pos.y = OFFSCREEN_Y;
                    vel = _Vector3<float>(0.0f, -1.0f, 0.0f);
                } else if (iVar7 != 1) {
                    wm->SpawnBomb(iVar7 - 1, nullptr, 0);
                }
            }
        }

        // Physics
        if (m_bMovement) {
            vel += m_AccelForce * scaledDt;
            AccelGrowth(vel, m_AccelForce, dtNorm);
        }
        pos += vel * dtNorm;
        // Port specific: freeze menu-ring bomb spin while the SettingsScreen popup is
        // open. HUD::Update already gates HUDControl updates (incl. MenuButton) behind
        // the modal, but this Bomb entity is owned by ActorManager and updated via a
        // separate path (GameUpdate -> ActorManager::Update) not gated by the HUD modal,
        // so an unsliced ring bomb kept spinning behind the frozen ring buttons. Scoped
        // to m_bMenuBombHit (set only by MenuButton::CreateFruit via SetCallback) so
        // real gameplay bombs are never affected; SettingsScreen is only reachable from
        // the main menu (MainScreen::SettingsCallback), so this can't gate gameplay.
        const bool freezeMenuSpin = m_bMenuBombHit && SettingsScreen::IsOpen();
        // v1.6.1 @0x1d6654 (ALIVE arm, m_bHit==0): PLAIN wrapping int16 add, no dt
        // scaling (ldrh/add/strh). The dt-scaled variant belongs to the menu-hit arm
        // (@0x1d624c), not here.
        if (scaledDt > 0.0f && !freezeMenuSpin) {
            m_RotX = (int16_t)(m_RotX + m_RotVelX);
            m_RotY = (int16_t)(m_RotY + m_RotVelY);
        }

        if (m_Col) static_cast<ColSphere*>(m_Col)->center() = _Vector3<float>(pos.x, pos.y, 0.0f);

    } else {
        // === HIT BRANCH ===
        if (m_bMenuBombHit == 0) {
            // Non-menu hit: spawn a BombBlast every 0.05s
            m_SpawnTimer -= gameDt;
            if (m_SpawnTimer < 0.0f) {
                Mortar::Entity* e = Mortar::ActorManager::GetInstance()->Add(4, true);
                if (e) {
                    e->pos = pos;
                    e->Init(nullptr, 0, nullptr);
                }
                m_SpawnTimer = BOMBBLAST_INTERVAL;
            }
        } else {
            // Menu-hit: keep physics alive so bomb falls off-screen
            if (m_bMovement) {
                vel += m_AccelForce * scaledDt;
                AccelGrowth(vel, m_AccelForce, dtNorm);
            }
            pos += vel * dtNorm;
            // v1.6.1 @0x1d624c (menu-hit arm, m_bHit && m_bMenuBombHit): dt-SCALED
            // rotation (vmla.f32 with dtNorm, then vcvt.u32.f32 truncate). The
            // (uint32_t) cast reproduces vcvt.u32.f32 incl. negatives saturating to 0.
            if (scaledDt > 0.0f) {
                m_RotX = (int16_t)(uint16_t)(uint32_t)((float)(uint16_t)m_RotX + (float)(uint16_t)m_RotVelX * dtNorm);
                m_RotY = (int16_t)(uint16_t)(uint32_t)((float)(uint16_t)m_RotY + (float)(uint16_t)m_RotVelY * dtNorm);
            }
        }

        // Hide collision sphere
        if (m_Col) {
            ColSphere* cs = static_cast<ColSphere*>(m_Col);
            cs->center() = _Vector3<float>(HIT_COL_POS, HIT_COL_POS, 0.0f);
            cs->radius = HIT_COL_RADIUS;
        }
    }

    // OOB check: kill if off-playfield, else lazy-create fuse emitter
    if (pos.y <= BOUNDS_MIN_Y || pos.y >= BOUNDS_MAX_Y ||
        pos.x <= BOUNDS_MIN_X || pos.x >= BOUNDS_MAX_X) {
        KillBomb();
    } else if (!m_pEmitter) {
        // ASM-spec v1.6.1 Bomb::Update @ 0x001d6098: index particleHash directly by m_BombVariant.
        // AddEmitter call site is @0x001d6728; updateWhenPaused = (game_work.m_PauseAmount == 0.0f).
        const int variant = m_BombVariant;
        const uint32_t hash = g_bombData.fuseHash[(variant != 0) ? 1 : 0];
        m_pEmitter = PSPParticleManager::GetInstance().AddEmitter(
            hash, nullptr,
            /*updateWhenPaused*/ game_work.m_PauseAmount == 0.0f);
        if (m_pEmitter)
            m_pEmitter->m_Pos = pos;
    }
}

// ASM-verified: 2026-05-09 v1.6.1 binary @ 0x001714e4 (asm-inspector)
// ASM-spec v1.6.1 Bomb::DrawUpdate (PostUpdate) @ 0x1d53a0
// Pure 2D circle in XY using m_RotY only; emitter tip + direction cosines.
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

    m_pEmitter->m_DirCos  =  c;
    m_pEmitter->m_DirSin  = -s;
}

// ASM-spec v1.6.1 Bomb::Draw @ 0x1d6c30
void Bomb::Draw(Renderer& r) {
    (void)r;

    // Clear per-frame fuse-SFX gate at top of every Draw (before countdown check)
    g_bombData.bFuseSfxFiredThisFrame = 0;

    if (m_Countdown > 0.0f) return;

    // Track highest-drawn bomb (highestBomb pointer = g_bombData.pTrackedBomb)
    static const float TRACKED_BOMB_MIN_Y = -1000.0f;  // DAT_00171d30
    if (this != g_bombData.pTrackedBomb &&
        m_bMenuBombHit == 0 &&
        pos.y > TRACKED_BOMB_MIN_Y) {
        g_bombData.pTrackedBomb = this;
    }

    Mortar::SmartPtr<Mortar::Model>& modelPtr = g_bombData.model[m_BombVariant];
    if (!modelPtr.IsValid()) return;

    // Matrix order: R*S then GlobalTranslate (matches Fruit::Draw, correct visual result).
    // Binary decomp appears to show S * R * T, but Fruit::Draw is verified-correct and
    // uses the same chain; preserve this ordering.
    Matrix44 mat = Matrix44::MakeScale(scale);

    Matrix44 rotMat;
    rotMat.RotX44(SinIdx((uint16_t)DRAW_TILT_ANGLE),
                  CosIdx((uint16_t)DRAW_TILT_ANGLE));
    rotMat.RotY44(SinIdx((uint16_t)(m_RotX * ANGLE_SCALE)),
                  CosIdx((uint16_t)(m_RotX * ANGLE_SCALE)));
    rotMat.RotZ44(SinIdx((uint16_t)(m_RotY * ANGLE_SCALE)),
                  CosIdx((uint16_t)(m_RotY * ANGLE_SCALE)));

    mat = rotMat * mat;
    mat.GlobalTranslate44(_Vector3<float>(pos.x, pos.y, pos.z + m_ZPosition));

    // Cull for bomb.mmd's duplicate back-face interior shell (156 outward /
    // 158 inward winding) is now handled faithfully by Geometry::Render
    // (v1.6.1 @0x00264468 forces GL_CULL_FACE on for every 3D-mesh draw) --
    // no bomb-local scoped toggle needed; the stale per-bomb workaround
    // (which assumed the binary drew meshes with cull off) is removed.
    modelPtr->Draw(mat);
}

// ASM-spec v1.6.1 Bomb::KillBomb @ 0x1d5660
void Bomb::KillBomb() {
    flags |= ENT_KILLED;
    if (m_pOwnerButton && reinterpret_cast<void*>(m_pOwnerButton->m_pTrackedFruit) == static_cast<void*>(this)) {
        m_pOwnerButton->m_pTrackedFruit = nullptr;
    }
    if (m_pEmitter) {
        PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter);
        m_pEmitter = nullptr;
    }
}

// ASM-spec v1.6.1 Bomb::Chuck @ 0x1d4ca4
void Bomb::Chuck(float delay) {
    if (delay <= 0.0f)
        delay = DEFAULT_CHUCK_DELAY;
    m_Countdown = delay;
}

// ASM-verified: 2026-06-18 v1.6.1 Bomb::GetHeighestBomb @ 0x001d5138 (asm-verify)
float Bomb::GetHeighestBomb() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    Mortar::Entity* e = am->GetEntity(1L, 0UL);
    float best = -10000.0f;
    unsigned long index = 1;
    if (!e) return best;

    do {
        float metric = e->pos.y;
        if (IsMultiplayer()) {
            metric = e->pos.x;
            if (metric < 0.0f) {
                metric += 240.0f;
            } else {
                metric = 240.0f - metric;
            }
        } else {
            metric += 160.0f;
        }

        Bomb* b = static_cast<Bomb*>(e);
        if (b->m_bMenuBombHit == 0 && best < metric) {
            best = metric;
        }

        am = Mortar::ActorManager::GetInstance();
        e = am->GetEntity(1L, index);
        index++;
    } while (e != nullptr);

    return best;
}

// v1.6.1 Bomb::GetWait @0x00155e68 -- thunk returning the chuck/fuse delay countdown.
float Bomb::GetWait() {
    return m_Countdown;
}

// ASM-spec v1.6.1 Bomb::CollisionResponse @ 0x1d5d4c
// Returns 0. Three branches: arcade bomb hit / classic+zen bomb hit / menu-bomb re-hit.
int Bomb::CollisionResponse(Mortar::Entity* hitter,
                             unsigned long /*flagsA*/,
                             unsigned long /*flagsB*/,
                             _Vector3<float>* /*bladeVelocity*/) {
    // Guard: DO NOT set guard=1 here; Disable() sets it
    if (m_bCollisionGuard != 0) return 0;

    // v1.6.1 Bomb::CollisionResponse @0x001d5d4c gates the fresh-bomb-hit arm on
    // `param_1 != 0` -- the HITTER -- and never calls Game::GetInstance. The port
    // used to test a Game instance here instead, which was a stand-in for this
    // gate; the real parameter is restored.
    // Callers that pass hitter == nullptr (MenuButton::Update's back-key forced
    // slice @0x0019ad14) always target a menu bomb, whose m_bMenuBombHit was set
    // to 1 by Bomb::SetCallback -- so they take the menu-rehit arm below and are
    // unaffected. The blade (SlashEntity::Update @0x001e867c) passes itself.
    if (m_bMenuBombHit == 0 && hitter != nullptr) {
        const bool isArcade = (game_work.gameMode == GAME_MODE_ARCADE);

        if (isArcade) {
            // Arcade path: stat first, then ResetSpeed FIRST, then HitMenuBomb, shake, score, powers
            // v1.6.1 Bomb::CollisionResponse @0x001d5d4c: AddToTotal takes the GOT-resolved
            // game_work.m_SaveData (+0x50) with no null test.
            game_work.m_SaveData->AddToTotal("bombs_hit", 1);
            WaveManager::GetInstance()->ResetSpeed(0);
            m_bMenuBombHit = 1;
            HitMenuBomb(pos);  // timer=2.0, flash-flag=1, "menu-bomb" SFX
            // ASM-spec v1.6.1 Bomb::CollisionResponse @ 0x001d5d4c: the camera is
            // loaded with 'ldr r8,[r3,#0x4c]' and passed straight to
            // CreateCameraShake @0x001d5e94-0x001d5eac with no cmp.
            game_work.m_FruitCamera->CreateCameraShake(pos, 2.0f, 3.0f);
            AddToCurrentScore(-10, 0, false, false);
            PowerUpManager::GetInstance()->ClearTimedPowers();
            // ASM-spec v1.6.1 Bomb::CollisionResponse @ 0x001d5ef4-0x001d5f50:
            // 'bl 0x0011503c; cpy r7,r0' -- the MissControl* is used with no cmp.
            // The binary then does 'ldr r1,[r4,#0x1c]' (minus_10.tex) ->
            // SmartPtr<Texture>::SetPtr, MakeDisappear(this,&pos,r2=0,r3=&tex),
            // then 'mov r3,#0x200; str r3,[r7,#0x34]' (m_LayerFlags).
            MissControl* mc = MissControl::GetFree();
            mc->MakeDisappear(pos, 0, g_bombData.texMinus10);
            mc->m_LayerFlags = 0x200;
        } else {
            // Classic/Zen path: HitBomb handles shake internally (v1.6.1 @ 0x1cf27c)
            if (game_work.bM_bPaused) return 0;
            HitBomb(pos);
        }
    } else if (m_bMenuBombHit != 0) {
        // Menu-bomb re-hit: gate ClearMenuItems on m_bClearsMenuItems
        // NOTE: genuine v1.6.1 gate -- @0x001d5d78-0x001d5d8c is
        // 'ldr r3,[r0,#0x84]; cmp r3,#0; beq call; ldrb r3,[r3,#0x13a]; cmp r3,#0;
        // beq skip', exactly this `||`. Not a port addition.
        if (m_pOwnerButton == nullptr || m_pOwnerButton->m_bClearsMenuItems != 0) {
            ClearMenuItems();
        }
        if (m_HitCallback) {
            m_HitCallback();
        }
    }

    m_bHit = 1;
    return 0;
}

// ASM-spec v1.6.1 Bomb::GetNumActiveForPlayer @ 0x1d5074
int Bomb::GetNumActiveForPlayer(int playerIdx, bool countPrespawn) {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return 0;
    int count = 0;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(1, it);
    if (!countPrespawn) {
        while (e) {
            Bomb* b = static_cast<Bomb*>(e);
            if (b->m_Countdown > 0.0f && b->m_bHit == 0)
                count++;
            e = am->GetEntityNext(1, it);
        }
    } else {
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

// ASM-verified: 2026-05-18 v1.6.1 binary @ 0x0017171c (re-analyst)
// ASM-spec v1.6.1 Bomb::ClearUnspawned @ 0x1d56b4
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

// ASM-spec v1.6.1 Bomb::DeactivateAll @0x001d5030: for each ActorManager type-1 bomb, call
//   Bomb::Disable() (@0x0012c9d8: m_bCollisionGuard(+0x78)=1). Static (ignores its Bomb* arg).
void Bomb::DeactivateAll() {
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;
    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(1, it);
    while (e) {
        static_cast<Bomb*>(e)->Disable();
        e = am->GetEntityNext(1, it);
    }
}

// ASM-verified: 2026-05-03 v1.6.1 Bomb::SetHit @0x0012c9e4 (re-analyst)
// ASM-spec v1.6.1 Bomb::SetHit @0x0012c9e4
void Bomb::SetHit(float speed) {
    m_SpawnTimer = speed;
    m_bHit       = 1;
}

// ASM-verified: 2026-05-03 v1.6.1 binary @ 0x00126390 (asm-inspector)
// ASM-spec v1.6.1 Bomb::SetForPlayer @0x0012702c
void Bomb::SetForPlayer(int playerIdx) {
    m_BombVariant = playerIdx;
}

// ASM-spec v1.6.1 Bomb::MakeFat @ 0x1d6fd4
// TODO: v1.6.1 0x1d6fd4 (Bomb::MakeFat) -- pin exact DAT constants (spec gap: 1.33002 vs 1.33 / 0.66597 vs 0.666)
void Bomb::MakeFat(bool skipSpawnFx) {
    m_SpeedMult = 0.66597f;                 // DAT_00171eec
    scale      *= 1.33002f;                 // DAT_00171ef0
    m_OrigScale = scale;
    if (m_Col) static_cast<ColSphere*>(m_Col)->radius *= 1.33002f;
    if (!skipSpawnFx) {
        // v1.6.1 Bomb::MakeFat @0x001d6fd4: the only gate is `!skipSpawnFx`.
        game_work.mGameSound->SFXPlay("player-bomb-launch", 1.0f, 1.0f);
        // TODO: v1.6.1 0x1d6fd4 (Bomb::MakeFat) -- AddEmitter at +/-240 X anchor (emitter hash variant)
        Chuck(0.25f);
    }
}

// --- Static helper methods ---

// ASM-spec v1.6.1 GetBombZPosition @ 0x1ca5c8
// Static counter init 0.0f; first call decrements -> -50; wraps at -400 -> -10.
static float s_BombZCounter = 0.0f;
float GetBombZPosition() {
    static const float STEP  =  50.0f;
    static const float FLOOR = -400.0f;
    static const float RESET = -10.0f;
    s_BombZCounter -= STEP;
    if (s_BombZCounter < FLOOR) {
        s_BombZCounter = RESET;
    }
    return s_BombZCounter;
}

// ASM-verified: 2026-06-26 v1.6.1 BombFlashFull @ 0x001ca40c (asm-inspector)
// Two-stage vcmpe (vs 1.55f then 1.0f) reduces to (m_BombHitTimer < 1.0f) since <1.0 implies <1.55.
bool BombFlashFull() {
    return game_work.m_BombHitTimer < 1.0f;
}

// ASM-spec v1.6.1 HitBomb @ 0x1cf27c
// Classic/Zen bomb hit: timer=3.2, shake(1.6,2.0), SFX, stat.
void HitBomb(_Vector3<float> pos) {
    if (game_work.bM_bPaused) return;
    // ASM-spec v1.6.1 HitBomb @0x001cf27c: AddToTotal(saveData,"bomb",hash,1,true,true)
    // -- trackSession=true, achievementGate=true (unlike other AddToTotal call
    // sites in this file, which pass both flags false). m_SaveData (+0x50) is read
    // straight off the GOT with no null test.
    game_work.m_SaveData->AddToTotal("bomb", StringHash("bomb"), 1,
                                      /*trackSession=*/true, /*achievementGate=*/true);
    game_work.m_BombHitTimer = 3.2f;
    // ASM-spec v1.6.1 HitBomb @ 0x001cf27c: 'ldr r7,[r5,#0x4c]' @0x001cf32c feeds
    // the 'bl' @0x001cf348 with no cmp -- the camera is never null-checked.
    game_work.m_FruitCamera->CreateCameraShake(pos, 1.6f, 2.0f);
    g_BombHitPos = pos;
    // Binary is a plain 'mov r3,#0; strb r3,[r12,#0x7a]'. GetTaskState() returns
    // &s_taskState (GameTaskState.cpp:46) and is never null -- same precedent as
    // src/game/BombHit.cpp:408.
    GetTaskState()->m_bMenuBombFlashFlag = 0;
    game_work.mGameSound->SFXPlay("Bomb-explode", 1.0f, 1.0f);
}

// ASM-spec v1.6.1 HitMenuBomb @0x001cf42c:
//  - early-out when s_mainScreen && MainScreen::m_State (+0x118) == 1 (menu-idle);
//    no SFX, no timer, no flash in that state.
//  - SFXPlay("menu-bomb", vol=1.0, gain=1.0, Delegate1(), pitch=0.0)
//  - m_BombHitTimer = 2.0f; g_BombHitPos = pos; s_menuBombHit = 1
//  - no camera shake (unlike HitBomb) -- the shake is at the CollisionResponse call site
void HitMenuBomb(_Vector3<float> pos) {
    // NOTE: genuine v1.6.1 gate -- HitMenuBomb @0x001cf42c early-outs only when
    // s_mainScreen is non-null AND its +0x118 state == 1. The null test is part of
    // the &&, not a port addition.
    if (game_work.mMainScreen && game_work.mMainScreen->m_State == STATE_CREATE_BUTTONS)
        return;

    // v1.6.1 HitMenuBomb @0x001cf42c: the only early-out is the MainScreen state test
    // above; SFXPlay then runs with no null test on game_work.mGameSound.
    game_work.mGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
    game_work.m_BombHitTimer = 2.0f;
    g_BombHitPos = pos;
    // Binary is a plain 'mov r3,#1; strb r3,[r12,#0x7a]'. GetTaskState() returns
    // &s_taskState (GameTaskState.cpp:46) and is never null.
    GetTaskState()->m_bMenuBombFlashFlag = 1;
}

// --- Bomb-hit overlay ---

_Vector3<float> g_BombHitPos(0.0f, 0.0f, 0.0f);

static const float BOMB_FLASH_START     = 1.55f;      // v1.6.1 DrawBombHit pool @0x001cd338
static const float BOMB_FLASH_DUR_RECIP = -0.45f;     // v1.6.1 @0x001cd33c (bee66668 = -0.45000005f, 1 ULP)
static const float BOMB_FLASH_MAX_SCALE = 20000.0f;   // v1.6.1 @0x001cd344
static const float BOMB_FLASH_ALPHA_MUL = 255.0f;     // v1.6.1 @0x001cd348
static const float BOMB_FLASH_THRESHOLD = 2.0f;

// ASM-spec v1.6.1 DrawBombHit @0x001cd1a0
// GameDraw @0x001cdc98 gates this call on (0.0 < m_BombHitTimer), between
// HUD::Draw(0x100) and HUD::Draw(0x200) -- already matched by the port's call site.
void DrawBombHit() {
    const float timer = game_work.m_BombHitTimer;
    if (timer <= 0.0f || timer >= BOMB_FLASH_THRESHOLD) return;

    if (!g_FlashTexture.IsValid()) {
        g_FlashTexture = Mortar::TextureManager::LoadLocalisedTexture("flash.tex");
        if (!g_FlashTexture.IsValid()) return;
    }

    const float t = (timer - BOMB_FLASH_START) / BOMB_FLASH_DUR_RECIP + 1.0f;
    float flashScale;
    if (t <= 0.0f)      flashScale = 0.0f;
    else if (t < 1.0f)  flashScale = t * BOMB_FLASH_MAX_SCALE;
    else                flashScale = BOMB_FLASH_MAX_SCALE;

    if (flashScale <= 0.0f) return;

    int a = (int)(BOMB_FLASH_ALPHA_MUL * timer);
    if (a < 0)   a = 0;
    if (a > 255) a = 255;
    const Colour tint(255, 255, 255, (uint8_t)a);

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    Matrix44 mat = Matrix44::MakeScale(flashScale, flashScale, 1.0f);
    mat.GlobalTranslate44(g_BombHitPos);
    mm.GetWorldStack().SetCurrentMatrix(mat);
    mm.UploadModelViewOnly();

    g_FlashTexture->Set();
    Mortar::Mesh::DrawQuadUnCached(tint, NULL);
    // ASM-spec v1.6.1 DrawBombHit @0x001cd1a0: binary passes 1 (true) to
    // vtable+0x10 UnSet(bool) (Ghidra decompile: `(**(...+0x10))(s_flashTexture,1)`),
    // matching the other two shared-flash-texture call sites (task #141).
    g_FlashTexture->UnSet(true);
}

static const float BOMB_BLAST_PURGE_THR = 1.55f;  // DAT_0016a1fc
static const float BOMB_BLAST_RESET_THR = 1.5f;

// ASM-spec v1.6.1 UpdateBombHit @ 0x001cbbac
void UpdateBombHit(float prevTimer) {
    const float currentTimer = game_work.m_BombHitTimer;

    if (prevTimer > BOMB_BLAST_RESET_THR && currentTimer <= BOMB_BLAST_RESET_THR) {
        ResetGameEntities(false);
    }

    if (currentTimer > 0.0f && currentTimer < BOMB_BLAST_PURGE_THR) {
        RemoveFlashEntities();  // v1.6.1 UpdateBombHit @ 0x001cbbac calls RemoveFlashEntities @ 0x001cb4b0
    }
}
