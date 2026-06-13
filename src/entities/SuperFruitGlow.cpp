//
// SuperFruitGlow : HUDControl3d — glow halo for the super-fruit (pomegranate/starfruit).
// Binary: ctor @ 0x001c06bc, dtor @ 0x1c02b4/0x1c0258
//         Release @ 0x1c01c8, DrawOrder @ 0x1bfb18,
//         Update @ 0x1c0024, SetToMultiplayerState @ 0x1bffb8.
//

#include "SuperFruitGlow.h"
#include "Fruit.h"
#include "game/GameWork.h"
#include "audio/GameSound.h"
#include "audio/MortarSound.h"
#include <cstring>

// Binary constants from spec (v1.6.1)

// DAT_001c01a4 — spin rate multiplier
static const float SFG_SPIN_RATE = 0.0f;        // TODO: 0x1c0024 — resolve DAT_001c01a4 spin rate

// DAT_001c0858 — base scale factor
static const float SFG_BASE_SCALE_FACTOR = 1.0f; // TODO: 0x1c06bc — resolve DAT_001c0858 base scale

// DAT_001c0854 — m_FadeAlt initial value
static const float SFG_FADE_ALT_INIT = 1.0f;     // TODO: 0x1c06bc — resolve DAT_001c0854 fadeAlt init

// DAT_001c01ac = 75.0f (IEEE 0x42960000) — alpha scale: byte alpha = trunc(75.0 * m_FadeAlt)
static const float SFG_ALPHA_SCALE = 75.0f;

// fade rate = +-2 per second
static const float SFG_FADE_RATE = 2.0f;         // binary: +=2*dt (in) / -=2*dt (out)

// ctor @ 0x001c06bc
// Calls HUDControl3d::HUDControl3d; subscribes to fruit-slice event;
// plays looping SFX; stores fruit pointer.
SuperFruitGlow::SuperFruitGlow(Fruit* fruit)
    : HUDControl3d()
    , m_pFruit(fruit)
    , m_pSound(0)
    , m_Fade(0.0f)
    , m_FadeAlt(SFG_FADE_ALT_INIT)
{
    // +0x30: HUDControl m_LayerFlags — ctor sets per spec (0x80)
    m_LayerFlags = 0x80;

    // TODO: 0x1c06bc — subscribe Delegate1<void,Fruit*>::Make(this,
    //   &SuperFruitGlow::OnFruitSliced) to the owning Event1<Fruit*>.
    //   The event owner (Fruit or a global per-fruit signal) is not yet ported;
    //   Fruit::sizeof==0x118 and SuperFruitControl::sizeof==0x108 are both
    //   layout-asserted. The event owner must be RE'd to identify the correct
    //   struct and offset before this can be wired.
    //   Binary: SuperFruitGlow ctor @ 0x1c06bc builds delegate on stack (local_24)
    //   and calls Event1<Fruit*>::operator+=.

    // ctor @ 0x1c06bc: GameSound::SFXPlay("pome-lp", 1.0, 0.0, finishCb) -> m_pSound.
    // Binary: vol  s0 = 0x3f800000 = 1.0f
    //         pitch s1 = DAT_001c0854 = 0.0f
    //         name = "pome-lp" (string @ binary, the pomegranate loop SFX)
    //         GameSound* = global game-work mGameSound (binary: *(GOT_global + 0x18c))
    // The binary builds a static Delegate1<bool,MortarSound*>::Global finish callback
    // (target @ iVar4+DAT_001c0870); the port follows the established convention
    // (Fruit.cpp:1175) of passing an empty delegate for the looping-SFX finish hook.
    // The handle is stored (asm: str r0,[r4,#0x84]); port keeps it in m_pSound.
    if (game_work.mGameSound) {
        m_pSound = game_work.mGameSound->SFXPlay(
            "pome-lp", 1.0f, 0.0f,
            Mortar::Delegate1<bool, Mortar::MortarSound*>());
    }
}

// dtor @ 0x1c02b4
SuperFruitGlow::~SuperFruitGlow() {
    m_pFruit = 0;
    m_pSound = 0;
}

// slot3: Release @ 0x1c01c8
// Binary: T_1621(); if(m_pFruit) Event1<Fruit*> -= delegate (unsubscribe).
void SuperFruitGlow::Release() {
    // TODO: 0x1c01c8 — unsubscribe Delegate1<void,Fruit*>::Make(this,
    //   &SuperFruitGlow::OnFruitSliced) from the owning Event1<Fruit*>.
    //   Same event owner as ctor subscribe TODO above (not yet ported).
    m_pFruit = 0;
    HUDControl3d::Release();
}

