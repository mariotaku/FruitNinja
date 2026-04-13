#include "Bomb.h"
#include "ActorManager.h"
#include "BombBlast.h"
#include "FruitInfo.h"
#include "Game.h"
#include "game/BombHit.h"
#include "game/FruitCamera.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "asset/TextureManager.h"
#include "asset/MeshManager.h"
#include "asset/Mesh.h"
#include "math/Matrix44.h"
#include "particle/PSPParticleManager.h"
#include "util/StringHash.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>

// Analysed: 2026-04-10T10:00

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
// See docs/entities/bomb.md for full struct layout
struct BombGlobalData {
    SmartPtr<Mortar::Model> model[3];  // +0x0C: [0]=bomb.mmd, [1]=bomb_purple.mmd, [2]=unused
    SmartPtr<Mortar::Texture> texMinus10;  // +0x24: minus_10.tex
    bool loaded;            // +0x28: guard flag
    uint32_t fuseHash[2];   // +0x2C/+0x30: bomb_smoke / purple_bomb_smoke hashes
    BombGlobalData() : loaded(false) { fuseHash[0] = fuseHash[1] = 0; }
};
static BombGlobalData g_bombData;

// Global bomb texture (lazy-loaded in Init, matches binary)
static SmartPtr<Mortar::Texture> g_BombTexture;

// Global bomb Z cycling (matches GetBombZPosition at 0x169080)
static float g_BombZCurrent = -10.0f;

static float GetBombZPosition() {
    float z = g_BombZCurrent - 50.0f;  // DAT_001690bc
    if (z < -400.0f)                    // DAT_001690c0
        z = -10.0f;
    g_BombZCurrent = z;
    return z;
}

// --- Bomb::LoadContent (0x1726c8) — called once from GameInitialise ---

void Bomb::LoadContent() {
    if (g_bombData.loaded) return;  // +0x28 guard

    Game* game = Game::GetInstance();
    if (!game) return;

    Mortar::MeshManager* meshMgr = Mortar::MeshManager::GetInstance();
    if (!meshMgr) {
        printf("[Bomb] LoadContent: MeshManager not available!\n");
        return;
    }

    // Model[0]: binary string "models/Fruit/Bomb.mmd" (0x1BCBDB)
    // Bada filesystem was case-insensitive; actual file is lowercase
    {
        std::string path = game->data_dir + "/models/Fruit/bomb.mmd";
        g_bombData.model[0] = meshMgr->Load(path.c_str());
        printf("[Bomb] LoadContent: model[0] '%s' → valid=%d\n",
               path.c_str(), g_bombData.model[0].IsValid());
    }

    // Model[1]: binary string "models/Fruit/Bomb_purple.mmd" (0x1BCBF1)
    {
        std::string path = game->data_dir + "/models/Fruit/bomb_purple.mmd";
        g_bombData.model[1] = meshMgr->Load(path.c_str());
        printf("[Bomb] LoadContent: model[1] '%s' → valid=%d\n",
               path.c_str(), g_bombData.model[1].IsValid());
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
    // TODO: SetupLighting on both models

    g_bombData.loaded = true;
    printf("Bomb::LoadContent: loaded bomb models\n");
}

// --- Bomb implementation ---

Bomb::Bomb()
    : m_SpawnTimer(0.0f),
      m_BombVariant(0),
      m_bHit(0),
      m_ZPosition(0.0f),
      m_RotVelX(0), m_RotVelY(0),
      m_RotX(0), m_RotY(0),
      m_bCollisionGuard(0),
      m_pEmitter(NULL),
      m_bMovement(0),
      m_bMenuBombHit(0),
      m_Countdown(0.0f),
      m_SpeedMult(1.0f)
{
    entityType = 1;  // Bomb
}

Bomb::~Bomb() {
    if (m_pEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter);
        m_pEmitter = NULL;
    }
    // TODO: Unlink from game state (field_0x84)
}

