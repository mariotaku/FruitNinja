// SuperFruitControl — super-fruit (pomegranate/starfruit frenzy) state machine.
// Binary: ctor @ 0x001be1c8, restore-from-save ctor @ 0x001bea90,
//         Update @ 0x001bca10, Sliced @ 0x001bb994, ExplodeSuperFruit @ 0x001baa20,
//         SuperFruitThrown @ 0x001bbf48, SuperFruitSliced @ 0x001be630,
//         IsInSuperFruitState @ 0x001b9828, Reset @ 0x001bb52c, Release @ 0x001bb664,
//         StopAllFruit @ 0x001ba460, SaveSuperFruitState @ 0x001ba73c,
//         ComboCancel @ 0x001b9850.

#include "SuperFruitControl.h"
#include "SuperFruitGlow.h"
#include "hud/MissControl.h"
#include "SuperFruitState.h"
#include "Fruit.h"
#include "FruitInfo.h"
#include "Bomb.h"
#include "SlashEntity.h"
#include "SplatEntity.h"
#include "Jiblet.h"
#include "ActorManager.h"
#include "game/GameWork.h"
#include "game/GameOver.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "game/FruitCamera.h"
#include "game/BombHit.h"
#include "Game.h"
#include "engine/particle/PSPParticleManager.h"
#include "engine/math/MathUtil.h"
#include "engine/math/Random.h"
#include "engine/asset/MeshManager.h"
#include "util/StringHash.h"
#include "debug/Logger.h"
#include <map>
#include <tinyxml2.h>
#include <cstring>
#include <cstdio>
#include <cmath>

// Static map definition (24-byte std::map per CLAUDE.md).
std::map<Fruit*, SuperFruitControl*> SuperFruitControl::SuperFruitControls;

// Static session counter: number of pomegranates spawned this game.
// Binary: BSS global; accessed via IsInSuperFruitState / NumPomegranatesSpawnedThisGame.
static int s_PomegranatesSpawnedThisGame = 0;

// Static active-state flag. IsInSuperFruitState() reads game singleton +0x14.
// Port: shadow the flag here since game+0x14 is not yet mapped in the port.
static int s_SuperFruitActive = 0;

// Binary @ 0x001be1c8: fresh controller ctor.
SuperFruitControl::SuperFruitControl(Fruit* fruit)
    : m_pHostFruit(fruit)
    , m_HitCount(0.0f)
    , m_Timer(0.0f)
    , m_PrevTimer(0.0f)
    , m_SliceCount(0)
    , m_pLinkedSlasher(nullptr)
    , m_Lifetime(5.0f)   // default baseline; TODO: 0x001be1c8 -- resolve from binary DAT
    , m_FadeIn(0.0f)
    , m_Scale(0.0f)
    , m_SliceCooldown(0)
    , m_pGlow(nullptr)
{
    entityType = 6;  // super-fruit type in binary
    memset(_pad_own, 0, sizeof(_pad_own));
    memset(_pad_80, 0, sizeof(_pad_80));
    memset(_pad_98, 0, sizeof(_pad_98));
    memset(&m_WorkVec1, 0, sizeof(m_WorkVec1));
    memset(&m_WorkVec2, 0, sizeof(m_WorkVec2));
    memset(&m_WorkVec3, 0, sizeof(m_WorkVec3));
    memset(&m_WorkVec4, 0, sizeof(m_WorkVec4));
    memset(_pad_e0, 0, sizeof(_pad_e0));
    memset(&m_WorkVec5, 0, sizeof(m_WorkVec5));
    memset(&m_WorkVec6, 0, sizeof(m_WorkVec6));

    s_SuperFruitActive = 1;
    ++s_PomegranatesSpawnedThisGame;
    AttachGlow();
}

// Binary @ 0x001bea90: restore-from-save ctor.
SuperFruitControl::SuperFruitControl(Fruit* fruit, SuperFruitState& state)
    : m_pHostFruit(fruit)
    , m_HitCount(0.0f)
    , m_Timer(state.m_Timer)
    , m_PrevTimer(state.m_Timer)
    , m_SliceCount(state.m_SliceCount)
    , m_pLinkedSlasher(nullptr)
    , m_Lifetime(state.m_Lifetime)
    , m_FadeIn(1.0f)   // already visible when restored
    , m_Scale(1.0f)
    , m_SliceCooldown(0)
    , m_pGlow(nullptr)
{
    entityType = 6;
    memset(_pad_own, 0, sizeof(_pad_own));
    memset(_pad_80, 0, sizeof(_pad_80));
    memset(_pad_98, 0, sizeof(_pad_98));
    memset(&m_WorkVec1, 0, sizeof(m_WorkVec1));
    memset(&m_WorkVec2, 0, sizeof(m_WorkVec2));
    memset(&m_WorkVec3, 0, sizeof(m_WorkVec3));
    memset(&m_WorkVec4, 0, sizeof(m_WorkVec4));
    memset(_pad_e0, 0, sizeof(_pad_e0));
    memset(&m_WorkVec5, 0, sizeof(m_WorkVec5));
    memset(&m_WorkVec6, 0, sizeof(m_WorkVec6));

    s_SuperFruitActive = 1;
    AttachGlow();
}

SuperFruitControl::~SuperFruitControl()
{
    m_pHostFruit = nullptr;
    m_pGlow = nullptr;
}

// Binary @ 0x001bb664.
void SuperFruitControl::Release()
{
    // Notify glow: trigger fade-out (marks m_bPendingRemoval for removal)
    if (m_pGlow) {
        m_pGlow->Release();
        m_pGlow = nullptr;
    }
    m_pHostFruit = nullptr;
    s_SuperFruitActive = 0;
    Mortar::Entity::Release();
}

