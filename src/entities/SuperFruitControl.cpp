// SuperFruitControl — super-fruit (pomegranate/starfruit frenzy) state machine.
// Binary: ctor @ 0x001be1c8, restore-from-save ctor @ 0x001bea90,
//         Update @ 0x001bca10, Sliced @ 0x001bb994, ExplodeSuperFruit @ 0x001baa20,
//         SuperFruitThrown @ 0x001bbf48, SuperFruitSliced @ 0x001be630,
//         IsInSuperFruitState @ 0x001b9828, Reset @ 0x001bb52c, Release @ 0x001bb664,
//         StopAllFruit @ 0x001ba460, SaveSuperFruitState @ 0x001ba73c,
//         ComboCancel @ 0x001b9850.

#include "SuperFruitControl.h"
#include "SuperFruitGlow.h"
#include "hud/HUD.h"
#include "engine/render/FancyBakedString.h"
#include "engine/render/MatrixManager.h"
#include "engine/asset/Texture.h"
#include "engine/asset/TextureManager.h"
#include "engine/asset/Mesh.h"
#include "math/Vec2.h"
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
#include "game/GameMode.h"
#include "game/GameOver.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "game/FruitCamera.h"
#include "game/BombHit.h"
#include "Game.h"
#include "engine/particle/PSPParticleManager.h"
#include "math/MathUtil.h"
#include "math/Random.h"
#include "engine/util/Transition.h"
#include "engine/asset/MeshManager.h"
#include "util/StringHash.h"
#include "debug/Logger.h"
#include "hud/SliceEffect.h"
#include <map>

#include <cstring>
#include <cstdio>
#include <cmath>

// Static map definition (24-byte std::map per CLAUDE.md).
std::map<Fruit*, SuperFruitControl*> SuperFruitControl::SuperFruitControls;

// Finale visuals loaded by LoadContent @0x001bda74 (file-static BSS SmartPtrs in
// the binary). ShockWaveTexture = "explosion_radius.tex" ring sprite;
// JibletModel = pomegranate jiblet mesh flung on explode.
static Mortar::SmartPtr<Mortar::Texture> ShockWaveTexture;
static Mortar::SmartPtr<Mortar::Model>   JibletModel;

// ---- ChangeText colour-morph helpers -----------------------------------------
// The finale text fill (colourA) and stroke (colourB) morph across three keys as
// the combo count grows (t = clamp(m_SliceCount/35, 0, 1)); key0 at t=0, key1 at
// t=0.5, key2 at t=1.
static inline Colour SuperFruitLerpColour(Colour a, Colour b, float t) {
    return Colour(
        (uint8_t)((float)a.r + ((float)b.r - (float)a.r) * t),
        (uint8_t)((float)a.g + ((float)b.g - (float)a.g) * t),
        (uint8_t)((float)a.b + ((float)b.b - (float)a.b) * t),
        (uint8_t)((float)a.a + ((float)b.a - (float)a.a) * t));
}
static inline Colour SuperFruitColourMorph3(Colour k0, Colour k1, Colour k2, float t) {
    if (t < 0.5f) return SuperFruitLerpColour(k0, k1, t * 2.0f);
    return SuperFruitLerpColour(k1, k2, (t - 0.5f) * 2.0f);
}

// ---- Finale ramp/lerp helpers (binary inlined templates T_1616 / T_1629) -----
// T_1616(v,a,b): clamp-ramp -- (v-a)/(b-a) clamped to [0,1]; a==b degenerates to
//   a step (0 below a, 1 at/above a). Works for both increasing (a<b) and
//   decreasing (a>b) ranges since it is the raw normalised-lerp formula.
static inline float T_1616(float v, float a, float b) {
    if (a == b) return v < a ? 0.0f : 1.0f;
    float t = (v - a) / (b - a);
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    return t;
}

// T_1629(a,b): lerp from a toward 1.0 by b -> a + (1-a)*b.
static inline float T_1629(float a, float b) {
    return a + (1.0f - a) * b;
}

// signedRand(x): uniform float in [-x, +x).
static inline float SuperFruitSignedRand(float x) {
    return Math::g_Random.RandF(2.0f * x) - x;
}

// uniformRange(a,b): uniform float in [a, b).
static inline float SuperFruitUniform(float a, float b) {
    return a + Math::g_Random.RandF(b - a);
}

// Binary @ 0x001be1c8: fresh controller ctor. Born on the first slice
// (SuperFruitSliced); the glow halo is spawned separately at throw time
// (SuperFruitThrown), so this ctor does NOT attach a glow.
// ASM-spec v1.6.1 SuperFruitControl::SuperFruitControl(Fruit*) @0x001be1c8:
//   the fresh ctor IS the first slice hit. Rolls a randomised lifetime [2,3),
//   marks m_SliceCount=1, snaps the control fully visible (m_FadeIn=m_Scale=1),
//   primes the host fruit, closes the wave spawn-gate, clears any unspawned
//   fruit/bombs, and computes the throw-orbit spin + offset position.
SuperFruitControl::SuperFruitControl(Fruit* fruit)
    : m_pHostFruit(fruit)
    , m_pHostFruit2(fruit)
    , m_HitCount(0.0f)
    , m_Timer(-2.0f)      // binary inits -2.0 (throw/anticipation phase runs while <Lifetime)
    , m_PrevTimer(-2.0f)
    , m_SliceCount(1)     // ctor counts the first hit; SuperFruitSliced does NOT re-call Sliced()
    , m_pLinkedSlasher(nullptr)
    , m_pComboText(nullptr)
    , m_pScoreText(nullptr)
    , m_Lifetime(SuperFruitUniform(2.0f, 3.0f))  // uniform[2,3); scales the whole finale timeline
    , m_FadeIn(1.0f)
    , m_Scale(1.0f)
    , m_GlowCounter(0)
{
    m_LayerFlags = 0x80;  // +0x34; HUD::Draw skips the control without a matching layer bit

    m_InnerRadius = 0.0f;
    m_OuterRadius = 0.0f;
    memset(_pad_e0, 0, sizeof(_pad_e0));

    // TODO: v1.6.1 DAT_002d928c/9290 (SuperFruitControl @0x001be1c8) -- binary inits
    //   m_SpinAxis/m_TintA/B/Current with (0, DAT_002d928c, DAT_002d9290); the two DAT
    //   Y/Z tint/spin constants are unmapped, so X=0 and Y/Z left zero (do NOT guess).
    m_SpinAxis    = Vec3(0.0f, 0.0f, 0.0f);
    m_TintCurrent = Vec3(0.0f, 0.0f, 0.0f);
    m_TintA       = Vec3(0.0f, 0.0f, 0.0f);
    m_TintB       = Vec3(0.0f, 0.0f, 0.0f);
    m_ExplodeOrigin = Vec3(0.0f, 0.0f, 0.0f);
    m_ZoomTarget    = Vec3(0.0f, 0.0f, 0.0f);

    // Prime the host fruit.
    if (m_pHostFruit) {
        m_pHostFruit->m_SliceTimer = 1.0f;   // Fruit+0xBC
        m_pHostFruit->m_ZPosition  = 10.0f;  // Fruit+0x9c (binary m_EmitterDepth)
        // Shrink the host collision sphere (binary: colSphere.m_Radius *= 0.775).
        if (m_pHostFruit->m_Col) {
            static_cast<ColSphere*>(m_pHostFruit->m_Col)->radius *= 0.775f;
        }
    }

    // Close the wave spawn-suppression gate (WaveManager+0x00) for the duration of
    // super state. Finale end (Update tEnd) clears it back to 0.
#if defined(__bada__)
    *(uint8_t*)WaveManager::GetInstance() = 1;
#else
    WaveManager::GetInstance()->m_SpeedControl[0] = reinterpret_cast<HUDControl3d*>(1);
#endif

    // Clear any still-unspawned fruit/bombs and deactivate live bombs.
    Fruit::ClearUnspawned(false);
    Bomb::ClearUnspawned();
    Bomb::DeactivateAll();

    // Roll the throw-orbit spin: sign * (uniform[-10,10) + 15).
    float spin = (Math::g_Random.Rand32(2) ? 1.0f : -1.0f)
               * (SuperFruitSignedRand(10.0f) + 15.0f);
    HUDControl::m_Timer = -spin;   // base +0x2c

    // Offset the control position along the spin direction, out from the host.
    size = Vec3(0.5f, 0.5f, 0.5f);
    if (m_pHostFruit) {
        pos = Vec3(m_pHostFruit->pos.x, m_pHostFruit->pos.y, 0.0f);
        uint16_t idx = (uint16_t)(int)(spin * 182.0f);
        Vec3 dir(CosIdx(idx), SinIdx(idx), 0.0f);
        pos += dir * (320.0f * 0.4f);
    }

    // Binary ctor @0x001be1c8: registers ComboCancel delegate on ComboCanceledEvent.
    SlashEntity::OnComboCancelEvent() += Mortar::Delegate1<void, SlashEntity*>::Make(
        this, &SuperFruitControl::ComboCancel);

    // First-slice popup label.
    ChangeText("SLICE!", false, NULL);

    // TODO: v1.6.1 SuperFruitControl::SuperFruitControl @0x001be1c8 -- BLOCKED deps:
    //   FruitCamera::Transition(...) throw-orbit camera move; slice-SFX
    //   GameSound::SFXPlay(pitch 0.125). Wire when ported.
}

