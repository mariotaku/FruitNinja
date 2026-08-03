//
// SuperFruitGlow : HUDControl3d — glow halo for the super-fruit (pomegranate/starfruit).
// Binary: ctor @ 0x001c06bc, dtor @ 0x1c02b4/0x1c0258
//         Release @ 0x1c01c8, DrawOrder @ 0x1bfb18,
//         Update @ 0x1c0024, SetToMultiplayerState @ 0x1bffb8.
//

#include "SuperFruitGlow.h"
#include "Fruit.h"
#include "game/GameWork.h"
#include "engine/audio/GameSound.h"
#include "engine/audio/MortarSound.h"
#include <cstring>

// DAT_001c01a4 = 60.0f — spin rate multiplier (m_Timer += dt*60)
static const float SFG_SPIN_RATE = 60.0f;

// DAT_001c0858 = 150.0f — base scale factor applied to size Vec3
static const float SFG_BASE_SCALE_FACTOR = 150.0f;

// DAT_001c01ac = 75.0f — alpha scale: byte alpha = trunc(75.0 * m_Fade)
static const float SFG_ALPHA_SCALE = 75.0f;

// DAT_001c01a8 = 40.0f — z-correction: pos.z = fruit(+0x9c) - 40.0
static const float SFG_Z_CORRECTION = 40.0f;

// DAT_001c01b0 = 0.0f — sound volume when game is paused
static const float SFG_PAUSED_VOLUME = 0.0f;

// fade rate = +-2 per second (binary: +=2*dt in / -=2*dt out)
static const float SFG_FADE_RATE = 2.0f;

// ctor @ 0x001c06bc
// Calls HUDControl3d::HUDControl3d; subscribes to fruit-killed event;
// plays looping SFX; stores fruit pointer.
SuperFruitGlow::SuperFruitGlow(Fruit* fruit)
    : HUDControl3d()
    , m_bSliced(0)
    , m_pFruit(fruit)
    , m_pSound(0)
    , m_Fade(0.0f)
{
    // +0x30: HUDControl m_LayerFlags — ctor sets per spec (0x80)
    m_LayerFlags = 0x80;

    // Subscribe to m_pFruit->m_OnKilled (+0x178) — binary @ 0x1c06bc:
    //   add r7,r7,#0x178; bl Event1::operator+= (0x112508).
    if (m_pFruit) {
        m_pFruit->m_OnKilled += Mortar::Delegate1<void, Fruit*>::Make(
            this, &SuperFruitGlow::OnFruitKilled);
    }

    // ctor @ 0x1c06bc: GameSound::SFXPlay("pome-lp", 1.0, 0.0, finishCb) -> m_pSound.
    // Binary: vol  s0 = 0x3f800000 = 1.0f
    //         pitch s1 = DAT_001c0854 = 0.0f
    //         name = "pome-lp" (string @ binary, the pomegranate loop SFX)
    //         GameSound* = global game-work mGameSound (binary: *(GOT_global + 0x18c))
    // The binary builds a static Delegate1<bool,MortarSound*>::Global finish callback
    // (target @ iVar4+DAT_001c0870); the port follows the established convention
    // (Fruit.cpp:1175) of passing an empty delegate for the looping-SFX finish hook.
    // The handle is stored (asm: str r0,[r4,#0x84]); port keeps it in m_pSound (+0x84).
    // v1.6.1 SuperFruitGlow::SuperFruitGlow @0x001c06bc: SFXPlay unguarded.
    m_pSound = game_work.mGameSound->SFXPlay(
        "pome-lp", 1.0f, 0.0f,
        Mortar::Delegate1<bool, Mortar::MortarSound*>());
}

// dtor @ 0x1c02b4
SuperFruitGlow::~SuperFruitGlow() {
    m_pFruit = 0;
    m_pSound = 0;
}

// slot3: Release @ 0x1c01c8
// Binary: T_1621(); if(m_pFruit) m_pFruit->m_OnKilled -= delegate (unsubscribe @ 0x1c021c).
void SuperFruitGlow::Release() {
    // Unsubscribe from m_pFruit->m_OnKilled (+0x178) — binary @ 0x1c01dc ldr m_pFruit,
    //   guarded if != 0, then add r0,r5,#0x178; bl Event1::operator-= (0x10ad84).
    if (m_pFruit) {
        m_pFruit->m_OnKilled -= Mortar::Delegate1<void, Fruit*>::Make(
            this, &SuperFruitGlow::OnFruitKilled);
    }
    m_pFruit = 0;
    HUDControl3d::Release();
}