// Binary @ 0x001bca10. Per-frame phase-ladder state machine.
// Master clock: m_Timer(+0x88). Phase thresholds keyed off m_Lifetime(+0xa0).
// DAT constants from literal pool @ 0x1bcd64 / 0x1bd488 — see spec.
void SuperFruitControl::Update(float dt)
{
    bool paused = game_work.m_Paused;

    if (!paused) {
        m_Timer += dt;                              // +0x88 advance
        // combo-pitch SFX accumulator decay: -17.5/s, floor 0
        if (m_HitCount > 0.0f) {                    // +0x84
            float v = m_HitCount + dt * (-17.5f);   // DAT_001bcd64 = -17.5
            if (v <= 0.0f) v = 0.0f;               // DAT_001bcdac = 0.0
            m_HitCount = v;
        }
    }

    // keep host fruit's +0xbc clamped to 1.0 while >0
    // TODO: 0x001bca10 -- clamp host-fruit field +0xbc to 1.0 (Fruit field not yet named in port)

    // one-shot edge: PrevTimer<0 && Timer>=0 && SliceCount==1 -> first ChangeText
    if (m_PrevTimer < 0.0f && m_Timer >= 0.0f && m_SliceCount == 1) {
        // TODO: 0x001bca10 -- ChangeText(this, DAT_001bcd78, 0, NULL) (needs FancyBakedString)
    }

    // pre-roll slowdown: while Timer < Lifetime+0.5
    if (m_Timer < m_Lifetime + 0.5f) {
        WaveManager::GetInstance()->m_SpeedScale = 0.1f;  // DAT_001bcd98 = 0.1; SetAbsoluteDtMod
        MissControl::MakeEmAllDissappear();
    }

    // bomb suppression window; fVar = 1.5 if Arcade else 0.5
    float modeBias = (game_work.gameMode == 2) ? 1.5f : 0.5f;
    if (m_Timer < m_Lifetime + 0.5f + 0.35f + 0.55f + 0.65f + 0.25f + modeBias) {
        // TODO: 0x001bca10 -- Bomb::DeactivateAll (needs Bomb::DeactivateAll static)
    }

    // whoosh SFX: one-shot on crossing (Lifetime - 0.1)
    if (m_Timer >= m_Lifetime - 0.1f && m_PrevTimer < m_Lifetime - 0.1f) {
        // TODO: 0x001bca10 -- GameSound::SFXPlay(game+0x18c, DAT_001bcd80, pitch=0.125, vol=1.0, cb@DAT_001bcd7c) (whoosh SFX)
    }

    if (m_Timer >= m_Lifetime) {
        // ===== main explosion timeline (Timer past Lifetime) =====

        // (a) one-shot at crossing Lifetime: snapshot camera target + zoom
        if (m_PrevTimer < m_Lifetime) {
            if (m_pHostFruit) {
                m_WorkVec5 = m_pHostFruit->pos;     // explosion centre (+0xf0)
            }
            // TODO: 0x001bca10 -- FruitCamera::TransitionOut(game+0x4c) (needs camera addr fix)
            StopAllFruit();
            // TODO: 0x001bca10 -- UnpauseSlices (function not yet ported)
            if (m_pLinkedSlasher) {
                // TODO: 0x001bca10 -- clear *(int*)(m_pLinkedSlasher+0x7c) = 0 (SlashEntity field +0x7c)
            }
            // zoom target = host pos clamped x in [-204,204], y in [-128,128]
            if (m_pHostFruit) {
                Vec3 hp = m_pHostFruit->pos;
                float zx = hp.x;
                if (zx < -204.0f) zx = -204.0f;        // DAT_001bcd68
                else if (zx >= 204.0f) zx = 204.0f;    // DAT_001bcd6c
                float zy = hp.y;
                if (zy < -128.0f) zy = -128.0f;        // DAT_001bcdc0
                else if (zy >= 128.0f) zy = 128.0f;    // DAT_001bcd70
                m_WorkVec6 = Vec3(zx, zy, 0.0f);       // +0xfc; DAT_001bcdac = 0.0
            }
        }

        // (b) while PrevTimer < Lifetime+0.5: refresh centre; on crossing fire the bang
        if (m_PrevTimer < m_Lifetime + 0.5f) {
            if (m_pHostFruit) {
                m_WorkVec5 = m_pHostFruit->pos;         // refresh explosion centre
            }
            if (m_Timer >= m_Lifetime + 0.5f) {
                // one-shot: the actual blast
                // TODO: 0x001bca10 -- FruitCamera::CreateCameraShake(game+0x4c, mag=1.0, dur=2.0, pos) (needs camera)
                ExplodeSuperFruit();
                SpawnJibs(0);
                StopRays();
                // TODO: 0x001bca10 -- ChangeText(this, sprintf(DAT_001bcd84, m_SliceCount), 0, &m_WorkVec3) (needs FancyBakedString)
            }
        }

        // (c) after blast (Timer >= Lifetime+0.5): explosion update + late shake + time un-slow
        if (m_Timer >= m_Lifetime + 0.5f) {
            m_RecycleFlag = 1;    // binary +0x34 = 1 (draw-layer/state flag)
            UpdateExplosion(dt);
            float tLateShake = m_Lifetime + 0.5f + 0.35f + 0.4f;  // DAT_001bcd9c=0.35, DAT_001bcd90=0.4
            if (m_PrevTimer < tLateShake && tLateShake <= m_Timer) {
                // TODO: 0x001bca10 -- FruitCamera::CreateCameraShake(game+0x4c, mag=1.6, dur=2.0, pos) (DAT_001bcd94=1.6)
            }
            // ease global time-scale back toward 1.0: ts = (ts-1)*pow(0.75, dt*60) + 1
            // TODO: 0x001bca10 -- *(game_work+0x40)->+0x24 time-scale ease (game_work._pad_0x40 unresolved)
        }

        // (d) score payoff window: Lifetime+0.5+0.35+0.55+0.1
        float tScore = m_Lifetime + 0.5f + 0.35f + 0.55f + 0.1f;  // DAT_001bcd98=0.1
        if (m_PrevTimer < tScore && tScore <= m_Timer) {
            if (game_work.gameMode == 0) {
                // persist stat (gameMode 0 only)
                uint32_t statHash = StringHash("super_slices");
                if (game_work.m_SaveData) {
                    game_work.m_SaveData->AddToTotal("super_slices", statHash, m_SliceCount, false, false);
                }
            }
            FN::AddToCurrentScore(m_SliceCount, 0, false, true);  // flag4=true; raw m_SliceCount
        }

        // (e) kill host fruit window: Lifetime+0.5+0.35+0.55+0.65+0.25+0.55
        float tKill = m_Lifetime + 0.5f + 0.35f + 0.55f + 0.65f + 0.25f + 0.55f;
        if (m_Timer > tKill) {
            m_RecycleFlag = 0x80;   // binary +0x34 = 0x80 (post-blast draw-layer marker)
            if (m_pHostFruit && m_PrevTimer <= tKill) {
                m_pHostFruit->KillFruit(false);     // Fruit::KillFruit(hostFruit, 0)
                m_pHostFruit = nullptr;             // +0x7c = 0
            }
        }

        // (f) finale teardown: Lifetime+0.5+0.35+0.55+0.65+0.25+modeBias+0.15
        float modeBias2 = (game_work.gameMode == 2) ? 1.5f : 0.5f;
        float tEnd = m_Lifetime + 0.5f + 0.35f + 0.55f + 0.65f + 0.25f + modeBias2 + 0.15f;  // DAT_001bcda8=0.15
        if (m_Timer > tEnd) {
            WaveManager::GetInstance()->m_SpeedScale = 1.0f;  // SetAbsoluteDtMod(1.0)
            // TODO: 0x001bca10 -- *(int*)WaveManager::GetInstance() = 0 (wave-active flag +0x00)
            WaveManager::GetInstance()->GetNextWave(0);
            PSPParticleManager::GetInstance().m_GlobalTimeMod   = 0.0f;
            PSPParticleManager::GetInstance().m_GlobalTimeScale = 1.0f;
            // TODO: 0x001bca10 -- *(game_work+0x40)->+0x24 = 1.0 time-scale restore (game_work._pad_0x40 unresolved)
            flags |= ENT_KILLED;                    // this->done(+0x33) = 1
        }
    } else {
        // ===== Timer still < Lifetime: throw/anticipation phase =====

        if (m_pHostFruit) {
            // host-fruit gravity-based spin angle write (Fruit+0x98), branchy
            // TODO: 0x001bca10 -- host-fruit spin(+0x98) = T_1616(...) write (Fruit field +0x98 not named in port)
            PushBombsAway(dt);
        }

        // global time-scale pre-roll: ts = 0.0 + ts*pow(0.75, dt*60)
        // TODO: 0x001bca10 -- *(game_work+0x40)->+0x24 *= pow(0.75, dt*60) (DAT_001bd488=0.0 double; game_work._pad_0x40 unresolved)

        // build the spin-orbit camera transition for the thrown fruit
        // TODO: 0x001bca10 -- FruitCamera::Transition(game+0x4c, dist, angle, target, doneCb@DAT_001bd4ac) (needs camera + Math::SinIdx/CosIdx)

        // recompute pos from host + scaled dir
        // TODO: 0x001bca10 -- pos(+0x08) orbit recompute from host + dirXY*320*0.25*0.625 (DAT_001bd49c=320.0)
    }

    // tail: LAB_001bd0ac -- runs every frame
    // ray-entity scaling: for i in 0..2
    if (m_Timer < m_Lifetime + 0.5f) {
        // TODO: 0x001bca10 -- ray-entity scale update: *(float*)(rg+0x14+i*4) *= T_1629(0.3, prog) for i=0..2 (needs ray-entity subsystem; DAT_001bd4a0=0.3)
    }

    // fade-in accumulator: += dt*3, clamp 1
    if (m_FadeIn < 1.0f) {
        float v = m_FadeIn + dt * 3.0f;
        if (v >= 1.0f) v = 1.0f;
        m_FadeIn = v;
    }

    // slow-transition accumulator: += dt/0.175, clamp 1  (DAT_001bd4a4=0.175)
    if (m_Scale < 1.0f) {
        float v = m_Scale + dt / 0.175f;
        if (v > 1.0f) v = 1.0f;
        m_Scale = v;
    }

    m_PrevTimer = m_Timer;  // commit edge tracker (+0x8c = +0x88)
}

