#include "Bomb.h"
#include "ActorManager.h"
#include "BombBlast.h"
#include "FruitInfo.h"
#include "Game.h"
#include "audio/GameSound.h"
#include "game/BombHit.h"
#include "game/FruitCamera.h"
#include "render/Renderer.h"
#include "render/MatrixManager.h"
#include "asset/TextureManager.h"
#include "asset/MeshManager.h"
#include "asset/Mesh.h"
#include "math/Matrix44.h"
#include "math/MathUtil.h"
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
// See docs/entities/bomb.md for full struct layout.
// Binary field offsets (kept as comments for reference; port layout may
// differ since we store SmartPtr<Texture> separately in g_BombTexture):
//   +0x00  Bomb*            pTrackedBomb
//   +0x04  SmartPtr<Texture> tex_02   (bomb_explode.tex — in g_BombTexture)
//   +0x08  uint8_t           bFuseSfxFiredThisFrame
//   +0x0C  SmartPtr<Model>[3] model
//   +0x24  SmartPtr<Texture> texMinus10
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

    SmartPtr<Mortar::Model> model[3];
    SmartPtr<Mortar::Texture> texMinus10;
    bool loaded;
    uint32_t fuseHash[2];

    BombGlobalData()
        : pTrackedBomb(NULL), bFuseSfxFiredThisFrame(0), loaded(false) {
        fuseHash[0] = fuseHash[1] = 0;
    }
};
static BombGlobalData g_bombData;