// ASM-spec v1.6.1 SuperFruitControl::SuperFruitControl(Fruit*,SuperFruitState&) @0x001bea90:
// self-registers SuperFruitControls[fruit]; m_Timer=m_PrevTimer=state.time;
// m_SliceCount=state.hits; m_Lifetime=state.sliceTime; base m_Timer(+0x2c)=state.rot;
// colSphere.m_Radius*=0.775; NO glow/camera/SFX.
SuperFruitControl::SuperFruitControl(Fruit* fruit, SuperFruitState& state)
    : m_pHostFruit(fruit)
    , m_pHostFruit2(fruit)
    , m_HitCount(0.0f)
    , m_Timer(state.m_Timer)
    , m_PrevTimer(state.m_Timer)
    , m_SliceCount(state.m_SliceCount)
    , m_pLinkedSlasher(nullptr)
    , m_pComboText(nullptr)
    , m_pScoreText(nullptr)
    , m_Lifetime(state.m_Lifetime)
    , m_FadeIn(1.0f)   // already visible when restored
    , m_Scale(1.0f)
    , m_GlowCounter(0)
{
    m_LayerFlags = 0x80;  // +0x34; HUD::Draw layer gate
    m_InnerRadius = 0.0f;
    m_OuterRadius = 0.0f;
    memset(&m_SpinAxis, 0, sizeof(m_SpinAxis));
    memset(&m_TintCurrent, 0, sizeof(m_TintCurrent));
    memset(&m_TintA, 0, sizeof(m_TintA));
    memset(&m_TintB, 0, sizeof(m_TintB));
    memset(_pad_e0, 0, sizeof(_pad_e0));
    memset(&m_ExplodeOrigin, 0, sizeof(m_ExplodeOrigin));
    memset(&m_ZoomTarget, 0, sizeof(m_ZoomTarget));

    // Restore the saved spin into the controller's base m_Timer (+0x2c), the same
    // field SaveSuperFruitState serialized as XML attr "rot". (HUDControl::m_Timer
    // is the +0x2c slot; explicit base qualifier avoids the shadow by the derived
    // finale-clock m_Timer at +0x88.)
    HUDControl::m_Timer = state.m_Spin;

    // TODO: v1.6.1 DAT_002d928c/9290 -- super-fruit restore tint/spinAxis Y,Z (unmapped; cosmetic).
    // The tint / spin-axis work vectors are left zero-initialised (memset above) as a
    // safe placeholder until the two DAT constants are resolved; do NOT guess values.

    // (restore ctor: register by symmetry with Release unregister; not byte-confirmed)
    SlashEntity::OnComboCancelEvent() += Mortar::Delegate1<void, SlashEntity*>::Make(
        this, &SuperFruitControl::ComboCancel);

    // Self-register so IsInSuperFruitState / SuperFruitSliced see the resumed controller.
    SuperFruitControls[fruit] = this;

    // Close the wave spawn-suppression gate (WaveManager+0x00) during super state.
    // Mirrors Reset()'s byte-write to the same slot (inverted: Reset opens it with 0).
#if defined(__bada__)
    *(uint8_t*)WaveManager::GetInstance() = 1;
#else
    WaveManager::GetInstance()->m_SpeedControl[0] = reinterpret_cast<HUDControl3d*>(1);
#endif

    // Shrink the host fruit's collision sphere (binary: colSphere.m_Radius *= 0.775).
    if (m_pHostFruit && m_pHostFruit->m_Col) {
        static_cast<ColSphere*>(m_pHostFruit->m_Col)->radius *= 0.775f;
    }
}

// Fixes the dangling-map bug: HUD deletes a finished control without calling
// Release(), so the SuperFruitControls map entry leaked -> IsInSuperFruitState()
// stuck true forever. Call Release() from the dtor (mirrors MenuButton::~MenuButton).
// Release() is idempotent: the delegate -= is a no-op if already removed, the map
// erase is find-guarded, and KillFruit is skipped once m_pHostFruit is null (the
// finale-end path in Update already nulls it before teardown).
SuperFruitControl::~SuperFruitControl()
{
    Release();
    m_pHostFruit = nullptr;
}

// Binary @ 0x001bb664.
// Order matches binary exactly:
//   1. UnRegister ComboCancel delegate from SlashEntity::ComboCanceledEvent
//   2. SuperFruitControls.find(key) -> erase if found
//   3. StopRays()
//   4. if (m_pHostFruit) { KillFruit(false); m_pHostFruit = NULL; }
//   5. if (m_pComboText)  { delete m_pComboText;  m_pComboText  = NULL; }
//   6. if (m_pScoreText)  { delete m_pScoreText;  m_pScoreText  = NULL; }
void SuperFruitControl::Release()
{
    // Binary @ 0x001bb664: step 1 -- UnRegister ComboCancel from ComboCanceledEvent.
    SlashEntity::OnComboCancelEvent() -= Mortar::Delegate1<void, SlashEntity*>::Make(
        this, &SuperFruitControl::ComboCancel);

    std::map<Fruit*, SuperFruitControl*>::iterator it = SuperFruitControls.find(m_pHostFruit);
    if (it != SuperFruitControls.end()) {
        SuperFruitControls.erase(it);
    }

    StopRays();

    if (m_pHostFruit) {
        m_pHostFruit->KillFruit(false);
        m_pHostFruit = nullptr;
    }

    if (m_pComboText) {
        delete m_pComboText;
        m_pComboText = nullptr;
    }
    if (m_pScoreText) {
        delete m_pScoreText;
        m_pScoreText = nullptr;
    }
}