// Matches Bomb::Init (0x172504, 99 lines)
void Bomb::Init(int param1, int fruitType, int param3) {
    (void)param1;
    (void)fruitType;

    float scaleFactor = 1.0f;
    // Original: if (p3 != NULL) scaleFactor = *(float*)p3;

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

    m_bMenuBombHit = 0;
    m_pEmitter = NULL;  // lazy-created in Update

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

    m_Col.center = Vec3(pos.x, pos.y, 0.0f);
    m_Col.radius = bombCol * 0.5f * scaleFactor;
    m_Countdown = 0.0f;
    scale = computedScale;
    m_OrigScale = computedScale;
    m_AccelForce = Vec3(0.0f, GRAVITY_Y, 0.0f);
    m_ZPosition = GetBombZPosition();

    active = true;

    // Use pre-loaded model from g_bombData (loaded by LoadContent in GameInitialise)
    // Draw indexes as g_bombData.model[m_BombVariant]
    // No per-instance mesh loading needed
    printf("[Bomb] Init: active=%d flags=0x%02x variant=%d scale=(%.2f,%.2f,%.2f) "
           "pos=(%.1f,%.1f,%.1f) countdown=%.2f model_valid=%d tex_valid=%d\n",
           active, flags, m_BombVariant, scale.x, scale.y, scale.z,
           pos.x, pos.y, pos.z, m_Countdown,
           g_bombData.model[m_BombVariant].IsValid(), g_BombTexture.IsValid());
}

// Matches Bomb::Update (0x1729fc, 195 lines)
void Bomb::Update(float dt) {
    if (!active) return;

    float scaledDt = dt * m_SpeedMult;
    float dtNorm = (DT_NORMALIZE > 0.0f) ? scaledDt / DT_NORMALIZE : 1.0f;

    if (m_bHit == 0) {
        // === ALIVE BOMB ===

        if (m_Countdown > 0.0f) {
            // Tick countdown (matches binary: uses game dt, not entity dt)
            // TODO: Check Game.bombTimer > 0 || Game.gameOverFlag → kill immediately
            // TODO: Check Game.paused → skip countdown
            m_Countdown -= dt;

            // TODO: Fuse SFX at 0.2s threshold
            // if (m_Countdown <= 0.2f && prev > 0.2f) PlayFuseSFX()

            if (m_Countdown > 0.0f) return;

            // TODO: Chain bomb spawning via WaveManager::SpawnBomb
        }

        // Physics: velocity += accelForce * scaledDt
        if (m_bMovement) {
            vel += m_AccelForce * scaledDt;

            // Acceleration growth when vel and accel are aligned
            float dot = vel.x * m_AccelForce.x + vel.y * m_AccelForce.y + vel.z * m_AccelForce.z;
            if (dot > 0.0f) {
                float len = sqrtf(m_AccelForce.x * m_AccelForce.x +
                                  m_AccelForce.y * m_AccelForce.y +
                                  m_AccelForce.z * m_AccelForce.z);
                if (len > 0.001f) {
                    float newLen = len + ACCEL_GROWTH_RATE * dtNorm * 2.0f;
                    float ratio = newLen / len;
                    m_AccelForce *= ratio;
                }
            }
        }
        pos += vel * scaledDt;

        // Rotation animation
        if (scaledDt > 0.0f) {
            m_RotX += m_RotVelX;
            m_RotY += m_RotVelY;
        }

        // Update collision sphere to follow the bomb (z clamped to 0).
        m_Col.center = Vec3(pos.x, pos.y, 0.0f);

    } else {
        // === HIT BOMB ===
        if (m_bMenuBombHit == 0) {
            // Non-menu hit: spawn BombBlast entities every 0.05s for the
            // ring shockwave burst. Matches binary Bomb::Update 0x1729fc
            // hit-branch at m_bBombFlag88 == 0.
            m_SpawnTimer -= dt;
            if (m_SpawnTimer < 0.0f) {
                if (ActorManager* am = ActorManager::GetInstance()) {
                    Entity* e = am->Add(4, true);   // type 4 = BombBlast
                    if (e) {
                        e->pos = pos;
                        e->Init(0, 0, 0);
                    }
                }
                m_SpawnTimer = BOMBBLAST_INTERVAL; // 0.05f
            }
        } else {
            // Menu-hit: continue physics (bomb falls away)
            if (m_bMovement) {
                vel += m_AccelForce * scaledDt;
            }
            pos += vel * scaledDt;
            if (scaledDt > 0.0f) {
                m_RotX += m_RotVelX;
                m_RotY += m_RotVelY;
            }
        }
        // Move collision offscreen once hit — blocks double-slicing and
        // matches binary DAT_00172ca4 = 1000.0 / DAT_00172cac = 0.01.
        m_Col.center = Vec3(HIT_COL_POS, HIT_COL_POS, 0.0f);
        m_Col.radius = HIT_COL_RADIUS;
    }

    // Out of bounds check
    if (pos.y <= BOUNDS_MIN_Y || pos.y >= BOUNDS_MAX_Y ||
        pos.x <= BOUNDS_MIN_X || pos.x >= BOUNDS_MAX_X) {
        KillBomb();
    }

    // Lazy-create fuse particle emitter (matches binary: first Update after
    // Init creates the emitter, then each tick the emitter pos tracks the bomb).
    // Only alive, non-menu-hit bombs get the fuse trail.
    if (!m_pEmitter && m_bHit == 0 && m_Countdown == 0.0f && active) {
        int variant = (m_BombVariant == 0) ? 0 : 1;
        uint32_t hash = g_bombData.fuseHash[variant];
        if (hash != 0) {
            Mortar::PSPParticleManager::GetInstance().AddEmitter(
                hash, &m_pEmitter, false);
        }
    }
    if (m_pEmitter) {
        m_pEmitter->m_Pos = pos;
    }
}