// TODO: 0x001bd7c8 -- DrawOrder ray/explosion VFX (needs ray-entity + explosion subsystem)
void SuperFruitControl::Draw(Renderer& /*r*/)
{
}

void SuperFruitControl::PostUpdate(float /*dt*/)
{
}

// Binary @ 0x001bb994. Per-hit combo response.
// Bumps m_SliceCount / m_HitCount, applies cooldown, accrues score.
// TODO: 0x001bb994 -- slash particles, combo-pitch SFX, FancyBakedString popup not yet ported
void SuperFruitControl::Sliced(Mortar::Entity* slashEntity)
{
    if (m_SliceCooldown > 0) return;

    ++m_SliceCount;
    m_HitCount += 1.0f;
    m_SliceCooldown = 6;  // TODO: 0x001bb994 -- resolve cooldown value from binary DAT

    // Null out linked slasher (binary @ 0x001bb994 nulls the stored SlashEntity).
    if (slashEntity) {
        m_pLinkedSlasher = nullptr;
    }

    LOG_INFO("SUPERFRUIT", "Sliced() hit %d", m_SliceCount);
}

// Binary @ 0x001baa20. Finale VFX: 10 or 25 radial jibs, 8 lettered fragments,
// white screen flash, SFXPlay vol=2.0.
// DAT constants: 0x1bae50=0.2f, 0x1bae54=0.0f, 0x1bae58=0.3f,
//                0x1bae5c=1000.0f (T_1628 hi), 0x1bae60=600.0f (T_1628 lo),
//                0x1bae64=700.0f (fragment angular-vel).
void SuperFruitControl::ExplodeSuperFruit()
{
    Fruit* host = m_pHostFruit;
    if (!host) {
        s_SuperFruitActive = 0;
        return;
    }

    // Build rotation basis from host's orientation quaternion (host+0xe0 = m_Rot1).
    // Binary: _Quaternion::Matrix33(host+0xe0) -> rot
    Matrix44 rot = host->m_Rot1.ToMatrix44();

    // host+0xc4 = m_SliceArcImpulse: base launch-speed scalar
    float baseSpeed = host->m_SliceArcImpulse;   // +0xC4

    // ---- (A) radial juice-splat jibs (DAT_001bae50=0.2f, 0x1bae58=0.3f) ----
    // N = IsFastHardware() ? 25 : 10  (binary: 0x19 : 0x0a)
    int N;
    {
        Game* g = Game::GetInstance();
        N = (g && g->IsFastHardware()) ? 25 : 10;
    }
    Math::Random& rng = WaveManager::GetInstance()->GetRandom();
    uint8_t hostFruitType = host->m_FruitType;
    Vec3 hostPos = host->pos;       // host+0x10

    for (int i = 0; i < N; ++i) {
        uint16_t angIdx = (uint16_t)(rng.Rand32(0xfff0) & 0xffff);
        // T_1607(0.5) = signed-random in [-0.5, +0.5]: RandF(1.0) - 0.5
        float t1607 = Math::g_Random.RandF(1.0f) - 0.5f;
        float spd = (baseSpeed + t1607 * baseSpeed) * ((float)i * 0.2f + 5.0f);  // DAT_001bae50=0.2

        SplatEntity* s = SplatEntity::GetFree();
        if (s) {
            Vec3 vel(SinIdx(angIdx) * spd, CosIdx(angIdx) * spd, 0.0f);  // DAT_001bae54=0.0
            s->MakeSplat(hostPos, vel, /*param3=*/false, (int)hostFruitType, /*landImmediately=*/true);

            // taper splat life: clamp(1 - (i-2)/N, 0.3, 1.0)
            float taper = 1.0f - (float)(i - 2) / (float)N;
            if (taper < 0.3f) taper = 0.3f;   // DAT_001bae58=0.3
            if (taper > 1.0f) taper = 1.0f;
            // DIFFERS: binary writes raw SplatEntity+0x64; port layout: m_Vel is +0x5C,
            //   so +0x64 = m_Vel.z (last component). Binary scales the landed vel tail by taper.
            s->m_Vel.z *= taper;
        }
    }

    // ---- (B) white critical screen-flash ----
    FN::CriticalFlash(hostPos, Colour(255, 255, 255, 255));

    // ---- (C) explosion SFX ----
    // TODO: 0x001baa20 -- SFXPlay(game+0x18c, DAT_001bae78 SFX key, pitch=0.125, vol=2.0, cb@DAT_001bae74) (SFX key/cb unresolved)

    // ---- (D) 8 lettered mesh fragments (cube-corner pattern) ----
    // fmt string @ 0x0028383a: "models/Fruit/%s_%c_piece_%d.mmd"
    // DAT_001bae64=700.0f (angular-vel scalar), 0x1bae60=600.0f (lo), 0x1bae5c=1000.0f (hi)
    const FruitInfo* fi = Fruit::FruitInfo((long)hostFruitType);
    const char* modelName = fi ? fi->m_ModelName : "";

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    Mortar::MeshManager* mm = Mortar::MeshManager::GetInstance();

    for (uint32_t k = 0; k < 8; ++k) {
        // cube-corner pattern from low 3 bits of k:
        //   bit1: corner.x = (k&2) ? -1 : +1
        //   bit0: corner.y = (k&1) ? +1 : -1
        //   k<4:  corner.z = +1; k>=4: corner.z = -1
        Vec3 corner;
        corner.x = (k & 2u) ? -1.0f : 1.0f;
        corner.y = (k & 1u) ?  1.0f : -1.0f;
        corner.z = (k + 3 < 7) ? 1.0f : -1.0f;
        corner.Normalise();

        // dir = rot.MultVec33(corner) -- multiply upper-left 3x3 of Matrix44 by corner
        Vec3 dir;
        dir.x = rot.m[0] * corner.x + rot.m[4] * corner.y + rot.m[8]  * corner.z;
        dir.y = rot.m[1] * corner.x + rot.m[5] * corner.y + rot.m[9]  * corner.z;
        dir.z = rot.m[2] * corner.x + rot.m[6] * corner.y + rot.m[10] * corner.z;

        // spawn jib actor (entity type 5 = Jiblet)
        Mortar::Entity* jibEnt = am ? am->Add(5) : 0;
        if (!jibEnt) continue;
        Jiblet* jiblet = static_cast<Jiblet*>(jibEnt);

        // dirN: dir with z zeroed, then normalised (planar velocity direction)
        Vec3 dirN = dir;
        dirN.z = 0.0f;   // DAT_001bae54=0.0
        dirN.Normalise();

        // build model name: "models/Fruit/%s_%c_piece_%d.mmd"
        char name[128];
        snprintf(name, sizeof(name), "models/Fruit/%s_%c_piece_%d.mmd",
                 modelName,
                 (modelName[0] != '\0') ? (int)(unsigned char)modelName[0] : 0,
                 (int)(k + 1));

        // linear velocity = dirN * T_1628(600, 1000)  (lo=DAT_001bae60, hi=DAT_001bae5c)
        float linSpeed = 600.0f + Math::g_Random.RandF(400.0f);   // uniform [600, 1000)
        Vec3 vel = dirN * linSpeed;

        // load model
        Mortar::SmartPtr<Mortar::Model> mdl;
        if (mm) {
            mdl = mm->Load(name);
        }

        // angular velocity: dirN * 700.0  (DAT_001bae64=700.0f)
        Vec3 angVel = dirN * 700.0f;

        // Jiblet::Init(gravScale=1.0, fadeRate=0.0, fruitType, pos=host+0x74, vel, mdl, emitterHash=0, gravBase=angVel)
        // DIFFERS: binary passes host+0x74 as Vec3* (raw offset into Fruit; port field at +0x74 is
        //   m_SpawnDelay (float), not a Vec3). Binary reuses the 12 bytes starting at +0x74 as a
        //   Vec3 spawn-position cache written by Slice/CollisionResponse. Port casts raw.
        Vec3* hostJibPos = reinterpret_cast<Vec3*>(reinterpret_cast<uint8_t*>(host) + 0x74);
        jiblet->Init(1.0f, 0.0f, (int)hostFruitType, hostJibPos, &vel, mdl, 0, &angVel);

        // post-init writes: copy host transform onto the jib actor
        jiblet->m_Age = 0.0f;                // jib+0xac = 0 (reset age set by Init to -0.04)
        jiblet->scale = host->scale;         // jib+0x28 = host scale (+0x28)
        jiblet->m_Rotation = rot;            // jib+0x4c = rot (64-byte Matrix44 copy)
    }

    // ---- (E) restore host scale ----
    // TODO: 0x001baa20 -- host->scale = *(Vec3*)DAT_001bae80 (GOT Vec3 restore; value unresolved from source)

    s_SuperFruitActive = 0;
}

