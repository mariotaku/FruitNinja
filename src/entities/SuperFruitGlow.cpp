//
// SuperFruitGlow : HUDControl3d — glow halo for the super-fruit (pomegranate/starfruit).
// Binary: ctor @ 0x001c06bc, dtor @ 0x1c02b4/0x1c0258
//         Release @ 0x1c01c8, DrawOrder @ 0x1bfb18,
//         Update @ 0x1c0024, SetToMultiplayerState @ 0x1bffb8.
//

#include "SuperFruitGlow.h"
#include "Fruit.h"
#include <cstring>

// Binary constants from spec (v1.6.1)

// DAT_001c01a4 — spin rate multiplier
static const float SFG_SPIN_RATE = 0.0f;        // TODO: 0x1c0024 — resolve DAT_001c01a4 spin rate

// DAT_001c0858 — base scale factor
static const float SFG_BASE_SCALE_FACTOR = 1.0f; // TODO: 0x1c06bc — resolve DAT_001c0858 base scale

// DAT_001c0854 — m_FadeAlt initial value
static const float SFG_FADE_ALT_INIT = 1.0f;     // TODO: 0x1c06bc — resolve DAT_001c0854 fadeAlt init

// DAT_001c01ac — alpha scale factor: colour.alpha = DAT * m_FadeAlt
static const float SFG_ALPHA_SCALE = 1.0f;        // TODO: 0x1c0024 — resolve DAT_001c01ac

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

    // TODO: 0x1c06bc — play looping SFX via GameSound::SFXPlay(1.0, ...) -> m_pSound handle
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
// Double-draw: renders glow twice with spin mirrored (pos scaled by m_FadeAlt),
// HUDControl3d::Draw called with +0x28 (m_Spin) and +0x2c (m_SpinDraw = -m_Spin).
// TODO: 0x1bfb18 — full double-draw spin mirror not yet ported (requires m_Spin field
//   alias resolution and two-pass HUDControl3d::Draw calls with mirrored timer)
void SuperFruitGlow::DrawOrder(const Vec3& hudScale, int layerMask) {
    // TODO: 0x1bfb18 — scale m_Pos by m_FadeAlt(+0x88), call HUDControl3d::Draw twice
    //   (with pos.m_Timer = m_Spin and pos.m_Timer = m_SpinDraw = -m_Spin for two-blade glow)
    HUDControl3d::DrawOrder(hudScale, layerMask);
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

    // Colour alpha = DAT_001c01ac * m_FadeAlt
    // TODO: 0x1c0024 — set m_DrawColour.a = (uint8_t)(SFG_ALPHA_SCALE * m_FadeAlt * 255)

    // TODO: 0x1c0024 — set sound volume from m_FadeAlt (or fixed when paused)

    HUDControl3d::Update(dt);
}

// slot11: SetToMultiplayerState @ 0x1bffb8
// Binary: call vtbl slot3 (Release) then HUDControl::SetToMultiplayerState.
bool SuperFruitGlow::SetToMultiplayerState() {
    Release();
    return HUDControl::SetToMultiplayerState();
}