// Matches Bomb::Draw (0x171be8)
// Transform: Scale * RotX(fixed tilt) * RotY(m_RotX) * RotZ(m_RotY) * Translate
void Bomb::Draw(Renderer& r) {
    static bool s_loggedOnce = false;
    if (!active || m_Countdown > 0.0f) {
        if (!s_loggedOnce) {
            printf("[Bomb] Draw: skip (active=%d countdown=%.2f)\n", active, m_Countdown);
            s_loggedOnce = true;
        }
        return;
    }

    // Model from global data, indexed by variant (matches binary: g_bombData->model[m_BombVariant])
    int variant = m_BombVariant;
    if (variant < 0 || variant > 2) variant = 0;
    SmartPtr<Mortar::Model>& modelPtr = g_bombData.model[variant];
    if (!modelPtr.IsValid() || !g_BombTexture.IsValid()) {
        if (!s_loggedOnce) {
            printf("[Bomb] Draw: skip (variant=%d model_valid=%d tex_valid=%d)\n",
                   variant, modelPtr.IsValid(), g_BombTexture.IsValid());
            s_loggedOnce = true;
        }
        return;
    }
    Mortar::Model* bombModel = modelPtr.Get();

    float s = scale.x;
    if (s <= 0.0f) {
        if (!s_loggedOnce) {
            printf("[Bomb] Draw: skip (scale=%.4f)\n", s);
            s_loggedOnce = true;
        }
        return;
    }

    if (!s_loggedOnce) {
        printf("[Bomb] Draw: rendering variant=%d scale=%.2f pos=(%.1f,%.1f,%.1f)\n",
               variant, s, pos.x, pos.y, pos.z);
        s_loggedOnce = true;
    }

    Mortar::MatrixManager& mm = Mortar::MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();

    // Scale
    Matrix44 mat = Matrix44::MakeScale(s, s, s);

    // RotX: fixed tilt 0xBFF4 ≈ -83 degrees (makes bomb face camera)
    // 0xBFF4 in 16-bit angle = 0xBFF4 * 2π / 65536
    {
        float angleRad = (float)(int16_t)0xBFF4 * 6.2831853f / 65536.0f;
        float sinA = sinf(angleRad);
        float cosA = cosf(angleRad);
        // RotX: rotate around X axis
        for (int i = 0; i < 4; i++) {
            float a = mat.m[4 + i];   // col 1 (Y)
            float b = mat.m[8 + i];   // col 2 (Z)
            mat.m[4 + i] = a * cosA + b * sinA;
            mat.m[8 + i] = -a * sinA + b * cosA;
        }
    }

    // RotY: animated by m_RotX * 0xB6 (182 ≈ 1 degree in 16-bit)
    {
        int16_t angle16 = m_RotX * ANGLE_SCALE;
        float angleRad = (float)angle16 * 6.2831853f / 65536.0f;
        float sinA = sinf(angleRad);
        float cosA = cosf(angleRad);
        for (int i = 0; i < 4; i++) {
            float a = mat.m[i];       // col 0 (X)
            float b = mat.m[8 + i];   // col 2 (Z)
            mat.m[i]     = a * cosA + b * sinA;
            mat.m[8 + i] = -a * sinA + b * cosA;
        }
    }

    // RotZ: animated by m_RotY * 0xB6
    {
        int16_t angle16 = m_RotY * ANGLE_SCALE;
        float angleRad = (float)angle16 * 6.2831853f / 65536.0f;
        float sinA = sinf(angleRad);
        float cosA = cosf(angleRad);
        mat.RotZ44(sinA, cosA);
    }

    // Position in binary-centred ortho space.
    // See docs/engine/coordinate-system.md and FruitCamera::SetupPerspective.
    Vec3 drawPos(pos.x, pos.y, m_ZPosition);
    mat.GlobalTranslate44(drawPos);

    // Use Model::Draw which handles its own texture, MVP, and mesh rendering
    // Matches binary: Mortar::Model::Draw(g_bombData->model[variant], combinedMatrix)
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    bombModel->Draw(mat);
    glDisable(GL_DEPTH_TEST);
}

