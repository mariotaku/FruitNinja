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
#include "engine/audio/GameSound.h"
#include "math/_Vector2.h"
#include "hud/MissControl.h"
#include "SuperFruitState.h"
#include "Fruit.h"
#include "FruitInfo.h"
#include "Bomb.h"
#include "SlashEntity.h"
#include "SplatEntity.h"
#include "collision/ColLine.h"
#include "Jiblet.h"
#include "FruitRay.h"
#include "ActorManager.h"
#include "game/GameWork.h"
#include "game/GameMode.h"
#include "game/GameOver.h"
#include "game/FruitSaveData.h"
#include "game/WaveManager.h"
#include "game/FruitCamera.h"
#include "game/BombHit.h"
#include "game/PowerUpManager.h"
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

    // v1.6.1 @0x001be1c8: tint/spin Y,Z read from DAT_002d928c/9290 which are uninitialized
    //   .bss (heap garbage); the port's zero-init is the faithful/safe behavior.
    m_SpinAxis    = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_TintCurrent = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_TintA       = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_TintB       = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_ExplodeOrigin = _Vector3<float>(0.0f, 0.0f, 0.0f);
    m_ZoomTarget    = _Vector3<float>(0.0f, 0.0f, 0.0f);

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
    size = _Vector3<float>(0.5f, 0.5f, 0.5f);
    _Vector3<float> dir(0.0f, 0.0f, 0.0f);
    if (m_pHostFruit) {
        pos = _Vector3<float>(m_pHostFruit->pos.x, m_pHostFruit->pos.y, 0.0f);
        uint16_t idx = (uint16_t)(int)(spin * 182.0f);
        // ASM-spec v1.6.1 SuperFruitControl::SuperFruitControl(Fruit*) @0x001bde88:
        //   dir = (SinIdx(idx), CosIdx(idx), 0) -- SinIdx feeds x (matches the Update
        //   throw-phase); pos += dir * 320.0 * 0.4 * 0.625 = dir*80 (pools
        //   @0x001be18c=320.0, @0x001be198=0.4, [sp,#0x114]=0.625).
        dir = _Vector3<float>(SinIdx(idx), CosIdx(idx), 0.0f);
        pos += dir * (320.0f * 0.4f * 0.625f);
    }

    // Binary ctor @0x001be1c8: registers ComboCancel delegate on ComboCanceledEvent.
    SlashEntity::OnComboCancelEvent() += Mortar::Delegate1<void, SlashEntity*>::Make(
        this, &SuperFruitControl::ComboCancel);

    // First-slice popup label.
    ChangeText("SLICE!", false, NULL);

    // v1.6.1 @0x001be1c8: throw-orbit camera zoom-in + ramp-down SFX.
    if (m_pHostFruit && game_work.m_FruitCamera) {
        // ASM-spec v1.6.1 SuperFruitControl::SuperFruitControl(Fruit*) @0x001bde88:
        //   camTgt = host.pos + dir * 320.0 * 0.15 * 0.625 = dir*30 (pool @0x001be190=0.15).
        _Vector3<float> camTgt = m_pHostFruit->pos + dir * (320.0f * 0.15f * 0.625f);
        game_work.m_FruitCamera->StartZoomIn(camTgt, 0.625f, spin,
            Mortar::Delegate0<void>::Make(this, &SuperFruitControl::TransitionFin));
    }
    // ASM-spec v1.6.1 SuperFruitControl::SuperFruitControl(Fruit*) @0x001be1c8:
    //   SFXPlay("pome-rampdown", atten=0.125, gain=1.0, pitch=0.0).
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("pome-rampdown", 0.125f, 1.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>(), 0.0f);
    }
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

    // v1.6.1 @0x001be1c8: tint/spin Y,Z read from DAT_002d928c/9290 which are uninitialized
    //   .bss (heap garbage); the port's zero-init (memset above) is the faithful/safe behavior.

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
        WaveManager::GetInstance()->SetAbsoluteDtMod(0.1f);  // DAT_001bcd98 = 0.1
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
        // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: whoosh SFXPlay("pome-zoomout", vol=1.0, pitch=0.125).
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay("pome-zoomout", 1.0f, 1.0f,
                Mortar::Delegate1<bool, Mortar::MortarSound*>(), 0.125f);
        }
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
                // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: clear linked slasher's head-pos X.
                m_pLinkedSlasher->ClearHeadPosX();   // SlashEntity+0x7c (m_HeadPos.x = 0)
            }
            // zoom target = host pos clamped x in [-204,204], y in [-128,128]
            if (m_pHostFruit) {
                _Vector3<float> hp = m_pHostFruit->pos;
                float zx = hp.x;
                if (zx < -204.0f) zx = -204.0f;        // DAT_001bcd68
                else if (zx >= 204.0f) zx = 204.0f;    // DAT_001bcd6c
                float zy = hp.y;
                if (zy < -128.0f) zy = -128.0f;        // DAT_001bcdc0
                else if (zy >= 128.0f) zy = 128.0f;    // DAT_001bcd70
                m_ZoomTarget = _Vector3<float>(zx, zy, 0.0f);       // +0xfc; DAT_001bcdac = 0.0
            }
        }

        // (b) while PrevTimer < Lifetime+0.5: refresh centre; on crossing fire the bang
        if (m_PrevTimer < m_Lifetime + 0.5f) {
            if (m_pHostFruit) {
                m_ExplodeOrigin = m_pHostFruit->pos;         // refresh explosion centre
            }
            if (m_Timer >= m_Lifetime + 0.5f) {
                // one-shot: the actual blast
                // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: blast CreateCameraShake(pos, 1.0, 2.0).
                if (game_work.m_FruitCamera) game_work.m_FruitCamera->CreateCameraShake(pos, 1.0f, 2.0f);
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
                // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: late CreateCameraShake(pos, 1.6, 2.0).
                if (game_work.m_FruitCamera) game_work.m_FruitCamera->CreateCameraShake(pos, 1.6f, 2.0f);
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
            WaveManager::GetInstance()->SetAbsoluteDtMod(1.0f);
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

        // DIFFERS: original = unconditional [this+0x7c] deref (v1.6.1 SuperFruitControl::Update
        //   @0x001bca10, no null check at 0x001bd0c0), using a null-guard because a null host
        //   deref is not worth reproducing.
        if (m_pHostFruit) {
            // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10 (gate 0x001bd0c0-0x001bd124):
            //   host-fruit time-scale (Fruit+0x98 = m_TimeScale) driven by a position clamp-ramp,
            //   gated on host->m_Gravity.x (Fruit+0xA0, vldr s15,[r6,#0xA0]):
            //     gravity.x != 0            -> ts = T_1616(pos.x, vel.x>=0 ? 216 : -216,
            //                                                     vel.x>=0 ? 144 : -144)
            //     gravity.x == 0 && vel.y<0 -> ts = T_1616(pos.y, -128, -96)
            //     gravity.x == 0 && vel.y>=0 -> NO write (bpl 0x001bd124 skips the store)
            if (m_pHostFruit->m_Gravity.x != 0.0f) {
                float ts;
                if (m_pHostFruit->vel.x < 0.0f) {
                    ts = T_1616(m_pHostFruit->pos.x, -216.0f, -144.0f);
                } else {
                    ts = T_1616(m_pHostFruit->pos.x, 216.0f, 144.0f);
                }
                m_pHostFruit->m_TimeScale = ts;
            } else if (m_pHostFruit->vel.y < 0.0f) {
                m_pHostFruit->m_TimeScale = T_1616(m_pHostFruit->pos.y, -128.0f, -96.0f);
            }
            PushBombsAway(dt);
        }

        // global time-scale pre-roll: ts = 0.0 + ts*pow(0.75, dt*60)
        // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bca10: slow-mo = game_work.mHud->m_globalTimeScale
        //   (HUD+0x24); pre-roll ts*=pow(0.75,dt*60) (eases to 0).
        if (game_work.mHud) {
            game_work.mHud->m_globalTimeScale *= powf(0.75f, dt * 60.0f);
        }

        // v1.6.1 @0x001bca10 throw phase (every frame): re-target the zoom to the orbiting fruit.
        float a = -HUDControl::m_Timer;                       // HUDControl::m_Timer (+0x2c)
        uint16_t idx = (uint16_t)(int)(a * 182.0f);
        _Vector3<float> dir(SinIdx(idx), CosIdx(idx), 0.0f);
        // ASM-spec v1.6.1 SuperFruitControl::Update @0x001bd0f8: camTgt fold -- host.pos, plus
        //   a fade-in wobble along m_SpinAxis (freq 2.0), plus the tint-lerp offset (which also
        //   refreshes m_TintCurrent this frame).
        if (m_pHostFruit) {
            _Vector3<float> camTgt = m_pHostFruit->pos;
            if (m_FadeIn < 1.0f) {
                camTgt += m_SpinAxis * JumpySinPulse(Clamp(2.0f * m_FadeIn, 0.0f, 1.0f), 2.0f);
            }
            float scaleClamped = m_Scale;
            if (scaleClamped > 1.0f) scaleClamped = 1.0f;
            m_TintCurrent = m_TintA + (m_TintB - m_TintA) * SinTransition(scaleClamped, 105.0f);
            camTgt += m_TintCurrent;
            if (game_work.m_FruitCamera) {
                game_work.m_FruitCamera->StartZoomIn(camTgt, 0.625f, a,
                    Mortar::Delegate0<void>::Make(this, &SuperFruitControl::TransitionFin));
            }

            // recompute pos from host + scaled dir
            pos = _Vector3<float>(m_pHostFruit->pos.x, m_pHostFruit->pos.y, 0.0f);
            pos += dir * (320.0f * 0.25f * 0.625f);           // dir*50
        }
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

// ASM-spec v1.6.1 SuperFruitControl::PregenerateText @0x001b9d60: one-shot
// (function-local static flag) TTF-cache warm -- bakes "0123456789HITSLICE" at
// size 50 (glow layer size 3 on fast HW, else 0, all colours white) into the
// glyph cache via a throwaway FancyBakedString, so the first mid-combo popup
// pays no glyph-bake hitch. Called at the top of ChangeText.
void SuperFruitControl::PregenerateText()
{
    static bool hasPreGenerated = false;
    Game* g = Game::GetInstance();
    float glowSize = (g && g->IsFastHardware()) ? 3.0f : 0.0f;
    if (hasPreGenerated || !game_work.m_pTTFFontMain) return;
    Mortar::FancyBakedString warm(
        game_work.m_pTTFFontMain, "0123456789HITSLICE", 50.0f,
        Colour(255, 255, 255, 255), 0, 0.0f,
        glowSize, Colour(255, 255, 255, 255),
        0.0f, Colour(255, 255, 255, 255),
        0.0f, Colour(255, 255, 255, 255),
        0, 0.0f, 0,
        Colour(255, 255, 255, 255), Colour(255, 255, 255, 255));
    hasPreGenerated = true;
}

// ASM-spec v1.6.1 SuperFruitControl::ChangeText @0x001b9ee4. Create-or-replace a
// combo/score popup FancyBakedString. Fill (main) + stroke (INNER_GLOW) colours
// morph across three keys by t = clamp(m_SliceCount/35, 0, 1); stroke drawn only
// on fast hardware (layers == 3).
void SuperFruitControl::ChangeText(const char* text, bool resetFade,
                                   Mortar::FancyBakedString** target)
{
    // v1.6.1 ChangeText @0x001b9ee4 head: one-shot glyph-cache warm (thunk @0x00112e18).
    PregenerateText();

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
        0.0f, Colour(255, 255, 255, 255), // shadow off (binary passes white, inert at size 0)
        0.0f, Colour(255, 255, 255, 255), // stroke off (binary passes white, inert at size 0)
        /*shadowMode*/0, /*extraSize*/0.0f, /*p15*/0,
        Colour(255, 255, 255, 255), Colour(255, 255, 255, 255));

    // ASM-spec v1.6.1 SuperFruitControl::ChangeText @0x001b9ee4: 3-stop vertical gradient, same t as fill.
    // Top (pale) -> mid -> bottom (fill colourA), binary call order 0.55 / 0.50 / 0.00.
    Colour colourC = SuperFruitColourMorph3(
        Colour(255, 244, 196, 255),
        Colour(255, 213, 194, 255),
        Colour(220, 194, 255, 255), t);
    Colour colourD = SuperFruitColourMorph3(
        Colour(217, 166, 46, 255),
        Colour(240, 86, 64, 255),
        Colour(109, 46, 239, 255), t);
    (*target)->ApplyGradientSplit(colourC, 0.55f);
    (*target)->ApplyGradientSplit(colourD, 0.50f);
    (*target)->ApplyGradientSplit(colourA, 0.00f);

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
        _Vector3<float> p = pos + _Vector3<float>(480.0f, 320.0f, 0.0f) * m_HudScale;
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

        m_pComboText->Draw(p, _Vector2<float>(s, s), HUDControl::m_Timer, Mortar::ALIGN_CENTRE);

        if (m_pScoreText) {
            float f2 = SinTransition(
                T_1616(m_Timer, m_Lifetime + 1.5f, m_Lifetime + 1.75f), 115.0f);
            float zoomT2 = game_work.m_FruitCamera ? game_work.m_FruitCamera->m_ZoomT : 0.0f;
            // Lerp from the zoom target to the explosion origin by zoomT.
            _Vector3<float> sp = m_ZoomTarget + (m_ExplodeOrigin - m_ZoomTarget) * zoomT2;
            float s2 = f2 * env * size.x;
            m_pScoreText->Draw(sp, _Vector2<float>(2.0f * s2, 2.0f * s2),
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

// ASM-spec v1.6.1 SuperFruitControl::DrawExplosion @0x001bd4d8 (DrawRing is a
// port-side extraction of the twice-inlined ring body; no separate symbol).
// One ShockWaveTexture ring: scaled to r, alpha fading over the 0.25s window
// ending at `base`. Same Reset/Scale/Translate/Upload/DrawQuadUnCached idiom as
// BombHit::DrawCritHit.
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
    mm.GetWorldStack().Scale(_Vector3<float>(s, s, s));
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
        // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bbaac: m_TintB reroll. All constants
        //   are inline immediates (not DATs). idx selects a direction by m_TintB's current
        //   magnitude: near-zero (<1) -> full-random angle; mid (1..44 incl.) -> along the
        //   current tint drift, Atan2Idx(m_TintB - m_TintA); large (>44) -> roughly away from
        //   current tint (+0x5ffa opposite fold, jittered by rand(0x3ffc)). Step magnitude is
        //   TRUNCATED to int before scaling (vcvt.s32/f32 pair @0x001bba34):
        //   r = (float)(int)(signedRand(1)*8+7).
        {
            float mag = m_TintB.Magnitude();
            uint16_t idx;
            if (mag < 1.0f) {
                idx = (uint16_t)Math::g_Random.Rand32(0);
            } else if (mag <= 44.0f) {
                _Vector3<float> d = m_TintB - m_TintA;
                idx = (uint16_t)Math::Atan2Idx(d.y, d.x);
            } else {
                idx = (uint16_t)((int)Math::Atan2Idx(m_TintB.y, m_TintB.x)
                                 + 0x5ffa + (int)Math::g_Random.Rand32(0x3ffc));
            }
            float r = (float)(int)(SuperFruitSignedRand(1.0f) * 8.0f + 7.0f);
            m_TintB += _Vector3<float>(CosIdx(idx), SinIdx(idx), 0.0f) * r;
        }
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
    if (m_pHostFruit) {
        // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bbbc4: slice angle is derived from
        //   the SLASH ENTITY's collision line (Entity+0x38 m_Col, a ColLine*), not host velocity:
        //   the swipe DIRECTION colLine.a(+0x04) - colLine.b(+0x14), then Atan2Idx(y,x).
        float angleDeg = 0.0f;
        if (slashEntity && slashEntity->m_Col) {
            ColLine* line = static_cast<ColLine*>(slashEntity->m_Col);
            _Vector3<float> d = line->a() - line->b;
            angleDeg = (float)(int16_t)Math::Atan2Idx(d.y, d.x) / 182.0f;
        }
        float impulse = 0.8f + Math::g_Random.RandF(0.3f);
        _Vector3<float> hostPos = m_pHostFruit->pos;
        float sliceZ = m_pHostFruit->m_ZPosition - 5.0f;   // m_EmitterDepth - 5
        AddSlice(_Vector3<float>(angleDeg, impulse, 0.65f),
                 hostPos.x, hostPos.y, 0, (Fruit*)0, sliceZ);
        // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bbc70: call B passes
        //   r1 = 3 (modelIdx 3 = super-fruit slice model, drawn by DrawSlices pass==true)
        //   and r2 = m_pHostFruit (+0x7c) -- AddSlice's dedup loop keys on m_pFruit,
        //   expiring older slice lines per host fruit.
        AddSlice(_Vector3<float>(angleDeg, impulse, 0.65f),
                 hostPos.x, hostPos.y, 3, m_pHostFruit, sliceZ);
    }

    // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bbcdc: on slow hardware, cancel the
    //   slicer's pending splat stream -- if slashEntity->entityType(+0x35) == 3 (SlashEntity)
    //   and !IsFastHardware(), write m_PendingSplats(+0x12c) = -1.
    if (slashEntity && slashEntity->entityType == 3) {
        Game* g = Game::GetInstance();
        if (!(g && g->IsFastHardware())) {
            static_cast<SlashEntity*>(slashEntity)->CancelPendingSplats();
        }
    }

    // Clear the linked slasher's head anchor and remove it quickly.
    if (m_pLinkedSlasher) {
        m_pLinkedSlasher->ClearHeadPosX();   // SlashEntity+0x7c (m_HeadPos.x = 0)
        m_pLinkedSlasher->ClampTailPosZ();   // SlashEntity+0x78 (m_TailPos.z floored to 0.8f)
    }

    // Combo-count popup label (resets the fade-in each hit).
    char buf[64];
    snprintf(buf, sizeof(buf), "%i HITS", m_SliceCount);
    ChangeText(buf, true, NULL);

    // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bb994: SpawnRay fires on
    // odd m_SliceCount.
    if (m_SliceCount & 1) {
        SpawnRay();
    }

    // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bb994: pome-slice SFX. n = T_1643(1,3)
    // (uniform int in [1,3]); pitch ramps down from 0.4 as the combo approaches 28 hits, then
    // holds at 0.4 past that. SFXPlay args: atten=0.125, gain=1.0, pitch=ramp.
    {
        int n = 1 + (int)Math::g_Random.Rand32(3);
        char key[24];
        snprintf(key, sizeof(key), "pome-slice-%i", n);
        float t = (float)m_SliceCount / 28.0f;
        float pitch = (t <= 0.8f) ? (t - 0.4f) : 0.4f;
        if (game_work.mGameSound) {
            game_work.mGameSound->SFXPlay(key, 0.125f, 1.0f,
                Mortar::Delegate1<bool, Mortar::MortarSound*>(), pitch);
        }
    }

    // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bb994: per-hit PSP emitter hookup.
    //   Gate: function-local static particleStopper (always incremented); the block runs when
    //   IsFastHardware() || particleStopper == 2, and resets the counter to 0 on every pass --
    //   slow-hardware profiles get half-rate particles, not zero.
    {
        static int particleStopper = 0;
        ++particleStopper;
        Game* g = Game::GetInstance();
        if ((g && g->IsFastHardware()) || particleStopper == 2) {
            particleStopper = 0;
            if (m_pHostFruit) {
                const FruitInfo* fi = Fruit::FruitInfo((long)m_pHostFruit->m_FruitType);
                uint32_t emitterHash = fi ? fi->m_NameHash : 0;
                PSPParticleManager& pm = PSPParticleManager::GetInstance();
                if (pm.EmitterExists(emitterHash)) {
                    // ASM-spec v1.6.1 SuperFruitControl::Sliced @0x001bbd8c: AddEmitter's
                    //   updateWhenPaused arg = (game_work.flM_PauseAmount(+0xc) < 1.0).
                    //   Call site @0x001bbdb4.
                    PSPParticleEmitter* e = pm.AddEmitter(emitterHash, 0,
                                                          /*updateWhenPaused=*/game_work.m_PauseAmount < 1.0f);
                    if (e) {
                        uint16_t negArc = (uint16_t)(-(int16_t)m_pHostFruit->m_SliceArcAngle);
                        e->m_DirCos = CosIdx(negArc);
                        e->m_DirSin = -SinIdx(negArc);
                        if (slashEntity && slashEntity->m_Col) {
                            ColLine* line = static_cast<ColLine*>(slashEntity->m_Col);
                            e->m_Pos = line->a();
                        }
                        e->m_TimeScale /= WaveManager::GetInstance()->m_ComboSpeedDivisor; // +0x2c
                        e->m_SpinScale *= 0.5f;                                            // +0x28
                    }
                }
            }
        }
    }

    LOG_INFO("SUPERFRUIT", "Sliced() hit %d", m_SliceCount);
}

// Binary @ 0x001baa20. Finale VFX: 10 or 25 radial jibs, 8 lettered fragments,
// white screen flash, SFXPlay gain=2.0.
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
    _Vector3<float> hostPos = host->pos;       // host+0x10

    for (int i = 0; i < N; ++i) {
        uint16_t angIdx = (uint16_t)(rng.Rand32(0xfff0) & 0xffff);
        // T_1607(0.5) = signed-random in [-0.5, +0.5]: RandF(1.0) - 0.5
        float t1607 = Math::g_Random.RandF(1.0f) - 0.5f;
        float spd = (baseSpeed + t1607 * baseSpeed) * ((float)i * 0.2f + 5.0f);  // DAT_001bae50=0.2

        // GetFree() never returns null (v1.6.1 SplatEntity::GetFree @0x001eb318 --
        // flat round-robin pool steals the cursor slot when full).
        SplatEntity* s = SplatEntity::GetFree();
        _Vector3<float> vel(SinIdx(angIdx) * spd, CosIdx(angIdx) * spd, 0.0f);  // DAT_001bae54=0.0
        // ASM-spec v1.6.1 ExplodeSuperFruit @0x001baaec: mute arg is a constant 1
        // (no FruitInfo lookup) -- finale splats always land silent.
        s->MakeSplat(hostPos, vel, false, /*mute=*/true, (long)hostFruitType);

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
    // ASM-spec v1.6.1 SuperFruitControl::ExplodeSuperFruit @0x001babf4:
    //   SFXPlay("pome-burst", atten=0.125, gain=2.0, pitch=0.0).
    if (game_work.mGameSound) {
        game_work.mGameSound->SFXPlay("pome-burst", 0.125f, 2.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>(), 0.0f);
    }

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
        _Vector3<float> corner;
        corner.x = (k & 2u) ? -1.0f : 1.0f;
        corner.y = (k & 1u) ?  1.0f : -1.0f;
        corner.z = (k + 3 < 7) ? 1.0f : -1.0f;
        corner.Normalise();

        // dir = rot.MultVec33(corner) -- multiply upper-left 3x3 of Matrix44 by corner
        _Vector3<float> dir;
        dir.x = rot.m[0] * corner.x + rot.m[4] * corner.y + rot.m[8]  * corner.z;
        dir.y = rot.m[1] * corner.x + rot.m[5] * corner.y + rot.m[9]  * corner.z;
        dir.z = rot.m[2] * corner.x + rot.m[6] * corner.y + rot.m[10] * corner.z;

        // spawn jib actor (entity type 5 = Jiblet)
        Mortar::Entity* jibEnt = am ? am->Add(5) : 0;
        if (!jibEnt) continue;
        Jiblet* jiblet = static_cast<Jiblet*>(jibEnt);

        // dirN: dir with z zeroed, then normalised (planar velocity direction)
        _Vector3<float> dirN = dir;
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
        _Vector3<float> vel = dirN * linSpeed;

        // load model
        Mortar::SmartPtr<Mortar::Model> mdl;
        if (mm) {
            mdl = mm->Load(name);
        }

        // angular velocity: dirN * 700.0  (DAT_001bae64=700.0f)
        _Vector3<float> angVel = dirN * 700.0f;

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
    host->scale = _Vector3<float>(0.0f, 0.0f, 0.0f);
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

// ASM-spec v1.6.1 SuperFruitControl::TransitionFin @0x001b9878: zoom-done Delegate0<void> cb.
// Re-arms host fruit's slice timer negative iff the finished transition was a zoom-IN.
void SuperFruitControl::TransitionFin()
{
    if (game_work.m_FruitCamera && game_work.m_FruitCamera->IsTransitionIn()) {
        if (m_pHostFruit) m_pHostFruit->m_SliceTimer = -1.0f;
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
        _Vector3<float> dir = b->pos - m_pHostFruit->pos;       // outward radial
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
        fruit->pos = _Vector3<float>(-35.0f, -260.0f, 0.0f);        // DAT_001bc104/0bc108/0bc10c
        fruit->vel = _Vector3<float>(0.5f, 8.5f, 0.0f);             // 0x3f000000, 0x41080000, DAT_001bc10c
        fruit->m_Gravity.z = -7.5f;                      // [fruit+0xa8] = 0xc0f00000
        fruit->m_Gravity.y = 0.0f;                       // [fruit+0xa4] = DAT_001bc10c
    } else {
        fruit->pos = _Vector3<float>(-340.0f, -100.0f, 0.0f);       // DAT_001bc110/0bc114/0bc10c
        fruit->vel = _Vector3<float>(5.0f, 5.0f, 0.0f);             // 0x40a00000, 0x40a00000, DAT_001bc10c
        fruit->m_Gravity.z = -4.5f;                      // [fruit+0xa8] = 0xc0900000
        fruit->m_Gravity.y = 0.01f;                      // [fruit+0xa4] = DAT_001bc118
    }
    // 51% chance: mirror the arc across the screen centreline.
    if (WaveManager::GetInstance()->GetRandom().Rand32(100) < 51) {  // cmp #0x32 / bhi
        fruit->m_Gravity.y = -fruit->m_Gravity.y;        // [fruit+0xa4]
        fruit->pos.x       = -fruit->pos.x;              // [fruit+0x10]
        fruit->vel.x       = -fruit->vel.x;              // [fruit+0x1c]
    }

    // v1.6.1 SuperFruitThrown @0x001bbf48 has no SFXPlay; the throw loop "pome-lp" is played
    // by SuperFruitGlow's ctor (already ported).

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

    // ASM-spec v1.6.1 SuperFruitControl::LoadContent @0x001bda74
    FruitRay::RayTexture = Mortar::TextureManager::LoadLocalisedTexture("pomegranate_rays.tex");

    // NOTE: v1.6.1 SuperFruitControl::LoadContent @0x001bda74 also loads
    //   SuperFruitGlow::GlowTexture here (LoadLocalisedTexture; the filename arg
    //   is still unresolved). Deferred until SuperFruitGlow::GlowTexture is ported
    //   -- the glow (a separate entity) is what consumes it, not this control.
}

// Frees the finale visuals loaded by LoadContent. Nulls the file-static SmartPtr
// globals (releases their refcount).
void SuperFruitControl::UnLoadContent() {
    ShockWaveTexture = NULL;
    JibletModel = NULL;
    FruitRay::RayTexture = NULL;
}

// Port specific: diagnostic accessor for tests/tooling (not a binary symbol).
bool SuperFruitControl::HasJibletModel() {
    return JibletModel.Get() != NULL;
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
// ASM-spec v1.6.1 SuperFruitControl::CanSpawnFinalPomegranate @0x001b99d4: same
// powerup-progression gate idiom as GlobalProbabilityOveride::CanSpawn @0x00120d2c --
// if no powerup-flagged fruit is currently active, gate on progression >= 2.0.
bool SuperFruitControl::CanSpawnFinalPomegranate()
{
    if (Fruit::NumberOfPowerupFruits() < 1) {
        return PowerUpManager::GetInstance()
            ? PowerUpManager::GetInstance()->GetActiveProgression(0.0f) >= 2.0f
            : false;
    }
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
    WaveManager::GetInstance()->SetAbsoluteDtMod(1.0f);
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
    // ASM-spec v1.6.1 SuperFruitControl::Reset @0x001bb52c: StackAllocatedPointer<Delegate0>::
    //   Delete((game+0x4c)+0x184) -- frees/clears the camera's zoom-done callback (FruitCamera+0x184).
    if (game_work.m_FruitCamera) game_work.m_FruitCamera->m_OnZoomDone = Mortar::Delegate0<void>();
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

    // v1.6.1 @0x001bb52c: binary's __thiscall writes this+0x33=1 (m_bPendingRemoval,
    //   HUDControl::SetPendingRemoval()) on the active controller instance. Port's ResetAll
    //   is static (renamed to dodge the HUDControl3d::Reset() virtual collision; called from
    //   PauseScreen.cpp with no instance in scope) -- needs the active-controller pointer to
    //   reap it. TODO: route the active instance here.
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
    const _Vector3<float>& centre = m_ExplodeOrigin;

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
        f->m_Gravity = _Vector3<float>(0.0f, 0.0f, 0.0f);  // Fruit+0xa0..0xab zero-Vec3 copy
        f->m_bBallisticEnable = 0;      // Fruit+0x70 (strb 0)

        // Only sliced fruits get their two half-bodies redirected.
        if (f->Sliced()) {
            // First body: pos +0x10 -> vel +0x1c.
            _Vector3<float> dir = f->pos - centre;
            dir.Normalise();
            dir *= 5.0f;
            f->vel = (f->vel + dir) / 2.0f;

            // Second body: pos +0xc8 (m_SecondPos region) -> vel +0xd4.
            // DIFFERS: binary reads Fruit+0xc8 and writes Fruit+0xd4; the port's
            // named second-body fields sit at +0xb8/+0xc4, so this redirect uses
            // the same raw +0x10 offset relationship the binary uses (pos->vel).
            _Vector3<float> dir2 = f->m_SecondPos - centre;
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
        _Vector3<float> dir = b->pos - centre;
        dir.Normalise();
        dir *= 5.0f;
        b->vel = (b->vel + dir) / 2.0f;

        // Freeze bomb physics (binary writes at Bomb+0x8c / +0xa8 / +0x80).
        b->m_AccelForce = _Vector3<float>(0.0f, 0.0f, 0.0f);  // Bomb+0x8c zero-Vec3
        b->m_SpeedMult = 0.0f;                      // Bomb+0xa8 = DAT_001ba6a4 (0.0f)
        b->m_bMovement = 0;                         // Bomb+0x80 (strb 0)

        e = am->GetEntityNext(1, fit);
    }
}

// ASM-spec v1.6.1 SuperFruitControl::UpdateExplosion @0x001baeb8. Per-frame
// shockwave: grows the inner/outer radii from T_1616 ramps, writes the particle-
// manager globals, eases the wave dt-mod (SetAbsoluteDtMod @0x001bee08), then
// radially pushes Actor types 0/1/5 outward from the epicenter.
// R = sqrt(384000)*1.2 ~= 743.61.
//
// Body ranges: push scale k = dt*GetWavedt(0) computed once @0x001baff4;
// early-out (Timer > Lifetime+2.55) @0x001bb01c; fruit loop (mult 4.0)
// @0x001bb048-0x001bb36c incl. host freeze @0x001bb358, m_TimeScale restore
// @0x001bb058-74, sliced second-half push @0x001bb098-0x001bb154, SliceTimer
// choreography + first-half push @0x001bb158-0x001bb298, unsliced force-explode
// @0x001bb29c-0x001bb338; bomb loop (mult 5.0, m_SpeedMult restore @0x001bb3c8)
// @0x001bb3c8-0x001bb434; jib loop (mult 5.0, no restore) @0x001bb494-.
// All pushes are gated dist < m_OuterRadius -- (outerR-dist) is never negative.
void SuperFruitControl::UpdateExplosion(float dt)
{
    PSPParticleManager& mgr = PSPParticleManager::GetInstance();

    const float R = Math::Sqrt(384000.0f) * 1.2f;   // ~743.61

    m_LayerFlags |= 0x200;

    // Inner shockwave radius: ramp 0->R across [Lifetime+0.5, Lifetime+0.85].
    m_InnerRadius = T_1616(m_Timer, m_Lifetime + 0.5f, m_Lifetime + 0.85f) * R;

    // Ease the wave dt-mod back from 0.1 toward 1.0.
    WaveManager::GetInstance()->SetAbsoluteDtMod(
        T_1629(0.1f, T_1616(m_Timer, m_Lifetime + 1.25f, m_Lifetime + 1.45f)));

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

    // Push scale, computed ONCE (@0x001baff4) -- same idiom as PushBombsAway.
    const float k = dt * WaveManager::GetInstance()->GetWavedt(0);

    // @0x001bb01c: no entity pushes past Lifetime+2.55.
    if (m_Timer > m_Lifetime + 2.55f) return;

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    const _Vector3<float>& origin = m_ExplodeOrigin;
    std::list<Mortar::Entity*>::iterator it;

    // -------- type 0: fruits (push mult 4.0) --------
    Mortar::Entity* e = am->GetEntityFirst(0, it);
    while (e != NULL) {
        Fruit* f = static_cast<Fruit*>(e);
        if (f == m_pHostFruit) {
            // @0x001bb358-6c: host fruit special-case -- freeze in place.
            f->m_SliceTimer = 0.5f;
            f->vel = _Vector3<float>(0.0f, 0.0f, 0.0f);
            f->m_SecondVel = _Vector3<float>(0.0f, 0.0f, 0.0f);
        } else {
            // @0x001bb058-74: restore the StopAllFruit physics freeze once the
            // shockwave is live.
            if (m_InnerRadius > 0.0f) f->m_TimeScale = 1.0f;

            _Vector3<float> dir = f->pos - origin;
            float dist = dir.Normalise();

            if (f->Sliced()) {
                // Second half FIRST (@0x001bb098-0x001bb154).
                _Vector3<float> dir2 = f->m_SecondPos - origin;
                float dist2 = dir2.Normalise();
                if (dist2 < m_OuterRadius && m_Timer > m_Lifetime + 1.1f) {
                    float a = m_OuterRadius - dist2;
                    if (a < 0.0f) a = 0.0f;   // @0x001bb10c (unreachable, kept for parity)
                    f->m_SecondVel += dir2 * (a * k * 4.0f);
                }
                // SliceTimer choreography + first half (@0x001bb158).
                if (dist < m_OuterRadius) {
                    if (f->m_SliceTimer > 1.0e-4f) f->m_SliceTimer = 1.0e-4f;  // @0x001bb170
                    if (m_Timer > m_Lifetime + 1.1f) {                         // @0x001bb19c
                        float b = m_OuterRadius - dist;
                        if (b < 0.0f) b = 0.0f;                                // @0x001bb1c8
                        f->vel += dir * (b * k * 4.0f);
                    }
                } else {                                                       // @0x001bb218
                    if (f->m_SliceTimer > 0.0f && m_Timer < m_Lifetime + 2.05f)
                        f->m_SliceTimer = 0.5f;
                }
            } else {
                // Force-explode, unsliced only (@0x001bb29c); unsliced fruit
                // get NO radial push at all.
                if (f->IsActive()                          // @0x001bb2a0
                    && dist < m_InnerRadius                // @0x001bb2b0
                    && m_InnerRadius < R                   // @0x001bb2bc
                    && m_Timer < m_Lifetime + 1.1f) {      // @0x001bb2e8
                    dir *= 2.0f;                           // @0x001bb2f8
                    f->CollisionResponse(NULL, 0, 0, &dir);  // @0x001bb30c
                    AddToCurrentScore(1, 0, true, true);     // @0x001bb324
                    f->m_SliceTimer = 1.0e-5f;               // @0x001bb338
                }
            }
        }
        e = am->GetEntityNext(0, it);
    }

    // -------- type 1: bombs (push mult 5.0) --------
    e = am->GetEntityFirst(1, it);
    while (e != NULL) {
        Bomb* bomb = static_cast<Bomb*>(e);
        _Vector3<float> dir = bomb->pos - origin;
        float dist = dir.Normalise();
        // @0x001bb3c8: restore the StopAllFruit physics freeze.
        if (m_InnerRadius > 0.0f) bomb->m_SpeedMult = 1.0f;
        if (dist < m_OuterRadius)                          // @0x001bb3d4
            bomb->vel += dir * ((m_OuterRadius - dist) * k * 5.0f);
        e = am->GetEntityNext(1, it);
    }

    // -------- type 5: jibs (push mult 5.0, no restore) --------
    e = am->GetEntityFirst(5, it);
    while (e != NULL) {
        _Vector3<float> dir = e->pos - origin;
        float dist = dir.Normalise();
        if (dist < m_OuterRadius)                          // @0x001bb494
            e->vel += dir * ((m_OuterRadius - dist) * k * 5.0f);
        e = am->GetEntityNext(5, it);
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
        // ASM-spec v1.6.1 SuperFruitControl::StopRays @0x001b9b4c: +0xE0 on the
        // type-6 entity is FruitRay::m_Expiring; setting it flips the ray from
        // host-tracking to its Update fade-out branch.
        static_cast<FruitRay*>(e)->m_Expiring = 1;
        e = am->GetEntityNext(6, it);
    }

    // ASM-spec v1.6.1 SuperFruitControl::StopRays @0x001b9b4c: after the ray loop,
    // zero the host fruit's two per-half spin rates.
    if (m_pHostFruit) {
        m_pHostFruit->m_RotVel1 = _Vector3<float>(0.0f, 0.0f, 0.0f);   // Fruit+0x100
        m_pHostFruit->m_RotVel2 = _Vector3<float>(0.0f, 0.0f, 0.0f);   // Fruit+0x10c
    }
}

// ASM-verified: 2026-07-24T00:00Z v1.6.1 SuperFruitControl::SpawnRay @0x001ba810 (asm-inspector)
// (Z/Y/X axis assignment, deg*182 brad conversion, qz*qy*qx order all MATCH). Spawns one
// type-6 FruitRay entity, oriented by a pseudo-random quaternion built from
// three axis-aligned rotations: the file-static `rayNum` counter cycles the
// elevation band (8-way, low/high split at rayNum&7 < 4) and the quadrant
// (rayNum&3) that seeds the heading sweep.
void SuperFruitControl::SpawnRay()
{
    static int rayNum = 0;
    rayNum++;
    int i = rayNum & 7;

    float yLo = 5.0f, yHi = 70.0f;
    if (i < 4) { yLo = -35.0f; yHi = -5.0f; }

    int q = i & 3;
    float zDeg = GetRandBetween(0.0f, 180.0f, 0.0f, 0.0f);
    float yDeg = GetRandBetween(yLo, yHi, 0.0f, 0.0f);
    float xDeg = GetRandBetween((float)(q * 90), (float)(q * 90 + 80), 0.0f, 0.0f);

    Quaternion qz; qz.CreateFromAxisAngle(0.0f, 0.0f, 1.0f, (uint16_t)(int)(zDeg * 182.0f));
    Quaternion qy; qy.CreateFromAxisAngle(0.0f, 1.0f, 0.0f, (uint16_t)(int)(yDeg * 182.0f));
    Quaternion qx; qx.CreateFromAxisAngle(1.0f, 0.0f, 0.0f, (uint16_t)(int)(xDeg * 182.0f));
    Quaternion rot = qz * qy * qx;

    Mortar::ActorManager* am = Mortar::ActorManager::GetInstance();
    FruitRay* r = am ? static_cast<FruitRay*>(am->Add(6, true)) : 0;
    if (r) {
        r->Init(m_pHostFruit, rot);
    }
}

// ASM-spec v1.6.1 SuperFruitControl::SpawnJibs @0x001bc748. PSPParticleManager
// emitter hookup for jib particle trails: emitter name = "<fruitModel>_explode",
// positioned at the explosion origin with the host fruit's slice-arc direction.
// Then spawns 8 JibletModel mesh actors in a radial fan, unconditionally --
// same as the 8-fragment loop in ExplodeSuperFruit above, the binary never
// null-checks JibletModel here (LoadContent always loads it at boot). A null
// model is safe regardless: Jiblet::Draw gates on `if (!m_pModel) return;`.
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
        // Unconditional -- the binary does not null-check JibletModel here.
        {
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
                _Vector3<float> dir(CosIdx(a16), SinIdx(a16), 0.0f);
                j->Init((int)m_pHostFruit->m_FruitType, m_ExplodeOrigin,
                        SuperFruitUniform(0.8f, 1.25f),
                        dir * SuperFruitUniform(500.0f, 900.0f),
                        JibletModel, jh, dripRate, dir * 45.0f);
            }
        }
    }
}