// Binary @ 0x001b9850. Clears m_pLinkedSlasher if it matches se.
void SuperFruitControl::ComboCancel(SlashEntity* se)
{
    if (m_pLinkedSlasher == se) {
        m_pLinkedSlasher = nullptr;
    }
}

// Binary @ 0x001b9c6c -- SuperFruitControl::PushBombsAway(float).
// Radially shove every nearby bomb (type-list 1) away from the host fruit.
void SuperFruitControl::PushBombsAway(float dt)
{
    float k = dt * WaveManager::GetInstance()->GetWavedt(0);
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    std::list<Mortar::Entity*>::iterator it;
    for (Mortar::Entity* b = am->GetEntityFirst(1, it); b; b = am->GetEntityNext(1, it)) {
        Vec3 dir = b->pos - m_pHostFruit->pos;       // outward radial
        float dist = dir.Normalise();                // returns old magnitude
        if (dist < 200.0f) {                         // DAT_001b9d5c
            b->vel += (dir * 100.0f) * k;            // DAT_001b9d58=100.0; vel @ +0x1c
        }
    }
}

// Attach a SuperFruitGlow entity to the host fruit via ActorManager.
void SuperFruitControl::AttachGlow()
{
    // TODO: 0x001c06bc -- wire SuperFruitGlow through ActorManager pool when
    //   Entity pool allocation for type-6 entities is supported.
    // For now: create on heap and track via m_pGlow pointer.
    m_pGlow = new SuperFruitGlow(m_pHostFruit);
}