void Bomb::Deactivate() {
    active = false;
    flags |= 0x10;
    if (m_pEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter);
        m_pEmitter = NULL;
    }
}

// Matches Bomb::Chuck (0x170f68)
void Bomb::Chuck(float delay) {
    if (delay <= 0.0f)
        delay = DEFAULT_CHUCK_DELAY;  // 0.2
    m_Countdown = delay;
}

// Matches Bomb::KillBomb (0x1716e8)
void Bomb::KillBomb() {
    flags |= 0x10;  // mark for removal
    if (m_pEmitter) {
        Mortar::PSPParticleManager::GetInstance().ClearEmitter(m_pEmitter);
        m_pEmitter = NULL;
    }
    // TODO: Unlink from game state (field_0x84)
}

// Matches Bomb::CollisionResponse (0x17280c). Three branches:
//   1. m_bMenuBombHit == 0, Classic/Arcade: HitBomb — bombHitTimer = 3.2,
//      camera shake (1.6, 2.0), explosion SFX. Classic is the game-over path.
//   2. m_bMenuBombHit == 0, Zen mode (gameMode == 2): HitMenuBomb —
//      bombHitTimer = 2.0, camera shake (2.0, 3.0), -10 score, clear
//      timed power-ups, mark as menu-hit so subsequent Update keeps the
//      physics alive and the bomb falls off-screen instead of exploding.
//   3. m_bMenuBombHit != 0 (menu bomb re-hit): just fire the hit callback.
void Bomb::OnSliced(const Vec3& bladeVel) {
    (void)bladeVel;

    if (m_bCollisionGuard != 0) return;   // +0x78 processed guard
    m_bCollisionGuard = 1;

    printf("[Bomb] OnSliced: pos=(%.1f,%.1f) menuHit=%d\n",
           pos.x, pos.y, m_bMenuBombHit);

    Game* game = Game::GetInstance();

    if (m_bMenuBombHit == 0 && game != NULL) {
        FN::SetBombHitPos(pos);

        const bool isZen = (game->gameMode == 2);

        // Camera shake — FruitCamera::CreateCameraShake at 0x180d10.
        // Binary intensities: Classic/Arcade = 1.6/2.0, Zen = 2.0/3.0.
        if (game->pCamera) {
            if (isZen)
                game->pCamera->CreateCameraShake(pos, 2.0f, 3.0f);
            else
                game->pCamera->CreateCameraShake(pos, 1.6f, 2.0f);
        }

        if (isZen) {
            // Zen penalty path. Binary HitMenuBomb (0x16b234).
            game->bombHitTimer = 2.0f;      // DAT @ 0x16b234 = 2.0
            // TODO: AddToCurrentScore(-10, 0, false, false)
            // TODO: PowerUpManager::ClearTimedPowers()
            // TODO: WaveManager::ResetSpeed(0)
            // TODO: "X" MissControl indicator
            // Mark as menu-hit so Update's hit branch runs the falling
            // physics instead of the BombBlast shockwave spawn loop.
            m_bMenuBombHit = 1;
        } else {
            // Classic/Arcade game-over path. Binary HitBomb (0x16b0fc).
            // TODO: FruitSaveData::AddToTotal("bomb_sliced", 1)
            // TODO: skip entirely if game->gameOverFlag already set
            // TODO: GameSound::SFXPlay("bomb_explode")
            game->bombHitTimer = 3.2f;      // DAT_0016b218 = 3.2
        }
    }

    m_bHit = 1;   // +0x68 -- triggers hit branch in Update next tick
}