// slot9: DrawOrder @ 0x1bfb18
// Double-draw: scales inherited size Vec3 (+0x20) by m_Fade, draws twice with
// m_Timer (+0x2c) negated between passes, then restores both.
void SuperFruitGlow::DrawOrder(float* hudScaleRaw, int layerMask) {
    (void)layerMask;
    // ASM @ 0x1bfb18: two-pass mirror using m_Fade (+0x88) as scale multiplier:
    //   1. save size (+0x20 Vec3)
    //   2. size *= m_Fade (+0x88)           -> _Vector3<float>::operator*=
    //   3. Draw(hudScaleRaw)               -> HUDControl3d::Draw (1st blade)
    //   4. m_Timer = -m_Timer (+0x2c)       -> mirror rotation for 2nd blade
    //   5. Draw(hudScaleRaw)               -> HUDControl3d::Draw (2nd blade)
    //   6. m_Timer = -m_Timer               -> restore +0x2c
    //   7. size = saved                     -> restore +0x20 Vec3
    _Vector3<float> savedSize = size;
    size *= m_Fade;
    HUDControl3d::Draw(hudScaleRaw);
    m_Timer = -m_Timer;
    HUDControl3d::Draw(hudScaleRaw);
    m_Timer = -m_Timer;
    size = savedSize;
}

// slot10: Update @ 0x1c0024
// spin += dt*60; if tracked Fruit sliced -> set m_bSliced; fade in (2*dt->1) or
// out (-2*dt->0, release sound, set m_Dead +0x33); copy Fruit pos->+0x08;
// z = fruit(+0x9c) - 40; colour alpha = trunc(75*m_Fade); set sound volume from m_Fade.
void SuperFruitGlow::Update(float dt) {
    bool paused = game_work.bM_Mode;

    if (!paused) {
        // Spin advance (m_Timer is the spin accumulator inherited from HUDControl)
        m_Timer += dt * SFG_SPIN_RATE;

        // Detect host fruit sliced -> enter fade-out path
        if (m_pFruit && m_pFruit->Sliced()) {
            m_bSliced = 1;
        }

        if (m_bSliced == 0) {
            // Fade in: += 2*dt, clamp to 1.0
            float v = m_Fade + dt + dt;
            if (v >= 1.0f) v = 1.0f;
            m_Fade = v;
        } else {
            // Fade out: -= 2*dt
            float v = m_Fade + dt * -SFG_FADE_RATE;
            m_Fade = v;
            if (v <= 0.0f) {
                // Release the looping sound handle
                // v1.6.1 SuperFruitGlow::Update @0x001c0024: the only test is
                // `m_pSound != 0`; GameSound::Release takes game_work.mGameSound
                // (+0x18c) straight off the GOT.
                if (m_pSound) {
                    game_work.mGameSound->Release(m_pSound, "pome-lp");
                    // TODO: v1.6.1 0x001c0024 (SuperFruitGlow::Update) -- pass exact
                    // DAT_001c01bc string arg to GameSound::Release
                }
                m_pSound = 0;
                // Mark for HUD removal (+0x33 = m_bPendingRemoval = 1)
                m_bPendingRemoval = 1;
            }
        }
    }

    // Track fruit position (+0x08 = HUDControl::pos)
    if (m_pFruit) {
        pos = m_pFruit->pos;
        // z = fruit(+0x9c) - 40.0f  (DAT_001c01a8; fruit +0x9c = m_ZPosition)
        pos.z = m_pFruit->m_ZPosition - SFG_Z_CORRECTION;
    }

    // Colour: RGB forced white, alpha = trunc(75 * m_Fade) gated to 0 when non-positive.
    // Binary @ 0x1c0024: fVar = 75.0f * +0x88 (m_Fade);
    //   Colour(0xff,0xff,0xff, (0.0 < fVar) * (char)(int)fVar); m_DrawColour = that.
    {
        float fAlpha = SFG_ALPHA_SCALE * m_Fade;
        uint8_t a = (fAlpha > 0.0f) ? (uint8_t)(int)fAlpha : (uint8_t)0;
        m_DrawColour = Colour(0xFF, 0xFF, 0xFF, a);
    }

    // Sound volume: m_Fade when running, 0.0 when paused (DAT_001c01b0)
    if (m_pSound) {
        float vol = paused ? SFG_PAUSED_VOLUME : m_Fade;
        m_pSound->SetVolume(vol);
    }

    HUDControl3d::Update(dt);
}

// slot11: SetToMultiplayerState @ 0x1bffb8
// Binary: call vtbl slot3 (Release) then HUDControl::SetToMultiplayerState.
bool SuperFruitGlow::SetToMultiplayerState() {
    Release();
    return HUDControl::SetToMultiplayerState();
}

// Called when m_pFruit fires m_OnKilled. Clears the fruit pointer so
// Update and Release no longer reference the dead entity.
void SuperFruitGlow::OnFruitKilled(Fruit* /*fruit*/) {
    m_pFruit = 0;
}