// -----------------------------------------------------------------------
// Static interface
// -----------------------------------------------------------------------

// Binary @ 0x001bbf48. Called when ANY fruit is thrown.
// Gate: FruitInfo[type].m_bIsSuperFruit != 0 && !(fruit->flags & 0x10).
void SuperFruitControl::SuperFruitThrown(Fruit* fruit)
{
    if (!fruit) return;
    // Already killed
    if (fruit->flags & ENT_KILLED) return;

    const FruitInfo* info = Fruit::FruitInfo((long)fruit->m_FruitType);
    if (!info) return;

    // Gate: m_bIsSuperFruit flag at FRUIT_INFO+0x330
    if (!info->m_bIsSuperFruit) return;

    // Already registered
    if (SuperFruitControls.count(fruit)) return;

    LOG_INFO("SUPERFRUIT", "SuperFruitThrown() spawning controller for fruit=%p", static_cast<void*>(fruit));

    // Binary @ 0x001bbf48: scripted slow-arc override written onto the thrown
    // super-fruit. m_Gravity.x is always -5.0; the pos/vel/(gravity.y,gravity.z)
    // preset is selected by game_work.gameMode (==2 -> Arcade, else default).
    // Then a 51% chance (Rand32(100) < 51) mirrors the arc horizontally by
    // negating m_Gravity.y, pos.x and vel.x.
    fruit->m_Gravity.x = -5.0f;                          // [fruit+0xa0] = 0xc0a00000
    if (game_work.gameMode == 2) {                       // GAME_MODE_ARCADE; byte at game_work+0x04
        fruit->pos = Vec3(-35.0f, -260.0f, 0.0f);        // DAT_001bc104/0bc108/0bc10c
        fruit->vel = Vec3(0.5f, 8.5f, 0.0f);             // 0x3f000000, 0x41080000, DAT_001bc10c
        fruit->m_Gravity.z = -7.5f;                      // [fruit+0xa8] = 0xc0f00000
        fruit->m_Gravity.y = 0.0f;                       // [fruit+0xa4] = DAT_001bc10c
    } else {
        fruit->pos = Vec3(-340.0f, -100.0f, 0.0f);       // DAT_001bc110/0bc114/0bc10c
        fruit->vel = Vec3(5.0f, 5.0f, 0.0f);             // 0x40a00000, 0x40a00000, DAT_001bc10c
        fruit->m_Gravity.z = -4.5f;                      // [fruit+0xa8] = 0xc0900000
        fruit->m_Gravity.y = 0.01f;                      // [fruit+0xa4] = DAT_001bc118
    }
    // 51% chance: mirror the arc across the screen centreline.
    if (WaveManager::GetInstance()->GetRandom().Rand32(100) < 51) {  // cmp #0x32 / bhi
        fruit->m_Gravity.y = -fruit->m_Gravity.y;        // [fruit+0xa4]
        fruit->pos.x       = -fruit->pos.x;              // [fruit+0x10]
        fruit->vel.x       = -fruit->vel.x;              // [fruit+0x1c]
    }

    // TODO: 0x001bbf48 -- SuperFruitThrown SFX not yet ported

    SuperFruitControl* ctrl = new SuperFruitControl(fruit);
    SuperFruitControls[fruit] = ctrl;
}