// Binary @ 0x001bca10. Per-frame phase-ladder state machine.
// Master clock: m_Timer(+0x88). Phase thresholds keyed off m_Lifetime(+0xa0).
// DAT constants from literal pool @ 0x1bcd64 / 0x1bd488 — see spec.
void SuperFruitControl::Update(float dt)
{
    bool paused = game_work.bM_Mode;

    if (!paused) {
        m_Timer += dt;                              // +0x88 advance
        // combo-pitch SFX accumulator decay: -17.5/s, floor 0
        if (m_HitCount > 0.0f) {                    // +0x84
            float v = m_HitCount + dt * (-17.5f);   // DAT_001bcd64 = -17.5
            if (v <= 0.0f) v = 0.0f;               // DAT_001bcdac = 0.0
            m_HitCount = v;
        }
    }

    // keep host fruit's slice-timer (+0xbc) pinned to 1.0 while it is still positive
    if (m_pHostFruit && m_pHostFruit->m_SliceTimer > 0.0f) {
        m_pHostFruit->m_SliceTimer = 1.0f;
    }

    // one-shot edge: PrevTimer<0 && Timer>=0 && SliceCount==1 -> first ChangeText
    if (m_PrevTimer < 0.0f && m_Timer >= 0.0f && m_SliceCount == 1) {
        ChangeText("1 HIT", false, NULL);   // DAT_001bcd78 = "1 HIT"
    }

    // pre-roll slowdown: while Timer < Lifetime+0.5
    if (m_Timer < m_Lifetime + 0.5f) {
        WaveManager::GetInstance()->m_SpeedScale = 0.1f;  // DAT_001bcd98 = 0.1; SetAbsoluteDtMod
        MissControl::MakeEmAllDissappear();
    }

    // bomb suppression window; fVar = 1.5 if Arcade else 0.5
    float modeBias = (game_work.gameMode == 2) ? 1.5f : 0.5f;
    if (m_Timer < m_Lifetime + 0.5f + 0.35f + 0.55f + 0.65f + 0.25f + modeBias) {
        // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: bomb-suppress window fires
        //   Bomb::DeactivateAll() every frame while Timer < Lifetime+2.3+modeBias.
        Bomb::DeactivateAll();
    }

    // whoosh SFX: one-shot on crossing (Lifetime - 0.1)
    if (m_Timer >= m_Lifetime - 0.1f && m_PrevTimer < m_Lifetime - 0.1f) {
        // TODO: v1.6.1 SuperFruitControl::Update @0x001bca10 -- GameSound::SFXPlay(game+0x18c, DAT_001bcd80, pitch=0.125, vol=1.0, cb@DAT_001bcd7c) (whoosh SFX)
    }

    if (m_Timer >= m_Lifetime) {
        // ===== main explosion timeline (Timer past Lifetime) =====

        // (a) one-shot at crossing Lifetime: snapshot camera target + zoom
        if (m_PrevTimer < m_Lifetime) {
            if (m_pHostFruit) {
                m_ExplodeOrigin = m_pHostFruit->pos;     // explosion centre (+0xf0)
            }
            // v1.6.1 SuperFruitControl::Update @0x001bca10: FruitCamera::TransitionOut(game+0x4c).
            //   Port method is StartZoomOut() (binary symbol FruitCamera::TransitionOut @0x1bede8).
            if (game_work.m_FruitCamera) game_work.m_FruitCamera->StartZoomOut();
            StopAllFruit();
            UnpauseSlices();
            if (m_pLinkedSlasher) {
                // TODO: v1.6.1 SuperFruitControl::Update @0x001bca10 -- clear *(int*)(m_pLinkedSlasher+0x7c) = 0 (SlashEntity field +0x7c)
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
                m_ZoomTarget = Vec3(zx, zy, 0.0f);       // +0xfc; DAT_001bcdac = 0.0
            }
        }

        // (b) while PrevTimer < Lifetime+0.5: refresh centre; on crossing fire the bang
        if (m_PrevTimer < m_Lifetime + 0.5f) {
            if (m_pHostFruit) {
                m_ExplodeOrigin = m_pHostFruit->pos;         // refresh explosion centre
            }
            if (m_Timer >= m_Lifetime + 0.5f) {
                // one-shot: the actual blast
                // TODO: v1.6.1 SuperFruitControl::Update @0x001bca10 -- FruitCamera::CreateCameraShake(game+0x4c, mag=1.0, dur=2.0, pos) (needs camera)
                ExplodeSuperFruit();
                SpawnJibs();
                StopRays();
                // Score payoff popup (DAT_001bcd84 = "+%i"); target = &m_pScoreText (+0x9c).
                char scoreBuf[64];
                snprintf(scoreBuf, sizeof(scoreBuf), "+%i", m_SliceCount);
                ChangeText(scoreBuf, false, &m_pScoreText);
            }
        }

        // (c) after blast (Timer >= Lifetime+0.5): explosion update + late shake + time un-slow
        if (m_Timer >= m_Lifetime + 0.5f) {
            m_LayerFlags = 1;    // binary +0x34 = 1 (HUD draw-layer flag)
            UpdateExplosion(dt);
            float tLateShake = m_Lifetime + 0.5f + 0.35f + 0.4f;  // DAT_001bcd9c=0.35, DAT_001bcd90=0.4
            if (m_PrevTimer < tLateShake && tLateShake <= m_Timer) {
                // TODO: v1.6.1 SuperFruitControl::Update @0x001bca10 -- FruitCamera::CreateCameraShake(game+0x4c, mag=1.6, dur=2.0, pos) (DAT_001bcd94=1.6)
            }
            // ease global time-scale back toward 1.0: ts = (ts-1)*pow(0.75, dt*60) + 1
            // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: slow-mo = game_work.mHud->m_globalTimeScale
            //   (HUD+0x24); pre-roll ts*=pow(0.75,dt*60); post-blast ts=(ts-1)*pow(0.75,dt*60)+1; end ts=1.
            if (game_work.mHud) {
                float& ts = game_work.mHud->m_globalTimeScale;
                ts = (ts - 1.0f) * powf(0.75f, dt * 60.0f) + 1.0f;
            }
        }

        // (d) score payoff window: Lifetime+0.5+0.35+0.55+0.1
        float tScore = m_Lifetime + 0.5f + 0.35f + 0.55f + 0.1f;  // DAT_001bcd98=0.1
        if (m_PrevTimer < tScore && tScore <= m_Timer) {
            if (game_work.gameMode == 0) {
                // persist stat (gameMode 0 only)
                uint32_t statHash = StringHash("super_fruit_gp_classic");
                if (game_work.m_SaveData) {
                    game_work.m_SaveData->AddToTotal("super_fruit_gp_classic", statHash, m_SliceCount, false, false);
                }
            }
            AddToCurrentScore(m_SliceCount, 0, false, true);  // flag4=true; raw m_SliceCount
        }

        // (e) kill host fruit window: Lifetime+0.5+0.35+0.55+0.65+0.25+0.55
        float tKill = m_Lifetime + 0.5f + 0.35f + 0.55f + 0.65f + 0.25f + 0.55f;
        if (m_Timer > tKill) {
            m_LayerFlags = 0x80;   // binary +0x34 = 0x80 (post-blast HUD draw-layer marker)
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
            // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: finale end byte-clears WaveManager+0x00
            //   (m_SpeedControl[0] spawn gate).
#if defined(__bada__)
            *(uint8_t*)WaveManager::GetInstance() = 0;
#else
            WaveManager::GetInstance()->m_SpeedControl[0] = nullptr;
#endif
            WaveManager::GetInstance()->GetNextWave(0);
            // v1.6.1 SuperFruitControl::Update @0x001bca10: finale end zeroes PSPParticleManager+0x00
            //   (m_GlobalPullRadius, the vortex pull radius -- a float, not a vptr).
            PSPParticleManager::GetInstance().m_GlobalPullRadius = 0.0f;
            PSPParticleManager::GetInstance().m_GlobalTimeScale = 1.0f;
            // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: slow-mo = game_work.mHud->m_globalTimeScale
            //   (HUD+0x24); end ts=1.
            if (game_work.mHud) game_work.mHud->m_globalTimeScale = 1.0f;
            m_bPendingRemoval = 1;                  // +0x33 = 1: HUD::Update removes this control
        }
    } else {
        // ===== Timer still < Lifetime: throw/anticipation phase =====

        if (m_pHostFruit) {
            // host-fruit time-scale (Fruit+0x98 = m_TimeScale) driven by a position
            // clamp-ramp. Only the spin.x==0 arm is RE'd; it selects pos.y (when the
            // fruit is descending) or pos.x (sign by vel.x) as the ramp input.
            if (m_SpinAxis.x == 0.0f) {
                float ts;
                if (m_pHostFruit->vel.y < 0.0f) {
                    ts = T_1616(m_pHostFruit->pos.y, -128.0f, -96.0f);
                } else if (m_pHostFruit->vel.x < 0.0f) {
                    ts = T_1616(m_pHostFruit->pos.x, -216.0f, -144.0f);
                } else {
                    ts = T_1616(m_pHostFruit->pos.x, 216.0f, 144.0f);
                }
                m_pHostFruit->m_TimeScale = ts;
            }
            // TODO: v1.6.1 SuperFruitControl::Update @0x001bca10 -- spin.x != 0 arm of the
            //   host m_TimeScale write is not yet RE'd (m_SpinAxis stays 0 in the port until
            //   GetSliceDir lands, so this arm is currently dormant).
            PushBombsAway(dt);
        }

        // global time-scale pre-roll: ts = 0.0 + ts*pow(0.75, dt*60)
        // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: slow-mo = game_work.mHud->m_globalTimeScale
        //   (HUD+0x24); pre-roll ts*=pow(0.75,dt*60) (eases to 0).
        if (game_work.mHud) {
            game_work.mHud->m_globalTimeScale *= powf(0.75f, dt * 60.0f);
        }

        // build the spin-orbit camera transition for the thrown fruit
        // TODO: v1.6.1 SuperFruitControl::Update @0x001bca10 -- FruitCamera::Transition(game+0x4c, dist, angle, target, doneCb@DAT_001bd4ac) (needs camera + Math::SinIdx/CosIdx)

        // recompute pos from host + scaled dir
        // TODO: v1.6.1 SuperFruitControl::Update @0x001bca10 -- pos(+0x08) orbit recompute from host + dirXY*320*0.25*0.625 (DAT_001bd49c=320.0)
    }

    // tail: LAB_001bd0ac -- runs every frame.
    // HUD scale-fade (was MISLABELED "ray-entity"): while Timer < Lifetime+0.5, fade
    // the three HUD scales[3..5] (HUD+0x14/+0x18/+0x1c) toward 0.3 by the phase progress.
    if (m_Timer < m_Lifetime + 0.5f) {
        if (game_work.mHud) {
            float prog = 1.0f - m_Timer / m_Lifetime;   // DAT_001bd4a0=0.3 (the T_1629 floor)
            if (prog < 0.0f) prog = 0.0f;
            else if (prog > 1.0f) prog = 1.0f;
            float f = T_1629(0.3f, prog);
            game_work.mHud->scales[3] *= f;
            game_work.mHud->scales[4] *= f;
            game_work.mHud->scales[5] *= f;
        }
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

// ASM-spec v1.6.1 SuperFruitControl::ChangeText @0x001b9ee4. Create-or-replace a
// combo/score popup FancyBakedString. Fill (main) + stroke (INNER_GLOW) colours
// morph across three keys by t = clamp(m_SliceCount/35, 0, 1); stroke drawn only
// on fast hardware (layers == 3).
void SuperFruitControl::ChangeText(const char* text, bool resetFade,
                                   Mortar::FancyBakedString** target)
{
    if (target == NULL) target = &m_pComboText;

    // Create-or-replace: drop any existing label in this slot first.
    if (*target) {
        delete *target;
        *target = NULL;
    }

    Game* g = Game::GetInstance();
    int layers = (g && g->IsFastHardware()) ? 3 : 0;   // stroke on fast HW only

    float t = (float)m_SliceCount / 35.0f;             // colour-morph key
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;

    // Fill colour (main layer): yellow -> orange -> purple as the combo grows.
    Colour colourA = SuperFruitColourMorph3(
        Colour(255, 218, 46, 255),
        Colour(255, 119, 54, 255),
        Colour(137, 46, 255, 255), t);
    // Stroke colour (INNER_GLOW layer); only applied when layers == 3.
    Colour colourB = SuperFruitColourMorph3(
        Colour(170, 120, 0, 255),
        Colour(170, 35, 0, 255),
        Colour(55, 0, 170, 255), t);

    // The finale halo is the GLOW (STROKE) layer, size 3 on fast HW; shadow=0.
    float glowSize = (layers == 3) ? 3.0f : 0.0f;

    *target = new Mortar::FancyBakedString(
        game_work.m_pTTFFontMain, text, 50.0f, colourA, /*p5*/0, /*circleRadius*/0.0f,
        glowSize, colourB,               // glow -> STROKE (the visible halo)
        0.0f, Colour(0, 0, 0, 255),      // shadow off
        0.0f, Colour(0, 0, 0, 255),      // stroke off
        /*shadowMode*/0, /*extraSize*/0.0f, /*p15*/0,
        Colour(255, 255, 255, 255), Colour(255, 255, 255, 255));

    // TODO: v1.6.1 SuperFruitControl::ChangeText @0x001b9ee4 -- the exact 3-stop
    //   vertical gradient (ApplyGradientSplit y=0.55 -> colourC, y=0.5 -> colourD,
    //   y=0.0 -> colourA) needs the colourC/colourD DAT constants, which are
    //   unmapped. The main fill (colourA) already makes the label visible; do NOT
    //   guess the two gradient stop colours.

    if (resetFade) m_FadeIn = 0.0f;
}

// ASM-spec v1.6.1 SuperFruitControl::DrawOrder @0x001bd7c8. Layer-gated finale
// draw: the 0x80/1 layer draws the combo/score text (fade via SinTransition +
// zoom follow), the 0x200 layer draws the explosion shockwave rings.
void SuperFruitControl::DrawOrder(float* /*hudScaleRaw*/, int layerMask)
{
    if (m_pComboText && (layerMask == 0x80 || layerMask == 1)) {
        float modeBias = (game_work.gameMode == 2) ? 1.5f : 0.5f;
        float tOut = m_Lifetime + 2.3f + modeBias;

        // Anchor re-based to the (480,320) screen anchor by per-control hud scale.
        Vec3 p = pos + Vec3(480.0f, 320.0f, 0.0f) * m_HudScale;
        // Past the explosion, drift the text toward the zoom target by (1 - zoomT).
        if (m_Timer > m_Lifetime && game_work.m_FruitCamera) {
            float zoomT = game_work.m_FruitCamera->m_ZoomT;
            p += (m_ZoomTarget - p) * (1.0f - zoomT);
        }

        // Fade envelope: fade-out approaching tOut, fade-in from anticipation.
        float env = SinTransition(T_1616(m_Timer, tOut + 0.15f, tOut), 115.0f)
                  * SinTransition(T_1616(m_Timer, -2.0f, -1.85f), 115.0f);

        float s = (size.x + JumpySinPulse(m_FadeIn, 3.0f) * 0.25f * size.x) * env;
        if (m_Timer < 0.0f) {
            float q = SinPulse(m_Timer, 8.0f) * 0.125f;
            s *= (q < 0.0f) ? 1.0f : (q + 1.0f);
        }

        m_pComboText->Draw(p, Vec2(s, s), HUDControl::m_Timer, Mortar::ALIGN_CENTRE);

        if (m_pScoreText) {
            float f2 = SinTransition(
                T_1616(m_Timer, m_Lifetime + 1.5f, m_Lifetime + 1.75f), 115.0f);
            float zoomT2 = game_work.m_FruitCamera ? game_work.m_FruitCamera->m_ZoomT : 0.0f;
            // Lerp from the zoom target to the explosion origin by zoomT.
            Vec3 sp = m_ZoomTarget + (m_ExplodeOrigin - m_ZoomTarget) * zoomT2;
            float s2 = f2 * env * size.x;
            m_pScoreText->Draw(sp, Vec2(2.0f * s2, 2.0f * s2),
                               HUDControl::m_Timer, Mortar::ALIGN_CENTRE);
        }
    }

    if (layerMask == 0x200) {
        DrawExplosion();
    }
}

// ASM-spec v1.6.1 SuperFruitControl::DrawExplosion @0x001bd4d8. Inner ring fades
// out ending at Lifetime+0.85, outer ring ending at Lifetime+2.05.
void SuperFruitControl::DrawExplosion()
{
    DrawRing(m_InnerRadius, m_Lifetime + 0.85f);   // +0xe8
    DrawRing(m_OuterRadius, m_Lifetime + 2.05f);   // +0xec
}

// ASM-spec v1.6.1 SuperFruitControl::DrawRing @0x001bd4d8. One ShockWaveTexture
// ring: scaled to r, alpha fading over the 0.25s window ending at `base`. Same
// Reset/Scale/Translate/Upload/DrawQuadUnCached idiom as BombHit::DrawCritHit.
void SuperFruitControl::DrawRing(float r, float base)
{
    Mortar::Texture* tex = ShockWaveTexture.Get();
    float env = (base + 0.25f - m_Timer) / 0.25f;
    if (env < 0.0f) env = 0.0f;
    else if (env > 1.0f) env = 1.0f;
    if (env <= 0.0f || r <= 0.0f || !tex) return;

    float s = r * 2.0f / 0.84375f;
    int a = (int)(env * 128.0f);
    if (a < 0) a = 0;
    else if (a > 255) a = 255;

    MatrixManager& mm = MatrixManager::GetInstance();
    mm.GetWorldStack().Reset();
    mm.GetWorldStack().Scale(Vec3(s, s, s));
    mm.GetWorldStack().Translate(m_ExplodeOrigin);
    mm.UploadModelViewOnly();

    tex->Set();
    Mortar::Mesh::DrawQuadUnCached(Colour(255, 150, 175, (unsigned char)a), NULL);
    tex->UnSet(true);
}

// Binary @ 0x001bb994. Per-hit combo response.
// ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bb994: entry-gate, bump counts,
// glow reroll + scale-pop, speed-loss bump, per-hit slash particles, slasher clear.
void SuperFruitControl::Sliced(Mortar::Entity* slashEntity)
{
    // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bb994: entry gate.
    if (m_Timer < 0.0f) {
        m_Timer = 0.0f;            // first slice: start the finale timer, proceed
    } else if (m_Timer >= m_Lifetime || m_HitCount > 1.5f) {
        return;                   // finale running/over, or hit-count exceeded -> ignore
    }

    ++m_SliceCount;
    m_HitCount += 1.0f;

    // Glow-counter reroll + per-hit scale-pop.
    if (m_GlowCounter > 0) m_GlowCounter--;
    if (m_GlowCounter < 1) {
        // TODO: v1.6.1 SuperFruitControl::Sliced @0x001bb994 -- BLOCKED: binary also rerolls
        //   m_TintB here (Magnitude gate -> Rand32 or Atan2Idx-based Cos/Sin * rand). GetSliceDir
        //   is now ported, but this reroll's exact branch logic + tint DAT constants are unmapped,
        //   so the tint term stays deferred.
        m_Scale = 0.0f;               // per-hit scale-pop (re-ramps in Update)
        m_TintA = m_TintCurrent;
        m_GlowCounter = 0;
    }

    // Bump the wave speed-loss timer (P0).
    WaveManager::GetInstance()->AddToSpeedLossTime(0.1f, 0);

    // v1.6.1 SuperFruitControl::Sliced @0x001bb994: release the host's trail emitters, then
    //   set the throw-spin axis from the blade direction. The slice index is the throw-orbit
    //   spin baked as a 16-bit angle: sliceIdx = (uint16)(int)(-182.0 * base m_Timer(+0x2c)),
    //   where HUDControl::m_Timer holds -spin (see ctor). m_SpinAxis = GetSliceDir(sliceIdx)
    //   * host->m_SliceArcImpulse(+0xc4) * 3.0 (ASM: two operator* calls, scalars host+0xc4 then 3.0).
    m_pHostFruit->RemoveTrailParticles();
    uint16_t sliceIdx = (uint16_t)(int)(-182.0f * HUDControl::m_Timer);
    m_SpinAxis = m_pHostFruit->GetSliceDir(sliceIdx)
               * m_pHostFruit->m_SliceArcImpulse * 3.0f;

    // v1.6.1 SuperFruitControl::Sliced @0x001bb994: two AddSlice effects at the host fruit pos.
    //   impulse = uniform[0.8, 1.1), rateMul = 0.65, pos.z = m_EmitterDepth - 5.0
    //   call A: fruit=(Fruit*)0, call B: fruit=(Fruit*)3
    if (m_pHostFruit) {
        // TODO: v1.6.1 SuperFruitControl::Sliced @0x001bb994 -- binary derives the slice
        //   angle from the slash-entity direction (Atan2Idx of the SlashEntity dir field),
        //   not the host velocity. The exact SlashEntity dir field is unconfirmed, so this
        //   keeps the host-velocity Atan2Idx as a stand-in until it is RE'd.
        float angleDeg = (float)(int16_t)Math::Atan2Idx(
            m_pHostFruit->vel.y, m_pHostFruit->vel.x) / 182.0f;
        float impulse = 0.8f + Math::g_Random.RandF(0.3f);
        Vec3 hostPos = m_pHostFruit->pos;
        float sliceZ = m_pHostFruit->m_ZPosition - 5.0f;   // m_EmitterDepth - 5
        AddSlice(Vec3(angleDeg, impulse, 0.65f),
                 hostPos.x, hostPos.y, 0, (Fruit*)0, sliceZ);
        AddSlice(Vec3(angleDeg, impulse, 0.65f),
                 hostPos.x, hostPos.y, 0, (Fruit*)3, sliceZ);
    }

    // Clear the linked slasher's head anchor and remove it quickly.
    if (m_pLinkedSlasher) {
        m_pLinkedSlasher->ClearHeadPosX();   // SlashEntity+0x7c (m_HeadPos.x = 0)
        // TODO: v1.6.1 SuperFruitControl::Sliced @0x001bb994 -- BLOCKED: SuperFruitHitControl::RemoveQuickly(m_pLinkedSlasher)
        //   (SuperFruitHitControl unported). Null the pointer for now to avoid a dangle.
        m_pLinkedSlasher = nullptr;
    }

    // Combo-count popup label (resets the fade-in each hit).
    char buf[64];
    snprintf(buf, sizeof(buf), "%i HITS", m_SliceCount);
    ChangeText(buf, true, NULL);

    // TODO: v1.6.1 SuperFruitControl::Sliced @0x001bb994 -- BLOCKED remaining:
    //   pome-slice SFX (GameSound::SFXPlay); SpawnRay on odd m_SliceCount;
    //   PSP emitter hookup. Wire when those subsystems land.

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

        // GetFree() never returns null (v1.6.1 SplatEntity::GetFree @0x001eb318 --
        // flat round-robin pool steals the cursor slot when full).
        SplatEntity* s = SplatEntity::GetFree();
        Vec3 vel(SinIdx(angIdx) * spd, CosIdx(angIdx) * spd, 0.0f);  // DAT_001bae54=0.0
        s->MakeSplat(hostPos, vel, false, true, (long)hostFruitType);

        // taper splat life: clamp(1 - (i-2)/N, 0.3, 1.0)
        float taper = 1.0f - (float)(i - 2) / (float)N;
        if (taper < 0.3f) taper = 0.3f;   // DAT_001bae58=0.3
        if (taper > 1.0f) taper = 1.0f;
        // DIFFERS: binary writes raw SplatEntity+0x64; port layout: m_Vel is +0x5C,
        //   so +0x64 = m_Vel.z (last component). Binary scales the landed vel tail by taper.
        s->m_Vel.z *= taper;
    }

    // ---- (B) white critical screen-flash ----
    CriticalFlash(hostPos, Colour(255, 255, 255, 255));

    // ---- (C) explosion SFX ----
    // TODO: v1.6.1 SuperFruitControl::ExplodeSuperFruit @0x001baa20 -- SFXPlay(game+0x18c, DAT_001bae78 SFX key, pitch=0.125, vol=2.0, cb@DAT_001bae74) (SFX key/cb unresolved)

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

        // Jiblet::Init(fruitType, pos=this->m_ExplodeOrigin, scale=1.0, vel, mdl, emitterHash=0, dripRate=0.0, grav=angVel)
        // v1.6.1 SuperFruitControl::ExplodeSuperFruit @0x001baa20: pos arg is &this->m_ExplodeOrigin
        // (SuperFruitControl+0xf0, the explosion origin). The prior host+0x74 cast was wrong.
        jiblet->Init((int)hostFruitType, m_ExplodeOrigin, 1.0f, vel, mdl, 0, 0.0f, angVel);

        // post-init writes: copy host transform onto the jib actor
        jiblet->m_Age = 0.0f;                // jib+0xac = 0 (reset age set by Init to -0.04)
        jiblet->m_Rotation = rot;            // jib+0x4c = rot (64-byte Matrix44 copy)
        // v1.6.1 SuperFruitControl::ExplodeSuperFruit @0x001baa20: jib->m_LaunchVelocity =
        //   host->m_LaunchVelocity. m_LaunchVelocity IS Entity::scale (+0x28), so this copies the
        //   host fruit's visual scale onto the jib -- read here, before the host scale-collapse below.
        jiblet->scale = host->scale;         // jib+0x28 = host+0x28 (Entity::scale = m_LaunchVelocity)
    }

    // ---- (E) restore host: collapse the host fruit's visual scale ----
    // v1.6.1 SuperFruitControl::ExplodeSuperFruit @0x001baa20: host->m_LaunchVelocity = Vec3(0,0,0).
    //   m_LaunchVelocity IS Entity::scale (+0x28), so this zeroes the host fruit's visual scale,
    //   collapsing it after the jiblets (which snapshotted the pre-collapse scale) have spawned.
    host->scale = Vec3(0.0f, 0.0f, 0.0f);
}