// Global blast / flash texture. Binary stores at `g_bombData->tex_02`
// (+0x04). Despite the name, this is NOT the bomb mesh texture (that
// comes from the .mmd embedded `fruit_atlas.tex`) — it's the
// `bomb_explode.tex` used by BombBlast::DrawBlast for the shockwave
// quads. BombBlast.cpp references it via `extern`.
SmartPtr<Mortar::Texture> g_BombTexture;

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
        std::string path = game->data_dir + "/models/Fruit/bomb.mmd";
        g_bombData.model[0] = meshMgr->Load(path.c_str());
        printf("[Bomb] LoadContent: model[0] '%s' -> valid=%d\n",
               path.c_str(), g_bombData.model[0].IsValid());
    }

    // Model[1]: binary string "models/Fruit/Bomb_purple.mmd" (0x1BCBF1)
    {
        std::string path = game->data_dir + "/models/Fruit/bomb_purple.mmd";
        g_bombData.model[1] = meshMgr->Load(path.c_str());
        printf("[Bomb] LoadContent: model[1] '%s' -> valid=%d\n",
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

// Matches Bomb::Update (0x001729fc, 195 lines).
void Bomb::Update(float /*dt*/) {
    if (!active) return;

    Game* game = Game::GetInstance();
    if (!game) return;

    const float gameDt  = game->dt;
    const float scaledDt = gameDt * m_SpeedMult;
    const float dtNorm   = (DT_NORMALIZE > 0.0f) ? scaledDt / DT_NORMALIZE : 1.0f;

    if (m_bHit == 0) {
        // === ALIVE BRANCH ===
        if (m_Countdown > 0.0f) {
            // Early-kill: if a bomb just exploded (bombHitTimer>0) or game
            // is transitioning out (pauseFlag!=0), force this bomb off-screen
            // so it expires on the OOB check below. Binary resets countdown
            // to 0 (DAT_00172f28) and pos.y to -320 (DAT_00172cb0).
            if (game->bombHitTimer > 0.0f || game->pauseFlag != 0) {
                m_Countdown = 0.0f;
                pos.y = OFFSCREEN_Y;
                vel = Vec3(HIT_COL_POS, -1.0f, HIT_COL_POS);
            }

            const float prevCountdown = m_Countdown;
            // Tick countdown using GAME dt (not entity scaledDt) — but only
            // when game is active (gameActiveFlag == 0).
            if (game->gameActiveFlag == 0) {
                m_Countdown -= gameDt;
            }

            // Fuse SFX: plays once per frame across all bombs when any bomb's
            // countdown crosses 0.2s downward. Gated by bFuseSfxFiredThisFrame
            // (cleared in Bomb::Draw) and pauseFlag==0.
            static constexpr float FUSE_SFX_THRESHOLD = 0.2f;  // DAT_00172ca0
            if (m_Countdown <= FUSE_SFX_THRESHOLD &&
                prevCountdown > FUSE_SFX_THRESHOLD &&
                !g_bombData.bFuseSfxFiredThisFrame &&
                game->pauseFlag == 0) {
                if (game->pGameSound) {
                    // Binary also calls SoundManager::PreLoadSound first;
                    // our SFX system plays on demand, no preload needed.
                    game->pGameSound->SFXPlay("Bomb-Fuse", 1.0f, 1.0f);
                }
                g_bombData.bFuseSfxFiredThisFrame = 1;
            }

            if (m_Countdown > 0.0f) return;

            // Countdown expired — chain-bomb spawning.
            // Binary: iVar7 = (int)WaveManager::spawnLevel, with a random
            // ceil based on the fractional part (rand100 < frac*100 → +1).
            //   if (iVar7 < 1): countdown = 0; pos.y = -320; vel = (0,-1,0);
            //   else if (iVar7 != 1): WaveManager::SpawnBomb(iVar7 - 1, 0, NULL, ...);
            // TODO: port when WaveManager::spawnLevel / SpawnBomb land.
        }

        // Physics — always runs in alive branch after countdown check.
        if (m_bMovement) {
            vel += m_AccelForce * scaledDt;
            AccelGrowth(vel, m_AccelForce, dtNorm);
        }
        pos += vel * scaledDt;
        if (scaledDt > 0.0f) {
            m_RotX += m_RotVelX;
            m_RotY += m_RotVelY;
        }

        // Update collision sphere to follow bomb. Binary writes pos.xyz then
        // immediately overwrites center.z with DAT_00172f28=0.0 — effectively
        // center = (pos.x, pos.y, 0).
        m_Col.center = Vec3(pos.x, pos.y, 0.0f);

    } else {
        // === HIT BRANCH ===
        if (m_bMenuBombHit == 0) {
            // Non-menu hit: spawn a BombBlast every 0.05s using GAME dt.
            m_SpawnTimer -= gameDt;
            if (m_SpawnTimer < 0.0f) {
                if (ActorManager* am = ActorManager::GetInstance()) {
                    Entity* e = am->Add(4, true);   // type 4 = BombBlast
                    if (e) {
                        e->pos = pos;
                        e->Init(0, 0, 0);
                    }
                }
                m_SpawnTimer = BOMBBLAST_INTERVAL;  // 0.05f (DAT_00172c9c)
            }
        } else {
            // Menu-hit: same physics as alive branch (including accel growth).
            if (m_bMovement) {
                vel += m_AccelForce * scaledDt;
                AccelGrowth(vel, m_AccelForce, dtNorm);
            }
            pos += vel * scaledDt;
            if (scaledDt > 0.0f) {
                m_RotX += m_RotVelX;
                m_RotY += m_RotVelY;
            }
        }

        // Hide collision — DAT_00172ca4=1000 / DAT_00172ca8=0 / DAT_00172cac=0.01.
        m_Col.center = Vec3(HIT_COL_POS, HIT_COL_POS, 0.0f);
        m_Col.radius = HIT_COL_RADIUS;
    }

    // OOB check — kill if off-playfield, else (and only else) lazy-create
    // the fuse emitter. Binary uses `else if` so a killed bomb never gets
    // an emitter attached in the same frame.
    if (pos.y <= BOUNDS_MIN_Y || pos.y >= BOUNDS_MAX_Y ||
        pos.x <= BOUNDS_MIN_X || pos.x >= BOUNDS_MAX_X) {
        KillBomb();
    } else if (!m_pEmitter) {
        const int variant = (m_BombVariant == 0) ? 0 : 1;
        const uint32_t hash = g_bombData.fuseHash[variant];
        if (hash != 0) {
            Mortar::PSPParticleManager::GetInstance().AddEmitter(
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
    static constexpr float TRACKED_BOMB_MIN_Y = -1000.0f;  // DAT_00171d30
    if (this != g_bombData.pTrackedBomb &&
        m_bMenuBombHit == 0 &&
        pos.y > TRACKED_BOMB_MIN_Y) {
        g_bombData.pTrackedBomb = this;
    }

    SmartPtr<Mortar::Model>& modelPtr = g_bombData.model[m_BombVariant];
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

    modelPtr->Draw(mat);
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
            // Zen penalty path. Binary HitMenuBomb (0x16b234) — plays
            // "menu-bomb" SFX (string at 0x001B96C9), bombHitTimer = 2.0,
            // sets g_bombHitData->m_bMenuBombHit_flag = 1. No camera shake.
            game->bombHitTimer = 2.0f;
            if (game->pGameSound) game->pGameSound->SFXPlay("menu-bomb", 1.0f, 1.0f);
            // TODO: AddToCurrentScore(-10, 0, false, false)
            // TODO: PowerUpManager::ClearTimedPowers()
            // TODO: WaveManager::ResetSpeed(0)
            // TODO: "X" MissControl indicator
            // Mark as menu-hit so Update's hit branch runs the falling
            // physics instead of the BombBlast shockwave spawn loop.
            m_bMenuBombHit = 1;
        } else {
            // Classic/Arcade game-over path. Binary HitBomb (0x16b0fc) —
            // plays "Bomb-explode" SFX (string at 0x001B96FC), records
            // stat for hash "bomb" (0x001B96CE), sets bombHitTimer = 3.2,
            // camera shake already fired above.
            game->bombHitTimer = 3.2f;      // DAT_0016b218 = 3.2
            if (game->pGameSound) game->pGameSound->SFXPlay("Bomb-explode", 1.0f, 1.0f);
            // TODO: FruitSaveData::AddToTotal("bomb", 1)
            // TODO: skip entirely if game->gameOverFlag already set
        }
    } else if (m_bMenuBombHit != 0) {
        // Menu-bomb re-hit branch. Binary code:
        //   if (field_0x84 == 0 || *(field_0x84 + 0x123) != 0)
        //       ClearMenuItems();
        //   Delegate0<void>::operator()(&field_0x40);   // hit callback
        //
        // field_0x84 is a backref to the owning game-state struct
        // (a MainScreen-ish container). The +0x123 byte gates whether
        // the menu items are still locked in. ClearMenuItems removes
        // every menu button + decoration entity from the HUD.
        //
        // Port v1: trigger ClearMenuItems by setting MainScreen state
        // to STATE_QUIT_WAIT, which already drives the bomb off-screen
        // animation and exits the game. Same end result without
        // touching the binary's vtable callback path.
        if (m_HitCallback) {
            m_HitCallback();
        }
    }

    m_bHit = 1;   // +0x68 -- triggers hit branch in Update next tick
}