// Binary @ 0x001be630. Slice dispatch: lookup map, forward or create.
void SuperFruitControl::SuperFruitSliced(Fruit* fruit, int /*idx*/, Mortar::Entity* slashEntity)
{
    if (!fruit) return;
    const FruitInfo* info = Fruit::FruitInfo((long)fruit->m_FruitType);
    if (!info || !info->m_bIsSuperFruit) return;

    std::map<Fruit*, SuperFruitControl*>::iterator it = SuperFruitControls.find(fruit);
    if (it != SuperFruitControls.end() && it->second) {
        it->second->Sliced(slashEntity);
    } else {
        // First hit: create controller (binary allocates 0x108 bytes here)
        SuperFruitControl* ctrl = new SuperFruitControl(fruit);
        ctrl->Sliced(slashEntity);
        SuperFruitControls[fruit] = ctrl;
    }
}

// Binary @ 0x001b9828. Returns true while a super fruit is active.
// Binary reads game+0x14; port uses shadow flag.
bool SuperFruitControl::IsInSuperFruitState()
{
    return s_SuperFruitActive != 0;
}

// Binary @ 0x001b98c0.
int SuperFruitControl::NumPomegranatesSpawnedThisGame()
{
    return s_PomegranatesSpawnedThisGame;
}

// Binary @ 0x001b99d4. Game-mode gating for final pomegranate spawn.
// TODO: 0x001b99d4 -- gate needs PowerUpManager::GetActiveProgression + Fruit::NumberOfPowerupFruits
bool SuperFruitControl::CanSpawnFinalPomegranate()
{
    return false;
}

// Binary @ 0x001b98f4. Spawns the terminal "super pomegranate" wave finale:
// two random decoy fruits chucked almost immediately (delay 0.01s), bumps the
// "super_pomegranates_spawned" save-stat, then chucks the actual super pomegranate
// (delay 0.1s). Returns true (binary returns CONCAT44(undef,1)).
//
// Binary call shape (WaveManager::SpawnFruit @ 0x00124298 returns the spawned
// Entity*, then Fruit::Chuck(delay, entity) overrides the spawner's default
// 0.21s chuck delay with the tighter finale delay). DAT constants resolved:
//   DAT_001b99bc = 0.01f  (decoy chuck delay)
//   DAT_001b99c0 = 0.1f   (super pomegranate chuck delay)
//   save-stat key string @ 0x002837d4 = "super_pomegranates_spawned"
//   fruit-type name string @ 0x002837ef = "super_pomegranate"
//   FruitSaveData = game_work.m_SaveData (binary: *(*(GameWork_glob)+0x50)).
bool SuperFruitControl::SpawnFinalPomegranate()
{
    // Two random decoy fruits, chucked near-instantly.
    Mortar::Entity* e0 = WaveManager::GetInstance()->SpawnFruit(1, -1, NULL, 0);
    if (e0) static_cast<Fruit*>(e0)->Chuck(0.01f);   // DAT_001b99bc

    Mortar::Entity* e1 = WaveManager::GetInstance()->SpawnFruit(1, -1, NULL, 0);
    if (e1) static_cast<Fruit*>(e1)->Chuck(0.01f);   // DAT_001b99bc

    // Increment the persistent "super_pomegranates_spawned" stat.
    const char* kStatKey = "super_pomegranates_spawned";
    uint32_t statHash = StringHash(kStatKey);
    if (game_work.m_SaveData) {
        game_work.m_SaveData->AddToTotal(kStatKey, statHash, 1, false, false);
    }

    // The actual super pomegranate, chucked slightly later.
    int superType = Fruit::FruitType("super_pomegranate", false);
    Mortar::Entity* e2 = WaveManager::GetInstance()->SpawnFruit(1, superType, NULL, 0);
    if (e2) static_cast<Fruit*>(e2)->Chuck(0.1f);    // DAT_001b99c0

    return true;
}

// Binary @ 0x001ba73c. Serializes active super-fruit state to XML.
void SuperFruitControl::SaveSuperFruitState(TiXmlElement* parent)
{
    if (!parent) return;
    if (SuperFruitControls.empty()) return;

    // Serialize first active controller (binary stores at most one active at a time)
    std::map<Fruit*, SuperFruitControl*>::iterator it = SuperFruitControls.begin();
    if (it == SuperFruitControls.end() || !it->second) return;

    SuperFruitControl* ctrl = it->second;

    tinyxml2::XMLDocument* doc = parent->GetDocument();
    if (!doc) return;
    tinyxml2::XMLElement* elem = doc->NewElement("superFruit");
    if (!elem) return;

    SuperFruitState state;
    state.m_Timer      = ctrl->m_Timer;
    state.m_Lifetime   = ctrl->m_Lifetime;
    state.m_SliceCount = ctrl->m_SliceCount;
    // Binary @ 0x001ba73c reads ctrl+0x2c -> serialized as XML attr "rot".
    // ctrl+0x2c is the controller's Entity-base scale.y (Entity::scale is the
    // Vec3 at +0x28; .y component sits at +0x2c). The binary repurposes the
    // controller's own scale.y as the saved spin/rotation value.
    state.m_Spin       = ctrl->scale.y;
    {
        TiXmlElement welem(elem);
        state.WriteToElement(&welem);
    }

    parent->InsertEndChild(elem);
}