// Binary @ 0x001b9850. Forces host fruit's slice timer negative when combo is cancelled
// while the super fruit is still in its anticipation phase (Timer < Lifetime).
void SuperFruitControl::ComboCancel(SlashEntity* se)
{
    (void)se;
    if (m_pHostFruit && m_Timer < m_Lifetime) {
        m_pHostFruit->m_SliceTimer = -1.0f;
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

    // TODO: v1.6.1 SuperFruitControl::SuperFruitThrown @0x001bbf48 -- SuperFruitThrown SFX not yet ported

    // ASM-spec v1.6.1 SuperFruitControl::SuperFruitThrown @0x001bbf48:
    // increment "super_pomegranates_spawned" stat when super-fruit is thrown.
    {
        const char* kSpawnedKey  = "super_pomegranates_spawned";
        uint32_t    spawnedHash  = StringHash(kSpawnedKey);
        if (game_work.m_SaveData)
            game_work.m_SaveData->AddToTotal(kSpawnedKey, spawnedHash, 1, false, false);
    }

    // Binary @ 0x001bbf48 spawns the glow halo (new 0x8c) at throw time and adds it
    // to the HUD. The SuperFruitControl itself is NOT created here -- it is born on
    // the first slice (SuperFruitSliced @0x001be630). The glow is self-managed:
    // it tracks the host fruit and fades + self-removes when the fruit is killed.
    SuperFruitGlow* glow = new SuperFruitGlow(fruit);
    if (game_work.mHud) {
        game_work.mHud->AddControl(glow, false);
    }
}

// v1.6.1 SuperFruitControl::LoadContent @0x001bda74: subscribes the slice/throw
// delegates and loads super-fruit visuals. Called from GameInitialise @0x0011daa8.
void SuperFruitControl::LoadContent() {
    // Subscribe SuperFruitSliced (static fn) to the global FruitSliced Event3.
    Fruit::FruitWasSlicedEvent() +=
        Mortar::Delegate3<void, Fruit*, int, Mortar::Entity*>::MakeFree(&SuperFruitControl::SuperFruitSliced);

    // Load the finale visuals.
    ShockWaveTexture = Mortar::TextureManager::LoadLocalisedTexture("explosion_radius.tex");
    if (Mortar::MeshManager::GetInstance()) {
        JibletModel = Mortar::MeshManager::GetInstance()->Load("models/fruit/pomegranate_jiblet.mmd");
    }

    // TODO: v1.6.1 SuperFruitControl::LoadContent @0x001bda74 -- also loads
    //   SuperFruitGlow::GlowTexture + FruitRay::RayTexture (2x LoadLocalisedTexture,
    //   filenames unresolved). Blocked: FruitRay is unported. Wire when it lands.
}

// Frees the finale visuals loaded by LoadContent. Nulls the file-static SmartPtr
// globals (releases their refcount).
void SuperFruitControl::UnLoadContent() {
    ShockWaveTexture = NULL;
    JibletModel = NULL;
}

// Binary @ 0x001be630. Slice dispatch: lookup map, forward or create.
void SuperFruitControl::SuperFruitSliced(Fruit* fruit, int /*idx*/, Mortar::Entity* slashEntity)
{
    if (!fruit) return;
    const FruitInfo* info = Fruit::FruitInfo((long)fruit->m_FruitType);
    if (!info || !info->m_bIsSuperFruit) return;

    std::map<Fruit*, SuperFruitControl*>::iterator it = SuperFruitControls.find(fruit);
    if (it != SuperFruitControls.end() && it->second) {
        // Forward the delegate's SlashEntity arg (Event3 arg2 = the aggressor
        // Entity*) to the live controller.
        it->second->Sliced(slashEntity);
    } else {
        // Not-found failure gate: in a failure-enabled mode while the game is
        // paused, swallow the slice (reset host slice-timer) and do NOT create.
        if (Mortar::FailureEnabled(game_work.gameMode) && game_work.bM_bPaused) {
            fruit->m_SliceTimer = 0.0f;
            return;
        }
        // First hit: create controller (binary allocates 0x108 bytes here) and
        // register it with the HUD so HUD::Update ticks it / HUD::Draw draws it.
        // The ctor IS the first hit (m_SliceCount=1) -- do NOT call Sliced() here
        // (that would double-count the slice).
        SuperFruitControl* ctrl = new SuperFruitControl(fruit);
        if (game_work.mHud) {
            game_work.mHud->AddControl(ctrl, false);
        }
        SuperFruitControls[fruit] = ctrl;
    }
}

// Binary @ 0x001b9828. Returns true while a super fruit is active.
// Binary reads SuperFruitControls+0x14 (_M_node_count, std::map size field != 0).
bool SuperFruitControl::IsInSuperFruitState()
{
    return !SuperFruitControls.empty();
}

// Binary @ 0x001b98c0. Reads FruitSaveData::GetTotal(game_work.m_SaveData, "super_pomegranates_spawned").
// There is no BSS counter; the value comes directly from the persistent save-data total.
int SuperFruitControl::NumPomegranatesSpawnedThisGame()
{
    if (!game_work.m_SaveData) return 0;
    return game_work.m_SaveData->GetTotal("super_pomegranates_spawned");
}

// Binary @ 0x001b99d4. Game-mode gating for final pomegranate spawn.
// TODO: v1.6.1 SuperFruitControl::CanSpawnFinalPomegranate @0x001b99d4 -- gate needs PowerUpManager::GetActiveProgression + Fruit::NumberOfPowerupFruits
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

// Binary @ 0x001ba73c. Serializes the active super-fruit state as a
// <superFruitState> child of `parent` (the <ent> element for the host fruit).
// Looks up the controller for `fruit` in SuperFruitControls; no-op if absent.
//
// Fixes #291: SuperFruitState::WriteToElement() returns a null standalone node
// under the tinyxml2 shim, so super-fruit save was silently dropped. Here we
// build the element inline through the parent's owning document
// (parent->GetDocument().NewElement) -- the same doc-owned pattern SaveGame uses --
// which keeps the binary (Fruit*, TiXmlElement*) signature intact.
void SuperFruitControl::SaveSuperFruitState(Fruit* fruit, TiXmlElement* parent)
{
    if (!parent) return;

    std::map<Fruit*, SuperFruitControl*>::iterator it = SuperFruitControls.find(fruit);
    if (it == SuperFruitControls.end() || !it->second) return;

    SuperFruitControl* ctrl = it->second;

    TiXmlElement sfs = parent->GetDocument().NewElement("superFruitState");
    sfs.SetDoubleAttribute("time",      (double)ctrl->m_Timer);       // +0x88
    sfs.SetAttribute      ("hits",      ctrl->m_SliceCount);           // +0x90
    sfs.SetDoubleAttribute("sliceTime", (double)ctrl->m_Lifetime);    // +0xa0
    // Binary @ 0x001ba73c reads ctrl+0x2c -> serialized as XML attr "rot".
    // ctrl+0x2c is the controller's base HUDControl::m_Timer slot; the binary
    // repurposes it as the saved spin/rotation value (explicit base qualifier
    // avoids the shadow by the derived finale-clock m_Timer at +0x88).
    sfs.SetDoubleAttribute("rot",       (double)ctrl->HUDControl::m_Timer); // +0x2c
    parent->InsertEndChild(sfs);
}

// Binary @ 0x001bb52c (binary symbol: SuperFruitControl::Reset). Global time-scale
// restore + type-6 ENT_KILLED walk + WaveManager/PSPParticleManager reset.
// Port name: ResetAll -- static class-level helper, renamed to avoid colliding
// with the HUDControl3d-inherited virtual void Reset() (GCC 4.4.1 error).
void SuperFruitControl::ResetAll()
{
    WaveManager::GetInstance()->m_SpeedScale = 1.0f;   // SetAbsoluteDtMod(1.0)
    // ASM-spec v1.6.1 SuperFruitControl::Reset @0x001bb52c: byte-clears WaveManager+0x00
    //   (m_SpeedControl[0] spawn gate).
#if defined(__bada__)
    *(uint8_t*)WaveManager::GetInstance() = 0;
#else
    WaveManager::GetInstance()->m_SpeedControl[0] = nullptr;
#endif
    // v1.6.1 SuperFruitControl::Reset @0x001bb52c: also zeroes PSPParticleManager+0x00
    //   (m_GlobalPullRadius, the vortex pull radius -- a float, not a vptr).
    PSPParticleManager::GetInstance().m_GlobalPullRadius = 0.0f;
    PSPParticleManager::GetInstance().m_GlobalTimeScale = 1.0f;
    // ASM-spec v1.6.1 SuperFruitControl::Reset @0x001bb52c: slow-mo = game_work.mHud->m_globalTimeScale
    //   (HUD+0x24); restore to 1.0.
    if (game_work.mHud) game_work.mHud->m_globalTimeScale = 1.0f;
    // v1.6.1 SuperFruitControl::Reset @0x001bb52c: FruitCamera::TransitionOut(game+0x4c).
    //   Port method is StartZoomOut() (binary symbol FruitCamera::TransitionOut @0x1bede8).
    if (game_work.m_FruitCamera) game_work.m_FruitCamera->StartZoomOut();
    // TODO: v1.6.1 SuperFruitControl::Reset @0x001bb52c -- StackAllocatedPointer<Delegate0>::Delete((game+0x4c)+0x184) camera done-cb free
    UnpauseSlices();

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

    // TODO: v1.6.1 SuperFruitControl::Reset @0x001bb52c -- this->+0x33 = 1 (game-level done-flag write; target unresolved in port)
}

// Binary @ 0x001ba460. Stops all in-flight fruit and bombs during the
// super-fruit explosion finale: clears any still-unspawned (chuck-delayed)
// fruits/bombs, then sweeps every live fruit (ActorManager type 0) and bomb
// (type 1), redirecting their velocity toward the explosion centre and
// freezing their physics. The explosion centre is this->m_ExplodeOrigin (+0xf0).
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
    const Vec3& centre = m_ExplodeOrigin;

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

// ASM-spec v1.6.1 SuperFruitControl::UpdateExplosion @0x001baeb8. Per-frame
// shockwave: grows the inner/outer radii from T_1616 ramps, writes the particle-
// manager globals, eases the wave dt-mod, then radially pushes Actor types 0/1/5
// outward from the epicenter. R = sqrt(384000)*1.2 ~= 743.61.
//
// NOTE: the radial-push formula (dir*(outerR-dist)*dt*mult, mults 4.0 fruit /
// 5.0 bomb+jib) and the inner-radius force-explode branch are ported from the RE
// spec + the prior TODO; the exact clamp/condition still wants asm-inspector
// confirmation. The mgr+0x00 write (m_GlobalPullRadius = m_InnerRadius*1.6) is wired below.
void SuperFruitControl::UpdateExplosion(float dt)
{
    PSPParticleManager& mgr = PSPParticleManager::GetInstance();

    const float R = Math::Sqrt(384000.0f) * 1.2f;   // ~743.61

    m_LayerFlags |= 0x200;

    // Inner shockwave radius: ramp 0->R across [Lifetime+0.5, Lifetime+0.85].
    m_InnerRadius = T_1616(m_Timer, m_Lifetime + 0.5f, m_Lifetime + 0.85f) * R;

    // Ease the wave dt-mod (binary: SetAbsoluteDtMod).
    WaveManager::GetInstance()->m_SpeedScale =
        T_1629(0.1f, T_1616(m_Timer, m_Lifetime + 1.25f, m_Lifetime + 1.45f));

    // Outer shockwave radius: ramp 0->R across [Lifetime+1.4, Lifetime+2.05].
    m_OuterRadius = T_1616(m_Timer, m_Lifetime + 1.4f, m_Lifetime + 2.05f) * R;

    // Write epicenter global.
    mgr.m_GlobalOrigin = m_ExplodeOrigin;    // this+0xf0 -> mgr+0x08

    // v1.6.1 SuperFruitControl::UpdateExplosion @0x001baeb8: PSPParticleManager+0x00 =
    //   m_InnerRadius*1.6. +0x00 is the vortex pull radius (float m_GlobalPullRadius, not a vptr).
    mgr.m_GlobalPullRadius = m_InnerRadius * 1.6f;

    float modeBias = (game_work.gameMode == 2) ? 1.5f : 0.5f;
    mgr.m_GlobalTimeScale =
        T_1616(m_Timer, m_Lifetime + 2.3f + modeBias, m_Lifetime + 2.05f);

    // Bounded window for the radial push / force-explode.
    if (m_Timer <= m_Lifetime + 2.55f) {
        Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
        if (am) {
            const Vec3& origin = m_ExplodeOrigin;
            std::list<Mortar::Entity*>::iterator it;

            // -------- type 0: fruits (push mult 4.0) --------
            Mortar::Entity* e = am->GetEntityFirst(0, it);
            while (e != NULL) {
                Fruit* f = static_cast<Fruit*>(e);
                if (f == m_pHostFruit) {
                    // Host fruit special-case: freeze in place.
                    f->m_SliceTimer = 0.5f;
                    f->vel = Vec3(0.0f, 0.0f, 0.0f);
                    f->m_SecondVel = Vec3(0.0f, 0.0f, 0.0f);
                } else {
                    Vec3 dir = f->pos - origin;
                    float dist = dir.Normalise();
                    f->vel += dir * ((m_OuterRadius - dist) * dt * 4.0f);
                    if (f->Sliced()) {
                        Vec3 dir2 = f->m_SecondPos - origin;
                        float dist2 = dir2.Normalise();
                        f->m_SecondVel += dir2 * ((m_OuterRadius - dist2) * dt * 4.0f);
                    } else if (dist < m_InnerRadius) {
                        // Inner ring force-explodes still-whole fruit and scores 1.
                        f->CollisionResponse(NULL, 0, 0, NULL);
                        AddToCurrentScore(1, 0, true, true);
                    }
                }
                e = am->GetEntityNext(0, it);
            }

            // -------- type 1: bombs (push mult 5.0) --------
            e = am->GetEntityFirst(1, it);
            while (e != NULL) {
                Vec3 dir = e->pos - origin;
                float dist = dir.Normalise();
                e->vel += dir * ((m_OuterRadius - dist) * dt * 5.0f);
                e = am->GetEntityNext(1, it);
            }

            // -------- type 5: jibs (push mult 5.0) --------
            e = am->GetEntityFirst(5, it);
            while (e != NULL) {
                Vec3 dir = e->pos - origin;
                float dist = dir.Normalise();
                e->vel += dir * ((m_OuterRadius - dist) * dt * 5.0f);
                e = am->GetEntityNext(5, it);
            }
        }
    }
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
        // TODO: v1.6.1 0x001b9b4c (SuperFruitControl::StopRays) -- x64 byte-offset landmine (#189);
        // ray (type-6) class unported, loop body dormant (no producer). +0xE0 = Entity[3].m_LaunchVelocity.y
        // on the ray subclass; use named field once ported.
#if defined(__bada__)
        uint8_t* rawBase = reinterpret_cast<uint8_t*>(e);
        rawBase[0xe0] = 1;
#endif
        e = am->GetEntityNext(6, it);
    }

    // ASM-spec v1.6.1 SuperFruitControl::StopRays @0x001b9b4c: after the ray loop,
    // zero the host fruit's two per-half spin rates.
    if (m_pHostFruit) {
        m_pHostFruit->m_RotVel1 = Vec3(0.0f, 0.0f, 0.0f);   // Fruit+0x100
        m_pHostFruit->m_RotVel2 = Vec3(0.0f, 0.0f, 0.0f);   // Fruit+0x10c
    }
}