// slot9: DrawOrder @ 0x1bfb18
// Double-draw: scales inherited size Vec3 (+0x20) by m_FadeAlt, draws twice with
// m_Timer (+0x2c) negated between passes, then restores both.
void SuperFruitGlow::DrawOrder(const Vec3& hudScale, int layerMask) {
    // ASM @ 0x1bfb18: the "spin" the prior TODO referenced is NOT a separate
    // m_Spin field — the binary operates on the inherited HUDControl::size
    // Vec3 (+0x20) and HUDControl::m_Timer (+0x2c). Two-pass mirror:
    //   1. save size (+0x20 Vec3)
    //   2. size *= m_FadeAlt (+0x88)        -> _Vector3<float>::operator*=
    //   3. Draw(hudScale, layerMask)        -> HUDControl3d::Draw (1st blade)
    //   4. m_Timer = -m_Timer (+0x2c)       -> mirror rotation for 2nd blade
    //   5. Draw(hudScale, layerMask)        -> HUDControl3d::Draw (2nd blade)
    //   6. m_Timer = -m_Timer               -> restore +0x2c
    //   7. size = saved                     -> restore +0x20 Vec3
    Vec3 savedSize = size;
    size *= m_FadeAlt;
    HUDControl3d::Draw(hudScale, layerMask);
    m_Timer = -m_Timer;
    HUDControl3d::Draw(hudScale, layerMask);
    m_Timer = -m_Timer;
    size = savedSize;
}

// slot10: Update @ 0x1c0024
// spin += dt*k; if tracked Fruit sliced -> set m_Sliced; fade in (2*dt->1) or
// out (-2*dt->0, release sound, set m_Dead +0x33); copy Fruit pos->+0x08;
// colour alpha = k*m_FadeAlt; set sound volume from m_FadeAlt.
void SuperFruitGlow::Update(float dt) {
    // Spin advance (m_Timer is the spin accumulator inherited from HUDControl)
    m_Timer += dt * SFG_SPIN_RATE;

    // Fade in / out
    if (!m_bPendingRemoval) {
        // Fade in: += 2*dt, clamp to 1.0
        m_Fade += SFG_FADE_RATE * dt;
        if (m_Fade > 1.0f) m_Fade = 1.0f;
    } else {
        // Fade out: -= 2*dt, clamp to 0; when done release sound and mark dead
        m_Fade -= SFG_FADE_RATE * dt;
        if (m_Fade < 0.0f) {
            m_Fade = 0.0f;
            // TODO: 0x1c0024 — release m_pSound (MortarSound::Release handle)
            m_pSound = 0;
            // Mark for HUD removal (m_Dead = 1 at +0x33 per spec = m_bPendingRemoval)
            // Note: already set; this branch doubles as the "dead" state
        }
    }

    // Track fruit position (+0x08 = HUDControl::pos)
    if (m_pFruit) {
        pos = m_pFruit->pos;
        // z = Fruit(+0x9c) - DAT (per spec)
        // TODO: 0x1c0024 — resolve Fruit field at +0x9c and DAT offset for z correction
    }

    // Colour: RGB forced white, alpha = trunc(DAT_001c01ac * m_FadeAlt) gated to 0 when non-positive.
    // Binary @ 0x1c0024: fVar = 75.0f * m_FadeAlt;
    //   Colour(0xff,0xff,0xff, (0.0 < fVar) * (char)(int)fVar); m_DrawColour = that.
    // Note: NOT a *255 scale -- DAT_001c01ac (75.0) already encodes the byte range, and the
    // float is truncated toward zero (int cast), then masked to a byte. RGB are overwritten
    // to white each frame (the existing colour is NOT preserved).
    {
        float fAlpha = SFG_ALPHA_SCALE * m_FadeAlt; // 75.0f * m_FadeAlt
        uint8_t a = (fAlpha > 0.0f) ? (uint8_t)(int)fAlpha : (uint8_t)0;
        // Colour(r,g,b,a)
        m_DrawColour = Colour(0xFF, 0xFF, 0xFF, a);
    }

    // TODO: 0x1c0024 — set sound volume from m_FadeAlt (or fixed when paused)

    HUDControl3d::Update(dt);
}

// slot11: SetToMultiplayerState @ 0x1bffb8
// Binary: call vtbl slot3 (Release) then HUDControl::SetToMultiplayerState.
bool SuperFruitGlow::SetToMultiplayerState() {
    Release();
    return HUDControl::SetToMultiplayerState();
}