// Binary @ 0x001bb52c. Global time-scale restore + type-6 ENT_KILLED walk +
// WaveManager/PSPParticleManager reset.
void SuperFruitControl::Reset()
{
    WaveManager::GetInstance()->m_SpeedScale = 1.0f;   // SetAbsoluteDtMod(1.0)
    // TODO: 0x001bb52c -- *(int*)WaveManager::GetInstance() = 0 (wave-active +0x00; private)
    PSPParticleManager::GetInstance().m_GlobalTimeMod   = 0.0f;
    PSPParticleManager::GetInstance().m_GlobalTimeScale = 1.0f;
    // TODO: 0x001bb52c -- *(game_work+0x40)->+0x24 = 1.0 global time-scale restore (game_work._pad_0x40 unresolved)
    // TODO: 0x001bb52c -- FruitCamera::TransitionOut(game+0x4c) (needs camera addr fix)
    // TODO: 0x001bb52c -- StackAllocatedPointer<Delegate0>::Delete((game+0x4c)+0x184) camera done-cb free
    // TODO: 0x001bb52c -- UnpauseSlices (function not yet ported)

    // Walk ActorManager type 6, OR 0x10 (ENT_KILLED) into each entity's flags(+0x0c)
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (am) {
        std::list<Mortar::Entity*>::iterator it;
        Mortar::Entity* e = am->GetEntityFirst(6, it);
        while (e != NULL) {
            e->flags |= ENT_KILLED;     // flags(+0x0c) |= 0x10
            e = am->GetEntityNext(6, it);
        }
    }

    SuperFruitControls.clear();

    // TODO: 0x001bb52c -- this->+0x33 = 1 (game-level done-flag write; target unresolved in port)

    s_SuperFruitActive = 0;
    s_PomegranatesSpawnedThisGame = 0;
}

// Binary @ 0x001ba460. Stops all in-flight fruit and bombs during the
// super-fruit explosion finale: clears any still-unspawned (chuck-delayed)
// fruits/bombs, then sweeps every live fruit (ActorManager type 0) and bomb
// (type 1), redirecting their velocity toward the explosion centre and
// freezing their physics. The explosion centre is this->m_WorkVec5 (+0xf0).
//
// Per-entity velocity redirect (both fruits and sliced-fruit halves and bombs):
//   dir    = Normalise(pos - centre)
//   newVel = (vel + dir * 5.0f) / 2.0f      // 5.0f = DAT (vmov 0x40a00000)
// (the /2.0f literal is the binary's local 2.0f operand to operator/).
//
// DAT constants (read from binary memory):
//   DAT_001ba6a4 = 0.0f (zeroed into the per-entity stop fields)
//   the copied Vec3 (GOT->0x0035f160) is _Vector3<float>::Zero == (0,0,0).
void SuperFruitControl::StopAllFruit()
{
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    // Explosion centre lives in this controller's work vector (+0xf0).
    const Vec3& centre = m_WorkVec5;

    // Remove fruits/bombs that haven't actually spawned yet (still in
    // chuck-delay) before redirecting the rest. Binary calls both here.
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();

    // -------- type 0: fruits --------
    std::list<Mortar::Entity*>::iterator fit;
    Mortar::Entity* e = am->GetEntityFirst(0, fit);
    while (e != NULL) {
        Fruit* f = static_cast<Fruit*>(e);

        // Freeze fruit physics. These reproduce the binary's exact per-fruit
        // stop writes at Fruit+0x98 (float=0) and the zero-Vec3 at Fruit+0xa0,
        // plus the byte clear at Fruit+0x70.
        // Binary: vstr 0.0f -> [this,#0x98] (m_TimeScale); stm zero-Vec3 -> [this,#0xa0] (m_Gravity); strb 0 -> [this,#0x70] (m_bBallisticEnable).
        f->m_TimeScale = 0.0f;          // Fruit+0x98 = DAT_001ba6a4 (0.0f)
        f->m_Gravity = Vec3(0.0f, 0.0f, 0.0f);  // Fruit+0xa0..0xab zero-Vec3 copy
        f->m_bBallisticEnable = 0;      // Fruit+0x70 (strb 0)

        // Only sliced fruits get their two half-bodies redirected.
        if (f->Sliced()) {
            // First body: pos +0x10 -> vel +0x1c.
            Vec3 dir = f->pos - centre;
            dir.Normalise();
            dir *= 5.0f;
            f->vel = (f->vel + dir) / 2.0f;

            // Second body: pos +0xc8 (m_SecondPos region) -> vel +0xd4.
            // DIFFERS: binary reads Fruit+0xc8 and writes Fruit+0xd4; the port's
            // named second-body fields sit at +0xb8/+0xc4, so this redirect uses
            // the same raw +0x10 offset relationship the binary uses (pos->vel).
            Vec3 dir2 = f->m_SecondPos - centre;
            dir2.Normalise();
            dir2 *= 5.0f;
            f->m_SecondVel = (f->m_SecondVel + dir2) / 2.0f;
        }

        e = am->GetEntityNext(0, fit);
    }

    // -------- type 1: bombs --------
    e = am->GetEntityFirst(1, fit);
    while (e != NULL) {
        Bomb* b = static_cast<Bomb*>(e);

        // Redirect bomb velocity toward the explosion centre (pos +0x10 -> vel +0x1c).
        Vec3 dir = b->pos - centre;
        dir.Normalise();
        dir *= 5.0f;
        b->vel = (b->vel + dir) / 2.0f;

        // Freeze bomb physics (binary writes at Bomb+0x8c / +0xa8 / +0x80).
        b->m_AccelForce = Vec3(0.0f, 0.0f, 0.0f);  // Bomb+0x8c zero-Vec3
        b->m_SpeedMult = 0.0f;                      // Bomb+0xa8 = DAT_001ba6a4 (0.0f)
        b->m_bMovement = 0;                         // Bomb+0x80 (strb 0)

        e = am->GetEntityNext(1, fit);
    }
}