// ASM-spec v1.6.1 SuperFruitControl::SpawnJibs @0x001bc748. PSPParticleManager
// emitter hookup for jib particle trails: emitter name = "<fruitModel>_explode",
// positioned at the explosion origin with the host fruit's slice-arc direction.
// The 8 JibletModel mesh-actor spawns are BLOCKED (JibletModel global unported).
void SuperFruitControl::SpawnJibs()
{
    PSPParticleManager& mgr = PSPParticleManager::GetInstance();

    if (m_pHostFruit) {
        // Emitter name = sprintf("%s_explode", <fruit model name>).
        const ::FruitInfo* fi = Fruit::FruitInfo((long)m_pHostFruit->m_FruitType);
        const char* modelName = fi ? fi->m_ModelName : "";
        char buf[128];
        snprintf(buf, sizeof(buf), "%s_explode", modelName);
        uint32_t hash = StringHash(buf);

        if (mgr.EmitterExists(hash)) {
            PSPParticleEmitter* e = mgr.AddEmitter(hash, 0, false);
            if (e) {
                e->m_bTrailStarted = 1;         // +0x4d
                e->m_Pos = m_ExplodeOrigin;     // explosion world pos (+0xf0)
                uint16_t ang = m_pHostFruit->m_SliceArcAngle;   // Fruit+0xc0
                e->m_DirCos = CosIdx(ang);
                e->m_DirSin = SinIdx(ang);
            }
        }

        // Spawn 8 JibletModel mesh actors in a radial fan (one per 45-degree sector).
        if (JibletModel.Get()) {
            Game* g = Game::GetInstance();
            float dripRate = (g && g->IsFastHardware()) ? 50.0f : 20.0f;
            float angBase = SuperFruitUniform(0.0f, 45.0f);
            char jb[64];
            snprintf(jb, sizeof(jb), "%s_jiblet", modelName);
            uint32_t jh = StringHash(jb);

            Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
            for (int i = 0; i < 8; ++i) {
                Jiblet* j = am ? static_cast<Jiblet*>(am->Add(5, true)) : 0;
                if (!j) continue;
                float ang = SuperFruitUniform((i + 0.2f) * 45.0f, (i + 0.8f) * 45.0f);
                uint16_t a16 = (uint16_t)((int)((angBase + ang) * 182.0f) & 0xffff);
                Vec3 dir(CosIdx(a16), SinIdx(a16), 0.0f);
                j->Init((int)m_pHostFruit->m_FruitType, m_ExplodeOrigin,
                        SuperFruitUniform(0.8f, 1.25f),
                        dir * SuperFruitUniform(500.0f, 900.0f),
                        JibletModel, jh, dripRate, dir * 45.0f);
            }
        }
    }
}