// Binary @ 0x1baeb8. Per-frame shockwave during the SuperFruit explosion.
// Writes PSPParticleManager globals, then radially pushes Actor types 0/1/5.
//
// Spec (from RE):
//   mgr+0x08 (m_GlobalOrigin)  = this+0xf0 (m_WorkVec5, explosion epicenter)
//   mgr+0x00 (m_GlobalTimeMod) = this+0xe8 (inner-radius field) * DAT_001bb27c
//   mgr+0x04 (m_GlobalTimeScale) = T_1616(time, ...) ramp
//   then push types 0/1/5 out by (outerR - dist)*dt*5 from m_GlobalOrigin.
//
// +0xe8 / +0xec (inner/outer radii), the T_1616 ramp, and the per-actor slice
// path require further RE. Faithfully write the globals we have; leave the
// radial-push and ramp computations as TODO until the remaining fields are named.
// TODO: 0x1baeb8 — compute inner/outer radii from T_1616 ramps and this+0xe8/+0xec;
//   DAT_001bb27c scalar not yet read; T_1616 time arg not resolved.
//   Radial push of types 0/1/5 and slice-on-innerR not yet ported.
void SuperFruitControl::UpdateExplosion(float /*dt*/)
{
    PSPParticleManager& mgr = PSPParticleManager::GetInstance();

    // Write explosion epicenter into manager global.
    mgr.m_GlobalOrigin = m_WorkVec5;    // this+0xf0 -> mgr+0x08

    // TODO: 0x1baeb8 — mgr.m_GlobalTimeMod = innerRadius(+0xe8) * DAT_001bb27c
    // TODO: 0x1baeb8 — mgr.m_GlobalTimeScale = T_1616(time, ...) ramp value
    // TODO: 0x1baeb8 — radial push: types 0 (Fruit), 1 (Bomb), 5 (Jiblet/SplatEntity)
    //   out by (outerR - dist)*dt*5 from m_GlobalOrigin; slice fruit in inner ring
}

// Binary @ 0x1b9b4c. Iterates ActorManager type-6 entities and sets their
// +0xe0 field to 1 (stop flag). "Rays" are type-6 Entity actors spawned by the
// SuperFruit intro sequence; they are NOT PSPParticles.
void SuperFruitControl::StopRays()
{
    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    if (!am) return;

    std::list<Mortar::Entity*>::iterator it;
    Mortar::Entity* e = am->GetEntityFirst(6, it);
    while (e != NULL) {
        // entity+0xe0 = 1: stop flag for ray entities.
        // DIFFERS: accessing via raw byte pointer because the ray-entity class
        // is not yet ported and its +0xe0 field is not named; the binary writes
        // exactly one byte at entity+0xe0. Once the ray entity is ported, replace
        // with the proper field access.
        uint8_t* rawBase = reinterpret_cast<uint8_t*>(e);
        rawBase[0xe0] = 1;
        e = am->GetEntityNext(6, it);
    }
}

// Binary @ 0x1bc748. PSPParticleManager emitter hookup for jib particle trails.
// Builds the emitter name via sprintf, calls EmitterExists/AddEmitter, sets
// m_Pos/m_DirCos/m_DirSin/m_bTrailStarted on the allocated emitter.
// Also spawns 8 Jiblet mesh actors via ActorManager (type 5, Jiblet::Init) --
// Jiblet/MeshManager not yet ported; only the PSPParticleManager hookup is
// implemented here.
void SuperFruitControl::SpawnJibs(int /*count*/)
{
    PSPParticleManager& mgr = PSPParticleManager::GetInstance();

    if (m_pHostFruit) {
        // Build emitter name from fruit type (binary: sprintf("jib_emitter_%d", fruitType)).
        // TODO: 0x1bc748 — confirm exact format string from binary string table.
        char buf[64];
        snprintf(buf, sizeof(buf), "jib_emitter_%d", (int)m_pHostFruit->m_FruitType);
        uint32_t hash = StringHash(buf);

        if (mgr.EmitterExists(hash)) {
            PSPParticleEmitter* e = mgr.AddEmitter(hash, 0, false);
            if (e) {
                e->m_bTrailStarted = 1;
                e->m_Pos = m_WorkVec5;  // explosion world pos (+0xf0)
                // TODO: 0x1bc748 — angle = *(uint16_t*)(m_pHostFruit + 0xc0);
                //   binary writes: e->m_DirCos = Math::CosIdx(angle);
                //                  e->m_DirSin = Math::SinIdx(angle);
                //   fruit+0xc0 field identity unresolved (overlaps m_SecondPos.z in port layout).
                e->m_DirCos = 1.0f;
                e->m_DirSin = 0.0f;
            }
        }
    }

    // TODO: 0x1bc748 — spawn 8 Jiblet mesh actors (ActorManager::Add type 5, Jiblet::Init).
    //   Needs Jiblet and MeshManager ported.
}
